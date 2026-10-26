# S085 — Oscillator and Modulation-Mode Cherry-Pick

## 0. Read this first

### 0.1 What this session does

Re-implement, on the rolled-back tree (`59406a4` plus the new S084 P0), every
S084 change to the **oscillator ("osc") section** and the Drum **`mod` row**
modes `snc` and `rng`. Nothing from the transient, filter drive, Mix FX,
voice-buffer, elastic-Effect, LFO or instrument-swap work comes across.

**Source of truth:**
- **The code:** this repository at commit `26fb7c3` ("the spike"), run side
  by side with the rolled-back tree.
- **The documents:** `S084_P1_IMPLEMENTATION.md` (P1-x.y IDs; review fixes
  §18) and `S084_P2_IMPLEMENTATION.md` (VX2-x.y IDs; review fixes §17).
  - The master plan is `S084_P1_VOICE_EXPANSIONS.md` (§4.1, §4.2, §5.1,
    §5.2).
  - Where a document and the spike code differ, **the spike code wins**: it
    contains the review fixes.

**How to read the tables:**
- **"Take whole":** the spike file can be copied as it is (new files with no
  out-of-scope content).
- **"Take hunks":** the file mixes scopes. Copy only the named hunks and
  leave the listed out-of-scope hunks behind.

### 0.2 Scope

| In scope (tested and working on hardware) | Out of scope (stays out of S085) |
|---|---|
| Oscillator types `wav pwm smp slp wtb` (`typ` row) | transient types `trt`, sample/burst transients, `buf` (S086) |
| Source row `src`: pwm family, sample index, wavetable index | filter drive types `dty`, `DriveShaper`, `SVF_calcBlockZDFShaped` (S086) |
| PWM families `prc psw ptr pq3 pw3 swp` | Mix FX `fxt`/`amt`, `InsertEffects/`, `VoiceBuffer`, `D+S` split (S086) |
| Sample playback `smp` (one-shot) and `slp` (loop), start position | elastic Effect contract, FxBuffer positions/leases, CrumpBit compaction (S086 or later) |
| Wavetable type `wtb`: `wav` row = continuous frame position | LFO one-shots / `rnd` fix (P1 WP1, decision pending) |
| Stock waves `sq3 sw3 sqa tra swa` (wave list 6 → 11) | deferred instrument swap, mixer declick, DEV trace `v` (P1 WP0/WP2, decision pending) |
| Run modes `rst fre itp` (`run` row), per-sample slew, filter slew | |
| Render pool (`itp` and waveform interpolation share 4 units) | |
| Drum `mod` row: `fm mix snc rng` | |
| Sample flash catalogue v2, installer (`samples/`, `wavetables/`, natural order, depth 3), boot notice, gain code | |
| `tools/sampleoptimize.py`, `tools/gen_stock_recipes.py`, `tools/plots/` (pwm, stock waves, wavetable formats) | |
| Menu formatting of all of the above (`InstrumentCellFormat`, `MenuText.h`, `menu.c`) | |

### 0.3 Stored values and compatibility

- **New rows are appended** at the end of each descriptor table. Existing
  indices, automation IDs, LFO tokens, MIDI Learn and NRPN IDs are unchanged.
- **A missing key loads 0**, which is today's behaviour: `typ` 0 = `wav`,
  `src` 0, `run` 0 = `rst`, and `mod` 0/1 = `fm`/`mix` as before.
- **The `wav` row:** values 0–5 (`Sin Tri Saw Rec Noi Cym`) are unchanged;
  6–10 are the new stock waves. Old kits whose `wav` pointed at a sample
  (the old sample tail of the list) clamp to `Cym` (P1-11.4, H7).
- **S086 will append its rows** (`chain`, the transient, drive and Mix rows)
  **after** S085's rows. Do not reserve indices for them.

| Instrument | Before | S085 rows appended | After S085 |
|---|---:|---|---:|
| Drum | 39 | `osc1_type osc1_source osc2_type osc2_source osc_run_mode` | 44 |
| Snare | 38 | `osc1_type osc1_source osc_run_mode` | 41 |
| Cymbal | 39 | `osc1_type osc1_source osc2_type osc2_source osc3_type osc3_source osc_run_mode` | 46 |
| HiHat | 39 | the same seven | 46 |

