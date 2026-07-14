# Asyncfatfs Recursive Delete - Unplanned Draft

This draft preserves the previous recursive-delete implementation plan that was
removed from LOAD_SAVE_EXPANSION_ADD_MORPH.md. It is intentionally not part of
the active Load/Save/Morph implementation direction because directory overwrites
will preserve directories, rename entries when needed, replace expected files,
and tolerate stale extra files.

## First Prerequisite: Recursive Delete in asyncfatfs

Before any further production overwrite save work, asyncfatfs must gain safe
recursive delete support.

### Current Code Facts From The Dive

The delete implementation must be designed around these current source facts:

- `asyncfatfs.h` already exposes `afatfsObjectInfo_t`, and that struct already
  carries `sfnEntry`, `lfnFirstEntry`, and `lfnEntryCount`. This is exactly the
  metadata delete/rename need to remove a whole VFAT object instead of only the
  short entry.
- `afatfs_findNextObject()` is the right scanner boundary, but it currently
  skips any object whose displayed short alias begins with `.`. That conflicts
  with the confirmed policy that dot-prefixed ordinary objects are visible and
  deleteable. Fix that before recursive delete depends on the iterator.
- `afatfs_funlink()` currently truncates/free-clusters and marks only the owning
  SFN entry deleted through `afatfs_saveDirectoryEntry(...DELETED)`. It does
  not delete the preceding LFN fragment run.
- `afatfs_ftruncateContinue()` already frees cluster chains asynchronously and
  updates the file handle/directory entry. Recursive delete should reuse that
  logic for the data clusters, but add a new entry-run deletion step.
- `afatfs.currentDirectory` is a global working-directory handle. It can have a
  queued operation and is polled by `afatfs_fileOperationsPoll()`. Recursive
  delete should be a filesystem-global operation that owns directory traversal,
  not a normal file operation that can accidentally fight with the current
  directory handle.
- `AFATFS_MAX_OPEN_FILES` is currently `3`. A recursive delete implementation
  should not hold one open handle per depth. Use at most one child handle plus
  one saved parent context at a time, or a small explicit stack of directory
  identities.
- `filesystem.c` has one active operation slot through
  `filesystem_start()` / `filesystem_tick()`. Production overwrite saves should
  call a filesystem-level delete wrapper before their create/write phases.

### Required Public asyncfatfs API

Add component-based public delete entry points to `asyncfatfs.h`.

Recommended shape:

```c
bool afatfs_deleteObject_lfn(const char *displayName,
                             afatfsMatchMode_t matchMode,
                             afatfsCallback_t complete);

bool afatfs_deleteObjectByInfo(const afatfsObjectInfo_t *object,
                               afatfsCallback_t complete);
```

If `deleteObjectByInfo` is too risky as a public API because its entry pointers
are only meaningful in the current directory that produced them, keep it
private and expose only the `_lfn` form. The code still needs the identity path
internally for slot-save deletes by scan-cache alias.

Header comment to add beside `afatfs_deleteObject_lfn()`:

```c
/*
 * Delete one object in the current directory by exact display component.
 *
 * What: Starts an asynchronous delete of one file or directory named by a
 * single display component. Files are removed with their complete VFAT entry
 * run and cluster chain. Directories are deleted recursively before the parent
 * entry is marked deleted.
 *
 * Why: Save overwrite semantics are delete-recreate, not truncate-in-place.
 * Existing afatfs_funlink() deletes only an opened file's SFN entry and cannot
 * remove stale children from a Kit/Scene directory. This API gives filesystem.c
 * one atomic-looking async operation it can place before a replacement save.
 *
 * Inputs: displayName is one component in the current directory, never a path.
 * matchMode selects case-sensitive or case-insensitive display-name lookup,
 * using the same visible-name rules as afatfs_findNextObject() and
 * afatfs_fopen_lfn(). complete is called after all dirty entry/FAT updates are
 * queued, or after a clean failure.
 *
 * Outputs/effects: the target object no longer exists on success. The current
 * working directory is restored to the parent directory that was current when
 * the call was made. On failure, no callback is skipped and no open-file handle
 * remains owned by the delete operation.
 *
 * Affiliates/clients: filesystem.c overwrite-save phases for Kit, Scene,
 * Instrument, Bank, Effect/FX-stack, and File/Dir diagnostics. Internally this
 * depends on afatfs_findNextObject(), afatfs_ftruncateContinue(), FAT entry-run
 * deletion helpers, currentDirectory restoration, and afatfs_poll().
 */
```

If adding `afatfs_deleteObjectByInfo()`, use this header comment:

```c
/*
 * Delete one object already selected by an LFN-aware directory scan.
 *
 * What: Starts the same asynchronous delete as afatfs_deleteObject_lfn(), but
 * uses the caller's afatfsObjectInfo_t identity instead of performing another
 * display-name lookup.
 *
 * Why: Product scanners cache exact object identity for numbered Kit/Scene
 * slots. Deleting by that identity avoids re-resolving a display name after the
 * user has edited the replacement name and prevents accidental deletion of a
 * newly-created same-display object in pathological duplicate-name media.
 *
 * Inputs: object must come from afatfs_findNextObject() in the current
 * directory, and the caller must not have changed currentDirectory since that
 * scan. This function is unsafe for stale object records after card mutation.
 *
 * Outputs/effects: the object identified by object->sfnEntry and its
 * object->lfnFirstEntry/object->lfnEntryCount run are removed. Directories are
 * recursively emptied first. complete is called when done.
 *
 * Affiliates/clients: filesystem.c numbered-slot overwrite helpers and any
 * future rename/replace primitive that first scans the target directory.
 */
```

### Required Public filesystem.c Facade

Add filesystem-layer wrappers only after the asyncfatfs primitive exists. These
wrappers are not user-facing features; they are internal save-state phases.

Recommended `filesystem.c` private helpers:

```c
static bool filesystem_deleteCurrentDirectoryObject_lfn(
    const char *display_name,
    fs_completion_cb_t cb);

static bool filesystem_deleteKitSlotObject(uint16_t slot,
                                           fs_completion_cb_t cb);
```

Later Scene/Bank variants should mirror Kit instead of inventing another rule.

Comment for a generic filesystem delete phase:

