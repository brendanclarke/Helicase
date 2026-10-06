# S076 P4 — Full Implementation Schedule

Reference: `S076_P4_RELOAD_SCN_BAR_FUNC.md`.

Every change is cited by file, line, and action (add/modify/remove). Each
change carries a comment-block description suitable for placement in the
source file alongside the change.

---

## Feature 3: SHIFT+SELECT Sets Pattern Length (STEP mode)

Implementation order: **first** — standalone, no dependencies.

### Change 3-A: `buttonHandler.c` — separate SHIFT+SELECT in STEP mode

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** 953–957 (inside `handleSelectButton()`, the SHIFT-held branch)
**Action:** Modify — split `SELECT_MODE_STEP` out of the shared
`SELECT_MODE_STEP` / `SELECT_MODE_VOICE` fallthrough.

**Current code (lines 952–957):**
```c
    if (buttonHandler_getShift()) {
        switch (bh_state.selectButtonMode) {
        case SELECT_MODE_STEP:
        case SELECT_MODE_VOICE:
            buttonHandler_selectBar(selectNr);
            break;
```

**Replace with:**
```c
    if (buttonHandler_getShift()) {
        switch (bh_state.selectButtonMode) {
        case SELECT_MODE_STEP:
            /*
             * SHIFT+SELECT in STEP mode: set the active track's pattern
             * length to the end of the bar represented by this SELECT button.
             *
             * What:       the active track's track_length is set to
             *             (selectNr + 1) * NUM_STEPS_PER_BAR, so SELECT 1
             *             gives 16 steps (one bar), SELECT 8 gives 128 steps
             *             (eight bars). The viewed bar clamps downward if it
             *             would exceed the new length, and the STEP/SELECT LEDs
             *             and the menu parameter display repaint.
             * Why:        provides a fast physical gesture for setting the
             *             per-track loop length from the front panel, matching
             *             the natural SELECT=bar mapping in STEP mode.
             * Inputs:     selectNr 0..7 from handleSelectButton(); the active
             *             Scene is menu_getViewedPattern(); the active track is
             *             menu_getActiveVoice().
             * Outputs:    pat_setTrackLength() commits the value and marks the
             *             Scene dirty (AutoSave). PAR_TRACK_LENGTH is refreshed
             *             by pat_applyTrackSettingsToMenu(). If the viewed bar
             *             was beyond the new length, buttonHandler_selectBar()
             *             clamps it, updating menu_currentBar, selectedStep, and
             *             repainting STEP/SELECT LEDs. menu_repaintAll()
             *             redraws the current LCD page.
             * Accessors:  menu_getViewedPattern(), menu_getActiveVoice(),
             *             menu_currentBar, menu_repaintAll().
             * Affiliates: pat_setTrackLength() (PatternData.c:1106),
             *             pat_applyTrackSettingsToMenu() (PatternData.c:1093),
             *             buttonHandler_selectBar() (buttonHandler.c:398),
             *             seq_advanceTrackStep() reads track_length at
             *             playback (sequencer.c:908).
             */
        {
            uint8_t newLen = (uint8_t)((selectNr + 1u) * NUM_STEPS_PER_BAR);
            uint8_t scene  = menu_getViewedPattern();
            uint8_t track  = menu_getActiveVoice();

            pat_setTrackLength(scene, track, newLen);

            if (menu_currentBar >= (uint8_t)(selectNr + 1u))
                buttonHandler_selectBar(selectNr);

            pat_applyTrackSettingsToMenu(scene, track);
            menu_repaintAll();
        }
            break;

        case SELECT_MODE_VOICE:
            buttonHandler_selectBar(selectNr);
            break;
```

**Includes needed:** none — `PatternData.h` and `menu.h` are already included
(lines 26, 21).

**No other files change for Feature 3.**

---

## Feature 1: "Reload Scene" in the Clear Scene Menu

Implementation order: **second**.

### Change 1-A: `clearOps.h` — add `CC_CLEAR_SCENE_RELOAD` enum constant

**File:** `Core/Menu/CopyClear/clearOps.h`
**Line:** 79 (after `CC_CLEAR_SCENE_RESET_FX_MORPH`)
**Action:** Add — new enum constant `CC_CLEAR_SCENE_RELOAD = 10u`.

