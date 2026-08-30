# Session 059 AsyncFATFS speedup — Phase One implementation schedule

Date: 2026-08-30

Status: implementation complete; static verification complete; media/product
acceptance pending. Functional source changes remain confined to
`Core/Hardware/SD/asyncfatfs/asyncfatfs.c`; the public header change is
comment-only and behaviorally unchanged.

## Decision

Phase One is **Gate A only** from `S059_ASYNCFATFS_SPEEDUP.md`:

- make short create, LFN create, and same-parent rename stop their collision
  scan at the first FAT `0x00` directory terminator;
- keep scanning through `0xE5` deleted holes so a later live display-name or
  SFN-alias collision is never missed;
- reserve one sector-local object-entry run and preserve, replace, or safely
  move the terminator;
- initialize and persist a next logical target sector before retiring an old
  terminator tail that would expose it; and
- keep the current full-cluster zero-fill in
  `afatfs_extendSubdirectoryContinue()` unchanged.

This is the smallest complete implementation boundary. It fixes every current
directory-entry writer together, retains the old whole-cluster initialization
safety net, and has a visible performance result in full Bank Save. It does not
leave short create or rename on the old unsafe allocation rule while changing
only the common LFN path.

Gate B (lazy first-sector-only directory initialization) is deliberately
deferred. It should be a later, isolated patch after this phase passes on-card,
reboot, FAT16/FAT32, and host-checker testing. Phase One must not partially
implement Gate B or alter the current `.` / `..` initialization loop.

## Observable result

On a successful Phase One build:

1. The first create in a fresh directory leaves a real `0x00` entry directly
   after the new LFN/SFN run instead of retiring every zero entry through the
   physical end of the allocated cluster.
2. Later creates stop at that marker, so they no longer scan all 64 sectors of
   a 32 KiB directory cluster.
3. A run that does not fit before the end of the current sector appears at
   entry 0 of the next **logical** sector, with a replacement marker after it.
4. The target sector is cleared and reaches the card before the old marker tail
   is retired, so stale directory-shaped bytes cannot become visible.
5. A fresh full Bank Save should show a material, repeatable wall-time
   improvement on the same card and content. The 2,048 whole-cluster directory
   initialization writes remain in Phase One; the additional terminator-
   retirement writes and repeated physical-end scans do not.

The proposal's 8–15 second estimate is not an acceptance promise. Record only
measured results after implementation.

## Audited baseline

The planning audit used the clean `dev-ph3-autosave-ph4` worktree at commit
`124a6cf`. The existing linked image reports:

```text
text=380,436  data=408  bss=94,848
linked afatfs symbol=6,984 bytes
sizeof(afatfsCreateFile_t)=144
sizeof(afatfsFile_t)=188
sizeof(afatfsRenameObject_t)=552
sizeof(afatfs_t)=6,984
```

The `sizeof` values were measured with a target-ABI debug compile of the current
translation unit; the linked symbol came from `build/lxr02.elf`. Rebuild and
remeasure before and after Phase One. Do not substitute the stale 7,344-byte
`afatfs` figure still present in the older SRAM manifest table.

Phase One is designed for **zero retained-state growth**. It replaces existing
fields rather than appending convenient diagnostics or a RAM-only initialized-
sector witness. If the implementation cannot keep the three structure sizes
above unchanged, stop before accepting the layout, identify the exact normal-
SRAM1 byte delta and multiplication through five open handles plus
`currentDirectory`, and obtain the required RAM approval.

## Contracts that remain binding

- `afatfs_poll()` remains foreground-owned, single-context, non-reentrant, and
  asynchronous.
- No public API, callback, result code, signature, path semantics, match mode,
  filename sanitization rule, LFN shape/checksum rule, SFN case-bit behavior,
  handle count, or final `afatfs_sync()` boundary changes.
- The first `0x00` ends the live namespace. A matching live object cannot exist
  after it.
- `0xE5` is reusable but does not end collision scanning.
- A deleted run is only a candidate until all live entries before the first
  terminator have been checked. This preserves the Session 056 duplicate fix.
- Every new LFN/SFN run remains sector-local. A short SFN is treated as a
  one-entry run under the same reservation rule.
- Reusing a deleted run does not touch the following entry or move the
  terminator.
- Consuming a terminator requires one complete zero entry immediately after
  the new SFN.
- A run plus replacement marker that does not fit in the remaining sector tail
  moves in full to entry 0 of the next logical sector.
- Logical movement uses `currentDirectory`'s cursor and FAT traversal. No
  `physicalSector + 1` calculation is permitted across a cluster boundary.
- A FAT16 fixed root directory never grows. If it has neither a sufficient
  deleted run nor a usable terminator tail, creation/rename fails normally.
- A legacy directory with no terminator remains supported: use a proven
  deleted run after full collision scanning, or extend if the directory type
  permits it.
- Rename still writes the new name before retiring the old complete name run.
  It does not become a power-loss transaction.
- Newly created directories still allocate and zero-fill their complete first
  cluster and create `.` / `..` before a successful mkdir callback.

## Exact private state model

The implementation should replace the current LFN-specific free-run fields in
`afatfsCreateFile_t`; it must not add another reservation object to
`afatfsRenameObject_t`, which already embeds `newNameState`.

Use the existing byte and pointer budget as follows (equivalent names are
acceptable only if the lifetimes remain this explicit):

| Proposed state | Existing storage to reuse | Meaning |
| --- | --- | --- |
| `requestedEntryCount` | replace `longNameEnabled` | `1` for a short SFN; `lfnEntryCount + 1` for an LFN/SFN run. `lfnEntryCount != 0` becomes the private LFN-path discriminator. |
| `deletedRunLength` | rename `freeRunLength` | Length of the current sector-local `0xE5` run while scanning. |
| `reservationOrigin` | replace `freeRunLatched` | `NONE`, `DELETED`, `TERMINATOR_LOCAL`, or `TERMINATOR_MOVED`. |
| `scanRunStart` | rename `freeRunStart` | Start of the current deleted scan run; after selecting `TERMINATOR_MOVED`, it is deliberately reused as the original terminator pointer. |
| `selectedRunStart` | rename `latchedFreeRunStart` | First latched deleted candidate or final local/moved target run. |
| create/rename phase | existing phase fields | Proves whether next-sector seek, extension/initialization, target write, target persistence wait, or old-tail retirement is pending. No separate target-ready byte is needed. |

