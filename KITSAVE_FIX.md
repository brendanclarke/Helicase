# Kit Save Empty Folder Fix Audit

## Scope

This document is a reset-baseline code dive for the failure where normal
`Save:[Kit     ]` creates a Kit folder but leaves it empty. It is based on the
current source tree, not the prior Morph Save attempt.

## Work Log

- Implementation started from the reset-baseline tree. The first pass will fix
  asyncfatfs directory creation before changing higher-level Kit/Scene save
  behavior, because every save variant depends on `mkdir` returning an enterable
  directory handle.
- Patched `asyncfatfs.h` to document the mkdir/chdir contract. Patched
  `asyncfatfs.c` so newly-created short and LFN directories copy out the create
  callback, clear `CREATE_FILE`, and hand the handle to
  `afatfs_extendSubdirectory()` before callback success. Added in-place comments
  at the first-cluster publish path, the zero-fill loop, `"."` / `".."` setup,
  and the defensive zero-cluster directory-extension guard.
- Patched LFN open/create matching so a checksum-valid reconstructed display
  name is checked before generated-alias collision handling. The actual SFN
  alias is returned when an existing display object is opened. Type mismatches
  between `mkdir_lfn()` and `fopen_lfn()` now fail instead of creating a second
  visible object. Removed the obsolete single-purpose
  `afatfs_existingEntryMatchesRequest()` helper after folding the logic into the
  create scanner branch.
- Patched `filesystem_saveKitDirectory_tick()` so occupied Kit slots open the
  scan-cache alias instead of calling `mkdir_lfn()` again. Empty slots still
  create `NNN Name` with LFN metadata. The final cache update now distinguishes
  created folders from existing folders so the UI does not claim a rename that
  never happened.
- Patched `filesystem_saveSceneDirectory_tick()` with the same root Scene
  occupied-slot alias policy. Added adjacent comments for the embedded Scene Kit
  limitation: without a child scan cache or recursive replace/rename, a Scene
  overwrite with a changed display name may leave the old embedded Kit child
  directory behind.
- Verification: `make` completed and linked `build/lxr02.elf`; `make img`
  rebuilt `build/LXRV2_lxr02.img`; `git diff --check` completed cleanly. The
  only compile warnings observed were the pre-existing asyncfatfs `eraseCount`
  unused-parameter warning and the usual nano-libc syscall stub linker warnings.
- Regression report after case-preservation image: saving an empty Kit slot
  from the UI, editing `Empty` to `Smpty`, returning to Voice mode, powering
  off, and mounting the SD card on macOS did not show the expected numbered Kit
  folder. Re-read the actual Menu -> Preset -> Filesystem flow and found that
  `filesystem_finish(FS_STATUS_DONE)` reported completion immediately after the
  last `afatfs_fclose()` callback, but before asyncfatfs had drained dirty cache
  sectors to the card. Patched `afatfs_flush()` so it remains false while a
  sector write callback is still in flight, and patched `filesystem_finish()` so
  successful operations go through `FS_INTERNAL_OP_FLUSH_FINISH` before
  invoking the completion callback. This keeps the Save UI busy until FAT,
  directory, LFN, and file data sectors are no longer just cached/in transit.
- Verification after the completion-boundary patch: `git diff --check` passed;
  `make img` completed and wrote `build/LXRV2_lxr02.img`. The only compiler
  warning remained the existing asyncfatfs `eraseCount` unused parameter; linker
  warnings remained the existing nano-libc syscall stubs.

The broken behavior is in the shared save foundation:

- `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`
- `Core/Hardware/SD/asyncfatfs/asyncfatfs.h`
- `Core/Hardware/SD/filesystem.c`

`storageTypes.c/h` formats the text files correctly enough for this issue. The
empty-folder failure happens before the first instrument file is successfully
opened inside the target directory.

## Observed Failure

- Normal Kit Save creates a visible directory under `Kit/`.
- The directory contains no `kitset.kcg` and no instrument files.
- Saving an occupied slot can appear to create a second visible folder with the
  same long display name.
- Morph Save failed in the same shape because it reused the same Kit directory
  save path.
- Later hardware test: the UI could complete a Kit Save and then a quick
  power-off left no visible new Kit folder on a desktop mount. That is a
  separate completion-boundary bug: the writer reached logical close, but the
  dirty sector cache had not necessarily committed the directory entry and FAT
  sectors to removable media yet.

## Primary Root Cause

`afatfs_mkdir()` and `afatfs_mkdir_lfn()` both route through
`afatfs_createFileInternal()` with `FAT_FILE_ATTRIBUTE_DIRECTORY`. When a new
directory entry is created, the create state machine writes only the parent
directory entry and then reports success.

In the current code:

- `AFATFS_CREATEFILE_PHASE_CREATE_NEW_FILE` writes a short SFN entry and jumps
  to `AFATFS_CREATEFILE_PHASE_SUCCESS`.
- `AFATFS_CREATEFILE_PHASE_CREATE_NEW_LFN_FILE` writes the LFN fragments plus
  final SFN entry and jumps to `AFATFS_CREATEFILE_PHASE_SUCCESS`.
- `AFATFS_CREATEFILE_PHASE_SUCCESS` calls `afatfs_fseek(file, 0,
  AFATFS_SEEK_SET)` and then calls the open callback.

That is acceptable for regular files because the first `afatfs_fwrite()` can
allocate the first cluster lazily. It is not acceptable for directories because
callers immediately `chdir()` into the returned handle and then try to create
child entries through `afatfs.currentDirectory`.

The result is a visible directory entry with:

- `firstClusterHigh == 0`
- `firstClusterLow == 0`
- no allocated directory cluster
- no `"."` / `".."` entries

`filesystem_saveKitDirectory_tick()` then copies that zero-cluster handle into
the current directory at phase 8 and child file creation fails, leaving only the
visible parent entry behind.

This bug affects both:

- newly-created root `Kit/` or `Scene/` folders through short `afatfs_mkdir()`;
- newly-created numbered Kit/Scene folders and embedded Scene Kit folders
  through LFN create/open when the requested object is a directory.

## Secondary Root Cause

The LFN matcher is currently alias-first.

`afatfs_createFileContinue()` only calls
`afatfs_existingEntryMatchesRequest()` after this raw SFN comparison succeeds:

```c
strncmp(entry->filename, (char*) opState->filename, FAT_FILENAME_LENGTH) == 0
```

That means an existing LFN object with the same visible display name but a
different generated 8.3 alias is missed. The create path can then choose a new
alias and write another visible folder with the same long name.

The current scanner also reconstructs LFN text without validating the LFN
checksum against the following SFN entry. That is tolerable for the current
alias-first use, but not for a display-name-first open policy.

## Exact Code Changes Required

### 0. `asyncfatfs.c/h` + `filesystem.c`: Do not report save success until dirty sectors are physically drained

Add a stricter persistence boundary after successful filesystem operations.

