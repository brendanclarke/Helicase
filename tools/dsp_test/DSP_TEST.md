# DSP test bench (`tools/dsp_test/`)

A host-side test bench for the firmware's audio DSP code. It runs on the
development Mac with the tools the project already has: Apple `cc`,
`python3` (standard library only) and the `arm-none-eabi` toolchain.
Nothing here is part of the firmware build, and nothing here runs on the
device.

It was built in Session 073 to prove that the DSP CPU refactor did not
change the sound (see `knowledge_files/log_archive/073_SESSION_HANDOFF_LOG.md`
§6). It was named `tools/dsp_golden/` until the end of that session.

---

## 1. What it does

The bench answers three kinds of question about DSP code.

| Question | Tool | Result |
|---|---|---|
| Does a rewritten function produce exactly the same output as the old one? | Host comparison programs (`test_*.c`) | Number of differing samples or bytes, maximum difference, signal-to-difference ratio |
| Does the ARM compiler generate the same floating-point work for the new code as for the old? | `fpseq.py` on objects built with the firmware's DSP flags | `MATCH` or `DIFF` on the per-sample operation multiset |
| How many instructions does one sample cost in a DSP loop? | `fpseq.py --report --all` | Loop instruction totals, old and new |

It also holds `check_special_tags.py`, which checks the instrument parameter
tables (§4.4).

### 1.1 What it cannot do

- **Judge a new sound.** A comparison needs an old version to compare
  against. For new DSP (a new Effect type, oscillator or voice) the bench
  can run the code, check its numbers, and estimate its cost, but it cannot
  say whether it sounds right. §6.3 lists what to check instead.
- **Measure cycles.** It counts instructions in a compiled loop. Cycles
  depend on the Cortex-M7 dual-issue pipeline, `VDIV` latency (about 14
  cycles), flash wait states and cache. The device's `cpu` widget and the
  underrun count under the worst-case Scene stay the real measure.
- **See link-time optimisation.** The firmware links with `-flto`, which
  inlines across files. The bench compiles single files without LTO. Loop
  bodies usually match the firmware, but check any claim that matters in the
  linked `build/lxr02.elf` with `arm-none-eabi-objdump` (S073 did this for
  the DMA pack).
- **Test hardware behaviour.** DMA ordering, the MPU, the codec, controls and
  listening all stay on the device.

---

## 2. Requirements

| Tool | Version used in S073 | Used for |
|---|---|---|
| `cc` | Apple clang 21.0.0 | Host comparison programs |
| `python3` | 3.9.6, standard library only | `extract.py`, `fpseq.py`, `check_special_tags.py` |
| `arm-none-eabi-gcc` / `objdump` | Arm GNU Toolchain 14.2.Rel1 | ARM code checks |

No extra packages. The project rule is that no new utilities are installed
for testing.

---

## 3. Layout

| Path | Role |
|---|---|
| `Makefile` | One target per check (§5). Host flags, ARM flags and the `extract.py` calls live here. |
| `prelude.h` | Force-included into every host program. It stubs firmware-only macros (`INITCM*`, `INDTCM*`, `INCCM*`), mirrors firmware constants (`OUTPUT_DMA_SIZE 32`, `AUDIO_DMA_FRAMES 96`, filter types, mixer routings), and provides `host_ssat()` for `__SSAT`, `sample_mx_t`, `sampleMix_fromInt16()` and the `golden_rand()` xorshift used for test noise. |
| `extract.py` | Copies exact function, object or line-range text out of a source file into `build/gen/`, with optional identifier renames. Tests always compile the real source text, never a transcription. |
| `snapshot.sh` | Copies the files a refactor will edit into `frozen/`, preserving their paths. Refuses to run if `frozen/` exists. |
| `frozen/` | The pre-S073 copies (14 files, byte-identical to commit `05bbd83`): `ResonantFilter.c/.h`, `BufferTools.c/.h`, `distortion.c/.h`, `Oscillator.c`, `mixer.c`, `squareRootLut.c`, the four voice `.c` files, and `AudioCodecManager.c`. |
| `fpseq.py` | Disassembles an ARM object and compares loop operation multisets (§4.3). |
| `check_special_tags.py` | Checks every instrument parameter row's special-writer tag (§4.4). |
| `test_filter.c` | ZDF filter: old against new over the full grid, both int16 and float variants, with the S1 report. Also the old-against-old self-test. |
| `test_pack.c` | DMA pack: old halfword packer against the word-store packer, byte for byte. |
| `test_postchain.c` | Voice post-chains: old separate passes against the fused helpers, all four engines. |
| `test_octave.c` | Wavetable octave selection: `log2f()` formula against the edge table, every float from 0.001 to 65,536 Hz. |
| `test_mixer.c` | Mixer: old dry + send calls against the combined function. |
| `armcheck/postchain.c`, `armcheck/mixer.c` | ARM translation units that include old and new code side by side for `fpseq.py`. |
| `build/` | Generated fragments, objects and programs. Git-ignored; `make clean` deletes it. |
| `DSP_TEST.md` | This document. |