**Current code (lines 69–80):**
```c
typedef enum {
    CC_CLEAR_SCENE_CANCEL = 0u,
    CC_CLEAR_SCENE_ALL,
    CC_CLEAR_SCENE_SETTINGS,
    CC_CLEAR_SCENE_PATTERN,
    CC_CLEAR_SCENE_AUTOMATION,
    CC_CLEAR_SCENE_NOTES,
    CC_CLEAR_SCENE_FX,
    CC_CLEAR_SCENE_FX_SEQUENCE,
    CC_CLEAR_SCENE_RESET_MORPH,     /* 8 */
    CC_CLEAR_SCENE_RESET_FX_MORPH   /* 9 */
} cc_clear_scene_sel_t;
```

**Replace with:**
```c
typedef enum {
    CC_CLEAR_SCENE_CANCEL = 0u,
    CC_CLEAR_SCENE_ALL,
    CC_CLEAR_SCENE_SETTINGS,
    CC_CLEAR_SCENE_PATTERN,
    CC_CLEAR_SCENE_AUTOMATION,
    CC_CLEAR_SCENE_NOTES,
    CC_CLEAR_SCENE_FX,
    CC_CLEAR_SCENE_FX_SEQUENCE,
    CC_CLEAR_SCENE_RESET_MORPH,     /* 8 */
    CC_CLEAR_SCENE_RESET_FX_MORPH,  /* 9 */
    /*
     * What:       reload the Scene from the SD card using its HCNAMES source.
     * Why:        provides a non-destructive "revert to saved" for a Scene
     *             without requiring navigation to the Load page. The operation
     *             is placed in the PERF clear-scene menu because it operates
     *             on a whole Scene and naturally extends the existing Scene
     *             management selections.
     * Inputs:     the clear object's Scene index; the source is read from the
     *             HCNAMES resident source register at runtime.
     * Outputs:    if a valid numeric source (0..999) exists, a full Scene
     *             load is issued through preset_loadSceneForScenes();
     *             otherwise the operation is silently dropped.
     * Accessors:  filesystem_identityRow(), filesystem_residentSource().
     * Affiliates: preset_loadSceneForScenes() (presetManager.c:2697),
     *             on_scene_load_complete() (presetManager.c:467),
     *             menu_pollPresetStatus() PRESET_OP_SCENE_LOAD (menu.c:12288).
     */
    CC_CLEAR_SCENE_RELOAD           /* 10 */
} cc_clear_scene_sel_t;
```

### Change 1-B: `clearOps.c` — add label string

**File:** `Core/Menu/CopyClear/clearOps.c`
**Lines:** 59–62 (`ccClear_sceneLabels[]`)
**Action:** Modify — append `"reload scene"` to the array.

**Current code (lines 59–62):**
```c
static const char *const ccClear_sceneLabels[] = {
    "cancel", "scene", "settings", "pattern", "automation", "notes", "fx",
    "fx sequence", "reset morph", "reset fx morph"
};
```

**Replace with:**
```c
/*
 * What:       PERF Scene clear labels. "reload scene" (index 10) reloads
 *             the Scene from its HCNAMES source slot on the SD card.
 * Why:        extends the menu to expose the Scene reload operation that
 *             reverts the Scene to its saved state without page navigation.
 * Affiliates: CC_CLEAR_SCENE_RELOAD, ccClear_runReloadScene().
 */
static const char *const ccClear_sceneLabels[] = {
    "cancel", "scene", "settings", "pattern", "automation", "notes", "fx",
    "fx sequence", "reset morph", "reset fx morph", "reload scene"
};
```

### Change 1-C: `clearOps.c` — bump selection count

**File:** `Core/Menu/CopyClear/clearOps.c`
**Line:** 94 (`CC_MENU_CLEAR_SCENE` case in `ccClear_selectionCount()`)
**Action:** Modify — change return value from `10u` to `11u`.

**Current code (line 94):**
```c
    case CC_MENU_CLEAR_SCENE:    return 10u; /* +reset morph, +reset fx morph */
```

**Replace with:**
```c
    case CC_MENU_CLEAR_SCENE:    return 11u; /* +reset morph, +reset fx morph, +reload scene */
```

### Change 1-D: `clearOps.c` — add `ccClear_runReloadScene()` function

