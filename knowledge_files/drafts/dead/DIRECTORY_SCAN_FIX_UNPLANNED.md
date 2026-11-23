# Directory Scan and Name-Repair Fix Plan

## Status and scope

This document is the implementation plan only. No production source has been
changed as part of writing it.

The scope is the existing boot/runtime product-name repair path and the
asyncfatfs directory-entry primitives that it depends on. The fix must:

- accept valid VFAT long-name runs that cross a 512-byte directory-sector
  boundary;
- operate on the exact physical object returned by the directory scan;
- distinguish a real target-name collision from source-not-found, corrupt
  metadata, unsupported layout, and card I/O failure;
- make a moved rename recoverable across power loss;
- recognize and remove stale duplicate aliases only when both aliases provably
  name the same underlying object;
- preserve both objects when equal/colliding names refer to different data;
- bound collision handling and boot repair so malformed media cannot hold the
  splash screen for hundreds of full-directory scans;
- keep all repair work pre-audio at boot or asynchronous through
  `filesystem_tick()` at runtime.

This is not a general FAT checker. It will not repair FAT-chain cycles,
cross-linked data belonging to unrelated objects, disagreement between FAT0 and
FAT1, the FAT dirty bit, or arbitrary host filesystem damage. Those conditions
must produce a bounded error, not an inferred destructive repair.

The two autosave records are not special-cased here. This work fixes the
directory scan, identity, rename, and name-only retirement facilities that any
root-file deduplication can safely use later. It does not change the autosave
record format or writer.

## Exact current code path

The current behavior is based on the source as it exists now:

1. `main.c` performs an initial physical scan for Kit, Scene, and Bank, then
   calls `filesystem_createLibraryIndexBlocking()` for each namespace.
2. `filesystem_createLibraryIndexBlocking()` always calls
   `filesystem_repairLibraryNamesBlocking()` before rebuilding the cache and
   writing `.hcindex`.
3. `filesystem_createBootIndexBlocking()` always calls
   `filesystem_repairInstrumentNamesBlocking()` before its per-type Instrument
   scans.
4. Bank Load calls `filesystem_startRepairBankNames()` before reading the
   selected Bank's child Scenes.
5. `filesystem_repairNames_tick()` scans until it finds one noncanonical name,
   closes the scan handle, renames by case-insensitive display text, syncs, and
   restarts the entire parent scan.
6. Repair success is inferred from
   `op_repair_rename_open_name[0] != '\0'`. Every empty result is treated as a
   target collision and retried with suffixes up to 998.
7. `afatfs_findNextObject()` can accumulate a valid LFN chain across directory
   sectors, but `afatfs_renameObjectRunIsSectorLocal()` rejects that object
   before rename.
8. `afatfs_renameObject_lfn()` discards the scanned object's physical identity
   and finds a source again by case-insensitive display name.
9. A moved rename writes the new LFN/SFN run and immediately retires the old
   run. There is no persistence boundary between those two metadata changes.
10. `main.c` discards the return values from the Kit, Scene, Bank, and
    Instrument blocking index builders.

The copied `SD_CARD/` tree currently contains no root Kit/Scene/Bank display
tail longer than eight characters, no immediate Bank Scene display tail longer
than eight characters, and no Instrument stem longer than eight characters.
That copied tree cannot expose original FAT directory-slot placement, duplicate
raw entries, orphan LFN fragments, or repairs made by macOS during mount/eject.

## Required invariants

The implementation must enforce these rules:

1. A scanned object is identified by its physical SFN entry and its complete
   validated LFN-entry list. A later name lookup must not silently substitute a
   different object.
2. A valid LFN run may cross sectors or cluster boundaries. Sector locality is
   an optimization for in-place rewrite, not a validity requirement.
3. A rename callback reports a structured result. A returned short alias is an
   output of success, never the success flag.
4. An in-place rename is allowed only when the old and new entry runs fit in
   the same sector and can be changed with one cached-sector image.
5. Every other rename uses a recoverable two-boundary sequence:

   - write the new entry run;
   - sync it until durable;
   - retire the old SFN entry, making the old alias non-openable;
   - retire any old LFN fragments;
   - sync the retirement;
   - report success.

6. A power loss before the first sync leaves the old object. A power loss
   between the two syncs can leave old and new aliases, but the new alias is
   durable and both point to the same payload. A power loss during or after old
   retirement leaves the durable new alias plus, at worst, ignored orphan LFN
   fragments.
7. Name-only retirement never frees a file or directory cluster chain.
8. Two aliases are treated as the same payload only under the following
   conservative test:

   - object kinds match;
   - directories have the same legal, nonzero first cluster;
   - nonempty files have the same legal, nonzero first cluster and logical
     size;
   - zero-length/zero-cluster files are never merged merely because both have
     cluster zero.