```c
/*
 * Delete one current-directory object before replacement save.
 *
 * What: Posts a filesystem-owned asynchronous wrapper around
 * afatfs_deleteObject_lfn(). This is not a public product operation; it is a
 * building block used inside save state machines before they create the
 * replacement file or directory.
 *
 * Why: filesystem.c owns user-visible save semantics, scan caches, and retry
 * state. asyncfatfs owns FAT mutation. Keeping this wrapper in filesystem.c
 * lets Kit/Scene/Instrument saves sequence "delete old object, create new
 * object, write contents, sync, update caches" without Menu or Preset learning
 * asyncfatfs details.
 *
 * Inputs: currentDirectory must already be the parent directory that contains
 * display_name. The display name is one exact component. cb is the save
 * operation's continuation callback or a small local completion latch.
 *
 * Outputs/effects: current_op remains owned by the outer save operation; the
 * delete helper only advances its phase after asyncfatfs reports completion.
 *
 * Affiliates/clients: filesystem_saveKitDirectory_tick(),
 * filesystem_saveSceneDirectory_tick(), filesystem_saveInstrument_tick(), and
 * future Bank/Effect save writers.
 */
```

Comment for Kit numbered-slot delete:

```c
/*
 * Delete the existing numbered Kit slot object.
 *
 * What: Removes the currently scanned Kit/<NNN ...>/ directory before an
 * occupied-slot save recreates Kit/<NNN edited-name>/. The three-digit prefix is
 * the slot identity; the old text name and all old children are discarded.
 *
 * Why: Existing Kit Save opens the old alias and overwrites only authoritative
 * children, leaving stale files possible. The new overwrite rule is
 * delete-recreate, so this phase must remove the whole old directory tree
 * before any new member instruments or kitset.kcg are written.
 *
 * Inputs: slot is 000..999. kit_slot_present[slot] and
 * kit_slot_open_name[slot] come from the Kit scan cache. The caller must have
 * already chdir'd into root Kit/.
 *
 * Outputs/effects: on success, that numbered directory is absent. The following
 * save phase creates a new directory using the current save-name editor. Cache
 * update is deliberately deferred until the replacement directory has been
 * created and written successfully.
 *
 * Affiliates/clients: filesystem_scanKits_tick(),
 * filesystem_recordKitDirectory(), filesystem_saveKitDirectory_tick(), Menu's
 * persistent OW indicator, and future Scene/Bank slot delete helpers.
 */
```

### Required asyncfatfs Internal State

Add a filesystem-global delete operation state to `afatfs_t`, not to
`afatfsFileOperation_t`.

Recommended additions near the existing asyncfatfs internal structs:

```c
#define AFATFS_DELETE_MAX_DEPTH 4u

typedef enum {
    AFATFS_DELETE_PHASE_IDLE = 0,
    AFATFS_DELETE_PHASE_FIND_TARGET,
    AFATFS_DELETE_PHASE_OPEN_TARGET,
    AFATFS_DELETE_PHASE_ENTER_DIRECTORY,
    AFATFS_DELETE_PHASE_SCAN_CHILD,
    AFATFS_DELETE_PHASE_DELETE_CHILD,
    AFATFS_DELETE_PHASE_LEAVE_DIRECTORY,
    AFATFS_DELETE_PHASE_TRUNCATE_FILE,
    AFATFS_DELETE_PHASE_DELETE_ENTRY_RUN,
    AFATFS_DELETE_PHASE_FINISH_SUCCESS,
    AFATFS_DELETE_PHASE_FINISH_FAILURE
} afatfsDeleteObjectPhase_e;

typedef struct {
    afatfsFile_t directory;
    afatfsObjectFinder_t finder;
    afatfsObjectInfo_t object;
} afatfsDeleteStackFrame_t;

typedef struct {
    afatfsCallback_t callback;
    afatfsDeleteObjectPhase_e phase;
    afatfsMatchMode_t matchMode;
    char displayName[AFATFS_LONG_FILENAME_MAX + 1u];
    afatfsObjectInfo_t target;
    afatfsFile_t targetFile;
    afatfsDeleteStackFrame_t stack[AFATFS_DELETE_MAX_DEPTH];
    uint8_t depth;
    uint8_t active;
    uint8_t targetLoadedFromInfo;
    afatfsDirEntryPointer_t deleteEntry;
    uint8_t deleteEntryCount;
    uint8_t deleteEntryIndex;
} afatfsDeleteObject_t;
```

The exact field names can change, but the state needs to represent:

- callback and active flag;
- requested display name and match mode;
- selected target object;
- one temporary file/directory handle loaded from a directory entry;
- saved parent directory/finder frames;
- depth limit;
- entry-run deletion cursor.

Comment for `AFATFS_DELETE_MAX_DEPTH`:

```c
/*
 * Maximum directory nesting deleted by one recursive delete operation.
 *
 * What: Bounds the explicit delete stack used instead of C recursion. Each
 * level stores one parent directory snapshot, one object finder, and the child
 * object being processed.
 *
 * Why: SD cards can contain arbitrary host-created directory trees, but this
 * firmware runs with a small embedded stack and a small open-file pool. A fixed
 * depth limit keeps the delete state machine deterministic. Product-owned
 * objects are shallow today: Kit/NNN contains files only, and future Scene/NNN
 * contains a small fixed set of child directories/files.
 *
 * Affiliates/clients: afatfs_deleteObjectContinue(), filesystem overwrite
 * saves, and the hardware test checklist. If a host-created tree exceeds this
 * limit, delete must fail cleanly rather than partially walking with C
 * recursion.
 */
```

Comment for delete stack frame:

```c
/*
 * One parent directory frame for recursive delete traversal.
 *
 * What: Captures the directory handle and object finder at the level above the
 * current target. When a child directory is entered, the parent frame lets the
 * state machine return and continue scanning siblings after the child has been
 * emptied and removed.
 *
 * Why: asyncfatfs has a single global currentDirectory and only three ordinary
 * open-file handles. Holding a conventional recursive call stack or one open
 * handle per level would be fragile. This frame stores just enough state to
 * re-establish the parent scan after a child delete completes.
 *
 * Affiliates/clients: afatfs_findFirstObject(), afatfs_findNextObject(),
 * afatfs_findLastObject(), afatfs_chdir(), and afatfsDeleteObject_t.depth.
 */
```

