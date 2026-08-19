# Recursive Tree Delete — Complete Re-implementation Recipe

**Scope:** Requirements and implementation log. Repair the existing foreground-pumped
`afatfs_deleteTree()` and make Bank, root Scene, and Kit overwrite use one
exact-object delete-recreate flow. The implementation log below records the
source changes and verification status; the acceptance matrix remains the
hardware/card test plan.

**Authority:** `SCOPING_TARGETS.md` requires native exact-object deletion and
forbids `oldNNN-xxxx` rename, temporary-root promotion, and boot cleanup as an
overwrite workaround. `FILESYSTEM_SPEC.md` and `ASYNCFATFS_REFERENCE.md` own
the product/API contract. The old recursive-delete draft is requirements/test
reference only, never an implementation plan.

## Implementation log

### 2026-08-18 — baseline and RAM gate

- Read `MEMORY.md`, the filesystem specifications, and this recipe before
  touching source. The only pre-existing worktree change is the deleted
  generated `build/LXRV2_lxr02.img`; it is being preserved.
- The baseline ARM build succeeds (`make -j2`). Linked baseline is
  `text=378,460`, `data=400`, `bss=95,176` bytes; `afatfs` is 7,344 bytes.
- ARM layout probe measured: `afatfsObjectId_t` 96 bytes,
  `afatfsObjectInfo_t` 100 bytes, `afatfsObjectFinder_t` 68 bytes,
  `afatfsDeleteTree_t` 284 bytes, `afatfsFile_t` 328 bytes, and the file
  operation union 284 bytes. The probe was temporary and was removed after
  measurement.
- The planned complete-LFN identity fields are three additional physical
  entry pointers (24 bytes) in each object result/finder owner, plus finder
  validation state. With the existing six resident `afatfsFile_t` owners and
  the global remove state, the projected `afatfs` increase is approximately
  508 bytes. The two filesystem object/finder pairs plus the exact delete
  target are projected at approximately 132 bytes, for approximately 640
  bytes net BSS. The implementation will add no tree stack, handle slot, or
  deliberate stack budget; the current relevant stack records are 24 bytes for
  name-run retirement, 56 bytes for object iteration, and 24 bytes for the
  remove continuation.
- This is a RAM-gated checkpoint under `MEMORY.md`. Source implementation is
  paused until the user acknowledges the exact projected allocation:
  approximately 640 bytes of additional normal SRAM1 BSS, owned by the
  AsyncFATFS object/finder/delete state and the filesystem exact-target scan
  state for the lifetime of those existing static/handle owners. The final
  linked delta and stack report will be measured again before closeout.

### 2026-08-19 — implementation pass 1

- User approved the RAM checkpoint and implementation resumed.
- Complete object results now retain the first LFN pointer plus up to three
  validated following physical pointers. The iterator validates LAST ordinal,
  descending sequence, checksum, entry shape, and run completeness; malformed
  runs remain browsable as SFN objects but are flagged for destructive clients.
- One shared name-retirement continuation now batches only pointers in the
  currently cached physical sector and yields between batches. It is used by
  rename, regular removal, and native tree deletion; it performs no FAT work.
  Regular removal now releases at most one FAT cluster per continuation and
  retires the complete name run only after chain release.
- `afatfs_deleteTree()` now accepts and copies complete `afatfsObjectInfo_t`
  input. Its traversal is iterative, exact-pointer based on parent re-find and
  root completion, handles FAT16 root `.. == 0` distinctly, frees clusters
  before names, and bounds descents/cluster releases by a saturated structural
  budget. A timeout is not used as native cancellation.
- The expanded delete state is owned once by `afatfs.deleteTreeState` and the
  operation handle stores only a pointer; this avoids multiplying the expanded
  state across all six existing file owners. The final ARM layout is
  `ObjectId=120`, `ObjectInfo=124`, `ObjectFinder=96`, `DeleteTree=388`,
  `File=180`, and operation union `136` bytes. The linked build is
  `text=378,956`, `data=396`, `bss=94,612` bytes, versus the baseline
  `text=378,460`, `data=400`, `bss=95,176`; there is no net SRAM increase.
