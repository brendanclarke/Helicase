# S085 P1 — Oscillator and Modulation-Mode Cherry-Pick: Evaluation and Plan Inputs

**Session:** 085, phase P1 (evaluation only; no firmware code changed)
**Branch:** `dev-ph7-voice_expansion` (base `91265e9c` + S084 roll fix-up, uncommitted)
**Date:** 2026-10-26

**Revisions:**
- **r1:** evaluation and 15 questions.
- **r2:** first answers folded in.
- **r3:** Q6 = (a), stock waves in sample flash. Q14 = `itp` crossfades the
  carrier only (1 pool unit per voice). This also settles Q3. No questions
  remain open (§12).
- **r4:** Q6, the 1,241,408 B user space accepted; which tables live where
  (§4.3.4). Q14 confirmed: in `itp` the main oscillator (the carrier)
  crossfades and the modulators slew as in `fre` (§4.3.7).
- **r5:** a move of the core tables into sample flash was considered.
- **r6:** that move is withdrawn (user: too complicated). The plan is back
  to the r4 design: the core tables stay in program flash; the stock waves
  are generated into sample flash at install. No questions remain open.

**Inputs:** `S085_OSC_MOD_CHERRYPICK.md` (the cherry-pick document); the spike
at `Helicase-dev-ph7-spike/` (`26fb7c3`); `S084_P1_VOICE_EXPANSIONS.md`,
`S084_P1_IMPLEMENTATION.md`, `S084_P2_IMPLEMENTATION.md`,
`S084_POST-MORTEM.md`, `S086_EFFECTS_CHAIN_POST_MORTEM_EVAL.md` (all in the
spike); the current tree.

**Purpose:** decide what S085 builds before its implementation schedule is
written. The schedule is written against the current tree, not as a copy of
the spike.

---

## 0. Summary

- **The scope is sound.** The oscillator types, run modes, render pool,
  Drum `snc`/`rng`, catalogue v2, installer, stock waves and `wtb` form one
  coherent feature. On hardware they were the part of the spike that worked.
  The spike code has both review rounds' fixes applied (P1 §18, P2 §17) and
  host tests that were shown to fail on the unfixed code.
- **The cherry-pick document cannot be run in its stated order.** The
  "take whole" files are the spike's *final* state, and they depend on each
  other across its four gates (§2.3). S085 is delivered as one change set
  (Q2), so the gates become internal build-and-test points in a
  dependency-correct order (§10).
- **It has several factual errors** (§2.2). The most important: old kits'
  `wav` 6–10 become the new stock waves, not `Cym`. No migration is wanted
  (Q8), so this is recorded, not fixed.
- **S085 departs from the spike in these places:**

  | Area | Spike | S085 |
  |---|---|---|
  | Pool | 4 units | 2 units (Q14) |
  | `itp` | crossfades the whole oscillator section | crossfades the **carrier only**, 1 unit per voice; modulators keep running as in `fre` (§4.3.7) |
  | `itp` snapshot | 3 per engine | 1 per engine (Q3 no longer arises) |
  | Sample count | 233 | 128 (Q7) |
  | `wtb` loop | per-sample frame multiplies | running frame pointers (Q9) |
  | `snc` | ratio computed per wrap | ratio once per block, worst case probed against a cap (Q5) |
  | Boot notice | STALE catalogue only | also an EMPTY catalogue, since stock waves need an install (Q6) |
  | Listening defaults (Q10) | — | `snc` crossfade law; no resets after a one-shot master ends; `prc` matches `Rec`; `smp` ends at 0 |

- **Separating the scope from the spike's shared files is mechanical but
  wide:** about 20 files take hunks. Two of them (`menu.c`, `MenuText.h`)
  have changed in the current tree since the spike was built.
- **Carrier-only `itp` is new.** The spike never built it. It needs its own
  host test and a listening check (§4.3.7).

---

## 1. Starting state (measured 2026-10-26)

| Item | Value | Source |
|---|---|---|
| Current tree | `59406a4` code + the S084 roll fix-up (uncommitted) | `git status` |
| Files changed by the S084 roll fix-up | `PatternData.c/h`, `PatternStackService.c`, `ParameterArray.h`, `buttonHandler.c`, `MidiNotes.c`, **`MenuText.h`**, **`menu.c`**, `LiveRecord.c/h`, `sequencer.c/h` | `git diff 59406a4 HEAD` |
| In-scope files unchanged since `59406a4` | all of `Core/DSPAudio/`, `Core/SampleRom/`, `Core/DSP/Instruments/`, `presetManager.c`, `filesystem.c/h`, `storageTypes.c/h`, `main.c`, `config.h`, `Makefile`, `tools/dsp_test/` | same |
| Program flash (DEV) | 609,712 / 753,664 B used (headroom 143,952) | `link_budget.py` |
| ITCM / DTCM statics | 4,168 / 16,384 B; 4,472 B | same |
| Sample flash | sectors 7–11, `0x080C0000`–`0x081FFFFF`, **1,310,720 B** (1,280 KB). Before S073 it started at sector 6 and was 1,536 KB; S073 gave sector 6 to the application | `STM32F765VIHx_FLASH.ld:44`, `SampleMemory.h` |
| `runtime_slots` | 7,056 B = 6 × 1,176 B; today `sizeof(DrumVoice)` = 588 B | `nm -S` |
| Waveform interpolation today | `OSC_WAVE_INTERP_MAX_ACTIVE` 2 (`config.h:881`) | — |
| Sample RAM to be released | `sample_manifest` 13,440 + 1 B; `SampleMemory` caches 5,040 B | `nm -S` |
| Spike location | `Helicase/Helicase-dev-ph7-spike/` **inside** the working tree, kept out of git by `.gitignore:62` | `git check-ignore` |

