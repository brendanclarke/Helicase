# asyncfatfs Reference

Updated: 2026-07-18.

This is the firmware-facing contract for
`Core/Hardware/SD/asyncfatfs/asyncfatfs.c` and `asyncfatfs.h`. It describes the
implemented low-level behavior through expansion Phases 1-6. Product paths,
schemas, slot selection, and save policy live in `FILESYSTEM_SPEC.md`;
implementation history and remaining qualification work live in
`AFATFS_EXPANSION_PLAN.md` and `AFATFS_ADDITIONS_SUMMARY.md`.

## Status And Scope

asyncfatfs is a single-context, foreground-pumped FAT16/FAT32 and VFAT layer.
It owns SD sector caching, FAT chain traversal, directory entry parsing and
creation, file I/O, LFN/SFN name handling, exact object identity, recursive
tree deletion/copy, rename/move, and recoverable staged directory replacement.

It does not support FAT12, exFAT, Unicode editing, cross-volume moves, general
filesystem repair, or recursive mount-time recovery scans. The current staged
replace API accepts directories, not individual files.

The intended layering is:

- Menu and Preset select the musical operation.
- `filesystem.c` selects a product parent and slot, sequences the operation,
  owns browser caches, and decides which objects are in scope.
- `storageTypes.c/h` parses and formats product text schemas.
- asyncfatfs performs component-level FAT/VFAT operations.

Product code must not reconstruct LFN runs, write FAT entries directly, or ask
asyncfatfs to infer product meaning from a filename.

## Execution And Completion Contract

`filesystem_tick()` must continue pumping `afatfs_poll()`. Operations advance
in small asynchronous phases and may need many polls, especially tree copy,
which deliberately trades rescans for lower permanent RAM.

For the structured Phase 1-6 APIs:

- A start function returning `false` accepted nothing and will not call back.
- Returning `true` means the driver copied all transient input it needs and
  will call the completion exactly once.
- An accepted operation retains the parent handles named by its API until the
  callback. The caller must not scan, seek, close, or rebind those handles.
- The driver releases its internal finders, cache ownership, and parent retains
  before invoking the terminal callback.
- Callback-scoped object pointers must be copied before the callback returns.

Legacy open/create APIs report failure with a `NULL` handle rather than a
structured result. Their callback and ownership conventions remain compatible
with existing `filesystem.c` state machines.

File streaming has a separate readiness rule. `afatfs_fread()` or
`afatfs_fwrite()` may return zero because no cache sector is ready yet. A zero
read is EOF only when `afatfs_feof(file)` is also true.

Closing a handle and making media durable are distinct boundaries:

- `afatfs_fclose()` completes handle metadata/ownership teardown.
- `afatfs_flush()` drains dirty cache sectors and waits for an already queued SD
  write callback.
- `afatfs_sync()` is the named persistence boundary and currently wraps the
  same strict `afatfs_flush()` behavior.

A product save must not report durable success merely because its last file
closed. It must either complete `afatfs_sync()` itself or use an operation whose
documented completion includes that sync.

## Components, Paths, And Names

asyncfatfs LFN APIs accept one visible path component. They do not parse `/` or
`\` paths. Legacy code navigates a path one directory at a time through the
current directory; new multi-parent code should use explicit parent-relative
APIs.

Public component bounds are:

- `AFATFS_LONG_FILENAME_MAX`: 48 visible bytes plus NUL in public buffers.
- `AFATFS_SHORT_FILENAME_MAX`: 12 printable 8.3 bytes plus NUL.

LFN component handling sanitizes before matching or creation:

- Supported characters are letters, digits, space, `_ - . ( ) [ ] + = @ # $ %
  & ! '`.
- Unsupported bytes, including separators, become `_`.
- Trailing spaces and periods are removed repeatedly.
- Empty results and structural names such as `.` and `..` are invalid for child
  creation.

The caller should retain the sanitized user-visible component when that name is
part of a schema or UI cache. asyncfatfs separately exposes the printable SFN
alias needed by older short-name reopen paths.

### Display names and short aliases

One VFAT object can have two valid string identities:

- `displayName`: checksum-validated LFN, or the case-preserved SFN display when
  no valid LFN exists.
- `shortName`: printable 8.3 alias derived from the authoritative raw 11-byte
  SFN entry.

Use the identity required by the next API:

- LFN display matching requires `displayName`.
- A legacy short-name open requires `shortName`.
- User-facing product schemas store display components, not generated aliases
  such as `001SLA~1`.

Do not assume the alias will match a host-created LFN. The Bank load failure
recorded as `ERR BnkL06` was caused by enumerating a correct display name,
caching its short alias, and later passing that alias to an LFN display match.

### Match and create policy

`afatfsMatchMode_t` selects display-name matching after sanitization:

- `AFATFS_MATCH_CASE_SENSITIVE` requires the exact component.
- `AFATFS_MATCH_CASE_INSENSITIVE` folds case and is appropriate only when the
  caller intentionally treats host-created case variants as the same product
  name.

`afatfsCreateMode_t` separates existence policy from file access mode:

- `AFATFS_CREATE_NEW`: fail with `ALREADY_EXISTS` on a match.
- `AFATFS_OPEN_EXISTING`: fail with `NOT_FOUND` when absent.
- `AFATFS_CREATE_OR_OPEN`: open a correctly typed match or create when absent.

Tree copy and replacement staging use `CREATE_NEW`; they never merge into an
old partial destination.

## Structured Results

`afatfsResultCode_t` reports terminal causes that callers can distinguish:

| Result | Meaning |
| --- | --- |
| `AFATFS_RESULT_OK` | Operation completed as documented. |
| `NOT_FOUND` | Requested component or exact object is absent. |
| `ALREADY_EXISTS` | Create-new or destination collision. |
| `INVALID_NAME` | Empty, structural, or otherwise invalid component input. |
| `UNSUPPORTED_NAME` | Reserved result for a name representation the driver cannot support. |
| `NOT_EMPTY` | Operation requires an empty directory. |
| `NOT_DIRECTORY` / `NOT_FILE` | Existing object has the wrong kind. |
| `IO_ERROR` | SD/cache/FAT operation failed after acceptance. |
| `CORRUPT_LFN_RUN` | The named LFN/SFN entry run is structurally invalid. |
| `UNSUPPORTED_LAYOUT` | The card/object layout is outside the supported operation. |
| `NO_SPACE` | Allocation failed after the operation began. |
| `STALE_OBJECT` | A saved physical identity no longer matches its directory entry. |
| `DEPTH_LIMIT` | A bounded walker would exceed `AFATFS_TREE_DEPTH_MAX`. |
| `RECOVERY_REQUIRED` | Scratch/journal state is unsafe to infer automatically. |
| `CORRUPT_DIRECTORY` | Structural directory ancestry or `.`/`..` metadata is invalid. |

Result failure and iteration end are different. Callers must not turn a cache
wait or I/O error into a successful end-of-directory/end-of-file result.

## Exact Object Identity

`afatfs_findNextObject()` returns `afatfsObjectInfo_t`, which wraps an
`afatfsObjectId_t`. The ID records:

- object kind, display name, and printable short alias;
- first LFN entry, LFN entry count, and owning SFN entry;
- first cluster, logical size, and FAT attributes;
- source parent cluster or FAT16-root marker;
- the raw 11-byte SFN fingerprint.

This is a short-lived capability, not a permanent database key. A by-identity
mutator reloads the SFN entry and checks the parent, raw name, kind, and first
cluster before changing anything. If the source parent has mutated since the
scan, the caller must rescan; the operation returns `STALE_OBJECT` instead of
silently selecting a same-name replacement.

Any mutation of a parent invalidates previously returned IDs from that parent.
Copy the complete structure when it must survive beyond an iterator callback or
state-machine phase.

## Object Iteration

LFN-aware iteration uses:

- `afatfs_findFirstObject(directory, finder)`
- `afatfs_findNextObject(directory, finder, object)`
- `afatfs_findLastObject(directory, finder)`

The caller must allocate a complete `afatfsObjectFinder_t`. It contains the raw
cursor plus the in-progress VFAT fragment run. Casting an `afatfsFinder_t` to
the larger type overwrites adjacent state and caused the former `TOut06` delete
stall.

The iterator emits ordinary files and directories with validated display and
short identities. Dot-prefixed names are not hidden; product scanners decide
whether to ignore transaction scratch, autosave backers, or other schema-external
objects. Raw `afatfs_findFirst()` / `afatfs_findNext()` remain for legacy code
that intentionally consumes individual FAT records.