`special_tags` expects **177 rows, 0 mismatches** after S085 (the P1
figure).

---

## 1. Order of work and gates

1. **Data model and renderers** (§2–§6): `Oscillator.h/.c`,
   `OscillatorSample`, `OscillatorPwm`, `OscRenderPool`, `OscSection`,
   `SVF_recalcFreqFrom`, `modulationNode.c`, and the `mixer.c` pool reset.
   - Gate A: build clean; host targets `osc_pwm`, `osc_sample` and
     `osc_run` pass.
2. **Sample flash v2 and installer** (§7):
   - `SampleMemory`, `SampleInstall` and `sampleFlash.c`;
   - the `filesystem.c` installer, `storageTypes`, the `menu.c` modal and
     the `main.c` boot notice.
   - Gate B: `natural_order` and `catalogue` pass; install on hardware from
     `samples/` and `wavetables/`.
3. **Stock waves and `wtb`** (§8–§9): `StockWaves`, `StockWaveGen`,
   `StockWaveRecipes` and `OscillatorWavetable`, plus their dispatch.
   - Gate C: `stock_gen` and `wtb` pass, and `armcheck-vx2` reports the
     stock reader and `wtb` loops.
4. **Engines, rows, writers, menu** (§10–§13).
   - Gate D: `special_tags` 177/0; the full host suite; `make img`.
5. **Specifications** (§14).
6. **Hardware check** (§15): every type, family, run mode, `snc`/`rng`, the
   installer, and old kits.

**Cost record:** before Gate D, record the oscillator loop costs (§16)
against the pre-S084 oscillator loops. Oscillator cost is outside the 88-cycle
effects budget, but it is part of the voice total that S086 budgets against.

---

## 2. Oscillator data model and dispatch (P1 WP3, P2 WP4/WP5 parts)

**Spike files:** `Core/DSPAudio/Oscillator.h`, `Core/DSPAudio/Oscillator.c`
(**take whole**). Every S084 change to these two files is in scope.

| Change | What | Doc |
|---|---|---|
| Type constants | `OSC_TYPE_WAV/PWM/SMP/SLP/WTB`, `OSC_TYPE_COUNT` 5 | P1-3.1, VX2-5.1 |
| `OscInfo` fields | `oscType`, `source`, slew fields, `waveRequest`/`sourceRequest` (in padding, F2), `sampleGainCode` (padding), the `pwm`/`wtb` anonymous union | P1-3.2, P1 §18 F2, VX2-1.5, VX2-5.2 |
| Declarations | `osc_armSlew()`, `osc_freqToTableIndex()`, `osc_isTableWave()` | P1-3.3, VX2-4.12 |
| Sample code removed from `Oscillator.c` | it moves to `OscillatorSample.c` | P1-3.4 |
| Waveform-interpolation gating through the pool | | P1-3.5, P1-6.4 |
| Block dispatch by type | `wav` → table / stock / sine / noise; `pwm` → `oscPwm_render`; `smp`/`slp` → `oscSample_render`; `wtb` → `oscWavetable_render` (plain and FM variants) | P1-3.6, VX2-4.7, VX2-5.5 |
| Per-sample slew in every renderer | constant cost: the slew step runs whether or not a slew is active | P1-3.7 |
| `osc_setFreq()` per type | cache key per type; for pwm, bit 7 carries `sampleMemory_stockValid()` (P2 F10) | P1-3.8, VX2-4.8, VX2-5.6, P2 §17 F10 |
| Stock wave ids | `SQ3 SW3 SQA TRA SWA` = 6–10, `OSC_STOCK_WAVE_COUNT` 11, `STOCK_WAVE_FIRST` 6 | VX2-4.5 |
| Stock wave readers | `calcStockWaveBlock()` / `calcStockWaveFmBlock()` | VX2-4.6 |

**ITCM:** the slew-enabled `wav` loops and the new renderers are placed in
ITCM (`INITCM_*`). The P1 deviation was +6.7 KB; after P2 the total is
12,720 B of 16,384 B. Check `link_budget.py` at each gate.

---

## 3. Sample playback `smp` / `slp` (P1 WP4, P2 VX2-1.4)

