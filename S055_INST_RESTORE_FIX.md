# Session 054 — Load: Instrument `kit` Row Restore Failure

**Date:** 2026-08-21 · **Branch:** `dev-ph3-autosave-ph3` ·
**Files changed:** `Core/Menu/menu.c` only (+160 / −0)
**Status:** Implemented, builds clean. Preliminary hardware pass looked
correct; full test matrix still outstanding — see [Status](#status).

---

## Symptom (as reported)

While a Pattern is playing, in **Load: Instrument** (and **Load:
InstrumentMrp**), scrolling the cursor back up past pool row `000` to the
fixed `kit` row at the top of the list is supposed to restore that voice's
original (pre-browsing) parameters. If the voice being browsed is sounding
— actively ringing, or has just been triggered — when the user scrolls
back to `kit`, the restore sometimes silently does not happen: the
parameters of whatever pool file was last previewed stay resident instead.
Selecting an actual pool file always works reliably, sounding or not; only
the hidden `kit` row is affected. Reproduces with both the encoder and the
endless pot.

---

## Root cause

`Core/Menu/menu.c`, the crossing-to-`kit` branch of the nested Instrument
Load encoder handler. Reduced to the essential ordering:

```c
menu_deferSelectionRequest = 0u;
if (menu_instrumentLoadSource == MENU_INSTRUMENT_SOURCE_KIT &&
    menu_instrumentLoadShownType == menu_instrumentLoadType) {
    if (!menu_storageBusy)
        menu_repaintAll();
    return;                                   /* (A) idempotent no-op  */
}
menu_instrumentLoadSource = MENU_INSTRUMENT_SOURCE_KIT;   /* (B) latch */
menu_instrumentLoadShownType = menu_instrumentLoadType;
menu_instrumentLoadShownIndex = 0u;
menu_restoreInstrumentLoadTemp();                         /* (C) attempt */
```

**The cursor is latched to `kit` at (B) before the restore is attempted at
(C) — and nothing anywhere checks whether (C) actually succeeded.**

`menu_restoreInstrumentLoadTemp()` is silent when it declines. It returns
without posting anything if `menu_storageBusy` is set, if the snapshot is
invalid or its type no longer matches, **or if
`preset_loadInstrumentTemp()` / `filesystem_requestLoadInstrumentTemp()`
rejects the request outright** (a separate, internal notion of busy that
`menu_storageBusy` does not cover). In every one of those cases the UI is
now showing `kit` while the parameters still belong to the last previewed
pool file.

From that moment the failure is **permanent**: every subsequent detent
satisfies the `source == KIT && shownType == loadType` test at (A) and
returns having done nothing at all. The user can keep turning the encoder
or the pot indefinitely — the row already says `kit`, so the firmware
believes there is nothing to do. Only leaving and re-entering the whole
nested Instrument session (which disposes and recaptures the snapshot)
clears it.

### Why playback is the trigger

Playback does not gate the restore directly — there is no quiet-check
anywhere on this path (`instrumentManager_resetRuntimeSlot()` at
`InstrumentManager.c:1358` is unconditional for the active Scene). Playback
simply **widens the window in which (C) is declined**:

- A sounding voice keeps `preset_tickDrumsetApply()`'s Scene-switch worker
  active (its own comment: *"Continuous play can keep an envelope non-quiet
  indefinitely"*, `presetManager.c:128-137`), which starves
  `menu_tickInstrumentApply()` and so stretches how long the previous pool
  preview holds `menu_storageBusy`.
- The audio render loop and trigger dispatch compete for foreground passes,
  further lengthening every async load.

So the user scrolls `000 → kit` while the `000` preview is still draining,
(C) declines, (B) has already latched, and the bug locks in. Hence
"sometimes", and hence the correlation with a sounding or just-triggered
voice.

### Why pool rows were never affected

Pool selection carries a genuine retry keyed on desired state, not on one
call's outcome: `menu_deferSelectionRequest` is set whenever a pool
selection can't post, and `menu_pollPresetStatus()` re-dispatches it once
the filesystem is idle (`menu.c:7796-7815`). Every additional detent also
re-requests. The `kit` row had no equivalent — its only recovery,
`menu_finishInstrumentApplySession()`, fires solely as a side effect of some
*other* Instrument apply completing, and does nothing if the cursor has
since moved. That asymmetry is the whole bug, and it matches the original
diagnosis in the bug report exactly: *"the same load path should be used for
the cached, original instrument parameters."*

---

## The fix

Track **owed-ness of the restore as data state**, not as the outcome of any
single call. One new byte:

```c
static uint8_t menu_instrumentKitRestorePending = 0u;
```

It means: *the resident slot currently holds a previewed pool Instrument
rather than the retained entry snapshot, so landing on `kit` still has real
work to do.* Because it describes the slot rather than a call, it is immune
to **why** any given attempt failed to post.

**Set / cleared at completion** (the only point that proves what actually
landed in the slot), using `filesystem_loadedInstrumentWasTemporary()` — the
filesystem's immutable record of which request opened the staged file, and
the only trustworthy discriminator here since the visible cursor may have
moved while the load drained:

| Event | Effect |
|---|---|
| `PRESET_OP_INSTRUMENT_LOAD`, pool origin, OK | set (restore now owed) |
| `PRESET_OP_INSTRUMENT_LOAD`, `.hctmp` origin | cleared (restore landed) |
| `PRESET_OP_INSTRUMENT_MORPH_LOAD`, OK | set |
| `PRESET_OP_INSTRUMENT_MORPH_TEMP_LOAD` | cleared |
| Failed terminal `.hctmp` read | cleared (bounds the retry; ERR overlay shown) |
| Failed *pool* load | unchanged (slot was never replaced) |
| `menu_invalidateInstrumentLoadTemp()` | cleared (snapshot and owed state share a lifetime) |

**Consumed at three independent sites**, so the restore lands whether the
user keeps scrolling or stops dead on `kit`:

1. **The crossing (C)** — unchanged in behaviour, but a decline is no longer
   terminal.
2. **The idempotent branch (A)** — now re-posts the restore when one is
   still owed, instead of unconditionally returning. This is the change that
   makes repeated scrolling self-healing and matches the user's mental
   model ("scrolling back up to that entry restores it"). Turns that arrive
   with nothing owed stay exactly as cheap as before.
3. **The top of `menu_pollPresetStatus()`** — an idle retry, deliberately
   placed *ahead of every tick function* (which return early as soon as any
   one of them does work) so it cannot be starved by
   `preset_tickDrumsetApply()`. This covers the user stopping the moment
   they reach `kit`, with no further detent to carry the work.

The critical ordering win: the owed flag is set when the **pool load
completes**, which is frequently *after* the user has already crossed to
`kit`. Previously nothing re-examined the situation after that point — that
is precisely the race the report describes. Now the idle retry picks it up
on the next foreground pass.

Both normal and Morph modes are covered; they share the flag because only
one nested Instrument session, and therefore only one snapshot, can be open
at a time. `menu_restoreInstrumentLoadTemp()` already dispatches to
`preset_loadInstrumentMorphTemp()` from its own morph branch, so no separate
morph plumbing was required.

### Termination / no-spin argument

- The idle retry requires `PRESET_IDLE && !menu_storageBusy`, so it cannot
  double-post while a request is in flight.
- `menu_restoreInstrumentLoadTemp()` self-declines on an invalid or
  type-mismatched snapshot, so an unusable snapshot cannot spin.
- Every type/voice/mode change routes through
  `menu_invalidateInstrumentLoadTemp()`, which disposes snapshot and owed
  state together — so the type guard can never strand a permanently-owed
  restore.
- A genuine terminal read failure clears the flag rather than retrying
  forever.

---

## Change inventory

`Core/Menu/menu.c` only. Purely additive: **160 insertions, 0 deletions**
(`git diff --stat`). Each change carries an adjacent descriptive comment
block per project convention.

| Site | Approx. line | Role |
|---|---|---|
| `menu_instrumentKitRestorePending` declaration | 1159 | owed-state definition |
| `menu_invalidateInstrumentLoadTemp()` | 3635 | clear (session boundary) |
| Crossing handler — idempotent branch + crossing | 6944–6980 | attempt / retry |
| `menu_pollPresetStatus()` idle retry | 7705–7730 | unstarvable retry |
| Failure-path clear | 7868 | bounds terminal failure |
| `PRESET_OP_INSTRUMENT_LOAD` | 8176 | set (pool) / clear (`.hctmp`) |
| `PRESET_OP_INSTRUMENT_MORPH_LOAD` | 8238 | set |
| `PRESET_OP_INSTRUMENT_MORPH_TEMP_LOAD` | 8251 | clear |

No change to `presetManager.c`, `InstrumentManager.c`, filesystem, the
quiet threshold, the force-tick bound, poll order, or any Scene-switch
commit logic.

**RAM:** +1 byte normal SRAM1 (one `uint8_t` global, unconditional — not
gated behind `DEV_MODE_LOGGING`).

**Build:** `make` succeeds, no new warnings under `-Wall -Wextra`:

```
   text    data     bss     dec     hex filename
 381156     400   94736  476292   74484 build/lxr02.elf
```

Not comparable to the Session 053 handoff baseline (text 379,660 / data 396
/ bss 94,612) — the working tree also carries the uncommitted Session 054
recursive-delete and filesystem/sequencer work.

---

## Rejected first attempt (kept as a record)

The first pass blamed **poller starvation**: `menu_pollPresetStatus()` checks
`preset_tickDrumsetApply()` before `menu_tickInstrumentApply()` and stops the
pass as soon as one poller reports work, so
`menu_finishInstrumentApplySession()` — at the time the only retry for a
dropped `kit` restore — could be starved for seconds during continuous
playback.

That mechanism is **real but is not the cause of this bug.** It only delays
the retry; it does not destroy it. The fix built on it (swapping the poll
order, plus a one-shot flag armed by sampling `menu_storageBusy` immediately
before the restore call) **did not fix the symptom on hardware.**

Why it missed: it assumed the only way the restore gets dropped is
`menu_storageBusy` being set at the instant of the crossing. That is one path
of several, and it left the *permanent* (A)-branch failure completely intact.
**The poll reorder has been reverted** so it cannot confound verification of
the real fix; the retry it was protecting is now placed where it cannot be
starved anyway.

Worth keeping in mind: the starvation is still real, and if a *different*
Instrument-apply latency symptom shows up later, the reorder is a known,
cheap, one-line mitigation.

---

## Deferred / follow-ups

Found while investigating; **not** addressed here, and not required for this
bug. Candidates for `SCOPING_TARGETS.md` at session closeout:

- **Shared non-reentrant apply cursor.** `instrument_apply_active` /
  `instrument_apply_phase` / `instrument_apply_rebind_source`
  (`presetManager.c:144-152`) are one state machine driven by *both*
  `preset_startSceneModulationRebind()` (`:1290`) and
  `preset_startInstrumentApplyImage()` (`:1754`). If a Scene-switch rebind
  starts while an Instrument Load apply is mid-flight, it forces the phase
  back to `TARGET_REBIND` / `rebind_source = 0`, discarding the remainder of
  that apply's `MORPH_REBUILD` phase. Stored SceneData is unaffected (it is
  committed synchronously before any ticking), but the morph-interpolated
  *runtime* value can be left stale relative to the descriptor just
  committed. Cosmetic/audible-only, low frequency; revisit if a morph
  artifact is observed after a Scene switch overlapping an Instrument load.

- **`menu_finishInstrumentApplySession()` is now partly redundant.** Its
  `kit`-restore branch overlaps the new owed-state retry. Left in place
  deliberately — it is harmless (the restore self-declines when nothing is
  owed) and removing it is unnecessary churn during an active bug hunt. Tidy
  only if that function is being touched for another reason.

---

## Status

Implemented, builds clean. Preliminary hardware check by the user looked
correct ("seems ok"); **the full matrix below is still outstanding.** Do not
promote this to verified, or write it into `MEMORY.md` / a handoff log as
confirmed, until the remaining cases pass.

Test matrix, mirroring the original report:

1. Start playback on a pattern that keeps re-triggering the target voice.
2. Enter Load: Instrument on that voice; note the `kit` row name.
3. Scroll down to `000` (and a few more pool rows) so a preview is audibly
   resident, then scroll straight back up to `kit` **without pausing** —
   this is the case that reliably reproduced the failure.
4. Confirm the original instrument's sound returns, with no further input.
5. Repeat while stopping dead on `kit` the instant it appears (exercises the
   idle-retry path rather than the repeat-turn path).
6. Repeat both with the endless pot instead of the encoder.
7. Repeat both in Load: InstrumentMrp.
8. Regression: ordinary pool selection still previews correctly.
9. Regression: a PERF Scene switch during playback still settles normally
   (poll order is back to its original arrangement, so this should be
   unchanged).
10. Regression: exiting the nested Instrument session while a restore is
    owed leaves no stale state — re-entering the same voice should capture a
    fresh snapshot and behave normally.
