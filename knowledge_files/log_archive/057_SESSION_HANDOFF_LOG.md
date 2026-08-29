# Session 057 — Handoff Log

```
DATE: 2026-08-25 to 2026-08-28 (commits ad9b026..f99329c on dev-ph3-autosave-ph4;
  true Session 057 boundary starts after 7602a03 "056 session closeout")
SESSION GOAL: Read back through the logs, SCOPING_TARGETS.md, and the current
  test card to produce and clear a punch list of what needed to be
  wrapped/bugfixed in load/save, HCNAMES, .hcindex, AutoSave, and dev
  logging/trace before starting the AutoSave boot reader. Assessment-only at
  the start; escalated into a full implementation session as items resolved.
COMPLETED: P1 (Bank Save present-mask union), P2 (redundant settings write,
  accepted as-is), the AutoSave Bank-identity-mismatch copy-forward design
  question, settings.cfg safe write (implemented + hardware-verified), the
  empty-Scene/Bank overwrite guard (implemented), the boot Kit-directory
  sanitizer refactor (implemented, partially hardware-tested), the Bank Save
  total-tree-delete -> per-Scene safe-replace rewrite (implemented,
  hardware-tested, fixes ErrS05 and a second undocumented data-loss bug), ten
  new per-state-machine stall detectors plus a DEV_STALL_DETECTION toggle, a
  Bank Save/Load progress indicator, and the full Bank Save screen-freeze
  diagnosis and fix (root cause found and closed, hardware-accepted).
VERIFIED ON HARDWARE:
  - settings.cfg safe write: YES — deliberate mid-promotion power-cut
    simulation, recovery-promotion test PASS (S057_SETTINGS_WRITE_SAFE.md §13).
  - Boot Kit-directory sanitizer refactor: PARTIAL — checklist items 1, 2, 4,
    5, 6 (boot timing/regression/hardware-boot) checked PASS; items 3, 7-14
    (the actual quarantine-rename/cascade/partial-failure behaviors, including
    the single most important boot-safety regression test) are UNCHECKED in
    S057_TESTING.md. Not fully verified.
  - Bank Save per-Scene safe replace / ErrS05 fix: YES — full 16-Scene Bank
    Save to slot 019 completed with a byte-identical, uncorrupted 161-file
    tree (S057_BANK_STALL_FIX.md, hardware acceptance section).
  - Bank Save stall root cause and fix: YES — same slot-019 hardware run is
    the closing evidence; ~2.5 minute wall time, no BkTo, no corrupt files.
  - Empty-Scene/Bank overwrite guard: NOT hardware-tested this session (code
    landed; S057_SCENE_OVERWRITE_SAFE.md's own 8-item test plan was not run).
  - Session 056 page-exit expedite: NOT re-verified this session despite being
    first on this session's own priority list. No record in any of the six
    companion documents of it being tested.

CHANGES THIS SESSION (see §18 for full file list):
  Core/Hardware/SD/filesystem.c            largest single change, ~2,000+ net
    lines: P1 mask union; empty-overwrite guards; settings safe-write state
    machines; boot Kit-quarantine deletion + lazy load-time quarantine
    machinery; Bank Save per-child delete-then-write rewrite; ten stall
    detectors; Bank Save/Load progress-cursor API; the total-duration
    watchdog added then fully removed; AutoSave validation winner_bank_match.
  Core/Hardware/SD/filesystem.h             filesystem_bankChildCursor(),
    filesystem_lastBankLoadFailedSceneMask() declarations.
  Core/Hardware/SD/storageTypes.h           STORAGE_SETTINGS_TEMP_FILENAME.
  Core/Hardware/SD/asyncfatfs/asyncfatfs.c  afatfs_countOpenHandles();
    AFATFS_MAX_OPEN_FILES bumped 5->8 (diagnostic), then reverted 8->5.
  Core/Hardware/SD/asyncfatfs/asyncfatfs.h  afatfs_countOpenHandles() decl.
  Core/Bank/Scene/AutosaveTrace.h           PHASE_STALL site mask widened
    3->4 bits; 7 new stall-site constants; CREATE_RESULT bit-packing fields.
  Core/Bank/Scene/Preset/presetManager.c/h  preset_bankLoadFailedSceneMask().
  Core/Menu/menu.c                          Bank-child progress indicator
    (`NN.`); two calls to the existing filesystem error overlay from the
    PRESET_OP_BANK_LOAD success path.
  config.h                                  DEV_STALL_DETECTION flag (default
    1); FS_BANK_TOTAL_TICK_LIMIT added then fully removed (never shipped as
    current code — see §14).

KNOWN ISSUES INTRODUCED: None identified. See §15 for two undocumented-but-
  verified-intentional asymmetries (BkSt/ScSv reverted to trace-only while
  six sibling detectors were not; a handle-census hard-abort at Bank Save
  entry remains permanently live) that are not bugs but are worth a future
  explicit decision.

KNOWN ISSUES RESOLVED:
  - Bank Save present-mask overwrite dropping resident-but-unselected Scenes
    from `bank_scenePresentMask()` (P1, SCOPING_TARGETS.md §249-287).
  - `ErrS05` — Bank Save's `afatfs_deleteTree()` aborting mid-tree on a
    quarantined `errKit` directory's rewritten LFN entries.
  - Undocumented: partial `Save:[Bank]` (subset mask) silently deleting every
    non-selected resident child from the on-disk Bank tree.
  - `settings.cfg` power-loss corruption (in-place truncate with no backup).
  - An empty/never-loaded Scene being saveable over an occupied Library slot.
  - The boot-time Kit-quarantine scan's false-boot-failure risk on large
    Kit libraries (308 blocking ops / 10 s deadline on the 44-Kit test card).
  - Bank Save/Load screen freeze with a static `...` and no recovery path
    short of a hard reboot (root cause: a foreground-poll counter treated as
    an elapsed-millisecond budget).

NEXT SESSION RECOMMENDED GOAL: Hardware-verify the two items this session
  left untested (empty-Scene/Bank overwrite guard's 8-item test plan; the
  boot-safety-regression test for the Kit-sanitizer refactor — test #12 in
  S057_TESTING.md, explicitly flagged there as "the most important test in
  this plan"). Re-verify the Session 056 page-exit expedite, still pending
  since that session. Then proceed to Session 058's already-seeded plans
  (S058_BANK_LOAD_SPEEDUP_PROPOSAL.md, S058_AUTOSAVE_CRC_SPLIT.md) or the
  AutoSave boot reader (AUTOSAVE_READ_PLAN.md), which still depends on the
  deferred boot-fallback latch scope in SCOPING_TARGETS.md.
BLOCKERS: None for further wrap-session work. The AutoSave boot reader
  remains blocked on its own prerequisite (SCOPING_TARGETS.md's deferred
  boot-fallback scope), unchanged from before this session.

CRITICAL REMINDERS FOR NEXT SESSION:
- A foreground-poll count is NOT an elapsed-time budget. `filesystem_tick()`
  runs at whatever rate the main loop achieves (~7,600/s measured on this
  build); any future "total duration" watchdog must use a wall-clock/tick16
  time source, not a call counter. Do not reintroduce
  `FS_BANK_TOTAL_TICK_LIMIT` in any form.
- `filesystem_finish(FS_STATUS_ERROR)` is not cancellation. It releases the
  shared facade without draining in-flight AsyncFATFS callbacks or handles.
  Before adding an abort to any new stall detector, confirm the phase being
  aborted owns no pending callback — the `BkSt`/`ScSv` reversion in this
  session is the concrete cautionary example (§14-§15).
- `AFATFS_MAX_OPEN_FILES` must stay `5`. A diagnostic bump to `8` this
  session was disproven by its own trace evidence and fully reverted; do not
  re-attempt this fix for a future stall without new evidence.
- `sizeof(afatfsFile_t)` is **188 bytes** on this target, not the 328 bytes
  previously recorded in `ASYNCFATFS_REFERENCE.md` (now corrected). If doing
  RAM math from that file, use 188.
- Bank Save is now per-Scene delete-then-write, not total-tree delete/
  recreate. A partial `Save:[Bank]` preserves every non-selected child.
- Six stall detectors added this session (`KtSv`, `KtLd`, `ScLd`, `BkLd`,
  `StWr`, `Flsh`) still call `filesystem_finish(FS_STATUS_ERROR)` on a
  20,000-30,000-poll stall. Only `BkSt` and `ScSv` were converted to
  trace-only. This asymmetry is real and verified against current source,
  not an oversight in this log — see §15 before changing any of them.
- The boot Kit-quarantine refactor changed *what* gets renamed and *when*,
  not the underlying rename primitive. Legacy `errNNN ...` folders from the
  old boot-time quarantine are untouched and permanently inert (§8g/§4d).
- `DEV_STALL_DETECTION` (config.h, default 1) compiles out every stall
  detector, its statics, and its counter resets when set to 0. Useful for
  bench-testing a slow card without a detector killing the operation
  mid-test.
```

---

## 1. Session scope and companion-document map

