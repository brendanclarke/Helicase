# Session 053 Pre-Planning

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Branch/state**: `dev-ph3-autosave-ph2`, Session 052 close-out in progress.
**Purpose**: Scope the next session. Confirm what is (and is not) still open in
`AUTOSAVE_PHASE2_PLAN.md`, stage applicable `SCOPING_TARGETS.md` bugfixes and
refactors, and assess the recursive-delete draft. No code is changed by this
document.

---

## 1. Headline

`AUTOSAVE_PHASE2_PLAN.md` is **mostly but not fully** closed. Steps 0-5 are
done (the last, selective Bank Load, closed in Session 052). Two things remain
from it: **Step 6 (Load/Save exclusion)** and a small set of **Step 3 hardware
evidence** items. The highest-priority new work is the **AsyncFATFS recursive
delete repair**, which is already partially implemented in the code and is the
prerequisite for correct overwrite of Bank, Scene, and Kit libraries.

---

## 2. AUTOSAVE_PHASE2_PLAN.md — closure status

The plan's execution state (reconciled against Sessions 045-052):

- **Step 0 (documentation)**: complete.
- **Step 1 (trace observability)**: complete (Session 047).
- **Step 2 (Phase 1 scalar matrix)**: complete and accepted.
- **Step 3 (settings/provenance)**: static reconciliation complete; the
  one-second `settings.cfg` writer is user-verified. **Remaining hardware
  evidence only** (see below).
- **Step 4 (identity/completeness contracts)**: settled (Bank slot/name are
  payload; a structurally-valid record may still be an incomplete snapshot).
- **Step 5 (whole-object boundaries)**: complete. Instrument (048),
  InstrumentMrp (048), Kit (049), KitMrp (049), root Scene without Pattern
  (049/050), and selective Bank Load (052) are all implemented; Bank Load was
  the last to be hardware-verified.
- **Step 6 (Load/Save exclusion)**: **NOT implemented.** Deferred to a later
  isolated session.
- **Step 7 (close-out)**: ongoing per-session.

### 2.1 Still open from AUTOSAVE_PHASE2_PLAN.md

1. **Step 6 — Load/Save exclusion.** The plan requires this to be done last and
   in isolation, in two sub-changes:
   - prevent a *new* AutoSave writer start while a Load/Save page owns the
     filesystem facade (with the explicit check that dirty bits already present
     before the page opened still drain afterward);
   - separately, defer physical Load/Save *entry* while an AutoSave transaction
     is mid-flight.
   The trace (`A/V/M/C/P/T`) already exists to prove which path is taken, so
   this is now diagnosable rather than the blind regression it was in 045.
2. **Step 3 remaining hardware evidence.** Independent fixtures for:
   - root Scene Load and root Scene Save provenance (source rows);
   - partial Bank Load and partial Bank Save provenance;
   - AutoSave OFF-to-ON lifecycle (idle re-arm, in-flight transaction reaches
     its close boundary).
   These are fixture/evidence items, not code changes, and can ride along with
   any next hardware pass.
3. **Step 5.4 regression check.** Explicitly prove that Menu preview/selection
   state alone never produces an AutoSave dirty mark (only a publicly completed
   Scene load does). This was the exact 045 failure that got mistaken for a fix.

---

## 3. Primary Session 053 target — AsyncFATFS recursive delete

### 3.1 Current code state (verified against source)

Recursive delete is **already implemented**, not greenfield:

- `afatfs_deleteTree(const afatfsObjectId_t *root, afatfsResultCallback_t cb)`
  exists in `asyncfatfs.h`/`.c` and deletes one concrete directory object by
  identity (not by re-resolving display text), including nested children, LFN/SFN
  entry runs, cluster chains, parent return, and an exactly-once callback.
- `afatfsObjectId_t` is the unified object identity returned by
  `afatfs_findNextObject()`.
- `filesystem.c` already has a delete-slot state machine that scans the parent,
  matches the exact same-slot directory via
  `filesystem_directoryObjectMatchesSlot()`, captures `op_delete_slot_target_id`,
  and calls `afatfs_deleteTree()` (`filesystem.c:12713-12792`).
- Additional primitives already exist: `afatfs_moveObject()`,
  `afatfs_copyObjectTree()`, and a transactional
  `afatfs_beginTreeReplace/commitTreeReplace/abortTreeReplace`.