**File:** `Core/Menu/CopyClear/clearOps.c`
**Line:** Insert immediately before `ccClear_runJob()` (before line 604).
**Action:** Add — new static function.

**Insert the following:**
```c
/*
 * `reload scene`: reload this Scene from its HCNAMES source slot on the SD
 * card.
 *
 * What:       reads the Scene's source slot from the filesystem-owned HCNAMES
 *             resident source register. If the source is a valid numeric
 *             library slot (0..999), issues a full Scene load through the
 *             Preset API. If the source is a non-numeric token (INHERIT,
 *             UNKNOWN, DIRECT, PATTERN_AUTOSAVE) or the Preset layer refuses
 *             the request (already busy, filesystem facade unavailable), the
 *             operation is silently dropped.
 * Why:        gives users a fast "revert to saved" from the PERF clear menu
 *             without navigating to the Load page. The Preset path is used
 *             (not a direct filesystem call) because it owns pm_status,
 *             the Scene-load completion callback, Bank-present promotion,
 *             Pattern AutoSave marking, and the menu_pollPresetStatus()
 *             handoff that applies the runtime surface and repaints the
 *             display.
 * Inputs:     job->scene is the clear object's Scene index (0..15).
 * Outputs:    CC_RUN_DONE unconditionally (fire-and-forget). If accepted by
 *             Preset, the load lifecycle proceeds asynchronously: filesystem
 *             reads sceneset.scg, embedded Kit, Pattern, and Effect;
 *             on_scene_load_complete() commits and marks AutoSave dirty;
 *             menu_pollPresetStatus() picks up PRESET_OP_SCENE_LOAD and
 *             calls menu_startSoundApply() with reset_save=0 (because the
 *             active page is not LOAD_PAGE/SAVE_PAGE), applying the Scene
 *             runtime and repainting the current display in place.
 * Accessors:  filesystem_identityRow(FS_ROW_SCENE, scene, 0),
 *             filesystem_residentSource(), FS_RESIDENT_SOURCE_VALUE_MASK,
 *             preset_loadSceneForScenes().
 * Affiliates: on_scene_load_complete() (presetManager.c:467),
 *             menu_pollPresetStatus() PRESET_OP_SCENE_LOAD (menu.c:12288),
 *             menu_startSoundApply() (menu.c:512).
 */
static uint8_t ccClear_runReloadScene(const cc_job_t *job)
{
    uint16_t row = filesystem_identityRow(FS_ROW_SCENE, job->scene, 0u);
    uint16_t src = (uint16_t)(filesystem_residentSource(row) &
                              FS_RESIDENT_SOURCE_VALUE_MASK);

    if (src <= 999u)
        (void)preset_loadSceneForScenes(src, (uint16_t)(1u << job->scene));

    return CC_RUN_DONE;
}
```

### Change 1-E: `clearOps.c` — add dispatch case in `ccClear_runJob()`

**File:** `Core/Menu/CopyClear/clearOps.c`
**Line:** 650 (after the `CC_CLEAR_SCENE_RESET_FX_MORPH` case, before `default:`)
**Action:** Add — new case.

**Current code (lines 649–653):**
```c
        case CC_CLEAR_SCENE_RESET_MORPH:    return ccClear_runResetSceneMorph(job);
        case CC_CLEAR_SCENE_RESET_FX_MORPH: return ccClear_runResetFxMorph(job);
        default:
            ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_BAD_SELECTION, sel);
            return CC_RUN_DROP;
```

**Replace with:**
```c
        case CC_CLEAR_SCENE_RESET_MORPH:    return ccClear_runResetSceneMorph(job);
        case CC_CLEAR_SCENE_RESET_FX_MORPH: return ccClear_runResetFxMorph(job);
        case CC_CLEAR_SCENE_RELOAD:         return ccClear_runReloadScene(job);
        default:
            ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_BAD_SELECTION, sel);
            return CC_RUN_DROP;
```

### Summary of includes

No new includes needed in `clearOps.c` — `filesystem.h` (line 22) and
`presetManager.h` (line 19) are already present.

---

## Feature 2: Bar Chaselight on SELECT LEDs (STEP mode)

Implementation order: **third**.

### Change 2-A: `ledHandler.c` — add `led_updateSelectBarChaselight()`

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Line:** Insert immediately after `led_setBeatPulse()` (after line 1408).
**Action:** Add — new static function.