---

## 4. How it works

### 4.1 Extracting real source (`extract.py`)

```
python3 extract.py --src FILE [--func NAME]... [--object NAME]...
                   [--lines A-B] [--rename OLD=NEW]... --out OUT.c
```

- `--func` copies one function definition, from its return type to the
  closing brace.
- `--object` copies one initialised array definition, such as
  `squareRootLut` or `osc_octaveEdgeHz`.
- `--lines A-B` copies a line range. S073 used it for loop bodies that are
  not functions of their own (the old Snare, Cymbal and HiHat post-chain
  loops).
- `--rename OLD=NEW` renames a whole identifier. Two common uses:
  - give the old and new versions different names, such as
    `mixer_addVoiceToFxBus=old_send`;
  - `--rename static=` removes `static`, so that the ARM compiler keeps a
    function as its own symbol and its loops stay readable.
- A selector that finds nothing fails the command, so a renamed or moved
  function shows up as an error rather than as a silently empty test.

### 4.2 Host builds

`HOSTFLAGS` in the `Makefile` are
`-O2 -std=gnu11 -Wall -Wno-unused-function -fno-vectorize -fno-slp-vectorize -include prelude.h`.

- **`-fno-vectorize -fno-slp-vectorize`.** In S073, clang's vectoriser made
  the old and new mixer code disagree when a test fed an out-of-range gain
  (1.5). Converting an out-of-range float to an integer is undefined
  behaviour in C. The disagreement vanished without vectorisation. Keep the
  flags, and keep test gains within the range the firmware can produce.
- **Rounding baseline.** For S1 checks (§4.5), the `filter` target builds the
  old code a second time with `-ffp-contract=off`. That shows how far
  rounding alone moves the output, so the new code is judged against the
  same scale of difference.
- The host build uses `-O2`, not `-Ofast`. Host results prove the
  arithmetic. The ARM check proves that the firmware compiler keeps it.

### 4.3 ARM code checks (`fpseq.py`)

```
python3 fpseq.py --obj OBJ --ref FUNC[:K]... --new FUNC[:K]...
                 [--shared-op OP]... [--report] [--all]
```

- **What it compares.** Every backward branch in a function marks a loop.
  For each loop it counts the value-affecting instructions: VFP arithmetic
  (`vadd`, `vmul`, `vfma`, `vdiv`, ...), conversions (`vcvt.*`) and
  saturations (`ssat`, `usat`, `qadd`, ...). Narrowing instructions (`sxth`,
  `sxtb`, `uxth`) are printed separately and are not compared. The sum over
  the `--ref` loops must equal the sum over the `--new` loops.
- **`FUNC:K`** selects loop *K* of that function, in address order. Without
  `:K`, all of the function's loops are summed.
- **Loop indices are fragile.** Loop rotation gives several backward branches
  per source loop, and loop order does not follow source order. Choose
  indices from the printed multisets, and write in the `Makefile` which loop
  each index is. A code change that moves a loop makes the line `DIFF`,
  which fails safe.
- **`--all`** counts every instruction, not only value operations. With
  `--report` the command prints the totals and never fails. The S073
  instruction counts came from this.
- **`--shared-op OP`** is for fused loops. When two old loops each converted
  the same value and the fused loop converts it once, one `OP` is removed
  from the reference before comparing. The rule is strict: an operation can
  be removed only while at least one other `--ref` loop still performs it.
  Misuse exits with an error. The multisets carry no operands, so it is the
  caller's claim, backed by the host test, that the shared conversions read
  the same value.
- **ARM flags** (`ARMFLAGS`) are the firmware's DSP flags without LTO:
  `-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -Ofast -std=gnu11`
  plus the firmware include paths.