- Filesystem overwrite selection is now singular: the parent scan continues
  after a candidate to prove no duplicate directory or same-slot file, closes
  its scan handle on every path, and passes the complete captured result to
  native delete. Delete success does not trigger a cleanup rescan.
- Kit, root Scene, and root Bank Save callers use the singular resolver. Bank
  Save now deletes the exact old root Bank and creates the final numbered Bank
  directly; temporary/old names, scratch collision scans, and promotion
  phases were removed. The legacy firmware recursive walker and unsupported
  movement/copy/replace/unlink API scaffolding were removed.
- Remaining work before closeout: finish structured-result gating audit in all
  active callers, update the authoritative filesystem references and
  `MEMORY.md`, run the complete build/image verification, and perform static
  acceptance checks for pointer/LFN/phase invariants. Hardware/card fixture
  validation is not claimed by this source-only pass.

### 2026-08-19 — final source verification pass

- Completed the structured-result audit: active remove/rename callers now gate
  create/publish work on `AFATFS_RESULT_OK`; native delete callbacks are
  likewise checked before Kit, root Scene, or direct Bank replacement proceeds.
- Added the final malformed-child guard to native traversal. A browsable SFN
  fallback is never accepted by destructive code when its preceding VFAT run
  is incomplete, out of order, checksum-invalid, or structurally malformed.
  The slot timeout path now remains the owner of pending open/close/native
  handles until their callback releases them; timeout is diagnostic, not abort.
- Final ARM build succeeds with `text=379,676`, `data=396`, `bss=94,612`
  bytes. Relative to the baseline, this is `+1,216` text, `-4` data, and
  `-564` BSS bytes; the approved approximately 640-byte SRAM checkpoint was
  not exceeded, and the final object/delete layout remains
  `ObjectId=120`, `ObjectInfo=124`, `ObjectFinder=96`, `DeleteTree=388`,
  `File=180`, operation union `136` bytes. The build emits only the existing
  `eraseCount`/unused-legacy-helper and embedded-libc warnings.
- Static checks passed: no removed movement/copy/tree-replace/unlink APIs or
  firmware recursive walker remain in live Core code; no Bank `tmp*`/`old*`
  promotion state remains; `git diff --check` is clean; and the generated
  `build/LXRV2_lxr02.img` deletion present before this work was preserved.
- Hardware/card fixture execution, desktop remount checks, injected
  FAT/cache-error tests, FAT16 media checks, and audio-under-save stress are
  still pending. This source/build pass does not claim those results.

## 1. Pre-implementation diagnosis and requirements

The checked-out code already has `AFATFS_FILE_OPERATION_DELETE_TREE` in
`asyncfatfs.c`; it allocates one private `openFiles[]` handle, copies an
`afatfsObjectId_t`, walks by an `afatfsObjectFinder_t`, frees FAT entries, and
calls an `afatfsResultCallback_t`. `filesystem.c` scans the current parent as
`.`, captures `op_delete_slot_target_id`, closes that scan handle, and starts
the native deleter. Kit and root Scene Save call that worker before mkdir.
Bank Save instead writes a `tmpNNN-xxxx` tree, renames the previous numbered
Bank to `oldNNN-xxxx`, and renames the temporary tree into place.

The implementation must correct these exact source facts:

1. Delete-tree currently retires LFN/SFN entries in
   `AFATFS_DELETE_TREE_RETIRE_ENTRIES` *before* freeing their FAT chain in
   `AFATFS_DELETE_TREE_FREE_FILE_CLUSTERS`.
2. `afatfs_retireObjectNameRun()` rejects every VFAT run which is not entirely
   in one sector. A valid host-created run can cross a sector and can cross a
   non-contiguous directory-cluster boundary.