Comment for delete operation state:

```c
/*
 * Filesystem-global recursive delete state.
 *
 * What: Owns one asynchronous delete-object operation outside the ordinary
 * openFiles[] operation union. It can scan the current directory, enter child
 * directories, truncate files, mark SFN/LFN entry runs deleted, and restore the
 * parent directory before invoking callback.
 *
 * Why: Recursive delete mutates directory traversal state and currentDirectory.
 * Queuing it on a normal afatfsFile_t would make it compete with the same
 * currentDirectory handle it must navigate. A global state machine keeps the
 * operation serialized under afatfs_poll(), matching how filesystem.c already
 * assumes one storage operation at a time.
 *
 * Key fields:
 * - displayName/matchMode: requested component when deleting by name.
 * - target: current object selected for deletion; includes SFN/LFN entry
 *   pointers from the object iterator.
 * - targetFile: temporary handle loaded from target's SFN entry so existing
 *   truncate/free-cluster logic can be reused.
 * - stack/depth: explicit parent directory stack for directory recursion.
 * - deleteEntry/deleteEntryCount/deleteEntryIndex: cursor for marking the LFN
 *   fragment run and final SFN entry as deleted.
 *
 * Affiliates/clients: public afatfs_deleteObject_lfn(),
 * afatfs_deleteObjectContinue(), afatfs_poll(), afatfs_findNextObject(),
 * afatfs_ftruncateContinue(), and low-level directory-entry dirtying helpers.
 */
```

Add this field to `afatfs_t`:

```c
afatfsDeleteObject_t deleteObject;
```

Comment for the field in `afatfs_t`:

```c
/*
 * Active recursive delete operation.
 *
 * What: A single filesystem-global delete state machine, separate from
 * openFiles[] and currentDirectory.operation.
 *
 * Why: Delete-recreate overwrite saves need to remove directories while owning
 * currentDirectory traversal. Keeping this state in afatfs_t lets afatfs_poll()
 * advance it in the same single-threaded context as cache/FAT/file operations.
 *
 * Affiliates/clients: afatfs_deleteObject_lfn(), afatfs_poll(), filesystem.c
 * save overwrite phases.
 */
```

### Required Helper: Dot-Prefixed Object Visibility Fix

Change `afatfs_findNextObject()` before recursive delete work depends on it.

Current code derives `object->shortName`, then skips when
`object->shortName[0] == '.'`. That hides ordinary SFN entries such as
`.DS_Store`. The object iterator should skip only structural `.` and `..`
directory entries.

Add a helper:

```c
static uint8_t afatfs_isStructuralDotEntry(const fatDirectoryEntry_t *entry);
```

Comment for helper:

```c
/*
 * Detect structural "." and ".." directory entries.
 *
 * What: Returns nonzero only for the FAT entries that represent the current
 * directory and parent directory inside a subdirectory.
 *
 * Why: Names beginning with '.' are ordinary FAT objects unless they are the
 * exact structural "." or ".." entries. The object iterator used to skip every
 * display name whose first character was '.', which made host files such as
 * .DS_Store invisible to scans and would make recursive delete leave them
 * behind. Recursive delete must remove every concrete child object.
 *
 * Inputs: raw SFN directory entry. LFN fragments are filtered before this
 * helper is called, so the check can use the 11-byte filename field directly.
 *
 * Outputs/effects: no mutation.
 *
 * Affiliates/clients: afatfs_findNextObject(), File/Dir diagnostics, product
 * scanners, and recursive delete child iteration.
 */
```

Important condition comment for the actual check:

```c
/*
 * Match only the exact SFN structural entries.
 *
 * "." is filename[0] == '.' and filename[1..10] all spaces. ".." is
 * filename[0..1] == ".." and filename[2..10] all spaces. A normal file named
 * ".DS_Store" has additional non-space bytes and must remain visible.
 */
```

Replace the current `object->shortName[0] == '.'` skip with:

```c
if (afatfs_isStructuralDotEntry(entry)) {
    afatfs_objectScanReset(finder);
    continue;
}
```

Comment for the replacement in the iterator:

```c
/*
 * Hide only structural dot entries.
 *
 * What: "." and ".." are traversal metadata, not user objects. Ordinary
 * dot-prefixed files and directories stay visible.
 *
 * Why: Product scanners may ignore files that do not match their format, but
 * asyncfatfs must enumerate the exact FAT contents. Recursive delete relies on
 * this loop to remove every child of a directory, including host-created
 * dot-prefixed files.
 *
 * Affiliates/clients: filesystem File/Dir diagnostics, Kit/Instrument scans,
 * and afatfs_deleteObjectContinue().
 */
```

### Required Helper: Load Handle From Object Info

Add an internal helper to build a temporary `afatfsFile_t` from the selected
object's SFN entry pointer.

Recommended shape:

```c
static afatfsOperationStatus_e afatfs_loadFileFromObjectInfo(
    afatfsFile_t *file,
    const afatfsObjectInfo_t *object);
```

Implementation behavior:

1. cache `object->sfnEntry.sectorNumberPhysical` for read;
2. locate `entryIndex`;
3. validate the raw entry still exists and is not deleted/terminator/LFN;
4. initialize `file` with `afatfs_initFileHandle(file)`;
5. set `mode = AFATFS_FILE_MODE_READ | AFATFS_FILE_MODE_WRITE`;
6. copy `directoryEntryPos = object->sfnEntry`;
7. call `afatfs_fileLoadDirectoryEntry(file, entry)`;
8. set directory handles' `logicalSize`/`physicalSize` correctly enough for
   scanning.

Comment for the helper:

```c
/*
 * Open an already-scanned FAT object into a temporary handle.
 *
 * What: Re-reads the object's owning SFN entry and populates an afatfsFile_t
 * without performing a display-name search.
 *
 * Why: Recursive delete already gets exact object identity from
 * afatfs_findNextObject(). Reusing that identity avoids a second lookup that
 * could resolve a different same-display object on damaged or surprising
 * media. The temporary handle lets delete reuse existing cluster-chain and
 * directory-scan helpers.
 *
 * Inputs: object->sfnEntry identifies the physical SFN entry in the current
 * directory. object->kind tells callers whether the resulting handle is a file
 * or directory. The caller must ensure currentDirectory still refers to the
 * directory that contains the entry.
 *
 * Outputs/effects: file receives type, firstCluster, size, mode, attrib, and
 * directoryEntryPos. No openFiles[] slot is consumed.
 *
 * Affiliates/clients: afatfs_deleteObjectContinue(), afatfs_fileLoadDirectoryEntry(),
 * afatfs_ftruncateContinue(), afatfs_chdir(), and directory recursion.
 */
```

