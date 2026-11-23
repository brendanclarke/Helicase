# Session 097 — Program Flash Refactor, Menu Semantics, Cold-Source Optimisation, Filesystem Fixes

```
DATE: 2026-11-17 / 2026-11-18
SESSION GOAL: Reduce program flash use without changing function; find and fix baseline defects
              exposed during -Os testing.
COMPLETED: P0 assessment and SOM archive (−8,536 B); P1 five source refactors (−7,504 B);
           P2 cold-source -Os and cold-last link order (−11,676 B default); P3 filesystem
           investigation probe, five baseline fixes (Scene Load apply mask, OK drop/retry,
           Pattern realign, Bank Save masks, boot notice underrun), background budget 3 %.
           FS_OS=1 tried (−47,916 B) and reverted after device stumble; retested clean with
           all P3 fixes but left off by default.
VERIFIED ON HARDWARE: Yes — P2 step-1 build (§9); FS_OS=1 stumble (§10–§11); P3 baseline
                      fixes confirmed (§10 user: "the last test fixed the stumble and other
                      scene load glitches"); FS_OS=1 retest with all fixes clean (§12);
                      OK replay confirmed (§12).

CHANGES THIS SESSION:
- archive/som/: SomGenerator.c/.h, SomData.c/.h moved here (P0)
- archive/som/README.md: new, describes the archived module (P0)
- Makefile: SOM sources removed, OS_SRCS + -Os rule, FS_OS switch, COLD_LAST link order,
  MenuLabels.c in SRCS, Space kit comment corrected (P0/P1/P2)
- Core/Menu/menuPages.h: menuStaticPages[6][8] + menuStaticPageSlot[17] replace menuPages[17][8] (P1)
- Core/Menu/menu.c: menu_pageRow(); formatDtype3/formatMenuCell3; soundApplyLoadMask +
  loadSaveDropClick + loadSaveOkRetry + loadSaveRetryOk + lcdOverlayRoom (P1/P3)
- Core/Menu/menu.h: menuPages extern removed; label enums extended (P1)
- Core/Menu/MenuText.h: table widths trimmed; pools → extern + accessors (P1)
- Core/Menu/MenuLabels.c: new — label pools + 217 descriptor labels (P1)
- Core/DSP/Instruments/InstrumentManager.h: ParamDescriptor 28 → 20 B (P1)
- Seven *Parameters.c, CrumpBitParameters.c, SpaceParameters.c, EffectParamRows.h: row labels
  as ids (391 calls) (P1)
- Core/Menu/menuEffects.c, Core/DSP/Effects/EffectsManager.c: label reader conversions (P1)
- Core/Hardware/SD/filesystem.c: safeTextWrite_tick + two descriptors; blockOpenMode;
  FS_HOT_O2 on six functions; probe under DEV_MODE_LOGGING; patternRealignPlayingTracks;
  Bank Save mask ordering fix (P1/P2/P3)
- Core/Bank/Scene/Preset/presetManager.c/.h: preset_startLoadApply() (P3)
- Core/Bank/Scene/AutosaveTrace.h: stages 'f' and 'k' (P3)
- tools/diagnostic_tools/decode_devlogs.py: 'f' and 'k' branches (P3)
- config.h: BACKGROUND_CPU_BUDGET_US_PER_MS_PLAYING 25 → 30 (P3)
- Core/Sequencer/sequencer.c: seq_stepIndex comment corrected; SOM removed (P0)
- main.c: SOM include and som_init() removed (P0)
- Core/Hardware/frontPanel/buttonHandler.c/.h: SELECT_MODE_SOM_GEN removed (P0)
- Core/DSP/Instruments/Alkaline/AlkalineSquareTable.c: header comment corrected (P0)
- tools/alkaline/gen_alk_tables.py: header comment corrected (P0)
- README.md: SOM removed, archive/som/ added, menuPages.h and MenuText.h/MenuLabels.c lines (P0/P1)

KNOWN ISSUES INTRODUCED: FS_OS=1 causes a sequencer stumble during Scene Load (the long
  filesystem pass has not been identified; the probe data points to a fixed-work phase at -Os).
  FS_OS stays 0 by default.
KNOWN ISSUES RESOLVED: Scene Load re-applied the active Scene regardless of destination;
  OK presses dropped without recovery on Load/Save pages; Pattern timing drifted after
  loading into the playing Scene; Bank replace Save kept old children (B3); boot notices
  stalled audio (63 underruns).

NEXT SESSION RECOMMENDED GOAL: Device test of P3 §13 (Bank replace Save fix) and §14
  (boot notice / budget). Then either the FS_OS=1 long-pass hunt (trace the specific
  phase) or new feature work. The semantic label merges (P0 §4 S) are now cheap with
  the index-based labels.
BLOCKERS: Bank replace Save fix needs device confirmation. Boot notice fix needs device
  confirmation. FS_OS=1 decision deferred.

CRITICAL REMINDERS FOR NEXT SESSION:
- FS_OS stays 0 (default). FS_OS=1 causes a stumble. The P3 fixes make it device-clean
  but off by default.
- The LTO partition pin was rejected; cold-last link order is what works.
- menuStaticPageSlot[NUM_PAGES] is 1-based; 0 means "not a table page".
- ParamDescriptor is 20 B; effect_param_descriptor_t is 28 B. Label ids, not pointers.
- Descriptor label pools are in MenuLabels.c, not MenuText.h.
- P0 §4 G (shared LFO block) is paused: the premise was wrong; rows are mid-table, not a tail.
```