`afatfs_flush()` previously returned true as soon as `cacheDirtyEntries == 0`.
That was insufficient because `cacheDirtyEntries` is decremented when a sector
write is queued, while `cacheFlushInProgress` stays true until
`afatfs_sdcardWriteComplete()` reports that the card accepted the sector. The
visible symptom is exactly the reported UI path: Kit Save can close all files,
Menu can reset the Save page, the user can power off, and the desktop reader can
see no new folder because the critical FAT/directory sectors were still cached
or in flight.

Required code-adjacent comment text in `Core/Hardware/SD/asyncfatfs/asyncfatfs.h`
above `afatfs_flush()`:

```c
/*
 * Drain dirty cache sectors to the SD card.
 *
 * Return value: true only when there are no dirty cache entries and no sector
 * write callback is still pending. Save completion code depends on this stricter
 * boundary so UI-visible "done" cannot outrun the final FAT/directory sectors
 * that make a newly-created folder visible to a desktop reader after power-off.
 */
```

Required code-adjacent comment text in `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`
on `afatfs_flush()`:

```c
/**
 * Attempt to flush dirty cache pages out to the sdcard.
 *
 * The return value is intentionally stricter than "no dirty sectors were found
 * this pass": it remains false while a previously-started sector write is still
 * waiting for sdcard_poll() to deliver afatfs_sdcardWriteComplete(). Filesystem
 * save completion uses this as the last persistence boundary before it tells
 * Menu/Preset that a save is done; without the in-flight check, the UI can
 * return to normal while the directory entry or FAT sector that names a new Kit
 * folder is still only in transit.
 */
```

Important return statement comment:

```c
/*
 * cacheDirtyEntries is decremented when the write is queued, not when the
 * card reports completion. Keep reporting "not flushed" across that gap so
 * callers that care about removable-media visibility can wait until the
 * write callback has either synced the sector or re-marked it dirty.
 */
```

Add `FS_INTERNAL_OP_FLUSH_FINISH` as a private filesystem operation and keep an
`op_flush_final_status` field beside the other operation state.

Required code-adjacent comment text in `Core/Hardware/SD/filesystem.c` beside
`op_flush_final_status`:

```c
/*
 * Deferred completion status used by FS_INTERNAL_OP_FLUSH_FINISH.
 *
 * Successful operations now finish in two steps: the individual state machine
 * closes its files, then the filesystem facade keeps status BUSY while
 * asyncfatfs drains dirty FAT, directory, and data sectors to the SD card. This
 * retained status is the value reported to Preset/Menu only after afatfs_flush()
 * confirms that no dirty or in-flight write remains. The affiliate boundary is
 * filesystem_finish(), filesystem_flushFinish_tick(), and completion_callback.
 */
```

Split the old `filesystem_finish()` into:

- `filesystem_complete(final_status)`: the old behavior that publishes DONE or
  ERROR and invokes the completion callback.
- `filesystem_finish(final_status)`: schedules `FS_INTERNAL_OP_FLUSH_FINISH`
  when `final_status == FS_STATUS_DONE`; otherwise keeps the existing immediate
  error path.
- `filesystem_flushFinish_tick()`: repeatedly calls `afatfs_flush()` until the
  cache is drained, then calls `filesystem_complete(op_flush_final_status)`.

Required code-adjacent comment text in `filesystem_finish()`:

```c
/*
 * Do not publish a successful filesystem operation until asyncfatfs has
 * persisted every dirty sector the operation left behind.
 *
 * Inputs: final_status is the state-machine result after all files have
 * been closed. Output: status remains FS_STATUS_BUSY and the normal
 * completion callback is deferred. This prevents the Save UI from
 * resetting before the directory entry, FAT allocation, and short/long
 * name sectors for a new Kit folder are visible on a freshly-mounted SD
 * card. Error exits keep their historical immediate callback path
 * because some failures may leave an open handle or locked cache sector
 * that cannot be safely drained here.
 */
```

Required leading comment text in `filesystem_flushFinish_tick()`:

```c
/*
 * Final save/load persistence gate.
 *
 * filesystem_tick() has already called afatfs_poll() for this foreground
 * pass. Calling afatfs_flush() here either starts/continues one bounded
 * cache write or confirms that no dirty sector and no in-flight sector
 * write remains. Only then is completion_callback invoked, so Preset/Menu
 * cannot display save completion while the removable card still lacks the
 * sectors that make the saved object discoverable.
 */
```

Why this must happen: the user-facing Save page and the physical removable-media
state were not synchronized. Closing a file updates cached FAT/directory/data
sectors, but the actual SD writeback is still asynchronous. The existing main
loop continued polling SD in the background, but the UI allowed the user to
leave and power off before the background cache had committed the save.

Inputs: any successful filesystem state machine result, especially
`filesystem_saveKitDirectory_tick()` after phase 24. Outputs:
`completion_callback` is delayed until `afatfs_flush()` reports fully drained.

Affiliates: `menu_pollPresetStatus()`, `preset_completeFilesystemOp()`,
`filesystem_tick()`, `afatfs_poll()`, `afatfs_sdcardWriteComplete()`,
`afatfs_destroy()`. `afatfs_destroy()` already checked `cacheFlushInProgress`,
which is the precedent for the stricter normal-completion boundary.

### 1. `asyncfatfs.h`: Strengthen the public mkdir contract

No signature change is required.

Add a leading comment above `afatfs_mkdir()` / `afatfs_mkdir_lfn()` explaining
that directory callbacks receive a handle that is safe to pass to
`afatfs_chdir()`. This matters because the implementation change below makes
directory creation stricter than regular file creation.

Comment text to place in `Core/Hardware/SD/asyncfatfs/asyncfatfs.h`:

```c
/*
 * Directory create/open contract.
 *
 * The callback receives either NULL or a directory handle that is immediately
 * safe to pass to afatfs_chdir(). For newly-created subdirectories that means
 * asyncfatfs has already allocated the first cluster, written the firstCluster
 * fields back into the parent SFN entry, zero-filled the cluster, and created
 * "." / ".." entries. Regular files may still allocate their first cluster
 * lazily on first fwrite(); directories may not because callers create children
 * through currentDirectory immediately after chdir().
 */
```

Why it must exist: the old header exposed `mkdir` as if it behaved like a normal
directory primitive. The implementation did not satisfy that contract. Future
callers need the invariant stated at the public boundary.

Inputs: component name and callback. Output: callback handle with initialized
directory storage or NULL.

Affiliates: `afatfs_chdir()`, `filesystem_saveKitDirectory_tick()`,
`filesystem_saveSceneDirectory_tick()`, `filesystem_scanKits_tick()`,
`filesystem_loadKitDirectory_tick()`.

### 2. `asyncfatfs.c`: Add a created-directory handoff helper

Add a static helper near the create-file helpers, before
`afatfs_createFileContinue()`:

```c
static void afatfs_handoffCreatedDirectoryToInitializer(
    afatfsFile_t *file,
    afatfsFileCallback_t callback)
{
    /*
     * Newly-created directories cannot complete through the ordinary empty-file
     * success path.
     *
     * CREATE_FILE is still active when the parent directory entry has just been
     * written, so afatfs_extendSubdirectory() would reject the handle as busy
     * and its operation union would overwrite createFile state. The handoff is
     * deliberate: preserve the original callback, clear CREATE_FILE, then let
     * EXTEND_SUBDIRECTORY own the handle until it has allocated the first
     * cluster and initialized "." / "..".
     *
     * Inputs: file is the newly-created directory entry; callback is the
     * original afatfs_mkdir()/afatfs_mkdir_lfn() completion. Output: callback is
     * invoked by EXTEND_SUBDIRECTORY with file or NULL. Affiliates:
     * afatfs_createFileContinue(), afatfs_extendSubdirectory(),
     * afatfs_appendRegularFreeClusterContinue(), afatfs_saveDirectoryEntry().
     */
    file->operation.operation = AFATFS_FILE_OPERATION_NONE;
    (void)afatfs_extendSubdirectory(file, &afatfs.currentDirectory, callback);
}
```

Important detail: do not call the callback again from the helper after
`afatfs_extendSubdirectory()` returns. Once the operation is handed off,
`afatfs_extendSubdirectoryContinue()` owns success and failure callbacks. If the
operation completes synchronously, the callback has already happened before the
helper returns. If it returns `IN_PROGRESS`, polling will finish it later.

Why this helper is needed instead of calling `afatfs_extendSubdirectory()`
inline:

- `afatfs_fileIsBusy(file)` rejects a file whose operation is still
  `AFATFS_FILE_OPERATION_CREATE_FILE`.
- `file->operation.state` is a union. Switching to
  `AFATFS_FILE_OPERATION_EXTEND_SUBDIRECTORY` overwrites the active
  `createFile` state, including its callback. The callback must be copied into a
  local before the handoff.

No new public helper should be added. This is an asyncfatfs internal invariant.

### 3. `asyncfatfs.c`: Route newly-created directories through the handoff

Change both create branches in `afatfs_createFileContinue()`.

#### Short-name branch

Current branch:

```c
case AFATFS_CREATEFILE_PHASE_CREATE_NEW_FILE:
    status = afatfs_allocateDirectoryEntry(...);
    if (status == AFATFS_OPERATION_SUCCESS) {
        ...
        opState->phase = AFATFS_CREATEFILE_PHASE_SUCCESS;
        goto doMore;
    }
```

Required change after the SFN entry is written:

```c
afatfsFileCallback_t callback = opState->callback;

if (file->type == AFATFS_FILE_TYPE_DIRECTORY) {
    afatfs_handoffCreatedDirectoryToInitializer(file, callback);
    return;
}

opState->phase = AFATFS_CREATEFILE_PHASE_SUCCESS;
goto doMore;
```

Comment text to place beside the branch:

```c
/*
 * A just-created directory entry still has firstCluster == 0. Regular files can
 * remain in that state until fwrite(), but directories must be initialized
 * before the mkdir callback because callers immediately chdir() and create
 * children. callback is copied before the handoff because switching the file
 * operation to EXTEND_SUBDIRECTORY overwrites the createFile union storage.
 */
```

Why it must happen: this fixes newly-created short directories such as root
`Kit/` and root `Scene/`. If those roots are absent on a fresh card, the current
code creates a zero-cluster root child and all later saves inside it are built
on invalid directory state.

Inputs: `file->type`, `opState->callback`, freshly-written SFN directory entry.
Output: directory initialization is queued/completed before callback.

Affiliates: `afatfs_mkdir()`, `filesystem_saveKitDirectory_tick()` phase 1,
`filesystem_saveSceneDirectory_tick()` phase 1.

#### LFN branch

Current branch:

```c
case AFATFS_CREATEFILE_PHASE_CREATE_NEW_LFN_FILE:
    status = afatfs_createLongDirectoryEntries(file);
    if (status == AFATFS_OPERATION_SUCCESS) {
        opState->phase = AFATFS_CREATEFILE_PHASE_SUCCESS;
        goto doMore;
    }
```

Required change after `afatfs_createLongDirectoryEntries(file)` succeeds:

```c
afatfsFileCallback_t callback = opState->callback;

if (file->type == AFATFS_FILE_TYPE_DIRECTORY) {
    afatfs_handoffCreatedDirectoryToInitializer(file, callback);
    return;
}

opState->phase = AFATFS_CREATEFILE_PHASE_SUCCESS;
goto doMore;
```

Comment text to place beside the branch:

```c
/*
 * LFN directory creation writes the visible VFAT fragments and final SFN entry
 * first, then uses the same directory-initialization handoff as short mkdir().
 * The LFN entries are only names; the child directory is not usable until its
 * first cluster is allocated and recorded in the SFN entry.
 */
```

Why it must happen: this is the direct fix for `Save:Kit` creating
`Kit/<NNN Name>/` and then failing to create member files inside it.

Inputs: `opState->freeRunStart`, `opState->lfnEntryCount`, `opState->callback`,
`file->type`.

Outputs:

- LFN fragments remain adjacent to their owning SFN entry.
- The SFN entry receives a real first cluster during
  `afatfs_saveDirectoryEntry()`.
- The callback is delayed until the directory can be entered safely.

Affiliates: `afatfs_mkdir_lfn()`, `filesystem_saveKitDirectory_tick()` phase 6,
`filesystem_saveSceneDirectory_tick()` phases 6 and 16.

### 4. `asyncfatfs.c`: Comment the cluster initialization path in place

The existing functions already perform most of the required work. They need
comments tying them to the mkdir invariant.

#### `afatfs_appendRegularFreeClusterContinue()`

Add/expand comments in these statements:

```c
file->cursorCluster = opState->searchCluster;
file->physicalSize += afatfs_clusterSize();

if (opState->previousCluster == 0) {
    file->firstCluster = opState->searchCluster;
}
```

Comment text:

```c
/*
 * Assign the new cluster to the active cursor before the FAT and directory
 * entry are fully committed. Directory initialization relies on this exactly
 * like fwrite(): the following zero-fill phase needs a physical sector to
 * write. When previousCluster is zero this is also the first cluster of the
 * file/directory, so saveDirectoryEntry() must publish it in firstClusterHigh
 * and firstClusterLow before mkdir reports success.
 */
```

Add/expand comments at:

```c
entry->firstClusterHigh = file->firstCluster >> 16;
entry->firstClusterLow = file->firstCluster & 0xFFFF;
```

Comment text:

```c
/*
 * FAT32 stores the first cluster split across high/low 16-bit fields. This
 * assignment is what turns a visible directory entry into an enterable
 * directory after appendRegularFreeClusterContinue() chooses a cluster.
 */
```

Why it must happen: these are the exact assignments that make the directory
entry non-empty on disk.

#### `afatfs_extendSubdirectoryContinue()`

Add comments to the first-sector `"."` / `".."` block:

```c
if (directory->directoryEntryPos.sectorNumberPhysical != 0 &&
    directory->cursorOffset == 0) {
```

Comment text:

```c
/*
 * The first sector of a non-root subdirectory must start with "." and "..".
 * directory->firstCluster points "." back at this directory; parentDirectory
 * supplied by mkdir/create points ".." back at the directory that contained the
 * newly-created entry. Later directory extensions pass parentDirectory == NULL
 * and skip this block because cursorOffset is no longer zero.
 */
```

