# PAT_SUPPLEMENTARY_FEATURES_AUDIT.md
# Audit: Morph Voice Mode, Voice Preview, Per-Track Shuffle

## Goal

Add three small but connected interaction/storage features after the STEP front
page work:

1. `SHIFT+VOICE` mode becomes a morph-parameter view/edit mode for all voice
   pages and subpages.
2. Pressing the already-selected VOICE/track button previews that voice when
   the sequencer is stopped.
3. Shuffle becomes a per-track Pattern setting, shown on STEP front-page
   subpage 1 and applied independently per track during playback.

This document is an implementation audit only. No firmware behavior changes are
part of this pass.

## Current State Summary

Relevant current ownership:

- `Core/Menu/menu.c/h`
  - Owns `parameter_values[]`, `parameters2[]`, `menu_activePage`,
    `menu_activeVoice`, `menuIndex`, display repaint, encoder edits, and knob
    edits.
  - `menu_repaintGeneric()`, `menu_encoderChangeParameter()`, and
    `menu_parseKnobDelta()` currently read/write `parameter_values[]`
    directly.
  - `parameters2[]` is the loaded morph buffer, but there is no UI flag that
    makes normal voice pages display/edit it.
- `Core/Hardware/frontPanel/buttonHandler.c/h`
  - Owns mode button gestures, physical VOICE button handling, `bh_state`,
    SHIFT state, and active LED mode.
  - Current `SHIFT` press while `SELECT_MODE_VOICE` calls
    `buttonHandler_enterSeqMode()`, which temporarily enters STEP behavior.
    This must be replaced.
  - `handleVoiceButton()` updates active voice/track and mode-specific UI.
- `Core/Hardware/frontPanel/ledHandler.c/h`
  - Owns active/blinking LED state. `led_setBlinkLed()` is the existing blink
    API. `led_setMode2()` writes the mode LEDs.
- `Core/Scene/Preset/presetManager.c/h`
  - `preset_applySoundParameter()` applies one active kit value and writes
    `parameter_values[]`.
  - `preset_morph()` / `preset_morphTick()` interpolate active kit values from
    `parameter_values[]` to morph-kit values in `parameters2[]`.
  - There is no direct public helper today for "write morph endpoint only".
- `Core/Scene/Pattern/PatternData.c/h`
  - `LengthRotate` currently stores length, rotation, scale, MIDI channel, and
    MIDI note, and shuffle per pattern track.
  - The temporary per-pattern shuffle backing was removed; shuffle now lives in
    the per-track PatternData settings record.
- `Core/Sequencer/sequencer.c/h`
  - Sequencer no longer owns a single global runtime shuffle value.
  - `seq_calcDeltaT()` leaves the 96 PPQ transport tick delta unshuffled.
  - Scaled per-track event timing and per-track shuffle are handled in
    `seq_dueTrackEvents()` and `seq_processSchedulerTick()`.
- `Core/Hardware/SD/filesystem.c`
  - Track length/settings extension already streams `LengthRotate` fields.
  - Session 031 follow-up removed the transitional legacy one-byte shuffle
    import/export path; final migration will be handled by external converters
    after Phase 2 storage settles.

## Feature 1: SHIFT+VOICE Morph Parameter Mode

### Required Behavior

- `SHIFT+MODE_VOICE` enters a persistent voice-mode variant: morph parameter
  view/edit mode.
- This replaces any current `SHIFT+MODE_VOICE` behavior. The current temporary
  STEP-mode behavior on SHIFT press in voice mode must go away.
- Morph voice mode should behave like normal voice mode:
  - same VOICE pages,
  - same SELECT subpages,
  - same encoder and knob navigation,
  - same displayed labels and dtypes,
  - same active voice selection behavior.
- The difference is only the parameter value source/destination:
  - normal voice mode displays/edits `parameter_values[param]`;
  - morph voice mode displays/edits `parameters2[param]` for sound parameters.
- Morph values must be editable in this mode. Encoder edits and endless-pot
  edits must write the morph endpoint value in `parameters2[]`; this is not a
  read-only inspection page.
- Add a Menu flag named `voiceModeShowMorph`.
  - When true, refresh/display uses the morph parameter value instead of the
    standard voice parameter value.
  - When false, refresh/display uses the existing standard value.
- Whenever in morph voice mode, the VOICE mode LED must blink continuously.
  - This should be true while the mode is active, not just while SHIFT is held.
- Leaving morph voice mode should restore normal VOICE LED behavior and set
  `voiceModeShowMorph = 0`.

### Open Design Decisions To Confirm Before Code

- Morph edits should most likely write only `parameters2[]`, not immediately
  apply to DSP. Audible preview of the morph endpoint should still happen via
  the existing `PAR_MORPH` control and `preset_morphTick()`.
- If direct audition of the morph endpoint is wanted later, it should be a
  separate command because applying morph endpoint edits directly to DSP would
  temporarily move the active kit away from `parameter_values[]`.
- Morph mode should probably be cleared by any non-VOICE mode button press.
  The audit assumes this because the flag is named `voiceModeShowMorph`, not
  "global alternate parameter buffer".

### Code Changes Needed

#### `Core/Menu/menu.h`

Add:

```c
extern uint8_t voiceModeShowMorph;
uint8_t menu_paramUsesMorphView(uint16_t paramNr);
uint8_t menu_getParameterDisplayValue(uint16_t paramNr);
uint8_t *menu_getParameterEditPtr(uint16_t paramNr);
```