---

## Source Documents

This log retains all relevant detail from the following documents, which are deleted
after this log is written:

- `S097_P0_PROGRAM_FLASH_USE_REFACTOR.md` — Assessment and SOM archive
- `S097_P1_MENU_SEMANTICS_REFACTOR_IMPLEMENTATION.md` — Five source refactors
- `S097_P2_UI_STORAGE_CODE_OPTIMIZATION.md` — Plan for cold-source -Os
- `S097_P2_UI_STORAGE_IMPLEMENTATION.md` — Implementation, device test, FS_OS revert
- `S097_P3_FS_OPTIMIZE_INVESTIGATION.md` — Probe, baseline fixes, FS_OS retest

---

## Phase P0 — Program Flash Assessment and SOM Archive

### Baseline

742,696 of 753,664 B used, **10,968 B headroom**. Already under the 16 KiB
`link_budget.py` warning. ITCM: 15,960 of 16,384 B.

### Flash breakdown

| Kind | Bytes |
|------|------:|
| Code in `.text` | 535,292 |
| Read-only data (`.rodata*`, merged into `.text`) | 195,739 |
|   of which string sections (`.rodata.str*`) | 16,165 |
| ITCM code (stored in flash, copied at boot) | 15,412 |
| Vectors, `.data`/`.dtcm` initialisers, stubs | ~1,850 |
| libc / libm / libgcc (included above) | 7,031 |

### Largest data tables

| Table | Bytes | Note |
|-------|------:|------|
| `sawTable`, `triTable`, `recTable`, `alkSqrTable` | 4 × 22,528 | 11 × 1,024 × int16 each; all rows distinct, cannot share |
| `transientData` | 26,460 | 12 × 2,205 int8; 2,483 B zero tails |
| `SomData.c` `Node_0..8` | 8,064 | 93 % zero; archived in §8 |
| `oscPwmTables.c` | 4,224 | |
| `alkResTable` | 3,744 | |
| `menuPages` | 3,264 | 96 % zero; compacted in P1 |
| `sine_table` | 2,050 | |
| `alk_zeroRow` | 2,048 | all zero |
| Instrument descriptors (7 types) | 9,100 | 325 rows × 28 B; reduced in P1 |
| Effect descriptors | ~2,800 | 36 B rows; reduced in P1 |

### Largest code by source file

| Source | Bytes | Source | Bytes |
|--------|------:|--------|------:|
| `filesystem.c` | 131,240 | `copyClearService.c` | 16,934 |
| `menu.c` | 93,040 | `Oscillator.c` | 16,102 |
| `asyncfatfs.c` | 19,890 | `InstrumentManager.c` | 15,460 |
| `sequencer.c` | 19,198 | `main.c` | 13,996 |
| `buttonHandler.c` | 17,032 | `CrumpBitEffect.c` | 13,426 |

### LTO jitter finding

Any change to the LTO unit moves some hot DSP loops by ±1 instruction, even a
change in code the audio path never runs. Each build variant needs the loop gate.
The jitter follows the LTO partition count: today the link makes 17 LTRANS
partitions; shrinking cold code changes the count, moving partition cuts through
the voice renders. Pinning the count (`--param=lto-partitions=17
--param=lto-min-partition=1000`) stops it, but every pin value tested in P2 moved
3 PWM loops (rejected). The cold-last link order (P2 §3) is what solved it.