**Spike files:** `Core/DSPAudio/OscillatorSample.h/.c` (**take whole**).

- **Phase:** Q20.12 sample phase (`OSC_SAMPLE_PHASE_SHIFT` 12). `smp`
  plays once; `slp` loops `[start, end)`. The start position comes from the
  `wav` row under these types.
- **Bind:** `oscSample_bind()` runs every block (a generation compare). It
  reads catalogue v2 and the gain code (VX2-1.4), and indexes with
  `min(sourceRequest, count − 1)` (P1 F2), so a short reinstall does not
  move the selection permanently.
- **Gain:** `sampleMemory_gainFromCode()` is applied once per block (D10).

---

## 4. PWM families (P1 WP5, P2 VX2-4.9)

**Spike files:** `Core/DSPAudio/OscillatorPwm.h/.c` (**take whole**).

- **Families:** `prc psw ptr pq3 pw3 swp` (`OSC_PWM_PRC … SWP`). The `wav`
  row is the width/position control under `pwm`.
- **303 families:** `pq3`/`pw3` warp the generated `sq3`/`sw3` stock tables
  at their natural split (VX2-4.9). The split is stored in
  `OscPwmState.mix` and re-derived when stock validity changes (P2 F10, via
  the `osc_setFreq` key).
- **`pwm_read1024()`:** interpolates with a 22-bit fraction ×
  2.384185791015625e-07f (P1 §18 F3). The warp/split/shift positions use
  1/2^32 (P1 deviation 3).
- **Reference model:** `tools/plots/plot_pwm.py` (§12).

---

## 5. Render pool (P1 WP6)

**Spike files:** `Core/DSPAudio/OscRenderPool.h/.c` (**take whole**).

| Hunk | Where | Doc |
|---|---|---|
| `OSC_RENDER_POOL_UNITS 4u` replaces `OSC_WAVE_INTERP_MAX_ACTIVE 2u` | `config.h` (the only S084 change to `config.h`) | P1-6.1 |
| Waveform interpolation claims from the pool; `modNode_waveInterpActiveCount` removed | `Core/DSPAudio/modulationNode.c` (**take whole**; the global maximum waveform is P1-3.9) | P1-6.4, P1-3.9 |
| Block-start pool reset and `itp` claims | `Core/DSPAudio/mixer.c` (**take hunks**, see the shared-file guide, §11.1) | P1-6.5 |

**RAM:** `oscRenderPool_scratchSection[32]` and `_scratchMod[32]` (int16, 128
B) plus `unitsUsed` (1 B), in SRAM1. This needs re-approval in S085.

---

## 6. Oscillator section: run modes and mod modes (P1 WP7, WP8)

**Spike files:** `Core/DSPAudio/OscSection.h/.c` (**take whole**).

- **Run modes** `OSC_RUN_RST/FRE/ITP`:
  - `rst`: today's behaviour (phase reset, filter cleared).
  - `fre`: no reset; the oscillators slew over `OSC_SLEW_SAMPLES` (132) and
    the filter keeps ringing.
  - `itp`: snapshot, then reset; the old section crossfades over
    `OSC_ITP_SAMPLES` (132) using the pool.
- **Mod modes** `OSC_MOD_FM/MIX/SNC/RNG` (0/1 keep their stored meaning):
  - `oscSection_renderDrum()` implements `snc` (hard sync with a shadow
    phase; for a sample master the shadow starts at
    `mod->phase − (sampleStart << 12)`, P1 F5);
  - `rng` (ring);
  - `oscSection_renderFm2()` covers the FM-only engines.
- **Filter slew:**
  - `oscSection_filterRecalc()` follows modulated `q` in `fre`/`itp` (P1
    F4; one compare per block, `rst` untouched);
  - `oscSection_noteResoTarget()`.
- **`SVF_recalcFreqFrom()`** (`ResonantFilter.c/.h`, P1-7.3): **take only
  this hunk**. `SVF_calcBlockZDFShaped()` and everything `DriveShaper`
  stays out.
- **Accepted behaviour:** P1 F6 (a step on retriggers inside about 3 ms in
  `itp`) is documented at `oscSection_beginTrigger()`.

