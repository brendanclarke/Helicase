# Phase B + B2 — `.hcnames` Atomic Safe-Write and Refreshed Flag

## Goal

Make `.hcnames` writes crash-safe (Phase B), then extend the file format and
writer to carry a per-row refreshed flag that the future autosave boot reader
needs (Phase B2). Phase B is a prerequisite for B2: B2's new autosave-writer
HCNAMES update must use the safe-write pattern from the start.

---

## Phase B: `.hcnames` Atomic Safe-Write

### What

Replace all direct `.hcnames` truncate-writes (`"w"` mode) with a temp-file
safe-write: write `.hcnamtmp` → close → sync → remove `.hcnames` → rename
`.hcnamtmp` → `.hcnames` → finish (final sync via `filesystem_finish`).

Add a boot recovery prelude that discovers and resolves a leftover `.hcnamtmp`
before any `.hcnames` read.

### Why

A power loss during the current `"w"` mode write leaves a partially written
or zero-length `.hcnames`. The ensure-autosave path treats a missing/invalid
`.hcnames` as a hard error and refuses to create `.hcprms` files. The
settings.cfg writer already solved this problem (Session 057); `.hcnames`
should follow the same pattern.

### What changes

Four write paths currently open `.hcnames` with `"w"` mode:

1. **Boot full-write** — `filesystem_writeResidentNames_tick()` (line 4779).
   Phase 0 probes, then opens `.hcnames "w"`. Phases 1-2 stream 129 rows.
   Phase 3 closes + finishes.

2. **Runtime targeted-update** — `filesystem_residentNames_tick()` (line 5593).
   Phases 0-2 read the existing file into the mirror. Phase 3 closes.
   Phases 3-4 overlay changed rows, reopen `.hcnames "w"`. Phase 5 streams
   all 129 rows from the mirror. Phase 6 closes + finishes.

3. **Bank Load HCNAMES write** — Bank Load phase 83 (line 12500). Opens
   `.hcnames "w"` (after the Phase A chdir fix), writes merged register,
   closes.

4. **Bank Save HCNAMES write** — Bank Save phase 83 (line 15928). Opens
   `.hcnames "w"`, writes merged register, closes.

All four need the same conversion: write to `.hcnamtmp` instead of
`.hcnames`, then sync → remove-old → rename → finish.

### Implementation approach

**New constant**: `FS_RESIDENT_NAMES_TEMP_FILENAME` = `".hcnamtmp"`.

**Shared tail phases**: Extract the close → sync → remove → rename → finish
sequence into a set of shared phases (or a shared suffix in each state
machine), following the `saveGlobals_tick` pattern:

| Step | What | Existing model |
|------|------|----------------|
| Close temp | `afatfs_fclose(op_file, on_file_closed)` | saveGlobals phase 3-4 |
| Sync temp | `afatfs_sync()` — durability barrier | saveGlobals phase 5 |
| Remove old | `afatfs_removeObjects_lfn(".hcnames", CASE_INSENSITIVE, FILES_ONLY)` | saveGlobals phase 6-7 |
| Rename | `afatfs_renameObject_lfn(".hcnamtmp", ".hcnames", CASE_INSENSITIVE)` | saveGlobals phase 8-9 |
| Finish | `filesystem_finish(FS_STATUS_DONE)` — final sync via flush gate | saveGlobals phase 10 |

All three LFN primitives (`afatfs_fopen_lfn`, `afatfs_removeObjects_lfn`,
`afatfs_renameObject_lfn`) and `afatfs_sync` are already available. No
asyncfatfs changes needed.

**Per write path**:

- *Boot full-write*: Change the `afatfs_fopen_lfn` target from
  `FS_RESIDENT_NAMES_FILENAME` to `FS_RESIDENT_NAMES_TEMP_FILENAME`. After
  all rows are written, enter the shared tail (close → sync → remove old →
  rename → finish). The probe at phase 0 remains — it still validates root
  state before any write.

- *Runtime update*: Same conversion at the rewrite open (currently phase 3/4
  where it opens `.hcnames "w"`). Change target to temp filename. After
  streaming, enter the shared tail.

- *Bank Load phase 83*: Same conversion. Write to temp, then the tail phases
  promote it. This adds ~4-5 new phases to the Bank Load state machine.

- *Bank Save phase 83*: Same conversion. Write to temp, then the tail phases
  promote it. This adds ~4-5 new phases to the Bank Save state machine.

**`hcnames_mirror_valid` timing**: Currently set to `PUBLISH_PENDING` at
phase 6 of `residentNames_tick`, then promoted to `VALID` by the final sync
callback. The safe-write should set `PUBLISH_PENDING` after rename succeeds
(not after close), since the file is only authoritative once renamed. Verify
each path's mirror-valid transition is correct.

### Boot recovery prelude

Add a recovery pass that runs **before** any `.hcnames` read, modeled on
`filesystem_loadGlobals_tick` phases 0-8.

**Placement**: Inside `filesystem_ensureAutosaveFiles_tick()`, before its
existing phase 0 `.hcnames` open. Or as a prefix to the ensure-blocking
wrapper. The key requirement is: recovery completes before any code path
tries to open `.hcnames "r"`.

**Logic** (identical to the settings.cfg pattern):

1. Open `.hcnamtmp "r"`. If NULL → no temp file, skip to normal flow.
2. Read and validate: 129 rows, each parseable as `name\tsource[\tR]\n`.
   Count rows; if count == 129 and all parsed → valid.
3. Close temp.
4. If valid: remove `.hcnames` → rename `.hcnamtmp` → `.hcnames`. Proceed
   to normal `.hcnames` read.
5. If invalid: remove `.hcnamtmp`. Proceed to normal `.hcnames` read
   (may find old file intact, or absent if power was lost mid-first-write).

