# S076 P4 — Reload Scene, Bar Chaselight, SHIFT+SELECT Pattern Length

Three independent features for this phase.

---

## Feature 1: "Reload Scene" in the Clear Scene Menu

### What

Add a new selection to the PERF clear-scene menu: **"reload scene"**. When
confirmed, it reloads the Scene's source from the SD card into the resident
Scene, exactly as if the user had loaded it from the Load menu — but without
any menu page change.

### Behaviour

1. The source slot is read from `.hcnames` via
   `filesystem_residentSource(filesystem_identityRow(FS_ROW_SCENE, scene, 0))`.
2. A **valid** source is a numeric slot 0..999 (not INHERIT `0x1fff`, UNKNOWN
   `0x1ffe`, DIRECT `0x1ffd`, or PATTERN_AUTOSAVE `0x1ffc`).
3. If the source is valid, issue
   `filesystem_requestLoadSceneForScenes(slot, scene_mask, cb)` with the
   single target Scene. The callback should not switch the menu page — it
   should apply the Scene through the existing runtime worker and repaint the
   current display in place.
4. If the source is invalid, or the filesystem request is refused (facade
   busy, file missing, etc.), silently drop the operation: no error overlay,
   no menu change, no state change.
5. The operation is a Scene-level clear selection, so it runs when the clear
   object is released with this selection active — same as `clear scene` etc.

### Implementation Plan

**clearOps.h** — add `CC_CLEAR_SCENE_RELOAD = 10u` to `cc_clear_scene_sel_t`,
after `CC_CLEAR_SCENE_RESET_FX_MORPH (9)`.

**clearOps.c**:
- Add `"reload scene"` to `ccClear_sceneLabels[]` (index 10).
- Bump `CC_MENU_CLEAR_SCENE` count from 10 to 11 in
  `ccClear_selectionCount()`.
- In `ccClear_runJob()`, add a case for selection 10 (`CC_CLEAR_SCENE_RELOAD`):
  - Read the source row: `filesystem_identityRow(FS_ROW_SCENE, job->scene, 0)`.
  - Get source: `filesystem_residentSource(row) & FS_RESIDENT_SOURCE_VALUE_MASK`.
  - If source ≤ 999, call `preset_loadSceneForScenes(source,
    1u << job->scene)`. This is the existing Preset API
    (`presetManager.c:2697`) which sets up `pm_status`, calls
    `filesystem_requestLoadSceneForScenes()` with the standard
    `on_scene_load_complete` callback, and handles the full lifecycle:
    Bank-present promotion, Pattern dirty marking,
    `preset_completeFilesystemOp()`, and runtime application through
    `menu_startSoundApply()`.
  - Otherwise return `CC_RUN_DONE` (silent drop).
  - If `preset_loadSceneForScenes()` returns 0 (refused — facade busy, file
    missing, or preset state conflict), also return `CC_RUN_DONE`.

**No filesystem.c/h changes.** The existing Scene-load path through Preset
does everything needed: loads sceneset.scg, embedded Kit, Pattern, Effect,
commits the payload, publishes HCNAMES, and applies the Scene runtime.

**No menu page change.** The Preset load path does not require the Load page
to be active — it just needs `pm_status` to be idle and the filesystem
facade to be available. The `on_scene_load_complete` callback runs
`menu_startSoundApply()` which repaints. The only precondition is that the
Preset layer isn't already busy with another operation, which the
`preset_loadSceneForScenes()` entry handles.

### Confirmed Path Detail

The `preset_loadSceneForScenes()` path sets `pm_status = PRESET_LOAD_IN_PROGRESS`
and the `on_scene_load_complete` callback eventually sets `pm_status =
PRESET_UPDATE_READY`. This is picked up by `menu_pollPresetStatus()` in the
main loop (called every iteration), which at the `PRESET_OP_SCENE_LOAD` case
(menu.c:12288):
1. Calls `menu_refreshResidentNameScratchKit()` for HCNAMES Kit-family rows.
2. Checks whether the active page is LOAD_PAGE/SAVE_PAGE — if not (our case),
   falls through.
3. Calls `menu_startSoundApply(1, reset_save=0, ...)` — `reset_save=0` because
   the active page is not Load/Save, so no cursor/page reset occurs. The scene
   runtime is applied and the display repaints in place.

This means the full reload path works correctly from any menu page without
switching to the Load page.

### Risks and Edge Cases

- If playback is running, the Scene reload replaces pattern data mid-playback.
  This matches what a normal Scene load does — the sequencer realigns. No
  special handling needed.