Comment to add with the flag:

```c
/*
 * Voice-page morph endpoint display flag.
 *
 * Why: SHIFT+VOICE needs to reuse the existing voice pages while looking at
 * the morph kit endpoint buffer instead of the active kit buffer. Menu owns
 * this because it owns the LCD value source and edit destination; Preset owns
 * applying active sound values to DSP.
 *
 * Input/source: buttonHandler toggles this when entering/leaving morph voice
 * mode. Output/effect: menu repaint and edit helpers choose parameters2[] for
 * sound parameters while the flag is set. Risk: Pattern, global, STEP, load,
 * save, and performance parameters must never read from parameters2[].
 */
extern uint8_t voiceModeShowMorph;
```

#### `Core/Menu/menu.c`

Add the global:

```c
uint8_t voiceModeShowMorph = 0;
```

Add helpers near the other menu accessors or repaint helpers:

```c
uint8_t menu_paramUsesMorphView(uint16_t paramNr);
uint8_t menu_getParameterDisplayValue(uint16_t paramNr);
uint8_t *menu_getParameterEditPtr(uint16_t paramNr);
```

Required helper semantics:

- `menu_paramUsesMorphView(paramNr)` returns nonzero only when:
  - `voiceModeShowMorph != 0`,
  - `menu_activePage` is one of `VOICE1_PAGE..VOICE7_PAGE`,
  - `paramNr < END_OF_SOUND_PARAMETERS`,
  - `paramNr != PAR_NONE`,
  - `paramNr != 127u` if keeping the same forbidden sound-param guard,
  - the parameter is not a runtime/global/pattern-only alias.
- `menu_getParameterDisplayValue(paramNr)` returns `parameters2[paramNr]` when
  morph view applies, otherwise `parameter_values[paramNr]`.
- `menu_getParameterEditPtr(paramNr)` returns a pointer into `parameters2[]`
  when morph view applies, otherwise into `parameter_values[]`.

Comment to add with the helpers:

```c
/*
 * Resolve the visible/editable parameter buffer for voice-page UI.
 *
 * Why: morph voice mode must reuse the normal voice menu table but point at
 * the morph endpoint buffer. Centralizing the decision prevents one display
 * path from reading parameter_values[] while another edit path writes
 * parameters2[].
 *
 * Inputs: canonical ParameterArray id from menuPages. Outputs: either a value
 * or mutable pointer for the currently active UI buffer. Clients:
 * menu_repaintGeneric(), menu_encoderChangeParameter(), menu_parseKnobDelta(),
 * menu_notifyExternalParamChanged(), and menu_updateEndlessPotScales() if
 * needed. Risk: only sound parameters on voice pages may use parameters2[];
 * track/pattern/global/runtime params must stay in parameter_values[].
 */
```

Update value reads in `menu_repaintGeneric()`:

- Edit mode:
  - replace `curParmVal = parameter_values[parNr];` with
    `menu_getParameterDisplayValue(parNr)`.
- Normal mode:
  - replace visible value reads with `menu_getParameterDisplayValue(parNr)`.

Update edit writes in `menu_encoderChangeParameter()`:

- Replace `uint8_t *paramValue = &parameter_values[paramNr];` with
  `uint8_t *paramValue = menu_getParameterEditPtr(paramNr);`.
- For dtype clauses that reference linked parameter values, keep normal
  `parameter_values[]` where the linked parameter is a routing selector that
  should remain the active kit value unless morph editing of that linked field
  is also intentional. For example:
  - `DTYPE_TARGET_SELECTION_LFO` currently reads
    `parameter_values[PAR_VOICE_LFO1 + ...]`.
  - In morph mode, this should probably use `menu_getParameterDisplayValue()` so
    the morph endpoint's selected LFO voice constrains its target list. This
    needs careful testing because LFO routing parameters are skipped by morph
    interpolation today.
- When morph mode writes `parameters2[]`, do not call
  `preset_applySoundParameter()` directly.
- Add a menu send wrapper:

```c
static void menu_sendEditedParameter(uint16_t paramNr, uint8_t value)
```

Semantics:

- If `menu_paramUsesMorphView(paramNr)`:
  - write `parameters2[paramNr] = value`;
  - optionally call `preset_morph(parameter_values[PAR_MORPH])` so the audible
    interpolated state updates when the current morph amount is nonzero;
  - do not record automation from a morph endpoint edit.
- Else:
  - use existing `menu_sendSoundParameter()` behavior.

Comment to add:

```c
/*
 * Commit one edited menu value to the correct owner.
 *
 * Why: active-kit edits and morph-endpoint edits share the same voice page UI
 * but have different side effects. Active edits must update parameter_values[],
 * apply to DSP, and optionally record automation. Morph endpoint edits should
 * update parameters2[] and refresh the current morph interpolation without
 * rewriting the active kit value.
 *
 * Inputs: paramNr/value from encoder or endless pot edit. Outputs: active
 * parameter apply or morph endpoint write. Clients: menu_encoderChangeParameter()
 * and menu_parseKnobDelta(). Risk: calling preset_applySoundParameter() for
 * morph endpoint edits would make the active kit and morph endpoint collapse
 * together, defeating morph.
 */
```

Update `menu_parseKnobDelta()` the same way:

- Resolve `pv` through `menu_getParameterEditPtr()`.
- Use `menu_getParameterDisplayValue()` for linked clamp reads.
- Commit through `menu_sendEditedParameter()`.

Update `menu_notifyExternalParamChanged()`:

- If morph mode is visible and an external morph-load/edit path changes
  `parameters2[]`, visible params should request repaint. For the first pass,
  it is enough that direct UI edits mark `menu_knobs_dirty`; external CCs
  should continue to target active kit values only.

Update `menu_switchPage()`:

- When entering non-voice pages, set `voiceModeShowMorph = 0`.
- When entering a voice page normally, do not implicitly clear it if the caller
  is intentionally in morph voice mode. The clean approach is a small setter:

```c
void menu_setVoiceModeShowMorph(uint8_t on);
```

That setter can own repaint/mapping dirty work.

#### `Core/Hardware/frontPanel/buttonHandler.h`

No new public mode value is strictly required, but one local flag in
`buttonHandler.c` is recommended:

```c
static uint8_t buttonHandler_morphVoiceModeActive;
```

Keep `SELECT_MODE_VOICE` as the visible mode. Morph mode is an overlay flag,
not a new SELECT mode, because SELECT buttons should still mean voice subpages.

#### `Core/Hardware/frontPanel/buttonHandler.c`

Change `handleModeButtons()`:

- Current mode selection:
  - `SHIFT+MODE1` maps to `(mode + 4) & 0x07`, so `SHIFT+MODE_VOICE` currently
    becomes `SELECT_MODE_PAT_GEN`.
  - This arithmetic must be special-cased before the generic shifted mapping.
- If `buttonHandler_getShift()` and `mode == SELECT_MODE_VOICE`:
  - set `bh_state.selectButtonMode = SELECT_MODE_VOICE`;
  - set `buttonHandler_morphVoiceModeActive = 1`;
  - call `menu_setVoiceModeShowMorph(1)`;
  - switch to the current voice page;
  - call `led_setMode2(SELECT_MODE_VOICE)`;
  - call `led_setBlinkLed(LED_MODE1, 1)` or the actual VOICE mode LED constant
    used by `led_setMode2()` for voice mode;
  - keep active voice LED lit as usual;
  - return before the shifted arithmetic path.

Comment to add:

```c
/*
 * Enter morph voice mode as a VOICE-mode overlay.
 *
 * Why: SHIFT+VOICE should not select one of the old shifted mode pages. It
 * keeps SELECT_MODE_VOICE semantics so VOICE buttons select tracks and SELECT
 * buttons select voice subpages, while Menu reads/writes the morph endpoint
 * buffer. Output: Menu's voiceModeShowMorph flag is set and the VOICE mode LED
 * blinks for persistent feedback.
 *
 * Risk: this is intentionally not a separate selectButtonMode. Adding another
 * mode value would force every SELECT/VOICE/LED branch to learn a duplicate of
 * voice mode and would make the overlay harder to leave cleanly.
 */
```

Change `processPress(BUT_SHIFT)`:

- Current behavior in `SELECT_MODE_VOICE` calls `buttonHandler_enterSeqMode()`.
- Replace this branch with no STEP-mode entry.
- If morph voice mode is already active, pressing SHIFT alone should not exit
  it unless we explicitly want SHIFT as a momentary alternate. The requested
  behavior says `SHIFT+VOICE mode`, not "hold SHIFT", so treat it as persistent
  mode entered by `SHIFT+MODE_VOICE`, not by holding SHIFT.

Comment to add:

```c
/*
 * SHIFT press no longer creates a transient STEP overlay while in VOICE mode.
 *
 * Why: SHIFT+VOICE is now reserved for persistent morph voice mode. Holding
 * SHIFT during ordinary voice mode should not steal the UI into STEP mode,
 * because that would conflict with editing and previewing morph endpoints.
 */
```

Change mode exit paths:

- On non-VOICE mode buttons, call `menu_setVoiceModeShowMorph(0)` and
  `buttonHandler_morphVoiceModeActive = 0`.
- Stop VOICE mode LED blinking when leaving morph mode:
  - `led_setBlinkLed(LED_MODE1, 0)` or an appropriate helper.
  - Then call `led_setMode2(newMode)` to restore base mode LEDs.

Change `handleVoiceButton()`:

- In morph voice mode, keep the same active-voice/subpage behavior.
- Voice preview behavior from Feature 2 must still apply.
- If active voice changes, repaint the current voice page using morph values.

#### `Core/Hardware/frontPanel/ledHandler.c/h`

No new LED system is required. Use existing blink layer:

- `led_setBlinkLed(VOICE mode LED, 1)` on morph voice mode entry.
- `led_setBlinkLed(VOICE mode LED, 0)` on exit.

If there is no symbolic constant for "VOICE mode LED", add a small helper:

```c
void led_setVoiceModeBlink(uint8_t on);
```

Comment to add if adding the helper:

```c
/*
 * Blink the VOICE mode LED while morph voice mode is active.
 *
 * Why: buttonHandler should not depend on the physical LED number that
 * represents SELECT_MODE_VOICE inside led_setMode2(). This helper keeps the
 * mode-row mapping inside ledHandler while allowing a persistent overlay
 * indicator. Input: on/off. Output: existing blink layer changes only for the
 * VOICE mode LED.
 */
```

