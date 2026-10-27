# Session 085 — Handoff Log

```
DATE: 2026-10-26 / 2026-10-27
SESSION GOAL: Re-implement, on the rolled-back tree, the oscillator part of
              the abandoned S084 voice spike: oscillator types
              wav/pwm/smp/slp/wtb, pwm families, run modes rst/fre/itp, the
              render pool, Drum mod modes fm/mix/snc/rng, the five stock
              waves, sample flash catalogue v2 with its installer, menu
              formatting and the host tools. Nothing from the transient,
              filter drive, Mix FX, voice buffer, elastic Effect, LFO or
              instrument-swap work.
COMPLETED:
  P1 evaluation  S085_P1_OSC_MOD_PLAN.md r1-r6: review of the cherry-pick
                 document (corrections C1-C11), the build-order problem,
                 the design points, questions Q1-Q15 answered by the user.
  P1 schedule    S085_P1_IMPLEMENTATION.md §0-§20: a line-level schedule
                 (WP0-WP19, change IDs C<wp>.<n>) with paste-ready comment
                 blocks and 13 deep-dive decisions D1-D13.
  Implementation (the user's implementer, logged in §21 of the schedule):
                 all WPs; G0-G4 green.
  Assessment     §22: findings A1-A10 (one slew bug, the snc cost cap,
                 the missing cost record, weak tests).
  Fixes          §23: all ten fixed or decided; constant-cost snc; per-sample
                 cost probes and the C19.2 cost record; 26 break proofs.
  Close-out      this log, the index entry, the specification updates, the
                 generator page draft, MEMORY.md, volatile notes.
VERIFIED ON HARDWARE: no. Build clean; every host gate green; break proofs
  26/26. The S084 matrix V1-V7 and the S085 matrix H1-H11 (§10) are pending.

CHANGES THIS SESSION (detail in §12):
- Core/SampleRom: SampleMemory.h/.c rewritten (catalogue v2); SampleInstall
  .h/.c, StockWaveGen.h/.c, StockWaveRecipes.c new; sampleFlash.c comment.
- Core/Hardware/SD: storageTypes.h/.c natural order; filesystem.h/.c the
  installer walk (v1 sample helpers removed), bounded blocking opens.
- Core/DSPAudio: Oscillator.h/.c (types, 11-wave list, slew, dispatch, stock
  renderers, sync dispatch); OscillatorSample, OscillatorPwm,
  OscillatorWavetable, OscRenderPool, OscSection, StockWaves (.h/.c) and
  OscSyncLane.h new; ResonantFilter.h/.c SVF_recalcFreqFrom();
  modulationNode.c pool claims; mixer.c block-start order.
- Core/DSP/Instruments: four engines (section, trigger gating, itp render);
  four *Parameters.c/.h (appended rows, cells, counts); InstrumentManager
  .h/.c (writers 20-23, domains, claims, accessors, fixed 1,176 B reserve);
  InstrumentCellFormat.h/.c new.
- Core/Bank/Scene/Preset/presetManager.c: selector store/apply.
- Core/Menu: MenuText.h (name tables), menu.c/.h (formatted cells,
  DTYPE_MIX_FM x4, 11-wave list, automation, SHIFT Morph, Load:[Samples]).
- main.c (boot notice), config.h (OSC_RENDER_POOL_UNITS 2), Makefile.
- tools: sampleoptimize.py, gen_stock_recipes.py, plots/; dsp_test: frozen85/,
  extract.py, snapshot.sh, check_special_tags.py, osccost.py, breakproofs.py,
  new tests and probes, DSP_TEST.md.
- SD_CARD: loops/ -> wavetables/ak_hvox (optimised); wavetables_old/,
  samples_old/ originals.
- Specifications and MEMORY.md (§15).

KNOWN ISSUES INTRODUCED: none open. Accepted behaviours: §14.
KNOWN ISSUES RESOLVED: the v1 sample path's long-sample limit (phase >> 17,
  "long samples not fully solved", MEMORY.md since S018) - samples now play
  through Q20.12 up to 1,048,575 frames; lexicographic (not natural) card
  order; /loops special case.

NEXT SESSION RECOMMENDED GOAL: run the S084 hardware matrix V1-V7, then the
  S085 matrix H1-H11 (§10; install H2 first, then the oscillator rows, then
  the H11 worst-case Scene). Then S086 (the effects chain inside about 88
  cycles per voice), budgeting the S085 oscillator worst cases (§9.4).
BLOCKERS: hardware results gate S086 (post-mortem rule: each session ends
  with a hardware check before the next starts).

CRITICAL REMINDERS FOR NEXT SESSION:
- Constant CPU: every oscillator loop runs its worst path every sample;
  budget the worst case (§9). The snc slave reset is inside the loop
  (OscSyncLane.h); never go back to one renderer call per master wrap.
- ITCM is 12,432 / 16,384 B (3,952 B free). The pwm, smp/slp and wtb loops
  are shared between plain and sync renders (OSC_SHARED_LOOP); do not let
  the compiler clone or unswitch them, or ITCM fills (it reached 16,272 B).
- The request bytes (waveRequest, sourceRequest) are written by retained
  writes only (D2); LFO overlays must keep notify_base_change = 0.
- Selector rows (typ/src/run) are applied before the Morph image on every
  slot reset path; any new apply path must call preset_applySelectorRows().
- The catalogue magic is programmed last; never write it earlier.
- Code comments cite S085 IDs (C<wp>.<n>, D<n>, "assessment A<n>"); §13 of
  this log resolves every one after the schedule is deleted.
```

---

## 1. Session overview and timeline

| When | Step | Output |
|---|---|---|
| 2026-10-26 | Read `MEMORY.md`, `SCOPING_TARGETS.md`, the S084 record, the spike and `S085_OSC_MOD_CHERRYPICK.md` (the session goal) | — |
| 2026-10-26 | Evaluation of the cherry-pick document against the spike and the current tree | `S085_P1_OSC_MOD_PLAN.md` r1, 15 questions |
| 2026-10-26 | User answers; plan revisions r2–r6 (§3.1) | plan r6, no questions open |
| 2026-10-26 | Code deep dive; line-level schedule (WP0–WP19, C-IDs, comment blocks, D1–D13) | `S085_P1_IMPLEMENTATION.md` §0–§20 |
| 2026-10-26 | Implementation by the user's implementer, with a working log | schedule §21; DEV `text=640,960` |
| 2026-10-27 | Assessment of the implementation | schedule §22 (A1–A10) |
| 2026-10-27 | Fixes, cost record, break proofs | schedule §23; DEV `text=642,360` |
| 2026-10-27 | Close-out | this log, index, specs, `MEMORY.md`, volatile notes |

**Branch and commits:**
- **Branch:** `dev-ph7-voice_expansion`.
- **Commits from S084:** `91265e9c` (roll fix-up pre-implementation),
  `fb2171a1` (S084 close-out).
