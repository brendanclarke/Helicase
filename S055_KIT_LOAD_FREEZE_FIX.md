# Session 054 — Load Menu Freeze (Kit, then Scene)

**Date:** 2026-08-21 · **Branch:** `dev-ph3-autosave-ph3`
**Files changed:** `Core/Menu/menu.c` only (+302 / −1)
**Status:** **CLOSED** — round 2 shipped; freeze no longer reproduces on
hardware. See [Closeout](#closeout) for the final state and what still needs
harvesting.

> **Round 1 (the two guards in "The fix" below) did not stop the freeze** — it
> recurred on the Scene page. Round 2 is in
> [Round 2 — the actual freeze](#round-2--the-actual-freeze) and supersedes the
> round-1 conclusion about *why* the menu locks. Round 1's guards are correct
> and retained, but they addressed a CPU-burning livelock, not the strand.

---

## Symptom (as reported)

1. Edited some pattern and voice parameters.
2. Saved the Scene; saved again, overwriting both the original save and a
   different Scene slot.
3. Went to PERF, switched to a different Scene, loaded the just-saved Scene
   into that Scene slot.
4. Re-entered the Load menu.

The display froze showing `Load:[Kit     ] 000 RollinZ` and did not recover.

---

## Evidence

From `SD_CARD/asavetrc.bin`, decoded with `tools/devlog_unpack.py`. The
terminal window of the ring, in order:

```
030517 t=64044 L f=0x01 | Kit   Scn6 TRK=1     <- Scene Load marks Scene 6 dirty
030518 t=64044 L f=0x01 | Scene Scn6 TRK=1
030519 t=64044 F f=0x01 | gate_held pending=511  (trace ring dropped 511 records)
030520 t=64044 S        | debounce_tick=3508
030521 t=64046 W f=0x01 | dirty=1 debounce_tick=3508
030522 t= 4602 A        | admitted               <- AutoSave takes the facade
--- 855 consecutive, identical records, ~2-3 per millisecond: ---
030523..031377  O f=0x00 v=0x00010000 | Kit REQUEST slot=0 FAIL=0 hi=0x0001
--- then: ---
031378 t= 6846 V f=0x01 | winner=A gen=31        <- AutoSave writes its record
031379 t= 6931 M f=0x01 | dirty=1 bytes=3856
031380 t= 6975 C f=0x00 | exhausted=0 patches=496
031381 t= 8439 P f=0x01 | target=B gen=32
031382 t= 8454 T f=0x01 | status=DONE            <- AutoSave releases
```

`O … hi=0x0001` is **branch A** of `menu_requestKitEntryNames()` — the
"resident seven-name scratch is invalid or belongs to another Scene" path.
855 of them back to back, with no intervening progress of any kind, spanning
the entire ~2.2 s window in which AutoSave held the filesystem.

A decoding trap worth recording: the spin contains **no `N` records**, which
initially looked like proof that no HCNAMES request was ever posted. It is
not. `menu_traceInstrumentEntry()` opens with `if (!menu_instrumentLoadActive)
return;`, so every `N` record is suppressed on the *top-level* Kit page. The
request was being posted and refused each pass; the refusal trace simply
could not be emitted from this context.

---

## Root cause

An unbounded, no-backoff retry livelock between Menu's deferred-selection
dispatcher and a filesystem facade owned by somebody else.

The cycle, one iteration per foreground pass:

1. `menu_pollPresetStatus()` sees `menu_deferSelectionRequest == 1`,
   `preset_getStatus() == PRESET_IDLE`, `!menu_storageBusy` — all true — and
   dispatches `menu_requestCurrentLoadSaveSelection()`.
2. That clears the defer flag and routes to `menu_requestKitEntryNames()`.
3. Branch A fires (emitting the `O` record) and calls
   `menu_requestResidentNameScratch(scene)`.
4. That function reassigns the scratch Scene, clears its valid flag, calls
   `filesystem_clearNameCache()`, sets `menu_storageBusy = 1`, then posts
   `filesystem_requestLoadResidentKitName()`.
5. The request is **refused** — `filesystem_start()` rejects on
   `status == FS_STATUS_BUSY`, and AutoSave owns the facade.
6. The refusal path clears `menu_storageBusy` and **re-arms
   `menu_deferSelectionRequest = 1`**, then returns.
7. Back to step 1 on the very next pass. Forever.

Two properties make this present as a hard freeze rather than a brief stall:

- **Nothing repaints.** Branch A returns before reaching any
  `menu_repaintAll()`, so the LCD holds whatever frame it last drew for the
  entire duration.
- **The retry is destructive.** Step 4 clears the shared name cache *before*
  discovering the request will be refused, once per pass. That is why the
  frozen frame reads `000 RollinZ`: the Kit row was rendering out of a cache
  that no longer contained Kit rows. `RollinZ` is a *Scene* stem (the card has
  `Scene/037 RollinZ` and `Scene/038 RollinZ`); Kit slot 000 is actually
  `Kit/000 Barf`.

### Why the user's exact sequence triggers it

Nothing here is specific to Kit Load — it is specific to *entering a
name-session menu while AutoSave happens to be writing*. Their sequence makes
that collision near-certain: the Scene Load at step 3 marked a very large
dirty set (the ring holds **25,006** `D` records, including full
whole-instrument sweeps such as `off=13272 Scene6 instrument[4] morph[5]`),
which schedules an AutoSave A/B write of the full 34,768-byte record. Walking
into the Load menu immediately afterwards lands squarely inside that write.

`menu_storageBusy` and `preset_getStatus()` both describe only work *Menu*
started. Neither can see an independent facade owner, so Menu had no way to
know it should wait.

---

## The fix

Two small guards in `Core/Menu/menu.c`, each with an adjacent comment block.

**1. Do not dispatch the coalesced retry while the facade is busy.**
`menu_pollPresetStatus()`'s deferred-selection condition gains
`filesystem_status() != FS_STATUS_BUSY`. The retry now waits quietly and
fires once, on the first pass after the facade goes idle. This alone converts
the livelock into a correct wait, for every deferred selection path (Kit,
Scene, Bank and the nested Instrument pool row), not just this one.

**2. Refuse early and non-destructively.**
`menu_requestResidentNameScratch()` gains a `filesystem_status() ==
FS_STATUS_BUSY` check placed *before* the three destructive lines. It arms the
deferred retry and returns, leaving the scratch identity and the shared name
cache untouched. The last good name therefore stays on screen until the real
reload completes, instead of the page rendering against a cleared cache.

Guard 1 stops the spin; guard 2 stops the spin from corrupting what is
displayed while it lasts. Both are needed: guard 2 also covers the direct
(non-deferred) entry paths into the scratch request.

Nothing else changes — no filesystem code, no AutoSave scheduling, no
ownership rules, no new state, no RAM.

```
   text    data     bss     dec     hex filename
 381172     400   94736  476308   74494 build/lxr02.elf
```

---

## Honest limits of this diagnosis

- The livelock itself is **proven** by the trace: 855 identical branch-A
  records, no progress, bracketed exactly by AutoSave's admit and completion.
- Whether the freeze was **permanent** is *not* proven. The capture ends four
  records after AutoSave reports `T status=DONE`, so the trace cannot show
  whether Menu recovered on the next pass. Reading the code, it should have:
  once the facade is idle the request is accepted and
  `menu_residentNameScratchLoaded()` repaints. Two possibilities remain open:
  (a) the user power-cycled during a long-but-finite spin, or (b) something
  re-acquired or never released the facade, making it genuinely permanent.
  **If the freeze recurs after this fix, that distinction is the next thing to
  establish** — and it is now much easier, because a spinning Menu will no
  longer be masking whatever else holds the facade.
- To make case (b) diagnosable, consider making the `N` refusal trace
  unconditional (drop the `!menu_instrumentLoadActive` early return in
  `menu_traceInstrumentEntry()`, or add a top-level equivalent). Its absence
  is what made this trace ambiguous for a long stretch of the investigation.

---

## Separate observations from this card (NOT fixed, NOT investigated)

Recorded only so they are not lost. I have **no evidence** these are
defects, and none of them were touched by this fix:

- **`Kit/err037 Slak2`** — this is the boot Kit-quarantine mechanism
  (`filesystem_makeQuarantineName()`, `filesystem.c:17285`) working *as
  designed*: a Kit that failed boot validation is renamed out of the loadable
  namespace with an `err` prefix. It does mean some Kit failed validation,
  which may be worth chasing separately. MEMORY.md already carries the boot
  Kit-quarantine refactor (KQ019KST) as a deferred target.
- **`Bank/old012-c76c`** — a leftover promotion-style name. Session 053's
  overwrite rework states that "temporary/old promotion names are not used",
  so this is probably a stale artifact predating that work rather than
  something being created now. Worth confirming it is not being regenerated.
- **Kit/Scene stems that look like near-duplicates** — `Slkty` / `Slktyz` /
  `Slktyzow`, `DocWire` / `DocWirez`, `Rollin` / `Rollinz` / `RollinJ` /
  `RollinZ`. These are equally consistent with ordinary test naming during
  this session, and I am **not** claiming name corruption. Flagged only
  because if they were *not* all typed deliberately, that would point at a
  name-buffer termination bug worth its own investigation.

---

## Hardware test

1. Reproduce the original sequence: edit parameters, save a Scene twice
   (overwriting), PERF-switch Scenes, load that Scene, immediately re-enter
   the Load menu. Confirm no freeze and that the Kit row shows a real Kit
   name for the selected slot.
2. The collision window is what matters, so try entering Load at several
   delays after the Scene Load (immediately, ~1 s, ~3 s) to sample across the
   AutoSave write.
3. Confirm the Kit row never displays a Scene/Bank stem — the name shown must
   belong to the Kit index.
4. Regression: ordinary Kit Load browsing, Scene Load, and Bank Load still
   work, and the nested Instrument pool-row retry still coalesces correctly
   while scrolling fast (that path shares the guard changed here).
5. If a freeze still occurs, capture `asavetrc.bin` immediately — under this
   fix a repeat freeze means a genuinely stuck facade owner, which is a
   different and more serious bug than the one fixed here.

---

# Round 2 — the actual freeze

Round 1's livelock is real and its guards are retained, but it was **not** the
freeze. Proof, from the round-1 capture itself (I had not read far enough):

```
007471 t=52898 O | Kit REQUEST slot=0 hi=0x0001   <- branch-A spin
007472 t=52898 T | status=DONE                    <- AutoSave releases
007473 t=52898 O | Kit REQUEST slot=0 hi=0x0001
007474 t=52935 O | Kit REQUEST slot=1 hi=0x0003   <- branch C: RECOVERED
```

The spin **self-recovers the instant AutoSave releases**, and that whole
episode lasted ~705 ms. A sub-second CPU spin is not the reported freeze.

## What the new (Scene) capture shows

The Scene-freeze capture contains **no spin at all** and ends cleanly with a
normal AutoSave completion (`A → V → M → C → P → T status=DONE`, gen 33→34).
Nothing whatsoever from the freeze itself is on the card.

That absence is the finding. Two facts explain it:

- `menu_requestSceneEntryName()` — the Scene twin of the Kit entry path —
  emits **no trace record of any kind**. A livelock there is invisible.
- More importantly, **the trace flush had already stopped running.**

## Root cause: `menu_showFilesystemErrorOverlay()` never acknowledged the facade

```c
static void menu_showFilesystemErrorOverlay(void)
{
    const char *code = filesystem_errorCode();
    menu_storageBusy = 0u;
    ...
    menu_repaintAll();          /* <- no filesystem_ack() anywhere */
}
```

This overlay is the common terminal path for essentially every failed Menu
filesystem operation. None of its callers acknowledged the facade either, so a
single failed name/index read parked `status` at `FS_STATUS_ERROR`
**permanently**.

`filesystem_tick()` admits the AutoSave parameter drain and the AutoSave trace
flush *only* while `status == FS_STATUS_IDLE`. So from that moment:

- the AutoSave writer never ran again — retained mutations stopped draining to
  `.hcprms`;
- the trace flush never ran again — every record produced afterwards stayed in
  the RAM ring and **never reached `asavetrc.bin`**.

Foreground requests kept working (`filesystem_start()` rejects only on `BUSY`,
not `ERROR`), which is exactly what made this silent and why the device
appeared fine right up until it wasn't.

This is precisely the failure mode already documented at length inside
`menu_loadCommandFinalIndexComplete()` — *"Without this call the facade stays
DONE indefinitely… every RAM-ring trace record… remains unflushed, and the
armed `.hcprms` mutation drain can never start"*. That comment fixed the strand
at **one** call site. `menu_residentNameScratchFlushComplete()` fixed it at a
second. Every other failure path still leaked it. Hence "something like this
has happened before and should have been fixed" — it was, twice, locally.

## Root cause: two helpers arm a retry they have already made unreachable

`menu_requestInstrumentIndexLoad()` and `menu_requestLibraryIndexLoad()` both do:

```c
menu_storageBusy = 1u;                       /* raised expecting acceptance */
if (!filesystem_request...(...))
    menu_deferSelectionRequest = 1u;         /* retry armed, busy still 1 */
```

The deferred retry is dispatched only while `!menu_storageBusy`. Leaving the
gate raised means the single recovery path these functions rely on **can never
fire**, and nothing else clears the flag: a permanently locked menu, with no
error overlay and no trace. Reachable whenever another owner — most often the
AutoSave drain — holds the facade as the user enters or re-enters the page.
This is a genuine hard strand, unlike round 1's self-recovering spin.

## Round 2 fixes

1. **`menu_showFilesystemErrorOverlay()` now calls `filesystem_ack()`** after
   copying the error code (ack does not clear `filesystem_errorCode()` and is a
   no-op when already IDLE/BUSY). One line; restores both background sinks for
   every failure path at once. **This is the AutoSave defect.**
2. **`menu_requestSceneEntryName()`** gains the same non-destructive busy guard
   already added to the Kit path in round 1.
3. **`menu_requestInstrumentIndexLoad()` and `menu_requestLibraryIndexLoad()`**
   now clear `menu_storageBusy` on a refused request.

```
   text    data     bss     dec     hex filename
 381228     400   94736  476364   744cc build/lxr02.elf
```

## Also confirmed: the cache-vs-AutoSave hazard you suspected

The AutoSave writer reads `fs_list_cache_name` — the shared 9,000-byte name
cache — as a **live data source** while serializing its record
(`autosave_formatInitialChunk()`, in both `filesystem_ensureAutosaveFiles_tick()`
and `filesystem_autosaveParameterDrain_tick()`). Menu calls
`filesystem_clearNameCache()` **directly**, bypassing facade arbitration
entirely, from 18 sites.

The writer's page guard only blocks *admission* while the user is on
Load/Save — there is no pause/resume, and nothing stops Menu from yanking the
cache out from under a transaction admitted just before the page was entered.
Fixes 2 and round-1's guard 2 close this window for the two hottest callers,
but **the general hazard remains** at the other `filesystem_clearNameCache()`
sites. A proper fix is an ownership interlock (refuse or defer the clear while
`fs_autosave_transaction_active` borrows the cache). Deferred deliberately:
that is an architectural change, not a targeted fix, and it should not ride
along with a freeze fix that needs clean verification.

## Honest limits

- Fix 1 is **certain** to be a real defect and certain to explain the missing
  evidence in both captures. It is **not proven** to be the freeze the user
  saw, because the freeze produced no record — that is the whole point.
- Fix 3 is the strongest candidate for the *visible* lock: it is the only path
  found that strands `menu_storageBusy` with no recovery and no overlay.
- I could not confirm whether the round-1 build was flashed before this
  recurrence. If it was not, round 1's guards are still untested.
- After this round, a repeat freeze should finally leave evidence: the trace
  flush stays alive, so capture `asavetrc.bin` immediately and look for the
  last records before the stall.

## Hardware test (round 2)

1. Repeat the reported sequence: load Scene → PERF → change active Scene →
   back to Load menu, switching quickly. Confirm no freeze on Scene or Kit.
2. Force a failure (e.g. select a slot whose payload is bad, or pull/reinsert
   scenarios that produce an `FsErr` overlay), then confirm the device keeps
   AutoSaving afterwards — `.hcprms` generation should keep advancing and
   `asavetrc.bin` should keep growing. Before fix 1 both stopped permanently
   after the first overlay.
3. Confirm an `FsErr` overlay still shows the correct code (ack must not blank it).
4. Regression: nested Instrument index load, Bank Load preview, Kit browsing.

---

# Closeout

**2026-08-21 — freeze no longer reproduces on hardware after round 2.**
Investigation closed.

## Final change set

All in `Core/Menu/menu.c`. **+302 / −1**, no other file touched. No filesystem
code, no AutoSave scheduling or ownership rules, no new state, **no RAM cost**.

| # | Site | Change | Round |
|---|------|--------|-------|
| 1 | `menu_showFilesystemErrorOverlay()` | add `filesystem_ack()` | 2 |
| 2 | `menu_pollPresetStatus()` deferred dispatch | require `filesystem_status() != FS_STATUS_BUSY` | 1 |
| 3 | `menu_requestResidentNameScratch()` | early non-destructive busy guard | 1 |
| 4 | `menu_requestSceneEntryName()` | early non-destructive busy guard | 2 |
| 5 | `menu_requestInstrumentIndexLoad()` | clear `menu_storageBusy` on refusal | 2 |
| 6 | `menu_requestLibraryIndexLoad()` | clear `menu_storageBusy` on refusal | 2 |

```
   text    data     bss     dec     hex filename
 381228     400   94736  476364   744cc build/lxr02.elf
```

## What was actually wrong

Two distinct defects with one shared theme — **Menu could not see that the
filesystem facade had another owner, and did not return it when it broke.**

1. **The strand (fixes 1, 5, 6).** `menu_showFilesystemErrorOverlay()` — the
   common terminal path for nearly every failed Menu filesystem operation —
   never acknowledged the facade, so one failed read parked `status` at
   `FS_STATUS_ERROR` permanently. Because `filesystem_tick()` admits the
   AutoSave parameter drain and the trace flush *only* while `IDLE`, both
   background sinks died silently from that point on. Separately, two request
   helpers raised `menu_storageBusy` expecting acceptance and, on refusal,
   armed a deferred retry that is dispatched only while `!menu_storageBusy` —
   a self-inflicted deadlock with no overlay and no trace.
2. **The livelock (fixes 2, 3, 4).** Menu re-posted a doomed request once per
   foreground pass for as long as another owner held the facade, clearing the
   shared name cache destructively each time. Self-recovering, but it burned
   the foreground and rendered the page against a cleared cache — which is how
   a Scene stem (`RollinZ`) appeared on the Kit row.

The strand is what made this so hard to see: it killed the trace flush, so the
evidence for everything that happened afterwards was never written to the card.
Both freeze captures ended cleanly with a normal AutoSave completion and
contained no record of the freeze that followed.

## Confidence

- Fixes 1, 5, 6 are **certain** defects on inspection, independent of the
  freeze: each is a provable strand of either the facade or the UI gate.
- Fix 1 is **confirmed** to explain the missing trace evidence in both captures.
- The freeze not recurring is **good but not exhaustive** evidence. The
  original was timing-dependent (a collision with an in-flight AutoSave write),
  so absence over a test session does not prove elimination. The regression
  test in step 2 below is the one that actually verifies fix 1 directly rather
  than by absence.
- Which of fixes 1/5/6 was *the* freeze is still not distinguished, and now
  cannot be without deliberately reverting them one at a time. Not worth doing:
  all three are correct regardless.

## Deferred — needs a decision, NOT fixed here

**Name-cache ownership interlock (the real remaining hazard).** The AutoSave
writer reads `fs_list_cache_name` — the shared 9,000-byte name cache — as a
*live data source* while serializing its record (`autosave_formatInitialChunk()`,
in both `filesystem_ensureAutosaveFiles_tick()` and
`filesystem_autosaveParameterDrain_tick()`). Menu calls
`filesystem_clearNameCache()` **directly from 18 sites**, bypassing facade
arbitration entirely. The writer's page guard only blocks *admission* while the
user is on Load/Save; nothing prevents Menu from yanking the cache out from
under a transaction that was admitted just before the page was entered.

Fixes 3 and 4 close this window for the two hottest callers. The general hazard
remains. A proper fix is an ownership interlock — refuse or defer the clear
while `fs_autosave_transaction_active` borrows the cache. Deliberately not done
here: that is an architectural change and had no business riding along with a
freeze fix that needed clean verification.

**Consequence if it fires:** a torn AutoSave record, not a hang. Worth
correlating against any future `.hcprms` CRC/validation failure, and against
the boot Kit-quarantine events noted below.

## Diagnostics debt worth paying

`menu_traceInstrumentEntry()` early-returns on `!menu_instrumentLoadActive`, so
every `N` refusal record is suppressed on the top-level Kit/Scene pages. This
cost real time twice in this investigation: the absent `N` records looked like
proof that no request was posted, when they were simply unrecordable. The
Scene entry path (`menu_requestSceneEntryName()`) emits **no trace at all**,
which is why the second freeze was completely invisible.

Recommend a top-level equivalent of the entry trace before the next
Load/Save-family investigation.

## To harvest at session closeout

Per the project convention (`MEMORY.md` → durable facts belong in
`knowledge_files/log_archive/` or `specification_reference/`; deferred targets
in `SCOPING_TARGETS.md`):

- **`MEMORY.md` Volatile Notes** — the facade-acknowledgement rule: *every*
  Menu terminal path must `filesystem_ack()`, because the AutoSave writer and
  trace flush are admitted only while the facade is `IDLE`. This has now been
  fixed locally three times (`menu_loadCommandFinalIndexComplete()`,
  `menu_residentNameScratchFlushComplete()`, and now the shared error overlay);
  it should be stated once as a rule rather than rediscovered a fourth time.
- **`SCOPING_TARGETS.md`** — the name-cache ownership interlock above, and the
  top-level entry trace.
- **`DEV_MODES.md`** — note that `N` records are gated on
  `menu_instrumentLoadActive`, so their absence is not evidence.

This working doc and `S054_INST_RESTORE_FIX.md` are disposable once the above
is harvested into the durable locations.

## Regression tests to keep

1. Load Scene → PERF → change active Scene → back to Load menu, switching
   quickly. No freeze on Scene or Kit. *(This is the original repro.)*
2. **Directly verifies fix 1:** force any `FsErr` overlay, then confirm the
   device keeps AutoSaving afterwards — `.hcprms` generation must keep
   advancing and `asavetrc.bin` must keep growing. Before fix 1 both stopped
   permanently after the first overlay, for the rest of the session.
3. An `FsErr` overlay still shows its correct code (ack must not blank it).
4. Kit row never displays a Scene/Bank stem.
5. Nested Instrument index load, Bank Load preview, fast Kit browsing.