**Insert the following:**
```c
/*
 * Update the SELECT LED bar chaselight during playback in STEP mode.
 *
 * What:       overlays a tempo-pulsed indicator on the SELECT row to show
 *             which bar the sequencer is currently playing, in addition to
 *             the steady-on indicator for the viewed bar (menu_currentBar).
 *             The pulse follows the same beat timing as the Play button:
 *             on at beat boundaries, off one step later.
 * Why:        gives the user visual bar-position feedback during playback
 *             without leaving STEP mode. The inverted-pulse rule ensures the
 *             chaselight is visible even when the playback bar and the viewed
 *             bar coincide (the LED blinks off instead of being a second on).
 * Inputs:     seq_ledState.chaseStep (the active voice's absolute step
 *             0..127, already reflecting the per-track length wrap),
 *             seq_ledState.beatPulse (1 at beat boundary, 0 between),
 *             menu_currentBar (the viewed bar 0..7),
 *             seq_isRunning() (transport state).
 *             Must only be called while SELECT_MODE_STEP is active.
 * Outputs:    SELECT LED base state: menu_currentBar on, all others off.
 *             When running and beat pulse is active:
 *               - playback bar == viewed bar: that LED turns OFF (inverted).
 *               - playback bar != viewed bar: that LED turns ON (normal pulse).
 *             When running and beat pulse is inactive:
 *               - base state only (viewed bar on, all others off).
 *             When stopped: base state only.
 * Accessors:  led_setValue(), seq_isRunning(), seq_ledState (ledHandler.h),
 *             menu_currentBar (menu.h), NUM_STEPS_PER_BAR (PatternData.h).
 * Affiliates: led_processSeqLedState() (the sole caller, via CHASE and BEAT
 *             dirty handlers), led_setActiveSelectButton() (base state writer
 *             called from buttonHandler_selectBar and led_updatePatternTrackView),
 *             led_setBeatPulse() (Play button pulse, same timing source).
 */
static void led_updateSelectBarChaselight(void)
{
    uint8_t i;
    uint8_t viewedBar = menu_currentBar;

    /* Write base state: viewed bar on, all others off. */
    for (i = 0u; i < 8u; i++)
        led_setValue((uint8_t)(i == viewedBar), (uint8_t)(LED_PART_SELECT1 + i));

    if (!seq_isRunning())
        return;

    {
        uint8_t playbackBar =
            (uint8_t)(seq_ledState.chaseStep / NUM_STEPS_PER_BAR);
        uint8_t pulse = seq_ledState.beatPulse;

        if (playbackBar >= NUM_BARS)
            return;

        if (pulse) {
            if (playbackBar == viewedBar) {
                /* Inverted pulse: the LED is already on for the viewed bar,
                 * so turn it OFF at the beat to create a visible blink. */
                led_setValue(0u, (uint8_t)(LED_PART_SELECT1 + playbackBar));
            } else {
                /* Normal pulse: light the playback bar LED at the beat. */
                led_setValue(1u, (uint8_t)(LED_PART_SELECT1 + playbackBar));
            }
        }
        /* When pulse is 0: base state only (the viewed bar on, others off).
         * A previous normal-pulse LED is extinguished by the base-state
         * write above; an inverted-pulse LED is restored the same way. */
    }
}
```

### Change 2-B: `ledHandler.c` — call chaselight from CHASE handler

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Lines:** 1518–1523 (the `SEQ_LED_DIRTY_CHASE` handler inside
`led_processSeqLedState()`)
**Action:** Modify — add SELECT bar chaselight call after the existing STEP-row
chase logic.

**Current code (lines 1518–1523):**
```c
    if (d & SEQ_LED_DIRTY_CHASE) {
        if (seq_isRunning())
            led_updateCurrentStep(seq_ledState.chaseStep);
        else
            led_clearActive_step();
    }
```

**Replace with:**
```c
    if (d & SEQ_LED_DIRTY_CHASE) {
        if (seq_isRunning())
            led_updateCurrentStep(seq_ledState.chaseStep);
        else
            led_clearActive_step();
        /*
         * What:       update the SELECT-row bar chaselight whenever the
         *             playback step advances and may have crossed a bar
         *             boundary, or when transport stops (clearing the pulse).
         * Why:        the CHASE dirty bit fires at every step advance and at
         *             stop, which is exactly when the playback bar may change
         *             or disappear. Coupling the call here avoids a new dirty
         *             flag.
         * Inputs:     SELECT_MODE_STEP guard; the function reads
         *             seq_ledState.chaseStep and beatPulse internally.
         * Affiliates: led_updateSelectBarChaselight() above.
         */
        if (buttonHandler_getMode() == SELECT_MODE_STEP)
            led_updateSelectBarChaselight();
    }
```

