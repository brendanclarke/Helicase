# S057 Kit Sanitize Refactor — Hardware Test Checklist

Ref: `S057_BOOT_KIT_SANITIZE_REFACTOR.md §6`

**What changed:** Removed the blocking boot-time Kit content validation and
quarantine (308 blocking file ops on a 44-Kit card). Kit content is now
validated lazily at load time — a proven-bad folder is renamed `err...` only
when a load actually fails, at the point of failure. Bank Loads use a
partial-failure contract: one bad child no longer fails the entire Bank.

---

## Boot / regression

- [x] **1. Boot timing**
  Measure boot time before/after on the 44-Kit card. The KITQUAR-specific
  delay should be gone with no other stage growing to compensate.
  **Expected:** measurably faster boot; no new delays.

- [x] **2. Normal boot, all-valid Kits**
  Boot on the current 44-Kit card with no corrupted content.
  **Expected:** every Kit appears in `.hcindex` with correct name, loads
  correctly, no regression versus pre-refactor behavior.

- [x] **4. Boot-timeout stress**
  Use a slow card or duplicate the 44 Kits several times over. Boot.
  **Expected:** boot no longer times out purely from Kit-count scaling —
  this is the exact false-boot-failure risk the refactor closes.

- [x] **5. Scene/Bank scan regression**
  Confirm the Kit boot-sequencing change does not affect subsequent
  Scene/Bank scan timing or ordering (they share `main.c`'s sequential boot
  block).
  **Expected:** identical scan behavior, no ordering change.

- [x] **6. Real-hardware boot**
  Boot the firmware on real hardware (not just a clean build). Boot-path
  changes have caused hangs before that simulation didn't catch.
  **Expected:** device boots to menu normally, no hang.

- [ ] **12. Boot-safety regression (most important test)**
  Boot with a resident Bank whose 16-scene load mask includes at least one
  corrupted embedded Kit. Before this fix, a Bank-wide `FS_STATUS_ERROR`
  here would hit `goto boot_filesystem_failure` in `main.c:898-901`.
  **Expected:** device boots normally. The bad child is excluded from the
  present mask; the boot does not fail. A regression here means one bad Kit
  can still brick a boot — the exact failure mode the refactor exists to
  close.

## Root Kit Load quarantine

- [ ] **3. Deliberately malformed Kit — load attempt**
  Corrupt one `kitset.kcg` line (or delete a referenced Instrument file) on
  a Kit in a test card copy. Boot, then attempt to Load that Kit from Menu.
  **Expected:** (a) boot completes without hitting the removed quarantine
  gate; (b) Kit still shows a normal row in `.hcindex`/menu with its display
  name; (c) Load fails cleanly — `FS_STATUS_ERROR`, no crash, no
  partial/corrupt state, failure UX not misleading; (d) Kit folder is
  renamed `err...` after the failed attempt and no longer appears on
  subsequent boot/rescan.

- [ ] **7. Root Kit Load quarantine rename**
  Corrupt one `kitset.kcg` line in a test Kit. From Menu, Load that Kit
  slot.
  **Expected:** `FS_STATUS_ERROR`, existing overlay appears, Kit folder
  renamed `errNNN ...`, subsequent `.hcindex` rebuild/rescan no longer lists
  it.

## Root Scene Load quarantine

- [ ] **8. Root Scene Load — Kit-layer cascade**
  Corrupt the embedded Kit inside a root Scene's `Kit <name>/` folder. Load
  that Scene from Menu.
  **Expected:** embedded Kit folder renamed `err...` **and** the owning
  `Scene/NNN Name` folder also renamed `err...` (Kit failure cascades to
  Scene). Overlay appears. Neither folder appears in later rescan.

- [ ] **9. Root Scene Load — Scene-layer-only failure**
  Corrupt `sceneset.scg` (or delete the bridge `.pat`) in a root Scene
  whose embedded Kit is otherwise valid. Load that Scene from Menu.
  **Expected:** only the Scene folder is renamed `err...`; the embedded Kit
  folder is untouched (it was never proven bad).

## Bank Load partial-failure

- [ ] **10. Bank Load — one bad child among several**
  Construct a test Bank with at least two Bank-local Scene children in the
  requested mask, one with a corrupted embedded Kit. Load the Bank.
  **Expected:** (a) corrupted child's embedded Kit folder renamed `err...`;
  (b) the Bank-local `SS Name` Scene folder itself is **not** renamed;
  (c) overall Bank Load completes `FS_STATUS_DONE`; (d) other valid
  child(ren) load and apply normally — sound, active Scene/Pattern, resident
  data unaffected; (e) failed slot's `bank_scenePresentMask()` bit is clear
  even if that slot had prior resident data; (f) error overlay appears once
  with `BKKit`-style code via `preset_bankLoadFailedSceneMask()` path.

- [ ] **11. Bank Load — only the active scene is bad**
  Repeat test 10 but make the single requested/failed child the Bank's
  declared active Scene.
  **Expected:** empty-Bank fallback ladder
  (`preset_loadFirstAvailableSceneOrKit()`) engages exactly as it does for a
  genuinely empty Bank. Overlay still appears.

- [ ] **13. Multiple simultaneous Bank Load failures**
  Two children in the same requested mask both invalid. Load the Bank.
  **Expected:** both excluded from present mask, both embedded Kits
  quarantined. Accept that the overlay code names only the more-recently-
  failed one (known limitation per §4f item 3).

## Legacy

- [ ] **14. Legacy `err...` folder interaction**
  Confirm a folder already renamed `err...` by the old boot-time quarantine
  (before this refactor shipped) is still correctly skipped by every scan.
  **Expected:** no regression, no reverse-quarantine — old `err...` folders
  are inert.

---

## §8 — Bank Save Per-Scene Safe Replace (Fix for ErrS05)

### 8.1 Problem

The current Bank Save uses total tree replacement:
`filesystem_deleteSlotDirectoryStart(op_slot, 0u)` at phase 4, then
`filesystem_deleteSlotDirectory_tick()` at phase 5 recursively deletes
the entire old Bank tree via `afatfs_deleteTree()` before phases 49+
create a replacement from scratch. Three issues:

1. **Non-masked children are destroyed.** Scenes not in
   `op_bank_scene_save_mask` are deleted along with the rest of the tree.
   A Bank that was previously saved with 8 valid children loses the
   unsaved 8 when the user re-saves with only a subset selected.

2. **Tree delete failure corrupts the Bank.** `afatfs_deleteTree()`
   aborts mid-tree when it encounters an unexpected directory layout
   (e.g. a quarantined `errKit` directory whose LFN entries were
   rewritten by `afatfs_renameObject_lfn()`). The abort leaves the Bank
   partially deleted: child `08 KitWool` removed, all others surviving,
   followed by an ErrS05 when phase 5 returns `FS_STATUS_ERROR`.

3. **Contradicts SCENE_OVERWRITE_SAFE.** The overwrite guard
   (S057_SCENE_OVERWRITE_SAFE.md) depends on the save mask to protect
   non-present Scenes from being overwritten. But total-delete removes
   ALL children before any child is written, so a power-loss during the
   delete phase destroys children that were never intended to be replaced.

### 8.2 Root cause of the ErrS05 test failure

