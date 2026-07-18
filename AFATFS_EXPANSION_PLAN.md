# asyncfatfs Expansion Plan

This plan is based on the current working implementation in
`Core/Hardware/SD/asyncfatfs/`, its callers in `filesystem.c`, and
`knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`. It replaces
the earlier aspirational outline, which listed several declarations and
dispatcher cases as though they were implemented.

The objective is a truthful, testable async API for exact-object operations,
parent-relative creation, recursive copy/move, and crash-recoverable object
replacement. Product naming and slot-selection policy remain in
`filesystem.c`; asyncfatfs owns FAT/VFAT objects and bounded asynchronous disk
work.

Every implementation change must receive an adjacent contract block in both
`.c` and `.h` describing what it does, why it exists, inputs, outputs, lifetime
rules, and affiliated operations/callers. Each phase must compile and pass its
own tests before the next phase starts.

## Implementation Log

### 2026-07-18 — implementation baseline

- Re-audited the declarations, dispatcher, create/open state machine, object
  iterator, rename/remove coordinators, native delete, and `filesystem.c`
  staging callers before modifying code.
- Confirmed that `afatfs_moveObject()` is currently unsafe: it accepts a job
  into an uninitialized/recycled file slot and dispatches forever to an empty
  continuation. Copy and replace are also represented by no-op dispatcher
  states, while their public APIs are either undefined or cannot satisfy their
  advertised asynchronous contract.
- Confirmed that parent-relative create/open should reuse
  `afatfsCreateFile_t`, but the operation must retain and exclusively own the
  supplied parent handle until completion. The create scanner, directory-entry
  allocator, directory extension, and `.`/`..` initializer all currently bind
  directly to `afatfs.currentDirectory`.
- Implementation order for this pass: make placeholder APIs fail at build time,
  complete result/object metadata, add retained-parent create modes and child
  APIs, build and measure, then proceed to by-identity mutators and multi-handle
  coordinators only on that verified foundation.

### 2026-07-18 — Phases 1-4 implementation progress

- Removed the public declarations, file-operation enum members, union storage,
  dispatcher cases, start function, and empty continuations that represented
  move/copy/replace as accepted per-file operations without a callback path.
  Cross-parent move was subsequently reintroduced only after it had a real
  global coordinator path; copy and replace remain unavailable at compile time.
- Extended structured results with no-space, stale-object, depth-limit,
  recovery-required, and corrupt-directory outcomes. `afatfsObjectId_t` now
  carries its parent form and raw 11-byte SFN fingerprint, populated by every
  successful object iteration.
- Added a media-backed object validator. Native delete now validates its root
  before mutation, bounds traversal to `AFATFS_TREE_DEPTH_MAX`, validates `..`
  records and cluster ranges, rejects self-parent links, and correctly rebinds
  to FAT16's fixed root directory during ascent.
- Implemented explicit `CREATE_NEW`, `OPEN_EXISTING`, and `CREATE_OR_OPEN`
  behavior in the shared create state machine. Access mode `w` still controls
  truncation only after policy resolution; whole-object replacement is not
  hidden inside create.
- Implemented `afatfs_fopenChild()`, `afatfs_openDirChild()`, and
  `afatfs_createDirChild()`. The selected parent is retained exclusively across
  scans, directory extension, append seek, write truncation, and new-directory
  `.`/`..` initialization. Close/chdir and other current-directory coordinators
  now respect that ownership count.
- Raised `AFATFS_MAX_OPEN_FILES` from 3 to 5. The original audited `afatfs`
  symbol was `0x1a20` (6688) bytes; after the parent/result/identity/move work it
  is `0x1e7c` (7804) bytes, an increase of 1116 bytes. Total firmware BSS is
  363836 bytes in the current build.
- Added `afatfs_renameObjectAt()` using parent-relative stale validation and
  structured results. The legacy name-based rename now delegates to the same
  retained-parent coordinator behavior.
- Implemented `afatfs_moveObject()` in that global coordinator. It performs a
  bounded destination-ancestry walk for directory self/descendant rejection,
  allocates and writes the destination run before retiring the source, updates
  a moved directory's `..` cluster, and copies all async inputs into state.