### Change 2-C: `ledHandler.c` — call chaselight from BEAT handler

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Lines:** 1512–1513 (the `SEQ_LED_DIRTY_BEAT` handler inside
`led_processSeqLedState()`)
**Action:** Modify — add SELECT bar chaselight call after the Play button
pulse.

**Current code (lines 1511–1513):**
```c
    /* Beat pulse: set/clear START_STOP according to seq_ledState.beatPulse. */
    if (d & SEQ_LED_DIRTY_BEAT)
        led_setBeatPulse(seq_ledState.beatPulse);
```

**Replace with:**
```c
    /* Beat pulse: set/clear START_STOP according to seq_ledState.beatPulse. */
    if (d & SEQ_LED_DIRTY_BEAT) {
        led_setBeatPulse(seq_ledState.beatPulse);
        /*
         * What:       update the SELECT-row bar chaselight whenever the beat
         *             pulse phase changes (on → off or off → on).
         * Why:        the pulse phase controls whether the playback-bar LED
         *             is lit or dark. BEAT fires once per phase transition,
         *             which is exactly when the SELECT-row chaselight must
         *             repaint. The CHASE handler above covers bar-boundary
         *             changes; this handler covers pulse-phase changes within
         *             the same bar.
         * Inputs:     SELECT_MODE_STEP guard; the function reads
         *             seq_ledState.beatPulse internally.
         * Affiliates: led_updateSelectBarChaselight() above.
         */
        if (buttonHandler_getMode() == SELECT_MODE_STEP)
            led_updateSelectBarChaselight();
    }
```

### Summary of includes

No new includes needed — `sequencer.h` (line 54), `buttonHandler.h` (line 45),
`menu.h` (line 44), and `PatternData.h` (line 46) are already present.

---

## Complete File-Change Index

| File | Lines | Action | Feature | Description |
|------|-------|--------|---------|-------------|
| `Core/Menu/CopyClear/clearOps.h` | 69–80 | Modify | F1 | Add `CC_CLEAR_SCENE_RELOAD = 10` to `cc_clear_scene_sel_t` |
| `Core/Menu/CopyClear/clearOps.c` | 59–62 | Modify | F1 | Append `"reload scene"` to `ccClear_sceneLabels[]` |
| `Core/Menu/CopyClear/clearOps.c` | 94 | Modify | F1 | Bump `CC_MENU_CLEAR_SCENE` count from 10 to 11 |
| `Core/Menu/CopyClear/clearOps.c` | before 604 | Add | F1 | New `ccClear_runReloadScene()` static function |
| `Core/Menu/CopyClear/clearOps.c` | 650 | Add | F1 | New `case CC_CLEAR_SCENE_RELOAD:` dispatch in `ccClear_runJob()` |
| `Core/Hardware/frontPanel/buttonHandler.c` | 953–957 | Modify | F3 | Split SHIFT+SELECT STEP from VOICE; set track length |
| `Core/Hardware/frontPanel/ledHandler.c` | after 1408 | Add | F2 | New `led_updateSelectBarChaselight()` static function |
| `Core/Hardware/frontPanel/ledHandler.c` | 1511–1513 | Modify | F2 | Call chaselight from BEAT handler |
| `Core/Hardware/frontPanel/ledHandler.c` | 1518–1523 | Modify | F2 | Call chaselight from CHASE handler |

**Total new includes:** 0
**Total new globals/statics:** 0
**Estimated flash growth:** ~200–250 bytes (three small functions, one label string, two dispatch additions)
**RAM growth:** 0

---

## Build and Test Order

1. **Feature 3** — change `buttonHandler.c`. Build. Test: enter STEP mode,
   SHIFT+SELECT each button, confirm `PAR_TRACK_LENGTH` changes on the LCD
   and the sequencer wraps at the new length. Confirm SHIFT+SELECT in VOICE
   mode still selects bars (unchanged fallthrough). Confirm unshifted SELECT
   in STEP mode still selects bars.

