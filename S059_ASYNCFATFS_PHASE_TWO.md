# Session 059 AsyncFATFS speedup — Phase Two implementation plan

Date: 2026-08-30

Status: implementation schedule executed for Gate B; source/build verification
is complete and hardware/media acceptance remains pending.

Line numbers below refer to the current pre-Phase-Two worktree on 2026-08-30.
Use the named symbol and quoted current text as the durable edit anchor if an
earlier edit shifts a later line.

## Decision and implementation boundary

Phase Two is **Gate B only** from `S059_ASYNCFATFS_SPEEDUP.md`:

- stop zero-filling every sector of a newly appended directory cluster;
- initialize only the first sector of that cluster immediately;
- retain `.` and `..` in the first sector of a new child directory;
- retain a valid on-disk `0x00` terminator in every newly visible first sector;
- leave later sectors untouched and hidden behind that terminator; and
- continue using Phase One/Gate A to clear a complete later logical sector
  before moving the terminator into it.

This phase does not implement the optional LFN-storage, directory-hint, cache,
transport, Bank-rewrite, or file-format ideas previously listed as possible
future investigations. Those require profiling and separate plans. Gate B must
remain an isolated, independently revertible filesystem change.

## Observable result

On the tested card, a directory cluster contains 64 sectors. Creating one new
Scene or embedded Kit directory should queue one initialized directory-sector
write instead of 64. The existing full 16-Scene Bank workload creates 32 such
child directories, so the intended static reduction remains approximately:

```text
old child-directory initialization: 32 x 64 = 2,048 sectors
expected sectors actually needed:   16 x 1 + 16 x 2 = 48 sectors
expected avoided initialization:                     2,000 sectors
```

This is a traffic projection, not an accepted timing result. Actual timing must
be recorded from repeatable hardware runs after implementation.

## Future Scene pattern-file size

The planned growth of each Scene's pattern file to roughly 20 kB does not
change Gate B's directory-sector algorithm. One larger file still consumes one
LFN/SFN directory-entry run. Its data sectors are allocated and written through
the regular-file path; they are not directory sectors hidden after `0x00`.

A 20 KiB pattern is about 40 512-byte payload sectors, or about 640 sectors for
16 Scenes. That will increase total Bank Save time and reduce Gate B's future
percentage improvement, but it does not weaken the marker model. Recalculate
directory occupancy only if the future Scene format adds separate files or
longer names, not when the existing pattern payload merely grows.

## Binding behavioral contracts

The implementation must preserve all of the following:

1. The first `0x00` filename byte remains the only persistent end-of-directory
   authority. No RAM-only initialized-sector map or witness may be added.
2. Newly created directories still allocate their first cluster before the
   mkdir callback and are immediately safe for `afatfs_chdir()` and child
   creation.
3. The first sector of the first child-directory cluster is completely zeroed,
   then receives `.` at entry 0 and `..` at entry 1. Entry 2 remains the
   replacement terminator.
4. The first sector of a later appended directory cluster is completely
   zeroed. Entry 0 remains the terminator and dot entries are not repeated.
5. Every later sector may retain arbitrary old media bytes while it remains
   after the terminator. Before Gate A moves the terminator into such a sector,
   `afatfs_prepareDirectoryRunTarget()` must overwrite all 512 bytes.
6. Directory traversal between sectors and clusters remains logical FAT-chain
   traversal. Never substitute `physicalSector + 1` at a cluster boundary.
7. The allocated FAT chain and `physicalSize` remain cluster-sized. Gate B
   changes initialization traffic, not allocation size.
8. The existing cache, callback, error, foreground polling, and final
   `afatfs_sync()` boundaries remain unchanged.
9. FAT16 fixed-root behavior remains unchanged: it cannot be extended.
10. Regular file allocation and data-sector writes are untouched.
11. No LFN/SFN run may cross a 512-byte directory-sector boundary.
12. No new public API, result, enum, signature, include, or caller obligation is
    introduced.

## SRAM and layout contract

Phase Two requires **zero additional retained SRAM**. It must reuse:

- the existing `afatfsExtendSubdirectory_t` union member;
- the directory handle's existing cursor and size fields;
- the existing AsyncFATFS cache;
- the Gate A reservation state and target-sector preparation helpers; and
- automatic scalar locals already used by the extension continuation.

Do not add a buffer, bitmap, initialized-sector counter, new structure member,
or persistent directory hint. The accepted target-ABI sizes must remain:

```text
sizeof(afatfsCreateFile_t)    = 144
sizeof(afatfsFile_t)          = 188
sizeof(afatfsRenameObject_t)  = 552
linked afatfs symbol          = 6,984 bytes (0x1b48)
```

Phase One also added zero retained SRAM: it reused existing union space and the
same four measurements remained unchanged. Phase Two may alter Flash/code size,
but any retained-RAM delta is a failed implementation constraint, not an
expected cost.

## Current audited source anchors

| File and current line | Current role | Phase Two disposition |
| --- | --- | --- |
| `asyncfatfs.c:426-442` | Extension phase enum and retained operation state | Rename the multi-sector phase; document existing state; add no fields. |
| `asyncfatfs.c:846-863` | Phase-One target-ABI size assertions | Retain values and broaden their comment/messages to cover all S059 work. |
| `asyncfatfs.c:1273-1285` | Cluster-and-sector cursor accessor | Delete; Gate B removes its sole caller. |
| `asyncfatfs.c:1938-1947` | Allocation handoff comment | Replace obsolete “zero-fill phase” wording. |
| `asyncfatfs.c:2945-3045` | Directory-extension continuation | Replace the whole-cluster loop and rewind with one first-sector initialization. |
| `asyncfatfs.c:3051-3083` | Directory-extension entry point | Replace the obsolete full-cluster contract comment; implementation otherwise unchanged. |
| `asyncfatfs.c:3738-3750` | Gate A target-sector preparation comment | State that this helper initializes every later sector immediately before exposure. |
| `asyncfatfs.c:4086-4111` | New-directory initialization handoff | Update the callback guarantee comment; implementation unchanged. |
| `asyncfatfs.c:4175-4179` | Legacy no-terminator create path comment | Replace the obsolete “unchanged initializer” wording. |
| `asyncfatfs.h:358-390` | Public directory-create contract | Replace Phase-One/deferred-Gate-B and full-zero-fill wording. |
| `ASYNCFATFS_REFERENCE.md:159-182` | Directory Create/Open reference | Document first-sector initialization and caller guarantees. |
| `ASYNCFATFS_REFERENCE.md:417-453` | Terminator and LFN allocation reference | Replace the obsolete pre-Gate-A description with the final Gate A+B model. |
| `SRAM_MANIFEST.md:1-18, 68-109` and a new Session 059 note | Linked-memory authority | Record final measurements and correct the stale AsyncFATFS owner size. |
| `MEMORY.md:1324-1340` | Durable S059 record | Replace “Gate B remains deferred” only after acceptance. |
| `S059_ASYNCFATFS_SPEEDUP.md:781-795` | Parent proposal completion record | Append actual Gate B code/test/timing/SRAM result. |
| This document | Phase Two implementation/verification record | Append the exact final result; do not rewrite projections as measurements. |