**Row validation**: The `.hcnames` file has no terminator line (unlike
settings.cfg's `lines=N`). Validate by: row count == 129 and every row
parsed without error. A partial write (< 129 rows) is invalid. A fully
written file with 129 rows is valid regardless of content (empty names and
default sources are legal).

### Error handling

Follow the settings.cfg precedent:

- Remove failure during promote: error out, temp stays for next boot.
- Rename failure after remove: error out, temp stays for next boot.
- The temp file is always the durable fallback.

### Testing

- Power-loss simulation: not practical on hardware. Verify by inspection
  that the sync → remove → rename ordering is correct and matches the proven
  settings.cfg pattern.
- Normal operation: confirm `.hcnames` is present and correct after boot,
  Kit Save, Scene Save, Bank Load, and Instrument Save.
- Recovery: manually place a valid `.hcnamtmp` on card, delete `.hcnames`,
  boot → confirm recovery promotes it.
- Recovery (invalid): place a truncated `.hcnamtmp` on card → confirm it is
  discarded and the old `.hcnames` survives.

---

## Phase B2: `.hcnames` Refreshed Flag

### What

Add a per-row refreshed flag to `.hcnames` that the autosave writer can
clear after fully capturing an object's mutations. The flag tells the future
boot reader (Phase E) whether it can trust autosave data for a given
sub-object.

### Why

When a Scene/Kit/Instrument is loaded from the library, its autosave record
is stale until the writer captures all its parameter bytes. The refreshed
flag marks these objects so the reader knows to load from the library source
instead of autosave. The writer clears the flag only after the relevant
mutation bits are fully resolved.

### What changes

**1. Define the flag**:
```
FS_RESIDENT_SOURCE_REFRESHED_FLAG  0x4000u  (bit 14 of fs_resident_source[])
```
Bit 14 is free. Bit 15 is already `FS_RESIDENT_SOURCE_DIRTY_FLAG`.

**2. Extend `.hcnames` row format**:

Current: `name\tsource\n`
Extended: `name\tsource[\tR]\n`

The `\tR` suffix is optional. Absent = not refreshed (backward compatible
with existing files). Present = refreshed. The parser must handle both.

**3. Extend the formatter** (`filesystem_formatResidentNameLine`):

If `FS_RESIDENT_SOURCE_REFRESHED_FLAG` is set in the source register for
this row, append `\tR` before `\n`.

**4. Extend the parser** (`filesystem_cacheResidentRecord`):

After parsing `name\tsource`, check for an additional `\t`. If present and
followed by `R`, set `FS_RESIDENT_SOURCE_REFRESHED_FLAG` in
`fs_resident_source[row]`. Otherwise clear it.

**5. Set refreshed on load/save completions**:

These are the terminal callbacks for filesystem operations. Each sets the
refreshed flag on the affected rows in `fs_resident_source[]`:

- Scene Load completion → Scene row + Kit row + 6 Instrument rows
- Kit Load completion → Kit row + 6 Instrument rows
- Bank Load completion → all loaded Scene rows and their children
- Instrument Load completion → Instrument row
- Scene Save → Scene row + Kit row + 6 Instrument rows
- Kit Save → Kit row + 6 Instrument rows
- Instrument Save → Instrument row

Setting the flag is a simple OR of `FS_RESIDENT_SOURCE_REFRESHED_FLAG` into
the existing source register. No new SRAM.

**6. Clear refreshed after autosave drain**:

New post-commit step in the autosave writer drain: after a successful drain
cycle completes (TERMINAL), examine each row that has
`FS_RESIDENT_SOURCE_REFRESHED_FLAG` set. If the canonical SRAM dirty mask
has no remaining dirty bits for that object's byte range, clear the flag.

**7. HCNAMES rewrite from the drain**:

If any refreshed flags were cleared in step 6, the writer must update
`.hcnames` on card to reflect the new state. This is a new sub-operation
within the drain transaction, using the Phase B safe-write pattern.

This is the third `.hcnames` write path (joining boot full-write and runtime
update). It writes the entire 129-row register from the mirror, not a
partial update. It uses the same temp-file safe-write (write `.hcnamtmp` →
sync → remove → rename → finish).

The drain already holds the filesystem facade, so this sub-operation runs
as additional phases after the `.hcprms` commit succeeds and before the
drain reaches TERMINAL.

### SRAM cost

Zero new static allocation. The refreshed flag uses bit 14 of the existing
258-byte `fs_resident_source[]` register.

### Interaction with existing HCNAMES writes

The refreshed flag is set in the SRAM register and serialized to card by
whichever HCNAMES write runs next. All three write paths (boot, runtime
update, drain post-commit) serialize from the same SRAM state. The flag
persists in SRAM across facade cycles and is only cleared by the drain
post-commit step.

If a Kit Save triggers `UPDATE_HCNAMES_KIT` while refreshed flags are set,
those flags will be serialized — the runtime update reads the mirror, overlays
changed rows, and rewrites all 129 rows including their refreshed state.
This is correct: the flag should persist until the drain clears it.

### Testing

- After a Scene Load, confirm `.hcnames` shows `\tR` on the affected rows.
- After enough autosave drain cycles to capture all mutations, confirm the
  `\tR` suffixes are gone from those rows.
- After a Kit Save followed by drain cycles, confirm the Kit and Instrument
  rows lose their `\tR`.
- Boot with a `.hcnames` containing `\tR` flags → confirm they are parsed
  and reflected in the source register.

---

## Implementation Order

Phase B first, Phase B2 second. Phase B is a standalone safety improvement
that can be tested independently. Phase B2 depends on Phase B's temp-file
pattern.

---

## Detailed Implementation Schedule

All changes are in `Core/Hardware/SD/filesystem.c` unless noted.

---

### Phase B — Task 1: Define `FS_RESIDENT_NAMES_TEMP_FILENAME`

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: After line 105 (after `FS_RESIDENT_NAMES_FILENAME`)
**Action**: ADD

```c
#define FS_RESIDENT_NAMES_TEMP_FILENAME ".hcnamtmp"
```

**Description**: Constant for the temporary safe-write target file. Follows
the same naming convention as `STORAGE_SETTINGS_TEMP_FILENAME` ("settings.tmp"
at `storageTypes.h:58`). All HCNAMES writers will open this file with `"w"`
instead of opening `.hcnames` directly; the promotion sequence (sync → remove
old → rename → finish) makes the final `.hcnames` an atomic swap. The temp
filename must be short enough for FAT LFN creation (9 chars including dot;
fits within the 8.3 SFN envelope as `HCNAMTMP` so no LFN entries are needed).

**Affiliates**: Every HCNAMES write path (Tasks 2-5), the recovery prelude
(Task 6), and the future Phase B2 drain writer.

---

### Phase B — Task 2: Convert boot full-write (`filesystem_writeResidentNames_tick`)

**File**: `Core/Hardware/SD/filesystem.c`
**Function**: `filesystem_writeResidentNames_tick()` (lines 4779-4885)

#### Change 2a: Open temp file instead of live file

**Line**: 4825
**Action**: MODIFY

Change:
```c
if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
```
To:
```c
if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_TEMP_FILENAME,
```

**Description**: The probe at phase 0 still validates the root singleton
state for `.hcnames` (not `.hcnamtmp`). After proving absent/present/unique,
the writer opens the temp target for truncate-write. This is safe because
truncating `.hcnamtmp` destroys only a stale interrupted temp file, never the
live register. The probe logic, match mode, and open name buffer all remain
unchanged — only the target filename constant changes.

**Affiliates**: `FS_RESIDENT_NAMES_TEMP_FILENAME` (Task 1),
`on_file_opened` callback (line 1783), `op_file` / `op_file_ready` latches.

#### Change 2b: Replace phase 3 (close+finish) with safe-write tail phases

**Lines**: 4869-4879 (current phase 3)
**Action**: MODIFY + ADD new phases 3-8

Current phase 3 does: wait close → chdir root → clear dirty flags → finish.
Replace with the five-step safe-write tail, modeled exactly on
`saveGlobals_tick` phases 3-10:

| New Phase | What | Model |
|-----------|------|-------|
| 3 | Close temp: `afatfs_fclose(op_file, on_file_closed)` | saveGlobals ph 3 (line 18328) |
| 4 | Wait close: `if (!op_close_done) return;` | saveGlobals ph 4 (line 18334) |
| 5 | Sync: `if (!afatfs_sync()) return;` — make temp durable | saveGlobals ph 5 (line 18339) |
| 6 | Remove old: `afatfs_removeObjects_lfn(FS_RESIDENT_NAMES_FILENAME, FS_RESIDENT_NAMES_MATCH_MODE, AFATFS_REMOVE_FILES_ONLY, on_remove_complete)` | saveGlobals ph 6 (line 18353) |
| 7 | Wait remove: check `op_remove_done`, gate on `op_remove_result == OK` | saveGlobals ph 7 (line 18372) |
| 8 | Rename: `afatfs_renameObject_lfn(FS_RESIDENT_NAMES_TEMP_FILENAME, FS_RESIDENT_NAMES_FILENAME, FS_RESIDENT_NAMES_MATCH_MODE, op_repair_rename_open_name, on_rename_complete)` | saveGlobals ph 8 (line 18387) |
| 9 | Wait rename: check `op_rename_done`, gate on `op_rename_result == OK` | saveGlobals ph 9 (line 18408) |
| 10 | Finish: `filesystem_clearResidentSourceDirtyFlags(); filesystem_finish(FS_STATUS_DONE);` | saveGlobals ph 10 (line 18422) |

**Description**: The close step (phase 3) now closes `.hcnamtmp`, not the
live register. Phase 5's `afatfs_sync()` ensures every data and directory
sector for the completed temp file is on the physical media before the
irreversible remove/rename. Phase 6 removes any existing `.hcnames` with
case-insensitive matching (tolerates missing, e.g. first boot). Phase 8
promotes `.hcnamtmp` → `.hcnames`. Phase 10 clears dirty source flags and
enters `filesystem_finish(DONE)`, which defers to `filesystem_flushFinish_tick`
for a second `afatfs_sync()` that makes the rename's directory-entry changes
durable, then promotes `hcnames_mirror_valid` from PUBLISH_PENDING to VALID.

The `chdir(NULL)` that was in old phase 3 (line 4873) moves to after the
rename wait (phase 9 or phase 10), since all operations happen in root.

**Error handling**: Remove failure → `filesystem_finish(ERROR)`, temp stays
for boot recovery. Rename failure → `filesystem_finish(ERROR)`, temp stays.
Matches `saveGlobals_tick` phases 7/9 exactly.

**State variables reused**: `op_remove_done` / `op_remove_result` (line 565-567),
`op_rename_done` / `op_rename_result` (line 1212-1214),
`op_repair_rename_open_name` (line 1241) for the rename API's required
open-name output buffer. All are shared operation-scoped scratch, idle during
`FS_INTERNAL_OP_WRITE_HCNAMES`.

**Mirror validity**: The current code does not set `hcnames_mirror_valid`
anywhere in `writeResidentNames_tick` — it is already INVALID (set at mount
reset, line 21420). This is correct: the boot full-write serializes from live
SRAM (BankData/SceneData), not from `hcnames_name_mirror[]`. After the
`filesystem_finish(DONE)` → `filesystem_flushFinish_tick()` sync, the
mirror remains INVALID because no code set it to PUBLISH_PENDING. This is
intentional — the boot full-write does not populate the mirror; the next
reader (ensure-autosave) will read `.hcnames` and populate it. No change
needed.

---

### Phase B — Task 3: Convert runtime update (`filesystem_residentNames_tick`)

**File**: `Core/Hardware/SD/filesystem.c`
**Function**: `filesystem_residentNames_tick()` (lines 5593-5867)

This function has two write-entry points that open `.hcnames` with `"w"`:

1. **Normal update path**: phase 3 success → overlay rows → reopen at phase 3/line 5713
2. **Bootstrap create path**: phase 7 absent proof → overlay → open at phase 7/line 5825

Both must be converted.

#### Change 3a: Convert normal update reopen to temp file

**Line**: 5713
**Action**: MODIFY

Change:
```c
if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                      "w",
```
To:
```c
if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_TEMP_FILENAME,
                      "w",
```

**Description**: After the read-close-overlay sequence (phases 0-3), the
runtime updater now opens the temp file for its truncate-rewrite instead of
the live register. All rows are streamed from `hcnames_name_mirror[]` through
`filesystem_formatResidentNameLine()` into `.hcnamtmp`. The overlay functions
(`filesystem_cacheCurrentResidentKitNames`, `...InstrumentNames`,
`...SceneNames`) are unchanged — they modify the in-memory mirror before the
temp file opens.

**Affiliates**: `on_file_opened` callback, `op_file_ready`/`op_file` latches,
`hcnames_name_mirror[]` (line 916), `fs_resident_source[]` (line 898).

#### Change 3b: Convert bootstrap create to temp file

**Line**: 5825
**Action**: MODIFY

Change:
```c
if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                      "w",
```
To:
```c
if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_TEMP_FILENAME,
                      "w",
```

**Description**: The bootstrap path reaches here only when the probe proves
`.hcnames` is absent and this is the first-ever HCNAMES creation. Writing to
`.hcnamtmp` first and then promoting is correct even for first creation: the
promote's remove step tolerates an absent `.hcnames` (returns OK when zero
objects match), and the rename creates `.hcnames` from the validated temp.