### 4.4 Special-writer tags (`check_special_tags.py`)

Each instrument parameter row carries a tag that picks its DSP setter
(`InstrumentManager.h` `IM_SPECIAL_*`, S073 Step 2). The script reads the
four tables (`DrumParameters.c`, `SnareParameters.c`, `CymbalParameters.c`,
`HiHatParameters.c`), classifies each `file_key` with the pre-S073
string rules, and compares the result with the row's tag. Output:
`special tags OK (155 rows, 0 mismatches)`.

Run it after adding or changing any instrument parameter row. The diagnostic
firmware build does the same comparison at boot (the `s` digit on the `FxBf`
screen; `DEV_MODES.md`).

### 4.5 Sound classes and acceptance

These classes are defined in `CPU_USE_DSP_AUDIT.md` (Session 073 section).

| Class | Meaning | Acceptance on the bench |
|---|---|---|
| S0 | Bit-identical | Zero differing samples on the host **and** `MATCH` on the ARM check |
| S1 | Rounding-level only | At most 1 LSB outside self-oscillating configurations; every configuration over 16 LSB lies in the family where the rounding-only baseline also diverges; SDR at least 80 dB; no NaN or Inf |
| S2 | An int16 truncation point moves (at most 1 LSB before distortion) | Needs a listening check |
| S3 | Small behavioural change meant to be inaudible | Needs a listening A/B |
| S4 | Audible change | Not a refactor; a design decision |

---

## 5. Targets

Run from the repository root with `make -C tools/dsp_test TARGET`, or from
this directory with `make TARGET`. Results below are from 2026-09-29 on the
S073 tree.

| Target | Checks | Runtime | Expected output |
|---|---|---|---|
| `selftest` | Frozen filter against itself (old == old), 276,480,000 samples per variant | about 50 s | `differing=0` for int16 and float |
| `filter` | Rounding baseline, then frozen filter against the batched solver; S1 report | about 2.5 min | `int16 ... SDR=91.6896 dB`, `float ... SDR=86.5468 dB`, `S1 acceptance: PASS (0 failures)` |
| `armcheck-filter` | `vdiv` count in the old and new ARM filter objects | seconds | `vdiv old: 81`, `vdiv new: 39` |
| `special_tags` | §4.4 | seconds | `special tags OK (155 rows, 0 mismatches)` |
| `pack` | DMA pack, old against new, including clamp values | seconds | `pack differing bytes: 0` |
| `postchain` | Fused post-chains, four engines, all distortion shapes, gain ramps, `volumeMod` both ways. Since S085 P2 the chains take "last" endpoints; the test passes equal ones, which must equal the frozen passes (the `rst` guarantee, §9.6) | seconds | `post-chain differing samples: 0` |
| `armcheck-postchain` | Since S085 P2 a cost report: `fpseq.py --report` for Drum, Snare and Cymbal/HiHat, then `osccost.py` for the flat (`new_*`) and ramped (`ramp_*`) chains (§9.6) | seconds | `ramp_drum` 35, `ramp_snare` 35, `ramp_addgain` 30 |
| `octave` | Octave-edge selection over every float in range | seconds | `octave mismatches: 16, max edge distance: 2 ulps, max error: 0.000403883111 cents` |
| `mixer` | Combined dry + send against the old calls, every routing, stereo and mono input, ramps, near-saturation buses | seconds | `mixer differing samples: 0` |
| `armcheck-mixer` | ARM `MATCH` for all four dry × send combinations and the send-only default case, plus instruction counts | seconds | six `MATCH` lines; totals 55→44, 69→59, 73→63, 87→78 |
| `clean` | Deletes `build/` | — | — |

These targets are specific to the S073 changes. They stay valid as
regression checks only while the code they extract keeps its names and the
frozen copies stay the reference.

---

## 6. Using the bench for new DSP work

### 6.1 Refactoring existing DSP (aim: S0)

Use this when the change is meant not to alter the sound: speed-ups,
restructuring, fusing loops.

1. **Freeze the current code.** Either delete `frozen/` and re-run
   `snapshot.sh`, or copy the files you will edit into a new directory and
   point the new targets at it. Edit the file list in `snapshot.sh` to the
   files you will touch. Freeze before the first edit.
