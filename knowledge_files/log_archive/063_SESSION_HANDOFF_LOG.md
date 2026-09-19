# Session 063 — v4 Binary Pattern File Format And Pattern Load/Save Fixes

## End of session block

```
DATE: 2026-09-12
SESSION GOAL: Implement v4 binary Pattern file format (Phase A/B from
S063_PATTERN_FILE_PHASE_A_B.md), fix Pattern Save and Pattern Load bugs,
create S064 Pattern AutoSave planning document.
COMPLETED: v4 PAT4 binary format, pat_scene_region_t packed struct with
Option B accessors, Pattern Save callback chain fix, Pattern Load dual-defect
fix (menu command lifecycle + scene mask plumbing), HCNAMES 145-row
expansion, filesystem_requestLoadPatternForScenes API with fan-out copy,
S064 planning document.
VERIFIED ON HARDWARE: Pattern Save (root slot 000), Pattern Load (slot 001,
both active and non-active destinations), Bank Save/Load (slot 036).

CHANGES THIS SESSION:
- Core/Hardware/SD/filesystem.c: v4 PAT4 binary writer/reader, Pattern Load
  fan-out copy, filesystem_requestLoadPatternForScenes(),
  filesystem_cacheCurrentResidentPatternName() mask iteration,
  Pattern Save op_scene_load_scene_mask, reverted HCNAMES bypass workaround
- Core/Hardware/SD/filesystem.h: filesystem_requestLoadPatternForScenes()
  declaration
- Core/Bank/Scene/Preset/presetManager.c: preset_loadPatternForScenes(),
  on_pattern_load_complete() preset_markRequestedScenesPresentOnSuccessfulLoad
- Core/Bank/Scene/Preset/presetManager.h: preset_loadPatternForScenes()
  declaration
- Core/Menu/menu.c: PRESET_OP_PATTERN_LOAD branch fix (terminal command
  lifecycle), Pattern Load dispatch with scene mask
- Core/Bank/Scene/Pattern/PatternData.c/h: pat_scene_region_t struct,
  pat_sceneRegion()/pat_sceneRegionMut() accessors

KNOWN ISSUES INTRODUCED: None
KNOWN ISSUES RESOLVED:
- Pattern Save freeze (HCNAMES callback chain double-trigger)
- Pattern Load freeze (menu command lifecycle never terminated)
- Pattern Load wrong destination (scene mask not plumbed)

NEXT SESSION RECOMMENDED GOAL: Session 064 — Pattern AutoSave (separate
per-Scene A/B pair files, dirty mask, snapshot region, drain writer, boot
reader, HCNAMES expansion to 145 in AutoSave)
BLOCKERS: None — S064 planning document is complete

CRITICAL REMINDERS FOR NEXT SESSION:
- AUTOSAVE_HCNAMES_ROW_COUNT stays at 129 until S064 explicitly expands it
  (breaking change to .hcprms format)
- autosave_markSceneWithPatternDirty() is still a stub calling
  markSceneWithoutPatternDirty()
- Pattern rows 129-144 in HCNAMES are filesystem-only for S063; AutoSave
  does not read or write them yet
- Bank Save empty-Scene follow-up: all resident Scenes save even when some
  should be marked empty at boot — pre-existing, not a regression
```

---

## 1. v4 PAT4 Binary Pattern File Format

### 1.1 File Layout

Total file size: 10,656 bytes.

```
Offset   Size    Content
0        4       Magic "PAT4" (0x50 0x41 0x54 0x34)
4        2       Version (1, uint16_t LE)
6        2       PAT_STACK_SIZE (256, uint16_t LE)
8        2       Reserved (0)
10       4       Generation counter (uint32_t LE)
14       4       CRC32C (uint32_t LE, zeroed during computation)
18       2       Reserved (0)
20       4       Reserved (0)
24       8       Reserved (0)
--- end fixed header: 32 bytes ---
32       1       pattern_change_bar
33       1       pattern_next
34       14      Reserved (0)
--- end pattern params: 16 bytes ---
48       16      Track 0: length(1), scale(1), shuffle(1), reserved(13)
64       16      Track 1: same layout
...
144      16      Track 6: same layout
--- end per-track params: 112 bytes (7 × 16) ---
--- end full header: 160 bytes ---
160      1792    Address array (7 tracks × 128 steps × 2 bytes, uint16_t LE)
1952     512     Free bitmap (512 bytes)
2464     8192    Event pool (PAT_STACK_SIZE × 32 bytes)
--- end file: 10,656 bytes ---
```

