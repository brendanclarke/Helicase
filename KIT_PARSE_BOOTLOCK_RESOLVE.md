# Kit Parse Boot-Lock Resolution Plan

Status: planning only this session. No source change is made by this document;
it is the implementation outline for the fix.

## Goal

Remove the pre-audio boot lock observed as `bootlog.bin == KQ003KST` (previously
`KQ019KST`), where boot stalls in the root Kit library index pass while opening
and streaming `kitset.kcg`. This is the Session 052 deferred "boot sanitation
versus load validation" refactor scoped in SCOPING_TARGETS.md.

## Evidence recap

- bootlog.bin is exactly 8 bytes: `KQ003KST`, with no 64-byte `ASENSURE` capsule.
- Token decode: `KQ` = Kit quarantine; `nnn` = root Kit slot; `KST` = the
  `kitset.kcg` stage.
- The Kit 003 fixture is valid: 397-byte `kitset.kcg`, six valid 8.3 member
  names. The stall is not a corrupt Kit.
- The single 10-second `KITQUAR` deadline fired; recovery then wrote the retained
  detail code to bootlog.bin.

## Root cause

`filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_KIT)` performs full
content validation of every root Kit during boot. It arms the `KITQUAR` deadline
and calls `filesystem_quarantineKitLibraryBlocking()`, which for each numbered Kit
opens `kitset.kcg`, streams it one byte at a time, opens and closes the six
member instruments, and optionally renames invalid Kits. All of that shares one
ten-second deadline. Boot only needs canonical folder names and index rows;
full Kit/content validation belongs to the load attempt.

## Fix strategy (from SCOPING_TARGETS.md)

Boot sanitation must establish only that every browser/menu member can be
reconstructed from its `.hcindex` display row, then write the index. It must not
parse or open every Kit payload. Full Kit/content validation moves to the actual
load attempt, reached through root Kit Load, root Scene embedded-Kit Load, or
Bank selected-child Load.

## Implementation outline

### Step 1 - Remove the boot quarantine gate

File: `Core/Hardware/SD/filesystem.c`, function
`filesystem_createLibraryIndexBlocking()`.

Delete the Kit-only quarantine block: the `filesystem_bootLoggingArm("KITQUAR ")`
call, the `filesystem_quarantineKitLibraryBlocking()` call, the
`filesystem_bootLoggingOperationDone()` call, and the failure return that aborts
index generation. After this change, Kit follows the same repair -> scan ->
index path already used by Scene and Bank.

### Step 2 - Retire the now-dead boot-quarantine helpers

Confirm no remaining live callers, then remove:

- `filesystem_quarantineKitLibraryBlocking()` (filesystem.c:16703)
- `filesystem_validateCurrentKitBlocking()` (filesystem.c:16409)
- `filesystem_makeQuarantineName()` (filesystem.c:16513)
- `filesystem_bootLoggingSetKitDetail()` (filesystem.c:16308)
- the `fs_kit_validation_result_t` and `fs_kit_quarantine_result_t` enums plus
  the quarantine prototype (filesystem.c:1075-1085)
- optionally the already `#if 0` retired embedded-Kit quarantine block
  (filesystem.c:16830+), which is compiled out and references the removed helpers
  only in dead text.

If any helper is still referenced outside the boot path, keep it and annotate
the dead code instead of deleting it.

### Step 3 - Keep name canonicalization as-is

No change required. `filesystem_repairLibraryNamesBlocking()` already
canonicalizes root Kit/Scene/Bank numbered folders to `NNN Name` via
`filesystem_repairNames_tick()` and `filesystem_repairBuildCandidate()`, and
`filesystem_repairInstrumentNamesBlocking()` already canonicalizes Instrument
stems. This is the post-sanitization physical scan the index writer consumes.

### Step 4 - Verify index generation is unchanged

After Step 1, `filesystem_createLibraryIndexBlocking()` still clears the shared
name cache, rescans when the cache tag mismatches, and writes
`FS_INTERNAL_OP_CREATE_LIBRARY_INDEX` with blank slot rows preserved. No change to
shared-cache ownership.

### Step 5 - Verify load-time validation covers the removed boot checks

`filesystem_loadKitDirectory_tick()` already opens `kitset.kcg` (phases 11-15),
parses every line, finalizes the kitset, opens each member, and fails with
`FS_STATUS_ERROR` plus `KDir`/`KSet` on invalid content. Root Scene embedded-Kit
and Bank selected-child loaders share this. A malformed payload now fails the
explicit load instead of being boot-quarantined; it must not become a silent
empty library.

### Step 6 - Build and static checks

- `make -j2`, then `make img`.
- `git diff --check`.
- Record text/data/bss delta; expected text shrink, bss unchanged or slightly
  smaller.
- Grep to confirm no live `KQ`/quarantine callers remain.

### Step 7 - Hardware and card verification

1. Flash the rebuilt image and boot once; confirm it reaches Bank Load / runtime
   without a `KQ` timeout.
2. Confirm Kit/Scene/Bank/Instrument `.hcindex` files are written.
3. Confirm `.hcnames` plus `.hcprms1`/`.hcprms2` appear after the normal Bank
   Load, proving boot now reaches AutoSave-ensure.
4. Negative fixture: add a Kit whose `kitset.kcg` references a missing member.
   Boot must still complete, that Kit must still appear in the browser, and its
   explicit load must fail cleanly without a boot lock.

### Step 8 - Documentation

- Mark the SCOPING_TARGETS.md deferred boot-sanitizer refactor resolved.
- Update MEMORY.md volatile notes and write the session handoff.
- Update ASYNCFATFS_REFERENCE.md / FILESYSTEM_SPEC.md only if they document boot
  quarantine.
- Record the build/RAM and hardware results in the handoff log.

## Explicitly out of scope / do not do

- Do not raise `BOOT_FILESYSTEM_TIMEOUT_MS`.
- Do not add SD timing holds.
- Do not reintroduce a boot-wide content scan or a new implicit
  quarantine-on-load policy.
- Do not fold this into the recursive-tree-delete work; that path is orthogonal
  and already closed.