**Note on the spike path.** `MEMORY.md` and the handoff log say
`../Helicase-dev-ph7-spike/` and call the `.gitignore` entry "unused".
Neither is true here: the spike sits inside the repository folder, and the
entry is the only thing keeping it out of git. Correct both at the S085
close-out.

---

## 2. Evaluation of `S085_OSC_MOD_CHERRYPICK.md`

### 2.1 What it gets right

- Scope boundary: osc types, sources, run modes, pool, stock waves,
  `wtb`, catalogue v2/installer, Drum `snc`/`rng`, menu formatting, tools.
  Transient, drive, Mix FX, voice buffers, LFO one-shots and the swap fix are
  out.
- Append-only rows and a missing key = 0 = today's behaviour.
- The take/leave tables for engines, InstrumentManager, presetManager and
  mixer match what the spike code contains (checked hunk by hunk, §3).
- It names the review fixes that must survive: P1 F1–F5 (F7 is LFO, so out),
  P2 F1, F2, F4, F7, F8, F10, F11, N1, N4.
- The engine rule for the transient tests is correct: keep the pre-S084
  `transGen.waveform == 0/1` tests and the fused `voicePost_drum()` /
  `_mixAddGainDist()` / `_addGainDist()`.

### 2.2 Corrections

| # | The document says | What is true | Evidence |
|---|---|---|---|
| C1 | §0.3, §15.8: old kits whose `wav` pointed at a sample clamp to `Cym` | That was P1 (6 stock waves). With 11 waves, stored 6–10 play `sq3 sw3 sqa tra swa`; 11+ clamp to `swa`. Accepted as is (Q8) | `instrumentManager_oscWaveMax()` = `OSC_STOCK_WAVE_COUNT − 1` = 10 |
| C2 | §11.1: take `IM_SPECIAL_OSC_MASK/OSC1/OSC2/OSC3/OSC_NOISE` | Already in the current tree (pre-S084). Only the four writer IDs are new | `InstrumentManager.h`, `.c:2016–3421` |
| C3 | §7.3 RAM: net ≈ −16,142 B; `sample_install` +32 B | As built: `SampleInstall` state 44 B; filesystem selection 2,313 B; `menu_installSummaryUntil` +2 B; `sample_io_buf` −4 B. Net −16,144 B | P2 §15.2, §15.8 dev. 1/4 |
| C4 | §7.1 `menu.c`: only `menu_loadSamplesModal()` | Also the summary helpers, `menu_serviceInstallSummary()` and its call in `menu_pollPresetStatus()`, the progress callback, the sample/wavetable short-name helpers | spike `menu.c` hunks at 1048, 11830, 17059 |
| C5 | §7.1 `sampleFlash.c`: "two-line change" | A comment only; no code change | spike diff |
| C6 | §5: pool RAM in SRAM1, "needs re-approval" | Plan §5.6 put it in DTCM. **Decided: SRAM1, approved (Q4)** | plan §5.6 vs P1 §0.4 |
| C7 | §16: `pwm` ≈ 30 "measure"; `smp`/`slp` "measure" | No probe exists for them, for the `snc` loop, or for the pre-S084 loops. `armcheck-vx2` covers only the stock reader and `wtb` (plus S086 probes) | `tools/dsp_test/armcheck/vx2*.c` |
| C8 | §1: Gate A = build clean + `osc_pwm`/`osc_sample`/`osc_run` | Cannot be met with the take-whole files (§2.3) | include graph |
| C9 | §0.2: `SAMPLE_MAX_COUNT` 233 | 233 served the **transient** `wav` list (out of scope). **Decided: 128 (Q7)** | plan §4.1.5 |
| C10 | §5, §6: pool of 4 units; `itp` claims one unit per oscillator and snapshots the whole section | **Decided: 2 units; `itp` crossfades the carrier only, 1 unit per voice (Q14, §4.3.7)** | — |
| C11 | Paths `../Helicase-dev-ph7-spike/` | The spike is at `Helicase-dev-ph7-spike/` inside the tree (§1) | — |

### 2.3 The gate order does not match the files

The document takes the spike's final files whole, then gates them in four
steps. The final files depend on each other across those steps:

| Step-1 file (Gate A) | Needs, from a later step |
|---|---|
| `Oscillator.c` | `StockWaves.h` (Gate C), `oscWavetable_render*` dispatch (Gate C) |
| `OscillatorPwm.c` | `StockWaves.h`, `sampleMemory_stockValid()` (Gates B/C) |
| `OscillatorSample.c` | catalogue v2 `SampleMemory` (Gate B: gain code, v2 entries) |
| `OscSection.c` | `OscillatorSample.h`, the pool |
| `Oscillator.h` | `OscWtbState` in the `pwm`/`wtb` union (Gate C) |

And `SampleMemory` v2 removes the v1 install API that `filesystem.c` calls,
so the catalogue and the installer must land in the same build. With one
change set (Q2) this is no longer a gate problem, only a build order (§10).

---

## 3. What the spike provides

