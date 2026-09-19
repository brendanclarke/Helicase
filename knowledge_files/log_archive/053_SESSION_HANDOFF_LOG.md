# Session 053 Handoff — Recursive Delete Reimplementation And Boot/Overwrite/AutoSave Diagnosis

DATE: 2026-08-19
SESSION GOAL: Reimplement AsyncFATFS recursive delete and the Bank/Scene/Kit
  overwrite callers (the pinned duplicate-slot target), then diagnose the
  follow-on boot and load/save/AutoSave failures that surfaced.
COMPLETED: See sections below.
VERIFIED ON HARDWARE: Partial. The ARM build is clean; hardware passes
  reproduced boot timeouts, HCNAMES creation (after the LFN fix), and several
  load/save/AutoSave defects. The recursive-delete overwrite acceptance matrix
  is NOT passing yet.

CHANGES THIS SESSION:
- Core/Hardware/SD/asyncfatfs/asyncfatfs.c: recursive delete reimplementation,
  object-finder LFN pointer capture + validation, structured remove/rename
  results, removed funlink/move/copy/replace; LFN shape validator relaxed.
- Core/Hardware/SD/asyncfatfs/asyncfatfs.h: object identity/finder LFN pointer
  fields, deleteTree signature, structured result callbacks, removed APIs.
- Core/Hardware/SD/filesystem.c: singular delete-slot resolver, direct Bank Save
  delete/recreate, structured remove/rename result gates, removed Bank tmp/old
  promotion and retired firmware recursive walker.
- knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md: updated to the
  new delete/recreate and structured-result contract.
- knowledge_files/specification_reference/FILESYSTEM_SPEC.md: updated to direct
  exact-object delete/recreate for Bank Save.
- SCOPING_TARGETS.md: pinned duplicate-slot target marked implemented-but-
  unproven; Session 053 deferred targets folded in (Phase-2 Step 6, Step 3,
  Step 5.4; HCNAMES source-on-save; ScnS05; Kit Save; boot timeouts/freezes;
  AutoSave boot-capture/read model).
- MEMORY.md: volatile notes updated.

KNOWN ISSUES INTRODUCED: None beyond the deferred/observed defects below; the
  recursive-delete source itself builds with no net SRAM increase.
KNOWN ISSUES RESOLVED: The boot fallback HCNAMES-creation stall caused by the
  too-strict LFN shape validator (restores Scene/Kit/Instrument boot loads).

NEXT SESSION RECOMMENDED GOAL: Fix the overwrite resolver (ScnS05) and the
  HCNAMES source-on-save provenance defect; then re-run the overwrite
  acceptance matrix.
BLOCKERS: Hardware-only validation of the recursive-delete matrix; a minimal
  overwrite fixture; a decision on the boot Bank Load budget vs. the AutoSave
  reader milestone.

CRITICAL REMINDERS FOR NEXT SESSION:
- The recursive-delete source is UNPROVEN: Scene overwrite returned ScnS05 even
  though slot 019 was replaced, and Kit Save did not materialize a library Kit.
  Do not treat delete/recreate as accepted until the matrix passes.
- Boot Bank Load still times out (B012S09I at 20 s); this is separate from the
  boot Kit-quarantine (KQ...) gate and from the AutoSave boot-capture gap.
- The AutoSave boot Bank section is intentionally empty because tracking is
  enabled only after the boot Bank Load. Do not fix it until the AutoSave
  reader goes in (see SCOPING_TARGETS.md).
- HCNAMES source provenance is not set on Save; fix before deleting the test
  report.

---

## 1. Scope

This session delivered four documents plus source changes:

- `SESSION_053_PRE_PLANNING.md` — scoped the recursive-delete repair and listed
  the still-open AUTOSAVE Phase-2 items.
- `RECURSIVE_TREE_DELETE_REIMPLEMENT.md` — the implementation log and acceptance
  matrix for the delete/recreate rework.
- `KIT_PARSE_BOOTLOCK_RESOLVE.md` — the plan to remove boot Kit quarantine.
- `LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md` — the hardware element test report.

## 2. Recursive delete reimplementation (RECURSIVE_TREE_DELETE_REIMPLEMENT.md)

Source changes, all build-verified:

- `afatfs_deleteTree()` now takes a complete `const afatfsObjectInfo_t *`,
  copies every physical LFN/SFN pointer, validates the VFAT run, frees each
  child cluster chain BEFORE retiring its name run, handles FAT16 root binding
  (`.. == 0` -> FAT16 root type), and is bounded by a saturated structural
  budget (descends + released clusters) instead of a wall-clock timeout.
- The object iterator captures the first LFN pointer plus up to three validated
  following physical pointers; it validates LAST ordinal, descending sequence,
  checksum, entry shape, and run completeness. Malformed runs stay browsable as
  SFN but are flagged `lfnMalformed`; destructive clients reject them.
- One shared name-retirement continuation batches only pointers in the currently
  cached sector, yields between batches, and performs no FAT work. Regular
  removal releases at most one FAT cluster per continuation.
- `afatfs_removeObjects_lfn` / `afatfs_removeObject` / `afatfs_renameObject_lfn`
  now use `afatfsResultCallback_t`; non-OK results forbid caller create/publish.
- `filesystem.c` gained a singular delete-slot resolver that proves zero/one
  candidate, rejects duplicates/same-slot files/malformed objects, and deletes
  only the one captured object. Kit allows the legacy short-alias fallback;
  Scene and Bank do not.