## Production source-change schedule

Only `Core/Hardware/SD/asyncfatfs/asyncfatfs.c` receives functional changes.
`Core/Hardware/SD/asyncfatfs/asyncfatfs.h` receives contract-comment changes
only.

### 1. Rename the extension phase and document the unchanged operation state

File: `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`

Current lines: 426-442.

Exact edits:

1. At current line 429, replace
   `AFATFS_EXTEND_SUBDIRECTORY_PHASE_WRITE_SECTORS` with
   `AFATFS_EXTEND_SUBDIRECTORY_PHASE_INITIALIZE_FIRST_SECTOR`.
2. Make the same token replacement at current lines 2959 and 2966.
3. Do not reorder the enum or change its underlying size.
4. Do not add anything to `afatfsExtendSubdirectory_t`.
5. Replace the one-line comment above `appendFreeCluster` with the following
   adjacent contract block. The fields below it remain byte-for-byte unchanged.

```c
/*
 * Retained state for one asynchronous directory-cluster extension.
 *
 * What: Reuses afatfsAppendFreeCluster_t as the leading sub-operation, then
 * retains only the extension phase, the parent cluster needed for a new
 * child's ".." entry, and the original completion callback.
 *
 * Why: Gate B changes how much of an appended cluster is initialized, not the
 * allocation or callback state machine. Keeping appendFreeCluster first
 * preserves the required compatible union layout and avoids any retained-SRAM
 * growth.
 *
 * Inputs: the prior cursor cluster supplied to the append sub-operation, the
 * parent directory cluster for a first child cluster, and the caller callback.
 *
 * Outputs/effects: resumable append/initialize/success/failure state only; no
 * sector buffer, initialized-sector witness, or additional cluster coordinate
 * is retained here.
 *
 * Accessors: afatfs_appendRegularFreeClusterContinue() consumes the leading
 * member; afatfs_fileGetCursorPhysicalSector() derives the initialized sector
 * from the directory cursor when needed.
 *
 * Affiliates: afatfs_extendSubdirectoryContinue(),
 * afatfs_extendSubdirectory(), afatfsFileOperation_t, AFATFS_MAX_OPEN_FILES,
 * and the S059 retained-state static assertions.
 */
```

Why this change exists: the old phase name promises a plural sector loop that
Gate B removes. Leaving it would make the source contract false. Renaming an
existing enum constant adds no state and creates no public ABI change because
the enum is private to `asyncfatfs.c`.

### 2. Extend the retained-state assertions to cover Phase Two

File: `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`

Current lines: 846-863.

Exact edits:

1. Keep all three `_Static_assert` expressions and numeric values unchanged.
2. Replace “pre-Phase-One”/“Phase One” wording in the adjacent block and
   assertion strings with S059-wide wording.
3. Do not add an `afatfsExtendSubdirectory_t` field or a separate allocation.

Use this adjacent comment text:

```c
/*
 * Retained-state ceiling for the S059 directory optimizations.
 *
 * What: Verifies that the target ABI still gives the create state, each file
 * handle, and the global rename state their accepted pre-S059 sizes.
 *
 * Why: afatfsCreateFile_t is the largest per-handle operation-union member,
 * so growth multiplies through five open handles and currentDirectory and also
 * enlarges rename's embedded newNameState. Gate B must reuse the existing
 * extension state and sector cache rather than consume Pattern-reserved SRAM1.
 *
 * Inputs: the compiler's completed private type layouts.
 *
 * Outputs/effects: a build failure instead of an unapproved retained-RAM
 * increase; no runtime code or storage is emitted by passing assertions.
 *
 * Accessors: sizeof() is the sole compile-time accessor.
 *
 * Affiliates: AFATFS_MAX_OPEN_FILES, afatfsFileOperation_t, afatfs_t,
 * afatfsExtendSubdirectory_t, and SRAM_MANIFEST.md.
 */
```

Change only the diagnostic strings to:

```c
"S059 create state must remain 144 bytes"
"S059 file handle must remain 188 bytes"
"S059 rename state must remain 552 bytes"
```

Inputs are compile-time type layouts. Outputs are unchanged layout or a failed
build. There is no runtime accessor or output.

### 3. Delete the accessor made obsolete by the removed loop

File: `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`

Current lines: 1273-1285.

Delete the complete comment and function definition for:

```c
static void afatfs_fileGetCursorClusterAndSector(
    afatfsFilePtr_t file, uint32_t *cluster, uint16_t *sector)
```

Exact reason: its only caller is current line 2968 inside the whole-cluster
zeroing loop. The Phase Two replacement needs only the current physical sector,
which is already supplied by `afatfs_fileGetCursorPhysicalSector()` at current
lines 1263-1271. Leaving the old accessor would introduce an unused-static
warning and preserve an API with no remaining semantic owner.

Inputs and outputs disappear with the function. The retained accessor is
`afatfs_fileGetCursorPhysicalSector(directory)`, whose input is the existing
logical cursor and whose output is the one physical sector to initialize.
Affiliates are the directory-extension continuation and `-Wall/-Wextra` build.
No replacement comment is needed at the deletion site.

