# Session 059 AsyncFATFS directory-create refactor and speedup

Date: 2026-08-30

Status: implementation plan for the next session. No source code was changed
while preparing this document.

## Objective and scope

Refactor AsyncFATFS directory-entry allocation so it preserves the FAT end-of-
directory marker and initializes newly allocated directory storage only when it
becomes visible. The change is intended to remove the dominant redundant I/O in
a fresh full Bank Save without changing Bank logic, file formats, public
filesystem behavior, or load behavior.

This is a **general AsyncFATFS correction and optimization**. It applies whether
playback is running or stopped and benefits any caller that creates directory
objects. Stopped playback supplies more foreground poll opportunities; it is
not part of the on-card algorithm. Do not make this conditional on playback
state.

The implementation has two independently testable gates:

1. **Gate A — terminator-aware entry reservation:** keep full-cluster directory
   zero-fill temporarily, but stop create and rename scans at the first `0x00`
   entry, preserve or move that marker correctly, and never expose stale bytes.
2. **Gate B — lazy directory-sector initialization:** only after Gate A passes,
   replace whole-cluster zero-fill with first-sector initialization. Later
   sectors are initialized before the terminator is moved into them.

If Gate B fails compatibility testing, revert only Gate B. Gate A remains a
valid speedup and preserves the old fully-zeroed-cluster safety net.

## Required project context

Read before implementation:

- `MEMORY.md`, especially the AsyncFATFS boundaries and Sessions 036, 040, 053,
  054, and 056 notes;
- `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md` in full;
- `knowledge_files/log_archive/040_SESSION_HANDOFF_LOG.md`, especially
  “AsyncFATFS follow-up boundary”; and
- `knowledge_files/specification_reference/SRAM_MANIFEST.md` before accepting
  any retained-state increase.

`MEMORY.md` names a root `SESSION_040_AFATFS_FOLLOWUP.md`, but that file is not
present in the current tree. The archived Session 040 handoff above is the
available authoritative record. This plan does not revive the removed parent-
relative APIs, opened-handle unlink, cross-parent move, tree copy, or tree
replace.

## Current evidence and expected impact

The checked `SD_CARD_BANK_NOPLAY_SAVE/Bank/046 Full` fixture contains 161 files
and 144,801 logical bytes. Rounded to sectors touched by payloads, those files
require 353 512-byte data-sector writes: 65 one-sector files and 96 three-sector
Instrument files.

Replacing an existing full Bank child creates 16 Scene directories and 16
embedded Kit directories. On the tested 32 KiB-cluster card, each directory
cluster contains 64 sectors. The current
`afatfs_extendSubdirectoryContinue()` therefore performs at least:

```
32 new child directories x 64 sectors = 2,048 directory-zero writes
file payloads                            353 payload writes
                                         --------------------
minimum initialization/payload work    2,401 sector writes
                                      1,229,312 bytes (1.17 MiB)
```

A new Bank target adds one more 64-sector directory initialization. These
figures exclude the old-tree delete, FAT allocation/freeing, parent entries,
LFNs, index/name files, `bankset.bcg`, and final flush.

The larger avoidable cost is caused by LFN creation. Current code treats both a
deleted entry (`0xE5`) and the FAT end marker (`0x00`) as free entries, retires
each `0x00`, and continues to the physical end of the allocated directory. The
first LFN creation in a new 64-sector cluster consequently dirties nearly the
whole cluster a second time. Later creates have no terminator and repeatedly
scan the entire cluster.

One saved Scene subtree performs four LFN creates in its Scene directory and
seven in its embedded Kit directory. For 16 Scenes this creates the following
conservative child-only work:

```
16 Scenes x 11 LFN scans x 64 sectors = 11,264 directory-sector visits
32 first LFN scans x 64 sectors       =  2,048 extra dirty-sector writes
```

Gate A removes those physical-end scans and second-pass writes. Gate B removes
the unused initial sectors too. With the existing always-LFN product calls, a
Scene directory needs one initialized sector and a Kit directory needs two:

```
16 Scene directories x 1 sector = 16 sectors
16 Kit directories x 2 sectors  = 32 sectors
file payloads                    = 353 sectors
                                  -----------
new static minimum               = 401 sectors before metadata
```

Gate B therefore avoids 2,000 of the 2,048 child-directory initialization
writes, or 1,024,000 bytes, in this workload. Timing remains card-dependent.
The earlier traffic-derived estimates of roughly 8–15 seconds for Gate A and
potentially several seconds for Gates A+B are planning ranges, not acceptance
promises.

## Non-negotiable compatibility contracts

The implementation must preserve all of the following:

- one foreground, non-reentrant `afatfs_poll()` owner;
- FAT16 and FAT32 mounting and directory traversal;
- current five-handle pool and current-directory lifetime rules;
- current component-only APIs, callback timing, result codes, and public
  signatures;
- LFN sanitization, trailing-space/period trimming, VFAT checksum/ordinal
  validation, deterministic `~N` aliases, SFN `ntReserved` case bits, exact and
  case-insensitive match modes, and type-collision rejection;
- the Session 056 duplicate-prevention rule: a deleted run is only a candidate;
  all **live** entries before the first terminator must still be scanned for a
  matching display name or alias before creation;
- complete LFN/SFN-run identity for rename, remove, and recursive delete;
- directory-first-cluster initialization before a successful mkdir callback;
- `.` and `..` only in the first sector of the first cluster of a non-root
  subdirectory;
- save completion only after the existing `afatfs_sync()`/flush boundary;
- normal file cluster allocation, truncate, append, close-size publication,
  and the Session 056 `fseekAtomic`/logical-size correction; and
- visibility of dot-prefixed objects to AsyncFATFS. Product filtering remains
  in `filesystem.c`.

The on-disk terminator is the only persistent initialization authority. Do not
add a RAM-only “initialized sector” witness: after a reboot, the first `0x00`
must still be sufficient to prove that every later directory byte is outside
the live namespace.

## On-disk rules the implementation must enforce

1. A `0x00` first filename byte ends the live FAT directory. No collision or
   valid object exists after the first such entry.
2. A `0xE5` entry is reusable but does not end the live directory. A matching
   object may exist later, so a deleted run cannot end collision scanning.
3. A new LFN run remains sector-local, exactly as today. It contains all LFN
   fragments followed immediately by its SFN.
4. A run placed in deleted entries before the terminator does not move the
   terminator and must not zero the entry following the run.
5. A run that consumes the terminator must reserve and write one replacement
   zero entry immediately after its SFN.
6. If the remaining sector tail cannot hold the object run **and** replacement
   terminator, put the entire run at entry zero of the next logical sector.
   Initialize and dirty that target sector before retiring the old terminator
   and skipped tail.
7. Logical-sector movement must use the directory cursor and FAT chain. Never
   calculate the next directory sector as `physicalSector + 1` across a cluster
   boundary.
8. If no next allocated sector exists, extend an extendable directory. A full
   FAT16 fixed root directory must fail normally rather than allocate a cluster.
9. A directory with no terminator remains supported. At physical exhaustion,
   use a previously proven deleted run or extend the directory. This is required
   for media written by the current firmware, which may contain clusters full
   of `0xE5` after the last live entry.

## Detailed source-change register

Every required production and documentation change is listed below. Each
source change must receive an adjacent comment block carrying the stated What,
Why, Inputs, Outputs/effects, and Affiliates contract. Names below are proposed
for clarity; equivalent private names are acceptable only if the state and
ordering remain explicit.

### 1. `Core/Hardware/SD/asyncfatfs/asyncfatfs.c` — private phases and run state

Refactor the current `afatfsCreateFile_t` free-run fields into one private
directory-run reservation state used by short create, LFN create, and the
embedded `afatfsRenameObject_t.newNameState`. Add create/rename phases needed to
prepare a next logical sector and retire an old terminator tail without blocking
or assuming physical contiguity.

The state must represent at least:

- requested object-entry count (`1` for SFN, `lfnEntryCount + 1` for LFN/SFN);
- current deleted-run length/start and the first viable latched deleted run;
- selected run start;
- selected origin: none, deleted-run, or terminator-owned;
- original terminator sector/index when the run must move to the next sector;
- whether the target logical sector has been initialized/selected; and
- enough phase information to resume after cache, seek, FAT, or directory-
  extension progress.

Adjacent comment contract:

- **What:** persists one candidate/selected sector-local directory-entry run
  across asynchronous polls and records whether creation owns and must replace
  the end marker.
- **Why:** create and rename currently implement subtly different free-run
  logic; short create also relies on pre-zeroed bytes after its SFN. Lazy sector
  initialization is safe only if every writer shares the same explicit
  terminator contract.
- **Inputs:** required entry count, raw finder position, entry classification,
  create permission, and the 16-entry sector boundary.
- **Outputs/effects:** either a deleted run that leaves the terminator intact,
  or a terminator-owned run with a reserved following marker; resumable state
  for next-sector preparation and directory extension.
