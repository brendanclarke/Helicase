# Session 055 Handoff — Recursive-Delete Hardware Confirmation, Load-Menu Freeze, Instrument `kit`-Row Restore, Stale Bank Name

DATE: 2026-08-21
SESSION GOAL: Hardware-verify Session 054's recursive-delete/HCNAMES fixes
  via a full Kit/Instrument/Scene/Bank load-modify-save round trip, then
  investigate whatever the round trip surfaces.
COMPLETED: Confirmed Session 054's fixes hold under real use. That same
  testing round surfaced a Load-menu freeze, root-caused and fixed across two
  rounds (the first attempt was wrong and is kept as a record, not deleted).
  Also fixed a permanent (not merely delayed) failure to restore the Load:
  Instrument `kit` row, and a stale-name race on fresh Load/Save entry.
VERIFIED ON HARDWARE:
  - Recursive-delete/HCNAMES-source-on-save (Session 054): **confirmed** —
    "load a kit, modify a kit, save a kit / load instruments, modify
    instruments / load a scene, modify a scene (pattern/parameters), save a
    scene including instrument types / save a Bank, Load a bank" reported no
    errors.
  - Load-menu freeze fix: **confirmed closed** — user reports the freeze is
    not recurring after the round-2 fixes.
  - Instrument `kit`-row restore fix: **preliminary only** — one hardware
    pass looked correct; the full test matrix (endless pot, InstrumentMrp,
    stopping dead on `kit` vs. continued scrolling) was not exhaustively run.
    Do not promote to "verified."
  - Stale Bank-name-on-entry fix: **not verified** — the race is proven by
    static analysis and matches the reported symptom shape exactly, but no
    trace record survived from the actual incident, so a second contributing
    factor cannot be ruled out. Recur ⇒ capture the card immediately.

CHANGES THIS SESSION: `Core/Menu/menu.c` only — 338 insertions, 2 deletions
  against HEAD (`0827b40`), uncommitted at session close. No other file
  touched by source changes this session; the specification-reference and
  `MEMORY.md`/`SCOPING_TARGETS.md` updates listed in §6 are this session's
  closeout documentation pass, not behavioral changes.
  - The Load-menu freeze fix (6 sites: `menu_showFilesystemErrorOverlay()`
    now acks the facade; `menu_pollPresetStatus()`'s deferred dispatch gated
    on `filesystem_status() != FS_STATUS_BUSY`; non-destructive early
    busy-checks added to `menu_requestResidentNameScratch()` and
    `menu_requestSceneEntryName()`; `menu_requestInstrumentIndexLoad()` and
    `menu_requestLibraryIndexLoad()` now clear `menu_storageBusy` on a
    refused request).
  - The Instrument `kit`-row restore fix (one new owed-state byte,
    `menu_instrumentKitRestorePending`, set/cleared at Instrument/InstrumentMrp
    Load completion, consumed at three sites: the crossing-to-`kit` handler,
    the repeat-turn idempotent branch, and an idle retry at the top of
    `menu_pollPresetStatus()`).
  - The stale Bank-name fix (one repaint in `menu_switchPage()`'s shared tail
    gated behind `!menu_storageBusy`).
  - A rejected first attempt at the freeze fix (a poll-order reorder between
    `menu_tickInstrumentApply()` and `preset_tickDrumsetApply()`) was tried,
    found not to be the actual freeze, and reverted — see §2.1.

KNOWN ISSUES INTRODUCED: None.
KNOWN ISSUES RESOLVED: Load-menu freeze on Kit/Scene entry (permanent, no
  forensic trace survived it — see §2); Load: Instrument `kit`-row restore
  permanently failing while a previewed pool file's apply was still draining
  (see §3); stale Bank/Scene/Kit name briefly shown with the correct slot
  number on fresh Load/Save entry (see §4).