### 4. Correct the append allocator's handoff comment

File: `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`

Current lines: 1938-1947 inside
`afatfs_appendRegularFreeClusterContinue()`.

No executable statement changes here. Replace the existing comment beginning
“Assign the new cluster to the active cursor” with:

```c
/*
 * Publish the newly allocated cluster through the active cursor.
 *
 * What: Records the allocated cluster as the cursor cluster, expands
 * physicalSize by one complete cluster, and records firstCluster when this is
 * the file or directory's first allocation.
 *
 * Why: The following directory-extension phase derives the first physical
 * sector from this cursor and initializes that sector before mkdir or marker
 * advance can expose it. FAT allocation remains cluster-sized even though Gate
 * B no longer initializes every sector immediately.
 *
 * Inputs: the successful free-cluster search result and previousCluster.
 *
 * Outputs/effects: updated cursorCluster, physicalSize, and possibly
 * firstCluster; the following FAT-link and directory-entry publication phases
 * are unchanged.
 *
 * Accessors: afatfs_clusterSize() supplies the allocation increment;
 * afatfs_saveDirectoryEntry() later writes firstClusterHigh/Low for a first
 * allocation.
 *
 * Affiliates: afatfs_extendSubdirectoryContinue(), regular-file fwrite
 * allocation, FAT1/FAT2 update phases, and the mkdir callback boundary.
 */
```

Why this must exist: the current comment says a later “zero-fill phase” needs a
sector. After Gate B that phrase incorrectly implies full-cluster clearing.

### 5. Replace whole-cluster initialization with first-sector initialization

File: `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`

Current function: `afatfs_extendSubdirectoryContinue()`, lines 2945-3045.

#### 5.1 Local declarations

At current lines 2949-2952, replace:

```c
uint32_t clusterNumber, physicalSector;
uint16_t sectorInCluster;
```

with:

```c
uint32_t physicalSector;
```

Keep `opState`, `status`, and `sectorBuffer` unchanged. `clusterNumber` and
`sectorInCluster` exist only to drive the loop that this phase deletes.

#### 5.2 Append-success phase transition

At current line 2959, change the success transition to:

```c
opState->phase =
    AFATFS_EXTEND_SUBDIRECTORY_PHASE_INITIALIZE_FIRST_SECTOR;
```

The append failure path remains exactly as it is.

#### 5.3 Replace the complete multi-sector case

Replace current lines 2966-3027, from
`case AFATFS_EXTEND_SUBDIRECTORY_PHASE_WRITE_SECTORS:` through the transition to
`AFATFS_EXTEND_SUBDIRECTORY_PHASE_SUCCESS`, with one first-sector case.

The replacement logic must be exactly equivalent to the following:

```c
case AFATFS_EXTEND_SUBDIRECTORY_PHASE_INITIALIZE_FIRST_SECTOR:
    /* Detailed contract block from below goes here. */
    physicalSector = afatfs_fileGetCursorPhysicalSector(directory);
    status = afatfs_cacheSector(physicalSector,
                                &sectorBuffer,
                                AFATFS_CACHE_WRITE,
                                0);
    if (status != AFATFS_OPERATION_SUCCESS)
        return status;

    memset(sectorBuffer, 0, AFATFS_SECTOR_SIZE);

    if (directory->directoryEntryPos.sectorNumberPhysical != 0u &&
        directory->cursorOffset == 0u) {
        fatDirectoryEntry_t *dirEntries =
            (fatDirectoryEntry_t *)sectorBuffer;

        memset(dirEntries[0].filename, ' ', sizeof(dirEntries[0].filename));
        dirEntries[0].filename[0] = '.';
        dirEntries[0].firstClusterHigh = directory->firstCluster >> 16;
        dirEntries[0].firstClusterLow = directory->firstCluster & 0xffffu;
        dirEntries[0].attrib = FAT_FILE_ATTRIBUTE_DIRECTORY;

        memset(dirEntries[1].filename, ' ', sizeof(dirEntries[1].filename));
        dirEntries[1].filename[0] = '.';
        dirEntries[1].filename[1] = '.';
        dirEntries[1].firstClusterHigh =
            opState->parentDirectoryCluster >> 16;
        dirEntries[1].firstClusterLow =
            opState->parentDirectoryCluster & 0xffffu;
        dirEntries[1].attrib = FAT_FILE_ATTRIBUTE_DIRECTORY;
    }

    opState->phase = AFATFS_EXTEND_SUBDIRECTORY_PHASE_SUCCESS;
    goto doMore;
break;
```

Preserve the project's prevailing constant style if the nearby code uses
`0xFFFF` rather than `0xffffu`; this is not a semantic change. Do not add an
explicit `afatfs_cacheSectorMarkDirty()` call: requesting
`AFATFS_CACHE_WRITE` already marks the cache descriptor dirty inside
`afatfs_cacheSector()` before it returns the buffer. The complete `memset()` is
still mandatory because WRITE-only access deliberately avoids reading old card
contents.

Delete all of the following old behavior:

- the call to `afatfs_fileGetCursorClusterAndSector()`;
- the `while (1)` loop;
- `sectorInCluster` comparison and increment;
- `physicalSector++`;
- the per-sector `afatfs_fseekAtomic(..., AFATFS_SECTOR_SIZE)`;
- the final negative seek that rewinds by `sectorsPerCluster - 1`; and
- comments promising that every sector is cleared.

Do not replace physical increment with another loop. The cursor already points
at the first sector of the appended cluster after
`afatfs_appendRegularFreeClusterContinue()`, and it must remain there when the
callback completes.

#### 5.4 Adjacent implementation comment block

Place this block immediately inside the new phase case, before the physical
sector accessor:

```c
/*
 * Initialize only the first visible sector of the appended directory cluster.
 *
 * What: Obtains the sector at the appended-cluster cursor with WRITE-only cache
 * ownership, clears all 512 bytes, writes "." and ".." only for a new child
 * directory's first cluster, and leaves the first unused entry as 0x00. Later
 * sectors in the allocated cluster remain untouched and logically invisible.
 *
 * Why: FAT allocation is cluster-sized, but a valid end marker makes bytes
 * after it outside the live namespace. Clearing every sector caused 64 writes
 * per child directory on the tested card. Gate A now guarantees that any later
 * sector is fully cleared before the marker moves into it.
 *
 * Inputs: directory->cursorCluster/cursorOffset after append completion,
 * directory->firstCluster, directoryEntryPos to distinguish a non-root child,
 * and opState->parentDirectoryCluster for the first-cluster ".." entry.
 *
 * Outputs/effects: one dirty, fully initialized sector; correct dot entries for
 * a first child cluster; a zero terminator at entry 2 for that cluster or entry
 * 0 for a later appended cluster; unchanged FAT chain and physicalSize; and no
 * cursor seek across the untouched remainder of the cluster.
 *
 * Accessors: afatfs_fileGetCursorPhysicalSector() maps the logical cursor to
 * media; afatfs_cacheSector(..., AFATFS_CACHE_WRITE, ...) obtains and marks the
 * cache sector dirty without reading stale media; memset() establishes the
 * complete hidden-to-visible sector invariant.
 *
 * Affiliates: afatfs_appendRegularFreeClusterContinue(),
 * afatfs_prepareDirectoryRunTarget(),
 * afatfs_handoffCreatedDirectoryToInitializer(), afatfs_findNext(), FAT16-root
 * no-extension handling, and the final afatfs_sync() boundary.
 */
```

#### 5.5 Inputs, outputs, and failure behavior

- Input cursor: positioned at the start of the newly appended cluster by the
  existing append sub-operation.
- First-cluster discriminator: `directory->cursorOffset == 0` together with a
  real parent directory-entry position.
- Parent input: `opState->parentDirectoryCluster`, set from the mkdir parent;
  zero for later extensions.
- Output sector: one completely initialized dirty cache sector.
- Output marker: entry 2 for a new child, entry 0 for a later cluster.
- Callback: still issued only by the existing SUCCESS case after this cache
  operation succeeds.
- Persistence: still guaranteed to callers only at the existing final sync;
  immediate child access is safe because AsyncFATFS observes its dirty cache.
- Cache busy/failure: return the same status as the old loop and resume through
  the same phase; do not invent a retry counter or result code.

### 6. Replace `afatfs_extendSubdirectory()`'s obsolete contract comment

File: `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`

Current lines: 3051-3061 immediately above
`afatfs_extendSubdirectory()`.

Keep the complete function body at current lines 3062-3083 unchanged. Replace
the comment, including the `Tthe` typo, with:

```c
/*
 * Queue asynchronous extension of one non-FAT16-root directory.
 *
 * What: Appends one FAT cluster and initializes its first sector immediately.
 * For a new child directory, that sector contains ".", "..", and a valid
 * terminator. For a later extension, entry 0 is the terminator. Remaining
 * sectors are initialized individually by marker-advance preparation before
 * they can enter the live namespace.
 *
 * Why: Callers need a directory that is safe to enter or continue scanning,
 * not an eagerly zero-filled allocation unit. Keeping later sectors hidden
 * removes redundant I/O while the persistent marker preserves remount safety.
 *
 * Inputs: directory must be idle with its cursor at allocated EOF;
 * parentDirectory is required only for the first cluster of a new child and
 * supplies the ".." cluster; callback may be NULL for an internal extension.
 *
 * Outputs/effects: returns SUCCESS, IN_PROGRESS, or FAILURE through the
 * existing asynchronous contract; appends one cluster, initializes one sector,
 * and invokes callback with directory or NULL on the existing terminal path.
 * FAT16 fixed root and busy handles fail before any operation is queued.
 *
 * Accessors: afatfs_fileIsBusy() enforces ownership;
 * afatfs_appendRegularFreeClusterInitOperationState() seeds allocation from
 * cursorPreviousCluster; afatfs_extendSubdirectoryContinue() performs the work.
 *
 * Affiliates: mkdir create handoff, Gate A next-sector preparation,
 * afatfs_fileOperationContinue(), FAT allocation, directory scans, and
 * afatfs_sync().
 */
```

### 7. Update Gate A's target-sector preparation comment for Gate B

File: `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`

Current lines: 3738-3750 above `afatfs_prepareDirectoryRunTarget()`.

The function body at current lines 3751-3784 remains unchanged. Replace only
its comment with:

```c
/*
 * Prepare a later logical directory sector before moving the end marker.
 *
 * What: Uses currentDirectory's logical cursor and FAT chain, extends the
 * directory if the cursor has reached allocated EOF, obtains the target sector
 * with WRITE ownership, clears all 512 bytes, and selects entry zero for the
 * pending SFN or LFN/SFN run.
 *
 * Why: Under Gate B, sectors after 0x00 may intentionally contain stale media
 * bytes. The complete target must therefore be initialized immediately before
 * the marker can move. Physical adjacency is not a valid substitute for FAT
 * traversal, and an appended cluster's initialized first sector is cleared
 * again here deliberately so this helper has one unconditional publication
 * contract.
 *
 * Inputs: afatfs.currentDirectory at the next logical sector and a
 * TERMINATOR_MOVED or end-of-allocated-directory reservation.
 *
 * Outputs/effects: a dirty fully zeroed target sector, selectedRunStart at
 * entry zero, or the existing asynchronous/FAT failure status. No old marker
 * byte is changed here.
 *
 * Accessors: afatfs_fileIsBusy(), afatfs_isEndOfAllocatedFile(),
 * afatfs_extendSubdirectory(), afatfs_fileGetCursorPhysicalSector(), and
 * afatfs_cacheSector(..., AFATFS_CACHE_WRITE, ...).
 *
 * Affiliates: create and rename target phases, target-media persistence,
 * old-terminator-tail retirement, the lazy first-sector initializer, and the
 * FAT16 fixed-root no-extension rule.
 */
```

Why this comment must change: its current affiliate text explicitly calls the
whole-cluster initializer “unchanged.” The executable helper is already the
Gate B safety mechanism and needs no functional edit.

### 8. Update the mkdir handoff and legacy no-terminator comments

File: `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`

#### 8.1 `afatfs_handoffCreatedDirectoryToInitializer()`

Current lines: 4086-4111.