- **Affiliates:** `afatfs_createFileContinue()`,
  `afatfs_allocateDirectoryEntry()` or its replacement,
  `afatfs_createLongDirectoryEntries()`, rename collision/write phases,
  `afatfs_extendSubdirectory()`, `afatfs_findNext()`, and
  `AFATFS_FILES_PER_DIRECTORY_SECTOR`.

Initialize/reset every field in `afatfs_createFileInternal()`, the initial
`afatfs_createFileContinue()` phase,
`afatfs_renameObjectRestartCollisionScan()`/new-name preparation, every alias
collision restart, success, and failure. `afatfs_initFileHandle()` currently
zeroes a new file handle, but the explicit phase resets must still make retries
and the embedded rename state self-contained. `afatfsRenameObject_t` embeds
`afatfsCreateFile_t`, so do not add a second divergent reservation state.

Before accepting the structure layout, record `sizeof(afatfsCreateFile_t)`,
`sizeof(afatfsFile_t)`, `sizeof(afatfsRenameObject_t)`, and the linked `afatfs`
symbol delta. Prefer reusing/repacking existing byte fields. Do not silently
increase every open handle merely for convenient diagnostics.

### 2. `asyncfatfs.c` — shared free-run and terminator helpers

Replace the LFN-only assumptions in `afatfs_noteFreeDirectoryEntry()`,
`afatfs_freeRunIsReady()`, and `afatfs_retireDirectoryTerminator()` with small
shared helpers that perform these distinct jobs:

- observe a deleted entry and latch the first sector-local run large enough for
  the requested object-entry count;
- finish collision scanning at a terminator and prefer an already-latched
  deleted run;
- determine whether the current terminator tail has room for object entries and
  one replacement marker;
- select a same-sector terminator-owned run when it fits;
- prepare the next logical sector when it does not fit; and
- retire only the old terminator-through-end-of-sector tail after the prepared
  target sector has been dirtied.

Adjacent comment contract:

- **What:** centralizes the FAT free-entry/end-marker rules for all entry
  writers while retaining the current sector-local LFN policy.
- **Why:** `0xE5` and `0x00` are not interchangeable. Duplicating this decision
  in LFN create, SFN create, and rename would make lazy initialization unsafe
  for one of the less frequent paths.
- **Inputs:** reservation state, finder pointer, entry kind, requested entry
  count, and current-directory allocation/cursor state.
- **Outputs/effects:** candidate latch, selected run and origin, or an
  asynchronous request to prepare/extend the next sector. No live entry is
  modified by observation alone.
- **Affiliates:** raw directory finder, cache ownership, FAT seek/append
  continuations, the three writers listed above, and object iteration's
  stop-at-terminator behavior.

Do not retain the current behavior that changes each observed `0x00` to `0xE5`.
The retirement helper is only for the one old tail that must be crossed to make
a prepared next-sector run visible.

### 3. `asyncfatfs.c` — target-sector preparation and write ordering

Add a resumable private continuation, or equivalent create/rename phases, for a
terminator that is too close to the end of its sector.

Required sequence:

1. Save the old terminator's logical/physical entry pointer.
2. End the raw scan cleanly so no cache retain is leaked.
3. Move to the next **logical** directory sector through the existing cursor/
   seek/FAT machinery. If it is beyond allocated size, call
   `afatfs_extendSubdirectory()` unless this is the fixed FAT16 root.
4. Cache the target with write ownership, zero the complete 512-byte sector,
   select entry zero, write the eventual object run and replacement marker, and
   mark this sector dirty first.
5. Only then cache the old terminator sector, convert the old terminator and
   all remaining entries in that sector to `0xE5`, and mark it dirty.

Adjacent comment contract:

- **What:** initializes a formerly invisible logical directory sector and
  establishes a safe new end marker before removing the marker that hid it.
- **Why:** sectors after `0x00` may contain stale nonzero card data, including
  after reboot. Retiring the old marker first could expose garbage or a partial
  LFN run.
- **Inputs:** old terminator pointer, selected object/SFN data, required entry
  count, directory cursor/allocation state, and cache availability.
- **Outputs/effects:** target sector dirty before source-tail sector, complete
  sector-local object run at entry zero, replacement `0x00`, and a visible
  directory only after the old tail is later persisted.
- **Affiliates:** `afatfs_cacheSector(AFATFS_CACHE_WRITE)`, dirty timestamps and
  oldest-first `afatfs_flush()`, `afatfs_fseekInternal()`/seek continuation,
  `afatfs_extendSubdirectory()`, create callbacks, and rename's old-run retire.

The cache's oldest-dirty-first policy is part of the safety ordering: dirty the
complete target sector before dirtying the sector that exposes it. If the
implementation cannot prove that ordering remains true under cache pressure,
add an internal wait-for-target-sector-in-sync phase before retiring the old
tail. Do not use a physical `+1` shortcut.