### Save/Load Implications

- No new file format is required for morph voice mode. It edits `parameters2[]`,
  which is already the morph-kit buffer used by morph load/save.
- Confirm that save morph kit writes `parameters2[]` and save normal kit writes
  `parameter_values[]`.
- If a morph endpoint edit calls `preset_morph(parameter_values[PAR_MORPH])`,
  it should not mark active kit dirty unless the project later adds dirty flags.

### Validation

- Enter normal VOICE mode, edit a parameter, confirm active kit changes.
- Enter `SHIFT+MODE_VOICE`, confirm:
  - same page/subpage layout,
  - values are from `parameters2[]`,
  - VOICE mode LED blinks continuously,
  - SELECT buttons still change subpages,
  - VOICE buttons still change active track,
  - leaving to STEP/PERF/LOAD/GLOBALS clears morph view and blink.
- Load a morph kit, enter morph voice mode, verify loaded morph values display.
- Edit morph value with encoder and endless pots, save morph kit, reload it.
- With `PAR_MORPH` nonzero, edit a morph endpoint and confirm interpolation
  refreshes without overwriting the active endpoint value.

## Feature 2: Voice Preview On Re-Press

### Required Behavior

- If the sequencer is stopped, pressing the VOICE/track button of the currently
  selected voice/track triggers that voice.
- Applies in any mode.
- It triggers only when the pressed voice equals `menu_getActiveVoice()`.
- If the sequencer is running, pressing the currently selected voice button
  should not preview.
- This should coexist with normal voice/track selection and STEP front-page
  toggling.

### Code Changes Needed

#### `Core/Sequencer/sequencer.h`

Add one or both helpers:

```c
uint8_t seq_isRunning(void);
void seq_previewVoice(uint8_t voice);
```

`seq_isRunning()` may already exist in `sequencer.c`; if not, add it as a
small public read of `seq_running`.

Comment for `seq_previewVoice()`:

```c
/*
 * Trigger one voice as a stopped-transport preview.
 *
 * Why: front-panel VOICE buttons need an audition path that does not advance
 * sequencer state or write pattern data. Sequencer already owns voice
 * triggering, MIDI note/channel lookup, trigger outputs, and mute interaction,
 * so the preview should stay here instead of duplicating voiceControl calls in
 * buttonHandler.
 *
 * Input: voice is the UI track/voice index. Output: the synth voice and MIDI
 * note path are triggered with a safe preview velocity/note. Risk: must not
 * mutate seq_stepIndex[], seq_trackEventCount[], recording, probability RNG, or
 * automation.
 */
```

#### `Core/Sequencer/sequencer.c`

Implement `seq_previewVoice(uint8_t voice)`.

Design choices:

- Use `seq_triggerVoice(voice, PREVIEW_VOLUME, note)` if it does not require a
  valid/current `seq_stepIndex[voice]`.
- Current `seq_triggerVoice()` reads step data only when
  `pat_readStep(seq_activePattern, voiceNr, seq_stepIndex[voiceNr], &stepData)`
  succeeds. If stopped, `seq_stepIndex[]` may be `-1`; passing that as `uint8_t`
  becomes invalid and the step read safely fails. Then the supplied note/volume
  are used.
- Preview note should use the PatternData track MIDI note if set, otherwise
  `PAT_DEFAULT_NOTE`.
- Preview volume should likely use a constant such as `PAT_PREVIEW_VOLUME 100`
  or reuse the default step velocity.
- Muted tracks: decide whether preview respects mute. The audit recommends
  ignoring sequencer mute for direct preview only if user expects audition even
  while muted; otherwise use existing `seq_triggerVoice()` mute behavior if it
  checks mute upstream. Need inspect current trigger call path before final code.

#### `Core/Hardware/frontPanel/buttonHandler.c`

Change `handleVoiceButton(uint8_t voiceNr)`:

- At the top, before muting/copy/perf special paths mutate state:

```c
uint8_t wasSelectedVoice = (uint8_t)(voiceNr == menu_getActiveVoice());
uint8_t shouldPreview = (uint8_t)(wasSelectedVoice && !seq_isRunning());
```

- After any mode-specific action that should still happen, call preview if
  `shouldPreview`.
- Do not preview when the button press is being used as:
  - mute toggle,
  - copy source/destination,
  - clear action,
  - PERF "clear mutes up to voice" action,
  unless the user explicitly wants preview there. The request says any mode, but
  destructive/modified gestures should probably be excluded.
- For plain mode VOICE presses, preview should happen whether or not the mode
  is VOICE, STEP, EUKLID, SOM, MENU, etc., as long as no shift/copy/mute
  behavior consumed the event.
- In STEP mode, repeated press of the selected voice will also toggle the STEP
  front-page subpage for per-track shuffle. Preview should still fire if
  stopped.

Comment to add:

```c
/*
 * Stopped-transport voice preview.
 *
 * Why: selecting a different voice changes UI context, but re-pressing the
 * already selected voice is an audition gesture when playback is stopped. The
 * preview is requested before active voice changes so it only fires for a true
 * re-press, then Sequencer owns the actual trigger path.
 *
 * Inputs: current Menu active voice, pressed voice, and Sequencer running
 * state. Output: one preview trigger for unmodified/non-copy voice presses.
 * Risk: do not fire preview for copy/mute/clear gestures because those use the
 * same physical row for editing commands.
 */
```