- A clean build was required after the public object-ID size changed because the
  Makefile does not track header dependencies; an incremental link exposed stale
  caller objects through LTO `memset` overflow diagnostics. Clean `make -j4`
  then completed successfully. This build-system limitation must be remembered
  for every later public-structure change.
- Final verification for this pass: `make img` completed and regenerated
  `build/LXRV2_lxr02.img`; `git diff --check` passed. The new signed/unsigned
  entry-run checks compile cleanly. Remaining compiler diagnostics are the
  pre-existing unused `eraseCount`, newlib syscall stubs, and serial-LTO notices.

### 2026-07-18 — Phases 5-6 reduced-RAM implementation

- Added `afatfs_copyObjectTree()` as a global exact-object coordinator. It uses
  a 96-byte stream buffer, one live LFN finder, and eight 28-byte compact
  frames; ascent reconstructs/rescans parents to the saved sector/index/raw-SFN
  key. The destination is always CREATE_NEW, supported FAT metadata is copied,
  and partial output is retained on failure.
- Added asynchronous `beginTreeReplace`, `commitTreeReplace`,
  `abortTreeReplace`, and explicit known-parent recovery. The small opaque
  transaction coexists with copy while staging is populated; commit/recovery
  execution overlays copy in the global tree workspace.
- Added alternating `AFATJ0.SYS`/`AFATJ1.SYS` 69-byte records with magic,
  version, sequence, state, object kind, nonce, canonical target, and CRC-32.
  Commit syncs PREPARED, OLD_RENAMED, PROMOTED, and CLEAN in order, using
  by-identity same-parent rename and exact native delete.
- Recovery handles torn writes/renames from the highest valid record, rejects
  equal-sequence ambiguity and invalid targets, reports unauthorised scratch as
  RECOVERY_REQUIRED, and never recursively guesses at arbitrary names. Commit
  also blocks an unfinished prior journal and preflights its nonce-old scratch.
- `afatfs_destroy(false)` now drains a live tree coordinator or aborts an idle
  begun transaction before closing ordinary handles.
- The final handle audit raised the pool from five to six: a nested file copy
  simultaneously owns two caller parents, two directory cursors, and two file
  streams. The shared transfer buffer was reduced from 128 to 96 bytes to
  offset part of the required 368-byte handle.
- ARM ABI measurements: frame 28, journal 69, transaction 68, replace arm 340,
  copy arm 540, shared workspace 640, final `afatfs` 8,880 (`0x22b0`) bytes.
  The Phase 4 baseline was 7,804 bytes, so both phases add 1,076 bytes and remain
  120 bytes below the 9 KB target.
- Final verification: clean `make -j4` completed, followed by a handle-budget
  rebuild with text 367,752, data 408, BSS 364,908; `make img` regenerated the
  368,160-byte packaged payload; and
  `git diff --check` passed. Hardware and deterministic power-cut injection
  remain pending and are required before Phase 7 product migration.

### Next implementation boundary

- Phase 5 recursive tree copy and Phase 6 journaled replacement now have real
  reduced-RAM global coordinators and public callback APIs. They were not added
  to the per-file dispatcher.
- Product Bank migration is no longer blocked on missing primitives, but Phase
  7 remains intentionally separate: the existing manual `filesystem.c` staging
  flow stays production until copy/replace pass hardware and injected-power-loss
  tests.

---

## Non-Negotiable Driver Invariants

1. `afatfs_poll()` remains single-context and foreground-pumped. No new API may
   wait synchronously for SD I/O.
2. Returning `false` from a start function means no operation was accepted and
   no callback will occur. Once a function returns `true`, its callback must
   occur exactly once on every terminal path.
3. Operation inputs that must survive the initiating call—names, object IDs,
   policies, and callbacks—must be copied into operation-owned state. Do not
   retain pointers to caller scratch strings.
4. A recycled `openFiles[]` slot must pass through `afatfs_initFileHandle()`
   before use. Terminal cleanup must release cache locks/retains, reset the
   handle, and only then invoke the callback.
5. LFN-aware scanning always stores a complete `afatfsObjectFinder_t`. Never
   cast an `afatfsFinder_t` to the larger object-finder type.