2. **Feature 1** — change `clearOps.h` and `clearOps.c`. Build. Test: in PERF
   mode, hold SHIFT+COPY on a Scene button, scroll to "reload scene",
   release. Confirm the Scene reloads from the card and the display repaints
   without a page change. Test with a Scene that has no valid source (created
   from defaults, not loaded): confirm silent drop.

3. **Feature 2** — change `ledHandler.c`. Build. Test: enter STEP mode, start
   playback, observe the SELECT bar LED pulsing to tempo. Change the viewed
   bar and confirm the viewed bar stays steady while the playback bar pulses.
   Let playback reach a bar boundary and confirm the chaselight moves. Stop
   transport and confirm only the viewed-bar LED stays on. Test with a
   per-track length shorter than 128 and confirm the chaselight wraps within
   the track's active bars.

---

## Implementation Log

Status: **implemented, builds clean, image produced.** No source change was
made beyond the three features below.

### Feature 3 — SHIFT+SELECT sets pattern length (landed first)

- [x] `Core/Hardware/frontPanel/buttonHandler.c` — split
  `SELECT_MODE_STEP` out of the former STEP/VOICE fallthrough in
  `handleSelectButton()`. The comment block and body sit at **lines
  955–1002** (case label `SELECT_MODE_STEP` at 955, body at 987–997);
  `SELECT_MODE_VOICE` keeps the original `buttonHandler_selectBar()` at
  line 1003.
- The body uses `menu_getViewedPattern()` as the active Scene,
  `menu_getActiveVoice()` as the track, `pat_setTrackLength()`,
  `pat_applyTrackSettingsToMenu()`, and `menu_repaintAll()`, exactly as
  planned. The clamp fires only when `menu_currentBar >= selectNr + 1`.
- No new includes: `menu.h` and `PatternData.h` were already present
  (buttonHandler.c:21, :26).

### Feature 1 — "reload scene" in the PERF clear-scene menu (landed second)

- [x] `Core/Menu/CopyClear/clearOps.h` — added
  `CC_CLEAR_SCENE_RELOAD /* 10 */` to `cc_clear_scene_sel_t`
  (**lines 81–97**), with the enum comment block adjacent.
- [x] `Core/Menu/CopyClear/clearOps.c` — appended `"reload scene"` to
  `ccClear_sceneLabels[]` and extended the array comment (**lines 53–66**).
- [x] `Core/Menu/CopyClear/clearOps.c` — `CC_MENU_CLEAR_SCENE` count
  `10u -> 11u` (**line 98**).
- [x] `Core/Menu/CopyClear/clearOps.c` — new
  `ccClear_runReloadScene()` with its adjacent comment block, inserted
  immediately before `ccClear_runJob()` (**lines 608–648**).
- [x] `Core/Menu/CopyClear/clearOps.c` — dispatch
  `case CC_CLEAR_SCENE_RELOAD: return ccClear_runReloadScene(job);`
  (**line 701**).
- No new includes: `filesystem.h` and `presetManager.h` already present
  (clearOps.c:22, :19).

Verification performed beyond the build:

- `job->scene` is the Scene button index: `cc_clearObject(CC_KIND_SCENE,
  index, 0u, index)` stores it into `cc_source.scene` and
  `ccClear_requestClear()` copies it into the job
  (copyClearSession.c:499, :445, clearOps.c request-build).
- The early trigger-off helper returns immediately for a Scene selection
  that is not `ALL`/`PATTERN`/`NOTES`, so selection 10 changes no
  triggers (`ccClear_triggersOffNow()`).
- `copyClear_backgroundSuspended()` clears on button release plus service
  drain; the Preset Scene-load completion therefore runs after the
  copy/clear suspension is lifted, so its `autosave_markSceneWithPatternDirty()`
  is not swallowed.
- `menu_pollPresetStatus()` `PRESET_OP_SCENE_LOAD` confirmed at
  menu.c:12288: with `menu_activePage != LOAD_PAGE/SAVE_PAGE`,
  `reset_save = 0` and the runtime surface is applied in place
  (`menu_startSoundApply(1u, 0, ...)`).

### Feature 2 — bar chaselight on the SELECT row (landed third)

