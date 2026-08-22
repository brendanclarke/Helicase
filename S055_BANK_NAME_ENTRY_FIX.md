# Session 054 — Stale Bank Name on Load/Save Entry

**Date:** 2026-08-21 · **Branch:** `dev-ph3-autosave-ph3`
**Files changed:** `Core/Menu/menu.c` only (+1 gated call, 1 comment block)
**Status:** Implemented, builds clean. **UNVERIFIED on hardware** — see
[Confidence](#confidence).

## Symptom (as reported)

After a full round of Kit/Instrument/Scene/Bank testing (load/modify/save at
each level, ending with "save a Bank, Load a bank"), entering the Load or
Save Bank menu showed the correct slot number (015, matching
`active_bank=15` in `settings.cfg`) but a stale/wrong name — "something like
CeeBee..." — until scrolling the menu forced a refresh. Not reproduced again
after a fresh boot.

## What I could confirm vs. what I couldn't

I found and fixed a concrete, provable race that causes exactly this shape
of bug — correct number, wrong name, self-heals on any later repaint,
timing-dependent, absent right after boot. I could **not** reproduce the
exact string "CeeBee" or verify this was the *only* contributing factor,
because `SD_CARD/asavetrc.bin` did not retain any record from this specific
incident (the ring had wrapped past it by the time the card was captured).
The fix is applied on the strength of the static analysis below; see
[Confidence](#confidence) for how to read that.

## Root cause

`menu_switchPage()` (`Core/Menu/menu.c`) is the mode-button handler for
Load/Save (and every other top-level page). Its `LOAD_PAGE` case — reached
both when *entering* Load/Save fresh and when *toggling* between the Load
and Save pages, since the button always calls `menu_switchPage(LOAD_PAGE)`
in both directions — does, in order:

```c
filesystem_clearNameCache();               // synchronous: cache -> blank
menu_resetSaveParameters();                // sets slot number correctly
menu_requestCurrentLoadSaveSelection(0);   // posts async .hcindex reload,
                                            // raises menu_storageBusy
menu_refreshLoadSceneLeds();
...
menu_repaintAll();                         // <-- ran unconditionally
```

That final `menu_repaintAll()` — shared by every case in the function, not
just `LOAD_PAGE` — ran regardless of `menu_storageBusy`. For every *other*
page target this is harmless: `menu_switchPage()` refuses to even start its
body while busy (there's a guard at the top of the function that queues the
request instead), and none of the non-Load/Save case bodies touch
`menu_storageBusy`, so it is provably still 0 by the time they reach this
line. But for `LOAD_PAGE` specifically, `menu_requestCurrentLoadSaveSelection()`
has by this point already cleared the shared name cache and posted an async
`.hcindex` reload, raising `menu_storageBusy` for the rest of the function.

The Load-page renderer reads the Bank/Scene name **live from that cache at
paint time**:

```c
if (menu_saveOptions.what == SAVE_TYPE_BANK)
    displayName = filesystem_bankSlotName(menu_currentPresetNr[SAVE_TYPE_BANK]);
```

`menu_currentPresetNr[SAVE_TYPE_BANK]` is set synchronously and correctly
(`bank_restoreBankSlot()`, called from `menu_resetSaveParameters()`) — hence
the slot **number** was always right. But `filesystem_bankSlotName()` reads
whatever the shared cache currently holds, and at the moment of this
premature repaint that cache has just been cleared and re-tagged for a fresh
reload that has not completed yet. The **name** field of the very same frame
is therefore composed from data that is not the requested Bank's data.

This premature frame is drawn into the same `editDisplayBuffer` /
`currentDisplayBuffer` pair that the reload's own completion chain
(`menu_libraryIndexLoadComplete()`, and for Bank specifically the chained
child-Scene preview `menu_bankLoadPreviewComplete()`) will shortly repaint
correctly once the cache is populated. Any *later* repaint — including the
one the completion chain performs, and including whatever the user's next
scroll triggers — reads the by-then-correct cache and shows the right name.
That is the self-heal: nothing about scrolling itself is special, it is
simply "any repaint after the reload finishes."

### Why this explains the specific symptom shape

- **Number correct, name wrong:** the number is written synchronously before
  the async reload starts; the name is read live from the not-yet-reloaded
  cache. Same frame, two different data sources with different readiness.
- **Self-heals on scroll:** scrolling triggers a fresh repaint against the by
  -then-populated cache. So does simply waiting for the completion chain's
  own repaint, which is why this is very likely just a **transient flash**,
  not a display that would ever have stayed wrong indefinitely.
- **Timing-dependent, not deterministic:** the wrong-name window only exists
  while the async `.hcindex` reload is in flight, and only in the way it
  displays — reproducing depends on whether a repaint lands inside that
  narrow window.
- **Absent right after boot:** this specific race requires the shared cache
  to have already held a *different* Load/Save type's data at the moment of
  entry (Kit/Instrument/Scene from the earlier testing steps, in the user's
  sequence). Boot's own synchronous pre-audio index population does not
  leave a stale different-type cache behind for the very first post-boot
  navigation into Load/Save, so the window this bug depends on doesn't exist
  yet on that first entry.

## The fix

One line, in `menu_switchPage()`:

```c
if (!menu_storageBusy)
    menu_repaintAll();
```

Guards the function's shared tail repaint, matching the `!menu_storageBusy`
gating convention already used at dozens of other call sites throughout this
file (e.g. `menu_requestCurrentLoadSaveSelection()`, the various completion
callbacks). Proven safe for every other page target by the reasoning above
(provably a no-op for them); for `LOAD_PAGE` it skips exactly the one
premature paint, letting the reload's own completion chain perform the
(correct) first paint of the new Bank/Scene/Kit row.

Full reasoning is in the adjacent comment block in the source.

```
   text    data     bss     dec     hex filename
 381260     400   94736  476396   744ec build/lxr02.elf
```

### What was deliberately not touched

`menu_resetSaveParameters()` (called earlier in the same `LOAD_PAGE` case,
line ~8859) has its own internal unconditional `menu_repaintAll()`. At the
point it runs, `menu_requestCurrentLoadSaveSelection()` has not been called
yet, so `menu_storageBusy` is still 0 there regardless — gating *that* call
would not have changed anything for this bug, since ordering, not gating, is
the issue at that call site. It is called from 13 other places in this file
with an established, presumably-correct contract ("repaint after resetting
the cursor, storage is normally already idle by then"), so it was left
alone rather than touched speculatively. If a similarly-shaped single-frame
flash is ever observed *before* the fixed frame described above, that
function is the next place to look — but it is one very short-lived frame
immediately superseded by the fixed call site's now-suppressed repaint (or,
if this were the frame the user actually saw, superseded a moment later by
the completion chain's own correct repaint either way), not a candidate for
a display that persists until a manual scroll.

## Confidence

- **The race is real and provable by static reading** — the code order
  (clear cache → set number → post async reload but do not gate the
  imminent repaint → repaint) is unambiguous, and the asymmetry between the
  synchronous number and the live-read name is directly visible in the
  renderer.
- **It is not confirmed to be the *only* thing that happened**, and I could
  not confirm the exact stale text ("CeeBee") because no trace record from
  this incident survived to the captured `asavetrc.bin`. If the glitch
  recurs after this fix, that would be strong evidence something else is
  also contributing — capture the card immediately in that case.
- This is a good match for "a minor glitch" as described: cosmetic-only
  (nothing is loaded, saved, or committed against the wrong name — the
  request that eventually runs, on OK/OW, always reads the *current* slot
  number and the *by-then-correct* cache, never this stale frame), and
  strictly timing-dependent rather than a data-correctness bug.

## Hardware test

1. Reproduce the reported sequence: Kit → Instrument → Scene work (each
   load/modify/save), then Save a Bank, then toggle to Load a Bank (or
   re-enter Load fresh) immediately afterward. Confirm the Bank row shows
   the correct name from the first frame, with no manual scroll needed.
2. Repeat entering Load/Save Bank, Scene, and Kit immediately after browsing
   a *different* one of those three types, to maximize the chance of
   catching the cache in a stale-type state (mirrors the mechanism above,
   not just the exact repro sequence).
3. Regression: Kit/Scene/Bank browsing, and toggling Load↔Save while
   already on the same type, still repaint promptly once their own async
   reload completes (this fix should never *delay* a correct paint — the
   completion chain's repaint is untouched).