3. `afatfsObjectFinder_t` saves only an LFN first-entry pointer and count; it
   cannot later name every physical fragment in a cross-sector run. Its LFN
   collector verifies checksum but not a complete descending ordinal sequence.
4. Ascend finds the child in its parent only by `firstCluster`. It must use the
   exact captured SFN-entry pointer, with cluster equality only as a corruption
   check.
5. The private ascend binder converts FAT16 `.. == 0` into ordinary directory
   cluster zero; FAT16 root instead requires
   `AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY`. Existing
   `afatfs_chdirParent()` has the correct distinction but delete-tree bypasses
   it.
6. Root completion compares first clusters, not physical root-entry identity.
   Cyclic/damaged media can therefore confuse a child for root.
7. No structural-work budget bounds cycles; `lastPhase` and `timeoutTicks` in
   `afatfsDeleteTree_t` are currently unused.
8. The slot worker rescans after a success and deletes *all* same-slot
   directories. The required contract is one captured object. Duplicate
   candidates must fail safely, not be broadly cleaned up.
9. Slot-worker scan errors and timeouts can leave an explicit scan handle open;
   a timeout during native deletion can free the facade while the native handle
   is still live and uncancellable.
10. `afatfs_removeObjects_lfn()` and `afatfs_removeObject()` use the same
    sector-local retire helper but expose completion-only callbacks. Current
    Instrument Save and AutoSave deduplication can continue after failed file
    removal.
11. `afatfs_funlink()` is a no-op success, and parent-relative child, move,
    copy-tree, and tree-replace APIs are declared stubs. They must not remain
    plausible destructive alternatives.

## 2. Final behavioral contract

One delete request removes one directory returned by one immediate-parent
`afatfs_findNextObject()` scan. It never accepts a path or re-resolves a display
string. The product scan must classify the complete parent before deleting:

| Scan result | Save behavior |
|---|---|
| No matching slot object | Create the numbered replacement. |
| One matching directory | Copy its complete iterator result, delete it, then create replacement. |
| A matching file, malformed product object, or two/more matching directories | Close scan and fail; delete/create nothing. |
| Scan/open/close/delete failure | Drain owned work and fail; never create/publish replacement. |

“Matching” is the namespace parser applied to `object.id.displayName`: a
checksum-validated LFN when present, otherwise case-preserved SFN display text.
Only root Kit may additionally use its documented three-leading-digit SFN alias
fallback. Root Scene and root Bank may not. Bank-local two-digit children are
not a Bank Save delete target because the direct Bank replacement deletes the
root Bank tree as a whole.

| Name condition | Product key | Native identity/result |
|---|---|---|
| Valid LFN + SFN | LFN display component | Retire every validated LFN fragment then owning SFN. |
| SFN-only/case-bit name | Printable case-preserved SFN | Retire owning SFN only. |
| Kit legacy alias such as `001SLA~1` | Alias digits, Kit only | One exact directory eligible. |
| Valid LFN spanning sectors/clusters | LFN display component | Stored physical pointer for every fragment; never `physical+1`. |
| Incomplete/checksum-wrong/out-of-order LFN | No trusted display key | `AFATFS_RESULT_CORRUPT_LFN_RUN`; no guessed fragment deletion. |
| `.`/`..` | None | Never exposed or selected. |
| `.DS_Store` or other dot-prefixed child | Normal LFN/SFN key | Ordinary child; delete it. |

Delete remains non-transactional: on a post-start I/O/layout failure, partial
retirement remains on-card, the callback is non-OK, and the caller never writes
a replacement. Every accepted start fires exactly one callback only after the
private handle is reset/released. A false start fires no callback. Every native
step yields after at most one object, one name-run sector batch, or one cluster.

## 3. Phase 0 — fixture and diagnosis (no source change)