#### Change 3c: Replace phase 6 (close+finish) with safe-write tail

**Lines**: 5765-5786 (current phase 6)
**Action**: MODIFY + ADD new phases 6-12

Current phase 6 does: wait close → chdir root → clear dirty flags →
set `hcnames_mirror_valid = PUBLISH_PENDING` → finish.

Replace with the same five-step safe-write tail as Task 2:

| New Phase | What |
|-----------|------|
| 6 | Close temp: `afatfs_fclose(op_file, on_file_closed)` |
| 7 (renumbered — **see note**) | Wait close |
| 8 | Sync: `afatfs_sync()` |
| 9 | Remove old `.hcnames` |
| 10 | Wait remove |
| 11 | Rename `.hcnamtmp` → `.hcnames` |
| 12 | Wait rename |
| 13 | Clear dirty flags, set `PUBLISH_PENDING`, `filesystem_finish(DONE)` |

**IMPORTANT PHASE CONFLICT**: The current function already uses phases 7-9
for the probe/retry bootstrap path. The safe-write tail phases must not
collide. Two approaches:

**Option A (recommended)**: Renumber the probe/retry phases (currently 7/8/9)
to higher numbers (e.g. 20/21/22) and use 7-13 for the safe-write tail. The
probe/retry path is entered from phase 1 (line 5648: `op_phase = 7u;`) and
phase 7 (line 5811: `op_phase = 8u;`). These jump targets change to 20/21/22.

**Option B**: Use phases 14-20 for the safe-write tail and keep 7-9 for
probe/retry. The fclose at the end of phase 5's streaming loop currently
jumps to phase 6; change to 14.

Either way, the function's `case` numbering must be checked for collisions.

**Description**: The safe-write tail is identical to Task 2's. After all rows
stream to `.hcnamtmp` and the file closes, sync ensures durability, then
remove-old + rename promotes atomically. The `PUBLISH_PENDING` assignment
moves from old phase 6 to the new terminal phase (after rename wait, before
`filesystem_finish`), because the file is only authoritative once renamed.

**Mirror validity transition**: Set `hcnames_mirror_valid = PUBLISH_PENDING`
in the terminal phase (after successful rename), not after the close. The
`filesystem_flushFinish_tick()` sync at line 3920-3929 will promote it to
VALID. On error at any safe-write step, `filesystem_complete()` at line
3512-3514 will demote PUBLISH_PENDING back to INVALID.

---