Bank `009 LoadTst` was loaded with Scene child 15's embedded Kit
corrupted (missing instrument `poph1 6.hat`). The S057 quarantine code
renamed `Kit Pop` → `errKit Pop` inside `15 Pop/`. When the user later
saved the Bank over the same slot, phase 4-5 called
`afatfs_deleteTree()` on the entire `009 LoadTst/` tree. The recursive
delete processed entries until it encountered `errKit Pop` — whose LFN
run was rewritten by the rename at a different position/count than the
original `Kit Pop` entries. `afatfs_validateObjectInfo()` failed at the
tree scan (asyncfatfs.c ~line 6620), aborting the delete. The Bank was
left partially deleted with child 08 gone and an `FS_STATUS_ERROR`
returned at phase 5.

### 8.3 Fix: per-Scene child replacement

Replace the total-tree-delete with per-Scene-child safe replacement
inside the Bank directory. The Bank directory is kept and reused; only
the individual Scene children that are in the save mask get replaced.

#### 8.3.1 Bank directory open-or-create (replaces phases 4-5)

Remove phases 4-5 (`filesystem_deleteSlotDirectoryStart` /
`filesystem_deleteSlotDirectory_tick` for the entire Bank tree).

Replace with a scan-and-reuse approach before the child loop:

- Scan `/Bank/` for an existing directory whose slot-number prefix
  matches `op_slot`.
- **Found, same name:** open and chdir into it. No delete, no rename.
- **Found, different name:** rename the directory to the new display
  name via `afatfs_renameObject_lfn()`, then open and chdir.
- **Not found:** create via `afatfs_mkdir_lfn()` (existing phase 49
  behavior).

This preserves every child directory that isn't being replaced.

#### 8.3.2 bankset.bcg update

Phase 7-10 opens bankset.bcg with `"w"` mode (truncate + write). This
is correct for both new and existing Bank directories — in the existing
case the old file is truncated and rewritten with current metadata. No
change needed.

#### 8.3.3 Per-child safe-replace loop

For each child in `op_bank_scene_save_mask`, wrap the existing Scene
writer delegation with pre-write rename and post-write cleanup:

**Pre-write (new phases, before Scene writer phase 8):**

1. `prepareBankSceneSaveSource()` — set up child name `SS Name` from
   resident HCNAMES cache. (Moved from current phase 20.)
2. Scan the Bank directory for an existing child matching this two-digit
   slot prefix (same mechanism as the Bank Load child scanner or
   `filesystem_objectMatchesSlot()`).
3. If old child found: rename it to a temp name. Prepend `tmp` or
   similar prefix that the scanner will not match on any future scan.
   Record that an old child exists (flag for post-write cleanup).
4. If no old child: set flag to skip post-write cleanup.

**Write (existing phases 8..37 of Scene writer):**

5. `afatfs_mkdir_lfn(op_save_kit_dir_display_name, ...)` creates the
   new `SS Name/` directory — the old name is clear because step 3
   renamed it. Scene writer creates sceneset.scg, Kit directory,
   instruments, pattern.pat, effects.fx as before.

**Post-write (new phases, after Scene writer returns at phase 37):**

6. If an old child was renamed in step 3: initiate
   `afatfs_deleteTree()` on the old (temp-named) child.
7. Wait for the delete to complete.
8. If the delete **fails**: record a trace, clear the flag, continue to
   next child. The Bank is valid — the new child is durable. The stale
   temp-named child is orphaned but inert. It will be cleaned up on the
   next save to this slot.
9. If the delete **succeeds**: clear the flag, continue to next child.
10. Advance `op_bank_child_cursor` to the next bit in
    `op_bank_scene_save_mask` (existing phase 12 logic).

#### 8.3.4 Non-masked children

Children NOT in `op_bank_scene_save_mask` are never scanned, renamed,
written, or deleted. They persist in the Bank directory from previous
saves. This is the correct behavior: the save mask selects which Scenes
to update, not which to keep.

#### 8.3.5 Bank-local Kit quarantine (phase 65)

With per-Scene replacement, the `afatfs_deleteTree()` blast radius is
reduced to one child tree (one Scene directory + one Kit directory +
member files). This is much safer than deleting the entire Bank tree.

However, if the old child still contains a quarantined `errKit Name/`
directory from a previous load, the per-child `deleteTree()` at step 6
may still fail on those entries. Two options:

- **A. Remove Bank-local Kit quarantine:** Don't rename `Kit Name` →
  `errKit Name` inside Bank-local Scenes (Scene loader phase 65). The
  partial-failure contract (present-mask exclusion + error overlay) is
  sufficient for Bank-local children. The quarantine rename provides
  diagnostic value but creates downstream problems.
- **B. Keep quarantine, tolerate delete failure:** The post-write delete
  failure path (step 8 above) already handles this — the stale child is
  orphaned and the save continues. But this leaves growing debris on the
  card.

Recommendation: **Option A** for Bank-local children. Root Kit and root
Scene quarantine (phases 22-24, 63-70) remain — those operate in `/Kit/`
and `/Scene/` where `deleteTree` never encounters them during save.

#### 8.3.6 Phase number assignment

New phases in `filesystem_saveBankDirectory_tick()`:

| Phase | Purpose |
|-------|---------|
| 21    | Scan Bank dir for existing child at `op_bank_child_cursor` slot |
| 22    | Wait scan result |
| 23    | Rename old child to temp name (if found) |
| 24    | Wait rename complete, fall through to phase 20 (Scene writer entry) |
| 25    | Post-write: initiate deleteTree on old temp child |
| 26    | Wait deleteTree, then advance to phase 12 |

Modified phases:

| Phase | Change |
|-------|--------|
| 4-5   | Removed (no whole-tree delete) |
| 49    | Modified: open-or-create Bank dir (scan + reuse or mkdir) |
| 20    | Modified: route through phase 21 (pre-write scan) instead of directly entering Scene writer |
| 12    | Modified: route through phase 25 (post-write cleanup) before advancing cursor |

Unchanged phases: 0, 1-3, 6-11, 13-19, 45, 50, 80-84.

#### 8.3.7 State variables

New statics needed:

- `op_bank_old_child_found` — flag: an old child was renamed and needs
  post-write cleanup.
- `op_bank_old_child_name` — buffer holding the temp-renamed child's
  display name for `deleteTree` targeting.
- `op_bank_existing_dir_found` — flag: the Bank directory at this slot
  already existed (open vs. create path).

### 8.4 Test additions

After implementation, the following tests become meaningful:

- **Test 10 re-run (Bank Save after partial-failure load):** Load Bank
  with one bad child, change settings, save over same slot. Per-Scene
  replacement should save all valid children; the failed child (not in
  save mask) is untouched.
- **Bank Save to existing slot, subset mask:** Save a Bank with 4 of 8
  children selected. The other 4 must survive on card.
- **Bank Save to existing slot, different name:** Verify the Bank
  directory is renamed and children are preserved.
- **Power-loss recovery:** Pull power during a per-child write. The
  previously-saved children and the already-replaced children should
  survive. Only the in-progress child may be lost, but the old
  (temp-renamed) child of that slot is still on card as a fallback.

### 8.5 Implementation notes

Implemented 2026-08-27. All changes in `Core/Hardware/SD/filesystem.c`.

**Implementation approach: delete-then-write per child** — The §8.3.3
plan describes rename-write-delete (safest, with old directory preserved
as power-loss fallback). The actual implementation uses the simpler
delete-then-write approach: for each Scene in the save mask, delete the
existing Bank-local child directory, then write the new one. This avoids
the complexity of managing stale temp-renamed directories. The key safety
improvement over the old total-tree-delete is that non-masked children
are never touched.