Important validation comment:

```c
/*
 * Revalidate the SFN entry before mutating it.
 *
 * The object record may have been captured earlier in the same async operation.
 * A clean card should not change underneath us, but rechecking deleted,
 * terminator, LFN, and volume-label markers prevents a stale object record from
 * being treated as a normal file handle after an error path or unexpected host
 * mutation.
 */
```

Important mode assignment comment:

```c
/*
 * Delete needs both read and write semantics.
 *
 * Read is required to scan directory children and follow cluster chains. Write
 * is required because truncate/delete updates FAT entries and parent directory
 * entries. This temporary handle is not exposed to callers, so it does not use
 * the public fopen mode parser.
 */
```

### Required Helper: Mark Arbitrary Directory Entry Deleted

`afatfs_saveDirectoryEntry(...DELETED)` only deletes the current file handle's
SFN entry. Recursive delete also has to delete LFN fragments.

Add:

```c
static afatfsOperationStatus_e afatfs_markDirectoryEntryDeleted(
    const afatfsDirEntryPointer_t *pos);
```

Comment for helper:

```c
/*
 * Mark one raw directory entry deleted.
 *
 * What: Loads the sector containing pos, writes FAT_DELETED_FILE_MARKER into
 * filename[0], marks the cache sector dirty, and returns an async status.
 *
 * Why: VFAT long names are separate directory entries immediately before the
 * owning SFN entry. Deleting only the SFN entry leaves stale LFN fragments that
 * can confuse scans and host tools. Recursive delete needs a small raw-entry
 * helper that can delete both LFN entries and the final SFN entry.
 *
 * Inputs: pos is a physical directory-entry pointer returned by
 * afatfs_findNextObject() or derived by stepping through its LFN run.
 *
 * Outputs/effects: one directory entry is marked deleted in the cached sector.
 * Cluster chains are not touched here; callers must truncate/free data clusters
 * before deleting the entry run.
 *
 * Affiliates/clients: afatfs_deleteEntryRunContinue(),
 * afatfs_findNextObject(), afatfs_ftruncateContinue(), and future rename/replace
 * code.
 */
```

Important mutation comment:

```c
/*
 * Deletion marker is only the first byte.
 *
 * FAT delete semantics leave the rest of the 32-byte entry unchanged. This is
 * enough for scanners to ignore it and keeps the operation one byte plus dirty
 * flag instead of rewriting timestamps, attributes, or cluster fields.
 */
```

### Required Helper: Advance Directory Entry Pointer

To delete an LFN run plus SFN entry, code needs to move from
`lfnFirstEntry` through `lfnEntryCount + 1` entries, possibly crossing sector
boundaries.

Add:

```c
static uint8_t afatfs_advanceDirectoryEntryPointer(
    afatfsDirEntryPointer_t *pos);
```

Comment:

```c
/*
 * Advance a physical directory-entry pointer by one entry.
 *
 * What: Moves pos to the next 32-byte directory entry, including crossing to
 * the next physical sector when entryIndex reaches
 * AFATFS_FILES_PER_DIRECTORY_SECTOR.
 *
 * Why: LFN fragments and their owning SFN are contiguous raw directory entries.
 * Delete must mark every entry in that run without invoking afatfs_findNext(),
 * because findNext skips, interprets, and retains scanner state rather than
 * walking a known physical run.
 *
 * Inputs: pos points at an entry in a directory sector. The caller guarantees
 * the run is sector-contiguous as recorded by afatfs_findNextObject().
 *
 * Outputs/effects: pos is updated in place. Returns zero if the pointer cannot
 * be advanced safely.
 *
 * Affiliates/clients: afatfs_deleteEntryRunContinue() and future LFN rename
 * code.
 */
```

Mathematical comment for sector crossing:

```c
/*
 * One FAT directory entry is 32 bytes and this implementation uses 512-byte
 * sectors, so AFATFS_FILES_PER_DIRECTORY_SECTOR entries fit in one sector.
 * entryIndex is zero-based; after index 15 the next entry is index 0 of the
 * following physical sector.
 */
```

### Required Helper: Delete LFN/SFN Entry Run

Add:

```c
static afatfsOperationStatus_e afatfs_deleteEntryRunContinue(
    afatfsDeleteObject_t *op);
```

Behavior:

- if object has LFN, start at `lfnFirstEntry` and delete
  `lfnEntryCount + 1` entries;
- if no LFN, start at `sfnEntry` and delete one entry;
- mark one entry per call or per available cache sector;
- tolerate `IN_PROGRESS`;
- finish only after all entries in the run are marked deleted.

Comment:

```c
/*
 * Delete the VFAT entry run for the current object.
 *
 * What: Marks every raw directory entry belonging to the object as deleted:
 * all LFN fragments first, then the owning SFN entry. Objects without a long
 * name delete only their SFN entry.
 *
 * Why: FAT object identity is a chain of optional LFN entries plus one SFN
 * entry. Existing afatfs_funlink() only deletes the SFN entry. Save overwrite
 * and future rename/replace must remove the whole entry run so scans, desktop
 * readers, and subsequent create operations do not see stale name fragments.
 *
 * Inputs: op->target contains sfnEntry, lfnFirstEntry, and lfnEntryCount from
 * afatfs_findNextObject(). op->deleteEntry/deleteEntryCount/deleteEntryIndex
 * are the progress cursor.
 *
 * Outputs/effects: raw directory entries are marked with FAT_DELETED_FILE_MARKER
 * and cache sectors are dirtied. No cluster chains are freed here; file or
 * directory data must already have been truncated/emptied.
 *
 * Affiliates/clients: afatfs_deleteObjectContinue(), afatfs_markDirectoryEntryDeleted(),
 * afatfs_advanceDirectoryEntryPointer(), afatfs_findNextObject(), and future
 * rename/replace.
 */
```

Loop comment:

```c
/*
 * Mark one known raw entry at a time.
 *
 * The loop intentionally uses the physical run recorded during object
 * enumeration instead of re-scanning by name. LFN fragments do not carry object
 * attributes or cluster chains; they are deleted solely because their checksum
 * was validated against the SFN entry when target was selected.
 */
```

Count comment:

```c
/*
 * Include the owning SFN entry in the count.
 *
 * lfnEntryCount counts only VFAT fragments. The visible object is not removed
 * until the final SFN entry is also marked deleted, so long-name objects delete
 * lfnEntryCount + 1 raw entries.
 */
```

### Required Helper: Match Object By Display Name

Add an internal helper so delete-by-name and open-by-name use matching logic
that is consistent with the object iterator:

```c
static uint8_t afatfs_objectDisplayMatches(
    const afatfsObjectInfo_t *object,
    const char *displayName,
    afatfsMatchMode_t matchMode);
```

Comment:

```c
/*
 * Compare a resolved object display name with a requested component.
 *
 * What: Applies the caller's case policy to object->displayName, which already
 * represents either a checksum-verified LFN or a case-preserved SFN display
 * spelling.
 *
 * Why: Delete-by-name must resolve the same object that File/Dir browsing and
 * fopen_lfn() would resolve. Centralizing this comparison prevents delete from
 * accidentally matching raw uppercase SFN bytes while the UI shows mixed-case
 * LFN text.
 *
 * Inputs: object from afatfs_findNextObject(), requested displayName, and match
 * mode.
 *
 * Outputs/effects: returns nonzero on match. No mutation.
 *
 * Affiliates/clients: afatfs_deleteObjectContinue(), afatfs_fopen_lfn()
 * matching policy, fat_compareDisplayName(), and File/Dir diagnostics.
 */
```

### Delete State Machine Phases

Implement:

```c
static void afatfs_deleteObjectContinue(void);
```

Call it from `afatfs_poll()` after `afatfs_fileOperationsPoll()` or before it,
but keep ordering deliberate. Recommendation: poll ordinary file operations
first so a truncate suboperation queued by delete can advance, then advance the
delete state machine. If delete never queues public operations and calls
`afatfs_ftruncateContinue()` directly, either order can work, but document it.

Comment at `afatfs_poll()` call site:

```c
/*
 * Advance the filesystem-global delete operation in the same poll context as
 * file and cache operations.
 *
 * Recursive delete owns currentDirectory traversal and may call existing FAT
 * truncate helpers. Polling it here keeps all SD mutation single-threaded and
 * prevents Menu/filesystem.c from needing a second progress loop.
 */
```

#### Phase: FIND_TARGET

Behavior:

- initialize object finder on `afatfs.currentDirectory`;
- scan with `afatfs_findNextObject()`;
- match `displayName` with helper;
- on match, copy object into `op->target`;
- release finder with `afatfs_findLastObject()`;
- fail if not found.

Comment:

```c
/*
 * Resolve the requested display component inside the current directory.
 *
 * What: Walks concrete objects using afatfs_findNextObject() and compares the
 * resolved display name under the requested case policy.
 *
 * Why: Delete must agree with browse and open semantics. Scanning concrete
 * objects also gives us the SFN and LFN entry pointers required for later
 * entry-run deletion.
 *
 * Affiliates/clients: afatfs_findNextObject(), afatfs_objectDisplayMatches(),
 * afatfs_deleteEntryRunContinue(), File/Dir diagnostics, and filesystem.c save
 * overwrite phases.
 */
```

Loop comment:

```c
/*
 * Continue until a concrete object matches or the directory is exhausted.
 *
 * AFATFS_OPERATION_IN_PROGRESS means the cache/SD layer is busy and this phase
 * must retry without advancing logical state. AFATFS_OBJECT_NONE is successful
 * directory exhaustion, not an error from the scanner.
 */
```

#### Phase: OPEN_TARGET

Behavior:

- call `afatfs_loadFileFromObjectInfo(&op->targetFile, &op->target)`;
- fail if root or invalid entry;
- branch to file truncate or directory enter.

Comment:

```c
/*
 * Convert the selected object into a temporary file handle.
 *
 * What: Loads cluster, size, type, attributes, and directoryEntryPos from the
 * selected SFN entry.
 *
 * Why: Existing FAT helpers operate on afatfsFile_t. Building a private handle
 * lets recursive delete reuse cluster-chain truncation and directory traversal
 * without allocating an openFiles[] slot or performing another name lookup.
 *
 * Affiliates/clients: afatfs_loadFileFromObjectInfo(),
 * afatfs_ftruncateContinue(), afatfs_chdir(), and delete entry-run cleanup.
 */
```

Branch comment:

```c
/*
 * Directories must be emptied before their parent entry is deleted.
 *
 * FAT has no "delete non-empty directory" primitive. Files can go straight to
 * cluster truncation; directories must be entered, scanned, and have each child
 * object removed first.
 */
```

#### Phase: ENTER_DIRECTORY

Behavior:

- fail if depth reaches `AFATFS_DELETE_MAX_DEPTH`;
- push current directory and finder state as a parent frame;
- `afatfs_chdir(&op->targetFile)`;
- initialize child finder on new current directory;
- branch to scan child.

Comment:

```c
/*
 * Enter a child directory while preserving parent traversal state.
 *
 * What: Pushes the current directory and finder onto the explicit delete stack,
 * then changes currentDirectory to the child directory selected for deletion.
 *
 * Why: Recursive delete needs to return to the parent after the child is empty
 * so it can delete the child's parent entry and continue scanning siblings.
 * The explicit stack avoids C recursion and avoids holding multiple open file
 * handles.
 *
 * Affiliates/clients: afatfs_chdir(), afatfs_findFirstObject(),
 * afatfsDeleteStackFrame_t, AFATFS_DELETE_MAX_DEPTH, and SCAN_CHILD.
 */
```

Depth guard comment:

```c
/*
 * Enforce bounded recursion before changing directory.
 *
 * If a host-created tree is deeper than the supported stack, fail cleanly while
 * still in the parent context. This avoids getting stranded inside a child
 * directory without a frame that can restore the caller-visible CWD.
 */
```

#### Phase: SCAN_CHILD

Behavior:

- iterate children in current directory with `afatfs_findNextObject()`;
- if child exists, copy to `target` and branch to OPEN_TARGET/DELETE_CHILD;
- if exhausted, release finder and branch to LEAVE_DIRECTORY.