This state is sufficient because the current deleted-run start is dead once a
terminator decision is made. A moved-terminator operation can therefore reuse
that pointer for the old marker while `selectedRunStart` holds the prepared
target. The phase proves whether the target has been initialized and written.

Recommended origin enum:

```c
typedef enum {
    AFATFS_DIRECTORY_RUN_ORIGIN_NONE = 0,
    AFATFS_DIRECTORY_RUN_ORIGIN_DELETED,
    AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_LOCAL,
    AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED,
} afatfsDirectoryRunOrigin_e;
```

`TERMINATOR_LOCAL` also covers entry 0 of a just-appended, fully zeroed cluster
when a no-terminator directory reached physical exhaustion; there is then no
old tail to retire. `TERMINATOR_MOVED` alone requires the target-media barrier
and old-tail retirement.

## Production source-change schedule

All functional changes in Phase One belong in
`Core/Hardware/SD/asyncfatfs/asyncfatfs.c`.

### 1. Replace create phases and free-run fields with the shared reservation state

Location:

- `AFATFS_CREATEFILE_PHASE_*` enum near the top of `asyncfatfs.c`;
- `afatfsCreateFile_t`;
- `afatfsRenameObjectPhase_e`; and
- the size checks immediately following the completed private type definitions.

Required change:

- Add resumable create phases for seek-next-logical-sector, prepare/extend
  target sector, write short/LFN run, wait for moved-target persistence, and
  retire the old terminator tail.
- Add corresponding rename phases between collision scan and old-name-run
  retirement.
- Replace, do not append to, the fields described in the private state model.
- Keep `requestedEntryCount`, `deletedRunLength`, and `reservationOrigin` byte-
  sized. Keep both directory pointers naturally aligned.
- Add target-build `_Static_assert` checks for the 144-byte create state,
  188-byte file handle, and 552-byte rename state, or perform equivalent
  compile-time enforcement adjacent to the definitions.
- Do not add state to `afatfs_t`, the five-handle array, or
  `currentDirectory`.

Documentation-in-place block to put beside the reservation fields:

```c
/*
 * Shared directory-entry run reservation for create and rename.
 *
 * What: Persists one sector-local candidate or selected FAT directory-entry
 * run across asynchronous polls. requestedEntryCount is one for an SFN and
 * lfnEntryCount plus one for an LFN/SFN run. reservationOrigin records whether
 * selectedRunStart is a proven deleted run, the current terminator, or entry
 * zero of a prepared next logical sector. scanRunStart tracks the current
 * deleted run while scanning and is reused for the old terminator only after a
 * moved-marker decision makes the scan run dead.
 *
 * Why: Deleted entries and the end marker are not interchangeable. Every
 * writer must know whether it may overwrite only selected slots or must also
 * publish a replacement zero entry. Keeping this state in afatfsCreateFile_t
 * makes short create, LFN create, and rename share one rule without adding a
 * RAM-only sector-initialization witness or a second rename allocator.
 *
 * Inputs: requested object-entry count, raw finder position, entry class,
 * create permission, and the 16-entry directory-sector boundary.
 *
 * Outputs/effects: either a latched deleted run that leaves the terminator
 * untouched, a local terminator-owned run with room for a replacement marker,
 * or resumable state for preparing a next logical sector and later retiring
 * the old terminator tail.
 *
 * Affiliates: afatfs_createFileContinue(), the short and LFN writers,
 * afatfs_renameObjectContinue(), afatfs_extendSubdirectory(),
 * afatfs_findNext(), and AFATFS_FILES_PER_DIRECTORY_SECTOR.
 */
```

Documentation-in-place block to put beside the layout assertions:

```c
/*
 * Retained-state ceiling for the terminator-aware reservation refactor.
 *
 * What: Verifies the target ABI still gives the create state, each file
 * handle, and the global rename state their accepted pre-Phase-One sizes.
 * Why: afatfsCreateFile_t is the largest per-handle operation-union member, so
 * casual growth multiplies through five open handles and currentDirectory and
 * also enlarges rename's embedded newNameState. Inputs are the compiler's
 * completed private type layouts. Output is a build failure instead of an
 * unapproved normal-SRAM1 increase. Affiliates are AFATFS_MAX_OPEN_FILES,
 * afatfsFileOperation_t, afatfs_t, and SRAM_MANIFEST.md.
 */
```

### 2. Replace the old LFN-only free-run helpers with shared classification and selection helpers

Location:

- replace `afatfs_noteFreeDirectoryEntry()`;
- replace `afatfs_freeRunIsReady()`;
- remove the current single-entry `afatfs_retireDirectoryTerminator()`; and
- add the new helpers immediately before the directory-entry writers.

Required change:

1. `afatfs_resetDirectoryRunReservation()` clears the deleted-run length,
   origin, and both pointers without disturbing the requested count, alias,
   names, callback, or LFN scanner.
2. `afatfs_noteDeletedDirectoryEntry()` tracks only `0xE5` entries, resets at a
   live entry or sector boundary, and latches only the first sector-local run
   whose length reaches `requestedEntryCount`.
3. Once a deleted candidate is latched, scanning continues; later holes never
   replace the first candidate.
4. `afatfs_selectDirectoryRunAtTerminator()` first prefers a latched deleted
   run. Otherwise, a local terminator run is legal only when
   `entryIndex + requestedEntryCount < 16`, leaving one complete entry for the
   replacement marker.
5. If the local tail is too short, save the marker pointer, select
   `TERMINATOR_MOVED`, and request next-sector preparation without modifying
   the marker or any skipped tail entry.
6. Do not merge an insufficient deleted suffix before the terminator into the
   object run. The Phase One rule is deliberately simple: use a complete
   deleted candidate, otherwise start at the terminator, otherwise move the
   complete run to the next sector.
7. Observation and selection helpers must never dirty a cache sector.

Documentation-in-place block for the reset/observe helpers:

```c
/*
 * Observe deleted runs without ending collision scanning.
 *
 * What: Tracks sector-local 0xE5 runs and latches the first run large enough
 * for the requested SFN or LFN/SFN entry count. Reset clears only reservation
 * progress, while a live entry breaks the current run without discarding an
 * already-latched candidate.
 *
 * Why: A deleted hole is reusable but does not prove absence; a matching live
 * display name or alias may still occur before the first 0x00 marker. This is
 * the shared form of the Session 056 latch-and-continue rule.
 *
 * Inputs: afatfsCreateFile_t reservation state, raw finder sector/index, the
 * requested entry count, and the caller's deleted-versus-live classification.
 *
 * Outputs/effects: deletedRunLength/scanRunStart progress and, at most once,
 * selectedRunStart with DELETED origin. No directory byte or cache state is
 * modified.
 *
 * Affiliates: afatfs_createFileContinue(), rename collision scan,
 * fat_isDirectoryEntryEmpty(), alias-restart reset, and the short/LFN writers.
 */
```

Documentation-in-place block for terminator selection:

```c
/*
 * Finish collision scanning and select a run at the first FAT terminator.
 *
 * What: Treats 0x00 as the end of the live namespace. It chooses an earlier
 * proven deleted run when one exists, otherwise reserves at the terminator if
 * the object run plus one replacement zero entry fits, or records that the
 * complete run must move to the next logical sector.
 *
 * Why: No valid collision exists after 0x00, and retiring that marker while
 * merely searching destroys the only persistent boundary hiding later stale
 * bytes. Selection must therefore be explicit and mutation-free.
 *
 * Inputs: reservation state, terminator finder pointer, requested entry count,
 * and AFATFS_FILES_PER_DIRECTORY_SECTOR.
 *
 * Outputs/effects: selected origin/start or the saved old-marker pointer for a
 * moved run. It performs no seek, extension, zeroing, or cache dirtying.
 *
 * Affiliates: create/open scan, rename collision scan, next-sector preparation,
 * replacement-marker writers, and old-tail retirement.
 */
```

### 3. Add next-logical-sector preparation, target persistence, and old-tail retirement helpers

Location:

- beside the shared reservation helpers;
- use `afatfs_findCacheSector()` and existing cache state privately; and
- call through the create and rename phase machines, never from product code.

Required change and exact ordering:

1. At a `TERMINATOR_MOVED` decision, call `afatfs_findLast()` first so the raw
   scan releases its retained cache sector.
2. In a dedicated phase, advance `currentDirectory` exactly one 512-byte
   logical sector using `afatfs_fseekAtomic()`. Retry asynchronously if FAT
   chain lookup is pending.
3. If the cursor is now beyond allocated directory storage, call
   `afatfs_extendSubdirectory(&afatfs.currentDirectory, NULL, NULL)`. Wait for
   its existing full-cluster initialization to finish. A FAT16 fixed root
   returns failure; do not special-case it into allocation.
4. Cache the target cursor sector with write ownership and no dependency on
   its old content, zero all 512 bytes, set `selectedRunStart` to entry 0, and
   leave origin as `TERMINATOR_MOVED`.
5. Write the complete object run and its replacement marker into that dirty
   target.
6. Enter a target-persistence phase. Because the old terminator sector may
   already have an older dirty timestamp from earlier directory creates,
   dirty-order alone is not a sufficient proof. Let the normal poll/flush path
   drain until the target descriptor is `IN_SYNC` or has been evicted after
   reaching sync. Do not retire the old tail earlier.
7. Only after that proof, cache the original marker sector for read/write, set
   `filename[0] = 0xE5` from the original marker through entry 15, and mark the
   sector dirty. This converts every formerly hidden tail entry, not just the
   marker, so stale bytes between the old and new marker cannot become live.
8. A physical-end extension with no prior terminator initializes/selects entry
   0 of the appended cluster but has no old tail and skips steps 6–7.

The target-persistence helper must not allocate retained state. It derives its
answer from `selectedRunStart.sectorNumberPhysical` plus the existing cache
descriptor. A descriptor can be evicted only after it is in sync, so absence
after the prepared/write phase is a valid persisted result. DIRTY or WRITING is
not. An impossible EMPTY/READING state for the prepared target should assert or
fail the operation rather than being called persisted.

Documentation-in-place block for next-sector preparation:

```c
/*
 * Prepare a next logical directory sector before moving the end marker.
 *
 * What: Advances currentDirectory through its cursor/FAT chain, extends an
 * extendable directory when the cursor reaches allocated EOF, obtains the
 * resulting sector with write ownership, clears all 512 bytes, and selects
 * entry zero for the object run.
 *
 * Why: Sectors after 0x00 may contain arbitrary stale card data and FAT cluster
 * chains need not be physically contiguous. The target must be initialized
 * through logical traversal before any old marker can expose it.
 *
 * Inputs: currentDirectory cursor and allocation state, the saved old
 * terminator pointer, requested entry count, and the caller's resumable phase.
 *
 * Outputs/effects: a dirty zeroed target sector, selectedRunStart at entry
 * zero, unchanged public APIs, or normal asynchronous/failure status from FAT
 * seek, cache, or directory extension.
 *
 * Affiliates: afatfs_fseekAtomic(), afatfs_extendSubdirectory(),
 * afatfs_cacheSector(AFATFS_CACHE_WRITE), create/rename phases, FAT16 fixed
 * root handling, and the full-cluster Gate-A initializer.
 */
```

Documentation-in-place block for the media barrier:

```c
/*
 * Prove a moved-run target has reached media before exposing it.
 *
 * What: Checks the existing cache descriptor for the selected target sector.
 * IN_SYNC, or absence after the sector was prepared and written, means the
 * target run and replacement marker have completed their SD write. DIRTY or
 * WRITING means the caller must keep yielding while the normal oldest-first
 * flush path progresses.
 *
 * Why: The old terminator sector can already be dirty with an earlier
 * timestamp. Merely dirtying the target first within this operation would not
 * prevent that older sector from flushing first after the terminator tail is
 * changed. This barrier makes the exposure order independent of prior cache
 * history.
 *
 * Inputs: selected target physical sector and existing cache descriptor state.
 * Outputs/effects: a read-only ready/not-ready decision; no polling, I/O,
 * allocation, timestamp change, or cache ownership change.
 *
 * Affiliates: afatfs_poll(), afatfs_flush(), afatfs_findCacheSector(), moved
 * create/rename phases, and old-tail retirement.
 */
```

Documentation-in-place block for old-tail retirement:

```c
/*
 * Retire the old terminator tail only after the new boundary is durable.
 *
 * What: Marks every entry from the saved old 0x00 position through the end of
 * that sector as deleted after the prepared next-sector run and replacement
 * marker have reached media.
 *
 * Why: Changing only the old marker could expose stale nonzero entries in the
 * skipped tail; changing the tail before the target is durable could expose a
 * stale or partial next sector after reboot.
 *
 * Inputs: saved old terminator sector/index and the completed target-media
 * barrier. Outputs/effects: one read/modify/write cache sector dirtied with
 * 0xE5 first filename bytes; no live entry before the old marker is touched.
 *
 * Affiliates: target-sector preparation/persistence, create completion,
 * rename's write-new-before-retire-old ordering, and final afatfs_sync().
 */
```

### 4. Rewrite `afatfs_createFileContinue()` scan decisions

Location:

- `AFATFS_CREATEFILE_PHASE_INITIAL`;
- `AFATFS_CREATEFILE_PHASE_FIND_FILE` entry-class branches;
- alias-collision restart;
- physical-exhaustion handling; and
- the new prepare/barrier/retire phases.

Required change:

- Initialize `requestedEntryCount` once (`1` short, `lfnEntryCount + 1` LFN)
  and reset reservation state at the initial scan.
- Treat `lfnEntryCount != 0` as the LFN path discriminator after removing
  `longNameEnabled`.
- For both short and LFN create-capable modes, note deleted entries but continue
  scanning all live entries.
- On a first `0x00`:
  - release the finder;
  - open an already-matching object as today if one was found earlier;
  - fail open-only immediately when no display match was found;
  - for create mode, choose a latched deleted run, a legal local terminator
    run, or the moved-sector phases; and
  - never call the old allocator rescan.
- For LFN open-only mode, an SFN alias collision with a different display name
  is not proof that the requested display component is absent; keep scanning
  live entries to the terminator. Alias generation is needed only if creation
  is allowed.
- For create-capable LFN alias collision, increment `aliasOrdinal`, regenerate
  the deterministic `~N` candidate, seek to directory start, and reset all
  reservation and LFN-scan state before the new full live scan.
- At physical exhaustion with no terminator, use a latched deleted run. If none
  exists, extend an extendable directory, initialize/select entry 0 of its
  already fully zeroed new cluster, and write a replacement marker; fail a full
  FAT16 root normally.
- Make every success/failure/alias-retry path release finder retention exactly
  once and leave no stale reservation state for a recycled handle.

Documentation-in-place block for the scan branch:

```c
/*
 * Terminator-aware create/open collision scan.
 *
 * What: Scans live SFN/LFN objects and deleted holes only until the first
 * 0x00. Deleted runs are latched as candidates while matching continues. At
 * the marker, open-only reports absence and create mode receives a fully
 * described deleted-, local-terminator-, or moved-terminator run.
 *
 * Why: FAT guarantees that no live object follows 0x00. Continuing to physical
 * exhaustion wastes I/O and destroys the persistent boundary, while creating
 * immediately in a deleted hole can duplicate a matching object later in the
 * live prefix.
 *
 * Inputs: file mode, requested object type, display/match mode, SFN alias
 * candidate, LFN scanner/checksum state, raw entry class, and shared
 * reservation state.
 *
 * Outputs/effects: open existing, reject type/name collision, report not
 * found, restart a create-capable alias scan, select a safe run, prepare or
 * extend a target sector, or fail normally. Observation does not mutate the
 * directory.
 *
 * Affiliates: afatfs_generateShortAlias(), afatfs_lfnScanAppend(),
 * afatfs_findFirst/findNext/findLast(), shared reservation helpers, the short
 * and LFN writers, and the directory-initialization handoff.
 */
```

Documentation-in-place block for alias restart:

```c
/*
 * Restart a create-capable LFN alias collision scan from clean state.
 *
 * What: Advances the deterministic ~N ordinal, regenerates the SFN candidate,
 * seeks to entry zero, and clears deleted-run, terminator, selected-target,
 * and in-progress LFN reconstruction state before rescanning.
 * Why: Candidate positions belong to one complete collision scan and cannot
 * survive an alias change; retaining one could overwrite a live object or use
 * a marker selected under the wrong alias. Inputs are the next alias ordinal
 * and unchanged sanitized display name. Outputs are a new SFN candidate and a
 * fresh scan. Affiliates: afatfs_generateShortAlias(), Session 056 duplicate
 * prevention, create permissions, and rename's equivalent restart.
 */
```

### 5. Replace `afatfs_allocateDirectoryEntry()` with a selected-run short writer

Location:

- remove or reduce `afatfs_allocateDirectoryEntry()`; and
- replace the `AFATFS_CREATEFILE_PHASE_CREATE_NEW_FILE` branch.

Required change:

- Do not rescan the directory.
- Cache `selectedRunStart.sectorNumberPhysical` for read/write.
- Validate the selected entry index and one-entry sector bound.
- Write the existing SFN fields unchanged: uppercase FAT key, attributes,
  `ntReserved`, default dates/times, and zero cluster/size fields.
- If origin is `DELETED`, write only the selected SFN.
- If origin is `TERMINATOR_LOCAL` or `TERMINATOR_MOVED`, zero the complete
  immediately following directory entry as the replacement marker.
- Publish `file->directoryEntryPos` at the SFN as today.
- For moved origin, go to the target-persistence and old-tail phases. For local
  or deleted origin, continue to regular-file success or the existing mkdir
  initializer handoff.
- Preserve all callback and failure cleanup behavior.

Documentation-in-place block:

```c
/*
 * Write one SFN into a previously selected directory-entry run.
 *
 * What: Initializes the selected short entry with the existing FAT name,
 * attributes, case bits, and timestamps. A terminator-owned selection also
 * clears the complete following entry; a deleted selection touches no byte
 * after its SFN.
 *
 * Why: Short writers such as settings.tmp cannot rely on zero-filled bytes
 * after the chosen slot, and a second allocator scan would lose whether the
 * selected entry consumed the live namespace marker.
 *
 * Inputs: reservation origin/start, requested attributes/type, raw FAT
 * filename, ntReserved case bits, and create callback state.
 *
 * Outputs/effects: dirty selected sector, file->directoryEntryPos at the SFN,
 * optional replacement 0x00 entry, and unchanged regular-file or mkdir
 * completion flow.
 *
 * Affiliates: afatfs_fopen(), afatfs_mkdir(), settings safe-write,
 * afatfs_handoffCreatedDirectoryToInitializer(), moved-target barrier, and
 * close/truncate metadata publication.
 */
```