### 4. `asyncfatfs.c` — `afatfs_createFileContinue()` scan and decisions

Change both short-name and LFN create/open scanning to distinguish deleted
entries from the first terminator.

LFN behavior:

- continue matching live entries and checking alias collisions through deleted
  runs;
- latch but do not immediately use the first viable deleted run;
- on the first terminator, the live collision scan is complete;
- for create mode, use the latched deleted run if present; otherwise reserve at
  the terminator or enter next-sector preparation;
- for open-only mode, fail not-found immediately at the terminator; and
- on alias collision, generate the next `~N` candidate and restart from the
  beginning with all reservation/terminator state reset.

Short-name behavior:

- preserve the existing full live-name scan through the terminator decision;
- if absent and creation is allowed, use a deleted slot found before the
  terminator or reserve a one-entry terminator-owned run;
- do not restart a second allocator scan that loses knowledge of which marker
  was consumed; and
- ensure a short entry at sector index 15 rolls to a prepared next sector so a
  replacement marker always exists.

Adjacent comment contract:

- **What:** turns the first `0x00` into a successful absence/collision boundary
  and hands a fully described run to the writer instead of scanning to physical
  exhaustion.
- **Why:** FAT guarantees that no live name follows the terminator. Continuing
  wastes I/O and destroys the persistent boundary required by lazy sectors.
- **Inputs:** mode bits, match mode, requested type, display/SFN candidate,
  finder entry, LFN scan/checksum state, and reservation state.
- **Outputs/effects:** open existing object, reject name/type collision, report
  not found, restart alias scan, select a safe create run, or asynchronously
  prepare/extend a target sector.
- **Affiliates:** `afatfs_generateShortAlias()`, `afatfs_lfnScanAppend()`,
  `afatfs_findFirst()/findNext()/findLast()`, create phases, and the short/LFN
  writers.

Do not optimize away LFN records for 8.3-compatible display names in this
session. Product code currently calls the LFN APIs deliberately; preserving the
physical representation keeps this refactor about allocation/termination only.

### 5. `asyncfatfs.c` — short SFN allocation and writer

Refactor `afatfs_allocateDirectoryEntry()` so it consumes the selected shared
reservation rather than independently rescanning for the first `0xE5`/`0x00`.
It may be renamed or reduced to a writer helper if the scan now performs all
allocation decisions.

When the selected run owns a terminator, write the SFN and zero the immediately
following entry in the same target sector. When it uses a deleted slot, write
only that slot. Retain all current SFN fields, dates, attributes, case bits,
directory-handoff behavior, and failure cleanup.

Adjacent comment contract:

- **What:** writes one short directory entry under the same replacement-marker
  contract as an LFN/SFN run.
- **Why:** `settings.tmp`, legacy short files, and raw short directory APIs use
  this path. Their safety must not depend on a previously zero-filled whole
  cluster.
- **Inputs:** selected run/origin, FAT-format filename, `ntReserved` case bits,
  requested attributes/type, and create callback state.
- **Outputs/effects:** initialized SFN, optional same-sector replacement
  terminator, `file->directoryEntryPos`, dirty cache sector, and unchanged mkdir
  handoff or regular-file success flow.
- **Affiliates:** `afatfs_fopen()`, `afatfs_mkdir()`, settings safe-write,
  `afatfs_handoffCreatedDirectoryToInitializer()`, and close/truncate logic.

### 6. `asyncfatfs.c` — `afatfs_createLongDirectoryEntries()`

Keep the existing fragment order, checksum, ASCII-boundary behavior, SFN alias,
case flags, attributes, and timestamps. Add only the selected-run contract:

- validate that the object run fits in one sector;
- when origin is terminator-owned, validate room for one extra entry and zero
  that complete entry after the SFN;
- when origin is a deleted run, do not touch the following entry; and
- publish `file->directoryEntryPos` at the SFN as today.

Adjacent comment contract:

- **What:** writes a complete VFAT LFN/SFN run and, only when it consumed the
  old end marker, writes its replacement.
- **Why:** a conditional explicit marker prevents stale bytes becoming visible
  without truncating live entries after a reused deleted run.
- **Inputs:** selected run/start/origin, `longName`, `lfnEntryCount`, generated
  SFN, case flags, and requested attributes.
- **Outputs/effects:** one dirty sector containing a valid VFAT run, optional
  replacement `0x00`, and the SFN entry pointer used by later save/close logic.