Add a loop comment for:

```c
while (1) {
    ...
    if (sectorInCluster < afatfs.sectorsPerCluster - 1) {
```

Comment text:

```c
/*
 * Zero every sector in the newly-appended cluster. FAT directory scans stop at
 * empty entries, so clearing the whole cluster prevents stale card data from
 * looking like child files. sectorInCluster is compared against
 * sectorsPerCluster - 1 because both values are zero-based within the new
 * cluster.
 */
```

Why it must happen: the save bug is a directory-initialization bug. These
comments preserve the invariant beside the code that actually initializes the
directory.

### 5. `asyncfatfs.c`: Defend `afatfs_allocateDirectoryEntry()`

Add a guard before this existing call:

```c
result = afatfs_extendSubdirectory(directory, NULL, NULL);
```

Required behavior:

- If `directory` is a non-root directory with `firstCluster == 0`, return
  `AFATFS_OPERATION_FAILURE` instead of trying to lazily create its first
  cluster with `parentDirectory == NULL`.
- Existing directories that already have a cluster may continue extending with
  `parentDirectory == NULL`.
- FAT16 root remains governed by `afatfs_extendSubdirectory()` rejecting root
  extension.

Comment text:

```c
/*
 * This allocator may extend an already-initialized current directory, but it
 * must not create the first cluster of a subdirectory. The first cluster needs
 * a real parentDirectory so ".." can be written correctly; only mkdir/create
 * still has that parent available before chdir() copies the child into
 * currentDirectory.
 */
```

Why it must happen: it prevents the same bug from reappearing as a lazy
first-child create inside a zero-cluster directory.

Inputs: `directory->type`, `directory->firstCluster`,
`directory->directoryEntryPos.sectorNumberPhysical`.

Output: invalid zero-cluster subdirectories fail instead of being extended with
an incorrect `".."` entry.

Affiliates: `afatfs_findNext()`, `afatfs_extendSubdirectory()`,
`filesystem_saveKitDirectory_tick()` phase 16.

### 6. `asyncfatfs.c`: Make LFN matching display-name-first

This is required to stop duplicate visible folders when aliases differ.

#### Extend `afatfsCreateFile_t`

Add one field:

```c
uint8_t scanLongNameChecksum;
```

Place it beside `scanLongNameValid`.

Comment text for the LFN state block:

```c
/*
 * scanLongNameChecksum stores the VFAT checksum from the accumulated LFN
 * fragments. Display-name matching may open an object whose SFN alias differs
 * from our generated candidate, so the following SFN entry must prove it owns
 * the accumulated LFN chain before strcmp(scanLongName, longName) is trusted.
 */
```

Why it must happen: without checksum validation, a stale/orphan LFN fragment
could be matched to the wrong following SFN entry.

#### Update `afatfs_lfnScanReset()`

Clear the checksum:

```c
opState->scanLongNameChecksum = 0u;
```

#### Update `afatfs_lfnScanAppend()`

When a new LFN chain starts (`raw[0] & 0x40u`), capture `raw[13]` as the
checksum. For subsequent fragments, reject the chain if `raw[13]` differs.

Comment text for the sequence math:

```c
/*
 * VFAT stores the last text fragment first and identifies it with bit 0x40.
 * The low five bits are a one-based ordinal, so (seq - 1) * 13 maps each
 * fragment back to its absolute character offset in the reconstructed display
 * component.
 */
```

Comment text for the checksum:

```c
/*
 * Every LFN fragment in one chain carries the checksum of the following SFN
 * entry. Keep only chains with a consistent checksum; the normal-entry branch
 * will compare it to afatfs_lfnChecksum(entry->filename) before treating the
 * display name as an existing object.
 */
```

#### Replace alias-only match logic in `afatfs_createFileContinue()`

When a normal non-LFN entry arrives and `opState->longNameEnabled` is true,
evaluate the reconstructed LFN before the alias-collision branch.

Required behavior:

1. If `scanLongNameValid` is true,
   `scanLongNameChecksum == afatfs_lfnChecksum(entry->filename)`, and
   `strcmp(scanLongName, longName) == 0`, then the requested display object
   already exists.
2. If the existing entry type is incompatible with the requested type
   (`directory` vs `archive`), fail rather than creating a duplicate.
3. If type is compatible:
   - call `afatfs_fileLoadDirectoryEntry(file, entry)`;
   - copy the actual SFN alias into `openNameOut` with
     `afatfs_copyShortAliasText(entry->filename, opState->openNameOut)`;
   - finish through `AFATFS_CREATEFILE_PHASE_SUCCESS`.
4. Only if the display name did not match should the generated alias collision
   path run.

Comment text for the normal-entry branch:

```c
/*
 * For LFN requests, identity is the completed display component, not the first
 * generated SFN alias candidate. Host tools or earlier firmware may have
 * assigned a different alias to the same visible name. If the preceding LFN
 * chain is checksum-valid for this SFN entry and the display text matches,
 * open that entry and return its actual alias to the caller.
 */
```

Comment text for the type check:

```c
/*
 * Do not resolve a same-display-name file as a directory or a directory as a
 * file. FAT permits both attributes in raw entries, but this component API is
 * typed by its caller: mkdir_lfn() requests directories and fopen_lfn() requests
 * archive files. A type mismatch is a real collision, not an invitation to make
 * another visible duplicate.
 */
```

Why it must happen: Kit and Scene save paths can otherwise create duplicate
visible folders even after directory initialization is fixed.

Affiliates:

- `afatfs_generateShortAlias()`
- `afatfs_copyShortAliasText()`
- `afatfs_existingEntryMatchesRequest()`; this helper can be deleted or folded
  into the normal-entry branch once display-first matching exists. Avoid keeping
  a one-line wrapper that hides the new checksum/type rules.

### 7. `filesystem.c`: Add save-target mode scratch

Add a private scratch flag near the existing Kit Save fields:

```c
static uint8_t op_save_opened_existing_dir = 0u;
```

Comment text:

```c
/*
 * Tracks whether the current root Kit/Scene save entered an existing numbered
 * directory by scan-cache alias or created a new directory by LFN display name.
 * Existing-directory saves overwrite authoritative child files but do not
 * rename the folder; cache updates must therefore preserve the scanned display
 * name instead of pretending preset_currentName changed on-card metadata.
 */
```

Why it must happen: once occupied slots are opened by alias, the final cache
update must know whether `op_save_kit_display_name` / `op_scene_dir_display_name`
was actually created or was only the desired save label.

Inputs: phase 6 branch outcome in Kit Save and Scene Save.

Outputs: final cache update policy.

### 8. `filesystem.c`: Open occupied Kit slots by cached alias

Change `filesystem_saveKitDirectory_tick()` phase 6.

Current behavior:

```c
if (!afatfs_mkdir_lfn(op_save_kit_display_name,
                      op_save_kit_dir_name,
                      on_file_opened))
    return;
```

Required behavior:

```c
op_save_opened_existing_dir = 0u;
if (kit_slot_present[op_slot] && kit_slot_open_name[op_slot][0] != '\0') {
    op_save_opened_existing_dir = 1u;
    storage_copyFilename(op_save_kit_dir_name, kit_slot_open_name[op_slot]);
    if (!afatfs_fopen(op_save_kit_dir_name, "r", on_file_opened))
        return;
} else {
    if (!afatfs_mkdir_lfn(op_save_kit_display_name,
                          op_save_kit_dir_name,
                          on_file_opened))
        return;
}
```

Comment text beside the branch:

```c
/*
 * Occupied Kit slots are entered by the short alias discovered during the last
 * Kit scan. The visible folder name is on-card metadata and cannot be renamed
 * by this save path; opening the known alias prevents mkdir_lfn() from
 * re-solving VFAT collision rules and accidentally creating a duplicate visible
 * folder. Empty slots still use mkdir_lfn() so firmware-created folders get the
 * preferred "NNN Name" display component.
 */
```

Why it must happen: replacement should overwrite files inside the existing slot
instead of asking the LFN create path to discover whether a display-name folder
already exists.

Inputs:

- `op_slot`
- `kit_slot_present[op_slot]`
- `kit_slot_open_name[op_slot]`
- `op_save_kit_display_name`

Outputs:

- `op_kit_slot_dir` still receives a directory handle through
  `on_file_opened`.
- `op_save_kit_dir_name` is the actual alias used by later cache/kitset logic.

Affiliates:

- `filesystem_recordKitDirectory()`
- `filesystem_loadKitDirectory_tick()` phase 6, which already opens by
  `kit_slot_open_name[op_slot]`
- `filesystem_kitSlotExists()` and overwrite UI logic in `menu.c`

### 9. `filesystem.c`: Preserve cache truth after occupied Kit save

Change `filesystem_saveKitDirectory_tick()` phase 24.

Current behavior always parses `op_save_kit_display_name` and writes that name
plus `op_save_kit_dir_name` into `kit_slot_name/open_name`.

Required behavior:

- If `op_save_opened_existing_dir == 0`, keep the current new-directory cache
  update.
- If `op_save_opened_existing_dir != 0`, preserve the existing scanned display
  name and alias. Optionally re-copy `op_save_kit_dir_name` into
  `kit_slot_open_name[op_slot]` only if it differs, but do not replace
  `kit_slot_name[op_slot]` with `preset_currentName`.
- Ensure `kit_slot_present[op_slot] = 1u` and `kb_map` still contains the slot.

Comment text:

```c
/*
 * Cache update mirrors what happened on disk. A newly-created slot receives the
 * display component returned by filesystem_makeKitDirectoryDisplayName() and
 * the alias returned by mkdir_lfn(). An occupied slot was opened by an existing
 * alias and was not renamed, so its cached display name remains the scanned
 * folder name while the child kitset/instrument files become the authoritative
 * overwritten data.
 */
```

Why it must happen: otherwise the UI will display a folder name that does not
exist on the card after an occupied-slot overwrite.

### 10. `filesystem.c`: Apply the same occupied-slot policy to root Scene save

Change `filesystem_saveSceneDirectory_tick()` phase 6 the same way, using:

- `scene_slot_present[op_slot]`
- `scene_slot_open_name[op_slot]`
- `op_scene_dir_name`
- `op_scene_dir_display_name`

Comment text:

```c
/*
 * Root Scene overwrite follows the same rule as Kit overwrite: enter the
 * scanned directory by alias when the slot already exists, and create a new LFN
 * directory only for empty slots. This prevents duplicate visible Scene folders
 * while asyncfatfs still lacks recursive replace/rename.
 */
```

Change phase 58 cache update so existing Scene saves do not claim a folder
rename that did not occur.

Why it must happen: Scene Save uses the same root numbered folder convention and
the same overwrite UI semantics as Kit Save.

### 11. `filesystem.c`: Document embedded Scene Kit limitation

`filesystem_saveSceneDirectory_tick()` phase 16 creates/opens an embedded
`Kit <SceneName>` directory inside the Scene folder. There is no scan cache for
that child directory.

After LFN display-name-first matching is fixed, `afatfs_mkdir_lfn()` can open an
existing embedded `Kit <SceneName>` even if the alias differs. If the Scene name
changes during an occupied Scene overwrite, the save path may still create a new
embedded Kit directory and leave the old one behind because asyncfatfs has no
recursive replace or rename primitive.

Add a comment beside phase 16:

```c
/*
 * Embedded Scene Kit directories are still display-name opened because there is
 * no child scan cache in the save path. With display-name-first LFN matching
 * this reuses an existing "Kit <SceneName>" child. If the root Scene is
 * overwritten with a different display name, the old embedded Kit directory may
 * remain until asyncfatfs gains recursive replace/rename; Scene Load discovers
 * the first valid Kit* child, so full Scene overwrite cleanup is a later
 * storage primitive, not part of the normal Kit Save blocker.
 */
```

Why it must happen: this records the remaining limitation honestly next to the
code that can still leave stale child directories.

### 12. `filesystem.c`: Do not change text writer logic for this bug

No required change in:

- `filesystem_writeTextLine()`
- `filesystem_nextKitsetLine()`
- `filesystem_nextInstrumentLine()`
- `storage_formatKitsetLine()`
- `storage_formatInstrumentLine()`

Reason: the empty folder occurs before the first member file write. The current
ordering writes six instruments first, collects their returned aliases in
`op_save_instrument_file[][]`, and writes `kitset.kcg` afterward. That ordering
is correct because `kitset.kcg` references aliases that are not known until the
LFN file opens complete.

Add a clarifying comment near phase 16 if desired:

```c
/*
 * Member instruments are written before kitset.kcg because each fopen_lfn()
 * returns the physical alias that kitset.kcg must reference. If a later member
 * open/write fails, no new kitset is committed for that partial save.
 */
```

## Implementation Order

1. Patch asyncfatfs directory initialization for both short and LFN mkdir.
2. Build and run a fresh-card save test where `Kit/` does not already exist.
3. Build and run an existing-`Kit/` empty-slot save test.
4. Patch LFN display-name-first matching with checksum validation.
5. Patch Kit Save occupied-slot alias opening and cache-truth update.
6. Patch Scene Save root occupied-slot alias opening and cache-truth update.
7. Add the embedded Scene Kit limitation comment.
8. Retest the matrix below.

Do not start Morph Save until normal Kit Save passes. Morph Save should reuse
the same directory writer after the filesystem primitive is proven.

## Retest Matrix

1. Fresh card with no `Kit/` directory:
   - Save Kit slot 001.
   - Expected: `Kit/001 Name/` exists with six instrument files and
     `kitset.kcg`.
2. Existing `Kit/`, empty target slot:
   - Save to an empty slot.
   - Expected: target folder is non-empty and loadable immediately.
3. Occupied Kit slot:
   - Save over the slot.
   - Expected: no duplicate visible folder; authoritative files are truncated
     and rewritten; UI cache does not claim a folder rename.
4. Existing same visible name with different alias:
   - Create or preserve a folder whose LFN display matches but alias differs.
   - Expected: LFN open/create finds the existing display object and returns its
     actual alias.
