# S091 P1 — CrumpBit CPU Reductions: Implementation Schedule

- **Plan:** `S091_P1_CrumpBit_Expansion.md` §2 (reductions R1–R4) and §0A
  (decisions, 2026-11-03).
- **Scope:** CPU reductions in the existing CrumpBit code only. **Nothing is
  added:** no new row, page cell, feature or RAM. The output is
  bit-identical (class S0). The features in plan §3 follow only after this
  step passes on hardware.
- **Baseline:** HEAD `93f35799` on `dev-ph7-voice_expansion`. Its image is
  byte-identical to `build/lxr02.bin` in the working tree. Every **HEAD
  line** cited below refers to that tree.
- **Pre-verified (2026-11-03):** every code block below was applied to a
  copy of HEAD in the session scratchpad (`git archive HEAD`). That copy was
  built with the firmware flags (`make all`, `make img`) and measured on the
  linked ELF, and its host laws and break proofs were run (§4). The numbers
  in this document come from that build. The code blocks were copied from
  the built files by script, not retyped.
- **Status:** implemented 2026-11-04 (see §8 for the record and gate results);
  assessed 2026-11-04 (§9). Hardware: the `cpu` widget reads 3–4 % lower
  with `cbt` (user). The listening checks H2–H6 have not been reported. The
  schedule below is unchanged.
- **Constraint:** `S089_P2_WAVEFOLDER_CHAIN.md` §8 (no per-sample increase).
  This step only lowers cost.

---

## 0. Decisions applied

| # | Decision (user) | Applied as |
|---|---|---|
| — | First implementation = the reductions only, nothing added (2026-11-03) | R1–R4 below; no feature code, no new parameter, no layout change |
| D1 (draft) | No StereoFilter tests | The hardware baseline and gate measure `off` and `cbt` only (§6) |
| D8 (draft) | +8 B RAM approved for the later features | Not used here: `CrumpBitRuntime` stays **56 B** and the union stays 76 B |
| — | DSP CPU mandate (constant cost) | Every per-sample statement still runs every frame. The Sync search runs every block with a fixed 7 steps |

---

## 1. The four reductions

| # | Change | Where | Measured effect (linked ELF) | Sound class |
|---|---|---|---|---|
| **R1** | Sync division: a fixed 7-step binary search instead of walking all 128 StepScale positions | `crumpBit_divisionFor()` (per block, from `crumpBit_targetSamples()`; also the Effect-page label) | walk **128 × 12–19 = 1,536–2,432** instructions per block → **117** instructions straight-line (≈ 48–76 → ≈ 4 per frame) | S0 |
| **R2** | 8-bit ADC with `__USAT`: removes two float clamps and the `code − (code >> 8)` fold, three times per frame | `crumpBit_adc()` | part of the 111 → 98 below (−6 `vminnm`/`vmaxnm`, −3 `sub`) | S0 |
| **R3** | `io->frames` read once into a 32-bit local | `crumpBit_process()` | `ldrb` + `uxtb` per frame → one `ldr` (the local is spilled to the stack under register pressure) | S0 |
| **R4** | AC coupling on integer code differences, converted by a fixed-point `vcvt #7` | `crumpBit_process()`, `crumpBit_init()`, `CrumpBitRuntime` | −2 `vmul`, −2 `vsub`, −2 `subs` per frame | S0 |

**Per-sample loop, longest path: 111 → 98 instructions per frame.** The
loop-filling path goes 101 → 88. There is no VDIV before or after.

---

## 2. Measured results (scratch build of HEAD + this schedule)

| Item | HEAD | After | Note |
|---|---:|---:|---|
| `crumpBit_process` per-sample loop (longest / filling) | 111 / 101 | **98 / 88** | `elf_looppath.py` |
| Sync walk per block | 1,536–2,432 (128 steps) | **117** (straight line, no loop) | `crumpBit_divisionFor` is now out of line (404 B, called once per block and by the label) |
| CrumpBit worst case per frame (loop + per block) | ≈ 191 | **≈ 106** | per block: walk + `expf` + 2 VDIV; plan §1.1 |
| `crumpBit_process` size | 1,206 B | 996 B | |
| `crumpBit_syncDivision` size | 256 B | 100 B | |
| `crumpBit_init` size | 14 B | 20 B | the two AC seeds |
| `.text` | 690,120 B | 690,160 B | +40 B |
| `.data` / `.bss` | 420 / 413,028 B | 420 / 413,028 B | unchanged |
| ITCM / DTCM statics / FXBUF | 15,240 B / 4,372 B / 126,688 B at `0x20001120` | same | `link_budget.py` |
| `CrumpBitRuntime` / `effects_runtime_t` | 56 / 76 B | 56 / 76 B | **0 B RAM change** |
| `mixer_calcNextSampleBlock` loops | — | 86 of 86 exact; hot loops 39 equal, 0 grew | the function is 4 B longer, from LTO layout only |
| Other symbols | — | `SVF_directSetFilterValue` −4 B | one alignment `nop` moved; no instruction changed |
| Image | 690,540 B | 690,580 B | `make img` OK |

**Opcode changes in the per-sample loop (HEAD → after):**

| Opcode | HEAD | After | Why |
|---|---:|---:|---|
| `vminnm` / `vmaxnm` | 3 / 3 | 0 / 0 | R2: no float clamps |
| `vcvt.u32.f32` | 4 | 1 | R2: the ADC converts signed now; the one left is the read index |
| `vcvt.s32.f32` | 0 | 3 | R2: the signed truncation that feeds `USAT` |
| `usat` | 0 | 3 | R2 |
| `vmul` | 6 | 4 | R4: the DAC scale is in the `vcvt #7` |
| `vsub` | 7 | 5 | R4: no `y − y_prev` |
| `subs` / `sub` | 3 / 4 | 1 / 3 | R2 (no fold), R4 (no `− 128`) |
| `vmov.f32` | 11 | 7 | fewer float temporaries |
| `ldrb` / `uxtb` / `ldr` | 1 / 1 / 0 | 0 / 0 / 1 | R3 (the spilled local, §7) |
| `bge` / `beq` | 1 / 0 | 0 / 1 | block layout only: the loop now exits at the `valid` test and closes on `bls` |
| `mov` | 0 | 2 | register copies for the previous codes |

**The AC-coupling shape was checked.** In the linked loop each channel is
`vcvt.f32.s32 sX, sX, #7` followed by `vfma.f32 sX, h_prev, R`, which is a
single-rounding `d/128 + R·h_prev`. That is the S074 rounding of
`(y − y_prev) + R·h_prev`, where `y − y_prev` is exact.

---

## 3. Firmware changes

All edits are in `Core/DSP/Effects/CrumpBit/`. No other firmware file
changes (§3.3).

### 3.1 `CrumpBitEffect.c`

#### C1 — add the intrinsics include (after HEAD line 19)

HEAD lines 19–20:

```c
#include "sequencer.h"
#include <math.h>
```

Add one line between them:

```c
#include "cmsis_intrinsics.h"   /* __USAT: crumpBit_adc() (S091 R2) */
```

- **Why:** R2 uses `__USAT`.
- **The header:** `Core/compat/cmsis_intrinsics.h` is already on the include
  path and already reaches this file through `sequencer.h` → `globals.h`.
  The explicit include makes the dependency visible and survives a future
  include clean-up.
- **The intrinsic:** the `usat` immediate form (`"I"` constraint).
  Affiliate: `__SSAT` in `ResonantFilter.c` and `BufferTools.h`.

#### C2 — add the R1–R4 constants (after HEAD line 43)

HEAD line 43 is `#define CRUMPBIT_DIVISION_NONE     0xFFu`. Add after it:

```c

/*
 * S091 R1-R4 constants (S091_P1_Reductions_Implementation.md).
 *
 * CRUMPBIT_CODE_MID the 8-bit code of silence: offset binary, mid-tread, so
 * code 128 decodes to 0.0. crumpBit_init() seeds the AC-coupling memory with
 * it, which is exactly the zeroed float state of the S074 form (R4).
 * CRUMPBIT_LSB_FBITS fractional bits of one 8-bit step: 1/128 = 2^-7. The
 * fixed-point VCVT in crumpBit_codeToFloat() scales by exactly 2^-7, so it
 * equals the S074 `(float)d * CRUMPBIT_LSB` bit for bit (R4).
 * Accessors: crumpBit_init(), crumpBit_codeToFloat(), crumpBit_process().
 */
#define CRUMPBIT_CODE_MID          128
#define CRUMPBIT_LSB_FBITS         7
```

- `CRUMPBIT_CODE_MID` is a signed literal on purpose: it is compared with
  and stored into `int32_t` fields.
- `CRUMPBIT_LSB_FBITS` documents the `#7` that is written literally in the
  asm string of C4. A string literal cannot take a macro value without
  stringification, which would hide the instruction.

#### C3 — add the search-size guard (before HEAD line 48)

HEAD line 48 starts the existing buffer guard
(`_Static_assert(CRUMPBIT_BUFFER_BYTES >= ...`). Insert before it:

```c
/*
 * The Sync search (R1) halves a power-of-two span: STEP_SCALE_COUNT steps
 * starting at STEP_SCALE_COUNT/2 visit every position only if the count is a
 * power of two. A future table of another size must change the search, not
 * silently skip positions.
 */
_Static_assert(STEP_SCALE_COUNT == 128u,
               "crumpBit_divisionFor() binary search assumes 128 positions");
```

- **Why:** R1 starts with a step of `STEP_SCALE_COUNT / 2` and halves it,
  which covers every position only for a power of two.
- **If the table ever grows,** this stops the build instead of silently
  skipping positions.
- **Untouched:** the size guard `sizeof(CrumpBitRuntime) == 56u` (HEAD
  line 45). R4 keeps the size.

#### C4 — replace the ADC; add the code-to-float converter (HEAD lines 53–70)

Remove HEAD lines 53–70:

```c
/*
 * Simulated bipolar 8-bit ADC (offset binary).
 *
 * What: clamps the normalised sample to +-1.0 full scale and converts it to a
 * code 0..255 with round-half-up. Silence is code 128 (mid-tread); +1.0
 * (256 before the fold) becomes 255. Branch-free: two compare-selects, one
 * multiply-add, one convert, one shift-subtract. Input: float sample. Output:
 * code 0..255. Callers: crumpBit_process(). Affiliates: CRUMPBIT_LSB.
 */
static inline uint32_t crumpBit_adc(float x)
{
    uint32_t code;

    x = (x > 1.0f) ? 1.0f : x;
    x = (x < -1.0f) ? -1.0f : x;
    code = (uint32_t)(x * 128.0f + 128.5f);
    return code - (code >> 8);
}
```

Replace with:

```c
/*
 * Simulated bipolar 8-bit ADC (offset binary).
 *
 * What: converts the normalised sample to a code 0..255 with round-half-up.
 * Silence is code 128 (mid-tread); +1.0 and above give 255, -1.0 and below
 * give 0. Branch-free: one multiply-add, one float->int convert, one USAT.
 * Why (S091 R2): the S074 form spent two float clamps and a shift-subtract
 * (`code - (code >> 8)`, folding 256 to 255) per call, three calls per frame.
 * USAT does all three jobs in one integer instruction, and the codes are the
 * same for every input (bit-identical, class S0):
 *   - |x| <= 1: y = x*128 + 128.5 lies in [0.5, 256.5]; truncation gives the
 *     S074 code 0..256, and USAT folds 256 to 255 exactly as the
 *     shift-subtract did;
 *   - x > 1: y > 256.5, truncation >= 256, USAT -> 255 (S074: clamp -> 255);
 *   - x < -1: y < 0.5; truncation toward zero gives 0 for y in (-1, 0.5) and
 *     a negative integer below that; USAT -> 0 (S074: clamp -> 0).
 * The multiply-add is the same single-rounding VFMA as before, applied to
 * the unclamped x, which only differs from the clamped x where both forms
 * saturate anyway.
 * Range: (int32_t)y is exact while |y| < 2^31; the Effect input is a sum of
 * at most six full-scale sends (|x| <= 6, |y| < 1,000). Beyond that range the
 * M7 VCVT saturates to INT32_MIN/MAX, which USAT still maps to 0/255.
 * Input: x, a normalised bus or loop-write sample. Output: code 0..255.
 * Callers: crumpBit_process() (left, right and the loop write).
 * Affiliates: __USAT (cmsis_intrinsics.h), CRUMPBIT_LSB,
 * crumpBit_codeToFloat().
 */
static inline uint32_t crumpBit_adc(float x)
{
    /* y: the analogue value on the code axis, +0.5 so truncation rounds. */
    const float y = x * 128.0f + 128.5f;

    /*
     * Truncate toward zero (VCVT.S32), then clamp to 0..255 (USAT #8): the
     * S074 clamp, convert and 256 -> 255 fold in two integer-side steps.
     */
    return __USAT((int32_t)y, 8);
}

/*
 * Exact 8-bit code (or code difference) to normalised float.
 *
 * What: returns d / 128 as a float. On the M7 this is VMOV plus one VCVT
 * with 7 fractional bits (CRUMPBIT_LSB_FBITS): the conversion itself scales by
 * 2^-7. The value is exact for |d| < 2^24 (here |d| <= 255).
 * Why (S091 R4): the S074 loop converted with VCVT and then multiplied by
 * CRUMPBIT_LSB (a third instruction). The inline asm also fixes the shape of
 * the AC-coupling expression: its result is a finished float, so -Ofast can
 * only fuse it as `result + R*h` (one VFMA), which is the S074 rounding.
 * Written in C, `(float)d * LSB + R*h` could be contracted the other way
 * (round(R*h) first), which would not be bit-identical.
 * Host builds (tools/dsp_test, no ARM VFP) use the exact C equivalent.
 * Input: d, a signed integer, |d| <= 255. Output: d * 2^-7.
 * Callers: crumpBit_process(). Affiliates: CRUMPBIT_LSB_FBITS, CRUMPBIT_LSB.
 */
static inline float crumpBit_codeToFloat(int32_t d)
{
#if defined(__arm__) && defined(__VFP_FP__) && !defined(__SOFTFP__)
    float f;

    __asm__("vmov %0, %1\n\t"
            "vcvt.f32.s32 %0, %0, #7"
            : "=t"(f)
            : "r"(d));
    return f;
#else
    return (float)d * CRUMPBIT_LSB;
#endif
}
```

**Equivalence (R2, host law CB2):**

| Input | S074 | R2 |
|---|---|---|
| −1 ≤ x ≤ 1 | `y` = `x·128 + 128.5` ∈ [0.5, 256.5], truncated to 0..256, 256 folded to 255 | the same truncation; `USAT` folds 256 to 255 |
| x > 1 | clamped to 1 → 255 | `y` > 256.5 → ≥ 256 → 255 |
| x < −1 | clamped to −1 → 0 | `y` < 0.5 → 0 or negative → 0 |

- Both forms use one VFMA for `x·128 + 128.5`. Where x is outside ±1, both
  saturate, so it does not matter that R2 applies the VFMA to the
  unclamped x.

**`crumpBit_codeToFloat()` (R4):**
- **Why asm:** it fixes the floating-point shape. C could let `-Ofast`
  contract `(float)d·LSB + R·h` around the wrong product.
- **Constraints:**
  - `"=t"`: any single-precision VFP register;
  - `"r"`: the core register holding the integer;
  - not `volatile`, so GCC may schedule it, and drop it if unused.
- **Host builds** take the C branch. `__arm__ && __VFP_FP__ && !__SOFTFP__`
  is false on the host, so host law CB3 tests the C branch.

#### C5 — replace the Sync division walk (HEAD lines 80–117)

Remove HEAD lines 80–117:

```c
/*
 * Nearest fitting division for one free-running length.
 *
 * What: walks the 128 ascending StepScale CC positions and keeps the longest
 * one <= target (low) and the shortest one > target (high), skipping any
 * longer than CRUMPBIT_DELAY_MAX_SAMPLES. It picks low when target^2 <
 * low*high, which is nearest in log time. Inputs: target length and samples per
 * tick at the current tempo. Output: a StepScale CC 0..127; 0 if nothing fits.
 * Callers: crumpBit_syncDivision(), crumpBit_targetSamples(). Affiliates:
 * stepScale_ticksQ8() (Q8.8 ticks).
 */
static uint8_t crumpBit_divisionFor(float target, float per_tick)
{
    uint8_t low = CRUMPBIT_DIVISION_NONE;
    uint8_t high = CRUMPBIT_DIVISION_NONE;
    float t_low = 0.0f;
    float t_high = 0.0f;
    uint8_t i;

    for (i = 0u; i < STEP_SCALE_COUNT; i++) {
        const float t = ((float)stepScale_ticksQ8(i) / 256.0f) * per_tick;

        if (t > CRUMPBIT_DELAY_MAX_SAMPLES)
            continue;
        if (t <= target) {
            low = i;
            t_low = t;
        } else if (high == CRUMPBIT_DIVISION_NONE) {
            high = i;
            t_high = t;
        }
    }
    if (low == CRUMPBIT_DIVISION_NONE)
        return (high == CRUMPBIT_DIVISION_NONE) ? 0u : high;
    if (high == CRUMPBIT_DIVISION_NONE)
        return low;
    return (target * target < t_low * t_high) ? low : high;
}
```

Replace with:

```c
/*
 * Length in samples of one StepScale position at the current tempo.
 *
 * What: Q8.8 ticks / 256 * samples per tick. It is the S074 expression
 * unchanged, in one place, so every length the Sync search compares is
 * computed exactly as the S074 walk computed it. Division by 256 is exact
 * (a power of two), so any reassociation -Ofast chooses gives the same bits.
 * Inputs: cc 0..127 (callers never pass more), per_tick > 0. Output: length
 * in samples. Callers: crumpBit_divisionFor(). Affiliates:
 * stepScale_ticksQ8(), CRUMPBIT_SAMPLES_PER_TICK.
 */
static inline float crumpBit_divisionLength(uint8_t cc, float per_tick)
{
    return ((float)stepScale_ticksQ8(cc) / 256.0f) * per_tick;
}

/*
 * Nearest fitting division for one free-running length (S091 R1).
 *
 * What: among the StepScale positions whose length fits
 * CRUMPBIT_DELAY_MAX_SAMPLES, finds the longest one <= target (low) and the
 * shortest one > target (high), and picks low when target^2 < low*high,
 * which is nearest in log time. Returns 0 if nothing fits (below ~4 BPM).
 * How: the 128 lengths ascend strictly with the CC (stepScale_q8Table is
 * strictly ascending and per_tick > 0), so "fits and <= target" is a prefix
 * of the table: every position below some count passes, none above it does.
 * A fixed 7-step binary search finds that count; low is count - 1 and the
 * only candidate for high is position count.
 * Why: the S074/S078 form walked all 128 positions every audio block
 * (12-19 instructions each, about 2,400 per block, ~76 per frame of the
 * render), from crumpBit_process() via crumpBit_targetSamples(). This form
 * costs about 7 search steps plus three lengths, and returns the same CC for
 * every input (class S0; host law CB1 checks every rate at BPM 1..1000).
 * Cost: constant to within the three early returns (a few instructions per
 * block), never data-dependent in the search itself.
 * Inputs: target length (samples), per_tick (samples per 96-PPQ tick at the
 * current tempo). Output: a StepScale CC 0..127.
 * Callers: crumpBit_syncDivision() (Effect-page label, foreground),
 * crumpBit_targetSamples() (render, once per block).
 * Affiliates: crumpBit_divisionLength(), stepScale_ticksQ8(),
 * STEP_SCALE_COUNT.
 */
static uint8_t crumpBit_divisionFor(float target, float per_tick)
{
    /*
     * key: one threshold for both S074 tests. A position passed the S074 walk
     * as a low candidate when it fitted (t <= MAX) and was not longer than
     * the target (t <= target), i.e. exactly when t <= min(target, MAX).
     */
    const float key = (target < CRUMPBIT_DELAY_MAX_SAMPLES)
        ? target : CRUMPBIT_DELAY_MAX_SAMPLES;
    /*
     * count: how many positions (from CC 0 up) have a length <= key. It is
     * built from the top bit down: after the step of size s, every position
     * below count is known to pass. Probing position count + s - 1 decides
     * whether the next s positions all pass (the lengths ascend, so testing
     * the last of them is enough). Steps 64, 32, ... 1 reach any count in
     * 0..128.
     */
    uint32_t count = 0u;
    uint32_t step;
    float t_low;
    float t_high;

    /* Fixed seven iterations; the add is a compare-select, not a branch. */
    for (step = STEP_SCALE_COUNT / 2u; step != 0u; step >>= 1u)
        count += (crumpBit_divisionLength((uint8_t)(count + step - 1u),
                                          per_tick) <= key) ? step : 0u;

    /*
     * No low candidate: S074 returned high if any position fitted, else 0.
     * high could only be CC 0 here, so both cases return 0.
     */
    if (count == 0u)
        return 0u;
    t_low = crumpBit_divisionLength((uint8_t)(count - 1u), per_tick);
    /* Every position passed: there is no longer one, so high is NONE. */
    if (count == STEP_SCALE_COUNT)
        return (uint8_t)(count - 1u);
    /*
     * Position count is the first that fails key. It is S074's high when it
     * still fits (then it failed only "<= target"); if it is longer than
     * MAX, no later position fits either, so high is NONE.
     */
    t_high = crumpBit_divisionLength((uint8_t)count, per_tick);
    if (t_high > CRUMPBIT_DELAY_MAX_SAMPLES)
        return (uint8_t)(count - 1u);
    /* Nearest in log time: target below the geometric mean picks low. */
    return (target * target < t_low * t_high)
        ? (uint8_t)(count - 1u) : (uint8_t)count;
}
```

**Why the result is the same CC for every input (host law CB1):**

| S074 walk | R1 |
|---|---|
| A position is a low candidate when it fits (`t ≤ MAX`) and `t ≤ target`. | That is `t ≤ key` with `key = min(target, MAX)`. |
| Lengths ascend strictly with the CC. | The passing set is the prefix `[0, count)`. |
| `low` is the last passing position. | `count − 1`. |
| `high` is the first position with `t > target` that still fits. | Only position `count` can be that, and only if `t(count) ≤ MAX` (otherwise no later position fits either). |
| The geometric-mean choice. | The same expression, on the same `t_low` and `t_high`. |
| `t` is `ticks/256·per_tick`, computed once per position. | Computed by the same expression (`crumpBit_divisionLength()`). The division by 256 is exact, so any reassociation is bit-identical. |

- **Probe bound:** before the step of size *s*, `count ≤ 128 − 2s`, so the
  probe `count + s − 1 ≤ 127 − s`: always in range. `t(count)` is read only
  when `count < 128`.
- **Cost:**
  - linked: 117 instructions straight-line, the search fully unrolled;
  - the three early returns differ by a few instructions per block;
  - the S074 walk varied by up to ≈ 900 instructions per block with tempo
    and rate.

