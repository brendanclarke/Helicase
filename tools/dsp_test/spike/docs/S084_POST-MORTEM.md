# S084 Post-Mortem — Scope of the Re-Implementation

## 1. Where things stand

**The spike:** commit `26fb7c3` ("voice feature spike over budget") on
`dev-ph6-midi` holds all of the S084 P0–P3 work as built:
- the roll fixes;
- the oscillator expansion;
- sample flash catalogue v2, wavetables and stock waves;
- the transient types and filter drive types;
- the Mix FX stage, the voice buffers and the elastic CrumpBit;
- every review and remediation document.

**The base:** `59406a4` ("add lxr02 converters"), the end of S083, is the
last commit before any S084 change.

**Why roll back:** the spike cannot ship as it is.
- **Budget:** the transient, filter drive and Mix FX additions do not fit
  the voice budget.
- **Freeze:** on hardware, selecting any buffer-using type or a shaped drive
  type locks the interface (cause not found).
- **Defects:** several of those additions were wrong in behaviour (P3 §17).

**The oscillator work is the exception:** the oscillator and modulation-mode
work tested well on hardware.

**The plan:** roll back to `59406a4`, keep this repository directory (at
`26fb7c3`) next to the rolled-back tree as a reference, and re-implement in
three sessions. Each session starts from a clean, building tree and ends
with a hardware check before the next one starts.

## 2. Reference budget

**The baseline:** measured on the pre-S084 code (git `59406a4`, firmware
`-Ofast` flags), per voice per sample.
- **Count method:** static loop instructions.
- **Cycles:** estimated as instructions plus 13 per float divide.

| Stage | Pre-S084 worst case | Instructions | VDIV | Est. cycles |
|---|---|---:|---:|---:|
| Transient | PCM click + mix-in | 32 | 0 | ≈32 |
| Mix FX | distortion curve + decimator | 30 | 1 | ≈43 |
| Filter drive | `sft` input soft clip | 13 | 0 | ≈13 |
| **Total** | | **75** | **1** | **≈88** |

This 88-cycle figure is the budget S086 works within.

## 3. The three sessions

### Session 084 (new) — P0, the roll fixes

- **What:** re-implement the P0 roll fixes from `S084_P0_IMPLEMENTATION.md`
  and `S084_P0_ROLL_FIXUP.md`. You handle this from those documents.
- **Scope boundary:** only the P0 files: pattern data, `PatternStackService`,
  the sequencer, LiveRecord, the button handler, `MidiNotes`, and the P0
  spec sections.
  - None of the P1–P3 voice work goes in.
  - Some of those files also carry P1–P3 changes in the spike. Take only the
    hunks that the P0 documents describe.
- **Gate:**
  - builds clean;
  - the P0 host and hardware checks pass;
  - the roll behaviour is confirmed on hardware.

### Session 085 — oscillator and modulation-mode cherry-pick

- **What:** re-implement everything added to the oscillator ("osc")
  section, plus the new Drum `mod` modes `snc` and `rng` next to `fm` and
  `mix`:
  - the oscillator types `wav` / `pwm` / `smp` / `slp` / `wtb`;
  - the pwm families, the run modes `rst` / `fre` / `itp`, and the render
    pool;
  - the five new stock waves;
  - sample flash catalogue v2, with the installer and natural order;
  - `tools/sampleoptimize.py`, `tools/gen_stock_recipes.py` and the plots;
  - the menu formatting for all of the above.
- **Detailed plan:** `S085_OSC_MOD_CHERRYPICK.md`. It names every change by
  its S084 change ID, every source file in this repository to copy from, and
  what to strip from shared files (the transient, drive, Mix FX, buffer, LFO
  and swap hunks).
- **Gate:**
  - builds clean;
  - the oscillator host tests pass;
  - the oscillator loop costs are measured and recorded against the
    pre-S084 oscillator baseline;
  - the hardware check of every type, family, run mode, `snc`/`rng`, the
    sample and wavetable install, and old kits loading.

### Session 086 — the effects chain (transient + filter drive + Mix FX)

- **What:** re-scope the three stages as one swappable unit. A `chain`
  parameter in the transient section selects which transient kinds, filter
  drive types and Mix FX types are available together. Each chain fits
  inside the 88-cycle budget, so the budget is spent once, not three times.
- **Evaluation:** `S086_EFFECTS_CHAIN_POST_MORTEM_EVAL.md`. It records:
  - what worked and what did not in the spike;
  - the voice-buffer problems;
  - the measured cost of every built component;
  - what cannot fit in 88 cycles under any chain;
  - what can fit if the other stages are restricted.
- **Also in S086:** the second filter-drive parameter
  (`S084_P5_FILTER_DRIVE_TYPE_REMEDIATION.md`) and the deferred architecture
  items (`S084_P4_VOICE_ARCHITECTURE_REMEDIATION.md`).
- **First:** the hardware freeze has to be explained (§5).

## 4. S084 work not assigned to any session

These were built in the spike but are neither oscillator work nor effects
chain work. Decide whether each is re-implemented, and when.

| Item | Spike reference | Status in the spike |
|---|---|---|
| LFO one-shot waves, Scene-activation fire, `rnd` 0..1 fix (P1 WP1) | `lfo.c/h`, `MenuText.h` `lfoWaveNames`, `test_lfo_oneshot`, P1-1.x, P1 §18 F7 | host-tested; hardware H2 not reported |
| Deferred instrument swap: real level, real-time bound, declick, preview on the shared swap (P1 WP2) | `InstrumentManager.c` quiet state, `presetManager.c` swap worker, `mixer.c` declick, `test_swap_level`, P1-2.x | host-tested; hardware H3 not reported |
| DEV swap trace stage `v` (P1 WP0) | `AutosaveTrace.h`, `decode_devlogs.py` | goes with the swap |
| Elastic Effect contract, CrumpBit compaction, FxBuffer positions and leases (P3-A, WP1/WP2) | `FxBuffer.c/h`, `EffectsManager.c/h`, `CrumpBitEffect.c/h`, `test_fxbuf`, `test_crumpbit_elastic` | needed only if S086 keeps voice buffers |

## 5. The open hardware defect

- **Symptom:** selecting any buffer-using Mix or transient type, or any
  filter drive type other than `sft`, locks the interface on the spike
  build.
- **Status:** static review found no cause (P3 §17.2).
- **Why it matters to S086:** S086 re-implements both areas, so the cause
  should be found on the spike build first, with the DEV fault capture and
  render-overrun counter described in P4 §2. Otherwise the re-implementation
  can reproduce it.
- **Not a blocker for S084 or S085:** neither touches those code paths.

## 6. Working rules for all three sessions

- **Starting state:** each session works from the rolled-back tree, and
  this directory is reference only.
- **Copying from the spike:** copy only the hunks the session's document
  names. Shared files carry changes from several phases.
- **Order:** model first (Python or host simulation), then the cost probe,
  then the code, for any new DSP.
- **Tests:** a test counts only if it fails on a deliberately broken
  version.
- **Cost:** costs are measured on the compiled firmware (`armcheck`
  probes). Each DSP option is recorded against its budget before it is
  offered.
- **RAM:** allocations need explicit approval.
- **Naming:** descriptive only.