#### New state variables

- `op_delete_slot_bank_local` (static uint8_t): When set to 1,
  `filesystem_deleteSlotDirectory_tick()` uses Bank-local 2-digit child
  matching (`storage_parseBankSceneFolder()`) instead of root 3-digit
  library matching (`storage_parseNumberedFolder()`). Cleared to 0 by
  `filesystem_deleteSlotDirectoryStart()`, set to 1 by
  `filesystem_deleteBankChildSlotDirectoryStart()`.

- `op_bank_existing_dir_found` (static uint8_t): Set during Bank
  directory scan (phases 51-52) to indicate whether an existing Bank
  directory was found matching `op_slot`. Used at phase 54 to decide
  whether a rename is needed.

#### New functions

- `filesystem_bankChildObjectMatchesSlot()`: Parses a directory entry
  using `storage_parseBankSceneFolder()` (2-digit "SS Name" format) and
  compares the extracted slot number against `op_delete_slot`. Returns 1
  on match, 0 otherwise. Used by the slot-delete scanner when
  `op_delete_slot_bank_local` is set.

- `filesystem_deleteBankChildSlotDirectoryStart(uint8_t child_slot)`:
  Entry point for deleting a Bank-local child directory by slot number.
  Calls `filesystem_deleteSlotDirectoryStart()` then sets
  `op_delete_slot_bank_local = 1u` so the scanner uses Bank-local
  matching.

#### Phase changes in `filesystem_saveBankDirectory_tick()`

**Phases 4, 51-55 (replaced old phases 4-5: total-tree-delete):**
- Phase 4: Opens "." to scan the Bank library for an existing directory
  matching `op_slot`. Initializes `op_bank_existing_dir_found = 0`.
- Phase 51: Waits for scan open, starts object finder iterator.
- Phase 52: Iterates directory entries using `storage_parseNumberedFolder()`.
  On match, captures the display name into `op_repair_old_name` and sets
  `op_bank_existing_dir_found = 1`. On scan complete, closes handle.
- Phase 53: Waits for close completion.
- Phase 54: Decision gate — if a match was found with a different name
  than the current Bank, renames the existing directory via
  `afatfs_renameObject_lfn()`. If no rename needed (same name or new
  slot), proceeds directly to phase 49.
- Phase 55: Waits for rename completion, then falls through to phase 49.
- Phase 49: Creates or opens the Bank directory via `afatfs_mkdir_lfn()`
  (create-or-open semantics — works for both new and existing slots).

**Phases 20-22 (per-child delete-before-write):**
- Phase 20: Calls `prepareBankSceneSaveSource()` to set up the Scene
  source data for the current child, then calls
  `filesystem_deleteBankChildSlotDirectoryStart()` to begin deleting
  any existing Bank-local child at the target slot number.
- Phase 21: Ticks `filesystem_deleteSlotDirectory_tick()` until the old
  child is deleted (returns `FS_ENUM_COMPLETE`). On failure, routes to
  the Bank Save error handler.
- Phase 22: Sets `op_bank_payload_active = 1u` and enters the Scene
  writer at phase 8, which writes the new child directory contents.

#### Removed: Bank-local Kit quarantine (phase 65)

The `FS_INTERNAL_OP_LOAD_BANK` branch at phase 62 previously routed to
phase 65 to rename `Kit Name` → `errKit Name` inside Bank-local Scenes
when a Kit failed validation. This quarantine rename was the root cause
of ErrS05: the LFN entries rewritten by the rename caused
`afatfs_deleteTree()` to abort mid-tree during the subsequent Bank Save.

Phase 65 is now bypassed — the `FS_INTERNAL_OP_LOAD_BANK` branch at
phase 62 skips directly to phase 72. Bank-local Kit failures are still
detected (the Scene's present-mask bit is cleared and an error overlay
is shown), but no rename occurs. This is safe because per-Scene safe
replacement means a failed Kit's Scene directory is simply not included
in the save mask, so it is never overwritten during Bank Save.

#### Test data changes

- `SD_CARD_TESTING/Kit/007 Chip/`: Deleted `chiph1.hat` to corrupt Kit
  007 for root-level Kit quarantine testing (6 files remain).
- `SD_CARD_TESTING/Bank/009 LoadTst/`: Restored for Bank Load +
  corrupted Kit re-testing. All 16 Scene children present (00-15).
  Scene 15 Pop has `Kit Pop/` with missing `poph1 6.hat` (Kit validation
  will fail on load). Scene 08 KitWool and Scene 03 Pop restored from
  Bank 012.

### 8.6 Hardware test: Bank Save stall (2026-08-27)

#### 8.6.1 Test procedure

Boot device, load Bank 009 LoadTst (16 Scene children, Scene 15 Pop has
a corrupt Kit — `Kit Pop/` is missing `poph1 6.hat`). Bank Load
completed as expected: sound applied, settings navigable. Changed a few
settings, then initiated Save:[Bank] to the same slot 009.

#### 8.6.2 Observed behavior

The Save menu's second row showed `009 LoadTst ...` — the "..." progress
indicator that `menu_beginLoadSaveCommand()` paints when a save request
is accepted. This text never cleared. Pressing mode buttons appeared to
change the underlying mode (button LEDs responded) but the LCD content
did not update. The device remained in this state until power-off.

#### 8.6.3 SD card evidence

SD card was removed and inspected. **No files were modified by the save
attempt:**

- `Bank/009 LoadTst/bankset.bcg`: 76 bytes, content unchanged —
  `active_scene=9, scene_mask_voice_edit=0200`. If the save had reached
  phase 7 (`afatfs_fopen_lfn(STORAGE_BANKSET_FILENAME, "w", ...)`), the
  file would have been truncated to 0 bytes immediately upon open with
  `"w"` mode. Its survival at 76 bytes proves the save stalled before
  phase 7.
- All 16 Scene child directories (00-15) remain unchanged — no children
  were deleted or rewritten by the per-child loop.
- `.hcnames`, `settings.cfg`: unchanged beyond the prior Bank Load's
  normal output.

Conclusion: the save stalled somewhere in phases 0-6 (HCNAMES preload,
`/Bank/` open+chdir, Bank directory scan for existing slot). No
filesystem modification occurred.

#### 8.6.4 Confirming the save was accepted

The "..." UI text proves the save request was accepted by the full
chain:

1. Menu (`menu.c:7183`): `preset_saveBank()` returned 1, so
   `commandAccepted = 1u` and `menu_beginLoadSaveCommand()` ran, which
   sets `menu_loadSaveCommandActive = 1u`, `menu_storageBusy = 1u`, and
   repaints the "..." overlay.
