# asyncfatfs Reference

This document is the firmware-facing reference for `Core/Hardware/SD/asyncfatfs/`.
The product filesystem layout lives in `FILESYSTEM_SPEC.md`; this file covers
the low-level async FAT/VFAT API contract and the rules callers must follow.

## Role

asyncfatfs is a single-context, foreground-pumped FAT32/VFAT layer. It owns SD
sector cache access, FAT chain traversal, directory-entry parsing, file reads
and writes, VFAT long filename entry construction, and object iteration.

Product code should not write FAT directory entries directly. The normal stack
is:

- Menu/Preset decides the musical operation.
- `filesystem.c` sequences directories, files, caches, and product-specific
  validation.
- `storageTypes.c/h` parses or formats text schemas.
- asyncfatfs performs component-level FAT/VFAT operations.

## Pumping And Completion

asyncfatfs operations are asynchronous unless specifically documented as a
state query. Callers start an operation, return to the main loop, and keep
calling `afatfs_poll()` through `filesystem_tick()`.

Important completion rules:

- A start function returning `false` means no operation was accepted.
- A callback may receive `NULL` for open/create failures.
- A close callback means the handle close state is complete, not necessarily
  that every dirty FAT/directory sector is already on card.
- `afatfs_flush()` returns `true` only after dirty cache entries and pending
  writes have drained. Product save completion should not report success before
  the final flush boundary.
- `afatfs_fread()` returning `0` is not EOF by itself. It may mean the sector
  buffer is not ready. EOF is `n == 0 && afatfs_feof(file)`.
- A callback receiving `NULL` does not prove that a singleton name is absent.
  Product code must distinguish missing, failed, and duplicate lookup before a
  CREATE-capable mode is allowed to run.

## Paths Are Components

The LFN APIs take one visible component in the current directory. They do not
parse slash-separated paths.

Callers must navigate one level at a time:

1. `afatfs_chdir(NULL)` to root when needed.
2. Open/create one directory component.
3. `afatfs_chdir(handle)` into it.
4. Open/create the next component.

This is deliberate. `filesystem.c` owns product paths such as
`Kit/003 Slak/kitset.kcg`; asyncfatfs owns individual FAT directory operations.

## Filename Sanitization

All asyncfatfs LFN create/open/remove/rename paths normalize components before
matching or writing:

- Allowed characters are `A-Z a-z 0-9 space _ - . ( ) [ ] + = @ # $ % & ! '`.
- Any other character is converted to `_`.
- Trailing spaces and periods are stripped repeatedly until the component is
  stable.

Why this exists:

- FAT/VFAT can encode names that macOS/Windows do not expose as ordinary user
  files.
- The firmware's fixed-width UI can enter trailing spaces but cannot reliably
  select/delete them later.
- Product code needs the name it verifies in caches to match the name asyncfatfs
  actually creates.