Always pair a started object scan with `afatfs_findLastObject()`, including
early exits. This releases retained cache state.

## Open, Create, And Navigation APIs

Legacy current-directory APIs include:

- `afatfs_fopen()` and `afatfs_fopen_lfn()`
- `afatfs_mkdir()` and `afatfs_mkdir_lfn()`
- `afatfs_opendir()` and `afatfs_opendir_lfn()`
- `afatfs_chdir()`, `afatfs_chdir(NULL)`, and `afatfs_chdirParent()`

`afatfs_chdir(NULL)` selects the filesystem root. `afatfs_chdirParent()` reads
the structural FAT `..` entry and must be used instead of feeding the literal
`".."` through the ordinary component parser.

Parent-relative APIs are:

- `afatfs_fopenChild(parent, display, access, createMode, matchMode, alias, cb)`
- `afatfs_openDirChild(parent, display, matchMode, alias, cb)`
- `afatfs_createDirChild(parent, display, createMode, matchMode, alias, cb)`

They resolve one component without changing the global current directory and
are the required basis for copy/replace graphs with concurrent source and
destination parents.

A newly created directory is not returned until asyncfatfs has allocated its
first cluster, updated the parent SFN entry, zero-filled the new directory
cluster, and created valid `.` and `..` entries. Ordinary files may allocate
their first cluster lazily on first write.

VFAT creation may need several contiguous directory entries. When an existing
`0x00` directory terminator cannot fit the entire new LFN/SFN run, the skipped
terminator is converted to a deleted entry before scanning continues; otherwise
later readers would stop before the new object. When a subdirectory extends,
the new cluster is scanned from entry zero.

## Remove And Recursive Delete

Removal APIs include:

- `afatfs_funlink(file, cb)` for one opened file.
- `afatfs_removeObjects_lfn(display, matchMode, mode, cb)` for every matching
  current-directory object.
- `afatfs_removeObject(shortName, mode, cb)` for one exact printable SFN alias.
- `afatfs_deleteTree(objectId, resultCb)` for one exact directory capability.

`AFATFS_REMOVE_FILES_ONLY` removes matching files and advances past matching
directories. `AFATFS_REMOVE_EMPTY_DIRECTORIES` retires a matching directory only
after the caller has made it empty. The display-name remover restarts its scan
after every deletion because each LFN/SFN retirement mutates its directory.

`afatfs_deleteTree()` validates the exact root and recursively deletes files and
subdirectories without re-resolving the root by display text. It uses a complete
LFN finder, validates directory ancestry, stops at eight child levels, retires
name runs, releases cluster chains, and guarantees terminal ownership cleanup.
`afatfs_getDeleteTreePhase()` is a diagnostic phase accessor for higher-level
timeout reporting.

The product layer still chooses deletion scope. It must open the intended
`/Kit`, `/Scene`, or `/Bank` parent, parse only immediate child display names,
and pass the exact selected ID. A broad recursive delete from root is never a
valid slot-overwrite strategy.

## Rename And Move

Rename/move APIs are:

- `afatfs_renameObject_lfn(oldDisplay, newDisplay, matchMode, aliasOut, cb)`
- `afatfs_renameObjectAt(parent, sourceId, newDisplay, matchMode, aliasOut, cb)`
- `afatfs_moveObject(sourceParent, sourceId, destinationParent,
  destinationDisplay, matchMode, aliasOut, cb)`

Prefer the explicit-parent by-identity forms after enumeration. Same-parent
rename rewrites the complete LFN/SFN name run while preserving cluster, size,
attributes, timestamps, and children.

Cross-parent move creates and syncs the destination entry run before retiring
the source. A failure can therefore leave two names, but must not erase the only
live name. A moved directory's structural `..` entry is updated and synced.
Self/descendant directory moves are rejected through a bounded ancestry walk.

No API should retain a caller-owned component pointer for later polling. The
implemented coordinators copy names and object identities into operation-owned
storage when they accept the request.

## Recursive Tree Copy

`afatfs_copyObjectTree(sourceParent, sourceId, destinationParent,
destinationName, flags, resultCb)` copies one exact file or directory tree.

The operation:

1. validates the source ID against `sourceParent`;
2. creates the destination root with `CREATE_NEW`;
3. copies file bytes without product parsing;
4. recreates directories, skipping structural `.`/`..`, volume labels,
   deleted entries, and orphan LFN fragments;