This session began as a read-back/assessment pass (`S057_AUTOSAVE_WRITER_WRAP.md`)
over the accumulated punch list from Sessions 052-056 and `SCOPING_TARGETS.md`,
then escalated into a full implementation session as each item resolved. It
produced six companion planning/report documents in the repository root, each
covering one workstream; this log consolidates all of them into the permanent
record. Per the user's request, the six source documents will be deleted after
this log and the specification-reference updates land — nothing in them should
be treated as needing separate preservation beyond what is captured here.

| Document | Role | Status at close |
|---|---|---|
| `S057_AUTOSAVE_WRITER_WRAP.md` | Top-level assessment; spun off the other three plans; owns P1/P2, the AutoSave validation design question, the trace-count decision, and the sequencer chaselight investigation | All items resolved or explicitly deferred (§2-§5, §17) |
| `S057_SETTINGS_WRITE_SAFE.md` | `settings.cfg` safe-write design + implementation | Implemented, build-verified, hardware-tested PASS (§6) |
| `S057_SCENE_OVERWRITE_SAFE.md` | Empty-Scene/Bank overwrite guard design + implementation | Implemented, build-verified, **not hardware-tested** (§7) |
| `S057_BOOT_KIT_SANITIZE_REFACTOR.md` | Boot Kit-quarantine removal + lazy load-time quarantine design + implementation | Implemented, build-verified, **partially hardware-tested** (§8-§9) |
| `S057_TESTING.md` | Hardware test checklist for the sanitizer refactor; grew a second, unplanned workstream (§8 of that document) covering the Bank Save total-tree-delete rewrite, stall-detection infrastructure, and the first (partial) fix attempt at the Bank Save freeze | Sanitizer checklist partial; Bank Save per-Scene rewrite implemented and hardware-tested; stall infrastructure implemented (§10-§12) |
| `S057_BANK_STALL.md` | Diagnostic assessment of the Bank Save freeze after the first remediation attempt did not fully resolve it; builds a handle-exhaustion diagnostic (handle pool 5->8, handle census, `BkHd`) | Superseded by `S057_BANK_STALL_FIX.md` — the handle-exhaustion hypothesis was disproven (§13) |
| `S057_BANK_STALL_FIX.md` | Root cause and closing fix for the Bank Save freeze | **CLOSED**, hardware-accepted 2026-08-28 (§14) |

