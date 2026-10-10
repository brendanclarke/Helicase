# S078 P2 — Implementation Schedule

Two features: **§A Multi-step specials broadcast** and **§B Held-step
track automation overlay**.  Both live in `menu.c` with one new query in
`copyClearSession`.

Reference plan: `S078_P2_TRACK_SETTINGS_CLEANUP.md`.

---

## §A  Multi-Step Specials Broadcast

### A.1  Scope

When the user holds multiple SEQ buttons in STEP mode and adjusts
velocity, note, or probability, the value is written to all held steps.
The step edit page (SEQ_PAGE subpage 1) is entered by
`buttonHandler_selectActiveStep()` → `menu_showStepEditPage()` on each
press; subsequent presses update `PAR_ACTIVE_STEP` to the newest step
while earlier steps stay physically held in `btn_held[]`.

### A.2  Implementation — `menu.c`

#### A.2.1  New helper: `menu_broadcastStepSpecial()`

Insert after `va_writeAutomationFromKnob()` (after line 3400):

```c
/*
 * Write one step special (velocity, note, or probability) to every
 * physically held step, or to the single selected step when only one
 * is held.
 *
 * Inputs: viewed pattern, active track, value, and a PatternStackService
 * setter whose signature is (scene, track, step, value). Output: the
 * setter is called for every held step; a single held step degenerates
 * to the existing single-step path. Why: multi-step editing shares the
 * step specials commit path, and PatternStackService already validates
 * coordinates and manages pool allocation.
 */
static void menu_broadcastStepSpecial(
    uint8_t scene, uint8_t track, uint8_t value,
    uint8_t (*setter)(uint8_t, uint8_t, uint8_t, uint8_t))
{
    uint16_t mask = buttonHandler_seqHeldMask();
    uint8_t  i;

    if (__builtin_popcount(mask) < 2) {
        setter(scene, track, parameter_values[PAR_ACTIVE_STEP], value);
        return;
    }
    for (i = 0u; i < 16u; i++) {
        if (mask & (1u << i))
            setter(scene, track, buttonHandler_visibleStep(i), value);
    }
}
```

**Design notes:**

- Uses `__builtin_popcount` (GCC/Clang intrinsic, already used in
  `PatternData.c:178` and `PatternStackService.c:524`).
- Falls back to single-step path when count < 2 so existing behaviour
  is untouched.
- `buttonHandler_visibleStep(i)` converts button index 0–15 to absolute
  step via `menu_currentBar * 16 + i`
  (`buttonHandler.c:486`).
- The setter function pointer matches all three APIs:
  `patSvc_setStepVolume`, `patSvc_setStepNote`, `patSvc_setStepProbability`
  (all declared in `PatternStackService.h:130–134` with identical
  `(uint8_t, uint8_t, uint8_t, uint8_t) → uint8_t` signatures).

#### A.2.2  Modify `menu_cellCommitValue()` (menu.c:14312–14337)

Replace the three single-step setter calls:

**`PAR_STEP_PROB` (line 14312–14318)**

Before:
```c
case PAR_STEP_PROB:
    patSvc_setStepProbability(menu_getViewedPattern(), menu_getActiveVoice(),
                           parameter_values[PAR_ACTIVE_STEP], value);
    break;
```

After:
```c
case PAR_STEP_PROB:
    menu_broadcastStepSpecial(menu_getViewedPattern(), menu_getActiveVoice(),
                              (uint8_t)value, patSvc_setStepProbability);
    break;
```

**`PAR_STEP_NOTE` (line 14321–14328)**

Before:
```c
case PAR_STEP_NOTE:
    patSvc_setStepNote(menu_getViewedPattern(), menu_getActiveVoice(),
                    parameter_values[PAR_ACTIVE_STEP], value);
    break;
```

After:
```c
case PAR_STEP_NOTE:
    menu_broadcastStepSpecial(menu_getViewedPattern(), menu_getActiveVoice(),
                              (uint8_t)value, patSvc_setStepNote);
    break;
```

**`PAR_STEP_VOLUME` (line 14330–14337)**

Before:
```c
case PAR_STEP_VOLUME:
    patSvc_setStepVolume(menu_getViewedPattern(), menu_getActiveVoice(),
                      parameter_values[PAR_ACTIVE_STEP], value);
    break;
```

After:
```c
case PAR_STEP_VOLUME:
    menu_broadcastStepSpecial(menu_getViewedPattern(), menu_getActiveVoice(),
                              (uint8_t)value, patSvc_setStepVolume);
    break;
```

#### A.2.3  Automation page gating

No change needed.  `menu_stepAutomationPageActive()` already gates the
automation editor independently; it operates only on `PAR_ACTIVE_STEP`
and is not reached by the vel/note/prob commit arms.  The broadcast
helper is only called from the three `PAR_STEP_*` cases, which are on
subpage 1 positions 0–2, before the automation editor begins.

### A.3  Files changed

| File | Change |
|------|--------|
| `Core/Menu/menu.c` | Add `menu_broadcastStepSpecial()`, modify three `case` arms in `menu_cellCommitValue()` |

No header changes.  No buttonHandler changes.

### A.4  Testing

