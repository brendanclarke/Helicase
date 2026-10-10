# S078 P2 — STEP Mode Multi-Step Editing & Track Automation Overlay

## Overview

Two new editing workflows in STEP mode, both activated by holding
multiple step buttons simultaneously:

**A. Multi-step specials broadcast** — adjusting velocity, note, or
probability while holding multiple steps writes the value to all held
steps.

**B. Held-step track automation** — holding steps → pressing a TRACK
button → adjusting an automatable track parameter writes that parameter
as step automation to every held step.  Underline marks, LED indicators,
and clear-button-plus-knob erase follow the established VOICE overlay
pattern.

Both features reuse the physical held-mask infrastructure already proven
in the VOICE overlay (`buttonHandler_seqHeldMask()`).

---

## §A  Multi-Step Specials Broadcast

### A.1  Behaviour

| Condition | Result |
|-----------|--------|
| Single step held | Current behaviour: select step, show vel/note/prob editor, adjust normally |
| Multiple steps held | Show vel/note/prob of the **last pressed** step; adjusting any special writes the new value to **all** held steps |
| Automation page (scroll right past prob) | Always shows/edits the **last pressed step only** — no broadcast |

"Last pressed" means the newest entry in the held-order array (index 0),
matching the VOICE overlay convention.

### A.2  State

No new static state.  Reuse `buttonHandler_seqHeldMask()` polled at
commit time:

```
uint16_t mask = buttonHandler_seqHeldMask();
uint8_t  count = popcount16(mask);
```

The step edit page already tracks the selected step in
`parameter_values[PAR_ACTIVE_STEP]`.  When count > 1 and the user is on
SEQ_PAGE subpage 1 (vel/note/prob), the commit path loops over the held
mask.

### A.3  Implementation

**File: `Core/Menu/menu.c`**

Modify `menu_cellCommitValue()` in the `PAR_STEP_VOLUME`,
`PAR_STEP_NOTE`, `PAR_STEP_PROB` commit arms (≈ line 14301–14337).
Currently each arm calls e.g. `patSvc_setStepVolume(scene, track, step,
value)` for the single selected step.

Add a helper:

```c
static void menu_broadcastStepSpecial(
    uint8_t scene, uint8_t track, uint8_t value,
    uint8_t (*setter)(uint8_t, uint8_t, uint8_t, uint8_t))
{
    uint16_t mask = buttonHandler_seqHeldMask();
    if (popcount16(mask) < 2u) {
        setter(scene, track, parameter_values[PAR_ACTIVE_STEP], value);
        return;
    }
    for (uint8_t i = 0u; i < 16u; i++) {
        if (mask & (1u << i))
            setter(scene, track, buttonHandler_visibleStep(i), value);
    }
}
```

Replace the three single-step setter calls with
`menu_broadcastStepSpecial(scene, track, value, patSvc_setStep*)`.

**File: `Core/Hardware/frontPanel/buttonHandler.c`**

No change to the step-press handler in SELECT_MODE_STEP.  Pressing a
second step while the first is still held calls
`buttonHandler_selectActiveStep()` again, moving the cursor to the
newest step — which is the desired display seed for multi-step editing.
The original step remains physically held and included in the mask.

**Automation gating**: the per-step automation editor
(`menu_stepAutomationPageActive()`) is entered only after scrolling
right past probability.  No change needed there — it already operates on
the single `PAR_ACTIVE_STEP` and is documented as single-step only.

### A.4  Edge cases

- Holding 16 steps and adjusting velocity writes to all 16.  Each call
  to `patSvc_setStepVolume` is O(1) pool work; 16 iterations is
  inexpensive.
- Steps that have no trigger (not yet toggled on) still get their
  special written.  This matches existing VOICE overlay behaviour:
  automation/specials are independent of trigger state.
- If a step has no dynamic block yet, `patSvc_setStep*` allocates one
  from the pool.  Pool exhaustion silently drops writes (existing
  behaviour).

---

## §B  Held-Step Track Automation Overlay

### B.1  User flow

1. In STEP mode, hold one or more step buttons.
2. Press a TRACK button (VOICE 1–7).
3. The display shows the STEP track settings page for that track
   (SEQ_PAGE subpage 0), but with automation overlay active.
