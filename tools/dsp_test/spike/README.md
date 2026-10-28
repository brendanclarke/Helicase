# Spike Retry Kit: Transient, Filter Drive, D+S

What the S084 voice-expansion spike built for the **click/transient**, the
**filter drive types** and the **D+S Mix effect** (post-distortion plus
sample-rate reduction), kept so the work can be retried without the spike
tree. The spike text is suspect, not settled: it ran over budget, and the
shaped drive types froze the interface on hardware. Read the docs before
reusing any of it.

## Read first

| File | Why |
|---|---|
| `docs/S086_EFFECTS_CHAIN_POST_MORTEM_EVAL.md` | measured cost of every option, what fits in 88 cycles, requirements and open decisions (§8, §9) |
| `docs/S084_POST-MORTEM.md` | scope of the re-implementation |
| `docs/S084_P2_*` | transient types (`trs`/`wav`), drive types, the per-type probes (§16) |
| `docs/S084_P3_*` | the insert framework and `D+S` (VX3-6.1) |
| `docs/S084_P4_VOICE_ARCHITECTURE_REMEDIATION.md` | the interface-freeze capture plan (requirement 1) |
| `docs/S084_P5_FILTER_DRIVE_TYPE_REMEDIATION.md` | the drive character parameter |
| `docs/S084_P1_*` | the voice foundations the later phases built on |

## Contents

```
spike/
├── Makefile               make -C tools/dsp_test/spike <target>
├── docs/                  the plans, schedules and post-mortems above
├── src/                   spike text by Core path (built by the tests)
│   ├── config.h, Core/{datatypes,globals}.h, Core/compat/   spike-era headers
│   ├── Core/DSPAudio/transientGenerator.c/.h, transientTables.c/.h   transient
│   ├── Core/DSPAudio/DriveShaper.c/.h, ResonantFilter.c/.h           filter drive (SVF_calcBlockZDFShaped)
│   ├── Core/DSPAudio/distortion.c/.h                                 distortion_curveSample16
│   ├── Core/DSP/InsertEffects/InsertDriveSrr.c, InsertEffect.h,
│   │   InsertTypeCommon.h, InsertEffectsManager.c/.h                 D+S and its dispatch
│   ├── Core/DSP/Instruments/{Drum,Snare,Cymbal,HiHat}/...            integration reference (not built)
│   └── remaining headers   the include closure of the above (spike Oscillator.h, SampleMemory.h, ...)
├── baseline/Core/DSPAudio/ResonantFilter.c/.h   today's (S087) filter, the retry's reference
├── test_transient.c       PCM entries bit-identical, trs silence, sample/burst one-shots
├── test_shaper.c          driveShaper_apply() against plot_waveshapers.py --export-ref
├── test_filter.c          baseline vs repository ZDF: identical, then the S1 bounds
├── armcheck/probes.c      one loop per transient kind and per ZDF drive type
├── armcheck/drive_srr.sh  the D+S insert loop
└── looplen.py             per-loop counts for drive_srr.sh
```

## Targets

| Target | Checks |
|---|---|
| `transient` | the spike transient generator |
| `shaper` | the spike drive shapers against the Python reference (`tools/plots/plot_waveshapers.py`) |
| `filter` | `baseline/` against `Core/DSPAudio/ResonantFilter.c` (a retry must keep `sft` within S1) |
| `armcheck` | per-sample instructions: transient kinds, `sft` and each drive type |
| `armcheck-filter` | VDIV count, baseline against the repository |
| `armcheck-ds` | the D+S loops |

`transient`, `shaper`, `armcheck` and `armcheck-ds` read `src/` by default;
`make SRC=../../.. <target>` points them at the repository once a retry
lands in `Core/`. `test_filter.c` uses `prelude.h`'s `ResonantFilter`
mirror: if a retry changes the struct, update the mirror.

## Not kept

The buffer transients (`Flm`, `Bzz`, `Rvs`, `Res`, `Tub`, `Spr`, `Plk`), the
other Mix types, the voice buffers and the spike oscillator work. The
post-mortem records their costs and failures; the full spike branch is
commit `26fb7c3` of the spike tree.