| Area | Spike files | State | Separability |
|---|---|---|---|
| Oscillator data model, dispatch, per-sample slew, stock readers | `Oscillator.h/.c` | P1+P2 with fixes F2, F10; `OscInfo` 120 B | Clean (comments name `transientGenerator.c` as an affiliate) |
| Sample playback `smp`/`slp` | `OscillatorSample.h/.c` | Q20.12 phase, bind every block, gain code, `min(sourceRequest, count−1)` | Clean |
| PWM families | `OscillatorPwm.h/.c` | F3 interpolation fix; `pq3`/`pw3` natural splits; F10 | Clean |
| Render pool | `OscRenderPool.h/.c`; `config.h` 1 macro; `modulationNode.c` | 4 units, 128 B scratch + 1 B (S085: 2 units, one scratch block) | Clean (`modulationNode.c` diff is pool-only) |
| Oscillator section | `OscSection.h/.c` | run modes, `snc` (F5), `rng`, `renderFm2`, whole-section `itp` blend, filter slew (F4) | Clean; S085 reworks `itp` (§4.3.7) and `snc` (§4.3.1) |
| Filter slew helper | `ResonantFilter.c/h` `SVF_recalcFreqFrom()` | 4-line function | One hunk; the rest of the diff is drive |
| Catalogue v2 | `SampleMemory.h/.c` | v2 layout, STALE detection, gain table, `stockValid()` | Clean (comment affiliates only); counts change to 128/128 |
| Installer core | `SampleInstall.h/.c` | F4 `setFrameFormat`, F7 `abortItem`, F8 −4 | Clean |
| Card walk | `filesystem.c/h` (installer hunks), `storageTypes.c/h` | F1 long-name opens, F2 depth + open give-up, F7 per-item skip | Hunks; the base region is unchanged in the current tree |
| Stock waves | `StockWaves.h/.c`, `StockWaveGen.h/.c`, `StockWaveRecipes.c`, `tools/gen_stock_recipes.py`, `frozen/stock_ref.bin` | ±1 LSB against the reference | Clean; kept as built (Q6 (a)) |
| `wtb` | `OscillatorWavetable.h/.c` | 8/16-bit, any length; 64/70 instructions (S085: running frame pointers, Q9) | Clean |
| Formatter | `InstrumentCellFormat.h/.c` | osc kinds + transient/drive/Mix kinds + P3 hidden mask and label hooks | Subset; the header includes `InsertEffectsManager.h` |
| Engines | four `*Voice.c/h` | section, trigger gating, whole-section `itp` render | Hunks, interleaved with drive, transient `isSnap/isOffset`, insert stage, `voicePost_*Gain`; the `itp` render changes (§4.3.7) |
| Rows, pages | four `*Parameters.c/h` | rows + pages + counts | Hunks; counts must be 44/41/46/46, not the spike's 48/45/50/50 |
| Registry | `InstrumentManager.c/h` | writers, domains, claims, classifier, reserve | Hunks, interleaved with swap/quiet, LFO one-shot, insert, drive, transient |
| Preset | `presetManager.c` | selector store/apply | Hunks; the spike's versions sit inside swap scaffolding and `voiceBuffer_*Pass()` |
| Menu | `menu.c`, `MenuText.h` | formatted cells, `DTYPE_MIX_FM` ×4, wave list, modal | Hunks; the P0 hunks in the same files differ from the current tree |
| Boot notice | `main.c` | one function + one call | Hunk; the rest is insert/FxBuffer DEV dump |
| Host tests | `test_osc_pwm/sample/run`, `test_natural_order`, `test_catalogue`, `test_stock_gen`, `test_wtb`, `extract.py`, `looplen.py` | all pass in the spike; reverted-fix proofs for F2–F4, F4(P2) | Clean files; `Makefile` and `armcheck` need hunks |
| Tools | `sampleoptimize.py`, `tools/plots/*` (osc subset) | tested in r6 | Clean; `plots/README.md` also describes S086 plots |

**Not covered by any host test in the spike:**
- P1 F1 (Preset store/apply);
- P2 F1/F2 (the FAT walk);
- F10 (`osc_setFreq` key) and F11 (type writer);
- F5 (`snc` sample master);
- the `fm`/`mix` S0 claim. There is no frozen engine golden; S085 adds one
  (Q13).

---

## 4. What must be re-thought for the current tree

### 4.1 Files the roll fix-up changed since the spike

- **`MenuText.h`:** the current `perfBtnTypeNames` (15 entries) is the
  re-implemented P0. The spike's version differs in places. Add the five osc
  tables and the 11-entry `waveformNames`; do not take the spike's file.
  `lfoWaveNames`, `transientTypeNames` and `driveTypeNames` stay out.
- **`menu.c`:** same rule. The spike's hunk at `menu_getS080StaticName()`
  is P0 and is already present in another form.

### 4.2 Shared files: what stays behind