4. Turn a pot or encoder to adjust a track parameter.  If the parameter
   is automatable (length, scale, shuffle — the three
   `SCENE_MOD_TARGET_KIND_TRACK_*` targets), the value is written as
   step automation to every held step.
5. Parameters that already have step automation on any of the held steps
   are **underlined** on the display.
6. Hold the **CLEAR** button and turn a pot to remove that parameter's
   automation from all held steps.
7. Releasing all step buttons exits the overlay and returns to normal
   STEP mode display.

### B.2  New state

```c
/* STEP held-step track automation overlay (mirrors va_* for VOICE). */
static uint16_t sa_heldMask;          /* physically held step buttons    */
static uint8_t  sa_heldOrder[16];     /* newest-first order              */
static uint8_t  sa_heldCount;         /* number of held steps            */
static uint8_t  sa_overlayActive;     /* 1 = overlay is live             */
static uint8_t  sa_underlineSuppressed; /* same nibble scheme as va_      */
static uint8_t  sa_workingValue[4];   /* parameter-domain cache per pot  */
static uint16_t sa_lastEditTick;      /* quiet period for marker restore */
```

~30 bytes static SRAM.

### B.3  Held-mask tracking

Add `sa_updateHeldState()` modelled exactly on `va_updateHeldState()`,
but scoped to `SELECT_MODE_STEP && SEQ_PAGE`.  Called from
`menu_mainService()` or `menu_handleStepModeService()`.

- When the mask goes from nonzero to zero, clear `sa_overlayActive`,
  restore normal LED view, repaint.
- When the mask changes while overlay is active, refresh automation
  LEDs, repaint.

### B.4  TRACK button entry

**File: `Core/Hardware/frontPanel/buttonHandler.c`**

In `handleVoiceButton()`, STEP mode arm (≈ line 1576–1595):

Before the existing track-settings page switch, check
`buttonHandler_seqHeldMask() != 0`.  If steps are held:

```c
if (buttonHandler_seqHeldMask() != 0u) {
    menu_enterStepTrackAutomationOverlay(voiceNr);
    return;
}
```

`menu_enterStepTrackAutomationOverlay(voiceNr)` (new, in menu.c):

- Sets `sa_overlayActive = 1`.
- Snapshots the current held mask into `sa_heldMask` / `sa_heldOrder` /
  `sa_heldCount`.
- Sets `menu_activeVoice = voiceNr`.
- Switches to SEQ_PAGE subpage 0 (the track settings page).
- Calls `sa_refreshAutomationLeds()` and `menu_repaint()`.

### B.5  Automation write path

Add `sa_writeAutomationFromKnob(knobNr, delta)`, modelled on
`va_writeAutomationFromKnob()`.

**Resolution**: map the pot/encoder column to a track automation target.
The SEQ_PAGE subpage 0 cells for track settings include length, scale,
shuffle, MIDI channel, note, play mode.  Only the first three are
automatable (they have `SCENE_MOD_TARGET_KIND_TRACK_*` entries in
`SceneModTargets.c`).  The helper checks whether the resolved cell maps
to one of these targets; if not, it falls through to the normal
parameter commit (non-automatable parameters edit the retained value
normally, same as without overlay).

For automatable cells:

```c
uint16_t target = sceneModTarget_trackParam(kind, menu_activeVoice);
uint8_t  stored7 = clamp_to_7bit(value);

for (i = 0; i < sa_heldCount; i++) {
    patSvc_writeStepAutomation(
        menu_shownPattern, menu_activeVoice,
        buttonHandler_visibleStep(sa_heldOrder[i]),
        target, stored7);
}
```

Hook into `menu_parseKnobDelta()` (≈ line 12240) and
`menu_encoderChangeParameter()` (≈ line 10905), ahead of the existing
`va_overlayActive` checks — the STEP overlay takes priority when active.

### B.6  Automation read / display

**Value display**: when `sa_overlayActive`, the display value for an
automatable cell seeds from the automation entries of the last-pressed
held step (newest in `sa_heldOrder`).  Add `sa_resolveHeldValue(target)`
modelled on `va_resolveHeldValue()`.  If no automation exists for that
target on the held step, show the retained parameter value (normal
display) — but do **not** underline.