5. preserves supported FAT attributes and timestamps;
6. closes every internal handle before completion;
7. runs a final sync only when `AFATFS_COPY_DURABLE` is set.

`AFATFS_COPY_DEFAULT` is intended for building a replace transaction's staging
tree, because commit performs the required staged-payload sync. A standalone
copy that must survive immediate power loss should use `AFATFS_COPY_DURABLE`.

Copy uses one 96-byte stream buffer, one active complete LFN finder, and eight
28-byte traversal frames. On ascent it reconstructs source/destination cursors
and rescans the source parent to a saved physical SFN key. The rescan makes
large or deeply nested saves slower but avoids storing full finder/object state
for every level.

Six open-file slots cover the worst nested-file point: the two caller parents,
active source/destination directory cursors, and source/destination stream
files. Exhaustion or any other terminal error leaves the partial destination in
place. The transaction owner may abort it; a diagnostic caller may inspect or
delete it explicitly.

## Crash-Recoverable Directory Replace

The transaction APIs are:

- `afatfs_beginTreeReplace(parent, targetDisplayName, beginCb)`
- `afatfs_commitTreeReplace(transaction, resultCb)`
- `afatfs_abortTreeReplace(transaction, resultCb)`
- `afatfs_recoverTreeReplace(parent, resultCb)`

Only one transaction may be active. Begin creates a guaranteed-new
`.afat-xxxxxxxx-new` directory in the supplied parent, where `xxxxxxxx` is an
eight-digit nonce. A collision advances the nonce; existing scratch is never
adopted. On success the begin callback receives a driver-owned opaque
transaction and an open staging directory. The caller may populate that
directory through parent-relative create/copy APIs and must close all staging
children before commit.

### Journal format and namespace

Two same-parent files alternate records:

- `AFATJ0.SYS`
- `AFATJ1.SYS`

Each packed 69-byte version-1 record contains magic, state, object kind,
sequence, nonce, a 49-byte target component buffer, and reflected CRC-32. The
highest-sequence valid record is authoritative. Equal-sequence records with
different valid bytes are ambiguous and require manual/product-level handling.

The nonce derives both reserved names:

- `.afat-xxxxxxxx-new`: complete staging candidate.
- `.afat-xxxxxxxx-old`: prior live target after retirement.

The current record validator accepts only `AFATFS_OBJECT_DIRECTORY`. The object
kind field preserves schema room for a future file transaction but does not make
file replacement implemented today.

### Commit ordering

Commit uses FAT-realistic durable ordering:

1. close the returned staging handle;
2. sync the complete staged payload;
3. reject an unfinished/ambiguous prior journal or same-nonce old scratch;
4. write and sync `PREPARED`;
5. move the existing target by identity to `.afat-xxxxxxxx-old`, when present,
   and sync the namespace;
6. write and sync `OLD_RENAMED`;
7. move staging by identity to the requested target name and sync;
8. write and sync `PROMOTED`;
9. delete the exact old tree when present and sync;
10. write and sync `CLEAN`, then complete the callback.

Completion currently waits through cleanup and the durable `CLEAN` record. FAT
does not atomically exchange two arbitrary LFN objects, so this is a
crash-recoverable protocol, not a strict atomic rename claim.

### Recovery rules

Recovery is explicit and parent-local. `afatfs_init()` never scans arbitrary
user directories. `filesystem.c` must open each known product parent, call
`afatfs_recoverTreeReplace()`, and wait for success before its browser scan or
next transaction.

Recovery selects the newest valid record and applies only its target and nonce:

- `PREPARED` with target present: target remains authoritative; delete exact
  staging and record `CLEAN`.
- `PREPARED` with target absent: a power cut may have followed old retirement;
  restore exact old when available, then remove staging and clean.
- `OLD_RENAMED` with target absent: promote exact new; if new is missing,
  restore exact old.
- `OLD_RENAMED` with target present: promotion completed before its record;
  advance to `PROMOTED` cleanup.
- `PROMOTED` with target present: retain target, delete exact old, and clean.
- Missing required target/new/old objects: return `RECOVERY_REQUIRED` instead of
  guessing.

With corrupt journal data, conflicting valid records, a `CLEAN` record plus
reserved scratch, or reserved scratch without a valid authorizing record,
asyncfatfs leaves scratch untouched and returns `RECOVERY_REQUIRED`. Repeating a
successful recovery is safe.

