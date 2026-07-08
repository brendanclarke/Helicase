# PAT_8BAR_SINGLE_AUDIT.md
# Bridge Plan: One 128-Step Scene Pattern

## Goal

Create the Phase 2 bridge needed for Scene work: one Scene owns one 128-step
pattern per track, shown as 8 bars of 16 steps. The existing 128 `Step` records
become the playable/editable steps. The old "16 main steps, each with 8
sub-steps" behavior stops being live behavior.

This is not the Phase 3 dynamic pool. It should be deliberately small:

- Keep the existing `Step` struct.
- Keep existing helpers where they can be redirected.
- Avoid broad renames, especially new step-model labels that are not already
  part of this codebase.
- Do not create helper functions unless they remove real risk.
- Any temporary bridge helper that converts to or from the old file/main-step
  representation must end in `_temp` and be documented as a deletion target.

## Main Critique Of The Previous Audit

The previous plan was too much like a final cleanup pass. It proposed removing
or renaming broad parts of PatternData (`pat_subStepPattern`, main-step helpers,
dirty LED fields, file phases) before the Scene storage architecture exists.
That increases blast radius without helping the bridge.

The safer bridge is:

1. Reinterpret `pat_subStepPattern[0][track][0..127]` as the Scene pattern's
   128 steps.
2. Make UI and sequencer code address those steps directly.
3. Stop using `pat_mainSteps` for live playback/display.
4. Keep old-format file compatibility by reading/writing legacy-sized pattern
   payloads, but only slot 0 carries the bridge pattern. Slots 1-7 are ignored
   on load and written blank on save.
5. Use `_temp` conversion helpers only at the old-file boundary.

## Decisions To Carry Into Implementation

### Storage

- Change `NUM_PATTERN` to `1` for live PatternData storage.
- Add:

```c
#define NUM_BARS 8u
#define NUM_STEPS_PER_BAR 16u
```

- Keep `NUM_STEPS` as `128`.
- Keep the `pat_subStepPattern` field name for this bridge. It is an old name,
  but renaming it now touches every storage, generator, filesystem, and menu
  path for little practical benefit.
- Remove `pat_mainSteps` from live semantics. It can either be removed from
  `PatternSet` now, or retained only as temporary legacy-load scratch. If it is
  retained, comments must say it is not live trigger state.

Preferred bridge shape:

```c
typedef struct PatternSetStruct {
    Step pat_subStepPattern[NUM_PATTERN][NUM_TRACKS][NUM_STEPS];
    PatternSetting pat_patternSettings[NUM_PATTERN];
    LengthRotate pat_patternLengthRotate[NUM_PATTERN][NUM_TRACKS];
} PatternSet;
```

`TempPattern` should mirror the one live pattern and stay in place. The staging
path is still useful because active-pattern loads can happen while playback is
running.

### Temporary Legacy Helpers

Add only the helpers needed to convert old file payloads:

```c
void pat_applyMainStepsToSteps_temp(uint8_t pattern, const uint16_t mainSteps[NUM_TRACKS]);
uint16_t pat_buildMainStepsFromSteps_temp(uint8_t pattern, uint8_t track);
void pat_clearPatternSlotForSave_temp(Step *step, uint32_t legacyStepIndex);
```

Names can be adjusted, but the `_temp` suffix is required. Each helper should
state that it exists only until the Scene/Phase 3 pattern file format replaces
legacy `.pat/.prf/.all` payloads.

Expected behavior:

- On old file load, read slot 0 steps into the bridge pattern.
- Read slot 0 main-step masks and apply them to the bridge steps:
  inactive old main-step groups clear their 8 member steps, because old files
  could have default active bits in those records even when the main-step mask
  says the group was off.
- This old-file cleanup is only a consistency guard. The bridge does not need
  to make musically faithful data from old slots or reconstruct the old 8-pattern
  behavior.
- Discard old slots 1-7.
- On save, write the bridge pattern as old slot 0 step data.
- Synthesize old slot 0 main-step masks from the bridge steps so a reload knows
  which old groups contain activity.
- Write old slots 1-7 as blank records.

This preserves the bridge pattern across new-firmware save/load without making
the old 8-pattern model live.

## File-by-File Implementation Notes

### `Core/Scene/Pattern/PatternData.h/.c`

Keep the public API mostly intact and redirect behavior:

- `pat_patternValid()` should only accept pattern `0`.
- `pat_clearTrack()` should reset all 128 steps inactive. Do not reactivate
  every old group start with `k % 8 == 0`.
- `pat_recordNote()` should write the target step directly: note, velocity,
  probability, active bit. Remove the old "turn off first sub-step if parent
  main step was off" rule.
- `pat_toggleStep()` is the live trigger toggle.
- `pat_isStepActive()` is the live trigger read.
- `pat_isMainStepActive()`, `pat_setMainStep()`, `pat_toggleMainStep()`, and
  `pat_setMainStepsRaw()` should not be used by playback or UI after this
  bridge. Prefer removing them if the compile fallout is manageable; otherwise
  keep them only as legacy wrappers with comments pointing at deletion.
- Do not add a `pat_steps` rename in this pass.

Track length is the subtle part. The current 4-bit `LengthRotate.length` cannot
represent 128 directly. For this bridge, replace the packed 4-bit length with
a real 1..128 step length. Rotation can remain a separate byte/field in the
same per-track settings struct so the public `LengthRotate` name can stay for
now.

Required length behavior:

- `pat_setTrackLength()` clamps to `1..128`.
- `pat_getTrackLength()` returns the stored step count directly.
- `pat_getEffectiveTrackLength()` returns the stored step count directly.
- `pat_clearTrack()` initializes length to `128`.
- Loaded legacy length bytes are taken as-is: a loaded value of `16` means
  16 steps, not 128.
- Missing/zero legacy length bytes should become a safe default of `128`.
- Save writes the real `1..128` length byte.

The menu currently exposes `PAR_TRACK_LENGTH` as `DTYPE_1B16`; add or repurpose
a datatype so track length can be adjusted from 1 to 128 anywhere it appears.

### `Core/Sequencer/sequencer.c`

`seq_stepIndex[]` already spans 0..127, so keep it and reinterpret it as the
current step.

Required behavior changes:

- Remove playback gating through `pat_isMainStepActive()`.
- Trigger from `pat_isStepActive(track, seq_stepIndex[i], seq_activePattern)`.
- Generate probability RNG per step, not once per old 8-step group.
- Live erase should erase the current step, not an old 8-step group. This can
  be done by redirecting the existing erase helper or by renaming it to
  `pat_eraseStep()` if that is cleaner.
- `seq_determineNextPattern()` should return `0` for the bridge. Keep the
  function shell; it is a good later Scene-switching seam.
- `seq_setNextPattern()` should clamp/ignore anything except `0`.
- `seq_activePattern` and `seq_pendingPattern` remain for now but stay `0`.

Audit every old length conversion:

- `seq_nextStep()`: compare step index against the step count returned by
  `pat_getEffectiveTrackLength()`. Do not divide by 8 for wrap.
- `seq_triggerNextMasterStep()`: do not multiply effective length by 8 if the
  helper now returns steps.
- `seq_setStepIndexToStart()`: rotation should be a step offset. Clamp or modulo
  it against the 1..128 track length.
- `seq_offsetTrackStepIndexForRotation()`: offset by step counts directly.
  Remove the old `* 8` rotation math.

### `Core/Menu/menu.c/.h`

Add:

```c
extern uint8_t menu_currentBar;
```

with `menu_currentBar = 0` in `menu.c`.

Keep `menu_shownPattern`, `menu_playedPattern`, and `menu_getViewedPattern()`
for now, but clamp them to `0`. They are old pattern-view state today and will
become Scene-view state later. Removing them now would spread this bridge into
unrelated menu code.

`buttonHandler_selectedStep` can stay as the selected absolute step. Do not
rename it in this pass.

### `Core/Hardware/frontPanel/buttonHandler.c`

Add one small local helper if needed:

```c
static uint8_t buttonHandler_visibleStep(uint8_t seqIndex)
{
    return (uint8_t)(menu_currentBar * NUM_STEPS_PER_BAR + seqIndex);
}
```

This is a useful exception to the "no new helpers" rule because it prevents
repeating bar math across press, release, select, timer, and repaint paths.

Required behavior changes:

- SEQ1-16 operate on `menu_currentBar * 16 + seqButton`.
- Plain VOICE mode SELECT1-8 must not change: they continue selecting voice
  subpages.
