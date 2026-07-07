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
- Add `led_flashLed()` for the 0.5-second bar SELECT flash:

```c
#define LED_FLASH_DURATION_TIME_MS 500
#define LED_FLASH_CYCLE_TIME_MS    100
void led_flashLed(uint8_t ledNr);
```

  The flash sequence is exactly:
  on for 100 ms, off for 100 ms, on for 100 ms, off for 100 ms, on for 100 ms,
  then return to the original LED state. This is separate from `led_pulseLed()`,
  which remains the existing short temporary inversion, and from the persistent
  blink slots.
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