- **Affiliates:** LFN fragment writer/checksum, create state machine, mkdir
  initializer handoff, object iterator, remove, and rename.

### 7. `asyncfatfs.c` — rename collision scan, run selection, and writer

Update `afatfs_renameObjectRestartCollisionScan()`, the
`AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN` branch,
`afatfs_renameObjectChooseRun()`, and `afatfs_renameObjectWriteRun()` to use the
same terminator-aware reservation state.

Required behavior:

- scan all live objects before the first terminator, excluding the source SFN
  from collision checks exactly as today;
- retain the in-place rewrite when the new run fits safely in the validated old
  sector-local run;
- otherwise prefer a proven deleted run, then a terminator-owned run, then
  prepare/extend a next sector;
- write the complete new run and any replacement marker before retiring the old
  source run; and
- keep the current structured result and publish `openNameOut` only on success.

Adjacent comment contract:

- **What:** gives rename the same end-marker and lazy-sector allocation rules as
  create while retaining its in-place and write-new-before-retire-old behavior.
- **Why:** settings promotion and canonical-name repair rename real objects.
  A create-only fix would leave these paths able to scan stale sectors or
  consume a terminator without replacing it.
- **Inputs:** validated source object/run, copied source SFN, requested new
  display name/match mode, generated alias, reservation state, and directory
  allocation state.
- **Outputs/effects:** unchanged object cluster/size/attributes/timestamps,
  valid new name run and marker, retired old complete run only after new-run
  creation, structured result, and alias output on OK.
- **Affiliates:** `settings.tmp` to `settings.cfg` promotion, Kit/Scene/Bank
  canonical repairs, object validation, complete-run retire, and remove/delete
  identity rules.

Do not claim rename becomes power-loss transactional. Its existing ordering may
temporarily leave both old and new names if interrupted. The refactor must not
worsen that behavior or expose stale entries beyond a moved terminator.

### 8. `asyncfatfs.c` — Gate B in `afatfs_extendSubdirectoryContinue()`

Only after Gate A passes, change
`AFATFS_EXTEND_SUBDIRECTORY_PHASE_WRITE_SECTORS` from a loop over every sector in
the appended cluster to initialization of the first sector only.

For a directory's first cluster:

- zero the complete first 512-byte sector;
- write `.` at entry 0 and `..` at entry 1 with the same cluster fields and
  attributes as today; and
- leave entry 2 zero as the end marker.

For a later appended cluster:

- zero the complete first sector;
- leave entry 0 zero as the end marker; and
- do not write `.` or `..` again.

Do not loop through or seek back over the remaining sectors. The allocated
cluster and `physicalSize` remain unchanged; only initialization traffic
changes. A later target-sector preparation is responsible for zeroing another
logical sector before moving the marker there.

Adjacent comment contract:

- **What:** initializes only the first visible sector of a newly appended
  directory cluster and leaves all later sectors logically hidden behind its
  on-disk `0x00` marker.
- **Why:** FAT allocation does not require every directory sector to be zeroed.
  Full zero-fill costs 64 writes on the tested card even when a directory uses
  one or two sectors.
- **Inputs:** appended cluster cursor, `cursorOffset`, first-cluster versus
  extension status, child `firstCluster`, and parent cluster supplied for a new
  subdirectory.
- **Outputs/effects:** one dirty initialized sector, correct dots for a new
  child, valid terminator, unchanged allocated-size/FAT chain, and callback only
  after initialization has been queued successfully.
- **Affiliates:** append-free-cluster state, parent-entry first-cluster save,
  mkdir handoff, target-sector preparation, FAT16-root no-extension rule, and
  directory scans after remount.

Rename the phase/comment from “write sectors/zero out cluster” if necessary so
the source no longer documents the old behavior. Also update the comment block
on `afatfs_extendSubdirectory()` itself: it currently promises that the whole
new cluster is zero-filled. Its replacement must state that the first sector is
initialized immediately and every later sector is cleared by the terminator-
advance continuation before exposure; its parent-directory and busy/failure
inputs and callback outputs remain unchanged.

### 9. `Core/Hardware/SD/asyncfatfs/asyncfatfs.h` — public contract comments

No public function, enum, result, structure, signature, or include should
change. Update the comment blocks adjacent to `afatfs_fopen[_lfn]()`,
`afatfs_mkdir[_lfn]()`, and `afatfs_renameObject_lfn()` where relevant.

Header comment contract:

- **What:** specifies that new directories have an allocated first cluster,
  initialized first sector, correct dots, and a valid logical terminator;
  additional sectors are initialized before becoming visible.
- **Why:** callers need the usable-on-callback guarantee, not the obsolete
  implementation detail that the complete cluster is zero-filled.