**Engine structs** (P1-8.1): add `OscSectionState section` to `DrumVoice`,
`SnareVoice`, `CymbalVoice` and `HiHatVoice`.
- **Out of the structs:** the spike's `driveType`, `shaper` (P2) and
  `insertType`, `insertAmount`, `stageSlot` (P3) fields stay out (§10).
- **Runtime-slot reserve:** fixed at **1,176 B** (P1 deviation 1), with the
  `_Static_assert`s in `InstrumentManager.c`. Measured after P1: Drum
  1,072, Snare 904, Cymbal 1,008, HiHat 1,016 B.

---

## 7. Sample flash catalogue v2 and the installer (P2 WP1–WP3)

### 7.1 Files

| Spike file | Take | Doc |
|---|---|---|
| `Core/SampleRom/SampleMemory.h/.c` | whole (v2 layout, header, entries, read API without RAM caches, gain table, stock block address, `stockValid()`) | VX2-1.1–1.3 |
| `Core/SampleRom/SampleInstall.h/.c` | whole (WAV format acceptance, frame conversion 24 → 16 bit and stereo → mono, peak gain code, item writes, `sampleInstall_abortItem()`, `sampleInstall_setFrameFormat()`, completeness checks returning −4) | VX2-2.x, P2 §17 F4/F7/F8 |
| `Core/SampleRom/sampleFlash.c` | whole (two-line change) | — |
| `Core/Hardware/SD/storageTypes.h/.c` | whole (`storage_naturalCompare()` and helpers) | VX2-3.1–3.2 |
| `Core/Hardware/SD/filesystem.h/.c` | **hunks**: installer state, v1 LFN/manifest helpers removed, `filesystem_parseWav()`, `filesystem_installCollect()`, `filesystem_installSampleTree()`, `filesystem_installWavetableTree()`, `filesystem_installWavetable()` Pass A/B, `filesystem_installCopy()` with `FS_COPY_*`, `filesystem_blockOpenGiveUp()` and bounded open retries, `filesystem_blockOpenDirLfn()` subfolder opens, depth bound | VX2-3.3–3.9, P2 §17 F1/F2/F7/F8, N1 |
| `Core/Menu/menu.c` | **hunk**: `menu_loadSamplesModal()` | VX2-3.10 |
| `main.c` | **hunk**: `boot_showSampleCatalogueNotice()` and its call after `sampleMemory_init()`. The `insertFx_init()` and FxBuffer/VoiceBuffer DEV-dump hunks stay out. | VX2-3.12 |

### 7.2 Rules carried over

- **Folders:** `samples/` and `wavetables/`; files first, then subfolders,
  in natural order; depth limit 3; a numeric prefix orders and is stripped
  from the display name (D2, D3).
- **Wavetables:** every folder holding `.wav` files is one wavetable named
  by the folder. One stored depth and one frame length per table; the source
  format may differ per frame file (P2 F4). At most 128 tables and 128
  frames.
- **Never expand:** 8-bit stays 8-bit, 24-bit becomes 16-bit, stereo
  becomes mono (D11, W1).
- **Gain code:** `gain = 2^(code/24)` (D10). Applied to `smp`/`slp`; no gain
  on wavetable frames.
- **Error handling:** one unreadable file skips that item, not the whole
  install (F7/F8). Long-named subfolders install (F1). No hang at depth (F2).
- **`/loops` is retired.** The v1 loop flag is ignored; `slp` loops the
  whole sample.

### 7.3 RAM (net release, needs approval)

| Change | Bytes |
|---|---:|
| `sample_manifest[120]` + count removed | −13,441 |
| `SampleMemory` caches removed | −5,040 |
| `fs_install_batch[32]` + count | +2,049 |
| `fs_install_last[4]` | +256 |
| `sample_install` state | +32 |
| catalogue counters | +2 |
| **Net** | **≈ −16,142** |

### 7.4 Sample flash map

| Region | Address | Bytes |
|---|---|---:|
| Stock-wave block | `0x080C0000` | 61,120 |
| Item data | `0x080CEEC0` | 1,241,408 |
| Catalogue v2 | `0x081FE000` | 8,192 |

A v1 install shows `Samples invalid` / `reload samples` at boot (D13).