9. When two aliases identify the same payload, retain the canonical alias and
   retire only the stale name-entry run.
10. When two objects have different payload identity, no cluster is deleted:

    - colliding Instrument stems receive a bounded numeric suffix;
    - duplicate numbered Kit/Scene/Bank slots and duplicate Bank-local Scene
      slots cannot be made unambiguous by changing only their eight-character
      display tail, so the later physical entry is renamed outside the loadable
      namespace with the existing `err...` quarantine convention;
    - the first physical loadable entry is the deterministic survivor because
      FAT timestamps are not a reliable “newest” authority.

11. An unsupported or corrupt layout returns an error once. It never enters the
    suffix loop.
12. All loops have explicit finite limits and leave the filesystem facade in a
    terminal state that `main.c` can acknowledge.

## File-by-file code changes

### 1. `config.h`

Add the repair policy constants next to the other SD/autosave timing policy:

- `FS_NAME_REPAIR_COLLISION_LIMIT`: maximum suffix candidates for a proven
  different-payload Instrument collision. Initial value: 32.
- `FS_NAME_REPAIR_ACTION_LIMIT`: maximum durable rename/name-retirement actions
  in one repair request. Initial value: 1024, enough to repair every entry in a
  1,000-slot root library while still bounding a damaged-card loop.
- `FS_NAME_REPAIR_STALL_TIMEOUT_MS`: maximum time without observable repair
  progress. Initial value: 5000 ms and therefore safely below the 32,768 ms
  half-range required for wrapping `uint16_t time_sysTick` subtraction.

What this does: makes the boot-safety limits explicit and adjustable without
editing a state machine.

Why it must exist: the current literal 999 suffix bound turns one unsupported
name into roughly one thousand complete directory scans, and there is no
time-based escape for a nonadvancing phase.

Inputs: build-time policy values.

Outputs: constants consumed only by `filesystem.c`.

Affiliates: `time_sysTick`, `filesystem_repairNames_tick()`, the blocking repair
wrappers, and boot index generation.

### 2. `Core/Hardware/SD/asyncfatfs/asyncfatfs.h`

#### 2.1 Publish complete LFN-entry identity

Add `AFATFS_LFN_ENTRY_MAX`, derived from `AFATFS_LONG_FILENAME_MAX` and VFAT's
13 characters per fragment.

Replace the assumption that `lfnFirstEntry + lfnEntryCount` describes a
sector-local run with an explicit fixed-size array of
`afatfsDirEntryPointer_t` values in `afatfsObjectId_t`. Retain
`lfnEntryCount`; remove `lfnFirstEntry` after every client is migrated.

What this does: an object identity records every physical LFN fragment plus the
owning SFN entry, including runs that cross noncontiguous physical sectors.

Why it must exist: a first pointer and count cannot address a run across a
directory cluster boundary, because adjacent logical directory sectors need
not be physically adjacent.

Inputs: raw entry pointers accumulated by `afatfs_findNextObject()`.

Outputs: a self-contained, copyable identity used by rename, name retirement,
remove, and delete-tree operations.

Affiliates: `afatfsObjectFinder_t`, `afatfsObjectInfo_t`,
`afatfs_retireObjectNameRun()`, `afatfs_removeObjects_lfn()`, and
`afatfs_deleteTree()`.

#### 2.2 Strengthen object-finder validation state

Extend `afatfsObjectFinder_t` with:

- the recorded pointer for each LFN fragment;
- the ordinal expected from the next fragment;
- the ordinal declared by the `LAST_LONG_ENTRY` fragment;
- a flag recording that malformed/orphan fragments preceded the SFN.

Add a corresponding read-only corruption/layout flag to
`afatfsObjectInfo_t`. It is scan metadata rather than physical identity, so it
must not be copied into `afatfsObjectId_t` or used by itself as authority to
delete an entry.

What this does: validates ordinal countdown, fragment count, checksum
consistency, and final SFN ownership instead of accepting any same-checksum
fragment sequence.

Why it must exist: name-only retirement is safe only when every retired LFN
entry has been proven to belong to the selected SFN.

Inputs: raw VFAT entries in physical scan order.

Outputs: either a checksum-verified complete LFN identity or an SFN-only object
with a corruption indication; malformed fragments are never attached to an
unrelated object.

Affiliates: `afatfs_objectScanReset()`,
`afatfs_objectScanAppendLfn()`, `afatfs_findNextObject()`, and structured rename
results.

#### 2.3 Convert rename completion to structured results