### Phase B — Task 4: Convert Bank Load HCNAMES write (phases 83-86)

**File**: `Core/Hardware/SD/filesystem.c`
**Function**: `filesystem_loadBankDirectory_tick()` (starts line 11540)
**Phases**: 83-86 (lines 12500-12586)

#### Change 4a: Open temp file instead of live file

**Line**: 12512
**Action**: MODIFY

Change:
```c
if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                      "w",
```
To:
```c
if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_TEMP_FILENAME,
                      "w",
```

**Description**: After Bank metadata commit and HCNAMES preload read (phases
0/79-82), the Bank Load writer now writes to `.hcnamtmp` instead of `.hcnames`.
The `hcnames_mirror_valid = INVALID` at line 12506 remains — the mirror must
be invalid while the write-capable open can truncate.

#### Change 4b: Replace phase 86 (close+finish) with safe-write tail

**Lines**: 12563-12586 (current phase 86)
**Action**: MODIFY + ADD new phases 87-92 (or similar, checking for conflicts)

**IMPORTANT PHASE CONFLICT**: Bank Load already uses phase 87+ for the
HCNAMES probe/retry bootstrap path (line 11647: `op_phase = 87u;`). The
safe-write tail phases must not collide with those. Renumber the safe-write
tail to fit, or move the probe/retry to higher numbers.

Current phase numbering in `filesystem_loadBankDirectory_tick`:
- 0: validate + HCNAMES preload
- 80-82: HCNAMES read
- 1-19: Bank container validation
- 20-78: Child Scene loading (uses `filesystem_loadSceneDirectory_tick`)
- 83-86: HCNAMES write
- 87-89: HCNAMES probe/retry for first-use cards
- default: error

The existing phases 83-86 become:

| New Phase | What |
|-----------|------|
| 83 | Open `.hcnamtmp "w"` (invalidate mirror, chdir root) |
| 84 | Wait open |
| 85 | Stream all 129 rows from `hcnames_name_mirror[]` |
| 86 | Close temp: `afatfs_fclose(op_file, on_file_closed)` |
| 90 | Wait close |
| 91 | Sync: `afatfs_sync()` |
| 92 | Remove old `.hcnames` |
| 93 | Wait remove |
| 94 | Rename `.hcnamtmp` → `.hcnames` |
| 95 | Wait rename |
| 96 | Clear dirty flags, set `PUBLISH_PENDING`, `filesystem_finish(DONE)` |

Phase numbers 90-96 are chosen to avoid the existing 87-89 probe/retry range.

**Description**: The streaming phase (85) is unchanged — it streams from
`hcnames_name_mirror[]` via `filesystem_formatResidentNameLine()`. The close
now closes `.hcnamtmp`. The sync/remove/rename tail follows the identical
pattern from `saveGlobals_tick`. PUBLISH_PENDING is set after successful
rename (phase 96) instead of after close (old phase 86). The Bank Load
completion comment (lines 12576-12586) moves to the new terminal phase.

**Error handling**: Same as Tasks 2/3. Remove or rename failure →
`filesystem_finish(ERROR)`. The temp file stays for boot recovery. Bank Load
completion callback is invoked only after `filesystem_finish(DONE)` succeeds
through the flush gate.

---

### Phase B — Task 5: Convert Bank Save HCNAMES write (phases 83-86)

**File**: `Core/Hardware/SD/filesystem.c`
**Function**: `filesystem_saveBankDirectory_tick()` (starts line 15163)
**Phases**: 83-86 (lines 15928-16019)

#### Change 5a: Open temp file instead of live file

**Line**: 15935
**Action**: MODIFY

Change:
```c
if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                      "w",
```
To:
```c
if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_TEMP_FILENAME,
                      "w",
```

**Description**: After the Bank tree is written and the source staged (phases
1-45+), Bank Save opens `.hcnamtmp` for the HCNAMES register write. The
mirror is already invalidated at phase 45 (line 15906) and defensively
repeated at phase 83 (line 15932).

#### Change 5b: Replace phase 86 (close + index rebuild + finish) with safe-write tail

**Lines**: 15985-16019 (current phase 86)
**Action**: MODIFY + ADD new phases 87-93 (checking for conflicts)

Current Bank Save phase numbering:
- 0: validate + optional HCNAMES preload (skip if mirror valid)
- 80-82: HCNAMES preload read
- 1-45+: Bank tree write (per-child delete-then-write)
- 83-86: HCNAMES register write + index rebuild setup

The existing phases 83-86 become:

| New Phase | What |
|-----------|------|
| 83 | Open `.hcnamtmp "w"` (invalidate mirror) |
| 84 | Wait open |
| 85 | Stream all 129 rows from mirror |
| 86 | Close temp |
| 87 | Wait close |
| 88 | Sync |
| 89 | Remove old `.hcnames` |
| 90 | Wait remove |
| 91 | Rename `.hcnamtmp` → `.hcnames` |
| 92 | Wait rename |
| 93 | Clear dirty flags, set PUBLISH_PENDING, setup index rebuild, finish |

**Description**: The terminal phase (93) inherits the index-rebuild setup
from old phase 86 (lines 15997-16019): it sets
`op_library_index_rebuild_kind` and `op_library_index_rebuild_pending`,
records the save lifecycle trace, and calls `filesystem_finish(DONE)`.
PUBLISH_PENDING is set after successful rename. The Bank Save completion
chain (index rebuild → flush → callback) is unchanged — it follows after
`filesystem_finish`.

**Phase conflict check**: Bank Save's current phases include 0, 80-86,
then 1-45+. Phases 87-93 are free.

---

### Phase B — Task 6: Add `.hcnamtmp` recovery prelude to ensure-autosave

**File**: `Core/Hardware/SD/filesystem.c`
**Function**: `filesystem_ensureAutosaveFiles_tick()` (starts line 5960)

#### Change 6a: Add recovery prelude phases before existing phase 0

**Lines**: Before line 5962 (current phase 0 / case 0)
**Action**: ADD new phases (use negative-free numbering by renumbering)

The current ensure-autosave phase 0 opens `.hcnames "r"`. The recovery
prelude must complete before that open. Two approaches:

**Option A (recommended)**: Renumber existing phases 0-N to 10-N+10. The
recovery prelude uses phases 0-8. The existing phase 0 becomes phase 10,
phase 1 becomes 11, etc. All `op_phase =` assignments within this function
must be updated.

**Option B**: Prefix the recovery as a separate gate function called before
`filesystem_ensureAutosaveFiles_tick()` enters its switch. Less clean.

Recovery prelude phases (modeled on `filesystem_loadGlobals_tick` phases 0-8):

