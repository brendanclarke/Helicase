# S070 Phase 2 — LSR-01 Menu User Feel

## Problem

After the A1–A4 restructure in S070_PHASE2_LSR01_SUPPLEMENT.md, menu
transitions work on the first button press, but the visual update is
delayed by the full HCNAMES flush-and-reload chain. The user sees no
response for ~1–3 seconds after pressing a voice button or exiting to Kit,
then the menu suddenly changes. This feels broken even though it is
functionally correct.

**Observed sequence (voice button entry, dirty mask present):**

```
t=0ms   Voice button pressed
        → state setup (instrumentLoadActive=1, type, voice, LEDs)
        → menu_endResidentNameScratchSession() starts HCNAMES write
        → menu_storageBusy = 1, function returns early
        — no repaint —

t≈100ms HCNAMES write completes
        → filesystem_clearNameCache()
        → menu_requestInstrumentEntryNames()
          → HCNAMES scratch read starts (facade BUSY again)
        — no repaint —

t≈200ms HCNAMES scratch read completes
        → menu_requestInstrumentIndexLoad()
          → .hcindex read starts (facade BUSY again)
        — no repaint —

t≈300ms .hcindex read completes
        → menu_repaintAll()               ← FIRST visible change
```

The user waits ~300ms–3s (depends on card speed and HCNAMES row count)
with no visual feedback. The same delay occurs on Instrument-to-Instrument
voice switches, Kit exit, and Pot-1 Instrument entries.

## Goal

Make the menu change visually immediate: the LCD shows the destination
context (correct header, voice LEDs, type label) on the same frame as the
button press. The scroll list is shown as unpopulated/blank until the
background SD operations complete and the name cache is re-populated. The
user can already scroll while `menu_storageBusy` is set because the
renderer uses `MENU_INSTRUMENT_PROVISIONAL_COUNT` (1000) when the cache
count is zero during busy, and the encoder path does not clamp against an
empty cache during busy (line 7719).

## Existing infrastructure that supports this

1. **`menu_storageBusy` paint path** (line 7719–7723): When
   `menu_storageBusy` is set and `filesystem_instrumentCount()` returns 0,
   the renderer substitutes `MENU_INSTRUMENT_PROVISIONAL_COUNT` (1000) as
   the display count and skips the index clamp. This already allows the
   Instrument Load menu to render a meaningful frame (type label, cursor,
   `kit` row with blank name) while the cache is loading.

2. **Kit blank-name tolerance** (line 7917–7927): The Kit Load renderer
   reads `preset_currentName` directly. During entry/exit cache handoff the
   name is blank (eight spaces); the renderer copies it as-is. No crash, no
   garbage — just a momentarily empty name field.

3. **`menu_repaintAll()` is free of filesystem I/O**: It zeroes the display
   shadow buffers and calls `menu_repaint()`, which calls the
   page-specific renderer. The renderer reads only RAM state
   (`menu_instrumentLoadActive`, `menu_saveOptions`, `menu_instrumentLoadType`,
   `preset_currentName`, etc.) and never touches the SD card.

4. **Completion-time repaint already exists** (line 4682–4683): The flush
   completion callback already calls `menu_repaintAll()` after dispatching
   to the entry-name request chain. When the final `.hcindex` load
   completes, its own completion also calls `menu_repaintAll()` (e.g.
   line 4543, 5180, 5202). So the populated list will appear as soon as the
   names are available — no additional repaint is needed at the end.

## Plan

### Principle

Insert one `menu_repaintAll()` call at each flush site, immediately after
the destination state is installed and before the flush starts. This is the
"optimistic repaint": it paints the destination context with whatever cache
data is available (typically blank names / provisional count). The existing
completion-time repaint provides the "definitive repaint" once the name
cache is fully loaded.

The flush itself sets `menu_storageBusy = 1u` (line 4710), which the
renderer already checks. As long as the repaint occurs after the state
setup and before the flush call, the renderer sees:

- `menu_instrumentLoadActive == 1` (or 0 for exit) → correct header
- `menu_instrumentLoadType` → correct type label
- `menu_storageBusy == 0` at paint time (flush hasn't started yet) →
  renders the `kit` row with its current (possibly stale) name, which is
  correct visual feedback
- Or, if repainted *during* the flush: `menu_storageBusy == 1` →
  provisional count, skip clamp, blank pool name

Either way, the header and type are correct and the user sees the menu
change instantly. The name field shows either the last-known name or blank
spaces — both are acceptable intermediate states.

### Voice LED feedback

The voice LEDs are already set by `menu_setActiveVoice()` and
`menu_refreshLoadSceneLeds()` before the flush in the A1–A4 restructure.
These are register writes that take effect immediately. No additional LED
work is needed.

---

### F1. ADD: Optimistic repaint in voice-button entry — `menu.c`

**Location:** `menu_loadInstrumentVoicePressed()`, after the full state
setup block (voice, type, mode, LEDs) and before the dirty-mask test at
line 6405.

**Change:** Insert `menu_repaintAll();` immediately before the
`if (menu_residentNameDirtySceneMask != 0u && ...)` block.

**After:**
```c
    menu_instrumentLoadClampIndex();
    menu_setActiveVoice(voiceNr);
    menu_refreshLoadSceneLeds();
    menu_repaintAll();                       /* ← optimistic repaint */
    if (menu_residentNameDirtySceneMask != 0u &&
        menu_endResidentNameScratchSession())
        return 1u;
    menu_requestInstrumentEntryNames();
    if (!menu_storageBusy)
        menu_repaintAll();
    return 1u;
```

**Effect:** The LCD immediately shows `Load:[Type]` for the selected
instrument voice. If no flush is needed, the existing conditional repaint
at line 6415–6416 fires as a second (redundant but harmless) repaint after
the entry names are requested. If a flush is needed, the function returns
early at `return 1u` and the LCD already shows the correct header; the
flush completion chain will repaint again when the names are loaded.

**Cost:** One extra `menu_repaintAll()` call per voice-button press.
This is a memset(34) + one LCD frame render (~150µs on the 216MHz M7).
Negligible.

---

### F2. ADD: Optimistic repaint in Pot-1 Instrument Load — `menu.c`

**Location:** `menu_loadSaveEnterInstrumentLoad()`, same position as F1.

**Change:** Insert `menu_repaintAll();` after the state setup and before
the dirty-mask test.

**After:**
```c
    menu_instrumentLoadClampIndex();
    menu_setActiveVoice(voice);
    menu_loadSaveSetInstrumentVoiceLed(voice);
    menu_refreshLoadSceneLeds();
    menu_repaintAll();                       /* ← optimistic repaint */
    if (menu_residentNameDirtySceneMask != 0u &&
        menu_endResidentNameScratchSession())
        return;
    menu_requestInstrumentEntryNames();
```

---

### F3. ADD: Optimistic repaint in Pot-1 Instrument Save — `menu.c`

**Location:** `menu_loadSaveEnterInstrumentSave()`, same position.

**Change:** Insert `menu_repaintAll();` after the state setup and before
the dirty-mask test.

**After:**
```c
    menu_setActiveVoice(voice);
    menu_loadSaveSetInstrumentVoiceLed(voice);
    menu_refreshLoadSceneLeds();
    menu_repaintAll();                       /* ← optimistic repaint */
    if (menu_residentNameDirtySceneMask != 0u &&
        menu_endResidentNameScratchSession())
        return;
    menu_requestInstrumentEntryNames();
```

---

### F4. ADD: Optimistic repaint in Instrument exit to Kit — `menu.c`

**Location:** `menu_loadInstrumentExit()`, after the teardown block and
before the dirty-mask test.

**Change:** Insert `menu_repaintAll();` after the state teardown and before
the dirty-mask test.

**After:**
```c
    menu_loadSaveClearInstrumentVoiceBlinks();
    menu_refreshLoadSceneLeds();
    menu_repaintAll();                       /* ← optimistic repaint */
    if (menu_residentNameDirtySceneMask != 0u &&
        menu_endResidentNameScratchSession())
        return;
    menu_requestKitEntryNames();
    if (!menu_storageBusy)
        menu_repaintAll();
```

**Effect:** The LCD immediately shows `Load:[Kit]` (or `Save:[Kit]`). The
bottom row shows the current Kit number with whatever name `preset_currentName`
holds — either the last-known Kit name (still valid) or blank spaces
(acceptable). Once the Kit `.hcindex` reload completes, the definitive
repaint shows the correct name.

---

### F5. VERIFY: Encoder scrolling during flush — no change needed

**Existing behavior:** The encoder handler (`menu_handleLoadSaveMenu` /
`menu_parseEncoder`) allows scrolling while `menu_storageBusy` is set.
The Instrument Load renderer uses `MENU_INSTRUMENT_PROVISIONAL_COUNT`
when the real count is zero during busy (line 7722–7723), so the user
can scroll freely. The scroll position is clamped to the real count only
when the `.hcindex` load completes (line 7719–7720).

The Kit renderer uses `preset_currentName` directly, which is populated
from the `.hcindex` row when the index load completes. During the flush
window, scrolling changes `menu_currentPresetNr` but the name field
remains blank/stale until the next repaint after the index load.

**No code change needed.** The provisional scroll behavior is already
correct.

---

### F6. VERIFY: Double repaint on clean path — acceptable

When no dirty mask exists, the flush block is skipped entirely. The
optimistic repaint fires, then `menu_requestInstrumentEntryNames()` or
`menu_requestKitEntryNames()` runs synchronously and may trigger another
`menu_repaintAll()`. Two repaints in quick succession is harmless — the
LCD driver queues the frame and the second one overwrites the first in the
shadow buffer before it is sent. The LCD refresh period is ~16ms; both
repaints complete within one foreground pass (~1ms total).

**No code change needed.**

---

## Summary

| # | Site | Change |
|---|------|--------|
| F1 | Voice button entry | Add `menu_repaintAll()` before flush |
| F2 | Pot-1 Instrument Load | Add `menu_repaintAll()` before flush |
| F3 | Pot-1 Instrument Save | Add `menu_repaintAll()` before flush |
| F4 | Instrument exit to Kit | Add `menu_repaintAll()` before flush |
| F5 | Encoder scrolling | Verify — no change |
| F6 | Clean-path double repaint | Verify — no change |

**Total code changes: 4 single-line insertions. No new state, no new API,
no new RAM.**

## Expected behavior after implementation

```
t=0ms   Voice button pressed
        → state setup
        → menu_repaintAll()  ← LCD shows "Load:[DrumKit]" with blank/stale name
        → menu_endResidentNameScratchSession() starts HCNAMES write
        → return (menu_storageBusy = 1)

        User sees the correct menu header immediately.
        Encoder scrolling works against MENU_INSTRUMENT_PROVISIONAL_COUNT.

t≈300ms HCNAMES flush + HCNAMES read + .hcindex read complete
        → menu_repaintAll()  ← LCD shows "Load:[DrumKit]" with populated name
        
        User sees the full name list appear.
```

## Testing

1. **Dirty-path visual:** Load a Kit (creating a dirty HCNAMES mask), press
   a voice button. The LCD should show `Load:[Type]` immediately. The name
   field populates ~300ms later. No double-press needed.

2. **Clean-path visual:** Enter Instrument Load from a clean state (no Kit
   load). The LCD should show `Load:[Type]` immediately with names populated
   in the same frame or the next.

3. **Exit visual:** From Instrument Load, press the mode button to exit to
   Kit. The LCD should show `Load:[Kit]` immediately. The Kit name populates
   after the `.hcindex` reload.

4. **Scroll during flush:** While the name field is blank (flush in progress),
   turn the encoder. The number should change freely. Names appear once the
   `.hcindex` load completes.

5. **Voice-to-voice switch:** From one Instrument Load, press a different
   voice button. The LCD should show the new type label immediately. The
   new voice's name appears after the flush chain completes.

6. **Reboot persistence:** After any of the above transitions, reboot
   before the flush completes. The HCNAMES change from the *previous*
   context should be persisted (this was already verified by the supplement
   fix; the optimistic repaint does not change persistence behavior).