| File | Spike content that must not come across | What replaces it in S085 |
|---|---|---|
| Engines | `driveType`, `shaper`, `driveShaper_reset()`, `SVF_calcBlockZDFShaped()`; `insertType/insertAmount/stageSlot`, `insertFx_*`; `transient_isSnap/isOffset()`; `voicePost_*Gain()`; the whole-section `itp` render | `SVF_calcBlockZDF()`; `transGen.waveform == 0/1`; the fused `voicePost_drum()` / `_mixAddGainDist()` / `_addGainDist()`; carrier-only `itp` (§4.3.7) |
| `InstrumentManager.c` | quiet/peak/declick state and API; `stageInputPeak`; `insertFx_renderMix` call; drive/transient/Mix writers and classifier keys; LFO `lfo_retriggerPhase()` / Scene fire; `SLOT_DECIMATION` → `insertFx_setRate()` | `instrumentManager_ampEnvelopeQuiet()`; `mixer_decimation_rate[slot]` |
| `presetManager.c` | the swap worker, `preset_armInstrumentSwap()`, `slotQuiet` gate in the immediate Instrument apply, `preset_traceSwap()`, `voiceBuffer_beginPass/endPass()` | the current immediate apply: `resetRuntimeSlot()` then `preset_applySelectorRows()` |
| `mixer.c` | slot level/declick, decimator removal, `insertFx_service()` | keep the decimator; add only `oscRenderPool_beginBlock()` + the `itp` claims before `modNode_resetTargets()` (claim order §4.3.7) |
| `mixer.h` | `USE_DECIMATOR` removal | unchanged |
| `ResonantFilter.h` | `#include "DriveShaper.h"`, `SVF_calcBlockZDFShaped` | only `SVF_recalcFreqFrom()` |
| `InstrumentCellFormat.h/.c` | `#include "InsertEffectsManager.h"`, `hidden`, transient/drive/Mix kinds, `_step()`, `_shortLabel/_longLabel()` | the osc kinds only |
| `menu.c` | encoder hidden-skip, label-hook sites (`repaintGeneric` long/short label, step-automation short label) | the P1 call sites with `descriptor->short_name/long_name` |
| `*Parameters.c` | `ROW_SELECT_NOAUTO_SPECIAL`, `trt`/`dty`/`fxt`/`amt` rows, `dpt`/`rte` renames, transient/filter page cells | — |
| `Makefile` | InsertEffects include paths, sources and pattern rules; `DriveShaper.c` | `InstrumentCellFormat.c` (SRCS); `SampleInstall.c`, `StockWaveGen.c`, `StockWaveRecipes.c`; six DSPAudio sources |
| `main.c` | `insertFx_init()`, FxBuffer/VoiceBuffer DEV dump | the boot notice (STALE and EMPTY) |
| `tools/dsp_test/Makefile`, `armcheck/vx2*.c`, `check_special_tags.py` | shaper, transient, fxbuf, insert, voicebuffer, crumpbit, lfo, swap targets and probes; `transient_type`/`filter_drive_type` keys | osc targets; osc probes (§6 item 2); osc keys |
| Comments in take-whole files | affiliate lines naming `transientGenerator.c` / `transient_calcBlock()` | remove or mark "S086" |

### 4.3 Design points, with the decisions

#### 4.3.1 `snc` cost (Q5)

**As built:**
- `oscSection_renderDrum()` `OSC_MOD_SNC` renders the slave in segments
  between master wraps.
- On each wrap it computes `ratio = car->phaseInc / mod->phaseInc` (a
  `VDIV`) and, for a sample master with a non-power-of-two span, runs
  `next % cycle` (a `UDIV`).
- It also makes one more renderer call per segment.
- A high-pitched master can wrap on most samples of a 32-sample block.

**S085:**
- `ratio` is computed once per block. It is constant within a block apart
  from an active slew, and the slew error over one block is below one sample
  of phase.
- The worst case is probed on the compiled loop: master at maximum pitch,
  a sample master with an odd span, slave of each type.
- The cap is the FM carrier cost + 10 instructions per sample. If the probe
  exceeds it, the render becomes a single per-sample loop with the reset
  inside, at constant cost.
- **With carrier-only `itp` (§4.3.7):** the old slave copy follows the same
  master wraps. The wrap positions and overshoots are found once per block and
  applied to both slaves, so the master is never rendered twice.

#### 4.3.2 Runtime-slot reserve (Q3: settled by §4.3.7)

- The reserve stays at a fixed 1,176 B per slot (today 2 × 588 B; 7,056 B in
  all, unchanged).
- With one carrier snapshot instead of three, `OscSectionState` falls from
  380 B to about 140 B.

| Engine | Spike | S085 (estimate) | Margin |
|---|---:|---:|---:|
| Drum | 1,072 | ≈ 832 | ≈ 344 |
| Snare | 904 | ≈ 664 | ≈ 512 |
| Cymbal | 1,008 | ≈ 768 | ≈ 408 |
| HiHat | 1,016 | ≈ 776 | ≈ 400 |

- The tightest margin rises from 104 B to about 344 B (Drum).
- This is RAM inside the fixed reserve. It has no CPU effect and releases no
  RAM.
- The spike's S086 work needed about 44–48 B per engine.

#### 4.3.3 Sample count (Q7: 128)

`SAMPLE_MAX_COUNT` = 128, so every sample is reachable by the 7-bit `src`
row, automation and MIDI.

#### 4.3.4 Stock waves (Q6: (a), sample flash)

**Where the sample flash goes.** The region is 1,310,720 B (sectors 7–11,
1,280 KB), not 1.4 MB:

| Region | Today (v1) | S085 (catalogue v2) |
|---|---:|---:|
| Stock-wave block (5 × 12,224 B) | — | 61,120 |
| User item data (samples + wavetables) | 1,308,316 | 1,241,408 |
| Catalogue / metadata | 2,404 (`SampleInfo[120]` + names) | 8,192 |
| **Total** | 1,310,720 | 1,310,720 |

User space falls by 66,908 B (5.1 %).
- The 8,192 B catalogue is sized for 233 + 128 entries of 20 B.
- At 128 + 128 entries it needs about 5.2 KB, so the region could shrink to
  6,144 B and return 2,048 B to item data.
- That is a schedule detail (§5 item 8).

**Program flash (the 104–109 KB figure).**
- That figure is the **application** flash headroom (753,664 B window, today
  143,952 B free), which (a) keeps.
- (b) would have spent about 50 KB more of it, leaving ≈ 54–59 KB.

**Which tables live where.** The 1,241,408 B user space holds only what you
install. Nothing else is stored there.