### SOM generator archived (−8,536 B)

The SOM ("self-organising map") Pattern generator could never run.
`seq_SomModeActive` was a `static uint8_t` that nothing set; `som_tick()` was
dead code. The page (SHIFT+MODE STEP → `SOM_PAGE`) opened and edited state
nothing read.

**Result:** 742,696 → 734,160 B (−8,536 B). `.bss` −32 B. ITCM unchanged.
`nm` shows no SOM symbol in the image. Loop gate: 86/86 oscillator loops exact,
77 oscillator and 58 mixer hot loops equal.

**Files changed:**

| File | Change |
|------|--------|
| `SomGenerator.c/.h`, `SomData.c/.h` | moved to `archive/som/` |
| `archive/som/README.md` | new: what the module was, why archived, how to restore |
| `Makefile` | SOM sources removed; Space kit `-Os` comment corrected |
| `main.c` | `#include "SomGenerator.h"` and `som_init()` removed |
| `sequencer.c` | include, `seq_SomModeActive`, dead `som_tick()` branch removed; `seq_stepIndex` comment corrected |
| `menu.c` | include and four `menu_parseGlobalParam()` SOM cases removed |
| `menuPages.h` | `SOM_PAGE` first row blanked, marked unused |
| `menu.h` | `SOM_PAGE` kept (so `EFFECT_PAGE` keeps its id), marked unused |
| `ParameterArray.h` | four SOM `PAR_*` ids kept, marked unused |
| `buttonHandler.h` | `SELECT_MODE_SOM_GEN` removed; 0x06 noted as unused |
| `buttonHandler.c` | SHIFT+MODE STEP returns before any action |
| `AlkalineSquareTable.c`, `gen_alk_tables.py` | header comment corrected |

**Kept on purpose:** `PAR_*` ids, `TEXT_*` name ids, `parameter_dtypes` entries,
`SOM_PAGE` id, and the four `valueNames` entries. Removing them would renumber
every later id.

### Candidates not taken

| Candidate | Reason |
|-----------|--------|
| Stock wave tables, `transientData` | all rows distinct; compression changes per-sample reader (CPU ceiling) |
| `DiodeLadder_renderB2Osc*`, oscillator `*Ramp` twins | binding ITCM specialisations |
| Engine triggers (Drum~Cymbal 90 %) | `-Ofast` engine code, S093 cost rule |
| `alk_zeroRow` to RAM | −2,048 B flash but +2,048 B RAM; needs sign-off |
| Link-time `-Os` alone | −824 B |
| libm (`sinf`/`cosf`, `powf`) | changes installed wavetable bytes |
| `DEV_STALL_DETECTION 0` | −1,856 B; gate clean; **future lever** |
| `DEV_MODE_LOGGING 0` | −17,820 B; 3 osc loops +1; **future lever** |

---

## Phase P1 — Menu and Semantics Refactor

Reference image: S097 P0 (734,160 B). Every step loop-gated and data-checked.

### Running totals

| Step | Flash used | Δ vs reference | Gate |
|------|-----------:|---------------:|------|
| reference (S097 P0) | 734,160 | — | — |
| 1 `menuPages` | 732,120 | −2,040 | all loops exact |
| 4 name-table widths | 730,960 | −3,200 | all loops exact |
| 5 K filesystem clones | 729,976 | −4,184 | all loops exact |
| 5 J value formatters | 729,688 | −4,472 | all loops exact |
| 2 labels as indexes | 726,656 | **−7,504** | all loops exact |
| 3 shared LFO block | — | — | **paused** |

### 1. `menuPages` → static-page table (−2,040 B)

`menuPages[NUM_PAGES][NUM_SUB_PAGES]` held 17 × 8 × 24 B = 3,264 B. 122 of 136
rows were all-zero. Only six pages read cells from it: MENU_MIDI (sub-pages 0–3),
PERFORMANCE (0–1), SEQ (0–2), EUKLID (0), PATTERN_SETTINGS (0) and RECORDING
(0–1).

**After:** `menuStaticPages[6][NUM_SUB_PAGES]` (1,152 B) +
`menuStaticPageSlot[NUM_PAGES]` (17 B, 1-based, 0 = "not a table page").
`menu_pageRow(page, subPage)` returns the row or the shared all-zero
`menu_emptyPageRow`. Four call sites converted. Each of the six table pages keeps
all eight sub-pages (room for growth).

