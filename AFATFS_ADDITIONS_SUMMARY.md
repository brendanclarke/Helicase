# AsyncFATFS Additions Summary

During this session, several new asynchronous filesystem operations and diagnostic tools were added to `asyncfatfs.c` and `asyncfatfs.h`. These additions aim to support complex, multi-step filesystem tasks (such as full Kit saves/overwrites) without blocking the main event loop or audio processing.

## 1. Recursive Tree Deletion (`afatfs_deleteTree`)
The primary feature implemented was an asynchronous state machine for recursively deleting entire directory trees. Because deleting a non-empty directory requires crawling its subdirectories and individually freeing clusters, this process is broken down into non-blocking phases:
- **`SCAN` & `OPEN_DIR`**: Iterates through the directory's contents.
- **`EMPTY_DIR_ASCEND`**: Once a subdirectory is emptied, ascends to the parent directory to delete the subdirectory's entry.
- **`SCAN_PARENT_FOR_SELF_LOOP`**: Locates the active subdirectory's entry within its parent's sector.
- **`RETIRE_ENTRIES`**: Marks the SFN/LFN directory entries as deleted.
- **`FREE_FILE_CLUSTERS`**: Iterates through the FAT table to mark the file/directory's clusters as free.

## 2. Object Movement and Copying (Stubs)
Foundational structures and polling dispatcher cases were added for future advanced file operations:
- **`afatfs_moveObject`**: Initiates a state machine for asynchronously moving files or directories across the filesystem.
- **`afatfs_copyTree` / `afatfs_replaceTree`**: Stub operations registered in the dispatcher to support recursive copying and atomic replacement of directory trees.

## 3. Dispatcher Integration & File Allocation Fixes
- Added all new tree operations (`DELETE_TREE`, `MOVE_OBJECT`, `COPY_TREE`, `REPLACE_TREE`) to the primary polling function `afatfs_fileOperationContinue`.
- **Bug Fix**: Fixed `afatfs_allocateFileHandle()` integration. Background file operations were initially skipping execution because the newly allocated file handles defaulted to `AFATFS_FILE_TYPE_NONE`. They are now correctly initialized to `AFATFS_FILE_TYPE_NORMAL`.

## 4. Sub-phase Timeout Diagnostics (`afatfs_getDeleteTreePhase`)
To diagnose timeouts without blocking the main loop, a diagnostic function was added.
- **`afatfs_getDeleteTreePhase()`**: Iterates through open files to find the active `DELETE_TREE` operation and returns its specific internal state machine phase. 
- Integrated this into `filesystem.c`'s `50,000` tick timeout. If the deletion takes too long, the UI will display a `TDelXX` error, where `XX` is the exact internal subphase where the asynchronous FAT driver stalled.

## 5. Kit Overwrite `TOut06` Repair

Hardware testing exposed `ERR TOut06` when saving into an occupied Kit slot.
The code trace showed that the new native delete state stored only a raw
`afatfsFinder_t`, then cast that storage to the much larger
`afatfsObjectFinder_t`. LFN scanner initialization consequently wrote beyond
the finder and erased the adjacent completion callback. The tree could be
physically deleted and its operation handle released, while `filesystem.c`
waited forever for a callback that no longer existed; phase `06` is the
high-level `FS_DELETE_SLOT_DELETE_MATCH` wait.

Repair work covers the complete native-operation lifecycle rather than only
changing the finder member:

- give the delete state a correctly sized `afatfsObjectFinder_t` with no casts;
- initialize every allocated asyncfatfs handle through the normal handle
  initializer;
- release retained directory cache sectors before changing the handle to a
  different directory cluster;
- distinguish a real scan failure from normal end-of-directory;
- release all cache ownership and reset the operation handle before issuing
  exactly one success/error callback.

### Implemented repair

`afatfsDeleteTree_t` now stores the complete `afatfsObjectFinder_t`, and every
object-iterator call uses that member directly without a cast. The native start
function validates that its root identity is a real directory and initializes
the recycled open-file slot through `afatfs_initFileHandle()` before publishing
the operation to the poll dispatcher.

Directory transitions now release any retained scan sector before rebinding the
private handle to a child or recovered parent cluster. Normal end-of-directory
and scan failure are handled separately, so an I/O failure cannot be mistaken
for proof that a directory is empty. A single `afatfs_deleteTreeFinish()` helper
owns every terminal path: it copies the callback, releases cache ownership,
resets the complete handle, and only then invokes exactly one result callback.
This keeps the low-level handle lifetime and `filesystem.c`'s
`op_delete_tree_done` handshake synchronized.

Detailed contract comments were added beside the finder state, handle setup,
directory transitions, root retirement, terminal cleanup, and the public
header declarations. The public object-finder documentation now explicitly
forbids casting a smaller raw finder to the LFN-aware type.

### Verification

- `make -j4` completes and links `build/lxr02.elf` successfully.
- `make img` produces `build/LXRV2_lxr02.img` successfully (356,068 bytes
  reported by the image packager).
- `git diff --check` reports no whitespace errors.
- The build retains existing unrelated warnings for the unused asyncfatfs
  `eraseCount` parameter, two signedness comparisons, and newlib's unimplemented
  host-style syscall stubs; the delete-tree repair adds no new compiler warning.