2. **Write a host comparison** `test_<topic>.c`:
   - include the generated old and new fragments from `build/gen/`;
   - drive both with the same deterministic inputs: the signals in
     `test_filter.c` (saw, square, noise, impulses, sines at 40, 220, 1,760
     and 7,000 Hz) and a sweep of every parameter over its full range;
   - compare every output sample and every piece of state that carries over
     between blocks;
   - print a single summary line and return non-zero on any mismatch.
3. **Add a `Makefile` target** that calls `extract.py` for the old side
   (from the snapshot) and the new side (from `$(ROOT)`), builds the program
   with `HOSTFLAGS`, and runs it.
4. **Add an ARM check** if the change touches floating-point work: an
   `armcheck/<topic>.c` that includes old and new side by side (use
   `--rename static=`), and `fpseq.py` lines in the `Makefile` that name each
   loop index.
5. **Keep the constant-CPU rule** (`MEMORY.md`, DSP CPU Policy): no new
   branch that skips work because of a control value or silence. Read the
   new loops for such branches as part of the review.
6. **Check the firmware:** `make clean && make all` in the repository root,
   `arm-none-eabi-size` (`data`/`bss` unchanged unless approved), and
   `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`.
7. **Hardware:** worst-case Scene for underruns and the `cpu` widget, and a
   listening check.

### 6.2 Extending shared code (a new filter type, waveform or distortion mode)

Code such as `ResonantFilter.c`, `Oscillator.c` and `distortion.c` serves
every voice and the Effects. When you add a mode:

- prove the **existing** modes are unchanged (S0) with §6.1, running the grid
  over the old mode values only;
- test the **new** mode separately with §6.3.

### 6.3 New DSP (Effect types, oscillators, voices)

There is nothing to compare against, so the checks change.

1. **Compile the new file on the host directly.** A new Effect or DSP file
   has few dependencies. Include it in a host program with `prelude.h`, and
   add small stubs for whatever else it includes (for Effects typically
   `globals.h`, `valueShaper.h` and the Effect headers). Do not compile
   `EffectsManager.c` on the host; call the type's `effect_type_ops_t`
   functions (`init`, `write_param`, `process`) directly.
2. **Drive it** with the test signals, 32-frame blocks (`io.frames = 32`),
   and sweeps of every parameter, including steps from minimum to maximum
   between two blocks.
3. **Check:**
   - no NaN or Inf, and bounded output at every extreme setting (maximum
     feedback, resonance and drive);
   - no DC offset build-up;
   - no step at a block boundary when a parameter jumps (parameters change
     once per 32-frame block, so a jump needs a ramp or smoothing);
   - for oscillators, aliasing at high pitch;
   - for Effects, correct behaviour with mono input (`io->r == NULL`) and
     with a zeroed right channel (mono-in, stereo-out).
4. **Estimate the cost.** Compile the file for ARM with `ARMFLAGS` and run
   `fpseq.py --obj FILE.o --ref FUNC --new FUNC --report --all` to get loop
   instruction totals. Compare them with §7. Make sure the loop's cost does
   not depend on parameter values (constant-CPU rule).
5. **Useful additions**, to build when the work starts (not built yet):
   - a WAV writer, so the output can be listened to on the Mac before
     flashing (a few dozen lines of C);
   - an Effect runner that drives any registered type's ops over the test
     signals and parameter sweeps;
   - a spectrum check written in C (a Goertzel filter or a small DFT), so
     nothing needs installing.

---

## 7. Reference costs (measured in S073)

Per-sample loop instruction totals from `fpseq.py --report --all` on
standalone `-Ofast` ARM objects of the S073 tree. They are instruction
counts, not cycles. `VDIV.F32` takes about 14 cycles and dominates the
filter.