Callers should still treat display strings as untrusted and avoid embedding `/`
or `\`; asyncfatfs is a component API, not a path parser.

## Short Aliases Versus Display Names

VFAT objects have two useful identities:

- Display component: user-visible LFN or case-preserved SFN name.
- Short alias: printable 8.3 SFN alias that asyncfatfs can reopen cheaply.

Rules:

- Store display names in product schemas when the user should see them.
  Example: `kitset.kcg` stores visible member filenames.
- Store short aliases only as operation-local scan/open implementation details.
  The generalized Kit/Scene/Bank name cache stores display components only;
  it does not retain one alias per slot. A single alias scratch buffer may be
  held while a state machine reopens the currently selected directory.
- Do not display aliases such as `001SLA~1` unless no display name exists.
- Do not feed a display name back through short-name APIs and expect case/LFN
  behavior.
- Do not feed a generated short alias into an LFN display-name open and expect
  it to match host-created long-name objects. Session 039's `ERR BnkL06`
  failure came from caching `op_object.shortName` for `Bank/000 Slak/` and then
  calling an LFN display match with that alias. The object listed correctly but
  read-only open failed. Bank root and Bank-local Scene caches now keep the
  display component as the later LFN open key.

Practical caller rule:

- If the next API is a short-name API, keep/use the returned short alias.
- If the next API is an LFN display-name API, keep/use the display component.
- Do not assume one identity can always substitute for the other.

## Matching Modes

LFN APIs accept `afatfsMatchMode_t`:

- `AFATFS_MATCH_CASE_SENSITIVE`: exact display component match after
  sanitization.
- `AFATFS_MATCH_CASE_INSENSITIVE`: folded match for overwrite/diagnostic paths
  that intentionally collapse external same-name case variants.

Current production Kit/Instrument save/load paths prefer exact visible
components once they have a known scan-cache object. Diagnostic and overwrite
cleanup paths may use case-insensitive matching to remove duplicate variants.

## Directory Create/Open

APIs:

- `afatfs_mkdir(name, cb)`
- `afatfs_opendir(name, cb)`
- `afatfs_mkdir_lfn(display_name, match_mode, alias_out, cb)`
- `afatfs_opendir_lfn(display_name, match_mode, alias_out, cb)`

Directory callbacks receive either `NULL` or a handle that is safe to pass to
`afatfs_chdir()`.

For newly-created directories, asyncfatfs must:

- allocate the first cluster;
- write the parent directory entry's first-cluster fields;
- zero-fill the new cluster;
- create `.` and `..` entries.

This differs from ordinary files, which can allocate their first cluster lazily
on first write.

The LFN variants return the generated/current short alias in `alias_out` when
requested. Use that alias only to reopen or chdir later; do not put it in
user-facing text schemas.

## File Create/Open

APIs:

- `afatfs_fopen(name, mode, cb)`
- `afatfs_fopen_lfn(display_name, mode, match_mode, alias_out, cb)`

Current modes used by product code are read/write/create/truncate forms such as
`"r"` and `"w"`. The LFN variant creates or opens a VFAT display component and
returns the short alias if requested.

After writing:

- use `afatfs_fclose(file, cb)`;
- then let `filesystem.c` drain the final flush before reporting save success.

## Unsupported alternatives

Parent-relative child lookup/create, cross-parent move, tree copy, tree
replace, and opened-handle unlink were removed from the public API because
they had no complete foreground-pumped implementation. Use only the component
APIs, object iteration, structured removal/rename, and exact `afatfs_deleteTree`
described in this reference. A future move or transaction needs a separately
approved API and RAM design.

## Object Iteration

APIs:

- `afatfs_findFirstObject(directory, finder)`
- `afatfs_findNextObject(directory, finder, object)`
- `afatfs_findLastObject(directory, finder)`

Object iteration resolves:

- real object kind: file or directory;
- display name;
- short alias;
- SFN case bits;
- whether VFAT LFN entries were present;
- the physical SFN entry and every physical LFN entry-run pointer.

The iterator validates the VFAT LAST ordinal, exact descending ordinal
sequence, stable checksum, legal entry shape, and complete run before exposing
the physical pointers. A malformed run is still returned as a browsable SFN
object with a malformed-run flag; destructive clients must return
`AFATFS_RESULT_CORRUPT_LFN_RUN` rather than infer adjacency across sectors or
directory clusters.

Use object iteration for production scans. It sees dot-prefixed files and
directories as real objects. Product code may filter names by schema, but
asyncfatfs must not hide them.

The old raw `afatfs_findFirst()` / `afatfs_findNext()` APIs expose raw directory
entries and are appropriate only for legacy code that explicitly wants raw FAT
records.

## Current Directory And Parent Navigation

APIs:

- `afatfs_chdir(handle)`
- `afatfs_chdir(NULL)`
- `afatfs_chdirParent()`

`afatfs_chdir(NULL)` returns to root.

`afatfs_chdir(handle)` copies the selected directory state into
`afatfs.currentDirectory`. The explicit application handle is not the current
directory object and does not need to remain open merely because traversal
entered it. Close directory handles after chdir when no later operation needs
that concrete handle.

`afatfs_chdirParent()` reads the structural FAT `..` entry from the current
directory's first sector. It exists because passing the literal string `".."` to
the normal filename parser is not reliable: the 8.3 converter normalizes it
like a blank component. Recursive directory deletion uses `afatfs_chdirParent()`
after emptying a child directory.

## Application handle pool

The current implementation has five application file-handle slots:

```c
#define AFATFS_MAX_OPEN_FILES 5
```

**Corrected Session 057** (a target-ABI debug compile plus GDB type
inspection found the true value): the linked `afatfsFile_t` size is **188
bytes**, not the 328 bytes previously recorded here — the 328-byte figure
predates an earlier change that moved expanded delete state out of every
handle. Raising the pool from three to five therefore added 376 bytes, not
656, to the zero-initialized asyncfatfs state. The `afatfs` symbol's exact
current total size was not re-measured this session; do not cite the old
7,344-byte figure without regenerating it.

Five slots are concurrency headroom, not a reason to retain redundant
directory handles. Session 042 boot diagnostics proved that retaining Bank
root, selected Bank, and Scene while opening an embedded Kit exhausted the
former three-slot pool. Product traversal now closes Bank root after opening
the selected Bank and closes the explicit Kit handle after chdir, leaving slots
for `kitset.kcg` and Instrument member files. `currentDirectory` remains usable
because it is stored outside the application handle pool.

**Session 057 case study — do not repeat this experiment without new
evidence.** A Bank Save screen freeze was initially hypothesized to be handle
exhaustion (the operation reliably stalled after exactly 5 children, matching
`AFATFS_MAX_OPEN_FILES`). The pool was bumped to 8 and a read-only census
helper, `afatfs_countOpenHandles()`, was added to test the hypothesis. Hardware
evidence disproved it outright: the open-handle count stayed at exactly 1 (the
just-created child directory handle) at every child boundary and never
accumulated, and the per-phase stall detector never fired even once across
three failed attempts — the operation was progressing normally throughout. The
real cause was an unrelated foreground-poll counter misread as an elapsed-time
budget (`057_SESSION_HANDOFF_LOG.md` §12-§14). The pool was reverted to 5.
`afatfs_countOpenHandles()` was kept as a permanent read-only diagnostic (no
retained SRAM) since it disproves rather than proves a leak at a glance, but
the pool size itself must stay 5 until new, different evidence appears.

Caller rule:

- calculate the maximum simultaneously live handles for the state, including
  payload files;
- release an explicit directory handle after chdir unless a later API
  specifically requires that handle;
- treat an unaccepted open as backpressure/failure according to that API;
- never solve a lifetime leak only by increasing `AFATFS_MAX_OPEN_FILES` — see
  the Session 057 case study above for a concrete instance where the pool size
  was never the actual problem.

## Removal

APIs:

- `afatfs_removeObjects_lfn(display_name, match_mode, mode, cb)`
- `afatfs_removeObject(short_alias, mode, cb)`

`afatfs_removeObjects_lfn()` scans the current directory and removes every
matching object under the supplied match mode. It restarts scanning after each
delete because removing VFAT/SFN entry runs mutates the directory. Completion
callbacks receive `afatfsResultCode_t`; non-OK results forbid a caller from
creating or publishing a replacement. Regular removal releases at most one
FAT cluster per foreground continuation, then retires the complete name run.

Removal modes:

- `AFATFS_REMOVE_FILES_ONLY`: remove matching files only.
- `AFATFS_REMOVE_EMPTY_DIRECTORIES`: remove matching directories only when they
  are already empty.

`afatfs_deleteTree()` is the native non-blocking recursive-delete primitive for
one captured `afatfsObjectInfo_t` directory identity. It copies every physical
LFN/SFN pointer, walks without C recursion, frees each chain before retiring its
name run, handles FAT16 root binding distinctly, and bounds descents plus
released clusters with a structural budget. It releases the private handle and
cache ownership before invoking exactly one structured result callback. A
false start means no handle was accepted and no callback will occur. The
operation is non-transactional: an I/O/layout failure may leave partial card
mutation, and callers must not create or publish a replacement after failure.

Product code still owns scope selection before it invokes deletion:

- enter the intended parent directory;
- scan only immediate children;
- parse the product-visible name with the namespace-appropriate parser; and
- pass the exact captured object identity for the selected slot.

This prevents root-wipe and duplicate-LFN failures. Root Kit may use its
documented legacy short-alias fallback; root Scene and root Bank do not. Bank
Save replaces the root Bank tree directly through this exact-object flow; it
does not use temporary or `old*` promotion names.

**Descend/ascend identity invariant (Session 054, hardware-confirmed Session
055).** The traversal binds one `file` handle to whichever directory is
currently open and tracks it via `file->directoryEntryPos`; that field is the
sole thing distinguishing "the delete root just emptied" (finish
successfully) from "a nested child just emptied" (ascend and continue). Two
fields on the persistent `afatfs.deleteTreeState` singleton — `descendTarget`
(a full `afatfsObjectInfo_t`) and `parentEntry` (a directory-entry
pointer) — snapshot the child's identity and its directory-entry location at
the moment of descent, and are restored into `op->currentTarget` and
`file->directoryEntryPos` respectively on ascend. Both fields hold exactly
one level (a descend nested inside another descend overwrites them), which
matches the existing `op->parentCluster` depth-one bound and is not a
regression: `afatfs_deleteTree()` has exactly one caller
(`filesystem_deleteSlotDirectory_tick()`), invoked on at most a Kit slot
(files only) or a Scene slot (files plus one `Kit …/` subdirectory) — never
more than one nested directory below the delete root.

Two earlier attempts at fixing an unrelated resume-target bug each broke one
half of this invariant before the final repair: reconstructing a resume
target directly from `op->parentCluster` cleared `directoryEntryPos` as a
side effect of reusing `OPEN_DIR`'s reset sequence, and a later fix that
removed a redundant (and apparently unreliable) parent re-scan assumed
`op->currentTarget` stayed untouched between a child's discovery and its
ascend — but the child's own internal deletes overwrite that register with
every object they process. Both symptoms (an emptied root misread as a
nested child; an ascend that free-list-corrupts by re-freeing an
already-freed cluster chain) trace to the same missing snapshot-and-restore,
not to two independent defects. Full round-by-round diagnostic trail
(originally `AFAT_RECURSIVE_WHITEPAPER.md`) is preserved in
`knowledge_files/log_archive/054_SESSION_HANDOFF_LOG.md`.

## Logging-only diagnostic snapshots

`afatfs_getDiagnosticSnapshot(file, snapshot)` and the paired SD-layer
`sdcard_getTransportSnapshot(snapshot)` exist solely for the Session 047
boot-time `ASENSURE` forensic capsule. They are read-only copies of live file,
allocator/cache, and transport fields taken immediately before existing boot
recovery abandons the failed operation. They must not poll, allocate, issue
I/O, change cache/handle ownership, alter callbacks/retries, or drive product
control flow. Their result is diagnostic evidence, not an AsyncFATFS recovery
API; the fixed 72-byte bootlog envelope is specified in `DEV_MODES.md`.

## Rename

API:

- `afatfs_renameObject_lfn(old_display, new_display, match_mode, alias_out, cb)`

Rename updates the complete VFAT LFN/SFN entry run while preserving the object's
first cluster, file size, attributes, timestamps, and directory children.

Rename updates a validated complete run and returns a structured result;
`alias_out` is valid only with `AFATFS_RESULT_OK`. Kit, Scene, and Bank
overwrite do not rely on rename: they use exact delete/recreate. The current
AutoSave design is the root A/B record pair specified in `AUTOSAVE.md`, not a
per-product-file dot-backer transaction.

## Directory Terminators And LFN Creation

VFAT LFN creation may need multiple contiguous directory entries. If a directory
terminator (`0x00`) is encountered where there is not enough room for the full
LFN/SFN run, asyncfatfs retires the skipped terminator into an ordinary deleted
entry before scanning onward. Otherwise future directory scans would stop before
the newly-created object.

When a subdirectory is extended to create space, the create state machine resets
the entry index so the fresh cluster is scanned from entry 0 instead of
skipping its first sector.

The LFN creation scan now uses a latch-and-continue approach (Session 056). The
first viable free run of deleted (`0xE5`) entries is latched but does not
immediately branch to the create phase. Scanning continues through the remainder
of the directory. If a matching existing entry is found later, it is opened
normally and the latch is unused. Only at directory exhaustion (`entry == NULL`)
does the latched position get used for creation. This prevents duplicate
directory entries when deleted-entry free runs appear before an existing file
with the same name.

The earlier code exited the scan the moment a viable free run was found,
which created duplicates in any directory that had deleted entries before
the target file's position. The SFN-only path and the rename path were never
affected — they create only at the `0x00` terminator or at physical directory
end respectively.

These are internal details, but callers should understand the consequence:
LFN creation is a multi-step directory mutation, and callers must wait for the
callback/flush boundary before assuming a host or later scan can see the file.

## Caller Do/Don't Checklist

Do:

- Use `filesystem.c` for product-level paths.
- Use LFN helpers for user-visible product saves.
- Store display names in schemas and short aliases only in scan/open caches.
- Use object iteration for scans that care about LFNs, case, aliases, or object
  kind.
- Treat missing objects as normal where a browser slot can be empty.
- For a firmware-owned singleton, prove absence with a complete,
  successfully-closed case-insensitive directory scan before CREATE. Treat
  multiple matches and scan/open/close failure as errors.
- Drain close/flush before reporting save completion.
- Use `afatfs_chdirParent()` for structural parent traversal.

Don't:

- Pass slash-separated paths to asyncfatfs.
- Use short-name APIs for visible LFN/case-sensitive product files.
- Treat `fread() == 0` as EOF without `afatfs_feof()`.
- Optimistically publish browser/cache entries before a real scan/open proves
  the object exists.
- Treat a failed/NULL open as permission to create a singleton, or silently
  choose/delete one of multiple case-folded matches.
- Start deletion from a display name after a scan already selected a concrete
  object; retain and use the captured object identity instead.
- Hide dot-prefixed objects in asyncfatfs; filtering belongs in product scans.
- Persist returned short aliases into user-facing schemas.

## Current Production Users

- Kit scan/load/save uses object iteration, LFN directory creation, returned
  aliases, identity-based native tree deletion for replacement, and text
  schemas in `storageTypes`.
- Scene scan/load/save uses object iteration, LFN directory creation, scoped
  same-slot replacement through captured identities, and text schemas in
  `storageTypes`.
- Bank scan/load/save uses object iteration, namespace-aware root/two-digit
  child matching, captured identities for cleanup, direct exact delete/recreate
  for root Bank Save, and text schemas in `storageTypes`. Bank Load rescans one selected
  child at a time and retains no 16-child name/alias table.
- Root Instrument scan/load/save uses object iteration, registry-owned typed
  directories, one shared generalized browser-name cache with up to 1,000
  sorted rows, LFN file writes, and descriptor-keyed text schemas. Product
  scan/repair policy excludes `.hctmp.<ext>`; asyncfatfs itself still exposes
  the dot-prefixed object normally.
- Kit/Scene/Bank `.hcindex` rows and HCNAMES reuse one 1,000-by-9-byte cache in
  `filesystem.c`; that cache is above the asyncfatfs layer and does not alter
  object iteration semantics.
- File/Dir/sDir diagnostic menu entries and their list caches are retired.
  Compatibility facade calls perform no asyncfatfs operation. A total of 107
  unreachable Menu bytes (two 49-byte editor/result strings plus nine result
  bytes) remains above this layer, but no multi-entry diagnostic cache or
  asyncfatfs traversal remains.

## Seek and file-size tracking

`afatfs_fseekAtomic()` and `afatfs_fseekInternalContinue()` both call
`afatfs_fileUpdateFilesize(file)` after advancing `cursorOffset`. This ensures
`logicalSize` tracks the true written extent on every successful seek, not only
on the queued continuation path.

Before Session 056 the atomic path omitted this call, leaving `logicalSize` at
0 for newly created files throughout the entire write sequence. The only
on-disk size that persisted was `physicalSize` (cluster-rounded) from
`AFATFS_SAVE_DIRECTORY_NORMAL` during cluster allocation. If `fclose()`'s
final `AFATFS_SAVE_DIRECTORY_FOR_CLOSE` write was lost (cache eviction, power
loss), the on-disk file size reverted to a cluster boundary — e.g. 32,768
instead of the true 34,768 bytes for an AutoSave record.

Do not remove the `afatfs_fileUpdateFilesize()` call from either seek path.

## AsyncFATFS work still required

- Parent-relative lookup/open/create with explicit collision policy and copied
  input lifetime.
- The recursive-delete descend/ascend defect (`ScnS05` and its relatives) is
  fixed and hardware-confirmed for ordinary Kit/Scene/Bank overwrite as of
  Session 055 (a full Kit-modify-save / Instrument-modify / Scene-modify-save
  / Bank-save-then-load round trip reported no errors). The low-level
  acceptance matrix from `RECURSIVE_TREE_DELETE_REIMPLEMENT.md` §10
  (malformed LFN, cyclic/broken-parent layout, injected FAT/cache error,
  exhausted handle pool, cross-sector LFN runs) has still never been
  exercised as dedicated fixtures — only encountered incidentally through
  ordinary product use. Do not claim that matrix closed from product-level
  testing alone.
- Effect storage and any feature that needs durable replacement/promotion.