#### C6 — seed the AC memory in init (after HEAD line 194)

HEAD lines 191–195 (the body of `crumpBit_init()`):

```c
    rt->length = 0u;
    rt->write_pos = 0u;
    rt->valid = 0u;
    rt->primed = 0u;
}
```

After `rt->primed = 0u;` (line 194), and before the closing brace, add:

```c
    /*
     * AC-coupling memory (S091 R4): the previous code of each channel. The
     * zeroed union would mean code 0 (-1.0, a full-scale step into the
     * high-pass on the first sample). Code 128 is silence, the exact
     * equivalent of the S074 zeroed float input memory (0.0f).
     */
    rt->ac_c_l = CRUMPBIT_CODE_MID;
    rt->ac_c_r = CRUMPBIT_CODE_MID;
```

- **Why it must exist:** `effects_switchRuntime()` zeroes the union before
  `init()`. Zero is exact silence for the S074 float memory, but as a code
  it means −1.0. Without the seed, the first sample after entering CrumpBit
  would see a full-scale step (break build 2 proves the test catches it).
- **Same-type Scene switch:** it does not call `init()`. The memory carries
  over, as the S074 floats did.

#### C7 — process: replace the AC input memory locals (HEAD lines 294–295)

Remove:

```c
    float ac_xl;
    float ac_xr;
```

Replace with:

```c
    /*
     * ac_cl/ac_cr (S091 R4): the previous data-line code of each channel,
     * 0..255. It replaces the S074 float input memory ac_x_* (= (code -
     * 128)/128 of the previous sample); the code difference carries the same
     * information exactly.
     */
    int32_t ac_cl;
    int32_t ac_cr;
```

`ac_yl`/`ac_yr` (HEAD lines 296–297) are unchanged.

#### C8 — process: the frame count and a 32-bit counter (HEAD lines 298–299)

Remove:

```c
    float per_frame;
    uint8_t n;
```

Replace with:

```c
    float per_frame;
    /*
     * frames (S091 R3): io->frames read once. io->frames is a uint8_t, and a
     * float store may alias any character-typed object, so the S074 loop
     * re-read it from io (and re-extended a uint8_t counter) after every
     * output store. A local copy and a 32-bit counter remove both.
     */
    uint32_t frames;
    uint32_t n;
```

#### C9 — process: read the frame count once (before HEAD line 307)

HEAD line 307 is `    left = io->l;`. Insert before it (after the
`CRUMPBIT_LOOP_MIN` return at HEAD lines 305–306, so the guards still read
`io->frames` directly):

```c
    frames = io->frames;
```

#### C10 — process: ramp step from the local (HEAD line 326)

Remove:

```c
    per_frame = 1.0f / (float)io->frames;
```

Replace with:

```c
    /* Ramp step per frame: the block's ramps land on their targets. */
    per_frame = 1.0f / (float)frames;
```

The divide is the same per-block VDIV as before, on the same value.

#### C11 — process: load the AC code memory (HEAD lines 351–352)

Remove:

```c
    ac_xl = rt->ac_x_l;
    ac_xr = rt->ac_x_r;
```

Replace with:

```c
    ac_cl = rt->ac_c_l;
    ac_cr = rt->ac_c_r;
```

#### C12 — process: loop header, ADC/data lines, DAC + AC coupling (HEAD lines 356–362)

Remove:

```c
    for (n = 0u; n < io->frames; n++) {
        const uint32_t code_l = (crumpBit_adc(left[n]) & keep) ^ invert;
        const uint32_t code_r = (crumpBit_adc(right[n]) & keep) ^ invert;
        const float yl = (float)((int32_t)code_l - 128) * CRUMPBIT_LSB;
        const float yr = (float)((int32_t)code_r - 128) * CRUMPBIT_LSB;
        const float hl = yl - ac_xl + CRUMPBIT_AC_R * ac_yl;
        const float hr = yr - ac_xr + CRUMPBIT_AC_R * ac_yr;
```

Replace with:

```c
    /*
     * One stereo frame per iteration. Every statement runs on every frame
     * whatever the settings (constant CPU); the count comes from the
     * hoisted local, so no output store forces a reload (R3).
     */
    for (n = 0u; n < frames; n++) {
        /*
         * ADC -> data lines per channel: the 8-bit code with the forced-off
         * bits cleared (keep) and the inverted bits flipped (invert), 0..255.
         */
        const int32_t code_l =
            (int32_t)((crumpBit_adc(left[n]) & keep) ^ invert);
        const int32_t code_r =
            (int32_t)((crumpBit_adc(right[n]) & keep) ^ invert);
        /*
         * DAC + AC coupling (S091 R4): h = (y - y_prev) + R*h_prev, where the
         * S074 DAC gave y = (code - 128)/128. Both y values are exact
         * multiples of 2^-7, so their difference is exactly
         * (code - code_prev)/128: the integer difference, converted once.
         * The "- 128" and one float subtract per channel disappear, and the
         * sum with R*h_prev is the same single VFMA as before.
         */
        const float hl = crumpBit_codeToFloat(code_l - ac_cl) +
                         CRUMPBIT_AC_R * ac_yl;
        const float hr = crumpBit_codeToFloat(code_r - ac_cr) +
                         CRUMPBIT_AC_R * ac_yr;
```

- **Exactness (host law CB3, all 65,536 pairs):** S074 had
  `y = (c − 128)/128` and `x = (p − 128)/128`. Both are multiples of 2⁻⁷ in
  [−1, 1], so `y − x` is exact, and it equals `(c − p)·2⁻⁷`, which the
  `vcvt #7` produces exactly.
- **The masks** (`& keep`, `^ invert`) and their order are unchanged.
- **The cast** moves from `uint32_t` to `int32_t`. The value 0..255 is the
  same, and the signed type is needed for the difference.
- **Unchanged:** the rest of the loop body, HEAD lines 363–368 and
  373–400 (ramps, glide, read, `valid` mute, the loop write through
  `crumpBit_adc()`, the output mix). C13 replaces lines 369–372 in between.
  The loop write now uses the R2 ADC, with identical codes.

#### C13 — process: AC memories (HEAD lines 369–372)

Remove:

```c
        ac_xl = yl;
        ac_yl = hl;
        ac_xr = yr;
        ac_yr = hr;
```

Replace with:

```c
        /* AC memories: this frame's code (input) and output, per channel. */
        ac_cl = code_l;
        ac_yl = hl;
        ac_cr = code_r;
        ac_yr = hr;
```

#### C14 — process: store the AC code memory (HEAD lines 405–406)

Remove:

```c
    rt->ac_x_l = ac_xl;
    rt->ac_x_r = ac_xr;
```

Replace with:

```c
    /* The last code of the block is the next block's AC input memory. */
    rt->ac_c_l = ac_cl;
    rt->ac_c_r = ac_cr;
```

### 3.2 `CrumpBitEffect.h`

#### H1 — runtime field description (HEAD line 48)

Remove:

```c
 * - ac_x_*, ac_y_*: the AC-coupling (one-pole high-pass) state per channel;
```

Replace with:

```c
 * - ac_c_*: the previous data-line code per channel, 0..255 (S091 R4; it
 *   replaced the float input memory ac_x_*). crumpBit_init() seeds 128,
 *   silence; the zeroed union value 0 would be a full-scale step;
 * - ac_y_*: the AC-coupling (one-pole high-pass) output memory per channel;
```

#### H2 — runtime fields (HEAD lines 67–68)

Remove:

```c
    float ac_x_l;
    float ac_x_r;
```

Replace with:

```c
    int32_t ac_c_l;
    int32_t ac_c_r;
```

- **Size and alignment:** `int32_t` and `float` are both 4 B, aligned 4,
  and the fields sit in the same slots. `sizeof(CrumpBitRuntime)` stays
  56 B, so the guard at `CrumpBitEffect.c` HEAD line 45 and the 76 B union
  guard in `EffectsManager.c` hold unchanged.
- **Accessors:** `CrumpBitEffect.c` only (the header rule). A repository
  search found no other reader of `ac_x_l` or `ac_x_r`.

#### H3 — `crumpBit_syncDivision()` contract text (HEAD lines 103–107)

Remove:

```c
 * pair and the geometric-mean rule: no logarithms, and a constant 128-step
 * loop. If no division fits (below about 4 BPM) it returns 0, and the DSP
 * clamps that length to the maximum. Why shared: the Effect-page label must
 * show exactly the division the DSP plays. Inputs: rate 0..127, bpm from
 * seq_getBpm() (0 is treated as 1). Output: a StepScale CC 0..127.
```

Replace with:

```c
 * pair and the geometric-mean rule: no logarithms, and a fixed 7-step binary
 * search over the 128 ascending positions (S091 R1; it replaced a 128-step
 * walk and returns the same division). If no division fits (below about
 * 4 BPM) it returns 0, and the DSP clamps that length to the maximum.
 * Why shared: the Effect-page label must show exactly the division the DSP
 * plays. Inputs: rate 0..127, bpm from seq_getBpm() (0 is treated as 1).
 * Output: a StepScale CC 0..127.
```

### 3.3 Files that do not change, and why

| File | Why it stays |
|---|---|
| `Core/DSP/Effects/EffectsManager.c` | The registry row (HEAD lines 98–119) passes `sizeof(CrumpBitRuntime)` and `CRUMPBIT_BUFFER_BYTES`; both are unchanged, as are the union guards (lines 122–132). |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | `crumpBit_uiFormatValue3()` (HEAD line 228) calls `crumpBit_syncDivision()`, which returns the same CC (CB1), so the label is unchanged. |
| `Core/Sequencer/StepScale.c/.h` | Read only. The search depends on `stepScale_q8Table` ascending strictly; it does today (1536 … 49152). |
| `Core/compat/cmsis_intrinsics.h` | `__USAT` already exists (line 155). |
| `Core/DSPAudio/mixer.c` | Effects run through the ops pointer and are not inlined into the mixer (verified: 86/86 loops exact). |
| `STORAGE_SRAM_MANIFEST.md` | No RAM change. |

---

## 4. Host tests (`tools/dsp_test`)

### 4.1 New file `tools/dsp_test/modules/crumpbit.c`

- **What it does:** compiles the real `CrumpBitEffect.c` and checks it
  against a verbatim copy of the S074 functions (renamed `ref_*`).
- **The laws:** CB1–CB5, listed in the file header.
- **Why a copied reference:** the project keeps no frozen firmware
  snapshots. A test-side reference copy is the existing convention (the
  `voicefold` reference models). It stays valid after this step as the
  definition of the S074 behaviour that S0 must match.