| Loop | Instructions per sample | Divisions |
|---|---:|---:|
| ZDF filter, int16 (per type), before S073 | 73–89 | 5 |
| ZDF filter, int16 (per type), after S073 | 85–104 | 2 |
| ZDF filter, float Effect twin, before S073 | 73–94 | 5 (6 LP) |
| ZDF filter, float Effect twin, after S073 | 80–103 | 2 (3 LP) |
| Naive 2-pole filter | about 40–60 | 1 |
| Sine oscillator block | 23 | 0 |
| Wavetable oscillator block (saw, tri, rect) | 24 | 0 |
| Noise block (hardware RNG on phase wrap) | 12–13 | 0 |
| FM wavetable block | 35 | 0 |
| FM sine block | 30 | 0 |
| Crash sample block / FM | 30 / 38 | 0 |
| Transient sample block | 22 | 0 |
| Decimator (per slot) | 12–18 | 0 |
| Drum post-chain: old three passes → fused | 38 → 32 | 1 |
| Snare post-chain: old → fused | 35 → 30 | 1 |
| Cymbal/HiHat post-chain: old → fused | 28 → 25 | 1 |
| Distortion alone (`calcDistBlock`) | 15 | 1 |
| Mixer dry only: single output / stereo | 28 / 46 | 0 |
| Mixer send only: mono input / stereo input | 27 / 41 | 0 |
| Mixer dry + send, old → combined (single dry, mono send) | 55 → 44 | 0 |
| Mixer dry + send, old → combined (single dry, stereo send) | 69 → 59 | 0 |
| Mixer dry + send, old → combined (stereo dry, mono send) | 73 → 63 | 0 |
| Mixer dry + send, old → combined (stereo dry, stereo send) | 87 → 78 | 0 |

`INSTRUMENTS_DSP_REFERENCE.md` and `EFFECTS_MIXER_DSP_REFERENCE.md` (in
`knowledge_files/specification_reference/`) put these numbers into the
per-voice and per-block budget.

---

## 8. Pitfalls

- **Out-of-range float→int conversions** are undefined behaviour in C. Keep
  test inputs in the range the firmware can produce, or clamp them.
- **Host maths differs from the device in the last ulp.** For example, the
  host `log2f()` is not the device's. Compare against the frozen code
  compiled on the same host, never against numbers copied from the device.
- **Firmware constants are mirrored in `prelude.h`.** If `config.h` changes
  `OUTPUT_DMA_SIZE`, `AUDIO_DMA_FRAMES`, the filter configuration or the
  mixer routings, update `prelude.h` too.
- **`frozen/` shows up in code searches** as duplicate definitions. Exclude
  it (`grep --exclude-dir=frozen`).
- **Source comments point here.** `mixer.c`, `Oscillator.c`,
  `ResonantFilter.c`, `InstrumentManager.c` and `InstrumentManager.h` name
  files in this directory. Update them if the bench is reorganised or
  removed.
- **A passing bench is not a hardware pass.** The user-owned hardware checks
  (worst-case Scene, listening, controls) still gate every DSP change.

---

## 9. S085 additions

**Purpose:** the oscillator, section, sample-flash and stock-wave tests;
the per-sample cost probes; the break-proof runner. The firmware being
tested is described in `INSTRUMENTS_DSP_REFERENCE.md` §12 and
`FILESYSTEM_SPEC.md` "S085 — Sample flash catalogue v2".

### 9.1 Layout

| Path | Role |
|---|---|
| `frozen85/` | the pre-S085 snapshot (`Oscillator.c/h`, `ResonantFilter.c/h`, `BufferTools.c/h`, `Samples.c`, `mixer.c`, `modulationNode.c`, `wavetable.c`, the four engine `.c` files), taken before the first S085 edit with `sh snapshot.sh tools/dsp_test/frozen85`. The S0 reference. |
| `prelude_osc.h`, `prelude_cat.h`, `hostcompat/` | host doubles for the real oscillator and catalogue sources (portable `__SSAT`, empty `INITCM`, `SAMPLE_FLASH_PTR()` mapped onto RAM windows) |
| `host_osc_support.c` | catalogue doubles (8 samples × 100,000 frames, 4 tables × 4 frames × 32 points, stock flag) and test hooks: `host_catalogue_reset/set()`, `host_set_gain_code()`, `host_set_table_bits()` (8 = read the same bytes as int8), `host_rng_reset()`, `host_sample_data()`, `host_table_data()`; `GetRngValue()`, the interpolation gate, the saturating add, filter stubs |
| `host_sampleflash_stub.c` | a RAM image of sectors 7–11 for `catalogue` |
| `test_osc_run.c` | the oscillator layers (§9.2) |
| `test_osc_s0.c`, `s0_engines.c`, `s0_engine_shim.h` | the S0 proof against `frozen85/` |
| `test_catalogue.c`, `test_natural_order.c`, `test_stock_gen.c`, `stock_all.c` | sample flash, card order, stock waves |
| `armcheck/probe_wav.c`, `probe_pwm.c`, `probe_smp.c`, `probe_wtb.c`, `probe_section.c`, `osc_base_compat.h` | ARM cost probes (§9.3) |
| `osccost.py` | worst-case path per loop from the control-flow graph (§9.3) |
| `breakproofs.py` | the break-proof runner (§9.4) |
| `looplen.py` | instructions *outside* loops (prologue/setup); not a per-sample measure |