6. A retained cache sector must be released before a cursor handle is rebound
   to another file or directory cluster.
7. `AFATFS_OPERATION_FAILURE` is not end-of-file or end-of-directory.
8. Product code selects the parent and the product object to modify. Low-level
   code must not scan unrelated roots or infer Kit/Scene/Bank slot identity.
9. A close callback does not imply persistence. Rename/promotion workflows
   must include explicit `afatfs_sync()` barriers.
10. FAT cannot make two arbitrary LFN renames atomically. Replacement is
    therefore described as **crash-recoverable staged promotion**, not atomic
    replacement.

---

## Audited Status of the Current Tree

| Capability | Current status | Required action |
|---|---|---|
| Structured result enum/callback | In progress. Child open/create, by-identity rename/move, and native delete now report structured results; legacy current-directory rename/remove remain callback-compatible. | Add result variants only as new callers migrate; do not break legacy save state machines. |
| Object identity | Implemented for current mutators. Iteration records parent form/raw SFN and delete/rename/move validate the fingerprint before mutation. | Reuse the validator in copy/recovery; add fault-injection coverage for slot reuse. |
| LFN object iteration | Implemented. `afatfs_findFirstObject(directory, ...)` is already parent-relative because it receives a directory handle. | Do not add the redundant declared `afatfs_findFirstObjectInDir()`. Document and use the existing API. |
| Name policy | Implemented. `fat_lfnCharAllowed()`, `fat_lfnSanitizeChar()`, display comparison, whole-component sanitization, trailing-space/period stripping, and alias generation exist. | Keep the current policy. `?` intentionally sanitizes to `_`; do not add it without a product-level policy decision. |
| Explicit create modes | Implemented in the shared create/open scanner. Access-mode truncation is preserved independently from create policy. | Migrate new callers to explicit policy; retain legacy wrappers. |
| Parent-relative create/open | Implemented with exclusive parent lifetime across scan, extension, append seek, truncation, and directory initialization. | Exercise with concurrent source/destination handles on hardware. |
| Exact recursive delete | Implemented with root validation, bounded depth, structural `..` validation, and FAT16-root ascent. | Add dedicated flat/nested/corrupt-media tests before broader product use. |
| Same-parent rename | Implemented both as legacy name lookup and `afatfs_renameObjectAt()` with explicit parent, stale validation, and structured result. | Migrate promotion callers to by-identity form. |
| Cross-parent move | Implemented through the global rename coordinator with bounded descendant checks, destination-first durable ordering, and directory `..` update. | Add failure-injection and FAT16/FAT32 hardware coverage. |
| Tree copy | Implemented as a global coordinator with exact-root validation, CREATE_NEW destination semantics, 96-byte streaming, metadata preservation, compact depth frames, source-parent rescans, optional durable sync, and structured completion. | Complete host/mock and hardware byte-comparison/failure tests before product migration. |
| Transactional replace | Implemented through async begin/commit/abort with a small opaque transaction, nonce scratch names, two alternating CRC journal slots, exact-object rename/delete, and durable PREPARED/OLD_RENAMED/PROMOTED/CLEAN ordering. | Exercise every cut point and collision on FAT16/FAT32 before product migration. |
| Transaction recovery | Implemented as explicit known-parent recovery. It validates target components/CRC/sequence, never deletes unauthorised scratch, and follows state-specific promote/restore cleanup. Mount remains product-agnostic. | Invoke from Phase 7 product-parent preflight only after fault-injection coverage. |
| Persistence barrier | Implemented as `afatfs_sync()`. | Use it explicitly in copy/replace promotion phases. |
| Product-level Bank staging | Partially implemented in `filesystem.c` using `tmp...`/`old...` names and two same-parent renames. It is not journaled and does not yet preserve untoggled Scenes by copying them. | Migrate only after copy and recoverable replace are proven. |

### Immediate API-truthfulness correction

Before adding features, the first implementation commit must stop advertising
operations that accept work and then hang:

- remove or compile-gate `afatfs_moveObject()` until its continuation exists;
- remove `AFATFS_FILE_OPERATION_MOVE_OBJECT`, `COPY_TREE`, and `REPLACE_TREE`
  no-op dispatcher cases until real state machines land;