### 6. Make `afatfs_createLongDirectoryEntries()` honor reservation origin

Location: `afatfs_createLongDirectoryEntries()`.

Required change:

- Read the run from `selectedRunStart`, not the obsolete free-run field.
- Validate `start + requestedEntryCount <= 16` for every origin.
- For terminator-owned origins, additionally validate
  `start + requestedEntryCount < 16` and zero the complete entry immediately
  after the SFN.
- For a deleted origin, do not modify the following entry.
- Keep LFN ordinal order, fragment text, ASCII handling, checksum, generated
  SFN, attributes, case flags, timestamps, and `file->directoryEntryPos`
  unchanged.
- After a moved write, enter the target-persistence/old-tail path. Otherwise
  finish through the existing file/mkdir path.

Documentation-in-place block:

```c
/*
 * Write a complete VFAT LFN/SFN run under the selected-marker contract.
 *
 * What: Emits the existing highest-to-lowest LFN fragments followed by their
 * SFN in one sector. When the reservation consumed a terminator it also writes
 * one complete zero entry after the SFN; when it reused deleted entries it
 * leaves the following entry untouched.
 *
 * Why: The replacement marker is what keeps stale later bytes outside the live
 * namespace, but clearing after a deleted run could erase the first entry of a
 * valid object that follows the hole.
 *
 * Inputs: selected run/origin, requested and LFN entry counts, sanitized long
 * name, generated FAT filename/checksum, case bits, attributes, and timestamps.
 *
 * Outputs/effects: one valid dirty sector-local VFAT run, optional replacement
 * marker, and file->directoryEntryPos pointing at the owning SFN.
 *
 * Affiliates: LFN fragment writer/checksum, create state machine, mkdir
 * initializer handoff, object iterator, remove/delete identity, rename writer,
 * and moved-target persistence.
 */
```

### 7. Initialize and clear reservation state at every create lifecycle boundary

Location:

- `afatfs_createFileInternal()`;
- `AFATFS_CREATEFILE_PHASE_INITIAL`;
- create alias restart;
- extension entry/return;
- success/failure; and
- `afatfs_handoffCreatedDirectoryToInitializer()`.

Required change:

- Set `requestedEntryCount = 1` for short APIs.
- For LFN APIs, sanitize/generate exactly as today, set `lfnEntryCount`, then
  set `requestedEntryCount = lfnEntryCount + 1`.
- Keep `lfnEntryCount != 0` as the LFN discriminator; do not optimize
  8.3-compatible LFN API calls into SFN-only storage.
- Reset all reservation fields at initial scan and alias restart.
- Clear reservation fields before terminal callback/failure or before the
  create operation union is handed to directory extension.
- Never clear callback/name/alias fields while they are still needed by a
  pending phase.
- Retain `afatfs_initFileHandle()`'s whole-handle zeroing; explicit lifecycle
  resets are still required so retries and embedded rename state do not depend
  on that incidental initialization.

Documentation-in-place block:

```c
/*
 * Initialize one create request's directory-run ownership.
 *
 * What: Derives the complete on-disk object-entry count, clears every
 * candidate/selected/terminator pointer, and establishes whether this request
 * writes an SFN-only or deliberate LFN/SFN representation.
 * Why: A recycled handle is zeroed at allocation, but alias restarts,
 * asynchronous extension, mkdir handoff, and rename's embedded state all need
 * explicit self-contained lifecycle boundaries. Inputs are createLongName and
 * the validated LFN fragment count. Outputs are a clean reservation with no
 * directory mutation. Affiliates: afatfs_initFileHandle(), create scan,
 * alias generation, success/failure callbacks, and rename preparation.
 */
```

### 8. Convert rename collision scan, placement, writer, and retirement ordering

Location:

- `afatfs_renameObjectRestartCollisionScan()`;
- `AFATFS_RENAME_OBJECT_PHASE_PREPARE_NEW_NAME`;
- `AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN`;
- replace the old `WAIT_EXTEND` behavior;
- `afatfs_renameObjectChooseRun()`;
- `afatfs_renameObjectWriteRun()`; and
- phases before `AFATFS_RENAME_OBJECT_PHASE_RETIRE_OLD_RUN`.

Required change:

- Initialize `newNameState.requestedEntryCount` after calculating
  `lfnEntryCount`.
- On every collision scan begin and every alias restart, reset all shared
  reservation fields plus LFN reconstruction state.
- Continue through deleted holes and live objects; exclude the source SFN from
  display/alias collision checks exactly as today.
- Stop at the first terminator. Placement priority is:
  1. validated in-place rewrite when the new run fits the old sector-local run;
  2. first proven deleted candidate;
  3. local terminator-owned run with a replacement slot;
  4. moved run in a prepared next logical sector.
- On physical exhaustion without a terminator, use in-place or deleted
  placement; otherwise extend and select entry 0 of the appended, fully zeroed
  cluster without restarting a scan from directory entry zero.
- A create-capable SFN alias collision increments `aliasOrdinal`, regenerates
  the alias, and restarts the complete live collision scan with clean
  reservation state.
- `afatfs_renameObjectChooseRun()` must consume the selected shared state
  rather than the final transient `freeRunStart`.
- In-place writer behavior remains unchanged, including deletion of unused old
  LFN prefix entries when the new run is shorter.
- A moved/deleted/local new run preserves the copied source SFN's cluster,
  size, attributes, and timestamps. Terminator-owned runs write a replacement
  marker; deleted runs do not touch the following entry.
- For `TERMINATOR_MOVED`, wait for target persistence, retire the old marker
  tail, and only then retire the old complete LFN/SFN source run.
- Preserve structured results and publish `openNameOut` only on
  `AFATFS_RESULT_OK`.

Documentation-in-place block for collision scan/placement:

```c
/*
 * Terminator-aware rename collision scan and run selection.
 *
 * What: Scans every live object before the first 0x00 while excluding the
 * validated source entry, latches deleted capacity, and selects in-place,
 * deleted, local-terminator, or moved-terminator placement in that order.
 *
 * Why: Rename is a real writer used by settings promotion and canonical-name
 * repair. Leaving it on physical-exhaustion allocation would let it scan stale
 * post-marker bytes or consume a marker without publishing a replacement.
 *
 * Inputs: copied source identity/SFN, requested new display name and match
 * mode, generated alias, raw finder/LFN state, old sector-local run, and shared
 * reservation state.
 *
 * Outputs/effects: collision result, clean alias restart, selected new-name
 * run, asynchronous next-sector preparation/extension, or normal failure. No
 * source entry is retired during scanning.
 *
 * Affiliates: afatfs_renameObjectCanRewriteInPlace(),
 * afatfs_generateShortAlias(), shared reservation helpers, settings.tmp
 * promotion, and Kit/Scene/Bank canonical repair.
 */
```

Documentation-in-place block for rename writer/order:

```c
/*
 * Publish the new rename run before retiring any old namespace boundary.
 *
 * What: Writes the copied source SFN under its new LFN/SFN name, adds a
 * replacement marker only for terminator-owned placement, waits for a moved
 * target to reach media, retires the old marker tail when present, and finally
 * retires the source object's complete old name run.
 *
 * Why: The object cluster, size, attributes, timestamps, and children must
 * survive rename unchanged, and neither stale next-sector bytes nor a partial
 * new LFN chain may be exposed by moving 0x00 too early.
 *
 * Inputs: validated source object/run, copied source SFN, selected run/origin,
 * generated new LFN/SFN data, target persistence state, and old-name retirement
 * cursor.
 *
 * Outputs/effects: valid new name and marker, then deleted old name entries;
 * structured OK/error result and alias publication only after completion.
 * Power loss may still leave both old and new names, matching the existing
 * non-transactional rename guarantee.
 *
 * Affiliates: afatfs_renameObjectWriteRun(), moved-target helpers,
 * afatfs_retireObjectNameRun(), afatfs_renameObjectFinish(), settings safe
 * write, and final afatfs_sync().
 */
```

### 9. Preserve full-cluster initialization unchanged and correct nearby comments only if they become false

Location:

- `afatfs_extendSubdirectoryContinue()`; and
- `afatfs_extendSubdirectory()`.

Required action:

- Do **not** change `AFATFS_EXTEND_SUBDIRECTORY_PHASE_WRITE_SECTORS`, its loop,
  its seek-back, or its `.` / `..` logic in Phase One.
- It must still zero every sector of every appended directory cluster.
- The current source/header statement that a new directory cluster is fully
  zero-filled remains true and should remain in place.
- Only adjust an affiliate comment if new next-sector preparation makes an old
  statement about who calls extension incomplete. No behavior change belongs
  here.

Documentation-in-place block to use only if the affiliate comment needs an
update:

```c
/*
 * Gate-A directory extension remains whole-cluster initialized.
 *
 * What: Appends one cluster, clears every sector, writes dot entries only for
 * a new child directory's first cluster, and returns the cursor to the new
 * cluster start. Why: Phase One changes terminator reservation but deliberately
 * retains the existing initialization safety net; lazy later-sector clearing
 * is Gate B. Inputs/outputs and callback timing are unchanged. Affiliates now
 * include physical-end create/rename placement and moved-terminator target
 * preparation in addition to mkdir initialization.
 */
```

## Public header disposition

`Core/Hardware/SD/asyncfatfs/asyncfatfs.h` requires **no Phase One edit**.

The existing public guarantee—that a newly-created directory's full first
cluster is zero-filled and is safe to `chdir()` on callback—remains exactly
true. No caller is exposed to run reservation, terminator movement, or the
internal persistence barrier. The header update described in the broader
S059 proposal belongs with Gate B, when full-cluster zero-fill actually stops
being true.

If implementation changes any public declaration or requires a new caller
responsibility, Phase One has exceeded its boundary and this schedule must be
amended before code is accepted.

## Documentation changes after the code passes

### 10. `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`

Update only the implemented Gate A model:

- In “Directory Terminators And LFN Creation,” replace the current description
  of retiring `0x00` and scanning to physical exhaustion with:
  - first-marker collision termination;
  - deleted-run latch-and-continue behavior for short create, LFN create, and
    rename;
  - conditional replacement marker;
  - logical-sector rollover and target-before-tail persistence barrier;
  - no-terminator legacy-media behavior; and
  - FAT16 fixed-root failure.
- In “Rename,” state that rename uses the same reservation and marker rules as
  create while retaining write-new-before-retire-old semantics.
- In “Directory Create/Open,” explicitly retain whole-cluster zero-fill. Do not
  document lazy sector initialization until Gate B ships.

Documentation contract to include in the reference prose:

```text
What: Records the implemented Gate-A persistent end-marker model for all
directory-entry writers. Why: Future create/rename/delete work must distinguish
0xE5 reusable holes from the 0x00 namespace boundary and must preserve the
target-before-exposure ordering. Inputs: the final source phases and completed
FAT16/FAT32/media tests. Outputs/effects: an accurate maintainer contract that
still states whole-cluster initialization is present. Affiliates: source
comments, Sessions 040/056, filesystem.c consumers, and the deferred Gate B.
```

### 11. `knowledge_files/specification_reference/SRAM_MANIFEST.md`

After a clean link:

- record before/after `sizeof` values;
- record the linked `afatfs` symbol before/after;
- correct the stale 7,344-byte table entry to the newly measured result;
- if the intended zero delta is achieved, say so explicitly without inventing
  a new allocation; and
- if any delta exists, stop for RAM approval before treating Phase One as
  complete.

Documentation contract:

```text
What: Records the measured retained-state result of the resumable reservation
refactor. Why: create state is multiplied through the handle pool and embedded
again in rename, so source field counts are not authoritative. Inputs: target-
ABI sizeof checks, linked nm/map symbol sizes, and logging-on/off builds where
applicable. Outputs/effects: exact normal-SRAM1 ownership or an explicit verified
zero delta. Affiliates: AFATFS_MAX_OPEN_FILES, afatfsCreateFile_t,
afatfsFile_t, afatfsRenameObject_t, afatfs_t, and the RAM allocation policy.
```

### 12. `MEMORY.md`, `S059_ASYNCFATFS_SPEEDUP.md`, and this phase record

Only after tests:

- add a concise MEMORY entry stating Gate A shipped and Gate B remains
  deferred;
- append measured card format/cluster size, Bank timing, content/hash results,
  reboot/load/fsck results, and retained-RAM delta to the broad S059 document;
  and