2. Preset (`presetManager.c:2250-2270`): `filesystem_ack()` ran (no-op
   since status was already IDLE after Bank Load's completion chain).
   `pm_status` was set to `PRESET_LOAD_IN_PROGRESS`, and
   `filesystem_requestSaveBank()` returned `true`.
3. Filesystem (`filesystem.c:21199-21220`): `filesystem_start()` passed
   the `status == FS_STATUS_BUSY` gate (status was IDLE), set
   `status = FS_STATUS_BUSY`, `current_op = FS_INTERNAL_OP_SAVE_BANK`,
   `op_phase = 0`, and reset all shared state variables.

If the save had been rejected at any of these levels, `pm_status` would
have been reset to `PRESET_IDLE` (presetManager.c:2272),
`commandAccepted` would have stayed 0, and `menu_beginLoadSaveCommand()`
would never have run — no "..." on screen.

#### 8.6.5 Why the menu froze

`menu_pollPresetStatus()` (menu.c:7907) checks
`preset_getStatus() != PRESET_UPDATE_READY` at the top of every poll.
While the save is in progress, `pm_status == PRESET_LOAD_IN_PROGRESS`,
so the poll returns early — no completion dispatch, no LCD repaint of
the Save page content, no `menu_finishLoadSaveCommand()`.

The mode-button response the user observed is consistent: mode buttons
are not fully gated by `menu_storageBusy`. Instead, page switches are
deferred into `menu_pendingPageSwitch` (menu.c:103-104) while
`menu_storageBusy == 1`, and normal completion processes them. Since
completion never arrived, the deferred switches accumulated but the LCD
stayed on the Save page showing "...".

#### 8.6.6 Code inspection: areas examined

Exhaustive review of the following was performed:

**Bank Save state machine** (`filesystem_saveBankDirectory_tick()`,
line 13989):
- Phase 0: validation, `filesystem_prepareResidentNamesCache()`,
  `afatfs_chdir(NULL)`, open `.hcnames` for reading.
- Phases 80-82: line-by-line `.hcnames` read, close, error gate.
- Phases 1-3: `afatfs_mkdir_lfn(STORAGE_ROOT_BANK, ...)` open/chdir
  into `/Bank/`, close handle.
- Phase 4 (NEW): wait for phase 3 close via `op_close_done`, then
  `afatfs_fopen(".", "r", on_file_opened)` to scan `/Bank/` for existing
  Bank directory.
- Phases 51-55 (NEW): directory scan, match, close, rename-if-needed.
- Phase 49: `afatfs_mkdir_lfn(op_save_bank_dir_display_name, ...)`.
- Phase 50: chdir into Bank dir.
- Phase 6: close Bank dir handle.
- Phases 7-10: open and write `bankset.bcg`.
- Phase 11: enter per-child loop.
- Phases 20-22 (NEW): per-child delete + Scene writer entry.

**Shared callbacks** (`on_file_opened`, `on_file_closed`):
- `on_file_opened` (line 1679): `op_file = file; op_file_ready = true;`.
- `on_file_closed` (line 1689): `op_close_done = true;`.
- Both are simple latchers with no conditional logic. No re-entrancy
  issue possible.

**asyncfatfs handle pool** (`AFATFS_MAX_OPEN_FILES = 5`):
- Traced handle allocation/free through phases 0-6. At any point in
  these phases, at most 1 handle is allocated. No handle leak path found
  in the Bank Save's own code.
- `afatfs_fopen(".", "r", ...)` path: `afatfs_createFileInternal()`
  sees `strcmp(name,".") == 0`, skips to `AFATFS_CREATEFILE_PHASE_SUCCESS`
  (asyncfatfs.c:4284-4286), copies `currentDirectory` data. This is a
  fast synchronous-completion path that doesn't require sector I/O.
- `afatfs_allocateFileHandle()` (asyncfatfs.c:3068-3076): scans
  `openFiles[0..4]` for `type == AFATFS_FILE_TYPE_NONE`. Returns NULL
  only when all 5 handles are allocated.
- `afatfs_fclose()` (asyncfatfs.c:5282-5287): sets `type = NONE` (frees
  handle), then calls callback. Handle is freed before callback fires.

**Phase retry semantics:**
- When `afatfs_fopen` / `afatfs_mkdir_lfn` / `afatfs_fopen_lfn` returns
  `false` (no handle available), the pattern is `if (!func()) return;`
  — the phase stays the same and retries next tick. The function calls
  `callback(NULL)` synchronously before returning `false`, setting
  `op_file_ready = true` and `op_file = NULL`. Next tick, the phase
  re-enters, sets `op_file_ready = false` (clearing the stale callback
  result), and retries. This is a correct retry loop — but it spins
  indefinitely if handles are permanently exhausted.
- When `afatfs_fclose` returns `false` (file busy), the phase stays the
  same and retries. This is also a correct retry loop.

**Completion chain** (`filesystem_finish()` → `filesystem_complete()`):
- `filesystem_finish(FS_STATUS_DONE)` transitions to
  `FS_INTERNAL_OP_FLUSH_FINISH` (line 3446), which pumps `afatfs_sync()`
  until all dirty sectors are flushed.
- If `op_library_index_rebuild_pending`, calls
  `filesystem_startLibraryIndexRebuild()` which sets
  `status = FS_STATUS_IDLE`, `current_op = FS_INTERNAL_OP_NONE`, then
  starts a new scan (e.g. `filesystem_requestScanBanks()`).
- Scan completion calls `filesystem_completeLibraryIndexRebuild()`:
  `status = final_status` (DONE or ERROR),
  `current_op = FS_INTERNAL_OP_NONE`, then calls parked callback.
- Parked callback is `on_bank_save_complete` →
  `preset_completeFilesystemOp(PRESET_OP_BANK_SAVE)` →
  `filesystem_ack()` → `pm_status = PRESET_UPDATE_READY`.

**`op_bank_payload_active` flag:**
- Bank Load sets it to 1 at line 10998 (entering Scene loader), clears
  it to 0 at line 10359 (returning to Bank loop). Checked: the Scene
  loader has zero direct calls to `filesystem_finish(FS_STATUS_ERROR)` —
  all its errors go through `op_close_status = ERROR; op_phase = 62;`
  which routes through the cleanup exit that clears the flag.
- Bank Save sets it to 1 at phase 22 (line 14597), clears it at phase 37
  of the Scene writer (line 15207). Same pattern.
- `filesystem_start()` did NOT previously reset this flag (defensive gap
  now fixed — see §8.6.8).

**Settings writer interaction:**
- `filesystem_settingsWriterSchedule_tick()` (line 20271) only starts
  when `status == FS_STATUS_IDLE` and `fs_settings_dirty` is true. It
  calls `filesystem_start()` which sets `status = BUSY`. If a settings
  write were in progress when the user triggered Bank Save,
  `preset_requestBankSave()` would call `filesystem_ack()` (no-op on
  BUSY) then `filesystem_start()` would reject (returns false). But this
  contradicts the evidence that the "..." appeared — the save WAS
  accepted. So the settings writer was not running at the time.
- Settings debounce is 1000ms (`config.h:282`). The user changed
  settings and then navigated to Save:[Bank] and confirmed — the
  debounce likely expired and the settings write completed before the
  Bank Save was triggered.

**Preset manager request flow:**
- `preset_saveBank()` (presetManager.c:2237): gets `source_scene` from
  `scene_getActiveIndex()`, calls `filesystem_ack()`, sets
  `pm_status = PRESET_LOAD_IN_PROGRESS`, calls
  `filesystem_requestSaveBank()`.
- `filesystem_requestSaveBank()` (filesystem.c:22192): validates inputs,
  intersects caller mask with `bank_scenePresentMask()`, calls
  `filesystem_start()`. If start fails, returns false, and
  `preset_saveBank()` resets `pm_status = PRESET_IDLE` and returns 0.

#### 8.6.7 Assessment: no definitive code bug found

Every phase transition, callback sequence, handle allocation/free path,
and shared variable access was traced through the Bank Save's
pre-bankset.bcg phases (0 through 6, including the new phases 4/51-55).
All paths appear correct on paper:

- Each phase either advances to the next or retries (returns) with the
  same phase, waiting for an async callback.
- Callbacks are simple latchers that set a flag and nothing else.
- Handle allocation and freeing are paired correctly.
- `op_file_ready`, `op_file`, `op_close_done` are reset before each use
  point.
- The Bank Load's completion chain (flush → index rebuild → callback →
  ack) leaves `status = IDLE` and all shared state clean.