- If the source Scene slot was deleted or renamed on the card since it was
  last loaded, the filesystem load will fail and `menu_pollPresetStatus()`
  will show the standard error overlay. To match the "silently drop" spec,
  the clear job could pre-check `filesystem_status()` or we can accept
  the error overlay as the existing behaviour for a failed load.
- The clear job should return `CC_RUN_DONE` immediately after issuing the
  request (fire-and-forget): the Scene load is its own lifecycle through
  Preset/filesystem/menu_pollPresetStatus, entirely separate from the
  copy/clear service. The copy/clear button will have been released by
  the time the reload completes.
- Preset's `pm_status` must be IDLE when we call `preset_loadSceneForScenes()`.
  If Preset is busy (another load/save in progress), the call returns 0 and
  we drop silently. This is correct.

---

## Feature 2: Bar Chaselight on SELECT LEDs in STEP Mode

### What

In STEP mode, the SELECT row (8 LEDs, one per bar) currently shows which bar
is being viewed as a steady light. Add a **tempo-pulsed chaselight** that
shows the bar currently being played by the sequencer.

### Behaviour

1. **Only shown during playback** (`seq_isRunning()` nonzero). When stopped,
   SELECT LEDs show only the viewed-bar indicator as today.
2. The chaselight LED **pulses to tempo** like the Play button does (on at
   beat boundaries, off one step later — using the same beat-pulse timing
   from `seq_ledState.beatPulse`).
3. The playback bar is derived from the chase step position:
   `playbackBar = seq_ledState.chaseStep / NUM_STEPS_PER_BAR`.
4. **If the playback bar and viewed bar are different**: the viewed bar is
   steady on; the playback bar pulses normally (on at beat, off between).
5. **If the playback bar and viewed bar are the same**: the LED is already
   steady on for the viewed bar, so the pulse is **inverted** — the LED
   blinks off at the beat boundary and back on between beats. This creates
   a visible pulse on an already-lit LED.
6. The chaselight moves with playback, so as the sequencer crosses bar
   boundaries, the pulsing LED moves across the SELECT row.

### Implementation Plan

**ledHandler.c** — modify `led_processSeqLedState()`:

The existing CHASE and BEAT dirty bits already fire at the right times. The
approach:

- When `SEQ_LED_DIRTY_CHASE` or `SEQ_LED_DIRTY_BEAT` fires and we're in
  `SELECT_MODE_STEP`:
  - Compute `playbackBar = seq_ledState.chaseStep / NUM_STEPS_PER_BAR`.
  - For each SELECT LED (0..7):
    - If it's `menu_currentBar` (viewed): **steady on** (base state).
    - Else: **off** (base state).
  - Then overlay the playback bar pulse:
    - If playback bar == viewed bar:
      - At beat pulse on (beatPulse == 1): turn that SELECT LED **off**
        (inverted pulse).
      - At beat pulse off (beatPulse == 0): turn it **on** (restore).
    - If playback bar != viewed bar:
      - At beat pulse on: turn that SELECT LED **on**.
      - At beat pulse off: turn it **off**.

This means we should re-render SELECT LEDs on both CHASE and BEAT dirty
events, since the bar can change (CHASE) and the pulse phase changes (BEAT).

An alternative cleaner approach: add a dedicated function
`led_updateSelectBarChaselight()` called from the existing BEAT and CHASE
handlers when in STEP mode. This function:
1. Writes the base state: `menu_currentBar` LED on, all others off.
2. If `seq_isRunning()`:
   - Compute `playbackBar`.
   - If `playbackBar == menu_currentBar` and `beatPulse`: turn the LED off.
   - If `playbackBar != menu_currentBar` and `beatPulse`: turn the LED on.
3. If not running: base state only (no overlay).

**Existing STEP mode SELECT rendering** — `led_setActiveSelectButton()` is
called from `led_updatePatternTrackView()` when `updateSelectRow` is true,
and from `buttonHandler_selectBar()`. These set the base state. The
chaselight overlay runs on top of this in the LED drain tick, same as the
STEP-row chaselight does.

**seq_ledState.chaseStep** gives the absolute step (0..127).
`chaseStep / NUM_STEPS_PER_BAR` gives the 0..7 bar.

**No new dirty flags needed** — the bar chaselight piggybacks on
`SEQ_LED_DIRTY_CHASE` (bar boundary) and `SEQ_LED_DIRTY_BEAT` (pulse phase).

### Risks

