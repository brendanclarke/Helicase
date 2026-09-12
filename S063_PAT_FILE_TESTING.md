# S063 Pattern File — Testing & Bugfix Log

## Test 1: Scene Save/Load round-trip — PASS

**Steps**: Boot (all Scenes emptied — expected). Load Kit to Scene 0. Create a
pattern. Save Scene (slot 073). Reboot. Load Scene 073.

**Result**: Scene loads correctly. Pattern data, Kit, and instruments restore as
expected.

---

## Test 2: Root Pattern Save (slot 000) — FAIL → FIXED

**Steps**: With the loaded Scene from Test 1, save Pattern to slot 000.

**Symptom**: Screen freezes on `...`. Buttons still respond (FS is cycling, not
hard-locked). The `.pat` file was created on the SD card, but the save
completion callback never fired.

### Root cause

`filesystem_startPatternHcnamesUpdate()` double-triggered the library index
rebuild chain. It did two conflicting things:

1. Set `op_library_index_rebuild_pending = 1` — this activates the standard
   rebuild path in `filesystem_flushFinish_tick()`.
2. Replaced `completion_callback` with `filesystem_patternHcnamesUpdateComplete`
   and parked the original user callback in `op_library_index_rebuild_callback`.

After the HCNAMES update flushed, `filesystem_flushFinish_tick()` saw
`pending = 1` and called `filesystem_startLibraryIndexRebuild()`, which
**overwrote** `op_library_index_rebuild_callback` with the injected
`filesystem_patternHcnamesUpdateComplete` (losing the original user callback).
The standard chain then completed and called
`filesystem_patternHcnamesUpdateComplete`, which started a **second** scan →
index rebuild cycle. That second cycle completed, but
`op_library_index_rebuild_callback` was NULL by then, so the original save
callback was never invoked. The filesystem returned to IDLE but the menu
never received completion — hence the permanent `...` display.

**Comparison**: Scene Save and Kit Save correctly use *only* the pending flag
and let `filesystem_flushFinish_tick()` → `filesystem_startLibraryIndexRebuild()`
handle the entire chain. They never replace `completion_callback`.

### Fix (applied)

Removed the three callback-replacement lines from
`filesystem_startPatternHcnamesUpdate()` and deleted the now-dead
`filesystem_patternHcnamesUpdateComplete()` function and its forward
declaration. The Pattern Save HCNAMES path now matches Scene Save exactly:

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

**Build**: `make -j2` succeeds. `text=423716, data=416, bss=279372` (−160 bytes
text from removing dead code).

### Verification chain (post-fix)

After fix, the root Pattern Save completion chain is:

1. `filesystem_savePattern_tick()` case 13 → `filesystem_startPatternHcnamesUpdate()`
2. Sets `op_library_index_rebuild_pending = 1`, enters HCNAMES update
3. HCNAMES update completes → `filesystem_finish(DONE)` → flush
4. `filesystem_flushFinish_tick()` sees `pending = 1` → `filesystem_startLibraryIndexRebuild()`
5. Parks `completion_callback` (the **original** user callback) into `op_library_index_rebuild_callback`
6. Scan `/Pattern/` → write `.hcindex` → flush → `filesystem_completeLibraryIndexRebuild(DONE)`
7. Calls the parked original user callback → menu receives completion

This matches the Kit Save and Scene Save chains exactly.

---

## Test 3: Root Pattern Load (slot 001) — FAIL → FIXED

**Steps**: After successful Pattern Save (Test 2 fix verified), load Pattern
from slot 001.

**Symptom**: Screen freezes on `Load: Pattern  ` / `001 Barf    ...` — the
ellipsis never clears. Buttons still respond (mode changes work), and the
Pattern data loads correctly (audio audibly changes to the loaded pattern).

**SD card post-test** (`SD_CARD_PAT_TEST_2/`, `SD_CARD_PAT_TEST_3/`):
`.hcnames` has 146 lines, correct format, no `.hcnamtmp` present —
proving the HCNAMES update completed successfully.

### Misdiagnosis

Initial investigation assumed the HCNAMES update state machine was hanging.
Two fix attempts targeted the filesystem layer:

1. Added `filesystem_prepareResidentNamesCache()` to
   `filesystem_startPatternHcnamesUpdate()`. Structurally correct (matches
   other HCNAMES entry points) but did not fix the symptom. Remains applied.
2. Bypassed the HCNAMES update entirely for Pattern Load. Eliminated the
   symptom but was a workaround, not a fix. Reverted.