Use the existing `afatfsResultCode_t` and `afatfsResultCallback_t`; do not add a
second rename-specific error enum. The current enum already contains
`OK`, `NOT_FOUND`, `ALREADY_EXISTS`, `INVALID_NAME`, `IO_ERROR`,
`CORRUPT_LFN_RUN`, and `UNSUPPORTED_LAYOUT`.

Change the name-based rename declaration so its callback is
`afatfsResultCallback_t`. Keep `openNameOut` as an optional success output.

Add an exact-identity entry point:

`afatfs_renameObjectById_lfn(const afatfsObjectId_t *source, const char *newName,
char *openNameOut, afatfsResultCallback_t complete)`.

Add a name-only retirement entry point:

`afatfs_retireObjectNameById(const afatfsObjectId_t *source,
afatfsResultCallback_t complete)`.

What this does: product repair can mutate exactly the object it scanned, while
legacy save flows that begin with a component name can retain a name-based
wrapper.

Why it must exist: the repair currently scans one object and can later rename a
different casefold-equal object. It also has no public safe way to remove one
stale alias without freeing the shared payload.

Inputs: exact source identity or old display component, new display component,
optional alias output, and completion callback.

Outputs: one structured terminal result and, on `OK`, the generated short alias
when requested.

Affiliates: filesystem name repair, blocking quarantine, Bank Save old-slot
rename and temp promotion, `on_rename_complete()`, and the global asyncfatfs
poller.

Update the public comments beside all affected declarations. They must document
that callbacks fire only after the rename's required sync boundaries and that
name retirement never frees clusters.

### 3. `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`

#### 3.1 Record and validate cross-sector LFN runs

Update `afatfs_objectScanReset()` to clear the pointer array, ordinal state, and
corruption flag.

Update `afatfs_objectScanAppendLfn()` to:

- record the physical pointer for each accepted fragment;
- require `LAST_LONG_ENTRY` to start a chain;
- require subsequent ordinals to decrement exactly to one;
- reject a count beyond `AFATFS_LFN_ENTRY_MAX`;
- require one checksum across the chain;
- preserve state across `afatfs_findNext()` sector transitions.

Update the SFN branch in `afatfs_findNextObject()` to attach the LFN only when
the ordinal sequence finished and the SFN checksum matches. Copy the complete
pointer array into `afatfsObjectId_t`.

What this does: legal cross-sector VFAT names remain visible and fully
addressable, while malformed fragments cannot become part of a destructive
identity.

Why it must exist: the scanner already carries LFN text across sectors but
throws away the physical information needed to rename or retire it safely.

Inputs: directory sectors returned asynchronously by the existing raw finder.

Outputs: validated `afatfsObjectInfo_t` records.

Affiliates: every LFN-aware scan in `filesystem.c`, rename, recursive deletion,
and remove-by-name overwrite preflight.

#### 3.2 Replace sector-local retirement with resumable exact retirement

Replace `afatfs_retireObjectNameRun()` with a resumable helper that accepts the
exact object identity and a caller-owned progress index.

The helper must:

- validate the current SFN entry still matches the captured kind, first
  cluster, logical size, and attributes before changing anything;
- mark the SFN deleted first;
- visit each captured LFN pointer and mark only checksum/ordinal-validated
  fragments deleted;
- process one cache sector at a time and return
  `AFATFS_OPERATION_IN_PROGRESS` without losing its progress index;
- tolerate already-deleted entries so recovery is idempotent;
- return failure if a live entry at a captured pointer no longer belongs to the
  source.

What this does: safely removes a name run spanning any supported directory
layout without touching its data chain.

Why it must exist: the current helper rejects cross-sector runs and assumes all
entries are an index range in one cached sector.

Inputs: `afatfsObjectId_t` and the next retirement element.

Outputs: deleted name metadata only; payload clusters are unchanged.

Affiliates: rename old-name retirement, the new public name-only retirement
operation, `afatfs_removeObjectsContinue()`, and delete-tree terminal cleanup.

#### 3.3 Rework private rename state

Change `afatfsRenameObject_t` as follows:

- replace `succeeded` with `afatfsResultCode_t result`;
- change its callback to `afatfsResultCallback_t`;
- add a flag distinguishing name-selected and identity-selected sources;
- copy an identity-selected source into the operation at queue time;
- add old-run retirement progress;
- add phases for `SYNC_NEW_RUN` and `SYNC_RETIRED_OLD_RUN`;
- add a retire-only mode so the same bounded global operation storage can
  service `afatfs_retireObjectNameById()` without allocating another large
  SRAM state object.