- In VOICE mode, SHIFT+SELECT1-8 selects `menu_currentBar`.
- In STEP/PATTERN mode, SELECT1-8 selects `menu_currentBar`.
- Whenever SHIFT+SELECT changes or re-selects the viewed bar, pulse the
  corresponding SELECT LED for 0.5 seconds using existing LED functions.
- PERF mode SELECT buttons should no longer queue old patterns. SELECT1 may
  remain the only lit/active placeholder.
- BAR1/BAR2 no longer trigger voices.
- BAR1 moves to the previous viewed bar until bar 0. At bar 0 it does not wrap
  and does not change state, but it still flashes SELECT1.
- BAR2 moves to the next viewed bar until bar 7. At bar 7 it does not wrap and
  does not change state, but it still flashes SELECT8.
- BAR1/BAR2 LEDs light while their buttons are pressed.
- Long-press automation arming should arm the visible step directly. Blink the
  SEQ LED that was held; SELECT LEDs no longer identify sub-steps.
- Hold COPY in VOICE or STEP/PATTERN mode, press a SELECT button to copy the
  current track's selected bar, then press a different SELECT button to paste
  to that destination bar. On paste, extend that track length to include the
  destination bar if needed: `length = max(length, (dstBar + 1) * 16)`.

### `Core/Hardware/frontPanel/ledHandler.c/.h`

`led_updatePatternTrack()` should show:

- STEP LEDs: active state for the 16 steps in `menu_currentBar`.
- SELECT LEDs in STEP/PATTERN mode: current bar indicator.

Do not add broad LED abstractions. Redirect the existing repaint path.

Specific updates:

- `led_setActive_step()` should display `step % 16`, and only when the played
  step is in `menu_currentBar`.
- `led_updateCurrentStep()` should keep the existing page/pattern guard but add
  the current-bar visibility guard.
- Recorded-step feedback should update the STEP LED when the recorded step is
  inside the visible bar. SELECT recorded-sub-step feedback can be removed or
  ignored.
- `SeqLedState.recordSubStep` and `SEQ_LED_DIRTY_REC_SUB` can be left unused
  for the bridge if removing them causes churn, but comments should mark them
  obsolete. Removing them is fine if the compile fallout is small.
- `led_initPerformanceLeds()` should light only SELECT1.
- Historical bridge note: this pass originally sketched a 500/100 flash helper,
  but the live implementation now keeps the project-local 400 ms duration and
  80 ms cycle in the existing `ledHandler` flash code. Do not add a separate
  flash layer for this behavior.

```c
#define LED_FLASH_DURATION_TIME_MS 400
#define LED_FLASH_CYCLE_TIME_MS     80
void led_flashLed(uint8_t ledNr);
```

  The flash sequence follows the existing handler timing and restores whatever
  the LED group's current logical state is when the flash ends. This is separate
  from `led_pulseLed()`, which remains the existing short temporary inversion,
  and from the persistent blink slots.
- Bar changes from SHIFT+SELECT or BAR1/BAR2 should call `led_flashLed()` on
  the corresponding SELECT LED.
- BAR1/BAR2 LEDs should light on press and return on release through the
  existing button press/release LED paths.

### `Core/Hardware/SD/filesystem.c`

Do not let `NUM_PATTERN == 1` shrink legacy file streams accidentally.

Add file-format constants independent of live storage:

```c
#define FS_LEGACY_PATTERN_COUNT 8u
#define FS_PATTERN_STEP_COUNT ((uint32_t)NUM_TRACKS * FS_LEGACY_PATTERN_COUNT * NUM_STEPS)
#define FS_PATTERN_MAIN_COUNT ((uint32_t)FS_LEGACY_PATTERN_COUNT * NUM_TRACKS)
#define FS_PATTERN_SETTINGS_COUNT ((uint32_t)FS_LEGACY_PATTERN_COUNT)
#define FS_PATTERN_LENGTH_COUNT ((uint32_t)FS_LEGACY_PATTERN_COUNT * NUM_TRACKS)
```

Adjust address helpers so file pattern slots 1-7 are valid file positions but
not valid live PatternData slots.

Save path:

- For slot 0: write live bridge steps.
- For slots 1-7: write blank steps/settings/lengths.
- For main-step masks: slot 0 uses `pat_buildMainStepsFromSteps_temp()`;
  slots 1-7 write zero.
- For track lengths: write the real `1..128` length byte for slot 0; write
  blank/default values for slots 1-7.

Load path:

- Slot 0 steps go to live/staged pattern 0.
- Slots 1-7 are read and discarded.
- Slot 0 main-step masks are read into temporary per-track masks and then
  applied through `pat_applyMainStepsToSteps_temp()`.
- Slot 0 length bytes are loaded as real step counts. `16` means 16 steps.
  Zero/missing values become 128.
- Slots 1-7 masks/settings/lengths are discarded.

Keep `FS_CONTAINER_VERSION` at 2 for this bridge unless we intentionally decide
to break all old container loading. The bridge can remain compatible enough to
load old files and save fixed-size containers. A version bump belongs with the
new Scene file format.

### `Core/Scene/Pattern/EuklidGenerator.c`

The current generator writes a 16-bit old main-step mask. For the bridge, do
not route through `pat_setMainStepsRaw()` as live state.

Bridge behavior:

- Generate up to the current track length in steps.
- If the track length is 128, Euklid can populate all 8 bars.
- Replace the current 16-bit generator mask/bounds with step-length-aware
  storage. `PAR_EUKLID_LENGTH` currently uses `DTYPE_1B16`; it needs the same
  1..128 treatment as track length or a clamp to the current track length.
- `PAR_EUKLID_STEPS` must clamp to the generated length, not to 16.
- Use `pat_toggleStep()`/direct step-set API rather than main-step masks.

### `Core/Menu/copyClearTools.c`

Keep track copy/clear as whole 128-step track operations.

Pattern copy becomes meaningless with one live pattern. Replace the SELECT-button
copy route with copy-bar:

- In VOICE or STEP/PATTERN mode, hold COPY.
- First SELECT press chooses the source bar on the current track.
- Second, different SELECT press chooses the destination bar on the same track.
- Copy all 16 `Step` records from source bar to destination bar, including
  trigger state, note, velocity, probability, and automation fields.
- After paste, extend the current track length if the destination bar lies
  beyond the existing length.
- Reset copy state and repaint STEP/SELECT LEDs from PatternData.

## Verification Checklist

- Build succeeds with no new warnings.
- `sizeof(PatternSet)` drops to one live pattern if `NUM_PATTERN` becomes 1.
- Existing `.pat`, `.prf`, and `.all` files load slot 0 without depending on
  old slots 1-7.
- Saving writes legacy-sized payloads: slot 0 populated, slots 1-7 blank.
- VOICE/STEP step buttons operate on the selected bar's 16 steps.
- SELECT bar navigation updates STEP LEDs immediately.
- Sequencer plays through 0..127 and wraps correctly.
- Track length can be set to any value from 1 to 128 and saved/loaded as that
  real value.
- Euklid can generate across the current track length, including all 128 steps.
- Copy-bar copies a 16-step bar and extends track length to include the
  destination bar.
- Chase LED only appears for the visible bar.
- Live recording lights the correct visible STEP LED.
- Live erase clears one current step, not an old 8-step group.
- PERF mode no longer queues old pattern slots; only SELECT1 is active/lit.

## Summary Recommendation

Proceed with a bridge, not a cleanup. Keep the current storage and UI seams
recognizable, make pattern slot 0 the single Scene pattern, redirect playback
and editing to direct 0..127 step access, and confine old-format conversion to
clearly marked `_temp` helpers.

## Implementation Notes From This Pass

- `NUM_PATTERN` is now one live pattern slot. Filesystem pattern streaming keeps the legacy eight-slot file shape with slot 0 mapped to PatternData and slots 1-7 routed to discard/blank bridge records.
- `LengthRotate` now stores byte-sized `length` and `rotate`; track length is treated as real 1..128 steps. Zero loaded from old files resolves to the 128-step default through PatternData readers/setters.
- Sequencer playback now advances against 1..128 step lengths and reads `Step.volume & STEP_ACTIVE_MASK` directly for each track step instead of treating `pat_mainSteps` as the active playback gate.
- STEP1..16 now displays `menu_currentBar * 16 + 0..15`; SELECT1..8 indicates the viewed bar. `led_flashLed()` provides the existing 400/80 bar acknowledgement timing.
- `SHIFT+SELECT` in VOICE mode and SELECT in STEP/PAT_GEN mode select the visible bar. Plain VOICE SELECT still selects voice subpages. PERF SELECT no longer queues old pattern slots.
- BAR1/BAR2 no longer trigger voices. They move the visible bar without wrapping, flash the boundary bar again at the ends, and light their own LEDs while pressed.
- COPY+SELECT now performs copy-bar/paste-bar on the current track. Paste extends the track length to include the destination bar.
- Euklid generation bypasses the legacy 16-bit transfer buffer for the bridge and writes generated rhythm directly to 128 `Step` active bits while preserving existing note/probability/automation fields.