- [x] `Core/Hardware/frontPanel/ledHandler.c` — new static
  `led_updateSelectBarChaselight()` with its adjacent comment block,
  inserted after `led_setBeatPulse()` (**lines 1412–1495**, function body at
  1443). It writes the base state (viewed bar on, others off) with
  `led_setValue()`, then overlays the pulse from
  `seq_ledState.chaseStep / NUM_STEPS_PER_BAR` and
  `seq_ledState.beatPulse`, using the inverted rule when the playback bar
  equals the viewed bar.
- [x] `Core/Hardware/frontPanel/ledHandler.c` — `SEQ_LED_DIRTY_BEAT`
  handler now calls the helper under the `SELECT_MODE_STEP` guard
  (**call at line 1596**), with the adjacent comment block.
- [x] `Core/Hardware/frontPanel/ledHandler.c` — `SEQ_LED_DIRTY_CHASE`
  handler likewise (**call at line 1620**), after the existing
  `led_updateCurrentStep()` / `led_clearActive_step()` logic.
- No new includes: `sequencer.h`, `buttonHandler.h`, `menu.h`,
  `PatternData.h` already present (ledHandler.c:54, :45, :44, :46).

Design confirmation: no `LED_PART_SELECT*` write exists in
`Core/Menu/CopyClear/` or `Core/Menu/menu.c`, so the base-state rewrite in
the drain cannot clobber another owner's SELECT-row state in STEP mode. The
direct `led_setValue()` approach (rather than a new `LED_LAYER_CHASE`
layer) was chosen per the plan's own note; the drain runs after base writes.

### Build result (DEV config)

`make all` and `make img` both exit 0; no new compiler warnings in
`buttonHandler.c`, `ledHandler.c`, or `clearOps.c`.

| Metric | Value |
|--------|-------|
| `text` | 535,976 B |
| `data` | 416 B |
| `bss` | 426,744 B |
| Flash used | 536,392 / 753,664 B (headroom 217,272 B) |
| ITCM | 4,168 / 16,384 B |
| DTCM statics | 4,472 B |
| FX arena | 126,592 B at `0x20001180` (margin 3,712 B) |
| Image | `build/LXRV2_lxr02.img` 536,408 B, SHA-256 `a0db86ac…e285f` |

**RAM growth: 0.** No new globals or statics were added; the two new
functions are stack-only and the new label is `.rodata`/flash. This keeps
the change inside the "no new persistent allocation" statement of the plan,
so no RAM approval question arises.

### Notes / deviations from the plan text

- The plan quoted approximate line numbers (clearOps.c runJob at 604, dispatch
  at 650; buttonHandler.c 953–957; ledHandler.c 1408/1511/1518). Actual
  insertion points shifted by the surrounding S076 P1–P3 work; the real
  locations are the ones listed above. No behavioural deviation.
- Feature 3's body adds a short inline comment on the clamp that the plan
  did not spell out; it documents intent only.
- The helper's variable was named `viewedBar` (the plan used
  `viewedBar`/`playbackBar`); unchanged semantics.

### Still to verify on hardware (user)

- F3: SHIFT+SELECT 1..8 sets `PAR_TRACK_LENGTH` to 16/32/…/128 and the
  sequencer wraps at the new length; unshifted STEP SELECT and VOICE
  SHIFT+SELECT still select bars.
- F1: hold SHIFT+COPY on a Scene button, scroll to "reload scene", release —
  the Scene reloads from its source slot with an in-place repaint and no page
  change; a Scene with no valid numeric source drops silently.
- F2: in STEP mode during playback the SELECT-row chaselight pulses to
  tempo, moves at bar boundaries, inverts when playback and view coincide,
  and leaves only the viewed bar lit when stopped; a short per-track length
  keeps the pulse within the track's bars.

---

## Post-Implementation Assessment

Reviewed all nine modification sites against the plan (session 076 P4
continuation). Each change was read from the working tree and compared to the
plan's "replace with" / "insert" blocks.

### Feature 3 — SHIFT+SELECT sets pattern length