What this does: gives every terminal path a reason and stores enough progress
for power-safe multi-sector retirement.

Why it must exist: the present `succeeded` bit collapses every failure into an
empty alias, and the current state has no persistence or cross-sector progress
phases.

Inputs: public rename/retire request and subsequent poll results.

Outputs: a single terminal structured result.

Affiliates: `afatfs_renameObjectContinue()`,
`afatfs_renameObjectFinish()`, the global `afatfs.renameObject` slot, and
`afatfs_poll()`.

#### 3.4 Validate exact source identity before mutation

For identity-selected rename:

- skip the case-insensitive source search;
- load the captured SFN sector directly;
- verify the entry is live and still matches the captured identity;
- verify each attached LFN pointer/checksum before using in-place rewrite or
  retirement;
- return `NOT_FOUND` for a deleted/replaced SFN and
  `CORRUPT_LFN_RUN` for inconsistent live metadata.

For name-selected rename:

- retain the existing scan behavior for compatibility;
- copy the found `afatfsObjectInfo_t` into the operation once;
- perform the same physical validation before mutation.

What this does: closes the time-of-check/time-of-use hole and makes retry after
power loss deterministic.

Why it must exist: matching the old display string case-insensitively is not
object identity when duplicate entries exist.

Inputs: captured physical pointers and source metadata.

Outputs: a verified source or a typed error with no mutation.

Affiliates: repair, Bank Save, quarantine, and collision scan source exclusion.

#### 3.5 Preserve in-place rename only as a sector-local optimization

Keep `afatfs_renameObjectCanRewriteInPlace()`, but make its requirements
explicit:

- the complete old run is in one sector;
- the new run is no larger;
- the new run can end on the old SFN slot;
- the captured source entry still validates.

Valid cross-sector sources must return false here and continue through the
moved-rename path; they must not fail the operation.

What this does: retains the efficient one-sector rewrite where it is safe.

Why it must exist: sector locality determines whether one-sector rewrite is
possible, not whether a valid VFAT object may be renamed.

Inputs: validated old identity and prepared new name.

Outputs: in-place or moved strategy selection.

Affiliates: `afatfs_renameObjectChooseRun()` and new persistence phases.

#### 3.6 Add recoverable moved-rename barriers

Change the moved path to:

1. write the copied source SFN plus new LFN fragments in the selected free run;
2. call `afatfs_sync()` until the new run is durable;
3. retire the old name through the resumable exact helper;
4. call `afatfs_sync()` until retirement is durable;
5. return `AFATFS_RESULT_OK`.

The in-place path must also pass a final `afatfs_sync()` before reporting
`OK`, so all callers receive the same completion contract.

Set terminal results at the point of failure:

- invalid sanitized component: `INVALID_NAME`;
- source absent/stale: `NOT_FOUND`;
- display target occupied: `ALREADY_EXISTS`;
- invalid LFN ownership: `CORRUPT_LFN_RUN`;
- unsupported address/layout not covered by the pointer model:
  `UNSUPPORTED_LAYOUT`;
- cache/SD failure: `IO_ERROR`.

What this does: turns interruption of a moved rename into a recoverable
duplicate-alias state instead of unordered dirty-sector publication.

Why it must exist: the current new-run write and old-run deletion can reach the
card in either order before the later filesystem-layer sync.

Inputs: dirty directory cache sectors and async SD completion.

Outputs: at least one durable name at every completed persistence boundary.

Affiliates: `afatfs_sync()`, cache flushing, boot repair recovery, Bank Save,
and quarantine.

#### 3.7 Make collision scanning return only real collisions

Preserve separate checks for:

- a display-name collision, which returns `ALREADY_EXISTS`;
- only an SFN alias collision, which increments `aliasOrdinal` internally and
  restarts alias generation;
- source-entry exclusion by exact SFN pointer.

Do not map directory exhaustion, cache error, source loss, or unsupported
layout to `ALREADY_EXISTS`.

What this does: gives `filesystem.c` permission to suffix only when a different
object actually owns the target display component.

Why it must exist: every current failure is interpreted as a collision.

Inputs: raw collision scan and prepared LFN/SFN candidate.

Outputs: free entry run, regenerated SFN alias, or typed terminal result.

Affiliates: `afatfs_renameObjectRawEntryMatchesNew()`,
`afatfs_generateShortAlias()`, and repair retry policy.

#### 3.8 Update other consumers of LFN identity

Migrate the following code to the pointer-array/resumable retirement contract:

- `afatfs_removeObjectsContinue()`: remove the sector-local rejection and
  retain retirement progress across polls;
- `afatfs_deleteTreeContinue()`: copy/use the complete identity and retire
  cross-sector root/child names safely;