- remove header declarations that have no definition;
- update `ASYNCFATFS_REFERENCE.md`, which still says recursive deletion does
  not exist even though `afatfs_deleteTree()` is now a production caller.

This cleanup changes no card behavior. It ensures an unavailable feature fails
at build time instead of becoming another timeout on hardware.

---

## Architectural Placement

### Per-handle operations

Operations that naturally own one open file handle remain in
`afatfsFileOperation_t`: open/create, seek, close, truncate, directory
extension, and native delete traversal.

### Global coordinators

Operations that must coordinate several handles or invoke other async
operations must use dedicated state in `afatfs_t`, following the existing
`renameObject` and `removeObjects` model:

- cross-parent move;
- recursive copy;
- replace transaction and recovery.

They must not be forced into one handle's operation union. A coordinator needs
to open/close child handles and start sub-operations without overwriting the
state that remembers the outer operation. The first native delete failure
demonstrated why union ownership must be explicit.

### Handle budget

`AFATFS_MAX_OPEN_FILES` is now 6. The final Phase 5 audit proved that nested
copy can legitimately need source parent, destination parent, active source and
destination directory cursors, plus source and destination file streams at the
same time. Five handles passed flat-tree reasoning but would retry forever when
allocating the nested destination file. Each handle is 368 bytes; the 96-byte
shared transfer buffer and compact traversal state keep the complete `afatfs`
singleton at 8,880 bytes despite the sixth slot. Do not silently reuse or copy a
busy `afatfsFile_t`.

All parent-relative start functions must state that parent handles remain open
and unmodified until completion. `filesystem.c` already serializes one product
operation, so that lifetime rule is practical and auditable.

---

## Phase 1 — Complete Results and Object Identity

### 1.1 Structured completion variants

Keep legacy callbacks for existing callers while adding result-bearing types:

```c
typedef void (*afatfsOpenResultCallback_t)(afatfsResultCode_t result,
                                           afatfsFilePtr_t file);
typedef void (*afatfsObjectResultCallback_t)(afatfsResultCode_t result,
                                             const afatfsObjectId_t *object);
```

Extend `afatfsResultCode_t` only for conditions the driver can distinguish:

- `AFATFS_RESULT_NO_SPACE`;
- `AFATFS_RESULT_STALE_OBJECT`;
- `AFATFS_RESULT_DEPTH_LIMIT`;
- `AFATFS_RESULT_RECOVERY_REQUIRED`.

Start-time resource contention remains a `false` return with no callback; it is
not an async result. Unsupported input characters continue to sanitize to `_`,
so `UNSUPPORTED_NAME` must not be emitted unless a future strict-name mode is
explicitly introduced. An empty component after trimming is `INVALID_NAME`.

Create one terminal helper per operation family. Each helper must copy callback
and output data, release iterator/cache/handle ownership, clear active state,
then callback exactly once.

### 1.2 Complete the object capability

Add the source parent to `afatfsObjectId_t`:

```c
uint32_t parentFirstCluster;
uint8_t parentIsFat16Root;
uint8_t rawShortName[FAT_FILENAME_LENGTH];
```

`afatfs_findNextObject()` can populate these from the directory handle and the
SFN it has just parsed. The raw SFN key plus `sfnEntry`, kind, first cluster,
and parent identity form the validation fingerprint.

Object IDs are **short-lived capabilities**, not permanent inode numbers. They
remain valid only while their parent directory has not been mutated. Every
by-identity mutator must reload the SFN entry at `sfnEntry` and verify:

- it is not deleted, empty, an LFN fragment, or a volume label;
- raw SFN bytes still match;
- kind/attributes and first cluster still match;
- the entry belongs to the expected parent.

Mismatch returns `STALE_OBJECT` without modifying the card.

### 1.3 Direct-object primitives

Add internal helpers used by later phases:

- validate an object ID asynchronously;
- open a file/directory from a validated ID without a name scan;
- retire one validated name run;
- copy an SFN metadata entry while replacing only name/cluster fields that the
  operation explicitly owns.

These helpers must retain the current sector only for the phase that consumes
it; no raw cache pointer may survive a poll boundary.

### Phase 1 verification