---

## 8. Stock waves `sq3 sw3 sqa tra swa` (P2 WP4)

| Spike file | Take | Doc |
|---|---|---|
| `tools/gen_stock_recipes.py` | whole (CSV → recipes) | VX2-4.1 |
| `tools/plots/stock_wave_harmonics.csv`, `plot_stock_waves.py`, `reference/303_square_saw.png`, `reference/tra_dirty_triangle.png`, `stock_waves_303_analog.png` | whole | — |
| `Core/SampleRom/StockWaveRecipes.c` | whole (generated; about 10.2 KB flash) | VX2-4.2 |
| `Core/SampleRom/StockWaveGen.h/.c` | whole (generated into the stock block at install time) | VX2-4.3 |
| `Core/DSPAudio/StockWaves.h/.c` | whole (read access with fallback, level lengths, 11 levels) | VX2-4.4 |
| `Core/Menu/MenuText.h` `waveformNames` | hunk (11 names) | VX2-4.10 |
| Engine trigger phase offsets (`osc_isTableWave()`) | hunk per engine (§10) | VX2-4.12 |

---

## 9. Wavetable type `wtb` (P2 WP5)

| Spike file | Take | Doc |
|---|---|---|
| `Core/DSPAudio/OscillatorWavetable.h/.c` | whole (bind, 8- and 16-bit render, FM render; two frames read every sample; the `wav` row = position 0–127 across the frames) | VX2-5.3–5.4 |
| `Core/DSP/Instruments/InstrumentManager.c` | hunks: `wtb` writers and maximum rules (VX2-5.7); on a change into `wtb`, clear `osc->wtb.base` and `primed` before the bind (P2 F11) | VX2-5.7, P2 §17 F11 |
| Engine trigger phase for `wtb` (table offset) | hunk per engine | VX2-5.8 |
| `tools/plots/plot_wavetable_formats.py`, `wavetable_formats.png` | whole | — |

**Known cost:** `wtb` measures 64 instructions per sample (FM 70) against
the 47/62 target. This was accepted in P2 (F6, deviation 3).
- **Possible saving, not done:** two running frame pointers updated only
  when the frame index changes.
- **When to decide:** in S085 if the voice total needs it (§16).

---

## 10. Engines (P1 WP8, P2 VX2-4.12 / 5.8)

**Spike files:** `DrumVoice.c/.h`, `Snare.c/.h`, `CymbalVoice.c/.h`,
`HiHat.c/.h`. **Take hunks only.**

| Take | Leave out |
|---|---|
| `OscSectionState section` in the struct; `oscSection_init(&voice->section, n)` in init (Drum 2, Snare 1, Cymbal/HiHat 3) | `DriveShaperState shaper`, `driveType`, `driveShaper_reset()` (P2 drive) |
| Trigger: `oscSection_beginTrigger()` gates the phase resets; the phase offsets per type: `smp`/`slp` → `oscSample_restart()`, `pwm`/`wtb` → table offset, `osc_isTableWave()`; filter cleared only in `rst` | `transient_isSnap()` / `transient_isOffset()` (P2 transient). Keep the pre-S084 tests `transGen.waveform == 0` (snap) and `== 1` (offset) |
| Render: `oscSection_renderDrum()` / `_renderFm2()`, the `itp` second render into the pool scratch and `oscSection_blendItp()`, `oscSection_finishBlock()` | `insertFx_renderTransient()` PRE/POST, `insertFx_transientTrigger()`, `stageSlot` (P3) |
| Drum seed fix `TRI + (seed_index % 3u)` (P1-8.2) | `voicePost_drumGain()` / `_mixAddGain()` / `_addGain()` and the removal of distortion from the post-chain (P3). Keep the pre-S084 fused `voicePost_drum()` / `_mixAddGainDist()` / `_addGainDist()` |
| | `SVF_calcBlockZDFShaped()` calls. Keep `SVF_calcBlockZDF()` |

Async (`*_calcAsync*`) has no S085 change (P1-8.5), except the snap test,
which stays pre-S084 (see above).

---

## 11. InstrumentManager, presets, mixer (P1 WP10, P1 §18 F1/F2, P2 D12)