### Follow-Up Build/Test Watchpoints

- This pass was not built here because the make toolchain is unavailable by request. Build should pay close attention to any stale comments/prototypes around the retained legacy `pat_mainSteps` helpers and any assumptions in filesystem container save/load paths.
- `pat_mainSteps` remains as legacy/file compatibility storage for this bridge, but playback and visible step editing now use the 128 `Step` active bits as the source of truth.
### Continuation Notes After Interruption

- `led_flashLed()` now restarts an existing flash slot for the same LED, so repeated BAR1/BAR2 boundary presses or repeated SHIFT+SELECT acknowledgements produce a fresh flash at the existing 400/80 timing instead of being ignored while the previous flash is active.
- Long-press automation arming now blinks the visible STEP1..16 LED for `step % 16`; it no longer routes non-boundary steps to SELECT LEDs, because SELECT1..8 is the bar indicator row during this bridge.
- `led_updateRecordedSubStep()` is now a documented compatibility no-op. SELECT-row record feedback is intentionally suppressed until the old REC_SUB dirty bit can be deleted with the Scene UI work.
- Euklid length/steps/rotation setters now guard invalid track indices before touching generator arrays. Generation still writes only Step active bits and clamps length/steps/rotation to the active 1..128 bridge range.
- Static checks run here: literal newline-artifact scan and `git diff --check`. `git diff --check` reported only Git LF-to-CRLF normalization warnings, not whitespace errors. No firmware build or `make` command was run.

## Fine-Tuning Plan: Clock, Flash Overlay, STEP Front Page, Track Scale, Re-Align

This section is the implementation plan for the post-bridge tuning pass. It
builds on the completed one-pattern/8-bar bridge and should still avoid turning
this into the full Phase 3 pattern rewrite.

### 1. Slow The Default Sequencer Step Rate To 1/8 Current Speed

Historical starting point: before this pass, internal timing advanced a
sequencer step every 3 internal PPQ ticks, which yielded 32 sequencer steps per
quarter note. The requested default is 4 sequencer steps per beat, so the
default step edge now occurs every 24 internal PPQ ticks instead.

Implementation direction:

- Replace the magic prescaler masks with named constants:
  - `SEQ_INTERNAL_PPQ = 96`
  - `SEQ_DEFAULT_STEPS_PER_BEAT = 4`
  - `SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP = 24`
  - `MIDI_CLOCKS_PER_QUARTER = 24`
- Stop using the old 12-count prescaler cycle for step scheduling. It is too
  small for a 24-tick default step interval.
- Keep MIDI clock output at 24 PPQ. The musical step clock should slow down;
  the MIDI realtime clock should not slow down with it.
- Keep shuffle tied to the default step grid for now. After this change, the
  old comments around "32 steps per beat" and quarter-beat pulse positions must
  be rewritten because they will be false.

Code-comment requirement for this change:

```c
/*
 * The internal timing ISR still runs a 96 PPQ phase clock, but the visible
 * pattern grid now advances at 4 steps per beat by default. That means one
 * default sequencer step is 24 internal PPQ ticks. MIDI clock output remains
 * 24 PPQ and is generated from the internal phase clock independently of the
 * pattern step scheduler, so slowing the pattern grid does not slow external
 * MIDI clock.
 */
```

### 2. Make LED Flash A Group-Aware Overlay

The current `led_flashLed(uint8_t ledNr)` is a useful start, but it stores only
the flashed LED IDs. It relies on `led_reset()` at expiry, which restores the
current remembered/base state. That part is good. The missing behavior is group
ownership: a new SELECT flash does not cancel the previous SELECT flash, so fast
BAR presses can leave multiple SELECT LEDs in flash phases.

New API shape:

```c
typedef enum {
    LED_FLASH_GROUP_SELECT,
    LED_FLASH_GROUP_SEQ,
    LED_FLASH_GROUP_MODE,
    LED_FLASH_GROUP_VOICE,
    LED_FLASH_GROUP_BAR,
    LED_FLASH_GROUP_FUNCTION,
    LED_FLASH_GROUP_COUNT
} LedFlashGroup;

void led_flashGroup(LedFlashGroup group, uint16_t mask);
```

Group bit meanings:

- SELECT: bits 0..7 map to `LED_PART_SELECT1..8`.
- SEQ: bits 0..15 map to `LED_STEP1..16`.
- MODE: bits 0..3 map to `LED_MODE1..4`.
- VOICE: bits 0..6 map to `LED_VOICE1..7`.
- BAR: bits 0..1 map to `LED_BAR1`, `LED_BAR2`.
- FUNCTION: bits 0..3 map to `LED_SHIFT`, `LED_START_STOP`, `LED_REC`,
  `LED_COPY` in the user-requested order SHIFT, PLAY, REC, COPY. Note that the
  code names PLAY as `LED_START_STOP`.

Implementation direction:

- Replace the generic flash slot pool with one flash record per group:
  - `activeMask`
  - `phase`
  - `nextTime`
  - `endTime`
- `led_flashGroup(group, mask)` first cancels that group's existing flash:
  - restore every currently flashed LED in the old mask with `led_reset()`
  - clear the old active mask
- Then it starts the new mask:
  - ignore bits beyond the group's LED count
  - set `activeMask` to the cleaned mask
  - force the masked LEDs on with `led_setValueTemp(1, led)`
  - schedule the flash using the current local timing constants,
    `LED_FLASH_DURATION_TIME_MS = 400` and `LED_FLASH_CYCLE_TIME_MS = 80`
- During a flash, base LED writes must continue to update
  `led_originalLedState[]` / `led_sw43OriginalState`. The flash overlay should
  only touch the physical output with `led_setValueTemp()`. On expiry/cancel it
  calls `led_reset()`, which restores whatever base state exists at that moment.
  This exactly covers "base changed from on to off/off to on while flashing."
- Keep a compatibility wrapper if useful:

```c
void led_flashLed(uint8_t ledNr)
```

  but make it route through the correct group+bit when the LED belongs to a
  known group. Prefer updating call sites directly to `led_flashGroup()` where
  the group is obvious.

Code-comment requirement for this change:

```c
/*
 * Flash is an overlay, not LED state. led_setValue() may change the remembered
 * base state while a flash is active; the flash group only forces temporary
 * physical output phases. When the flash is cancelled or expires, led_reset()
 * restores the LED to the current base state, not to a snapshot taken when the
 * flash began. Starting a new flash for a group cancels that group's previous
 * mask first so, for example, only one SELECT-row acknowledgement can flash at
 * a time.
 */
```

Call-site changes:

- BAR1/BAR2 boundary and bar-change feedback should use
  `led_flashGroup(LED_FLASH_GROUP_SELECT, 1u << bar)`.
- Any future acknowledgement on STEP, MODE, VOICE, BAR, or function LEDs should
  use the same group function instead of allocating ad hoc slots.
- The VOICE-mode SELECT subpage bug should disappear because
  `led_setActiveSelectButton(menu_getSubPage())` can update the SELECT base
  state while the SELECT flash is active, and flash expiry restores that base.

### 3. Add A STEP Mode Front Page

The existing `SEQ_PAGE` first subpage is the per-step editor. Add a new STEP
front page modeled on the quiet, always-available PERF page:

- It appears when entering STEP mode.
- It appears when the selected track/voice changes while in STEP mode.
- It remains visible until a step is selected.
- After a step is selected, the page does not automatically return until the
  STEP mode button is pressed again or a VOICE button changes the selected
  track.

Suggested page layout:

```c
/* SEQ_PAGE subpage 0: STEP front page */
{TEXT_PAT_LENGTH, TEXT_MIDI_CHANNEL, TEXT_NOTE, TEXT_TRACK_SCALE,
 TEXT_EMPTY,      TEXT_EMPTY,        TEXT_EMPTY,TEXT_EMPTY,
 PAR_TRACK_LENGTH, PAR_MIDI_CHAN_X,  PAR_MIDI_NOTE_X, PAR_TRACK_SCALE,
 PAR_NONE,         PAR_NONE,         PAR_NONE,        PAR_NONE}

/* SEQ_PAGE subpage 1: existing per-step editor */
{TEXT_STEP_VELOCITY, TEXT_NOTE, TEXT_PROBABILITY, ...}
```