The filesystem operation was completing successfully all along.

### Root cause (two independent defects)

See `S063_PAT_LOAD_FAILS_PRECISE_FIX.md` for the full evidence chain.

**Defect 1 — persistent `...`**: The `PRESET_OP_PATTERN_LOAD` completion
branch in `menu.c` cleared `menu_storageBusy` directly but never called
`menu_finishLoadSaveCommand()`. This left `menu_loadSaveCommandActive = 1`
permanently, so every Load/Save repaint showed `...` and input remained
command-gated. The filesystem callback fired and reached Menu, but Menu
failed to terminate the accepted command.

**Defect 2 — Pattern loaded into wrong Scene**: `preset_loadPattern()` had
no destination argument. The filesystem request substituted
`scene_getActiveIndex()`, so the PAT4 reader always wrote the playing Scene
regardless of the user's SEQ-button selection. The selected Scene mask from
the Load page never reached the filesystem.

### Fix (applied)

**Menu completion** (`menu.c` `PRESET_OP_PATTERN_LOAD` branch): Now calls
`pat_applyTrackSettingsToMenu()` for the current track, then routes through
`menu_requestLoadCommandFinalIndexRestore()` → `menu_finishLoadSaveCommand()`,
matching Scene/Bank Load's terminal contract.

**Scene mask plumbing**: Menu passes `menu_kitLoadSceneMask` through
`preset_loadPatternForScenes()` → `filesystem_requestLoadPatternForScenes()`.
The filesystem validates the mask, sets `op_pattern_scene` to the first
selected bit as the stream target, and stores the full mask in
`op_scene_load_scene_mask`.

**Fan-out copy** (`filesystem_loadPattern_tick` case 14u): After CRC
validation, copies `pat_scene_region_t` from the stream target to every other
selected Scene via `memcpy`. Calls `bank_invalidateSdCleanScene()` for each
destination. On error, also invalidates the first target after `pat_initScene`.

**HCNAMES multi-row publication**: `filesystem_cacheCurrentResidentPatternName`
now iterates `op_scene_load_scene_mask` instead of using the single
`op_pattern_scene`, publishing name/source/refreshed for every selected Scene's
Pattern row. Pattern Save sets `op_scene_load_scene_mask` to its single
`op_pattern_scene` bit, preserving single-row publication.

**Preset**: `on_pattern_load_complete` now calls
`preset_markRequestedScenesPresentOnSuccessfulLoad()` before
`preset_completeFilesystemOp()`, matching Kit/Scene/Instrument Load.

**Build**: `text=423996 (+280), data=416, bss=279372`.

---

## Test 4: Root Pattern Load — non-active destination — PASS

**Steps**: Load Pattern into a non-playing/non-active resident Scene via SEQ
mask selection.

**Result**: Pattern loads into the selected Scene without hanging. Menu exits
`...` correctly and returns to the bracketed type row. No freeze.

---

## Test 5: Bank Save/Load (slot 036) — PASS (with follow-up)

**Steps**: Save and load Bank slot 036.

**SD card post-test**: `SD_CARD_PAT_TEST_4/`.

**Result**: Bank Save and Bank Load complete successfully. Pattern data
round-trips through the embedded Scene children.

**Follow-up required**: All resident Scenes were saved into the Bank, including
Scenes that should have been marked empty at boot and were never explicitly
loaded. This suggests the Bank Save path does not filter by the Bank present
mask, or that boot is marking Scenes present when it should not. This is not a
regression from the Pattern file changes — it is a pre-existing behavior that
needs more investigation in a later pass. Do not fix now.

---

## Session 063 Pattern file status

Pattern Save and Pattern Load are functional. The v4 binary format, HCNAMES
publication, library index rebuild, and multi-Scene fan-out are verified on
hardware. The next session (S064) will address Pattern AutoSave.

## Remaining tests

- [x] Root Pattern Save completes without freeze (Test 2)
- [x] Root Pattern Load — single non-active destination (Test 4)
- [ ] Root Pattern Load — multi-destination with active opt-in
- [x] Pattern Load terminal ownership — no hang (Test 4)
- [ ] Pattern Save with rename retires the old file
- [ ] Scene Save round-trip still works after fix
- [x] Bank Save/Load with Pattern children (Test 5 — functional, see follow-up)
- [ ] Pattern HCNAMES rows correct after multi-Scene load (row 129+)
- [ ] Pattern `.hcindex` correct after save
- [ ] **Follow-up**: investigate why empty/unloaded Scenes are saved in Bank