Comment:

```c
/*
 * Scan the current directory for the next child to delete.
 *
 * What: Returns every concrete file/directory child, including dot-prefixed
 * ordinary objects. Structural "." and ".." are filtered by the object
 * iterator.
 *
 * Why: A directory can only be removed after every child object is gone. This
 * loop deliberately deletes host-created leftovers too, because overwrite save
 * semantics require the old target directory tree to be fully removed before
 * recreating it.
 *
 * Affiliates/clients: afatfs_findNextObject(), dot-entry visibility fix,
 * DELETE_CHILD/OPEN_TARGET, and filesystem Kit/Scene overwrite saves.
 */
```

Child selection comment:

```c
/*
 * Reuse the same target field for nested children.
 *
 * The parent frame stores the scan state needed to resume later. The active
 * target always describes the child currently being removed, whether that child
 * is a file or another directory.
 */
```

#### Phase: DELETE_CHILD / Nested Dispatch

This can simply reuse `OPEN_TARGET` after a child is selected. The plan should
keep one path for target deletion so top-level and nested objects share all
logic.

Comment:

```c
/*
 * Delete top-level and child objects through the same path.
 *
 * What: Once SCAN_CHILD selects a child object, the state machine falls back to
 * the same OPEN_TARGET branch used for the original request.
 *
 * Why: Files, directories, LFN entry runs, and cluster chains have identical
 * deletion rules at every depth. One path prevents shallow and nested deletes
 * from drifting apart.
 */
```

#### Phase: TRUNCATE_FILE

Behavior:

- initialize `op->targetFile.operation.state.truncateFile` or queue a truncate
  operation on the private handle;
- use `afatfs_ftruncateContinue(&op->targetFile, false)` directly, or a new
  lower-level helper that frees cluster chain without marking the SFN deleted;
- after success, branch to DELETE_ENTRY_RUN.

Important: do not use `afatfs_funlink()` as-is, because it closes/destroys a
handle and deletes only the SFN entry.

Comment:

```c
/*
 * Free the target file's cluster chain before deleting its directory entries.
 *
 * What: Reuses the existing truncate state machine to set firstCluster/fileSize
 * to zero and mark every FAT cluster in the chain free.
 *
 * Why: Directory entries are only names and metadata. The data clusters must be
 * released before the SFN/LFN entries disappear or the card would leak space.
 * afatfs_funlink() cannot be used directly because it deletes only the SFN
 * entry and then closes an ordinary public handle.
 *
 * Affiliates/clients: afatfs_ftruncateContinue(), AFATFS_TRUNCATE_FILE_*,
 * afatfs_FATGetNextCluster(), afatfs_FATSetNextCluster(), and
 * DELETE_ENTRY_RUN.
 */
```

Comment for direct `ftruncateContinue` use:

```c
/*
 * Drive truncate on the private delete handle.
 *
 * The handle is not in openFiles[], so afatfs_fileOperationsPoll() will not
 * advance it. The delete state machine calls afatfs_ftruncateContinue()
 * directly and treats IN_PROGRESS as "retry this phase next poll".
 */
```

#### Phase: LEAVE_DIRECTORY

Behavior:

- current directory is empty;
- restore parent directory from stack frame;
- target remains the directory object that has just been emptied;
- branch to TRUNCATE_FILE or directly DELETE_ENTRY_RUN for directory.

Directories have a first cluster containing `.`/`..` and empty entries. That
cluster chain must be freed too. Reuse truncate/free-cluster logic on the
directory handle, but do not rely on directory `fileSize`.

Comment:

```c
/*
 * Return to the parent after a child directory is empty.
 *
 * What: Restores currentDirectory and the parent finder from the explicit stack
 * frame, then continues deletion of the now-empty child directory's own cluster
 * chain and parent entry run.
 *
 * Why: Child entries live in the child directory, but the child's SFN/LFN name
 * lives in the parent directory. The state machine must be back in the parent
 * before marking the child entry run deleted.
 *
 * Affiliates/clients: afatfs_chdir()-style currentDirectory copies,
 * afatfsDeleteStackFrame_t, afatfs_ftruncateContinue(), and
 * afatfs_deleteEntryRunContinue().
 */
```

Directory truncate comment:

```c
/*
 * Free the emptied directory's cluster chain.
 *
 * A directory's fileSize field is always zero, but firstCluster still owns the
 * cluster chain containing "."/".." and child entry sectors. After all children
 * are deleted, that cluster chain must be released just like a file's data
 * chain before the parent entry is marked deleted.
 */
```

#### Phase: DELETE_ENTRY_RUN

Behavior:

- initialize entry-run deletion cursor from active target;
- call `afatfs_deleteEntryRunContinue()`;
- if nested and parent scan has more work, return to SCAN_CHILD;
- if top-level, finish success.

Comment:

```c
/*
 * Remove the object's visible directory metadata.
 *
 * What: Marks the LFN fragment run and owning SFN entry deleted after all data
 * clusters or child directory contents have been freed.
 *
 * Why: This is the point where the object stops existing to scans. It must come
 * after truncate/recursive child removal so a power loss cannot leave a hidden
 * allocated cluster chain with no directory entry.
 *
 * Affiliates/clients: afatfs_deleteEntryRunContinue(),
 * afatfs_markDirectoryEntryDeleted(), parent SCAN_CHILD loop, and final
 * callback completion.
 */
```

#### Phase: FINISH_SUCCESS / FINISH_FAILURE

Behavior:

- release any active finder with `afatfs_findLastObject()`;
- restore original current directory if possible;
- clear delete active flag;
- call callback exactly once;
- leave `lastError` generic or add a more specific error enum later.

Comment:

```c
/*
 * Finish recursive delete and restore caller-visible state.
 *
 * What: Releases retained scan cache sectors, restores currentDirectory to the
 * directory that was active when delete was requested, clears deleteObject.active,
 * and invokes the callback exactly once.
 *
 * Why: filesystem.c save state machines assume asyncfatfs returns them to the
 * same parent directory context after helper operations. Even on failure, the
 * caller needs a deterministic state so it can return to root or report a save
 * error without inheriting a child-directory CWD.
 *
 * Affiliates/clients: afatfs_findLastObject(), afatfs_chdir(NULL) fallback,
 * filesystem.c overwrite phases, and afatfs_destroy() cleanup.
 */
```