Keep all executable statements unchanged. Replace its inner comment with:

```c
/*
 * Transfer a newly named directory from create state to initialization state.
 *
 * What: Preserves the original mkdir callback, clears the CREATE_FILE union
 * owner, and starts EXTEND_SUBDIRECTORY so the child's first cluster and first
 * sector are ready before completion.
 *
 * Why: A directory entry initially has firstCluster == 0 and cannot be entered.
 * The operation union also cannot hold create and extension state at once.
 * Gate B still requires the visible first sector, dot entries, and terminator
 * before callback; only the unused remainder of the cluster is deferred.
 *
 * Inputs: the newly created directory handle and the original
 * afatfs_mkdir()/afatfs_mkdir_lfn() callback.
 *
 * Outputs/effects: create reservation/LFN scan state is cleared; extension owns
 * the handle; the callback is later invoked with a usable directory or NULL.
 *
 * Accessors: afatfs_resetDirectoryRunReservation(), afatfs_lfnScanReset(), and
 * afatfs_extendSubdirectory().
 *
 * Affiliates: short/LFN create success phases,
 * afatfs_appendRegularFreeClusterContinue(), afatfs_saveDirectoryEntry(),
 * afatfs_chdir(), and the public mkdir contract.
 */
```

#### 8.2 No-terminator create fallback

Current lines: 4175-4179 inside
`AFATFS_CREATEFILE_PHASE_FIND_FILE` when `entry == NULL`.

Keep the code unchanged and replace only the short comment with:

```c
/*
 * A legacy directory with no 0x00 either uses a previously proven deleted run
 * or advances to logical allocated EOF. An extendable directory then appends a
 * cluster whose initialized first sector exposes entry zero as a local
 * terminator-owned run; a full FAT16 fixed root fails normally.
 */
```

This preserves old all-`0xE5` media support while removing the obsolete claim
that extension still uses the prior initializer.

## Public header change schedule

### 9. Replace the Phase-One directory-create contract with the final Gate A+B contract

File: `Core/Hardware/SD/asyncfatfs/asyncfatfs.h`

Current lines: 358-390, above `afatfs_mkdir()` and `afatfs_opendir()`.

Delete the existing block beginning “Phase-One public contract remains” and
ending with the visible-schema guidance. Replace it with the following block.
Do not change any declaration at current lines 245-250, 279-283, or 392-427.

```c
/*
 * Persistent-marker and lazy directory-initialization public contract.
 *
 * What: Create and rename preserve the first FAT 0x00 namespace boundary.
 * Newly created directories have an allocated first cluster, a completely
 * initialized first sector, correct "." / ".." entries, and a valid
 * terminator before their callback. Additional directory sectors are cleared
 * internally before the marker can move into them.
 *
 * Why: Callers need a handle that is immediately safe for afatfs_chdir() and
 * child creation; they do not require every unused sector in the allocated
 * cluster to be written. The on-disk terminator, rather than retained RAM, is
 * sufficient to hide uninitialized sectors across close and remount.
 *
 * Inputs: the existing component name, mode or match policy, optional alias
 * output buffer, and completion callback accepted by the declarations below.
 *
 * Outputs/effects: public handles, aliases, results, and callback timing are
 * unchanged. No caller initializes sectors. Final removable-media persistence
 * remains the caller-visible afatfs_sync()/flush boundary.
 *
 * Accessors/APIs: afatfs_fopen[_lfn](), afatfs_mkdir[_lfn](),
 * afatfs_opendir[_lfn](), afatfs_renameObject_lfn(), afatfs_chdir(), and
 * afatfs_sync().
 *
 * Affiliates: asyncfatfs.c create/rename reservation, directory extension,
 * target-sector preparation, filesystem.c component workflows, and
 * ASYNCFATFS_REFERENCE.md.
 *
 * Directory create/open details:
 * The callback receives NULL or a directory handle immediately safe for
 * afatfs_chdir(). A new child has its firstCluster fields written back to the
 * parent SFN entry and its first sector initialized before callback. Ordinary
 * files may still allocate their first cluster lazily on first fwrite().
 *
 * The *_lfn variants continue to accept one visible current-directory
 * component, sanitize unsupported characters, strip trailing spaces/periods,
 * apply matchMode, and optionally return an 8.3 alias. That alias remains an
 * operation detail; user-visible schemas store the display component.
 */
```

Why this is comment-only: Gate B changes a private implementation detail while
preserving the externally useful guarantee. `fopen`, mkdir, open-directory, and
rename signatures and result semantics do not change.

## Documentation and completion-record schedule

Documentation changes occur only after the implementation passes its source,
build, media, and product gates. Failed/reverted Gate B must not be documented
as shipped.

### 10. Update `ASYNCFATFS_REFERENCE.md` — Directory Create/Open

File: `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`

Current lines: 159-182.

Replace the newly-created-directory bullet list that currently says
“zero-fill the new cluster” with an exact final contract:

- allocate the first cluster and publish its cluster number in the parent SFN;
- clear the complete first sector;
- write `.` and `..` with the existing cluster values;
- retain a zero entry immediately after the dot entries;
- complete mkdir only after this visible first sector is queued successfully;
- leave later cluster sectors hidden and untouched until marker advance clears
  each complete target sector; and
- preserve the existing final sync/flush persistence boundary.

Add one sentence distinguishing allocated size from initialized extent: the FAT
chain and `physicalSize` cover the entire cluster even when only its first
sector has been initialized. Retain the ordinary-file lazy allocation paragraph
and alias guidance unchanged.

Documentation description:

- **What:** describes the caller-visible state of a created directory.
- **Why:** removes the now-false full-cluster promise without weakening mkdir.
- **Inputs:** public mkdir component, match mode, alias buffer, and callback.
- **Outputs:** usable directory handle, dots, marker, and unchanged sync rule.
- **Accessors/APIs:** mkdir/opendir variants, `afatfs_chdir()`, `afatfs_sync()`.
- **Affiliates:** extension continuation, create handoff, filesystem workflows.

### 11. Replace `ASYNCFATFS_REFERENCE.md` — Directory Terminators And LFN Creation

File: `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`