Data check: all 136 (page, sub-page) rows equal the reference. Loop gate clean.

### 4. Name tables trimmed (−1,160 B)

| Table | Width | Before → After | Δ |
|-------|------:|---------------:|--:|
| `longNames` | 16 → 9 | 1,632 → 918 | −714 |
| `catNames` | 16 → 9 | 480 → 270 | −210 |
| `midiFilterNames` | 16 → 4 | 272 → 68 | −204 |
| `filterTypes` | 8 → 4 | 72 → 36 | −36 |

Renames: "Gate Mode" → "GateMode" (visible), "CPU use time" → "CPU use" (no
change, 8th cell was a space), "PtrnStoreUse" → "PtrnStor" (no change). Readers
use at most 8 characters.

### 2. Descriptor labels as indexes (−3,032 B)

`ParamDescriptor` shrank from 28 to 20 B by replacing three `const char *`
display fields (12 B) with three `uint8_t` pool indexes (3 B). The `file_key`
pointer stays (it is the storage key).

**Pools:** `shortNames[]`, `catNames[]`, `longNames[]` moved from `MenuText.h` to
new `Core/Menu/MenuLabels.c`. Each existing entry reuses its old id. 217
descriptor-only labels appended. Compile-time count asserts.

| Pool | Before | Reused | Added | After | Width |
|------|-------:|-------:|------:|------:|------:|
| `shortNames` | 101 | 31 | 123 | 224 | 4 |
| `catNames` | 30 | 12 | 18 | 48 | 9 |
| `longNames` | 102 | 31 | 76 | 178 | 10 (was 9) |

`longNames` widened 9 → 10 for two 9-character descriptor long names
("DstVoice2", "DstParam2").

**Rows:** 325 instrument and 78 effect row labels converted to ids. A misspelt
id is now a build error.

**Readers:** 16 lines in `menu.c`, `menuEffects.c`, `EffectsManager.c` use
`menuLabel_short(id)`/`menuLabel_cat(id)`/`menuLabel_long(id)`.

**Untouched:** Scene mod-target descriptors keep pointer labels (their
`short_name` is the `midilrn.cfg` key). CrumpBit per-sub-type label overrides
keep their pointer struct.

Data check: all 403 rows (325 instrument, 78 effect) verified label-identical.
Every other field (file key, dtype, flags, binding) identical. Static pool
entries: first 101/30/102 entries unchanged.

**What this enables:** semantic label merges (P0 §4 S) are now cheap — a label
change is one id edit per row; merging two pool entries frees a slot.

### 5.J Value formatters (−288 B)

`menu_formatDtype3(dtype, value, out)` and `menu_formatMenuCell3(cell, value, out)`
merge the shared branches of `menu_formatCellValue3()`, `va_formatValue3()` and
`menu_formatAutomationValue3()`. Each formatter calls the shared helper first;
its own special cases still win. Helpers are `noinline`.

### 5.K Filesystem clone families (−984 B)

- `filesystem_saveGlobals_tick()` + `filesystem_saveMidiLearn_tick()` → one
  `filesystem_safeTextWrite_tick(const fs_safe_text_write_t *w)` with two
  `static const` descriptors. `noinline, noclone`. Phase numbers, stall
  observer, error codes unchanged. Wrappers preserve the dispatcher.
- `filesystem_blockOpen()` + `blockOpenLfn()` + `blockOpenDirLfn()` → one
  `filesystem_blockOpenMode(name, mode)`. 25 call sites unchanged.

### 3. Shared LFO block (paused)

The P0 premise (LFO rows as a shared tail) was wrong. The LFO rows sit
mid-table at a different index in each type, split by Velocity rows. The
descriptor index is the storage cell; rows cannot move. Sharing needs a row
accessor at 41 sites in 6 files, including main-loop descriptor walks and the
Morph passes. Options: (a) row accessor with per-type `lfo` base offset, ~1.9 KB
saving but per-block CPU cost; (b) share only the 4 identical target rows, ~0.6
KB; (c) reorder tables (storage change, no migration); (d) leave as is.
Recommendation: d, or a only together with item 2 since both change the same
struct.

### 5.1 Padding and enum fixes (nothing to take)

Both P0 claims were wrong: `sizeof(enum Datatypes)` is 1 (this toolchain packs
enums); `scene_mod_targets[]` is already packed. No change.