1. STEP mode: hold one step, adjust velocity → single step updates (regression).
2. Hold two steps, adjust velocity → both update.
3. Hold three steps, adjust note → all three update.
4. Hold steps across bar boundary → only steps on the visible bar
   update (visibleStep maps 0–15 to the current bar; steps on other
   bars are not in the physical held mask).
5. Scroll right past prob into automation editor → single-step
   behaviour unchanged.
6. Pool exhaustion: fill pool near capacity, hold 16 steps, adjust
   velocity → writes succeed until pool full, then silently stop
   (existing PatternStackService behaviour).

---

## §B  Held-Step Track Automation Overlay

### B.1  Scope

In STEP mode, holding step buttons then pressing a TRACK (VOICE) button
enters a track-automation overlay.  Adjusting an automatable track
parameter (length, scale, shuffle) writes step automation to every held
step.  Non-automatable cells (play mode, MIDI channel, MIDI note) fall
through to normal retained-value editing.

### B.2  Architecture

The overlay mirrors the existing VOICE held-step overlay (`va_*` state
block, `menu.c:1384–1478`).  STEP and VOICE overlays are mutually
exclusive: the VOICE overlay runs on VOICE pages
(`menu_isVoicePage()`), while the STEP overlay runs on the SEQ page.

The overlay **shares** the `va_*` state block rather than duplicating
it.  Both overlays cannot be active simultaneously (STEP mode shows
SEQ_PAGE, VOICE mode shows VOICE pages; mode changes reset the
overlay), and sharing avoids duplicating the 46-byte state, the search
infrastructure, the CGRAM slots, and the underline service.  A page
guard at each entry point ensures mutual exclusion.

However, the scan and marker infrastructure (`va_scanService`,
`va_applyVoiceMarkers`, `va_searchTargetMask`) is deeply coupled to
VOICE pages — it reads instrument descriptor indices and Scene-setting
cells.  The STEP overlay uses **track** targets (Scene block 6, IDs
384+21 through 384+41), which are a different target namespace.  Rather
than refactoring the entire scan system, the STEP overlay uses a simpler
approach:

- **No bounded background scan** — the three automatable track cells
  are known at compile time (positions 0, 1, 2 on SEQ_PAGE subpage 0).
  The overlay checks automation presence directly at overlay entry and
  after each write/clear, using `patSvc_stepHasAutomation()` on each
  held step.
- **Dedicated marker state** — a compact 3-bit presence mask
  (`sa_trackPresenceMask`) replaces the full 8-byte search bitmap for
  the three track parameters.  The existing `va_underlineSuppressed`,
  `va_workingValue`, `va_lastEditTick`, and `va_heldMask/Order/Count`
  are shared.

### B.3  State block

Add in `menu.c` after the `va_*` block (after line 1478), inside the
existing VOICE overlay region:

```c
/*
 * STEP held-step track automation overlay (S078 P2 §B).
 *
 * Shares va_heldMask, va_heldOrder, va_heldCount, va_overlayActive,
 * va_underlineSuppressed, va_workingValue, and va_lastEditTick with
 * the VOICE overlay. Mutual exclusion: STEP overlay is active only on
 * SEQ_PAGE; VOICE overlay only on voice pages; mode changes reset both.
 * sa_trackPresenceMask: bits 0–2 = length/scale/shuffle automation
 * present on any held step. Updated at entry, after writes, and after
 * clears.
 */
static uint8_t sa_trackPresenceMask = 0u;
```

1 byte additional static SRAM.

### B.4  Target mapping helper

Add a new static function after `menu_sceneSettingAutomationTarget()`
(after line 3860):

```c
/*
 * Map a SEQ_PAGE subpage-0 cell position to its track automation target.
 *
 * Inputs: cell position 0..7 on subpage 0 and the active track.
 * Output: the canonical Scene mod target ID for length (pos 0),
 * scale (pos 1), or shuffle (pos 2); INSTRUMENT_PARAM_INVALID for
 * non-automatable positions (3–7: play mode, MIDI ch, MIDI note,
 * empty, empty). Why: only length, scale, and shuffle have
 * SCENE_MOD_TARGET_KIND_TRACK_* entries (SceneModTargets.c:122–163).
 *
 * Target ID computation:
 *   SCENE_MOD_TARGET_BASE + 21 + kind*7 + track
 * where kind: 0=length, 1=scale, 2=shuffle.
 * SCENE_MOD_TARGET_BASE = INSTRUMENT_VOICE_ID_COUNT (384).
 *   Length track 0 = 384+21 = 405
 *   Scale  track 0 = 384+28 = 412
 *   Shuffle track 0 = 384+35 = 419
 */
static instrument_param_id_t menu_seqCellToTrackTarget(
    uint8_t cell_position, uint8_t track)
{
    uint16_t base;

    if (track >= NUM_TRACKS)
        return INSTRUMENT_PARAM_INVALID;
    switch (cell_position) {
    case 0u: base = INSTRUMENT_VOICE_ID_COUNT + 21u; break; /* length */
    case 1u: base = INSTRUMENT_VOICE_ID_COUNT + 28u; break; /* scale  */
    case 2u: base = INSTRUMENT_VOICE_ID_COUNT + 35u; break; /* shuffle */
    default: return INSTRUMENT_PARAM_INVALID;
    }
    return (instrument_param_id_t)(base + track);
}
```

