# DSP Test Bench (`tools/dsp_test`)

## 1. What it does

Host programs that compile the **real** firmware source text and check the
laws earlier sessions proved, one program per firmware module, plus the ARM
cost tools and the retry kit for the spike's effects work.

Nothing here replaces the hardware check: the device is the real test.

---

## 2. Requirements

| Tool | Used for |
|---|---|
| `cc` (Apple clang) | host programs |
| `python3` (standard library; `numpy` for `sampleoptimize`) | `extract.py`, `fpseq.py`, `elf_*.py`, `check_special_tags.py`, the optimizer test |
| `arm-none-eabi-gcc` / `objdump` | ARM cost checks (`fpseq.py`, `elf_*.py`, `spike/` probes) |

No new utilities are installed for testing.

---

## 3. Layout

```
tools/dsp_test/
├── Makefile                 module targets (make -C tools/dsp_test all)
├── DSP_TEST.md              this file
├── modules/                 one test file per firmware module
│   ├── oscillator.c         Oscillator.c: ring, sync, stock, pos, type, sample, wtb groups
│   ├── modulation.c         modulationNode.c: the sticky interpolation slots
│   ├── voice_control.c      MidiVoiceControl.c: step automation before the trigger
│   ├── sequencer.c          sequencer.c: the automation drain cursor
│   ├── instrument_manager.c InstrumentManager.c gate + the osc1_type row
│   ├── samples.c            SampleMemory/SampleInstall/storageTypes: catalogue, install, order
│   └── sampleoptimize.py    tools/sampleoptimize.py
├── spike/                   retry kit: transient, filter drive, D+S (spike/README.md)
├── prelude.h                host stand-ins (placement macros, DSP mirrors)
├── osc_host.h               host stand-ins for the whole-file Oscillator build
├── cat_host.h               maps sample-flash addresses onto a RAM image
├── catalogue_host.c         the RAM flash image (NOR rule: bits only clear)
├── extract.py               source extraction by name (see §4)
├── fpseq.py                 per-loop instruction counts of an ARM object
├── elf_symdump.py           normalised disassembly of linked functions (compare mode)
├── elf_loopdump.py          normalised loop bodies of linked functions (compare mode)
├── elf_hotloops.py          hot-loop instruction report of the linked image
└── check_special_tags.py    descriptor special-tag consistency
```

Everything generated goes to `build/` (`make clean` removes it).

---

## 4. How it works

- **Real source, extracted by name.** `extract.py` pulls `--func`,
  `--object`, `--typedef`, `--enum` and `--macro` selectors out of a source
  file, so line shifts never break a target. `--body` takes a whole file
  without its quoted `#include` lines and with
  `__attribute__((section(...)))` removed; the oscillator module builds the
  whole of `Oscillator.c/.h` this way, with `osc_host.h` supplying what the
  stripped firmware headers provided. Never extract by line range.
- **Real catalogue.** `samples` and `oscillator` link the real
  `SampleMemory.c` and `SampleInstall.c` over `catalogue_host.c`.
- **Break builds.** `voice_control`, `sequencer`, `instrument_manager` and
  `samples` also build a deliberately broken copy (a `sed` of the generated
  fragment, or a `-D` switch) that must fail; the target fails if it does
  not.
- **Groups.** A module program prints one failure count per group and exits
  non-zero on any failure.

---

## 5. Targets

| Target | What it checks |
|---|---|
| `oscillator` | S085 ring/sync laws; S086 stock waves and continuous pos (and the `OSC_POS_BEND_CAP=8` build); S087 `typ`, sample and wavetable readers |
| `modulation` | two interpolation slots, sticky holders, stamp wrap |
| `voice_control` | Scene apply → restore → drain to the mark → trigger |
| `sequencer` | the drain cursor continues, never re-applies |
| `instrument_manager` | `osc1_type` is a selector: automation yes, modulation no, no ramp, `DTYPE_MENU`, appended last |
| `samples` | catalogue v2 layout and commit order, WAV conversions, natural order and names |
| `sampleoptimize` | the optimizer never grows a table, keeps `smpl`, keeps subfolders |
| `special_tags` | descriptor special tags |
| `stock_tables` | `tools/plots/gen_stock_tables.py --check` |
| `all` | all of the above |