- Bank Save now deletes the exact old root Bank and creates the final numbered
  Bank directly; all `tmp*`/`old*` promotion names and scratch collision scans
  were removed.
- Removed the no-op/unsupported `afatfs_funlink`, parent-relative child, move,
  copy-tree, and begin/commit/abort tree-replace APIs, plus the retired firmware
  recursive walker.

Final ARM layout: ObjectId=120, ObjectInfo=124, ObjectFinder=96, DeleteTree=388,
File=180, operation union=136 bytes. The expanded delete state is owned once by
`afatfs.deleteTreeState`; the handle stores only a pointer.

Build/RAM: text=379,660, data=396, bss=94,612 (no net SRAM increase vs the
Session 052 baseline bss=94,612). Config at close: DEV_MODE_DIAGNOSTIC=0,
DEV_MODE_LOGGING=1, BOOT_FILESYSTEM_TIMEOUT_MS=20000. Image 380,072 bytes.

Acceptance status: the low-level fixture matrix (LFN/SFN, cross-sector LFN,
FAT16, malformed LFN, broken parent, injected FAT/cache error, exhausted handle
pool) and the product fixtures (occupied Kit/Scene/Bank, empty slot, duplicate,
legacy alias, delete error/timeout) are NOT yet hardware-validated. The
overwrite pass found Scene `ScnS05` and Kit Save non-materialization, so the
pinned target stays open (see SCOPING_TARGETS.md).

## 3. LFN validator repair

After the reimplementation, boot fell back to a Scene/Kit and Load menus threw
`HNsL01`/`HNkL01` (missing `/.hcnames`). Root cause: the new
`afatfs_objectLfnEntryShapeIsValid()` required `0x0000` before `0xffff` and
rejected non-ASCII units, so valid host-written long names padded with `0xffff`
(no `0x0000` terminator) were classified malformed; the object iterator then
downgraded `displayName` to the SFN alias, which broke `storage_parseNumberedFolder`
and `filesystem_nameStartsWithKitSpace`.

Fix: treat both `0x0000` and `0xffff` as end-of-name, and stop rejecting
non-ASCII (the name builder renders them as `_`). This restored boot fallback and
HCNAMES creation. The destructive `lfnMalformed` flag is retained for delete.

## 4. Boot / AutoSave / overwrite diagnosis (LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md)

Hardware pass observations:

- Boot Kit-quarantine first failed `KQ003KST`, then the boot progressed to Bank
  Load and timed out `B008S00I`/`B008S03I`/`B012S09I` (embedded-instrument stage).
  This is the boot Bank Load budget, separate from Kit quarantine and separate
  from AutoSave.
- AutoSave records are created and the drain machinery works (A/B ping-pong,
  V/M/C/P/T, generations, re-dirty/merge), but the Bank section stays empty
  (present_mask/active_scene/voice_edit = 0) because `autosave_setMutationTracking
  Enabled(1)` runs only after the boot Bank Load. Deferred to the AutoSave reader
  milestone.
- HCNAMES source provenance is NOT updated on Save: Scene saved to slot 031 kept
  source 004 (loaded), Kit kept 013, Instrument save wrote the pool file but
  published no `@`. Primary defect.
- Overwrite defects: Scene `ScnS05` (delete-slot resolver phase) though slot 019
  was physically replaced; Kit Save did not materialize a library Kit; Kit Save
  menu empty (cache tag); Instrument overwrite unconfirmed; Bank Save entry
  freeze; boot freeze with `.hcprms2` truncated at 32 KiB.

## 5. Documentation close-out

- `ASYNCFATFS_REFERENCE.md` and `FILESYSTEM_SPEC.md` updated to the exact-object
  delete/recreate and structured-result contract.
- `SCOPING_TARGETS.md` updated: pinned duplicate-slot target marked implemented-
  but-unproven; Session 053 deferred targets folded in (Phase-2 Step 6 Load/Save
  exclusion, Step 3 evidence, Step 5.4 regression; HCNAMES source-on-save,
  ScnS05, Kit Save, boot timeouts/freezes; AutoSave boot-capture/read model).
- `MEMORY.md` volatile notes updated.

## 6. Remaining work

See SCOPING_TARGETS.md Session 053 deferred targets. Priority order: overwrite
resolver ScnS05 + HCNAMES source-on-save, then Kit Save materialization and
Kit Save menu cache, then the boot Bank Load budget and the two freezes. The
AutoSave boot-capture fix waits for the AutoSave reader.

## 7. Superseded working documents

The following session working docs are superseded by this handoff and
`SCOPING_TARGETS.md` and may be deleted after this session:

- `SESSION_053_PRE_PLANNING.md` - folded into SCOPING_TARGETS.md (Phase-2 Step 6
  Load/Save exclusion, Step 3 hardware evidence, Step 5.4 regression check).
- `RECURSIVE_TREE_DELETE_REIMPLEMENT.md` - contract and RAM numbers preserved in
  sections 2-3 above and in SCOPING_TARGETS.md; acceptance matrix still pending.
- `KIT_PARSE_BOOTLOCK_RESOLVE.md` - the boot Kit-quarantine removal plan is
  preserved in the SCOPING_TARGETS.md "boot sanitation versus load validation"
  target; it is still unapplied.
- `LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md` - defects folded into SCOPING_TARGETS.md
  Session 053 deferred targets.