**Underline**: add `sa_applyTrackMarkers()` modelled on
`va_applyVoiceMarkers()`.  For each of the four visible cells, check
whether any held step contains automation for the resolved target.  If
yes, underline the parameter name using `lcd_underlineGlyph()`.  The
quiet-period suppression during rapid edits follows the same
`sa_underlineSuppressed` / `sa_lastEditTick` scheme as the VOICE
overlay.

### B.7  LED feedback

When `sa_overlayActive`, the step LED row shows which of the 16 visible
steps have automation for the **currently selected track parameter** (the
clicked-in or last-adjusted cell).  Steps with automation are lit solid;
steps without are off.  Held steps blink.

Add `sa_refreshAutomationLeds()` modelled on
`va_refreshAutomationLeds()`.

### B.8  Clear button + knob

The physical clear button is `BUT_COPY` pressed with SHIFT.  When the
user enters clear mode (SHIFT+COPY), the copy/clear system takes
ownership of pots via `copyClear_ownsPots()`.  Its default action
(`ccClear_potTurned`) clears automation from the **entire track** (all
128 steps) — wrong for this overlay, which should clear only the held
steps.

**Solution**: intercept in `menu_parseKnobDelta()` **before** the
`copyClear_ownsPots()` check.  When `sa_overlayActive` and the
copy/clear system is in clear mode (`copyClear_isClearMode()`), the
overlay handles the pot turn with per-step removal and returns early,
preventing `copyClear_potTurned` from running.

```c
/* In menu_parseKnobDelta(), new block before copyClear_ownsPots(): */
if (sa_overlayActive && copyClear_isClearMode()) {
    uint16_t target = menu_seqCellToTrackTarget(knobNr);
    if (target != INSTRUMENT_PARAM_INVALID) {
        for (i = 0; i < sa_heldCount; i++)
            patSvc_removeStepAutomation(
                menu_shownPattern, menu_activeVoice,
                buttonHandler_visibleStep(sa_heldOrder[i]),
                target);
        sa_refreshAutomationLeds();
        menu_automationTargetCleared(target);
        menu_repaint();
    }
    return;
}
```

**`copyClear_isClearMode()`** — new one-line query in
`copyClearSession.c/h` returning `(cc_state.phase == CC_OP_CLEAR)`.
No existing public API exposes the clear/copy distinction; the overlay
needs it to intercept only clears, not copies.

The encoder path (`menu_handleEncoder`) needs the same guard for the
clicked-in single-parameter clear case.

### B.9  Overlay exit

The overlay exits when:

- All step buttons are released (mask → 0 in `sa_updateHeldState()`).
- The user changes mode (mode button press → `sa_resetOverlay()` via
  `menu_switchPage()`).
- The user presses a different TRACK button while no steps are held
  (normal track switch).

`sa_resetOverlay()` clears all `sa_*` state, restores normal pattern
LEDs, and repaints.

### B.10  Target mapping helper

Add `menu_seqCellToTrackTarget(cell)` that maps a SEQ_PAGE cell to its
Scene mod target ID, or returns `INSTRUMENT_PARAM_INVALID` for
non-automatable cells:

| SEQ_PAGE cell | SceneModTargets kind | Automatable |
|---------------|---------------------|-------------|
| Track Length | SCENE_MOD_TARGET_KIND_TRACK_LENGTH | Yes |
| Track Scale | SCENE_MOD_TARGET_KIND_TRACK_SCALE | Yes |
| Track Shuffle | SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE | Yes |
| MIDI Channel | — | No |
| Note | — | No |
| Play Mode | — | No |

The SceneModTargets table already has target IDs 21–41 encoding
(kind × 7 + track), so the helper computes:

```c
uint16_t base = scene_mod_target_base_for_kind(kind);
return (uint16_t)(base + menu_activeVoice);
```

---

## §C  Implementation Steps

### Step 1 — Multi-step specials broadcast

1. Add `menu_broadcastStepSpecial()` helper in `menu.c`.
2. Replace the three single-step setter calls in `menu_cellCommitValue()`
   with broadcast calls.
3. Test: hold two steps in STEP mode, adjust velocity — both steps
   should update.

### Step 2 — STEP overlay state and held-mask tracking

1. Add `sa_*` static state block in `menu.c`.
2. Add `sa_updateHeldState()`, `sa_resetOverlay()`.
3. Wire `sa_updateHeldState()` into the STEP mode service path.
4. Wire `sa_resetOverlay()` into `menu_switchPage()`.