Current lines: 417-453.

Replace the complete current section. It predates Gate A and incorrectly says
that a short create and rename were unaffected and that an insufficient local
terminator is retired during continued scanning.

The replacement section must document, in this order:

1. `0x00` ends the live namespace and scanning stops at the first one.
2. `0xE5` is reusable but does not prove absence; the first large-enough
   sector-local deleted run is latched while collision scanning continues.
3. A deleted-run writer touches only its selected entries and never clears the
   following entry.
4. A local terminator-owned run must fit the complete SFN or LFN/SFN run plus
   one replacement zero entry in the same 16-entry sector.
5. If it does not fit, the whole run moves to entry 0 of the next logical
   directory sector; no LFN/SFN run crosses a sector.
6. The next sector is reached through the cursor/FAT chain. If allocated EOF is
   reached, a non-FAT16-root directory appends a cluster.
7. The complete target sector is cleared, the new run and replacement marker
   are written, and that target is allowed to reach media before the old marker
   through old-sector tail is retired to `0xE5`.
8. Gate B initializes only an appended cluster's first sector. All later stale
   sectors remain invisible until step 7 initializes one.
9. A directory with no terminator remains compatible: use a proven deleted run
   or extend at logical exhaustion; a full FAT16 fixed root fails normally.
10. Short create, LFN create, and same-parent rename share these reservation and
    publication rules.
11. This ordering does not make create/rename fully power-loss transactional;
    it prevents exposure of stale post-marker entries and preserves the final
    sync guarantee.

Use the same What/Why/Inputs/Outputs/Accessors/Affiliates vocabulary as the
source comments so the reference and code cannot imply different guarantees.

### 12. Update the SRAM manifest from the final linked image

File: `knowledge_files/specification_reference/SRAM_MANIFEST.md`

Current anchors: introductory linked snapshot at lines 1-18, `afatfs` owner row
at current line 85, and a new Session 059 result section after the Session 058
note.

Required edits after a clean final link:

1. Regenerate `text`, `data`, `bss`, `.text`, `.data`, `.bss`, `.dma_nocache`,
   `.dtcm`, `.dtcmz`, image size, and relevant owner symbols from the actual
   final `build/lxr02.elf`; do not copy estimates from this plan.
2. Replace the stale primary-owner row `afatfs | 7,344 B` with the measured
   current result. Gate B is expected to remain 6,984 B (`0x1b48`).
3. Add a Session 059 Gate A/B note recording the before/after linked `afatfs`
   symbol and the three asserted structure sizes.
4. State explicitly that Phase One and Phase Two added zero retained SRAM if
   and only if the measured values remain unchanged.
5. Attribute any whole-image SRAM movement to its exact symbol owner; do not
   infer an AsyncFATFS allocation from aggregate `.bss` movement alone.
6. Record that no Pattern-reserved SRAM1 or delay-line-reserved DTCM was used.

Accessors/commands used for the record:

```text
arm-none-eabi-size build/lxr02.elf
arm-none-eabi-size -A build/lxr02.elf
arm-none-eabi-nm -S --size-sort build/lxr02.elf
```

With LTO, the global may appear as `afatfs.lto_priv.0`; its expected size field
is hexadecimal `00001b48`.

### 13. Update durable Session 059 records after acceptance

#### `MEMORY.md`

Current lines: 1324-1340, “Session 059 AsyncFATFS Phase One record.”

Do not erase the Gate A facts. Replace the bullet saying Gate B is deferred
with a dated Gate B result that records:

- first-sector-only extension shipped or was reverted;
- exact build and SRAM results;
- hardware/media filesystems exercised;
- measured stopped/running playback Bank times;
- payload/hash and host-check result; and
- any unrun acceptance item stated explicitly.

If Gate B is accepted, retitle the section from “Phase One record” to
“Phase One/Two record.” If it is reverted, retain the original title and record
the failed case without claiming Gate B shipped.

#### `S059_ASYNCFATFS_SPEEDUP.md`

Current lines: 781-795, “2026-08-30 implementation record.”

Append rather than overwrite the Gate A record. State the exact Gate B source
scope, whether the full-cluster loop was removed, final linked sizes, test
coverage, timing, card format/cluster size, and acceptance/reversion decision.
The 2,000-sector and several-second figures remain projections unless measured.

#### `S059_ASYNCFATFS_PHASE_TWO.md`

Append an “Implementation notes / verification record” section containing the
starting commit, exact changed symbols, clean-build result, retained-RAM result,
hardware/media evidence, future-size pattern fixture result, and final Gate B
decision. Do not edit the plan body to make a failed test look planned.

### 14. Preserve the Phase One plan as a historical phase boundary

File: `S059_ASYNCFATFS_PHASE_ONE.md`.

Make no retrospective edits to its statements that Gate B was deferred from
Phase One and whole-cluster zero-fill remained in that build. Those statements
are historically correct. Phase Two shipment belongs in the parent proposal,
MEMORY, SRAM manifest, reference, and this Phase Two record.

## Files requiring no functional change

The following files must be inspected/tested but not functionally edited for
Gate B:

- `Core/Hardware/SD/filesystem.c` and `.h`: no Bank/Scene/Kit/Instrument phase,
  serializer, index, cache, playback, or final-sync change.
- `Core/Hardware/SD/asyncfatfs/fat_standard.c` and `.h`: existing `0x00`,
  `0xE5`, dot-entry, and LFN classifiers remain authoritative.
- `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c` and `.h`: no timeout, burst, SPI,
  DMA, command, or callback change.
- `Core/Hardware/SD/storageTypes.c` and `.h`: no schema or filename change.
- Menu, Preset, playback, codec, and UI files: no ownership or progress change.
- Bank Load: no traversal, commit, or error-path change.
- Instrument index repair: no `.hcindex` policy or recovery change.
- Linker scripts: no memory region or section change.

If implementation appears to require any functional edit in these files, stop
and amend this plan before making it. Such a need indicates that Gate B is no
longer an isolated directory-initialization optimization.

## Implementation order

1. Record the current source revision, clean/dirty worktree state, linked image
   sizes, `afatfs` symbol, structure sizes, card format, and Bank timing fixture.