Create card fixtures for occupied Kit, root Scene, and root Bank slots plus a
clean neighbor. Include nested directories, an SFN-only child, normal LFN
child, dot-prefixed child, and valid LFN whose run crosses a directory-sector
boundary. Capture pre/post desktop listings and raw directory/FAT sectors.

For one overwrite at a time record outer slot-worker phase and
`afatfs_getDeleteTreePhase()`, then remount and classify the survivor: SFN,
orphan LFN, live FAT chain, missed nested child, or premature caller create.
Run Bank separately because its current temporary rename path is not evidence
of native delete correctness. Do not add a trace allocation: existing phases,
named errors, and card images are enough.

**Checkpoint:** record the observed failing phase and byte evidence in the
handoff before implementation; do not turn a symptom into an assumed cause.

## 4. Phase 1 — complete, validated VFAT identity

### 4.1 `asyncfatfs.h/.c`: object finder and object result

Add `AFATFS_LONG_FILENAME_ENTRY_MAX`, computed from the existing 48-character
component bound and 13 VFAT characters/entry (currently four). Keep
`afatfsObjectId_t` as the SFN/object metadata identity. Extend
`afatfsObjectInfo_t` with the three physical LFN pointers after existing
`id.lfnFirstEntry`, and extend `afatfsObjectFinder_t` with the matching
scan-time pointers plus expected ordinal/malformed-run state. The first entry
stays in `id.lfnFirstEntry`; no pointer is duplicated.

**What/why:** the iterator is the only point at which each exact physical VFAT
entry is known. A count cannot safely reconstruct cross-cluster physical
adjacency later.

**Inputs/outputs:** `afatfs_findNextObject()` still returns one object. A valid
LFN has `lfnEntryCount` 1..4, first pointer plus follow pointers in physical
directory order; SFN-only has count zero. Product callers never manufacture
these fields. Header comments must document field order, lifetime, ownership,
and that `shortName` is an open alias—not a display-key substitute.

**Affiliates:** object iteration, shared retire helper, rename, remove,
delete-tree input, and filesystem same-slot capture.

**RAM gate:** three `afatfsDirEntryPointer_t` values are 24 bytes on the ARM
ABI per expanded object-info/finder owner. Before code, measure target
`sizeof(afatfsObjectId_t)`, `sizeof(afatfsObjectInfo_t)`,
`sizeof(afatfsObjectFinder_t)`, `sizeof(afatfsDeleteTree_t)`,
`sizeof(afatfsFile_t)`, the five-slot union multiplication, all BSS owners,
and worst stack delta. Obtain explicit user approval under `MEMORY.md` before
committing any net allocation. Do not add a tree stack or raise handle count.

Fold sequence validation into `afatfs_objectScanAppendLfn()` and reset into
`afatfs_objectScanReset()`; do not add another parser. Require legal
`LAST|N`, then exact `N-1..1`, stable checksum and legal LFN shape. Store a
pointer only after validation. A deleted entry, terminator, volume label,
unexpected LAST, skipped ordinal, or mismatch clears the pending run. At SFN,
publish LFN text/run only if the checksum matches. Browsing may fall back to
the SFN display text, but destructive clients must see malformed-run state and
return `CORRUPT_LFN_RUN`, never guess which orphan entries belong to the SFN.

### 4.2 `asyncfatfs.h/.c`: delete-tree accepts complete information

Change `afatfs_deleteTree(const afatfsObjectId_t *, ...)` to accept
`const afatfsObjectInfo_t *`. Copy the complete result into private operation
state at acceptance; change `filesystem.c`’s target field likewise.

Validate directory kind, legal first cluster, valid SFN pointer, and valid LFN
run before accepting. The caller can reuse its scan object immediately after a
true return. Header comment must state iterator-produced input only, copied
identity, false/no-callback, result callback, and clients.

## 5. Phase 2 — one resumable complete-name retirement path

### 5.1 `asyncfatfs.c`: replace sector-local helper

