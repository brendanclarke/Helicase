# S073: DSP CPU reduction — code implementation schedule

- **Plan and decisions:** `S073_CPU_USE_DSP_REDUCTION_REFACTOR.md`. Its §0
  holds the governing rules, which this document implements exactly:
  - **constant CPU:** no skip, bypass or early-out keyed on a control value or
    on silence;
  - **the sound class required for each step;**
  - **no new tools;**
  - **no profiler;**
  - **no RAM added.**
- **Evidence and cost model:** `knowledge_files/specification_reference/CPU_USE_DSP_AUDIT.md`
  (Session 073 section).
- **Line numbers:** from the S073 working tree on 2026-09-28. HEAD is
  `05bbd83` plus the uncommitted S073 flash-expansion and Samples work;
  every DSP file below is unchanged from `05bbd83`.
  - Numbers are given against that tree **before any step is applied**.
    Each step shifts lines in the files it edits, so re-locate every site by
    the quoted text before editing.
- **Notation:** **ADD** (new lines), **MODIFY** (replace the cited lines),
  **REMOVE** (delete the cited lines). "After L" means insert after line L.
- **Comment blocks:** every change carries an in-place block in this form,
  which the implementation copies into the source verbatim:

  ```
  /*
   * <Title> (S073 Step N).
   *
   * What:       what the code does.
   * Why:        why it must exist (the plan rule it serves).
   * Inputs:     parameters, state and globals it reads.
   * Outputs:    return value, buffers and state it writes.
   * Accessors:  who calls it.
   * Affiliates: code that must stay consistent with it.
   */
  ```

---

## 0. Schedule

| Order | Step | Files touched | Required result | Gate (§12) |
|---|---|---|---|---|
| 1 | 0. Golden harness + ARM codegen check | `tools/dsp_golden/` (new) | old == old | G0 |
| 2 | 1. ZDF filter: batched divisions | `ResonantFilter.c/.h` | S1 | G1 |
| 3 | 2. Descriptor writers without strings | `InstrumentManager.c/.h`, 4 × `*Parameters.c`, `EffectParamRows.h`, `main.c` (diagnostic only) | S0 | G2 |
| 4 | 3a. DMA pack: word stores | `AudioCodecManager.c` | S0 | G3a |
| 5 | 3b. DMA region: Normal non-cacheable | `clocks.c`, `AudioCodecManager.c`, `STM32F765VIHx_FLASH.ld` (comment) | S0 | G3b |
| 6 | 4. Fused voice post-chain | `BufferTools.c/.h`, `distortion.c/.h`, `voicePostChain.h` (new), 4 voice `.c` | S0 | G4 |
| 7 | 6. Octave selection without `log2f()` | `Oscillator.c` | "good enough" (host report) | G6 |
| 8 | 5. Mixer dry + send in one pass | `mixer.c` | S0 (D-5 decided: implement) | G5 |

## Implementation progress notes (2026-09-28)

The implementation is now landed in the working tree through Steps 1–6, with
Step 6 intentionally executed before Step 5 as scheduled. These notes record
the gates as they were run; the user still owns the hardware listening,
underrun, DMA-ordering, and control-regression checks.

- Step 0: `tools/dsp_golden/snapshot.sh` froze 14 source files before DSP
  edits. The full frozen filter grid reports 276,480,000 int16 and
  276,480,000 float samples with zero differences in the old-vs-old self-test.
- Step 1: the full S1 grid reports 276,480,000 samples per variant; the
  batched solver passes at 91.6896 dB int16 / 86.5468 dB float, with 9/10
  configurations over 16 LSB, all in the cutoff 0.8 / resonance 0.98
  self-oscillating family. The standalone ARM object report is `vdiv` 81 → 39.
- Step 2: compile-time special tags pass `155 rows, 0 mismatches` against the
  old string classifier. The diagnostic row now displays the tag self-check.
- Step 3a/3b: the host DMA pack comparison reports `0` differing bytes. The
  DMA arrays are word-aligned, the packer ends with `DSB`, and MPU region 1 is
  documented/configured as Normal non-cacheable.
- Step 4: the fused Drum/Snare/Cymbal/HiHat post-chain comparison reports
  `0` differing samples. The ARM checks report MATCH for the three covered
  loop shapes; the distortion path remains constant-cost.
- Step 6: the exhaustive octave comparison reports 16 mismatches, all within
  2 ulps and a maximum `0.000403883111` cents of an octave edge.
- Step 5: the mixer comparison reports `0` differing samples. The ARM value
  operation multisets MATCH after subtracting the two input conversions that
  the fused loop shares. *Corrected 2026-09-29:* the pair first reported
  here as "DAC1-stereo" (55 old versus 44 combined) is a single-output
  routing with a mono-input send. `armcheck-mixer` now gates all four
  dry × send combinations plus the default case. The counts are single
  output 55 → 44 / 69 → 59 and stereo 73 → 63 / 87 → 78 (mono / stereo
  send). See the audit, item 21.
- Firmware link after all source changes: `text=486688`, `data=416`,
  `bss=426336`, flash `487104 / 753664 B`, headroom `266560 B`. No `data`
  or `bss` was added by S073. *Corrected 2026-09-29:* ITCM grew by 400 B,
  from 3,768 to 4,168 / 16,384 B: `osc_setFreq()` is now linked as its own
  ITCM function (audit item 22).

**Unchanged by design:**
- the hardware RNG noise (Step 8 rejected);
- no idle-voice gating (Step 7 rejected);
- the existing "only when changed/active" paths the user kept (plan §3.1): the
  FX bus when `off`, the zero-send skip, the Effect coefficient recompute,
  and the oscillator frequency cache in `osc_setFreq()` (`Oscillator.c:918`,
  same category, found during this dive).

---

## 1. Step 0 — Golden harness and ARM codegen check (all ADD)

New directory `tools/dsp_golden/`. It is host-only: no firmware file changes
and no target RAM. Build products go in `tools/dsp_golden/build/`, which is
covered by the existing `*.o` and `*.bin` ignore rules, plus one new rule
(§1.8).

### 1.1 ADD `tools/dsp_golden/snapshot.sh`

```sh
#!/bin/sh
# Freeze the pre-change DSP sources for the S073 golden harness (Step 0).
#
# What:       copies every source file the CPU steps will edit into
#             tools/dsp_golden/frozen/, preserving the relative path.
# Why:        each step is judged old-against-new. The "old" side must be the
#             exact pre-change text, independent of commits (the user owns
#             commits) and of later edits.
# Inputs:     the working tree before Step 1. Run once, from the repository
#             root; refuses to overwrite an existing snapshot.
# Outputs:    tools/dsp_golden/frozen/<same relative paths>.
# Accessors:  the user or implementer, once, at Step 0.
# Affiliates: extract.py (reads frozen/), Makefile targets.
set -eu
cd "$(dirname "$0")/../.."
DEST=tools/dsp_golden/frozen
[ -e "$DEST" ] && { echo "snapshot exists: $DEST (delete it deliberately to refreeze)"; exit 1; }
for f in \
  Core/DSPAudio/ResonantFilter.c Core/DSPAudio/ResonantFilter.h \
  Core/DSPAudio/BufferTools.c Core/DSPAudio/BufferTools.h \
  Core/DSPAudio/distortion.c Core/DSPAudio/distortion.h \
  Core/DSPAudio/Oscillator.c Core/DSPAudio/mixer.c Core/DSPAudio/squareRootLut.c \
  Core/DSP/Instruments/Drum/DrumVoice.c Core/DSP/Instruments/Snare/Snare.c \
  Core/DSP/Instruments/Cymbal/CymbalVoice.c Core/DSP/Instruments/HiHat/HiHat.c \
  Core/Hardware/AudioCodecManager.c
do
  mkdir -p "$DEST/$(dirname "$f")"
  cp "$f" "$DEST/$f"
done
echo "frozen $(find "$DEST" -type f | wc -l | tr -d ' ') files into $DEST"
```

### 1.2 ADD `tools/dsp_golden/extract.py`

```python
#!/usr/bin/env python3
"""
Extract C functions, objects or line ranges for the S073 golden harness.

What:       copies named function definitions (`--func`), initialised objects
            such as const tables (`--object`), or an inclusive line range
            (`--lines A-B`, emitted first) out of one C source or header.
            Optionally renames
            identifiers on word boundaries (`--rename old=new`), so that
            frozen and current copies can coexist in one translation unit.
Why:        the DSP sources cannot be host-compiled whole (their include
            trees reach CMSIS, the MPU and the Scene model). Extracting just
            the functions under test keeps the harness honest: it compiles
            the exact source text of the old and new code, never a
            transcription.
Inputs:     --src FILE, one or more selectors, renames, --out FILE.
Outputs:    one C fragment containing the selections in the order requested;
            exit status 1 if any selection is missing.
Accessors:  tools/dsp_golden/Makefile.
Affiliates: prelude.h (supplies the types and macros the fragments need),
            snapshot.sh (the frozen side).
"""
import argparse
import re
import sys


def _match_block(text, open_idx):
    depth = 0
    for k in range(open_idx, len(text)):
        if text[k] == '{':
            depth += 1
        elif text[k] == '}':
            depth -= 1
            if depth == 0:
                return k
    return -1


def find_function(text, name):
    head = re.compile(r'^[^\n;#]*\b' + re.escape(name) + r'\s*\(', re.M)
    for m in head.finditer(text):
        i, depth = m.end() - 1, 0
        while i < len(text):
            if text[i] == '(':
                depth += 1
            elif text[i] == ')':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        j = i + 1
        while j < len(text) and text[j] in ' \t\r\n':
            j += 1
        if j < len(text) and text[j] == '{':
            end = _match_block(text, j)
            if end > 0:
                return text[m.start():end + 1]
    return None


def find_object(text, name):
    m = re.search(r'^[^\n;#]*\b' + re.escape(name) + r'\s*\[[^\]]*\]\s*=\s*\{',
                  text, re.M)
    if not m:
        return None
    end = _match_block(text, m.end() - 1)
    semi = text.index(';', end)
    return text[m.start():semi + 1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', required=True)
    ap.add_argument('--func', action='append', default=[])
    ap.add_argument('--object', action='append', default=[])
    ap.add_argument('--lines')
    ap.add_argument('--rename', action='append', default=[])
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    text = open(a.src).read()
    parts = []
    if a.lines:                      # lines first: they are #defines/typedefs
        lo, hi = (int(v) for v in a.lines.split('-'))
        parts.append('\n'.join(text.splitlines()[lo - 1:hi]))
    for name in a.object:
        got = find_object(text, name)
        if got is None:
            sys.exit(f'extract.py: object {name} not found in {a.src}')
        parts.append(got)
    for name in a.func:
        got = find_function(text, name)
        if got is None:
            sys.exit(f'extract.py: function {name} not found in {a.src}')
        parts.append(got)
    out = '\n\n'.join(parts) + '\n'
    for pair in a.rename:
        old, new = pair.split('=', 1)
        out = re.sub(r'\b' + re.escape(old) + r'\b', new, out)
    open(a.out, 'w').write(f'/* generated by extract.py from {a.src} */\n' + out)


if __name__ == '__main__':
    main()
```

### 1.3 ADD `tools/dsp_golden/prelude.h`

```c
/*
 * Host prelude for extracted DSP fragments (S073 Step 0).
 *
 * What:       supplies the types, macros and intrinsics that the extracted
 *             firmware fragments expect, with host definitions: placement
 *             macros become empty, __SSAT becomes a portable clamp, and the
 *             DSP structs are mirrored field-for-field.
 * Why:        the harness compiles exact source text of old and new
 *             functions without the firmware include tree (CMSIS, MPU,
 *             Scene model).
 * Inputs:     none. Outputs: declarations only.
 * Accessors:  every test_*.c through `-include prelude.h`.
 * Affiliates: ResonantFilter.h (struct and enums), distortion.h (Distortion),
 *             sample_mix.h, mixer.h (MIXER_ROUTING_*), config.h
 *             (OUTPUT_DMA_SIZE 32, AUDIO_DMA_FRAMES 96). Re-verify these
 *             mirrors if those headers change.
 */
#ifndef DSP_GOLDEN_PRELUDE_H
#define DSP_GOLDEN_PRELUDE_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define INITCM
#define INITCM_EFFECT
#define INITCM_EFFECT_NOINLINE
#define INITCM_NOINLINE
#define INCCM
#define INCCMZ
#define INDTCM
#define INDTCMZ
#define OUTPUT_DMA_SIZE  32
#define AUDIO_DMA_FRAMES 96
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline int32_t host_ssat(int32_t v, unsigned bits)
{
    const int32_t hi = (int32_t)((1u << (bits - 1u)) - 1u);
    const int32_t lo = -hi - 1;
    return v > hi ? hi : (v < lo ? lo : v);
}
#define __SSAT(v, b) host_ssat((int32_t)(v), (b))

typedef int32_t sample_mx_t;
#define SAMPLE_MIX_SHIFT_16_TO_24 8
static inline sample_mx_t sampleMix_fromInt16(int16_t x)
{
    return ((sample_mx_t)x) << SAMPLE_MIX_SHIFT_16_TO_24;
}

typedef struct DistStruct { float shape; float inv_shape; } Distortion;

#define ENABLE_NONLINEAR_INTEGRATORS 1
#define FILTER_GAIN 0x70ff
#define USE_SHAPER_NONLINEARITY 0
enum filterTypeEnum { FILTER_LP = 1, FILTER_HP, FILTER_BP, FILTER_UNITY_BP,
                      FILTER_NOTCH, FILTER_PEAK, FILTER_NAIVE_2_POLE };
typedef struct ResoFilterStruct {
    float f, g, q, s1, s2, a, b, zi, drive;
} ResonantFilter;

enum { MIXER_ROUTING_DAC1_STEREO = 0, MIXER_ROUTING_DAC2_STEREO,
       MIXER_ROUTING_DAC1_L, MIXER_ROUTING_DAC1_R,
       MIXER_ROUTING_DAC2_L, MIXER_ROUTING_DAC2_R };

typedef union {
    sample_mx_t mx[2][OUTPUT_DMA_SIZE];
    float f[2][OUTPUT_DMA_SIZE];
} mixer_fx_bus_t;

/* Deterministic generator for test signals (xorshift32; host only). */
static inline uint32_t golden_rand(uint32_t *s)
{
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x;
}
#endif
```

The `ResonantFilter` field order must match `ResonantFilter.h:77–92`
(`f, g, q, s1, s2, a, b, zi, drive`; the shaper is off). Verify against the
header when the harness is first built.

### 1.4 ADD `tools/dsp_golden/fpseq.py` (the ARM codegen check)

```python
#!/usr/bin/env python3
"""
Compare the value-affecting ARM instructions of old and new DSP loops.

What:       disassembles an ARM object (`arm-none-eabi-objdump -d
            --no-show-raw-insn`), finds each loop body in the named functions
            (the span from a backward branch's target to the branch), and
            builds a multiset of value-affecting instructions:
            - VFP arithmetic and conversions (vadd, vsub, vmul, vnmul, vfma,
              vfms, vfnma, vfnms, vdiv, vabs, vneg, vsqrt, vcvt, vcmp);
            - integer saturation (ssat, usat, qadd, qsub).
            These are compared (MATCH/DIFF). Integer narrowing (sxth, sxtb,
            uxth) is printed separately and not compared: a fused loop
            replaces each strh->ldrsh stage boundary with sxth by design.
            Register numbers and addressing are ignored; VFP data-type
            suffixes are kept (vcvt.s32.f32 differs from vcvt.f32.s32);
            Thumb width suffixes (.w/.n) are dropped.
            Each --ref/--new item is FUNC (all loops of FUNC) or FUNC:K (only
            loop K, 0-based). The reference total (the sum of its items) is
            compared with the candidate total.
Why:        plan §0.3. IEEE single-precision arithmetic is deterministic,
            so equal operation multisets between the old passes and the new
            fused loop, together with bit-identical host results, show that
            -Ofast did not reassociate, contract or reciprocal-transform the
            new code differently.
Inputs:     --obj OBJECT, --ref ITEM (repeatable), --new ITEM (repeatable),
            --report (print only), --all (count every loop instruction; used
            for the Step 5 instruction-count record).
Outputs:    per-item loop multisets, then MATCH or DIFF; exit 1 on DIFF.
Accessors:  tools/dsp_golden/Makefile armcheck-* targets.
Affiliates: armcheck/*.c.
"""
import argparse
import collections
import re
import subprocess
import sys

VALUE_OPS = re.compile(
    r'^(vadd|vsub|vmul|vnmul|vfma|vfms|vfnma|vfnms|vdiv|vabs|vneg|vsqrt|vcvt|'
    r'vcmp|vcmpe|ssat|usat|qadd|qadd16|qsub|qsub16)(\.|$)')
NARROW_OPS = re.compile(r'^(sxth|sxtb|uxth)$')


def norm(op):
    return re.sub(r'\.(w|n)$', '', op)
BRANCH = re.compile(
    r'^(b|beq|bne|bcs|bhs|bcc|blo|bmi|bpl|bvs|bvc|bhi|bls|bge|blt|bgt|ble)'
    r'(\.[nw])?$|^cbn?z$')


def disasm(obj):
    return subprocess.run(['arm-none-eabi-objdump', '-d', '--no-show-raw-insn', obj],
                          check=True, capture_output=True, text=True).stdout


def function_rows(text, name):
    m = re.search(r'^[0-9a-f]+ <' + re.escape(name) + r'>:\n(.*?)(?:\n\n|\Z)',
                  text, re.S | re.M)
    if not m:
        sys.exit(f'fpseq.py: {name} not found')
    rows = []
    for line in m.group(1).splitlines():
        p = re.match(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)', line)
        if p:
            rows.append((int(p.group(1), 16), p.group(2), p.group(3)))
    return rows


def loop_counters(rows, count_all):
    out = []
    for addr, op, args in rows:
        if BRANCH.match(norm(op)):
            t = re.search(r'\b([0-9a-f]+)\s*<', args)
            if t and int(t.group(1), 16) < addr:
                lo = int(t.group(1), 16)
                body = [norm(o) for a, o, _ in rows if lo <= a <= addr]
                values = collections.Counter(
                    o for o in body if count_all or VALUE_OPS.match(o))
                narrow = collections.Counter(o for o in body if NARROW_OPS.match(o))
                out.append((values, narrow))
    return out


def total(text, items, count_all, label):
    acc = collections.Counter()
    for item in items:
        name, _, k = item.partition(':')
        loops = loop_counters(function_rows(text, name), count_all)
        chosen = [loops[int(k)]] if k else loops
        for i, (c, narrow) in enumerate(chosen):
            print(f'{label} {item} loop{i}: {dict(sorted(c.items()))}'
                  f'  narrowing {dict(narrow)}')
            acc += c
    return acc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--obj', required=True)
    ap.add_argument('--ref', action='append', required=True)
    ap.add_argument('--new', action='append', required=True)
    ap.add_argument('--report', action='store_true')
    ap.add_argument('--all', action='store_true')
    a = ap.parse_args()
    text = disasm(a.obj)
    ref = total(text, a.ref, a.all, 'ref')
    new = total(text, a.new, a.all, 'new')
    print(f'ref total {sum(ref.values())}  new total {sum(new.values())}')
    if a.report:
        return 0
    if ref == new:
        print('MATCH')
        return 0
    print('DIFF', dict((ref - new) + (new - ref)))
    return 1


if __name__ == '__main__':
    sys.exit(main())
```