| Table | Used by | Where | Bytes | Counted in user space |
|---|---|---|---:|---|
| `sine_table` | `Sin`, sine FM, LFO sine | program flash (`.rodata`) | 8,194 | no |
| `triTable`, `sawTable`, `recTable` (11 octaves × 1,024) | `Tri Saw Rec`; `pwm` `prc psw ptr swp` | program flash | 3 × 22,528 | no |
| `sq3 sw3 sqa tra swa` stock block | `wav` 6–10; `pwm` `pq3 pw3` | sample flash, stock block | 61,120 | no (separate region) |
| User samples | `smp`/`slp` | sample flash, item data | as installed | **yes** |
| User wavetables (frames) | `wtb` | sample flash, item data | as installed (as supplied or optimised) | **yes** |

- **PWM** needs no stored tables of its own. Every family is computed from
  the tables above: `prc`, `psw`, `ptr` and `swp` read the program-flash
  tables, and `pq3`/`pw3` warp the stock block.
- **Waveform interpolation** (blending between neighbouring `wav` entries)
  reads the same tables and stores nothing.
- **`wtb` frame morphing** is the user's wavetable itself, interpolated
  between two frames at play time. Only the frames you install take space.
- **The core tables stay in program flash** (r6). A move into sample flash
  was considered in r5 and withdrawn.

**Behaviour under (a).**
- The tables are generated into the stock block at every Load:[Samples].
- Until the first install, `sq3 sw3 sqa tra swa` play as `Rec`/`Saw`/`Tri`,
  and `pq3`/`pw3` use the fallback split.
- The boot notice therefore covers an EMPTY catalogue as well as a STALE one.
  The wording is set in the schedule.

#### 4.3.5 Waveform interpolation loses its sample path

- `OSC_INTERP_AUDIT.md` describes interpolation across the `wav` list,
  including user samples.
- Samples now live under `typ`/`src`, and the pool grants interpolation only
  to `wav`-type oscillators. So interpolating between samples disappears, and
  blending between stock waves now includes the five new waves.
- `wtb` frames never use the pool (two frames always rendered).
- The audit must say so.

#### 4.3.6 Old content (Q8: no migration for anything)

- Installs read STALE until reloaded.
- `wav` 6–10 play the new stock waves (C1).
- `wav` sweeps that crossed into samples stop at `swa`.
- The converters are not changed.

#### 4.3.7 `itp`: carrier-only crossfade (Q14, confirmed r4)

**The rule (user, r4):** in `itp` the main oscillator (the carrier)
crossfades, and the modulator oscillators slew as in `fre`. Snare's noise
oscillator also slews. The rest of this section is the reasoning and the
proof plan.

**Where a retrigger click comes from:** a step in the output *amplitude*. In
the oscillator section that has three sources:

1. **The carrier phase reset.** The audible oscillator jumps from wherever it
   was to its start phase. This is what `itp` exists to hide.
2. **A modulator phase reset in `mix` mode.** On Drum, `mix` adds the
   modulator to the output, so resetting it is a direct amplitude step.
3. **A modulator phase reset in FM.** The modulator sets the carrier's
   *frequency*, not its amplitude. When the modulator jumps, the carrier's
   phase keeps running from where it is; only its rate of change jumps. The
   output is continuous, with a kink, so there is no click. At deep FM there
   can be a brief change of timbre, but no amplitude step.

**The S085 design.**
- On an `itp` trigger, only the **carrier** is snapshotted and restarted. The
  copy keeps running on its old phase and fades out over 132 samples while
  the restarted carrier fades in.