Replace `afatfs_renameObjectRunIsSectorLocal()` and
`afatfs_retireObjectNameRun()` with one private stateful continuation placed
beside rename/remove/delete operation state. It owns next entry ordinal and
terminal status. Given validated `afatfsObjectInfo_t`, it obtains the exact
next LFN/SFN pointer, batches only entries in the same cached physical sector,
marks them deleted, dirties that sector, advances, and yields. It holds no
cache sector across a yield, derives no pointer arithmetically, and does not
touch FAT/children.

**Why:** rename, regular file removal, and tree deletion require identical
cross-sector VFAT behavior. One helper prevents three subtly incompatible
implementations. Its adjacent comment must cover entry order, cache ownership,
inputs/outputs, no FAT effect, errors, and all three clients.

### 5.2 `asyncfatfs.h/.c` and `filesystem.c`: structured remove/rename results

Change `afatfs_removeObjects_lfn()`, `afatfs_removeObject()`, and
`afatfs_renameObject_lfn()` completion parameters to `afatfsResultCallback_t`.
Their finish paths copy callback/result, release active state, then callback
once. No-match remove is OK; malformed run, cache/FAT error, collision, type
conflict, and rename source absence are non-OK. Rename alias output is valid
only with OK, not a failure side channel.

Replace `on_remove_complete(void)` and `on_rename_complete(void)` with result
latches. Update active callers: AutoSave duplicate-file phases 11/24 and
34/25, Instrument Save phases 15/16, name-repair phases around 6638, and the
blocking repair helper around 16788. Each non-OK must take its current normal
error route before create/rename/publish. Delete Bank promotion callers in
Phase 4. Update callback/API comments with ownership, false-start rule,
success output, and no-create-after-failure requirement.

### 5.3 `asyncfatfs.c`: bounded regular-file removal

Replace the current synthetic `afatfs_ftruncateContinue(..., true)` removal
route. It marks its SFN deleted first and can loop through a cache-hit chain in
one poll. The remove state must instead load SFN metadata, release one legal
FAT cluster per poll, retire the complete name run through the shared helper,
then restart the scan. Zero first cluster is allowed only for an empty regular
file; directories use the same directory-chain-before-name-run ordering.

Inputs remain display-folded for `removeObjects_lfn` and exact printable SFN
alias for `removeObject`. `FILES_ONLY` leaves directories; `EMPTY_DIRECTORIES`
requires an already-empty directory. Outputs are all matching objects removed
or a structured error. Affiliates are Instrument Save, AutoSave deduplication,
allocator hint, and future exact empty-directory cleanup. Reuse one common
bounded FAT release step if practical; do not create duplicate unbounded loops.

## 6. Phase 3 — native delete traversal

### 6.1 `asyncfatfs.c`: reshape `afatfsDeleteTree_t` and phases

Replace the current loose sequence with explicit validate/bind, scan, descend,
read-dotdot, bind-parent, exact-parent-re-find, free-cluster, retire-name-run,
and finish phases. Keep one private file handle, no C recursion, no firmware
name/alias stack, and no added open handles.

State must own copied complete root and current-retirement objects, full finder,
pending descended child SFN pointer/first cluster, shared name-run state, and
structural-work budget. While scanning, store the current directory’s owning
SFN pointer in the private file handle’s otherwise-unused `directoryEntryPos`;
this avoids a redundant root flag. State comments must say owner, lifetime,
inputs/outputs, affiliates, and no-stack guarantee.

### 6.2 `asyncfatfs.c`: private binder handles FAT16 root correctly

Fold a private bind block into existing OPEN_DIR/parent-scan code; do not add a
public accessor. It unlocks prior retained cache, resets cursor/finder state,
then binds either a legal ordinary directory cluster or FAT16 root type. `..`
zero means FAT16 root; FAT32 uses configured root cluster. Invalid cluster/type
returns `UNSUPPORTED_LAYOUT` before address/FAT access. It does not mutate
`afatfs.currentDirectory`.