```c
/*
 * CrumpBit host tests (S091 P1 reductions).
 *
 * What:       compiles the real Core/DSP/Effects/CrumpBit/CrumpBitEffect.c
 *             (firmware includes stripped by extract.py) against the S074
 *             reference copied below, and checks that the S091 reductions
 *             R1-R4 are bit-identical (class S0):
 *               CB1  Sync division: crumpBit_divisionFor(),
 *                    crumpBit_syncDivision() and crumpBit_targetSamples()
 *                    against the 128-step S074 walk, for every rate at BPM
 *                    0..1000, for every exact division length (ties), and
 *                    for random targets;
 *               CB2  8-bit ADC: crumpBit_adc() against the clamped S074 form
 *                    at every code edge (+-8 ulps), across +-3 and at large
 *                    magnitudes;
 *               CB3  code-difference DAC: crumpBit_codeToFloat(c - p) equals
 *                    the S074 (c - 128)/128 - (p - 128)/128 for every pair;
 *               CB4  whole block: crumpBit_process() against the S074
 *                    process over 60 s of random blocks with parameter jumps,
 *                    out-of-range input, mono io->r == NULL, short blocks
 *                    and share changes; outputs, loop bytes and runtime
 *                    state compared bit for bit;
 *               CB5  init: the AC memory is seeded with code 128.
 * Why:        the reductions remove instructions from code whose output
 *             the board cannot report sample by sample; S0 is proved here
 *             and the ARM shape by the linked-ELF gate (DSP_TEST.md §6.4).
 * Inputs:     build/gen/cb_*.c|h, CrumpBitEffect.host.h/.c (extract.py).
 * Outputs:    one line per failure (first 40); "crumpbit failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test crumpbit (two break builds follow).
 * Affiliates: CrumpBitEffect.c/.h; StepScale.c; S091_P1_Reductions_
 *             Implementation.md §4.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Host substitutes for firmware-only pieces ------------------------------ */

/* __USAT: the ARM instruction clamps a signed value to [0, 2^bits - 1]. */
static inline uint32_t host_usat(int32_t v, unsigned bits)
{
    const int32_t hi = (int32_t)((1u << bits) - 1u);

    return (uint32_t)(v < 0 ? 0 : (v > hi ? hi : v));
}
#define __USAT(v, b) host_usat((int32_t)(v), (b))

#define EFFECT_COMMON_PARAM_AUDIO_OUT 0u
#define EFFECT_COMMON_PARAM_LEVEL     1u
#define EFFECT_COMMON_PARAM_PAN       2u

/* seq_getBpm(): the test sets the tempo the DSP reads. */
static uint16_t cb_bpm = 120u;
uint16_t seq_getBpm(void) { return cb_bpm; }

#include "cb_stepscale.h"
#include "cb_stepscale.c"
#include "cb_fxbuf.c"
#include "cb_effects.c"
#include "cb_params.c"
#include "CrumpBitEffect.host.h"
#include "CrumpBitEffect.host.c"

/*
 * S074 reference (CrumpBitEffect.c at 93f35799, renamed ref_*).
 * Copied verbatim except for the names; the constants come from the real
 * file, which R1-R4 did not change.
 */
typedef struct {
    uint32_t length;
    uint32_t write_pos;
    uint32_t valid;
    float delay;
    float mix;
    float feedback;
    float gain_l;
    float gain_r;
    float ac_x_l;
    float ac_x_r;
    float ac_y_l;
    float ac_y_r;
    uint8_t bit_off;
    uint8_t bit_invert;
    uint8_t mix_raw;
    uint8_t feedback_raw;
    uint8_t rate;
    uint8_t sync;
    uint8_t pan_raw;
    uint8_t primed;
} RefRuntime;

static inline uint32_t ref_adc(float x)
{
    uint32_t code;

    x = (x > 1.0f) ? 1.0f : x;
    x = (x < -1.0f) ? -1.0f : x;
    code = (uint32_t)(x * 128.0f + 128.5f);
    return code - (code >> 8);
}

static uint8_t ref_divisionFor(float target, float per_tick)
{
    uint8_t low = CRUMPBIT_DIVISION_NONE;
    uint8_t high = CRUMPBIT_DIVISION_NONE;
    float t_low = 0.0f;
    float t_high = 0.0f;
    uint8_t i;

    for (i = 0u; i < STEP_SCALE_COUNT; i++) {
        const float t = ((float)stepScale_ticksQ8(i) / 256.0f) * per_tick;

        if (t > CRUMPBIT_DELAY_MAX_SAMPLES)
            continue;
        if (t <= target) {
            low = i;
            t_low = t;
        } else if (high == CRUMPBIT_DIVISION_NONE) {
            high = i;
            t_high = t;
        }
    }
    if (low == CRUMPBIT_DIVISION_NONE)
        return (high == CRUMPBIT_DIVISION_NONE) ? 0u : high;
    if (high == CRUMPBIT_DIVISION_NONE)
        return low;
    return (target * target < t_low * t_high) ? low : high;
}

static uint8_t ref_syncDivision(uint8_t rate, uint16_t bpm)
{
    const float per_tick =
        CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);

    return ref_divisionFor(crumpBit_rateSamples(rate), per_tick);
}

static float ref_targetSamples(uint8_t rate, uint8_t sync, uint16_t bpm)
{
    const float per_tick =
        CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);
    const float free_run = crumpBit_rateSamples(rate);
    const float synced = ((float)stepScale_ticksQ8(
        ref_divisionFor(free_run, per_tick)) / 256.0f) * per_tick;
    const float target = sync ? synced : free_run;

    return (target > CRUMPBIT_DELAY_MAX_SAMPLES)
        ? CRUMPBIT_DELAY_MAX_SAMPLES : target;
}

static uint32_t ref_seat(RefRuntime *rt, const fx_share_t *share)
{
    uint32_t len = share ? share->bytes : 0u;

    if (len > CRUMPBIT_BUFFER_BYTES)
        len = CRUMPBIT_BUFFER_BYTES;
    if (len != rt->length) {
        rt->length = len;
        rt->write_pos = 0u;
        rt->valid = 0u;
    }
    return len;
}

static void ref_init(RefRuntime *rt)
{
    rt->length = 0u;
    rt->write_pos = 0u;
    rt->valid = 0u;
    rt->primed = 0u;
}

static void ref_writeParam(RefRuntime *rt, uint8_t index, uint8_t value)
{
    switch (index) {
    case CRUMPBIT_PARAM_BIT_OFF:    rt->bit_off = value;               break;
    case CRUMPBIT_PARAM_BIT_INVERT: rt->bit_invert = value;            break;
    case CRUMPBIT_PARAM_MIX:        rt->mix_raw = value;               break;
    case CRUMPBIT_PARAM_FEEDBACK:  rt->feedback_raw = value;          break;
    case CRUMPBIT_PARAM_RATE:       rt->rate = value;                  break;
    case CRUMPBIT_PARAM_SYNC:       rt->sync = (uint8_t)(value != 0u); break;
    case CRUMPBIT_PARAM_DLY_PAN:    rt->pan_raw = value;               break;
    default:                                                           break;
    }
}

static void ref_process(RefRuntime *rt, effect_io_t *io)
{
    float *left;
    float *right;
    int8_t *loop;
    uint32_t len;
    uint32_t keep;
    uint32_t invert;
    uint32_t wp;
    uint32_t valid;
    float flen;
    float target;
    float delay;
    float mix;
    float fb;
    float gain_l;
    float gain_r;
    float mix_to;
    float fb_to;
    float gain_l_to;
    float gain_r_to;
    float mix_inc;
    float fb_inc;
    float gain_l_inc;
    float gain_r_inc;
    float ac_xl;
    float ac_xr;
    float ac_yl;
    float ac_yr;
    float per_frame;
    uint8_t n;

    if (!rt || !io || !io->l || !io->share || !io->share->base ||
        io->frames == 0u)
        return;
    len = ref_seat(rt, io->share);
    if (len < CRUMPBIT_LOOP_MIN)
        return;
    left = io->l;
    right = io->r ? io->r : io->l;
    loop = (int8_t *)io->share->base;
    flen = (float)len;
    invert = rt->bit_invert;
    keep = (uint32_t)~((uint32_t)rt->bit_off & ~invert) & 0xFFu;
    target = ref_targetSamples(rt->rate, rt->sync, seq_getBpm());
    if (target > flen - 2.0f)
        target = flen - 2.0f;
    if (!rt->primed) {
        rt->delay = target;
        rt->primed = 1u;
    }
    per_frame = 1.0f / (float)io->frames;
    mix_to = (float)rt->mix_raw * (1.0f / 127.0f);
    fb_to = (float)rt->feedback_raw * (CRUMPBIT_FEEDBACK_MAX / 127.0f);
    gain_l_to = (rt->pan_raw <= 63u) ? 1.0f
        : (float)(127u - rt->pan_raw) * (1.0f / 64.0f);
    gain_r_to = (rt->pan_raw >= 63u) ? 1.0f
        : (float)rt->pan_raw * (1.0f / 63.0f);
    mix = rt->mix;
    fb = rt->feedback;
    gain_l = rt->gain_l;
    gain_r = rt->gain_r;
    mix_inc = (mix_to - mix) * per_frame;
    fb_inc = (fb_to - fb) * per_frame;
    gain_l_inc = (gain_l_to - gain_l) * per_frame;
    gain_r_inc = (gain_r_to - gain_r) * per_frame;
    delay = rt->delay;
    wp = rt->write_pos;
    valid = rt->valid;
    ac_xl = rt->ac_x_l;
    ac_xr = rt->ac_x_r;
    ac_yl = rt->ac_y_l;
    ac_yr = rt->ac_y_r;
    for (n = 0u; n < io->frames; n++) {
        const uint32_t code_l = (ref_adc(left[n]) & keep) ^ invert;
        const uint32_t code_r = (ref_adc(right[n]) & keep) ^ invert;
        const float yl = (float)((int32_t)code_l - 128) * CRUMPBIT_LSB;
        const float yr = (float)((int32_t)code_r - 128) * CRUMPBIT_LSB;
        const float hl = yl - ac_xl + CRUMPBIT_AC_R * ac_yl;
        const float hr = yr - ac_xr + CRUMPBIT_AC_R * ac_yr;
        float rp;
        float frac;
        float wet;
        uint32_t i0;
        uint32_t i1;

        ac_xl = yl;
        ac_yl = hl;
        ac_xr = yr;
        ac_yr = hr;
        mix += mix_inc;
        fb += fb_inc;
        gain_l += gain_l_inc;
        gain_r += gain_r_inc;
        delay += (target - delay) * CRUMPBIT_GLIDE_K;
        rp = (float)wp - delay;
        rp += (rp < 0.0f) ? flen : 0.0f;
        i0 = (uint32_t)rp;
        frac = rp - (float)i0;
        i0 -= (i0 >= len) ? len : 0u;
        i1 = i0 + 1u;
        i1 -= (i1 >= len) ? len : 0u;
        wet = ((float)loop[i0] +
               frac * (float)(loop[i1] - loop[i0])) * CRUMPBIT_LSB;
        wet = ((delay + 2.0f) <= (float)valid) ? wet : 0.0f;
        loop[wp] = (int8_t)((int32_t)ref_adc(0.5f * (hl + hr) +
                                             fb * wet) - 128);
        wp++;
        wp -= (wp >= len) ? len : 0u;
        valid += (valid < len) ? 1u : 0u;
        right[n] = hr + mix * (gain_r * wet - hr);
        left[n] = hl + mix * (gain_l * wet - hl);
    }
    rt->delay = delay;
    rt->write_pos = wp;
    rt->valid = valid;
    rt->ac_x_l = ac_xl;
    rt->ac_x_r = ac_xr;
    rt->ac_y_l = ac_yl;
    rt->ac_y_r = ac_yr;
    rt->mix = mix_to;
    rt->feedback = fb_to;
    rt->gain_l = gain_l_to;
    rt->gain_r = gain_r_to;
}

/* Test plumbing ----------------------------------------------------------- */

static int failures;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        if (failures < 40)
            printf("FAIL: %s\n", msg);
        failures++;
    }
}

static uint32_t rng_state = 0x5091u;

static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static float rndf(float lo, float hi)
{
    return lo + (hi - lo) * ((float)(rnd() & 0xFFFFFFu) / 16777215.0f);
}

static int same_bits(float a, float b)
{
    uint32_t x;
    uint32_t y;

    memcpy(&x, &a, 4u);
    memcpy(&y, &b, 4u);
    return x == y;
}

/* CB1: the Sync division ------------------------------------------------- */
static void law_cb1(void)
{
    uint32_t bpm;
    uint32_t i;
    char msg[96];

    for (bpm = 0u; bpm <= 1000u; bpm++) {
        const float per_tick =
            CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);
        uint32_t r;

        for (r = 0u; r < 128u; r++) {
            const float t = crumpBit_rateSamples((uint8_t)r);

            snprintf(msg, sizeof msg, "CB1 rate %u bpm %u", r, bpm);
            expect(crumpBit_divisionFor(t, per_tick) ==
                   ref_divisionFor(t, per_tick), msg);
            expect(crumpBit_syncDivision((uint8_t)r, (uint16_t)bpm) ==
                   ref_syncDivision((uint8_t)r, (uint16_t)bpm), msg);
            expect(same_bits(crumpBit_targetSamples((uint8_t)r, 1u,
                                                    (uint16_t)bpm),
                             ref_targetSamples((uint8_t)r, 1u,
                                               (uint16_t)bpm)), msg);
            expect(same_bits(crumpBit_targetSamples((uint8_t)r, 0u,
                                                    (uint16_t)bpm),
                             ref_targetSamples((uint8_t)r, 0u,
                                               (uint16_t)bpm)), msg);
        }
        /* Ties: a target exactly equal to each division length, and the
         * floats either side of it. */
        for (i = 0u; i < STEP_SCALE_COUNT; i++) {
            const float t = ((float)stepScale_ticksQ8((uint8_t)i) / 256.0f)
                            * per_tick;
            const float probe[3] = { t, nextafterf(t, 0.0f),
                                     nextafterf(t, INFINITY) };
            uint32_t k;

            for (k = 0u; k < 3u; k++) {
                snprintf(msg, sizeof msg, "CB1 tie cc %u bpm %u k %u",
                         i, bpm, k);
                expect(crumpBit_divisionFor(probe[k], per_tick) ==
                       ref_divisionFor(probe[k], per_tick), msg);
            }
        }
    }
    /* MAX itself and around it, random targets and tempos. */
    for (i = 0u; i < 2000000u; i++) {
        const float per_tick = rndf(0.4f, 30000.0f);
        const float t = (i & 1u)
            ? rndf(0.0f, 2.0f * CRUMPBIT_DELAY_MAX_SAMPLES)
            : CRUMPBIT_DELAY_MAX_SAMPLES * rndf(0.999f, 1.001f);

        snprintf(msg, sizeof msg, "CB1 random %u", i);
        expect(crumpBit_divisionFor(t, per_tick) ==
               ref_divisionFor(t, per_tick), msg);
    }
}

/* CB2: the 8-bit ADC ----------------------------------------------------- */
static void law_cb2(void)
{
    uint32_t i;
    int32_t k;
    char msg[96];

    for (k = -2; k <= 258; k++) {
        float x = ((float)k - 128.5f) / 128.0f;
        int32_t u;

        for (u = 0; u < 8; u++)
            x = nextafterf(x, -INFINITY);
        for (u = 0; u < 17; u++) {
            snprintf(msg, sizeof msg, "CB2 edge %d step %d", k, u);
            expect(crumpBit_adc(x) == ref_adc(x), msg);
            x = nextafterf(x, INFINITY);
        }
    }
    for (i = 0u; i < 4000000u; i++) {
        const float x = rndf(-3.0f, 3.0f);

        snprintf(msg, sizeof msg, "CB2 sweep %u", i);
        expect(crumpBit_adc(x) == ref_adc(x), msg);
    }
    {
        static const float big[] = { 1.0f, -1.0f, 0.0f, -0.0f, 6.0f, -6.0f,
                                     1.0e6f, -1.0e6f, 1.6e7f, -1.6e7f };
        for (i = 0u; i < sizeof big / sizeof big[0]; i++) {
            snprintf(msg, sizeof msg, "CB2 big %g", (double)big[i]);
            expect(crumpBit_adc(big[i]) == ref_adc(big[i]), msg);
        }
    }
}

/* CB3: code-difference DAC is exact -------------------------------------- */
static void law_cb3(void)
{
    int32_t c;
    int32_t p;
    char msg[96];

    for (c = 0; c < 256; c++)
        for (p = 0; p < 256; p++) {
            const float want = (float)(c - 128) * CRUMPBIT_LSB -
                               (float)(p - 128) * CRUMPBIT_LSB;

            snprintf(msg, sizeof msg, "CB3 c %d p %d", c, p);
            expect(same_bits(crumpBit_codeToFloat(c - p), want), msg);
        }
}

/* CB4 + CB5: the whole block --------------------------------------------- */
#define CB_ARENA 126688u

static uint8_t arena_new[CB_ARENA];
static uint8_t arena_ref[CB_ARENA];

static void law_cb4(void)
{
    static const uint32_t shares[] = { CB_ARENA, 73696u, 40000u, 70592u };
    CrumpBitRuntime rt;
    RefRuntime ref;
    fx_share_t share_new;
    fx_share_t share_ref;
    float l_new[32], r_new[32], l_ref[32], r_ref[32];
    uint32_t block;
    uint32_t share_i = 0u;
    char msg[128];

    memset(arena_new, 0x55, sizeof arena_new);
    memcpy(arena_ref, arena_new, sizeof arena_ref);
    memset(&rt, 0, sizeof rt);
    memset(&ref, 0, sizeof ref);
    crumpBit_ops.init(&rt, NULL);
    ref_init(&ref);
    expect(rt.ac_c_l == 128 && rt.ac_c_r == 128, "CB5 AC memory seeded 128");
    share_new.base = arena_new;
    share_new.offset = 0u;
    share_new.bytes = shares[0];
    share_ref = share_new;
    share_ref.base = arena_ref;

    for (block = 0u; block < 82800u; block++) {    /* 60 s at 32 frames */
        effect_io_t io_new;
        effect_io_t io_ref;
        const uint32_t mono = ((block / 997u) % 7u) == 3u;
        const uint32_t frames = ((block % 211u) == 5u)
            ? 1u + (rnd() % 32u) : 32u;
        const uint32_t kind = (block / 400u) % 5u;
        uint32_t n;

        /* Parameter jumps and LFO-like rewrites. */
        if ((block % 13u) == 0u || (block % 1000u) < 40u) {
            static const uint8_t rows[] = {
                CRUMPBIT_PARAM_BIT_OFF, CRUMPBIT_PARAM_BIT_INVERT,
                CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK,
                CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SYNC,
                CRUMPBIT_PARAM_DLY_PAN };
            const uint8_t row = rows[rnd() % 7u];
            uint8_t v = (uint8_t)(rnd() & 0x7Fu);

            if (row == CRUMPBIT_PARAM_BIT_OFF ||
                row == CRUMPBIT_PARAM_BIT_INVERT)
                v = ((rnd() & 3u) == 0u) ? (uint8_t)rnd() : 0u;
            crumpBit_ops.write_param(&rt, row, v);
            ref_writeParam(&ref, row, v);
        }
        if ((block % 5000u) == 2500u)
            cb_bpm = (uint16_t)(20u + rnd() % 280u);
        /* Share changes (voice units claimed or released). */
        if ((block % 9000u) == 8999u) {
            share_i = (share_i + 1u) % 4u;
            share_new.bytes = shares[share_i];
            share_ref.bytes = shares[share_i];
            crumpBit_ops.buffer_changed(&rt, &share_new);
            (void)ref_seat(&ref, &share_ref);
        }
        for (n = 0u; n < frames; n++) {
            float a;
            float b;

            switch (kind) {
            case 0: a = rndf(-1.0f, 1.0f); b = rndf(-1.0f, 1.0f); break;
            case 1: a = 0.0f; b = 0.0f; break;                    /* silence */
            case 2: a = rndf(-3.0f, 3.0f); b = rndf(-3.0f, 3.0f); break;
            case 3: a = 1.0f; b = -1.0f; break;                   /* full DC */
            default: a = 0.25f * sinf((float)(block * 32u + n) * 0.01f);
                     b = a * 0.5f; break;
            }
            l_new[n] = a; r_new[n] = b; l_ref[n] = a; r_ref[n] = b;
        }
        io_new.l = l_new; io_new.r = mono ? NULL : r_new;
        io_new.frames = (uint8_t)frames; io_new.channels = mono ? 1u : 2u;
        io_new.share = &share_new;
        io_ref = io_new;
        io_ref.l = l_ref; io_ref.r = mono ? NULL : r_ref;
        io_ref.share = &share_ref;
        crumpBit_ops.process(&rt, &io_new);
        ref_process(&ref, &io_ref);

        for (n = 0u; n < frames; n++) {
            snprintf(msg, sizeof msg, "CB4 block %u frame %u", block, n);
            expect(same_bits(l_new[n], l_ref[n]), msg);
            if (!mono)
                expect(same_bits(r_new[n], r_ref[n]), msg);
        }
        snprintf(msg, sizeof msg, "CB4 state block %u", block);
        expect(rt.length == ref.length && rt.write_pos == ref.write_pos &&
               rt.valid == ref.valid && same_bits(rt.delay, ref.delay) &&
               same_bits(rt.mix, ref.mix) &&
               same_bits(rt.feedback, ref.feedback) &&
               same_bits(rt.gain_l, ref.gain_l) &&
               same_bits(rt.gain_r, ref.gain_r) &&
               same_bits(rt.ac_y_l, ref.ac_y_l) &&
               same_bits(rt.ac_y_r, ref.ac_y_r) &&
               same_bits((float)(rt.ac_c_l - 128) * CRUMPBIT_LSB,
                         ref.ac_x_l) &&
               same_bits((float)(rt.ac_c_r - 128) * CRUMPBIT_LSB,
                         ref.ac_x_r), msg);
    }
    expect(memcmp(arena_new, arena_ref, sizeof arena_new) == 0,
           "CB4 loop bytes identical after 60 s");
}

int main(void)
{
    law_cb1();
    law_cb2();
    law_cb3();
    law_cb4();
    printf("crumpbit failures: %d\n", failures);
    return failures ? 1 : 0;
}
```