Two forward-looking planning documents were also created this session but are
explicitly out of scope for Session 057 itself and are not detailed here
beyond acknowledging their existence: `S058_AUTOSAVE_CRC_SPLIT.md` (per-section
CRC record format, status "design, pre-implementation") and
`S058_BANK_LOAD_SPEEDUP_PROPOSAL.md` (addresses the ~2.5-minute Bank Save
latency this session's fix left in place). `AUTOSAVE_READ_PLAN.md` and
`AUTOSAVE_EXTENSION.md` are pre-existing planning documents this session read
and referenced (the boot reader's own implementation remains not started) but
did not substantively change.

**A note on how this log was produced.** Several of the six documents above
were revised multiple times in place as understanding improved (most visibly
`S057_BANK_STALL.md` -> `S057_BANK_STALL_FIX.md`, and the Bank Save rewrite's
"planned rename-write-delete" design in `S057_TESTING.md` §8.3 versus its
"actual implementation used the simpler delete-then-write approach" in §8.5).
Rather than trust the documents' own narrative of what shipped, every
non-trivial claim below about the *current* state of the code was independently
re-verified by reading `Core/Hardware/SD/filesystem.c` and
`Core/Hardware/SD/asyncfatfs/asyncfatfs.c` directly, by diffing the actual
commits, and — for the final build numbers — by re-running
`arm-none-eabi-size` against the current `build/lxr02.elf` myself. Every place
below where the verified reality differs from what a companion document
states is called out explicitly, most importantly in §15.

---

## 2. P1 — Bank Save present-mask overwrite fix

**Status: implemented and confirmed present in current source.**

`SCOPING_TARGETS.md` §249-287 recorded this as a deferred target since Session
052: `filesystem.c`'s Bank Save present-mask commit assigned the caller's
save-subset mask directly instead of unioning it into the existing resident
mask, so saving fewer than all resident Scenes silently cleared
`bank_scenePresentMask()` bits for Scenes that were still genuinely resident
in SRAM. Bank Load already did the union correctly; Save was the one
inconsistent writer.

Fix (one line, `filesystem.c`, inside the Bank Save present-mask commit):

```c
bank_setScenePresentMask((uint16_t)(bank_scenePresentMask() |
                                     op_bank_scene_save_mask));
```

**Why this mattered beyond bookkeeping:** the SEQ LED toggle surface for
`Save:[Bank]` (`menu_loadSaveSelectableSceneMask()` ->
`menu_residentPresentSceneMask()` -> `bank_scenePresentMask()`) gates both
which LEDs can light and which button presses are allowed to toggle a bit at
all, directly off this same present mask. With the union fix, a Scene that's
still genuinely resident no longer becomes untoggleable after a prior partial
Bank Save. This fix is also the direct prerequisite for §7's empty-overwrite
guard, whose "is this Scene real" signal is `bank_scenePresentMask()` itself —
untrustworthy until this landed.

---

## 3. P2 — redundant per-boot settings write

**Status: accepted as-is, no code change.** `SCOPING_TARGETS.md` §289-317
flagged that three unconditional `filesystem_markSettingsDirty()` call sites
produce one value-idempotent `settings.cfg` rewrite on every boot with a valid
Bank, and asked for an explicit decision: gate it on `fs_settings_runtime_ready`,
or accept it deliberately. Decision this session: **accept as-is.** Now that
the write itself is safe and hardware-tested (§6), the extra idempotent
rewrite is harmless, and it keeps a real upside — reconciling `settings.cfg`
if boot fell back to a different Bank slot than the one stored on disk. No
gating was added.

---

## 4. AutoSave validation: Bank-identity-mismatch copy-forward vs. regeneration

**Status: implemented — and, per direct source verification, this landed in
an earlier commit this session (`bd681db`, "s058 planning crc split") that
predates the punch-list document's own claim that it was still open.**

Prior behavior: `autosave_streamValidationMatchesBank()` returning false for
an otherwise-valid winner (a legitimate Bank-session transition — the
`.hcprms` record still names the *previous* resident Bank) forced full
two-cycle regeneration, discarding a structurally valid record.

New behavior: the writer's validation phase now distinguishes two outcomes via
a new `winner_bank_match` field (alongside the existing `candidate_valid`):
"no winner" still routes to case 30 (regeneration), but "winner exists, Bank
identity doesn't match" now routes to case 50 (transformed copy-forward) with
`autosave_markResidentBankDirty()` called first to seed the canonical mask so
every live byte — Bank identity, Scene parameters, Kit, and Instrument values
— substitutes into the copy in one drain cycle (plus continuations if the
capture budget is exceeded). A legitimate Bank-session transition now reuses
the structurally valid peer and overwrites its content, instead of forcing a
full regeneration.

**Verification note:** `S057_AUTOSAVE_WRITER_WRAP.md` §11 states "AUTOSAVE.md
is now stale and should be updated to reflect the settled contract." Reading
the current `knowledge_files/specification_reference/AUTOSAVE.md` directly
shows this update was **already made**, in a later commit this session
(`2b43a76`, "kit sanitize refactor pre-implement" — a misleading commit
message; the actual `AUTOSAVE.md` diff in that commit is the winner_bank_match
documentation, bundled in alongside unrelated work). No further AUTOSAVE.md
change was needed for this item; see §19 for what AUTOSAVE.md still needed
from this session's own newly-discovered issues (none — it remains accurate).

One additional, smaller change in the same commit: `filesystem_ensureAutosaveFilesBlocking()`
(the very-first-boot bootstrap path, before any `.hcprms` files exist) now also
calls `autosave_markResidentBankDirty()`, so the first-ever record generation
also starts from a fully-dirty canonical mask rather than relying on whatever
the boot Bank Load's own (currently still-disabled-during-boot) mutation
tracking would otherwise capture.

---

## 5. `AUTOSAVE_TRACE_RECORD_COUNT` reversion — deferred again

**Status: unchanged, confirmed via `config.h`.** Still `2048u` ("TEMPORARY
approved expansion"; normal default `64u`). `SCOPING_TARGETS.md` §478-482
already flagged this as needing an explicit keep/revert decision now that the
pinned recursive-delete target (Session 054) is hardware-confirmed closed.
This session's decision: defer again — the expanded trace window is useful
headroom while the AutoSave reader lands. Revisit after the reader is fully
implemented and tested.

---

## 6. `settings.cfg` safe write — implemented and hardware-verified

**Status: DONE.** Full design in `S057_SETTINGS_WRITE_SAFE.md`; summarized
here with the parts that matter for future maintenance.

### 6.1 The bug this closes

`settings.cfg` was a direct in-place truncate-and-rewrite (`afatfs_fopen(...,
"w", ...)` on the live filename) with no backup, unlike AutoSave's
`.hcprms1/2`. A power loss mid-write could leave it empty or torn, silently
reverting `active_bank` (and every other setting) to firmware defaults with
no error surfaced.

### 6.2 Design: temp file + sync + validated promote

New shared constant `STORAGE_SETTINGS_TEMP_FILENAME "settings.tmp"`
(`storageTypes.h`) — plainly visible, no dot-prefix, deliberately readable so
a leftover temp file after an interrupted write is discoverable by anyone
inspecting the card root (unlike `.hcnames`/`.hcprms`, which are opaque wire
formats nobody is meant to read directly).

`filesystem_saveGlobals_tick()` was rewritten from 5 phases to 11:

```
OPEN(temp) -> WAIT_OPEN -> WRITE_LINES -> CLOSE -> WAIT_CLOSE ->
SYNC -> REMOVE_OLD -> WAIT_REMOVE -> RENAME(temp->settings.cfg) ->
WAIT_RENAME -> FINISH
```

The load-bearing correctness step is the explicit `afatfs_sync()` between
close and the old-file removal — `afatfs_fclose()` completing means the write
was accepted into the cache and its close-time directory-entry save was
*queued*, not that it is physically on the card. Without a sync boundary
before the irreversible remove/rename, a still-incomplete temp file could in
principle get promoted. With sync placed correctly: a crash before sync means
removal of the old `settings.cfg` is never reached (untouched); a crash after
sync means the temp file is guaranteed fully durable before removal/rename
ever starts. There is no window where a genuinely partial file is available to
be promoted.

`filesystem_loadGlobals_tick()` gained a new 9-phase recovery prelude (Pass A,
phases 0-8) before its existing, otherwise-unmodified load (Pass B, renumbered
from 0-4 to 10-14): check for a leftover `settings.tmp`; if found, read and
validate it with the *same* `filesystem_parseSettingsLine()` parser the normal
load path uses (no duplicated "what does valid settings.cfg look like" logic);
if valid, promote (remove old, rename); if invalid, discard (remove the temp
file only, leaving the existing `settings.cfg` — if any — untouched and
authoritative).

### 6.3 Terminator line — not load-bearing for correctness, kept anyway

A new final `case 17u` in `filesystem_nextSettingsLine()` emits `lines=17`
(the count of the 17 preceding data-key cases). Sync is what actually
prevents a partial file from ever being promoted; the terminator's purpose is
narrower — it gives boot recovery a cheap way to distinguish a genuinely
complete file from a clean-but-early EOF (e.g., a hand-edited or otherwise
truncated fixture), and gives any future settings-format expansion a
self-checking completeness signal without a format-version bump. New static
scratch (4 bytes total, normal SRAM `.bss`, meaningful only during
`FS_INTERNAL_OP_LOAD_GLOBALS`): `op_settings_recovery_valid`,
`op_settings_recovery_line_count`, `op_settings_recovery_terminator_n`,
`op_settings_recovery_terminator_seen`.

### 6.4 What did not change

`filesystem_markSettingsDirty()`, the debounce scheduler
(`filesystem_settingsWriterSchedule_tick()`), and every caller (Bank Load,
Bank Save, Global Menu edits) are untouched — none of them know or care how
the write itself is implemented. `filesystem_nextSettingsLine()`'s existing 17
field cases are untouched. The revision-check discipline at close
(`op_settings_change_revision`) is preserved unchanged across the new phases.

### 6.5 Hardware test — PASS (2026-08-26)

Test shim: a temporary 3-line short-circuit at phase 6 (REMOVE_OLD) that
skipped straight to finish, deliberately leaving `settings.tmp` on the card
alongside `settings.cfg` after boot 1. Reverted after that one boot.

| Boot | Firmware | Card before | Card after | Result |
|---|---|---|---|---|
| 1 | Test shim | `settings.cfg` only | `settings.cfg` + `settings.tmp` (both 277 B, identical) | Phases 0-5 ran correctly: temp created, written, synced durable. Shim prevented promote. |
| 2 | Clean (shim reverted) | `settings.tmp` only (`settings.cfg` deleted manually) | `settings.cfg` only | Recovery prelude found the temp file, validated it (18 lines, terminator `lines=17` matched), promoted via remove+rename. Normal load then read the promoted file correctly. |

All three files (temp, and both before/after `settings.cfg`) were byte-identical,
containing all 17 keys plus the new `lines=17` terminator. **This is the one
item from this session's whole punch list with a complete, closed-loop
hardware test covering the actual failure mode it was designed to prevent.**

RAM cost: **+4 bytes** `.bss` (the four recovery-scratch bytes in §6.3).

---

## 7. Empty-Scene/Bank overwrite guard — implemented, NOT hardware-tested

**Status: code landed, build-verified. `S057_SCENE_OVERWRITE_SAFE.md` §8's
8-item test plan was written but not run this session — no test results are
recorded anywhere in the six companion documents.** Flag this explicitly to
whoever picks up next: do not assume this guard works correctly on hardware
merely because it compiled.

### 7.1 Mandate

An empty (never-loaded) Scene must never be saved over an occupied Scene in
the Library, enforced at the actual save/write layer, not only by the SEQ LED
selection UI (which was already confirmed correct, but is UI convenience, not
a data-integrity guarantee — nothing stops a different caller from
constructing a mask that bypasses it).

### 7.2 What "empty" means

There is no formal `scene_isEmpty()`. The guard uses `bank_scenePresentMask()`
/ `bank_scenePresent()` — the codebase's one existing "has this Scene received
real content" signal, whose only setters are real, completed Loads or
Instrument commits (never mere navigation/selection) — **but only once P1
(§2) makes that mask trustworthy**, and only combined with a second gate,
`bank_hasResidentBank()`, because `bank_init()` unconditionally marks boot-seeded
Scene 0 present even though its SRAM content is BSS-zero and no real Bank has
loaded yet. A Scene is treated as non-empty only when both gates pass.

### 7.3 The two guards

- **`filesystem_requestSaveBank()`**: after the existing mask bounds-clamp,
  filters the save mask:
  ```c
  bank_scene_save_mask =
      (uint16_t)(bank_scene_save_mask &
                 (bank_hasResidentBank() ? bank_scenePresentMask() : 0u));
  ```
  Empty Scenes are silently excluded from the save mask before the state
  machine even starts, before the `active_scene` relocation logic runs (so an
  empty Scene can never become the relocated active_scene). If the resulting
  mask is `0u`, Bank Save's existing `op_bank_scene_save_mask == 0u` path
  already handles it — completes with `bankset.bcg` but no child directories.
- **`filesystem_saveSceneDirectory_tick()` case 0** (root Scene Save):
  ```c
  if (!bank_hasResidentBank() || !bank_scenePresent(op_kit_save_source_scene)) {
      filesystem_finish(FS_STATUS_ERROR);
      return;
  }
  ```
  Silent-fail via the same `FS_STATUS_ERROR` already used for every other
  case-0 rejection in this function — no new status code.

### 7.4 Deliberately out of scope

KitMrp/InstrumentMrp partial/morph saves — they write into an existing
Scene's component slots and don't create/replace a Scene identity. No `menu.c`
change was needed; the SEQ LED mechanism was already correct, it was only ever
reading a mask that P1 had corrupted.

---

## 8. Boot Kit-directory sanitizer refactor — design and implementation

**Status: implemented, build-verified. See §9 for hardware-test status
(partial).** Full design in `S057_BOOT_KIT_SANITIZE_REFACTOR.md` (Rev 2, then
a Rev 3 implementation-notes addendum documenting fixes made during coding).
This was the last remaining code-change item on the original wrap punch list
before the AutoSave boot reader.

### 8.1 The problem this removes

`filesystem_quarantineKitLibraryBlocking()` (called from
`filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_KIT)` before every
boot's Scene/Bank scan) did full content validation of every present root
Kit — not just a name/reconstructability check: opened `kitset.kcg` and
streamed it one byte at a time, then opened and closed each of the six member
Instrument files individually. That's 7 blocking file operations per Kit,
sharing one 10-second `KITQUAR` deadline for the *entire* root Kit traversal
(armed once per boot, not once per Kit). On the 44-Kit test card that's 308
blocking operations sharing one 10-second budget — a real, growing risk of a
**false boot failure** on a larger library or a slower card, even though the
code correctly distinguished I/O timeout from invalid content and never
quarantined good data on a timeout.

**Confirmed via direct source read: both `filesystem_quarantineKitLibraryBlocking()`
and `filesystem_validateCurrentKitBlocking()` are deleted from the live boot
path.** (A call to `filesystem_validateCurrentKitBlocking()` still exists in
the source at `filesystem.c:18693`, but it is inside the pre-existing,
already-`#if 0`'d retired code block — `filesystem_quarantineEmbeddedKitBlocking()`
and siblings, dating to Session 042 — that this refactor's own plan explicitly
says to leave alone as historical reference, not resurrect. It does not
compile into the current build.)

### 8.2 Why removing it is safe

Two load-bearing findings from this session's research, both confirmed by
reading the code rather than assumed:

1. **Scene and Bank boot indexing already do the lighter thing this refactor
   wants for Kit.** `filesystem_createLibraryIndexBlocking()` for
   `FS_LIBRARY_INDEX_SCENE`/`FS_LIBRARY_INDEX_BANK` only canonicalizes names
   and does a plain directory scan — no content is ever opened. Kit was the
   only one of the three with an extra content-validation step. This refactor
   deletes that one extra step so Kit matches the pattern already proven safe
   for the other two.
2. **Every context that actually loads Kit content already re-parses
   `kitset.kcg` and already fails gracefully**, independent of boot
   quarantine: root Kit Load, root Scene Load's embedded Kit, and Bank
   Load's Bank-local child all already call the same `storage_kitsetParseLine()`
   /`storage_instrumentParseLine()` machinery and already route a failure
   through `FS_STATUS_ERROR` with no dangling handle. Removing boot
   quarantine only changes *when* a broken Kit's brokenness becomes visible —
   from hidden pre-emptively to surfaced as a failed Load attempt.

### 8.3 New design: lazy quarantine-on-failed-load

A new `fs_load_invalid_layer_t` classification (`FS_LOAD_INVALID_NONE` /
`_SCENE` / `_KIT`) is set alongside every existing `op_close_status =
FS_STATUS_ERROR` assignment that represents a *proven content defect* (a
parse/finalize rejection or a required-child-absent check) — explicitly
excluding the ambiguous "directory wouldn't even open" failures (phase 7 in
both the Kit and Scene loaders, and phase 35 in the Scene loader, found and
added during implementation — see §8.6), which stay unclassified so a
transient I/O fault is never mistaken for provably bad content.

**Confirmed via direct source read** — `filesystem.c` has: the
`op_load_invalid_layer` static and its enum; classification set at every
documented phase in both `filesystem_loadKitDirectory_tick()` (phases 12,
13, 18, 19) and `filesystem_loadSceneDirectory_tick()` (phases 9, 11, 13, 14,
18, 23, 24, 28, 29, 40, 45, 46, 53, 57, 58); the new terminal decision-gate
phases (Kit loader 22-27, Scene/Bank loader 62-71); `op_bank_scene_failed_mask`;
and `filesystem_lastBankLoadFailedSceneMask()` — all present and wired
exactly as the design describes.

**Rules implemented:**

- **Root Kit Load**: a proven-bad Kit is renamed `err...` in `/Kit/`. No
  owning Scene to cascade to.
- **Root Scene Load — Kit-layer failure**: renames **both** the embedded
  `Kit <name>` folder and the owning `Scene/NNN Name` folder (a Scene whose
  Kit cannot be loaded is not a usable Scene).
- **Root Scene Load — Scene-layer-only failure** (bad `sceneset.scg`,
  missing child, bad pattern/effect): renames only the owning Scene folder;
  the embedded Kit is untouched since it was never proven bad.
- **Bank Load — Bank-local child**: **never renames the `SS Name` Scene
  folder** — its identity is positional (fixed two-digit slot), not
  browsable, and renaming it would corrupt the Bank's slot mapping, not just
  hide it from a browser. An embedded Kit failure inside a Bank-local Scene
  is still renamed (the "any kit load" rule is unconditional). Only that one
  child slot fails; the rest of the requested Bank Load mask still commits
  normally.

  **Confirmed via direct source read**, `filesystem.c` case 62: the
  `FS_INTERNAL_OP_LOAD_BANK` branch routes straight to the terminal phase (72)
  without ever reaching the embedded-Kit-rename sub-machine when the failing
  child is Bank-local — with an explicit in-source comment naming this as the
  fix for §10's `ErrS05` (a Bank-local `errKit` rename breaking a later Bank
  Save's `afatfs_deleteTree()` scan). This routing was *added* after the
  original refactor plan shipped, during the §10 rewrite — see that section.

- **Bank Load never fails the whole operation for one bad child.** This is a
  **boot-safety requirement**, not a style choice: boot's Bank Load poll loop
  (`main.c`) takes `goto boot_filesystem_failure` on any `FS_STATUS_ERROR`
  from the boot Bank Load. If one corrupted Kit inside one Scene turned the
  *whole* Bank Load into `FS_STATUS_ERROR`, that would turn one bad Kit into a
  failure to boot the device — exactly the class of false failure this whole
  refactor exists to close, reintroduced one layer up. Resolution: Bank Load
  keeps its `FS_STATUS_DONE` contract for anything except a genuine I/O abort;
  a new `op_bank_scene_failed_mask` tracks which child(ren) failed with
  provably-invalid content (excluded from the effective load mask and force-
  excluded from the final present-mask commit even if previously resident);
  `preset_bankLoadFailedSceneMask()` surfaces it; Menu's existing
  `PRESET_OP_BANK_LOAD` success-path branches (both the empty-Bank fallback
  and the normal sound-apply branch) each gained one call to the
  **already-existing** `menu_showFilesystemErrorOverlay()` when the mask is
  nonzero — no new UI was invented.

### 8.4 §4g — new standing truncation rule: recorded, NOT implemented

A new general rule was recorded in the plan document (any filesystem object
name longer than 8 characters gets truncated to 8 on sight, with
Instrument-specific disambiguation), triggered lazily at the next natural
touch-point rather than a new proactive scan. **This is design only.** The
plan's own §4g explicitly states its interaction with the Kit-quarantine
classification table (an over-length Bank-embedded member name currently
still classifies as `FS_LOAD_INVALID_KIT` and gets quarantined, when under
the new rule it should instead be silently repaired) is "flagged for the
implementation pass, not resolved here... left as a follow-up detailing pass."
**Confirmed:** no `filesystem.c` code implementing §4g's truncate-on-sight
behavior was found. Do not assume this rule is active.

### 8.5 Implementation deviations from the written plan (Rev 3 notes)

Three issues were found and fixed during coding that the plan's pseudocode
did not anticipate — recorded here since a future reader of the plan document
alone would not know the shipped code differs from it in these ways:

1. **Routing bug**: the plan's phases 22-27/62-71 were pseudocoded as sitting
   "before" the existing terminal phase but never said the existing
   `op_phase = 28`/`op_phase = 72` assignments elsewhere in each function had
   to be changed to route *through* the new decision gates (22/62) instead of
   jumping straight past them. Without this fix the new quarantine
   sub-machines would have been unreachable dead code. Fixed: all such
   assignments (except phases 0/2, which correctly bypass quarantine entirely
   since no folder has been identified yet) now target 22/62.
2. **Per-child reset bug**: for a Bank Load's multiple children, each enters
   the Scene tick at phase 8 (skipping phase 0), and `op_load_invalid_layer`
   was only being reset once per whole Bank Load in `filesystem_start()`. A
   stale classification from an earlier child could leak into a later
   child's decision. Fixed: reset alongside `op_bank_payload_active = 1u` at
   each per-child dispatch.
3. **Missing classification**: Scene loader phase 35 (a directory-reopen
   failure after a prior successful open) was omitted from the plan's
   classification table. Resolved the same way as phase 7 — excluded
   (treated as ambiguous, not quarantine-eligible).

### 8.6 What was deleted

`filesystem_validateCurrentKitBlocking()`, `filesystem_quarantineKitLibraryBlocking()`,
`fs_kit_validation_result_t`, `fs_kit_quarantine_result_t`, and the boot call
site in `filesystem_createLibraryIndexBlocking()` including its `KITQUAR`
arm/disarm pair. The already-`#if 0`'d retired Session-042 quarantine
functions (§8.1) are untouched, as designed.

---

## 9. Boot Kit-sanitizer refactor — hardware test status

**Status: PARTIAL. Do not treat this refactor as fully verified.**
`S057_TESTING.md`'s 14-item checklist:

| # | Test | Result |
|---|---|---|
| 1 | Boot timing (KITQUAR delay gone) | **PASS** |
| 2 | Normal boot, all-valid Kits | **PASS** |
| 3 | Deliberately malformed Kit — load attempt | not run |
| 4 | Boot-timeout stress (slow/large card) | **PASS** |
| 5 | Scene/Bank scan regression | **PASS** |
| 6 | Real-hardware boot | **PASS** |
| 7 | Root Kit Load quarantine rename | not run |
| 8 | Root Scene Load — Kit-layer cascade | not run |
| 9 | Root Scene Load — Scene-layer-only failure | not run |
| 10 | Bank Load — one bad child among several | not run |
| 11 | Bank Load — only the active scene is bad | not run |
| 12 | **Boot-safety regression (marked "the most important test in this plan")** | **not run** |
| 13 | Multiple simultaneous Bank Load failures | not run |
| 14 | Legacy `err...` folder interaction | not run |

Tests 1/2/4/5/6 prove the refactor did not regress ordinary boot. **None of
the actual new behavior — quarantine renames, the Kit-to-Scene cascade rule,
the Bank-local no-rename rule, or the partial-failure contract that this
whole refactor exists to provide — has been exercised on hardware.** Test 12
specifically is the one that proves the original false-boot-failure risk is
actually closed; until it runs, treat that risk as mitigated by code review
only, not by evidence.

---

## 10. Bank Save total-tree-delete -> per-Scene safe replace (`ErrS05` fix)

**Status: implemented and hardware-tested. This is a separate, larger
workstream that grew out of testing §8-§9** — corrupted test fixtures used to
exercise the new lazy quarantine (a Bank-local Scene 15 with a deliberately
corrupted embedded Kit) exposed a real, independent bug in Bank Save itself.

### 10.1 The bug (`ErrS05`)

Bank Save's old design deleted the *entire* target Bank tree
(`filesystem_deleteSlotDirectoryStart()` + `afatfs_deleteTree()`) before
writing any replacement child. Loading Bank `009 LoadTst` with Scene 15's
embedded Kit corrupted quarantined `Kit Pop` -> `errKit Pop` (§8's lazy
quarantine, working as designed). Saving that Bank back over the same slot
then ran the total-tree delete, which walked into `errKit Pop`'s
rewritten LFN entry layout and failed `afatfs_validateObjectInfo()` mid-scan,
aborting the delete with child `08 KitWool` already gone and the rest of the
tree left in a partially-deleted state — `ErrS05`.

### 10.2 A second, independent bug this same rewrite fixes

Total-tree-delete-then-recreate also meant a **partial** `Save:[Bank]`
(fewer than all resident Scenes selected) silently deleted every
**non-selected** child from the on-disk tree too — contradicting the
empty-overwrite guard's whole purpose (§7) and risking real data loss on a
power cut during the delete phase, before any of the selected replacement
children had even started writing. This was not previously documented as a
known bug in any prior session's records.

### 10.3 What actually shipped (differs from the first written plan)

The original design (`S057_TESTING.md` §8.3) specified a safer
rename-old-to-temp -> write-new -> delete-old-temp sequence, explicitly
chosen so a crash mid-write would leave the old child recoverable. **The
actual implementation (§8.5 of that same document) uses a simpler
delete-then-write approach instead**: for each Scene bit in the save mask,
delete the existing Bank-local child directory first, then write the new
one. Confirmed current in source: `filesystem_deleteBankChildSlotDirectoryStart()`
and `filesystem_bankChildObjectMatchesSlot()` exist and are called from Bank
Save's per-child loop (new phases 20-22 in the per-child cycle: prepare
source -> delete-old -> enter Scene writer). The stated reason for the
simpler approach was avoiding the complexity of managing stale temp-renamed
directories; the key safety property that *is* preserved is the one that
matters most — **non-masked children are never touched at all**, since the
per-child loop only ever visits slots present in the save mask.

Also removed as part of this rewrite: **Bank-local Kit quarantine (the old
phase 65 rename)** is now permanently bypassed for `FS_INTERNAL_OP_LOAD_BANK`
(§8.3's case-62 routing, confirmed live in source) — this is the direct fix
for `ErrS05`'s root cause. A Bank-local Kit failure is still detected (the
Scene's present-mask bit clears, the error overlay still shows) but the
folder is never renamed, so a later Bank Save's per-child delete never
encounters a rewritten-LFN `errKit` directory in the first place.

### 10.4 Bank directory-level handling (open-or-create, not delete-recreate)

The Bank directory itself (`Bank/<NNN Name>/`) is now scanned and reused —
opened directly if an existing directory matches the target slot with the
same name, renamed-then-opened if the name changed, or created if absent —
replacing the old unconditional delete of the whole Bank folder before phase
49's `mkdir_lfn()`. New phases 4, 51-55 in `filesystem_saveBankDirectory_tick()`
implement this scan/rename/reuse; new state `op_bank_existing_dir_found`
tracks which path was taken.

### 10.5 Hardware test

Confirmed via the same slot-019 hardware run documented in §14.4: a full
16-Scene Bank Save produced a complete, byte-identical, uncorrupted 161-file
Bank tree with no 0-byte/512-byte/32,768-byte artifacts anywhere.
Subset-mask preservation and same-slot-rename-preserves-children were **not**
independently re-tested this session (the successful run was a full 16/16
mask); `S057_TESTING.md` §8.4 records this as recommended follow-up test
coverage, not yet executed.

---

## 11. Stall-detection infrastructure and `DEV_STALL_DETECTION`

**Status: implemented, confirmed live in current source, gated by a new
compile-time toggle.**

### 11.1 Motivation and audit

An audit of every foreground state machine in `filesystem.c` for stall
coverage (`S057_TESTING.md` §8.7.2) found three sites already had detection
(delete-slot resolver, the original Bank Save entry detector, autosave
drain) and seven user-facing state machines had none at all: Kit Save,
Scene Save, Kit Load, Scene Load, Bank Load entry, settings write, and
Flush Finish (the shared completion gate for every successful operation).
Each of those seven can call `afatfs_fopen`/`fclose`/`chdir`/`mkdir_lfn` in a
`if (!func()) return;` retry loop that spins indefinitely if the underlying
asyncfatfs operation never completes.

### 11.2 What was added

Seven new detectors, each following the same pattern
(`filesystem_pollPhaseStall()` -> on fire: trace record -> named error code
-> `filesystem_finish(FS_STATUS_ERROR)`):

| Detector | State machine | Threshold | Named code |
|---|---|---|---|
| Kit Save | `filesystem_saveKitDirectory_tick()` | 20,000 polls | `KtSv NN` |
| Scene Save | `filesystem_saveSceneDirectory_tick()` | 20,000 polls | `ScSv NN` |
| Kit Load | `filesystem_loadKitDirectory_tick()` | 20,000 polls | `KtLd NN` |
| Scene Load | `filesystem_loadSceneDirectory_tick()` | 20,000 polls | `ScLd NN` |
| Bank Load entry | `filesystem_loadBankDirectory_tick()` | 20,000 polls | `BkLd NN` |
| Settings write | `filesystem_saveGlobals_tick()` | 30,000 polls | `StWr NN` |
| Flush Finish | `filesystem_flushFinish_tick()` | 50,000 polls | `Flsh 00` |

The Scene Save and Scene Load detectors each cover **both** their standalone
root operation and the equivalent Bank-delegated per-child path (entered via
`op_bank_payload_active`), since the Bank container-level detectors
deliberately do not fire while a child is delegated. `AutosaveTrace.h`'s
`AUTOSAVE_TRACE_PHASE_STALL_SITE_MASK` was widened from 3 bits (8 values) to
4 bits (16 values) to carry the new site IDs; the existing
`FLAG_IN_NATIVE_DELETE` bit moved from bit 3 to bit 4 to make room (its one
consumer, the delete-slot detector, picks up the new position automatically
through the macro).

The Bank-child `CREATE_RESULT` trace record also gained two new packed
fields for Bank Save/Load only: bits 10-13 carry `op_bank_child_cursor` (the
Bank-local Scene number being processed, resolving an earlier ambiguity about
whether the progress cursor was actually advancing), bits 14-15 carry a
saturating open-handle count from the new `afatfs_countOpenHandles()`.

### 11.3 `DEV_STALL_DETECTION` (config.h, default 1)

A new compile-time flag gates every stall detector, its statics (10 sites x
2 variables each), its counter resets, and `filesystem_pollPhaseStall()`
itself out of the build entirely when set to 0 — for bench-testing a
genuinely slow card without a detector aborting mid-test. Verified build
deltas: `1` (default) is byte-identical to a build with the toggle absent;
`0` removes 840 bytes text + 56 bytes BSS.

### 11.4 The permanent, un-gated exception: `BkHd`

**Confirmed via direct source read, and not mentioned in
`S057_BANK_STALL_FIX.md`'s implementation record at all:** a handle-pool
census hard-check remains live at the entry to every Bank Save per-child
cycle (phase 20), **not gated by `DEV_STALL_DETECTION`**:

```c
uint8_t open_handles = afatfs_countOpenHandles();
if (open_handles != 0u) {
    filesystem_makeNamedErrorCode("BkHd", open_handles);
    filesystem_finish(FS_STATUS_ERROR);
    return;
}
```

This was added in `S057_BANK_STALL.md` §11.4 as a handle-exhaustion diagnostic
and never removed, even after that hypothesis was disproven (§13-§14). It is
safe by construction — phase 20 is reached only after navigation has already
closed its own handles (phases 13-19) and before the delete scanner opens
one, so no callback is pending at the point it checks — and per the
exhaustive audit in §13, the count it checks is provably always zero at this
point, so in practice it can never fire on correct code. It exists now as a
permanent, cheap regression guard against a future handle leak, not as an
active diagnostic. Flagging its existence and un-gated status here since
nothing else in this session's own documents records it.

---

## 12. First remediation attempt: progress indicator + total-duration watchdog

**Status: superseded — the watchdog from this section was fully removed in
§14. Recorded here because it is real Session 057 work with a real user-facing
symptom, and because the progress indicator it shipped alongside is still
current.** This is `S057_TESTING.md` §8.10 (and the near-identical content
repeated at the top of `S057_BANK_STALL.md` §2, confirmed to be the same
build — the "post-remediation" size figures in both documents match exactly:
text 380,364, BSS 94,856).

### 12.1 Symptom and first diagnosis

Bank Save/Load on a large Bank produced a static `...` on the LCD with no
progress feedback and no way to tell working-slowly from actually-stuck; the
user's only recourse was a hard reboot, corrupting partially-written Bank
data. Code review found no menu livelock and no single-phase bug — the
per-phase stall detectors (§11) correctly never fired, because a 16-Scene
Bank Save cycles through ~40 phases per child and no single phase stayed
stuck long enough to trip a 20,000-poll threshold — but *no* detector existed
that watched the operation's *total* duration.

### 12.2 What shipped (still current)

- **`filesystem_bankChildCursor()`** (`filesystem.c`/`.h`): returns the live
  0-based child slot (0-15) during a Bank Load/Save, or `0xFF` otherwise. Pure
  read-only accessor of the existing `op_bank_child_cursor`. **Confirmed
  current in source.**
- **Progress indicator** (`menu.c`, `menu_paintLoadSaveConfirmation()`):
  replaced the static `...` with a live `NN.` indicator during Bank
  operations, querying the cursor above on each paint. Non-Bank operations
  still show `...`. **Confirmed current in source.**

### 12.3 What shipped and was later fully reverted

- **`op_bank_total_ticks` / `FS_BANK_TOTAL_TICK_LIMIT` (300,000) / `BkTo`**:
  a counter incremented once per `filesystem_tick()` call while a Bank Save
  or Load was the active owner, aborting with error code `BkTo` if it
  exceeded the limit. This is the item §14 found to be the actual bug, not
  the fix — see that section. **Confirmed via direct source read: none of
  `FS_BANK_TOTAL_TICK_LIMIT`, `op_bank_total_ticks`, or the string `"BkTo"`
  exist anywhere in the current `filesystem.c`.** Do not reintroduce any form
  of this counter (see the CRITICAL REMINDERS block at the top of this log).

---

## 13. Handle-exhaustion hypothesis and the diagnostic build

**Status: disproven, fully reverted. Recorded for the negative result and the
reusable audit methodology, not as a change that shipped.**

After the §12 remediation, hardware testing showed the `BkTo` watchdog firing
reliably — every attempted Bank Save stalled and aborted after **exactly 5
children**, regardless of target slot. `AFATFS_MAX_OPEN_FILES = 5` was too
precise a numerical match to ignore, and `S057_BANK_STALL.md` built a full
diagnostic to test it: `AFATFS_MAX_OPEN_FILES` bumped 5->8, a new
`afatfs_countOpenHandles()` accessor, the `CREATE_RESULT` bit-packing from
§11.2, and the `BkHd` phase-20 hard check from §11.4.

An exhaustive line-by-line audit of every AFATFS handle allocation/free path
in the bank-child save/load/delete code found every allocation correctly
paired with a free on every exit path — no provable leak in static analysis
(`S057_BANK_STALL.md` §9). The subsequent hardware test with the 8-handle
diagnostic build **confirmed this analysis**: the trace's `CREATE_RESULT`
handle-count field stayed at exactly 1 (the just-created child directory
handle) at every child boundary, never accumulating, and the `BkSt`
per-phase stall detector (20,000-poll threshold) never fired even once across
three failed attempts — meaning no single phase was ever actually stuck; the
operation was progressing normally the entire time. This directly disproved
handle exhaustion and pointed at the total-duration watchdog itself as the
thing terminating otherwise-healthy operations — leading directly to §14.

---

## 14. Bank Save/Load stall — root cause and closing fix

**Status: CLOSED. Hardware-accepted 2026-08-28.** This is
`S057_BANK_STALL_FIX.md` in full; independently re-verified against current
source and a fresh local build rather than only summarized.

### 14.1 Root cause

`FS_BANK_TOTAL_TICK_LIMIT` (300,000) was a count of `filesystem_tick()`
foreground-loop calls, but its comments and chosen value treated that count
as if it were milliseconds. On the tested firmware the foreground loop calls
`filesystem_tick()` roughly 7,600 times per second (confirmed by four
independent test runs, all terminating at 39.1-39.5 seconds of wall time
regardless of which Bank slot or how much data — the tell that the cutoff was
time-based, not content-based):

```
300,000 polls / ~7,600 polls-per-second ≈ 39.5 seconds
```

A 16-Scene Bank Save was taking ~7.7-8.3 seconds *per scene* (measured from
`CREATE_RESULT` trace spacing) — needing ~125-135 seconds for the scenes
alone before final index/sync work. The watchdog therefore always cut off
partway through the 5th child, at whatever asynchronous phase happened to be
active at that instant (phase 12 in one attempt, phase 24 in two others —
the varying phase is itself evidence the cutoff was time-based, not a stuck
phase). The 32,768-byte all-`0xFF` "corrupt" instrument file this produced
was not evidence of an oversized serializer — it is the exact, characteristic
footprint of `AFATFS_SAVE_DIRECTORY_NORMAL`'s cluster-rounded provisional
size, captured mid-write when the operation was force-finished before its
normal `AFATFS_SAVE_DIRECTORY_FOR_CLOSE` logical-size correction could run.

**A second, more serious finding**: `filesystem_finish(FS_STATUS_ERROR)` is
not a valid cancellation operation. It returns the facade to idle without
closing the in-flight object, waiting for callbacks, returning to root, or
syncing dirty FAT state. One prior test artifact
(`SD_CARD_POST_TEST_2/Bank/016 Filltern/04 Slak/Kit Emott/emottd33.drm`, 512
bytes containing 64 valid `AutosaveTrace` records instead of instrument text)
is best explained by exactly this: the watchdog aborted mid-write, the
abandoned Bank callback later fired and wrote the shared global `op_file`,
and unrelated trace-flush bytes landed inside the orphaned Bank instrument
file. **This is the reasoning behind reverting `BkSt`/`ScSv` to trace-only
rather than merely deleting the watchdog** (§14.2 item 2) — the same
corruption class can occur from *any* abort at a phase that might still own a
live callback, not only the specific 300,000-poll one.

### 14.2 The fix — three changes, confirmed present in current source

1. **Removed the Bank total-duration watchdog outright** (both Bank Save and
   the identically-broken Bank Load instance, which had not itself terminated
   the reported test but shared the same invalid time-base logic) — no
   replacement counter, timer, or diagnostic layer of any kind. **Verified**:
   `grep` for `BkTo`/`FS_BANK_TOTAL_TICK_LIMIT`/`op_bank_total_ticks` in
   current `filesystem.c` returns nothing.
2. **Made the `BkSt` (Bank Save entry) and `ScSv` (Scene Save, standalone and
   Bank-delegated) stall detectors trace-only** — removed their
   `filesystem_makeNamedErrorCode()` + `filesystem_finish(FS_STATUS_ERROR)`
   side effects, keeping only the existing edge-triggered
   `autosaveTrace_record()` call. **Verified by diffing the exact commit**:
   both sites' surrounding comments were rewritten in place to state the
   ownership reason (entry/metadata phases can still own a pending open/
   chdir/close/scan callback; observation must not be promoted to
   cancellation).
3. **Restored `AFATFS_MAX_OPEN_FILES` from the diagnostic value 8 back to
   5**, with an in-source comment recording the measured zero-between-
   children/one-at-create proof. **Verified current value: 5** (checked
   directly in `asyncfatfs.c`). This also corrected a stale documentation
   claim (`ASYNCFATFS_REFERENCE.md` said `afatfsFile_t` was 328 bytes; a
   target-ABI debug compile during this fix found the real current size is
   **188 bytes** — the 328-byte figure predates an unrelated later change
   that moved expanded delete state out of every handle. See §19 for the
   documentation correction this session made for that).

### 14.3 What was deliberately *not* done

No replacement wall-clock timeout, counter, or cancellation shim was added.
A real terminal timeout would first require a Bank-owned cleanup state
machine capable of draining any outstanding callback, closing owned
handles, returning to root, and completing `afatfs_sync()` before publishing
an error — AsyncFATFS currently exposes no general cancellation primitive
that makes an arbitrary phase safe to abandon mid-flight. Until such cleanup
exists, letting the operation run to its own natural completion is safer
than declaring it finished early. `afatfs_countOpenHandles()` was kept as a
read-only diagnostic (it retains no SRAM) but any comment implying a
handle leak was corrected — the measured data disproved it.

### 14.4 Hardware acceptance (2026-08-28) — independently re-verified

A full 16-Scene Bank Save to slot 019 completed. User-observed wall time
~2.5 minutes. Card inspection (`SD_CARD_BANK_STALL_TEST/`) confirmed: 16
child Scene directories (00-15), each with 3 direct files, one embedded Kit
directory, and 7 Kit files; 161 total data files (1 `bankset.bcg` + 16 each of
Scene/Pattern/Effect/Kit settings + 96 Instruments); every Instrument at
normal serialized text size (1,314-1,378 bytes); zero 0-byte/512-byte/
32,768-byte artifacts anywhere; sampled snare files contain normal parseable
`format=helicase.instrument` text. The trace records 16 successful
`CREATE_RESULT`s advancing monotonically through child indices 0-15
(`FAIL=0` throughout), followed by `Bank SOURCE_STAGED` and `Bank FINISH`
with no phase-stall or error record between request and finish. Trace-interval
math (accounting for two 16-bit tick wraps) gives ~137.3 seconds from request
to `Bank FINISH`, consistent with the pre-fix per-child cadence estimate and
well past the old 39.3-second false cutoff.

**Independently re-confirmed by this log's author, not merely restated from
the document**: `arm-none-eabi-size build/lxr02.elf` on the current working
tree, right now, reports `text=380436 data=408 bss=94848` — exactly matching
`S057_BANK_STALL_FIX.md`'s own claimed final numbers, and matching the
380,860-byte `build/LXRV2_lxr02.img` already committed at `f99329c`. BSS
dropped from the pre-fix diagnostic build's 95,424 bytes to 94,848 — a net
576-byte reduction (three reverted `afatfsFile_t` slots at 188 bytes each =
564 bytes, plus the removed 4-byte `op_bank_total_ticks` counter and
alignment).

Remaining, explicitly-deferred issue: Bank Save latency itself (~2.5 minutes
for 16 Scenes) is unchanged by this fix and is real; it is Session 058's
target (`S058_BANK_LOAD_SPEEDUP_PROPOSAL.md`), not evidence the correctness
fix above is incomplete. A separate power-cycle-and-reload regression check
was not explicitly reported alongside this result.

---

## 15. Verified current code state beyond what the session's own documents claim

This section exists because the user asked specifically for the *real* state
of the code where documents are incomplete or silent, not only what the
documents assert. Everything below was confirmed by reading current source
directly, not inferred from the six companion documents.

1. **The `BkSt`/`ScSv` trace-only reversion is narrower than it might read.**
   `S057_BANK_STALL_FIX.md` explains *why* an abort mid-callback is unsafe in
   general terms, but its own "Implementation record" section names only
   `BkSt` and `ScSv` as reverted. Direct source inspection confirms this is
   exactly and only what happened: `KtSv`, `KtLd`, `ScLd`, `BkLd`, `StWr`,
   `Flsh` (all added in the same session, §11) **still call
   `filesystem_finish(FS_STATUS_ERROR)`** on their respective stall
   thresholds — their in-source comments are unchanged from their original
   "aborting is safe" justification. This is a real, verified asymmetry: the
   fix addressed the two sites directly implicated by observed evidence
   (Bank Save's own container phases, and the per-child Scene writer
   delegated during Bank Save — both provably mid-flight during the failure
   this session investigated) and left the other six sites' original
   safety reasoning (open/chdir/close/read work with no non-cancellable
   native handle ownership) unchallenged. Whether `BkLd`/`ScLd` share the
   exact same *write*-side corruption risk as `BkSt`/`ScSv` (the specific
   evidence in §14.1 was about writes redirecting into the wrong file; a
   stalled *Load* only reads) was not investigated this session. Flagging
   this as a real fact, not a doc gap to silently "fix" — a future session
   should make an explicit, evidence-based decision about `BkLd`/`ScLd`
   rather than assuming either the current asymmetry or a hypothetical
   symmetric fix is correct.
2. **`BkHd` (handle census) remains a permanent, un-gated abort path** — see
   §11.4. Not mentioned at all in `S057_BANK_STALL_FIX.md`.
3. **The `S057_BANK_STALL_FIX` committed-vs-`.md` discrepancy.** `git status`
   shows a tracked, extensionless `S057_BANK_STALL_FIX` (deleted in the
   working tree) alongside an untracked `S057_BANK_STALL_FIX.md`. Diffing
   them: the `.md` version is a strict superset, adding only the "Status:
   CLOSED" line and the full hardware-acceptance/closeout section (§14.4
   above) on top of the otherwise-identical pre-test committed version. No
   code-relevant content differs between them.
4. **A repo-hygiene finding, not a code issue**: the `bd681db` commit ("s058
   planning crc split") accidentally carried in a large set of macOS
   Spotlight index files under `SD_CARD/.Spotlight-V100/Store-V2/...`
   (visible in `git diff --stat`, e.g. `.store.db`, `.indexArrays`,
   `.directoryStoreFile`). **Unlike the near-identical Session 056 incident**
   (which WRITER_WRAP §14 confirmed had already been cleaned up in a
   follow-up commit), `git ls-files` confirms these are **still tracked in
   the current HEAD**. No action was taken on this by this logging pass —
   flagging it so it isn't mistaken for new Session 057 output if repo size
   or `git status` cleanliness is ever investigated.
5. **`bd681db`'s commit message is misleading.** Despite being titled "s058
   planning crc split," its actual `filesystem.c` diff is the §4 AutoSave
   `winner_bank_match` fix (real Session 057 work), not any part of the CRC
   split design (`S058_AUTOSAVE_CRC_SPLIT.md`, which is pure design/pre-
   implementation prose with zero corresponding code in that commit or
   anywhere else in the session). Worth knowing if `git blame`/`git log` is
   used to attribute this fix later.

---

## 16. Session 056 page-exit expedite — status unchanged, not re-verified

`S057_AUTOSAVE_WRITER_WRAP.md` §1 opened by flagging that
`fs_autosave_page_suppressed` (the Session 056 fix that resets the AutoSave
writer's deadline to 250 ms after leaving the Load/Save page) was still
"pending hardware verification" per Session 056's own handoff, and put
re-verifying it **first** in this session's suggested order, noting "user
recalls possibly having already tested this — re-check and close out, don't
re-derive from scratch if evidence already exists."

**No record of this re-check exists anywhere in this session's six companion
documents.** None of `S057_SETTINGS_WRITE_SAFE.md`, `S057_SCENE_OVERWRITE_SAFE.md`,
`S057_BOOT_KIT_SANITIZE_REFACTOR.md`, `S057_TESTING.md`, `S057_BANK_STALL.md`,
or `S057_BANK_STALL_FIX.md` mentions the page-exit expedite at all. The code
itself is unchanged since Session 056 (confirmed — `fs_autosave_page_suppressed`
and its four call sites were not touched by any commit in this session's
range). **Treat this item as still exactly where Session 056 left it: code-
complete, build-clean, not hardware-verified.**

---

## 17. Deferred / still-open items carried forward

Everything below was explicitly re-flagged this session as still open, or
newly discovered and deliberately not investigated further this session.
None of it was fixed.

- **Sequencer chaselight can disappear** (`S057_AUTOSAVE_WRITER_WRAP.md` §6).
  User-reported, not root-caused. Rendering-side logic traced
  (`led_updateCurrentStep()`/`led_processSeqLedState()`); producer side traced
  (`seq_ledState.chaseStep` from a per-voice `seq_stepIndex[]`). Four
  hypotheses recorded, ranked by likelihood; the top one explicitly connects
  to `SCOPING_TARGETS.md`'s existing "Single-source-of-truth Pattern/Scene
  index" deferred item (Session 054's `seq_alignActivePatternToScene()` fix
  patched only the one reachable seam, not the systemic
  `seq_activePattern`/`menu_shownPattern` vs. `scene_getActiveIndex()` drift
  risk). Recommend reproducing with `DEV_MODE_DIAGNOSTIC` on before assuming
  a cause.
- **HCNAMES/`settings.cfg` Bank-15 discrepancy on the current test card**
  (`S057_AUTOSAVE_WRITER_WRAP.md` §9a). `settings.cfg` and the winning
  AutoSave record agree (Bank 15, `"LoadTst!"`); `.hcnames` row 0 disagrees
  (names Bank 12, `"012 LoadTst"`). No `/bootlog.bin` or `/asavetrc.bin`
  exists on that card copy, so there is no trace evidence to root-cause it —
  could be a genuine gap (some Bank-activation path updates
  settings.cfg/AutoSave but skips the HCNAMES row-0 publish), a later
  reversion, or test-session artifact. Needs a dedicated logging-enabled
  boot capture before assuming which.
- **`bootlog.bin`/`asavetrc.bin` duplicate-name limitation** — needs a
  re-verification pass, not a re-fix. `DEV_MODES.md` still documents this as
  fully open, but the *specific* mechanism that caused the observed
  duplicates (the Session 056 LFN early-free-run-exit bug) was a shared
  low-level AsyncFATFS bug affecting *every* LFN-capable creator, and was
  fixed and hardware-verified in Session 056. It's plausible this
  incidentally closes or reduces the exposure `DEV_MODES.md` still describes
  as fully open — but this was flagged as worth a deliberate check in
  `S057_AUTOSAVE_WRITER_WRAP.md` §13/§9b, and that check was **not performed**
  this session either. `DEV_MODES.md`'s wording is left unchanged (see §19)
  since nothing was actually re-verified.
- **Name-cache ownership interlock** (`SCOPING_TARGETS.md` §458-469) — the
  AutoSave writer reads the shared 9,000-byte name cache live while
  serializing, and Menu's `filesystem_clearNameCache()` bypasses facade
  arbitration from (as of Session 055) 16 remaining call sites. Consequence
  is a torn AutoSave record, not a hang. Still open, not touched this
  session.
- **Top-level Load/Save entry trace gap** (`SCOPING_TARGETS.md` §470-477,
  `DEV_MODES.md` §240-249) — `menu_traceInstrumentEntry()`'s `'N'` record is
  gated on `menu_instrumentLoadActive`, so top-level Kit/Scene/Bank refusals
  are invisible, and `menu_requestSceneEntryName()` has no trace producer at
  all. Recommended before the next Load/Save-family investigation for two
  sessions running now; still not implemented.
- **`AUTOSAVE_TRACE_RECORD_COUNT` reversion** — deferred again, see §5.
- **§4g's truncate-on-sight rule** — recorded as a standing design rule, not
  implemented; see §8.4.
- **Boot AutoSave known-incorrect write** (`SCOPING_TARGETS.md` §319-393,
  `S057_AUTOSAVE_WRITER_WRAP.md` §10) — a boot that loads a Bank still writes
  `.hcprms1/2` with `scene_present_mask=0x0000`/empty Scene payloads, because
  mutation tracking only enables after the boot Bank Load already ran. Still
  deliberately deferred, unchanged this session — but now more directly
  load-bearing than before, since it is an explicit prerequisite for the
  AutoSave boot reader's own implementation order.
- **Empty-Scene/Bank overwrite guard hardware test** — see §7, not run.
- **Boot Kit-sanitizer refactor's behavioral tests (3, 7-14)** — see §9, not
  run; test 12 (boot-safety regression) is the most important of these.
- **Bank Save subset-mask / rename-preserves-children re-test** — see §10.5,
  not independently re-run after the per-Scene rewrite (only the full-mask
  16/16 case was exercised).
- **Session 056 page-exit expedite** — see §16, not re-verified.
- **`BkLd`/`ScLd` write-vs-read corruption-risk symmetry with `BkSt`/`ScSv`**
  — see §15 item 1, newly identified this session, deliberately left as an
  open question for a future evidence-based decision rather than resolved
  unilaterally here.

---

## 18. Files changed this session

Restricted to the true Session 057 range (after commit `7602a03`, "056
session closeout" — the tail of that commit's own diff briefly appeared in a
wider `git diff` and is Session 056 documentation work, not this session's;
see §19 for what that means for which spec-reference files already carried
Session 056 content into this range).

**Firmware source:**
- `Core/Hardware/SD/filesystem.c` — by far the largest change (~2,000+ net
  lines across the whole session): P1 mask union (§2); empty-overwrite
  guards (§7); settings.cfg safe-write state machines (§6); boot
  Kit-quarantine deletion plus the new lazy load-time quarantine machinery
  (§8); Bank Save per-child delete-then-write rewrite plus Bank-directory
  open-or-create (§10); ten stall detectors (§11); Bank Save/Load progress-
  cursor API (§12); the total-duration watchdog, added then fully removed
  (§12-§14); AutoSave validation `winner_bank_match` (§4).
- `Core/Hardware/SD/filesystem.h` — `filesystem_bankChildCursor()`,
  `filesystem_lastBankLoadFailedSceneMask()` declarations.
- `Core/Hardware/SD/storageTypes.h` — `STORAGE_SETTINGS_TEMP_FILENAME`.
- `Core/Hardware/SD/asyncfatfs/asyncfatfs.c` — `afatfs_countOpenHandles()`;
  `AFATFS_MAX_OPEN_FILES` 5->8 (diagnostic) then 8->5 (final).
- `Core/Hardware/SD/asyncfatfs/asyncfatfs.h` — `afatfs_countOpenHandles()`
  declaration.
- `Core/Bank/Scene/AutosaveTrace.h` — PHASE_STALL site mask 3->4 bits, 7 new
  site constants, `CREATE_RESULT` bit-packing field shifts.
- `Core/Bank/Scene/Preset/presetManager.c` / `.h` —
  `preset_bankLoadFailedSceneMask()` and its backing static.
- `Core/Menu/menu.c` — Bank-child progress indicator in
  `menu_paintLoadSaveConfirmation()`; two `menu_showFilesystemErrorOverlay()`
  calls in the `PRESET_OP_BANK_LOAD` success path.
- `config.h` — `DEV_STALL_DETECTION` (default 1); `FS_BANK_TOTAL_TICK_LIMIT`
  added then fully removed (net: not present in the final config.h).

**Root planning/report documents** (per the user's stated intent, delete
after this log and the spec-reference updates land):
`S057_AUTOSAVE_WRITER_WRAP.md`, `S057_SETTINGS_WRITE_SAFE.md`,
`S057_SCENE_OVERWRITE_SAFE.md`, `S057_BOOT_KIT_SANITIZE_REFACTOR.md`,
`S057_TESTING.md`, `S057_BANK_STALL.md`, `S057_BANK_STALL_FIX.md` /
`S057_BANK_STALL_FIX` (extensionless, tracked-then-deleted; superseded by the
`.md` version — see §15 item 3).

**Forward-looking, out of scope for this log**: `S058_AUTOSAVE_CRC_SPLIT.md`,
`S058_BANK_LOAD_SPEEDUP_PROPOSAL.md` (both design/pre-implementation, no
corresponding code).

**Test fixtures**: `SD_CARD_TESTING/Kit/007 Chip/chiph1.hat` deleted (Kit
corruption test fixture); `SD_CARD_TESTING/Bank/009 LoadTst/` restored/
modified for corrupted-Kit re-testing; `SD_CARD_BANK_STALL_TEST/` added
(the slot-019 hardware-acceptance card copy, §14.4).

**Build artifact**: `build/LXRV2_lxr02.img` — final committed image,
380,860 bytes, matching the `text=380436 data=408 bss=94848` build
independently re-verified in §14.4.

Final verified build (re-run by this logging pass, not merely copied from a
document): `arm-none-eabi-size build/lxr02.elf` -> `text=380436  data=408
bss=94848  dec=475692  hex=7422c`.

---

## 19. Documentation updated as part of this logging pass

Beyond this log and the `000_SESSION_INDEX.md` entry, the following were
checked against current source and updated where stale (full detail in each
file's own diff, not repeated here):

- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` — the
  `settings.cfg` "no backer" claim, the boot Kit/Scene blocking-quarantine
  description, and the Bank Save total-tree-delete description were all
  stale relative to §6/§8/§10 above and were rewritten; the Save/Overwrite
  Safety section gained the empty-Scene/Bank guard from §7.
- `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md` — the
  handle-pool section's `afatfsFile_t` size (328 -> 188 bytes) and its
  dependent byte math were corrected per §14.2 item 3; the Session 057
  handle-pool diagnostic excursion (5->8->5) was added as a concrete example
  alongside the existing "never solve a lifetime leak only by increasing
  `AFATFS_MAX_OPEN_FILES`" caller rule.
- `knowledge_files/specification_reference/DEV_MODES.md` — added
  `DEV_STALL_DETECTION` (entirely undocumented before this pass, despite
  being squarely in this file's stated scope); rewrote the `X`/`PHASE_STALL`
  section, which described only the original 3 sites and stated Bank Save
  entry was an aborting site — both now stale per §11/§15 item 1 (10 sites
  now exist; `BkSt`/`ScSv` are trace-only, the other six new sites still
  abort). The duplicate-name-limitation section was deliberately **not**
  changed — see §17, that re-verification was not performed this session.
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` —
  added table rows for `filesystem_bankChildCursor()` and
  `filesystem_lastBankLoadFailedSceneMask()`; added
  `preset_bankLoadFailedSceneMask()` to the presetManager section; corrected
  the `filesystem_saveBankDirectory_tick()` private-helper description
  (total-tree-delete language replaced with per-child delete-then-write,
  §10).
- `knowledge_files/specification_reference/SRAM_MANIFEST.md` — appended a
  dated Session 057 delta note (net static-allocation change from the
  Session 056 baseline, using the independently-verified §14.4/§18 build
  numbers) in the same lightweight per-session-note style every prior
  session since 052 has used; the underlying linked table was not
  regenerated in full.
- `knowledge_files/specification_reference/AUTOSAVE.md` — checked, already
  current (§4); no change needed.
- `knowledge_files/specification_reference/OSC_INTERP_AUDIT.md`,
  `CPU_USE_DSP_AUDIT.md` — checked, unrelated to this session's changes; no
  change made.
- `SCOPING_TARGETS.md` — P1/P2 (§249-317) and the boot-sanitation-vs-load-
  validation deferred target (§211-247) are marked resolved with a pointer
  to this log, appended at the end of the file (not edited in place, to
  avoid shifting the line numbers that multiple already-archived session
  logs and this session's own companion documents reference by exact line
  range).
- `MEMORY.md` — "Current working source" pointer updated; one new dated
  Volatile Notes entry summarizing this session in the same style as the
  Session 052-056 entries; the stale P1/P2/KQ019KST deferred-target sentence
  inside the "Session 051 carryover and known defects" list corrected to
  reflect resolution.