- stale ID after rename/delete returns `STALE_OBJECT`;
- duplicate display LFNs remain distinguishable by physical ID;
- wrong-kind identity returns `NOT_FILE`/`NOT_DIRECTORY`;
- every accepted operation produces exactly one callback under injected read
  failure.

---

## Phase 2 — Explicit Create Modes and Parent-Relative Open/Create

### 2.1 Correct the create policy

Revise the unused enum before exposing it. File truncation is an access-mode
decision, not a create policy, so remove `AFATFS_REPLACE_FILE`:

```c
typedef enum {
    AFATFS_CREATE_NEW,
    AFATFS_OPEN_EXISTING,
    AFATFS_CREATE_OR_OPEN
} afatfsCreateMode_t;
```

Required behavior during `AFATFS_CREATEFILE_PHASE_FIND_FILE`:

| Policy | Existing same-display object | Missing object |
|---|---|---|
| `CREATE_NEW` | `ALREADY_EXISTS`, including wrong-kind collision | Create |
| `OPEN_EXISTING` | Open only if kind matches; otherwise typed error | `NOT_FOUND` |
| `CREATE_OR_OPEN` | Open only if kind matches | Create |

`"w"` may truncate the opened file after policy resolution, preserving current
file access behavior. Directory APIs never truncate. Replacement of a whole
object is handled by remove/transaction composition, not hidden inside create.

### 2.2 Replace the incomplete child declarations

Use APIs that carry parent, access, policy, matching, alias output, and a
structured result. Exact spelling may be adjusted during implementation, but
the complete information must be present:

```c
bool afatfs_fopenChild(afatfsDirHandle_t parent,
                       const char *displayName,
                       const char *accessMode,
                       afatfsCreateMode_t createMode,
                       afatfsMatchMode_t matchMode,
                       char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                       afatfsOpenResultCallback_t cb);

bool afatfs_openDirChild(afatfsDirHandle_t parent,
                         const char *displayName,
                         afatfsMatchMode_t matchMode,
                         char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                         afatfsOpenResultCallback_t cb);

bool afatfs_createDirChild(afatfsDirHandle_t parent,
                           const char *displayName,
                           afatfsCreateMode_t createMode,
                           afatfsMatchMode_t matchMode,
                           char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                           afatfsOpenResultCallback_t cb);
```

Do not implement `afatfs_findFirstObjectInDir()`: the existing object iterator
already accepts its parent handle.

### 2.3 Fit into the existing create state machine

Add to `afatfsCreateFile_t`:

- retained parent handle;
- create policy;
- structured result callback/adaptation state;
- terminal result;
- copied sanitized display component and generated alias state (already
  present; continue using it).

Replace create-path uses of `&afatfs.currentDirectory` with the retained parent
handle. In particular:

- initial scan and alias collision scan;
- directory-entry allocation;
- parent directory extension;
- the newly-created-directory handoff that writes `.` and `..`.

Reject `.` and `..` in child APIs. Validate that parent is a non-busy directory
handle before accepting the request. Copy every caller string during start.

Legacy `afatfs_fopen[_lfn]`, `mkdir[_lfn]`, and `opendir[_lfn]` become thin
wrappers using `&afatfs.currentDirectory` and callback adapters. Product code
can migrate incrementally.

### Phase 2 verification

- create-new collision, open-existing miss, and kind mismatch return distinct
  results;
- two independent parent handles can create children without `chdir()`;
- parent directory extension still starts at entry zero in the new cluster;
- created subdirectories have correct `.` and `..` on FAT16 and FAT32;
- parent cannot be closed/reused before callback (assert in debug builds and
  document as caller contract);
- map file records the open-handle pool RAM increase.

---

## Phase 3 — Harden Native Recursive Delete

The primary implementation exists. This phase promotes it from a Kit overwrite
primitive to a verified general tree operation.

Required additions:

1. Validate the root object ID before the first mutation.
2. Validate structural `..` entries before ascending: directory attribute,
   legal cluster range, and no self-parent except a real filesystem root.
3. Add a bounded visited-cluster/depth guard for corrupt directory cycles.
   Product trees need only a small depth, but failure must return
   `DEPTH_LIMIT`/`CORRUPT_LFN_RUN` rather than poll forever.