### 11.1 Shared-file guide

| File | Take | Leave out |
|---|---|---|
| `InstrumentManager.h` | `IM_SPECIAL_OSC_TYPE`, `_OSC_SOURCE`, `_OSC_WAVE`, `_RUN_MODE`; `IM_SPECIAL_OSC_MASK/OSC1/OSC2/OSC3/OSC_NOISE`; declarations of `instrumentManager_oscWaveMax`, `_oscSourceMax`, `_isSelectorDescriptor`, `_oscSection`, `_oscForDescriptor` | `IM_SPECIAL_TRANSIENT_TYPE`, `_DRIVE_TYPE`, `_MIX_FX_TYPE`, `_MIX_FX_AMOUNT`; swap/quiet API (P1 WP2) |
| `InstrumentManager.c` | the four writers (OSC_TYPE re-derives `waveform`/`source` from the request bytes, F2; OSC_WAVE/OSC_SOURCE record the request then clamp); `instrumentManager_osc()`, `_oscBySelector()`, `_oscWaveMax()`, `_oscSourceMax()`; per-slot wave domain and `instrumentManager_refreshWaveDomains()` (adapter and velocity refresh, P1-10.4); filter reso target and recalc via the section (P1-10.3); `itp` claims for all slots (P1-10.5); classifier (P1-10.6, with the deviation 7 exclusion of `osc2_mod_type`); `isSelectorDescriptor()` in its **P2 generalised form** (D12: INSTANCE_OFFSET, MORPHABLE clear, any special writer); `wtb` rules and F11; the 1,176 B reserve and asserts | transient writers, `IM_SPECIAL_DRIVE_TYPE`, `driveFields`, Mix writers, `insertFields`, `stageInputPeak`, the `SLOT_DECIMATION` re-point to `insertFx_setRate()` (keep `mixer_decimation_rate[slot]`); slot quiet/declick (P1-2.1–2.3) unless the swap decision says otherwise |
| `presetManager.c` | P1 §18 F1: selector rows accepted in `preset_setSupplementalParameterInternal()` before the INSTANCE_OFFSET rejection (Normal cell + Morph mirror + `morph_interpolation[]`); `preset_applySelectorRows()` (P2 name) called in `preset_resetAndApplyKitVoiceImage()` right after the runtime reset, and in the immediate Instrument apply; `preset_applyKitVoiceSupplementalInternal()` skips selector rows | every `voiceBuffer_beginPass()`/`endPass()` (P3); the swap worker, `preset_armInstrumentSwap()` and the rest (P1 WP2) unless the swap decision says otherwise |
| `mixer.c` | the block-start pool reset and `itp` claims (P1-6.5) | the decimator removal and `insertFx_service()` (P3); peak tracking and swap declick (P1-2.4) unless the swap decision says otherwise |

### 11.2 Behaviour to preserve

- **Selectors:** `typ`, `src` and `run` are stored single-ended, applied
  before the Morph image on every Scene, track and Instrument apply, and
  survive type round trips (F1/F2).
- **Not tracked:** LFO modulation of `wav` writes `osc->waveform` directly
  every block and is not tracked in `waveRequest` (documented at the
  writer).

---

## 12. Descriptor rows, pages, menu (P1 WP9/WP11, P2 WP9 osc parts)

### 12.1 Parameter tables (`*Parameters.c/.h`, take hunks)

- **Selector macro:** `ROW_SELECT_SPECIAL` (P1-9.1).
  `ROW_SELECT_NOAUTO_SPECIAL` (P2) is only needed for `trt`/`fxt`, so it
  stays out.
- **Wave rows:** the waveform rows gain the `IM_SPECIAL_OSC_WAVE` tag
  (P1-9.2).
- **New rows and counts:** the rows in §0.3 (P1-9.3), the enums, the counts
  in each `.h` (Drum 44, Snare 41, Cymbal/HiHat 46) and the
  `_Static_assert`s.
- **Menu cells** (P1-9.4):

  | Instrument | Page | Cells |
  |---|---|---|
  | Drum | page 0 | 3–5 `typ src run` |
  | Drum | FM page | 4–5 `typ src` |
  | Snare | page 0 | `typ src run` |
  | Cymbal/HiHat | page 0 | 3–5 `typ src run` |
  | Cymbal/HiHat | Mod page | `typ src` ×2 |