Because `PAR_MIDI_CHAN_X` and `PAR_MIDI_NOTE_X` are voice-specific parameters,
the page table cannot name a single fixed parameter for all tracks without help.
The least invasive bridge approach is to add display/edit indirection for
track-scoped aliases, or to refresh the four front-page parameter cells when
the active voice changes. Implementation should choose the smaller code change
after checking menu table constraints.

Remove these from the VOICE mix subpage:

- `PAR_TRACK_LENGTH`
- `PAR_MIDI_CHAN_1..7`
- `PAR_MIDI_NOTE1..7`

Replace those VOICE mix slots with `PAR_NONE`/`TEXT_EMPTY` unless there are
obvious voice-level parameters already waiting for the slots.

Code-comment requirement for this change:

```c
/*
 * STEP mode has two UI states: a track front page and the per-step editor.
 * Entering STEP mode or changing the active track shows the front page so the
 * user sees track-level length, MIDI channel, MIDI note, and scale before
 * selecting a step. Selecting a STEP1..16 button moves to the step editor and
 * keeps it there until STEP mode is re-entered or a VOICE button changes the
 * active track. This is UI presentation state only; PatternData remains the
 * owner of track length/scale and step data.
 */
```

### 4. Add Track Scale / Track Scaling

Add a new Pattern-owned, per-track parameter:

- Semantic name: track scale / track scaling.
- UI category: `Pattern`.
- Long name: `Scale`.
- Short name: `sca`.
- Parameter name: `PAR_TRACK_SCALE`.
- DType: menu.
- Default: `off`.
- Center value: `off`.

Menu values, in display order, should be centered around `off`:

```c
/8, /7, /6, /5, /4, /3, /25, /2, /.6, /.3, off, x.3, x.6, x2, x25, x3, x4, x5, x6, x7, x8
```

This order is my interpretation of "off is the center value": decrementing from
`off` visits `/.3`, `/.6`, `/2`, `/25`, `/3`, ... `/8`, while incrementing
visits `x.3`, `x.6`, `x2`, `x25`, ... `x8`.

Intended ratios:

- `off`: 1/1, default speed, 4 track steps per beat.
- `x.3`: 4/3 default speed.
- `x.6`: 5/3 default speed.
- `x2`: 2/1.
- `x25`: 5/2.
- `x3`..`x8`: 3/1 through 8/1.
- `/.3`: 3/4 default speed.
- `/.6`: 3/5 default speed.
- `/25`: 2/5 default speed.
- `/2`..`/8`: 1/2 through 1/8.

Storage direction:

- Add `scale` to `LengthRotate`, or introduce a separate per-track PatternData
  array if keeping `LengthRotate` as length/rotation-only is cleaner.
- Initialize scale to `TRACK_SCALE_OFF`.
- Copy scale in track copy and pattern/staging commit paths.
- Reset scale to off in `pat_clearTrack()`.
- Add `pat_setTrackScale()`, `pat_getTrackScale()`, and a
  `pat_getTrackScaleRatio()` helper that returns a small rational pair
  `{num, den}`.
- Refresh `PAR_TRACK_SCALE` in `pat_applyTrackSettingsToMenu()`.
- Do not change the legacy `.pat/.prf/.all` binary stream shape in this pass
  unless explicitly decided. Loading old files should leave scale at `off`.
  Saving old-format files cannot preserve scale without a deliberate versioned
  extension.

Code-comment requirement for this change:

```c
/*
 * Track scale is PatternData-owned per-track timing metadata. It does not
 * change the stored step grid; it changes how many stored steps a track advances
 * for each default master step. The default/off value is a 1:1 ratio against
 * the corrected 4-steps-per-beat master grid. File compatibility note: the
 * legacy pattern stream has no scale byte, so old-format loads initialize this
 * field to off and old-format saves do not preserve it until the Scene pattern
 * format replaces the bridge serializer.
 */
```

### 5. Add A 16-Bit Master Step Clock And Per-Track Scaled Advancement

Introduce a 16-bit `seq_masterStepClock` that resets:

- when the sequencer is started,
- when `seq_resetToPatternStart()` runs,
- when changing/committing pattern.

This clock advances once per corrected default step. It may overflow naturally.
Each track then advances from that default grid according to its scale ratio and
track length.

Recommended runtime state:

```c
static uint16_t seq_masterStepClock;
static uint16_t seq_trackScalePhase[NUM_TRACKS];
```

Default-tick algorithm:

- On each default master step:
  - increment `seq_masterStepClock`
  - for each track:
    - add scale numerator to that track's phase
    - while phase >= denominator:
      - phase -= denominator
      - advance that track one step, wrapping by its effective length
      - trigger/erase/roll using the stepped position
- For `off`, ratio 1/1, this advances once per default master step.
- For fast scales, e.g. `x2`, the track still plays every traversed step. This
  is a timing multiplier/divider, not a skip/land-only transform. Multiple step
  advances that fall within one default master interval must each be scheduled
  at the finest tick spacing available to the sequencer instead of collapsing
  into one audible instant.
- For slow scales, some default ticks do not advance that track.

This keeps fractional values exact without floats and uses only tiny per-track
state. The implementation should calculate per-track step timing from the
available tick interval and re-calculate each track's 128-step-loop offset
against the master step clock so fractional ratios do not accumulate drift over
long playback.

Code-comment requirement for this change:

```c
/*
 * seq_masterStepClock is the transport's corrected default-step counter: one
 * tick means one 4-steps-per-beat grid position, independent of individual
 * track scaling. Each track owns a small rational phase accumulator. A scale of
 * 1/1 advances one stored step per master tick; faster scales may advance more
 * than once inside one master interval, and those traversed steps are scheduled
 * at the finest available sequencer tick positions rather than skipped.
 * Slower scales may wait across several master ticks. Loop-offset correction is
 * re-derived against the master clock at each 128-step master loop so fractional
 * scale ratios do not drift. The 16-bit master clock is allowed to overflow
 * because realign only needs modulo arithmetic against track length.
 */
```

### 6. Pattern Re-Align

Because this bridge has one live pattern, the only pattern SELECT that can
realign is SELECT1 while the selected/playing pattern is 0.

Implementation direction:

- Add `seq_realignActivePatternToMasterClock()` to Sequencer.
- It should recompute each track's `seq_stepIndex[]` and
  `seq_trackScalePhase[]` from:
  - `seq_masterStepClock`
  - track length
  - track scale ratio
  - track rotation
- It must not clear pattern data or reset transport.
- It should be safe while running and should also be harmless while stopped.
- In PERF mode only, pressing SELECT1 again should call this helper when
  `seq_activePattern == 0` and `menu_getViewedPattern() == 0`. SELECT2..8 stay
  inactive placeholders during this bridge. Do not add the same gesture to other
  modes yet; pattern-selection triggering will be redesigned later.

Suggested math:

- Compute total scaled advances from the fine scheduler tick count, with the
  master step clock retained as the coarse default-grid reference:
  - `advances = (seq_elapsedPpqTicks * num) / (24 * den) + 1`
  - `phase = (seq_elapsedPpqTicks * num) % (24 * den)`
- Position track to `(rotation + advances) % length`.
- The implemented scaled scheduler tracks per-track event counts directly.
  Realign sets the currently sounding/selected step from that count, so the
  legacy single global pre-increment helper no longer drives scaled playback.

Code-comment requirement for this change:

```c
/*
 * Pattern realign derives runtime track positions from the master step clock
 * instead of from where the per-track counters happened to drift. This is a
 * performance action: it does not alter PatternData length/rotation/scale, it
 * only rewrites Sequencer runtime counters and scale phases. The calculation is
 * ratio-based so fractional scales realign to the same position they would have
 * reached if playback had run from master clock zero without interruption.
 */
```

### Verification Checklist For This Pass

- Build succeeds with no new warnings.
- At 120 BPM, default/off track scale advances 4 steps per beat, not 32.
- MIDI clock output remains 24 PPQ when internal sync is active.
- External sync still advances at the corrected default grid.
- SELECT-row flash no longer leaves multiple SELECT LEDs flashing after fast
  BAR1/BAR2 presses.
- While a SELECT LED is flashing, changing VOICE subpage updates the base LED;
  flash expiry restores the new subpage LED, not the old one.