NEXT SESSION RECOMMENDED GOAL: Run the full Instrument `kit`-row restore test
  matrix (endless pot, InstrumentMrp, stop-dead-on-`kit` vs. continued
  scrolling — see §3's test list) and the stale-Bank-name repro sequence
  explicitly, rather than relying on incidental confirmation.
BLOCKERS: None structural. Two deliberately-deferred architectural items
  remain open (see §5) and should not be attempted as quick fixes.

CRITICAL REMINDERS FOR NEXT SESSION:
- **The general rule, now stated once in `AUTOSAVE.md`**: every Menu-side
  filesystem terminal path, success or failure, must call `filesystem_ack()`.
  This has now been independently missed and fixed at three different call
  sites across Sessions 050, 051, and 055. Audit any *new* Menu completion
  path against this rule before assuming the AutoSave writer/trace flush will
  keep running afterward — both are admitted only while the facade is
  `FS_STATUS_IDLE`, and neither failure mode announces itself: foreground
  requests keep working right up until they silently don't.
- **`N` (`INSTRUMENT_ENTRY`) trace records are gated on
  `menu_instrumentLoadActive`**, so their absence on a top-level Kit/Scene
  row is not evidence nothing was posted — it may simply be unrecordable
  from that context. `menu_requestSceneEntryName()` has no trace producer at
  all. Cost real time twice this session. See `DEV_MODES.md`.
- Do not re-attempt the reverted poll-order swap between
  `menu_tickInstrumentApply()` and `preset_tickDrumsetApply()` as a fix for
  anything — it was a real, separately-confirmed livelock mechanism, just
  not the freeze it was built to fix. If a *different* Instrument-apply
  latency symptom shows up later, it is a known, cheap, one-line mitigation
  — but verify against the actual new symptom first.

---

## 1. Scope

This session delivered four working documents (superseded by this log and
the specification-reference updates in §6, safe to delete):

- `SESSION_054-055_TESTING.md` — the retest checklist for Session 054's
  fixes, carried over and hand-annotated during this session's hardware
  round (recursive-delete/HCNAMES confirmation).
- `S055_KIT_LOAD_FREEZE_FIX.md` — the Load-menu freeze investigation, two
  rounds, closed.
- `S055_INST_RESTORE_FIX.md` — the Instrument `kit`-row restore investigation
  and fix.
- `S055_BANK_NAME_ENTRY_FIX.md` — the stale Bank-name-on-entry investigation
  and fix.

(Each of these documents' own internal `# Session 054 —` headers are stale;
they were written before this conversation's session boundary was corrected.
The filenames and this log are authoritative: all four are Session 055 work.)

## 2. Load-menu freeze

**Symptom**: after editing parameters, saving a Scene twice (overwriting),
switching the active Scene in PERF, loading the just-saved Scene, and
re-entering the Load menu, the display froze — first observed on
`Load:[Kit]`, later recurred on `Load:[Scene]`. No recovery without a power
cycle.

### 2.1 Round 1 (WRONG — real bug, not the freeze)

Diagnosed as an unbounded, no-backoff retry livelock:
`menu_pollPresetStatus()` re-dispatched a doomed Kit-entry request once per
foreground pass for as long as AutoSave held the filesystem facade, via
`menu_requestResidentNameScratch()` clearing the shared name cache
*destructively* before discovering the refusal each time. Proven by trace:
855 identical branch-A `'O'` records back to back, bracketed exactly by
AutoSave's `admitted`/`status=DONE`.

Fixed (at the time) by gating the deferred-selection dispatch on
`filesystem_status() != FS_STATUS_BUSY` and adding a non-destructive
early-busy check to `menu_requestResidentNameScratch()`. **This did not stop
the freeze** — it recurred on the Scene page. Re-reading the same capture
further showed the round-1 spin actually *self-recovers* the instant AutoSave
releases (~705 ms total), which is not a hard freeze. The mechanism is real
(and the two guards are correct and were kept), but it was not what the user
saw. A tempting, initially-considered companion fix — reordering
`menu_pollPresetStatus()`'s poll priority so `menu_tickInstrumentApply()`
runs before `preset_tickDrumsetApply()` — was implemented, found unnecessary
once the real cause was located, and **reverted** so it could not confound
verification of the actual fix.

### 2.2 Round 2 (the actual freeze)

The Scene-freeze capture contained **no spin at all** and ended cleanly with
a normal AutoSave completion — nothing whatsoever from the freeze itself was
on the card. Two facts explained the silence:

- `menu_requestSceneEntryName()` (the Scene twin of the Kit entry path)
  emits no trace record of any kind.
- More importantly: **the trace flush had already permanently stopped
  running.**

Root cause: `menu_showFilesystemErrorOverlay()` — the shared terminal path
for nearly every failed Menu filesystem operation (nested Instrument entry,
top-level Kit/Scene/Bank entry, index reloads, and more) — never called
`filesystem_ack()`. One failed read from *any* of those callers parked the
facade at `FS_STATUS_ERROR` **permanently**. Since `filesystem_tick()` admits
the AutoSave parameter drain and the trace flush only while the facade is
`IDLE`, both died silently from that moment on — explaining why *both*
freeze captures ended cleanly with a normal AutoSave completion and zero
evidence of what happened afterward. Foreground requests kept working
(`filesystem_start()` rejects only on `BUSY`, not `ERROR`), which is exactly
what made this invisible until it wasn't.

Separately, two request helpers (`menu_requestInstrumentIndexLoad()`,
`menu_requestLibraryIndexLoad()`) raised `menu_storageBusy` expecting
acceptance and, on refusal, armed a deferred retry that is dispatched only
while `!menu_storageBusy` — a self-inflicted deadlock with no overlay and no
trace, reachable whenever another owner (most often AutoSave) held the
facade at the exact moment of entry.

**Fixed**: `filesystem_ack()` added to the shared error overlay;
`menu_requestSceneEntryName()` given the same non-destructive busy guard as
the Kit path; both helpers now clear `menu_storageBusy` on refusal. Six
sites total, all in `menu.c`, no RAM cost. **Confirmed closed by the user —
freeze no longer reproduces.**

**Deferred, not fixed**: the AutoSave writer reads the shared 9,000-byte name
cache (`fs_list_cache_name`) live while serializing its record, and Menu
clears that same cache directly — bypassing facade arbitration entirely —
from 18 call sites. Only the two hottest callers were closed by the round-2
fix. The general hazard remains; its consequence if it fires is a torn
AutoSave record, not a hang. Needs a proper ownership interlock (refuse or
defer the clear while `fs_autosave_transaction_active` borrows the cache) —
deliberately not attempted here, since it is an architectural change that had
no business riding along with a freeze fix that needed clean verification.
Recorded in `SCOPING_TARGETS.md`.

## 3. Load: Instrument `kit`-row restore

**Symptom**: while a Pattern is playing, scrolling the cursor back to the
fixed `kit` row in Load: Instrument (or InstrumentMrp) is supposed to restore
that voice's pre-browsing parameters. Sometimes it silently does not: the
previewed pool file's parameters stay resident, correlated with the browsed
voice sounding or having just been triggered. Reproducible with both the
encoder and the endless pot.

Root cause: the crossing-to-`kit` handler latches the cursor to `kit`
*before* attempting the restore, and nothing checks whether the attempt
actually posted. `menu_restoreInstrumentLoadTemp()` silently declines when
the filesystem is busy, the snapshot is invalid/type-mismatched, or the
underlying request is rejected outright. Once declined, the failure is
**permanent**: every later detent hits an idempotent "already on `kit`,
nothing to do" branch and does nothing at all — no amount of further
scrolling recovers it, matching the report precisely. A sounding/just-triggered
voice does not gate the restore directly (there is no quiet-check on this
path); it only widens the busy window the crossing can land in, by
lengthening how long the previous pool preview's apply holds the filesystem
busy.

Fixed by tracking **owed-ness of the restore as slot state**
(`menu_instrumentKitRestorePending`) rather than as the outcome of any single
call — it means "the resident slot still holds a previewed pool file, not
the retained snapshot," and is immune to *why* a given attempt declined. Set
when a pool preview (normal or Morph) completes; cleared when a `.hctmp`
restore actually completes (success or genuine terminal failure) or the
snapshot is invalidated at a session boundary. Consumed at three independent
sites so the restore lands whether the user keeps scrolling or stops dead on
`kit`: the crossing itself, the repeat-turn idempotent branch (now re-posts
when something is owed instead of unconditionally no-op'ing), and an idle
retry placed at the very top of `menu_pollPresetStatus()` — ahead of every
tick function, specifically so it cannot be starved the way the reverted
round-1 poll-reorder theory (§2.1) had assumed.

A first attempt blamed poller starvation (the same
`preset_tickDrumsetApply()`-vs-`menu_tickInstrumentApply()` ordering
considered in §2.1) and did not fix the symptom on hardware — kept as a
record in `S055_INST_RESTORE_FIX.md` before deletion, same shape of mistake
as the freeze investigation's round 1.

**Status**: preliminary hardware pass looked correct; full matrix not yet
exhaustively run — endless pot (not just encoder), InstrumentMrp (not just
normal Load), and stopping dead on `kit` the instant it appears (exercises
the idle-retry path specifically, distinct from the repeat-turn path).

## 4. Stale Bank/Scene/Kit name on fresh Load/Save entry

**Symptom**: after a full round of Kit/Instrument/Scene/Bank testing, entering
Load or Save Bank showed the correct slot number (015, matching
`active_bank=15`) but a stale/wrong name until scrolling forced a refresh.
Not reproduced again after a fresh boot.

Root cause: `menu_switchPage()`'s `LOAD_PAGE` case — reached both entering
fresh and toggling Load↔Save — clears the shared name cache, sets the slot
number synchronously, then posts an async `.hcindex` reload
(`menu_requestCurrentLoadSaveSelection()`, which raises `menu_storageBusy`);
the function's shared tail `menu_repaintAll()` then ran **unconditionally**,
painting a frame whose slot *number* was already correct but whose slot
*name* was read live from a cache that had just been cleared and not yet
repopulated. Provably harmless for every other page target (the function
refuses to even start its body while busy, and no non-Load/Save case touches
`menu_storageBusy`, so it is provably still 0 for them at that line) — the
bug is specific to `LOAD_PAGE`.

Fixed by gating that one repaint behind `!menu_storageBusy`, matching the
convention already used elsewhere in this file. Skips exactly the one
premature paint; the reload's own completion chain performs the correct
first paint of the new row.

**Status**: the race is real and provable by static reading, and matches
every detail of the reported symptom shape (correct number, wrong name,
self-heals on any later repaint, absent right after boot since the cache
isn't yet stale-tagged on the very first post-boot navigation). **Not
independently confirmed** — no trace record survived from the actual
incident (the ring had wrapped past it by capture time), so a second
contributing factor cannot be ruled out. Cosmetic only either way: nothing
is loaded/saved against the stale name.

## 5. Deferred (not fixed this session)

- **Name-cache ownership interlock** — §2.2's deferred item. Consequence if
  it fires: a torn AutoSave record, not a hang.
- **Top-level Load/Save entry trace coverage** — `'N'`'s
  `menu_instrumentLoadActive` gate and `menu_requestSceneEntryName()`'s total
  absence of tracing. Cost real time twice this session.
- **`AUTOSAVE_TRACE_RECORD_COUNT` reversion** — still 2,048 (session-scoped
  expansion from Session 054); revert-vs-keep is an open decision.

Both recorded in `SCOPING_TARGETS.md`.

## 6. Documentation close-out

- `AUTOSAVE.md`: the facade-acknowledgement rule generalized and restated
  once, with this session's finding as the concrete example, rather than
  being rediscovered a fourth time.
- `DEV_MODES.md`: `'X'`/`'O'`/`'E'` (and retired `'Y'`) stage documentation
  (Session 054's additions, not yet written up before this close-out), plus
  the `'N'`/Scene-entry tracing-gap note from this session.
- `ASYNCFATFS_REFERENCE.md`, `FILESYSTEM_SPEC.md`: the descend/ascend
  invariant and HCNAMES source-on-save documentation (Session 054's fixes,
  written up at this close-out).
- `SCOPING_TARGETS.md`: pinned duplicate-slot target marked closed for
  ordinary use with the acceptance-matrix caveat; Session 053's test-report
  defects re-triaged; new Session 054-055 deferred-targets section added.
- `MEMORY.md` volatile notes updated with all of the above; the Session
  054/055 root-directory working documents are marked safe to delete.

## 7. Superseded working documents

`SESSION_054-055_TESTING.md`, `S055_KIT_LOAD_FREEZE_FIX.md`,
`S055_INST_RESTORE_FIX.md`, and `S055_BANK_NAME_ENTRY_FIX.md` are superseded
by this handoff and the specification-reference updates above, and may be
deleted.