The stall remains unexplained by code inspection alone. Without
on-hardware phase tracking, the exact stalling phase cannot be
identified. The three most likely hypotheses:

**Hypothesis 1 — Handle pool exhaustion (most likely).**
`AFATFS_MAX_OPEN_FILES` is 5. If the Bank Load's completion chain (index
rebuild → Bank scan) leaked a handle — for example, a Bank scan
callback returning without closing its directory handle — the pool
could be reduced to 4 or fewer free handles. The Bank Save's phases
0-4 each allocate exactly 1 handle, but if the `"."` fast path at
phase 4 happens to need a second handle (implementation detail of
`afatfs_createFileInternal()`), the pool could be exhausted. The retry
loop at `if (!afatfs_fopen(".", "r", on_file_opened)) return;` would
then spin indefinitely with `op_phase` stuck at 4. The synchronous
`callback(NULL)` on failure sets `op_file_ready = true` and
`op_file = NULL`, but next tick `op_file_ready` is cleared and fopen
retries — never advancing, never failing cleanly.

**Hypothesis 2 — Stale `afatfs.currentDirectory` state.**
`afatfs_chdir(NULL)` at phase 0 sets `currentDirectory` to the root
cluster. Phase 1's `afatfs_mkdir_lfn(STORAGE_ROOT_BANK, ...)` creates
or opens `/Bank/` relative to root. Phase 2's `afatfs_chdir(op_kit_root_dir)`
sets `currentDirectory` to `/Bank/`. Phase 3 closes the `/Bank/` handle
but does NOT change `currentDirectory`. Phase 4 then opens `"."` which
snapshots `currentDirectory` — this should still be `/Bank/`. If
however `currentDirectory` was corrupted or overwritten between
phase 3's close and phase 4's open (e.g. by an asyncfatfs-internal
operation), the snapshot would reference invalid cluster data. The
resulting handle would be usable but its directory entries might not
parse, causing the scan at phase 52 to spin through garbage entries or
stall waiting for sector I/O that never completes.

**Hypothesis 3 — `afatfs_findNextObject()` infinite loop at phase 52.**
The Bank directory `009 LoadTst/` contains 16 Scene children plus
`bankset.bcg`. Phase 52 iterates with `afatfs_findNextObject()`. If
the underlying FAT directory chain has a cycle (cluster chain loops
back on itself, a known corruption mode), the iterator would return
`AFATFS_OPERATION_IN_PROGRESS` indefinitely as it reads the same
sectors in a loop. `op_phase` stays at 52, the stall detector
eventually fires.

All three hypotheses produce the observed behavior: `op_phase` stuck
at a fixed value, no file modifications, no completion callback.

#### 8.6.8 Remediation: stall abort + defensive cleanup

Two code changes were made in the same `filesystem.c`:

**1. Bank Save entry stall abort (line 14000-14017).**

The existing phase stall detector at the top of
`filesystem_saveBankDirectory_tick()` uses `filesystem_pollPhaseStall()`
with a 20,000-tick threshold. When the threshold fires it was
observe-only: record one `AUTOSAVE_TRACE_STAGE_PHASE_STALL` trace entry,
then continue. The operation remained in `FS_STATUS_BUSY` indefinitely.
The menu stayed frozen on "..." with no user feedback and no recovery
path short of power-cycling.

The detector now additionally:
- Writes a named error code `BkSt NN` (hex phase number) via
  `filesystem_makeNamedErrorCode("BkSt", op_phase)`.
- Calls `filesystem_finish(FS_STATUS_ERROR)` to terminate the operation.

This is safe for Bank Save entry phases (0-49) because they perform
only open/chdir/close/scan work with no non-cancellable
`afatfs_deleteTree()` handle ownership. (The delete-slot stall observer
intentionally does NOT abort because its `DELETE_MATCH` phase owns a
native deleteTree handle that asyncfatfs may still be using.)

On abort, the completion chain fires:
`filesystem_finish(ERROR)` → `filesystem_complete()` → sets
`status = FS_STATUS_ERROR`, calls `on_bank_save_complete` →
`preset_completeFilesystemOp(PRESET_OP_BANK_SAVE)` →
`filesystem_ack()` + `pm_status = PRESET_UPDATE_READY`. Then
`menu_pollPresetStatus()` dispatches `PRESET_OP_BANK_SAVE` →
`menu_showFilesystemErrorOverlay()` shows the `BkSt NN` code on
screen → `menu_finishLoadSaveCommand()` clears `menu_storageBusy` and
`menu_loadSaveCommandActive`, restoring normal navigation.

The hex phase in the error code identifies the stalling phase:

| Error code | Stalled phase | Meaning |
|------------|---------------|---------|
| `BkSt00`   | 0             | Validation or HCNAMES open (`afatfs_chdir(NULL)` or `afatfs_fopen_lfn` of `.hcnames` returning false) |
| `BkSt50`   | 80 (0x50)     | Waiting for `.hcnames` open callback |
| `BkSt51`   | 81 (0x51)     | Reading `.hcnames` lines (read stall or I/O error loop) |
| `BkSt52`   | 82 (0x52)     | Waiting for `.hcnames` close |
| `BkSt01`   | 1             | `afatfs_mkdir_lfn(STORAGE_ROOT_BANK)` returning false (handle exhaustion) |
| `BkSt02`   | 2             | Waiting for `/Bank/` open callback |
| `BkSt03`   | 3             | `afatfs_fclose(op_kit_root_dir)` returning false (handle busy) |
| `BkSt04`   | 4             | `afatfs_fopen(".", "r", ...)` returning false (handle exhaustion) or `op_close_done` never becoming true |
| `BkSt33`   | 51 (0x33)     | Waiting for `"."` open callback |
| `BkSt34`   | 52 (0x34)     | `afatfs_findNextObject()` never completing (directory chain corruption or I/O stall) |
| `BkSt35`   | 53 (0x35)     | `afatfs_fclose(op_delete_slot_dir)` returning false |
| `BkSt36`   | 54 (0x36)     | `op_close_done` never becoming true after phase 53 close |
| `BkSt37`   | 55 (0x37)     | `op_rename_done` never becoming true |
| `BkSt31`   | 49 (0x31)     | `afatfs_mkdir_lfn(op_save_bank_dir_display_name)` returning false |
| `BkSt32`   | 50 (0x32)     | Waiting for Bank dir open callback |
| `BkSt06`   | 6             | `afatfs_fclose(op_kit_slot_dir)` returning false |

**2. Defensive `op_bank_payload_active` reset (line 21264).**