### 9.2 Targets

| Target | Proves |
|---|---|
| `natural_order` | numeric-aware card order (`f2 < f10`, case folding, leading zeros) and the ordering-prefix strip |
| `catalogue` | catalogue v2 states (EMPTY/STALE/V2), commit and reload, gain code (23 for peak 16,384), limits (128), fit, mixed-format frames, abort, short item (−4), torn install (no magic = EMPTY), entry bounds |
| `stock_gen` | every device stock point vs `frozen/stock_ref.bin` within ±1 LSB |
| `osc_run` | **slew:** the rendered trajectory, up and down. **itp:** the click bound in fm and mix (rst and a modulator reset must exceed it). **pool:** claim order. **snc:** no-wrap bit-identity for all nine slave families; reset on *i* + 1 for saw/smp/pwm/wtb slaves; the `itp` copy's ratio; mix law; sample master with start > 0; finished one-shot master. **rng:** the law. **smp/slp:** clamp and re-expansion, start, a 100,000-sample run, end silence, seam, wrap, gain code. **wtb:** frame-pair limits, block continuity, nx wrap, the 8-bit path. **pwm:** polarity > 0.99, F3 midpoints within 1 LSB, monotonic width. Prints the itp margins. |
| `osc_pwm` / `osc_sample` / `wtb` | the same binary with mode `pwm` / `sample` / `wtb` |
| `osc_s0` | frozen85 vs current with step 0: sine, tri, saw, rec, crash (plain and FM) and noise; a 20-block `osc_setFreq()` sweep (27.5 Hz → 10.9 kHz; increments, levels, samples); Drum fm/mix (4 cases) and Cymbal/HiHat FM2 against the **verbatim frozen engine lines** (Drum 313–326, Cymbal 240–247, HiHat 257–264), extracted with `extract.py --lines` and wrapped by `s0_engines.c` |
| `armcheck-osc-base` | worst-case loop paths of the frozen pre-S085 loops |
| `armcheck-osc` | worst-case loop paths of every S085 loop and probe |
| `ampretrig` | (S085 P2, §9.6) the gain-matched amp retrigger, the audible threshold, the post-chain ramps, no step on a `fre` retrigger |
| `breakproofs` | 35 deliberate defects (B27–B32 from S085 P2, B33–B35 from P2 F-A); each must make its target fail |

### 9.3 Cost probes (`osccost.py`, `armcheck/probe_*.c`)

**Why a new script:**
- `fpseq.py --report --all` sums every instruction inside each
  backward-branch address range. That counts both arms of an if/else and
  nested or versioned ranges.
- `looplen.py` counts what is *outside* the loops.
- Neither gives a per-sample worst case for a loop with branches.

**What `osccost.py --obj X --func F…` does:**
1. Builds the instruction-level control-flow graph.
2. Finds natural loops by dominator back edges.
3. Prints the longest path from the header round to the back edge: one
   iteration's worst case, each instruction counted once, calls as 1.
- A loop the compiler versioned (for example FM buffer NULL or not) shows
  as two loops; take the larger.
- An irreducible loop (two entries) prints nothing. Trace it by hand, as
  for `p_noise_sync`.

**Probe wrappers:**
- Each wrapper `#include`s one source file and fixes the block constants
  (the pwm family and swp segment, the `smp`/`slp` type), so its loop is
  one case.
- `*_lane` wrappers reproduce the firmware's shared plain/sync loop:
  - their *worst* path is the synced slave;
  - the plain firmware cost is the plain wrapper + 2.

**Results:** the table is in `CPU_USE_DSP_AUDIT.md` "S085 oscillator cost
record" and `INSTRUMENTS_DSP_REFERENCE.md` §12.11.

### 9.4 Break proofs (`breakproofs.py`, `make breakproofs`)

**Rule:** a test counts only if it fails on a deliberately broken version.

**How it runs:**
1. Copy `Core/`, `tools/` and `config.h` into a temporary directory.
2. For each break: apply one source edit there and run the named target.
3. Report `FAILS (good)` or `PASSES (blind)`, then restore and continue.

- The working tree is never touched.
- `python3 breakproofs.py B3 B7` runs a subset.
- Each break names the decision or finding it guards (D13, Q10, F5, …).
  When you add a test, add a break.