2. Rename the extension phase and improve the operation-state/SRAM comments.
3. Delete `afatfs_fileGetCursorClusterAndSector()`.
4. Replace the extension loop with the first-sector phase.
5. Update the allocator, extension wrapper, target preparation, mkdir handoff,
   and no-terminator source comments.
6. Update the public header contract comment without changing declarations.
7. Run static review and clean target builds.
8. Run focused directory-sector/media fixtures.
9. Run all product load/save workflows and Bank performance tests.
10. If Gate B passes, update the reference, SRAM manifest, MEMORY, parent plan,
    and this completion record.
11. If a failure is unique to Gate B, revert only items 1-9 of the production
    source/header patch, retain Gate A, and document the exact failure.

Do not combine optional LFN, caching, transport, or product-flow experiments
with these commits. A timing or compatibility regression must be attributable
to Gate B alone.

## Verification schedule

### A. Static source and build checks

1. Confirm the old phase token is gone:

   ```text
   rg -n "AFATFS_EXTEND_SUBDIRECTORY_PHASE_WRITE_SECTORS" Core/Hardware/SD/asyncfatfs
   ```

   Expected: no result.

2. Confirm the deleted accessor and old full-zero wording are gone from live
   source/header/reference files:

   ```text
   rg -n "afatfs_fileGetCursorClusterAndSector|zero-fill the new cluster|fully zero-filled|zero out that cluster" \
     Core/Hardware/SD/asyncfatfs knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md
   ```

   Expected: no result. Historical plans may still contain those phrases.

3. Confirm no `physicalSector++`, multi-sector loop, or rewind remains inside
   `afatfs_extendSubdirectoryContinue()`.
4. Confirm the new phase performs exactly one WRITE-only cache acquisition and
   one complete 512-byte `memset()`.
5. Confirm `.`/`..` creation retains the exact current condition and cluster
   fields.
6. Confirm `afatfs_prepareDirectoryRunTarget()` still performs its complete
   sector `memset()` and selects entry zero.
7. Confirm no public declaration or result enum changed.
8. Run `git diff --check`.
9. Run a clean `make` and `make img`; inspect all warnings and do not dismiss a
   newly unused helper or state variable.

### B. Retained-memory checks

1. Verify all three `_Static_assert`s pass at 144/188/552 bytes.
2. Measure the linked `afatfs` symbol with `arm-none-eabi-nm -S`; expect
   `0x1b48`/6,984 bytes.
3. Compare before/after `.data`, `.bss`, `.dtcm`, `.dtcmz`, and
   `.dma_nocache` section sizes.
4. Confirm no new retained global, cache, buffer, or operation-state member was
   added.
5. If `afatfs` or any asserted type grows, reject the implementation pending an
   exact allocation review; do not consume Pattern-reserved SRAM implicitly.

### C. Focused directory initialization fixtures

Use a disposable FAT image/card whose free directory cluster sectors are
pre-filled with recognizable nonzero bytes. Inspect raw sectors before and
after each case.

1. **First child cluster:** create a new directory. Verify only its first sector
   is initialized, `.` points to the child, `..` points to the parent, entry 2 is
   `0x00`, and sectors 1..N retain the pre-fill pattern.
2. **Later appended cluster:** fill a directory until extension is required.
   Verify the appended cluster's first sector is zero with entry 0 as `0x00`,
   has no dot entries, and later sectors retain pre-fill.
3. **Advance within one cluster:** force a run that cannot fit after a marker.
   Verify the next logical sector is completely cleared before its new run and
   marker appear, while the old marker tail becomes `0xE5` only after target
   persistence.
4. **Advance across a non-contiguous FAT link:** arrange a fragmented directory
   chain and repeat case 3. Verify traversal follows the FAT link rather than
   physical adjacency.
5. **Advance at allocated EOF:** force extension from the final sector. Verify
   cluster allocation, first-sector initialization, target run/marker, and
   callback completion.
6. **One-sector clusters:** verify first-sector initialization is also complete
   cluster initialization and no rewind/underflow assumption remains.
7. **FAT16 fixed root:** fill it to exhaustion and verify normal failure with no
   attempted cluster append.
8. **Legacy no-terminator directory:** use an allocated directory ending in
   `0xE5` rather than `0x00`; verify deleted-run reuse or normal extension.
9. **Stale directory-shaped bytes:** place plausible SFN/LFN records after the
   marker; verify scans do not expose them before target clearing.
10. **Reboot/remount:** repeat scans and creates without any retained RAM state;
    the on-disk marker alone must recover the same namespace.

### D. Create, rename, removal, and interruption fixtures

1. Short SFN and one-to-four-fragment LFN creates where run+marker fits locally,
   exactly fails to fit, and moves to the next sector.
2. Deleted holes before a later same-display or same-alias object; no duplicate
   may be created.
3. Rename in place, into a deleted run, at a local terminator, and through a
   moved terminator; preserve first cluster, attributes, size, and children.
4. Remove and recursive delete followed by recreation in the retired holes.
5. Power interruption or fault injection before target clear, after target
   run/marker write, before old-tail retirement, and before final sync. No stale
   post-marker object may become visible.
6. Host-created FAT16/FAT32 media, small-cluster media, and the tested 32 KiB-
   cluster card.

### E. Product compatibility matrix

After a fresh boot, test save, immediate reload, reboot, and reload as
applicable:

- Instrument normal and Morph save/load, overwrite, typed-directory creation,
  `.hctmp` cleanup, and `.hcindex` regeneration/recovery;
- Kit and Kit Morph save/load, rename/canonical repair, overwrite/delete/
  recreate, six Instrument members, and `kitset.kcg`;
- Scene save/load, canonical repair, embedded Kit, `sceneset.scg`,
  `pattern.pat`, and `effects.fx`;
- Bank sparse/full new and overwrite save, all 16 progress steps, final index/
  HCNAMES work, full Bank Load, and content comparison;
- settings temporary-file creation and rename promotion;
- AutoSave ensure/read/write/sync and reboot selection;
- Samples/Loops enumeration and legacy short-name reads; and
- dot-prefixed files plus host-visible scans.