- comments and assertions referring to `lfnFirstEntry`;
- any initialization or copy sites found by searching for
  `lfnFirstEntry`, `lfnEntryCount`, and `sfnEntry`.

What this does: prevents the new object identity from leaving deletion paths
with the old one-sector assumption.

Why it must exist: these paths share `afatfsObjectId_t` and
`afatfs_retireObjectNameRun()`; updating rename alone would either fail the
build or preserve unsafe partial behavior elsewhere.

Inputs: existing scanned object identities.

Outputs: unchanged deletion semantics, now valid across directory sectors.

Affiliates: Instrument overwrite deduplication, recursive Kit/Scene/Bank
cleanup, and delete-tree diagnostics.

### 4. `Core/Hardware/SD/filesystem.c`

#### 4.1 Latch a structured rename result

Add `op_rename_result` beside `op_rename_done`. Change
`on_rename_complete()` to accept `afatfsResultCode_t`, store it, and set the
done flag.

Reset both fields in `filesystem_start()` and before every rename request.

What this does: filesystem state machines consume the low-level reason instead
of inferring it from a short-alias buffer.

Why it must exist: an alias string cannot distinguish collision, absent source,
corruption, unsupported layout, or I/O failure.

Inputs: asyncfatfs completion result.

Outputs: operation-local completion state.

Affiliates: repair phases, Bank Save phases 43-45, blocking quarantine, and
future rename clients.

#### 4.2 Preserve the exact repair candidate

Add `op_repair_source_id` and, when needed for duplicate comparison,
`op_repair_conflict_id`. Copy `op_object.id` when phase 20 selects a candidate.

Replace the repair call to name-based `afatfs_renameObject_lfn()` with
`afatfs_renameObjectById_lfn()`.

What this does: the object selected during scan remains the object mutated
after the scan handle is closed.

Why it must exist: duplicate casefold names make the current second
case-insensitive lookup ambiguous.

Inputs: `op_object.id` from `afatfs_findNextObject()`.

Outputs: stable physical source identity throughout the operation.

Affiliates: phases 20, 30-34, generic request initialization, and duplicate
reconciliation.

#### 4.3 Separate candidate construction from collision policy

Keep the existing canonical formatters:

- `<eight-character stem>.<type extension>` for Instruments;
- `NNN <eight-character display>` for root libraries;
- `SS <eight-character display>` for Bank-local Scenes.

Change `filesystem_repairBuildCandidate()` so suffix state is supplied only
after a confirmed different-payload Instrument collision. It must not mutate
suffix state or silently rebuild a candidate after any generic failure.

What this does: canonicalization remains deterministic while retry policy moves
to the state that knows the structured result.

Why it must exist: candidate formatting currently participates in the
all-errors-are-collisions loop.

Inputs: scanned display name, scope, optional proven suffix ordinal.

Outputs: old/canonical component and semantic product key.

Affiliates: `filesystem_copyInstrumentStemDisplay()`,
`filesystem_makeSuffixedDisplay()`,
`filesystem_makeCanonicalInstrumentName()`,
`filesystem_makeCanonicalNumberedDir()`, and
`filesystem_makeCanonicalBankSceneDir()`.

#### 4.4 Add a bounded duplicate ledger using existing SRAM

For root Kit/Scene/Bank and Instrument boot repair, temporarily borrow
`fs_list_cache_name[1000][9]` as the repair ledger. These callers already
discard and rebuild the cache after repair. Do not allocate another 9 kB
array.

For numbered roots, index the ledger by parsed slot and record the first
physical display key. For Instruments, retain sorted canonical eight-character
stems as the existing cache already does. When a repeated key is found, rescan
only to recover the first entry's complete physical identity; do not perform an
unconditional O(n²) comparison of every directory pair.

For runtime selected-Bank repair, do not destroy the root Bank cache. Add a
16-row repair ledger inside the existing 2 kB `filesystem_stage_workspace_t`
union, because Bank child repair precedes Scene staging and foreground
filesystem serialization prevents concurrent autosave use.

Add static assertions that:

- the shared 9 kB cache size is unchanged;
- the Bank repair ledger fits in the existing stage workspace;
- no second browser-sized allocation is introduced.

What this does: detects exact duplicate canonical names/slots that would
otherwise be skipped as “already canonical.”

Why it must exist: collision handling alone sees a stale old alias colliding
with a new canonical alias, but it cannot see two already-canonical physical
entries. Exact duplicate entries are legal to represent in raw FAT metadata
even though host copies may collapse them.

Inputs: accepted product key and first-seen physical display component.