- **Inputs:** component name, create/open mode or match mode, alias buffer, and
  completion callback.
- **Outputs/effects:** same handle/result/alias behavior and callback timing as
  before; no new caller responsibility for initialization. Persistence still
  occurs at the existing caller-visible sync/flush boundary.
- **Affiliates:** AsyncFATFS create/rename state machines,
  `afatfs_chdir()`, `filesystem.c`, and the reference document.

Explicitly retain that mkdir callbacks receive a handle immediately safe for
`afatfs_chdir()` and child creation.

### 10. `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`

Update “Directory Create/Open” and “Directory Terminators And LFN Creation”
after the code passes its gate. Document the persistent-marker model, deleted-
run collision rule, same-sector replacement marker, next-logical-sector
ordering, lazy first-sector initialization, FAT16 fixed-root behavior, and
legacy no-terminator compatibility.

Documentation contract:

- **What:** replaces the obsolete full-cluster-zero and latch-until-physical-
  exhaustion descriptions with the implemented terminator-aware model.
- **Why:** this file is the firmware-facing AsyncFATFS authority for future
  create, rename, delete, and media-compatibility work.
- **Inputs:** final implemented state transitions and hardware/media results.
- **Outputs/effects:** an accurate maintainer contract, including what remains
  deliberately unsupported.
- **Affiliates:** `MEMORY.md`, source/header comments, Sessions 040/056
  boundaries, and all current filesystem consumers.

### Related Instrument Load regression follow-up

The captured Drum blank row and missing Snare/Cymbal/HiHat indexes were
confirmed as a filesystem index producer/consumer issue, not an AsyncFATFS
allocation or Gate-A terminator-placement issue. The repair is tracked in
`S059_INST_LOAD_FIX.md`: it filters `.hctmp` LFN/SFN aliases, validates compact
typed rows, and reuses the existing selected-type scan/write chain for runtime
metadata recovery. This follow-up leaves the Phase One/Gate B boundary,
AsyncFATFS state model, and Bank timing scope unchanged.

### 11. `knowledge_files/specification_reference/SRAM_MANIFEST.md`

After linking, update this document only with measured retained-memory results.
Record the `afatfs` symbol change and explain whether it comes from
`afatfsFile_t` multiplied by five handles/current-directory storage, the global
rename state, or both. If layout was successfully kept unchanged, record the
zero-delta verification in the Session 059 result rather than inventing a
manifest allocation.

Documentation contract:

- **What:** accounts for any real SRAM change caused by resumable reservation
  state.
- **Why:** private AsyncFATFS state is retained globally and can multiply through
  the handle pool; source-level byte estimates are not sufficient.
- **Inputs:** compiler `sizeof` checks and linked map/nm symbol sizes.
- **Outputs/effects:** exact before/after bytes and owner attribution.
- **Affiliates:** `AFATFS_MAX_OPEN_FILES`, `afatfsFile_t`, `afatfs_t`, and the
  project's retained-RAM budget.

### 12. `MEMORY.md` and this plan's completion record

After both gates reach a final disposition, add a concise Session 059 memory
entry stating what shipped, measured timing, card/cluster format, compatibility
results, SRAM delta, and any reverted gate. Append the detailed implementation
and test results to this document; do not rewrite the proposal as though its
estimates were measurements.

Documentation contract:

- **What:** preserves the final architectural decision and empirical outcome.
- **Why:** later work must know whether full-cluster zeroing is still present,
  which terminator protocol is authoritative, and what rollback occurred.
- **Inputs:** committed implementation, hardware timings, file-tree/hash checks,
  filesystem checks, and linked RAM result.
- **Outputs/effects:** durable project context and a truthful historical plan/
  result boundary.
- **Affiliates:** S058 closeout, AsyncFATFS reference, SRAM manifest, and future
  load/save sessions.

## Files deliberately requiring no functional change

The implementation is incomplete if it needs product-specific exceptions in
these files. They must be inspected and regression-tested, but no functional
edit is planned:

- `Core/Hardware/SD/filesystem.c` and `.h`: no Bank phase, load path, save
  serializer, autosave FSM, index/name registry, settings FSM, playback gate,
  or public facade change;
- `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c` and `.h`: no global burst-size,
  timeout, SPI, command, or callback change;
- `Core/Hardware/SD/asyncfatfs/fat_standard.c` and `.h`: existing `0x00`,
  `0xE5`, LFN, and checksum classifiers remain the source of FAT semantics;
- `Core/Hardware/SD/storageTypes.c` and `.h`: no schema, filename, or alias
  representation change;