| Phase | What | Model |
|-------|------|-------|
| 0 | Open `.hcnamtmp "r"` via `afatfs_fopen_lfn(FS_RESIDENT_NAMES_TEMP_FILENAME, "r", FS_RESIDENT_NAMES_MATCH_MODE, NULL, on_file_opened)` | loadGlobals ph 0 (line 17957) |
| 1 | Wait open: NULL = no temp file, skip to existing flow (phase 10). Non-NULL = temp found, init validation state | loadGlobals ph 1 (line 17972) |
| 2 | Read-validate: stream all rows via `filesystem_readTextLine()` + `filesystem_cacheResidentRecord()`. Count rows in `op_item_offset`. On parse error: mark invalid | loadGlobals ph 2 (line 17997) |
| 3 | Close temp: `afatfs_fclose(op_file, on_file_closed)` | loadGlobals ph 3 (line 18053) |
| 4 | Wait close: decide promote or discard based on `op_item_offset == 129` and no parse errors | loadGlobals ph 4 (line 18059) |
| 5 | Remove: if valid → remove `.hcnames`; if invalid → remove `.hcnamtmp` | loadGlobals ph 5 (line 18070) |
| 6 | Wait remove | loadGlobals ph 6 (line 18096) |
| 7 | Rename (promote path only): `.hcnamtmp` → `.hcnames` | loadGlobals ph 7 (line 18117) |
| 8 | Wait rename → fall through to existing flow (phase 10) | loadGlobals ph 8 (line 18137) |