### Validation

- Stop sequencer, press selected VOICE1: voice triggers.
- Stop sequencer, press different VOICE2: active track changes but no preview
  on first press; pressing VOICE2 again previews.
- Running sequencer, press selected voice: no preview.
- Verify preview works in VOICE, STEP, PERF if not consumed by mute logic,
  EUKLID, SOM, MENU/global, and morph voice mode.
- Verify preview does not write steps, automation, pattern values, or transport
  counters.

## Feature 3: Per-Track Shuffle

### Required Behavior

- Shuffle becomes a per-track Pattern setting.
- It is no longer a global sequencer coefficient.
- In the menu, shuffle lives on subpage 1 of the STEP mode front page.
- STEP front page:
  - subpage 0 remains length, scale, MIDI channel, MIDI note.
  - subpage 1 contains per-track shuffle.
- Subpage 1 can be reached:
  - by multiple presses of the VOICE/track button in STEP mode, toggling like
    the PERF menu/subpage behavior,
  - by scrolling with the encoder.
- Pressing the selected VOICE/track button in STEP mode should also trigger
  voice preview if the sequencer is stopped.
- Sequencer timing must apply shuffle per track.

### Storage Location

Per-track shuffle belongs in PatternData with the other per-track Pattern
settings. The intended destination is one PatternData-owned per-track settings
record. This pass should add shuffle to that record; it should not fan out new
parallel arrays or create a second "shuffle settings" owner.

Preferred structure change:

```c
typedef struct {
    uint8_t length;
    uint8_t rotate;
    uint8_t scale;
    uint8_t midiChannel;
    uint8_t midiNote;
    uint8_t shuffle;
} LengthRotate;
```

The name `LengthRotate` is already historical and overloaded; do not rename it
in this pass unless the implementation chooses to introduce a clearer single
struct name at the same storage site. Add comments making it clear this single
record carries the STEP front-page per-track settings. The broader cleanup that
moves/finalizes all pattern settings into their long-term Phase 3 shape belongs
to `SCOPING_TARGETS.md`, not this feature pass.

Comment to add:

```c
/*
 * Per-track Pattern settings shown by the STEP front page.
 *
 * Why: the bridge keeps one PatternData-owned per-track settings record for
 * the STEP front page. The historical LengthRotate name may remain for
 * file-format and call-site stability, but the record now owns length,
 * rotation, scale, MIDI output, note, and shuffle. Shuffle is stored here
 * because it changes playback timing per track and must travel with Pattern
 * data rather than Sequencer globals.
 *
 * Risk: this type is serialized by pattern/all/performance files. Adding fields
 * requires updating the optional settings-extension stream and legacy fallback
 * defaults.
 */
```

### Code Changes Needed

#### `Core/Scene/Preset/ParameterArray.h`

Keep `PAR_SHUFFLE` as the public menu parameter unless there is a strong reason
to create `PAR_TRACK_SHUFFLE`.

Rationale:

- `PAR_SHUFFLE` is already a menu/global-ish parameter used by PERF and STEP.
- The same parameter ID can be interpreted by `menu_parseGlobalParam()` as a
  PatternData per-track edit when shown on the STEP page.
- If PERF should later keep a global/performance shuffle, split then into
  `PAR_PERFORMANCE_SHUFFLE` and `PAR_TRACK_SHUFFLE`.

Audit recommendation:

- Do not create a new ParameterArray enum in this pass.
- Move the `PAR_SHUFFLE` menu placement from `PERFORMANCE_PAGE` to STEP subpage
  1 if PERF should no longer expose global shuffle.
- If PERF must keep a future global shuffle, leave a placeholder but do not wire
  it to the per-track value.

#### `Core/Menu/menuPages.h`

Update `SEQ_PAGE`:

```c
/* SEQ_PAGE */ {
  {TEXT_PAT_LENGTH,TEXT_TRACK_SCALE,TEXT_MIDI_CHANNEL,TEXT_NOTE,
   TEXT_SHUFFLE,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
   PAR_TRACK_LENGTH,PAR_TRACK_SCALE,PAR_TRACK_MIDI_CHAN,PAR_TRACK_MIDI_NOTE,
   PAR_SHUFFLE,PAR_NONE,PAR_NONE,PAR_NONE},
  ...
}
```

This uses the existing "second half of same subpage" behavior:

- active parameters 0-3 show the front page;
- active parameters 4-7 show the second half, which contains shuffle.

Important: `has2ndPage(0)` returns true only if `top5 != TEXT_EMPTY`, so
putting shuffle in slot 5 is enough for encoder and repeated subpage toggles.

If the STEP per-step editor must remain subpage 1, then the front-page second
half should be on subpage 0 activeParameter 4, and the current step editor stays
as actual `menuPages[SEQ_PAGE][1]`.

#### `Core/Menu/menu.c`

Update `menu_parseGlobalParam(PAR_SHUFFLE)`:

Current behavior calls `pat_setShuffle(menu_getViewedPattern(), value)` and
bridges to global sequencer shuffle.

New behavior should call:

```c
pat_setTrackShuffle(menu_getViewedPattern(), menu_getActiveVoice(), value);
```

Comment:

```c
/*
 * Shuffle is a per-track Pattern timing setting.
 *
 * Menu supplies the viewed pattern and active track because the STEP front-page
 * second half edits the track in front of the user. PatternData stores the
 * value and Sequencer queries it per track when scheduling. This replaces the
 * old global seq_shuffle bridge.
 */
```