Failure cleanup comment:

```c
/*
 * Failure may leave a partially deleted tree.
 *
 * Recursive delete is not atomic. If a cache/FAT error occurs after some child
 * objects have been removed, do not attempt speculative rollback. The safe
 * response is to release handles, restore the best-known parent/root directory,
 * report failure, and let the higher-level save abort before creating the
 * replacement object.
 */
```

### Public API Implementation Flow

`afatfs_deleteObject_lfn()` should:

1. reject NULL/empty names;
2. reject names containing `/` or `\`;
3. reject calls while filesystem is not ready;
4. reject calls if `afatfs.deleteObject.active` is nonzero;
5. reject calls while `afatfs.currentDirectory` is busy;
6. copy displayName into delete state;
7. copy `matchMode`;
8. snapshot original currentDirectory into stack frame 0 or a dedicated field;
9. set phase to `FIND_TARGET`;
10. set active;
11. call `afatfs_deleteObjectContinue()` once to begin work;
12. return true when queued.

Comment for public function:

```c
/*
 * Queue one recursive delete by display name.
 *
 * What: Validates a single-component request, captures the caller's current
 * directory, and starts the delete state machine.
 *
 * Why: Save overwrite code needs a nonblocking primitive it can post and then
 * poll through afatfs_poll(). Rejecting paths keeps asyncfatfs component-based,
 * matching fopen_lfn()/mkdir_lfn()/opendir_lfn().
 *
 * Affiliates/clients: filesystem.c delete wrapper, afatfs_deleteObjectContinue(),
 * afatfs.currentDirectory, and afatfs_poll().
 */
```

Input validation comment:

```c
/*
 * Keep delete component-based.
 *
 * Paths are still owned by higher layers that chdir one component at a time.
 * Accepting slashes here would create a second path parser with different
 * currentDirectory and case-sensitivity behavior.
 */
```

Busy guard comment:

```c
/*
 * Only one recursive delete may own currentDirectory.
 *
 * currentDirectory is global mutable traversal state. Starting a second delete
 * while one is active would corrupt the parent stack and callback ownership.
 */
```

### Changes To Existing asyncfatfs Functions

#### `afatfs_findNextObject()`

Required changes:

- replace broad dot-prefix skip with structural dot helper;
- ensure `lfnFirstEntry`/`lfnEntryCount` are populated only for verified LFN
  chains;
- leave ordinary dot-prefixed files and directories visible.

Comment additions are described above.

#### `afatfs_funlink()`

Do not change public semantics unless needed. It can remain a file-handle delete
for old callers, but document that it is SFN-only and not the primitive for
production overwrite saves.

Comment to add near `afatfs_funlink()`:

```c
/*
 * Legacy opened-file unlink.
 *
 * This removes the opened file's cluster chain and SFN entry, then closes the
 * handle. It does not know about the preceding LFN run and it does not recurse
 * into directories. New overwrite-save code must use afatfs_deleteObject_lfn()
 * so long names and directory trees are removed completely.
 */
```

Optionally, after the new entry-run helper exists, `afatfs_funlink()` can be
upgraded to delete the LFN run for handles opened through LFN/object scan, but
current `afatfsFile_t` does not retain `lfnFirstEntry`/`lfnEntryCount`. Do not
block recursive delete on retrofitting this unless a caller needs it.

#### `afatfs_destroy()`

Ensure destroy/teardown clears or ignores active delete state. If a delete is
active during card removal or filesystem destroy, callback should not fire after
state has been torn down.

Comment:

```c
/*
 * Drop any in-flight recursive delete during filesystem teardown.
 *
 * Card removal/destroy invalidates cached directory pointers and FAT state.
 * The delete operation cannot safely continue or call back after the filesystem
 * is being destroyed, so clear the active flag with the rest of asyncfatfs
 * state.
 */
```

### Changes To filesystem.h

No public product API is required yet unless tests need one. If a test/delete
diagnostic is added, expose it behind the same temporary File/Dir diagnostic
style.

Recommended internal comments only, unless adding test functions.

If a public facade is added:

```c
bool filesystem_requestDeleteTestObject(const char *display_name,
                                        fs_completion_cb_t cb);
```

Comment:

```c
/*
 * Temporary File/Dir recursive-delete diagnostic.
 *
 * What: Deletes one root object by exact display name through asyncfatfs'
 * recursive delete primitive.
 *
 * Why: Recursive delete must be hardware-tested before musical overwrite saves
 * depend on it. Keeping this under filesystem.c preserves the rule that Menu
 * and Preset do not call asyncfatfs directly.
 *
 * Affiliates/clients: optional diagnostic Menu entry, afatfs_deleteObject_lfn(),
 * filesystem_tick(), and the File/Dir scan caches.
 */
```

### Changes To filesystem.c

Add internal operation kinds only if the wrapper needs to be independently
pollable:

```c
FS_INTERNAL_OP_DELETE_OBJECT,
FS_INTERNAL_OP_DELETE_KIT_SLOT,
```

For save-internal delete phases, it may be cleaner not to add public operations
and instead add phases inside `filesystem_saveKitDirectory_tick()` and later
save writers:

1. chdir root;
2. open `Kit/`;
3. chdir `Kit/`;
4. if slot occupied, call `afatfs_deleteObject_lfn()` or by-info helper on
   cached object;
5. wait for delete callback/latch;
6. create replacement folder.

Use a small latch:

```c
static uint8_t op_delete_done;
static uint8_t op_delete_ok;
```

Comment for latch variables:

```c
/*
 * asyncfatfs delete completion latch.
 *
 * What: Bridges afatfs_deleteObject_lfn()'s callback into the outer filesystem
 * save state machine. op_delete_done means the callback fired; op_delete_ok
 * records whether asyncfatfs is still ready and the delete operation reported
 * success.
 *
 * Why: filesystem.c save ticks are explicit phase machines. They need a
 * nonblocking way to wait for recursive delete before creating the replacement
 * object.
 *
 * Affiliates/clients: filesystem_saveKitDirectory_tick(),
 * future Scene/Instrument/Bank/Effect save ticks, and on_delete_complete().
 */