- append the implementation commit, exact changed functions, and pass/fail
  checklist to this phase document.

Do not rewrite estimates as observations. If Phase One is reverted, record the
failed fixture and rollback rather than documenting it as shipped.

## Files inspected but requiring no functional Phase One change

- `Core/Hardware/SD/asyncfatfs/asyncfatfs.h`: public contract stays whole-
  cluster initialized.
- `Core/Hardware/SD/asyncfatfs/fat_standard.c/.h`: existing `0x00`, `0xE5`,
  LFN, checksum, and case helpers remain authoritative.
- `Core/Hardware/SD/filesystem.c/.h`: no Bank phase, save serializer, settings
  FSM, AutoSave FSM, facade arbitration, UI progress, or load behavior change.
- `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c/.h`: no transport, SPI burst,
  timeout, or callback change.
- `Core/Hardware/SD/storageTypes.c/.h`: no schema or filename change.
- Menu, Preset, Bank, Scene, Audio, and playback code: no change.
- `afatfs_findNext()` remains a raw physical iterator; create and rename own
  the stop-at-terminator decision. `afatfs_findNextObject()` already stops at
  the marker and requires no change.
- `afatfs_extendSubdirectoryContinue()` retains full-cluster zero-fill.

If any of these files truly needs a functional change, stop and amend this
schedule with the exact contract before editing it. Do not hide a product-flow,
poll-count, cache-size, or SD-transport change inside the AsyncFATFS patch.

## Implementation order

1. Preserve baseline commit, clean status, build sizes, four structure sizes,
   linked `afatfs` size, card FAT type, sectors per cluster, and Bank fixture
   hashes.
2. Change the private enum/state layout and enforce the zero-growth assertions.
3. Add reset, deleted-run observation, terminator selection, logical-target
   preparation, target-persistence, and old-tail helpers.
4. Convert short create and its writer.
5. Convert LFN create/open and its writer, including create-only alias restart.
6. Convert rename collision, placement, write, moved-marker exposure, and old-
   run retirement.
7. Perform a clean build and static review before any card mutation.
8. Run the focused native/media fixtures below.
9. Run every product mutation family and reboot/load verification.
10. Repeat the controlled Bank timing and hash/fsck comparison.
11. Update the reference, SRAM manifest, MEMORY, and session result records.

Do not combine Gate B with any of these commits. A Phase One failure should be
debuggable/revertible without changing directory-cluster initialization.

## Verification schedule

### A. Static and build checks

- `git diff --check`.
- Clean build: `make clean && make && make img`. A clean build avoids the
  repository's missing header-dependency trap and establishes trustworthy
  sizes even if only the C file was expected to change.
- No new warnings relative to baseline.
- Confirm the three `_Static_assert` values.
- Measure `afatfs`, `.bss`, `.data`, DTCM, and the full image with
  `arm-none-eabi-size -A`, `arm-none-eabi-nm -S --size-sort`, and the map.
- Confirm no functional diff outside `asyncfatfs.c` before documentation.
- Inspect every `0x00` branch: none may dirty/retire the marker during
  observation.
- Inspect every moved-marker path: writer -> target persisted -> old tail
  retired, never another order.
- Inspect all finder exits for balanced `afatfs_findLast()` ownership.

### B. Focused FAT/name fixtures

Run on FAT32 and FAT16 where applicable, using at least one small-cluster image
and the tested 32 KiB-cluster card.

1. Short SFN create in an empty directory, in a deleted hole, at terminator
   index 14, and at index 15 (which must roll and leave a marker at target
   index 1).
2. One-, two-, three-, and four-fragment LFN creates where run+marker fits and
   where it must roll. No LFN/SFN run may cross a sector.
3. A deleted run before a later live same display name and before a later live
   alias collision. Creation must open/reject the live object, not duplicate it.
4. A deleted run before a terminator. The chosen deleted run must leave the
   marker and the entry after the run unchanged.
5. Alias collisions requiring `~1` and later restarts. Exact and folded opens
   must find a later display object even when an earlier object owns the first
   generated alias.
6. File-versus-directory collisions, case-only rename, leading dots, and
   trailing-space/period sanitation.
7. A directory with no terminator and all remaining slots `0xE5`, representing
   current-firmware media. It must scan to physical exhaustion, use the first
   proven hole, and remain readable after reboot.
8. A full no-terminator directory with no sufficient deleted run. An ordinary
   subdirectory must extend; a fixed FAT16 root must fail without allocation.
9. Next logical sector within one cluster, across a deliberately non-contiguous
   FAT link, and beyond allocated size.
10. A post-terminator target sector prefilled with nonzero directory-shaped
    bytes. Completed rollover must clear the full target sector before the old
    tail is retired; no fake object may enumerate before or after reboot.
11. Completed create/rename followed by remove, recursive delete, recreate in
    holes, sync, unmount/remount, and a read-only host filesystem check.

For the moved-target fixture, inspect raw sectors as well as directory listings.
The source order and media barrier are part of acceptance; a normal listing
alone cannot prove stale bytes were cleared before exposure.

### C. Current product workflow matrix

At minimum:

- `settings.tmp` short create/write/close, remove-old, LFN rename to
  `settings.cfg`, reboot read, and interrupted-promotion recovery.
- `/.hcnames`, every `.hcindex`, `/.hcprms1`, `/.hcprms2`, `.hctmp.<ext>`,
  `bootlog.bin`, and `asavetrc.bin`: create when absent, rewrite/append where
  applicable, sync, reboot, and reopen.
- Root Instrument and InstrumentMrp save/load, overwrite, type directory
  creation, index regeneration, and temp cleanup.
- Kit and KitMrp new save, overwrite/delete/recreate, canonical rename repair,
  six child instruments, reboot, and immediate load.
- Scene new save and overwrite with sceneset, embedded Kit, pattern, effects,
  canonical rename repair, reboot, and immediate load.
- Sparse and full Bank Save to both a new and existing target; all progress
  updates; final name/index/settings work; reboot; full Bank Load; and semantic
  content comparison.
- One full Bank Save with playback stopped (primary timing) and one with
  playback running (compatibility). Stopping playback after save begins must
  not alter correctness.
- Samples/Loops and legacy short-name read paths: enumeration, reopen, and EOF
  behavior unchanged.