Abort before commit deletes only the active transaction's exact `-new` tree.
After commit begins, abort follows the same journal recovery decisions because
the only complete old tree may already be under `-old`.

## RAM And Concurrency Budget

The ARM build on 2026-07-18 measures:

| Item | Bytes | Contract |
| --- | ---: | --- |
| `afatfs_t` singleton | 8,880 | Compile-time target is no more than 9,000 B. |
| Sector cache payload | 4,096 | Eight 512-byte sectors inside the singleton. |
| Open handle | 368 | Six statically allocated slots. |
| Shared copy/replace workspace | 640 | Union prevents copy and commit/recovery overlap. |
| Copy arm | 540 | Largest workspace arm before the shared I/O bytes. |
| Replace arm | 340 | Reuses the copy workspace. |
| Replace transaction descriptor | 68 | Must coexist while staging is populated. |
| Copy frame | 28 | Eight frames, bounded by `AFATFS_TREE_DEPTH_MAX`. |
| Shared I/O buffer | 96 | Copy stream and 69-byte journal record storage. |

The 8,880-byte singleton is 1,076 bytes above the pre-copy Phase 4 baseline of
7,804 bytes and 120 bytes below the decimal 9 KB target. Do not increase cache,
handle count, filename bounds, depth, or operation state without remeasuring the
ARM ABI and updating `MEMORY_AUDIT.md`.

The shared workspace permits:

- one begun transaction while its staging directory is populated, including by
  tree copy;
- either copy execution or replace commit/abort/recovery execution;
- never copy execution and replace execution simultaneously.

## Shutdown

`afatfs_destroy(false)` is asynchronous. A live copy/replace coordinator first
drains its own handle graph. An idle begun transaction is converted to an exact
abort so its retained parent cannot make shutdown wait forever. Ordinary handles
then close and the strict flush completes before the singleton is cleared.

`afatfs_destroy(true)` is the dirty emergency path; it may discard buffered
state and must not be described as a durable shutdown.

## Product Integration Status

The low-level Phase 1-6 APIs are implemented and compile successfully. They are
not yet the production save path:

- Existing Kit/Scene/Bank writers still use their current `filesystem.c`
  cleanup and serialization state machines.
- Phase 7 must integrate Bank first so untoggled child Scenes can be copied into
  staging without parser round-trips.
- Scene and Kit can migrate after Bank replacement is qualified.
- Browser caches must update only after commit and a real rescan/open proves the
  promoted object.
- Hardware and power-cut fault injection remain required before claiming
  production crash recovery.

Autosave dot-file promotion still needs a file-shaped transaction variant. The
directory transaction must not be used as evidence that `.tmp` file promotion
is already power-loss safe.

## Caller Checklist

Before accepting new asyncfatfs product code, verify that it:

- operates inside an explicit, correctly opened product parent;
- uses object iteration and copies the complete exact ID before mutation;
- allocates a real `afatfsObjectFinder_t`, never a cast smaller finder;
- uses display names for LFN APIs and schemas, aliases only for SFN APIs;
- chooses match and create policy explicitly;
- treats a `false` start as no callback and an accepted structured request as
  exactly one callback;
- leaves retained parents idle until completion;
- distinguishes cache wait, EOF, and terminal failure;
- closes all handles and crosses the required sync boundary;
- handles partial copy destinations and `RECOVERY_REQUIRED` without guessing;
- invokes known-parent replacement recovery before browser scans;
- never broadens a product slot selection into a root-level recursive delete.

## Qualification Still Required

The source compiles and its static ABI has been measured, but the following are
release gates for Phase 7:

- FAT16 and recommended FAT32 cards;
- empty, flat Kit, nested Scene, and multi-Scene Bank trees;
- zero-byte and multi-cluster byte-for-byte copies after remount;
- no-space, collision, stale identity, depth, corrupt ancestry, read/write, and
  card-removal failures;
- power removal after every journal write, sync, move, promotion, and delete;
- repeated recovery and corrupt/torn/equal-sequence journal cases;
- proof that every accepted operation calls once and leaks no handle, finder,
  cache lock, or retain count;
- desktop inspection for correct LFNs, no merged tree, and no unauthorized
  scratch deletion.