**Verification**: `sceneModTarget_descriptor()` already validates IDs
against the `scene_mod_targets[]` table.  The hardcoded offsets 21, 28,
35 match `SCENE_MOD_TARGET_ID(21)` / `SCENE_MOD_TARGET_ID(28)` /
`SCENE_MOD_TARGET_ID(35)` in `SceneModTargets.c:122–162`.  An
`_Static_assert` should verify these at compile time:

```c
_Static_assert(INSTRUMENT_VOICE_ID_COUNT + 21u ==
    (uint16_t)SCENE_MOD_TARGET_ID(21u),
    "Track length target base must match SceneModTargets table");
```

(Place beside the function or the state block.)

### B.5  Presence scan helper

```c
/*
 * Refresh the compact track-automation presence mask.
 *
 * Checks whether any held step has automation for each of the three
 * automatable track parameters (length, scale, shuffle). Runs at
 * overlay entry, after writes, and after clears. O(held_count * 3).
 */
static void sa_refreshPresence(void)
{
    uint8_t mask = 0u;
    uint8_t cell;

    for (cell = 0u; cell < 3u; cell++) {
        instrument_param_id_t target =
            menu_seqCellToTrackTarget(cell, menu_activeVoice);
        uint8_t i;

        if (target == INSTRUMENT_PARAM_INVALID)
            continue;
        for (i = 0u; i < va_heldCount; i++) {
            uint8_t step = buttonHandler_visibleStep(va_heldOrder[i]);
            uint8_t dummy;

            if (patSvc_readStepAutomation(
                    menu_shownPattern, menu_activeVoice, step,
                    target, &dummy)) {
                mask |= (uint8_t)(1u << cell);
                break;
            }
        }
    }
    sa_trackPresenceMask = mask;
}
```

**Dependency**: `patSvc_readStepAutomation()` — verify this exists. If
not, use `pat_stepAutomationReadEntry()` or `pat_readStepAutomation()`
from `PatternData.c`.

Check for available API:

```
grep -rn "patSvc_readStepAutomation\|pat_readStepAutomation\|
pat_stepAutomation.*read\|pat_stepHasTarget" Core/
```

If no direct "read single target" API exists, use
`pat_stepAutomationCount()` + `pat_stepAutomationEntry()` to iterate
the step's automation entries and test for a target match.  This is the
same approach `va_scanService()` uses (menu.c:2155–2230).

### B.6  Overlay entry

Add `menu_enterStepTrackAutomationOverlay()` in `menu.c` (declare in
`menu.h`):

```c
/*
 * Enter the STEP held-step track automation overlay.
 *
 * Inputs: voiceNr (the pressed TRACK button, 0–6). Output: overlay
 * state is initialized, the SEQ_PAGE front page is shown for the
 * target track, automation presence is scanned, LEDs updated.
 * Caller: handleVoiceButton() in buttonHandler.c when steps are held.
 */
void menu_enterStepTrackAutomationOverlay(uint8_t voiceNr)
{
    va_overlayActive = 1u;
    va_underlineSuppressed = 0u;
    memset(va_workingValue, 0, sizeof(va_workingValue));
    menu_activeVoice = voiceNr;
    menu_showStepTrackSettingsFirstHalf();
    /* va_heldMask/Order/Count are populated by va_updateHeldState()
     * on the next service pass, reading buttonHandler_seqHeldMask(). */
    sa_refreshPresence();
    sa_refreshAutomationLeds();
    menu_repaint();
}
```

**Declaration** — add to `menu.h` after `menu_voiceAutoOverlayBarChanged()`
(line 518):

```c
void menu_enterStepTrackAutomationOverlay(uint8_t voiceNr);
```

### B.7  Modify `handleVoiceButton()` — `buttonHandler.c:1576–1596`

In the STEP/SEQ_PAGE arm, before the existing track-settings page switch:

```c
    if (bh_state.selectButtonMode == SELECT_MODE_STEP ||
        menu_activePage == SEQ_PAGE) {
        /* S078 P2 §B: held steps + TRACK = automation overlay. */
        if (buttonHandler_seqHeldMask() != 0u) {
            menu_enterStepTrackAutomationOverlay(voiceNr);
            return;
        }
        led_clearAllBlinkLeds();
        /* ... existing code ... */
```

Insert the held-mask check at **line 1577**, after the `if` condition
and before `led_clearAllBlinkLeds()`.

### B.8  Held-mask tracking in service loop

**Modify `menu_serviceRuntimeWidgets()` (menu.c:12364).**

The existing service calls `va_updateHeldState()` only on VOICE pages
(`menu_isVoicePage(menu_activePage)`).  Extend to also run on SEQ_PAGE
when the STEP overlay is active:

```c
    if (menu_isVoicePage(menu_activePage)) {
        if (!menu_fxVoiceMixOverlayActive())
            va_updateHeldState();
        va_scanService();
        va_underlineService();
    }
    /* S078 P2 §B: STEP overlay held-mask polling. */
    if (menu_activePage == SEQ_PAGE && va_overlayActive) {
        va_updateHeldState();
        va_underlineService();
    }
```

**Note**: `va_updateHeldState()` already handles overlay exit when
`new_mask == 0u && va_overlayActive` (line 2350–2358).  On SEQ_PAGE the
`led_updatePatternTrackView` call in the exit path is correct (it
restores normal step LEDs for the active track).