Affiliates: cursor physical-sector conversion, EOF test, seek, structural `..`
read, and root completion. The nearby comment must explicitly distinguish this
private handle from current-directory state.

### 6.3 `asyncfatfs.c`: exact descend/ascend and free-before-retire

On child-directory discovery validate its cluster, copy complete info, save SFN
pointer+cluster, release parent finder, then bind child. On exhaustion read
only child `..`, bind parent, and scan until an object is a directory whose SFN
pointer equals the saved child pointer *and* first cluster equals saved cluster.
No display-name or alias relookup, no “first matching cluster.” Exhaustion,
broken `..`, kind/pointer/cluster mismatch return `UNSUPPORTED_LAYOUT`.

For every selected file or emptied directory, seed a FAT cursor from its first
cluster, validate current/next values, read next, clear one FAT entry, update
`lastClusterAllocated` using the just-freed cluster, and yield. Only when the
chain ends does shared name retirement run. Directory `fileSize == 0` is never
used to skip its chain. Zero cluster is legal only for empty regular files.
FAT/cache error is `IO_ERROR`; loops/reserved/free links are layout error.

The root test compares the current scanned-directory SFN pointer with copied
root SFN pointer, not clusters. This exact condition, name-run state, FAT
ordering, and non-transactional partial-failure rule need adjacent comments.

### 6.4 `asyncfatfs.c`: bounded corruption termination and one finish path

Reuse unused delete bookkeeping as a saturated allowance derived from
`afatfs.numClusters`. Consume a unit per successful directory descent and per
released cluster; exhaustion is `UNSUPPORTED_LAYOUT`. This is structural work,
not a wall-clock timeout. A valid tree is bounded by card capacity; cyclic
topology cannot poll forever.

Route validation, finder, malformed-LFN, FAT/cache, budget/layout, and success
through `afatfs_deleteTreeFinish()`. Track finder-active state so finish closes
it only when owned; then unlock cache, initialize handle, and invoke copied
callback once. Rejected starts still callback never. Update public header
comment and `afatfs_getDeleteTreePhase()` documentation accordingly.

## 7. Phase 4 — product callers

### 7.1 `filesystem.c`: singular exact slot resolver

Replace plural `filesystem_deleteSlotDirectoriesStart/tick` with singular
`filesystem_deleteSlotDirectoryStart/tick`. Remove unused active
`bank_scene_namespace` state. The worker must open `.`, scan all immediate
objects, parse using root numbered-folder rules, retain one full candidate but
continue to prove no duplicate/file conflict, close the scan handle on every
path, then delete only exactly one complete candidate and wait its result.

Kit enables legacy alias fallback; Scene and Bank disable it. Inputs are only
current parent, slot, and policy. Outputs are busy/done/error plus named
diagnostic result. It accepts zero candidates as new-slot success. It never
takes a display path or opens by name to rediscover target. Error/duplicate
paths close their handle before `FS_STATUS_ERROR`.

Replace current timeout-to-error with a timeout-observed latch. On expiry,
record outer/native phase (`TOut`/`TDel`), force eventual error, but stay owner
until scan close or native callback. There is no cancellation API; never free
the facade while delete is active and never mkdir after a timed-out callback.
Comments must state this is observation, not abort.

### 7.2 Kit and root Scene

At existing Save phases 4/5 call the renamed resolver. Preserve sequence:
enter `/Kit` or `/Scene`; prove zero/one/duplicate; successfully delete one;
then mkdir/write. Replace comments claiming every same-slot directory is
removed. Resolver error calls `filesystem_finish(ERROR)` before phase 8, so no
HCNAMES/index publish occurs. No new cache/name storage is allowed.

### 7.3 Bank direct delete-recreate

Remove Bank temporary/promotion state and code:

- `op_save_bank_tmp_display_name`, `op_save_bank_old_display_name`,
  `op_save_bank_rename_open_name`, scratch nonce/attempt/collision fields and
  their reset/request initialization;