Update `menu_switchPage(SEQ_PAGE)`:

- Existing pre-repaint `pat_applyTrackSettingsToMenu()` will now refresh
  `PAR_SHUFFLE` from the active track, so no new call is needed.

Update `menu_moveToMenuItem()`:

- Current non-MIDI pages return when activeParameter would cross from 7 to next
  subpage, and only toggle between parameter groups by subpage button/repeated
  subpage selection.
- Encoder scrolling requirement says the STEP second page can be reached by
  scrolling with the encoder.
- Because STEP front-page second half is slot 4 on the same subpage, current
  encoder movement from activeParameter 3 to 4 already works and locks to the
  second half. Verify after menu table change.
- If entering from activeParameter 0 and rotating counterclockwise should wrap
  to shuffle, change only for `menu_activePage == SEQ_PAGE` and activePage `0`.
  This is optional unless the user's "scrolling with encoder" means wrap both
  directions.

#### `Core/Hardware/frontPanel/buttonHandler.c`

Update `handleVoiceButton()` for STEP mode:

- If `bh_state.selectButtonMode == SELECT_MODE_STEP` and `voiceNr` is already
  active:
  - call `menu_switchSubPage(0)` or an explicit helper that toggles between
    activeParameter 0 and 4 on `SEQ_PAGE` subpage 0;
  - repaint;
  - still fire voice preview if stopped.
- If `voiceNr` changes:
  - set active voice,
  - force STEP front-page first half by resetting activeParameter to 0,
  - refresh track settings and repaint.

Potential new helper in Menu:

```c
void menu_toggleStepTrackSettingsHalf(void);
void menu_showStepTrackSettingsFirstHalf(void);
```

This is cleaner than having buttonHandler manipulate `menuIndex`.

Comments:

```c
/*
 * Toggle the STEP track-settings front page between its first and second half.
 *
 * Why: in STEP mode, repeated presses of the selected VOICE button should act
 * like a front-page page toggle, while selecting a different VOICE should show
 * the first half for that newly selected track. Menu owns menuIndex and the
 * Page table, so buttonHandler asks Menu to change the visible half instead of
 * editing index bits directly.
 *
 * Output: activeParameter moves between 0 and 4 on SEQ_PAGE subpage 0 and the
 * endless-pot mapping is refreshed. Risk: this helper must not switch to the
 * per-step editor subpage; STEP1..16 selection still owns that transition.
 */
```

#### `Core/Scene/Pattern/PatternData.h`

Add prototypes:

```c
void pat_setTrackShuffle(uint8_t pattern, uint8_t track, uint8_t shuffle);
uint8_t pat_getTrackShuffle(uint8_t pattern, uint8_t track);
```

Remove/adapt:

- `pat_setShuffle(uint8_t pattern, uint8_t value)`
- `pat_getShuffle(uint8_t pattern)`

Recommended migration:

- Replace `pat_shuffleValue[NUM_PATTERN]` with per-track storage in
  `LengthRotate.shuffle`.
- Do not keep a legacy all-track shuffle import helper; the old single shuffle
  byte is ignored in Phase 2.
- Either remove `pat_setShuffle()` / `pat_getShuffle()` or keep wrappers that
  target `menu_getActiveVoice()` only if call sites need a smaller diff.
  Prefer replacing call sites and deleting wrappers if compile fallout is small.

Update `pat_clearTrack()`:

- Set `shuffle = 0`.

Update `pat_applyTrackSettingsToMenu()`:

- Replace `parameter_values[PAR_SHUFFLE] = pat_getShuffle(pattern);`
  with `pat_getTrackShuffle(pattern, track)`.

#### `Core/Scene/Pattern/PatternData.c`

Implement per-track shuffle accessors.

`pat_setTrackShuffle()`:

- validate pattern/track,
- clamp value to `0..127`,
- store in `LengthRotate.shuffle`,
- mirror to `parameter_values[PAR_SHUFFLE]`.

`pat_getTrackShuffle()`:

- return stored `0..127`,
- invalid coordinates return `0`.

Comment:

```c
/*
 * Store one track's shuffle amount.
 *
 * Why: shuffle affects timing and must follow the Pattern track, not the
 * transport. Menu edits the currently viewed track; Sequencer reads this value
 * when scheduling that track's next event.
 *
 * Inputs: pattern/track plus 0..127 menu value. Outputs: PatternData storage
 * and PAR_SHUFFLE UI mirror update. Risk: this function must not forward to a
 * transport-global shuffle path; playback should query the track value
 * directly so tracks can differ.
 */
```

#### `Core/Sequencer/sequencer.h`

Remove the old global shuffle setter entirely. If a future performance/global
shuffle is added, give it a new owner and do not reuse the removed single-track
bridge.

#### `Core/Sequencer/sequencer.c`

Remove global runtime shuffle state:

- `float seq_shuffle`
- `float seq_lastShuffle`
- possibly global `seq_shuffleTable` remains useful.

Per-track shuffle requires per-track last correction state:

```c
static float seq_lastTrackShuffle[NUM_TRACKS];
```

But the current PPQ scheduler applies shuffle to the global tick delta before
per-track due events. That cannot create independent track shuffle. The
scheduling change should happen in the per-track due-event path.