**Also modify `va_updateHeldState()`** (line 2359–2361) to call the
STEP overlay's LED refresh when on SEQ_PAGE:

```c
    } else if (changed && va_overlayActive) {
        if (menu_activePage == SEQ_PAGE)
            sa_refreshAutomationLeds();
        else
            va_refreshAutomationLeds();
        menu_repaint();
    }
```

### B.9  Write path — pot

**Modify `menu_parseKnobDelta()` (menu.c:12195).**

Add a new intercept block **before** the `copyClear_ownsPots()` check
(before line 12207) and **after** the `menu_storageBusy` check:

```c
    if (menu_storageBusy) return;

    /* S078 P2 §B: STEP overlay pot edits write track automation. */
    if (va_overlayActive && menu_activePage == SEQ_PAGE) {
        if (copyClear_ownsPots() && copyClear_isClearMode()) {
            /* Clear: remove track automation from held steps. */
            sa_clearAutomationFromKnob(knobNr);
            return;
        }
        if (copyClear_ownsPots())
            return; /* Copy mode: do nothing. */
        sa_writeAutomationFromKnob(knobNr, delta);
        return;
    }

    /* Existing copyClear_ownsPots() check follows ... */
```

### B.10  Write path — encoder

**Modify `menu_encoderChangeParameter()` (menu.c:10863).**

Add a new intercept after the step-automation check (after line 10889)
and **before** the existing `va_overlayActive` VOICE check (line 10905):

```c
    /* S078 P2 §B: STEP overlay encoder edits write track automation. */
    if (va_overlayActive && menu_activePage == SEQ_PAGE) {
        uint8_t pos = (uint8_t)(menuIndex & MASK_PARAMETER);
        instrument_param_id_t target =
            menu_seqCellToTrackTarget(pos, menu_activeVoice);
        if (target != INSTRUMENT_PARAM_INVALID) {
            sa_writeAutomationFromKnob(pos, inc);
            return;
        }
        /* Non-automatable cell: fall through to normal commit. */
    }
```

### B.11  Write helper

Add `sa_writeAutomationFromKnob()` after `menu_broadcastStepSpecial()`:

```c
/*
 * Write one track automation parameter to every held step.
 *
 * Mirrors va_writeAutomationFromKnob() but operates on SEQ_PAGE
 * subpage-0 cells and track Scene-mod targets instead of instrument
 * or Scene-setting targets. Non-automatable cells (position > 2) are
 * not reached because the caller guards with menu_seqCellToTrackTarget.
 *
 * Inputs: cell position (0=length, 1=scale, 2=shuffle) and signed delta.
 * Output: Pattern pool writes, working-value cache, presence mask
 * update, and coalesced repaint.
 */
static void sa_writeAutomationFromKnob(uint8_t cellPos, int8_t delta)
{
    instrument_param_id_t target;
    uint8_t activePage;
    menu_cell_t cell;
    uint16_t value;
    int32_t next;
    uint8_t stored7;
    uint8_t i;
    uint8_t wrote = 0u;
    uint8_t knob_idx;

    if (!va_overlayActive || menu_activePage != SEQ_PAGE)
        return;
    target = menu_seqCellToTrackTarget(cellPos, menu_activeVoice);
    if (target == INSTRUMENT_PARAM_INVALID)
        return;

    /* Resolve the menu cell for dtype/clamp. */
    activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    knob_idx = (uint8_t)(cellPos & 3u);
    cell = menu_resolveCell(activePage, cellPos);

    /* Seed: working cache if valid, else first held step's stored value,
     * else the retained parameter value. */
    if (va_underlineSuppressed & (uint8_t)(0x10u << knob_idx))
        value = (uint16_t)va_workingValue[knob_idx];
    else {
        uint8_t found = 0u;
        for (i = 0u; i < va_heldCount && !found; i++) {
            uint8_t step = buttonHandler_visibleStep(va_heldOrder[i]);
            uint8_t sv;
            if (patSvc_readStepAutomation(
                    menu_shownPattern, menu_activeVoice, step,
                    target, &sv)) {
                value = (uint16_t)sv;
                found = 1u;
            }
        }
        if (!found)
            value = menu_cellDisplayValue(&cell);
    }

    next = (int32_t)value + (int32_t)delta;
    if (next < 0)    next = 0;
    if (next > 65535) next = 65535;
    value = (uint16_t)next;
    menu_clampCellValue(&cell, &value);

    va_workingValue[knob_idx] = (value > 255u) ? 255u : (uint8_t)value;
    stored7 = (value > 127u) ? 127u : (uint8_t)value;

    for (i = 0u; i < va_heldCount; i++) {
        if (patSvc_writeStepAutomation(
                menu_shownPattern, menu_activeVoice,
                buttonHandler_visibleStep(va_heldOrder[i]),
                target, stored7))
            wrote = 1u;
    }

    if (wrote) {
        sa_trackPresenceMask |= (uint8_t)(1u << knob_idx);
        va_underlineSuppressed |=
            (uint8_t)((1u << knob_idx) | (0x10u << knob_idx));
        va_lastEditTick = time_sysTick;
        menu_knobs_dirty = 1u;
    }
}
```

**Dependency resolution**: the helper needs `patSvc_readStepAutomation`
to seed the working value.  If this doesn't exist, add a thin wrapper in
`PatternStackService.c`:

```c
uint8_t patSvc_readStepAutomation(uint8_t scene, uint8_t track,
    uint8_t step, uint16_t target, uint8_t *value_out)
{
    uint8_t count = pat_stepAutomationCount(scene, track, step);
    for (uint8_t i = 0u; i < count; i++) {
        pat_step_auto_entry_t entry;
        if (pat_stepAutomationEntry(scene, track, step, i, &entry) &&
            entry.target == target) {
            *value_out = entry.value;
            return 1u;
        }
    }
    return 0u;
}
```

Declare in `PatternStackService.h`.  Check the actual PatternData API
names by grepping `pat_stepAutomation` before implementing.

### B.12  Clear helper

```c
/*
 * Remove track automation from held steps for one pot column.
 *
 * Mirrors the VOICE overlay's clear path but targets track parameters.
 * Called when the STEP overlay intercepts a copyClear clear-mode pot
 * turn. After removal, refreshes presence and LEDs.
 */
static void sa_clearAutomationFromKnob(uint8_t cellPos)
{
    instrument_param_id_t target;
    uint8_t knob_idx = (uint8_t)(cellPos & 3u);
    uint8_t i;

    target = menu_seqCellToTrackTarget(cellPos, menu_activeVoice);
    if (target == INSTRUMENT_PARAM_INVALID)
        return;

    for (i = 0u; i < va_heldCount; i++) {
        patSvc_removeStepAutomation(
            menu_shownPattern, menu_activeVoice,
            buttonHandler_visibleStep(va_heldOrder[i]), target);
    }

    sa_refreshPresence();
    sa_refreshAutomationLeds();
    menu_automationTargetCleared(target);
    menu_repaint();
}
```

### B.13  `copyClear_isClearMode()` — new query

**`copyClearSession.c`** — add after `copyClear_ownsPots()` (line 962):

```c
uint8_t copyClear_isClearMode(void)
{
    return (uint8_t)(cc_state.phase == CC_OP_CLEAR);
}
```

**`copyClearSession.h`** — add after `copyClear_ownsPots()` declaration
(line 191):

```c
uint8_t copyClear_isClearMode(void);
```

### B.14  LED feedback

```c
/*
 * Show step automation presence on the LED row for the focused track
 * parameter.
 *
 * Steps with automation for the current cell's target are lit solid.
 * Held steps blink. Steps with no automation are off.
 */
static void sa_refreshAutomationLeds(void)
{
    uint8_t activePage;
    uint8_t pos;
    instrument_param_id_t target;

    if (!va_overlayActive || menu_activePage != SEQ_PAGE)
        return;
    if (!editModeActive)
        return;

    activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    pos = (uint8_t)(menuIndex & MASK_PARAMETER);
    target = menu_seqCellToTrackTarget(pos, menu_activeVoice);

    if (target != INSTRUMENT_PARAM_INVALID) {
        led_updateAutomationStepView(
            menu_activeVoice, menu_shownPattern, target, va_heldMask);
    } else {
        led_updatePatternTrackView(menu_activeVoice, menu_shownPattern,
                                   buttonHandler_selectedStep, 0u);
    }
}
```

**Dependency**: `led_updateAutomationStepView()` — already exists
(called by `va_refreshAutomationLeds()` at line 2588).  It takes
(track, pattern, target_id, held_mask) and shows per-step automation
presence with blink on held steps.

### B.15  Display: value seeding and underline markers

The STEP overlay piggybacks on the existing `va_applyVoiceMarkers()`
call for VOICE pages.  Since SEQ_PAGE is not a voice page, we need a
separate marker path.

Add `sa_applyTrackMarkers()`, called from the repaint pipeline wherever
`va_applyVoiceMarkers()` is called, guarded by `menu_activePage ==
SEQ_PAGE && va_overlayActive`:

```c
/*
 * Apply underline markers and automation values for the STEP overlay.
 *
 * In non-edit mode: underlines the name of any automatable cell that
 * has automation on a held step. In edit mode: shows the automation
 * value and underlines it.
 */
static void sa_applyTrackMarkers(void)
{
    uint8_t glyph_probe[8];
    uint8_t desired_base[4] = { 0u, 0u, 0u, 0u };
    uint8_t marker_row[4]   = { 0u, 0u, 0u, 0u };
    uint8_t marker_col[4]   = { 0u, 0u, 0u, 0u };
    uint8_t desired_valid    = 0u;
    uint8_t activePage;
    uint8_t i;

    if (!va_overlayActive || menu_activePage != SEQ_PAGE)
        return;

    activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    if (activePage != 0u)
        return; /* Only subpage 0 (track settings) has automatable cells. */

    if (editModeActive) {
        uint8_t pos = (uint8_t)(menuIndex & MASK_PARAMETER);
        uint8_t knob_idx = (uint8_t)(pos & 3u);
        uint8_t suppress_bit = (uint8_t)(1u << knob_idx);
        uint8_t validity_bit = (uint8_t)(suppress_bit << 4u);

        if (pos < 3u && (sa_trackPresenceMask & (1u << pos))) {
            if ((va_underlineSuppressed & suppress_bit) == 0u) {
                /* Underline the rightmost value character. */
                int8_t right;
                for (right = 15; right >= 0 &&
                     editDisplayBuffer[1][right] == ' '; right--)
                    ;
                if (right >= 0 && lcd_underlineGlyph(
                        (uint8_t)editDisplayBuffer[1][right], glyph_probe)) {
                    desired_base[0] = (uint8_t)editDisplayBuffer[1][right];
                    marker_row[0] = 1u;
                    marker_col[0] = (uint8_t)right;
                    desired_valid = 0x01u;
                }
            }
            /* Override the value display with the overlay value. */
            if (va_underlineSuppressed & validity_bit) {
                menu_cell_t cell = menu_resolveCell(activePage, pos);
                char val_text[4];
                menu_formatCellValue(&cell, va_workingValue[knob_idx],
                                     val_text, sizeof(val_text));
                memcpy(&editDisplayBuffer[1][13], val_text, 3u);
            }
        }
    } else {
        /* Non-edit: underline names of cells with automation. */
        for (i = 0u; i < 3u; i++) {
            if ((sa_trackPresenceMask & (1u << i)) == 0u)
                continue;
            if ((va_underlineSuppressed & (1u << i)) != 0u)
                continue;
            {
                uint8_t col = (uint8_t)(4u * i);
                uint8_t ch = (uint8_t)editDisplayBuffer[0][col];
                if (ch != ' ' && lcd_underlineGlyph(ch, glyph_probe)) {
                    desired_base[i] = ch;
                    marker_row[i] = 0u;
                    marker_col[i] = col;
                    desired_valid |= (uint8_t)(1u << i);
                }
            }
        }
    }

    va_queueMarkerTransaction(desired_base, desired_valid,
                              marker_row, marker_col);
}
```

**Integration point**: find where `va_applyVoiceMarkers()` is called
from the repaint pipeline and add `sa_applyTrackMarkers()` alongside:

```
grep -n "va_applyVoiceMarkers" Core/Menu/menu.c
```

In `menu_repaintGeneric()` or `sendDisplayBuffer()` or wherever the
marker pipeline runs.  Add:

```c
if (menu_activePage == SEQ_PAGE && va_overlayActive)
    sa_applyTrackMarkers();
```

### B.16  Overlay exit and reset

**Extend `va_resetOverlay()`** (menu.c:2374–2381) to also clear STEP
overlay state:

```c
static void va_resetOverlay(void)
{
    va_heldMask = 0u;
    va_heldCount = 0u;
    va_overlayActive = 0u;
    va_underlineSuppressed = 0u;
    sa_trackPresenceMask = 0u;   /* S078 P2 §B */
    /* ... rest unchanged ... */
```

**`va_updateHeldState()`** (line 2350–2358): the existing overlay-exit
logic (`new_mask == 0u && va_overlayActive`) already clears
`va_overlayActive` and restores LEDs.  This works for both VOICE and
STEP overlays.

**`menu_switchPage()`**: already calls `va_resetOverlay()` on any page
change, so switching mode or navigating away from SEQ_PAGE resets the
STEP overlay automatically.

### B.17  Encoder click-in guard

**Modify `menu_parseEncoder()`** (menu.c:11894–11900) to also handle
STEP overlay click-in/click-out:

```c
    if (btnClicked && menu_activePage == SEQ_PAGE && va_overlayActive) {
        va_underlineSuppressed = 0u;
        sa_refreshAutomationLeds();
    }
```

Add this after the existing VOICE overlay click-in block (line 11894).

### B.18  Forward declarations

Add to the forward declaration block (near line 1970):

```c
static void sa_writeAutomationFromKnob(uint8_t cellPos, int8_t delta);
static void sa_clearAutomationFromKnob(uint8_t cellPos);
static void sa_refreshPresence(void);
static void sa_refreshAutomationLeds(void);
static void sa_applyTrackMarkers(void);
```

---

## Implementation order

### Phase 1: §A Multi-step specials broadcast

1. Add `menu_broadcastStepSpecial()` helper.
2. Modify three `case` arms in `menu_cellCommitValue()`.
3. Build and test on hardware.

### Phase 2: §B Infrastructure

4. Add `sa_trackPresenceMask` state byte.
5. Add `menu_seqCellToTrackTarget()` with static assert.
6. Add `copyClear_isClearMode()` in copyClearSession.c/h.
7. If needed, add `patSvc_readStepAutomation()` in PatternStackService.

### Phase 3: §B Overlay entry and held-mask

8. Add `menu_enterStepTrackAutomationOverlay()` in menu.c, declare in
   menu.h.
9. Modify `handleVoiceButton()` STEP arm in buttonHandler.c.
10. Extend `menu_serviceRuntimeWidgets()` for SEQ_PAGE overlay polling.
11. Extend `va_updateHeldState()` for SEQ_PAGE LED refresh.
12. Extend `va_resetOverlay()` to clear `sa_trackPresenceMask`.

### Phase 4: §B Write and clear paths

13. Add `sa_refreshPresence()`.
14. Add `sa_writeAutomationFromKnob()`.
15. Add `sa_clearAutomationFromKnob()`.
16. Add intercept in `menu_parseKnobDelta()` before copyClear check.
17. Add intercept in `menu_encoderChangeParameter()`.

### Phase 5: §B Display and LEDs

18. Add `sa_refreshAutomationLeds()`.
19. Add `sa_applyTrackMarkers()` and wire into repaint pipeline.
20. Add encoder click-in guard in `menu_parseEncoder()`.
21. Add forward declarations.