- A new flash for any group cancels only that group's previous flash.
- STEP mode entry shows the track front page.
- Changing active voice/track while in STEP mode shows the track front page.
- Selecting a step hides the front page and shows the step editor until STEP is
  re-entered or track changes.
- VOICE mix subpage no longer shows track length/channel/note.
- Track scale defaults to `off` after init, clear, and legacy file load.
- Track scale edit changes playback rate for the active track without changing
  stored step data.
- Fast track scales visit and trigger every traversed step at sequencer tick
  timing; they do not skip intermediate steps or collapse them into a single
  landed-step trigger.
- Long-running fractional scales do not drift across repeated 128-step master
  loops because offsets are re-derived against the master step clock.
- PERF SELECT1 realigns all tracks to the master step clock; SELECT2..8 remain
  inactive in the one-pattern bridge.

### Decisions Confirmed Before Implementation

- Scale menu order is:
  `/8 /7 /6 /5 /4 /3 /25 /2 /.6 /.3 off x.3 x.6 x2 x25 x3 x4 x5 x6 x7 x8`.
- Keep the current 400 ms / 80 ms flash timing and modify the existing flash
  slot machinery in place. Do not add a second flash layer beside it.
- Track scaling is a timing multiplier/divider. Tracks always play every
  traversed step; fast scales must schedule intermediate steps at the finest
  timing resolution available rather than skipping them.
- Track-scale offset should be re-calculated against the master step clock for
  each 128-step master loop so fractional ratios do not drift.
- Track scale is runtime-only for legacy `.pat/.prf/.all` saves in this bridge.
  Persisted storage waits for the later Instrument/Kit/Scene save design.
- Pattern Re-Align is triggered only from PERF mode SELECT1 for now.

### Implementation Notes From Fine-Tuning Pass

- `led_flashLed()` remains as a compatibility wrapper, but the existing flash
  slot machinery now treats each slot as one group: SELECT, SEQ, MODE, VOICE,
  BAR, or FUNCTION. Starting a flash for a group restores that group's previous
  mask first, then starts the new mask with the existing 400 ms / 80 ms timing.
- `LengthRotate` now includes runtime-only `scale`. Legacy pattern/container
  file readers explicitly set scale to `TRACK_SCALE_OFF` when loading old
  length bytes, and legacy save paths still write only the existing length byte.
- `PAR_TRACK_SCALE`, `PAR_TRACK_MIDI_CHAN`, and `PAR_TRACK_MIDI_NOTE` were added
  before globals so they are menu/runtime parameters rather than saved globals.
  `PAR_TRACK_SCALE` uses the normal `DTYPE_MENU` path with menu table id 0; the
  existing dtype encoding stores the id in the high nibble, and id 0 was unused.
  `PAR_TRACK_MIDI_*` are aliases for the active track's real MIDI channel/note.
- `SEQ_PAGE` subpage 0 is now the STEP front page: track length, current-track
  MIDI channel, current-track MIDI note, and track scale. The old per-step edit
  page moved to SEQ subpage 1 and is shown only after a STEP button selects a
  concrete step.
- The sequencer scheduler now advances from a 96 PPQ internal tick counter. A
  default/off track emits one step every 24 PPQ ticks, so the corrected default
  is 4 steps per beat. Track scale ratios convert elapsed PPQ ticks to due step
  events and emit every unplayed event in order.
- MIDI external sync now advances four 96-PPQ scheduler ticks per MIDI clock.
  Trigger-jack sync still uses the legacy native 32 PPQ prescaler value, mapped
  to scheduler ticks by multiplying by 3.
- PERF SELECT1 calls `seq_realignActivePatternToMasterClock()` in the
  one-pattern bridge and flashes SELECT1 through the group flash path.

## Follow-Up Plan: STEP Track Settings Ownership And SELECT Flash Persistence

This follow-up corrects two issues found after the fine-tuning pass:

- The STEP front page is conceptually a track settings page, not an Euklid
  generator page or a transient collection of aliases.
- VOICE subpage SELECT LEDs still do not persist through BAR-change flash in
  VOICE mode because bar repaint code rewrites the SELECT-row base state before
  the flash expires.

### Investigation Findings

- `SEQ_PAGE` subpage 0 already displays the intended track settings surface:
  track length, track MIDI channel, track note, and track scale.
- Track length and track scale currently live in `LengthRotate`, but scale is
  still runtime-only in the filesystem path.
- `PAR_TRACK_MIDI_CHAN` and `PAR_TRACK_MIDI_NOTE` are menu-only aliases today.
  They mirror `PAR_MIDI_CHAN_1..7` and `PAR_MIDI_NOTE1..7`, so they are not
  owned by PatternData and are not serialized as per-pattern track settings.
- Euklid generator settings still have their own page and storage arrays. That
  page should stay generator-specific; it should not be the owner for ordinary
  per-track pattern settings.
- The SELECT flash restore path uses temporary LED writes and `led_reset()`,
  which correctly restores the current base state. The failure is earlier:
  `buttonHandler_selectBar()` calls `led_updatePatternTrack()`, and
  `led_updatePatternTrack()` always calls `led_setActiveSelectButton(menu_currentBar)`.
  In VOICE mode that changes the SELECT-row base state from voice subpage to
  bar indicator, so flash expiry restores the wrong semantic owner.

### Menu/Display Plan

- Rename the STEP front page in comments and local helper names as the track
  settings front page. It remains `SEQ_PAGE` subpage 0 for now, because STEP mode
  is the user-facing entry point.
- Keep Euklid generator length/steps/rotation on `EUKLID_PAGE`, but stop treating
  that page or its helper as the source of truth for the STEP track settings
  page. Plain VOICE button presses in STEP/track-settings context should call the
  track-settings refresh path, not only `buttonHandler_applyEuklidParamsToMenu()`.
- When the active voice changes while the displayed page is `SEQ_PAGE`, show
  subpage 0 again and repaint from PatternData. This should be tied to the
  displayed page/context as well as `SELECT_MODE_STEP`, so the page refresh is
  consistent even if a later gesture reuses the track-settings page outside the
  strict STEP mode enum.

Code-comment requirement for this change:

```c
/*
 * Track settings page refresh is based on the visible page/context, not only on
 * the select-button mode enum. The page presents PatternData-owned track
 * settings, so a VOICE-row track change must reload PatternData, repaint the
 * STEP front page, and leave the per-step editor hidden until the user selects
 * a concrete STEP button again.
 */
```

### PatternData Ownership Plan

- Replace the narrow `LengthRotate` concept with a broader per-track pattern
  settings record, or extend it in place and rename it in a later cleanup. The
  minimum fields for this pass are:
  - `length`
  - `rotate`
  - `scale`
  - `midiChannel`
  - `midiNote`
- Add PatternData setters/getters for track MIDI channel and track MIDI note:
  `pat_setTrackMidiChannel()`, `pat_getTrackMidiChannel()`,
  `pat_setTrackMidiNote()`, and `pat_getTrackMidiNote()`.
- Update `pat_applyTrackSettingsToMenu()` so `PAR_TRACK_MIDI_CHAN` and
  `PAR_TRACK_MIDI_NOTE` mirror PatternData-owned values, not the legacy preset
  MIDI parameter array.
- Update `menu_parseGlobalParam(PAR_TRACK_MIDI_CHAN)` and
  `menu_parseGlobalParam(PAR_TRACK_MIDI_NOTE)` so edits write PatternData. If the
  existing MIDI input/playback code still requires the legacy `PAR_MIDI_*`
  values, mirror the active track's PatternData value into those parameters as a
  compatibility output, not as the owner.
- Decide whether track MIDI channel/note should also replace the old kit-level
  `PAR_MIDI_CHAN_1..7` and `PAR_MIDI_NOTE1..7` semantics globally in this pass.
  If not, keep the old params for legacy MIDI behavior and document the temporary
  mirror.

Code-comment requirement for this change:

```c
/*
 * PatternData owns per-track pattern settings shown on the STEP front page.
 * The menu parameters are view/edit aliases only: they load from the active
 * pattern/track before display, and edits write back through PatternData
 * setters. Legacy PAR_MIDI_* mirrors may still be updated for compatibility,
 * but they are not the source of truth for this page.
 */
```

### Pattern Storage Protocol Plan

- Extend the pattern storage protocol for the per-track settings record instead
  of continuing to save only the legacy length byte.
- Preserve old loads:
  - Old one-byte track-length records load `length` and default `rotate = 0`,
    `scale = off`, `midiChannel = legacy/default`, and `midiNote = legacy/default`.
  - Missing or zero length still resolves through the existing 128-step default.