- **Leave out:**
  - the `trt`, `dty`, `fxt` and `amt` rows;
  - the P2 transient/filter page cells and the P3 Voice page cells;
  - the P3 `dpt`/`rte` renames and the `SLOT_DECIMATION` comment changes.

### 12.2 `InstrumentCellFormat.h/.c` (new file, take the osc subset)

**Kinds to keep:**
- `CELL_FMT_OSC_TYPE`, `RUN_MODE`, `PWM_FAMILY`;
- `SAMPLE`, `NO_SOURCE`, `STOCK_WAVE`;
- `PWM_WIDTH`, `SAMPLE_START`;
- `WAVETABLE`, `WTB_POSITION` (with `aux` = frame count).

**Leave out:**
- `TRANSIENT_TYPE/SHAPE/PCM/BUF`, `DRIVE_TYPE`, `MIX_FX_TYPE`;
- the `hidden` mask field and `instrumentCellFormat_step()` (P3);
- the label hooks `instrumentCellFormat_shortLabel/longLabel()` (P3).

### 12.3 `MenuText.h` (take hunks)

**Tables to take:**
- `oscTypeNames` (5);
- `runModeNames`;
- `pwmFamilyNames`;
- `mixFmNames` (`fm mix snc rng`);
- `waveformNames` (11).

**Leave out:** `transientTypeNames`, `driveTypeNames`, the `transientNames`
changes and `lfoWaveNames` (LFO decision).

### 12.4 `menu.c` (take hunks)

| Hunk | Doc |
|---|---|
| `menu_instrumentCellText3()` and `menu_instrumentCellFull()`, with the osc kinds only | P1-11.2, VX2-9.4/9.5 |
| The five formatted-cell call sites; the P1 form, not the P3 label-hook form | P1-11.2 |
| `DTYPE_MIX_FM` four values at the six sites (format, clamp 3, automation max 3) | P1-11.3 |
| `MENU_WAVEFORM` without the sample tail | P1-11.4 |
| `menu_clampCellValue()` format branch (clamp to the cell maximum; no hidden-skip) | P1-11.2 |
| Count asserts for the osc tables | VX2-9.6 |
| Automation range for `wav` rows by type (`menu_automationValueMaxSlot()`) | VX2-9.7 |
| `menu_loadSamplesModal()` | VX2-3.10 |
| **Leave out** | the P3 encoder hidden-skip (`menu.c` ~14803) and the label-hook sites |

---

## 13. Tools and host tests

| Spike file | Take |
|---|---|
| `tools/sampleoptimize.py` | whole: the card optimiser. It shrinks wavetables to the smallest power-of-two frame length that plays the same (−20 dB worst, −40 dB typical), trims sample tails below −80 dB and converts non-installable WAVs. It keeps `_old` backups and supports `--dry-run`. |
| `tools/plots/lxr_plotlib.py`, `README.md`, `plot_pwm.py`, `pwm_families.png`, and the stock/wavetable plots (§8, §9) | whole. Leave out `plot_waveshapers.py`, `waveshapers_*.png` and `reference/waveshapers.png` (S086), and `__pycache__/`. |
| `tools/dsp_test/test_osc_pwm.c`, `test_osc_sample.c`, `test_osc_run.c`, `test_natural_order.c`, `test_catalogue.c`, `test_stock_gen.c`, `test_wtb.c`, `frozen/stock_ref.bin` | whole |
| `tools/dsp_test/extract.py` | whole (multi-dimensional objects, P1 deviation 8) |
| `tools/dsp_test/Makefile` | hunks: the targets for the tests above and `armcheck-vx2` (stock reader and `wtb` loops only; drop the shaper and transient probes) |
| `tools/dsp_test/armcheck/vx2.c` / `vx2_spec.c` | osc parts only |
| `tools/dsp_test/check_special_tags.py` | hunk: the osc keys (`osc1_type` … `osc_run_mode`) |
| `tools/dsp_test/DSP_TEST.md` | the osc target rows |
| `tools/dsp_test/looplen.py` | whole (generic loop report; useful for §16) |