Outputs: either a unique first-seen record or a duplicate candidate requiring
identity comparison.

Affiliates: `filesystem_clearNameCacheStorage()`, cache-kind tagging,
`filesystem_stage_workspace_t`, root library scanners, Instrument typed
scanners, and Bank Load.

#### 4.5 Add duplicate identity comparison and policy phases

Expand `filesystem_repairNames_tick()` with explicit phases for:

- reopening/rescanning the parent to recover the first-seen conflicting
  identity;
- comparing payload identity conservatively;
- name-only retirement of a stale duplicate alias;
- suffix retry for a different-payload Instrument collision;
- quarantine rename for a different-payload duplicate numbered slot;
- syncing/restarting after each durable action.

Use the policy in “Required invariants”:

- same payload plus canonical target: retire only the noncanonical/stale name;
- same payload plus two identical canonical entries: retain the first physical
  entry and retire the later name;
- different Instrument payloads: suffix the later stem, up to
  `FS_NAME_REPAIR_COLLISION_LIMIT`;
- different numbered-slot payloads: rename the later entry to `err...` so only
  one loadable slot remains;
- zero-cluster files: never infer shared identity; suffix rather than retire.

What this does: repairs the exact recoverable state deliberately produced by an
interrupted two-boundary rename and safely resolves preexisting duplicates.

Why it must exist: the current numeric suffix changes only a display tail and
does not resolve two Kit/Scene/Bank directories with the same numeric slot.

Inputs: source and conflict identities, canonical spelling, repair scope, and
structured low-level result.

Outputs: one unambiguous loadable product key without freeing user data.

Affiliates: the existing quarantine-name formatter, index generation, Bank
child discovery, and the new name-only retirement API.

#### 4.6 Replace the phase-33 generic retry

Handle rename results explicitly:

- `OK`: accept the alias and restart after the low-level durable completion;
- `ALREADY_EXISTS`: enter duplicate identity comparison;
- `NOT_FOUND`: restart the parent scan once because the durable new alias may
  already exist; repeated source loss becomes an error;
- `CORRUPT_LFN_RUN` or `UNSUPPORTED_LAYOUT`: stop this repair with a named
  error; never suffix;
- `INVALID_NAME`: quarantine when a safe exact identity rename is still
  possible, otherwise stop with error;
- `IO_ERROR`: stop with error;
- any unexpected result: stop with error.

Remove the 0..998 retry behavior and use the configured limit only for proven
different-payload Instrument collisions.

What this does: eliminates the splash-screen delay caused by treating a
cross-sector layout failure as hundreds of collisions.

Why it must exist: phase 33 is the direct amplification point for the observed
boot symptom.

Inputs: `op_rename_result`.

Outputs: one deterministic next phase.

Affiliates: `filesystem_makeNamedErrorCode()`, `filesystem_errorCode()`, boot
diagnostics, and collision suffix formatting.

#### 4.7 Add progress/action bounds and cleanup

Add repair-local fields:

- action count;
- collision-attempt count;
- timestamp of last observable progress;
- last phase/object coordinate used by the watchdog.

Update progress when an object scan advances, a phase advances, or a durable
metadata action completes. If the action limit, collision limit, or stall
timeout is exceeded:

- publish a specific `NameR...` error code;
- close/release an active parent scan where possible;
- return to root;
- finish with `FS_STATUS_ERROR`;
- never start another rename from stale scratch.

The low-level rename operation must already have reached a terminal callback
before the facade abandons it. The facade must not clear an active asyncfatfs
operation merely because its own timer expired.

What this does: bounds both logical repair loops and a nonadvancing facade
phase without violating async I/O ownership.

Why it must exist: boot wrappers pump until `FS_STATUS_BUSY` changes.

Inputs: `time_sysTick`, configured limits, repair phase/action state.

Outputs: successful completion or a clean, diagnosable error.

Affiliates: blocking repair wrappers, `filesystem_finish()`,
`filesystem_flushFinish_tick()`, and `filesystem_getBootDiagnostic()`.

#### 4.8 Convert every existing rename caller

Update all current call sites found in the exact source:

1. Name repair around phase 32: use exact-identity rename and structured
   duplicate handling.
2. Bank Save old numbered slot to `old...`: name-based rename is allowed;
   `OK` and `NOT_FOUND` permit temp promotion, while all other results fail the
   save.
3. Bank Save temp tree promotion: require `OK`; every other result is
   `BProm` failure.
4. `filesystem_blockRename()` used by boot/pre-load quarantine: wait for the
   structured result and require `OK`; remove alias-buffer success inference.