- For new saves, write a versioned per-track settings payload. Candidate byte
  order:
  - length
  - rotate
  - scale
  - midiChannel
  - midiNote
- Bump the relevant pattern/container version only if the existing loader cannot
  distinguish old one-byte length records from new multi-byte settings records.
  Otherwise keep a backward-compatible record-size branch.
- Update staged/temp pattern copy and pattern commit paths so the whole track
  settings record copies together.

Code-comment requirement for this change:

```c
/*
 * Pattern track settings are versioned beyond the legacy one-byte length stream.
 * Old files provide only length, so the loader supplies defaults for the newer
 * fields. New saves write the complete per-track settings record so STEP front
 * page values round-trip with PatternData instead of leaking through kit/global
 * parameter storage.
 */
```

### General Flash Overlay Plan

- Treat flash as a general overlay/compositor behavior for any supported LED
  group. It is not intrinsic to bar selection; bar selection is just one caller
  that asks the SELECT group to flash a mask.
- The steady/base LED state remains owned by the current UI mode:
  - VOICE mode: SELECT LEDs show the voice subpage.
  - STEP/track-settings context: SELECT LEDs show the current bar.
  - PERF mode: SELECT LEDs show performance/pattern status.
  - Other groups keep their own existing mode-specific meanings.
- `led_flashGroup(group, mask)` should overlay the requested mask on top of that
  base state. While a flash is active, ordinary LED state changes in the same
  group must update the remembered/base state without tearing down or visibly
  replacing the flash overlay. When the flash ends, the group renders whatever
  base state is current at that moment.
- A new flash request for a group cancels only that group's previous overlay,
  restores/renders the current base state for the previous mask, then starts the
  new overlay mask. Flashes in other groups continue independently.
- Do not solve this by adding a second flash layer. Modify the existing
  ledHandler flash machinery so base-state writes and temporary flash rendering
  compose correctly in one place.
- Separate pattern STEP-row repaint from SELECT-row ownership. Bar-selection
  callers may request a SELECT flash overlay, but they must not force the SELECT
  row's base state to bar indication unless the current UI context actually owns
  SELECT as bar indication.

Code-comment requirement for this change:

```c
/*
 * Flash is an overlay on top of the current UI-owned LED base state. Base LED
 * writes continue to update the remembered state while a flash is active, but
 * the rendered output remains controlled by the active flash mask until that
 * group's overlay expires or is replaced. Expiry renders the latest base state,
 * not a snapshot from flash start. Callers such as bar selection request flash
 * feedback; they do not become owners of the SELECT row's persistent meaning.
 */
```

### Verification Checklist

- In STEP mode, pressing any VOICE button shows the track settings front page
  for that track and repaints immediately.
- The track settings page values come from PatternData for length, MIDI channel,
  MIDI note, and scale.
- Editing each track setting updates PatternData and survives pattern
  save/load through the new storage protocol.
- Loading old patterns still restores track length and defaults the newer fields
  safely.
- In VOICE mode, changing bars requests a SELECT flash overlay but preserves
  SELECT-row base ownership as the current voice subpage.
- If the VOICE subpage changes during a flash, flash expiry restores the new
  subpage LED.
- In STEP mode, changing bars still leaves the selected bar LED as the SELECT
  row's persistent state.
- Flashing MODE, VOICE, BAR, SEQ, SELECT, or FUNCTION groups overlays only that
  group and does not disturb unrelated groups or persistent mode state.

### Implementation Notes From Track Settings Follow-Up

- `SCOPING_TARGETS.md` now includes Phase 3.11 as the final Phase 3 LED-state
  consolidation pass before Phase 4. That pass is intentionally not implemented
  here.
- `LengthRotate` keeps its historical name for this bridge, but now owns the
  full STEP front-page track settings record: length, rotation, scale, MIDI
  channel, and MIDI note.
- PatternData now provides track MIDI channel/note setters and getters.
  `PAR_TRACK_MIDI_CHAN` and `PAR_TRACK_MIDI_NOTE` load from PatternData and edit
  PatternData first; the old `PAR_MIDI_*` arrays are only mirrored for current
  compatibility with existing MIDI/parser paths.
- Sequencer MIDI output and MIDI input note/channel matching now read the active
  pattern track's MIDI channel/note from PatternData, so loaded pattern settings
  behave without requiring a UI edit to refresh the old globals.
- Pattern file/container storage keeps the legacy one-byte track-length block
  exactly where it was, then appends an optional four-byte per-track settings
  extension: rotation, scale, MIDI channel, MIDI note. Old files stop after the
  length block and load with defaults for the newer fields. When old files omit
  MIDI channel/note, the loader seeds those fields from the currently loaded
  legacy MIDI parameters when they are valid, preserving old kit/pattern behavior.
- `led_updatePatternTrackView()` separates STEP-row repaint from SELECT-row
  ownership. Existing `led_updatePatternTrack()` keeps the old behavior for
  callers that want SELECT as the bar row; VOICE-mode bar changes use the new
  helper without taking SELECT-row ownership, then request a normal SELECT flash
  overlay.
- Plain VOICE button changes while `SEQ_PAGE` is displayed now re-enter the
  STEP/track-settings front page and repaint from PatternData, even if a future
  gesture arrives at that page through a route other than strict STEP mode.

## Additional Boot-Hang Bugfix Plan: Kitset EOF Phase

This note continues the interrupted boot-hang investigation from the task DB.
The current boot path in `main.c` does not load `.pat` files. With an SD card
present, the blocking startup sequence is:

1. `filesystem_requestScanKits()` while polling `filesystem_tick()`.
2. `preset_loadDrumset(0, 0)` while polling `filesystem_tick()`.
3. `preset_loadGlobals()` while polling `filesystem_tick()`.

The most concrete hang candidate is in the normal kit load state machine in
`Core/Hardware/SD/filesystem.c`, case 13, `READ kitset.kcg`.

Current behavior:

- If `filesystem_readTextLine()` returns an error, the code marks the kit
  invalid but sets `op_phase = 13`, so it retries the same read phase instead
  of closing the file and finishing the operation.
- If EOF is reached and `storage_kitsetFinalize()` succeeds or fails, the code
  sets `op_phase = 13` again. That means a valid `kitset.kcg` can finish parsing
  but never advance to the close/final status path, leaving
  `preset_loadDrumset(0, 0)` stuck in `PRESET_LOAD_IN_PROGRESS` during boot.
- The parse-line invalid-format path already advances to phase 14, so the EOF
  path is now the higher-priority bug.

Bugfix direction:

- In case 13, every terminal kitset-read outcome should transition to phase 14
  so `kitset.kcg` is closed and the existing phase 15/28 completion path runs.
- Preserve the existing `op_close_status` values:
  - read error: `FS_STATUS_ERROR`
  - finalize failure: `FS_STATUS_ERROR`
  - finalize success: `FS_STATUS_DONE`
- Do not treat this as a pattern-storage migration issue. Pattern load is a
  user/menu operation after boot; the startup hang must be isolated to kit scan,
  kit load, or globals load unless another boot caller is added later.

Suggested implementation shape:

```c
if (st != STORAGE_STATUS_OK) {
    filesystem_setPresetNameInvalid();
    op_close_status = FS_STATUS_ERROR;
    op_phase = 14;
    return;
}
...
if (eof) {
    st = storage_kitsetFinalize(&op_kitset);
    if (st != STORAGE_STATUS_OK) {
        filesystem_setPresetNameInvalid();
        op_close_status = FS_STATUS_ERROR;
    } else {
        op_close_status = FS_STATUS_DONE;
    }
    op_phase = 14;
}
```

Verification plan for the bugfix:

- Boot with the current SD card. If the hang was in kit 0 load, startup should
  pass the loading screen.
- Boot with a deliberately malformed `Kit/001 .../kitset.kcg`. The loader
  should close the file, mark the kit invalid or fail the load, and return from
  the boot polling loop instead of spinning.
- Boot with no `Kit/` directory. The kit scan/load path should fail cleanly and
  not block globals/menu startup.
- Boot with no SD card. The `if (sd_ok)` block should still be skipped, proving
  the issue is in boot SD operations rather than hardware init.
- If boot still hangs, add a temporary LED/LCD marker before and after each of
  the three boot polling loops above to identify whether the remaining block is
  kit scan, kit load, or globals load.

## Kitset Schema Cleanup Notes