5. Fresh card with no `Scene/` directory:
   - Save Scene.
   - Expected: root Scene directory and embedded Kit directory are initialized,
     entered, and populated.
6. Occupied Scene slot:
   - Save over the slot.
   - Expected: no duplicate root Scene folder; cache does not claim a folder
     rename.
7. Failure injection:
   - Force no-space or open failure after directory creation.
   - Expected: callback reports error, no dead CREATE_FILE operation remains in
     the open-file table, and later retries can allocate a handle.

## Build/Static Checks

After implementation:

```sh
make
make img
git diff --check
```

Pay particular attention to `-Werror`-style fallout if the local Makefile
enables it:

- The created-directory handoff must not leave an unused `opState` access after
  the operation union is overwritten.
- Any helper introduced for matching should do real work. Avoid one-line
  wrappers around simple comparisons.
- If `afatfs_extendSubdirectoryContinue()` keeps `clusterNumber` only to call
  `afatfs_fileGetCursorClusterAndSector()`, either use it in a diagnostic
  comment/assertion or explicitly cast it unused if the compiler complains.

## Morph Save Dependency

Morph Save must wait for this repair. Once normal Kit Save is reliable, Morph
Save should change only the instrument serialization policy:

- normal file `[params]` receive current interpolated parameter values;
- file `[morph]` receives the normal endpoint values;
- non-morphable parameters remain whatever the normal endpoint stores.

It should not add another directory writer or another LFN create path.

## Summary

The empty-folder failure is caused by directory creation returning too early.
The correct first fix is in asyncfatfs: newly-created directories must be
cluster-allocated and `"."` / `".."` initialized before `mkdir` callbacks fire.

The duplicate-folder risk is a separate LFN identity bug: existing long names
must be matched by checksum-validated display text, not only by the first
generated short alias. Kit and Scene save should also open occupied numbered
slots by the alias already found during the scan, then keep the scan cache
truthful about whether a folder was actually renamed.

## Follow-Up Plan: Case-Preserving System Files And Aliases

### Scope

Hardware testing of `Save:[Kit     ]` produced a valid, non-empty
`SD_CARD/Kit/037 Slkty/` folder with lowercase-looking instrument LFNs, but the
system file appears as `KITSET.KCG` in the host view. Future system files named
in firmware/spec text should appear with the exact lowercase spellings already
used in source constants, such as:

- `kitset.kcg`
- `sceneset.scg`
- `pattern.pat`
- `effects.fx`
- `glo.cfg`

The broader asyncfatfs requirement is case preservation for created/modified
files. On FAT this means case-preserving, case-insensitive behavior. FAT should
not treat `kitset.kcg` and `KITSET.KCG` as separate files, but it should store
enough metadata that host tools and firmware scanners display `kitset.kcg` when
that is the name the firmware created.

No code change was made for this follow-up in the current pass. This section is
the implementation plan.

Implementation work log:

- Starting case-preservation implementation. The first layer will move FAT
  short-name lowercase-bit handling into `fat_standard.c/h`; the second will
  make asyncfatfs store and return those bits on create/open; the third will
  replace filesystem.c's local display helper with the shared FAT helper.
- Added `FAT_NTRES_LOWERCASE_BASE` / `FAT_NTRES_LOWERCASE_EXT` and shared
  `fat_calculateFilenameCaseFlags()` / `fat_applyFilenameCaseFlags()` helpers.
  The calculation intentionally sets lowercase bits only for all-lowercase
  representable 8.3 base/extension segments; exact mixed case remains an LFN
  responsibility.
- Threaded `shortNameCaseFlags` through asyncfatfs create state. Ordinary
  `afatfs_fopen()` now captures lowercase display bits before the raw SFN is
  uppercased, LFN-generated aliases do the same, newly-created SFN entries write
  `ntReserved`, and returned aliases are adjusted through
  `fat_applyFilenameCaseFlags()` before higher-level code stores them in files
  such as `kitset.kcg`.
- Existing ordinary short-name files opened for write now refresh their
  `ntReserved` case flags before truncation/rewrite. This covers overwriting an
  older uppercase `KITSET.KCG` with the lowercase source literal
  `kitset.kcg`; read-only opens intentionally leave metadata untouched.
- Replaced filesystem.c's local `filesystem_applyFatShortNameCase()` helper with
  the shared FAT helper at Kit, Scene, Instrument, and Sample scan call sites.
  Added a storageTypes.h comment that system file constants are written in their
  intended lowercase display case and should not be uppercased to match raw SFN
  storage.
- Verification: `make` completed and linked `build/lxr02.elf`; `make img`
  rebuilt `build/LXRV2_lxr02.img`; `git diff --check` completed cleanly. The
  warning profile remained the known asyncfatfs `eraseCount` unused-parameter
  warning and nano-libc syscall stub linker warnings.

### Current Code Facts

- `Core/Hardware/SD/storageTypes.h` already defines the desired lowercase
  system literals:
  - `STORAGE_KITSET_FILENAME "kitset.kcg"`
  - `STORAGE_SCENESET_FILENAME "sceneset.scg"`
- `filesystem_saveKitDirectory_tick()` writes `kitset.kcg` through ordinary
  `afatfs_fopen(STORAGE_KITSET_FILENAME, "w", ...)`.
- `filesystem_saveSceneDirectory_tick()` writes `sceneset.scg`, embedded
  `kitset.kcg`, `pattern.pat`, and `effects.fx` through ordinary
  `afatfs_fopen(..., "w", ...)`.
- globals save/load writes `glo.cfg` through ordinary `afatfs_fopen()`.
- `fat_convertFilenameToFATStyle()` uppercases every byte of the raw 8.3 base
  and extension.
- `fatDirectoryEntry_t` already has the FAT case-preservation field:
  `uint8_t ntReserved`.
- Read-side code already knows how to display lowercase short names:
  `filesystem_applyFatShortNameCase()` applies bit `0x08` to lowercase the base
  and bit `0x10` to lowercase the extension after
  `fat_convertFATStyleToFilename()`.
- Create-side code never sets `entry->ntReserved`, so new short-name files such
  as `kitset.kcg` are stored as uppercase raw 8.3 entries with no lowercase
  display bits.
- LFN-created instrument files appear lowercase because their visible names are
  stored in VFAT LFN entries. However, `openNameOut` and `kitset.kcg` still get
  the generated short alias text from `afatfs_copyShortAliasText()`, which
  currently returns uppercase because it has no `ntReserved` input.

### FAT Case Rule To Implement

FAT short entries can preserve only these 8.3 case shapes without an LFN:

- all-uppercase base and/or extension;
- all-lowercase base and/or extension;
- digits and punctuation unaffected.

Mixed-case 8.3 names cannot be represented exactly with only `ntReserved`. If a
future caller needs exact mixed-case display for an 8.3-length name, that caller
must use `afatfs_fopen_lfn()` / `afatfs_mkdir_lfn()` so a VFAT LFN entry owns
the visible spelling.

For the current system files, the FAT short-name case bits are sufficient
because the requested names are all lowercase base plus lowercase extension.