5. Any disabled diagnostic/test code retained under `#if 0`: either update the
   signature so it remains compilable when restored or explicitly document it
   as stale before removal. Do not leave an old callback signature hidden in
   dead code.

What this does: completes the API migration without mixed success contracts.

Why it must exist: leaving even one old caller would either fail compilation or
continue conflating failure reasons.

Inputs: caller-specific old/new name or exact identity.

Outputs: caller-specific success/error transitions.

Affiliates: Bank Save phases 43-45, quarantine, repair, error strings, and
`on_rename_complete()`.

#### 4.9 Update operation initialization and diagnostics

Initialize every new identity, ledger, result, retry, timeout, and retirement
field in `filesystem_start()` and the dedicated repair starters.

Extend the code-adjacent operation comments and `filesystem_errorPrefix()`
usage so a repair timeout, collision exhaustion, corrupt LFN, unsupported
layout, and I/O failure have distinguishable short error codes.

Keep `filesystem_getBootDiagnostic()` observational, but document the new
repair phases so `CONFIG_DEV_MODE` can identify scan, conflict comparison,
rename, retirement, and sync.

What this does: prevents recycled global operation scratch from changing repair
behavior and makes a hardware failure location visible.

Why it must exist: filesystem state is static and reused across every request.

Inputs: new request and terminal errors.

Outputs: deterministic zeroed state and stable diagnostics.

Affiliates: `filesystem_start()`, boot OLED diagnostics, and Menu filesystem
error display.

### 5. `Core/Hardware/SD/filesystem.h`

Update the public repair comments to state:

- cross-sector VFAT names are supported;
- stale duplicate aliases are collapsed only after same-payload proof;
- different-payload duplicates are preserved by suffix/quarantine;
- the repair is bounded and can return failure;
- Bank child repair remains asynchronous through `filesystem_tick()`;
- blocking root/Instrument wrappers are pre-audio only.

If new public diagnostic codes are needed, add them beside the existing boot
diagnostic enum rather than exposing private phase numbers.

What this does: makes the corrected safety and completion contract visible to
callers.

Why it must exist: the current comment explicitly describes FAT repair as
ordered but non-atomic and does not describe recoverable duplicate handling or
bounded failure.

Inputs: public repair requests.

Outputs: documented success/error and scheduling contract.

Affiliates: `main.c`, preset/Bank Load, Menu, and development diagnostics.

### 6. `main.c`

Stop discarding the results of:

- `filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_KIT)`;
- `filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_SCENE)`;
- `filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_BANK)`;
- `filesystem_createBootIndexBlocking()`.

Introduce one pre-audio storage-bootstrap success flag within the existing
`sd_ok` scope. On failure:

- acknowledge any terminal filesystem status;
- skip subsequent SD index/load/autosave boot work;
- leave the resident Bank predicate false;
- continue to audio initialization with the existing no-card/default behavior
  rather than remaining in a blocking loop or loading from an ambiguous
  namespace.

Do not reformat the card, delete a conflicting payload, or silently regenerate
the failed namespace from defaults.

What this does: a bounded repair error becomes a safe boot fallback instead of
being ignored before later filesystem work.

Why it must exist: the blocking builders already return success/failure, but
all four return values are currently cast to `void`.

Inputs: blocking builder results and current filesystem status.

Outputs: either the normal Bank/Scene/Kit boot ladder or audio boot without SD
content.

Affiliates: boot stages 3, 5, 7, and 8; stage 9 Bank index reload; Bank resident
state; and autosave authorization.

## Resulting repair state sequence

The repaired high-level sequence will be:

1. Enter the target parent.
2. Initialize the namespace-appropriate duplicate ledger.
3. Scan one exact `afatfsObjectInfo_t`.
4. Validate its product key and build its canonical component.
5. If the key is first-seen and already canonical, record it and continue.
6. If it needs canonicalization, close the scan and exact-identity rename it.
7. If rename succeeds, restart after durable completion.
8. If the target exists, recover the conflicting exact identity and compare
   payload identity.
9. Same payload: retain canonical/first entry and retire only the stale name.
10. Different Instrument payload: try the next bounded stem suffix.
11. Different numbered-slot payload: quarantine the later entry.
12. Exact duplicate already-canonical key: apply the same identity policy even
    though no truncation was required.
13. On each durable mutation, increment the action count and restart from the
    physical card.
14. On a clean full scan, close the parent, return to root, and finish.
15. On typed error or configured bound, cleanly finish with
    `FS_STATUS_ERROR`.

## Power-loss recovery cases

The boot repair must be tested against each observable rename boundary:

| Card state | Expected next boot behavior |
|---|---|
| Only old alias exists | Canonicalize normally |
| New run was never synced | Old alias remains authoritative |
| New run is durable; old alias still live | Detect same payload, keep canonical new alias, retire old name only |
| New run durable; old SFN deleted; old LFN fragments remain | Scanner ignores/flags orphan fragments; canonical new alias remains usable |
| Old and new aliases point to different clusters | Preserve both; suffix Instrument or quarantine later numbered slot |
| Two identical canonical aliases point to same nonzero cluster | Keep first entry; retire later name only |
| Two identical canonical zero-length files have cluster zero | Do not merge; suffix/quarantine according to namespace |
| Source LFN crosses a directory-sector boundary | Use moved rename; no unsupported-layout suffix loop |
| LFN ordinals/checksum are corrupt | Bounded `CORRUPT_LFN_RUN` error; no destructive inference |
| SD I/O fails during either sync | Return `IO_ERROR`; next boot observes at least the last completed persistence boundary |

## Verification plan

There is no active host-side asyncfatfs unit-test target in the repository; the
former File/Dir menu diagnostics are compiled out and exposed only as stubs.
Verification therefore has three layers.

### Build/static verification

1. Run `make`.
2. Run `git diff --check`.
3. Search for all old API and identity assumptions:

   - no remaining callback-only `afatfs_renameObject_lfn()` call;
   - no remaining `lfnFirstEntry` reference;
   - no success decision based on `openNameOut[0]`;
   - no repair literal `999`;
   - no sector-local rejection before a valid moved rename.

4. Inspect the map/size output and record SRAM/flash deltas. The 9 kB browser
   cache and 2 kB stage workspace must not grow.

### Controlled card fixtures

Prepare a disposable FAT32 card/image with raw directory-entry fixtures. A
normal copied directory is insufficient because it cannot force directory-slot
placement.

Fixtures must include:

- a one-fragment LFN in slot 15 followed by its SFN in slot 0 of the next
  sector;
- a multi-fragment LFN spanning sectors;
- a canonical plus noncanonical alias sharing one directory cluster;
- two identical canonical directory names sharing one cluster;
- equal names with different directory clusters;
- Instrument long stems truncating to the same eight characters with both same
  and different payload clusters;
- malformed ordinal sequence;
- checksum mismatch;
- orphan LFN fragments;
- duplicate numbered root slot;
- duplicate Bank-local Scene slot.

For each fixture, record raw directory entries before boot, after one boot, and
after a second boot. The second boot must perform no further mutation once the
namespace is canonical.

### Hardware power-cycle verification

1. Enable `CONFIG_DEV_MODE` only for this diagnostic build.
2. Confirm the displayed repair phase advances through scan, new-run sync,
   old-name retirement, and final sync.
3. Interrupt power repeatedly:

   - before new-run sync;
   - after new-run sync and before old retirement;
   - during multi-sector old retirement;
   - after final sync.

4. Confirm every restart either completes repair or exits to the bounded
   no-SD-content boot path.
5. Confirm no test frees or alters payload clusters when removing a stale
   alias.
6. Mount/eject on macOS only after preserving a raw sector image, because the
   host may normalize the evidence being tested.

## Implementation order

Keep implementation reviewable in these smallest safe steps:

1. Add structured rename results and convert every caller without changing
   rename ordering.
2. Extend object identity/scanning to retain and validate every LFN pointer.
3. Replace sector-local retirement and migrate remove/delete-tree users.
4. Add exact-identity rename and switch name repair to it.
5. Add new-run/final sync phases for recoverable moved rename.
6. Replace phase-33 generic retries with typed, configured collision handling.
7. Add duplicate ledgers, same-payload comparison, name-only retirement, and
   numbered-slot quarantine.
8. Add repair progress/action bounds and diagnostic error codes.
9. Make `main.c` honor every blocking index/repair failure.
10. Execute static, raw-fixture, and rapid-power-cycle verification.

Each production `.c` and `.h` change must receive an adjacent comment block
describing what it does, why it exists, inputs, outputs/effects, and affiliated
callers/state, matching the documentation convention already used throughout
the filesystem code.

## Completion criteria

The fix is complete only when:

- a legal cross-sector LFN is canonicalized without entering a suffix loop;
- an interrupted moved rename is repaired on the next boot without freeing its
  payload;
- duplicate same-payload aliases collapse to one canonical entry;
- different-payload conflicts retain both payloads and expose at most one
  numbered product slot;
- all rename callers use structured completion;
- a malformed/unrepairable directory reaches a bounded error and audio boot
  continues;
- a second clean boot performs no further name repair;
- filesystem SRAM allocations remain within the current 9 kB name cache and
  2 kB stage workspace budgets.
