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

## Declared APIs That Are Not Yet Product Primitives

The public header currently declares parent-relative child lookup/create,
move, copy-tree, and tree-replace entry points. They are design placeholders,
not supported production operations in this checkout:

- `afatfs_findFirstObjectInDir`, `afatfs_fopenChild`, and `afatfs_mkdirChild`
  do not have the required parent-relative implementation.
- `afatfs_moveObject` does not initialize its recycled handle or copy its
  destination name, and its dispatcher continuation does not perform a move.
- `afatfs_copyObjectTree` and the begin/commit/abort tree-replace API do not
  provide a functioning foreground-pumped implementation.
- `AFATFS_CREATE_REPLACE_FILE` must not be interpreted as an atomic or
  crash-recoverable replacement promise.

Use only the implemented component APIs, object iteration, exact removal, and
`afatfs_deleteTree` described in this reference until those contracts are made
real. The implementation plan is archived in `SESSION_040_AFATFS_FOLLOWUP.md`.

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
- the physical SFN entry and LFN entry-run position.

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

The linked `afatfsFile_t` size is 328 bytes. Raising the pool from three to five
therefore added exactly 656 bytes to the zero-initialized asyncfatfs state; the
current complete `afatfs` symbol is 7,344 bytes.

Five slots are concurrency headroom, not a reason to retain redundant
directory handles. Session 042 boot diagnostics proved that retaining Bank
root, selected Bank, and Scene while opening an embedded Kit exhausted the
former three-slot pool. Product traversal now closes Bank root after opening
the selected Bank and closes the explicit Kit handle after chdir, leaving slots
for `kitset.kcg` and Instrument member files. `currentDirectory` remains usable
because it is stored outside the application handle pool.

Caller rule:

- calculate the maximum simultaneously live handles for the state, including
  payload files;
- release an explicit directory handle after chdir unless a later API
  specifically requires that handle;
- treat an unaccepted open as backpressure/failure according to that API;
- never solve a lifetime leak only by increasing `AFATFS_MAX_OPEN_FILES`.

## Removal

APIs:

- `afatfs_funlink(file, cb)`
- `afatfs_removeObjects_lfn(display_name, match_mode, mode, cb)`

`afatfs_funlink()` removes one opened file handle.

`afatfs_removeObjects_lfn()` scans the current directory and removes every
matching object under the supplied match mode. It restarts scanning after each
delete because removing VFAT/SFN entry runs mutates the directory.

Removal modes:

- `AFATFS_REMOVE_FILES_ONLY`: remove matching files only.
- `AFATFS_REMOVE_EMPTY_DIRECTORIES`: remove matching directories only when they
  are already empty.

`afatfs_deleteTree()` is the native non-blocking recursive-delete primitive
for one captured directory identity. It copies the supplied `afatfsObjectId_t`,
walks the target tree, retires complete LFN/SFN entry runs, frees cluster
chains, releases retained cache state, resets its private handle, and invokes
its result callback once. A false start means no handle accepted the request
and no callback will occur.

Product code still owns scope selection before it invokes deletion:

- enter the intended parent directory;
- scan only immediate children;
- parse the product-visible name with the namespace-appropriate parser; and
- pass the exact captured object identity for the selected slot.

This prevents root-wipe and duplicate-LFN failures. Bank-local children are
parsed as two-digit `00..15` folders before their selected identity is supplied
to native deletion. The legacy `filesystem.c` delete walker remains only as
compatibility/fallback code and uses `afatfs_removeObject()` with an exact
short alias when available.

## Rename

API:

- `afatfs_renameObject_lfn(old_display, new_display, match_mode, alias_out, cb)`

Rename updates the complete VFAT LFN/SFN entry run while preserving the object's
first cluster, file size, attributes, timestamps, and directory children.

Session 038's working Kit Save path does not rely on rename for overwrite. It
recursively deletes the old numbered slot directories and writes a clean
replacement. Rename remains the intended future primitive for safe dot-file
promotion and Bank autosave workflows after dedicated testing.

## Directory Terminators And LFN Creation

VFAT LFN creation may need multiple contiguous directory entries. If a directory
terminator (`0x00`) is encountered where there is not enough room for the full
LFN/SFN run, asyncfatfs retires the skipped terminator into an ordinary deleted
entry before scanning onward. Otherwise future directory scans would stop before
the newly-created object.

When a subdirectory is extended to create space, the create state machine resets
the entry index so the fresh cluster is scanned from entry 0 instead of
skipping its first sector.

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
- Drain close/flush before reporting save completion.
- Use `afatfs_chdirParent()` for structural parent traversal.

Don't:

- Pass slash-separated paths to asyncfatfs.
- Use short-name APIs for visible LFN/case-sensitive product files.
- Treat `fread() == 0` as EOF without `afatfs_feof()`.
- Optimistically publish browser/cache entries before a real scan/open proves
  the object exists.
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
  child matching, captured identities for cleanup, staging/promotion
  preflight, and text schemas in `storageTypes`. Bank Load rescans one selected
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

## AsyncFATFS work still required

- Parent-relative lookup/open/create with explicit collision policy and copied
  input lifetime.
- Hardening native delete against corrupt/cyclic structures and reporting
  partial progress.
- Real same-parent rename and cross-parent move, bounded tree copy, and a
  crash-recoverable staged replace protocol. The current move/copy/replace
  declarations are not completed APIs.
- Effect storage and any feature that needs durable replacement/promotion.