### 5.2 Value-name table merges (not done, ~100 B)

Four small overlaps identified (`colFiltTypeNames`/`colRouteNames`,
`stepRollRateNames`/`perfBtnTypeNames`, `sceneChangeDivNames`/
`sceneQueueDivNames`, `menuEffects_runShort`/`storage_effectRunModeTokens`).
Better done as part of the semantic label merge (P0 §4 S) if that goes ahead.

---

## Phase P2 — Cold-Source Build Optimisation

Plan: `S097_P2_UI_STORAGE_CODE_OPTIMIZATION.md`. Implementation:
`S097_P2_UI_STORAGE_IMPLEMENTATION.md`.

### Hot-path reachability analysis

A call graph from the ELF disassembly listed each candidate file's functions
reachable from ISRs, the render path, TIM3, and `midiIn_dispatch()`. Files with
pointer hooks on these paths counted as reachable.

Key paths found:
- **TIM3 runs a full menu repaint** (`seq_processSchedulerTick →
  seq_handleMasterBoundary → led_notifyPatternChanged → menu_setShownPattern →
  menu_repaint`) — Follow Scene change repaints inside the ISR.
- **Render path reaches AutoSave** (`Tetrakt_triggerVoice → … →
  autosave_markPayloadOffsetDirty`).
- **TIM3 reaches** `pat_setStepActive → autosave_markPatternDirty`.
- **MIDI per message reaches** `midiLearn_applySource`.

### Step 1: five boot/install/FAT sources at `-Os` (−10,456 B)

| File | Δ alone | Why safe |
|------|--------:|----------|
| `asyncfatfs.c` | — | main loop only, inside filesystem operations |
| `SampleInstall.c`, `sampleFlash.c` | — | `Load:[Samples]`, audio suspended |
| `flashImage.c`, `SplashAnimation.c` | — | boot only |

`Makefile`: `OS_SRCS` lists the five files; rule adds `-Os` after `-O2`.

Gate: all loops exact, all 21 ISR/render bodies identical. 726,656 → 716,200 B.

### Step 2: cold sources linked after DSP objects (−1,220 B more)

LTO's balanced partitioner fills partitions in object order. Cold UI/storage
objects came before DSP objects, so any size change moved partition cuts through
voice renders. **Linking the cold objects last** left every loop exact. The
partition pin (`--param=lto-partitions=N`) was swept over N = 16–24; every value
moved 3 PWM loops — rejected.

`Makefile`: `COLD_LAST` list (`filesystem.c`, `storageTypes.c`, `asyncfatfs.c`,
`fat_standard.c`, `menu.c`, `menuEffects.c`, four CopyClear sources) filtered
out of `SRCS` in `OBJS` and appended after `DSP_SRCS`.

Gate: all loops exact. LTRANS partitions: 17 → 16. 716,200 → 714,980 B.

### Step 3: `filesystem.c` at `-Os` (`FS_OS=1`, opt-in)

`FS_OS ?= 0`. With `FS_OS=1`, `filesystem.c` joins `OS_SRCS` and gets
`-DFS_OS_BUILD`. Six functions reachable from TIM3 or MIDI marked `FS_HOT_O2`
(`__attribute__((optimize("O2")))` under `FS_OS_BUILD`).

Measured: 714,980 → 678,740 B (**−36,240 B**). Gate clean. Device test: stumble
(§10–§11), reverted. See P3.

### Default build result