The reference side names the old loops that one fused iteration replaces.
For example, Snare is `--ref ref_snare:0 --ref old_calcDistBlock`: one
volumeMod branch loop plus the distortion loop.

A pass boundary today is `strh` then `ldrsh`, and it becomes `sxth` in a fused
loop. Memory operations are therefore not counted. The implementer confirms by
eye that every reference `strh`→`ldrsh` stage boundary corresponds to one
`sxth` (or the final `strh`) in the candidate.

### 1.5 ADD `tools/dsp_golden/Makefile`

```make
# S073 golden harness and ARM codegen check (Step 0).
#
# What:       builds and runs the host old-vs-new tests and the ARM codegen
#             comparisons for each CPU step. Every target is listed in this
#             document; each is added at the step named in its comment.
# Why:        plan §0.3: no new tools. The host `cc` and the existing
#             arm-none-eabi toolchain are all the harness needs.
# Inputs:     frozen/ (snapshot.sh), current sources, extract.py.
# Outputs:    build/ (generated fragments, objects, binaries, reports).
# Accessors:  `make -C tools/dsp_golden <target>` from the repository root.
# Affiliates: test_*.c, armcheck/*.c, fpseq.py, check_special_tags.py.
ROOT   := ../..
FROZEN := frozen
GEN    := build/gen
HOSTCC ?= cc
# -fno-vectorize/-fno-slp-vectorize (clang; use -fno-tree-vectorize with gcc)
# keep host FP scalar like the Cortex-M7 (no NEON). A vectorised host loop can
# turn an out-of-range float->int16 conversion (undefined in C) into a
# saturating narrow in one version and a wrap in the other: a host-only
# artefact seen during pre-validation.
HOSTFLAGS := -O2 -std=gnu11 -Wall -Wno-unused-function -fno-vectorize -fno-slp-vectorize \
             -include prelude.h -I$(GEN)
ARMCC  := arm-none-eabi-gcc
ARMFLAGS := -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -Ofast -std=gnu11 \
            -fdata-sections -ffunction-sections -I$(ROOT) -I$(ROOT)/Core \
            -I$(ROOT)/Core/DSPAudio -I$(ROOT)/Core/compat -I$(GEN)
EX := python3 extract.py
FP := python3 fpseq.py

# Filter symbols extracted per side; `rn` prefixes them (A_ or B_) so both
# sides link into one test binary. Order matters: callees first, because the
# extracted fragment has no header prototypes.
FILTER_SYMS := fastTanh fastTan tanhXdX softClipTwo SVF_setReso \
               SVF_directSetFilterValue SVF_init SVF_reset SVF_recalcFreq \
               SVF_setDrive SVF_calcBlockZDF SVF_calcBlockZDFFloat
fn = $(foreach s,$(1),--func $(s))
rn = $(foreach s,$(2),--rename $(s)=$(1)$(s))

.PHONY: selftest filter special_tags pack postchain octave mixer \
        armcheck-filter armcheck-postchain armcheck-mixer clean
$(GEN):
	mkdir -p $(GEN)

# ---- Step 0 gate: frozen filter (A) against itself (B), zero differences.
selftest: | $(GEN)
	$(EX) --src $(FROZEN)/Core/DSPAudio/ResonantFilter.c $(call fn,$(FILTER_SYMS)) \
	  $(call rn,A_,$(FILTER_SYMS)) --out $(GEN)/filter_a.c
	$(EX) --src $(FROZEN)/Core/DSPAudio/ResonantFilter.c $(call fn,$(FILTER_SYMS)) \
	  $(call rn,B_,$(FILTER_SYMS)) --out $(GEN)/filter_b.c
	$(HOSTCC) $(HOSTFLAGS) -c $(GEN)/filter_a.c -o build/filter_a.o
	$(HOSTCC) $(HOSTFLAGS) -c $(GEN)/filter_b.c -o build/filter_b.o
	$(HOSTCC) $(HOSTFLAGS) test_filter.c build/filter_a.o build/filter_b.o -o build/selftest -lm
	./build/selftest --expect-identical

clean:
	rm -rf build
```

Each step's section adds its target(s) to this Makefile: §3.6, §4.10,
§5.3, §7.7–§7.8, §8.3 and §9.3–§9.4. Every rename goes through
`extract.py --rename`, which matches on word boundaries, so no `sed` is
needed.

### 1.6 ADD `tools/dsp_golden/test_filter.c`

The Step 0 self-test, reused unchanged by Step 1 (§3.6).

```c
/*
 * ZDF filter old-vs-new comparison (S073 Steps 0 and 1).
 *
 * What:       runs the A and B filter implementations (generated into
 *             build/gen as filter_a.c / filter_b.c with A_/B_ prefixes and
 *             compiled as separate objects, so each side can use its own
 *             FP-contraction flag) over the fixed grid: 6 ZDF types × 10 cutoffs × 8 resonances ×
 *             6 drives × 10 signals = 28,800 configurations × 9,600 samples
 *             (300 blocks of 32), for both the int16 and the float variant.
 *             Reports samples differing, max |Δ| in LSB, SDR, and the
 *             configurations diverging by more than 16 LSB.
 * Why:        Gate 0 (A == B when both are the frozen code) and Gate 1 (S1
 *             acceptance when B is the batched solver). The grid reproduces
 *             the Session 073 host run (28,800 configurations, 276 M
 *             samples).
 * Inputs:     argv: --expect-identical (Gate 0) or --s1-report FILE (Gate 1;
 *             FILE is the rounding-baseline divergence list from the
 *             `filter-baseline` build).
 * Outputs:    report on stdout; exit 1 if --expect-identical finds any
 *             difference.
 * Accessors:  Makefile targets selftest, filter, filter-baseline.
 * Affiliates: ResonantFilter.c (both sides), prelude.h.
 */
/* Prototypes of the two extracted sides (definitions in filter_a.o /
** filter_b.o). */
#define SIDE_DECLS(P) \
    void P##SVF_init(ResonantFilter *); void P##SVF_reset(ResonantFilter *); \
    void P##SVF_setReso(ResonantFilter *, float); \
    void P##SVF_setDrive(ResonantFilter *, uint8_t); \
    void P##SVF_directSetFilterValue(ResonantFilter *, float); \
    void P##SVF_recalcFreq(ResonantFilter *); \
    void P##SVF_calcBlockZDF(ResonantFilter *, const uint8_t, int16_t *, const uint8_t); \
    void P##SVF_calcBlockZDFFloat(ResonantFilter *, const uint8_t, float *, const uint8_t);
SIDE_DECLS(A_)
SIDE_DECLS(B_)

static const float cutoffs[10]  = {0.02f,0.05f,0.1f,0.2f,0.3f,0.4f,0.5f,0.6f,0.8f,1.0f};
static const float resos[8]     = {0.0f,0.2f,0.4f,0.6f,0.8f,0.85f,0.9f,0.98f};
static const uint8_t drives[6]  = {0u,25u,50u,75u,100u,127u};

/* 10 signals: saw 40/220 Hz, square 110 Hz full scale, white noise,
** impulses every 1,000 samples, sine 40/220/1,760/7,000 Hz, saw 1,760 Hz. */
static int16_t signal_at(int sig, uint32_t n, uint32_t *rng)
{
    const double fs = 44108.0, t = n / fs;
    switch (sig) {
    case 0: return (int16_t)(32767.0 * (2.0 * fmod(t * 40.0, 1.0) - 1.0));
    case 1: return (int16_t)(32767.0 * (2.0 * fmod(t * 220.0, 1.0) - 1.0));
    case 2: return fmod(t * 110.0, 1.0) < 0.5 ? 32767 : -32768;
    case 3: return (int16_t)(golden_rand(rng) >> 16);
    case 4: return (n % 1000u) == 0u ? 32767 : 0;
    case 5: return (int16_t)(32767.0 * sin(2 * M_PI * 40.0 * t));
    case 6: return (int16_t)(32767.0 * sin(2 * M_PI * 220.0 * t));
    case 7: return (int16_t)(32767.0 * sin(2 * M_PI * 1760.0 * t));
    case 8: return (int16_t)(32767.0 * sin(2 * M_PI * 7000.0 * t));
    default: return (int16_t)(32767.0 * (2.0 * fmod(t * 1760.0, 1.0) - 1.0));
    }
}
```

The body:

1. Loop the grid. For each configuration, run `A_SVF_init` / `B_SVF_init`,
   then `*_SVF_reset`, `*_SVF_directSetFilterValue(cutoff)`,
   `*_SVF_setReso(reso)` and `*_SVF_setDrive(drive)`.
2. Per 32-sample block, call `*_SVF_recalcFreq`, then
   `*_SVF_calcBlockZDF(&flt, type, buf, 32)` on identical copies of the
   block.
3. Compare the outputs.
4. Repeat with `*_SVF_calcBlockZDFFloat` on `buf / 32767.0f`, comparing the
   float outputs scaled by 32767.
5. Accumulate:
   - the differing-sample count;
   - max |Δ|;
   - Σx² and Σ(x−y)², for the SDR;
   - the configuration IDs with max |Δ| > 16.

Print one summary line per variant and the >16-LSB list.

**Modes:**

- `--expect-identical`: exit 1 on any difference (Gate 0).
- `--baseline-out FILE`: write the >16-LSB configuration IDs (the rounding
  baseline).
- `--s1-report FILE`: apply the S1 acceptance of plan §3.4 against that
  baseline and exit 1 on failure (Gate 1).

### 1.7 ADD `tools/dsp_golden/armcheck/` (directory)

