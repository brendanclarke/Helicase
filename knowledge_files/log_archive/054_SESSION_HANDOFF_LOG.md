# Session 054 Handoff — Recursive-Delete Root-Cause Repair, HCNAMES Source-on-Save, Scene-Pattern Desync

DATE: 2026-08-20 to 2026-08-21
SESSION GOAL: Fix the two root-caused bugs left open by Session 053's
  recursive-delete reimplementation (delete-resolver spurious error;
  HCNAMES source not staged on Save), and add durable diagnostics for the
  remaining not-yet-root-caused defects from `LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md`
  (Kit Save non-materialization, Kit Save menu empty cache, Bank Save entry
  freeze, boot Bank Load timing, Instrument overwrite content verification).
COMPLETED: The two planned fixes, plus — via iterative card-driven diagnosis
  using the new instrumentation — found and fixed the actual `ScnS05` native
  delete-tree defect across three further bugs (#3/#4/#5). Also fixed an
  unrelated Scene-Pattern playback/UI desync found during retest, and a
  self-inflicted boot-hang regression in a watchdog diagnostic feature added
  and fixed within this same session.
VERIFIED ON HARDWARE: Partial, as of this session's own close. The Bug #1/#2
  fixes and the Bug #3/#4/#5 native-delete repair were each confirmed by a
  dedicated card retest at the time they were made (see §3 below for the
  round-by-round trace evidence). The Scene-Pattern fix and the IWDG
  regression fix were NOT hardware-verified before this session closed — both
  are confirmed by Session 055's continued testing; see
  `055_SESSION_HANDOFF_LOG.md`.

CHANGES THIS SESSION (commits `b9bdb92`, `0827b40`):
- `Core/Hardware/SD/asyncfatfs/asyncfatfs.c` / `.h`: the descend/ascend
  identity-invariant repair (`descendTarget`/`parentEntry` snapshot-at-descend,
  restore-at-ascend on the persistent `afatfs.deleteTreeState` singleton);
  removal of the redundant `AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF(_LOOP)`
  re-scan; the `RESUME_PARENT`/`REOPEN_PARENT` rework; the 17-value
  `afatfsDeleteTreeFailureSite_e` native failure classification and its
  accessor; the short-lived `'Y'` `SCAN_PARENT_DIAG` probe (added, then fully
  retired in the same session once its root cause was fixed outright — its
  fields/accessors/call site are gone, only the trace-format documentation
  and decoder entry remain so already-captured evidence still decodes).
- `Core/Hardware/SD/filesystem.c`: both delete-slot completion-gate fixes
  (Bug #1); `filesystem_setResidentSource()` staging added to all four Save
  paths plus the new Instrument HCNAMES hand-off and its supporting
  type-filtered index-rebuild path (Bug #2); the shared
  `filesystem_pollPhaseStall()` helper and its three call sites (delete-slot,
  Bank Save entry, AutoSave drain); `fs_delete_slot_reason_t` and exhaustive
  per-branch tagging; the `'O'` `SAVE_LIFECYCLE` and `'E'`
  `OPERATION_ERROR` trace call sites; the Instrument content CRC32C
  fingerprint accumulator; the boot Bank Load per-instrument timing
  breadcrumb (reused `'N'` stage); the Scene-Pattern fix call site
  (`seq_alignActivePatternToScene(op_bank_active_scene)` +
  `menu_setShownPattern()` at the Bank Load phase-20 commit); the rewritten,
  correctly-ordered `filesystem_devIwdgStart()` plus the `fs_devwdg_armed`
  gate and the mandatory `filesystem_blockPoll()` feed.
- `Core/Hardware/SD/filesystem.h`: declarations for the above.
- `Core/Sequencer/sequencer.c` / `.h`: new
  `seq_alignActivePatternToScene()` — realigns `seq_activePattern`/
  `seq_pendingPattern` to a Scene selection some other owner already made,
  deliberately without the LED-notify/MIDI-program-change/note-off side
  effects `seq_selectActivePattern()` carries.
- `Core/Bank/Scene/Autosave.c` / `.h`: `autosave_crc32cByteUpdate()`, a
  one-line public pass-through so `filesystem.c` can accumulate the
  Instrument content fingerprint (the underlying CRC32C helper is otherwise
  file-local to `Autosave.c`).
- `Core/Bank/Scene/AutosaveTrace.h`: the `'X'`/`'O'`/`'E'` stage
  definitions and their flag/value layouts (the retired `'Y'` layout stays
  documented, no live producer).
- `Core/Menu/menu.c`: the Menu-side `'O'` `REQUEST` witnesses (three Kit
  cache-domain branches, one Bank Save entry bracket).
- `config.h`: `DEV_LOGGING_IWDG` added, defaulting to **0** (was briefly 1
  during this session's own regression, corrected before close).
- `STM32F765VIHx_FLASH.ld`: new `.devwdg_noinit` SRAM2 section (≤32 B
  approved ceiling; holds a 12-byte post-reset capsule only when the flag is
  on).
- `main.c`: `filesystem_devIwdgBootCheck()` call site.
- `tools/decode_devlogs.py`: decoder support for `'X'`/`'O'`/`'E'` (and the
  now-retired `'Y'`).
- `tools/devlog_unpack.py`: new — a low-token sibling of `decode_devlogs.py`
  sharing its lookup tables, used throughout this session's card retests.
- `SD_CARD/Scene/`: six damaged root Scene folders repaired (not a source
  change; see §5).

KNOWN ISSUES INTRODUCED: None left standing. `DEV_LOGGING_IWDG`'s first
  version caused an indefinite boot-splash hang; fixed within this same
  session and shipped disabled by default (never exercised on hardware
  since the fix — see Session 055 status).
KNOWN ISSUES RESOLVED: The pinned recursive-delete/overwrite correctness
  target (`ScnS05` and the class of bugs behind it); HCNAMES source
  provenance on Save for all four element types; the Scene-Pattern
  playback/UI desync; six structurally-damaged root Scene folders on the
  test card (repaired, not the underlying non-atomic-save cause, which
  remains open — see `SCOPING_TARGETS.md`).

NEXT SESSION RECOMMENDED GOAL: Hardware-verify the Scene-Pattern fix and the
  disabled-by-default IWDG feature; re-run the full four-element Load/Save/
  AutoSave provenance fixture explicitly against the new `'O'` trace records.
  (Session 055 in fact picked this up and went further — see its log.)
BLOCKERS: None structural. The low-level recursive-delete acceptance matrix
  (malformed LFN, cyclic/broken-parent, injected FAT/cache error) still needs
  dedicated fixtures, not just incidental product-level exercise.

CRITICAL REMINDERS FOR NEXT SESSION:
- **Always `make clean` after any `config.h`/header edit.** The Makefile has
  no header dependency tracking (no `-MMD`/`-MP`/`-include *.d`). This
  session lost real time to a `DEV_LOGGING_IWDG` flip that silently produced
  a binary with the old flag value. Fix the Makefile, or keep doing this by
  hand.
- The recursive-delete fix depends on a subtle invariant
  (`file->directoryEntryPos` must always identify the currently-open
  directory; `op->currentTarget` is overwritten by every object a descended
  child itself deletes). Do not "simplify" `RESUME_PARENT`/`REOPEN_PARENT`
  again without re-reading §3 below — this exact class of well-intentioned
  simplification broke the invariant twice already in this same session.
- `AUTOSAVE_TRACE_RECORD_COUNT` is still the temporary 2,048-record
  expansion; revert-vs-keep is an open decision in `SCOPING_TARGETS.md`.

---

## 1. Scope

This session delivered five working documents (all superseded by this log and
`SCOPING_TARGETS.md`, safe to delete):

- `SESSION_054_PREPLAN_ASYNC_RECURSIVE_CLEANUP.md` — re-verified Session 053's
  test report against current source, root-caused Bugs #1/#2 to exact
  code, catalogued six not-yet-root-caused defects.
- `SESSION_054_PLAN_DEFECT_EVIDENCE_FIX.md` — the Bug #1/#2 fix
  implementation plan and record, plus the full diagnostic-instrumentation
  plan (Part 3) that made the subsequent bug hunt possible.
- `AFAT_RECURSIVE_WHITEPAPER.md` — the round-by-round diagnostic trail that
  found Bugs #3/#4/#5, the actual `ScnS05` root cause.
- `SCENE_LOAD_PAT_RESTORE.md` — the Scene-Pattern desync investigation, fix,
  and the IWDG regression found and fixed while retesting it.
- `LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md` — the Session 053-era hardware test
  report this session's Part 1/2 fixes were root-caused against (already
  substantially folded into `SCOPING_TARGETS.md`'s Session 053 section; kept
  in this session's document set because it is what `SESSION_054_PREPLAN`
  re-verified against current source).

## 2. Bug #1 — delete-resolver spurious timeout error

`filesystem_deleteSlotDirectory_tick()` had **two** completion gates
(`FS_DELETE_SLOT_WAIT_CLOSE_SCAN` and `FS_DELETE_SLOT_DELETE_MATCH`) that
treated `op_delete_slot_timeout_observed` — a diagnostic-only 50,000-poll
stall latch, explicitly documented as "observation is not cancellation" —
as a hard failure condition by itself. A nested Scene delete (embedded Kit
dir + 6 instrument files + pattern.pat + effects.fx, each retired at most one
name-run sector batch or one FAT cluster per poll) can legitimately exceed
50,000 polls without any real problem, since most polls return `BUSY`
waiting on SD hardware. Once the counter tripped, the operation reported
`FS_STATUS_ERROR` even when the native delete finished with
`AFATFS_RESULT_OK` afterward — exactly the observed `ScnS05` (Scene 019
correctly replaced on the card, save still reported error).

Fix: both gates now key their pass/fail decision only on the scan's own
error/duplicate latches (`op_delete_slot_scan_error`,
`op_delete_slot_match_count`) or the native result
(`op_delete_tree_result != AFATFS_RESULT_OK`). The stall is now purely
informational, refactored onto the new shared `filesystem_pollPhaseStall()`
helper (§4) and durably traced via `'X'` `PHASE_STALL` instead of only
writing an on-screen error code.

**Status: confirmed fixed** — zero `'X'` `PHASE_STALL`/`DELETE_SLOT` records
appeared in any subsequent retest, meaning the 50,000-tick stall path was
never actually what was failing in the retests that followed (Bugs #3-#5
below were).

## 3. Bugs #3/#4/#5 — the actual `ScnS05` root cause

Bug #1's fix did not stop `ScnS05` from recurring. Rather than guess, this
session built exhaustive failure-reason instrumentation (`fs_delete_slot_reason_t`,
9 values, every `FS_STATUS_ERROR` branch in the resolver tagged) and, when
that still wasn't specific enough, a full 17-value native failure-site
classification (`afatfsDeleteTreeFailureSite_e`) inside
`afatfs_deleteTreeContinue()` itself — the low-level traversal has roughly
seventeen structurally distinct checks that all previously produced the
identical `AFATFS_RESULT_UNSUPPORTED_LAYOUT`. Seven iterative card retests
then pinned the real defect:

1. **Round 1-2**: proved Bug #1's stall path was not the cause this time
   (failure in ~37 ticks); found and tagged an 8th, previously-untagged
   resolver failure branch (`"."`-open failure).
2. **Round 3**: `reason=DELETE_RESULT`,
   `detail=AFATFS_RESULT_UNSUPPORTED_LAYOUT` — confirmed the bug lives inside
   the native traversal, not the resolver.
3. **Round 4**: `failure site=OPEN_DIR_BAD_ROOT_ON_FAT32` at Scene slot 2.
   **Bug #3**: `AFATFS_DELETE_TREE_RESUME_PARENT` reconstructed a resume
   target from `op->parentCluster`, which is only ever assigned by a prior
   descend/ascend — so when the very first object a root-level scan
   encounters is a plain file rather than a subdirectory (e.g. `pattern.pat`
   sorting before `Kit Hard`), no descend has happened yet,
   `parentCluster` is still zero, and the reconstructed target is rejected as
   an invalid FAT16-root reference on this FAT32 card. Fixed: the phase no
   longer reconstructs a target; it resets the scan cursor on the
   already-open handle in place and transitions to `SCAN` directly.
4. **Round 5**: `OPEN_DIR_BAD_ROOT_ON_FAT32` did not recur (Bug #3 confirmed),
   but a new site fired: `SCAN_PARENT_FOR_SELF_EXHAUSTED` at Scene slot 6.
   Added the (short-lived) `'Y'` `SCAN_PARENT_DIAG` probe to disambiguate two
   possible causes.
5. **Round 6**: the probe's seen-count exactly matched the directory's full
   child count, ruling out "parent looked empty" and pointing at the
   identity-match predicate itself. Re-reading the surrounding code found the
   re-scan was unnecessary in the first place — **Bug #4**: the object
   identity being re-derived by the parent re-scan was already sitting,
   complete and (it was believed) untouched, in `op->currentTarget` since the
   moment the child was first discovered. Fixed by deleting the re-scan
   mechanism outright and reading `op->currentTarget` directly (net
   **-904 bytes text, -24 bytes bss**; also retired the now-unreachable
   `'Y'` probe and its two associated failure sites).
6. **Round 7**: `failure site=FREE_CLUSTERS_NEXT_CLUSTER_INVALID` at Scene
   slot 12 (`012 Emott`) — the traversal freeing an *already-freed* cluster
   chain. **Bug #5, the actual defect**: Bugs #3 and #4 had each broken one
   half of the same invariant. `file->directoryEntryPos` is the sole record
   of which directory a handle currently has open, and it distinguishes "the
   delete root just emptied, finish" from "a nested child just emptied,
   ascend." Bug #3's fix cleared that field as a side effect of reusing
   `OPEN_DIR`'s reset sequence (it only needed to rewind a scan, never to
   rebind the handle). Bug #4's fix assumed `op->currentTarget` survived a
   descent untouched — false: the descended-into child's own internal
   deletes overwrite that register with every object *it* processes, so by
   the time of ascend it held the identity of the last-deleted grandchild
   file, not the child directory itself.

   **Fix**: two new fields on the persistent `afatfs.deleteTreeState`
   singleton, `descendTarget` (full `afatfsObjectInfo_t`) and `parentEntry`
   (directory-entry pointer), snapshot the child's identity and
   directory-entry location at the moment of descent and are restored into
   `op->currentTarget`/`file->directoryEntryPos` on ascend. One level deep
   only — matching the existing `parentCluster` depth-one bound, not a
   regression, since `afatfs_deleteTree()` has exactly one caller and is
   never invoked more than one nested directory below a delete root (a Kit
   slot has files only; a Scene slot has files plus one `Kit …/`
   subdirectory).

**Hardware-confirmed Session 055**: see that log. Full byte-level trace
evidence and the exact card fixtures used for every round are preserved in
`AFAT_RECURSIVE_WHITEPAPER.md` before deletion.

## 4. Bug #2 — HCNAMES source provenance not staged on Save

Every Load path calls `filesystem_setResidentSource(row, source)` alongside
its name update; no Save path did, so a saved row kept reporting its
previously *loaded* slot as source (Scene saved to 031 still said source
004; Kit stayed 013; Instrument published no `@` at all). Re-reading each
Save's actual wiring (not assuming uniformity) found the four paths are not
architecturally alike:

- **Kit/Scene/Bank**: straightforward — stage the row/source pair at the
  same point each already updates its ephemeral display-name cache, before
  the existing HCNAMES read-merge-rewrite hand-off.
- **Instrument**: a deeper gap. Root Instrument Save only ever updated the
  ephemeral cache and handed off to `FS_INTERNAL_OP_CREATE_BOOT_INDEX`
  (rebuilds only the type `.hcindex`, a completely separate artifact).
  `filesystem_requestUpdateResidentInstrumentNames()` — the only path that
  reaches `FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT` — had **zero callers
  anywhere in the repository**, confirmed by a full-repository grep: neither
  the name nor the source of a root Instrument Save had ever durably reached
  `/.hcnames` through any reachable path. Fixed by staging the source and
  hand-off into `FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT` directly (chained
  through the existing `op_library_index_rebuild_pending` mechanism so the
  type-filtered `.hcindex` rebuild still happens afterward, via a new
  `filesystem_startInstrumentIndexRebuildScan()` helper).

**Status**: implementation verified correct by line-by-line review against
`filesystem_cacheCurrentResidentInstrumentNames()`'s actual field reads.
Hardware-confirmed by Session 055 (all four element types, part of its full
Kit/Instrument/Scene/Bank round-trip test).

## 5. Diagnostics added (all permanent, kept)

- `filesystem_pollPhaseStall()` — shared edge-triggered stall detector, used
  at delete-slot (50,000 polls), Bank Save entry (20,000), and the runtime
  AutoSave drain (30,000 — the one site where a stall also forces a real
  error completion, since a wedged drain previously had no bounded escape at
  all).
- `'X'` `PHASE_STALL`, `'O'` `SAVE_LIFECYCLE` (five checkpoints ×
  four element types, including a raw CRC32C content fingerprint for
  Instrument Save), `'E'` `OPERATION_ERROR` (a universal backstop hooked
  into both of filesystem.c's shared terminal-completion functions, so a
  future failure path nobody thought to specifically instrument still gets
  caught by construction).
- Boot Bank Load per-instrument timing breadcrumbs (reused the existing `'N'`
  stage's unused `PHASE_REQUEST` milestone).
- All documented in `DEV_MODES.md`'s stage-letter list and
  `tools/decode_devlogs.py`'s lookup tables.

## 6. Scene-Pattern desync (found during retest, unrelated to the delete work)

`Load:[Scene]` correctly wrote pattern data into resident SRAM, but playback
and the STEP UI read `seq_activePattern`/`menu_shownPattern` — separate
indices from `scene_getActiveIndex()` that Bank Load's active-Scene commit
never realigned. Confirmed by trace (`R DONE=1 mask=0x0020 {5}`, eight
consecutive successful Scene 5 loads while `seq_activePattern` stayed 0).
Fixed with a new, narrower `seq_alignActivePatternToScene()` (state realign
only — no LED notify, no MIDI program change, no note-off) called at the
Bank Load phase-20 commit alongside the existing `scene_selectActive()`.
Deliberately did not touch `scene_selectActive()` itself (would invert its
"identity, never data" contract) or `seq_selectActivePattern()` (front-panel
PERF switching keeps its existing side effects). Hardware-confirmed by
Session 055.

## 7. IWDG regression (introduced and fixed within this session)

A watchdog diagnostic feature (`DEV_LOGGING_IWDG`) added while investigating
an unrelated boot-hang report caused its own indefinite boot-splash hang: its
init spun on `IWDG_SR`'s PVU/RVU bits before ever writing the `0xCCCC` key
that starts the LSI clock domain those bits depend on — and nothing else in
this firmware enables the LSI. Fixed with correct ordering (enable LSI and
confirm `LSIRDY` first; bounded TIM6-timed waits at every handshake so it can
no longer spin forever regardless of silicon behavior; starts nothing at all
if `LSIRDY` never appears, since a watchdog with the wrong default period
would be worse than no watchdog). A second hazard was found while fixing the
first: the feed only lived in `filesystem_tick()`, but modal sample install
runs entirely through `filesystem_blockPoll()` (which explicitly bypasses
`filesystem_tick()`), so an enabled watchdog would have reset the MCU
mid-`sampleFlash` erase/program — a flash-corruption risk, not just a reboot.
Fixed by also feeding from `filesystem_blockPoll()`. **Ships disabled by
default** (`DEV_LOGGING_IWDG` = 0); the feature itself has never successfully
run on hardware even after these fixes.

## 8. Card repair (not a source change)

Six root Scene folders on the test card were structurally damaged (missing
`effects.fx` and/or embedded Kit contents), each mapping cleanly onto an
abort point in Scene Save's non-atomic ~12-phase write sequence (old tree
deleted first; `effects.fx` written last). Repaired on-card (byte-identical
`effects.fx` copied from an intact Scene; embedded Kit contents restored
from the matching root `/Kit/` entry; one Scene's `sceneset.scg` was fully
absent and rebuilt — only its `audio_out` field was genuinely recovered from
legacy data, everything else is a firmware default and flagged as such). No
save-path hardening was implemented; four ranked options are recorded in
`SCOPING_TARGETS.md`.

## 9. Documentation close-out

- `ASYNCFATFS_REFERENCE.md`, `FILESYSTEM_SPEC.md`, `AUTOSAVE.md`, and
  `DEV_MODES.md` updated with the descend/ascend invariant, HCNAMES
  source-on-save, the facade-acknowledgement rule, and the new trace stages
  respectively (folded in at Session 055 close-out alongside this log).
- `SCOPING_TARGETS.md`: the pinned duplicate-slot target marked closed for
  ordinary use (acceptance matrix still open); Session 053's test-report
  defects re-triaged against what this session actually fixed; new deferred
  targets added (name-cache ownership interlock, top-level entry trace
  coverage, `AUTOSAVE_TRACE_RECORD_COUNT` reversion decision, Scene Save
  partial-write hardening, Scene Load error-code granularity, the
  single-source-of-truth Pattern/Scene index refactor, `DEV_LOGGING_IWDG`
  hardware validation, the Makefile header-dependency footgun).
- `MEMORY.md` volatile notes updated with all of the above.

## 10. Superseded working documents

`SESSION_054_PREPLAN_ASYNC_RECURSIVE_CLEANUP.md`,
`SESSION_054_PLAN_DEFECT_EVIDENCE_FIX.md`, `AFAT_RECURSIVE_WHITEPAPER.md`,
`SCENE_LOAD_PAT_RESTORE.md`, and `LOAD_SAVE_AUTO_ELEMENT_TEST_REPORT.md` are
superseded by this handoff, `055_SESSION_HANDOFF_LOG.md`, and the
specification-reference updates above, and may be deleted.