The remaining problem is a **correctness defect** in `afatfs_deleteTree()` (or
its filesystem.c caller), observed as "overwrite Save may leave the old slot
folder in place" / the "duplicate-folder defect" in Bank, root Scene, and Kit
replacement. This is the item pinned in `SCOPING_TARGETS.md` ("repair
`afatfs_deleteTree()`").

### 3.2 Draft assessment — `ASYNCFS_RECURSIVE_DEL_UNPLANNED.md`

**Advice: the draft cannot be used as an implementation plan as written. Use it
only as a requirements / safety / test-plan reference. It needs major revision
before it would match the current code.**

Reasons:

1. **The API it proposes is obsolete.** The draft designs a new
   `afatfs_deleteObject_lfn()` / `afatfs_deleteObjectByInfo()` pair. The code
   has already moved to `afatfs_deleteTree(afatfsObjectId_t*)` (identity-based),
   which is the evolved form of "delete by identity" the draft was heading
   toward. Greenfield-implementing the draft's API would duplicate what exists.
2. **Its "current code facts" are stale.** It describes `afatfsObjectInfo_t`
   as the exposed type and `AFATFS_MAX_OPEN_FILES`/dot-prefix-skip behavior that
   has since changed (the code now uses `afatfsObjectId_t`, and has
   `moveObject`/`copyObjectTree`/`treeReplace` the draft never anticipated).
3. **Its opening framing is now wrong.** The draft says directory overwrites
   "will preserve directories ... and tolerate stale extra files." The accepted
   direction is the opposite: **delete-recreate** (recursively retire the old
   exact-slot directory, then create the replacement). `SCOPING_TARGETS.md`
   explicitly forbids the `oldNNN-xxxx` rename / boot-cleanup workaround the
   draft's own transactional-replace note gestures toward.

What is still valuable and reusable from the draft:

- the **requirements list** (retire one exact object: nested children, LFN/SFN
  entry runs, cluster chains, parent return, exactly-once terminal callback);
- the **safety rules** (never delete root, never delete `.`/`..`, component-only
  names, bounded non-recursive traversal, no replacement after delete failure,
  no save success before delete+recreate+write+close+sync);
- the **test plan** (root LFN file, SFN-only file, empty dir, dir with files,
  nested dirs, dot-prefixed files like `.DS_Store`, mixed LFN/SFN children,
  over-depth clean failure, remount/desktop confirm, unrelated objects
  untouched).

### 3.3 Implied Session 053 work (to be detailed later)

Diagnose the existing `afatfs_deleteTree()` duplicate-folder defect and repair
it, then confirm the Bank/Scene/Kit overwrite callers use it correctly, rather
than writing a new delete API. The draft's requirements/safety/test plan are the
acceptance checklist for that repair.

---

## 4. Other staged bugfixes / refactors (general, from SCOPING_TARGETS.md)

Candidate items to schedule, in no committed order yet:

- **Runtime Bank Load switches the playing Scene.** Preserve the pre-load active
  Scene on a runtime Bank Load while keeping boot's saved-default-Scene restore.
  Now applicable because Bank Load/Save is otherwise stable (Session 052).
- **Load/Save exclusion (Step 6 above).** Last, isolated, after the recursive
  delete and other filesystem work stabilizes.
- **Boot Kit-quarantine refactor (KQ019KST).** Move `kitset.kcg` parsing /
  six-member opens / Kit quarantine out of the boot index pass; validate at
  load time. Scoped in `SCOPING_TARGETS.md` (Session 052).
- **Bank Save present-mask union (P1).** Change the direct overwrite at
  `filesystem.c:13945` to a union with the retained mask.
- **Boot settings-mark redundancy (P2).** Accept the per-boot `settings.cfg`
  rewrite or gate the mark on `fs_settings_runtime_ready`.
- **Pinned AutoSave reader rule.** (Future, not immediate) the HCPRMS reader must
  resolve each Instrument's three-byte type token before its parameter matrix;
  slot-6 Choke vs non-Choke decay ownership. This is a reader concern, not a
  writer change.

Note: the "InstrumentMrp blank `kit` row" entry in `SCOPING_TARGETS.md` is now
**resolved** (Session 051) and should be treated as stale when staging.

---

## 5. Suggested staging order (general, to be confirmed)

1. **AsyncFATFS recursive delete repair** — unblocks correct Bank/Scene/Kit
   overwrite; do first because other save work depends on it.
2. **Runtime Bank Load active-Scene preservation** — small, now-testable.
3. **Step 3 + Step 5.4 hardware evidence** — can be captured alongside the above
   rebuilds.
4. **Load/Save exclusion (Step 6)** — last and isolated.
5. Defer (separate later sessions unless chosen): boot Kit-quarantine refactor,
   P1 Bank Save union, P2 boot settings mark, HCPRMS reader rule.

---

## 6. Decisions needed before detailed planning

- Confirm the actual observed duplicate-folder symptom and a minimal reproducing
  fixture (Bank, Scene, and Kit) so the `afatfs_deleteTree()` defect can be
  localized before writing the repair.
- Confirm whether `afatfs_beginTreeReplace/commitTreeReplace` (the `old_XXXX`
  rename path) is to be left dormant/unused in favor of delete-recreate, per the
  `SCOPING_TARGETS.md` prohibition.
- Confirm the Session 053 priority: recursive delete only, or recursive delete
  plus the runtime active-Scene fix and the leftover hardware-evidence fixtures.