```

Comment for delete callback:

```c
/*
 * Complete one asyncfatfs recursive delete sub-operation.
 *
 * What: Records that the delete helper finished so the outer filesystem save
 * phase can continue on the next tick.
 *
 * Why: asyncfatfs callbacks run from filesystem_tick()/afatfs_poll() context.
 * Keeping the callback to a latch avoids re-entering the save state machine
 * while asyncfatfs is still unwinding its own operation.
 *
 * Affiliates/clients: op_delete_done/op_delete_ok and every save phase that
 * waits for delete-recreate overwrite behavior.
 */
```

Comment for save phase that starts delete:

```c
/*
 * Remove the occupied target before creating the replacement object.
 *
 * What: If the selected slot/file already exists, post recursive delete for the
 * old object and wait for completion before mkdir/fopen of the new object.
 *
 * Why: Overwrite save semantics are delete-recreate. This guarantees old
 * unreferenced Kit member files, stale Scene children, and future Effect/Bank
 * artifacts cannot survive merely because the new save did not overwrite their
 * names.
 *
 * Affiliates/clients: asyncfatfs delete primitive, Kit/Scene scan caches,
 * persistent Menu OW display, and later replacement create phases.
 */
```

Comment for waiting phase:

```c
/*
 * Wait for recursive delete to finish before creating replacement data.
 *
 * The delete callback only sets a latch. This phase is the first point where
 * filesystem.c decides whether to continue to mkdir/fopen or abort the save.
 * That keeps all save error handling inside the outer operation.
 */
```

### Kit Save Integration Point

Current `filesystem_saveKitDirectory_tick()` opens an existing occupied folder
by `kit_slot_open_name[op_slot]`. Replace that occupied-slot branch with:

1. in root, open/chdir `Kit/`;
2. if `kit_slot_present[op_slot]`, delete the old numbered directory;
3. wait for delete;
4. create the new `op_save_kit_display_name` with `afatfs_mkdir_lfn()`;
5. continue with member file writes.

Comment replacing the current occupied-folder open comment:

```c
/*
 * Occupied Kit slots are replaced, not reused.
 *
 * What: The old Kit/<NNN ...>/ directory is recursively deleted before the new
 * Kit/<NNN edited-name>/ directory is created.
 *
 * Why: The three-digit prefix is the slot identity, but the text name and child
 * files belong to the current save operation. Reusing the old directory alias
 * would preserve the old visible name and can leave stale child files. The
 * delete-recreate rule makes the SD card match exactly what the user saved.
 *
 * Affiliates/clients: kit_slot_present/open_name from scan, Menu's OW
 * indicator, filesystem_makeKitDirectoryDisplayName(), afatfs_deleteObject_lfn(),
 * afatfs_mkdir_lfn(), and Kit browser cache update after successful save.
 */
```

### Instrument Save Integration Point

Current `filesystem_saveInstrument_tick()` opens target file with
`afatfs_fopen_lfn(..., "w", ...)`, which truncates existing files in place.
After delete exists:

1. chdir root;
2. mkdir/chdir `Instrument/`;
3. if exact target file exists, delete it;
4. create new target with `afatfs_fopen_lfn(..., "w", ...)`;
5. stream contents.

Comment for future Instrument overwrite delete phase:

```c
/*
 * Delete an existing standalone Instrument file before rewrite.
 *
 * What: If Instrument/<edited-stem.ext> already exists, remove that whole file
 * object, including any LFN entries, before creating the replacement file.
 *
 * Why: Standalone Instrument Save is user-named and Menu already shows OW for
 * exact target collisions. Delete-recreate keeps file overwrite semantics
 * consistent with Kit/Scene directory overwrite and avoids relying on the old
 * fopen("w") truncate path for production saves.
 *
 * Affiliates/clients: Instrument scan cache, filename-existence helper,
 * afatfs_deleteObject_lfn(), afatfs_fopen_lfn(), and retained Instrument name
 * update on normal save completion.
 */
```

### Test Plan For Recursive Delete

Before Kit/Instrument save uses delete-recreate, test asyncfatfs delete with a
temporary filesystem diagnostic or controlled save-state harness.

Required tests:

- delete root LFN file;
- delete root SFN-only file;
- delete root empty directory;
- delete root directory containing files;
- delete root directory containing nested subdirectories;
- delete directory containing dot-prefixed files like `.DS_Store`;
- delete directory containing mixed LFN and SFN children;
- fail cleanly on a tree deeper than `AFATFS_DELETE_MAX_DEPTH`;
- after delete, `afatfs_sync()` completes and a remount/desktop read confirms
  objects are gone;
- unrelated root objects remain visible and readable.

Test comment for diagnostics:

```c
/*
 * Recursive delete diagnostic target.
 *
 * What: Exercises asyncfatfs delete on ordinary root objects before production
 * Kit/Scene overwrite saves depend on it.
 *
 * Why: Delete is destructive and non-atomic. Testing it against expendable
 * File/Dir diagnostic objects isolates filesystem correctness from musical
 * serialization bugs.
 *
 * Affiliates/clients: File/Dir scan caches, afatfs_deleteObject_lfn(),
 * afatfs_sync(), and later Kit Save delete-recreate phases.
 */
```

### Safety Rules

- Never delete root.
- Never delete structural `.` or `..`.
- Never proceed from a caller-built path string containing slashes. Keep the
  first implementation component-based.
- Preserve a bounded stack/state model. Avoid C recursion if it risks stack
  depth on deeply nested cards; use an explicit small stack of directory handles
  or directory identity records if needed.
- If the implementation cannot support arbitrary depth immediately, define and
  enforce a maximum depth with a clean error.
- Never leave a retained finder active across completion.
- Never create the replacement object after a delete failure.
- Never report save success until delete, recreate, write, close, and sync have
  all completed.

### Why This Comes First

Overwrite save semantics are now delete-recreate, not in-place overwrite.
Current Kit Save opens an occupied folder by its scanned alias and overwrites
authoritative child files while allowing stale unreferenced files to remain.
That is no longer the target behavior.

Recursive delete is required before these saves are considered correct:

- occupied Kit Save;
- future occupied Scene Save;
- future occupied Bank Save;
- any future directory-shaped Effect/FX-stack save;
- any file save whose overwrite policy should be delete-recreate rather than
  truncate-in-place.