**Leave out:**
- `test_shaper`, `test_transient`, `test_insert_types`,
  `test_voicebuffer_cases`, `test_fxbuf`, `test_crumpbit_elastic`,
  `host_transient_tables.c`, `armcheck/vx3_probe.sh`;
- `test_lfo_oneshot` and `test_swap_level` (pending decisions).

**Gate D host suite:**
- the pre-S084 targets (`selftest`, `filter`, `pack`, `postchain`,
  `octave`, `mixer`, `special_tags` 177);
- `osc_pwm`, `osc_sample`, `osc_run`, `natural_order`, `catalogue`,
  `stock_gen`, `wtb`;
- `armcheck-postchain`, `armcheck-mixer`, `armcheck-vx2`.

**What the tests prove:** the P1 review reverted each fix and the target
failed on the old code (P1 §18.3). That shows these tests can fail. Repeat
one such revert per test in S085.

---

## 14. Specifications (osc sections only)

| File | Sections |
|---|---|
| `dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` | oscillator types, sources, pwm families, sample playback, `wtb`, stock waves, run modes, mod modes, pool |
| `FILESYSTEM_SPEC.md` | catalogue v2, installer, folders, natural order, depth, boot notice |
| `MODULE_INTERCHANGE_SPEC.md` | the osc keys and the missing-key rule |
| `STORAGE_SRAM_MANIFEST.md` | pool scratch, the installer RAM changes, the 1,176 B reserve |
| `BANK_PRESET_ARCHITECTURE.md` | selector rows stored and applied before the Morph image |
| `OSC_INTERP_AUDIT.md` | the pool replaces the two-slot interpolation limit |
| `dsp_instruments_effects/CPU_USE_DSP_AUDIT.md` | the oscillator loop costs from §16 |
| `MIDI_SPEC.md`, `DEV_MODES.md` | the osc rows only |

Copy the spike's osc paragraphs. Do not copy any transient, drive, Mix FX,
buffer or elastic-Effect paragraphs.

---

## 15. Hardware check (after Gate D)

From P1 §15.4 / P2 §13.3, osc parts:

1. **`wav`:** every stock wave including `sq3 sw3 sqa tra swa`, at low and
   high notes, plain and FM.
2. **`pwm`:** each family across the width row and the note range.
3. **`smp`/`slp`:** start sweeps; loop seam; gain normalisation.
4. **`wtb`:** position sweep and LFO on position, 8- and 16-bit tables.
5. **Run modes:** `rst`/`fre`/`itp` on fast retriggers; the filter rings in
   `fre`/`itp`; velocity and LFO on resonance in `fre`/`itp`.
6. **Drum `mod` modes:** `fm mix snc rng`; `snc` with a sample master and
   start > 0.
7. **Install:** install from `samples/` and `wavetables/`, including a
   long-named and a three-deep folder, and one truncated WAV (skipped, not
   aborted). Check the boot notice with a v1 flash.
8. **Old kits:** they load and sound the same; sample-pointing `wav` values
   clamp to `Cym`.
9. **CPU:** the worst-case Scene with `wtb` FM oscillators and `itp` on all
   slots; no underruns (`audioCodec_underrunCount`).

---

## 16. Cost record (before Gate D)

**Spike measurements** (static loop instructions per sample, `armcheck-vx2`):

| Loop | Instructions | Note |
|---|---:|---|
| 1,024-point table oscillator / FM | 24 / 35 | pre-S084 path, with the per-sample slew added in P1 |
| Stock reader / FM | 24 / 31 | |
| `wtb` / FM | 64 / 70 | target 47 / 62 (accepted in P2) |
| `pwm` families | ≈ 30 (P1 estimate; measure) | |
| `smp`/`slp` | measure | two reads + interpolate |
| `itp` crossfade | a second section render for 132 samples, pool-limited | |

**For S085:**
- **Measure:** the same loops on the pre-S084 tree (the oscillator slew
  changed every `wav` loop) and on the S085 tree.
- **Record:** both in `CPU_USE_DSP_AUDIT.md`.
- **The figure for S086:** the per-voice oscillator worst case (`wtb` FM
  with `itp` active). S086 budgets the effects chain next to it.