- `filesystem_makeBankScratchDir`, `filesystem_prepareBankScratchDirs`, and
  scratch collision scan;
- temp mkdir/reopen flow, old-name rename, temp-name rename, and promotion
  phases/comments.

After existing HCNAMES preload and `/Bank` entry, invoke the Bank singular
resolver. After successful zero/one result, create final
`op_save_bank_dir_display_name` directly, enter it, write `bankset.bcg`, and
reuse existing selected-child Scene writing. `op_save_bank_dir_open_name` is
the only short alias retained to reopen this final Bank after child writers
return to root. At final child/empty mask, return root and run existing Bank
identity, settings dirty, HCNAMES, index rebuild, and final-sync sequence.

Inputs: accepted root slot/display/mask. Outputs: one final Bank or error. On
duplicate/conflict/delete failure, create no Bank and publish no metadata; a
native partial deletion is explicitly nonrecoverable, with no `tmp*`/`old*`
fallback. Preserve existing `bank_set*`,
`filesystem_recordSavedBankDirectory`, settings, HCNAMES, and index ordering
only after every file is closed. This removes at least named 49+49+13 bytes
before alignment; measure full linked delta and do not repurpose it.

## 8. Phase 5 — remove false fallback APIs

Remove no-op/unsupported public APIs with no live callers:
`afatfs_funlink`, parent-relative child lookup/create, move, copy-tree, and
begin/commit/abort tree-replace. Remove associated operation enum/union/
dispatcher/empty-continuation scaffolding together. Keep implemented component
open/create, iterator, precise remove, rename, and delete-tree. A future move
or transaction requires a separately approved API/RAM design.

Remove the `#if 0` firmware recursive walker in `filesystem.c` after callback
migration. It retains obsolete stacks, alias/display behavior, and an unsafe
fallback narrative; history preserves archaeology. Record released memory but
do not reuse it.

## 9. Documentation change after implementation

Update `ASYNCFATFS_REFERENCE.md`, `FILESYSTEM_SPEC.md`, the handoff, and only
confirmed `MEMORY.md` facts. Document full VFAT-run identity, malformed-run
failure, FAT16 root handling, one-target duplicate policy, direct Bank
delete-recreate, diagnostic-but-not-cancel timeout, and non-atomic failure.
Remove claims about production `old*`/temporary promotion and stub APIs. Do
not claim power-loss atomicity or unperformed hardware validation.

## 10. Acceptance matrix

Low-level fixtures, each foreground-pumped then `afatfs_sync()`/desktop remount:

1. SFN-only and LFN root file removal.
2. LFN run crossing sector/cluster boundary.
3. Empty directory; files; nested directories; `.DS_Store`; mixed LFN/SFN.
4. FAT16 child under fixed root and FAT32 child under root cluster.
5. Malformed LFN: `CORRUPT_LFN_RUN`, no guessed entry removal.
6. Broken `..`, duplicate cluster/pointer, and cyclic relation:
   `UNSUPPORTED_LAYOUT`, no infinite poll/no C recursion.
7. Injected FAT/cache error after partial work: one non-OK callback and no
   product replacement.
8. Exhausted handle pool: false start and no callback.

For every success verify no LFN/SFN entry or FAT chain remains, unrelated
objects are unchanged, and no completed private handle retains/locks cache.

Product fixtures:

| Case | Required outcome |
|---|---|
| Occupied Kit | Fresh Kit reloads; old tree gone; neighbours unchanged. |
| Occupied nested Scene | Whole old Scene gone before new tree; neighbours unchanged. |
| Occupied multi-child Bank | Whole old Bank gone before direct final tree; no `tmp*`/`old*`; reload works. |
| Empty target slot | No delete start; one numbered tree created. |
| Duplicate same-slot directory / same-slot file | Fail before deletion or creation; card remains diagnosable. |
| Legacy Kit alias | Exactly one Kit eligible; Scene/Bank equivalent rejected. |
| Delete error or observed timeout | No mkdir, HCNAMES, Bank metadata, or index rebuild. |