### Step 3 — TRACK button overlay entry

1. Add `menu_enterStepTrackAutomationOverlay()` in `menu.c` (declared
   in `menu.h`).
2. Modify `handleVoiceButton()` STEP arm to detect held steps and call
   the overlay entry.

### Step 4 — Write path (pot/encoder → automation)

1. Add `menu_seqCellToTrackTarget()` target resolver.
2. Add `sa_writeAutomationFromKnob()`.
3. Hook into `menu_parseKnobDelta()` and
   `menu_encoderChangeParameter()` ahead of existing overlay checks.
4. Non-automatable cells fall through to normal retained-value edit.

### Step 5 — Display: value seeding and underline

1. Add `sa_resolveHeldValue()`.
2. Add `sa_applyTrackMarkers()` and wire into the display pipeline
   alongside `va_applyVoiceMarkers()`.
3. Add `sa_underlineService()` for quiet-period restore.

### Step 6 — LED feedback

1. Add `sa_refreshAutomationLeds()`.
2. Call from overlay entry, mask change, and after writes/clears.

### Step 7 — Clear button + knob erase

1. Add `copyClear_isClearMode()` query in `copyClearSession.c/h`.
2. Add clear-branch intercept in `menu_parseKnobDelta()` and
   `menu_handleEncoder()`, before `copyClear_ownsPots/Encoder`.
3. Call `patSvc_removeStepAutomation()` per held step,
   `menu_automationTargetCleared()` for immediate underline drop.

### Step 8 — Integration testing — PASS (user, 2026-10-10)

1. Hold 1 step → TRACK → adjust length → verify automation written. **PASS**
2. Hold 3 steps → TRACK → adjust scale → verify all 3 get automation. **PASS**
3. Verify underline appears on the adjusted parameter. **PASS**
4. Clear button + knob → verify automation removed, underline gone. **PASS**
5. Release all steps → verify overlay exits, normal display returns. **PASS**
6. Verify multi-step specials (vel/note/prob) broadcast works. **PASS**
7. Verify single-step behaviour unchanged (vel/note/prob and automation
   editor). **PASS**
8. Play transport: verify step automation fires at the correct steps. **PASS**

---

## Files changed

| File | Change |
|------|--------|
| `Core/Menu/menu.c` | `sa_*` state, overlay entry/exit, held-mask service, write path, display/underline, clear intercept, multi-step specials broadcast |
| `Core/Menu/menu.h` | Declare `menu_enterStepTrackAutomationOverlay()` |
| `Core/Hardware/frontPanel/buttonHandler.c` | TRACK button held-step detection in `handleVoiceButton()` |
| `Core/Menu/CopyClear/copyClearSession.c` | Add `copyClear_isClearMode()` |
| `Core/Menu/CopyClear/copyClearSession.h` | Declare `copyClear_isClearMode()` |

No changes to `PatternData`, `PatternStackService`, `presetMorphEngine`,
`SceneModTargets`, `sequencer`, `buttonHandler.h`, or any persistence
path — all automation storage and playback uses the existing
infrastructure.

---

## RAM cost

~30 bytes static SRAM for `sa_*` state (mirrors the ~46 B VOICE overlay
allocation).

## Key findings

- There is no `BUT_CLEAR` — clear uses `BUT_COPY` with SHIFT.  The
  copy/clear session (`copyClearSession.c`) tracks the mode in
  `cc_state.phase` (`CC_OP_CLEAR` vs `CC_OP_COPY`).  No public API
  currently exposes the clear/copy distinction, so we add
  `copyClear_isClearMode()`.
- The existing `copyClear_ownsPots()` returns true for both copy AND
  clear, and its `ccClear_potTurned()` path removes automation from all
  128 steps of the track.  The overlay must intercept before that to do
  per-held-step removal only.
- `buttonHandler_seqHeldMask()` reads physical `btn_held[]` state and
  works in any mode — no new buttonHandler API needed.
- Non-automatable cells (MIDI ch, note, play mode) fall through to
  normal retained-value editing when the overlay is active.

## Follow-ups

None blocking.  Both features are self-contained within the existing
step automation and held-mask infrastructure.