Preferred implementation:

- Keep `seq_calcDeltaT()` as pure 96 PPQ tick duration with no shuffle.
- Apply per-track shuffle inside the "is event due for this track" calculation,
  not by mutating transport tick duration.
- Current scaled scheduler uses:
  - `seq_elapsedPpqTicks`
  - `SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP`
  - `pat_getTrackScaleRatio()`
  - `seq_dueTrackEvents(track)`
- Modify `seq_dueTrackEvents(track)` so the effective due threshold for each
  track's next event includes a shuffle offset based on:
  - the track's event count or default-step phase,
  - `pat_getTrackShuffle(seq_activePattern, track)`,
  - the existing `seq_shuffleTable[]`.

Important drift rule:

- Shuffle should move event timing around the grid without changing long-term
  track speed or event count.
- The per-track scheduler should never accumulate a global `seq_lastShuffle`
  correction. Instead, compute the desired offset for event `n` from absolute
  event/PPQ position and compare against elapsed ticks. This matches the recent
  "recalculate each 128-step loop to avoid drift" approach for scaling.

Possible integer strategy:

- Represent shuffle offset in 96 PPQ ticks or fixed-point sub-ticks.
- For the next event number `eventIndex = seq_trackEventCount[track]`, compute:
  - unshuffled due PPQ tick for that event from scale ratio,
  - phase in half-beat or 16-step shuffle table,
  - positive/negative offset from `seq_shuffleTable[phase] * shuffle`,
  - due when `seq_elapsedPpqTicks >= unshuffled_due + offset - previous_offset`.
- Avoid floats in the 4 kHz scheduler if feasible. Existing code uses floats,
  but per-track and per-event fixed-point would be safer.

Minimum change strategy:

- Keep `seq_shuffleTable[]` as floats.
- Add `static float seq_trackLastShuffle[NUM_TRACKS]`.
- When a track event fires, compute that track's shuffle correction using
  `pat_getTrackShuffle()` and that track's default-step phase.
- This is easier but risks old-style accumulated correction. Use only if
  hardware testing says the simpler change is musically correct.

Comment for final scheduler helper:

```c
/*
 * Return the absolute PPQ tick at which one track event should be due.
 *
 * Why: track scale and shuffle are both per-track Pattern timing settings.
 * Computing due time from the absolute event count rather than accumulating
 * delta corrections prevents shuffle or fractional scale from drifting over
 * repeated 128-step loops.
 *
 * Inputs: track index and event count since pattern start. Output: absolute
 * 96-PPQ tick threshold for that event. Confederates: PatternData supplies
 * track length, scale, rotation, and shuffle; seq_processSchedulerTick()
 * compares the threshold to seq_elapsedPpqTicks. Risk: this runs from the
 * sequencer timing path, so keep math bounded and avoid loops.
 */
```

Reset paths:

- `seq_resetScaledScheduler()` should clear any per-track shuffle residuals.
- `seq_setRunning(0)` / stop should clear per-track residuals if using them.
- Pattern re-align should recompute positions from absolute due time and not
  preserve stale shuffle residuals.

#### `Core/Hardware/SD/filesystem.c`

Legacy one-byte shuffle:

- Session 031 follow-up decision: remove the legacy shuffle save/load phase and
  do not import or export the old single shuffle value.
- Rationale: Phase 2 storage is still changing; final interchange compatibility
  will be handled externally with Python converters once the storage shape and
  save operations are final.

Track settings and shuffle extensions:

- Keep the existing optional per-track settings block at four bytes:
  `rotate`, `scale`, `midiChannel`, and `midiNote`.
- Add shuffle as a separate optional append-only block after the four-byte
  settings block, one byte per pattern/track.
- Why this differs from the initial extension idea: widening the existing
  four-byte record would make files saved by the earlier Phase 2 settings
  extension ambiguous because the streaming reader has no public file-size API
  to distinguish "4 bytes per track" from "5 bytes per track".
- Load logic must accept both optional boundaries:
  - if no settings extension exists, track settings keep defaults from the
    length/default path;
  - if settings exists but no shuffle extension exists, shuffle remains off;
  - if shuffle extension exists, it overrides that track's shuffle.

Comment to add:

```c
/*
 * Per-track shuffle extension, versionless append rule.
 *
 * Why: PatternData now stores shuffle per track and the legacy single shuffle
 * byte is intentionally ignored in Phase 2. Shuffle is appended as a separate
 * optional block after the existing four-byte track-settings extension: missing
 * bytes leave shuffle off, while files with the extension restore individual
 * track shuffle values.
 *
 * Risk: never reorder or widen existing extension bytes in place. Append-only
 * keeps older saved files compatible with the current streaming reader.
 */
```

Container `.all/.prf` paths:

- Apply the same extension update in both pattern-file and container pattern
  stream phases.
- Ensure staging buffer sizes still cover the extended track settings record.

#### `Core/Menu/MenuText.h`, `Core/Menu/menu.h`

Existing `TEXT_SHUFFLE`, `SHORT_SHUFFLE`, and `LONG_SHUFFLE` already exist.
No new text is required.

#### `Core/Menu/menuPages.h`

Remove or reconsider `PAR_SHUFFLE` from `PERFORMANCE_PAGE`.

Recommendation:

- Remove `PAR_SHUFFLE` from `PERFORMANCE_PAGE` for now if shuffle is fully
  per-track.