- **Commits from S085:** `54e2de05` (doc cleanup), `1ea234ea` ("s085 osc
  mod re-implementation pre implement": the plan and the schedule).
- **State at close:** the S085 code, the §21–§23 schedule sections and the
  close-out documents are uncommitted.

**Spike:**
- **Location:** `Helicase-dev-ph7-spike/` inside the working tree (commit
  `26fb7c3`, branch `dev-ph7-voice-spike`), kept out of git by
  `.gitignore:62`. A second copy sits in `~/.Trash`.
- **Path history:** the plan found it inside the tree. §21 recorded it at
  `../Helicase-dev-ph7-spike/` for a time. At close it is inside the tree
  again.

---

## 2. Inputs and what they got wrong

### 2.1 The cherry-pick document (`S085_OSC_MOD_CHERRYPICK.md`)

**Goal:** re-implement every S084 change to the oscillator section, and the
Drum `mod` modes `snc`/`rng`, on `59406a4` + the S084 P0 roll fix-up.

**Sources of truth it named:**
- the spike code (`26fb7c3`);
- `S084_P1_IMPLEMENTATION.md` (P1-x.y IDs; review fixes §18);
- `S084_P2_IMPLEMENTATION.md` (VX2-x.y IDs; review fixes §17);
- the master plan `S084_P1_VOICE_EXPANSIONS.md` §4.1, §4.2, §5.1, §5.2.

Where a document and the spike code differed, the code won, because it
carries the review fixes.

**Scope (in):**
- oscillator types `wav pwm smp slp wtb` (`typ`) and the source row `src`
  (pwm family, sample index, wavetable index);
- pwm families `prc psw ptr pq3 pw3 swp`;
- `smp` (one-shot), `slp` (loop) and the start position;
- `wtb` (the `wav` row is a continuous frame position);
- stock waves `sq3 sw3 sqa tra swa` (the wave list grows from 6 to 11);
- run modes `rst fre itp` (`run`), per-sample slew, filter slew;
- the render pool;
- Drum `mod` `fm mix snc rng`;
- sample flash catalogue v2, the installer (`samples/`, `wavetables/`,
  natural order, depth 3), the boot notice, the gain code;
- `sampleoptimize.py`, `gen_stock_recipes.py`, the plots;
- menu formatting (`InstrumentCellFormat`, `MenuText.h`, `menu.c`).

**Scope (out; stays in the spike for S086 or later):**
- transient types `trt`, sample/burst transients, `buf`;
- filter drive types `dty`, `DriveShaper`, `SVF_calcBlockZDFShaped`;
- Mix FX `fxt`/`amt`, `InsertEffects/`, `VoiceBuffer`, the D+S split;
- the elastic Effect contract, FxBuffer positions and leases, CrumpBit
  compaction;
- LFO one-shots and the `rnd` fix;
- the deferred instrument swap, the mixer declick, DEV trace `v`.

**Compatibility rules (kept):**
- New rows are appended, so existing indices, automation IDs, LFO tokens,
  MIDI Learn and NRPN IDs are unchanged.
- A missing key loads 0: `typ` 0 = `wav`, `src` 0, `run` 0 = `rst`;
  `mod` 0/1 keep `fm`/`mix`.
- S086 appends its rows after S085's; no indices are reserved.

**Row counts** (`special_tags` 177 rows / 0 mismatches after S085):

| Instrument | Before | Appended | After |
|---|---:|---|---:|
| Drum | 39 | `osc1_type osc1_source osc2_type osc2_source osc_run_mode` | 44 |
| Snare | 38 | `osc1_type osc1_source osc_run_mode` | 41 |
| Cymbal | 39 | `osc1_type osc1_source osc2_type osc2_source osc3_type osc3_source osc_run_mode` | 46 |
| HiHat | 39 | the same seven | 46 |

**Spike figures it quoted** (kept for the record):
- `wtb` 64 / FM 70 instructions per sample against a 47 / 62 target
  (accepted in P2, F6);
- the stock reader 24 / 31;
- the table oscillator 24 / 35 "with the slew";
- pwm ≈ 30 (estimate);
- spike engine sizes after P1: Drum 1,072, Snare 904, Cymbal 1,008,
  HiHat 1,016 B;
- spike ITCM 12,720 B (including the out-of-scope
  `SVF_calcBlockZDFShaped()`).

### 2.2 Corrections found in the evaluation (plan §2.2)

| # | The document said | What was true |
|---|---|---|
| C1 | Old kits whose `wav` pointed at a sample clamp to `Cym` | That was P1 (6 waves). With 11 waves, stored 6–10 play `sq3 sw3 sqa tra swa`, and 11+ clamp to `swa`. Accepted (Q8, no migration). |
| C2 | Take `IM_SPECIAL_OSC_MASK/OSC1/OSC2/OSC3/OSC_NOISE` | Already in the tree before S084; only the four writer IDs are new. |
| C3 | RAM net ≈ −16,142 B; `sample_install` +32 B | As built in the spike: install state 44 B, filesystem selection 2,313 B, summary +2 B, `sample_io_buf` −4 B; net −16,144 B. |
| C4 | `menu.c`: only `menu_loadSamplesModal()` | Also the summary helpers, `menu_serviceInstallSummary()` and its call, the progress callback, and the sample/wavetable short-name helpers. |
| C5 | `sampleFlash.c`: "two-line change" | A comment only. |
| C6 | Pool RAM in SRAM1 "needs re-approval" | The P1 plan had DTCM. Decided: SRAM1, approved (Q4). |
| C7 | `pwm`/`smp`/`slp` costs "measure" | No probe existed for them, for `snc`, or for the pre-S084 loops. `armcheck-vx2` covered only the stock reader and `wtb`. |
| C8 | Gate A = build + `osc_pwm`/`osc_sample`/`osc_run` | Impossible with take-whole files (§2.3). |
| C9 | `SAMPLE_MAX_COUNT` 233 | 233 served the transient `wav` list (out of scope). Decided: 128 (Q7). |
| C10 | Pool of 4 units; `itp` claims one unit per oscillator and snapshots the whole section | Decided: 2 units; carrier-only `itp`, 1 unit per voice (Q14). |
| C11 | Paths `../Helicase-dev-ph7-spike/` | At the evaluation the spike was inside the tree (§1). |

### 2.3 The gate order did not match the files

The "take whole" files are the spike's final state, and they depend on
each other across its four gates:
- `Oscillator.c` needs `StockWaves.h` and the `wtb` dispatch.
- `OscillatorPwm.c` needs `StockWaves.h` and `sampleMemory_stockValid()`.
- `OscillatorSample.c` needs catalogue v2.
- `OscSection.c` needs the sample header and the pool.
- `Oscillator.h` needs `OscWtbState` for its union.
- `SampleMemory` v2 removes the v1 install API that `filesystem.c` calls.

S085 was therefore one change set (Q2) with internal build-and-test gates in
dependency order:

| Gate | After | Checks |
|---|---|---|
| G0 | WP0 | `frozen85/` snapshot before the first edit; baseline probes |
| G1 | WP1–WP3 | build, `natural_order`, `catalogue` (folded into G3 in practice: WP1 removed the v1 API while `Oscillator.c` still called it) |
| G2 | WP4 | `stock_gen` ±1 LSB |
| G3 | WP5–WP11 | build, oscillator host tests, probes, the `snc` cap decision, sizes, ITCM |
| G4 | WP12–WP16 | `special_tags` 177/0, the whole suite, `make img`, `link_budget.py` |
| G5 | WP17–WP19 | cost record, specs, card move, hardware matrix |

### 2.4 What the spike provided (plan §3)

| Area | Spike state | Separability and S085 treatment |
|---|---|---|
| `Oscillator.h/.c` | P1 + P2 with F2 and F10; `OscInfo` 120 B | clean; written fresh against the tree |
| `OscillatorSample` | Q20.12 phase, bind every block, gain code, `min(sourceRequest, count−1)` | clean |
| `OscillatorPwm` | F3 interpolation fix; `pq3`/`pw3` natural splits; F10 | clean |
| `OscRenderPool` | 4 units, 128 B scratch | S085: 2 units, one 64 B scratch |
| `OscSection` | run modes, `snc` (F5), `rng`, `renderFm2`, whole-section `itp` blend, filter slew (F4) | S085 reworked `itp` (carrier-only) and `snc` |
| `SVF_recalcFreqFrom()` | 4-line function | one hunk; the rest of `ResonantFilter` is drive (out) |
| `SampleMemory` v2 | STALE detection, gain table, `stockValid()` | counts changed to 128/128; region 6 KB (D9) |
| `SampleInstall` | F4 `setFrameFormat`, F7 `abortItem`, F8 −4 | clean |
| `filesystem`/`storageTypes` | F1 long-name opens, F2 depth and open give-up, F7 per-item skip | installer region hunks only |
| Stock waves | ±1 LSB against the reference | clean (Q6 (a)) |
| `wtb` | 8/16-bit, any length; 64/70 instructions | S085: block-rate frame pair (D1) |
| `InstrumentCellFormat` | osc + transient/drive/Mix kinds, P3 hidden mask, label hooks | osc kinds only |
| Engines, registry, Preset, menu | interleaved with drive, transient, insert stage, swap, `voicePost_*Gain` | hunks rewritten against the tree |
| Host tests | all pass; reverted-fix proofs for P1 F2–F4 and P2 F4 | S085 rewrote them, with a frozen S0 golden |

**Not covered by any spike host test:** P1 F1 (Preset store/apply), P2 F1/F2
(the FAT walk), F10, F11, F5, and the `fm`/`mix` S0 claim.

---

## 3. Decisions

### 3.1 Plan questions (user, 2026-10-26)

| Q | Decision |
|---|---|
| Q1 | Withdrawn (user): the implementation does not depend on the S084 hardware matrix. Do not raise the S084 hardware gate as a question again. |
| Q2 | One change set, gates internal (§2.3). |
| Q3 | Settled by carrier-only `itp`: one snapshot per engine, the tightest slot margin ≈ 344 B. (The user asked whether "doubles the margin" meant a worst-case CPU gain. It was RAM, not CPU, and was corrected in r2. The user cares only about peak worst-case CPU, never about average savings.) |
| Q4 | All RAM changes approved (§5.1). |
| Q5 | `snc` ratio once per block; probe the worst case; cap = FM carrier + 10; use the constant-cost loop if exceeded. |
| Q6 | (a): stock waves are generated into sample flash at install; program-flash headroom kept; EMPTY boot notice. The user accepted 1,241,408 B of user space (r4; 1,243,456 B after D9). In r5 a move of the core tables into sample flash was considered; in r6 it was withdrawn ("too complicated"). PWM needs no stored tables (§4.6). |
| Q7 | 128 samples. |
| Q8 | No migration for anything; old content is not a concern; converters unchanged. |
| Q9 | Running `wtb` frame pointers. Superseded by D1 (block-rate frame pair). |
| Q10 | `snc` mix law slave·(1−amt) + master·amt; no slave resets after a one-shot master ends; `prc` matches `Rec`; `smp` ends at 0. |
| Q11 | Linear sample start. |
| Q12 | Move and optimise the reference wavetable set; originals in `wavetables_old/`. |
| Q13 | A frozen S0 golden and loop probes as gates; selector store/reload proven by hardware row H4a. |
| Q14 | Pool stays at 2 units (the user: plenty for most cases; trigger overlaps are rarer). The user asked whether interpolating only the main oscillator would still click with FM on the tail; then decided "in `itp` the main oscillator interpolates, the mod oscillators slew like in `fre`" (r4). Claim order: ongoing `itp` → interpolation → new `itp`. |
| Q15 | Keep the `fre`/`itp` filter slew. |

**Plan ambiguities resolved (plan §9; labelled PA here to avoid a clash
with the assessment's A-numbers):**

| # | Point | Resolution |
|---|---|---|
| PA1 | `snc` mix law | Q10 |
| PA2 | `snc` after a one-shot master | Q10 |
| PA3 | `prc` polarity | Q10 |
| PA4 | `smp` end value | Q10 |
| PA5 | sample start scale | Q11 |
| PA6 | Snare page 0 cells | cells 5–7 (D3) |
| PA7 | Snare noise in `fre`/`itp` | slews in both; not snapshotted |
| PA8 | step-automating `typ` | rebinds on each change; the stored type returns on release; hardware H4d |
| PA10 | the cost baseline | the current tree, whose oscillator files equal `59406a4` |
| PA11 | notice for EMPTY | yes |
| PA12 | pool scratch region | SRAM1 |
| PA13 | pool claim order, Cymbal/HiHat `itp` | carrier-only, 1 unit per voice |

### 3.2 Deep-dive decisions (schedule §0.4)

D1, D2 and D11 changed what the plan said. Each was flagged "Confirm" and
implemented.

| # | Finding | Decision |
|---|---|---|
| D1 | Q9's running pointers do not lower the `wtb` worst case: a fast position sweep changes the frame index every sample. | **Block-rate frame pair:** two neighbouring frames chosen once per block; the blend ramps per sample. Cost: the position moves at most one frame per block (≈ 1,378 frames/s; a 104-frame table sweeps in 75 ms). Faster sweeps glide. |
| D2 | LFO overlays went through the same special writer as menu edits, so the spike's `waveRequest` recorded LFO values. | The special writer takes `retained` (the old `notify_base_change`). `wav`/`src` record the request only for retained writes (menu, MIDI, load, Morph, automation); LFO overlays clamp the live value only. |
| D3 | Snare page 0 already uses cells 0–4. | Snare `typ src run` at cells 5–7; Drum/Cymbal/HiHat at cells 3–5. |
| D4 | `prc = saw(p) − saw(p+½)` already correlates 1.000 with `recTable` level 0 (0.9996 at level 5); the P1 F9 note was wrong. | No flip; a host check locks it (> 0.99). |
| D5 | The spike's formatter read the type from the runtime, which can differ from the shown Scene image (held automation, deferred slot, per-track played Scene). | The formatter reads the **stored** type from the shown image. |
| D6 | "One-shot master ended" needed a definition. | An `smp` master whose read index reached `end − 1`. The wrap test still runs; its result is discarded (constant cost). |
| D7 | `SVF_recalcFreq()` runs `setDistortionShape()` under `USE_SHAPER_NONLINEARITY` (0 today). | `SVF_recalcFreqFrom()` mirrors that `#if`. |
| D8 | `menu_numSamples`/`menu_setNumSamples()` existed only for the sample tail of the `wav` list. | Removed with their two callers. |
| D9 | The 8,192 B catalogue was sized for 233 + 128 entries. | 6,144 B at `0x081FE800`; user space 1,243,456 B. |
| D10 | `tools/dsp_test/frozen/` held the S073 snapshot. | A second snapshot `frozen85/`, taken before the first edit, is the S0 reference. |
| D11 | The `wav` domain was slot-aware in only one of three places; velocity and LFO on a pwm/smp/wtb `wav` row were capped at 10. | All three use the per-slot domain. |
| D12 | `menu_cellCommitValueEx()` dropped a Morph-view edit of a non-morphable instrument cell. | Selector rows commit to Normal in the SHIFT Morph view; other non-morphable rows keep the old behaviour. |
| D13 | The spike's `snc` started the post-reset segment one sample early. | The slave renders through sample *i* (the master wraps between *i* and *i* + 1), then resets. |

### 3.3 Other user instructions this session

- **Commits:** belong to the user. Do not commit or suggest committing;
  state "uncommitted" as a fact.
- **Constant CPU:** no skipping work for average savings; budget the worst
  case. "Reducing peak usage under the worst-case settings" is all that
  matters.
- **RAM:** allocations need explicit approval. Q4 approved all S085 RAM.
- **Comments:** contract-style blocks (What / Why / Inputs / Outputs /
  Accessors / Affiliates) beside every change, in `.h` and `.c`.
- **Close-out:** must keep every relevant detail of the three task
  documents, which the user deletes afterwards.

---

## 4. Design as built

### 4.1 Oscillator data model (`Oscillator.h`)

**Constants:**

| Group | Values |
|---|---|
| Wave ids | `SINE TRI SAW REC NOISE CRASH` 0–5 (unchanged); `SQ3 SW3 SQA TRA SWA` 6–10; `STOCK_WAVE_FIRST` 6; `OSC_STOCK_WAVE_COUNT` 11 |
| Types | `OSC_TYPE_WAV PWM SMP SLP WTB` 0–4, `OSC_TYPE_COUNT` 5 |
| Slew length | `OSC_SLEW_SAMPLES` 132 (3 ms at 44,108 Hz) |

**`OscInfo` additions** (struct 124 B):

| Field | Meaning |
|---|---|
| `oscType` | the type |
| `source` | the live, clamped pwm family, sample or table |
| `freqCacheType` | type key of the frequency cache; bit 7 = `stockValid` for pwm |
| `slewSamplesLeft`, `phaseIncTarget`, `phaseIncStep` | per-sample slew state |
| `sampleStart`, `sampleEnd` | frames |
| union of `OscPwmState pwm` / `OscWtbState wtb` | per-type block state; each type re-initialises its member, so a type change never reads the other's bytes |
| `waveRequest`, `sourceRequest` | the last retained bytes, before clamping |
| `sampleGainCode` | replaces the retired `sampleCacheLooped` |

**Functions:**
- **Helpers:** `osc_armSlew()`, `osc_freqToTableIndex()`,
  `osc_isTableWave()`. The last is true for Tri/Saw/Rec and `sq3`…`swa`;
  the engines use it for the trigger phase offset.
- **Renderers:** `calcStockWaveBlock()` / `calcStockWaveFmBlock()`.
- **Sync dispatch:** `osc_renderSyncBlock()` (§4.9).

**Dispatch:**
- `calcNextOscSampleBlock()` / `calcNextOscSampleFmBlock()` switch on
  `oscType` once per block: pwm → `oscPwm_render*`, smp/slp →
  `oscSample_render*`, wtb → `oscWavetable_render*`.
- `wav` keeps the waveform switch: sine, Tri/Saw/Rec tables, noise, crash,
  stock waves. A value outside the list renders silence.
- Waveform interpolation applies to `wav` only.
- The scalar dispatchers route non-`wav` types through a one-sample block
  render (no external caller).

### 4.2 Per-sample pitch slew

- **In the renderers:** every block renderer keeps a local increment, adds
  `phaseIncStep` on every sample, and writes the final increment back. In
  `rst` the step is 0, so the output equals the pre-S085 loop (`osc_s0`).
- **Per block, in `osc_setFreq()`:**
  - The target is recomputed per type, cached on (frequency, waveform,
    type key).
  - With no slew armed: `phaseInc = target`, step 0.
  - With a slew armed: step = (target − phaseInc) / **max(left, 32)**, and
    `left` falls by 32.
  - The difference is taken in `uint32_t` and cast once.
- **Shape (A1 fix):** linear for 128 samples; the last 4/132 of the interval
  spreads over a fifth block (160 samples in all); monotonic; never past
  the target. Dividing by `left` alone overshot by 20.5 % of the interval
  for 28 samples, then snapped back.
- **Who arms it:** `osc_armSlew()` is called by `oscSection_beginTrigger()`
  (`fre`: all oscillators; `itp`: the modulators) and by a refused `itp`
  claim (the carrier).

### 4.3 The `wav` list and the stock waves

**Stock tables:**
- `sq3 sw3 sqa tra swa` are generated into the sample-flash stock block at
  every install by `StockWaveGen.c`, from `StockWaveRecipes.c`
  (≈ 10.2 KB, generated by `tools/gen_stock_recipes.py` from
  `tools/plots/stock_wave_harmonics.csv`).
- They are additive sums in float32, in the same order as the host
  generator (±1 LSB, `stock_gen`).

**Level geometry** (`STOCK_WAVE_LEVELS` 11; `STOCK_WAVE_POINTS_PER_WAVE`
6,112; 5 × 6,112 × 2 B = 61,120 B):

| Level | 0–4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---:|---:|---:|---:|---:|---:|
| Points | 1,024 each | 512 | 256 | 128 | 64 | 16 | 16 |
| Harmonics | 511, 511, 337, 168, 84 | 42 | 21 | 10 | 5 | 2 | 1 |

- `stockWave_levelShift[]` = 22,22,22,22,22,23,24,25,26,28,28.
- **Read:** `stockWave_level(wave, level)`. Before the first install
  (`sampleMemory_stockValid()` = 0) it returns the program-flash fallback:
  `sw3`/`swa` → Saw, `tra` → Tri, `sq3`/`sqa` → Rec.
- **Renderers:** linear interpolation, fraction `(phase << (32 − shift)) ×
  2^−32`. FM adds `(mod × fmMod) << 19`, the `calcFmBlock` depth.
- **Where every table lives** (plan §4.3.4):

| Table | Used by | Where | Bytes |
|---|---|---|---:|
| `sine_table` | Sin, sine FM, LFO sine | program flash | 8,194 |
| `triTable`, `sawTable`, `recTable` (11 octaves × 1,024) | Tri, Saw, Rec; pwm `prc psw ptr swp` | program flash | 3 × 22,528 |
| `sq3 sw3 sqa tra swa` stock block | `wav` 6–10; pwm `pq3 pw3` | sample flash, stock block | 61,120 |
| user samples | `smp`/`slp` | sample flash, items | as installed |
| user wavetables | `wtb` | sample flash, items | as installed |

  PWM, waveform interpolation and `wtb` morphing store nothing of their
  own. The user's question "does 1.24 MB include the PWM and morphing
  tables?" was answered: no; PWM is computed.

### 4.4 `smp` / `slp` (`OscillatorSample.c`)

**Phase and binding:**
- **Phase:** Q20.12 frames (`OSC_SAMPLE_PHASE_SHIFT` 12), up to 1,048,575
  frames. The increment is `f × 2^22 / FS` (1,024 × f / FS frames per
  sample; the old `freq2PhaseIncr32767()` playback ratio).
- **`oscSample_bind()`:** runs every block.
  - Index = `min(sourceRequest, count − 1)`, so a short reinstall does not
    move the stored selection.
  - The cache is refreshed on an index or generation change.
  - `sampleStart = waveform × frames / 128`, linear (Q11); 0 if out of
    range. `sampleEnd = frames`.
  - With no samples installed it binds silence.

**Render:**
- **Reads:** clamp the index to start; interpolate to the next frame.
- **`slp`:** wraps the read and the phase to `[start, end)`; the next point
  of the last frame is `start` (the seam).
- **`smp`:** holds at `end − 1` and outputs 0 there and after (a select,
  constant cost; Q10).
- **Gain:** `sampleMemory_gainFromCode()` is applied once per block.
- **FM:** adds `(mod × fmMod) << 9`.

**Other entry points:**
- `oscSample_restart()`: phase = `start << 12`; the engines call it on a
  reset trigger.
- `oscSample_isFinished()`: `smp` and the read index + 1 ≥ end (D6).

### 4.5 `wtb` (`OscillatorWavetable.c`)

**State and binding:**
- **`OscWtbState`** (lives in the pwm union slot): `base` (0 = silent),
  `pos` (the frame position at the end of the last block), `frame_bytes`,
  `generation`, `frame_len`, `frame_count` (≤ 128), `bits` (8/16),
  `index`, `primed`.
- **Bind:** every block; re-reads the catalogue on an index or generation
  change.
- **F11:** the type writer clears `base` and `primed` on entering `wtb`.

**Frame pair (D1):**
- target = `wav × (n − 1) / 127`.
- On the first block (unprimed) `pos` jumps to the target.
- Each block picks `fa`/`fb` around `pos` toward the target, moves `pos` at
  most to the next frame boundary, and ramps the blend `ff` by
  `ffStep = (pEnd − p0) / size` (one float divide per block).

**Per-sample read:**
- `m = phase × frame_len` (64-bit); `idx = m >> 32`;
  `nx = idx + 1` wrapped to 0 at `frame_len`; fraction `(uint32)m × 2^−32`.
- Two points from each of `A` and `B`; two blends. 8-bit points are
  `int8 << 8`.
- **FM:** adds `(mod × fmMod) << 19`.
- **Increment:** `freq2PhaseIncr1024()` (2^32 per cycle).

### 4.6 `pwm` (`OscillatorPwm.c`)

**Width state:** `oscPwm_prepare()` runs on a frequency-cache miss and
computes the width state from c = `wav` / 127 (up to three divides per
block). The table level is taken at frequency × the warp compression.

| Family | Shape | Width rule | Reads per sample |
|---|---|---|---|
| `prc` | saw(p) − saw(p + shift) × scale | w = 0.5 − 0.46c (50 % → 4 %); scale = 1 / (2·max(w, 1−w)) | 2 |
| `psw` | saw(p) + saw(p + shift) × scale | w = 0.5c; scale = 0.5 / (1 − w) | 2 |
| `ptr` | warped triangle | w = 0.5 → 0.03 | 1 |
| `pq3` | warped `sq3` (stock) | natural split s0 = 0.78 (0.5 before an install), w = s0 → 0.12 | 1 |
| `pw3` | warped `sw3` | s0 = 0.42 (0.5 before), w = s0 → 0.04 | 1 |
| `swp` | 4 segments: sine → tri → saw → square/narrow pulse (`prc` with w = 0.5 − 0.46·mix) | seg = ⌊4c⌋, mix = frac | up to 3 + a blend |

**Read and warp details:**
- **Warp:** two slopes meeting at the split (`warpA = s0 / w`,
  `warpB = (1 − s0) / (1 − w)`); w is clamped to [0.02, 0.98].
- **F3:** `pwm_read1024()` interpolates with a 22-bit fraction ×
  2.384185791015625e−07. The whole-cycle positions use 2^−32.
- **F10:** the `pq3`/`pw3` split follows `sampleMemory_stockValid()`; bit 7
  of the `osc_setFreq()` cache key re-derives it after an install.

**Measured shape:**
- `prc` correlates > 0.99 with `Rec` (D4).
- `prc` high time rises from 50 % to 96 % as `wav` goes 0 → 127.

**Plots:** `tools/plots/plot_pwm.py` → `pwm_families.png` (the reference
model).

### 4.7 Run modes and the section (`OscSection.c`)

**`OscSectionState`** (140 B, one per engine):

| Field | Meaning |
|---|---|
| `runMode` | `rst` / `fre` / `itp` |
| `itpState` | IDLE / PENDING / ACTIVE |
| `itpPos` | the crossfade position |
| `itpOld` | one `OscInfo` copy of the carrier |
| `fSmooth`, `qSmooth`, `qTarget` | filter slew state |

**Trigger:** `oscSection_beginTrigger()` returns one of:
- **`OSC_TRIGGER_RESET_ALL` (`rst`):** the engine's usual resets. Drum
  resets the carrier only when the amp envelope is closed (as before) and
  always clears the filter; Cymbal also resets its two modulators; Snare
  and HiHat do not.
- **`OSC_TRIGGER_RESET_NONE` (`fre`):** arms the slew on every oscillator;
  no reset; the filter keeps ringing.
- **`OSC_TRIGGER_RESET_CARRIER` (`itp`):**
  - Snapshots the carrier into `itpOld` (step 0, no interpolation) and
    arms the modulators' slew.
  - State goes PENDING, or stays ACTIVE.
  - The engine restarts the carrier.

The phase offsets are unchanged:
- `smp`/`slp` → `oscSample_restart()`;
- pwm/wtb and table waves → `(0xff << 20) × offset`;
- sine → `1024 + ((0x3ff << 20) − 1024) × offset` on Drum, `(0x3ff << 20)
  × offset` on the others;
- otherwise 0.

The pre-S084 transient tests `transGen.waveform == 0/1` are kept.

**Why `itp` crossfades only the carrier (plan §4.3.7):** a retrigger click
is an amplitude step. In the oscillator section it has three sources:
1. The carrier reset. This is what the crossfade hides.
2. A modulator reset in `mix`. This is a direct step.
3. A modulator reset in FM. This changes the carrier's frequency, not its
   amplitude: a kink, not a click.

In `itp` the modulators are never reset (they slew as in `fre`). The old
carrier copy renders against the **same live modulator block**, so its FM
tail is exactly the un-retriggered sound. Only source 1 remains, and it is
crossfaded over `OSC_ITP_SAMPLES` 132.

**Differences from the spike's whole-section crossfade:**
- In `itp` the Cymbal FM attack is not identical from hit to hit (it is
  in `rst`).
- The two carriers differ in phase for about 3 ms (a comb character).
- P1 F6 (retrigger inside 3 ms) still applies (§14).

**The block, in engine order:**
1. The mixer claims (§4.8).
2. `osc_setFreq()` for each oscillator.
3. The section render: `oscSection_renderDrum()` (fm/mix/snc/rng) or
   `oscSection_renderFm2()` (two modulators, saturating sum, FM carrier at
   `carGain`: Cymbal 1.0, HiHat 0.5). With `oscSection_itpCarrier()` set,
   the old carrier renders into `oscRenderPool_scratch()` with the same
   modulator block.
4. `oscSection_blendItp()` (linear over 132 samples).
5. `oscSection_finishBlock()` (advance; IDLE at 132).

Snare renders its tone oscillator and blends it before the transient sum.

**Combination modes (Drum `mod`; `mixFmNames` 4 values):**

| Mode | Output |
|---|---|
| `fm` (0) | modulator at `amount` into the carrier FM; unchanged |
| `mix` (1) | carrier × (1 − amt) + modulator × amt (the modulator rendered at `amt`); unchanged |
| `snc` (2) | §4.9 |
| `rng` (3) | carrier × (1 − amt) + ((carrier × modulator) >> 15) × amt, saturated |

**Filter slew** (`oscSection_filterRecalc()`, called by
`instrumentManager_recalcSlotFilter()`):
- **`rst`:** exactly `SVF_recalcFreq()`; the smoothers are reseeded.
- **`fre`/`itp`:** cutoff and resonance slew with coefficient 0.28 per
  block (≈ 2 ms), through `SVF_recalcFreqFrom(filter, fUsed)`.
- **Modulated `q` (F4):** a `q` written since the last block becomes the
  new target. `oscSection_noteResoTarget()` is called by the resonance
  writer.

### 4.8 The render pool (`OscRenderPool.c`, `mixer.c`, `modulationNode.c`)

**The pool:**
- `OSC_RENDER_POOL_UNITS` 2 (`config.h`; replaces
  `OSC_WAVE_INTERP_MAX_ACTIVE` 2); `oscRenderPool_unitsUsed`; one
  `int16_t` scratch block of 32.
- One unit = one extra oscillator render per block: an `itp` old carrier,
  or the second render of a `wav` interpolation pair.

**Order in `mixer_calcNextSampleBlock()`:**
1. `oscRenderPool_beginBlock()`.
2. `instrumentManager_claimOscSectionOngoing()`: ACTIVE crossfades keep
   their unit, lowest slot first. A refused one ends.
3. `modNode_resetTargets()`, `modNode_reassignVeloMod()`.
4. `instrumentManager_dispatchRuntimeLfos()`. Waveform interpolation claims
   here, `wav` type only.
5. `instrumentManager_claimOscSectionPending()`: new crossfades, lowest slot
   first. A refused trigger restores the carrier phase from the snapshot
   and arms its slew (behaves as `fre`).
6. Filter recalc, slot async (`osc_setFreq()`), then render.

### 4.9 Constant-cost hard sync (`snc`; assessment A2)

**Original form:** the schedule's `snc` rendered the slave in segments,
calling the renderer once per master wrap. A sample master at a high note
wraps on every sample, so the per-call overhead landed on every sample:
dispatch, bind, the frame pair with its divide; 38 to over 100
instructions. C10.6 step 4 replaces it.

**Lane** (`OscSyncLane.h`): `OscSyncLane { wrap*, over*, ratio }`.
- `oscSection_buildSyncLane()` walks a shadow of the master phase once per
  block, before the master renders:
  - cycle-relative: a sample master starts at `phase − (start << 12)` (F5);
  - its cycle is `(end − start) << 12`; a periodic master wraps at 2^32.
- It records `wrap[i]` (wrapped after sample *i*) and `over[i]` (the
  overshoot; the sample-master modulo runs every sample).
- A finished one-shot master records no wraps (D6).
- `wrap[32]` and `over[32]` are on the stack (160 B).

**Slave render:**
- One call per block: `osc_renderSyncBlock(slave, buf, size, 1.0, &lane)`.
  The `itp` copy uses the same lane with its own ratio. The master renders
  once at unit gain.
- After each sample, `oscSync_phase()` selects
  `base + over[i] × ratio` if `wrap[i]` is set, so the reset lands on
  *i* + 1 (D13). `base` is 0, or `start << 12` for `smp`/`slp`.
- `ratio` = slave inc / master inc, once per block (Q5).
- A synced one-shot slave restarts from its start on every master wrap.
- Mix pass: slave × (1 − amt) + master × amt, saturated (Q10).

**Code placement (ITCM):**
- **wav cores** (`calcSineCore`, `calcWavetableOscCore`, `calcNoiseCore`,
  `calcSampleOscCore`, `calcStockWaveCore`):
  - always-inline with a lane argument;
  - the plain entry points pass a literal NULL, so the plain loops are
    unchanged (fpseq counts, `osc_s0`);
  - their sync copies live in `osc_renderSyncBlock()` (1,972 B).
- **pwm, smp/slp, wtb:**
  - one shared loop each (`oscPwm_blockLane()`, `oscSample_blockLane()`,
    `oscWavetable_blockLane()`, `OSC_SHARED_LOOP` = noinline, noclone,
    `optimize("no-unswitch-loops")`, GCC only);
  - a per-sample lane test, +2 instructions on their plain render;
  - separate sync copies of everything had cost 5.6 KB of ITCM
    (16,272 / 16,384 B).
- GCC compiles the select as a branch around the reset arithmetic. The
  cost record budgets the wrap path.

### 4.10 Rows, cells and keys

- **Selector rows** (`ROW_SELECT_SPECIAL`): `DTYPE_0B127`,
  `INSTRUMENT_PARAM_FLAG_AUTOMATABLE` only (not MORPHABLE), `MOD_NONE`,
  `BIND_SPECIAL(member, TYPE_UINT8, IM_SPECIAL_…)`. The `wav` rows gain
  `IM_SPECIAL_OSC_WAVE | IM_SPECIAL_OSC<n>`.
- **Keys and indices:**

| Key | Drum | Snare | Cymbal | HiHat | Binds |
|---|---:|---:|---:|---:|---|
| `osc1_type` | 39 | 38 | 39 | 39 | `osc.oscType` |
| `osc1_source` | 40 | 39 | 40 | 40 | `osc.source` |
| `osc2_type` | 41 | — | 41 | 41 | `modOsc.oscType` |
| `osc2_source` | 42 | — | 42 | 42 | `modOsc.source` |
| `osc3_type` | — | — | 43 | 43 | `modOsc2.oscType` |
| `osc3_source` | — | — | 44 | 44 | `modOsc2.source` |
| `osc_run_mode` | 43 | 40 | 45 | 45 | `section.runMode` |

  NRPN ID = slot × 64 + local index. `osc2_mod_type` (Drum `mod`) has no
  special tag.
- **Cells:**
  - Drum page 0: `coa fin wav typ src run`. Drum FM page: `amt coa wav mod
    typ src`.
  - Snare page 0: `coa fin noi mix wav typ src run` (`typ` at cell 5, D3).
  - Cymbal/HiHat page 0: `coa fin wav typ src run`. Mod page: `co2 co3 am2
    am3 wv2 wv3 typ src typ src`.
- **Display (`InstrumentCellFormat`):**

| Row | Short (3 chars) | Full |
|---|---|---|
| `typ` | `wav pwm smp slp wtb` | — |
| `run` | `rst fre itp` | — |
| `src`, wav type | `-` | — |
| `src`, pwm | `prc psw ptr pq3 pw3 swp` | — |
| `src`, sample | `sNN` (`s01`…`s99`, `sA0`…) | the 8-character name |
| `src`, wavetable | `wNN` | the 8-character name |
| `wav`, wav type | `Sin Tri Saw Rec Noi Cym sq3 sw3 sqa tra swa` | — |
| `wav`, pwm | number | `width NNN` |
| `wav`, smp/slp | number | `start NNN` |
| `wav`, wtb | number | `frame NNN` |

### 4.11 InstrumentManager

**Writers** (`InstrumentManager.h`, count 24 ≤ 32):
`IM_SPECIAL_OSC_TYPE` 20, `IM_SPECIAL_OSC_SOURCE` 21, `IM_SPECIAL_OSC_WAVE`
22, `IM_SPECIAL_RUN_MODE` 23.
- **TYPE:**
  - Clamps to 0–4.
  - On entering `wtb` it clears `wtb.base` and `primed` (F11).
  - It re-derives `waveform` from `waveRequest` and `source` from
    `sourceRequest`, clamped to the new type's maxima (F2), and
    invalidates the frequency cache.
  - It binds `smp`/`slp`/`wtb` and refreshes the slot's `wav` domains.
- **SOURCE:** records `sourceRequest` if retained, clamps (pwm 5; sample or
  table count − 1), and binds.
- **WAVE:** records `waveRequest` if retained, clamps (`wav` type 10; other
  types 127), and rebinds `smp`/`slp` (the start follows `wav`).
- **RUN_MODE:** clamps to 0–2.
- **`retained`:** the writer receives `notify_base_change` as `retained`
  (D2).

**Domains (D11 + A9):** `instrumentManager_descriptorModDomainForSlot()`
gives the per-slot `wav` maximum. It is used for velocity range building,
the LFO base update and LFO adapters.
`instrumentManager_refreshWaveDomains()` runs on a type change: it
recomputes each LFO adapter's domain, sets its base to
`min(osc->waveRequest, new max)` (A9), and reinstalls velocity targets.

**Accessors and other changes:**
- `instrumentManager_oscWaveMaxForType()`, `_oscSourceMaxForType()`,
  `_isSelectorDescriptor()` (INSTANCE_OFFSET, not MORPHABLE, with a
  special writer), `_oscSection()`, `_oscForDescriptor()`, the carrier
  accessor, `_claimOscSectionOngoing()` / `_Pending()`.
- The resonance writer calls `oscSection_noteResoTarget()`. Filter recalc
  goes through the section.
- The classifier gained the osc keys, excluding `_mod_type`.
- **Runtime-slot reserve:** fixed `INSTRUMENT_RUNTIME_SLOT_BYTES` 1,176
  (it was `2 × sizeof(DrumVoice)`), with `_Static_assert`s.

### 4.12 Preset (`presetManager.c`)

The selector rows are INSTANCE_OFFSET rows without MORPHABLE. Before S085,
`preset_setSupplementalParameterInternal()` rejected every INSTANCE_OFFSET
row, and the Morph image skips non-morphable rows. Menu, MIDI and file
values therefore never reached the runtime (S084 P1 F1).

| Change | Where | What |
|---|---|---|
| Store | `preset_setSupplementalParameterInternal()`, before the rejection | Normal cell via `preset_storeInstrumentEndpoint()` (dirty, AutoSave, card-clean); the Morph mirror and `morph_interpolation[]` set equal (step-automation release writes that cell back); the runtime written for the active Scene or with `force` |
| Apply | new `preset_applySelectorRows(scene, voice)` | copies each selector row's Normal cell into both mirrors and writes the runtime, retained, in descriptor order (all typ/src pairs, then `run`) |
| Called from | `preset_resetAndApplyKitVoiceImage()` right after `instrumentManager_resetRuntimeSlotFromScene()`; `preset_startInstrumentApplyImage()` after `instrumentManager_resetRuntimeSlot()` | covers boot `preset_sendDrumsetParameters()`, per-track played Scene, the Scene-switch worker, the deferred slot on trigger, Scene 17, background Bank load, copy/clear paste, AutoSave restore, immediate Instrument apply |
| Rebind | `preset_applyKitVoiceSupplementalInternal()` | skips selector rows (a held automation value is not overwritten; side effects not repeated) |

**Ordering:** the rows land before the Morph image, because the `wav` row is
clamped against the incoming type.

**Unchanged:**
- Kit, Instrument and Scene save write `[params]` for every row; a missing
  key loads 0.
- AutoSave journals the Normal cell.
- Morph-endpoint-only loads never touch single-ended rows.

### 4.13 Menu (`menu.c`, `MenuText.h`, `InstrumentCellFormat.c`)

**Name tables** (`MenuText.h`; count `_Static_assert`s in `menu.c`):
`oscTypeNames` (5), `runModeNames` (3), `pwmFamilyNames` (6), `mixFmNames`
(4), `waveformNames` (11).

**Formatted cells:**
- `menu_instrumentCellText3()` and `menu_instrumentCellFull()` are called at
  the five value-formatting sites.
- `instrumentCellFormat_describe(scene, slot, descriptor, value)` returns
  kind, clamped index, maximum and aux (the frame count). It reads the
  stored type from the shown Scene image (D5).
- `menu_clampCellValue()` clamps to the formatted maximum.

**`DTYPE_MIX_FM`:** four values at all six sites (format, clamp 3, automation
maximum 3).

**`MENU_WAVEFORM`:** 11 entries, no sample tail. `menu_numSamples` and
`menu_setNumSamples()` are removed (D8).

**Automation:** `menu_automationValueMaxSlot()` gives the slot-aware maximum
and text.

**SHIFT Morph (D12):** selector rows commit to Normal.

**Load:[Samples]:**
- **Process:** audio suspended; `filesystem_installSamplesBlocking(&report)`
  with a progress callback.
- **Progress line:** `Erasing`, `Stock waves`, `Samples NNN`,
  `Wavetbl NNN`, `Commit`.
- **Summary:** `Smp NNN Wtb NNN` / `Skip NNN`, or `Sample upload` /
  `Failed`.
- **Hold:** the summary stays until a 1,500-tick deadline (`time_sysTick`
  is 1 kHz); `menu_serviceInstallSummary()` then repaints.

### 4.14 Sample flash catalogue v2 (`SampleMemory`, `SampleInstall`, `sampleFlash`)

**Map:** sectors 7–11, 1,310,720 B. Regions are 32 B aligned.

| Region | Address | Bytes |
|---|---|---:|
| Stock block | `0x080C0000` | 61,120 |
| Items (samples, wavetable frames, 4-byte aligned) | `0x080CEEC0` | 1,243,456 |
| Catalogue | `0x081FE800` | 6,144 |

User space fell by 64,860 B against v1 (1,308,316 B): the stock block plus
the larger catalogue.

**Records** (naturally aligned):

| Record | Size | Fields |
|---|---:|---|
| Header | 32 B | `magic` `0x3243584C` "LXC2", programmed last; `version` 2; `entry_bytes` 20; `sample_count`; `wavetable_count`; `stock_valid`; `data_end`; `stock_start` |
| Sample entry (×128) | 20 B | `offset`, `frames` (excluding the 2-frame zero pad), `name[8]`, `gain_code` |
| Wavetable entry (×128) | 20 B | `offset`, `frame_len` (≥ 2), `frame_count` (1–128), `bits` (8/16), `name[8]` |

The catalogue occupies 5,152 B of the 6,144 B region.

**Reading:**
- **States:** EMPTY (erased, never installed, or torn), V2, STALE (a v1
  count word 1–120 in sector 7).
- **API:** in place, no RAM caches (−5,040 B): `sampleMemory_init()`,
  `_refresh()`, `_catalogueState()`, `_getGeneration()` (bumped per
  refresh; the DSP binds compare it), `_getNumSamples()`,
  `_getSampleInfo()`, `_getDisplayName()`, `_getNumWavetables()`,
  `_getWavetableInfo()`, `_getWavetableName()`, `_stockValid()`.
- **`SAMPLE_FLASH_PTR(addr)`:** wraps every read so the host tests can map
  flash onto RAM.
- **Gain:** `gain = 2^(code/24)` (`sampleMemory_gainFromCode()`: a
  24-entry mantissa table plus an exponent add, no libm). The installer
  uses `code = ⌊24 · log2(32767 / peak)⌋`, clamped 0–255, so normalisation
  never clips.

**Installer core** (`SampleInstall.c`, 44 B state; the card is unknown to
it):
- **Formats:** `sampleInstall_acceptFormat()` accepts PCM or
  WAVE_FORMAT_EXTENSIBLE PCM.
  - Samples: mono 16-bit 44,100 Hz only.
  - Frames: 1–2 channels, 8/16/24-bit. Stored depth: 8-bit stays 8-bit;
    16 and 24 become 16; stereo becomes mono.
- **Sequence:** `begin()` (erase sectors 7–11) → `writeStockBlock()` →
  per sample `beginSample()` / `writeSampleData()` / `finishSample()` →
  per table `beginWavetable(name, fmt, frame_len, frame_count)` /
  `setFrameFormat()` per frame / `writeFrameData()` /
  `finishWavetable()` → `commit()`.
- **Carries:** a source frame can split across two 512 B reads (in-carry);
  programming is per 32-bit word (out-carry).
- **Entries:** each item's entry is programmed at its finish. `commit()`
  programs the header without its magic, then the magic, invalidates the
  D-cache and refreshes. A power loss before the magic leaves EMPTY.
- **Errors:** −1 wrong sequence; −2 table full (128); −3 format or no
  space; −4 short item (aborted); otherwise a `sampleFlash` status.
  `abortItem()` drops the open item; its bytes stay unused until the next
  erase.

**Card walk** (`filesystem.c`, `filesystem_installSamplesBlocking()`):
- **Order:** erase → stock block → `samples/` tree → `wavetables/` tree →
  commit.
- **Directory reading:** `filesystem_installCollect()` reads a directory
  in batches of 32 (`fs_install_batch`), sorted by
  `storage_naturalCompare()` on the long name. The key is up to 48
  characters (`AFATFS_LONG_FILENAME_MAX`); longer or non-ASCII names fall
  back to the 8.3 alias. Ties fall back to `strcmp`, then the short name.
  The walk resumes after the last key, so any number of files is handled
  with 2 KB of RAM.
- **Natural order** (`storageTypes.c`): numeric runs compare by value
  (length after leading zeros, then digits); ASCII letters compare case
  folded.
  - `storage_stripOrderPrefix()` drops a leading number plus
    space/`_`/`-` from **wavetable folder** display names. Sample display
    names use the file name as is.
  - The 8-character display name compresses longer stems to first 4 +
    `FS_SAMPLE_ELLIPSIS` + last 3.
- **Samples:** files first, then subfolders, to depth 3
  (`FS_INSTALL_MAX_DEPTH`).
- **Wavetables:**
  - Every folder under `wavetables/` that holds valid `.wav` files is one
    table, named by the folder; its subfolders are further tables (depth
    3).
  - **Pass A** counts frames that match the first valid frame's length and
    stored depth (at most 128; more are counted as skipped, N4).
  - **Pass B** copies the same frames, calling `setFrameFormat()` per file
    (P2 F4: each frame decodes its own source format).
- **Errors:**
  - An unreadable or unsupported file is skipped (`skipped_files`), and
    an unreadable table is skipped (`skipped_tables`) (P2 F7).
  - A flash or state error aborts the install (`fs_install_failed`).
- **Opens:**
  - `filesystem_blockOpenDirLfn()` opens long-named subfolders (P2 F1).
  - `filesystem_blockOpenGiveUp()` bounds every blocking open to 2,000
    retries (`FS_BLOCK_OPEN_HANDLE_RETRIES`), so a deep or broken folder
    cannot hang (P2 F2).
- **Removed:** the v1 sample helpers and `sample_manifest`; `/loops` is
  retired.
- **Boot notice** (`main.c`, after `sampleMemory_init()`; 1.5 s): STALE →
  "Samples invalid" / "reload samples"; EMPTY → "No samples" /
  "load samples".

### 4.15 Card and tools

**Card:**
- `SD_CARD/loops/` became `SD_CARD/wavetables/ak_hvox/` (the user's move).
- `tools/sampleoptimize.py` optimised it from 600 to 256 points per frame,
  124,800 → 53,248 B (worst −23.2 dB, median −44.8 dB; table scaled
  −0.14 dB).
- Originals are in `SD_CARD/wavetables_old/ak_hvox/`; `samples_old/` was
  created by the tool. `SD_CARD/samples/01_cut.wav` is unchanged
  (7,460 B).

**`tools/sampleoptimize.py [CARD_ROOT] [--worst-db -20] [--typical-db -40]
[--trim-db -80] [--dry-run]`:**
- **Backups:** `wavetables_old/`, `samples_old/` (an existing `_old` folder
  is the source on re-runs).
- **Wavetables:** the smallest power-of-two frame length that plays the
  same, judged the way the oscillator reads it (worst frame ≤ −20 dB,
  median ≤ −40 dB). Harmonic-truncation resampling; one common length per
  table; common down-scaling if a peak would clip.
- **Samples:** trim the tail below −80 dB; convert non-installable WAVs.

**Other tools:**
- **`tools/gen_stock_recipes.py [--emit-ref PATH]`:** CSV → recipes, plus
  the int16 reference for `stock_gen`.
- **`tools/plots/`:** `lxr_plotlib.py`, `README.md`, `plot_pwm.py` →
  `pwm_families.png`, `plot_stock_waves.py` → `stock_waves_303_analog.png`
  (with `reference/303_square_saw.png`, `tra_dirty_triangle.png`),
  `plot_wavetable_formats.py` → `wavetable_formats.png`,
  `stock_wave_harmonics.csv`. The S086 waveshaper plots were left in the
  spike.

---

## 5. Resources

### 5.1 RAM (approved, Q4)

**SRAM1 `.bss`:**

| Item | Bytes |
|---|---:|
| `oscRenderPool_scratch[32]` + `oscRenderPool_unitsUsed` (approved 129 B) | +65 (+3 alignment) |
| installer selection: `fs_install_batch[32]` (64 B entries), `fs_install_batch_count`, `fs_install_last[4]`, report/progress/failed | +2,313 |
| `sample_install` state | +44 |
| catalogue counters/state/stock flag | +3 |
| `menu_installSummaryUntil` | +2 |
| `sample_manifest[120]` + count removed | −13,441 |
| `SampleMemory` caches (`sample_info_cache`, `sample_name_cache`, `sample_loop_cache`, `install_info`, `install_names`) and v1 install scalars removed | −5,040 (+ ≈ 20 scalars) |
| `sample_io_buf` 516 → 512 | −4 |
| `menu_numSamples` | −1 |
| **Measured `bss` change** | **−16,100** (released, not reused) |

**DTCM:** `modNode_waveInterpActiveCount` −1 B; statics 4,468 B;
`osc_interp_a/b` (2 × 64 B) unchanged.

**Stack:**
- The card walk keeps one 64 B entry per level, ≈ 200 B, in the modal
  install only, with audio suspended.
- The `snc` lane uses 160 B in `oscSection_renderSync()` on the audio
  stack (assessment A2).

**Runtime slots:** 7,056 B (6 × 1,176 B, fixed).

| Struct | Spike | S085 estimate | S085 measured | Margin |
|---|---:|---:|---:|---:|
| `DrumVoice` | 1,072 | ≈ 832 | 832 | 344 (pre-S085 588) |
| `HiHatVoice` | 1,016 | ≈ 776 | 780 | 396 |
| `CymbalVoice` | 1,008 | ≈ 768 | 772 | 404 |
| `SnareVoice` | 904 | ≈ 664 | 664 | 512 |
| `OscInfo` | 120 | — | 124 | — |
| `OscSectionState` | 380 | ≈ 140 | 140 | — |

The spike's S086 work needed about 44–48 B per engine.

### 5.2 Flash, ITCM

| Item | S084 close | S085 implementation | S085 after fixes |
|---|---:|---:|---:|
| `text` | 609,288 | 640,960 | **642,360** |
| `data` | 424 | 424 | 424 |
| `bss` | 430,200 | 414,100 | **414,100** |
| Flash used / headroom (of 753,664) | 609,712 / 143,952 | 641,384 / 112,280 | **642,784 / 110,880** |
| ITCM (of 16,384) | 4,168 | 10,632 | **12,432** |
| DTCM statics | 4,472 | 4,468 | 4,468 |
| FXBUF (min 122,880) | 126,592 | 126,592 | 126,592 (margin 3,712) |

- The plan estimated ≈ 645–650 KB flash and ≈ 10.0–10.9 KB ITCM.
- ITCM rose by 1.8 KB in the fixes (the wav sync copies).
- The largest ITCM blocks after LTO:

| Block | Bytes |
|---|---:|
| `calcNextOscSampleFmBlock` | 4,508 |
| `osc_renderSyncBlock` | 1,972 |
| `calcNextOscSampleBlock` | 1,504 |
| `oscPwm_blockLane` | 1,504 |
| `osc_setFreq` | 1,372 |
| `oscWavetable_blockLane` | 856 |
| `oscSample_blockLane` | 464 |
| `calcFmBlock` | 220 |

### 5.3 Constants

See §4 and §13. The single source of truth for each constant is its header:
`Oscillator.h`, `OscSection.h`, `OscillatorSample.h`, `OscillatorPwm.h`,
`StockWaves.h`, `SampleMemory.h`, `InstrumentManager.h`, `config.h`,
`filesystem.c/.h`.

---

## 6. Implementation record (schedule §21, summarised)

**WP0:**
- `snapshot.sh` gained a destination argument and the S085 list.
- `frozen85/` holds 14 files: `Oscillator.c/h`, `ResonantFilter.c/h`,
  `BufferTools.c/h`, `Samples.c`, `mixer.c`, `modulationNode.c`,
  `wavetable.c` and the four engine `.c` files.
- Baseline DEV: `text=609,288 data=424 bss=430,200`.

**WP1–WP3:**
- Catalogue, installer, walk, boot notice and modal as in §4.13–§4.14.
- `natural_order` and `catalogue` passed.
- G1 was folded into G3 (WP1 removed the v1 API while `Oscillator.c`
  still used it).

**WP4:** stock waves; `stock_gen` worst 1 LSB; the generator header
corrected; SHA-256 in the generated file.

**WP5–WP11:** model, renderers, pool, section, engines; `osc_run` and
`osc_s0` passed.

**WP12–WP15:** rows 44/41/46/46, `special_tags` 177/0, writers, domains,
Preset, menu.

**WP16–WP17:**
- `make all` clean, DEV `text=640,960`.
- New targets: `natural_order`, `catalogue`, `stock_gen`, `osc_run` (+
  `osc_pwm`/`osc_sample`/`wtb` aliases), `osc_s0`,
  `armcheck-osc-base`/`armcheck-osc`.
- The card optimised (§4.15).

**WP18–WP19:** first spec pass; G0–G4 reported green.

**Open at the end of §21:**
- the per-sample probes, the `snc` cap and the fallback loops;
- the break proofs;
- the engine sizes;
- the hardware matrix.

---

## 7. Assessment (schedule §22, 2026-10-27)

- **Re-run:** the build and the whole host suite, independently.
- **G4 leftover search:** 0 hits for `insertFx_`, `driveShaper`,
  `transient_is`, `voiceBuffer_`, `slotQuiet`, `OSC_SAMPLE_START`,
  `OSC_WAVE_INTERP_MAX_ACTIVE`, `menu_numSamples`, `sampleMemory_install`,
  `filesystem_installLoopsBlocking`, `calcUserSampleOsc`,
  `sampleCacheLooped`, `sampleMemory_isLooped`.

**Findings:**

| # | Severity | Finding |
|---|---|---|
| A1 | high, correctness | The last slew block overshoots: 132 is not a multiple of 32, so the final block divides by 4 and applies the step for 32 samples. The increment peaked 20.5 % of the interval past the target, then snapped back. |
| A2 | high, CPU | `snc` over the C10.6 cap: one renderer call per wrap; one wrap per sample is reachable (sample master at a high note, or `wav` 127). The fallback loops had not been added. |
| A3 | high, process | No cost record. `looplen.py` counted instructions outside loops (the §21 "18/22/17/20" were prologues); the C17.4 one-loop wrappers were never built. |
| A4 | medium, test | The `itp` click test could not fail: no carrier reset and no pitch change, so old and new carriers were bit-identical. |
| A5 | medium, test | The slew test never rendered, so it missed A1. |
| A6 | medium, test | Missing C17.6 cases: pool order with interpolation; `snc` reset index, mix law, start > 0; `rng`; full playback, re-expansion, gain, a seam check that could fail; `wtb` continuity, bounds, 8-bit; pwm F3 midpoints, > 0.99 polarity, monotonic width. Separate test files were merged into `osc_run` modes (harmless). |
| A7 | medium, test | `osc_s0` omitted noise, HiHat 0.5, an `osc_setFreq` pitch change, and compared against a hand-written old composition instead of the frozen engine lines. |
| A8 | low, behaviour | An `itp` retrigger during an active crossfade steps by (1 − t) × (old − new) (P1 F6). |
| A9 | low, behaviour | Narrowing the `wav` domain clamped the LFO base permanently; back on pwm/smp/wtb the LFO stayed centred on 10. |
| A10 | low, process | Break proofs "for hardware/CI" (they are host runs); engine sizes unmeasured. |

**Found correct:**
- the dispatch and the slew placement;
- the writers, D2, D11, F11;
- the pool order and the refused-claim fallback;
- `snc` semantics apart from cost (D13, D6, F5, ratio once per block);
- `rng`, `mix`, the filter slew;
- the engines (Drum's reset gating and filter clear, HiHat 0.5, Cymbal
  modulators reset only in `rst`, Snare's blend placement);
- Q20.12, the `smp` end select, the `slp` wrap, the 128-frame cap;
- Preset C14.1–C14.5; menu D5, D12, the 1,500-tick summary.

---

## 8. Fixes (schedule §23, 2026-10-27)

| # | Fix |
|---|---|
| A1 | Divisor `max(left, OUTPUT_DMA_SIZE)`; difference in `uint32_t` (§4.2). |
| A2 | Constant-cost `snc` with `OscSyncLane.h` (§4.9). |
| A3 | `osccost.py` (CFG longest path per natural loop, dominator back edges) and `armcheck/probe_wav.c`, `probe_pwm.c`, `probe_smp.c`, `probe_wtb.c`, `probe_section.c`; the cost record (§9). |
| A4 | The `itp` test applies the engine's carrier reset and a new pitch (220 → 330 Hz), runs the mixer claim order for 6 blocks in fm and mix, and asserts the old and new blocks differ. Bound = the same sound changing pitch without a trigger + 65,536 / 132. Measured: fm 1,785 ≤ 2,404 (rst 32,365); mix 921 ≤ 1,406 (rst 16,007); mix with a modulator reset 9,222 (must exceed). |
| A5 | The rendered slew trajectory, up and down: monotonic, never past the target, reached within 6 blocks, write-back matches. |
| A6 | The new cases listed in A6; host hooks `host_set_gain_code()`, `host_set_table_bits()`, `host_rng_reset()`, `host_sample_data()`, `host_table_data()`. |
| A7 | `osc_s0`: frozen noise; a 20-block sweep 27.5 Hz → 10.9 kHz through frozen and current `osc_setFreq()` (increments, levels, samples); Drum fm/mix (4 cases) and Cymbal/HiHat FM2 against the verbatim frozen85 engine lines (Drum 313–326, Cymbal 240–247, HiHat 257–264) via `extract.py --lines`, `s0_engines.c`, `s0_engine_shim.h`; `extract.py` skips comments between `)` and `{`. |
| A8 | Accepted and documented at `oscSection_beginTrigger()`; H7 gains a fast roll. An exact fix needs a second carrier copy (+124 B per voice). |
| A9 | The LFO base re-derived from `waveRequest` (§4.11). |
| A10 | `breakproofs.py` / `make breakproofs` (temporary copy of the tree); sizes recorded (§5.1). |

---

## 9. Verification

### 9.1 Host gates at close (all green)

| Target | Result |
|---|---|
| `make all` | clean (only the toolchain `_close/_lseek/_read/_write` link notes); `make img` 642,784 B |
| `natural_order` | 0 failures |
| `catalogue` | 0 failures: states, commit/reload, gain code 23 for peak 16,384, limits, fit, mixed frames, abort, short item, torn install, entry bounds, 128 samples |
| `stock_gen` | worst 1 LSB, 0 failures |
| `osc_run` (`all`, `run`, `pwm`, `sample`, `wtb`) | 0 failures (§8 A4–A6) |
| `osc_s0` | 0 failures (§8 A7) |
| `special_tags` | 177 rows, 0 mismatches |
| `selftest`, `filter` (S1 PASS), `pack`, `postchain`, `mixer` | unchanged |
| `octave` | its documented 16 mismatches (≤ 2 ulps) |
| `armcheck-postchain` | MATCH |
| `armcheck-mixer` | 87 → 78 |
| `breakproofs` | 26/26 caught |

### 9.2 Break proofs (C17.9)

| ID | Defect | Caught by |
|---|---|---|
| B1 | slew divisor back to `left` | slew never past target (`osc_run`) |
| B2 | `itp` blend no-op | itp step 32,365 > 2,404 |
| B3 | sync reset one sample early | D13 (saw, smp, pwm, wtb) |
| B4 | `snc` mix law swapped | no-wrap identity, mix law |
| B5 | `rng` `>> 14` | rng law |
| B6 | shadow ignores the sample start | F5 |
| B7 | a finished master still resets | D6 |
| B8 | `itp` copy uses the carrier's ratio | itp copy ratio |
| B9 | smp sync base 0 | D13 smp |
| B10 | pending ignores the pool | pool fallback and order |
| B11 | `smp` end not silent | end silence, full playback (`osc_sample`) |
| B12 | `slp` seam reads the last frame twice | seam |
| B13 | gain code ignored | gain ×2 |
| B14 | the clamp overwrites `sourceRequest` | re-expansion |
| B15 | `wtb` nx bound removed | nx wrap (`wtb`) |
| B16 | `wtb` 8-bit `<< 7` | 8-bit path |
| B17 | `wtb` phase not written back | block continuity |
| B18 | pwm fraction 1/2^32 (F3) | midpoints (`osc_pwm`) |
| B19 | `prc` polarity inverted | polarity, width |
| B20 | HiHat carrier gain ignored | hihat fm2 (`osc_s0`) |
| B21 | noise value halved | noise, sweep |
| B22 | table level capped at 9 | sweep block 19 |
| B23 | Drum `mix` carrier gain | drum mix |
| B24 | natural order ignores run length | f2 < f10 (`natural_order`) |
| B25 | gain code not from the peak | gain code 23 (`catalogue`) |
| B26 | stock level scale dropped | wave 0 level 0 (`stock_gen`) |

**Not host-testable** (`InstrumentManager.c`, `presetManager.c` and
`menu.c` are not in the harness): F11, A9, the selector store/apply, D5,
D12. They are hardware rows H4a–H4d, H6, H9.

### 9.3 Cost record (worst-case instructions per sample)

- **Method:** `osccost.py`, longest path through each loop, on
  `arm-none-eabi-gcc 14.2 -Ofast`, Cortex-M7.
- **Firmware plain pwm/smp/wtb** = the no-lane figure + 2.
- **Calls count 1.**
- **Not included:** sample-flash wait states.

| Loop | pre-S085 | plain | FM | as `snc` slave |
|---|---:|---:|---:|---:|
| table Tri/Saw/Rec | 24 | 25 | 36 | 35 |
| sine | 23 | 24 | 31 | 34 |
| noise | 16 + call | 16 + call | (plain) | 20 + call |
| crash | 30 | 31 | 39 | 37 |
| stock | — | 24 | 34 | 35 |
| pwm `prc`/`psw` | — | 51 | 56 | 63 |
| pwm `ptr`/`pq3`/`pw3` | — | 42 | 47 | 53 |
| pwm `swp` seg 0/1/2/3 | — | 48/50/58/52 | 53/55/62/57 | 60/63/70/65 |
| `smp` | — | 40 | 45 | 53 |
| `slp` | — | 47 | 53 | 61 |
| `wtb` 8/16-bit | — | 40/43 | 44/48 | 51/54 |

**Section loops:**

| Loop | Worst |
|---|---:|
| `snc` lane, full-wrap master | 11 |
| `snc` lane, sample master (includes a UDIV) | 18 |
| `snc` mix pass / with the `itp` copy | 16 / 28 |
| `rng` | 19 |
| `itp` blend | 23 |

- The pre-S084 "table 24 / FM 35" and the spike's "24 / 35 with slew" were
  the same number. The slew costs 1 instruction; the spike's figure was
  stale.
- D1 met: `wtb` 43 / 48 against 42 / 48. The dearest oscillator is
  `pwm swp` segment 2, not `wtb`.

### 9.4 Derived worst cases (for S086)

| Engine / mode | Worst (instructions per sample, oscillator section) |
|---|---:|
| Drum `fm` | 120 (58 + 62) |
| Drum `mix` | 116 (+ the saturating add) |
| Drum `snc` | **162** (lane 18 + master 58 + slave 70 + mix 16) |
| Drum `rng` | 135 |
| Snare | 74 (tone 58 + noise 16) |
| Cymbal / HiHat | **178** (2 × 58 + carrier FM 62; + the add) |
| Pre-S085 Cymbal | 98 |
| One `itp` unit | ≤ 105 |
| Pool (2 units) | ≤ 210 |
| System, 6 Cymbal-class slots + pool | ≈ 1,278, ≈ 26 % of 4,897 cycles per sample at one instruction per cycle |

S086 budgets its 88-cycle chain per voice beside these figures.

---

## 10. Hardware matrix (C19.3, pending; the user runs it)

| # | Check | Pass |
|---|---|---|
| H1 | Boot on the old card: "Samples invalid" / "reload samples"; on an erased sample flash: "No samples" / "load samples" | notices shown once, boot continues |
| H2 | Load:[Samples] with `samples/` and `wavetables/ak_hvox/`, plus a long-named folder at each level, a 3-deep table folder, a 4-deep folder, a mixed mono/stereo/24-bit table, one truncated WAV | counts correct; 4-deep and truncated skipped, not hung or aborted; names in the single-parameter view; a reboot keeps everything |
| H3 | `wav` 0–10 at low and high notes, plain and FM; `sq3`…`swa` before an install (fallback) and after | no clicks or level jumps; fallback audible as Rec/Saw/Tri |
| H4 | `pwm`: each family across `wav` 0..127 and the note range; `prc` at 0 against Rec | smooth width; no zipper at low notes |
| H4a | menu-edit `typ`/`src`/`run`, save the Kit, reboot, reload | type, width/start and run mode survive |
| H4b | a MIDI CC to `typ` and to `src` | sound and display change |
| H4c | SHIFT Morph view on `typ`/`src`/`run` | shows Normal; an edit changes Normal |
| H4d | step-automate `typ`, let the lock release | the stored type returns on the next trigger |
| H5 | `smp`/`slp`: start sweeps; loop seam; gain normalisation; `smp` silence after the end | as specified |
| H6 | `wtb`: position sweep by knob, LFO and Morph; 8- and 16-bit tables; fast LFO; switch pwm → wtb → pwm (F11) | smooth; sweeps faster than 1 frame/block glide |
| H7 | run modes on fast retriggers: `rst`, `fre`, `itp` on Drum fm/mix/snc/rng, Snare, Cymbal, HiHat; two voices in `itp` at once plus a third; **a fast roll in `itp` (A8)** | no clicks in fre/itp (the A8 step on retriggers < 3 ms apart is accepted); the third voice falls back to fre smoothly; Cymbal itp FM attack varies per hit (expected) |
| H7a | velocity and LFO on resonance in `fre`/`itp` | audible |
| H8 | Drum `mod` `fm mix snc rng`; `snc` with an `smp` master and start > 0, after the master ends, and **at `wav` 127 at a high note (one wrap per sample)** | sync stable; no resets after a one-shot master ends; mix law by ear |
| H9 | velocity and LFO on `wav` of a `pwm`/`smp`/`wtb` oscillator; then type → `wav` → back with the LFO still assigned (A9) | range reaches 127 (D11); the LFO centre returns |
| H10 | old kits load | same sound except `wav` 6+ (now stock waves, Q8) |
| H11 | worst-case Scene: `wtb`/`pwm swp` FM oscillators on every voice, two `itp` crossfades, waveform interpolation, `snc` at high master pitch with an `smp` master; DEV render-overrun counter and `audioCodec_underrunCount` | no underruns; `cpu` figure recorded |

**Order:** install (H2) first, then the oscillator rows (plan R2), with the
DEV render-overrun counter on (R9).

---

## 11. Risks (plan §8) and status

| # | Risk | Status |
|---|---|---|
| R1 | Hand re-application drops a fix or brings an S086 hunk | Mitigated: G4 leftover search 0 hits; assessment §22 |
| R2 | One change set, one hardware check | Open: run H2 first |
| R3 | `snc` worst case above the cap | Realised and fixed (A2) |
| R4 | Carrier-only `itp` never heard | Open: H7 |
| R5 | S086 room | ITCM 3,952 B free; flash 110,880 B free |
| R6 | Stock waves fall back before the first install | Accepted; EMPTY notice |
| R7 | Selector apply regresses on an unexercised path | Open: H4a–H4d |
| R8 | The modal install blocks with audio suspended | Bounded opens; time a full-card install |
| R9 | The spike's freeze recurs | Its causes (buffers, drive) are out of scope; watch the DEV counter in H11 |
| R10 | `fm`/`mix` S0 claim wrong | Closed: frozen engine lines (A7) |
| R11 | `itp` retrigger inside 3 ms | Accepted (A8) |
| R12 | 48-character key truncation | Defined: longer or non-ASCII names use the 8.3 alias |
| R13 | Documentation drift from spike copies | Specs written fresh (§15) |

---

## 12. Affected files and API changes

### 12.1 Files (uncommitted)

**New:**

| Area | Files |
|---|---|
| Sample flash | `Core/SampleRom/SampleInstall.h/.c`, `StockWaveGen.h/.c`, `StockWaveRecipes.c` |
| Oscillators | `Core/DSPAudio/OscillatorSample.h/.c`, `OscillatorPwm.h/.c`, `OscillatorWavetable.h/.c`, `OscRenderPool.h/.c`, `OscSection.h/.c`, `StockWaves.h/.c`, `OscSyncLane.h` |
| Menu | `Core/DSP/Instruments/InstrumentCellFormat.h/.c` |
| Host tools | `tools/sampleoptimize.py`, `tools/gen_stock_recipes.py`, `tools/plots/*` |
| `tools/dsp_test/` | `frozen85/`, `host_osc_support.c`, `host_sampleflash_stub.c`, `hostcompat/`, `prelude_cat.h`, `prelude_osc.h`, `looplen.py`, `osccost.py`, `breakproofs.py`, `stock_all.c`, `test_catalogue.c`, `test_natural_order.c`, `test_osc_run.c`, `test_osc_s0.c`, `test_stock_gen.c`, `s0_engines.c`, `s0_engine_shim.h`, `armcheck/osc_base_compat.h`, `armcheck/probe_wav.c`, `probe_pwm.c`, `probe_smp.c`, `probe_wtb.c`, `probe_section.c` |
| Card | `SD_CARD/wavetables/ak_hvox/` (optimised), `SD_CARD/wavetables_old/`, `SD_CARD/samples_old/` |

**Modified:**
- `Core/SampleRom/SampleMemory.h/.c` (rewritten), `sampleFlash.c`;
  `Core/Hardware/SD/filesystem.h/.c`, `storageTypes.h/.c`.
- `Core/DSPAudio/Oscillator.h/.c`, `ResonantFilter.h/.c`, `mixer.c`,
  `modulationNode.c`.
- The four engines `*Voice.c/.h` / `Snare.c/.h` / `HiHat.c/.h`; the four
  `*Parameters.c/.h`; `InstrumentManager.h/.c`.
- `presetManager.c`; `Core/Menu/MenuText.h`, `menu.c`, `menu.h`.
- `main.c`, `config.h`, `Makefile`.
- `tools/dsp_test/Makefile`, `extract.py`, `snapshot.sh`,
  `check_special_tags.py`, `DSP_TEST.md`.

### 12.2 API added, changed, removed

| Area | Added | Changed | Removed |
|---|---|---|---|
| Oscillator | `OSC_TYPE_*`, `SQ3`…`SWA`, `OSC_STOCK_WAVE_COUNT`, `OSC_SLEW_SAMPLES`, `osc_armSlew()`, `osc_freqToTableIndex()`, `osc_isTableWave()`, `calcStockWaveBlock()`/`FmBlock()`, `osc_renderSyncBlock()` | `osc_setFreq()` (per type, slew); dispatchers (type switch) | `OSC_SAMPLE_START`, `calcUserSampleOscBlock()`/`FmBlock()`, `osc_updateSampleCache()`, `sampleCacheLooped` |
| Type renderers | `oscSample_bind/restart/isFinished/phaseIncForFreq/render/renderFm/renderSync`, `oscPwm_prepare/render/renderFm/renderSync`, `oscWavetable_bind/render/renderFm/renderSync`, `stockWave_level()` | — | — |
| Sync | `OscSyncLane`, `oscSync_phase()`, `OSC_SYNC_INLINE`, `OSC_SHARED_LOOP` | — | — |
| Pool | `oscRenderPool_beginBlock/claim/scratch()`, `OSC_RENDER_POOL_UNITS` | — | `OSC_WAVE_INTERP_MAX_ACTIVE`, `modNode_waveInterpActiveCount` |
| Section | the `oscSection_*` API (`init`, `beginTrigger`, `claimOngoing`, `claimPending`, `itpCarrier`, `renderDrum`, `renderFm2`, `blendItp`, `finishBlock`, `noteResoTarget`, `filterRecalc`), `OSC_RUN_*`, `OSC_MOD_*`, `OSC_ITP_*`, `OSC_TRIGGER_RESET_*` | — | — |
| Filter | `SVF_recalcFreqFrom()` | — | — |
| InstrumentManager | writers 20–23; `instrumentManager_oscForDescriptor`, `_oscSection`, `_oscWaveMaxForType`, `_oscSourceMaxForType`, `_isSelectorDescriptor`, `_claimOscSectionOngoing/Pending`; static `_descriptorModDomainForSlot`, `_refreshWaveDomains` | the special writer takes `retained`; `INSTRUMENT_RUNTIME_SLOT_BYTES` fixed 1,176 | — |
| Preset | static `preset_applySelectorRows()` | supplemental store accepts selector rows; rebind skips them | — |
| Menu | `instrumentCellFormat_describe()`, `menu_automationValueMaxSlot()`, the install summary helpers, `oscTypeNames`, `runModeNames`, `pwmFamilyNames`, `mixFmNames` | `waveformNames` 11 | `menu_numSamples`, `menu_setNumSamples()` |
| Sample flash | the v2 records, the state API, `sampleMemory_getNumWavetables/getWavetableInfo/getWavetableName/stockValid/catalogueState/getGeneration`, `sampleMemory_gainFromCode()`, the whole `sampleInstall_*` API, `stockWaveGen_programAll()` | `SampleInfo` (size, offset, gain code) | v1 install API, `sampleMemory_isLooped()`, the RAM caches |
| Filesystem | `filesystem_installSamplesBlocking(report)`, `filesystem_setInstallProgress()`, `fs_install_report_t`, `FS_INSTALL_STAGE_*`, `storage_naturalCompare()`, `storage_stripOrderPrefix()` | — | `filesystem_installLoopsBlocking()`, `sample_manifest` |

---

## 13. ID index (code comments cite these)

### 13.1 Schedule change IDs `S085 C<wp>.<n>`

| ID | What |
|---|---|
| C0.1 | `snapshot.sh` destination argument, S085 file list (`frozen85/`) |
| C0.2 | baseline probe run |
| C1.1 | `SampleMemory.h`: map, records, read API, gain table |
| C1.2 | `SampleMemory.c`: catalogue reader, states, generation |
| C1.3 | `sampleFlash.c`: comment only |
| C2.1 | `SampleInstall.h`: installer API, formats |
| C2.2 | `SampleInstall.c`: conversion, programming, entries, commit, abort |
| C3.1, C3.2 | `storageTypes.h/.c`: natural order, prefix strip |
| C3.3 | `filesystem.h`: installer API, stages, report |
| C3.4 | `filesystem.c`: includes, installer state |
| C3.5 | `filesystem.c`: bounded blocking opens (give-up) |
| C3.6 | `filesystem.c`: v1 sample helpers removed |
| C3.7 | `filesystem_parseWav()` |
| C3.8 | the install walk (collect, sample tree, wavetable passes A/B, copy) |
| C3.9 | `main.c` boot notice; `menu_setNumSamples` removed |
| C3.10 | `menu.c` Load:[Samples] modal, progress, summary deadline |
| C3.11 | `menu_numSamples` removal (D8) |
| C4.1 | `tools/plots/stock_wave_harmonics.csv` |
| C4.2 | `tools/gen_stock_recipes.py` |
| C4.3 | `StockWaveRecipes.c` (generated) |
| C4.4 | `StockWaveGen.h/.c` |
| C4.5 | `StockWaves.h/.c` (level resolution, fallback) |
| C5.1 | `Oscillator.h`: wave ids, types, slew length |
| C5.2 | `OscInfo` fields |
| C5.3 | `Oscillator.h` declarations |
| C5.4 | `Oscillator.c` includes; sample cache removed |
| C5.5 | waveform interpolation for `wav` only |
| C5.6 | per-sample slew in every block renderer |
| C5.7 | block dispatch by type |
| C5.8 | scalar dispatchers |
| C5.9 | stock-wave renderers |
| C5.10 | the sample code leaves `Oscillator.c` |
| C5.11 | frequency helpers write the target |
| C5.12 | `osc_setFreq()` per type, cache key, slew; helpers |
| C5.13 | `config.h` render pool replaces the interpolation limit |
| C6.1, C6.2 | `OscillatorSample.h/.c` |
| C7.1, C7.2 | `OscillatorPwm.h/.c` |
| C8.1, C8.2 | `OscillatorWavetable.h/.c` (frame pair) |
| C9.1, C9.2 | `OscRenderPool.h/.c` |
| C9.3 | `modulationNode.c` interpolation claims |
| C9.4 | `mixer.c` block-start pool reset and claims |
| C10.1, C10.2 | `OscSection.h/.c` |
| C10.3 | `snc` placement note (D13) |
| C10.4, C10.5 | `SVF_recalcFreqFrom()` |
| C10.6 | the `snc` cost decision (step 4 = the per-type loops, done as assessment A2) |
| C11.1 | engine headers: `OscSectionState section` |
| C11.2–C11.5 | Drum, Snare, Cymbal, HiHat engines |
| C12.1 | `ROW_SELECT_SPECIAL` |
| C12.2 | `wav` rows tagged `IM_SPECIAL_OSC_WAVE` |
| C12.3–C12.6 | index enums, rows, counts, menu cells |
| C13.1 | `InstrumentManager.h` writer IDs, accessors |
| C13.2 | fixed runtime-slot reserve |
| C13.3 | includes, forward declarations |
| C13.4 | special writers |
| C13.5 | oscillator accessors, maxima |
| C13.6 | per-slot `wav` domain (D11) |
| C13.7 | `itp` claims |
| C13.8 | filter recalc through the section |
| C13.9 | diagnostic classifier |
| C13.10 | unchanged, verified |
| C14.1–C14.6 | Preset store, apply, rebind skip, post-reset apply, immediate Instrument apply, verified paths |
| C15.1, C15.2 | `InstrumentCellFormat.h/.c` |
| C15.3 | `MenuText.h` name tables |
| C15.4 | `menu.c` formatter helpers |
| C15.5 | formatted-cell sites |
| C15.6 | `DTYPE_MIX_FM` four values |
| C15.7 | `wav` list without samples |
| C15.8 | count asserts |
| C15.9 | slot-aware automation |
| C15.10 | SHIFT Morph selector edit (D12) |
| C15.11 | nothing else |
| C16.1 | `Makefile` sources |
| C17.1–C17.11 | host targets, `extract.py` multi-dimension objects, catalogue/natural-order/stock tests, ARM probes, `osc_s0`, DSP tests, special tags, tools and plots, the proof rule, the card move, `DSP_TEST.md` |
| C19.1–C19.3 | gate checklist, cost record, hardware matrix |

### 13.2 Other IDs

| ID | Meaning |
|---|---|
| D1–D13 | §3.2 |
| Q1–Q15 | §3.1 |
| PA1–PA13 | plan ambiguities, §3.1 |
| C1–C11 (plan §2.2) | corrections, §2.2 (not the C-IDs above) |
| assessment A1–A10 | §7, §8 |
| B1–B26 | §9.2 |
| H1–H11 | §10 |
| R1–R13 | §11 |
| S084 P1 F1 | selector rows never stored/applied → C14 |
| S084 P1 F2 | wave byte clamped against the old type; request bytes → C13.4 |
| S084 P1 F3 | `pwm_read1024()` did not interpolate → 22-bit fraction |
| S084 P1 F4 | velocity/LFO on resonance inert in `fre`/`itp` → `noteResoTarget` / `q` follow |
| S084 P1 F5 | `snc` sample master with start > 0 drifted → cycle-relative shadow |
| S084 P1 F6 | `itp` retrigger inside a running crossfade steps → accepted (A8) |
| S084 P1 F7 | LFO one-shot (out of scope) |
| S084 P1 F8 | `snc` mix plan/schedule difference → Q10 |
| S084 P1 F9 | `prc` polarity note → D4 |
| S084 P2 F1 | long-named subfolders never installed → `blockOpenDirLfn` |
| S084 P2 F2 | four-deep nesting hung the install → depth 3 + open give-up |
| S084 P2 F4 | frames in another source format decoded wrongly → `setFrameFormat()` |
| S084 P2 F6 | `wtb` 64/70 → D1 |
| S084 P2 F7 | one unreadable file aborted the install → per-item skip |
| S084 P2 F8 | finish did not check completeness → −4 |
| S084 P2 F10 | `pq3`/`pw3` kept the fallback split across an install → cache key bit 7 |
| S084 P2 F11 | union hygiene on pwm → wtb → clear `base`/`primed` |
| S084 P2 F3, F5, F9 | drive/transient (out of scope) |
| S084 P2 N1 | `filesystem_le16()` unused, removed |
| S084 P2 N4 | frames beyond the 128th valid one are counted as skipped |

---

## 14. Accepted behaviours and known limits (not bugs)

**Oscillators:**
- **`itp` retrigger inside 3 ms** steps by (1 − t) × (old − new) (A8 /
  P1 F6).
- **`itp` FM attack:** in `itp` the modulators are not reset, so Cymbal's FM
  attack differs from hit to hit (identical in `rst`). The crossfade has a
  brief comb character.
- **`wtb` position** moves at most one frame per block (≈ 1,378 frames/s);
  faster sweeps glide (D1). Under `snc` the frame pair is still per block.
- **Synced one-shot slave:** restarts from its start on every master wrap.
- **The `snc` select** compiles to a branch (the no-wrap path is shorter);
  the budget uses the wrap path.
- **Slew:** 160 samples in all; the last 3 % arrives over the fifth block
  (A1).
- **The plain pwm/smp/wtb loops** pay the 2-instruction lane test (the
  ITCM trade, §4.9).
- **Cost figures exclude sample-flash wait states:** the stock, `smp` and
  `wtb` reads come from sample flash through the 16 KB D-cache. Samples and
  large tables miss it, and a miss pays the flash wait states. H11 is the
  real measurement.

**Content and storage:**
- **No migration:** old kits' `wav` 6–10 play the stock waves; 11+ clamp to
  `swa`; v1 installs read STALE until reloaded; waveform interpolation
  across samples is gone.
- **Stock waves** fall back to Rec/Saw/Tri until the first install (EMPTY
  notice).
- **Sample formats:** samples must be mono 16-bit 44.1 kHz at install
  (`sampleoptimize.py` converts); frames may be mixed formats.
- **Long names:** names over 48 characters, or non-ASCII, sort and display
  by the 8.3 alias.

---

## 15. Specification and memory updates (S085 close)

| Document | Update |
|---|---|
| `dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` | the S085 oscillator chapter rewritten as a full reference: model, dispatch, slew, every renderer, stock waves, pwm formulas, sample and wavetable playback, run modes, `itp`, the section, constant-cost sync, the pool, costs, tests, how to add a type |
| `FILESYSTEM_SPEC.md` | sample flash v2: byte layout, states, installer API and error codes, card walk, natural order, names, boot notice, UI |
| `STORAGE_SRAM_MANIFEST.md` | measured engine sizes, ITCM blocks, stack, the ledger |
| `dsp_instruments_effects/CPU_USE_DSP_AUDIT.md` | the cost record and derived worst cases replace the `looplen` paragraph and the segment-`snc` text |
| `dsp_instruments_effects/EFFECTS_MIXER_DSP_REFERENCE.md` | block-start order (kept), with the pool's cost |
| `MODULE_INTERCHANGE_SPEC.md` | `OscSyncLane.h`, the sync renders, the shared loops |
| `OSC_INTERP_AUDIT.md` | interpolation under `snc` |
| `BANK_PRESET_ARCHITECTURE.md`, `MIDI_SPEC.md`, `DEV_MODES.md` | reviewed; small additions |
| `tools/dsp_test/DSP_TEST.md` | new targets, `osccost.py`, `breakproofs.py`, probes, `osc_s0` engine lines |
| `knowledge_files/drafts/GENERATOR_PAGE_SPEC_DRAFT.md` | paths and select-mode facts verified; the S085 type-selector pattern (selector rows, request bytes, formatter, per-type domains) proposed for the generator type row |
| `MEMORY.md` | current state, build, sample flash, oscillator rules, spike path |
| `knowledge_files/volatile/S078_WORKING_NOTES.md` | rewritten as the S085 → S086 notes |
| `knowledge_files/volatile/memory/VOLATILE_MEMORY.md` | S085 carryover and working preferences |

---

## 16. Carry-forward

1. **Hardware:** S084 V1–V7, then S085 H1–H11 (§10). Record the H11 `cpu`
   figure and the underrun count.
2. **S086** (effects chain inside ≈ 88 cycles per voice):
   - Budget the §9.4 oscillator worst cases.
   - Spike items still waiting there: transient types and
     sample/burst transients, filter drive types and `DriveShaper`, Mix FX
     and InsertEffects, voice buffers and the D+S split, the elastic Effect
     contract, CrumpBit compaction, FxBuffer leases.
   - First explain the spike freeze (buffer-using Mix/transient types, or
     drive types other than `sft`) with the DEV fault capture and the
     render-overrun counter.
   - ITCM free: 3,952 B.
3. **Unassigned spike work** (decide): LFO one-shot waves, the Scene
   activation fire and the `rnd` 0..1 fix; the deferred instrument swap
   fixes; DEV swap trace `v`.
4. **Disposable** (the user's plan): root `S085_OSC_MOD_CHERRYPICK.md`,
   `S085_P1_OSC_MOD_PLAN.md`, `S085_P1_IMPLEMENTATION.md`. This log and
   the specifications carry them. Keep `Helicase-dev-ph7-spike/` until S086
   is done.
5. **Possible later work** (not requested):
   - an exact `itp` retrigger (A8, +124 B per voice);
   - per-family pwm loops (removes the per-sample family switch, at an ITCM
     cost);
   - if ITCM runs short, moving the colder sync copies out of ITCM onto
     the I-cache (both L1 caches are enabled, since Session 13) is the
     fallback. Measure the cost first.

---

## 17. S085 P2: amp envelope on retrigger (2026-10-27; hardware pending)

**Schedule and working notes:** root `S085_P2_OSC_RESET_FOLLOWUP.md`
(schedule §0–§10, implementation log §11). Specification:
`INSTRUMENTS_DSP_REFERENCE.md` §12.6.1.

### 17.1 Problem

`fre` and `itp` keep the oscillators and the filter running through a
retrigger, but every engine restarted its amp envelope with
`slopeEg2_trigger()`. That call keeps the raw value and switches from the
decay curve `f(v, slope)` to the attack curve `f(v, invSlope)`, so the gain
stepped by up to full scale (v = 0.5, slope 96: 0.76 → 0.24).
- **Drum:** ramped the step over one block, except at attack 0 (no ramp).
  At attack 20–30 this was the soft click heard on hardware.
- **Snare, Cymbal, HiHat:** one constant gain per block, so the step landed
  on one sample.
- **Velocity:** a new velocity (`volumeMod` on) stepped the level too.

### 17.2 Decisions (user, 2026-10-27)

| # | Decision |
|---|---|
| Rule | `rst` exactly as before (bit-identical); every new path is armed only by a `fre`/`itp` trigger. One-block ramp (32 samples). Constant CPU. |
| U1 | "Something left" = applied gain ≥ 1/32768, not the raw-value test of `instrumentManager_ampEnvelopeQuiet()` |
| U2 | A `fre`/`itp` attack always starts at the current gain (≈ 0 when decayed, so a fresh attack) |
| U3 | Drum also ramps the velocity gain (+3 per sample) |
| U4 | RAM: +8 B per engine struct inside the fixed 1,176 B slot, approved |
| U5 | Snare/Cymbal/HiHat ramp only the retrigger block |

### 17.3 Changes

- **`SlopeEg2`:**
  - `slopeEg2_retriggerFrom(eg, g)` sets `v = g/(1 + invSlope·(1 − g))`,
    clamped to [0, 1], state `EG_A`;
  - repeat mode falls back to `slopeEg2_trigger()`;
  - `SLOPEEG2_AUDIBLE_GAIN` (1/32768) and `slopeEg2_isAudibleGain()`.
- **`voicePostChain.h`:** the three chains take a "last" endpoint and ramp
  with `bufferTool_interpolatedGain()`. Drum ramps velocity; Snare and
  Cymbal/HiHat ramp the amp gain. With equal endpoints each is exactly the
  S073 loop.
- **Engines:**
  - **trigger:** `rst` calls `slopeEg2_trigger()`; `fre`/`itp` call
    `slopeEg2_retriggerFrom()` with the last applied gain (Drum
    `ampFilterInput`, the others `egValueOscVol`), and arm `ampRampArm`
    when that gain is audible;
  - **render:** ramps from `lastVeloGain`/`lastAmpGain` when armed, then
    records the gain and clears the arm;
  - **Drum async:** an armed block ramps even at attack 0;
  - **Drum trigger:** an `#error` guards the `AMP_EG_SYNC == 0`
    assumption.
- **Still restarts by design:** the transient sample, the pitch and snap
  envelopes (frequency, not amplitude; glided by the slew or crossfade).

### 17.4 Cost, RAM, build

- **Post-chain worst case per sample:** Drum 32 → **35**, Snare 30 →
  **35**, Cymbal/HiHat 25 → **30**. That is at most +30 per sample system
  wide (≈ 0.6 %), in every mode. Each engine has one post-chain loop, with
  no versioning on the arm.
- **Per `fre`/`itp` trigger:** one VDIV.
- **Engine sizes:**

  | Struct | Before | After |
  |---|---:|---:|
  | Drum | 832 | 836 |
  | Snare | 664 | 672 |
  | Cymbal | 772 | 776 |
  | HiHat | 780 | 788 |

  The slot `_Static_assert`s pass.
- **Build (DEV):** `text=643,192` (+832), `data=424`, `bss=414,100`
  (unchanged). Flash 643,616 / 753,664 (110,048 B free). ITCM 12,432,
  DTCM 4,468, FXBUF margin 3,712 (all unchanged).

### 17.5 Tests

- **`ampretrig`** (new): 0 failures. It checks the gain match (every slope),
  repeat mode, the audible threshold, ramp wiring and no step. The no-step
  sequence measures a largest step of 0.05677, equal to one normal
  attack-block increment.
- **Deviations from the schedule's C7.2 wording:**
  - the gain-match tolerance adds a float-resolution term at the steepest
    slopes (slope 127 misses by up to 9e-5);
  - check 5 adds a no-dip condition, without which B28 is not caught.
- **`postchain`:** 0 differing samples (the `rst` guarantee).
  **`armcheck-postchain`:** now a cost report (35/35/30).
- **`breakproofs`:** 32 / 32 fail their target, including B27–B32.
- **Rest of the suite:** unchanged and passing (`osc_run`, `osc_s0`,
  `osc_pwm`, `osc_sample`, `wtb`, `special_tags` 177/0, `natural_order`,
  `catalogue`, `stock_gen`, `filter`, `pack`, `octave`, `mixer`,
  `selftest`); `armcheck-osc` rc 0.

### 17.6 Hardware (pending, the user runs it)

Rows P2-H1 … P2-H8 in `S085_P2_OSC_RESET_FOLLOWUP.md` §9:
- Drum, Snare, Cymbal and HiHat retriggered halfway through the decay
  across attack and slope settings (no click);
- a retrigger after a full decay (a normal attack);
- alternating velocities with `volumeMod` on;
- repeat mode (soft onset);
- an `rst` A/B against the pre-P2 build (identical);
- the worst-case Scene (`cpu` within about +0.6 %);
- what still restarts by design.

### 17.7 F-A: Drum FM depth ramp (2026-10-27)

- **Report (user, hardware):** low, long Drum kick, `fre`, sine carrier at
  26 Hz, high FM amount, attack 1: sporadic clicks. They go away with FM 0
  or in `itp`.
- **Cause:** `fmMod = fmModAmount × pitch envelope`. `DecayEg_trigger()`
  restarts the envelope at 1.0, so the FM depth jumps at the retrigger; the
  FM is phase modulation, so the read phase jumps by `mod × Δ fmMod`. The
  size depends on the modulator's value at the trigger, which is why it is
  sporadic. `itp` hides it, because its old carrier copy keeps the old
  depth.
- **Fix (F-A):**
  - `oscSection_renderDrumRamped()` / `oscSection_rampModDepth()` scale the
    fm modulator block from `lastFmMod / fmMod` (clamped 0..1) to 1 on the
    armed block (`ampRampArm`); every other block is flat.
  - The ramp is forced flat while an `itp` crossfade runs.
  - `oscSection_renderDrum()` is now the flat wrapper.
- **Cost:** 15 per sample in Drum fm mode (estimate was ≈ 4). Drum fm 135.
- **RAM:** `lastFmMod` +4 B, paid by removing the unused Drum
  `egValueOscVol`; Drum 840 B (+8 for all of P2, within U4).
- **Tests:** `osc_run` fm depth ramp (worst boundary step 294 against
  65,311 unramped); B33–B35; `breakproofs` 35/35.
- **Hardware rows:** P2-H9, P2-H10 (`S085_P2_OSC_RESET_FOLLOWUP.md` §12.5).
- **Detail:** `S085_P2_OSC_RESET_FOLLOWUP.md` §13.

### 17.8 Menu names, the `wav`/`mod` swap, no `pwm` on the Cymbal/HiHat modulators (2026-10-27)

- **Names** (`MenuText.h`):
  - stock waves `sq3 sw3 sqa tra swa` → `Re3 Sw3 ReA TrA SwA`;
  - pwm families `prc psw ptr pq3 pw3 swp` → `Rec Saw Tri Re3 Sw3 Swp`;
  - the code constants are unchanged.
- **Row swap:**
  - `wav` (`waveform`, key `oscN_wave`) is the selection: wave, pwm family,
    sample or table.
  - `src` became `mod` (`source`, key `oscN_source`). It is a plain 0..127:
    pwm width, sample start or table position.
  - `mod` is now an ordinary Morphable / LFO / velocity row. The selector
    rows are `typ` and `run`.
  - The binds re-expand `waveform` from `waveRequest` on a catalogue
    change.
  - `osc_setFreq()` keys its cache on `source` too (new
    `freqCacheSource`, in padding).
  - The full view shows the sample or table name on the left for `wav`,
    and only the number on the left for `mod`.
  - Old non-`wav` kits load with the two values exchanged (no migration).
- **Cymbal/HiHat modulators:** `pwm` is excluded by
  `instrumentManager_oscTypeAllowed()`:
  - the writer stores `wav`;
  - the menu steps over it;
  - the display shows `wav`.

  The other types stay. The crash's root cause was not found.
- **Wavetable looping report:** no non-looping path found in the code, and
  the card data is seamless. Open until the user describes the symptom.
- **Build:** ITCM 12,520 / 16,384 B (+88). Struct sizes are unchanged.
- **Gates:** all pass. `breakproofs` is 35/35, with B14 moved to the new
  bind.
- **Hardware rows:** P2-H11 to P2-H14.
- **Detail:** `S085_P2_OSC_RESET_FOLLOWUP.md` §14.