The ARM tools are run by hand on a build:
`fpseq.py --obj FILE.o --ref FUNC[:loop] --new FUNC[:loop] --report --all`;
`elf_symdump.py` / `elf_loopdump.py --elf build/lxr02.elf --sym NAME ... --out FILE`,
then `--compare OLD NEW` against a copy saved before the edit.

---

## 6. Using the bench for new DSP work

### 6.1 Refactoring existing DSP (aim: identical output)

1. **Keep the old text.** Copy the files you will edit to a directory under
   `build/` (or a test's own baseline directory, as `spike/baseline/` does)
   before the first edit.
2. **Write the comparison** as a group in the module's file: extract the old
   side from the copy and the new side from `Core/` with `--rename` to keep
   both, drive both with the same deterministic inputs and parameter sweeps,
   compare every output sample and all state that carries between blocks.
3. **ARM check** if the change touches a loop: `fpseq.py` on both sides, or
   `elf_symdump.py`/`elf_loopdump.py` on the linked image before and after.
4. **Keep the CPU rule** (`MEMORY.md`): no worst-case increase without the
   user's explicit approval of the number.

### 6.2 Extending shared code (a new filter type, waveform or distortion mode)

Prove the existing modes are unchanged with §6.1, then test the new mode
separately with §6.3.

### 6.3 New DSP

1. Compile the new file on the host with `prelude.h` and small stubs.
2. Drive it with test signals in 32-frame blocks and sweeps of every
   parameter, including minimum-to-maximum steps between two blocks.
3. Check: no NaN/Inf, bounded output at every extreme, no DC build-up, no
   block-boundary step, aliasing at high pitch (oscillators), mono and
   stereo input (Effects).
4. Estimate the cost with `fpseq.py` on an `-Ofast` ARM object; the loop's
   cost must not depend on parameter values.
5. Add the test as a group of the module's file in `modules/`, not as a new
   file per feature.

---

## 7. Reference costs (measured in S073)

Per-sample loop instruction totals from `fpseq.py --report --all` on
standalone `-Ofast` ARM objects. They are instruction counts, not cycles;
`VDIV.F32` takes about 14 cycles.

| Loop | Instructions per sample | Divisions |
|---|---:|---:|
| ZDF filter, int16 (per type) | 85–104 | 2 |
| ZDF filter, float Effect twin | 80–103 | 2 (3 LP) |
| Naive 2-pole filter | about 40–60 | 1 |
| Sine oscillator block | 23 | 0 |
| Wavetable oscillator block (saw, tri, rect) | 24 | 0 |
| Noise block | 12–13 | 0 |
| FM wavetable / FM sine block | 35 / 30 | 0 |
| Crash sample block / FM | 30 / 38 | 0 |
| Transient sample block | 22 | 0 |
| Decimator (per slot) | 12–18 | 0 |
| Drum / Snare / Cymbal-HiHat post-chain (fused) | 32 / 30 / 25 | 1 |
| Distortion alone (`calcDistBlock`) | 15 | 1 |

The S085–S087 oscillator figures are in the S085–S087 implementation
documents; the spike's effect costs are in
`spike/docs/S086_EFFECTS_CHAIN_POST_MORTEM_EVAL.md` §2.

---

## 8. Pitfalls

- **Out-of-range float→int conversions** are undefined behaviour in C. Keep
  test inputs in the range the firmware can produce.
- **Host maths differs from the device in the last ulp.** Compare against
  code compiled on the same host, never against numbers copied from the
  device.
- **Firmware constants are mirrored in `prelude.h` and `osc_host.h`.** If
  `config.h` changes `OUTPUT_DMA_SIZE`, `REAL_FS`, the filter configuration
  or the interpolation settings, update them too.
- **Source comments point here.** Some `Core/` comments still name the
  pre-reorganisation files (`test_osc_*.c`, `test_catalogue.c`,
  `test_natural_order.c`, `test_mixer.c`, `frozen/`); the tests now live in
  `modules/` by module.
- **A passing bench is not a hardware pass.**