### 1.2 CRC32C

Castagnoli CRC32C computed over the entire file while treating the 4 bytes at
offset 14 as zero. This matches the HCPR AutoSave record CRC contract.

### 1.3 Generation Counter

- Library saves write generation 0.
- AutoSave (S064) will increment from 1.
- Boot picks the valid file with the highest generation; A wins ties.

### 1.4 Validation

`filesystem_patternHeaderValid()` checks:
- File size == expected (10,656 at PAT_STACK_SIZE=256)
- Magic == "PAT4"
- Version == 1
- PAT_STACK_SIZE matches firmware constant
- CRC32C over entire file validates

---

## 2. `pat_scene_region_t` Packed SRAM Struct

### 2.1 Definition

```c
typedef struct __attribute__((packed)) {
    uint16_t address[NUM_TRACKS][NUM_STEPS];  /* 7 × 128 × 2 = 1,792 */
    uint8_t  pool[PAT_STACK_SIZE * 32];       /* 256 × 32 = 8,192 */
    uint8_t  bitmap[512];                      /* 512 */
    uint8_t  track_length[NUM_TRACKS];         /* 7 */
    uint8_t  track_scale[NUM_TRACKS];          /* 7 */
    uint8_t  track_shuffle[NUM_TRACKS];        /* 7 */
    uint8_t  pattern_change_bar;               /* 1 */
    uint8_t  pattern_next;                     /* 1 */
} pat_scene_region_t;                          /* total: 10,519 */
```

16 Scenes × 10,519 = 168,304 bytes in SRAM1.

### 2.2 Option B Accessors

```c
const pat_scene_region_t *pat_sceneRegion(uint8_t scene_index);
pat_scene_region_t *pat_sceneRegionMut(uint8_t scene_index);
```

These expose the struct pointer directly, matching the `scene_t` / `kit_t`
accessor pattern. All pattern read/write operations go through these.

### 2.3 File Serialization Mapping

The v4 file header contains the per-track params (`track_length`, `track_scale`,
`track_shuffle`, `pattern_change_bar`, `pattern_next`) in a padded header
layout. The address array, bitmap, and pool are stored directly in file order.
The reader/writer maps between the packed SRAM struct and the padded file
header.

---

## 3. Pattern Save Fix

### 3.1 Root Cause

`filesystem_startPatternHcnamesUpdate()` double-triggered the library index
rebuild chain. It did two conflicting things:

1. Set `op_library_index_rebuild_pending = 1` — activating the standard
   rebuild path in `filesystem_flushFinish_tick()`.
2. Replaced `completion_callback` with
   `filesystem_patternHcnamesUpdateComplete` and parked the original user
   callback in `op_library_index_rebuild_callback`.

After the HCNAMES update flushed, `filesystem_flushFinish_tick()` saw
`pending = 1` and called `filesystem_startLibraryIndexRebuild()`, which
**overwrote** `op_library_index_rebuild_callback` with the injected
`filesystem_patternHcnamesUpdateComplete` (losing the original user callback).
The standard chain completed and called
`filesystem_patternHcnamesUpdateComplete`, which started a **second** scan →
index rebuild. That second cycle completed with NULL callback. The filesystem
returned to IDLE but the menu never received completion — hence permanent
`...`.

### 3.2 Fix

Removed the three callback-replacement lines from
`filesystem_startPatternHcnamesUpdate()` and deleted the now-dead
`filesystem_patternHcnamesUpdateComplete()` function and forward declaration.
The Pattern Save HCNAMES path now matches Scene/Kit Save exactly:

```c
static void filesystem_startPatternHcnamesUpdate(void)
{
    if (current_op == FS_INTERNAL_OP_SAVE_PATTERN) {
        op_library_index_rebuild_kind = FS_NAME_CACHE_PATTERN;
        op_library_index_rebuild_pending = 1u;
    }
    current_op = FS_INTERNAL_OP_UPDATE_HCNAMES_PATTERN;
    op_phase = 0u;
}
```

### 3.3 Post-Fix Completion Chain

1. `filesystem_savePattern_tick()` case 13 →
   `filesystem_startPatternHcnamesUpdate()`
2. Sets `op_library_index_rebuild_pending = 1`, enters HCNAMES update
3. HCNAMES update completes → `filesystem_finish(DONE)` → flush
4. `filesystem_flushFinish_tick()` sees `pending = 1` →
   `filesystem_startLibraryIndexRebuild()`
5. Parks `completion_callback` (the **original** user callback) into
   `op_library_index_rebuild_callback`
6. Scan `/Pattern/` → write `.hcindex` → flush →
   `filesystem_completeLibraryIndexRebuild(DONE)`
7. Calls the parked original user callback → menu receives completion

Matches Kit Save and Scene Save chains exactly.

---

## 4. Pattern Load Fix

### 4.1 Two Independent Defects

**Defect 1 — persistent `...` (menu command lifecycle):**

The `PRESET_OP_PATTERN_LOAD` completion branch in `menu.c` cleared
`menu_storageBusy` directly but never called `menu_finishLoadSaveCommand()`.
This left `menu_loadSaveCommandActive = 1` permanently, so every Load/Save
repaint showed `...` and input remained command-gated. The filesystem callback
fired and reached Menu, but Menu failed to terminate the accepted command.

**Defect 2 — Pattern loaded into wrong Scene (scene mask):**

`preset_loadPattern()` had no destination argument. The filesystem request
substituted `scene_getActiveIndex()`, so the PAT4 reader always wrote the
playing Scene regardless of the user's SEQ-button selection. The selected
Scene mask from the Load page never reached the filesystem.

### 4.2 Misdiagnosis Note

Initial investigation assumed the HCNAMES update state machine was hanging.
Two fix attempts targeted the filesystem layer:
1. Added `filesystem_prepareResidentNamesCache()` to
   `filesystem_startPatternHcnamesUpdate()` — structurally correct but did
   not fix the symptom. Remains applied.
2. Bypassed the HCNAMES update entirely for Pattern Load — eliminated the
   symptom but was a workaround. **Reverted.**

The filesystem operation was completing successfully all along. The user found
both root causes independently.

### 4.3 Fix — Menu Completion

`PRESET_OP_PATTERN_LOAD` branch now:
```c
case PRESET_OP_PATTERN_LOAD:
    pat_applyPatternSettingsToMenu(menu_getViewedPattern());
    pat_applyTrackSettingsToMenu(menu_getViewedPattern(),
                                 menu_getActiveVoice());
    if (!menu_requestLoadCommandFinalIndexRestore()) {
        if (menu_loadSaveCommandActive)
            menu_finishLoadSaveCommand();
        else {
            menu_storageBusy = 0u;
            menu_resetSaveParameters();
        }
    }
    break;
```

This routes through `menu_requestLoadCommandFinalIndexRestore()` →
`menu_loadCommandFinalIndexComplete()` → `filesystem_ack()` →
`menu_finishLoadSaveCommand()`, matching Scene/Bank Load's terminal contract.

### 4.4 Fix — Scene Mask Plumbing

**Menu** (`menu.c`): Pattern Load dispatch passes `menu_kitLoadSceneMask`:
```c
case SAVE_TYPE_PATTERN:
    if (preset_loadPatternForScenes(
            menu_currentPresetNr[SAVE_TYPE_PATTERN],
            menu_kitLoadSceneMask))
        commandAccepted = 1u;
    break;
```