4. Preserve the current safe visibility ordering: retire a name before freeing
   its chain. A power loss may leak clusters, but it must not leave a live name
   pointing at clusters already returned to the free pool.
5. Keep one-cluster-per-poll FAT freeing. Let the shared cache combine dirty FAT
   sector writes; do not add an unbounded batch loop.
6. Expose diagnostic phase without coupling product code to private enum values;
   a structured diagnostic snapshot is preferable to another raw phase number.

### Phase 3 verification

- empty, flat Kit, nested Scene, and maximum supported depth;
- empty files (`firstCluster == 0`) and multi-cluster files;
- directory spanning multiple sectors/clusters;
- checksum-valid LFN, SFN-only, duplicate display LFN, and stale/corrupt LFN;
- FAT16 and FAT32;
- injected failure in scan, entry retirement, FAT read, and FAT write;
- callback and cache retain counts return to baseline on every exit.

---

## Phase 4 — Parent-Relative Rename and Cross-Parent Move

### 4.1 Refactor same-parent rename first

Create a structured by-identity core:

```c
bool afatfs_renameObjectAt(afatfsDirHandle_t parent,
                           const afatfsObjectId_t *source,
                           const char *newDisplayName,
                           afatfsMatchMode_t collisionMode,
                           char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                           afatfsResultCallback_t cb);
```

Reuse the current LFN run sizing, alias generation, collision scan, directory
extension, metadata copy, and old-run retirement. Replace the source display
scan with identity validation. The existing name-based
`afatfs_renameObject_lfn()` remains a compatibility wrapper that scans once and
delegates.

### 4.2 Implement cross-parent move

`afatfs_moveObject()` must copy its destination name and all identities into a
global `afatfsMoveObject_t`; the current stub's retained `const char *` is not
safe for an async operation.

Move phases:

1. validate source identity and both parents;
2. reject destination collision and moving a directory into itself or one of
   its descendants (walk destination `..` ancestry);
3. reserve/write the destination LFN/SFN run using source metadata and cluster;
4. if a directory crosses parents, update its structural `..` entry to the
   destination parent;
5. retire the source LFN/SFN run;
6. release both parents/coordinator state and callback.

The destination entry is written before the source is retired. A crash can
temporarily leave two names for one cluster, but must not lose the only live
name. Transaction recovery handles promotion-level duplicates; general move
reports I/O failure and leaves repair to a desktop filesystem check.

Cross-volume move is out of scope and returns `UNSUPPORTED_LAYOUT`.

### Phase 4 verification

- same-parent case-only and longer/shorter LFN rename;
- alias collision and existing destination;
- file and directory cross-parent move;
- correct `..` after directory move;
- reject move into self/descendant;
- injected failure before/after destination write and before source retirement;
- source object ID becomes stale after success.

---

## Phase 5 — Recursive Tree Copy

**Implementation status: code complete; hardware/fault-injection verification pending.**

Tree copy is a coordinator, not a file-handle operation. Add one
`afatfsCopyTree_t` to `afatfs_t` and poll it after ordinary file handles, like
the existing rename/remove coordinators.

### 5.1 Bounded state

- `AFATFS_TREE_DEPTH_MAX`: explicit product-supported depth, initially 8;
- one compact traversal frame per depth containing only source/destination
  parent clusters and the physical SFN resume key; after ascent the source
  parent is rescanned to that key rather than retaining full object/finder
  records at every level;
- one 96-byte copy buffer; `afatfs_fread()`/`afatfs_fwrite()` already support
  partial-sector progress, and slower user-initiated save/copy work is preferred
  to another 512-byte permanent allocation;
- one complete LFN finder only for the active directory; it is explicitly
  released before every rebind or ascent rescan;
- source and destination file handles opened only while streaming one file;
- copied destination name and create policy in operation-owned storage.

Copy execution storage shares a union with replace commit/recovery execution
storage. A small transaction descriptor is separate because callers may use
tree copy while a begun transaction's staging directory is being populated.
The implementation target is a final `afatfs` symbol no larger than 9 KB (the
Phase 4 baseline is 7,804 bytes).

### 5.2 Copy phases