`filesystem_start()` now clears `op_bank_payload_active = 0u` alongside
the other operation-shared state resets. Previously this flag was only
cleared by its owning state machines (Bank Load at line 10359, Bank
Save's Scene writer at line 15207). If a previous Bank Save's Scene
writer encountered an error and called `filesystem_finish(FS_STATUS_ERROR)`
directly (bypassing the normal `op_bank_payload_active = 0u;
op_phase = 12u;` return at phase 37), the flag would remain 1. On the
next Bank Save request, `filesystem_start()` resets `op_phase = 0` but
the stale flag would cause `filesystem_saveBankDirectory_tick()` to
dispatch immediately to `filesystem_saveSceneDirectory_tick()` with
`op_phase = 0` — the Scene writer's own phase 0, not the Bank Save's.
This would corrupt the operation. The defensive reset closes this gap.

Note: this gap is NOT the cause of the observed stall. The observed
stall was the first Bank Save after a successful Bank Load, and the
Bank Load confirmed that `op_bank_payload_active` was properly cleared
(the Scene loader's only error path goes through `op_close_status =
FS_STATUS_ERROR; op_phase = 62;` → cleanup exit at line 10347 which
clears the flag). The reset is a defense-in-depth against a future
scenario where a Bank Save's Scene writer errors during the per-child
loop.

#### 8.6.9 Forward plan: next hardware test

Flash the updated build and repeat the test:

1. **Reproduce the stall.** Boot, load Bank 009, change settings, save
   to slot 009. If the save stalls again:
   - The stall abort fires after ~20 seconds (20,000 ticks at 1kHz).
   - The menu shows `ERR BkSt NN`.
   - Record the exact error code. This identifies the stalling phase
     per the table in §8.6.8.
   - Consult the phase-specific diagnosis below for next steps.

2. **If `BkSt04` (phase 4: `"."` open):** Confirms handle pool
   exhaustion. The Bank Load or its index rebuild leaked a handle. Next
   step: add a handle-pool occupancy diagnostic at the entry of phase 4
   (count `openFiles[]` entries with `type != NONE`) and record the
   count in the named error code.

3. **If `BkSt34` (phase 52: scan loop):** Confirms a FAT directory
   chain issue in `/Bank/`. Next step: use a PC-based `fsck`/`chkdsk`
   to inspect the Bank directory for cluster chain cycles or corrupted
   entries. Alternatively, reformat the test card and rebuild the test
   data.

4. **If `BkSt00` (phase 0: root chdir or hcnames open):** The stall is
   in the very first phase — `afatfs_chdir(NULL)` returning false
   (currentDirectory busy) or `afatfs_fopen_lfn` returning false
   (handle exhaustion). Next step: check whether the settings writer or
   autosave trace flush completed between Load and Save — if not, the
   facade may have been blocked by a still-completing autonomous write.

5. **If no stall occurs (save completes):** The stall was a one-time
   condition (likely a timing-dependent race between the Bank Load's
   index rebuild completion and the settings writer). Continue with the
   remaining §8 test suite:
   - Root Kit quarantine test (Kit 007 Chip, corrupted with missing
     `chiph1.hat`).
   - Bank Save to existing slot with subset mask (select 4 of 16
     scenes, verify other 12 survive on card).
   - Bank Save to existing slot with different name (verify rename +
     child preservation).

6. **After root cause is identified:** If the fix requires additional
   code changes, document the root cause in a new §8.7 section and
   re-test.

### 8.7 Stall evidence policy

#### 8.7.1 Principle

No filesystem operation may stall and then recover silently with no
evidence produced. A stall that times out and recovers without recording
how to diagnose and correct the underlying cause is unacceptable. Every
stall detection site must:

1. **Identify itself.** Write a named error code
   (`filesystem_makeNamedErrorCode()`) that encodes the stall site and
   the stalled phase number. The generic auto-error fallback in
   `filesystem_complete()` is insufficient — it identifies the operation
   and terminal phase, not the fact that a stall was detected or what
   phase stalled.
2. **Record a trace entry.** Call `autosaveTrace_record()` with the
   `AUTOSAVE_TRACE_STAGE_PHASE_STALL` stage so the trace buffer
   contains a machine-readable stall record even if the named error
   code is overwritten by a subsequent error in the abort path.
3. **Abort or escalate.** If the operation can be safely aborted (no
   non-cancellable native handle ownership), call
   `filesystem_finish(FS_STATUS_ERROR)` so the menu shows the error
   overlay and the user can recover. If abort is unsafe (e.g.
   `afatfs_deleteTree()` owns a handle), the detector must still
   produce items 1 and 2 and must document why it cannot abort.

#### 8.7.2 Audit of current stall detection (2026-08-27)

**State machines WITH stall detection:**

| State machine | Detection site | Threshold | Named code | Aborts | Notes |
|---------------|---------------|-----------|------------|--------|-------|
| Bank Save entry (`filesystem_saveBankDirectory_tick`) | Pre-dispatch, line 13989 | 20,000 | `BkSt NN` | Yes | Added §8.6.8. Safe: pre-bankset phases have no native delete ownership. |
| Delete-slot (`filesystem_deleteSlotDirectory_tick`) | Top of function, line 13400 | 50,000 | `TDel NN` or `TOut NN` | Partial | `DELETE_MATCH`/`WAIT_SCAN`/`WAIT_CLOSE_SCAN`: observe-only, cannot abort native `deleteTree()`. `OPEN_SCAN`/`SCAN_NEXT`: sets `op_delete_slot_scan_error`, routes to `CLOSE_SCAN` → `ERROR`. Named code written at detection. |
| Autosave drain (`filesystem_autosaveParameterDrain_tick`) | Top of function, line 5748 | 30,000 | `DrSt NN` | Yes | Autonomous background operation. Routes to `filesystem_autosaveWriterFinishError()` which closes handles then calls `filesystem_finish(FS_STATUS_ERROR)`. Named code `DrSt NN` added in this build (was previously absent — relied on generic auto-code fallback). |

**State machines WITHOUT stall detection:**

| State machine | Function | User-facing | Risk |
|---------------|----------|-------------|------|
| Kit Save | `filesystem_saveKitDirectory_tick` | Yes — menu "..." | An `afatfs_fopen`/`fclose`/`chdir` returning false indefinitely hangs the menu. |
| Scene Save | `filesystem_saveSceneDirectory_tick` | Yes — menu "..." (standalone) or Bank Save "..." (delegated) | Same as Kit Save. When delegated from Bank Save, the Bank entry stall detector does not cover phases inside the Scene writer because `op_bank_payload_active` bypasses it. |
| Kit Load | `filesystem_loadKitDirectory_tick` | Yes — menu "..." | Same retry-loop risk. |
| Scene Load | `filesystem_loadSceneDirectory_tick` | Yes — menu "..." (standalone) or Bank Load "..." (delegated) | Same. Bank Load delegates Scene loads but has no stall detector of its own. |
| Bank Load | `filesystem_loadBankDirectory_tick` | Yes — menu "..." | Same retry-loop risk for Bank-container phases and HCNAMES phases. |
| Settings write | `filesystem_saveGlobals_tick` | No — autonomous | Settings writer is background. Its completion callback (`filesystem_settingsWriterCompleted`) is internal; the menu never shows its errors. A stall here blocks the facade from accepting foreground operations. |
| HCNAMES update | Various internal ops | Yes — indirectly | Register update phases after a Scene/Kit Save can stall on `.hcnames` open/read/write. No dedicated detector. |
| Flush finish | `filesystem_flushFinish_tick` | Yes — indirectly | `afatfs_sync()` returning false indefinitely keeps the operation in FLUSH_FINISH. No detector. |
| Library index rebuild | Various scan operations | Yes — indirectly | Scan-rebuild launched from `filesystem_startLibraryIndexRebuild()` runs as a new `filesystem_start()` operation. No detector beyond what the scan itself has. |

#### 8.7.3 Remediation applied in this build

1. **Bank Save entry:** Stall detector upgraded from observe-only to
   abort + named code `BkSt NN` (§8.6.8 item 1).
2. **Autosave drain:** Named error code `DrSt NN` added at the existing
   stall detection point (line 5759). Previously produced only a trace
   record and relied on the generic auto-code fallback in
   `filesystem_complete()`.

#### 8.7.4 Remaining gaps (future work)

The six user-facing foreground state machines (Kit Save, Scene Save,
Kit Load, Scene Load, Bank Load, Settings write) plus Flush Finish have
no stall detection. Any phase that calls `afatfs_fopen` /
`afatfs_fclose` / `afatfs_chdir` / `afatfs_mkdir_lfn` in a retry loop
(`if (!func()) return;`) can stall indefinitely if the underlying
asyncfatfs operation never completes or a handle is permanently
unavailable.

Adding `filesystem_pollPhaseStall()` with named error codes and abort
to these state machines should follow the same pattern as the Bank Save
entry fix: a pre-dispatch stall check with a threshold proportional to
the operation's expected duration. The abort is safe whenever the
stalling phase does not own a non-cancellable native handle (which is
true for all open/close/chdir/read/write phases — only
`afatfs_deleteTree()` is non-cancellable).

Priority order for adding detectors (by user-visible impact):
1. Bank Load entry — same pattern as Bank Save entry
2. Scene Save (standalone) — user-triggered foreground save
3. Kit Save — user-triggered foreground save
4. Scene Load / Kit Load — user-triggered foreground loads
5. Settings write — autonomous, but a stall blocks the facade
6. Flush Finish — shared completion gate for all operations
7. HCNAMES update — post-save register maintenance

The Bank Save's delegated Scene writer (entered via `op_bank_payload_active`)
is partially covered: the Bank Save entry detector runs only when
`op_bank_payload_active == 0`, so a stall inside the delegated Scene
writer is invisible to it. A separate detector inside
`filesystem_saveSceneDirectory_tick()` (or a secondary detector in the
Bank Save wrapper that also fires when `op_bank_payload_active == 1`)
is needed for complete coverage.

### 8.8 Stall evidence implementation (§8.7) — implementation notes

Implemented 2026-08-27. All changes in `Core/Hardware/SD/filesystem.c`
and `Core/Bank/Scene/AutosaveTrace.h`.

#### 8.8.1 Trace site mask widening

The `AUTOSAVE_TRACE_PHASE_STALL_SITE_MASK` was widened from 3 bits
(0x07, max 8 values) to 4 bits (0x0F, max 16 values) in
`AutosaveTrace.h` to accommodate the full set of new observer sites.
The `AUTOSAVE_TRACE_PHASE_STALL_FLAG_IN_NATIVE_DELETE` flag was moved
from bit 3 (`1u << 3u`) to bit 4 (`1u << 4u`) to make room. This flag
is consumed only by the delete-slot stall detector
(`filesystem_deleteSlotDirectory_tick()`) at one site in filesystem.c
— the bit-position change is picked up automatically through the macro.

New trace site constants added:

| Constant | Value | Observer |
|----------|-------|----------|
| `AUTOSAVE_TRACE_PHASE_STALL_SITE_KIT_SAVE` | 3 | `filesystem_saveKitDirectory_tick()` |
| `AUTOSAVE_TRACE_PHASE_STALL_SITE_SCENE_SAVE` | 4 | `filesystem_saveSceneDirectory_tick()` |
| `AUTOSAVE_TRACE_PHASE_STALL_SITE_KIT_LOAD` | 5 | `filesystem_loadKitDirectory_tick()` |
| `AUTOSAVE_TRACE_PHASE_STALL_SITE_SCENE_LOAD` | 6 | `filesystem_loadSceneDirectory_tick()` |
| `AUTOSAVE_TRACE_PHASE_STALL_SITE_BANK_LOAD_ENTRY` | 7 | `filesystem_loadBankDirectory_tick()` |
| `AUTOSAVE_TRACE_PHASE_STALL_SITE_SETTINGS` | 8 | `filesystem_saveGlobals_tick()` |
| `AUTOSAVE_TRACE_PHASE_STALL_SITE_FLUSH` | 9 | `filesystem_flushFinish_tick()` |

#### 8.8.2 New stall detectors

Each detector follows the established Bank Save entry pattern:
`filesystem_pollPhaseStall()` with a threshold, then on fire: trace
record, named error code, `filesystem_finish(FS_STATUS_ERROR)` abort.

**Kit Save** (`filesystem_saveKitDirectory_tick()`):
- Static variables: `op_kit_save_stall_last_phase`, `op_kit_save_stall_ticks`
- Threshold: 20,000 ticks
- Named error code: `KtSv NN` (hex phase number)
- Trace site: `PHASE_STALL_SITE_KIT_SAVE`
- Abort: safe — Kit Save phases perform open/chdir/close/write with no
  non-cancellable native handle ownership
- Counter reset: `filesystem_requestSaveKitDirectory()`

**Scene Save** (`filesystem_saveSceneDirectory_tick()`):
- Static variables: `op_scene_save_stall_last_phase`, `op_scene_save_stall_ticks`
- Threshold: 20,000 ticks
- Named error code: `ScSv NN`
- Trace site: `PHASE_STALL_SITE_SCENE_SAVE`
- Abort: safe — same reasoning as Kit Save
- Counter reset: `filesystem_requestSaveSceneDirectory()` (standalone),
  `filesystem_requestSaveBank()` (delegated via `op_bank_payload_active`)
- Coverage note: this detector covers both standalone root Scene Saves
  and Bank Save delegated Scene writes. The Bank Save entry detector
  (`BkSt`) does not fire when `op_bank_payload_active == 1`, so this
  Scene Save detector is the sole stall coverage for the per-child
  Scene writer phases during Bank Save. This closes the §8.7.4
  delegated-Scene coverage gap.

**Kit Load** (`filesystem_loadKitDirectory_tick()`):
- Static variables: `op_kit_load_stall_last_phase`, `op_kit_load_stall_ticks`
- Threshold: 20,000 ticks
- Named error code: `KtLd NN`
- Trace site: `PHASE_STALL_SITE_KIT_LOAD`
- Abort: safe — Kit Load phases perform open/chdir/close/read with no
  non-cancellable native handle ownership
- Counter reset: `filesystem_requestLoadKitForScenes()`,
  `filesystem_requestLoadKitMorphForScenes()` (both share
  `filesystem_loadKitDirectory_tick()`)

**Scene Load** (`filesystem_loadSceneDirectory_tick()`):
- Static variables: `op_scene_load_stall_last_phase`, `op_scene_load_stall_ticks`
- Threshold: 20,000 ticks
- Named error code: `ScLd NN`
- Trace site: `PHASE_STALL_SITE_SCENE_LOAD`
- Abort: safe — same reasoning as Kit Load
- Counter reset: `filesystem_requestLoadSceneForScenes()` (standalone),
  `filesystem_requestLoadBank()` (delegated via `op_bank_payload_active`)
- Coverage note: this detector covers both standalone root Scene Loads
  and Bank Load delegated Scene reads, same pattern as the Scene Save
  detector above. The Bank Load entry detector (`BkLd`) does not fire
  when `op_bank_payload_active == 1`.

**Bank Load entry** (`filesystem_loadBankDirectory_tick()`):
- Static variables: `op_bank_load_entry_stall_last_phase`,
  `op_bank_load_entry_stall_ticks`
- Threshold: 20,000 ticks
- Named error code: `BkLd NN`
- Trace site: `PHASE_STALL_SITE_BANK_LOAD_ENTRY`
- Abort: safe — Bank Load container phases (0..18) perform
  open/chdir/close/read/scan with no non-cancellable native handle
  ownership
- Counter reset: `filesystem_requestLoadBank()`
- Dispatch: placed after the `op_bank_payload_active` pre-dispatch
  return, so only container phases are observed. Delegated Scene payload
  is covered by the Scene Load detector above.

**Settings write** (`filesystem_saveGlobals_tick()`):
- Static variables: `op_settings_stall_last_phase`, `op_settings_stall_ticks`
- Threshold: 30,000 ticks (higher because settings serializes a
  multi-line file and legitimate writes can take longer)
- Named error code: `StWr NN`
- Trace site: `PHASE_STALL_SITE_SETTINGS`
- Abort: safe — settings write performs open/write/close with no
  non-cancellable native handle ownership. The abort calls
  `filesystem_finish(ERROR)` which routes through
  `filesystem_settingsWriterCompleted()`, freeing the facade
- Counter reset: `filesystem_settingsWriterSchedule_tick()` (before
  `filesystem_start()` call). Reset is unconditional because if start
  fails the detector never runs
- Note: the settings writer is autonomous — its errors do not produce
  a visible overlay on screen. But a stall here blocks the facade
  (status == FS_STATUS_BUSY) indefinitely, preventing all foreground
  Load/Save operations until power cycle

**Flush Finish** (`filesystem_flushFinish_tick()`):
- Static variables: `op_flush_stall_last_phase`, `op_flush_stall_ticks`
  (declared before `filesystem_finish()` because that function resets
  them when entering FLUSH_FINISH mode)
- Threshold: 50,000 ticks (higher because sync legitimately pumps many
  small sector writes and a slow card may take several seconds)
- Named error code: `Flsh 00` (op_phase is always 0 during flush)
- Trace site: `PHASE_STALL_SITE_FLUSH`
- Abort: calls `filesystem_complete(FS_STATUS_ERROR)` directly (not
  `filesystem_finish()`) to bypass the normal flush-then-complete path
  that would re-enter FLUSH_FINISH
- Counter reset: in `filesystem_finish()` at the FLUSH_FINISH entry
  point (line 3467-3468). op_phase is always 0 during flush, so
  `last_phase` is set to 0xFF to force a "changed" reading on the first
  poll
- Note: this is the shared completion gate for ALL successful
  operations. A stall here keeps the menu frozen on "..." for any
  Save/Load operation

#### 8.8.3 Counter reset pattern

Each detector requires a pair of static variables: `_last_phase`
(uint8_t) and `_stall_ticks` (uint32_t). These are reset in the
request function that starts the operation:

| Request function | Resets |
|-----------------|--------|
| `filesystem_requestSaveKitDirectory()` | Kit Save (`op_kit_save_stall_*`) |
| `filesystem_requestSaveSceneDirectory()` | Scene Save (`op_scene_save_stall_*`) |
| `filesystem_requestSaveBank()` | Bank Save entry (`op_bank_save_entry_*`, existing), Scene Save (`op_scene_save_stall_*`, new) |
| `filesystem_requestLoadKitForScenes()` | Kit Load (`op_kit_load_stall_*`) |
| `filesystem_requestLoadKitMorphForScenes()` | Kit Load (`op_kit_load_stall_*`) |
| `filesystem_requestLoadSceneForScenes()` | Scene Load (`op_scene_load_stall_*`) |
| `filesystem_requestLoadBank()` | Bank Load entry (`op_bank_load_entry_stall_*`), Scene Load (`op_scene_load_stall_*`) |
| `filesystem_settingsWriterSchedule_tick()` | Settings (`op_settings_stall_*`) |
| `filesystem_finish()` (FLUSH_FINISH entry) | Flush (`op_flush_stall_*`) |

The reset pattern is: `last_phase = 0xFF; ticks = 0;`. Setting
`last_phase` to 0xFF (a value `op_phase` can never hold) forces
`filesystem_pollPhaseStall()` to see a "changed" phase on its first
call, starting the counter from zero.

#### 8.8.4 Named error code summary

| Code | State machine | Meaning |
|------|---------------|---------|
| `BkSt NN` | Bank Save entry | Phase NN stalled (existing, §8.6.8) |
| `DrSt NN` | Autosave drain | Phase NN stalled (existing, §8.7.3) |
| `TDel NN` | Delete-slot (native delete) | deleteTree subphase stalled (existing) |
| `TOut NN` | Delete-slot (scan/close) | Phase NN stalled (existing) |
| `KtSv NN` | Kit Save | Phase NN stalled (new) |
| `ScSv NN` | Scene Save | Phase NN stalled (new) |
| `KtLd NN` | Kit Load | Phase NN stalled (new) |
| `ScLd NN` | Scene Load | Phase NN stalled (new) |
| `BkLd NN` | Bank Load entry | Phase NN stalled (new) |
| `StWr NN` | Settings write | Phase NN stalled (new) |
| `Flsh 00` | Flush Finish | afatfs_sync() stalled (new) |

All new named error codes are written via
`filesystem_makeNamedErrorCode()` before `filesystem_finish(ERROR)`, so
`filesystem_complete()` does not overwrite them with the generic
auto-error fallback.

#### 8.8.5 Remaining uncovered operations

The following operations were not given dedicated stall detectors in
this build:

- **HCNAMES update** (`filesystem_writeResidentNames_tick()`,
  `filesystem_residentNames_tick()`): Post-save name register
  maintenance. These run as separate `filesystem_start()` operations
  launched from the library index rebuild chain. The Flush Finish
  detector provides downstream coverage, but a stall during the
  HCNAMES read/write phases would not be detected until the 50,000-tick
  flush threshold. A dedicated HCNAMES detector would provide faster
  diagnosis but was deferred because HCNAMES operations are short and
  have not been observed to stall.

- **Library index rebuild scans** (`filesystem_scanKits_tick()`,
  `filesystem_scanScenes_tick()`, `filesystem_scanBanks_tick()`,
  `filesystem_scanBankScenes_tick()`, `filesystem_scanInstruments_tick()`):
  These run as separate `filesystem_start()` operations. Same flush-
  downstream coverage argument applies. Deferred.

#### 8.8.6 Build

Compiled clean. Text size: 380,380 bytes (up from 379,780 — +600 bytes
for 7 new stall detectors, 14 new static variables, and the trace site
constant additions).

#### Files changed

| File | Change |
|------|--------|
| `Core/Hardware/SD/filesystem.c` | Per-Scene safe-replace Bank Save, Bank-local child delete, quarantine removal, stall abort diagnostics (`BkSt`, `KtSv`, `ScSv`, `KtLd`, `ScLd`, `BkLd`, `StWr`, `Flsh`), defensive `op_bank_payload_active` reset, autosave drain named error code (`DrSt`), stall counter resets in all request functions |
| `Core/Bank/Scene/AutosaveTrace.h` | Widened PHASE_STALL site mask from 3 to 4 bits, moved native-delete flag from bit 3 to bit 4, added 7 new site constants |
| `SD_CARD_TESTING/Kit/007 Chip/chiph1.hat` | Deleted (Kit corruption test) |
| `SD_CARD_TESTING/Bank/009 LoadTst/` | Restored 08 KitWool, 03 Pop; renamed errKit→Kit in 15 Pop |