### 4.2 `tools/dsp_test/Makefile`

**T1 — file header list (after HEAD line 14):** add

```make
#               crumpbit            modules/crumpbit.c (+ 2 breaks: S091 R1
#                                   probe, R4 AC seed)
```

**T2 — `.PHONY` and `all` (HEAD lines 35–39):** remove

```make
.PHONY: all clean oscillator modulation voice_control sequencer \
        instrument_manager samples sampleoptimize run_modes special_tags \
        stock_tables voicefold fold_tables
all: oscillator modulation voice_control sequencer instrument_manager samples \
     sampleoptimize run_modes special_tags stock_tables voicefold fold_tables
```

and replace with

```make
.PHONY: all clean oscillator modulation voice_control sequencer \
        instrument_manager samples sampleoptimize run_modes special_tags \
        stock_tables voicefold fold_tables crumpbit crumpbit_elf_check
all: oscillator modulation voice_control sequencer instrument_manager samples \
     sampleoptimize run_modes special_tags stock_tables voicefold fold_tables \
     crumpbit
```

- `crumpbit_elf_check` is phony, but it is **not** in `all`, because it
  needs a firmware build.

**T3 — new targets (append after HEAD line 251, the end of the file):**

```make

# S091 P1: the real CrumpBitEffect.c against the S074 reference in
# modules/crumpbit.c (laws CB1-CB5, all bit-exact). Break proofs:
#   - the Sync search probing one position too far (count + step instead
#     of count + step - 1, an off-by-one in the binary search) must fail
#     CB1;
#   - the AC memory seeded 0 instead of 128 (a full-scale step on the first
#     sample after init) must fail CB4/CB5.
CB_GEN := $(GEN)/cb_fxbuf.c $(GEN)/cb_effects.c $(GEN)/cb_params.c \
          $(GEN)/cb_stepscale.c $(GEN)/cb_stepscale.h \
          $(GEN)/CrumpBitEffect.host.h $(GEN)/CrumpBitEffect.host.c
crumpbit: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSP/Effects/FxBuffer.h \
	  --typedef fx_share_t --typedef fxbuf_handoff_t \
	  --macro FXBUF_VOICE_UNIT_COUNT --macro FXBUF_EFFECT_POINTER_COUNT \
	  --macro FXBUF_HANDOFF_POINTER_COUNT \
	  --macro FXBUF_HANDOFF_EFFECT_POINTER_BASE \
	  --macro FXBUF_STATE_EFFECT_WRITTEN --out $(GEN)/cb_fxbuf.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/EffectsManager.h \
	  --typedef effect_io_t --typedef effect_type_ops_t \
	  --out $(GEN)/cb_effects.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitParameters.h \
	  --typedef crumpbit_param_t --out $(GEN)/cb_params.c
	$(EX) --src $(ROOT)/Core/Sequencer/StepScale.h \
	  --macro STEP_SCALE_COUNT --macro STEP_SCALE_DEFAULT \
	  --out $(GEN)/cb_stepscale.h
	$(EX) --src $(ROOT)/Core/Sequencer/StepScale.c \
	  --object stepScale_q8Table --func stepScale_ticksQ8 \
	  --out $(GEN)/cb_stepscale.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitEffect.h --body \
	  --out $(GEN)/CrumpBitEffect.host.h
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitEffect.c --body \
	  --out $(GEN)/CrumpBitEffect.host.c
	$(HOSTCC) $(HOSTFLAGS) modules/crumpbit.c -o build/crumpbit -lm
	./build/crumpbit
	mkdir -p build/break_cb1 build/break_cb2
	sed 's/(uint8_t)(count + step - 1u)/(uint8_t)(count + step)/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb1/CrumpBitEffect.host.c
	@grep -q '(uint8_t)(count + step),' build/break_cb1/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb1 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break1 -lm
	@if ./build/crumpbit_break1 >/dev/null 2>&1; then \
	  echo "crumpbit_break1: FAILED (probe off-by-one not caught)"; exit 1; \
	else echo "crumpbit_break1: caught (law CB1 fails as expected)"; fi
	sed 's/rt->ac_c_l = CRUMPBIT_CODE_MID;/rt->ac_c_l = 0;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb2/CrumpBitEffect.host.c
	@grep -q 'rt->ac_c_l = 0;' build/break_cb2/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb2 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break2 -lm
	@if ./build/crumpbit_break2 >/dev/null 2>&1; then \
	  echo "crumpbit_break2: FAILED (AC-seed break not caught)"; exit 1; \
	else echo "crumpbit_break2: caught (law CB4/CB5 fails as expected)"; fi

# S091 P1 ARM gate (needs `make all` at the repository root first): the
# linked crumpBit_process() per-sample loop must stay at or below 98
# instructions per frame on its longest path (111 before S091), and the
# unrolled Sync search crumpBit_divisionFor() at or below 117 instructions
# per block (the 128-step walk it replaced was up to ~2,430).
crumpbit_elf_check:
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_process --top 2 --max 98
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_divisionFor --straight --max 117
```