726,656 → 714,980 B (**−11,676 B**, headroom **38,684 B**). Device-tested ("seems
ok"). All 86 oscillator loops exact; all hot loops equal; hot call sets
unchanged; ISR bodies differ only in data offsets.

### Excluded and why

| File(s) | Saving | Reason |
|---------|-------:|--------|
| `SceneData.c`, `StepScale.c` | — | accessors inlined into mixer/ISR; at `-Os` they become calls |
| `presetManager.c` | −3,836 | mixer gains a call to `scene_getVoiceFxSendAmount()` |
| `menu.c` | −20,984 | TIM3 repaints; still moves 9 mixer loops with cold-last |
| CopyClear | −10,428 | runs during play; moves 3 PWM loops today |
| `buttonHandler.c`, `ledHandler.c` | −6,136 | code runs in panel exchange ISR and sequencer |
| USB stack | −756 | changes `OTG_FS_IRQHandler` |
| `fat_standard.c`, `storageTypes.c`, `menuEffects.c` | — | reachable from TIM3 repaint |
| AutoSave/trace, `MidiLearn.c`, `*Parameters.c` | — | reachable from render, TIM3, or MIDI |
| `BankData.c` | +192 | grows at `-Os` |

---

## Phase P3 — Filesystem Investigation and Baseline Fixes

### The probe (DEV_MODE_LOGGING only)

New trace stage `'f'` (`AUTOSAVE_TRACE_STAGE_FS_PASS`). `filesystem_tick()`
becomes a wrapper that times `filesystem_tickBody()`. Records:

| flags | Record | When |
|------:|--------|------|
| 0 | slow pass | `filesystem_tick()` call ≥ 1,000 µs |
| 1 | operation maximum | when the entered operation changes |
| 2 | underrun | every rise of `audioCodec_underrunCount` |
| 3 | main-loop gap | once per second: longest gap between two `filesystem_tick()` starts |

Rules: nothing recorded before `audioCodec_isInitialized()`; nothing recorded
for trace flush or boot-log operations (a record must not start the next flush).
Cost: two TIM2 reads and a few compares per pass; at most one record per second.

RAM: 20 B DEV `.bss` (filesystem.c, SRAM1).

Decoder: `tools/diagnostic_tools/decode_devlogs.py` gains the `'f'` branch.

### Baseline device run: the problems predate `-Os`

User (`FS_OS=0` + probe): "something is fucked up in Scene load and possibly Scene
save, the 'ok' frequently does not respond, freezes after being clicked". The
probe showed no slow filesystem pass (longest 597 µs, phase 33), no underrun
during any Load or Save, and median main-loop gap 4.2 ms. **The audio never
starved.** The stumble is not CPU time.

### Fix 1: Scene Load re-applied the active Scene (§8.1)

After every runtime Scene or Kit Load, `preset_startDrumsetApply()` re-applied
the **active** Scene to the running engine regardless of which Scenes the Load
wrote: clearing step overrides, resetting voices, modulation, and Effect
mid-pattern.

**Fix:** `preset_startLoadApply(scene_mask)`. If the Load's destinations include
the active Scene, runs `preset_startDrumsetApply()` as before. Otherwise applies
nothing to the active Scene and only re-arms slots whose tracks play a loaded
Scene via per-track Scene playback. `menu_soundApplyLoadMask` (default
`MENU_SOUND_APPLY_ALL_SCENES`) is set to the Load's Scene mask in the
`PRESET_OP_SCENE_LOAD` and `PRESET_OP_KIT_LOAD` completions. Every other caller
(Bank, All/Performance, Kit Morph, copy/clear, boot) keeps the full apply.

### Fix 2: OK did nothing / needed several presses (§8.2)

**Inner gate:** `menu_handleLoadSaveMenu()` dropped clicks while filesystem busy,
blank name, copy held, or refused — leaving `editModeActive` = 1 on the OK row,
where the encoder does nothing.

Fix: `menu_loadSaveDropClick(outcome)` toggles `editModeActive` back at each drop
point.

**Outer gate:** `menu_parseEncoder()` swallowed every click while
`menu_storageBusy` or Preset not idle. No toggle, no request, no trace record.

Fix: `menu_loadSaveOkRetry` holds a refused OK and replays it at
`menu_pollPresetStatus()` when storage, Preset and filesystem are idle, the cursor
is still on the OK row, and attempts remain (`MENU_LS_OK_ATTEMPTS` = 4). Dropped
by any encoder turn or click, leaving the row or page, or attempts used up.

New DEV trace stage `'k'` (`AUTOSAVE_TRACE_STAGE_LOADSAVE_CLICK`):

| flags | Outcome |
|------:|---------|
| 0 | accepted |
| 1 | dropped: command running |
| 2 | dropped: blank name |
| 3 | dropped: copy held |
| 4 | refused |
| 5 | edit-mode toggle |
| 6 | held (busy gate) |
| 7 | replayed |

### Fix 3: Pattern timing drift after loading into the playing Scene (§9.2)

A Scene Load (phase 45) calls `pat_initScene(scene)` on the resident region, then
reads the file into it. For a playing destination, the region holds Normal
defaults (length 16, default scale) until the header extension is read (phase 47).
`presetMorph_tick()` → `seq_refreshTrackEffectiveParams()` picks them up
immediately, so a track with non-default scale/length advances at the wrong rate.
Nothing repositions it afterwards.

**Fix:** `filesystem_patternRealignPlayingTracks(scene_mask)`. While the sequencer
runs, calls `seq_refreshTrackEffectiveParams()` then
`seq_realignTrackToMasterClock(track)` for each track whose played Scene is in the
mask.

| Site | Mask |
|------|------|
| Scene Load phase 52 (after `filesystem_patternServiceFinish()`) | `op_scene_load_scene_mask` |
| Pattern Load phase 14, success | `op_scene_load_scene_mask` |
| Pattern Load phase 14, error | `op_scene_load_scene_mask` |
| Bank Load phase 17, empty Scenes | `op_bank_scene_empty_mask` |

Not called from `filesystem_finish()` (would fire on stale masks with side
effects).

### Fix 4: Bank replace Save kept old children (§13, B3)

`filesystem_requestSaveBank()` set `op_bank_save_written_mask` and
`op_bank_scene_empty_mask` **before** `filesystem_start()`, which cleared them.
Only `op_bank_scene_save_mask` (set after start) survived. Result: removed
children were never visited, so their folders stayed on the card; a Bank Load
brought them back.

**Fix:**
1. Written and empty masks set after `filesystem_start()`, before the manifest
   staging that reads them.
2. Bank Save phase 21: after deleting a removed child, `op_bank_cwd_at_parent = 1`
   (the delete scanner does not change the working directory).

**Behaviour now:**

| Save | Children afterwards | Manifest |
|------|--------------------|----------|
| Replace | exactly the saved Scenes; every other child deleted | written entries from RAM; removed entries self-only |
| Merge | saved Scenes replaced; others untouched | old manifest read for untouched entries only |

### Fix 5: Boot notice stalled audio (§14.3)

`menu_drainAutosaveBootNotices()` called `lcd_waitForIdle()` twice per overlay
after audio start, spinning until TIM7 drained the LCD queue. +63 underruns
logged.

**Fix:** `menu_lcdOverlayRoom()`. Before audio: waits as before. After audio:
draws only when the queue has room (40 ops; queue holds 64); skips otherwise.
Notice text: "AutoSave / bank load" → **"Last Bank / loaded"**.

### Fix 6: Background budget 3 %

`config.h`: `BACKGROUND_CPU_BUDGET_US_PER_MS_PLAYING` 25 → **30** µs/ms (3 %
while playing; stopped stays 5 %). Positive credit stays capped at 1 ms of
allowance.

### `FS_OS=1` retest with all fixes (§12)

Device run clean: 7 commands, 3 OK presses refused while card busy, each
replayed and accepted within 20–70 ms. No stumble. Longest filesystem pass:
740 µs (Scene Load phase 33 — same as `-O2`). One underrun at boot (idle). Still
off by default pending user decision.

### Load provenance at power-off (§15)

Scene and Pattern Loads write their `.hcnames` refreshed rows within the Load
operation (before it completes). Kit Loads commit on page exit
(`menu_endResidentNameScratchSession()`). The user confirmed this is the intended
design: "the intended workflow is to commit the kit on exit".

### Device evidence summary

| Card | Image | Key observation |
|------|-------|-----------------|
| `SD_CARD_BASE` | `FS_OS=0` + probe | stumble and OK problems in baseline |
| `SD_CARD_BASE_2` | P3 §8 fixes | OK works; timing drift on track 5 after loading into playing Scene |
| `SD_CARD_BASE_3` | P3 §9 fixes | stumble and drift fixed (user confirmed) |
| `SD_CARD_FS_OPT` | `FS_OS=1` + all fixes | clean: no stumble, OK replay works, longest pass 740 µs |
| `SD_CARD_ATS_TEST` | §14 fixes | boot notice: "Last Bank / loaded", no underrun burst expected |

### Gate and build (all of P3)

| Build | Flash | Loops | ISR/render |
|-------|------:|-------|------------|
| `FS_OS=0` + probe + all fixes | 716,216 | 86/86 exact; 77 + 58 hot loops equal | all 21 identical |
| `FS_OS=1` + probe + all fixes | 680,384 | 86/86 exact; all hot loops equal | `din_dout_exchange` data offsets only |

### Boot/install-only code at `-Os` (measured, not applied)

46 `filesystem.c` functions reachable only from boot or sample install, marked
`FS_COLD_OS`: 715,524 → 712,772 (**−2,752 B**). Gate clean. No runtime cost.
Not applied until `FS_OS=1` decision is made.

---

## Files Changed (all phases)

| File | Change | Phase |
|------|--------|-------|
| `archive/som/` | SomGenerator.c/.h, SomData.c/.h, README.md | P0 |
| `Makefile` | SOM removed; `OS_SRCS` + `-Os` rule; `FS_OS` switch (default 0); `COLD_LAST` link order; `MenuLabels.c` in `SRCS`; Space kit comment | P0/P1/P2 |
| `Core/Menu/menuPages.h` | `menuStaticPages[6][8]` + `menuStaticPageSlot[17]` | P1 |
| `Core/Menu/menu.c` | `menu_pageRow()`; `formatDtype3`/`formatMenuCell3`; `soundApplyLoadMask` + `loadSaveDropClick` + `loadSaveOkRetry` + `loadSaveRetryOk` + `lcdOverlayRoom`; SOM cases removed | P1/P3 |
| `Core/Menu/menu.h` | `menuPages` extern removed; label enums extended | P1 |
| `Core/Menu/MenuText.h` | table widths trimmed; pools → `extern` + `menuLabel_*()` | P1 |
| `Core/Menu/MenuLabels.c` (new) | three label pools + 217 descriptor labels; count asserts | P1 |
| `Core/DSP/Instruments/InstrumentManager.h` | `short_id`/`long_id`/`cat_id`; size assert 20 | P1 |
| seven `*Parameters.c`, `CrumpBitParameters.c`, `SpaceParameters.c`, `EffectParamRows.h` | row labels as ids (391 calls) | P1 |
| `Core/Menu/menuEffects.c`, `Core/DSP/Effects/EffectsManager.c` | 16 reader lines use accessors | P1 |
| `Core/Hardware/SD/filesystem.c` | `safeTextWrite_tick` + 2 descriptors; `blockOpenMode`; `FS_HOT_O2` on 6 functions; DEV probe; `patternRealignPlayingTracks`; Bank Save mask fix; probe tidy-up | P1/P2/P3 |
| `Core/Bank/Scene/Preset/presetManager.c/.h` | `preset_startLoadApply()` | P3 |
| `Core/Bank/Scene/AutosaveTrace.h` | stages `'f'` and `'k'` | P3 |
| `tools/diagnostic_tools/decode_devlogs.py` | `'f'` and `'k'` branches | P3 |
| `config.h` | `BACKGROUND_CPU_BUDGET_US_PER_MS_PLAYING` 25 → 30 | P3 |
| `Core/Sequencer/sequencer.c` | SOM removed; `seq_stepIndex` comment corrected | P0 |
| `main.c` | SOM include and `som_init()` removed | P0 |
| `Core/Hardware/frontPanel/buttonHandler.c/.h` | `SELECT_MODE_SOM_GEN` removed | P0 |
| `Core/DSP/Instruments/Alkaline/AlkalineSquareTable.c` | header comment corrected | P0 |
| `tools/alkaline/gen_alk_tables.py` | header comment corrected | P0 |
| `README.md` | SOM/archive/menuPages/MenuLabels lines | P0/P1 |

---

## Measurements (end of session)

| Metric | `FS_OS=0` (default) | `FS_OS=1` (off) |
|--------|--------------------:|-----------------:|
| Flash used | 714,980 | 678,740 |
| Flash headroom | 38,684 | 74,924 |
| ITCM | 15,976 / 16,384 | unchanged |
| SRAM1 `.bss` | unchanged | unchanged |
| `.data` | +4 B alignment | — |
| LTRANS partitions | 16 | 15 |

---

## Open Items Carried from This Session

- **`FS_OS=1` decision:** device-tested clean with all P3 fixes, but off by
  default. The long pass that caused the original stumble was never identified;
  the probe showed no pass ≥ 1 ms. Recovery path: trace the specific phase, keep
  it at `-O2`, or time-bound it.
- **Bank replace Save fix (§13):** waiting for device confirmation.
- **Boot notice / budget (§14):** waiting for device confirmation.
- **P0 §4 S (semantic label merges):** now cheap with index-based labels. User
  decision pending on specific renames.
- **P0 §4 G (shared LFO block):** paused; premise wrong. Options in P1 §3.
- **P0 §4 M (`alk_zeroRow` to RAM):** −2,048 B flash, +2,048 B RAM. Needs
  sign-off.