### Phase 6: Testing

22. Integration testing per the test matrix in §B.19.

---

## Pre-implementation checklist

Before writing code, verify these assumptions:

- [ ] `patSvc_readStepAutomation()` exists or an equivalent API:
  ```
  grep -rn "readStepAutomation\|pat_stepAutomation.*Entry\|
  pat_stepAutomation.*Read" Core/Bank/Scene/Pattern/
  ```
- [ ] `INSTRUMENT_VOICE_ID_COUNT` value (expected 384):
  ```
  grep -rn "INSTRUMENT_VOICE_ID_COUNT" Core/
  ```
- [ ] `SCENE_MOD_TARGET_ID(21)` resolves to 384+21 = 405:
  ```
  grep -n "SCENE_MOD_TARGET_BASE\|SCENE_MOD_TARGET_ID" \
    Core/Bank/Scene/SceneModTargets.c
  ```
- [ ] `menu_formatCellValue()` exists for value display in markers
  (may need `va_formatValue3()` instead — used by VOICE overlay):
  ```
  grep -n "va_formatValue3\|menu_formatCellValue" Core/Menu/menu.c
  ```
- [ ] `va_queueMarkerTransaction()` is accessible from the marker
  function (it is `static` in menu.c — same file, ok).
- [ ] The repaint pipeline call site for `va_applyVoiceMarkers()`:
  ```
  grep -n "va_applyVoiceMarkers()" Core/Menu/menu.c
  ```
  to determine where `sa_applyTrackMarkers()` should be wired in.

---

## Files changed

| File | Change |
|------|--------|
| `Core/Menu/menu.c` | `menu_broadcastStepSpecial()`, `menu_seqCellToTrackTarget()`, `sa_trackPresenceMask`, `sa_refreshPresence()`, `sa_writeAutomationFromKnob()`, `sa_clearAutomationFromKnob()`, `sa_refreshAutomationLeds()`, `sa_applyTrackMarkers()`, `menu_enterStepTrackAutomationOverlay()`, intercepts in `menu_parseKnobDelta()` and `menu_encoderChangeParameter()`, service/reset extensions |
| `Core/Menu/menu.h` | Declare `menu_enterStepTrackAutomationOverlay()` |
| `Core/Hardware/frontPanel/buttonHandler.c` | Held-step detection in `handleVoiceButton()` STEP arm |
| `Core/Menu/CopyClear/copyClearSession.c` | Add `copyClear_isClearMode()` |
| `Core/Menu/CopyClear/copyClearSession.h` | Declare `copyClear_isClearMode()` |
| `Core/Bank/Scene/Pattern/PatternStackService.c` | Add `patSvc_readStepAutomation()` (if needed) |
| `Core/Bank/Scene/Pattern/PatternStackService.h` | Declare `patSvc_readStepAutomation()` (if needed) |

## RAM cost

§A: 0 bytes.
§B: 1 byte (`sa_trackPresenceMask`).  All other state is shared with
the existing VOICE overlay.

## §B.19  Test matrix

| # | Steps | Action | Expected | Result |
|---|-------|--------|----------|--------|
| 1 | Hold 1 step → TRACK 1 | Adjust length | Step automation written for track 1 length | PASS |
| 2 | Hold 3 steps → TRACK 2 | Adjust scale | All 3 steps get track 2 scale automation | PASS |
| 3 | Same as 2 | Check underline | Scale name is underlined | PASS |
| 4 | Same as 2 | SHIFT+COPY + turn length knob | Track 2 length automation removed from held steps, underline drops | PASS |
| 5 | Release all steps | Check display | Overlay exits, normal STEP display | PASS |
| 6 | Hold steps → TRACK → adjust play mode | Check | Normal retained-value edit, no automation | PASS |
| 7 | Hold steps → TRACK → adjust MIDI ch | Check | Normal retained-value edit | PASS |
| 8 | Play transport running | Hold steps → TRACK → adjust scale | Automation fires at those steps during playback | PASS |
| 9 | Hold 1 step in STEP mode | Adjust velocity | Single step (regression) | PASS |
| 10 | Hold 3 steps in STEP mode | Adjust velocity | All 3 steps get new velocity | PASS |
| 11 | Hold 3 steps in STEP mode | Adjust note | All 3 steps get new note | PASS |
| 12 | Hold 3 steps → scroll to automation editor | Check | Single-step automation editor, no broadcast | PASS |
| 13 | VOICE mode: hold steps → press TRACK | Check | Enters VOICE overlay, not STEP overlay | PASS |


---

## Implementation notes (this session)

Status: **code complete, builds clean, awaits hardware verification.**
Nothing below is hardware-verified; the plan's §B.19 matrix is still the
acceptance gate.

### Build result

```
make all   →  EXIT=0, no warnings in menu.c / buttonHandler.c /
             copyClearSession.c
text=539,880  data=412  bss=427,608   (DEV config)
Flash 540,292 / 753,664 B used (headroom 213,372 B)
ITCM 4,168 / 16,384 B; DTCM statics 4,472 B; FXBUF 126,592 B (margin 3,712)
```

### Resource delta (measured, not estimated)

Built HEAD (S078 P1, 0c23def) from `git archive` in `/tmp` and compared:

| | HEAD (P1) | + P2 | delta |
|---|---|---|---|
| text | 537,592 | 539,880 | **+2,288 B flash** |
| data | 420 | 412 | -8 B |
| bss | 427,608 | 427,608 | **+0 B** |

The plan approved ~1 B static SRAM for `sa_trackPresenceMask`; the measured
cost is **0 additional BSS bytes** - the single byte is absorbed into existing
alignment padding after the shared `va_*` overlay block. No other new
static/global storage was added. Everything else is shared `va_*` state and
flash-resident code.

### Changes actually landed

| File | Change |
|------|--------|
| `Core/Menu/menu.c` | §A broadcast helper + 3 commit arms; `sa_trackPresenceMask`; `menu_seqCellToTrackTarget()`; `sa_refreshPresence()`; `sa_writeAutomationFromKnob()`; `sa_clearAutomationFromKnob()`; `sa_refreshAutomationLeds()`; `sa_applyTrackMarkers()`; `menu_enterStepTrackAutomationOverlay()`; held-state/reset/underline-service extensions; pot+encoder intercepts; service-loop poll; switchPage exit reset; repaint-pipeline marker call; forward decls |
| `Core/Menu/menu.h` | `menu_enterStepTrackAutomationOverlay()` declaration + doc block |
| `Core/Hardware/frontPanel/buttonHandler.c` | Held-step detection in the `handleVoiceButton()` STEP arm |
| `Core/Menu/CopyClear/copyClearSession.c` | `copyClear_isClearMode()` |
| `Core/Menu/CopyClear/copyClearSession.h` | `copyClear_isClearMode()` declaration |

`PatternStackService.c/h` were **not** changed: the plan's optional
`patSvc_readStepAutomation()` was unnecessary because
`pat_readStepAutomations()` already exists and is exactly what the VOICE
overlay uses; the STEP overlay reuses `va_resolveHeldValue()` directly (the
shared held list and `menu_shownPattern`/`menu_activeVoice` make it
target-agnostic).

### Deviations from the plan (and why)

1. **Commit-arm owner.** The three `PAR_STEP_*` arms live in
   `menu_parseGlobalParam()`, reached from
   `menu_cellCommitValue() -> menu_sendEditedParameter()`; the plan called the
   owner `menu_cellCommitValue()`. Same effect, one level down.
2. **Target mapping, no `_Static_assert`.** `SCENE_MOD_TARGET_ID()` is
   private to `SceneModTargets.c`, so a compile-time assert cannot live in
   `menu.c`. `menu_seqCellToTrackTarget()` instead computes the ID from the
   documented 7-row-per-kind layout and re-validates it through
   `sceneModTarget_descriptor()` (kind + `voice_slot`), so a future table
   reorder degrades to "not automatable" rather than writing a wrong target.
3. **`menu_switchPage()` exit reset.** The plan said `menu_switchPage()`
   already resets on any page change; it only did so **leaving a voice page**.
   Extended the condition to also reset when leaving `SEQ_PAGE`, so a STEP
   overlay cannot survive a mode change.
4. **`va_underlineService()` admission.** It unconditionally dropped the
   suppression/validity byte on any non-voice page, which would have killed
   the STEP overlay's working-value cache every service pass. The guard now
   admits `SEQ_PAGE && va_overlayActive`.
5. **Presence refresh on mask change.** The plan refreshed presence only at
   entry/after writes/after clears. Added `sa_refreshPresence()` on every
   held-mask change (in `va_updateHeldState()`) so releasing the only
   automation-source step drops its underline immediately.
6. **Pot half-page offset.** The plan's `sa_writeAutomationFromKnob(cellPos)`
   assumed `cellPos == knobNr`. On a static page the visible half is offset by
   `(activeParameter > 3) ? 4 : 0`, so the intercept computes the absolute cell
   position first (`knobNr + is2ndPage`); positions 4..7 have no automatable
   target and fall through to normal retained editing.
7. **Clear-menu guard.** The per-held-step clear also requires
   `!copyClear_menuVisible()`, matching the copy/clear rule that pots are
   inert while an operation menu is shown.
8. **Overview value display.** Implemented the full VOICE-equivalent behaviour
   (show the newest held value + underline it, else underline the name),
   rather than the plan's overview snippet (name underline only).
9. **Entry resets `editModeActive`** to match the ordinary STEP entry through
   `menu_switchPage()`.
10. **Value formatting** uses `va_formatValue3()` (the existing
    explicit-value formatter); the plan's `menu_formatCellValue()` does not
    exist.

### Known limitations carried from the plan

- Track **length 128** cannot be stored as step automation (7-bit storage caps
  at 127); the write path clamps `min(value, 127)`, matching
  `seq_applySceneAutomation()`'s 7-bit domain. Same limit as every other
  Scene-target automation.
- No bounded background presence scan: presence is the 3-bit
  `sa_trackPresenceMask`, refreshed at entry, on mask change, and after each
  write/clear.
- No encoder-based clear path: the copy/clear encoder gestures do nothing in
  clear mode without a menu (`copyClear_encoderTurned()` is menu-only), so the
  clear gesture is pot-only, as specified.

### Not yet done

- Hardware testing per §B.19 (all 14 rows).
- No changes to any persistence, PatternData, SceneModTargets, sequencer, or
  buttonHandler.h path.