- Per-track length means different tracks can be in different bars. The
  chaselight should show the **active track's** bar, matching the existing
  STEP-row chaselight which uses `seq_stepIndex[menu_getActiveVoice()]`.
  The chaseStep already reflects the active voice.
- When a track wraps at a length shorter than 128, the chaselight bar stays
  within the track's active range (e.g., a 48-step track only pulses
  bars 0-2).
- The LED layer system in ledHandler may need care — the SELECT row doesn't
  currently use `LED_LAYER_CHASE`. We may need to either use the blink
  mechanism for the pulse, or simply overwrite the SELECT LED values directly
  in the drain, which is simple and correct since the drain runs after all
  base-state writes.

---

## Feature 3: SHIFT+SELECT Sets Pattern Length in STEP Mode

### What

In STEP mode, pressing SHIFT + a SELECT button (1..8) sets the active
track's pattern length to end at the end of that bar.

### Behaviour

1. SELECT button N (0-indexed) represents bar N. The pattern length is set to
   `(N + 1) * NUM_STEPS_PER_BAR`, i.e. SELECT 1 → 16, SELECT 2 → 32, …
   SELECT 8 → 128.
2. This sets the active track's `track_length` via `pat_setTrackLength()`.
3. The active track is `menu_getActiveVoice()` in the active Scene.
4. The viewed bar (`menu_currentBar`) should clamp if it's now beyond the new
   length — e.g., if the user sets length to 2 bars and was viewing bar 5,
   snap to the last valid bar.
5. After the length change: refresh the display (track settings visible on
   the LCD) and refresh the SELECT LEDs to reflect the new length (optional:
   light all bars within the length, only the viewed bar, or just the viewed
   bar as today — follow existing convention).
6. The track length is per-track in the resident Scene and is persisted via
   `pat_markSceneDirty()` (already done inside `pat_setTrackLength()`).

### Implementation Plan

**buttonHandler.c** — `handleSelectButton()`:

Currently, SHIFT+SELECT in STEP mode calls `buttonHandler_selectBar(selectNr)`
(same as unshifted). Change the SHIFT+SELECT_MODE_STEP case:

```c
case SELECT_MODE_STEP:
    /* SHIFT+SELECT sets pattern length to end of this bar. */
    {
        uint8_t newLength = (uint8_t)((selectNr + 1u) * NUM_STEPS_PER_BAR);
        uint8_t scene = menu_getViewedPattern();
        uint8_t track = menu_getActiveVoice();
        pat_setTrackLength(scene, track, newLength);
        /* Clamp viewed bar if beyond new length. */
        if (menu_currentBar >= (selectNr + 1u))
            buttonHandler_selectBar(selectNr);
        /* Update displayed track settings. */
        pat_applyTrackSettingsToMenu(scene, track);
        menu_repaintAll();
    }
    break;
```

The active Scene index is `menu_getViewedPattern()`, which is the accessor
used by `pat_setTrackLength()` calls from menu.c (line 13473) and by the
copy/clear module's `cc_activeScene()` helper.

**No new PatternData API needed** — `pat_setTrackLength()` already exists and
handles validation and dirty marking.

**Interaction with Feature 2**: setting a shorter length means the bar
chaselight should not pulse on bars beyond the track length. This is
automatic — `seq_stepIndex` wraps within `track_length`, so the chaseStep
will never exceed the track length and the derived bar is always valid.

### Risks

- Per-track length is already the model. The user may want a per-pattern (all
  tracks) length set. This implementation sets it per-track as the existing
  model supports. If per-pattern is desired, iterate `pat_setTrackLength()`
  across all 7 tracks.
- The menu parameter `PAR_TRACK_LENGTH` should be refreshed in the display
  after the change — `pat_applyTrackSettingsToMenu()` handles this.

---

## Build Impact

- **RAM**: zero new persistent allocations. The chaselight state is derived
  from existing `seq_ledState` fields. No new globals.
- **Flash**: small — a new label string, one new case in `ccClear_runJob()`,
  one new function in `ledHandler.c`, and a modified `handleSelectButton()`.
- **Existing behaviour preserved**: unshifted SELECT in STEP mode still
  selects bars. SHIFT+SELECT in VOICE mode still selects bars (unchanged).
  SHIFT+SELECT in STEP mode changes from bar-select to pattern-length-set.

## Implementation Order

1. Feature 3 (SHIFT+SELECT length) — simplest, standalone.
2. Feature 1 (reload scene) — involves filesystem request, needs care around
   the clear job lifecycle.
3. Feature 2 (bar chaselight) — UI polish, can be tested with playback.