- Menu, Preset, AudioCodecManager, playback, and UI files: no ownership,
  progress, or completion behavior change; and
- Bank Load: no traversal, parser, commit, completion, clean-authority, or UI
  change.

If implementation discovers that one of these files truly must change, stop
and amend this plan with the exact new contract before editing it. Do not fold
an unrelated poll-count or product-flow change into the AsyncFATFS patch.

## Current-use compatibility matrix

All rows must pass with Gate A alone and again with Gates A+B.

### Product files and dot-prefixed objects

- Root `/.hcnames`: create, rewrite, close/sync, reboot, read, and regeneration.
- Root `.hcindex` and typed-root `.hcindex` files: create when absent, rewrite,
  scan filtering, reboot, and regeneration.
- AutoSave `/.hcprms1` and `/.hcprms2`: boot-time ensure when neither/one/both
  exist; leading-dot alias independence; source reader and inactive-record
  writer open concurrently; record data/CRC/commit order; remove old variants;
  sync; reboot selection; and subsequent save.
- `.hctmp.<ext>` Instrument temporary files inside a typed Instrument directory:
  create/write/read/remove inside that directory,
  typed directory, exclusion from product indexing, visibility to raw
  AsyncFATFS object iteration, and cleanup after interruption.
- `settings.tmp`: short-name create/write/close, old `settings.cfg` removal,
  LFN-aware rename promotion, reboot read, and retry after a failed/interrupted
  promotion.
- `bootlog.bin` and `asavetrc.bin`: create if absent, append when present,
  close/sync, and reboot append.

### Every load/save family

- Instrument normal save/load, Morph save/load, overwrite, type/extension
  change, typed-directory creation, index regeneration, and temp cleanup.
- Kit and Kit Morph save/load, canonical directory-name repair, kitset creation,
  six Instrument children, overwrite/delete/recreate, and immediate reload.
- Scene save/load, canonical directory-name repair, sceneset, embedded Kit,
  pattern/effects files, overwrite/delete/recreate, and immediate reload.
- Bank sparse and full save, new and existing target, fresh boot and later save,
  playback running and stopped, stop-after-save-start, all 16 progress updates,
  final index/name work, reboot, full load, and content comparison.
- Settings save/load and autosave start/drain/finalization while other normal
  filesystem work is idle or resumes afterward.
- Samples/Loops and legacy blocking reads: directory enumeration, raw short-name
  reopen, EOF behavior, and no change to their read-only data path.

### Native filesystem operations

- LFN and SFN create in an empty directory, a populated directory, and a
  directory with deleted holes before an existing matching object.
- Exact and folded display-name opens; case-only rename; alias collision and
  `~N` restart; file-versus-directory collision; leading dots; trailing
  space/period sanitation; and 8.3-compatible names passed through LFN APIs.
- Complete LFN/SFN remove, exact short-alias remove, recursive delete, empty
  directory removal, and re-creation in the resulting holes.
- Current-directory chdir/parent/root behavior and five-handle exhaustion/retry.
- Close/sync visibility and remount behavior. A remount must require no retained
  RAM witness to locate the correct directory end.

### FAT/media edge cases

- FAT32 root and ordinary subdirectories; FAT16 fixed root and FAT16
  subdirectories.
- At least a small-cluster card/image and the tested 32 KiB-cluster card.
- Terminator at entry indexes where object+marker fits, exactly does not fit,
  and leaves only a partial LFN tail.
- One-, two-, three-, and maximum-supported-fragment LFNs; no LFN/SFN run may
  cross a sector.
- Deleted run before a later same-name object (Session 056 regression), deleted
  run before a terminator, no deleted run, and a directory with no terminator.
- Next logical sector inside the same cluster, across a non-contiguous FAT
  cluster link, and beyond allocated size requiring extension.
- A sector after a terminator pre-filled with nonzero/stale directory-shaped
  bytes. Those bytes must remain invisible and must be zeroed before exposure.
- Reboot after media was created by the old firmware whose unused directory
  cluster is all `0xE5`; create must use a deleted run or extend normally.
- Host mount and filesystem checker after creation, rename, removal, and reboot.

## Failure and interruption behavior

Exercise removal/power interruption, where practical, at these boundaries:

- before and after target-sector zeroing;
- after the target run/marker is dirty or persisted but before old-terminator
  retirement;
- after old-tail retirement but before create callback/final sync;
- after a parent directory entry names a new child but before/while its first
  directory sector is initialized;
- during rename after the new run is written but before the old run is retired;
  and
- during final save flush.