### 9.5 Reference figures

| | S084 | S085 (implementation) | S085 (after the assessment fixes) |
|---|---:|---:|---:|
| Flash used / 753,664 | 609,712 | 641,384 | 642,784 |
| ITCM / 16,384 | 4,168 | 10,632 | 12,432 |
| DTCM statics | 4,472 | 4,468 | 4,468 |

Per-sample oscillator costs: see §9.3.

### 9.7 S085 P2 F-A: the Drum FM depth ramp

**`osc_run` "fm depth ramp"** (`test_fm_depth_ramp()`):
1. **Exact wiring:** the modulator block equals a plain render scaled by
   the written-out ramp; the carrier equals an FM render of it.
2. **Flat:** `oscSection_renderDrumRamped(…, 1.0f)` equals
   `oscSection_renderDrum()`.
3. **Forced flat under `itp`:** with an old carrier the modulator block is
   unscaled.
4. **No step:** 16 modulator × 8 carrier start phases; block 1 at depth
   0.15, block 2 at 0.8. The ramped boundary step is never above the
   block's own largest step + 3 LSB (int16 truncation), and the worst is
   less than a quarter of the unramped one. Printed: worst 294 against
   65,311.

**Breaks:** B33 (ramp ignores its start), B34 (not forced flat under
`itp`), B35 (ramp runs the wrong way).

**Probe:** `p_fm_ramp` in `armcheck/probe_section.c` (15 per sample).

### 9.6 S085 P2: amp envelope on a `fre`/`itp` retrigger

The firmware is described in `INSTRUMENTS_DSP_REFERENCE.md` §12.6.1.

**`ampretrig`** (`test_amp_retrigger.c`; the real `SlopeEg2.c`,
`distortion.c` and `voicePostChain.h`, built with `OSCFLAGS`):
1. **Gain match:** slope 0..127, nine gains from 0 to 1, attack
   1/20/30/64/127. After `slopeEg2_retriggerFrom(g)` the attack curve at the
   new raw value equals g, the state is `EG_A`, and the following calcs
   never fall below g.
   - Tolerance (`match_tolerance()`): 1e-5, plus the gain change that
     8 float steps of v make on the attack curve. Near slope 127 the curve
     is so steep near v = 1 that float v cannot do better than ~1e-4.
2. **Repeat mode:** `repeat > 0` gives value 1, `EG_REPEAT`, the counter
   reloaded.
3. **Audible threshold:** 0 below 1/32768, 1 at and above it.
4. **Ramp wiring:** the three chains with last ≠ current, rising and
   falling, against written-out reference steps (sample 0 at the last
   gain, sample 31 at the current gain, monotonic).
5. **No step:** a Snare-style sequence: decay at slope 96 to ≈ 0.76, a
   `fre` retrigger at attack 25, the armed block, three attack blocks.
   Two conditions must hold:
   - no sample step is larger than one block's attack increment;
   - no sample dips below the gain before the trigger.

   The dip condition is needed for B28: there the armed ramp spreads the
   drop, so the step bound alone does not catch it. Prints the figures.

**`postchain`:** passes equal endpoints, so it still proves that `rst` (and
every unarmed block) is bit-identical to the S073 loops. It cannot see a
wrong ramp direction (B32); `ampretrig` check 4 does.

**`armcheck-postchain`:** the ramp adds value operations, so the three
`fpseq.py` runs are reports. In `new_*` (one variable passed twice) `-Ofast`
folds the ramp away and the counts stay 32/30/25. The firmware passes a
runtime select, so its cost is the `ramp_*` figure: **35 / 35 / 30**.
Each engine file, compiled alone, has one post-chain loop at that cost (no
versioning on the arm).

**Breaks:**

| ID | Defect | Caught by |
|---|---|---|
| B27 | inversion uses `slope`, not `invSlope` | gain match |
| B28 | the raw value is kept (no match) | gain match; no-step dip |
| B29 | `voicePost_addGainDist()` ignores `lastAmpGain` | ramp wiring |
| B30 | Drum velocity ramp endpoints swapped | ramp wiring |
| B31 | `SLOPEEG2_AUDIBLE_GAIN` = 0.01 | audible threshold |
| B32 | `voicePost_mixAddGainDist()` ramps the wrong way | ramp wiring, no-step step (`postchain` stays 0 differing) |
