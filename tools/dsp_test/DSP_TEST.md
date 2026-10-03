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
| `postchain` | Fused post-chains, four engines, all distortion shapes, gain ramps, `volumeMod` both ways | seconds | `post-chain differing samples: 0` |
| `armcheck-postchain` | ARM `MATCH` for Drum, Snare and Cymbal/HiHat | seconds | three `MATCH` lines |
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