The reported fast-scrolling Kit filesystem error is not part of Gate B unless
it becomes reproducible specifically after this change. Do not silently fold an
unrelated product repair into the Phase Two patch.

### F. Bank performance and future-pattern fixture

Use the same card, format, firmware logging mode, Bank content, existing target,
and timing boundaries as the accepted Phase One observation:

- start: accepted Bank Save command;
- finish: UI leaves `...` after the existing final sync;
- primary: full 16-Scene Bank, playback stopped;
- compatibility: same Bank, playback running.

Run at least two successful post-change trials for each condition. Compare file
count, logical sizes, payload hashes, filesystem-check result, reboot load, and
wall time.

Also create a representative future fixture with approximately 20 KiB of
pattern payload per Scene. Expect about 640 pattern payload sectors across 16
Scenes. Use it to separate the fixed directory-initialization saving from
future payload cost; do not expect Gate B to remove regular-file writes.

## Acceptance criteria

Gate B may ship only when all of the following are true:

- the extension continuation initializes one sector, not every sector;
- new child directories have correct `.`/`..` and a valid marker before mkdir
  completion;
- later appended clusters expose a valid entry-0 marker and no duplicate dots;
- untouched later sectors remain invisible across scan and remount;
- every exposed later sector is completely cleared first;
- same-cluster, fragmented-chain, allocated-EOF, FAT16-root, and legacy
  no-terminator fixtures pass;
- create/rename/remove/delete semantics remain correct;
- every product load/save family passes after reboot;
- Bank payload counts, sizes, and hashes are semantically unchanged;
- host filesystem checking reports no new error;
- stopped-playback Bank Save shows a material repeatable improvement or the
  measured result is explicitly judged sufficient despite card variance;
- the future-size pattern fixture behaves correctly;
- the final sync remains the UI completion boundary;
- retained AsyncFATFS SRAM remains exactly unchanged; and
- source, header, reference, SRAM manifest, MEMORY, and completion records agree
  on what shipped.

## Reversion boundary

If a compatibility or media failure is unique to Gate B:

1. restore the phase name and full-cluster loop, including its cursor rewind;
2. restore the cluster/sector accessor if the loop again needs it;
3. restore source/header/reference wording to describe full-cluster
   initialization;
4. retain all proven Gate A reservation, target preparation, persistence, and
   terminator-tail ordering; and
5. record the exact failing media/layout/interruption case.

Do not revert Phase One merely because Phase Two fails. Do not mask a Gate B
failure with a product-specific retry, larger cache, extra polling, or a RAM
initialization witness.

## Explicitly deferred beyond Phase Two

- SFN-only storage for currently LFN-created 8.3-compatible display names;
- retained or short-lived directory-position lookup hints;
- polling cadence, SD burst size, SPI, DMA, or cache-size changes;
- Bank-specific in-place rewrite or transaction witnesses;
- pack files, compression, whole-file staging, or allocation-unit changes;
- Bank Load changes or product-format/schema changes; and
- new public move/copy/replace/unlink APIs.

Profile after Gate B before selecting any of these. The next plan must identify
whether remaining time is deletion, FAT allocation, directory lookup, payload
writing, metadata, or final flush rather than assuming another cause.

## Implementation notes / verification record

- 2026-08-30: Phase Two work started from commit `0c90434` on a clean
  `dev-ph3-autosave-ph5` worktree. The baseline logging-on build completed with
  `text=385,580`, `data=404`, `bss=96,176`, `.text=372,648`, `.bss=89,504`,
  and `build/lxr02.bin=385,984` bytes. The linked `afatfs` symbol was
  `6,984` bytes (`0x1b48`); the source assertions target
  `afatfsCreateFile_t=144`, `afatfsFile_t=188`, and
  `afatfsRenameObject_t=552`. No hardware/media fixture or Bank timing result
  is available yet.
- 2026-08-30: Implemented the scheduled Gate B source scope. Changed symbols
  are `afatfsExtendSubdirectoryPhase_e`,
  `afatfsExtendSubdirectory_t`'s adjacent retained-state contract,
  `afatfs_appendRegularFreeClusterContinue()`'s allocation handoff comment,
  `afatfs_extendSubdirectoryContinue()`,
  `afatfs_extendSubdirectory()`'s contract comment,
  `afatfs_prepareDirectoryRunTarget()`'s contract comment,
  `afatfs_handoffCreatedDirectoryToInitializer()`'s contract comment, and the
  no-terminator create comment. The obsolete
  `afatfs_fileGetCursorClusterAndSector()` was deleted. The public
  `asyncfatfs.h` directory contract and the AsyncFATFS reference now describe
  the final Gate A+B marker/initialization model; no declaration changed.
- 2026-08-30: Clean `make` and `make img` pass. The final logging-on link
  reports `text=385,420`, `data=404`, `bss=96,176`, `.text=372,488`,
  `.bss=89,504`, `build/lxr02.bin=385,824`, and
  `build/LXRV2_lxr02.img=385,840` bytes. The three retained-state assertions
  pass at 144/188/552 bytes and `afatfs` remains 6,984 bytes, so the measured
  retained-RAM delta is zero. `git diff --check` and forbidden-token/source
  review pass; only the pre-existing compiler/linker warnings remain.
- 2026-08-30: No physical card or disposable FAT image was available. The
  required first-child, later-appended, one-sector, fragmented-chain,
  allocated-EOF, FAT16 fixed-root, legacy no-terminator, stale-post-marker,
  interruption, reboot/remount, product compatibility, host-checker, and
  two-trial Bank timing fixtures were not run. The approximately 20 KiB per
  Scene future-pattern fixture was also not run, so there is no measured card
  format/cluster size, playback-stopped/running time, payload/hash result, or
  filesystem-check result to report.
- 2026-08-30: Final Gate B decision for this workspace: source/build complete
  and implementation retained in the working tree; hardware/media acceptance
  is pending. No projected sector or timing reduction is recorded as a
  measurement, and no claim is made that the full acceptance criteria have
  passed.
- 2026-08-31: The user deliberately deferred Gate B hardware/media testing.
  Source review and the forced ARM build found no expected problem; hardware
  acceptance is not claimed.