- PERF page remains roll, morph, sample rate/global decimation. If an empty
  slot is unattractive, leave it blank rather than showing a misleading global
  shuffle.

### Validation

- Boot empty pattern: all tracks have shuffle `0`.
- STEP front page:
  - page 0 first half shows length, scale, MIDI channel, MIDI note;
  - repeated selected VOICE press toggles to second half and shows shuffle;
  - encoder can reach shuffle;
  - selecting a different VOICE returns to first half for that track;
  - pressing selected VOICE previews when stopped.
- Editing shuffle on track 1 does not alter track 2's displayed value.
- Sequencer playback:
  - track 1 shuffled, track 2 unshuffled: only track 1 timing moves;
  - slow/fast track scales still work with shuffle;
  - pattern realign produces deterministic step positions;
  - 128-step loop does not drift.
- Save/load:
  - new file reload restores different shuffle values per track;
  - old single-shuffle byte is ignored; missing per-track shuffle stays off;
  - old file with no extension still loads safely.

## Implementation Order

1. Add Menu morph-buffer access helpers and `voiceModeShowMorph`.
2. Rework `SHIFT+MODE_VOICE` in buttonHandler to enter morph voice overlay and
   blink the VOICE mode LED.
3. Add stopped voice preview through Sequencer and call it from
   `handleVoiceButton()`.
4. Move shuffle storage into PatternData per-track settings.
5. Update STEP menu page and voice-button front-page toggling.
6. Update sequencer timing to query per-track shuffle.
7. Update pattern/all/performance save/load extension for per-track shuffle.
8. Build and hardware-test the interaction matrix.

## Implementation Notes

### 2026-07-09 Code Pass

- Implemented `SHIFT+MODE_VOICE` as a persistent morph voice overlay.
  `buttonHandler` owns the gesture and blinking VOICE mode LED; `Menu` owns the
  `voiceModeShowMorph` flag and parameter-buffer resolution helpers. Encoder
  and endless-pot edits now write `parameters2[]` while in morph voice mode and
  call `preset_morph(parameter_values[PAR_MORPH])` to refresh the interpolated
  audible state without applying morph endpoint values as active kit writes.
- Removed the old SHIFT-held temporary STEP overlay from VOICE mode. The dead
  helper functions that only supported that overlay were removed after build
  warnings showed they were no longer used.
- Implemented stopped-transport voice preview in `seq_previewVoice()`.
  `handleVoiceButton()` computes whether the pressed voice was already active
  before changing selection, then previews after the normal non-copy/non-mute
  button behavior. This lets STEP front-page toggling and stopped preview happen
  on the same repeated VOICE press, as requested.
- Moved shuffle ownership into `LengthRotate.shuffle` in PatternData and removed
  the temporary per-pattern/global shuffle bridge. `PAR_SHUFFLE` now edits the
  viewed pattern/active track through `pat_setTrackShuffle()`, and
  `pat_applyTrackSettingsToMenu()` refreshes the displayed per-track value.
- Updated `menuPages.h`: PERF no longer shows shuffle; STEP subpage 0 now shows
  `length, scale, MIDI channel, MIDI note` in the first half and per-track
  `shuffle` in the second half. Repeated VOICE presses in STEP toggle halves;
  selecting a different track returns to the first half.
- Updated sequencer timing so the 96 PPQ transport tick remains uniform and
  per-track shuffle is applied in the per-track due-event calculation.
  `seq_trackEventBaseTick()` derives absolute timing from the track scale ratio;
  `seq_trackEventShuffleOffset()` adds a per-track delay based on the existing
  shuffle curve and `pat_getTrackShuffle()`. Because due time is derived from
  absolute event count rather than accumulated deltas, pattern realign and loop
  timing do not preserve stale shuffle residuals.
- Updated standalone `.pat` and `.all/.prf` container save/load paths. The
  legacy one-byte shuffle field was removed/ignored per follow-up decision. The
  existing four-byte track-settings extension remains four bytes. A new optional
  one-byte-per-track shuffle extension follows it. EOF before either optional
  extension is treated as a valid provisional file; EOF inside optional data
  closes as done without carrying a partial stream offset into the next phase.
- Kept `FS_CONTAINER_VERSION` at `2` because the new data is append-only at the
  end of the pattern payload. Bumping the version would make older compatible
  containers reject the file even though the required prefix is unchanged.
- Added adjacent code comments for the new `.c`/`.h` surfaces: Menu morph-buffer
  helpers, button gesture/preview paths, Sequencer preview and timing helpers,
  PatternData shuffle accessors/storage, STEP/PERF page-table changes, and
  filesystem extension phases.

### Validation Run

```sh
make
make img
```

Results:

- `make` completed and linked `build/lxr02.elf`.
- `make img` wrote `build/LXRV2_lxr02.img` successfully.
- Remaining build output is the existing/newlib nano syscall linker warnings
  (`_close`, `_lseek`, `_read`, `_write`) plus LTO serial-compilation notes.
  No feature-source warnings remained after removing the obsolete VOICE SHIFT
  overlay helpers.

## Build And Test Commands

Expected validation after implementation:

```sh
make
make img
```

Recommended focused source checks:

```sh
rg -n "voiceModeShowMorph|parameters2|pat_setTrackShuffle|pat_getTrackShuffle|PAR_SHUFFLE" Core
git diff --check
```