### 1. `fat_standard.h`: Name The FAT Case Bits

Add constants next to the directory entry struct or FAT filename declarations:

```c
#define FAT_NTRES_LOWERCASE_BASE 0x08u
#define FAT_NTRES_LOWERCASE_EXT  0x10u
```

Comment text:

```c
/*
 * FAT short-name case preservation bits.
 *
 * Raw 8.3 names are stored uppercase in directoryEntry.filename. These bits in
 * directoryEntry.ntReserved tell FAT-aware readers to display the base and/or
 * extension as lowercase. They do not make FAT lookups case-sensitive and they
 * cannot represent mixed-case text; exact mixed-case display must use VFAT LFN
 * entries.
 */
```

Why it must happen: current code uses magic values `0x08` and `0x10` on the
read side. Naming them in `fat_standard.h` makes the write-side asyncfatfs
change readable and keeps scanner/display code aligned with create code.

Inputs: none.

Outputs: shared symbolic constants used by asyncfatfs create and filesystem
scan/display code.

Affiliates:

- `filesystem_applyFatShortNameCase()`
- new short-name case helper in `fat_standard.c`
- `afatfs_createFileContinue()`
- `afatfs_createLongDirectoryEntries()`

### 2. `fat_standard.c/h`: Add Short-Name Case Helpers

Add two helpers:

```c
uint8_t fat_calculateFilenameCaseFlags(const char *filename);
void fat_applyFilenameCaseFlags(char *filename, uint8_t ntReserved);
```

`fat_calculateFilenameCaseFlags()` computes display-case metadata from the
caller-supplied `prefix.ext` component before `fat_convertFilenameToFATStyle()`
uppercases it.

Required behavior:

- Split the input at the final/only dot the same way the current converter uses
  the dot.
- Inspect only alphabetic characters that fit inside the 8-character base and
  3-character extension windows.
- Set `FAT_NTRES_LOWERCASE_BASE` only when the base contains alphabetic
  characters and every alphabetic base character is lowercase.
- Set `FAT_NTRES_LOWERCASE_EXT` only when the extension contains alphabetic
  characters and every alphabetic extension character is lowercase.
- If a segment contains any uppercase alphabetic character, leave that segment's
  lowercase bit clear.
- If a segment contains mixed upper/lowercase, leave the bit clear because FAT
  short-name case bits cannot represent mixed case.
- Digits and valid punctuation do not affect the decision.

`fat_applyFilenameCaseFlags()` should perform the current
`filesystem_applyFatShortNameCase()` behavior in the FAT helper layer.

Comment text for `fat_calculateFilenameCaseFlags()`:

```c
/*
 * Derive the ntReserved lowercase-display flags for a caller-supplied 8.3
 * component before fat_convertFilenameToFATStyle() uppercases the raw SFN.
 *
 * FAT has only one lowercase bit for the base and one for the extension, so
 * this helper preserves all-lowercase system names such as kitset.kcg while
 * deliberately refusing to claim mixed-case names are preserved. Mixed-case
 * display requires VFAT LFN entries.
 */
```

Important loop comment:

```c
/*
 * Only the characters that can fit into the raw 8.3 base/extension participate
 * in the case decision. The converter truncates beyond those windows, so
 * looking farther ahead would let discarded characters change the display flags
 * for the stored name.
 */
```

Comment text for `fat_applyFilenameCaseFlags()`:

```c
/*
 * Apply FAT short-name lowercase-display bits after converting raw 8.3 text to
 * printable prefix.ext form. This is display metadata only: lookups remain
 * case-insensitive and the raw directory entry remains uppercase.
 */
```

Why it must happen: case logic belongs beside the FAT conversion routines, not
duplicated in `filesystem.c` and `asyncfatfs.c`.

Inputs:

- original caller filename for `fat_calculateFilenameCaseFlags()`;
- printable converted name plus `ntReserved` for `fat_applyFilenameCaseFlags()`.

Outputs:

- `ntReserved` lowercase flag byte;
- display-case-adjusted printable filename.

Affiliates:

- `fat_convertFilenameToFATStyle()`
- `fat_convertFATStyleToFilename()`
- `filesystem_applyFatShortNameCase()` replacement
- `afatfs_copyShortAliasText()` replacement/extension

### 3. `filesystem.c`: Remove The Local Case Helper

Replace `filesystem_applyFatShortNameCase()` with
`fat_applyFilenameCaseFlags()` at every scan/display call site:

- Kit child scan around current `entry->ntReserved` use
- root Kit scan
- root Scene scan
- Instrument scan
- Sample scan

Then delete the local static helper.

Comment text to place near one representative scanner branch:

```c
/*
 * Preserve FAT short-name display case before recording the open alias. The raw
 * SFN remains case-insensitive uppercase, but ntReserved marks all-lowercase
 * base/extension names created by firmware or host tools.
 */
```

Why it must happen: once write-side asyncfatfs starts setting `ntReserved`,
every scanner should use the shared FAT helper so display behavior is identical
for Kit, Scene, Instrument, Sample, and future file pools.

Inputs: raw FAT filename plus `entry->ntReserved`.

Outputs: printable name with lowercase base/extension when FAT says so.

Affiliates:

- `filesystem_recordKitDirectory()`
- `filesystem_recordSceneDirectory()`
- `filesystem_recordInstrumentFile()`
- sample browser sorting/display

### 4. `asyncfatfs.c`: Store Case Flags In Create State

Extend `afatfsCreateFile_t`:

```c
uint8_t shortNameCaseFlags;
```

Place it near `filename[FAT_FILENAME_LENGTH]`, because it describes the same
raw SFN.

Comment text:

```c
/*
 * Case-preservation flags for filename[].
 *
 * filename[] is always the uppercase raw 8.3 key used for FAT lookup. The
 * shortNameCaseFlags byte is copied into directoryEntry.ntReserved when a new
 * SFN entry is created, and is also used when returning printable aliases to
 * higher layers. It preserves all-lowercase names such as kitset.kcg without
 * changing FAT's case-insensitive lookup behavior.
 */
```

Why it must happen: create state needs to carry the original requested display
case past the point where the raw filename has been uppercased.

Inputs: original `name` or generated alias text.

Outputs: `entry->ntReserved` for newly-created short and LFN final SFN entries;
case-adjusted alias text for `openNameOut`.

### 5. `asyncfatfs.c`: Compute Flags For Short-Name Opens/Creates

In `afatfs_createFileInternal()`:

Current short path:

```c
fat_convertFilenameToFATStyle(name, opState->filename);
```

Required short path:

```c
opState->shortNameCaseFlags = fat_calculateFilenameCaseFlags(name);
fat_convertFilenameToFATStyle(name, opState->filename);
```

Comment text:

```c
/*
 * Preserve display case for ordinary 8.3 callers before converting the raw FAT
 * key to uppercase. This is what lets system files created through afatfs_fopen
 * appear as kitset.kcg, sceneset.scg, pattern.pat, effects.fx, and glo.cfg
 * while keeping existing case-insensitive open behavior.
 */
```