1. validate source and destination parent;
2. create destination root with `CREATE_NEW`;
3. scan one source object;
4. for a directory, create its destination child, push a frame, and descend;
5. for a file, create destination file, stream bounded `fread()`/`fwrite()`
   chunks, close both handles, then continue scanning;
6. on directory exhaustion, close/release the frame and ascend;
7. complete only after every handle is closed. Persistence remains a separate
   `afatfs_sync()` boundary unless the caller requests a durable-copy option.

Copy file data byte-for-byte and preserve supported SFN metadata (attributes,
timestamps, logical size through normal close). Do not copy `.`/`..`, volume
labels, deleted entries, or orphan LFN fragments.

On failure, return the exact result and the identity/name of the partial
destination when available. Do not recursively erase it inside the error path;
a transaction can abort it, while a diagnostic caller may need it preserved.

### Phase 5 verification

- empty directory, flat Kit, full nested Scene, and multi-Scene Bank subset;
- zero-byte and multi-cluster files with byte comparison after remount;
- depth limit, no-space, destination collision, and source read failure;
- no handle leak when `fread()` or `fwrite()` returns zero while not at EOF;
- source tree unchanged after copy;
- bounded poll work under the existing audio render loop.

---

## Phase 6 — Crash-Recoverable Object Replace

**Implementation status: code complete; hardware/fault-injection verification pending.**

The old synchronous-looking `beginTreeReplace(..., tx_out)` signature is
invalid: creating a staging directory requires async I/O, so a usable
transaction cannot be returned synchronously. Replace it with an opaque async
transaction callback:

```c
typedef struct afatfsReplaceTransaction *afatfsReplaceTransactionPtr_t;

typedef void (*afatfsReplaceBeginCallback_t)(
    afatfsResultCode_t result,
    afatfsReplaceTransactionPtr_t transaction,
    afatfsDirHandle_t stagingDirectory);

bool afatfs_beginTreeReplace(afatfsDirHandle_t parent,
                             const char *targetDisplayName,
                             afatfsReplaceBeginCallback_t cb);
bool afatfs_commitTreeReplace(afatfsReplaceTransactionPtr_t transaction,
                              afatfsResultCallback_t cb);
bool afatfs_abortTreeReplace(afatfsReplaceTransactionPtr_t transaction,
                             afatfsResultCallback_t cb);
bool afatfs_recoverTreeReplace(afatfsDirHandle_t parent,
                               afatfsResultCallback_t cb);
```

Only one replace transaction may be active initially, matching
`filesystem.c`'s serialized operation model.

### 6.1 Scratch namespace

The implemented reserved names are `.afat-xxxxxxxx-new` and
`.afat-xxxxxxxx-old`, where `xxxxxxxx` is the transaction's eight-digit
lowercase hexadecimal nonce. Product scanners reject these non-numbered names.
Always create scratch with `CREATE_NEW`; nonce collisions advance to another
pair, and an existing scratch directory is never adopted by a new transaction.

### 6.2 Journal

Two same-parent journal slots, `AFATJ0.SYS` and `AFATJ1.SYS`, store alternating
packed 69-byte records with magic, version, sequence, state, object kind, nonce,
target display component, and CRC. New/old scratch names are regenerated from
the nonce instead of consuming two more permanent component buffers. Recovery
selects the highest-sequence valid record; a torn newer record does not destroy
the older valid state.

Journal states:

- `PREPARED`: staging tree is complete and synced; target is still authoritative;
- `OLD_RENAMED`: old target is under the reserved old name and staging must be
  promoted (or old restored if staging is missing);
- `PROMOTED`: target is the staged object; old scratch may be deleted;
- `CLEAN`: old scratch and journal may be removed.

### 6.3 Commit ordering

1. require all caller-owned staging child handles closed;
2. `afatfs_sync()` the staged payload;
3. write/sync `PREPARED` journal;
4. rename existing target by identity to old scratch, if present;
5. sync, write/sync `OLD_RENAMED`;
6. rename staging by identity to target;
7. sync, write/sync `PROMOTED`;
8. delete exact old scratch asynchronously and sync;
9. write/sync `CLEAN`;
10. report commit success after cleanup and the CLEAN persistence boundary.