**Preset** (`presetManager.c`): New `preset_loadPatternForScenes()`:
```c
uint8_t preset_loadPatternForScenes(uint16_t presetNr, uint16_t scene_mask)
{
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = PRESET_REQUEST_PATTERN;
    pm_kit_request_scene_mask = scene_mask;
    if (filesystem_requestLoadPatternForScenes(presetNr, scene_mask,
                                               on_pattern_load_complete))
        return 1;
    pm_status = PRESET_IDLE;
    return 0;
}
```

`on_pattern_load_complete()` now calls
`preset_markRequestedScenesPresentOnSuccessfulLoad()` before
`preset_completeFilesystemOp(PRESET_OP_PATTERN_LOAD)`, matching
Kit/Scene/Instrument Load.

**Filesystem** (`filesystem.c`): New
`filesystem_requestLoadPatternForScenes(slot, scene_mask, cb)`:
- Validates mask against `SCENE_COUNT`
- Sets `op_pattern_scene` to the first selected bit (stream target)
- Stores full mask in `op_scene_load_scene_mask`
- Copies display name from cached library name
- Starts `FS_INTERNAL_OP_LOAD_PATTERN`

The existing `filesystem_requestLoad(FS_FILE_PATTERN)` now delegates:
```c
case FS_FILE_PATTERN:
    return filesystem_requestLoadPatternForScenes(
        slot, (uint16_t)(1u << scene_getActiveIndex()), cb);
```

### 4.5 Fix — Fan-Out Copy

`filesystem_loadPattern_tick()` case 14u, after CRC validation:
```c
case 14u:
    if (!op_close_done) return;
    op_file = NULL;
    if (!afatfs_chdir(NULL)) return;
    if (op_close_status != FS_STATUS_DONE) {
        pat_initScene(op_pattern_scene);
        bank_invalidateSdCleanScene(op_pattern_scene);
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
    {
        const pat_scene_region_t *source =
            pat_sceneRegion(op_pattern_scene);
        uint8_t si;
        for (si = 0u; si < SCENE_COUNT && si < 16u; si++) {
            if ((op_scene_load_scene_mask &
                 (uint16_t)(1u << si)) == 0u)
                continue;
            if (si != op_pattern_scene) {
                pat_scene_region_t *target = pat_sceneRegionMut(si);
                if (target)
                    memcpy(target, source, sizeof(*target));
            }
            bank_invalidateSdCleanScene(si);
        }
    }
    filesystem_startPatternHcnamesUpdate();
    return;
```

On error, invalidates the first target after `pat_initScene`. On success,
copies `pat_scene_region_t` from the stream target to every other selected
Scene.

### 4.6 Fix — HCNAMES Multi-Row Publication

`filesystem_cacheCurrentResidentPatternName()` now iterates
`op_scene_load_scene_mask` instead of using the single `op_pattern_scene`:

```c
static void filesystem_cacheCurrentResidentPatternName(void)
{
    uint8_t scene_index;
    for (scene_index = 0u;
         scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        uint16_t row;
        if ((op_scene_load_scene_mask &
             (uint16_t)(1u << scene_index)) == 0u)
            continue;
        row = filesystem_residentPatternRow(scene_index);
        if (row >= FS_RESIDENT_NAMES_ROW_COUNT)
            continue;
        filesystem_cacheResidentName(row, op_pattern_display_name);
        (void)filesystem_setResidentSource(row, op_pattern_source);
        filesystem_setResidentRefreshed(row);
    }
}
```

Pattern Save sets `op_scene_load_scene_mask` to its single `op_pattern_scene`
bit:
```c
op_pattern_scene = scene_getActiveIndex();
op_scene_load_scene_mask = (uint16_t)(1u << op_pattern_scene);
```

---

## 5. HCNAMES Expansion: 129 → 145 Rows

### 5.1 New Row Layout

```
row 0          Bank
rows 1..16     resident Scene 0..15
rows 17..32    embedded Kit for resident Scene 0..15
rows 33..128   six Instruments per Scene (row = 33 + scene * 6 + voice)
rows 129..144  Pattern for resident Scene 0..15 (row = 129 + scene)
```