- Five-handle exhaustion/retry and current-directory chdir/parent/root behavior
  unchanged.

For every saved tree, compare file count, logical byte size, and payload hashes
against the baseline semantic output. Run a host FAT checker without repair and
then load the saved content on hardware after a real reboot.

### D. Bank performance observation

Use the same card, cluster format, firmware logging mode, Bank content, target,
playback state, and measurement boundary before and after:

- start: accepted Bank Save command;
- finish: UI leaves `...` only after the existing final sync/flush boundary;
- primary fixture: existing full 16-Scene Bank target, playback stopped;
- secondary compatibility fixture: same save with playback running.

Record at least two successful post-change runs. A material, repeatable
improvement plus identical payload hashes and clean filesystem is the observable
Phase One success. Do not attribute Gate B's projected 2,000-sector
initialization saving to this build; whole-cluster zero-fill still exists.

## Phase One acceptance criteria

Phase One is complete only when all of the following are true:

- short create, LFN create/open, and rename stop at the first `0x00`;
- every live entry before that marker is still collision-checked;
- a deleted candidate never causes a duplicate object;
- every terminator-owned writer emits a replacement marker;
- a deleted-run writer never clears the entry after its run;
- a moved run uses logical FAT traversal, clears its complete target sector,
  reaches media, and only then retires the old marker-through-sector-end tail;
- no LFN/SFN run crosses a directory sector;
- legacy no-terminator media and FAT16 fixed-root exhaustion behave normally;
- create/mkdir/rename callbacks and final product sync timing are unchanged;
- settings promotion, canonical repairs, hidden files, every load/save family,
  reboot, and host FAT checking pass;
- Bank payload counts/sizes/hashes are semantically identical and Bank Load is
  unchanged;
- full Bank Save shows a material repeatable improvement on the same fixture;
- `afatfsCreateFile_t`, `afatfsFile_t`, and `afatfsRenameObject_t` retain their
  baseline sizes and the linked SRAM delta is measured as zero; and
- source comments, AsyncFATFS reference, SRAM manifest, MEMORY, and recorded
  test results agree that Gate A shipped and Gate B did not.

## Deferred to Phase Two / Gate B

- Replacing whole-cluster zero-fill with first-sector-only initialization.
- Changing `AFATFS_EXTEND_SUBDIRECTORY_PHASE_WRITE_SECTORS` or its public
  header contract.
- Initializing later directory sectors solely on marker advance.
- Claiming the projected reduction from 2,048 child-directory initialization
  sectors to 48.
- Bank logic, load behavior, serializers, file formats, cache size, SPI burst,
  polling cadence, playback-specific branching, public move/copy/replace APIs,
  or transactional rename/create.

If Phase One passes, Gate B can use the same selected-run, target preparation,
media barrier, and old-tail ordering. Its implementation diff should then be
limited principally to `afatfs_extendSubdirectoryContinue()`, its private
comments/phases, the public header's mkdir guarantee wording, reference
documentation, and a second full compatibility run.

## Implementation notes / verification record

- 2026-08-30: Read `MEMORY.md`, this phase schedule, the broader
  `S059_ASYNCFATFS_SPEEDUP.md`, the full AsyncFATFS reference, and the
  archived Session 040 AsyncFATFS boundary. The current baseline source is
  commit `53a7676`; the working tree was clean before implementation.
- 2026-08-30: The pre-implementation build artifact reported `text=380,436`,
  `data=408`, `bss=94,848`, with `afatfs` linked at `6,984` bytes. That
  artifact predates a clean dependency rebuild, so its whole-image totals are
  not used as a retained-state comparison; the `afatfs` symbol is the direct
  owner comparison.
- 2026-08-30: Implemented the shared reservation state, deleted-run
  latch-and-continue scan, first-marker stop, short/LFN selected-run writers,
  logical next-sector preparation, target persistence barrier, old-marker-tail
  retirement, and the equivalent rename flow. The former single-entry
  allocator and old free-run helpers are removed. Gate B remains explicitly
  deferred: `afatfs_extendSubdirectoryContinue()` and its full-cluster
  zero-fill loop were not functionally changed.
- 2026-08-30: Clean `make` and `make img` pass. The final logging-on link
  reports `text=383,956`, `data=404`, `bss=96,160`, `.text=371,024`,
  `.bss=89,488`, and `afatfs=6,984` bytes. The compile-time state checks pass
  at 144/188/552 bytes. `git diff --check` passes and no new compiler warning
  was observed beyond the existing asyncfatfs/cache, unused-function, nano
  syscall, and LTO warnings.
- 2026-08-30: Changed functions include the create/rename private phase enums
  and state lifecycle, `afatfs_resetDirectoryRunReservation()`,
  `afatfs_noteDeletedDirectoryEntry()`, `afatfs_selectDirectoryRunAtTerminator()`,
  `afatfs_prepareDirectoryRunTarget()`,
  `afatfs_directoryRunTargetPersistence()`,
  `afatfs_retireDirectoryTerminatorTail()`,
  `afatfs_createShortDirectoryEntry()`, the LFN writer, and the create/rename
  continuations. The public header has only the adjacent component-contract
  comment requested for this change.
- 2026-08-30: Native FAT/media fixtures, FAT16/FAT32 card runs, reboot/remount,
  host FAT checking, product compatibility, and repeatable Bank timing remain
  unrun in this workspace. Phase One is therefore source/build complete but
  not hardware-acceptance complete; no timing improvement or payload result is
  claimed.

### Related Instrument Load regression follow-up

- 2026-08-30: The supplied load/save capture exposed a separate product-layer
  defect while Phase One was being integrated: a case-sensitive display-only
  `.hctmp` filter and byte-count-only typed-index reader admitted a blank Drum
  row, while three missing typed indexes were treated as successful empty
  lists. This is recorded here as a compatibility finding only; it does not
  change Gate A's AsyncFATFS scope or its deferred Gate B decision.
- 2026-08-30: `S059_INST_LOAD_FIX.md` now fixes that finding in the
  filesystem/Menu layer. The producer rejects reserved aliases and unusable
  stems before shared-cache mutation; the consumer validates compact typed
  rows and transparently scans/rewrites only the selected type on missing or
  corrupt metadata. No AsyncFATFS reservation state, create semantics, or
  Phase One timing claim is changed.