Final confirmation still requires the hardware overwrite test: save into an
occupied root Kit slot, verify the UI completes without `TOut06`, remount the
card, and confirm that exactly one correctly named `NNN Name/` folder contains
`kitset.kcg` plus all six current Instrument files. A nested Scene overwrite is
also recommended because it exercises child-directory descend/ascend rather
than only the flat Kit tree.

## 6. Phases 5-6 Reduced-RAM Implementation

Work began on 2026-07-18 to add native recursive tree copy and recoverable
directory replacement without growing the 7,804-byte `afatfs` object toward the
earlier approximately 12 KB estimate.

The implementation design intentionally accepts slower save-time traversal:

- copy data moves through one 96-byte buffer rather than a full 512-byte
  sector buffer;
- only the active directory owns a complete 68-byte LFN finder;
- each of the eight traversal levels stores a compact resume key (parent
  clusters plus the copied child's physical SFN key), then rescans the source
  parent after ascent instead of retaining a full object/finder pair;
- copy and replace/recovery execution storage share a union because commit or
  recovery cannot run concurrently with a copy operation;
- the small transaction descriptor remains separate so callers may populate a
  staging directory, including by tree copy, between begin and commit;
- scratch names are regenerated from one nonce and journal records are handled
  in the shared 96-byte workspace rather than stored in several permanent name
  arrays.

### Phase 5 tree copy

`afatfs_copyObjectTree()` is now a real global coordinator rather than a
per-file placeholder. It validates the root's physical SFN fingerprint, creates
the destination with `CREATE_NEW`, preserves supported attributes/timestamps,
and streams file bytes without parsing them. Files, empty directories, nested
directories, zero-byte files, FAT16/FAT32 ordinary directory clusters, depth
errors, destination collisions, no-space errors, and optional durable-copy sync
all have explicit state-machine paths.

The active source finder is released before every child operation or directory
rebind. On descent a 28-byte frame records the two parent clusters and the
source child's physical SFN resume key. On ascent the source parent is rescanned
until sector/index/raw-SFN all match that key, then iteration continues after
the completed child. Partial destinations deliberately survive errors so a
transaction abort or diagnostic caller—not a generic copy error path—decides
whether to erase evidence.

The open-file pool is six slots because the maximum nested-file state owns two
caller parents, two active directory cursors, and the source/destination file
pair simultaneously. Five slots would make destination-file allocation retry
forever only after a copy reached a nested file. Reducing the shared stream
buffer from 128 to 96 bytes offsets part of that required handle.

### Phase 6 recoverable replace

The public async API now exposes begin, commit, abort, and explicit known-parent
recovery with one opaque transaction descriptor. Begin always creates a fresh
`.afat-xxxxxxxx-new` directory; it advances the nonce on `CREATE_NEW` collision
and never adopts stale scratch. Commit rejects an unfinished older journal and
preflights the corresponding `-old` name before it writes PREPARED.

Two fixed same-parent files, `AFATJ0.SYS` and `AFATJ1.SYS`, alternate packed
69-byte records containing magic, version, sequence, state, object kind, nonce,
target component, and reflected CRC-32. The commit order is staged-payload sync,
PREPARED, old-target rename/sync, OLD_RENAMED, staging promotion/sync, PROMOTED,
old-tree delete/sync, then CLEAN. Completion waits for CLEAN rather than
reporting while cleanup is still active.

Recovery selects the highest-sequence valid record, rejects conflicting equal
sequences and invalid target components, and handles power loss on either side
of every rename/journal update. PREPARED preserves/restores the old target;
OLD_RENAMED promotes new or restores old; PROMOTED retains the promoted target
and removes exact old scratch. With no authoritative record, reserved scratch is
reported as `RECOVERY_REQUIRED` and is never guessed/deleted. Abort before
PREPARED deletes only its exact new identity; abort after commit begins uses the
journal recovery rules.

`afatfs_destroy(false)` now lets a live coordinator release its own handle graph
and converts an idle begun transaction into an exact abort before closing
ordinary handles. This prevents shutdown from closing source/destination or
journal handles underneath their callbacks, and prevents a retained staging
parent from making clean destroy wait forever.

### RAM and build measurements

- Phase 4 baseline `afatfs`: 7,804 bytes (`0x1e7c`).
- Final `afatfs`: 8,880 bytes (`0x22b0`), an increase of 1,076 bytes.
- Shared copy/replace workspace: 640 bytes; copy arm 540 bytes; replace arm
  340 bytes; transaction descriptor 68 bytes; traversal frame 28 bytes;
  journal record 69 bytes.
- Final firmware after the implementation build: text 367,752 bytes, data 408
  bytes, BSS 364,908 bytes. The exact text value may move slightly with later
  comment-only/documentation or LTO rebuilds; the static RAM sizes are ABI
  measurements from the ARM compiler.

The 8,880-byte result is 120 bytes below the 9 KB decimal target and roughly
3.5 KB below the original approximately 12 KB design estimate.

### Verification status

Both incremental and clean `make -j4` firmware builds link successfully;
`make img` regenerated `build/LXRV2_lxr02.img` from the 368,160-byte packaged
payload,
and `git diff --check` is clean. The implementation adds no compiler diagnostic
beyond the repository's existing
unused asyncfatfs `eraseCount`, filesystem dead-code, USB packed-pointer, LTO,
newlib syscall-stub, and similar pre-existing warnings. A final clean build,
image packaging, and static ABI measurement have completed. The hardware and
fault-injection matrix remains required because power-cut behavior cannot be
proven by the compiler alone.