The completed implementation deliberately waits through cleanup instead of
reporting an early user-visible success. This keeps callback ownership simple
for the first production integration; a future optimization may split durable
promotion from background cleanup only if it adds a separate observable state
and preserves the same recovery rules.

### 6.4 Recovery rules

Recovery is explicitly invoked on known product parents after mount and before
their browser scans. `afatfs_init()` must not recursively search arbitrary card
directories or guess which user names are scratch objects.

- valid `PREPARED` + intact target: abort/delete staging;
- valid `OLD_RENAMED` + staging present: promote staging;
- valid `OLD_RENAMED` + staging missing + old present: restore old target;
- valid `PROMOTED` + target present: delete old/staging leftovers;
- corrupt/no valid journal: leave unknown objects untouched and return
  `RECOVERY_REQUIRED` for product-level diagnostics.

### 6.5 Abort rules

Before commit begins, abort deletes only the transaction's exact staging
identity. Once target retirement has begun, abort delegates to recovery rules;
it must not blindly delete old or target objects.

### Phase 6 verification

Inject power loss after every journal write, sync barrier, rename, and delete.
After remount/recovery, assert:

- exactly one authoritative target is loadable;
- it is either the complete old tree or complete new tree, never a merged tree;
- no numbered product slot resolves to scratch data;
- repeated recovery is idempotent;
- corrupt/torn journal records never trigger deletion of an unrelated object.

---

## Phase 7 — Product Integration

Do not remove the current `filesystem.c` save paths until each replacement has
round-tripped on hardware.

### Bank Save

1. Open `/Bank` once as the transaction parent and run recovery before scan.
2. Begin staged replacement for the selected numbered Bank.
3. If an old Bank exists, copy every untoggled Bank-local Scene child into the
   staging Bank using exact object IDs.
4. Serialize selected/toggled resident Scenes into staging with parent-relative
   child APIs.
5. Write `bankset.bcg`, close all handles, and commit.
6. Remove the existing manual phases 39–50 only after journaled promotion is
   proven.

The 16-Scene resident workspace and selected-child Bank load/save now exist,
but the current manual temp/old writer serializes only the selected mask into
the new tree. This is the first transaction consumer because tree copy is
required to preserve untoggled old Bank-local Scenes instead of leaving them
only in the displaced `oldNNN-xxxx` folder.

### Scene and Kit Save

- Keep exact same-slot selection in `filesystem.c`.
- Migrate Scene to staged replace after Bank proves nested-tree recovery.
- Kit may keep delete-and-recreate temporarily; migrate when power-loss-safe
  Kit commits are desired.
- Product caches are updated only after commit and final sync succeed.

### Autosave and file promotion

The journal schema includes object kind so a later file-staging variant can
reuse the replace coordinator for `.tmp` autosave files. Do not implement
autosave promotion by assuming two LFN renames are atomic.

---

## Verification Infrastructure

### Host/mock tests

Add a mock block device with deterministic async completion and fault injection
at sector read/write boundaries. Required assertions include:

- maximum work per poll is bounded;
- accepted operations callback exactly once;
- no open handle, cache lock, or retain-count leak;
- FAT chains contain no double allocation after successful operations;
- directory scans reconstruct the expected LFN/SFN objects after remount;
- duplicate aliases/display names never redirect by-identity operations.

### Firmware build

For every phase:

1. `make -j4`;
2. `make img`;
3. `git diff --check`;
4. record text/data/BSS changes from `arm-none-eabi-size` and the map file.

### Hardware matrix

- recommended MBR-FAT32 card and supported FAT16 card;
- occupied Kit overwrite;
- nested Scene overwrite;
- Bank replacement preserving untoggled Scenes;
- card removal/error during copy and each commit barrier;
- desktop remount inspection for duplicate LFNs, stale scratch objects, and
  exact file bytes.

---

## Explicitly Out of Scope

- exFAT, FAT12, Unicode input/editing, and cross-volume move;
- a general fsck or orphan-cluster reclamation engine;
- recursive scanning of all user directories during mount;
- blocking I/O or moving `afatfs_poll()` into another context as part of this
  expansion;
- claiming strict atomicity where FAT sector ordering plus journal recovery is
  the actual guarantee.