| Site | Plan | Actual | Verdict |
|------|------|--------|---------|
| `buttonHandler.c` SELECT_MODE_STEP case split | lines 953–957 | lines 952–1005 | **Match.** SELECT_MODE_STEP has its own case with the comment block and body; SELECT_MODE_VOICE retains `buttonHandler_selectBar()` at line 1003. |
| Body logic | newLen, scene, track, `pat_setTrackLength`, clamp, `pat_applyTrackSettingsToMenu`, `menu_repaintAll` | Identical sequence at lines 987–999 | **Match.** One inline comment added on the clamp (line 993–994), not in plan — cosmetic only, documents intent. |

**No concerns.** The fallthrough is cleanly broken. VOICE SHIFT+SELECT
behaviour preserved.

### Feature 1 — "reload scene" in the clear-scene menu

| Site | Plan | Actual | Verdict |
|------|------|--------|---------|
| `clearOps.h` enum | Add `CC_CLEAR_SCENE_RELOAD = 10` after `…FX_MORPH` | Lines 79–97: trailing comma on `…FX_MORPH`, comment block, `CC_CLEAR_SCENE_RELOAD /* 10 */` | **Match.** |
| `clearOps.c` labels | Append `"reload scene"` | Line 65: `"reload scene"` at end of array | **Match.** Label comment block expanded to mention all three added selections (morph, fx morph, reload) — cosmetic expansion, correct. |
| `clearOps.c` count | `10u` → `11u` | Line 98: `return 11u;` | **Match.** |
| `clearOps.c` `ccClear_runReloadScene()` | New static before `ccClear_runJob()` | Lines 608–652: comment block + function body | **Match.** `filesystem_identityRow` → `filesystem_residentSource` masked with `FS_RESIDENT_SOURCE_VALUE_MASK` → `preset_loadSceneForScenes` if ≤ 999 → `CC_RUN_DONE`. Identical to plan. |
| `clearOps.c` dispatch | New case before `default:` | Line 701: `case CC_CLEAR_SCENE_RELOAD: return ccClear_runReloadScene(job);` | **Match.** |

**No concerns.** The `ccClear_triggersOffNow()` path was confirmed safe for
selection 10 (returns at the non-ALL/PATTERN/NOTES guard). The Preset
completion lifecycle proceeds after the copy/clear button-release suspension
lifts, so `autosave_markSceneWithPatternDirty()` is not swallowed.

### Feature 2 — bar chaselight on SELECT LEDs

| Site | Plan | Actual | Verdict |
|------|------|--------|---------|
| `ledHandler.c` new function | `led_updateSelectBarChaselight()` after `led_setBeatPulse()` | Lines 1411–1477: comment block + function body | **Match.** Base-state loop, `seq_isRunning()` guard, `playbackBar` from `chaseStep / NUM_STEPS_PER_BAR`, `NUM_BARS` bounds check, inverted-pulse rule when `playbackBar == viewedBar`, normal pulse otherwise. Identical logic. |
| `ledHandler.c` BEAT handler | Wrap in braces, add chaselight call under `SELECT_MODE_STEP` guard | Lines 1580–1597: brace block, `led_setBeatPulse()` first, then comment block + `buttonHandler_getMode() == SELECT_MODE_STEP` guard → call | **Match.** BEAT handler comment correctly says "CHASE handler below" (BEAT at 1580, CHASE at 1602). |
| `ledHandler.c` CHASE handler | Add chaselight call after existing step-chase logic | Lines 1602–1621: existing `led_updateCurrentStep` / `led_clearActive_step` preserved, then comment block + same guard → call | **Match.** |

**No concerns.** The `led_setValue()` approach (direct base-state writes rather
than a new `LED_LAYER_CHASE` layer) is correct: the dirty-flag drain runs in
foreground after all button/menu base-state writes, so the chaselight overlay
is the final word on SELECT LED state during STEP mode playback.

### Cross-feature interactions

- **F3 → F2**: a SHIFT+SELECT length change that shortens the track causes
  `seq_stepIndex` to wrap within the new length, so `chaseStep` stays within
  bounds and the derived `playbackBar` cannot exceed the track's bar range.
  Verified by the `playbackBar >= NUM_BARS` guard (line 1460).
- **F1 → F2**: a "reload scene" replaces Pattern data mid-playback; the
  sequencer realigns and the chaselight follows the new step position
  naturally via the existing CHASE dirty bit.
- **F3 → F1**: independent; no interaction.

### Verdict

All nine changes match the plan. No missing changes, no extraneous
modifications, no behavioural deviations. The three features are ready for
hardware verification.