Per the follow-up decision, `kitset.kcg` is now only a kit-folder guard plus the
six voice-slot manifest. The kit display name is owned by the `Kit/NNN Name`
folder, not by a `kit_name` line. `voice_decimation_all` is not kit data; the
directory-kit loader initializes `PAR_VOICE_DECIMATION_ALL` to 127 for now.
Legacy conversion metadata such as `source_name`, `source_file`, and
`legacy_slot` is not emitted into `kitset.kcg`.

Work in progress:

- `storage_kitsetParseLine()` no longer consumes `kit_name` or
  `voice_decimation_all`.
- `storage_kitsetFinalize()` no longer requires `kit_name` or
  `voice_decimation_all`.
- `filesystem_loadKitDirectory_tick()` copies the loaded kit name from the
  scanned folder cache and defaults `PAR_VOICE_DECIMATION_ALL` to 127.
- The boot-lock fix changes terminal kitset read outcomes to advance from phase
  13 to phase 14 so `kitset.kcg` closes and the load operation can finish.
- `tools/convert_legacy_kits.py` no longer emits the removed `.kcg` fields.

Verification from this cleanup:

- All 33 mirrored `SD_CARD/Kit/*/kitset.kcg` files were rewritten to the reduced
  schema: `format`, `version`, then six `[slotN]` sections with `type`, `file`,
  and `audio_out`.
- A schema sweep found no remaining `kit_name`, `source_name`, `source_file`,
  `legacy_slot`, `legacy_trailing_hex`, or `voice_decimation_all` keys in
  mirrored `kitset.kcg` files.
- A parser-equivalent validation passed for all 33 mirrored kitsets.
- Root-level Markdown docs were swept so they no longer describe `kitset.kcg` as
  owning kit metadata, kit names, or `PAR_VOICE_DECIMATION_ALL`.
- Firmware build was not run here because `make` is not installed in this
  environment.

## Instrument Morph Endpoint And Padding Notes

Follow-up work updated the generated instrument files and loader behavior:

- `tools/convert_legacy_kits.py` now emits both `[params]` and `[morph]` for
  every generated `.drm`, `.snr`, `.cym`, and `.hat` file. Legacy `.SND` files
  do not contain a separate morph endpoint, so the converter writes the same
  parameter values to both sections.
- Each endpoint is padded to 64 byte-valued key/value rows with ignored
  `_padNN=0` entries. Current instrument maps use 34 or 35 named parameters,
  leaving room for later fields while old readers can ignore unknown pad keys.
- The instrument reader already routed `[params]` into `parameter_values[]` and
  `[morph]` into `parameters2[]`; this pass made the bookkeeping explicit by
  replacing `seen_morph_data` with `seen_morph_count`.
- Validation checked all 198 mirrored instrument files: every file has exactly
  64 `[params]` rows and 64 `[morph]` rows.

Important data finding from `SD_CARD/P000.SND`:

- The first readback used the wrong legacy addressing model. Ordinary
  instrument parameters in the `.SND` payload are addressed as
  `ParamEnums - 1`, because the persisted blob starts at the first real sound
  parameter rather than at `PAR_NONE`.
- `audio_out` is the exception. It lives in the CC2 stream, so the converter
  now reads it from `128 + CC2_AUDIO_OUTn` instead of using the ordinary
  instrument parameter offset. This keeps the Slak kit routing at zero instead
  of incorrectly reading the later `114` byte as slot 3 output.
- After regenerating `SD_CARD/Kit`, Slak drum 3 reads back as
  `velo_attack=12`, `velo_decay=0`, `vol_slope=7`, `volume=77`, `pan=63`,
  `drive=17`. Slak cymbal reads back as `velo_attack=30`, `velo_decay=0`,
  `vol_slope=4`, `volume=127`, `pan=63`, `drive=30`.
- Validation checked all 198 mirrored instrument files again: every file has
  exactly 64 `[params]` rows and 64 `[morph]` rows. The regenerated kitsets
  still omit `kit_name`, `voice_decimation_all`, and trailing metadata.
- Follow-up converter audit: CC2 is not just `audio_out`; the CC2 boundary is
  `PAR_FILTER_DRIVE_1`. `tools/convert_legacy_kits.py` now derives that split
  from `ParameterArray.h`: parameters before `PAR_FILTER_DRIVE_1` read from
  `ParamEnums - 1`, while parameters at or after it read from
  `128 + CC2_*`. Validation covered all emitted instrument parameters and the
  six kitset `audio_out` fields: 93 generated references land in CC2, with no
  missing `CC2_*` counterpart.
- Firmware build was not run here because `make` is not installed in this
  environment.

## Session Addendum: Kit Directory Loader And Converter Work

This session migrated beyond the original single-pattern audit into the Phase 2
Kit/ directory loader and legacy kit conversion path. Keep this addendum as the
end-of-document handoff for the code and SD-card data changes piled on during
that work.

### Boot-Lock Fix In Directory Kit Loading

Changed code:

- `Core/Hardware/SD/filesystem.c`

Why:

- Boot was able to hang while `main.c` synchronously waited for
  `preset_loadDrumset(0, 0)` to finish. The directory-kit load state machine
  reached `case 13`, the `READ kitset.kcg` phase, but terminal outcomes could
  set `op_phase` back to 13. A valid EOF, a finalize failure, or a read error
  could therefore loop on the same phase instead of closing the file and
  reporting completion or failure.

What changed:

- Terminal kitset-read outcomes in phase 13 now advance to phase 14.
- `op_close_status` still carries the final result:
  - read error: `FS_STATUS_ERROR`
  - finalize error: `FS_STATUS_ERROR`
  - finalize success: `FS_STATUS_DONE`
- Phase 14 closes `kitset.kcg`; the existing close/completion path then lets
  the preset load operation leave `PRESET_LOAD_IN_PROGRESS`.

Inputs:

- Streaming text lines from `Kit/NNN Name/kitset.kcg`, read by
  `filesystem_readTextLine()`.
- Parser results from `storage_kitsetParseLine()` and
  `storage_kitsetFinalize()`.

Outputs:

- `op_close_status`, `op_phase`, and the global filesystem operation status.
- On success, parsed kitset state that drives the six instrument-file loads.
- On failure, an invalid preset name marker plus a completed filesystem
  operation instead of an infinite boot wait.

Accessors and clients:

- External clients still enter through `preset_loadDrumset()` and the
  filesystem request/tick API. No new public accessor was added.
- Boot client: `main.c`, through preset-manager polling.
- Runtime clients: menu/load flows that use the same preset-manager path.

Confederate functions:

- `filesystem_loadKitDirectory_tick()`
- `filesystem_readTextLine()`
- `storage_kitsetParseLine()`
- `storage_kitsetFinalize()`
- async close callback `on_file_closed()`

### Reduced `kitset.kcg` Schema

Changed code and data:

- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`
- `Core/Hardware/SD/filesystem.c`
- `tools/convert_legacy_kits.py`
- `SD_CARD/Kit/*/kitset.kcg`
- Root Markdown docs were swept for stale claims about removed kitset fields.

Why:

- `kitset.kcg` had been carrying data that should not belong to the kitset
  manifest. The kit name is defined by the folder name, global voice
  decimation is not kit data, and conversion metadata is not runtime data.
- Storing unnecessary fields made the format easier to corrupt and harder to
  reason about during boot.

What changed:

- `kitset.kcg` now contains only:
  - `format=helicase.kitset`
  - `version=1`
  - six `[slotN]` sections
  - each slot has `type`, `file`, and `audio_out`
- The parser no longer consumes or requires `kit_name`.
- The parser no longer consumes or requires `voice_decimation_all`.
- The converter no longer emits kitset metadata such as `source_name`,
  `source_file`, `legacy_slot`, or trailing legacy hex.
- `filesystem_loadKitDirectory_tick()` takes the kit display name from the
  scanned folder cache and initializes `PAR_VOICE_DECIMATION_ALL` to 127.

Inputs:

- Numbered folder names such as `001 Slak`, parsed by
  `storage_parseNumberedFolder()` and recorded by the kit scan.
- `kitset.kcg` slot sections with instrument type, filename, and `audio_out`.

Outputs:

- `storage_kitset_t.instrument_type[]`
- `storage_kitset_t.instrument_file[][]`
- `parameter_values[PAR_AUDIO_OUT1..PAR_AUDIO_OUT6]`
- `parameter_values[PAR_VOICE_DECIMATION_ALL] = 127`
- Preset display/current kit name copied from the folder-derived slot name.

Accessors and clients:

- Runtime clients should use the existing filesystem kit-slot accessors:
  `filesystem_kitSlotExists()` and `filesystem_kitSlotName()`.
- Loader client: `filesystem_loadKitDirectory_tick()`.
- Browser compatibility client: the scan still populates the legacy
  `kitBrowser` map through `filesystem_recordKitDirectory()`.
- Generated-data client: `tools/convert_legacy_kits.py` writes the on-card
  files consumed by these parsers.

Confederate functions:

- `storage_kitsetInit()`
- `storage_kitsetParseLine()`
- `storage_kitsetFinalize()`
- `storage_instrumentFilenameMatchesType()`
- `storage_instrumentTypeFromText()`
- `filesystem_recordKitDirectory()`
- `storage_parseNumberedFolder()`

### Instrument File Morph Endpoint And 64-Byte Padding

Changed code and data:

- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`
- `Core/Hardware/SD/filesystem.c`
- `tools/convert_legacy_kits.py`
- `SD_CARD/Kit/*/*.drm`
- `SD_CARD/Kit/*/*.snr`
- `SD_CARD/Kit/*/*.cym`
- `SD_CARD/Kit/*/*.hat`

Why:

- Future Scene work needs both the normal endpoint and the morph endpoint to be
  represented in generated instrument files.
- Legacy `.SND` files do not contain a distinct morph endpoint, so the safest
  conversion is to seed morph with the same values as the normal endpoint.
- Padding each endpoint to 64 byte-valued entries leaves room to add parameters
  later without immediately breaking older readers that ignore unknown keys.

What changed:

- Converted instruments now write:
  - header metadata: `format`, `version`, `type`, `slot`, plus conversion
    source fields in the instrument file only
  - `[params]` endpoint
  - `_padNN=0` rows through 64 entries
  - `[morph]` endpoint
  - `_padNN=0` rows through 64 entries
- `storage_instrument_state_t` now tracks `seen_morph_count` instead of the
  older boolean-style `seen_morph_data` name.
- The reader writes `[params]` values into `parameter_values[]`.
- The reader writes `[morph]` values into `parameters2[]`.
- If an instrument file has no explicit morph values, the loader calls the
  fallback copier to seed morph from the already-loaded main endpoint.

Inputs:

- Instrument text lines from `.drm`, `.snr`, `.cym`, and `.hat` files.
- Expected instrument type and one-based slot from the validated kitset.
- Generated field maps in `tools/convert_legacy_kits.py`.

Outputs:

- Main endpoint parameter writes to `parameter_values[]`.
- Morph endpoint parameter writes to `parameters2[]`.
- Validation flags in `storage_instrument_state_t`, including
  `seen_param_count` and `seen_morph_count`.

Accessors and clients:

- There is no new public accessor. The output buffers are the existing synth
  parameter arrays:
  - `parameter_values[]` for the active endpoint
  - `parameters2[]` for the morph endpoint
- Runtime client: `filesystem_loadKitDirectory_tick()`.
- Parser client: `storage_instrumentParseLine()`.
- Future save client: the eventual directory-kit save path should emit the same
  `[params]` and `[morph]` sections.

Confederate functions:

- `storage_instrumentStateInit()`
- `storage_instrumentParseLine()`
- `storage_instrumentFinalize()`
- `storage_instrumentCopyMainToMorphFallback()`
- `storage_paramsForInstrument()`
- `storage_findParamMap()`

### Legacy `.SND` Parameter Landing In The Converter

Changed code and data:

- `tools/convert_legacy_kits.py`
- Regenerated `SD_CARD/Kit`

Why:

- The first conversion pass landed several Slak drum 3 and cymbal values at
  zero because the converter used the wrong legacy byte addressing model.
- The legacy payload after the eight-byte name is not simply indexed by all
  current runtime enum values. Main endpoint parameters land at
  `ParamEnums - 1`, while CC2 parameters live in the separate CC2 block.
- `audio_out` exposed the issue first, but it is not special by itself. The
  real split is at `PAR_FILTER_DRIVE_1`, the first parameter after the
  `END OF MIDI DATASIZE` boundary in `ParameterArray.h`.

What changed:

- `parse_param_values()` reads `enum ParamEnums` from `ParameterArray.h` and
  returns the enum table plus `END_OF_SOUND_PARAMETERS`.
- `parse_cc2_values()` reads the CC2 enum names from `MidiMessages.h`. For now,
  only the CC2 enum is used from that file; the main MIDI enum in
  `MidiMessages.h` has drifted and is not used as the split authority.
- `cc2_name_for_param()` derives the split from `ParameterArray.h`:
  - if `param_indexes[name] < PAR_FILTER_DRIVE_1`, read from the main endpoint
    blob at `ParamEnums - 1`
  - otherwise read from `128 + CC2_*`
- `param_value()` centralizes this address calculation for both instrument
  endpoint emission and kitset `audio_out` emission.
- `append_endpoint()` emits one named endpoint plus zero padding to 64 rows.
- `write_instrument()` emits both `[params]` and `[morph]` from the same legacy
  values.
- `write_kitset()` emits only the reduced manifest and reads
  `PAR_AUDIO_OUT1..PAR_AUDIO_OUT6` through the same CC2-aware `param_value()`.

Inputs:

- Legacy `SD_CARD/P000.SND` through `SD_CARD/P032.SND`.
- `Core/Scene/Preset/ParameterArray.h` for current `PAR_*` enum positions and
  the `PAR_FILTER_DRIVE_1` boundary.
- `Core/MIDI/MidiMessages.h` for the CC2 enum names and ordering.
- Converter field maps for drum, snare, cymbal, and hat instruments.

Outputs:

- `SD_CARD/Kit/NNN Name/kitset.kcg`
- `SD_CARD/Kit/NNN Name/*.drm`
- `SD_CARD/Kit/NNN Name/*.snr`
- `SD_CARD/Kit/NNN Name/*.cym`
- `SD_CARD/Kit/NNN Name/*.hat`

Accessors and clients:

- Script functions:
  - `parse_param_values()`
  - `parse_cc2_values()`
  - `param_value()`
  - `append_endpoint()`
  - `write_instrument()`
  - `write_kitset()`
  - `main()`
- Runtime clients are indirect: the generated files are consumed by
  `storage_kitsetParseLine()` and `storage_instrumentParseLine()` through
  `filesystem_loadKitDirectory_tick()`.

Confederate functions and tables:

- `DRUM_PARAM_TEMPLATE`
- `SNARE_PARAMS`
- `CYMBAL_PARAMS`
- `HAT_PARAMS`
- `DRUM_FIELDS`
- `SNARE_FIELDS`
- `CYMBAL_FIELDS`
- `HAT_FIELDS`
- `storageTypes.c` parameter maps, which must stay semantically aligned with
  the converter field names.

Validation performed:

- Clean-replaced `SD_CARD/Kit` by removing the directory and rerunning
  `python3 tools/convert_legacy_kits.py`.
- Regenerated output has 33 kit directories and 198 instrument files.
- Every generated instrument file has exactly 64 `[params]` rows and 64
  `[morph]` rows.
- Every generated kitset omits removed keys: `kit_name`,
  `voice_decimation_all`, `[metadata]`, `source_name`, `source_file`, and
  `legacy_slot`.
- Converter audit found 93 generated parameter references in the CC2 half and
  no missing `CC2_*` counterpart.
- Slak canary after corrected landing:
  - drum 3: `velo_attack=12`, `velo_decay=0`, `vol_slope=7`, `volume=77`,
    `pan=63`, `drive=17`
  - cymbal: `velo_attack=30`, `velo_decay=0`, `vol_slope=4`, `volume=127`,
    `pan=63`, `drive=30`
  - `kitset.kcg` `audio_out` fields are all zero for Slak and are read from
    the CC2 block.

### Documentation And Test State

Changed documentation:

- `PAT_8BAR_SINGLE_AUDIT.md` now contains the boot-lock notes, kitset cleanup
  notes, morph endpoint notes, converter landing notes, and this consolidated
  session addendum.
- Root-level Markdown was swept earlier in the session to remove claims that
  `kitset.kcg` stores the kit name, global voice decimation, or legacy
  conversion metadata.

Known limitations:

- Firmware build was not run because `make` is not installed in this
  environment.
- The generated `tools/__pycache__` directory may remain from Python validation;
  it is intentionally not part of the firmware or SD-card data path and was
  left alone after the user said it can stay.
- `Core/MIDI/MidiMessages.h` appears drifted in its main MIDI enum around the
  MIDI-size boundary, but this session explicitly deferred that cleanup. The
  converter uses `ParameterArray.h` as the authority for the
  `PAR_FILTER_DRIVE_1` split and uses only the CC2 enum from `MidiMessages.h`.