- The **modulators are not reset** in `itp`. They slew to the new pitch as
  in `fre` (Snare's noise oscillator, likewise).
- The old carrier copy is rendered with the **same, live modulator block**
  as the new carrier. In FM, both carriers receive the same continuous
  modulation, so the fading copy carries the FM tail exactly as the
  un-retriggered sound would have. Source 3 never occurs.
- In `mix` and `rng` the modulator is continuous, so sources 2 and 3 do not
  arise; only the carrier step is crossfaded.
- In `snc` both slaves follow the same master wraps (§4.3.1).

**Does it still click?** No: with the modulators running free, the only
amplitude discontinuity in the section is the carrier reset, and that is
crossfaded. Remaining differences from the spike's whole-section crossfade:
- **Cymbal (and every engine in `itp`):** the modulators no longer restart
  with each hit. In `rst`, Cymbal resets them, so an `itp` hit's FM attack is
  not identical from hit to hit. It is identical in `rst`. This is a sound
  difference, not a click.
- **A crossfade is not a seam.** While the copy fades, the two carriers
  differ in phase, so their sum has a brief comb character (about 3 ms). The
  spike had the same.
- **P1 F6 (retrigger inside 3 ms)** is unchanged and still accepted.

**Pool use.**
- One unit per voice for every engine, so two voices can crossfade at once
  in a 2-unit pool, and Cymbal/HiHat `itp` works.
- **Claim order:**
  1. ongoing `itp` crossfades (never cut);
  2. waveform interpolation;
  3. new `itp` crossfades, lowest slot first.
- A trigger that finds no free unit behaves as `fre` (smooth, no crossfade).
  Interpolation is the common, continuous case, and only an ongoing crossfade
  (at most about 5 blocks) can take a unit from it.
- **Cost:** the pool is budgeted full. Worst case: 2 units × the dearest
  carrier render (`wtb` FM, 70 before Q9) per sample, for the whole system.
  That is the same ceiling as a 2-unit interpolation pool of `wtb`-class
  renders.

**Proof.** Carrier-only `itp` is new code, not taken from the spike:
- **Host test (`osc_run`):** retrigger a running FM carrier and a `mix`
  voice in `itp`. The largest sample-to-sample step across the trigger must
  stay within the largest step of the same sound with no trigger. The test
  must fail on `rst` and on a version that also resets the modulators.
- **Hardware H5:** fast retriggers in `itp` on Drum `fm`/`mix`/`snc`/`rng`,
  Cymbal and HiHat, listening for clicks and for the per-hit FM attack
  difference.

---

## 5. What a full implementation is

S085 is complete when all of the following hold:

1. **Data model.**
   - `OscInfo` carries:
     - type and source;
     - request bytes;
     - slew;
     - sample bounds and gain code;
     - the `pwm`/`wtb` union, with the `wtb` running frame pointers.
   - Engines carry `OscSectionState` with **one** carrier snapshot.
   - Every slot fits the 1,176 B reserve (`_Static_assert`).
2. **Renderers.**
   - `wav`: stock tables 0–5, stock readers 6–10, sine, noise; plain and FM.
   - `pwm`: six families; `prc` matches `Rec` polarity (Q10).
   - `smp`: one-shot, outputs 0 after the last sample (Q10).
   - `slp`: loops `[start, end)`; the start is linear 0..127 across the
     sample (Q11).
   - `wtb`: 8/16-bit, two frames per sample, ramped position, running frame
     pointers updated when the frame index changes (Q9).
   - Every renderer carries the per-sample slew at constant cost, is in ITCM
     where measured to fit, and is dispatched per block by type.
3. **Section.**
   - Run modes:
     - `rst`: bit-identical to today;
     - `fre`: every oscillator slews, the filter rings;
     - `itp`: carrier snapshot and 132-sample crossfade; the modulators slew
       as in `fre` and feed both carriers; falls back to `fre` when the pool
       has no room (§4.3.7).
   - Filter slew in `fre`/`itp`, following modulated `q` (Q15).
   - Drum `fm`/`mix`: bit-identical.
   - Drum `snc`:
     - mix law `slave·(1−amt) + master·amt` (Q10);
     - no slave resets once a one-shot master has ended (Q10);
     - ratio once per block; wraps found once per block and shared with the
       `itp` copy; worst case within the cap (Q5).
   - Drum `rng`.
   - Cymbal/HiHat `renderFm2`: bit-identical.
4. **Pool.**
   - `OSC_RENDER_POOL_UNITS` 2, reset at block start.
   - One scratch block for the old carrier.
   - Claim order: ongoing `itp`, interpolation (`wav` type only), new `itp`
     lowest slot first.
   - `OSC_WAVE_INTERP_MAX_ACTIVE` and `modNode_waveInterpActiveCount` are
     gone.
5. **Rows and writers.**
   - Rows appended: Drum 44, Snare 41, Cymbal/HiHat 46.
   - The wave rows are tagged `IM_SPECIAL_OSC_WAVE`.
   - Four writers:
     - type re-derives wave/source from the request bytes;
     - wave/source record the request, then clamp;
     - F11 hygiene on entering `wtb`.
   - Per-slot `wav` domain with adapter/velocity refresh.
   - Classifier with the `_mod_type` exclusion.
   - `special_tags` 177/0.
6. **Preset.**
   - Selector rows are stored through the supplemental path (Normal + Morph
     mirror + `morph_interpolation[]`).
   - They are applied before the Morph image on every apply path. Every caller
     of `instrumentManager_resetRuntimeSlot*()` is listed in the schedule:
     Scene switch, per-track Scene, staged and immediate Instrument, deferred
     Scene slot, boot, Scene 17, copy/clear paste, AutoSave restore.
   - They are skipped in the supplemental rebind loop.
   - They survive a type round trip and a step-automation release.
   - The SHIFT Morph view shows and edits Normal for them (the S078 rule).
7. **Menu.**
   - Formatted cells at the five sites, with clamp to the format maximum.
   - Slot-aware automation maximum and text.
   - `DTYPE_MIX_FM` with four values at all six sites.
   - `MENU_WAVEFORM` with 11 entries and no sample tail.
   - Count asserts.
   - The single-parameter view shows sample and wavetable names.
   - Snare page 0: `typ src run` at cells 3–5, as on Drum.
8. **Sample flash.**
   - Catalogue v2: stock block, item data, catalogue (magic last).
   - At most 128 samples and 128 wavetables; the catalogue region sized to
     fit.
   - Stock waves generated at every install; the `Rec`/`Saw`/`Tri` fallback
     until then.
   - Boot notice for STALE and EMPTY catalogues.
   - Installer from `samples/` and `wavetables/`:
     - natural order, files then folders, depth 3;
     - long-named folders;
     - a defined rule for names longer than the 48-character key;
     - per-file skip on read failure;
     - per-frame source format;
     - 8-bit never expanded, 24 → 16, stereo → mono;
     - gain code;
     - progress and summary.
   - `/loops` retired.
9. **Card and tools.**
   - `SD_CARD/loops/` moved to `SD_CARD/wavetables/hvoice/`, optimised by
     `sampleoptimize.py`, with the originals in `SD_CARD/wavetables_old/`
     (Q12).
   - `sampleoptimize.py`; `gen_stock_recipes.py`; the osc plots.
10. **Proof.**
    - Host tests `osc_pwm`, `osc_sample`, `osc_run` (with the §4.3.7 click
      test), `natural_order`, `catalogue`, `stock_gen`, `wtb`. Each fails on
      one deliberately broken change.
    - A frozen golden proves the `fm`/`mix`/FM2 and `rst` S0 claims (Q13).
    - Loop probes for every oscillator loop and the `snc` worst case (Q13,
      §6 item 2).
    - The selector store/reload is proven on hardware (H4a; Q13).
    - The cost record holds pre-S084 and S085 figures.
11. **Documents.**
    - The specifications in §14 of the cherry-pick document.
    - `EFFECTS_MIXER_DSP_REFERENCE.md` (the mixer block-start order).
    - `MEMORY.md`: "Sample Flash Loading", the sample flash map, the spike
      path and the `.gitignore` note.
    - `SCOPING_TARGETS.md` §7.4.
    - `FILESYSTEM_SPEC.md` folder names (`samples/`, `wavetables/`).
    - `STORAGE_SRAM_MANIFEST.md`: the sample flash map.
    - The S085 handoff log.
12. **Hardware.** Every item of the cherry-pick document §15, plus:
    - H4a: save, reboot, reload;
    - H4b: a MIDI CC to `typ`;
    - H5: carrier-only `itp` (§4.3.7);
    - H5a: velocity → resonance in `fre`/`itp`;
    - a step-automation release of `typ`;
    - X1 install cases;
    - the boot notice on an erased sample flash;
    - the DEV render-overrun counter during the worst-case Scene.

---

## 6. Missing from the cherry-pick document

1. **A dependency-correct build order** (§2.3, §10).
2. **Probes for the cost record.**
   - Static loop counts for `pwm` (six families), `smp`/`slp`, the slew-enabled
     `wav` table/sine/noise loops, `snc` (worst case), `rng`, the `itp` blend,
     and `wtb` with running pointers.
   - The pre-S084 counterparts too. `armcheck-vx2` has only the stock reader
     and `wtb`.
   - The pre-S084 "wavetable 24 / FM 35" figure and the spike's "24 / 35 with
     slew" are the same number. Either the slew is free or one of the counts
     is stale; measure both trees.
   - **PWM is computed, not stored.**
     - `prc`/`psw` read two table points per sample; `ptr`/`pq3`/`pw3` warp the
       phase and read once.
     - `swp` costs depend on its segment. Segment 2 (saw → square) does three
       reads and a blend, so it is the worst case and is the one to probe.
     - The per-block `oscPwm_prepare()` holds up to three divisions and is
       budgeted as running every block.
     - The spike's figures (≈ 30–32, FM 42–48) are plan estimates, not
       measurements.
     - The render loop switches on the family every sample although the family
       is block-constant. One loop per family would save that branch, at an
       ITCM cost. Decide from the probe.
3. **Selector store/reload proof.** P1 F1 was the blocker, and it lives in
   Preset, which the host harness does not compile. Covered by H4a/H4b and
   the step-automation release check (§5 item 12).
4. **A frozen S0 golden** for Drum `fm`/`mix`, Cymbal/HiHat FM and `rst`.
5. **The repository card move** (Q12).
6. **The install summary RAM and service hook** (C3, C4).
7. **The `.gitignore` spike entry must stay** (§1).
8. **Spec coverage gaps** (§5 item 11).
9. **Long file names.** The natural-order key is
   `AFATFS_LONG_FILENAME_MAX` (48) characters. The behaviour for longer names
   (truncation, ties) is not specified or tested.
10. **The Morph view on selector rows** (§5 item 6).

---

## 7. Resource ledger (estimates until built)

| Resource | Today | S085 estimate | Basis |
|---|---:|---:|---|
| Program flash used | 609,712 B | ≈ 645–650 KB (headroom ≈ 104–109 KB) | P1 +15.3 KB less LFO/swap; P2 osc share incl. `StockWaveRecipes.c` 10.2 KB |
| ITCM | 4,168 B | ≈ 10,000–10,900 B | spike 12,720 B includes `SVF_calcBlockZDFShaped()` (out); measure |
| SRAM1 static (approved, Q4) | — | −18,481 released; +2,313 (selection) +44 (install state) +2 (summary) +65 (pool: one 64 B scratch block + count; 129 B approved) | P2 §0.5/§15.8; §4.3.7 |
| Runtime slots | 7,056 B | 7,056 B; tightest margin ≈ 344 B (Drum) | §4.3.2 |
| DTCM | 4,472 B | 4,471 B (`modNode_waveInterpActiveCount` −1) | — |
| Sample flash for users | 1,308,316 B | 1,241,408 B (+2,048 B if the catalogue region shrinks to 6 KB) | §4.3.4 |
| Per-voice osc worst case | Cymbal ≈ 83 instructions (2 × 24 + 35) | ≈ 198 with the spike's `wtb` (2 × 64 + 70); lower after Q9; measure | §16 of the cherry-pick document |
| System pool | 2 interpolation renders/block (`wav`, ≤ FM 35) | 2 units, budgeted full; ≤ 2 × the dearest carrier render (`wtb` FM) | §4.3.7 |

**For S086:**
- The per-voice oscillator worst case rises by about 115 instructions
  (all-`wtb` Cymbal, before Q9) plus the pool.
- Six such voices add roughly 14 % of a 4,897-cycle frame at one instruction
  per cycle.
- This is the figure S086 budgets beside its 88-cycle chain.

---

## 8. Risks

| # | Risk | Likelihood / impact | Mitigation |
|---|---|---|---|
| R1 | Re-applying hunks by hand into four engines, Preset and Menu drops a fix or brings an S086 hunk along | Medium / high | Write against the current tree from the schedule; review against the fix list in §2.1; a `grep` check for `insertFx_`, `driveShaper`, `transient_is`, `voiceBuffer_`, `slotQuiet` |
| R2 | One change set with one hardware check: a fault may be in the installer or the DSP | Medium / medium | The internal order in §10 keeps host gates per layer. On hardware, test the install (X1) first, then the oscillator rows |
| R3 | `snc` worst case above the cap | Medium / low | Q5 fallback: the constant-cost loop |
| R4 | Carrier-only `itp` is new and has never been heard; the FM attack varies from hit to hit in `itp` | Medium / low | §4.3.7 host test; H5 listening |
| R5 | S086 room: ≈ 5.5–6.4 KB ITCM; ≈ 104–109 KB program flash | Medium / medium | Record each figure at the close |
| R6 | Stock waves fall back before the first install | Certain on fresh units / low | EMPTY boot notice |
| R7 | Selector apply order regresses on a path the spike did not exercise | Medium / high | §5 item 6 lists every path; H4a |
| R8 | Installer blocks with audio suspended; a slow card or many files lengthens the modal | Medium / low | `FS_BLOCK_OPEN_HANDLE_RETRIES` bounds opens; time an install of the full reference card |
| R9 | The spike's hardware freeze recurs | Low / high | The freeze followed buffer/drive selections, which are out; but `mixer.c`, `InstrumentManager.c` and `presetManager.c` are shared. DEV render-overrun counter during the hardware check |
| R10 | `fm`/`mix` S0 claim wrong | Low / medium | Frozen golden (Q13) |
| R11 | `itp` retrigger inside 3 ms steps (P1 F6) | Certain on fast rolls / low | Accepted; listen at H5 |
| R12 | Natural-order key truncation at 48 characters | Low / low | §5 item 8 |
| R13 | Documentation drift: the spike's spec copies mix S086 paragraphs | Medium / low | Write the osc sections fresh |

---

## 9. Ambiguities (all resolved)

| # | Point | Resolution |
|---|---|---|
| A1 | `snc` mix law | `slave·(1−amt) + master·amt` (Q10) |
| A2 | `snc` after a one-shot master ends | no further slave resets (Q10) |
| A3 | `prc` polarity | matches `Rec` (Q10) |
| A4 | `smp` end value | 0 after the last sample (Q10) |
| A5 | Sample start scale | linear 0..127 (Q11) |
| A6 | Snare page 0 cell positions | cells 3–5, as Drum |
| A7 | Snare noise oscillator in `fre`/`itp` | slews in both; not snapshotted (§4.3.7) |
| A8 | Step-automating `typ` | rebinds on each change; the stored type returns on release; hardware check |
| A10 | "Pre-S084" tree for the cost baseline | the current tree (oscillator files identical to `59406a4`) |
| A11 | Notice for an EMPTY catalogue | yes (Q6 (a), §4.3.4) |
| A12 | Pool scratch region | SRAM1 (Q4) |
| A13 | 2-unit pool claim order; Cymbal/HiHat `itp` | carrier-only, 1 unit per voice; ongoing `itp` → interpolation → new `itp` (§4.3.7) |

---

## 10. Build order inside the single change set (Q2)

S085 lands as one change set with one hardware check. The order below is
how the work is built and host-tested, so each layer compiles against the
one before it:

1. **Sample flash v2 and installer.**
   - `SampleMemory` v2 (128/128); `SampleInstall`; the walk; natural order;
     boot notice; modal.
   - Host: `natural_order`, `catalogue`.
2. **Stock waves.** Generator, recipes, readers, `wav` list of 11.
   - Host: `stock_gen` against `frozen/stock_ref.bin`.
3. **Oscillator types and section.**
   - Data model; slew; `pwm`; `smp`/`slp`; `wtb` with running pointers; pool
     (2); run modes with carrier-only `itp`; `snc`/`rng`; engines.
   - Host: `osc_pwm`, `osc_sample`, `osc_run` (incl. the click test), `wtb`,
     the frozen S0 golden; all probes, with the `snc` cap.
4. **Rows, writers, Preset, menu.**
   - Host: `special_tags` 177/0; the full suite; `make img`.
5. **Cost record, specifications, card move, hardware matrix** (§5 item 12;
   install cases first).

---

## 11. Decisions (user, 2026-10-26)

| Q | Decision |
|---|---|
| Q1 | Withdrawn: the implementation does not depend on the S084 hardware matrix. |
| Q2 | One change set (§10). |
| Q3 | Settled by carrier-only `itp`: one snapshot per engine; tightest margin ≈ 344 B (§4.3.2). |
| Q4 | All RAM changes approved: pool scratch + count (approved at +129 B, needs ≈ +65 B), SRAM1; selection +2,313 B; install state +44 B; summary +2 B; releases −18,486 B (not reused). |
| Q5 | Ratio once per block; worst-case probe; cap = FM carrier + 10; constant-cost loop if exceeded (§4.3.1). |
| Q6 | **(a)**: stock waves generated into sample flash at install; program flash headroom kept; EMPTY boot notice added. User space of 1,241,408 B accepted (r4); it holds only installed samples and wavetables. The core tables stay in program flash (r6; the r5 move is withdrawn) (§4.3.4). |
| Q7 | 128 samples. |
| Q8 | No migration for anything; old content is not a concern. The converters are unchanged. |
| Q9 | `wtb` running frame pointers in S085. |
| Q10 | Recommendation: `snc` crossfade law; no resets after a one-shot master ends; `prc` matches `Rec`; `smp` ends at 0. |
| Q11 | Recommendation: linear sample start. |
| Q12 | Recommendation: move and optimise the reference wavetable set; originals in `wavetables_old/`. |
| Q13 | Recommendation: frozen S0 golden and loop probes as gates; selector store/reload by hardware row H4a. |
| Q14 | Pool stays at 2 units. In `itp` the main oscillator crossfades (1 unit per voice) and the modulators slew as in `fre`, feeding both carriers (confirmed r4). Claim order: ongoing `itp` → interpolation → new `itp` (§4.3.7). |
| Q15 | Recommendation: keep the `fre`/`itp` filter slew. |

---

## 12. Remaining questions

None. The implementation schedule can be written from this document.