Acceptable outcomes follow existing AsyncFATFS guarantees: an interrupted new
object may be absent, and rename may retain duplicate old/new names as it can
today, but directory traversal must never expose stale post-terminator garbage,
an incomplete name as a valid unrelated object, or a child whose successful
mkdir callback occurred before dots/terminator initialization. No cluster-chain
leak or malformed LFN run may be introduced by a normal completed operation.

## Implementation and verification order

### Baseline

1. Preserve the current source revision and card format details.
2. Fresh boot, full-save the same Bank into a known existing target with
   playback stopped; record wall time and progress behavior.
3. Copy the card tree, count files/bytes, hash every payload, and run a host FAT
   checker without repair. Repeat once with playback running for compatibility,
   not as the optimization target.
4. Record current linked `afatfs` size and relevant `sizeof` values.

### Gate A

1. Implement source-change items 1–7 while leaving whole-cluster zero-fill in
   place.
2. Build with warnings treated exactly as the project normally does; inspect
   the map for retained-state growth.
3. Run the native FAT/name/media edge cases, especially the Session 056 deleted-
   hole duplicate regression, short create, rename, a run at sector end, and
   old all-`0xE5` directories.
4. Run the complete product compatibility matrix.
5. Repeat the baseline Bank test and inspect the resulting card. Advance only
   if content is identical, the filesystem is clean, and timing materially
   improves without a load regression.

### Gate B

1. Implement source-change item 8 and its header/reference documentation.
2. Verify a fresh directory has one initialized sector and a valid terminator;
   verify later allocated sectors may contain stale bytes but remain invisible.
3. Fill a Kit far enough to force the second logical directory sector; verify
   that sector is cleared before exposure and survives reboot.
4. Exercise same-cluster and FAT-chain-crossing next-sector cases, FAT16 fixed
   root full behavior, rename, delete/recreate, and host-created media.
5. Repeat the full product matrix, Bank timing, payload hashes, remount, loads,
   and host filesystem check.
6. If any failure is unique to Gate B, restore full-cluster zero-fill and keep
   the proven Gate A implementation; document the exact failed case.

### Documentation and closeout

After the final code result is known, update items 9–12, append measured results
to this plan, and state explicitly whether Gate A and Gate B shipped. Do not
record the projected 8–15-second or several-second ranges as measurements.

## Acceptance criteria

The refactor is complete only when:

- every create/rename writer preserves a correct end marker without relying on
  whole-cluster zero-fill;
- deleted holes never cause a duplicate display-name or alias creation;
- stale bytes after a terminator never become visible;
- the full compatibility matrix passes after reboot/remount;
- all saved payload counts, sizes, and hashes match their pre-change semantic
  output, and every load/save family succeeds;
- Bank Load source and behavior remain unchanged;
- fresh-boot full Bank Save shows a material, repeatable improvement on the same
  card and test content;
- playback-running behavior remains correct even though the largest timing gain
  is expected with playback stopped;
- `afatfs_sync()` remains the final UI-visible completion boundary;
- linked SRAM change is measured, justified, and documented; and
- source/header/reference comment blocks agree with the implemented behavior.

## Explicitly out of scope

- Option 3 retained in-place Bank rewrite and rejected Option 3B witnesses;
- Bank Load changes;
- Bank/Scene/Kit/Instrument file format or schema changes;
- omitting LFNs for currently LFN-created 8.3-compatible components;
- changing `FS_FAST_DRAIN_POLL_PASSES`, `SDCARD_BURST_SIZE`, SPI, DMA, or moving
  filesystem work into an ISR;
- cache-size expansion, whole-file staging, compression, pack files, or
  formatting user cards with a different allocation unit;
- new public AsyncFATFS move/copy/replace/unlink APIs; and
- claiming crash-transactional rename/create semantics beyond the ordering and
  final-sync guarantees already described.

## 2026-08-30 implementation record

Gate A is implemented in `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`. The
private reservation state is reused at the existing byte/pointer budget;
`afatfsCreateFile_t`, `afatfsFile_t`, and `afatfsRenameObject_t` remain 144,
188, and 552 bytes, and the linked `afatfs` symbol remains 6,984 bytes. The
clean logging-on link reports `text=383,956`, `data=404`, and `bss=96,160`.
Gate B remains deferred: appended directory clusters still receive complete
zero-fill and `.`/`..` initialization.

The implementation has passed the clean ARM build, static assertions,
`git diff --check`, and source-order review. Native/media fixtures, reboot and
remount checks, host FAT checking, product compatibility, and repeatable Bank
timing have not been run in this workspace, so Gate-A acceptance is not being
claimed yet. Do not treat the projected timing reductions as measurements.