Run audio-under-save only after this correctness matrix. Before implementation
approval, report exact memory sizes/BSS/stack deltas. Before closeout, confirm
every changed `.c` state/helper and `.h` API has adjacent comments stating what,
why, inputs, outputs, ownership, failure behavior, and affiliates; fold code
into existing scanner/state-machine helpers rather than adding one-line
accessors.

## Follow-up — boot now falls back; Load menus fail on missing /.hcnames (2026-08-19, updated)

### Current state

- Rebuilt with DEV_MODE_DIAGNOSTIC re-enabled: LXRV2_lxr02.img is now 380,776 bytes.
- Boot no longer stops at the Kit-quarantine gate in this run; it reaches the Scene/Kit fallback.
- bootlog.bin is 8 bytes B008S03I (Bank 008 / Scene 03 / Instrument). This is stale evidence from the prior Bank-load attempt; the fallback boot did not write a new boot-failure record.
- /.hcnames, /.hcprms1, and /.hcprms2 are still absent from the card root.

### New symptom

Entering the load browsers now errors before any row is shown:

- Load:[Scene] -> HNsL01
- Load:[Kit] -> HNkL01

### What the error codes mean

HNsL / HNkL are the FS_INTERNAL_OP_LOAD_HCNAMES_SCENE / _KIT operation prefixes (filesystem.c error-prefix table). The trailing 01 is op_phase.

In filesystem_residentNames_tick():

- phase 0 opens /.hcnames read-only with afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME, "r", ...);
- phase 1, in LOAD mode, calls filesystem_finish(FS_STATUS_ERROR) immediately when that open callback returns NULL.

So HNsL01 and HNkL01 are the "register is missing or unreadable" result at the open boundary. They are not a scan/parse failure and do not go through the delete/rename/remove code changed by this recipe.

### Why this is the missing /.hcnames, not a read-path regression

The HCNAMES LOAD path touches only:

- afatfs_chdir(NULL) — unchanged;
- afatfs_fopen_lfn(".hcnames", "r") — the raw-finder create-file LFN scan (afatfs_createFileContinue / afatfs_lfnScan*) is unchanged by this recipe;
- filesystem_readTextLine() / afatfs_fclose() — unchanged.

The only HCNAMES-adjacent code this recipe changed is the absence-proof probe filesystem_hcnamesProbe_tick(), which uses the object finder. That probe is part of the HCNAMES update/create path, not the LOAD path that emits HNsL01 / HNkL01. The register is simply absent on the card, so the LOAD path correctly errors.

### Why the register is absent

The earlier failed boots (KQ003KST, then B008S00I, B008S03I) all abort before the Bank/Scene commit stage that reads and writes /.hcnames. The failed-boot recovery writes only /bootlog.bin; it does not delete or recreate /.hcnames. The Session 052 register files are no longer on the card, so there is nothing to read until a successful load/save commits the register.

The fallback boot reached a Scene/Kit; whether that fallback actually completed its HCNAMES register write is the open question. The reported HNsL01/HNkL01 occur on menu entry, before that update path is involved.

### What to do

1. Restore a valid /.hcnames to the card (it is tracked in git) and re-test Load:[Scene] and Load:[Kit]. If the two errors clear, the asyncfatfs open and HCNAMES read path are healthy and the symptom was purely the missing file.
2. Exercise the HCNAMES write/update path once (a Scene or Kit save/load that commits the register) and watch for HNPrb, HNDup, HNsU, or HNkU. That path uses the changed object finder through filesystem_hcnamesProbe_tick(), so it is the correct place to confirm the probe still returns ABSENT and creates the file.
3. Keep the boot Kit-quarantine removal from KIT_PARSE_BOOTLOCK_RESOLVE.md as the next source change; it is still unapplied and still addresses the earlier KQ... boot gate.
