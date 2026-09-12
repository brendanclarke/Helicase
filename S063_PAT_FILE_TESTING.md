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

## Test 3: Root Pattern Load (slot 000) — FAIL (OPEN)

**Steps**: After successful Pattern Save (Test 2 fix verified), load Pattern
from slot 000.

**Symptom**: Screen freezes on `Load: Pattern  ` / `001 Barf    ...` — the
ellipsis never clears. Buttons still respond (mode changes work), and the
Pattern data loads correctly (audio audibly changes to the loaded pattern), but
the completion callback never fires so the menu never exits the loading screen.

**SD card post-test** (`SD_CARD_PAT_TEST_2/`): `.hcnames` has 146 lines,
correct format, Pattern row shows `Barf	001	R`. Both `.pat` files are 10656
bytes with valid PAT4 headers. No `.hcnamtmp` file present. `.hcindex` correct.

### Fix attempt 1 — prepareResidentNamesCache (DID NOT FIX)

`filesystem_startPatternHcnamesUpdate()` was missing the call to
`filesystem_prepareResidentNamesCache()` that every other HCNAMES update entry
point makes. Added it:

```c
static void filesystem_startPatternHcnamesUpdate(void)
{
    if (current_op == FS_INTERNAL_OP_SAVE_PATTERN) {
        op_library_index_rebuild_kind = FS_NAME_CACHE_PATTERN;
        op_library_index_rebuild_pending = 1u;
    }
    filesystem_prepareResidentNamesCache();
    current_op = FS_INTERNAL_OP_UPDATE_HCNAMES_PATTERN;
    op_phase = 0u;
}
```

This is structurally correct (matches all other entry points) and remains
applied, but **did not fix the Pattern Load freeze**. The freeze persists
identically after this change.

### Fix attempt 2 — bypass HCNAMES update for load (REVERTED)

Replaced the `filesystem_startPatternHcnamesUpdate()` call in
`filesystem_loadPattern_tick()` case 14u with a direct
`filesystem_cacheCurrentResidentPatternName()` + `filesystem_finish(DONE)`,
skipping the HCNAMES update entirely for Pattern Load.

This eliminated the freeze but was **not a real fix** — it avoided the problem
by skipping the operation rather than finding why the HCNAMES update hangs.
Pattern Load must complete the full HCNAMES update (as Scene Load does) to
ensure the `.hcnames` file reflects the loaded state. **Reverted.**

### Current state

- Fix 1 (`prepareResidentNamesCache`) remains applied — correct but insufficient
- Fix 2 (HCNAMES bypass) reverted — case 14u calls
  `filesystem_startPatternHcnamesUpdate()` again
- **Root cause is unknown.** The HCNAMES update state machine
  (`filesystem_residentNames_tick`) is shared code that works for Scene
  Save/Load but hangs for Pattern Load. The HCNAMES update has no stall
  detection, so the hang is permanent.
- Pattern data loads correctly into SRAM (audio works); the freeze is in the
  post-load HCNAMES update only.

### Investigation notes

Exhaustive static analysis checked:
- `completion_callback` is not lost (only modified at 6 known locations)
- `op_close_status` is initialized to DONE by `filesystem_start()`
- `op_library_index_rebuild_pending` is 0 for loads (no index rebuild)
- HCNAMES file format is correct (146 lines, proper Pattern rows)
- `filesystem_formatResidentNameLine` handles Pattern rows (no type field)
- `filesystem_residentSourceValid` accepts Pattern row sources

Uninvestigated areas (for manual debugging):
- Which HCNAMES phase the state machine is stuck in (no runtime trace exists)
- Whether asyncfatfs internal state after the Pattern Load file read sequence
  (open dir → chdir → close dir → open file → read → close → chdir root)
  affects subsequent root-level `.hcnames` file open
- Whether `op_file_ready` / `op_close_done` / `op_rename_done` carry stale
  state from the Pattern Load into the HCNAMES update phases
- Whether the probe/bootstrap path (phase 7) is entered unexpectedly

---

## Remaining tests

- [x] Root Pattern Save completes without freeze (Test 2)
- [ ] Root Pattern Load completes without freeze (Test 3 — **BLOCKED**)
- [ ] Pattern Save with rename retires the old file
- [ ] Scene Save round-trip still works after fix
- [ ] Bank Save/Load with Pattern children
- [ ] Pattern HCNAMES row visible after save (row 129+)
- [ ] Pattern `.hcindex` correct after save