Why it must happen: all system file writes currently use ordinary
`afatfs_fopen()`, not LFN APIs.

Inputs: caller literal `name`.

Outputs: `opState->shortNameCaseFlags`.

Affiliates:

- `filesystem_saveKitDirectory_tick()`
- `filesystem_saveSceneDirectory_tick()`
- globals save/load
- legacy flat save/load paths

### 6. `asyncfatfs.c`: Compute Flags For LFN-Generated Aliases

In `afatfs_generateShortAlias()` after the printable `alias` string is built
and before/after `fat_convertFilenameToFATStyle(alias, opState->filename)`:

```c
opState->shortNameCaseFlags = fat_calculateFilenameCaseFlags(alias);
fat_convertFilenameToFATStyle(alias, opState->filename);
```

But there is an important policy choice:

- If aliases are generated from a lowercase LFN display component such as
  `slakd1.drm`, the returned open alias can be lowercase (`slakd1.drm`) while
  the raw SFN remains uppercase plus ntReserved lowercase bits.
- If an alias receives a `~N` suffix, preserve lowercase for the base only when
  the alias text is all lowercase plus digits/punctuation. If the generator
  intentionally emits uppercase `~N`, the lowercase flag should stay clear for
  that segment.

Comment text:

```c
/*
 * The generated SFN alias is still the physical lookup key for an LFN object.
 * Store lowercase-display flags for that alias too, so kitset.kcg can receive
 * the same case-preserved open name that host tools display for the final SFN
 * entry. The LFN entry remains the authoritative mixed-case display name.
 */
```

Why it must happen: Kit Save stores `op_save_instrument_file[][]` in
`kitset.kcg` from `openNameOut`. Returning lowercase aliases makes the kitset
file match the visible instrument file casing instead of forcing uppercase
`SLAKD1.DRM` references.

Inputs: generated printable alias.

Outputs: `opState->shortNameCaseFlags` and `opState->filename`.

Affiliates:

- `afatfs_copyShortAliasText()`
- `storage_formatKitsetLine()`
- `filesystem_nextKitsetLine()`

### 7. `asyncfatfs.c`: Write `ntReserved` On New SFN Entries

Set `entry->ntReserved` / `entries[sfnIndex].ntReserved` in both create paths.

Short-name create branch after:

```c
entry->attrib = file->attrib;
```

Add:

```c
entry->ntReserved = opState->shortNameCaseFlags;
```

LFN create branch after:

```c
entries[sfnIndex].attrib = file->attrib;
```

Add:

```c
entries[sfnIndex].ntReserved = opState->shortNameCaseFlags;
```

Comment text for the short create branch:

```c
/*
 * Preserve the caller's 8.3 display case in ntReserved while keeping
 * filename[] as the uppercase FAT lookup key. This is what makes newly-created
 * lowercase system files show as kitset.kcg instead of KITSET.KCG.
 */
```

Comment text for the LFN final SFN branch:

```c
/*
 * The LFN fragments preserve the full display name. The final SFN entry still
 * needs coherent ntReserved case bits because higher layers cache and write the
 * returned alias into files such as kitset.kcg.
 */
```

Why it must happen: this is the on-card metadata that host filesystems use to
display lowercase short names.

Inputs: `opState->shortNameCaseFlags`.

Outputs: FAT directory entry `ntReserved`.

### 8. `asyncfatfs.c`: Return Case-Preserved Aliases

Replace or extend `afatfs_copyShortAliasText()` so it accepts the case flags:

```c
static void afatfs_copyShortAliasText(
    const uint8_t fatFilename[FAT_FILENAME_LENGTH],
    uint8_t ntReserved,
    char out[AFATFS_SHORT_FILENAME_MAX])
{
    fat_convertFATStyleToFilename((const char *)fatFilename, out);
    fat_applyFilenameCaseFlags(out, ntReserved);
}
```

Update call sites:

- In `afatfs_generateShortAlias()`, pass `opState->shortNameCaseFlags`.
- In the LFN display-name match branch, pass `entry->ntReserved`.

Comment text:

```c
/*
 * Return the printable alias exactly as FAT readers should display it. The raw
 * SFN is uppercase, but ntReserved may mark the base and/or extension as
 * lowercase. Callers store this string in kitset.kcg and later pass it back to
 * afatfs_fopen(), whose lookup remains case-insensitive.
 */
```

Why it must happen: otherwise `kitset.kcg` keeps receiving uppercase aliases
even after the on-card entry has lowercase display bits.

Inputs: raw FAT SFN and `ntReserved`.

Outputs: NUL-terminated printable alias with preserved lowercase display.

Affiliates:

- `afatfs_generateShortAlias()`
- LFN existing-entry open branch
- `filesystem_saveKitDirectory_tick()`
- `storage_formatKitsetLine()`

### 9. `filesystem.c`: Keep System File Constants Lowercase

Do not rename `STORAGE_KITSET_FILENAME`, `STORAGE_SCENESET_FILENAME`, or the
literal save names in `filesystem.c`. They are already lowercase and should stay
the single source of intended system-file spelling.

Add one comment near the storage constants in `storageTypes.h`:

```c
/*
 * System file literals are written in their intended display case. asyncfatfs
 * preserves all-lowercase 8.3 names through FAT ntReserved case bits, so callers
 * should not uppercase these constants to match raw SFN storage.
 */
```

Why it must happen: this prevents future patches from "fixing" host-visible
uppercase names by uppercasing the source constants.

Inputs: existing lowercase constants/literals.

Outputs: stable intended display spelling.

### 10. Compatibility And Retest Matrix

Retest after implementation:

1. Save Kit to a new empty slot.
   - Expected host view: `kitset.kcg`, not `KITSET.KCG`.
   - Expected kitset file references: lowercase aliases such as `slakd1.drm`
     when the created aliases are all-lowercase representable.
   - Expected load: firmware can still open the files using those lowercase
     aliases because raw FAT lookup is case-insensitive.
2. Save Scene to a new empty slot.
   - Expected host view: `sceneset.scg`, embedded `kitset.kcg`, `pattern.pat`,
     `effects.fx`.
3. Save globals.
   - Expected host view: `glo.cfg`.
4. Load existing uppercase files.
   - Expected: existing cards with `KITSET.KCG`, `SCENESET.SCG`, or `GLO.CFG`
     still load because lookup remains raw uppercase/case-insensitive.
5. Re-save over an existing uppercase system file.
   - If the file already exists, `afatfs_fopen("kitset.kcg", "w")` opens and
     truncates it. Because current truncate/close updates do not rewrite
     `ntReserved`, its host-visible case may remain uppercase until a future
     rename/recreate primitive exists.
   - If exact recasing of existing files is required, add an asyncfatfs metadata
     update primitive that can update `ntReserved` on an existing directory
     entry without changing the raw filename or cluster chain.

### Important Non-Goal

Do not make FAT lookups case-sensitive. FAT short names and VFAT LFNs should be
treated as case-insensitive identities with case-preserved display metadata.
Creating distinct sibling files whose names differ only by case would be
incompatible with normal FAT behavior and would break the existing scan/open
model.