It holds one TU per S0 step, written in that step's section (§5.4, §7.8,
§9.4). Each TU defines a `noinline` reference function (the frozen passes)
and a `noinline` candidate (the new code) with identical signatures. They
are compiled with `ARMFLAGS` (the project's DSP flags without LTO) and
compared by `fpseq.py`.

### 1.8 MODIFY `.gitignore` (append)

```
# S073 golden harness build products (generated fragments and binaries)
tools/dsp_golden/build/
```

### 1.9 Gate 0

- `sh tools/dsp_golden/snapshot.sh` has run **before any Step 1 edit**.
- `make -C tools/dsp_golden selftest` reports 0 differing samples.
- `fpseq.py --report` runs on a trivial armcheck TU, which proves the ARM
  toolchain path works.
- The firmware is untouched, so `bss`/`data`/flash are unchanged.

---

## 2. Conventions shared by Steps 1–6

- **Constant-CPU review item.** Before each gate, read the diff for any new
  `if` on a control value or on signal content inside a render loop or
  around a render call. The only allowed conditionals are:
  - loop-invariant **selects** of a value, which cost the same either way
    (for example Step 4's gain select);
  - the existing kept paths listed in §0.
- **No RAM.** Every step's gate checks `arm-none-eabi-size`: `data` = 416,
  `bss` = 426,336 (the baseline), and DTCM statics 4,448 B in
  `link_budget.py`.

---

## 3. Step 1 — ZDF filter: batched divisions (S1)

### 3.1 ADD `Core/DSPAudio/ResonantFilter.c`, after line 153 (end of `softClipTwo()`)

```c
//------------------------------------------------------------------------------------
/*
 * Normalised Padé pieces of tanhXdX() for the batched ZDF solver (S073 Step 1).
 *
 * What:       tanhXdX(v) = ((a+105)a+945) / ((15a+420)a+945) with a = v^2,
 *             rewritten as N/D where N = (a/945 + 105/945)*a + 1 and
 *             D = (15a/945 + 420/945)*a + 1. The ratio is unchanged, and N
 *             and D are both >= 1 for every real v.
 * Why:        the batched solver multiplies several denominators together
 *             and inverts the product with a single division. Denominators
 *             >= 1 keep those products far from float overflow for any drive
 *             or resonance, so the batching needs no range check and no
 *             data-dependent path (constant-CPU rule).
 * Inputs:     a = v*v, where v is the saturator argument.
 * Outputs:    N or D as float, >= 1.
 * Accessors:  SVF_calcBlockZDF(), SVF_calcBlockZDFFloat().
 * Affiliates: tanhXdX() and softClipTwo() stay for the naive 2-pole path;
 *             tools/dsp_golden/test_filter.c checks the S1 equivalence.
 */
static inline float svf_padeNum(const float a)
{
	return (a * (1.0f / 945.0f) + (105.0f / 945.0f)) * a + 1.0f;
}

static inline float svf_padeDen(const float a)
{
	return (a * (15.0f / 945.0f) + (420.0f / 945.0f)) * a + 1.0f;
}
//------------------------------------------------------------------------------------
/*
 * Configuration guard for the batched ZDF solver (S073 Step 1).
 *
 * What:       stops the build if the filter is configured without nonlinear
 *             integrators or with the output shaper.
 * Why:        the batched algebra below is derived for t0/t1 = tanhXdX(...)
 *             and a soft-clipped input. The shaper and linear variants were
 *             never enabled on this firmware, and their branches were
 *             removed from SVF_calcBlockZDF() in this step.
 * Inputs:     ResonantFilter.h switches. Outputs: none.
 * Accessors:  the preprocessor.
 * Affiliates: SVF_calcBlockZDFFloat()'s own shaper #error (line ~329).
 */
#if !ENABLE_NONLINEAR_INTEGRATORS || USE_SHAPER_NONLINEARITY
#error "Batched ZDF solver (S073 Step 1) requires nonlinear integrators and no shaper"
#endif
```

### 3.2 MODIFY `ResonantFilter.c:187–318` (the non-naive branch of `SVF_calcBlockZDF()`)

- Lines 154–186 (signature, `f`/`R`/`ff`, and the naive 2-pole branch) are
  **unchanged**.
- Replace everything from the `} else {` at line 187 through the closing
  brace of that `else` at line 318 with:

```c
	} else {
		/*
		 * Batched ZDF solver: two divisions per sample (S073 Step 1).
		 *
		 * What:       the same trapezoidal SVF with nonlinear integrators as
		 *             before, but the five reciprocals of each sample are
		 *             computed with two divisions:
		 *             - Stage A inverts Dx*D1 once to give the soft-clipped
		 *               input x = u*tanhXdX(u/2) and t1 = tanhXdX(s1/2);
		 *             - Stage B inverts D0*E*F once to give t0 =
		 *               tanhXdX(v0), g0 = 1/(1+2fR*t0) and the feedback
		 *               solution y1.
		 *             softClipTwo(s1) is s1*t1, as GCC already shared. The
		 *             output switch is unchanged, and LP keeps fastTanh().
		 * Why:        audit F1. The filter was the largest single cost
		 *             (5 VDIV per sample, 6 for LP, four of them on one
		 *             dependent chain). This is exact algebra: only float
		 *             rounding changes (class S1, approved).
		 * Inputs:     buf (int16 input block), filter coefficients g, f, q
		 *             and drive, and state s1, s2, zi.
		 * Outputs:    buf (int16 output block, __SSAT as before); updated
		 *             s1, s2, zi. The state is held in locals and written
		 *             back once, including on the legacy default-type
		 *             return.
		 * Accessors:  DrumVoice.c:334, Snare.c:229, CymbalVoice.c:247,
		 *             HiHat.c:265.
		 * Affiliates: SVF_calcBlockZDFFloat() (must stay the float twin),
		 *             svf_padeNum()/svf_padeDen(),
		 *             stereoFilter_linkCoefficients() (no layout change, so
		 *             no edit), tools/dsp_golden/test_filter.c.
		 */
		const float drive = filter->drive;
		float s1 = filter->s1;
		float s2 = filter->s2;
		float zi = filter->zi;

		for(i=0;i<size;i++)
		{
			/* Stage A: input soft clip and t1 share one division. */
			const float u    = (buf[i]/((float)0x7fff))*drive;
			const float ax   = 0.25f*u*u;
			const float a1   = 0.25f*s1*s1;
			const float Nx   = svf_padeNum(ax);
			const float Dx   = svf_padeDen(ax);
			const float N1   = svf_padeNum(a1);
			const float D1   = svf_padeDen(a1);
			const float invA = 1.f / (Dx*D1);
			const float x    = u*Nx*D1*invA;          /* softClipTwo(u)      */
			const float t1   = N1*Dx*invA;            /* tanhXdX(0.5f*s1)    */

			// input with half sample delay, for non-linearities
			const float ih = 0.5f * (x + zi);
			zi = x;

			/* Stage B: t0, g0 and y1 share one division. */
			const float v0   = 0.5f * (ih - 2*R*s1 - s2);
			const float a0   = v0*v0;
			const float N0   = svf_padeNum(a0);
			const float D0   = svf_padeDen(a0);
			const float E    = D0 + f*2*R*N0;         /* g0 = D0/E           */
			const float P    = ff*N0*t1;              /* f1 = P/E            */
			const float F    = P + E;
			const float Q    = P*x + s2*E + f*D0*t1*s1; /* y1 = Q/F          */
			const float invB = 1.f / (D0*E*F);
			const float t0   = N0*E*F*invB;
			const float g0   = D0*D0*F*invB;
			const float y1   = Q*D0*E*invB;

			// solve the remaining stages with nonlinear gain
			const float s1t1 = s1*t1;                 /* softClipTwo(s1)     */
			const float xx   = t0*(x - y1);
			const float y0   = (s1t1 + f*xx)*g0;

			s1 = s1t1 + 2*f*(xx - t0*2*R*y0);
			s2 = s2 + 2*f* t1*y0;

			int32_t tmp;
			switch(type)
			{
			default:
				/* Legacy behaviour kept: an unknown type advances one
				** sample of state and returns without writing buf. The
				** state lives in locals now, so write it back first. */
				filter->s1 = s1;
				filter->s2 = s2;
				filter->zi = zi;
				return;
			case FILTER_LP:
				tmp = fastTanh(y1) * 0x7fff ;//FILTER_GAIN;
				buf[i] = __SSAT(tmp,16);
				break;
			case FILTER_HP:
			{
				const float ugb = 2*R*y0;
				const float h = x - ugb - y1;
				tmp = h * FILTER_GAIN;
				buf[i] = __SSAT(tmp,16);
			}
				break;
			case FILTER_BP:
				tmp = y0 * FILTER_GAIN;
				buf[i] = __SSAT(tmp,16);
				break;
			case FILTER_UNITY_BP:
			{
				const float ugb = 2*R*y0;
				tmp = ugb * FILTER_GAIN;
				buf[i] = __SSAT(tmp,16);
			}
				break;
			case FILTER_NOTCH:
			{
				const float ugb = 2*R*y0;
				tmp = (x-ugb) * FILTER_GAIN;
				buf[i] = __SSAT(tmp,16);
			}
				break;
			case FILTER_PEAK:
			{
				const float ugb = 2*R*y0;
				const float h = x - ugb - y1;
				tmp = (y1-h) * FILTER_GAIN;
				buf[i] = __SSAT(tmp,16);
			}
				break;
			}
		}
		filter->s1 = s1;
		filter->s2 = s2;
		filter->zi = zi;
	}
```

**REMOVE** as part of this replacement:

- the `#if USE_SHAPER_NONLINEARITY` branches and the linear
  (`!ENABLE_NONLINEAR_INTEGRATORS`) branches inside the loop (old lines 191–195,
  197–201, 208–215, and each output case's shaper half). §3.1's `#error`
  now guards those configurations.
- The old comment at lines 204–207 ("You can trivially remove any
  saturator…") goes with them.

### 3.3 MODIFY `ResonantFilter.c:372–419` (the non-naive loop of `SVF_calcBlockZDFFloat()`)

- Lines 332–371 (signature, `out_gain`/`f`/`R`/`ff`, the up-front type check,
  and the naive branch) are **unchanged**.
- Replace the loop from `for (i = 0u; i < size; i++) {` at line 372 through its
  closing brace at line 419 with:

```c
    /*
     * Batched ZDF solver, float I/O twin (S073 Step 1).
     *
     * What:       the identical two-division batching as SVF_calcBlockZDF(),
     *             on normalised float samples: the input is buf[i]*drive (no
     *             /32767), and the outputs use the existing out_gain scaling
     *             without __SSAT.
     * Why:        audit F1. The StereoFilter Effect runs two of these per
     *             block (L and R), so it carries the same division cost
     *             as the voices. Class S1.
     * Inputs:     buf (normalised float block), coefficients and state as
     *             in the int16 twin.
     * Outputs:    buf (normalised float block); updated s1, s2, zi, written
     *             back once after the loop. Invalid types never reach this
     *             loop: they return before advancing state, as before.
     * Accessors:  StereoFilterEffect.c:101 (left) and :103 (right).
     * Affiliates: SVF_calcBlockZDF() (keep the algebra identical),
     *             stereoFilter_linkCoefficients() (copies f, g, q, drive
     *             only; no layout change), tools/dsp_golden/test_filter.c.
     */
    {
        const float drive = filter->drive;
        float s1 = filter->s1;
        float s2 = filter->s2;
        float zi = filter->zi;

        for (i = 0u; i < size; i++) {
            const float u    = buf[i] * drive;
            const float ax   = 0.25f * u * u;
            const float a1   = 0.25f * s1 * s1;
            const float Nx   = svf_padeNum(ax);
            const float Dx   = svf_padeDen(ax);
            const float N1   = svf_padeNum(a1);
            const float D1   = svf_padeDen(a1);
            const float invA = 1.0f / (Dx * D1);
            const float x    = u * Nx * D1 * invA;
            const float t1   = N1 * Dx * invA;
            const float ih   = 0.5f * (x + zi);
            zi = x;
            const float v0   = 0.5f * (ih - 2.0f * R * s1 - s2);
            const float a0   = v0 * v0;
            const float N0   = svf_padeNum(a0);
            const float D0   = svf_padeDen(a0);
            const float E    = D0 + f * 2.0f * R * N0;
            const float P    = ff * N0 * t1;
            const float F    = P + E;
            const float Q    = P * x + s2 * E + f * D0 * t1 * s1;
            const float invB = 1.0f / (D0 * E * F);
            const float t0   = N0 * E * F * invB;
            const float g0   = D0 * D0 * F * invB;
            const float y1   = Q * D0 * E * invB;
            const float s1t1 = s1 * t1;
            const float xx   = t0 * (x - y1);
            const float y0   = (s1t1 + f * xx) * g0;

            s1 = s1t1 + 2.0f * f * (xx - t0 * 2.0f * R * y0);
            s2 += 2.0f * f * t1 * y0;

            switch (type) {
            case FILTER_LP:
                buf[i] = fastTanh(y1);
                break;
            case FILTER_HP:
                buf[i] = (x - 2.0f * R * y0 - y1) * out_gain;
                break;
            case FILTER_BP:
                buf[i] = y0 * out_gain;
                break;
            case FILTER_UNITY_BP:
                buf[i] = 2.0f * R * y0 * out_gain;
                break;
            case FILTER_NOTCH:
                buf[i] = (x - 2.0f * R * y0) * out_gain;
                break;
            case FILTER_PEAK:
                buf[i] = (y1 - (x - 2.0f * R * y0 - y1)) * out_gain;
                break;
            default:
                break;
            }
        }
        filter->s1 = s1;
        filter->s2 = s2;
        filter->zi = zi;
    }
```

Also **REMOVE** the `#if ENABLE_NONLINEAR_INTEGRATORS … #else … #endif`
inside the old loop (lines 374–384); §3.1's `#error` covers it.

### 3.4 MODIFY `Core/DSPAudio/ResonantFilter.h:103–112` (the comment above `SVF_calcBlockZDFFloat`)

Append to the existing comment block, before its closing `*/`:

```c
 * S073 Step 1: both ZDF blocks use the batched two-division solver
 * (svf_padeNum()/svf_padeDen() in ResonantFilter.c). Keep the float and
 * int16 twins algebraically identical; the golden harness
 * (tools/dsp_golden/test_filter.c) compares both against the frozen
 * pre-S073 code.
```

### 3.5 No change: `StereoFilterEffect.c`

`stereoFilter_linkCoefficients()` (`StereoFilterEffect.c:~30`) copies `f`,
`g`, `q` and `drive`. Step 1 changes no field and adds none, so there is
nothing to mirror.

### 3.6 Harness target for Step 1 (ADD to `tools/dsp_golden/Makefile`)

```make
# ---- Step 1 (Gate 1): rounding baseline, then frozen (A) vs batched (B).
# The new side lists svf_padeNum/svf_padeDen first so they precede their users.
NEW_FILTER_SYMS := svf_padeNum svf_padeDen $(FILTER_SYMS)
filter: selftest
	$(HOSTCC) $(HOSTFLAGS) -ffp-contract=off -c $(GEN)/filter_b.c -o build/filter_b_strict.o
	$(HOSTCC) $(HOSTFLAGS) test_filter.c build/filter_a.o build/filter_b_strict.o \
	  -o build/filter_baseline -lm
	./build/filter_baseline --baseline-out build/baseline_div.txt
	$(EX) --src $(ROOT)/Core/DSPAudio/ResonantFilter.c $(call fn,$(NEW_FILTER_SYMS)) \
	  $(call rn,B_,$(NEW_FILTER_SYMS)) --out $(GEN)/filter_b_new.c
	$(HOSTCC) $(HOSTFLAGS) -c $(GEN)/filter_b_new.c -o build/filter_b_new.o
	$(HOSTCC) $(HOSTFLAGS) test_filter.c build/filter_a.o build/filter_b_new.o \
	  -o build/filter -lm
	./build/filter --s1-report build/baseline_div.txt

# ---- Step 1 report (not a gate): division count per object.
armcheck-filter:
	$(ARMCC) $(ARMFLAGS) -c $(FROZEN)/Core/DSPAudio/ResonantFilter.c -o build/rf_old.o
	$(ARMCC) $(ARMFLAGS) -c $(ROOT)/Core/DSPAudio/ResonantFilter.c -o build/rf_new.o
	@echo "vdiv old: $$(arm-none-eabi-objdump -d build/rf_old.o | grep -c vdiv)"
	@echo "vdiv new: $$(arm-none-eabi-objdump -d build/rf_new.o | grep -c vdiv)"
```

`armcheck-filter` is a **report**, not a gate: Step 1 is S1. Pre-validation
measured the object's `vdiv` count falling from 81 to 39, with `.text`
growing by 732 B for the two ZDF variants together.

### 3.7 Gate 1

- `make -C tools/dsp_golden filter`, S1 acceptance as measured in
  pre-validation (§13):
  - outside the self-oscillating corner (cutoff 0.8 with resonance 0.98 in
    this grid), max |Δ| ≤ 1 LSB for int16 and < 1 LSB for float (32767
    scale), the same as the rounding-only baseline;
  - every configuration over 16 LSB lies in that same corner, where the
    rounding-only baseline also diverges. The plan's "subset ±10 %" count
    rule is replaced by this family rule: pre-validation found 13 against 10
    configurations (int16), all in the corner;
  - SDR ≥ 80 dB (measured 91.7 dB int16, 86.5 dB float);
  - no NaN or Inf.
- `armcheck-filter` shows the division count falling. Record the numbers in
  the audit.
- Firmware: `make clean && make all`; `data`/`bss` unchanged; record the flash
  delta (expected about +700 B).
- **Hardware (user):**
  - the worst-case Scene runs 10 minutes with no new underruns;
  - listening on the kits plus a high-reso self-oscillating patch;
  - a StereoFilter check at high reso/drive.

---

## 4. Step 2 — Descriptor special writers without strings (S0)

### 4.1 ADD `Core/DSP/Instruments/InstrumentManager.h`, after line 109 (`} instrument_binding_kind_t;`)

```c
/*
 * Descriptor special-writer IDs (S073 Step 2).
 *
 * What:       names the DSP setter that a descriptor row needs beyond a plain
 *             offset write. Bits 0-4 hold the writer
 *             (IM_SPECIAL_WRITER_MASK); bits 5-6 select the oscillator for
 *             the NOISE_FREQ/PITCH_* writers (IM_SPECIAL_OSC_*), replacing the
 *             old "osc1_"/"osc2_"/"osc3_"/"noise_" key-prefix match.
 * Why:        audit F2. instrumentManager_writeSpecialRuntime() used to
 *             rediscover this fixed per-row mapping with up to 20 strcmp,
 *             2 strstr and 7 strncmp on every LFO block (1,378 Hz x up to
 *             12 targets), every Morph worker pass, every velocity write and
 *             every Scene activation. The mapping never changes at run time,
 *             so it is tagged once in the flash tables.
 * Inputs:     none (compile-time constants).
 * Outputs:    the values stored in instrument_runtime_binding_t.special.
 * Accessors:  the four *Parameters.c tables (ROW_SPECIAL /
 *             ROW_MENU_SPECIAL), instrumentManager_writeSpecialRuntime(),
 *             and the DEV_MODE_DIAGNOSTIC self-check.
 * Affiliates: tools/dsp_golden/check_special_tags.py, which verifies every
 *             tag against the original key rules. AMP_DECAY deliberately
 *             covers the HiHat closed-hat cache too: the writer keys that
 *             branch on the slot's live runtime type, exactly as the string
 *             chain did.
 */
typedef enum {
    IM_SPECIAL_NONE = 0u,
    IM_SPECIAL_NOISE_FREQ,
    IM_SPECIAL_PITCH_COARSE,
    IM_SPECIAL_PITCH_FINE,
    IM_SPECIAL_FILTER_FREQ,
    IM_SPECIAL_FILTER_RESO,
    IM_SPECIAL_FILTER_DRIVE,
    IM_SPECIAL_FILTER_TYPE,
    IM_SPECIAL_AMP_ATTACK,
    IM_SPECIAL_AMP_DECAY,
    IM_SPECIAL_HAT_DECAY_CHOKE,
    IM_SPECIAL_AMP_SLOPE,
    IM_SPECIAL_PITCH_EG_DECAY,
    IM_SPECIAL_PITCH_EG_SLOPE,
    IM_SPECIAL_PITCH_EG_AMOUNT,
    IM_SPECIAL_TRANSIENT_WAVE,
    IM_SPECIAL_TRANSIENT_FREQ,
    IM_SPECIAL_INSTRUMENT_DRIVE,
    IM_SPECIAL_LFO_RATE,
    IM_SPECIAL_WRITER_COUNT
} instrument_special_writer_t;

#define IM_SPECIAL_WRITER_MASK  0x1Fu
#define IM_SPECIAL_OSC_MASK     0x60u
#define IM_SPECIAL_OSC1         0x00u   /* voice->osc                     */
#define IM_SPECIAL_OSC2         0x20u   /* voice->modOsc                  */
#define IM_SPECIAL_OSC3         0x40u   /* voice->modOsc2 (Cymbal/HiHat)  */
#define IM_SPECIAL_OSC_NOISE    0x60u   /* voice->noiseOsc (Snare)        */
_Static_assert(IM_SPECIAL_WRITER_COUNT <= (IM_SPECIAL_WRITER_MASK + 1u),
               "special writer IDs must fit bits 0-4");
```

### 4.2 MODIFY `InstrumentManager.h:111–115` (`instrument_runtime_binding_t`)

```c
typedef struct {
    instrument_binding_kind_t kind;
    uint16_t offset;
    uint8_t parameter_type;
    /*
     * Special DSP writer tag (S073 Step 2).
     *
     * What:       IM_SPECIAL_* writer ID | IM_SPECIAL_OSC_* selector, or 0
     *             for rows that need only the generic offset write.
     * Why:        replaces the per-write key-string search (audit F2). It
     *             occupies the struct's existing padding byte (offset 5 of
     *             6, with arm-none-eabi short enums), so no descriptor table
     *             grows.
     * Inputs:     set by BIND_SPECIAL() in each *Parameters.c; 0 elsewhere.
     * Outputs:    read by instrumentManager_writeSpecialRuntime().
     * Accessors:  InstrumentManager.c only.
     * Affiliates: the size asserts below ParamDescriptor;
     *             EffectParamRows.h EFFECT_BIND_NONE (Effect rows are always
     *             0).
     */
    uint8_t special;
} instrument_runtime_binding_t;
```

### 4.3 ADD `InstrumentManager.h`, after line 157 (`} ParamDescriptor;`)

```c
/*
 * Descriptor layout guards (S073 Step 2).
 *
 * What:       pins the binding at 6 bytes and ParamDescriptor at 28 bytes,
 *             the sizes measured in the pre-S073 image (drum_param_descriptors
 *             is 0x444 bytes for 39 rows).
 * Why:        the special tag must use the existing padding byte. If a
 *             toolchain or field change grew the rows, all eight descriptor
 *             tables would silently grow in flash.
 * Inputs:     the compiler's layout. Outputs: a build error on mismatch.
 * Accessors:  the compiler.
 * Affiliates: instrument_runtime_binding_t.special.
 */
_Static_assert(sizeof(instrument_runtime_binding_t) == 6u,
               "instrument_runtime_binding_t must stay 6 bytes");
_Static_assert(sizeof(ParamDescriptor) == 28u,
               "ParamDescriptor must stay 28 bytes");
```

### 4.4 ADD `InstrumentManager.h`, after the `instrumentManager_writeRuntime()` prototype (line 551–553)

```c
/*
 * Special-tag self-check, DEV_MODE_DIAGNOSTIC builds (S073 Step 2).
 *
 * What:       classifies every registry descriptor row with the original
 *             key-string rules and counts the rows whose runtime.special tag
 *             differs.
 * Why:        a boot-time proof, on the device, that the flash tags
 *             reproduce the old string chain exactly. It needs no RAM: it
 *             is computed on demand.
 * Inputs:     the instrument registry and descriptor tables.
 * Outputs:    the mismatch count, clamped to 9 (0 = pass).
 * Accessors:  main.c boot_showFxBufDiagnostic() (DEV_MODE_DIAGNOSTIC only).
 * Affiliates: tools/dsp_golden/check_special_tags.py (the host twin).
 *             The definition exists only when DEV_MODE_DIAGNOSTIC is 1.
 */
uint8_t instrumentManager_specialTagSelfCheck(void);
```

### 4.5 MODIFY the four instrument tables: the macros

The same change applies in each file. Line numbers per file:

| File | `BIND` | `ROW` | `ROW_MENU` | `ROW_NOBIND` | `ROW_NOBIND_IMAGE` |
|---|---|---|---|---|---|
| `Drum/DrumParameters.c` | 41–42 | 64–65 | 71–72 | 80–81 | 88–89 |
| `Snare/SnareParameters.c` | 41–42 | 64–65 | 71–72 | 80–81 | 88–89 |
| `Cymbal/CymbalParameters.c` | 41–42 | 64–65 | 71–72 | 80–81 | 88–89 |
| `HiHat/HiHatParameters.c` | 42–43 | 65–66 | 72–73 | 81–82 | 89–90 |

**MODIFY** `BIND` (shown for Drum; use `SnareVoice`, `CymbalVoice` or
`HiHatVoice` in the other files):

```c
/*
 * Runtime binding with an optional special-writer tag (S073 Step 2).
 *
 * What:       BIND_SPECIAL() stores the DrumVoice byte offset, scalar type
 *             and IM_SPECIAL_* tag. BIND() keeps its old meaning (tag 0).
 * Why:        the tag names the DSP setter once, in flash, instead of
 *             InstrumentManager rediscovering it from file_key on every
 *             write (audit F2).
 * Inputs:     member path, TYPE_* scalar type, IM_SPECIAL_* tag.
 * Outputs:    one instrument_runtime_binding_t initialiser.
 * Accessors:  ROW*, ROW_MENU* below.
 * Affiliates: InstrumentManager.h instrument_special_writer_t;
 *             tools/dsp_golden/check_special_tags.py.
 */
#define BIND_SPECIAL(member_, type_, special_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, (uint16_t)offsetof(DrumVoice, member_), type_, special_ }
#define BIND(member_, type_) BIND_SPECIAL(member_, type_, IM_SPECIAL_NONE)
```

**ADD** after the existing `ROW` macro:

```c
/*
 * ROW_SPECIAL is ROW for a row whose value also needs a DSP setter (S073
 * Step 2). The tag must match tools/dsp_golden/check_special_tags.py; the
 * DEV_MODE_DIAGNOSTIC boot check verifies it on the device.
 */
#define ROW_SPECIAL(key_, cat_, long_, short_, dtype_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
```

**ADD** after the existing `ROW_MENU` macro:

```c
/* ROW_MENU_SPECIAL is ROW_MENU with a special-writer tag (S073 Step 2). */
#define ROW_MENU_SPECIAL(key_, cat_, long_, short_, menu_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
```

**MODIFY** `ROW_NOBIND` and `ROW_NOBIND_IMAGE`: change
`{ bind_kind_, 0u, 0u }` to `{ bind_kind_, 0u, 0u, IM_SPECIAL_NONE }`.
`-Wextra` (`-Wmissing-field-initializers`) would otherwise warn on every row.

### 4.6 MODIFY the tagged rows

In each listed row, change the macro name and append the tag as the last
argument. Every other row is unchanged and keeps tag 0. `ROW(` becomes
`ROW_SPECIAL(`; `ROW_MENU(` becomes `ROW_MENU_SPECIAL(`.

**`DrumParameters.c`** (17 rows):

| Line | Key | Tag |
|---|---|---|
| 172 | `osc1_pitch_coarse` | `IM_SPECIAL_PITCH_COARSE \| IM_SPECIAL_OSC1` |
| 173 | `osc1_pitch_fine` | `IM_SPECIAL_PITCH_FINE \| IM_SPECIAL_OSC1` |
| 175 | `osc2_pitch_coarse` | `IM_SPECIAL_PITCH_COARSE \| IM_SPECIAL_OSC2` |
| 178 | `filter_freq` | `IM_SPECIAL_FILTER_FREQ` |
| 179 | `filter_reso` | `IM_SPECIAL_FILTER_RESO` |
| 180 | `filter_drive` | `IM_SPECIAL_FILTER_DRIVE` |
| 181 | `filter_type` (menu) | `IM_SPECIAL_FILTER_TYPE` |
| 182 | `amp_envelope_attack` | `IM_SPECIAL_AMP_ATTACK` |
| 183 | `amp_envelope_decay` | `IM_SPECIAL_AMP_DECAY` |
| 184 | `amp_envelope_slope` | `IM_SPECIAL_AMP_SLOPE` |
| 185 | `pitch_envelope_decay` | `IM_SPECIAL_PITCH_EG_DECAY` |
| 186 | `pitch_envelope_amount` | `IM_SPECIAL_PITCH_EG_AMOUNT` |
| 187 | `pitch_envelope_slope` | `IM_SPECIAL_PITCH_EG_SLOPE` |
| 190 | `instrument_drive` | `IM_SPECIAL_INSTRUMENT_DRIVE` |
| 201 | `lfo_rate` | `IM_SPECIAL_LFO_RATE` |
| 216 | `transient_wave` (menu) | `IM_SPECIAL_TRANSIENT_WAVE` |
| 218 | `transient_freq` | `IM_SPECIAL_TRANSIENT_FREQ` |

**`SnareParameters.c`** (17 rows):

| Line | Key | Tag |
|---|---|---|
| 170 | `osc1_pitch_coarse` | `IM_SPECIAL_PITCH_COARSE \| IM_SPECIAL_OSC1` |
| 171 | `osc1_pitch_fine` | `IM_SPECIAL_PITCH_FINE \| IM_SPECIAL_OSC1` |
| 172 | `noise_freq` | `IM_SPECIAL_NOISE_FREQ \| IM_SPECIAL_OSC_NOISE` |
| 174–176 | `filter_freq`, `filter_reso`, `filter_drive` | `FILTER_FREQ`, `FILTER_RESO`, `FILTER_DRIVE` |
| 177 | `filter_type` (menu) | `IM_SPECIAL_FILTER_TYPE` |
| 178–180 | `amp_envelope_attack`, `_decay`, `_slope` | `AMP_ATTACK`, `AMP_DECAY`, `AMP_SLOPE` |
| 182–184 | `pitch_envelope_decay`, `_amount`, `_slope` | `PITCH_EG_DECAY`, `PITCH_EG_AMOUNT`, `PITCH_EG_SLOPE` |
| 187 | `instrument_drive` | `IM_SPECIAL_INSTRUMENT_DRIVE` |
| 198 | `lfo_rate` | `IM_SPECIAL_LFO_RATE` |
| 213 | `transient_wave` (menu) | `IM_SPECIAL_TRANSIENT_WAVE` |
| 215 | `transient_freq` | `IM_SPECIAL_TRANSIENT_FREQ` |

`osc1_noise_mix` (173) and `amp_attack_repeat` (181) stay untagged. The old
chain resolved an oscillator for `osc1_noise_mix` but matched no writer.

**`CymbalParameters.c`** (15 rows):

| Line | Key | Tag |
|---|---|---|
| 171, 172 | `osc1_pitch_coarse`, `osc1_pitch_fine` | `PITCH_COARSE \| OSC1`, `PITCH_FINE \| OSC1` |
| 174 | `osc2_pitch_coarse` | `IM_SPECIAL_PITCH_COARSE \| IM_SPECIAL_OSC2` |
| 177 | `osc3_pitch_coarse` | `IM_SPECIAL_PITCH_COARSE \| IM_SPECIAL_OSC3` |
| 179–181 | `filter_freq`, `_reso`, `_drive` | `FILTER_FREQ`, `FILTER_RESO`, `FILTER_DRIVE` |
| 182 | `filter_type` (menu) | `IM_SPECIAL_FILTER_TYPE` |
| 183–185 | `amp_envelope_attack`, `_decay`, `_slope` | `AMP_ATTACK`, `AMP_DECAY`, `AMP_SLOPE` |
| 189 | `instrument_drive` | `IM_SPECIAL_INSTRUMENT_DRIVE` |
| 200 | `lfo_rate` | `IM_SPECIAL_LFO_RATE` |
| 215 | `transient_wave` (menu) | `IM_SPECIAL_TRANSIENT_WAVE` |
| 217 | `transient_freq` | `IM_SPECIAL_TRANSIENT_FREQ` |

**`HiHatParameters.c`** (16 rows):

| Line | Key | Tag |
|---|---|---|
| 174, 175 | `osc1_pitch_coarse`, `osc1_pitch_fine` | `PITCH_COARSE \| OSC1`, `PITCH_FINE \| OSC1` |
| 177 | `osc2_pitch_coarse` | `IM_SPECIAL_PITCH_COARSE \| IM_SPECIAL_OSC2` |
| 180 | `osc3_pitch_coarse` | `IM_SPECIAL_PITCH_COARSE \| IM_SPECIAL_OSC3` |
| 182–184 | `filter_freq`, `_reso`, `_drive` | `FILTER_FREQ`, `FILTER_RESO`, `FILTER_DRIVE` |
| 185 | `filter_type` (menu) | `IM_SPECIAL_FILTER_TYPE` |
| 186 | `amp_envelope_attack` | `IM_SPECIAL_AMP_ATTACK` |
| 187 | `amp_envelope_decay` | `IM_SPECIAL_AMP_DECAY` (the writer routes a HiHat runtime to `decayClosed`) |
| 188 | `amp_envelope_decay_choke` | `IM_SPECIAL_HAT_DECAY_CHOKE` |
| 189 | `amp_envelope_slope` | `IM_SPECIAL_AMP_SLOPE` |
| 192 | `instrument_drive` | `IM_SPECIAL_INSTRUMENT_DRIVE` |
| 203 | `lfo_rate` | `IM_SPECIAL_LFO_RATE` |
| 218 | `transient_wave` (menu) | `IM_SPECIAL_TRANSIENT_WAVE` |
| 220 | `transient_freq` | `IM_SPECIAL_TRANSIENT_FREQ` |

In the Snare, Cymbal and HiHat tables, the short names in the Tag column mean
the full `IM_SPECIAL_*` constants.

### 4.7 MODIFY `Core/DSP/Effects/EffectParamRows.h:27`

```c
/* S073 Step 2: Effect rows never use the instrument special writers; the
** explicit IM_SPECIAL_NONE keeps -Wmissing-field-initializers quiet. */
#define EFFECT_BIND_NONE  { INSTRUMENT_BIND_NONE, 0u, 0u, IM_SPECIAL_NONE }
```

### 4.8 `Core/DSP/Instruments/InstrumentManager.c`

**MODIFY** the includes (lines 1–20): add `#include "config.h"` before
`#include "globals.h"` (line 19), so `DEV_MODE_DIAGNOSTIC` is always visible
to the self-check.

**ADD** after line 1853 (the end of `instrumentManager_osc()`):

```c
/*
 * Resolve a tagged oscillator selector to the live runtime member (S073 Step 2).
 *
 * What:       returns the OscInfo named by an IM_SPECIAL_OSC_* selector for
 *             the slot's live runtime type, or NULL where that type has no
 *             such oscillator:
 *             - Drum: osc1 -> osc, osc2 -> modOsc;
 *             - Snare: osc1 -> osc, noise -> noiseOsc;
 *             - Cymbal/HiHat: osc1 -> osc, osc2 -> modOsc, osc3 -> modOsc2.
 * Why:        the special writer used to find the oscillator by matching the
 *             key prefix ("osc1_" ...) against the runtime type. This table
 *             is exactly that prefix rule, keyed by the flash tag, so a
 *             descriptor applied during a Scene type handoff resolves (or
 *             fails to resolve) the same way it did before.
 * Inputs:     slot; selector = special & IM_SPECIAL_OSC_MASK.
 * Outputs:    OscInfo* or NULL.
 * Accessors:  instrumentManager_writeSpecialRuntime().
 * Affiliates: instrumentManager_osc() (the key-based twin, kept for
 *             instrumentManager_waveInterpTarget(), which runs only at
 *             modulation bind time).
 */
static OscInfo *instrumentManager_oscBySelector(uint8_t slot, uint8_t selector)
{
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM: {
        DrumVoice *voice = instrumentManager_drumRuntime(slot);
        if (!voice) return 0;
        if (selector == IM_SPECIAL_OSC1) return &voice->osc;
        if (selector == IM_SPECIAL_OSC2) return &voice->modOsc;
        return 0; }
    case INSTRUMENT_TYPE_SNR: {
        SnareVoice *voice = instrumentManager_snareRuntime(slot);
        if (!voice) return 0;
        if (selector == IM_SPECIAL_OSC1) return &voice->osc;
        if (selector == IM_SPECIAL_OSC_NOISE) return &voice->noiseOsc;
        return 0; }
    case INSTRUMENT_TYPE_CYM: {
        CymbalVoice *voice = instrumentManager_cymbalRuntime(slot);
        if (!voice) return 0;
        if (selector == IM_SPECIAL_OSC1) return &voice->osc;
        if (selector == IM_SPECIAL_OSC2) return &voice->modOsc;
        if (selector == IM_SPECIAL_OSC3) return &voice->modOsc2;
        return 0; }
    case INSTRUMENT_TYPE_HAT: {
        HiHatVoice *voice = instrumentManager_hihatRuntime(slot);
        if (!voice) return 0;
        if (selector == IM_SPECIAL_OSC1) return &voice->osc;
        if (selector == IM_SPECIAL_OSC2) return &voice->modOsc;
        if (selector == IM_SPECIAL_OSC3) return &voice->modOsc2;
        return 0; }
    default:
        return 0;
    }
}
```

**MODIFY** lines 2917–3098: replace the whole of
`instrumentManager_writeSpecialRuntime()` with:

```c
static uint8_t instrumentManager_writeSpecialRuntime(
    uint8_t slot, const ParamDescriptor *descriptor,
    instrument_param_value_t value)
{
    /*
     * Apply a descriptor row's DSP setter from its flash tag (S073 Step 2).
     *
     * What:       one switch on descriptor->runtime.special replaces the old
     *             file_key string chain. Each case calls exactly what the
     *             matching string branch called, with the same arguments. It
     *             also re-checks the same live-runtime conditions: the member
     *             exists, the HiHat caches are used only for a HiHat runtime,
     *             Drum's EG sync flag, and the pitch-EG amount for
     *             Drum/Snare only. So the result is identical even while a
     *             Scene type handoff renders an outgoing engine.
     * Why:        audit F2: 200-600 cycles of strcmp/strstr/strncmp per
     *             write, at 1,378 Hz per LFO target plus the Morph worker.
     *             The tag lookup is constant-time for every row, special or
     *             not (constant-CPU rule).
     * Inputs:     slot; the descriptor (NULL-safe); the descriptor-domain
     *             value.
     * Outputs:    1 when a special setter consumed the write, 0 when the
     *             caller must fall through to the generic binding write
     *             (untagged rows, or a runtime member that does not exist).
     * Accessors:  instrumentManager_writeRuntimeInternal() (public writes,
     *             LFO overlay and base restore, Morph, velocity, automation).
     * Affiliates: InstrumentManager.h instrument_special_writer_t,
     *             the *Parameters.c tags, instrumentManager_oscBySelector(),
     *             instrumentManager_specialTagSelfCheck(),
     *             tools/dsp_golden/check_special_tags.py.
     */
    const uint8_t special = descriptor ? descriptor->runtime.special
                                       : (uint8_t)IM_SPECIAL_NONE;
    const uint8_t byteValue = value;

    switch (special & IM_SPECIAL_WRITER_MASK) {
    case IM_SPECIAL_NOISE_FREQ: {
        OscInfo *osc = instrumentManager_oscBySelector(
            slot, (uint8_t)(special & IM_SPECIAL_OSC_MASK));
        if (!osc)
            return 0u;
        osc->freq = byteValue / 127.0f * 22000.0f;
        return 1u; }
    case IM_SPECIAL_PITCH_COARSE: {
        OscInfo *osc = instrumentManager_oscBySelector(
            slot, (uint8_t)(special & IM_SPECIAL_OSC_MASK));
        if (!osc)
            return 0u;
        osc->midiFreq = (uint16_t)((osc->midiFreq & 0x00ffu) |
                                   ((uint16_t)byteValue << 8));
        osc_recalcFreq(osc);
        return 1u; }
    case IM_SPECIAL_PITCH_FINE: {
        OscInfo *osc = instrumentManager_oscBySelector(
            slot, (uint8_t)(special & IM_SPECIAL_OSC_MASK));
        if (!osc)
            return 0u;
        osc->midiFreq = (uint16_t)((osc->midiFreq & 0xff00u) | byteValue);
        osc_recalcFreq(osc);
        return 1u; }
    case IM_SPECIAL_FILTER_FREQ: {
        ResonantFilter *filter = instrumentManager_filter(slot);
        if (!filter)
            return 0u;
        SVF_directSetFilterValue(filter,
            valueShaperF2F(byteValue / 127.0f, FILTER_SHAPER));
        return 1u; }
    case IM_SPECIAL_FILTER_RESO: {
        ResonantFilter *filter = instrumentManager_filter(slot);
        if (!filter)
            return 0u;
        SVF_setReso(filter, byteValue / 127.0f);
        return 1u; }
    case IM_SPECIAL_FILTER_DRIVE: {
        ResonantFilter *filter = instrumentManager_filter(slot);
        if (!filter)
            return 0u;
#if UNIT_GAIN_DRIVE
        filter->drive = byteValue / 127.0f;
#else
        SVF_setDrive(filter, byteValue);
#endif
        return 1u; }
    case IM_SPECIAL_FILTER_TYPE:
        if (!instrumentManager_filter(slot))
            return 0u;
        instrumentManager_writeParameter(
            (Parameter){ (void *)((uint8_t *)instrumentManager_runtimeInstance(slot) +
                                  descriptor->runtime.offset),
                         descriptor->runtime.parameter_type },
            (uint8_t)(byteValue + 1u));
        return 1u;
    case IM_SPECIAL_AMP_ATTACK: {
        SlopeEg2 *ampEg = instrumentManager_ampEg(slot);
        if (!ampEg)
            return 0u;
        slopeEg2_setAttack(ampEg, byteValue,
                           (uint8_t)(instrumentManager_slotType(slot) ==
                                     INSTRUMENT_TYPE_DRM ? AMP_EG_SYNC : 0u));
        return 1u; }
    case IM_SPECIAL_AMP_DECAY: {
        SlopeEg2 *ampEg = instrumentManager_ampEg(slot);
        if (!ampEg)
            return 0u;
        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_HAT) {
            HiHatVoice *voice = instrumentManager_hihatRuntime(slot);
            /* HiHat base decay keeps its dedicated closed-hat cache: the
            ** value used when slot 6 is triggered from track 6. */
            if (voice)
                voice->decayClosed = slopeEg2_calcDecay(byteValue);
            return 1u;
        }
        slopeEg2_setDecay(ampEg, byteValue,
                          (uint8_t)(instrumentManager_slotType(slot) ==
                                    INSTRUMENT_TYPE_DRM ? AMP_EG_SYNC : 0u));
        return 1u; }
    case IM_SPECIAL_HAT_DECAY_CHOKE: {
        HiHatVoice *voice;
        if (!instrumentManager_ampEg(slot) ||
            instrumentManager_slotType(slot) != INSTRUMENT_TYPE_HAT)
            return 0u;
        voice = instrumentManager_hihatRuntime(slot);
        /* HiHat choke decay keeps the open-hat cache used when the shared
        ** hihat slot is triggered from track 7. */
        if (voice)
            voice->decayOpen = slopeEg2_calcDecay(byteValue);
        return 1u; }
    case IM_SPECIAL_AMP_SLOPE: {
        SlopeEg2 *ampEg = instrumentManager_ampEg(slot);
        if (!ampEg)
            return 0u;
        slopeEg2_setSlope(ampEg, byteValue);
        return 1u; }
    case IM_SPECIAL_PITCH_EG_DECAY: {
        DecayEg *pitchEg = instrumentManager_pitchEg(slot);
        if (!pitchEg)
            return 0u;
        DecayEg_setDecay(pitchEg, byteValue);
        return 1u; }
    case IM_SPECIAL_PITCH_EG_SLOPE: {
        DecayEg *pitchEg = instrumentManager_pitchEg(slot);
        if (!pitchEg)
            return 0u;
        DecayEg_setSlope(pitchEg, byteValue);
        return 1u; }
    case IM_SPECIAL_PITCH_EG_AMOUNT:
        if (!instrumentManager_pitchEg(slot))
            return 0u;
        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_DRM) {
            DrumVoice *voice = instrumentManager_drumRuntime(slot);
            if (voice)
                voice->egPitchModAmount =
                    instrumentManager_pitchModAmount(byteValue);
        } else if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_SNR) {
            SnareVoice *voice = instrumentManager_snareRuntime(slot);
            if (voice)
                voice->egPitchModAmount =
                    instrumentManager_pitchModAmount(byteValue);
        }
        return 1u;
    case IM_SPECIAL_TRANSIENT_WAVE: {
        TransientGenerator *transient = instrumentManager_transient(slot);
        if (!transient)
            return 0u;
        transient_setWaveform(transient, byteValue);
        return 1u; }
    case IM_SPECIAL_TRANSIENT_FREQ: {
        TransientGenerator *transient = instrumentManager_transient(slot);
        if (!transient)
            return 0u;
        transient->pitch = 1.0f + ((byteValue / 33.9f) - 0.75f);
        return 1u; }
    case IM_SPECIAL_INSTRUMENT_DRIVE: {
        Distortion *distortion = instrumentManager_distortion(slot);
        if (!distortion)
            return 0u;
        setDistortionShape(distortion, byteValue);
        return 1u; }
    case IM_SPECIAL_LFO_RATE: {
        Lfo *lfo = instrumentManager_runtimeLfo(slot);
        if (!lfo)
            return 0u;
        lfo_setFreq(lfo, byteValue);
        return 1u; }
    case IM_SPECIAL_NONE:
    default:
        return 0u;
    }
}
```

**Equivalence notes**, to check at review:

| Situation | Old string chain | New switch |
|---|---|---|
| Key with an oscillator prefix, but the live type lacks that oscillator (handoff) | skipped the oscillator group; no later group matched → 0 | `oscBySelector()` returns NULL → 0 |
| `amp_envelope_decay_choke` on a non-HiHat runtime | no branch matched → 0 | `HAT_DECAY_CHOKE` returns 0 |
| `pitch_envelope_amount` on a Cymbal/HiHat runtime | the pitch EG is NULL for those types → 0 | 0 |
| A member resolved as NULL | the group was skipped → 0 | 0 |

The old `if (!key) return 0u;` is covered by the NULL-descriptor → tag
`IM_SPECIAL_NONE` → 0.

**ADD** directly after the new `instrumentManager_writeSpecialRuntime()`:

```c
#if DEV_MODE_DIAGNOSTIC
/*
 * Original key rules as a pure classifier (S073 Step 2, diagnostic only).
 *
 * What:       returns the tag the pre-S073 string chain would have chosen
 *             for (table type, file_key), assuming the live runtime matches
 *             the table type and every member exists. It mirrors the
 *             old instrumentManager_osc() prefix table and the old branch
 *             order (the HiHat choke key before the generic amp keys).
 * Why:        instrumentManager_specialTagSelfCheck() compares it with the
 *             flash tags on the device. It is kept only in diagnostic
 *             builds, so production carries no string code for this path.
 * Inputs:     instrument type; key (NULL-safe).
 * Outputs:    IM_SPECIAL_* | IM_SPECIAL_OSC_*.
 * Accessors:  instrumentManager_specialTagSelfCheck().
 * Affiliates: tools/dsp_golden/check_special_tags.py (the same rules on the
 *             host).
 */
static uint8_t instrumentManager_classifySpecialKey(instrument_type_t type,
                                                    const char *key)
{
    uint8_t osc = 0xFFu;

    if (!key)
        return IM_SPECIAL_NONE;
    if (strncmp(key, "osc1_", 5) == 0)
        osc = IM_SPECIAL_OSC1;
    else if (strncmp(key, "osc2_", 5) == 0 &&
             (type == INSTRUMENT_TYPE_DRM || type == INSTRUMENT_TYPE_CYM ||
              type == INSTRUMENT_TYPE_HAT))
        osc = IM_SPECIAL_OSC2;
    else if (strncmp(key, "osc3_", 5) == 0 &&
             (type == INSTRUMENT_TYPE_CYM || type == INSTRUMENT_TYPE_HAT))
        osc = IM_SPECIAL_OSC3;
    else if (strncmp(key, "noise_", 6) == 0 && type == INSTRUMENT_TYPE_SNR)
        osc = IM_SPECIAL_OSC_NOISE;
    if (osc != 0xFFu) {
        if (strcmp(key, "noise_freq") == 0)  return IM_SPECIAL_NOISE_FREQ | osc;
        if (strstr(key, "pitch_coarse"))     return IM_SPECIAL_PITCH_COARSE | osc;
        if (strstr(key, "pitch_fine"))       return IM_SPECIAL_PITCH_FINE | osc;
    }
    if (strcmp(key, "filter_freq") == 0)     return IM_SPECIAL_FILTER_FREQ;
    if (strcmp(key, "filter_reso") == 0)     return IM_SPECIAL_FILTER_RESO;
    if (strcmp(key, "filter_drive") == 0)    return IM_SPECIAL_FILTER_DRIVE;
    if (strcmp(key, "filter_type") == 0)     return IM_SPECIAL_FILTER_TYPE;
    if (type == INSTRUMENT_TYPE_HAT &&
        strcmp(key, "amp_envelope_decay_choke") == 0)
        return IM_SPECIAL_HAT_DECAY_CHOKE;
    if (strcmp(key, "amp_envelope_attack") == 0) return IM_SPECIAL_AMP_ATTACK;
    if (strcmp(key, "amp_envelope_decay") == 0)  return IM_SPECIAL_AMP_DECAY;
    if (strcmp(key, "amp_envelope_slope") == 0)  return IM_SPECIAL_AMP_SLOPE;
    if (type == INSTRUMENT_TYPE_DRM || type == INSTRUMENT_TYPE_SNR) {
        if (strcmp(key, "pitch_envelope_decay") == 0)  return IM_SPECIAL_PITCH_EG_DECAY;
        if (strcmp(key, "pitch_envelope_slope") == 0)  return IM_SPECIAL_PITCH_EG_SLOPE;
        if (strcmp(key, "pitch_envelope_amount") == 0) return IM_SPECIAL_PITCH_EG_AMOUNT;
    }
    if (strcmp(key, "transient_wave") == 0)   return IM_SPECIAL_TRANSIENT_WAVE;
    if (strcmp(key, "transient_freq") == 0)   return IM_SPECIAL_TRANSIENT_FREQ;
    if (strcmp(key, "instrument_drive") == 0) return IM_SPECIAL_INSTRUMENT_DRIVE;
    if (strcmp(key, "lfo_rate") == 0)         return IM_SPECIAL_LFO_RATE;
    return IM_SPECIAL_NONE;
}

uint8_t instrumentManager_specialTagSelfCheck(void)
{
    /*
     * Count descriptor rows whose flash tag disagrees with the original key
     * rules (S073 Step 2, diagnostic only). See the prototype in
     * InstrumentManager.h for the full contract. No RAM: computed on demand.
     */
    uint8_t mismatches = 0u;
    uint8_t e;

    for (e = 0u; e < instrumentManager_registryCount(); e++) {
        const instrument_registry_entry_t *entry =
            instrumentManager_registryEntryAt(e);
        uint8_t i;

        if (!entry)
            continue;
        for (i = 0u; i < entry->descriptor_count; i++) {
            const ParamDescriptor *d = &entry->descriptors[i];
            if (d->runtime.special !=
                instrumentManager_classifySpecialKey(entry->type, d->file_key) &&
                mismatches < 9u)
                mismatches++;
        }
    }
    return mismatches;
}
#endif
```

### 4.9 MODIFY `main.c:302–324` (`boot_showFxBufDiagnostic()`, inside `#if DEV_MODE_DIAGNOSTIC`)

- **MODIFY** line 305: change `char row1[17] = "FxBf 000K u00   ";` to
  `char row1[17] = "FxBf 000K u00 s0";`.
- **ADD** after line 311 (`boot_formatDec(&row1[11], fxbuf_unitsInUse(), 2u);`):

```c
    /*
     * S073 Step 2 special-tag self-check digit.
     *
     * What:       row 1 columns 14-15 show "s<n>", where n is the number of
     *             descriptor rows whose flash special-writer tag disagrees
     *             with the original key rules (0 = pass, clamped to 9).
     * Why:        on-device proof that the tag switch reproduces the old
     *             string chain, without a new screen or boot delay.
     * Inputs:     instrumentManager_specialTagSelfCheck().
     * Outputs:    row1[15].
     * Accessors:  diagnostic boot only.
     * Affiliates: tools/dsp_golden/check_special_tags.py.
     */
    row1[15] = (char)('0' + (int)instrumentManager_specialTagSelfCheck());
```

### 4.10 ADD `tools/dsp_golden/check_special_tags.py`

```python
#!/usr/bin/env python3
"""
Verify the S073 Step 2 special-writer tags in the four instrument tables.

What:       parses every ROW*/ROW_MENU* row of the Drum, Snare, Cymbal and
            HiHat *Parameters.c tables, computes the tag the original
            file_key string chain would select (the same rules as
            instrumentManager_classifySpecialKey()), and compares it with the
            tag written in the row (IM_SPECIAL_NONE when the row uses a plain
            ROW/ROW_MENU).
Why:        host proof, before any device time, that the tags are exactly
            the old behaviour.
Inputs:     the four tables, from the repository root.
Outputs:    one line per mismatch; "special tags OK (N rows)"; exit 1 on any
            mismatch.
Accessors:  `make -C tools/dsp_golden special_tags`.
Affiliates: InstrumentManager.c instrumentManager_classifySpecialKey().
"""
import re
import sys

TABLES = {
    'DRM': 'Core/DSP/Instruments/Drum/DrumParameters.c',
    'SNR': 'Core/DSP/Instruments/Snare/SnareParameters.c',
    'CYM': 'Core/DSP/Instruments/Cymbal/CymbalParameters.c',
    'HAT': 'Core/DSP/Instruments/HiHat/HiHatParameters.c',
}
ROW = re.compile(r'^\s*(ROW[A-Z_]*)\("([^"]+)"(.*)\),?\s*$')


def expected(t, key):
    osc = None
    if key.startswith('osc1_'):
        osc = 'IM_SPECIAL_OSC1'
    elif key.startswith('osc2_') and t in ('DRM', 'CYM', 'HAT'):
        osc = 'IM_SPECIAL_OSC2'
    elif key.startswith('osc3_') and t in ('CYM', 'HAT'):
        osc = 'IM_SPECIAL_OSC3'
    elif key.startswith('noise_') and t == 'SNR':
        osc = 'IM_SPECIAL_OSC_NOISE'
    if osc:
        if key == 'noise_freq':
            return {'IM_SPECIAL_NOISE_FREQ', osc}
        if 'pitch_coarse' in key:
            return {'IM_SPECIAL_PITCH_COARSE', osc}
        if 'pitch_fine' in key:
            return {'IM_SPECIAL_PITCH_FINE', osc}
    simple = {
        'filter_freq': 'FILTER_FREQ', 'filter_reso': 'FILTER_RESO',
        'filter_drive': 'FILTER_DRIVE', 'filter_type': 'FILTER_TYPE',
        'amp_envelope_attack': 'AMP_ATTACK', 'amp_envelope_decay': 'AMP_DECAY',
        'amp_envelope_slope': 'AMP_SLOPE', 'transient_wave': 'TRANSIENT_WAVE',
        'transient_freq': 'TRANSIENT_FREQ', 'instrument_drive': 'INSTRUMENT_DRIVE',
        'lfo_rate': 'LFO_RATE'}
    if t == 'HAT' and key == 'amp_envelope_decay_choke':
        return {'IM_SPECIAL_HAT_DECAY_CHOKE'}
    if t in ('DRM', 'SNR'):
        simple.update({'pitch_envelope_decay': 'PITCH_EG_DECAY',
                       'pitch_envelope_slope': 'PITCH_EG_SLOPE',
                       'pitch_envelope_amount': 'PITCH_EG_AMOUNT'})
    if key in simple:
        return {'IM_SPECIAL_' + simple[key]}
    return set()


def actual(macro, rest):
    if not macro.endswith('_SPECIAL'):
        return set()
    return {p.strip() for p in rest.rsplit(',', 1)[1].split('|')}


def main():
    bad = rows = 0
    for t, path in TABLES.items():
        for n, line in enumerate(open(path), 1):
            m = ROW.match(line)
            if not m:
                continue
            rows += 1
            want, got = expected(t, m.group(2)), actual(m.group(1), m.group(3))
            if want != got:
                bad += 1
                print(f'{path}:{n}: {m.group(2)} expected {sorted(want)} got {sorted(got)}')
    print(f'special tags {"OK" if not bad else "FAILED"} ({rows} rows, {bad} mismatches)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
```

Every tagged oscillator row names its selector explicitly, including
`IM_SPECIAL_OSC1` (value `0x00`), so the check compares the written
expression, not only its value.

Makefile target:

```make
special_tags:
	cd $(ROOT) && python3 tools/dsp_golden/check_special_tags.py
```

### 4.11 Gate 2

- `make -C tools/dsp_golden special_tags` → OK.
  - Expected tagged-row counts: Drum 17, Snare 17, Cymbal 15, HiHat 16.
- Firmware: builds without warnings; the `_Static_assert`s hold; `data`/`bss`
  unchanged; the flash delta is expected ≤ 0.
- **Diagnostic image (`DEV_MODE_DIAGNOSTIC 1`, user):** the FxBf boot screen
  shows `s0`.
- **Hardware regression (user):**
  - Instrument Load, Kit Load, Scene switch;
  - LFO rebind and per-voice Morph;
  - the HiHat closed/choke decay pair;
  - the slot-6 track-7 alternate decay;
  - LFOs on `osc1_pitch_coarse`, `filter_freq`, `amp_envelope_decay` and
    `lfo_rate`.

---

## 5. Step 3a — DMA pack: word stores (S0)

### 5.1 MODIFY `Core/Hardware/AudioCodecManager.c:215–216`

```c
/*
 * DMA output buffers, word-aligned (S073 Step 3a).
 *
 * What:       unchanged contents, size and section. Only aligned(4) is added.
 * Why:        pack_half() now writes each channel frame as one 32-bit store.
 *             The arrays are int16_t, so without this attribute they would
 *             be only 2-byte aligned.
 * Inputs/Outputs: written by pack_half() in the DMA ISR; read by DMA1 streams
 *             4/7 in halfword mode.
 * Accessors:  pack_audio_half(), the DMA setup (M0AR at lines ~539/547),
 *             audioCodec suspend/resume memsets (lines ~617/675).
 * Affiliates: AudioCodecManager.h externs (unchanged: alignment is a property
 *             of the definition), STM32F765VIHx_FLASH.ld .dma_nocache.
 */
volatile int16_t dma_buffer  [AUDIO_DMA_FRAMES * 8] __attribute__((section(".dma_nocache"), aligned(4)));
volatile int16_t dma_buffer2 [AUDIO_DMA_FRAMES * 8] __attribute__((section(".dma_nocache"), aligned(4)));
```

### 5.2 MODIFY `AudioCodecManager.c:382–395` (`pack_half()`)

```c
/*
 * One 32-bit DMA word per channel frame (S073 Step 3a).
 *
 * What:       builds the word whose little-endian memory image equals the two
 *             halfword stores used before: MSW at the lower address, then
 *             LSW. That is the 24-bit-in-32 frame rotated by 16.
 * Why:        audit F6. Halves the stores into the non-cacheable DMA region
 *             (768 → 384 per ISR). Bit-identical output (S0).
 * Inputs:     s24, a signed 24-bit sample from sampleMix_toS24().
 * Outputs:    the packed word.
 * Accessors:  pack_half().
 * Affiliates: the I2S 24-in-32 halfword-mode DMA configuration;
 *             tools/dsp_golden/test_pack.c.
 */
static inline uint32_t pack_frameWord(int32_t s24)
{
    const uint32_t frame = ((uint32_t)(s24 & 0x00FFFFFF)) << 8;
    return (frame >> 16) | (frame << 16);
}

/* Word view of the halfword DMA buffers; may_alias keeps the store legal
** under strict aliasing (the buffers are declared int16_t). */
typedef uint32_t __attribute__((may_alias)) dma_word_t;

static void pack_half(volatile int16_t *dst, const sample_mx_t *src)
{
    /*
     * Pack one DMA half with word stores (S073 Step 3a).
     *
     * What:       for each frame, one 32-bit store per channel (left, then
     *             right), replacing four halfword stores. The memory image is
     *             identical to the old MSW/LSW/MSW/LSW sequence.
     * Why:        audit F6: fewer strongly-ordered (and after Step 3b,
     *             bufferable) stores in the DMA ISR. Constant cost per frame.
     * Inputs:     dst, a 4-byte-aligned half of dma_buffer/dma_buffer2;
     *             src, the rendered sample_mx_t stereo slot.
     * Outputs:    AUDIO_DMA_FRAMES x 2 words in dst.
     * Accessors:  pack_audio_half() (DMA1 stream 4 HT/TC ISR).
     * Affiliates: pack_frameWord(), sampleMix_toS24(),
     *             tools/dsp_golden/test_pack.c.
     */
    volatile dma_word_t *dstw = (volatile dma_word_t *)dst;

    for (uint32_t i = 0; i < AUDIO_DMA_FRAMES; i++) {
        dstw[2*i + 0] = pack_frameWord(sampleMix_toS24(src[2*i + 0]));
        dstw[2*i + 1] = pack_frameWord(sampleMix_toS24(src[2*i + 1]));
    }
}
```

`pack_audio_half()` (lines 397–414) is unchanged in 3a. Its offsets
`AUDIO_DMA_FRAMES * 4` halfwords are 768 bytes, so they are word-aligned.

### 5.3 ADD `tools/dsp_golden/test_pack.c`

It extracts the frozen `sampleMix_toS24` and `pack_half` (renamed `old_`) and
the current `pack_frameWord`, `pack_half` (renamed `new_`), plus the
`dma_word_t` typedef. It runs 100,000 random blocks of `AUDIO_DMA_FRAMES × 2`
`sample_mx_t` values, including values beyond ±2^23 to exercise the clamp, and
fixed patterns (0, ±full scale, alternating signs). It packs into two 4-aligned
buffers and `memcmp`s them.

- **Pass:** 0 differing bytes.
- `test_pack.c` defines `typedef uint32_t __attribute__((may_alias)) dma_word_t;`
  itself, before including `pack_new.c`, because the typedef is not a
  function or object and is not extracted.

```make
# ---- Step 3a (Gate 3a): pack_half old vs new, byte-identical.
pack: | $(GEN)
	$(EX) --src $(FROZEN)/Core/Hardware/AudioCodecManager.c --lines 370-371 \
	  --func sampleMix_toS24 --func pack_half --rename pack_half=old_pack_half \
	  --out $(GEN)/pack_old.c
	$(EX) --src $(ROOT)/Core/Hardware/AudioCodecManager.c --func pack_frameWord \
	  --func pack_half --rename pack_half=new_pack_half --out $(GEN)/pack_new.c
	$(HOSTCC) $(HOSTFLAGS) test_pack.c -o build/pack -lm
	./build/pack
```

### 5.4 Gate 3a

- `make -C tools/dsp_golden pack` → 0 differing bytes.
- The disassembly of `DMA1_Stream4_IRQHandler` in `build/lxr02.elf` shows word
  stores (`str`) with `ror` in `pack_half`, and no `strh`.
- Firmware: `data`/`bss` unchanged.
- **Hardware (user):** audio on both DAC outputs is unchanged by ear on the
  kits; no new underruns.

---

## 6. Step 3b — DMA region: Normal non-cacheable (S0)

### 6.1 MODIFY `Core/Hardware/clocks.c:155–158` (the region comment in the header block)

```c
    ** Region 1: DMA buffers (first 4KB of SRAM1) — Normal, non-cacheable
    **   (S073 Step 3b; Strongly-Ordered before). SIZE=11 → 4KB,
    **   0x20020000–0x20021000. Higher region number wins on overlap, so this
    **   overrides Region 0's write-through cacheable attribute. Stores are
    **   bufferable, and nothing is cached, so DMA coherency is unchanged.
```

### 6.2 MODIFY `clocks.c:187–193` (Region 1 setup)

```c
        /*
         * Region 1: DMA buffers (4KB) — Normal, non-cacheable (S073 Step 3b).
         *
         * What:       TEX=001 C=0 B=0 S=1 (Normal, non-cacheable, shareable),
         *             AP=011 full access, XN=1. RASR = XN | AP | TEX(1) | S |
         *             SIZE(11) | ENABLE.
         * Why:        audit F6. Strongly-Ordered made every DMA-buffer store
         *             wait for completion. Normal non-cacheable lets the
         *             write buffer absorb the ISR's stores, while no data is
         *             ever cached, so DMA coherency is unchanged.
         *             pack_audio_half() ends with a DSB, so each half is
         *             complete before the ISR returns.
         * Inputs:     none (boot-time MPU setup, MPU disabled around it).
         * Outputs:    the MPU region 1 attributes.
         * Accessors:  sysclk_init() at boot.
         * Affiliates: .dma_nocache in STM32F765VIHx_FLASH.ld (the 3,100-byte
         *             DMA buffers plus adc_dma_buf); the remaining ~996 bytes
         *             of the 4 KB region hold ordinary .data/.bss variables
         *             (for example buttonHandler state at 0x20020c20), which
         *             were Strongly-Ordered only by accident and now become
         *             Normal non-cacheable, a relaxation with no correctness
         *             effect; pack_audio_half() DSB; endlessPots.c
         *             adc_dma_buf (CPU reads stay uncached).
         */
        MPU_RNR  = 1;
        MPU_RBAR = 0x20020000UL;
        MPU_RASR = (1UL << 28) | (3UL << 24) | (1UL << 19) | (1UL << 18) | (11UL << 1) | 1UL;
```

(Line 192, `MPU_RBAR = 0x20020000UL;`, is unchanged and repeated here for
context.)

### 6.3 ADD `Core/Hardware/AudioCodecManager.c`, at the end of `pack_audio_half()` (after line 413, `pack_half(dst2, audioOutBuffer2[slot]);`)

```c
    /*
     * Complete the packed half before the ISR returns (S073 Step 3b).
     *
     * What:       data synchronisation barrier after both pack_half() calls.
     * Why:        region 1 is now Normal non-cacheable, so stores may sit in
     *             the write buffer. The DMA reads this half only after the
     *             other half plays, so a DSB here is a cheap guarantee that
     *             the whole half is in SRAM before the next DMA event.
     * Inputs/Outputs: none.
     * Accessors:  DMA1_Stream4_IRQHandler() via pack_audio_half().
     * Affiliates: clocks.c MPU region 1.
     */
    __asm volatile("dsb" ::: "memory");
```

### 6.4 MODIFY `STM32F765VIHx_FLASH.ld` (the `.dma_nocache` comment, "MPU region 1 marks this 4KB as Device memory…")

```
    /* DMA buffers — non-cacheable region at start of SRAM1.
    ** MPU region 1 marks this 4KB as Normal non-cacheable (S073 Step 3b;
    ** Strongly-Ordered before), so D-cache never caches DMA-visible data.
    ** Must stay ≤ 4KB total. */
```

### 6.5 Gate 3b

- Firmware: builds; `data`/`bss` unchanged.
- The disassembly of `pack_audio_half` ends with `dsb`.
- **Hardware (user):**
  - audio on both DACs is unchanged;
  - the sliders and endless pots read correctly (the ADC DMA shares the
    region);
  - no new underruns under the worst-case Scene;
  - SD, MIDI and button behaviour are normal (their state variables sit in
    the region's tail).

---

## 7. Step 4 — Fused voice post-chain (S0)

### 7.1 ADD `Core/DSPAudio/BufferTools.h`, after line 67 (end of `bufferTool_satSub16()`)

```c
/*
 * Register form of "store a float into an int16_t" (S073 Step 4).
 *
 * What:       converts to int32 (VCVT, round toward zero), then keeps the low
 *             16 bits, exactly what `int16_t v = floatExpr;` or
 *             `buf[i] *= gain;` compiled to (VCVT.S32.F32 then STRH).
 * Why:        the fused post-chain must feed each stage's value to the next
 *             stage in a register, bit-identically to the old
 *             store-then-reload through the int16 block (S0).
 * Inputs:     x, a float.
 * Outputs:    the int16 value the old store would have held.
 * Accessors:  voicePostChain.h, bufferTool_addGain(),
 *             bufferTool_addGainInterpolated(), distortion_curveSample16().
 * Affiliates: tools/dsp_golden/test_postchain.c and armcheck/postchain.c
 *             (which confirm the VCVT and narrowing sequence).
 */
static inline int16_t bufferTool_floatToInt16Store(const float x)
{
	return (int16_t)(int32_t)x;
}

/*
 * Linear per-sample gain ramp (S073 Step 4).
 *
 * What:       lastGain + (i*inv_size)*(gain - lastGain), with the same
 *             operand order as the original bufferTool_addGainInterpolated()
 *             body.
 * Why:        one definition of the ramp for the old helper and the fused
 *             Drum post-chain, so both compile the identical expression.
 * Inputs:     i, the sample index; inv_size = 1/(size-1); the target gain;
 *             the previous block's gain.
 * Outputs:    the gain for sample i.
 * Accessors:  bufferTool_addGainInterpolated(), voicePost_drum().
 * Affiliates: DrumVoice.c lastGain / ampFilterInput bookkeeping.
 */
static inline float bufferTool_interpolatedGain(const uint8_t i,
		const float inv_size, const float gain, const float lastGain)
{
	const float frac = i * inv_size;
	return lastGain + frac*(gain - lastGain);
}
```

### 7.2 MODIFY `Core/DSPAudio/BufferTools.c:132–139` and `:150–160`

```c
void bufferTool_addGain(int16_t* buf, const float gain, const uint8_t size)
{
	/* S073 Step 4: body expressed through the shared store helper; the
	** result is identical to `buf[i] *= gain`. After Step 4 the voices no
	** longer call this; it is kept for API continuity (LTO drops it if
	** unused). */
	uint8_t i;
	for(i=0;i<size;i++)
	{
		buf[i] = bufferTool_floatToInt16Store(buf[i] * gain);
	}
}
```

```c
void bufferTool_addGainInterpolated(int16_t* buf, const float gain, const float lastGain, const uint8_t size)
{
	/* S073 Step 4: body expressed through the shared ramp and store helpers,
	** the same expressions voicePost_drum() uses. */
	uint8_t i;
	const float inv_size = 1.f/(size-1.f);
	for(i=0;i<size;i++)
	{
		buf[i] = bufferTool_floatToInt16Store(
			buf[i] * bufferTool_interpolatedGain(i, inv_size, gain, lastGain));
	}
}
```

### 7.3 ADD `Core/DSPAudio/distortion.h`, after line 53 (the `distortion_calcSampleFloat` prototype); also ADD `#include <math.h>` and `#include <stdint.h>` after line 40

```c
/*
 * Distortion curve for one int16 sample (S073 Step 4).
 *
 * What:       x = in/32767; y = (1+k)*x / (1+k*|x|); the result is y*32767
 *             stored as int16, with exactly the expressions of the original
 *             calcDistBlock() body. The division is kept (no reciprocal
 *             rewrite) and there is no shape-0 bypass.
 * Why:        the fused voice post-chain applies the curve in a register;
 *             the user requires the distortion to sound identical (S0), and
 *             the constant-CPU rule forbids skipping it at k = 0.
 * Inputs:     dist->shape (k, from setDistortionShape()); in, an int16
 *             sample.
 * Outputs:    the distorted int16 sample.
 * Accessors:  calcDistBlock(), voicePostChain.h.
 * Affiliates: bufferTool_floatToInt16Store(),
 *             tools/dsp_golden/test_postchain.c.
 */
static inline int16_t distortion_curveSample16(const Distortion *dist,
                                               const int16_t in)
{
	float x = in/32767.f;
	x = (1+dist->shape)*x/(1+dist->shape*fabsf(x));
	return (int16_t)(int32_t)(x*32767);
}
```

### 7.4 MODIFY `Core/DSPAudio/distortion.c:61–70` (`calcDistBlock()`)

```c
INITCM_EFFECT_NOINLINE void calcDistBlock(const Distortion *dist, int16_t* buf, const uint8_t size)
{
	/* S073 Step 4: the per-sample curve lives in distortion_curveSample16()
	** so the fused voice post-chains and this block helper share one
	** definition. Output identical to the previous loop. */
	uint8_t i;
	for(i=0;i<size;i++)
	{
		buf[i] = distortion_curveSample16(dist, buf[i]);
	}
}
```

### 7.5 ADD `Core/DSPAudio/voicePostChain.h` (new file)

Use the project licence header, `Created on: 28.09.2026`, then:

```c
/*
 * Fused voice post-chains (S073 Step 4).
 *
 * What:       one loop per engine that performs, per sample, the same stages
 *             the engines used to run as separate int16 passes (amp EG ramp,
 *             velocity gain, mix/add, distortion), feeding each stage's
 *             int16 result to the next in a register.
 * Why:        audit F3/F4. The separate passes cost one load, one
 *             conversion and one truncating store each per sample. The user
 *             requires bit-identical sound (S0) and constant CPU, so:
 *             - every stage still runs for every sample;
 *             - every int16 conversion/saturation point is kept exactly;
 *             - the distortion division stays, with no shape-0 bypass;
 *             - the old `if (volumeMod)` choices become gain selects the
 *               caller makes once per block (a multiply by the selected
 *               gain always runs).
 * Inputs:     per function, below.
 * Outputs:    the post-chain int16 block in place.
 * Accessors:  DrumVoice.c, Snare.c, CymbalVoice.c, HiHat.c calcSyncBlock
 *             render functions.
 * Affiliates: BufferTools.h (satAdd16, floatToInt16Store,
 *             interpolatedGain), distortion.h (distortion_curveSample16),
 *             tools/dsp_golden/test_postchain.c and armcheck/postchain.c.
 */
#ifndef VOICE_POST_CHAIN_H_
#define VOICE_POST_CHAIN_H_

#include <stdint.h>
#include "BufferTools.h"
#include "distortion.h"

/*
 * Drum: amp EG ramp -> velocity gain -> distortion (S073 Step 4).
 *
 * Inputs:  buf, the filtered voice block; gain/lastGain, the amp EG ramp
 *          endpoints (ampFilterInput, lastGain); veloGain =
 *          volumeMod ? velo : 1.0f (bit-identical to the old optional pass,
 *          because int16 x 1.0f converts back unchanged); dist; size.
 * Outputs: buf.
 */
static inline void voicePost_drum(int16_t *buf, const float gain,
		const float lastGain, const float veloGain,
		const Distortion *dist, const uint8_t size)
{
	uint8_t i;
	const float inv_size = 1.f/(size-1.f);
	for(i=0;i<size;i++)
	{
		const int16_t eg = bufferTool_floatToInt16Store(
			buf[i] * bufferTool_interpolatedGain(i, inv_size, gain, lastGain));
		const int16_t vel = bufferTool_floatToInt16Store(eg * veloGain);
		buf[i] = distortion_curveSample16(dist, vel);
	}
}

/*
 * Snare: mix gain -> saturating add of the second source -> amp gain ->
 * distortion (S073 Step 4).
 *
 * Inputs:  buf, the filtered noise block; add, the oscillator/transient
 *          block (transBuf); mix, the voice mix; ampGain = volumeMod ?
 *          velo*egValueOscVol : egValueOscVol (the same float product the
 *          old loop formed per sample); dist; size.
 * Outputs: buf.
 */
static inline void voicePost_mixAddGainDist(int16_t *buf, const int16_t *add,
		const float mix, const float ampGain,
		const Distortion *dist, const uint8_t size)
{
	uint8_t j;
	for(j=0;j<size;j++)
	{
		const int16_t m = bufferTool_floatToInt16Store(buf[j] * mix);
		const int16_t s = bufferTool_satAdd16(m, add[j]);
		const int16_t g = bufferTool_floatToInt16Store(s * ampGain);
		buf[j] = distortion_curveSample16(dist, g);
	}
}

/*
 * Cymbal/HiHat: saturating add of the transient -> amp gain -> distortion
 * (S073 Step 4).
 *
 * Inputs:  buf, the filtered FM block; add, the transient block (mod/mod1);
 *          ampGain as for Snare; dist; size.
 * Outputs: buf.
 */
static inline void voicePost_addGainDist(int16_t *buf, const int16_t *add,
		const float ampGain, const Distortion *dist, const uint8_t size)
{
	uint8_t j;
	for(j=0;j<size;j++)
	{
		const int16_t s = bufferTool_satAdd16(buf[j], add[j]);
		const int16_t g = bufferTool_floatToInt16Store(s * ampGain);
		buf[j] = distortion_curveSample16(dist, g);
	}
}

#endif /* VOICE_POST_CHAIN_H_ */
```

### 7.6 Voice render changes

**`Core/DSP/Instruments/Drum/DrumVoice.c`**

- **ADD** `#include "voicePostChain.h"` after line 47 (`#include "InstrumentManager.h"`).
- **MODIFY** lines 336–351 (from `//attentuate main OSCs by amp EG` through
  the `#endif` after `calcDistBlock`):

```c
#if defined(USE_AMP_FILTER) || (USE_FILTER_DRIVE != 0)
	/* Non-default legacy configurations (neither macro is defined in this
	** firmware) keep their original separate passes. */
	//attentuate main OSCs by amp EG
#ifdef USE_AMP_FILTER
	bufferTool_multiplyWithFloatBufferDithered(&voice->dither, buf,voice->volEgValueBlock,size);
#else
	bufferTool_addGainInterpolated(buf,voice->ampFilterInput, voice->lastGain, size);
#endif
	//MIDI velocity
	if(voice->volumeMod)
	{
		bufferTool_addGain(buf,voice->velo,size);
	}
#if (USE_FILTER_DRIVE == 0)
	calcDistBlock(&voice->distortion,buf,size);
#endif
#else
	/*
	 * Fused Drum post-chain (S073 Step 4).
	 *
	 * What:       amp EG ramp, velocity gain and distortion in one pass
	 *             (voicePost_drum()), replacing three int16 passes.
	 * Why:        audit F4: two fewer load/convert/store passes per sample.
	 *             Bit-identical (S0). The velocity stage always runs:
	 *             gain 1.0 when velocity-to-volume is off, which gives the
	 *             same bits as skipping it but keeps the cost constant.
	 * Inputs:     buf (filtered block), voice->ampFilterInput,
	 *             voice->lastGain, voice->volumeMod/velo,
	 *             voice->distortion.
	 * Outputs:    buf, pre-volume (the mixer applies voice->vol).
	 * Accessors:  this render function, via the InstrumentManager dispatch.
	 * Affiliates: voicePostChain.h; the lastGain updates at DrumVoice.c:256
	 *             and :260; tools/dsp_golden/test_postchain.c.
	 */
	voicePost_drum(buf, voice->ampFilterInput, voice->lastGain,
	               voice->volumeMod ? voice->velo : 1.0f,
	               &voice->distortion, size);
#endif
```

**`Core/DSP/Instruments/Snare/Snare.c`**

- **ADD** `#include "voicePostChain.h"` after line 42.
- **MODIFY** lines 239–262 (from `uint8_t j;` through
  `calcDistBlock(&voice->distortion,buf,size);`):

```c
	/*
	 * Fused Snare post-chain (S073 Step 4).
	 *
	 * What:       mix gain, saturating add of the oscillator/transient block,
	 *             amp EG (x velocity) and distortion in one pass
	 *             (voicePost_mixAddGainDist()), replacing the two
	 *             volumeMod-selected loops plus calcDistBlock().
	 * Why:        audit F4: one fewer load/convert/store pass per sample and
	 *             a single constant-cost loop. Bit-identical (S0): ampGain is
	 *             the same float product the old loop formed per sample.
	 * Inputs:     buf (filtered noise), transBuf, voice->mix,
	 *             voice->volumeMod/velo/egValueOscVol, voice->distortion.
	 * Outputs:    buf, pre-volume.
	 * Accessors:  this render function.
	 * Affiliates: voicePostChain.h; tools/dsp_golden/test_postchain.c.
	 */
	{
		const float ampGain = voice->volumeMod
			? voice->velo * voice->egValueOscVol
			: voice->egValueOscVol;
		voicePost_mixAddGainDist(buf, transBuf, voice->mix, ampGain,
		                         &voice->distortion, size);
	}
```

**`Core/DSP/Instruments/Cymbal/CymbalVoice.c`**

- **ADD** `#include "voicePostChain.h"` after line 41.
- **MODIFY** lines 252–272 (from `uint8_t j;` through `calcDistBlock(...)`):

```c
	/*
	 * Fused Cymbal post-chain (S073 Step 4).
	 *
	 * What:       saturating add of the transient block, amp EG (x velocity)
	 *             and distortion in one pass (voicePost_addGainDist()),
	 *             replacing the two volumeMod-selected loops plus
	 *             calcDistBlock().
	 * Why:        audit F4. Bit-identical (S0), constant cost.
	 * Inputs:     buf (filtered FM block), mod (transient),
	 *             voice->volumeMod/velo/egValueOscVol, voice->distortion.
	 * Outputs:    buf, pre-volume.
	 * Accessors:  this render function.
	 * Affiliates: voicePostChain.h; tools/dsp_golden/test_postchain.c.
	 */
	{
		const float ampGain = voice->volumeMod
			? voice->velo * voice->egValueOscVol
			: voice->egValueOscVol;
		voicePost_addGainDist(buf, mod, ampGain, &voice->distortion, size);
	}
```

**`Core/DSP/Instruments/HiHat/HiHat.c`**

- **ADD** `#include "voicePostChain.h"` after line 54.
- **MODIFY** lines 270–291 (from `uint8_t j;` through `calcDistBlock(...)`).
  The replacement is identical to Cymbal's, with `mod1` in place of `mod`,
  and "HiHat" in the comment title.

### 7.7 ADD `tools/dsp_golden/test_postchain.c`

What it compares, for S0 (zero differing samples):

| Engine | Reference (frozen sources via `extract.py`) | Candidate |
|---|---|---|
| Drum | `old_bufferTool_addGainInterpolated` + `old_bufferTool_addGain` (only when `volumeMod`) + `old_calcDistBlock` | `voicePost_drum` (extracted from `voicePostChain.h`, with `bufferTool_floatToInt16Store`, `bufferTool_interpolatedGain`, `distortion_curveSample16`) |
| Snare | frozen `Snare.c` lines 239–262, extracted with `--lines 239-262 --rename calcDistBlock=old_calcDistBlock`, wrapped in a function taking a stub struct `{ float mix, velo, egValueOscVol; uint8_t volumeMod; Distortion distortion; }` named `voice` | `voicePost_mixAddGainDist` with the caller's gain select |
| Cymbal | frozen `CymbalVoice.c` 252–272, wrapped the same way | `voicePost_addGainDist` |
| HiHat | frozen `HiHat.c` 270–291, wrapped the same way | `voicePost_addGainDist` |

**Grid:**

- signals: 10,000 random blocks (full int16 range) plus fixed blocks (0,
  ±32767, −32768, alternating), for both `buf` and `add`;
- `shape` from `setDistortionShape(0..127)` (all 128);
- `mix` ∈ {0, 0.25, 0.5, 0.999, 1};
- EG gains ∈ {0, 1e-4, 0.3, 0.7, 1.0, 1.02} (the last checks wrap
  equivalence above unity);
- `velo` ∈ {0, 0.5, 1};
- `volumeMod` ∈ {0, 1};
- Drum `lastGain`/`gain` pairs from the EG set.

**Makefile target.** `setDistortionShape` is `__inline` in the source (a
C99 inline definition with no external symbol), so the host extraction
renames `__inline` to `static`:

```make
# ---- Step 4 (Gate 4): fused post-chains vs the frozen passes, bit-identical.
postchain: | $(GEN)
	$(EX) --src $(FROZEN)/Core/DSPAudio/BufferTools.h --func bufferTool_satAdd16 \
	  --out $(GEN)/pc_sat.c
	$(EX) --src $(FROZEN)/Core/DSPAudio/BufferTools.c \
	  --func bufferTool_addGainInterpolated --func bufferTool_addGain \
	  --rename bufferTool_addGainInterpolated=old_addGainInterpolated \
	  --rename bufferTool_addGain=old_addGain --out $(GEN)/pc_old_bt.c
	$(EX) --src $(FROZEN)/Core/DSPAudio/distortion.c --func setDistortionShape \
	  --func calcDistBlock --rename calcDistBlock=old_calcDistBlock \
	  --rename __inline=static --out $(GEN)/pc_old_dist.c
	$(EX) --src $(FROZEN)/Core/DSP/Instruments/Snare/Snare.c --lines 239-262 \
	  --rename calcDistBlock=old_calcDistBlock --out $(GEN)/pc_old_snare_body.inc
	$(EX) --src $(FROZEN)/Core/DSP/Instruments/Cymbal/CymbalVoice.c --lines 252-272 \
	  --rename calcDistBlock=old_calcDistBlock --out $(GEN)/pc_old_cym_body.inc
	$(EX) --src $(FROZEN)/Core/DSP/Instruments/HiHat/HiHat.c --lines 270-291 \
	  --rename calcDistBlock=old_calcDistBlock --out $(GEN)/pc_old_hat_body.inc
	$(EX) --src $(ROOT)/Core/DSPAudio/BufferTools.h --func bufferTool_floatToInt16Store \
	  --func bufferTool_interpolatedGain --out $(GEN)/pc_new_bt.c
	$(EX) --src $(ROOT)/Core/DSPAudio/distortion.h --func distortion_curveSample16 \
	  --out $(GEN)/pc_new_dist.c
	$(EX) --src $(ROOT)/Core/DSPAudio/voicePostChain.h --func voicePost_drum \
	  --func voicePost_mixAddGainDist --func voicePost_addGainDist \
	  --out $(GEN)/pc_new_chain.c
	$(HOSTCC) $(HOSTFLAGS) test_postchain.c -o build/postchain -lm
	./build/postchain
```

**`test_postchain.c` layout:**

1. Include `pc_sat.c`, `pc_old_bt.c`, `pc_old_dist.c`, `pc_new_bt.c`,
   `pc_new_dist.c` and `pc_new_chain.c`, in that order.
2. Define the stub struct
   `typedef struct { float mix, velo, egValueOscVol; uint8_t volumeMod; Distortion distortion; } VoiceStub;`.
3. Wrap the three frozen bodies. The parameter names are exactly the
   identifiers the frozen lines use:

   ```c
   static void old_snare(int16_t *buf, int16_t *transBuf, VoiceStub *voice, uint8_t size)
   {
   #include "pc_old_snare_body.inc"
   }
   static void old_cymbal(int16_t *buf, int16_t *mod, VoiceStub *voice, uint8_t size)
   {
   #include "pc_old_cym_body.inc"
   }
   static void old_hihat(int16_t *buf, int16_t *mod1, VoiceStub *voice, uint8_t size)
   {
   #include "pc_old_hat_body.inc"
   }
   ```

4. The reference Drum sequence is
   `old_addGainInterpolated(buf, g, lg, size); if (vm) old_addGain(buf, velo, size); old_calcDistBlock(&d, buf, size);`.
5. Every candidate runs on a copy of the same input. The caller-side gain
   select is written exactly as in §7.6.

**Pass:** 0 differing samples for every engine and grid point.

### 7.8 ADD `tools/dsp_golden/armcheck/postchain.c` and Makefile target `armcheck-postchain`

```c
/*
 * ARM codegen check for the fused voice post-chains (S073 Step 4).
 *
 * What:       compiles the frozen separate passes (reference) and the new
 *             fused helpers (candidate) with the firmware's DSP flags, as
 *             noinline functions, so fpseq.py can compare their per-sample
 *             VFP/saturation instruction multisets.
 * Why:        plan §0.3: the S0 claim must hold for arm-none-eabi -Ofast code
 *             generation, not only on the host.
 * Inputs:     build/gen fragments made by the `postchain` target (frozen
 *             BufferTools/distortion/voice bodies) and the real headers.
 * Outputs:    build/ac_postchain.o.
 * Accessors:  Makefile armcheck-postchain.
 * Affiliates: voicePostChain.h, BufferTools.h, distortion.h, fpseq.py.
 */
#include "config.h"
#include "BufferTools.h"
#include "distortion.h"
#include "voicePostChain.h"
#include "pc_old_bt.c"        /* old_addGainInterpolated, old_addGain        */
#include "pc_old_dist_arm.c"  /* old_calcDistBlock only (see Makefile)       */

typedef struct { float mix, velo, egValueOscVol; uint8_t volumeMod;
                 Distortion distortion; } VoiceStub;

__attribute__((noinline)) void ref_snare(int16_t *buf, int16_t *transBuf,
                                         VoiceStub *voice, uint8_t size)
{
#include "pc_old_snare_body.inc"
}
__attribute__((noinline)) void new_snare(int16_t *buf, int16_t *transBuf,
                                         VoiceStub *voice, uint8_t size)
{
    const float ampGain = voice->volumeMod ? voice->velo * voice->egValueOscVol
                                           : voice->egValueOscVol;
    voicePost_mixAddGainDist(buf, transBuf, voice->mix, ampGain,
                             &voice->distortion, size);
}
__attribute__((noinline)) void ref_cymbal(int16_t *buf, int16_t *mod,
                                          VoiceStub *voice, uint8_t size)
{
#include "pc_old_cym_body.inc"
}
__attribute__((noinline)) void new_addgain(int16_t *buf, int16_t *mod,
                                           VoiceStub *voice, uint8_t size)
{
    const float ampGain = voice->volumeMod ? voice->velo * voice->egValueOscVol
                                           : voice->egValueOscVol;
    voicePost_addGainDist(buf, mod, ampGain, &voice->distortion, size);
}
__attribute__((noinline)) void new_drum(int16_t *buf, float g, float lg,
                                        float velo, uint8_t vm,
                                        const Distortion *d, uint8_t size)
{
    voicePost_drum(buf, g, lg, vm ? velo : 1.0f, d, size);
}
```

- The frozen `old_addGainInterpolated`, `old_addGain` and `old_calcDistBlock`
  are ordinary global functions in the fragments. Their loops are
  therefore compiled in their own bodies, and `fpseq.py` reads them there.
- The HiHat body is loop-identical to Cymbal's apart from the buffer name. It
  is covered by the host test, and it does not need a separate armcheck pair.

```make
# ---- Step 4 (Gate 4): ARM codegen MATCH for each fused post-chain.
armcheck-postchain: postchain
	$(EX) --src $(FROZEN)/Core/DSPAudio/distortion.c --func calcDistBlock \
	  --rename calcDistBlock=old_calcDistBlock --out $(GEN)/pc_old_dist_arm.c
	$(ARMCC) $(ARMFLAGS) -c armcheck/postchain.c -o build/ac_postchain.o
	$(FP) --obj build/ac_postchain.o --ref old_addGainInterpolated \
	  --ref old_addGain --ref old_calcDistBlock --new new_drum
	$(FP) --obj build/ac_postchain.o --ref ref_snare:0 --ref old_calcDistBlock \
	  --new new_snare
	$(FP) --obj build/ac_postchain.o --ref ref_cymbal:0 --ref old_calcDistBlock \
	  --new new_addgain
```

- The armcheck extraction of the frozen `distortion.c` takes
  `calcDistBlock` only. `setDistortionShape` would clash with the real
  `distortion.h` prototype.
- In `ref_snare:0` and `ref_cymbal:0`, loop 0 is the `volumeMod` branch
  loop. Both branch loops contain the same operations, because the
  `velo*eg` product is loop-invariant and hoisted.
- **Expected output:** `MATCH` for every pair. The candidate additionally
  shows `sxth` narrowing where the reference stored and reloaded through the
  int16 block (pre-validation: Drum 2, Snare 2, Cymbal 2).
- For Drum, the reference includes the optional velocity pass (`old_addGain`).
  The candidate always multiplies, so the multisets match with
  velocity-to-volume **on**, which is the worst case the constant-CPU rule
  budgets. With it off, the candidate does one extra multiply by 1.0 whose
  result is bit-identical (proved on the host).

### 7.9 Gate 4

- `make -C tools/dsp_golden postchain` → 0 differing samples (all engines).
- `make -C tools/dsp_golden armcheck-postchain` → `MATCH` for all pairs.
- A spot check of the final `build/lxr02.elf`: in `mixer_calcNextSampleBlock`
  (where LTO inlines the voices), each post-chain loop contains one `vdiv` (the
  distortion) and no `strh`/`ldrsh` pair between the EG and distortion stages.
- Firmware: `data`/`bss` unchanged; record the flash delta.
- The constant-CPU review: the only new conditionals are the per-block gain
  selects.
- **Hardware (user):** listening on the kits at drive 0 and drive max, and
  with velocity-to-volume on and off; no new underruns.

---

## 8. Step 6 — Octave selection without `log2f()` ("good enough")

### 8.1 MODIFY `Core/DSPAudio/Oscillator.c:61–80` (`freqToTableIndex()` and the comment above it)

```c
/*
 * Wavetable octave edges (S073 Step 6).
 *
 * What:       T_k = 440 * 2^(k - 5.75) Hz for k = 1..10 (C0 ... C9), stored
 *             as the nearest floats. The table index is the number of edges
 *             at or below f.
 * Why:        the old index, (int)((69 + 12*log2f(f/440))/12) clamped to
 *             0..10, is floor(5.75 + log2(f/440)): the same count, up to
 *             float and log2f rounding at each edge. Comparing against 10
 *             constants removes a log2f() per wavetable oscillator per block
 *             during pitch sweeps (audit F7).
 * Inputs:     none (flash constants, 40 B).
 * Outputs:    read by freqToTableIndex().
 * Accessors:  freqToTableIndex().
 * Affiliates: tools/dsp_golden/test_octave.c reports every input where the
 *             old and new index differ (only within a few ulps of an edge).
 */
static const float osc_octaveEdgeHz[10] = {
	16.3515987f,   32.7031975f,   65.406395f,   130.81279f,   261.62558f,
	523.25116f,  1046.50232f,  2093.00464f,  4186.00928f,  8372.01855f,
};

/*
 * Band-limited wavetable octave for a frequency (S073 Step 6).
 *
 * What:       counts the octave edges <= f with ten branch-free compares.
 *             NaN and f <= 0 give 0 (every compare is false); +inf gives 10.
 *             This matches the old expression's results for those inputs.
 * Why:        see osc_octaveEdgeHz. Constant cost: all ten compares always
 *             run (no early exit), so the cost does not depend on pitch.
 * Inputs:     f, the effective oscillator frequency in Hz.
 * Outputs:    the table index 0..10.
 * Accessors:  osc_calcWavetableFreqValue() (Oscillator.c:904).
 * Affiliates: osc_setFreq()'s frequency cache (existing, kept); wavetable.c
 *             table layout (11 octaves).
 */
static inline uint8_t freqToTableIndex(const float f)
{
	uint8_t index = 0u;
	uint8_t k;

	for (k = 0u; k < 10u; k++)
		index = (uint8_t)(index + (uint8_t)(f >= osc_octaveEdgeHz[k]));
	return index;
}
```

The old comment at line 61 (`//TODO die phaseInc berechnung …`) is about
`freq2PhaseIncr` and stays. The float literals are the nearest-float values of
the exact edges (bit patterns `0x4182d013` … `0x4602d013`, each edge exactly
2× the previous). Write them with enough digits to round-trip, as above.

### 8.2 MODIFY `Oscillator.c:921–923` (comment in `osc_setFreq()`)

```c
		/* osc_setFreq() is called frequently by the block dispatcher. Cache the
		** effective frequency+waveform tuple so unchanged blocks skip the
		** phase-increment recalculation and wavetable octave selection
		** (S073 Step 6: octave selection is now an edge count, not log2f()). */
```

### 8.3 ADD `tools/dsp_golden/test_octave.c`

- **Extracts:** the frozen `freqToTableIndex` (as `old_`) and the new one
  plus `osc_octaveEdgeHz` (as `new_`).
- **Iterates** every float bit pattern from `0x3A83126F` (0.001 Hz) to
  `0x47800000` (65,536 Hz), about 2.0 × 10^8 inputs. It also checks 0, −1,
  +inf and NaN.
- **Reports:**
  - the number of mismatching inputs;
  - for each, the nearest edge k, the distance in ulps, and the distance in
    cents (`1200·log2(f/T_k)`);
  - the maximum over all mismatches.
- **Makefile target:**

```make
# ---- Step 6 (Gate 6): octave index old vs new over every float in range.
octave: | $(GEN)
	$(EX) --src $(FROZEN)/Core/DSPAudio/Oscillator.c --func freqToTableIndex \
	  --rename freqToTableIndex=old_freqToTableIndex --out $(GEN)/oct_old.c
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.c --object osc_octaveEdgeHz \
	  --func freqToTableIndex --rename freqToTableIndex=new_freqToTableIndex \
	  --out $(GEN)/oct_new.c
	$(HOSTCC) $(HOSTFLAGS) test_octave.c -o build/octave -lm
	./build/octave
```

### 8.4 Gate 6 ("good enough")

- `make -C tools/dsp_golden octave`: every mismatch lies within 8 ulps of an
  edge (≪ 0.001 cents). Record the counts in the audit.
- Firmware: `data`/`bss` unchanged. Record the flash delta; `log2f` may drop
  out of the image if it has no other user.
- **Hardware (user):** a pitch-sweep listening check (a pitch EG on a
  wavetable oscillator across several octaves).

---

## 9. Step 5 — Mixer dry + send in one pass (S0; approved, D-5 decided 2026-09-28)

**Decision D-5 (user, 2026-09-28): implement Step 5.** The combined loop
replaces the send pass outright:

- the dry-only function stays for voices whose send is inactive;
- the send-only function is removed, because nothing else calls it.

The flash cost is about +0.7–1 KB net (the combined function is added and the
send function removed).

**Change list for Step 5:**

| Section | File:lines | Action | What |
|---|---|---|---|
| §9.1 | `mixer.c:532–568` | MODIFY (replace) | `mixer_addVoiceToFxBus()` and its comment block become `mixer_addVoiceInt16ToOutputAndFx()`. |
| §9.2 | `mixer.c:825–832` | MODIFY | The slot loop calls the combined function when the send is active, and the dry-only function otherwise. |
| §9.2a, §9.2b | `mixer.c:101–102`, `:772` | MODIFY (comments) | Replace the references to the removed `mixer_addVoiceToFxBus()`. |
| §9.2c | — | CHECK | No definition or call of the removed function remains (only explanatory comments); no new warning. |
| §9.3 | `tools/dsp_golden/test_mixer.c` | ADD | Host bit-identity test. |
| §9.4 | `tools/dsp_golden/armcheck/mixer.c` | ADD | ARM codegen MATCH and instruction counts. |

`mixer_addVoiceInt16ToOutput()` (`mixer.c:409–501`) is **unchanged**.


### 9.1 MODIFY `Core/DSPAudio/mixer.c:532–568`: replace `mixer_addVoiceToFxBus()` with the combined function

Delete the comment block that begins at line 532 ("Add one decimated voice
block to the FX bus with a click-free send ramp") and the whole
`mixer_addVoiceToFxBus()` definition through its closing brace at line 568.
In the same place, insert:

```c
/*
 * Dry output and FX send in one pass (S073 Step 5).
 *
 * What:       for one decimated voice block, produces exactly what
 *             mixer_addVoiceInt16ToOutput() and the pre-S073
 *             mixer_addVoiceToFxBus() produced when both ran, reading each
 *             sample once:
 *             - dry: the voice gain ramp, int16 truncation, sample_mx_t
 *               conversion, pan/route/saturating add to the routed DAC
 *               pair;
 *             - send: its own ramp, float x 256 straight to sample_mx_t
 *               without the int16 truncation (on purpose), stereo-input
 *               types panned into bus L/R, mono-input types unpanned into
 *               L only, saturating adds. This is the click-free send ramp
 *               the removed mixer_addVoiceToFxBus() provided.
 *             Each expression keeps its original operand order.
 * Why:        audit F5: the send pass re-read every sample and recomputed
 *             a ramp. The user requires unchanged functionality (S0).
 * Inputs:     dest (jack-resolved routing), panL/panR (squareRootLut),
 *             data (decimated pre-volume block), gain/lastGain (dry ramp:
 *             vol x mix fader), sendGain/sendLastGain (send ramp:
 *             fxSend/127 x send fader), stereo (live Effect stereo-input
 *             flag), and the four interleaved output buses.
 * Outputs:    the output buses and mixer_fx_bus.mx[0..1].
 * Accessors:  mixer_calcNextSampleBlock(), only when the send is active
 *             (fx_active and either send gain > 0: the existing kept path).
 *             Otherwise the dry-only function runs unchanged.
 * Affiliates: mixer_addVoiceInt16ToOutput() (the dry-only path, whose dry
 *             expressions this copies exactly); the former
 *             mixer_addVoiceToFxBus(), removed in S073 Step 5, whose send
 *             expressions this copies exactly (its pre-S073 text is kept in
 *             tools/dsp_golden/frozen/Core/DSPAudio/mixer.c as the test
 *             reference); the mixer_send_last_gain / mixer_voice_last_gain
 *             updates in the caller (unchanged); mixer_faderGains();
 *             tools/dsp_golden/test_mixer.c.
 */
static void mixer_addVoiceInt16ToOutputAndFx(uint8_t dest,
		const float panL,
		const float panR,
		const int16_t* data,
		const float gain,
		const float lastGain,
		const float sendGain,
		const float sendLastGain,
		const uint8_t stereo,
		sample_mx_t* outL,
		sample_mx_t* outR,
		sample_mx_t* outL2,
		sample_mx_t* outR2)
{
	uint8_t i;
	const float inv_size = 1.f / (OUTPUT_DMA_SIZE - 1.f);
	const float gain_delta = gain - lastGain;
	const float send_delta = sendGain - sendLastGain;

/* The send half of one sample, identical to mixer_addVoiceToFxBus(). The
** stereo test is loop-invariant; GCC -Ofast unswitches it. */
#define MIXER_FX_SEND_SAMPLE()                                                \
	do {                                                                      \
		const float sendCurrentGain = sendLastGain                            \
				+ ((float)i * inv_size * send_delta);                         \
		const float sample = (float)data[i] * sendCurrentGain * 256.0f;       \
		if (stereo) {                                                         \
			mixer_fx_bus.mx[0][i] = bufferTool_satAdd32(                      \
					mixer_fx_bus.mx[0][i], (sample_mx_t)(sample * panL));     \
			mixer_fx_bus.mx[1][i] = bufferTool_satAdd32(                      \
					mixer_fx_bus.mx[1][i], (sample_mx_t)(sample * panR));     \
		} else {                                                              \
			mixer_fx_bus.mx[0][i] = bufferTool_satAdd32(                      \
					mixer_fx_bus.mx[0][i], (sample_mx_t)sample);              \
		}                                                                     \
	} while (0)

	switch(dest)
	{
	case MIXER_ROUTING_DAC1_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			const sample_mx_t sm = sampleMix_fromInt16(s16);
			MIXER_FX_SEND_SAMPLE();
			*outL2 = bufferTool_satAdd32(*outL2, (sample_mx_t)((float)sm * panL));
			outL2 += 2;
			*outR2 = bufferTool_satAdd32(*outR2, (sample_mx_t)((float)sm * panR));
			outR2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			const sample_mx_t sm = sampleMix_fromInt16(s16);
			MIXER_FX_SEND_SAMPLE();
			*outL = bufferTool_satAdd32(*outL, (sample_mx_t)((float)sm * panL));
			outL += 2;
			*outR = bufferTool_satAdd32(*outR, (sample_mx_t)((float)sm * panR));
			outR += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			MIXER_FX_SEND_SAMPLE();
			*outL2 = bufferTool_satAdd32(*outL2, sampleMix_fromInt16(s16));
			outL2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			MIXER_FX_SEND_SAMPLE();
			*outR2 = bufferTool_satAdd32(*outR2, sampleMix_fromInt16(s16));
			outR2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			MIXER_FX_SEND_SAMPLE();
			*outL = bufferTool_satAdd32(*outL, sampleMix_fromInt16(s16));
			outL += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			MIXER_FX_SEND_SAMPLE();
			*outR = bufferTool_satAdd32(*outR, sampleMix_fromInt16(s16));
			outR += 2;
		}
		break;
	default:
		/* An unknown routing adds no dry signal (as the dry-only function's
		** switch falls through), but the send must still accumulate exactly
		** as mixer_addVoiceToFxBus() would. */
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
			MIXER_FX_SEND_SAMPLE();
		break;
	}
#undef MIXER_FX_SEND_SAMPLE
}
```

**The `default` case is live and required:**

- `mixer_audioRouting[slot]` can hold a value outside the six routings.
  `MidiParser.c:1357` stores a MIDI CC's `msg.data2` (0..127) into it
  without a clamp.
- `mixer_checkOutJackAvailable()` returns an unknown value unchanged
  (`mixer.c:284`).
- For such a value today, `mixer_addVoiceInt16ToOutput()`'s switch (which has
  no default) adds nothing to the dry buses, while the send still accumulates.
  The combined function's `default` reproduces exactly that.
- `tools/dsp_golden/test_mixer.c` covers it with the invalid routing value in
  its grid.

### 9.2 MODIFY `mixer.c:825–832` (the send and dry calls in the slot loop)

```c
		/*
		 * One-pass dry + send when the send is active (S073 Step 5).
		 *
		 * What:       with an active send (the existing condition, kept by
		 *             user decision), the combined function produces the dry
		 *             output and the FX bus contribution in one read of
		 *             sampleData. Otherwise the dry-only function runs as
		 *             before.
		 * Why:        audit F5; S0 (see the combined function's contract).
		 * Inputs:     the per-slot gains, pan and routing resolved above.
		 * Outputs:    the output buses and mixer_fx_bus; the last-gain
		 *             updates below are unchanged and still run every block.
		 * Accessors:  this loop.
		 * Affiliates: mixer_addVoiceInt16ToOutputAndFx(),
		 *             mixer_addVoiceInt16ToOutput(); the send-only
		 *             mixer_addVoiceToFxBus() was removed in this step.
		 */
		if (fx_active && (sendGain > 0.0f || mixer_send_last_gain[slot] > 0.0f))
			mixer_addVoiceInt16ToOutputAndFx(effectiveRouting[slot],
					squareRootLut[127-pan], squareRootLut[pan],
					sampleData, voiceGain, mixer_voice_last_gain[slot],
					sendGain, mixer_send_last_gain[slot], fx_stereo_in,
					&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
		else
			mixer_addVoiceInt16ToOutput(effectiveRouting[slot],
					squareRootLut[127-pan], squareRootLut[pan],
					sampleData, voiceGain, mixer_voice_last_gain[slot],
					&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
```

- Lines 833–834 (`mixer_voice_last_gain[slot] = voiceGain;` and
  `mixer_send_last_gain[slot] = sendGain;`) are **unchanged**: they still
  update every block, which is the S072 contract. Re-enabling an Effect
  depends on the send ramp origin being current.
- The send condition on the first line is the existing one, kept by user
  decision (plan §3.1). The combined function runs exactly when the old code
  ran `mixer_addVoiceToFxBus()`.

### 9.2a MODIFY `mixer.c:101–102` (the comment on `mixer_send_last_gain`)

Change

```c
 * mixer_addVoiceToFxBus() ramps send changes from knobs, automation, and
```

to

```c
 * mixer_addVoiceInt16ToOutputAndFx() (S073 Step 5) ramps send changes from
 * knobs, automation, and
```

and reflow the rest of that sentence ("fader-mode edits without a zipper.")
unchanged. `mixer_send_last_gain` itself is unchanged.

### 9.2b MODIFY `mixer.c:772` (the Affiliates line in the FX-bus snapshot comment)

Change

```c
	 * mixer_addVoiceToFxBus(), effects_process(), and mixer_addFxReturnToOutput().
```

to

```c
	 * mixer_addVoiceInt16ToOutputAndFx(), effects_process(), and
	 * mixer_addFxReturnToOutput().
```

### 9.2c Removal check

- After the edits, no definition or call of `mixer_addVoiceToFxBus` remains.
  Check with `grep -nE "mixer_addVoiceToFxBus\s*\(" Core/DSPAudio/mixer.c`: the
  only matches must be inside comments that describe the S073 change (the
  new function's contract and the call-site comment).
- The old function then exists only in
  `tools/dsp_golden/frozen/Core/DSPAudio/mixer.c`, which is where the
  harness extracts its reference from.
- The build must not show a new `-Wunused-function` warning for `mixer.c`.

### 9.3 ADD `tools/dsp_golden/test_mixer.c`

- **Extracts:**
  - the frozen `mixer_addVoiceInt16ToOutput` and `mixer_addVoiceToFxBus` as
    `old_`, with `--rename mixer_fx_bus=old_bus`;
  - the current `mixer_addVoiceInt16ToOutputAndFx` as `new_`, with
    `--rename mixer_fx_bus=new_bus`;
  - `bufferTool_satAdd32` from `BufferTools.h`;
  - `squareRootLut` from `squareRootLut.c` (`--object`).
- **Grid:** every routing (6 plus one invalid value) × stereo {0, 1} × pan
  0..127 × dry-gain pairs {0, 0.3, 0.999, 1} × send pairs {0, 0.2, 1} × 2,000
  random and fixed data blocks. The dry gain is vol × fader, so it is never
  above 1.0 in the firmware; values above 1.0 would test undefined C
  conversions rather than the firmware. It also seeds the output and bus buffers with
  near-saturation values, to exercise `satAdd32`.
- **Compares:** the output buffers and the bus. **Pass:** 0 differences.
- **Makefile target:**

```make
# ---- Step 5 (Gate 5): dry + send old vs combined, bit-identical.
mixer: | $(GEN)
	$(EX) --src $(FROZEN)/Core/DSPAudio/BufferTools.h --func bufferTool_satAdd32 \
	  --out $(GEN)/mx_sat.c
	$(EX) --src $(FROZEN)/Core/DSPAudio/squareRootLut.c --object squareRootLut \
	  --out $(GEN)/mx_lut.c
	$(EX) --src $(FROZEN)/Core/DSPAudio/mixer.c --func mixer_addVoiceInt16ToOutput \
	  --func mixer_addVoiceToFxBus --rename mixer_addVoiceInt16ToOutput=old_dry \
	  --rename mixer_addVoiceToFxBus=old_send --rename mixer_fx_bus=old_bus \
	  --out $(GEN)/mx_old.c
	$(EX) --src $(ROOT)/Core/DSPAudio/mixer.c --func mixer_addVoiceInt16ToOutputAndFx \
	  --rename mixer_addVoiceInt16ToOutputAndFx=new_combined \
	  --rename mixer_fx_bus=new_bus --out $(GEN)/mx_new.c
	$(HOSTCC) $(HOSTFLAGS) test_mixer.c -o build/mixer -lm
	./build/mixer
```

`test_mixer.c` declares `static mixer_fx_bus_t old_bus, new_bus;` before
including `mx_old.c` and `mx_new.c`.

### 9.4 ADD `tools/dsp_golden/armcheck/mixer.c` and target `armcheck-mixer`

```c
/*
 * ARM codegen check and instruction count for the one-pass mixer (S073 Step 5).
 *
 * What:       compiles the frozen dry and send functions and the new combined
 *             function with the firmware's DSP flags, so fpseq.py can:
 *             - confirm the VFP/saturation multiset of the combined
 *               DAC1_STEREO loop equals dry + send (corrected 2026-09-29:
 *               the landed target checks every dry x send combination;
 *               see the notes after the Makefile block);
 *             - report total loop instruction counts, recorded in the
 *               audit as Step 5's measured saving (D-5 is decided:
 *               implement).
 * Why:        plan §0.3 (S0 on ARM code generation), and a measured saving
 *             without a profiler.
 * Inputs:     build/gen fragments from the `mixer` target.
 * Outputs:    build/ac_mixer.o.
 * Accessors:  Makefile armcheck-mixer.
 * Affiliates: mixer.c, fpseq.py.
 */
#include "config.h"
#include "BufferTools.h"
/* Routing values mirrored from mixer.h (not included: its include tree reaches
** the instrument model, outside the harness include path). */
enum { MIXER_ROUTING_DAC1_STEREO = 0, MIXER_ROUTING_DAC2_STEREO,
       MIXER_ROUTING_DAC1_L, MIXER_ROUTING_DAC1_R,
       MIXER_ROUTING_DAC2_L, MIXER_ROUTING_DAC2_R };
typedef union { sample_mx_t mx[2][OUTPUT_DMA_SIZE];
                float f[2][OUTPUT_DMA_SIZE]; } golden_bus_t;
static golden_bus_t old_bus, new_bus;
#include "mx_old.c"   /* old_dry, old_send (static in the source: made global */
#include "mx_new.c"   /* new_combined        by the --rename of `static`)     */
```

- The frozen `mixer_addVoiceInt16ToOutput`, `mixer_addVoiceToFxBus` and the
  new combined function are `static` in `mixer.c`, so this target's
  extraction adds `--rename static=` to make them global and keep their
  loops in their own bodies. The host `mixer` target does not need that.
- `old_bus`/`new_bus` replace `mixer_fx_bus` through the renames already in
  the `mixer` target.

```make
# ---- Step 5 (Gate 5): ARM codegen MATCH, plus the instruction counts for the audit.
armcheck-mixer: mixer
	$(EX) --src $(FROZEN)/Core/DSPAudio/mixer.c --func mixer_addVoiceInt16ToOutput \
	  --func mixer_addVoiceToFxBus --rename mixer_addVoiceInt16ToOutput=old_dry \
	  --rename mixer_addVoiceToFxBus=old_send --rename mixer_fx_bus=old_bus \
	  --rename static= --out $(GEN)/mx_old.c
	$(EX) --src $(ROOT)/Core/DSPAudio/mixer.c --func mixer_addVoiceInt16ToOutputAndFx \
	  --rename mixer_addVoiceInt16ToOutputAndFx=new_combined \
	  --rename mixer_fx_bus=new_bus --rename static= --out $(GEN)/mx_new.c
	$(ARMCC) $(ARMFLAGS) -c armcheck/mixer.c -o build/ac_mixer.o
	$(FP) --obj build/ac_mixer.o --ref old_dry:0 --ref old_send --new new_combined:0
	$(FP) --obj build/ac_mixer.o --ref old_dry:0 --ref old_send --new new_combined:0 \
	  --report --all
```

- `old_dry:0` is the `DAC1_STEREO` loop, the first case.
- `new_combined:0` is its combined twin. After loop unswitching, the stereo
  and mono send variants may be separate loops, so the implementer confirms
  which index is the stereo variant from the printed multisets, and adjusts
  `:K` if needed.
- **Corrected 2026-09-29.** Loop order in the object does not follow the
  switch order, and loop rotation gives several backward branches per
  case. `old_dry:0` is not a routing loop. The landed target first compared
  `old_dry:1` + `old_send:0` against `new_combined:0`: a single-output
  routing with a mono-input send, not DAC1_STEREO. The target now gates
  one loop of each combination, identified by its multiset:
  `old_dry:1`/`:4` (single/stereo dry), `old_send:0`/`:3` (mono/stereo
  send) against `new_combined:0`, `:22`, `:2` and `:27`, plus the default
  case (`old_send:0` → `new_combined:87`, `old_send:3` → `:12`). All
  MATCH.
- The `--report --all` line gives the measured saving for the audit: total
  instructions per sample for dry + send today against the combined loop.

### 9.6 Decision D-5 (decided) and Gate 5

- **D-5: decided (user, 2026-09-28): implement.** The
  `armcheck-mixer --report --all` counts (instructions per sample for dry +
  send today against the combined loop) are recorded in the audit as the
  measured saving. They are not a keep/drop gate.
- **Gate 5:**
  - `make -C tools/dsp_golden mixer` → 0 differences;
  - `armcheck-mixer` → `MATCH`;
  - the §9.2c removal check passes: no definition or call remains, and no
    new warnings;
  - firmware `data`/`bss` unchanged; record the flash delta (expected about
    +0.7–1 KB);
  - **hardware (user) manual FX check:** send and return in all three fader
    modes, stereo panning of sends, mono-input vs stereo-input types, `flt`
    at high reso/drive, a Scene switch to `off` and back, and the Effect
    budget.

---

## 10. Documentation changes (after each gate, in the same change)

| When | File | Change |
|---|---|---|
| Each step | `knowledge_files/specification_reference/CPU_USE_DSP_AUDIT.md` | The priority-list item status (16–25); the harness numbers (S1 report, octave report, D-5 counts); the flash delta. |
| Step 3b | `MEMORY.md` hardware section | "D-Cache enabled (16KB) with MPU (WT for SRAM, SO for DMA buffers)" → "Normal non-cacheable for DMA buffers (S073)"; "DMA buffers live in the `.dma_nocache` linker section, marked Strongly-Ordered via MPU" → "marked Normal non-cacheable via MPU region 1 (S073)". |
| Step 2 | `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` (if it describes `writeSpecialRuntime` or the file_key-driven setters) | The descriptor `runtime.special` tag replaces the key-string dispatch. |
| Closeout | `CPU_USE_DSP_AUDIT.md` | Steps 7 and 8 rejected; Step 4 is S0 without the bypass; no profiler. |
| Closeout | `073_SESSION_HANDOFF_LOG.md` | The step results. This document and the plan are then superseded. |

---

## 11. Risks and how the schedule contains them

| Risk | Where | Containment |
|---|---|---|
| `-Ofast` compiles the fused loops with a different FMA contraction or reciprocal transform than the separate passes | Steps 4 and 5 | The ARM codegen check (`fpseq.py` `MATCH`) plus host bit-identity. On `DIFF`: restructure (for example isolate the expression in the shared inline helper), or do not merge. |
| The float→int16 conversion instruction differs between the store form and the register form | Step 4 | armcheck: both must use `vcvt.s32.f32`. The wrap equivalence is covered by the EG-gain 1.02 grid point. |
| A tag table error changes a writer | Step 2 | `check_special_tags.py` (host) and the diagnostic boot `s0` (device). The switch cases are line-for-line the old branches, including the runtime-type rechecks. |
| Normal-memory ordering on the DMA buffers | Step 3b | `DSB` at the end of the pack; the audio, ADC and control regression on hardware; 3a and 3b land separately. |
| Batched filter divergence in self-oscillating cases | Step 1 | S1 acceptance against the rounding baseline (a compiler-flag-level difference); a listening check on a self-oscillating patch. |
| Octave-edge differences audible | Step 6 | The host report bounds them to a few ulps of the edges (about 1e-7 relative); a pitch-sweep listening check. |

---

## 12. Per-step verification commands

Run from the repository root after each step.

```sh
make clean && make all                      # expect no new warnings
arm-none-eabi-size build/lxr02.elf          # data=416, bss=426,336 (unchanged)
python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf   # record flash delta
make -C tools/dsp_golden <step target>      # selftest | filter | special_tags | pack |
                                            # postchain | octave | mixer | armcheck-*
make img                                    # image for the user's hardware checks
```

Gates G0–G6 are the "Gate" subsections above: §1.9, §3.7, §4.11, §5.4, §6.5,
§7.9, §8.4 and §9.6. The hardware items in each gate are run by the user.

---

## 13. Pre-validation of this document's code (2026-09-28, scratch only)

The code blocks in this document were run in a scratch directory, not in the
repository, against the current sources, which equal the frozen baseline.
No project file was changed.

| Check | Result |
|---|---|
| Harness tools | `extract.py` extracted every selector used (functions, `squareRootLut` object, `#define` line ranges, the frozen Snare body, renames including `__inline=static`). `check_special_tags.py` on today's untagged tables: 155 rows, 65 mismatches, exactly the 17 + 17 + 15 + 16 rows §4.6 tags. `fpseq.py` read loops from a real `-Ofast` object. |
| Step 1 source | §3.1–§3.3 spliced into a copy of `ResonantFilter.c` at the cited lines. It compiles for ARM with `-Wall -Wextra` and no warnings. `vdiv` 81 → 39; +732 B. |
| Step 1 S1 (28,800 configs, 276,480,000 samples each variant) | int16: 0.210 % differing, SDR 91.7 dB, 13 configs over 16 LSB, ≤ 1 LSB elsewhere, no NaN/Inf. float: SDR 86.5 dB, 14 configs over 16 LSB, < 1 LSB elsewhere. Rounding-only baseline: int16 0.148 %, SDR 91.8 dB, 10 configs; float SDR 86.8 dB, 11 configs. Every >16-LSB configuration in both runs is at cutoff 0.8, resonance 0.98. |
| Step 3a S0 | 100,000 blocks (including clamp values): 0 differing bytes. |
| Step 4 S0 (host) | 148,608,000 samples per engine (Drum, Snare, Cymbal, HiHat), 43 distortion shapes, all mix/EG/velocity/volumeMod values including EG 1.02: 0 differing samples, with FMA contraction on and off. |
| Step 4 ARM codegen | `-Ofast` objects: the VFP/saturation multisets MATCH for Drum, Snare and Cymbal/HiHat; the only difference is the expected `sxth` narrowing. The ramp's `vfma` and the distortion `vdiv` are present in both. |
| Step 5 S0 | 11,491,200 cases (every routing incl. invalid, stereo/mono, pans, in-range gains, near-saturation buses): 0 differing. (With out-of-range dry gain 1.5 and host vectorisation on, differences appeared; they vanish with vectorisation off, so they are a host UB artefact. Hence the §1.5 flags.) |
| Step 6 | 217,902,482 float inputs (0.001–65,536 Hz): 16 mismatches, all within 2 ulps of an octave edge, max 4.0 × 10⁻⁴ cents. 0, −1, +inf and NaN match. |

**Not pre-validated (needs the tree edits or hardware):** Step 2's
`InstrumentManager.c` compile and the diagnostic `s0` screen; Step 3b's MPU
behaviour; every hardware listening, underrun and control check.
