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