Pattern rows (129-144) use the Bank/Scene/Kit format:
`name<TAB>source[<TAB>R]\n` — no type field (unlike Instrument rows).

### 5.2 Current Scope

`FS_RESIDENT_NAMES_ROW_COUNT` is 145 in the filesystem. Pattern rows are
published by Pattern Save and Pattern Load.

`AUTOSAVE_HCNAMES_ROW_COUNT` stays at 129. The AutoSave record does not
read or write Pattern rows. Expansion to 145 is a breaking change to the
`.hcprms` format, deferred to Session 064.

### 5.3 Row Helper

`filesystem_residentPatternRow(scene_index)` returns `129 + scene_index`.

---

## 6. Hardware Test Results

### Test 1: Scene Save/Load round-trip — PASS

Boot, load Kit, create pattern, save Scene 073, reboot, load Scene 073.
Pattern data, Kit, and instruments restore correctly.

### Test 2: Root Pattern Save (slot 000) — FAIL → FIXED

Pattern Save froze on `...` due to HCNAMES callback chain double-trigger.
Fixed by removing callback replacement lines (§3 above).

### Test 3: Root Pattern Load (slot 001) — FAIL → FIXED

Pattern Load froze on `Load: Pattern ... 001 Barf    ...`. Two independent
defects: menu command lifecycle never terminated, scene mask not plumbed.
Fixed by routing through final index restore and adding scene mask plumbing
(§4 above).

### Test 4: Root Pattern Load — non-active destination — PASS

Pattern loads into non-playing/non-active resident Scene via SEQ mask
selection. Menu exits `...` correctly.

### Test 5: Bank Save/Load (slot 036) — PASS (with follow-up)

SD card post-test: `SD_CARD_PAT_TEST_4/`. Bank Save and Bank Load complete.
Pattern data round-trips through embedded Scene children.

**Follow-up**: All resident Scenes were saved into the Bank, including Scenes
that should have been marked empty at boot and were never explicitly loaded.
Pre-existing behavior, not a regression. Needs investigation in a later pass.

### Remaining Untested

- Root Pattern Load — multi-destination with active opt-in
- Pattern Save with rename retires the old file
- Scene Save round-trip after fix
- Pattern HCNAMES rows correct after multi-Scene load (row 129+)
- Pattern `.hcindex` correct after save

---

## 7. S064 Pattern AutoSave Plan Summary

Created `S064_DYNAMIC_PATTERN_AUTOSAVE.md` with the complete implementation
plan:

- **Separate per-Scene A/B pair files**: `.pat00a`/`.pat00b` through
  `.pat15b` (32 files total, 8.3-compatible)
- **Reuses v4 PAT4 format**: same 10,656-byte format, generation counter
  distinguishes A/B, CRC32C validates integrity
- **16-bit dirty mask**: `autosave_pattern_dirty_mask` in `Autosave.c`,
  one bit per Scene, whole-file drain (no per-byte tracking)
- **17th Scene snapshot region**: standalone
  `pat_scene_region_t pat_autosave_snapshot` (10,519 B) for coherent capture
- **TIM3-masked `memcpy`**: ~50 µs, BASEPRI to block TIM3 during snapshot
- **Drain scheduling**: one Scene per cycle, parameter drain priority
- **Boot reader**: validate A/B pair per Scene, pick higher generation,
  AutoSave wins over Scene directory when generation > 0 and HCNAMES source
  matches
- **HCNAMES expansion**: `AUTOSAVE_HCNAMES_ROW_COUNT` 129 → 145 (breaking
  format change, version bump to 2)
- **RAM cost**: ~10,809 bytes (snapshot + dirty mask + HCNAMES expansion)
- **9 implementation steps, 6 risks, 7 open questions**

---

## 8. Build

```
text=423996, data=416, bss=279372
```

Build delta from Session 062: text +15,776, bss +16,904 (from
`pat_scene_region_t` replacing the separate `pat_regions` + per-track params).