- **Break 1** (probe off-by-one) changes the searched position and fails
  CB1 about 5 million times.
- **Break 2** (seed 0) fails CB5 and the first CB4 frames.
- **A break considered and rejected:** probing with `<` instead of `<=`
  cannot be caught. At an exact tie, the geometric-mean test
  `t_i² < t_(i−1)·t_i` picks the same CC either way. It is therefore not a
  fault, and the tie cases in CB1 cover it.

### 4.3 New file `tools/dsp_test/elf_looppath.py`

- **Why it is needed:** `elf_loopdump.py` rejects the CrumpBit loop. The loop
  body is laid out around a literal pool (`.word`), which that tool treats
  as dispatch code. The unrolled search has no loop at all. Neither can be
  gated with the existing tools.

```python
#!/usr/bin/env python3
"""Longest per-iteration path of a function's loops, and straight-line counts.

What:       disassembles one function of the linked firmware ELF and, for
            every loop head (the target of a backward branch), walks every
            control-flow path from the head back to itself and reports the
            longest and shortest path in instructions, and the VDIVs on the
            longest. Out-of-line blocks are followed through their branches,
            so a loop whose body jumps around a literal pool (which
            elf_loopdump.py skips as dispatch code) is still measured. With
            --straight it instead counts the instructions of a function that
            has no loop (for example a fully unrolled search).
Why:        S091 P1: crumpBit_process() keeps a literal pool inside its
            per-sample loop and the unrolled Sync search has no loop at all,
            so neither elf_loopdump.py nor fpseq.py can gate them.
Inputs:     --elf PATH, --sym NAME (the base name; .lto_priv/.constprop
            suffixes are matched), optional --max N (fail if the longest
            loop path is above N), --straight (count the whole function),
            --top K (report the K longest loops), --show (print the longest
            path).
Outputs:    one line per loop; exit 1 when --max is exceeded.
Accessors:  tools/dsp_test/Makefile crumpbit_elf_check; by hand.
Affiliates: elf_loopdump.py (loop text compare), fpseq.py (value ops).
"""
import argparse
import re
import subprocess
import sys

BRANCH = re.compile(
    r'^(b|beq|bne|bcs|bhs|bcc|blo|bmi|bpl|bvs|bvc|bhi|bls|bge|blt|bgt|ble)'
    r'(\.[nw])?$|^cbn?z$')
UNCOND = {'b', 'b.n', 'b.w'}
SUFFIX = r'(\.(lto_priv|constprop|isra|part|cold)\.\d+)*'


def rows(elf, sym):
    text = subprocess.run(['arm-none-eabi-objdump', '-d', '--no-show-raw-insn',
                           elf], check=True, capture_output=True,
                          text=True).stdout
    m = re.search(r'^[0-9a-f]+ <' + re.escape(sym) + SUFFIX +
                  r'>:\n(?P<body>.*?)(?:\n\n|\Z)', text, re.S | re.M)
    if not m:
        sys.exit(f'elf_looppath.py: {sym} not found')
    out = []
    for line in m.group('body').splitlines():
        p = re.match(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)', line)
        if p and not p.group(2).startswith('.'):
            out.append((int(p.group(1), 16), p.group(2), p.group(3)))
    return out


def target(ops):
    m = re.search(r'\b([0-9a-f]+) <', ops)
    return int(m.group(1), 16) if m else None


def is_return(op, ops):
    return op == 'bx' or op.startswith('pop') or \
        (op.startswith('ldmia') and 'pc' in ops)


def successors(r, idx, i):
    _, op, ops = r[i]
    if BRANCH.match(op):
        out = []
        t = target(ops)
        if t is not None and t in idx:
            out.append(idx[t])
        if op not in UNCOND and i + 1 < len(r):
            out.append(i + 1)
        return out
    if is_return(op, ops):
        return []
    return [i + 1] if i + 1 < len(r) else []


def loop_paths(r, idx, head):
    """Every simple path head -> ... -> head, as (length, vdivs, indices)."""
    found = []
    stack = [(idx[head], [idx[head]])]
    while stack:
        i, path = stack.pop()
        for j in successors(r, idx, i):
            if r[j][0] == head:
                found.append((len(path),
                              sum('vdiv' in r[k][1] for k in path), path))
            elif j not in path and len(found) < 50000:
                stack.append((j, path + [j]))
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--sym', required=True)
    ap.add_argument('--max', type=int)
    ap.add_argument('--straight', action='store_true')
    ap.add_argument('--top', type=int, default=1)
    ap.add_argument('--show', action='store_true')
    a = ap.parse_args()
    r = rows(a.elf, a.sym)
    if a.straight:
        n = len(r)
        print(f'{a.sym}: {n} instructions (straight-line count)')
        sys.exit(1 if a.max is not None and n > a.max else 0)
    idx = {addr: i for i, (addr, _, _) in enumerate(r)}
    heads = sorted({target(ops) for (addr, op, ops) in r
                    if BRANCH.match(op) and target(ops) is not None
                    and target(ops) <= addr and target(ops) in idx})
    loops = []
    for h in heads:
        p = loop_paths(r, idx, h)
        if p:
            p.sort(key=lambda x: -x[0])
            loops.append((p[0][0], h, p))
    loops.sort(key=lambda x: -x[0])
    if not loops:
        sys.exit(f'elf_looppath.py: no loop in {a.sym}')
    for longest, h, p in loops[:a.top]:
        print(f'{a.sym}: loop head {h:#x}: longest {longest} '
              f'(vdiv {p[0][1]}), shortest {p[-1][0]}, paths {len(p)}')
    if a.show:
        for k in loops[0][2][0][2]:
            print('    ', r[k][1], r[k][2])
    if a.max is not None and loops[0][0] > a.max:
        print(f'elf_looppath.py: FAIL {a.sym} longest {loops[0][0]} > '
              f'{a.max}')
        sys.exit(1)


if __name__ == '__main__':
    main()
```

### 4.4 Results (scratch build)

```
make -C tools/dsp_test crumpbit
  crumpbit failures: 0
  crumpbit_break1: caught (law CB1 fails as expected)
  crumpbit_break2: caught (law CB4/CB5 fails as expected)
make -C tools/dsp_test crumpbit_elf_check
  crumpBit_process: loop head 0x8064f88: longest 98 (vdiv 0), shortest 98, paths 1
  crumpBit_process: loop head 0x8064fbe: longest 98 (vdiv 0), shortest 88, paths 2
  crumpBit_divisionFor: 117 instructions (straight-line count)
```

- **The same gate on the HEAD ELF fails, as intended:** the longest path is
  111, and the search check finds no `crumpBit_divisionFor` symbol.
- **Other targets in the scratch tree:**
  - `oscillator`, `modulation`, `voice_control`, `sequencer`,
    `instrument_manager`, `samples`, `sampleoptimize`, `special_tags`,
    `voicefold` and `fold_tables` all pass.
  - **Two targets already fail on the unmodified working tree.** This
    schedule neither causes nor fixes them. Record them as carried debt:
    - `run_modes`: `modules/run_modes.c` has no mirror of
      `distortion_curveSampleF()`, which the S088 P1 post-chains call (3
      compile errors).
    - `stock_tables`: the recipe runs `cd $(ROOT)`, but
      `gen_stock_tables.py` resolves `tools/Core/DSPAudio/wavetable.c`
      (file not found).
  - `make all` stops at `run_modes`. Run `crumpbit` by name until that
    target is repaired.

---

## 5. Documentation changes (same step)

### D1 — `tools/dsp_test/DSP_TEST.md`

**§3 Layout.**
- In the `modules/` list, after the `samples.c` line, add:
  `│   ├── crumpbit.c           CrumpBitEffect.c: S091 R1–R4 bit-exact against the S074 reference`
- After the `elf_hotloops.py` line, add:
  `├── elf_looppath.py          longest loop path / straight-line count of one linked function`

**§5 Targets.** Add two rows before `all`:

| Target | What it checks |
|---|---|
| `crumpbit` | S091 R1–R4: Sync division, ADC, code-difference DAC and whole blocks bit-exact against the S074 reference (CB1–CB5); two break builds |
| `crumpbit_elf_check` | linked image: the `crumpBit_process` loop ≤ 98 per frame; `crumpBit_divisionFor` ≤ 117 instructions (needs `make all` first; not in `all`) |

**Paragraph after the table.** Append this sentence:
"`elf_looppath.py --elf build/lxr02.elf --sym NAME [--top K] [--max N]
[--straight]` reports the longest per-iteration path of a function's loops
(following out-of-line blocks), or with `--straight` its instruction count."

### D2 — `EFFECTS_MIXER_DSP_REFERENCE.md` §4.4 and §6.2a

- **Line 357:** replace the `code = adc(x)` comment with
  `x·128 + 128.5 truncated, then USAT to 0..255 (S091 R2; = the S074 clamp and 256 → 255 fold)`.
- **Lines 359–360:** replace the `y` and `h` lines with:
  `h     = (code' − code'[n−1])/128 + 0.9985755·h[n−1]   DAC + AC coupling, ~10 Hz one-pole high-pass (S091 R4: the code difference is exactly y − y[n−1])`.
- **Lines 411–412 (Runtime):** replace "four AC states" with "the two
  previous data-line codes (`ac_c_*`, seeded 128 in `init`) and the two AC
  outputs (`ac_y_*`)".
- **Line 419:** replace "a 14-step division walk" with "a fixed 7-step
  binary search over the 128 StepScale positions (S091 R1; was a 128-step
  walk)".
- **Lines 420–425 (Cost).** Replace with:
  - Measured on the linked image (S091): the per-sample loop is 98
    instructions per frame (111 before S091) with no VDIV.
  - Per block: the 117-instruction search, one `expf` and two divides.
  - In total, about 106 per frame in the worst case.
  - Flash: `crumpBit_process` 996 B, `crumpBit_divisionFor` 404 B,
    `crumpBit_syncDivision` 100 B.
- **Line 703 (§6.2a table):** set the cost cell to "98 per frame (both
  channels + loop, measured S091) + per block a 117-instruction search, one
  `expf`, two divides ≈ 106 per frame".

### D3 — `CPU_USE_DSP_AUDIT.md`

After item 60 (line 988) and before the `---` that precedes "Binding
Mandates", add:

```markdown
### Session 091 P1 (record in `S091_P1_Reductions_Implementation.md`)

61. **REDUCED (S091 P1): CrumpBit (`cbt`), bit-identical (S0).** Item 27
    under-counted the type: the S078 Sync walk ran all 128 StepScale
    positions every block (128 × 12–19 = 1,536–2,432 instructions,
    ≈ 48–76 per frame), on top of a 111-instruction per-sample loop
    (≈ 191 per frame in the worst case, not 75–95).
    - R1: a fixed 7-step binary search returns the same division
      (117 instructions per block, straight line).
    - R2: the ADC uses `USAT` instead of two float clamps and a fold.
    - R3: `io->frames` is read once.
    - R4: AC coupling on integer code differences with a fixed-point
      `vcvt #7`, the same single-rounding VFMA as before.
    - Linked loop 111 → 98 per frame; total ≈ 191 → ≈ 106 per frame.
      Text +40 B; RAM 0 B; mixer loops unchanged (86/86 exact).
    - Host laws CB1–CB5 bit-exact (`make -C tools/dsp_test crumpbit`);
      gate `crumpbit_elf_check`. Hardware: H1–H6 of the schedule.
```

### D4 — `MEMORY.md` (after the hardware result)

Add a status line to the session notes near the top, following the S090
lines:

"**S091 P1 implemented** (date): CrumpBit CPU reductions, bit-identical:
binary-search Sync division, `USAT` ADC, hoisted frame count, AC coupling on
code differences. Loop 111 → 98 per frame, about −85 per frame with the
per-block walk. Host `crumpbit`, gate `crumpbit_elf_check`. Hardware
H1–H6: (result). Plan `S091_P1_CrumpBit_Expansion.md` §0A holds the feature
decisions."

---

## 6. Build, gates and hardware checks

**Order of work.**
1. Before editing, copy `build/lxr02.elf` to `build/s091_base.elf`. It is
   the before-image for G4 (no frozen snapshots are kept otherwise).
2. Apply C1–C14 and H1–H3.
3. Apply T1–T3 and the two new files.
4. Apply D1–D3. D4 waits for the hardware result.

**Gates.**

| # | Gate | Pass |
|---|---|---|
| G1 | `make all` | exit 0, no new warnings in `CrumpBitEffect.c` |
| G2 | `make img` | `Written:build/LXRV2_lxr02.img (690580b) OK` (± a few bytes if the tree differs from HEAD) |
| G3 | `make -C tools/dsp_test crumpbit` | `crumpbit failures: 0`, both breaks caught |
| G4 | `make -C tools/dsp_test crumpbit_elf_check` | loop ≤ 98, search ≤ 117 |
| G5 | Mixer loops unchanged: `elf_loopdump.py --sym mixer_calcNextSampleBlock` on `build/s091_base.elf` and on the new ELF, then `elf_hotloops.py --compare BASE NOW` and `elf_loopdump.py --compare BASE NOW` | 0 grew; 86/86 exact |
| G6 | `tools/link_budget.py` (run by `make all`) | ITCM 15,240 B, DTCM statics 4,372 B, FXBUF 126,688 B at `0x20001120` |
| G7 | `arm-none-eabi-size` | `.data` 420 B, `.bss` 413,028 B unchanged |

**Hardware checks (user).**
- **H0 (before flashing, current image):** the worst-case Scene with Effect
  `off` and `cbt`, every send open. Record the `cpu` widget and the underrun
  count over a fixed time. A DEV build with the `UNR` snippet in `main.c`
  enabled shows `audioCodec_underrunCount`.

| # | Check | Expected |
|---|---|---|
| H1 | The H0 measurement repeated on the new image | `cbt` reads lower than H0; `off` reads the same; no underruns |
| H2 | Listening: a kit through `cbt` with bit masks and inversions, Mix 0 / 64 / 127, `fbk` 0 and 127, Rate swept | identical to the old image |
| H3 | Sync on: sweep `rte` at 120 BPM and at a slow tempo (≈ 60 BPM) | the Effect-page label shows the same divisions as before; the delay snaps to them |
| H4 | Tempo change with Sync on | the delay glides to the new division, as before |
| H5 | Enter `cbt` from `off` (and from another Scene type) | no thump on entry (this is the R4 seed) |
| H6 | Same-type Scene switch with a ringing tail | the tail keeps ringing, as before |

---

## 7. Risks and notes

- **Inline asm (R4).**
  - The asm is not `volatile`, so GCC may schedule it, but it cannot
    contract across it. That is the intent.
  - **If a later compiler emits `vcvt #7` by itself,** the asm can be
    replaced with C. Re-check the VFMA shape first (§2).
  - **Host coverage:** the host branch is the plain C equivalent, so the
    host laws prove the algebra and G4/§2 prove the ARM shape.
- **`__USAT` is `asm volatile`** in `cmsis_intrinsics.h`. The linked loop
  was still scheduled tightly (98). A future change that makes it worse
  could use the GCC builtin `__usat` (`arm_acle.h`) instead: the same
  instruction, without `volatile`. The scratch prototype used `__usat`
  (101 before R4); the linked build with `__USAT` measures 98 after R4.
- **Spill of `frames`.** Under register pressure GCC keeps `frames` on the
  stack (`ldr r3, [sp]`), which costs one load per frame instead of two
  instructions. Adding live values to this loop later (the features) will
  move spills around. Re-measure with G4 at every feature step.
- **Per-block cost is not per-sample.** R1's saving is real render time (it
  runs inside `mixer_calcNextSampleBlock()` every block), but it is
  amortised over 32 frames. The S074 walk's `vcmpe`/`vmrs` pairs and
  data-dependent branches make its saving in cycles larger than its count.
- **The `valid` mute is still a branch** (98 vs 88 per frame). It is
  unchanged from S074 and is not made constant here, because the
  cheaper path is the one taken only while the loop is filling. The worst
  case is the full path, and that is what the budget uses.
- **Host floating point.** The host suite builds with `-O2` (no fast-math)
  on the host compiler. Reference and new code are compiled in one
  translation unit with the same flags, so a contraction choice applies to
  both. The `-Ofast` ARM shape is gated separately (§2, G4).

---

## 8. Implementation record (2026-11-04)

**Status: implemented in the working tree. Hardware H1–H6 pending.**

### Step 0 — baseline snapshot

- Repository HEAD was `fb4a43d2`; its only difference from the schedule's
  `93f35799` is the two S091 documents, so the firmware source is the
  scheduled baseline.
- `build/lxr02.elf` copied to `build/s091_base.elf` before any edit (G5).

### Step 1 — firmware reductions (C1–C14, H1–H3)

- `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`: C1 include, C2 constants,
  C3 search-size guard, C4 `USAT` ADC + `crumpBit_codeToFloat()`, C5
  binary-search Sync division, C6 AC seed, C7–C14 process/init changes.
  Each change carries its comment block from §3.1.
- `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`: H1 runtime description,
  H2 `ac_c_*` fields, H3 `crumpBit_syncDivision()` contract text.
- No other firmware file changed. `CrumpBitRuntime` stayed 56 B and the
  Effect union stayed 76 B, as intended (0 B RAM).

### Step 2 — host tests and tools (§4, T1–T3)

- Added `tools/dsp_test/modules/crumpbit.c` (CB1–CB5, S074 reference
  copy, two break builds).
- Added `tools/dsp_test/elf_looppath.py`.
- `tools/dsp_test/Makefile`: header list, `.PHONY`/`all`, `crumpbit` and
  `crumpbit_elf_check` targets.

### Step 3 — documentation (D1–D3)

- `tools/dsp_test/DSP_TEST.md`: layout, target rows, `elf_looppath.py`
  paragraph.
- `EFFECTS_MIXER_DSP_REFERENCE.md` §4.4 and §6.2a: the R2/R4 equations,
  the R1 search, the measured cost and symbol sizes.
- `CPU_USE_DSP_AUDIT.md`: item 61 after item 60.
- **D4 (`MEMORY.md`)** waits for the hardware result, as scheduled.

### Gate results

| # | Gate | Result |
|---|---|---|
| G1 | `make all` | exit 0; no warning from `CrumpBitEffect.c` (only the pre-existing `_close`/`_lseek`/`_read`/`_write` linker notes) |
| G2 | `make img` | `Written:build/LXRV2_lxr02.img (690580b) OK` |
| G3 | `make -C tools/dsp_test crumpbit` | `crumpbit failures: 0`; `crumpbit_break1: caught`; `crumpbit_break2: caught` |
| G4 | `make -C tools/dsp_test crumpbit_elf_check` | `crumpBit_process`: longest 98 (vdiv 0), shortest 98 and 88; `crumpBit_divisionFor`: 117 straight-line |
| G5 | mixer loops, base vs new | `86 baseline loops, 86 exact, 0 renamed, 0 missing`; hot loops `39 equal, 0 grew` |
| G6 | `tools/link_budget.py` | ITCM 15,240 B; DTCM statics 4,372 B; FXBUF 126,688 B at `0x20001120` |
| G7 | `arm-none-eabi-size` | text 690,160; data 420; bss 413,028 |

Linked symbol sizes match §2: `crumpBit_process` 996 B,
`crumpBit_divisionFor` 404 B, `crumpBit_syncDivision` 100 B,
`crumpBit_init` 20 B.

### Notes and deviations