**Description**: The recovery prelude runs before any `.hcnames` read. It
uses the existing `filesystem_cacheResidentRecord()` parser to validate each
row, so format extensions (like Phase B2's `\tR` flag) are automatically
handled. The validation criterion is: exactly 129 rows parsed without error.
Unlike `settings.cfg`, `.hcnames` has no self-checking `lines=N` terminator,
so row count is the sole completeness signal.

**Validation scratch**: Reuse `op_item_offset` as the row counter (already
used this way in the existing HCNAMES read phases). Add one recovery-valid
flag — either reuse `op_settings_recovery_valid` (lifetime is
`FS_INTERNAL_OP_LOAD_GLOBALS` phases 0-10, non-overlapping with
`FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES`) or add a new operation-local flag.
Since these operations never run concurrently, reusing the existing static is
safe.

**State variables reused**:
- `op_file_ready`, `op_file` (line ~490-500)
- `op_close_done` (line ~494)
- `op_item_offset` (row counter, line ~545)
- `op_line_buf`, `op_line_len` (line 560-561)
- `op_remove_done`, `op_remove_result` (line 565-567)
- `op_rename_done`, `op_rename_result` (line 1212-1214)
- `op_repair_rename_open_name` (line 1241)
- `hcnames_name_mirror[]` (line 916) — validated rows are cached into the
  mirror during read-validate; if valid and promoted, the mirror already
  contains the complete register. If invalid, `filesystem_prepareResidentNamesCache()`
  at old phase 0 (now phase 10) clears it before the normal `.hcnames` read.

**Mirror validity**: After a successful promote (rename), the mirror contains
the validated temp file's content. Set `hcnames_mirror_valid = VALID` after
rename, then skip the normal HCNAMES read in phase 10+ (jump directly to the
autosave record scan phase). OR: leave the mirror valid but still let the
existing read path re-read the now-promoted `.hcnames` for simplicity — the
`filesystem_prepareResidentNamesCache()` at old phase 0 clears it anyway, so
the re-read is harmless. The simpler approach (always fall through to the
existing read) is preferred because it adds no conditional skip logic.

**Placement in boot order**: The boot sequence is:
```
settings.cfg recovery (loadGlobals phases 0-8) → settings.cfg load (phases 10-14)
→ boot Kit/Scene/Bank scans + index writes
→ boot HCNAMES full-write (writeResidentNames_tick)
→ initial Bank Load
→ ensure-autosave (ensureAutosaveFiles_tick) ← RECOVERY PRELUDE HERE
→ enable autosave writer
```

The recovery prelude at ensure-autosave is correct: it runs after the boot
full-write (which may have left a `.hcnamtmp` on a previous interrupted boot)
and before the ensure's `.hcnames` read. The boot full-write itself now
writes to `.hcnamtmp` + promotes, so if that promotion was interrupted, the
ensure prelude finds and resolves it.

---

### Phase B — Task 7: Verify `hcnames_mirror_valid` transitions

**File**: `Core/Hardware/SD/filesystem.c`

No code changes — verification pass only. Confirm that after the safe-write
conversion:

| Write path | When PUBLISH_PENDING is set | When promoted to VALID | Error demotes? |
|------------|---------------------------|----------------------|----------------|
| Boot full-write | Not set (boot writer doesn't populate mirror) | N/A — mirror stays INVALID until ensure reads | N/A |
| Runtime update | After rename succeeds (new phase 13) | `filesystem_flushFinish_tick` line 3927 | Yes — `filesystem_complete` line 3512 |
| Bank Load | After rename succeeds (new phase 96) | `filesystem_flushFinish_tick` line 3927 | Yes — `filesystem_complete` line 3512 |
| Bank Save | After rename succeeds (new phase 93) | `filesystem_flushFinish_tick` line 3927 | Yes — `filesystem_complete` line 3512 |

The key correctness property: PUBLISH_PENDING must not be set until the
rename succeeds, because only then does `.hcnames` exist with the correct
content. Before rename, the mirror content may be correct in SRAM but the
on-card file is either missing or stale.

---

### Phase B — Summary of all changes

| Task | File:Line | Action | Description |
|------|-----------|--------|-------------|
| 1 | filesystem.c:~106 | ADD | `FS_RESIDENT_NAMES_TEMP_FILENAME` constant |
| 2a | filesystem.c:4825 | MODIFY | Boot write: open temp instead of live |
| 2b | filesystem.c:4864-4879 | MODIFY+ADD | Boot write: replace close+finish with safe-write tail (phases 3-10) |
| 3a | filesystem.c:5713 | MODIFY | Runtime update: normal reopen targets temp |
| 3b | filesystem.c:5825 | MODIFY | Runtime update: bootstrap create targets temp |
| 3c | filesystem.c:5760-5786 | MODIFY+ADD | Runtime update: replace phase 6 with safe-write tail + renumber probe phases |
| 4a | filesystem.c:12512 | MODIFY | Bank Load: open temp |
| 4b | filesystem.c:12558-12586 | MODIFY+ADD | Bank Load: replace phase 86 with safe-write tail (phases 86-96) |
| 5a | filesystem.c:15935 | MODIFY | Bank Save: open temp |
| 5b | filesystem.c:15980-16019 | MODIFY+ADD | Bank Save: replace phase 86 with safe-write tail (phases 86-93) |
| 6a | filesystem.c:5960-6050 | MODIFY+ADD | Ensure-autosave: add recovery prelude (phases 0-8), renumber existing phases |
| 7 | (verification only) | VERIFY | Confirm mirror-valid transitions are correct |

**Total new phases**: ~8 per write path × 4 paths = ~32 new case labels, plus
~9 recovery prelude phases. All follow the same proven pattern from
`saveGlobals_tick`.

**SRAM cost**: Zero. All scratch variables (`op_remove_done`, `op_rename_done`,
`op_repair_rename_open_name`, etc.) are existing shared operation-local state.

**asyncfatfs changes**: None. All required primitives exist.

---

### Phase B2 — Task 1: Define `FS_RESIDENT_SOURCE_REFRESHED_FLAG`

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: After line 144 (after `FS_RESIDENT_SOURCE_DIRTY_FLAG`)
**Action**: ADD

```c
#define FS_RESIDENT_SOURCE_REFRESHED_FLAG    0x4000u
```

**Description**: Bit 14 of the 16-bit `fs_resident_source[]` register. Bit 15
is already `FS_RESIDENT_SOURCE_DIRTY_FLAG` (0x8000). Bits 0-12 hold the
source value (0-999 for numbered slots, 0x7ffd-0x7fff for special tokens).
Bit 13 is free (unused today). Bit 14 marks a row whose object has been
freshly loaded or saved from the library and whose autosave record is not yet
fully captured. The flag is set in SRAM only and persists across facade cycles.

**Interaction with existing masks**: `FS_RESIDENT_SOURCE_VALUE_MASK` (0x7fff)
masks off both the dirty flag (bit 15) and now also the refreshed flag (bit
14). This mask is used in:
- `filesystem_formatResidentNameLine` (line 19619): `source & VALUE_MASK`
- `filesystem_clearResidentSourceDirtyFlags` (line 5435): `&= VALUE_MASK`
- `filesystem_setResidentSource` (various): via VALUE_MASK
- `filesystem_cacheResidentRecord` (line 5401): dirty flag check

**CRITICAL**: `FS_RESIDENT_SOURCE_VALUE_MASK` is currently 0x7fff. After
adding the refreshed flag at bit 14, the VALUE_MASK must change to 0x3fff
to mask off both bits 14 and 15. BUT: the special source tokens use values
0x7ffd-0x7fff, which extend into bit 14. This means the value space and
the refreshed flag overlap.

**Resolution**: The refreshed flag must NOT be stored in the source register
value bits. Instead, it must be in a separate bit that is masked off before
any value comparison. Two options:

**Option A**: Change `FS_RESIDENT_SOURCE_VALUE_MASK` to 0x3fff and move
the special tokens down (e.g. INHERIT=0x3fff, UNKNOWN=0x3ffe,
INSTRUMENT_DIRECT=0x3ffd). This changes `filesystem.h` public defines.

**Option B (recommended)**: Keep the current value layout and put the
refreshed flag at bit 13 instead (0x2000). The source values 0-999 fit in
10 bits (0x3E7). The special tokens 0x7ffd-0x7fff use bits 0-14. So neither
bit 13 nor bit 14 works without moving the special tokens.

**Option C (simplest)**: Reduce `FS_RESIDENT_SOURCE_VALUE_MASK` to 0x1fff
(bits 0-12) and redefine the three special tokens within that range:
INHERIT=0x1fff, UNKNOWN=0x1ffe, INSTRUMENT_DIRECT=0x1ffd. Bit 13 is
refreshed (0x2000), bit 14 is free, bit 15 is dirty (0x8000). This gives
values 0-999 in 10 bits (plenty of room below 0x1ffd) and three flags.

The following changes propagate:

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: 143-145
**Action**: MODIFY

```c
#define FS_RESIDENT_SOURCE_DIRECT_SLOT_LIMIT 1000u
#define FS_RESIDENT_SOURCE_REFRESHED_FLAG    0x2000u
#define FS_RESIDENT_SOURCE_DIRTY_FLAG        0x8000u
#define FS_RESIDENT_SOURCE_VALUE_MASK        0x1fffu
```

**File**: `Core/Hardware/SD/filesystem.h`
**Lines**: 631-633
**Action**: MODIFY

```c
#define FS_RESIDENT_SOURCE_INHERIT           0x1fffu
#define FS_RESIDENT_SOURCE_UNKNOWN           0x1ffeu
#define FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT 0x1ffdu
```

**Description**: The three special source values are redefined within the
13-bit value range. Numbered library slots (0-999) are unaffected. The
dirty flag stays at bit 15. The refreshed flag occupies bit 13. Bit 14 is
now free for future use.

**Affiliates**: `filesystem_residentSourceValid()` (line 5189), the
source-token parser `filesystem_parseResidentSourceToken()` (line 5301),
the formatter `filesystem_formatResidentNameLine()` (line 19602), and
`filesystem_clearResidentSourceDirtyFlags()` (line 5424). All use
`VALUE_MASK` and are unaffected by the mask change since they already
strip flags before comparison.

---

### Phase B2 — Task 2: Extend formatter for `\tR` suffix

**File**: `Core/Hardware/SD/filesystem.c`
**Function**: `filesystem_formatResidentNameLine()` (lines 19602-19663)

**Line**: After line 19619 (after `source & VALUE_MASK`)
**Action**: MODIFY

The formatter must check if `FS_RESIDENT_SOURCE_REFRESHED_FLAG` was set in
the **original** (pre-masked) source register for this row, and if so,
append `\tR` before `\n`.

**Implementation**: Capture the raw source word before masking:

```c
uint8_t refreshed = (uint8_t)((source & FS_RESIDENT_SOURCE_REFRESHED_FLAG) != 0u);
source = (uint16_t)(source & FS_RESIDENT_SOURCE_VALUE_MASK);
```

Then after emitting the source token and before `\n`:

```c
if (refreshed) {
    if (len + 3u >= cap) return 0u;
    dst[len++] = '\t';
    dst[len++] = 'R';
}
dst[len++] = '\n';
dst[len] = '\0';
```

This applies at two exit points:
1. Line 19654 (numeric source: `dst[len++] = '\n';`)
2. Line 19661 (symbolic source: `dst[len++] = '\n';`)

Both must be modified to check `refreshed` before the final `\n`.

**Description**: The `\tR` suffix is the serialized refreshed flag. When
present, it tells the boot reader (Phase E) that this row's autosave data is
not yet fully captured and the object should be loaded from the library source
instead. When absent, autosave data is authoritative (or was never marked
as needing refresh). The suffix is backward compatible: firmware without
Phase B2 will silently discard the extra tab-separated field (line 5364-5374
already handles a second tab by truncating at it).

**Affiliates**: `filesystem_nextResidentNameLine()` (line 19666) — this
helper calls `filesystem_formatResidentNameLine()` and needs no change since
it passes `fs_resident_source[row]` which includes the refreshed flag.
All four HCNAMES write paths (boot, runtime, Bank Load, Bank Save) call the
formatter and will serialize the flag automatically.

---

### Phase B2 — Task 3: Extend parser for `\tR` suffix

**File**: `Core/Hardware/SD/filesystem.c`
**Function**: `filesystem_cacheResidentRecord()` (lines 5337-5403)

**Lines**: 5364-5374 (second-tab handling)
**Action**: MODIFY

The parser already detects a second tab (`second_tab = strchr(tail, '\t')`)
and truncates the source token at it (lines 5366-5374). After parsing the
source token, the code must check the content after the second tab for `R`:

```c
if (second_tab) {
    /* existing: truncate source token at second tab */
    ...
    /* NEW: check for refreshed flag after second tab */
    const char *flag_field = second_tab + 1u;
    if (flag_field[0] == 'R' &&
        (flag_field[1] == '\0' || flag_field[1] == '\t' || flag_field[1] == '\n'))
        source |= FS_RESIDENT_SOURCE_REFRESHED_FLAG;
}
```

**At line 5401**: The existing dirty-flag guard protects staged dirty values:
```c
if ((fs_resident_source[row] & FS_RESIDENT_SOURCE_DIRTY_FLAG) == 0u)
    fs_resident_source[row] = source;
```

The refreshed flag parsed from card content will be stored alongside the
source value when the row is not dirty. When the row IS dirty (a caller staged
a new source), the refreshed flag from the card is correctly discarded because
the caller's staged source has its own refreshed state.

**Description**: The parser now recognizes the optional `\tR` third column in
HCNAMES rows. The `R` token is the only recognized flag; any other content
after the second tab is silently ignored (future-compatible). The parsed flag
is stored in-register as `FS_RESIDENT_SOURCE_REFRESHED_FLAG` and serialized
back by the formatter (Task 2).

**Affiliates**: All HCNAMES readers use this parser:
- `filesystem_residentNames_tick()` phase 2 (line 5676)
- `filesystem_ensureAutosaveFiles_tick()` phase 2 (line 6017)
- Bank Load phase 81 (line 11674)
- Bank Save phase 81 (line 15294)
- Autosave recovery phase 32 (line 7253)
- Recovery prelude phase 2 (Task 6)

All call `filesystem_cacheResidentRecord()` — no per-site changes needed.

---

### Phase B2 — Task 4: Set refreshed flag on load/save completions

**File**: `Core/Hardware/SD/filesystem.c`

Add a helper function:

```c
static void filesystem_setResidentRefreshed(uint16_t row)
{
    if (row < FS_RESIDENT_NAMES_ROW_COUNT)
        fs_resident_source[row] |= FS_RESIDENT_SOURCE_REFRESHED_FLAG;
}
```

**Line**: After `filesystem_clearResidentSourceDirtyFlags()` (line 5437)
**Action**: ADD

Then add a composite helper for Scene-scope refresh:

```c
static void filesystem_setResidentRefreshedScene(uint8_t scene_index)
{
    uint16_t scene_row = filesystem_residentSceneRow(scene_index);
    uint16_t kit_row = filesystem_residentKitRow(scene_index);
    uint16_t inst_base = filesystem_residentInstrumentRow(scene_index, 0u);
    uint8_t v;

    filesystem_setResidentRefreshed(scene_row);
    filesystem_setResidentRefreshed(kit_row);
    for (v = 0u; v < STORAGE_KIT_SLOT_COUNT; v++)
        filesystem_setResidentRefreshed(inst_base + v);
}
```

**Call sites** — add at each load/save terminal callback:

1. **Scene Load completion** — in `filesystem_loadSceneDirectory_tick()`,
   at the terminal commit phase (after `filesystem_finish(DONE)` or in the
   completion callback setup). Call `filesystem_setResidentRefreshedScene(scene_index)`.

2. **Kit Load completion** — in `filesystem_loadKitDirectory_tick()`, at the
   terminal phase. Call `filesystem_setResidentRefreshed(kit_row)` + 6
   instrument rows.

3. **Bank Load completion** — in `filesystem_loadBankDirectory_tick()`,
   before `filesystem_finish(DONE)`. For each loaded child (bit in the
   loaded mask), call `filesystem_setResidentRefreshedScene(child_index)`.

4. **Instrument Load completion** — in `filesystem_loadInstrument_tick()`,
   at the terminal phase. Call `filesystem_setResidentRefreshed(inst_row)`.

5. **Scene Save** — in `filesystem_saveSceneDirectory_tick()`, at the
   terminal phase. Call `filesystem_setResidentRefreshedScene(scene_index)`.

6. **Kit Save** — in `filesystem_saveKitDirectory_tick()`, at the terminal
   phase. Call `filesystem_setResidentRefreshed(kit_row)` + 6 instrument rows.

7. **Instrument Save** — in `filesystem_saveInstrument_tick()`, at the
   terminal phase. Call `filesystem_setResidentRefreshed(inst_row)`.

**Description**: Setting the refreshed flag means "this object's autosave
record is stale — load from the library source instead of autosave." The flag
is set at the completion of any operation that replaces the resident object's
identity. It is cleared only by the autosave drain after it has fully captured
the object's mutations (Task 5). The flag persists in SRAM across facade
cycles and is serialized to `.hcnames` by whichever write path runs next.

**Exact call sites**: These need to be identified by reading each load/save
terminal phase during implementation. The general pattern is: the refreshed
flag is set immediately after the payload commit and before the HCNAMES write
or `filesystem_finish()`, so the next HCNAMES write will serialize it.

---

### Phase B2 — Task 5: Clear refreshed after autosave drain

**File**: `Core/Hardware/SD/filesystem.c`

Add a post-commit step in the autosave writer drain. After a successful drain
cycle completes (reaches TERMINAL state), examine each row:

```c
static uint8_t filesystem_clearDrainedRefreshedFlags(void)
{
    uint8_t any_cleared = 0u;
    uint16_t row;

    for (row = 0u; row < FS_RESIDENT_NAMES_ROW_COUNT; row++) {
        if ((fs_resident_source[row] & FS_RESIDENT_SOURCE_REFRESHED_FLAG) == 0u)
            continue;
        if (autosave_objectFullyCaptured(row)) {
            fs_resident_source[row] &= ~FS_RESIDENT_SOURCE_REFRESHED_FLAG;
            any_cleared = 1u;
        }
    }
    return any_cleared;
}
```

**Line**: Near the autosave drain terminal code
**Action**: ADD helper + ADD call at drain terminal

**Description**: `autosave_objectFullyCaptured(row)` checks whether the
canonical dirty mask has no remaining dirty bits for the byte range
corresponding to this HCNAMES row's object. The exact implementation depends
on the dirty mask structure — it needs to map HCNAMES row numbers to
autosave byte ranges and check if all bits in that range have been captured
(written to the `.hcprms` record and committed).

The function returns 1 if any flags were cleared, which tells the caller
(Task 6) that an HCNAMES rewrite is needed.

**Affiliates**: `filesystem_autosaveParameterDrain_tick()` (the drain state
machine), the canonical dirty mask in `autosave_*` functions, and the
HCNAMES rewrite sub-operation (Task 6).

---

### Phase B2 — Task 6: Add drain HCNAMES rewrite sub-operation

**File**: `Core/Hardware/SD/filesystem.c`

If `filesystem_clearDrainedRefreshedFlags()` returned nonzero (any flags
cleared), the drain must update `.hcnames` on card using the Phase B
safe-write pattern.

**Implementation**: Add new phases to the autosave drain state machine,
after the `.hcprms` commit succeeds and before the drain reaches TERMINAL:

| Phase | What |
|-------|------|
| N | Call `filesystem_clearDrainedRefreshedFlags()`. If zero → skip to TERMINAL. If nonzero → continue |
| N+1 | Open `.hcnamtmp "w"` via `afatfs_fopen_lfn` |
| N+2 | Wait open |
| N+3 | Stream all 129 rows from `hcnames_name_mirror[]` |
| N+4 | Close temp |
| N+5 | Wait close |
| N+6 | Sync |
| N+7 | Remove old `.hcnames` |
| N+8 | Wait remove |
| N+9 | Rename `.hcnamtmp` → `.hcnames` |
| N+10 | Wait rename → set PUBLISH_PENDING → continue to TERMINAL |

**Description**: This is the fifth HCNAMES write path (joining boot, runtime
update, Bank Load, Bank Save). It writes the entire 129-row register from the
mirror. The drain already holds the filesystem facade, so this sub-operation
runs within the existing drain state machine. The PUBLISH_PENDING →
VALID promotion happens through the normal `filesystem_flushFinish_tick()`
sync gate.

**SRAM cost**: Zero. Reuses existing operation scratch.

---

### Phase B2 — Task 7: Update `filesystem_clearResidentSourceDirtyFlags`

**File**: `Core/Hardware/SD/filesystem.c`
**Function**: `filesystem_clearResidentSourceDirtyFlags()` (line 5424)
**Line**: 5435
**Action**: VERIFY (no change needed if correct)

Current code:
```c
fs_resident_source[row] &= FS_RESIDENT_SOURCE_VALUE_MASK;
```

After Task 1's mask change (`VALUE_MASK = 0x1fff`), this clears both the
dirty flag (bit 15) AND the refreshed flag (bit 13). This is WRONG — the
dirty-clear function should clear only the dirty flag, preserving the
refreshed flag.

**Fix**:
```c
fs_resident_source[row] &= (uint16_t)~FS_RESIDENT_SOURCE_DIRTY_FLAG;
```

This clears only bit 15 (dirty), preserving bits 0-14 including the
refreshed flag at bit 13.

**Affiliates**: Called from:
- `filesystem_writeResidentNames_tick()` phase 10 (boot)
- `filesystem_residentNames_tick()` phase 13 (runtime)
- Bank Load phase 96
- Bank Save phase 93

All should preserve the refreshed flag through an HCNAMES write.

---

### Phase B2 — Summary of all changes

| Task | File:Line | Action | Description |
|------|-----------|--------|-------------|
| 1 | filesystem.c:143-145, filesystem.h:631-633 | MODIFY | Add `REFRESHED_FLAG`, narrow `VALUE_MASK`, move special tokens |
| 2 | filesystem.c:19602-19663 | MODIFY | Formatter: append `\tR` when refreshed |
| 3 | filesystem.c:5364-5403 | MODIFY | Parser: recognize `\tR` third column |
| 4 | filesystem.c: multiple load/save terminals | ADD | Set refreshed on load/save completions |
| 5 | filesystem.c: drain terminal | ADD | Clear refreshed after autosave capture |
| 6 | filesystem.c: drain state machine | ADD | Drain HCNAMES safe-write sub-operation |
| 7 | filesystem.c:5435 | MODIFY | Fix dirty-clear to preserve refreshed flag |

**SRAM cost**: Zero — bit 13 of existing 258-byte register.

---

## Best Practices (from Phase A)

- **One fix per state-machine site**: Phase A's bug was a single missing
  `afatfs_chdir(NULL)` in one phase. Each write-path conversion in Phase B
  is similarly localized — change the target filename and append the tail
  phases. Don't refactor surrounding phases.

- **Match the existing pattern exactly**: The settings.cfg safe-write
  (`saveGlobals_tick` phases 0-10) is the proven model. Copy its
  close → sync → remove → rename → finish sequence. Don't innovate on the
  ordering or error handling.

- **Keep asyncfatfs untouched**: No driver changes. All needed primitives
  (`fopen_lfn`, `removeObjects_lfn`, `renameObject_lfn`, `sync`) exist.

- **Test after each conversion**: Don't batch all four write paths. Convert
  one, build, flash, verify `.hcnames` is present and correct, then proceed
  to the next. The boot full-write is the safest starting point.

- **Trace verification**: Use the `asavetrc.bin` decoder to confirm zero
  OPERATION_ERROR records after each change, same as Phase A verification.

- **SRAM budget**: Phase B adds zero new SRAM (phase variables reuse
  existing workspace). Phase B2 adds zero new SRAM (bit 13 of existing
  register). Both are within the 100-byte pre-approval.

- **Recovery prelude placement**: Must run before any `.hcnames "r"` open.
  The ensure-autosave path is the gate. Verify by tracing the boot order:
  settings → recovery prelude → ensure (creates .hcprms) → Bank Load
  (reads/writes .hcnames) → enable autosave.

---

## Implementation work log

- 2026-09-02: Read `MEMORY.md`, the Phase 042 handoff, and the current
  filesystem/autosave sources. The written line references are stale, so the
  implementation follows the current state-machine phases rather than those
  numbers.
- 2026-09-02: Confirmed the existing special source tokens (`0x7ffd..0x7fff`)
  overlap the proposed refreshed bit. Chosen representation is Option C:
  direct slots `0..999`, refreshed `0x2000`, dirty `0x8000`, value mask
  `0x1fff`, with special tokens moved to `0x1ffd..0x1fff`.
- 2026-09-02: Confirmed the five AsyncFATFS handles, the 1,161-byte dedicated
  HCNAMES mirror, and the existing operation scratch are the applicable SRAM
  boundary. The change will not add a handle, cache, bitmap, or other SRAM
  allocation.
- 2026-09-02: Safe publication will be implemented as temp close → sync →
  remove live → rename temp to live → final facade sync. The mirror becomes
  `PUBLISH_PENDING` only after the rename succeeds, and refreshed bits are
  cleared only after a successful autosave-triggered HCNAMES publication.
- 2026-09-02: Implemented the safe-write tails in the current state machines:
  boot HCNAMES phases 3–6, runtime targeted-update phases 10–14, boot recovery
  phases 15–19, Bank Load phases 90–94, and Bank Save phases 87–91. All five
  physical HCNAMES write opens now target `.hcnamtmp`; no direct live-file
  truncate remains. Malformed rows, formatter failures, close failures, and
  sticky full-card writes close the temp file without publishing it.
- 2026-09-02: Added optional `name<TAB>source<TAB>R` parsing/formatting and
  completion-boundary refresh marking. Scene/Kit/Instrument rows are marked at
  successful payload boundaries; Morph-only and hidden temporary Instrument
  paths remain identity-neutral. Bank Load marks the root Bank row and each
  successfully loaded selected Scene hierarchy.
- 2026-09-02: Added the zero-allocation HCNAMES-row-to-autosave-scope query in
  `Autosave.c/.h`. After a committed autosave, clean refreshed scopes are
  serialized without `R`; the refreshed bits are retired only from the
  terminal callback after final sync succeeds, so a failed facade flush keeps
  them available for retry.
- 2026-09-02: Verification completed with `make -j2` and `git diff --check`.
  The firmware links as `build/lxr02.elf` (`text=388868`, `data=400`,
  `bss=96180`); no automated hardware/card tests are present in the repo.

## What This Does NOT Change

- The `.hcprms` record format, dirty mask, or autosave drain phases
  (except the Phase B2 post-commit addition).
- The asyncfatfs driver.
- The settings.cfg writer or its recovery prelude.
- The boot Bank Load flow (other than its HCNAMES safe-write target/tail).
- The Phase A winner cache or any autosave validation logic.
- The probe mechanism or SFN/LFN matching.
- SRAM reservation policy (everything fits in existing allocations).

## What This Prepares For

- **Phase C** (autosave source fields): the `.hcnames` parser already
  handles the extended format. Source fields in the autosave record can
  reference `fs_resident_source[]` values knowing the refreshed bit is
  masked off before record writes.
- **Phase D** (re-dirty mechanism): load/save completions already set the
  refreshed flag; the re-dirty request flags are a separate addition that
  coordinates with the same completion callbacks.
- **Phase E** (boot reader): reads `.hcnames` including `\tR` flags to
  decide per-object load source (autosave vs. library).