- No deviation from the scheduled code blocks. The existing
  `/* Block ramps ... */` comment above `per_frame` was kept; C10's
  comment is adjacent to the assignment.
- `make -C tools/dsp_test all` still stops at the two carried-debt targets
  (`run_modes`, `stock_tables`) recorded in §4.4. `crumpbit` and
  `crumpbit_elf_check` were run by name, as the schedule anticipates.
- Host float coverage is the C branch of `crumpBit_codeToFloat()`; the ARM
  `vcvt #7` shape is proved by G4 (the loop counts at 98 with no VDIV).

### Pending (user)

- H0 baseline on the current image, then H1–H6 on the new image (§6).
- After H1–H6: D4 (`MEMORY.md` status line) and the feature step (plan §3).

---

## 9. Post-implementation assessment (2026-11-04)

**Verdict: the implementation matches the schedule, every gate passes when
re-run, and the ARM code of the new image computes the same values as the
old image, operation by operation. The output is bit-identical (class S0).
On hardware, the `cpu` widget reads 3–4 % lower with `cbt` (user).**

### 9.1 Code against the schedule

- **The diff of commit `5c9a92f9` against `fb4a43d2`** touches only
  `CrumpBitEffect.c` and `CrumpBitEffect.h` in `Core/`. Each change is the
  scheduled block C1–C14 / H1–H3, comments included. There is no
  unscheduled firmware change.
- **The AC seed always runs.** `effects_state.runtime_type` is written in
  only two places:
  - `effects_init()`, which sets it to `off`;
  - `effects_switchRuntime()`, which zeroes the union and then calls
    `init()`.

  So CrumpBit can only become active through `crumpBit_init()`, and the
  seed of code 128 (C6) cannot be skipped. A same-type Scene switch keeps
  the memory, as S074 did.
- **No other reader of the old fields.** A repository search finds no
  `ac_x_*` and no reader of `ac_c_*` outside `CrumpBitEffect.c/.h`.

### 9.2 Gates re-run on the committed tree

| # | Gate | Result |
|---|---|---|
| G1/G2 | `make all`, `make img` | `Written:build/LXRV2_lxr02.img (690580b) OK`. The rebuilt image is byte-identical to the committed `build/LXRV2_lxr02.img`, so the image tested on the board is this source. |
| G3 | `make -C tools/dsp_test crumpbit` | `crumpbit failures: 0`; break 1 (CB1) and break 2 (CB4/CB5) both caught |
| G4 | `crumpbit_elf_check` | loop longest 98, shortest 98 / 88, vdiv 0; `crumpBit_divisionFor` 117 straight-line |
| G4 on the base | `elf_looppath.py` on a rebuild of `fb4a43d2` | loop longest 111, shortest 111 / 101: the §2 baseline is reproduced |
| G6/G7 | `link_budget.py`, `arm-none-eabi-size` | text 690,160; data 420; bss 413,028; ITCM 15,240; DTCM statics 4,372; FXBUF 126,688 B at `0x20001120` |

### 9.3 ARM comparison against the base image

The host laws run on the host compiler at `-O2`. The board runs `-Ofast`
ARM code, which may contract and reassociate. Host laws alone therefore do
not prove the board's output. Two things are needed: the new code must not
introduce a rounding of its own, and the compiler must not have chosen a
different association. To check both, `fb4a43d2` was rebuilt in the session
scratchpad and its `crumpBit_process` was disassembled next to the new one.

| Part | Old image (`fb4a43d2`) | New image (`5c9a92f9`) | Same value? |
|---|---|---|---|
| ADC (×3) | `vminnm`, `vmaxnm`, `vfma` (x·128 + 128.5), `vcvt.u32`, fold | `vfma` (same operands, unclamped x), `vcvt.s32`, `usat #8` | Yes, for every non-NaN input (§9.4 R2) |
| AC coupling (×2) | `vcvt.f32.s32`, `vmul` (×1/128), `vsub` (y − y_prev), `vfma` (+ R·h_prev) | `vcvt.f32.s32 #7`, `vfma` (+ R·h_prev) | Yes: the same single-rounding FMA on an exact addend |
| Division length | `ticks·per_tick`, compared with `MAX·256` (‑Ofast scaled both sides by 256), then ×2⁻⁸ | `ticks·(per_tick·2⁻⁸)`, compared with `min(target, MAX)`; the high candidate compared as `ticks·per_tick` against `MAX·256` | Yes: power-of-two scaling is exact |
| Geometric mean | `vmul target²`, `vmul t_low·t_high`, `vcmpe`, `movpl` | `vmul target²`, `(t_low·2⁻⁸)·(256·t_high)`, `vcmpe`, `movpl` | Yes: both products round once from the same exact value |
| Interpolated read, glide, loop write, mix and pan output | `vrintz`/`vsub`/`vfma` read; `vfma` glide; `vfma(0.5, hL + hR, fb·wet)`; `vfma(mix, g·wet − h, h)` | the same operations in the same association | Yes |

### 9.4 Why the output is bit-identical

The principle is the same for all four changes. **None of them adds or moves
a rounding step.** Each one removes work that was already exact, or work
whose result the remaining code reproduces exactly:
- a clamp that a later saturation already covers;
- a subtraction of two values whose difference is exact;
- a linear scan over a sorted table, replaced by a search that finds the
  same index.

Every float operation that rounds is still present, with the same operands
and the same association (§9.3).

**R3 (frame count): no arithmetic changes.** `io->frames` is read once
instead of after every store. The value cannot change during the block, so
the loop runs the same frames with the same data.

**R2 (ADC): the same integer code for every real input.** Both forms
compute `y = x·128 + 128.5` with one FMA.
- **For |x| ≤ 1** the operands are the same, so `y` is the same float in
  [0.5, 256.5]:
  - truncation gives the same integer 0..256 whether the conversion is
    signed or unsigned;
  - S074 folded 256 to 255 with `code − (code >> 8)`; `USAT #8` maps 256
    to 255 and leaves 0..255 alone.
- **For x > 1** S074 clamped to 1 and produced 255. R2 has y > 256.5, which
  truncates to at least 256, and `USAT` gives 255.
- **For x < −1** S074 clamped to −1 and produced 0. R2 has y < 0.5, which
  truncates to 0 or a negative integer, and `USAT` gives 0.
- **±∞** gives 255 / 0 in both: the M7 `vcvt` saturates.
- **The one input that differs is NaN** (S074 → 255, R2 → 0). It cannot
  occur:
  - the bus is built from integer voice samples;
  - the loop write is a finite sum of bounded values;
  - `-Ofast` already assumes finite math.

  The C4 comment "the same for every input" means every number.

**R4 (AC coupling): exact arithmetic, then the same FMA.**
- **The S074 values are exact.** S074 formed `y = (c − 128)/128` and kept
  the previous `y` as a float. Every such value is an integer times 2⁻⁷ in
  [−1, 1].
- **Their difference is exact.** `y − y_prev` is an integer times 2⁻⁷ in
  [−2, 2]. That needs at most 9 significant bits, and a float has 24, so the
  `vsub` did not round.
- **R4 produces the same value directly.** It forms `c − c_prev` as an
  integer, and `vcvt.f32.s32 #7` converts it as exactly
  `(c − c_prev)·2⁻⁷`. That is the same float S074's subtraction produced.
- **The FMA is unchanged.** Both images then compute
  `fma(R, h_prev, that value)`: one rounding, the same operands (§9.3). The
  inline asm is what fixes this shape. In C, `-Ofast` could have rounded
  `R·h_prev` first.
- **The state maps one to one.** The stored float `y_prev` and the stored
  code `c_prev` are related by `y_prev = (c_prev − 128)/128`. The zeroed
  S074 state (0.0) is code 128, which is what `crumpBit_init()` seeds.

**R1 (Sync search): the same index, from the same floats.**
- **The passing positions are a prefix.** StepScale ticks ascend strictly
  and `per_tick > 0`. Float rounding is monotone, so the lengths
  `t(cc)` do not decrease. S074 counted a position as a low candidate when
  `t ≤ MAX` and `t ≤ target`, which is `t ≤ min(target, MAX)`. Because the
  lengths do not decrease, the positions that pass form a prefix
  `[0, count)` of the table.
- **The search finds `count`.** The 7-step binary search finds `count`
  exactly, and S074's `low` was its last element, `count − 1`.
- **Only one position can be `high`.** S074's `high` was the first
  position with `t > target` that still fits. Only position `count` can be
  that, and only when `t(count) ≤ MAX`. If it does not fit, no later one
  does either.
- **The comparisons use the same floats.**
  - Every `t` is `ticks·per_tick` rounded once and scaled by 2⁻⁸. A
    power-of-two scale commutes with rounding (no overflow or subnormals in
    this range), so each comparison sees the same value in both images,
    even where `-Ofast` moved the 256 to the other side of the comparison.
  - The geometric-mean test compares the same two once-rounded products.
- **The shared label agrees.** `crumpBit_syncDivision()` uses the same
  search, so the Effect-page label shows exactly the division the DSP
  plays, as before.

**Everything downstream is unchanged** (read, glide, `valid` mute, loop
write, mix, pan, ramps). It receives the same codes, the same `h` values and
the same target, so it produces the same output samples, the same loop bytes
and the same stored state.

**Evidence:**
- **Host, CB1–CB5, bit for bit:**
  - every rate at BPM 0..1000, including the exact ties;
  - 2 million random division targets;
  - every ADC code edge;
  - all 65,536 code pairs;
  - 60 s of random blocks with parameter jumps, mono, short blocks and
    share changes: outputs, loop bytes and state.
- **ARM:** the operation-by-operation comparison of §9.3.
- **Break builds:** a wrong search or a missing seed is caught.

### 9.5 Hardware result and what it means

- **Reported (user, 2026-11-04):** with `cbt` on the device, the `cpu`
  widget reads **3–4 % lower**.
- **The instruction count predicted less.** 191 → 106 per frame is −85
  instructions, about 1.7 % of the 4,897-cycle frame. A 3–4 % drop is about
  150–200 cycles per frame.
- **The difference fits the old walk's cost in cycles.** Each step had two
  `vcmpe`/`vmrs` FPSCR transfers and data-dependent branches, so it ran at
  roughly 2 cycles per instruction. §7 anticipated this.
- **The widget is not a pure cycle meter** (plan §1.3). It also includes
  foreground latency and is not linear near 100 %. Read the 3–4 % as a
  confirmed direction and a magnitude, not as an exact cycle figure.
- **Consequence for the features (plan §3.5).** The recommended package
  (A + F, about +45 instructions per frame over S0) is straight-line FMA
  code, about 1 cycle per instruction or a little more. It should land well
  below the pre-S091 reading. The no-increase rule (§8 of S089 P2) is judged
  against the pre-S091 baseline, so the measured saving is the budget.
  Each feature step re-measures with G4 and on the board.

### 9.6 Findings and what remains

- **No defect found.**
- **Comment precision (cosmetic, optional).** The C4 comment says the codes
  are the same "for every input". NaN is the one exception (§9.4 R2), and it
  is unreachable. A wording change is optional and does not affect the
  image.
- **Carried debt (unchanged):** `make -C tools/dsp_test all` still stops at
  `run_modes` and `stock_tables` (§4.4).
- **Still open (user):**
  - H1: whether the underrun count was zero, and whether `off` reads the
    same as before;
  - H2–H6: listening, Sync labels, the tempo glide, a clean entry from
    `off`, and the tail across a same-type Scene switch;
  - D4: the `MEMORY.md` status line, once H1–H6 are confirmed.
