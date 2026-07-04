# Remove frontPanelParser.c Audit 2

This replaces the first audit's bad central idea. Do **not** replace
`frontPanelParser.c` with a new generic bridge. The goal is to remove the
processor-protocol shape entirely and route each operation to the module that
will own it in the future architecture.

No production code has been changed for this audit.

## Future Shape Used For This Audit

After rereading `SCOPING_TARGETS.md`, the relevant ownership model is:

- `Core/Hardware/frontPanel/ledHandler.c`
  owns LED state and LED presentation helpers.
- `Core/Menu/menu.c`
  owns selected UI state such as active voice and viewed pattern.
- `Core/Scene/Pattern/PatternData.c`
  should be created now as the first `Core/Scene` component. It owns current
  pattern storage access, pattern/track/step reads, and pattern/track/step
  mutations. All public functions here use the `pat_` prefix.
- `Core/Scene/Pattern/EuklidGenerator.c` and `SomGenerator.c`
  should move beside `PatternData.c`, because they generate or service pattern
  behavior.
- `Core/Sequencer/sequencer.c`
  remains the timing/playback scheduler for now. Later sessions can move more
  pattern playback code out, but this session should move the pattern data API
  and stop routing through opcodes.
- `Core/Preset/`
  owns sound-parameter apply/morph/kit-load behavior for now. It will
  eventually move under `Core/Scene/`, but that can wait.
- `Core/MIDI/MidiParser.c`
  owns MIDI channels, MIDI filters/routing, CC parsing, and MIDI-originated
  automation recording.
- `Core/SampleRom/`
  owns sample-memory state. The old sample opcode is probably obsolete.

Hard rule: if a value was only being read or written through an opcode, replace
that with direct state access or an owner-specific API. There is no split
processor anymore.

## Current Source Findings

- `Core/MIDI/frontPanelParser.c` is 816 lines.
- There are 93 live `frontPanel_sendData()` call sites:
  - `Core/Menu/menu.c`: 55
  - `Core/Hardware/frontPanel/buttonHandler.c`: 35
  - `Core/Preset/presetManager.c`: 3
- The old parser state is mostly redundant:
  - `frontParser_activeTrack` duplicates `menu_activeVoice` /
    `menu_getActiveVoice()`.
  - `frontParser_shownPattern` duplicates `menu_shownPattern` /
    `menu_getViewedPattern()`.
  - `frontParser_activeStep` duplicates `seq_selectedStep` and
    `parameter_values[PAR_ACTIVE_STEP]`; with `PatternData`, the selected step
    should be passed explicitly or stored as pattern edit state.
  - `frontParser_midiMsg` is only used by one live call site in
    `buttonHandler.c`, and that usage can be removed because the old
    `SEQ_REQUEST_PATTERN_PARAMS` payload was ignored anyway.
  - sysex parser buffers and `frontParser_newSeqDataAvailable` are not used by
    live code outside the parser.
- `SAMPLE_CC/SAMPLE_COUNT` has no live send call. Runtime sample count is
  already set from `sampleMemory_getNumSamples()` in `main.c` and `menu.c`.
- `seq_sendStepInfoToFront()` and `seq_sendMainStepInfoToFront()` only write
  bytes to the no-op front-panel UART sysex path. No live source calls them.

## New Files To Create

### `Core/Scene/Pattern/PatternData.h`

Create this as the public API for current pattern storage and pattern edit/read
operations.

Move these type definitions from `sequencer.h`:

- `Step`
- `PatternSetting`
- `LengthRotate`
- `PatternSet`
- `TempPattern`

Move these constants if no non-pattern owner needs to define them:

- `NUM_TRACKS`
- `NUM_PATTERN`
- `NUM_STEPS`
- `STEP_ACTIVE_MASK`
- `STEP_VOLUME_MASK`
- `SEQ_DEFAULT_NOTE`
- `SEQ_NEXT_RANDOM`
- `SEQ_NEXT_RANDOM_PREV`

If keeping a small subset in `sequencer.h` avoids churn, `PatternData.h` can
include `sequencer.h` temporarily, but the destination should be PatternData
owning the pattern data model.

Declare these globals here, not in `sequencer.h`:

- `extern PatternSet pat_patternSet;`
- `extern TempPattern pat_tmpPattern;`

During the transition, one of two compatibility approaches is acceptable:

- Rename globals now to `pat_patternSet` / `pat_tmpPattern` and update all
  users.
- Or define compatibility macros in `PatternData.h`:
  - `#define seq_patternSet pat_patternSet`
  - `#define seq_tmpPattern pat_tmpPattern`

The first option is cleaner. The macro option reduces churn while still moving
ownership out of `sequencer.c`.

### `Core/Scene/Pattern/PatternData.c`

Own these definitions:

- `PatternSet pat_patternSet;`
- `TempPattern pat_tmpPattern;`

Implement current-pattern-data functions with `pat_` prefix.

Minimum API needed to remove `frontPanelParser.c`:

```c
uint8_t pat_trackValid(uint8_t track);
uint8_t pat_patternValid(uint8_t pattern);
uint8_t pat_stepValid(uint8_t step);

Step *pat_stepPtr(uint8_t pattern, uint8_t track, uint8_t step);
uint16_t *pat_mainStepsPtr(uint8_t pattern, uint8_t track);
PatternSetting *pat_patternSettingPtr(uint8_t pattern);
LengthRotate *pat_lengthRotatePtr(uint8_t pattern, uint8_t track);

void pat_init(void);

uint8_t pat_isStepActive(uint8_t track, uint8_t step, uint8_t pattern);
uint8_t pat_isMainStepActive(uint8_t track, uint8_t mainStep, uint8_t pattern);
void pat_toggleStep(uint8_t track, uint8_t step, uint8_t pattern);
void pat_toggleMainStep(uint8_t track, uint8_t mainStep, uint8_t pattern);

void pat_setStepNote(uint8_t pattern, uint8_t track, uint8_t step, uint8_t note);
void pat_setStepVolume(uint8_t pattern, uint8_t track, uint8_t step, uint8_t volume);
void pat_setStepProbability(uint8_t pattern, uint8_t track, uint8_t step, uint8_t prob);

void pat_setStepAutomationDestination(uint8_t pattern, uint8_t track,
                                      uint8_t step, uint8_t slot,
                                      uint16_t targetParam);
void pat_setStepAutomationValue(uint8_t pattern, uint8_t track,
                                uint8_t step, uint8_t slot,
                                uint8_t value);

void pat_setPatternChangeBar(uint8_t pattern, uint8_t value);
void pat_setPatternNext(uint8_t pattern, uint8_t value);
uint8_t pat_getPatternChangeBar(uint8_t pattern);
uint8_t pat_getPatternNext(uint8_t pattern);

void pat_setTrackLength(uint8_t pattern, uint8_t track, uint8_t length);
uint8_t pat_getTrackLength(uint8_t pattern, uint8_t track);
void pat_setTrackRotation(uint8_t pattern, uint8_t track, uint8_t rotation,
                          uint8_t sequencerRunning, int8_t *runtimeStepIndex);
uint8_t pat_getTrackRotation(uint8_t pattern, uint8_t track);
void pat_setShuffle(uint8_t pattern, uint8_t value);
uint8_t pat_getShuffle(uint8_t pattern);

void pat_clearTrack(uint8_t pattern, uint8_t track);
void pat_clearPattern(uint8_t pattern);
void pat_clearAutomation(uint8_t pattern, uint8_t track, uint8_t automTrack);
void pat_copyTrack(uint8_t pattern, uint8_t srcTrack, uint8_t dstTrack);
void pat_copyPattern(uint8_t srcPattern, uint8_t dstPattern);

void pat_applyStepToMenu(uint8_t pattern, uint8_t track, uint8_t step);
void pat_applyPatternSettingsToMenu(uint8_t pattern);
void pat_applyTrackSettingsToMenu(uint8_t pattern, uint8_t track);
```

Notes:

- `pat_applyStepToMenu()` replaces `SEQ_REQUEST_STEP_PARAMS`.
- `pat_applyPatternSettingsToMenu()` replaces `SEQ_REQUEST_PATTERN_PARAMS`.
- `pat_applyTrackSettingsToMenu()` covers the non-LED side effect hidden inside
  `LED_QUERY_SEQ_TRACK`: updating `PAR_TRACK_LENGTH` and
  `PAR_TRACK_ROTATION`.
- `pat_setTrackRotation()` needs special care because current
  `seq_setTrackRotation()` adjusts runtime `seq_stepIndex[]` when the
  sequencer is running. Move the public mutation interface to
  `pat_setTrackRotation()` now and preserve the current compensation logic when
  that mutation is applied. This logic will change later, but the API belongs
  under `Core/Scene/Pattern/`.
- Shuffle should get `pat_setShuffle()` / `pat_getShuffle()` now. The value may
  still be backed by the existing sequencer/global storage during this
  transition, but the interface should be pattern-owned because shuffle should
  eventually be per-pattern or per-track-pattern.

## Files To Move

### Move Euklid files

Move:

- `Core/Sequencer/EuklidGenerator.c`
- `Core/Sequencer/EuklidGenerator.h`

To:

- `Core/Scene/Pattern/EuklidGenerator.c`
- `Core/Scene/Pattern/EuklidGenerator.h`

Then update includes and `Makefile`.

Inside `EuklidGenerator.c`, replace direct `seq_patternSet` writes with
PatternData calls:

- `seq_patternSet.seq_mainSteps[pattern][track] = euklid_patternBuffer`
  becomes a `pat_setMainSteps(pattern, track, bits)` helper or equivalent.
- `seq_patternSet.seq_patternLengthRotate[pattern][track].length = len`
  becomes `pat_setTrackLengthRaw()` or a helper that preserves the old
  zero-means-16 encoding.

Why:

Euklid generates pattern data. It belongs with PatternData, not in the
sequencer scheduler and not in a parser.

### Move SOM files

Move:

- `Core/Sequencer/SomGenerator.c`
- `Core/Sequencer/SomGenerator.h`
- `Core/Sequencer/SomData.c`
- `Core/Sequencer/SomData.h`

To:

- `Core/Scene/Pattern/SomGenerator.c`
- `Core/Scene/Pattern/SomGenerator.h`
- `Core/Scene/Pattern/SomData.c`
- `Core/Scene/Pattern/SomData.h`

Then update includes and `Makefile`.

Why:

SOM is a pattern/generator performance mode. It can still call
`seq_triggerVoice()` during playback for now, but its data/control ownership is
part of the Pattern/Scene direction.

SOM frequency decision:

- `PAR_SOM_FREQ` should call `som_setFreq(value, menu_getActiveVoice())`
  directly from `menu.c`. Do not preserve the old parser's packed high-nibble
  voice / low-nibble frequency decode for this menu path.

## Opcode-To-Owner Map

### LED opcodes -> `ledHandler.c`

Add these to `ledHandler.h/c`:

```c
void led_updateCurrentStep(uint8_t step);
void led_updateRecordedMainStep(uint8_t activeTrack,
                                uint8_t shownPattern,
                                uint8_t subStep);
void led_updateRecordedSubStep(uint8_t activeTrack,
                               uint8_t shownPattern,
                               uint8_t step,
                               uint8_t selectedStepBase,
                               uint8_t shiftHeld,
                               uint8_t selectMode);
void led_updatePatternTrack(uint8_t track, uint8_t pattern,
                            uint8_t selectedStepBase);
void led_setBeatPulse(uint8_t on);
```

Mapping:

- `LED_CURRENT_STEP_NR` -> `led_updateCurrentStep(step)`
- `LED_SEQ_BUTTON` -> `led_updateRecordedMainStep(...)`
- `LED_SEQ_SUB_STEP` -> `led_updateRecordedSubStep(...)`
- `LED_QUERY_SEQ_TRACK` -> `led_updatePatternTrack(...)` plus
  `pat_applyTrackSettingsToMenu(pattern, track)`
- `LED_PULSE_BEAT` -> `led_setBeatPulse(on)`

Why these belong in `ledHandler.c`:

They are display decisions about which physical LEDs represent current pattern
state. `ledHandler.c` already includes `menu.h` and owns higher-level LED
helpers like `led_initPerformanceLeds()`, `led_setActive_step()`, and
`led_clearSequencerLeds()`.

Implementation detail:

- `led_updatePatternTrack()` may include `PatternData.h` so it can call
  `pat_isMainStepActive()` and `pat_isStepActive()`.
- Do **not** update menu parameter values from LED helpers. The current parser
  mixed LED refresh and track-setting menu refresh in `LED_QUERY_SEQ_TRACK`;
  split that side effect into an explicit adjacent `pat_applyTrackSettingsToMenu()`
  call at each caller.

### Pattern/track/step opcodes -> `Core/Scene/Pattern/PatternData.c`

Mapping:

- `SEQ_REQUEST_STEP_PARAMS` -> `pat_applyStepToMenu(pattern, track, step)`
- `SEQ_REQUEST_PATTERN_PARAMS` -> `pat_applyPatternSettingsToMenu(pattern)`
- `SEQ_SET_PAT_BEAT` -> `pat_setPatternChangeBar(pattern, value)`
- `SEQ_SET_PAT_NEXT` -> `pat_setPatternNext(pattern, value)`
- `SEQ_SELECT_ACTIVE_STEP` -> set `parameter_values[PAR_ACTIVE_STEP]` and
  `seq_selectedStep` directly, or move selected edit step into PatternData as
  `pat_setSelectedStep(step)`.
- `SEQ_NOTE` -> `pat_setStepNote(pattern, track, step, value)`
- `SEQ_VOLUME` -> `pat_setStepVolume(pattern, track, step, value)`
- `SEQ_PROB` -> `pat_setStepProbability(pattern, track, step, value)`
- `SEQ_TRACK_LENGTH` -> `pat_setTrackLength(pattern, track, value)`
- `SEQ_TRACK_ROTATION` -> `pat_setTrackRotation(pattern, track, value, ...)`
  with the current runtime compensation logic preserved inside the PatternData
  mutation path.
- `MAIN_STEP_CC` -> `pat_toggleMainStep(pattern, track, mainStep)`
- `STEP_CC` -> `pat_toggleStep(pattern, track, step)`
- `SET_P1_DEST` -> `pat_setStepAutomationDestination(..., slot 0, target)`
- `SET_P2_DEST` -> `pat_setStepAutomationDestination(..., slot 1, target)`
- `SET_P1_VAL` -> `pat_setStepAutomationValue(..., slot 0, value)`
- `SET_P2_VAL` -> `pat_setStepAutomationValue(..., slot 1, value)`
- `ARM_AUTOMATION_STEP` -> move automation-arm state into PatternData:
  `pat_armAutomationStep(step, track, armed)`

Why:

These all read or mutate pattern data or pattern-edit state. They should land
in the new Scene/Pattern API now, even if `sequencer.c` still consumes the old
storage layout during playback.

### Sequencer transport/playback opcodes -> `sequencer.c`

Mapping:

- `SEQ_SHUFFLE` -> `pat_setShuffle(pattern, value)`, even if the first
  implementation still forwards to or writes the existing sequencer shuffle
  storage.
- `SEQ_SET_QUANT` -> `seq_setQuantisation(value)`
- `SEQ_ROLL_RATE` -> `seq_setRollRate(value)`
- `SEQ_ROLL_ON_OFF` -> `seq_setRoll(voice, onOff)`
- `SEQ_CHANGE_PAT` -> `seq_setNextPattern(pattern)` for current behavior.
  Future scene switching will replace this, but not in this parser removal.
- `SEQ_RUN_STOP` -> `seq_setRunning(running)`
- `SEQ_REC_ON_OFF` -> `seq_setRecordingMode(active)`
- `SEQ_ERASE_ON_OFF` -> `seq_setErasingMode(active)`
- `SEQ_MUTE_TRACK` -> `seq_setMute(track, 1)`
- `SEQ_UNMUTE_TRACK` -> `seq_setMute(track, 0)`
- `SET_BPM` -> `seq_setBpm(bpm)`

Why:

These operate transport, playback timing, mute state, or the current scheduler.
They are not pattern storage API, even if playback later gets more
PatternData-backed.

### Euklid opcodes -> moved `EuklidGenerator.c`

Mapping:

- `SEQ_REQUEST_EUKLID_PARAMS` -> direct reads from euklid into menu:
  `parameter_values[PAR_EUKLID_LENGTH] = euklid_getLength(track)`, etc.
- `SEQ_EUKLID_LENGTH` -> `euklid_setLength(track, length)`
- `SEQ_EUKLID_STEPS` -> `euklid_setSteps(track, steps, pattern)`
- `SEQ_EUKLID_ROTATION` -> `euklid_setRotation(track, rotation, pattern)`

Why:

The euklid module owns its generator parameters. It writes generated pattern
bits through PatternData after it moves.

### SOM opcodes -> moved `SomGenerator.c`

Mapping:

- `SEQ_POSX` -> `som_setX(value)`
- `SEQ_POSY` -> `som_setY(value)`
- `SEQ_FLUX` -> `som_setFlux((float)value / 127.0f)`
- `SEQ_SOM_FREQ` -> `som_setFreq(value, menu_getActiveVoice())`

Decision:

The menu path is direct: `menu.c` line 2386 can call `som_setFreq()` directly.
Do not keep the parser's packed-byte interpretation for this call path.

### MIDI opcodes -> `MidiParser.c`

Mapping:

- `SEQ_MIDI_CHAN` -> add `midiParser_setChannel(uint8_t voice, uint8_t channel)`.
  It should preserve current behavior: if `voice < 7` and the channel changes,
  call `voiceControl_noteOff(voice)`, then write `midi_MidiChannels[voice]`.
- `SEQ_MIDI_ROUTING` -> `midiParser_setRouting(value)`
- `SEQ_MIDI_FILT_TX` -> `midiParser_setFilter(1u, value)`
- `SEQ_MIDI_FILT_RX` -> `midiParser_setFilter(0u, value)`
- `MIDI_CC` / `CC_2` -> do **not** keep these status names. Sound-parameter
  apply belongs in Preset for this refactor; MIDI-originated CC parsing remains
  in `MidiParser.c`.

Why:

MIDI channel/filter/routing state is MIDI parser state. The local menu writing
that state should call MIDI parser APIs directly.

### Trigger opcodes -> `triggerJacks.c`

Mapping:

- `SEQ_TRIGGER_IN_PPQ` -> `triggerJacks_setClockInputPpq(value)`
- `SEQ_TRIGGER_OUT1_PPQ` -> `triggerJacks_setClockOut1Ppq(value)`
- `SEQ_TRIGGER_OUT2_PPQ` -> `triggerJacks_setClockOut2Ppq(value)`
- `SEQ_TRIGGER_GATE_MODE` -> `trigger_setGatemode(value)`

Why:

These are jack/backend settings, not sequencer protocol messages.

### Sound parameter opcodes -> `Core/Preset/`

Add to `presetManager.h/c` or a new `Core/Preset/presetApply.c/h`:

```c
void preset_applySoundParameter(uint16_t paramNr, uint8_t value,
                                uint8_t recordAutomation);
void preset_applyVelocityModTarget(uint8_t voice, uint16_t targetParam);
void preset_applyLfoModTarget(uint8_t lfo, uint16_t targetParam);
```

Mapping:

- `MIDI_CC` -> `preset_applySoundParameter(paramNr, value, 1)`
- `CC_2` -> `preset_applySoundParameter(paramNr, value, 1)`
- `VOICE_CC/VOICE_DECIMATION` -> `preset_applySoundParameter(paramNr, value, 1)`
- `CC_VELO_TARGET` -> `preset_applyVelocityModTarget(voice, targetParam)`
- `CC_LFO_TARGET` -> `preset_applyLfoModTarget(lfo, targetParam)`

Why:

The user-facing/local sound parameter path is conceptually Preset/Scene state:
kit parameters, morph, and sound apply. `MidiParser.c` is still used internally
to apply the current old CC table where needed, but callers should not pack
fake MIDI protocol bytes to get there.

`preset_applySoundParameter()` can internally create the `MidiMsg` and call
`midiParser_ccHandler()` for now, because that is still the current DSP
parameter application implementation. The important part is that callers pass
`paramNr` directly, not `(status, data1, data2)`.

Important edge:

- Keep the parameter 127 guard. Current morph skips index 127 because the old
  CC encoding wraps it to CC0 and can underflow in `midiParser_ccHandler()`.
  The new Preset helper should reject or skip `paramNr == 127` centrally.

Automation recording:

- If `recordAutomation` is true, call the PatternData automation-record API
  after applying the parameter.
- This replaces parser-side `seq_recordAutomation(frontParser_activeTrack, ...)`.
- The recording target should use `menu_getActiveVoice()` for current behavior,
  until the Phase 3/4 record-to-track model exists.

### Sample opcode -> `Core/SampleRom/` or remove as dead

Mapping:

- `SAMPLE_CC/SAMPLE_COUNT` currently only called `menu_setNumSamples(data2)`.
- No live source sends this opcode.
- Current runtime paths already use `sampleMemory_getNumSamples()`:
  - `main.c` sets the menu count at boot.
  - `menu.c` updates it around sample install/load UI.

Recommendation:

Remove this opcode path with no replacement unless a real caller is found
during implementation. If a future caller needs it, it should call
`menu_setNumSamples(sampleMemory_getNumSamples())` or a SampleRom-owned helper,
not a parser opcode.

## Per-File Implementation Plan

### `Core/MIDI/frontPanelParser.c`

Delete this file after all callers are converted.

Things not to preserve:

- `frontPanel_sendData()`
- `frontPanel_sendMidiMsg()`
- `frontPanel_sendByte()`
- `frontPanel_parseData()`
- `frontParser_parseUartData()`
- `frontParser_*` sysex globals
- `frontParser_midiMsg`
- front-panel UART stubs

Things to move to owners:

- LED display logic -> `ledHandler.c`
- pattern/track/step reads and writes -> `PatternData.c`
- euklid parameter reads/writes -> moved `EuklidGenerator.c`
- SOM reads/writes -> moved `SomGenerator.c`
- MIDI channel/filter/routing -> `MidiParser.c`
- sound-parameter apply -> `Preset`
- reverse run/pattern/rotation UI updates -> direct owner calls described below
- `SeqLedState` -> likely `ledHandler.c/h`, because it is a deferred LED
  presentation buffer

### `Core/MIDI/frontPanelParser.h`

Delete this header.

Before deletion, move any still-needed constants/types:

- `SeqLedState` and dirty flags -> `ledHandler.h`
- If short opcode constants are only used during conversion, remove them after
  conversion.
- Do not keep short `SEQ_CC`, `LED_CC`, `CC_2`, etc. aliases in a new header.

### `Core/Hardware/frontPanel/ledHandler.c/h`

Add LED helpers listed above.

Move `SeqLedState` here:

```c
typedef struct {
    volatile uint8_t dirty;
    volatile uint8_t chaseStep;
    volatile uint8_t beatPulse;
    volatile uint8_t recordSubStep;
    volatile uint8_t recordMainStep;
} SeqLedState;

extern SeqLedState seq_ledState;
void led_processSeqLedState(void);
```

Then:

- `main.c` calls `led_processSeqLedState()` instead of
  `seq_ledState_process()`.
- `sequencer.c` continues to write `seq_ledState`, but includes
  `ledHandler.h`.

Why:

This is not a generic bridge. It is a deferred LED update buffer, and LEDs are
the owner.

Add a helper for pattern-change display:

```c
void led_notifyPatternChanged(uint8_t playedPattern);
```

This can replace the old `seq_notifyFront(SEQ_CHANGE_PAT)` UI side:

- set `menu_playedPattern = playedPattern`
- if follow mode is enabled, call `menu_setShownPattern(playedPattern)`, clear
  sequencer LEDs, and refresh track LEDs via `led_updatePatternTrack(...)`
- if in performance mode, clear blink LEDs and call `led_initPerformanceLeds()`

This function lives in `ledHandler.c` because the behavior is UI/LED
presentation. It can call `menu_*` because `ledHandler.c` already includes
`menu.h`.

Add a helper for rotation display:

```c
void led_notifyTrackRotationReset(uint8_t rotation);
```

This can simply set `parameter_values[PAR_TRACK_ROTATION] = rotation`, or the
assignment can be in PatternData/Menu if preferred. I would put it in
PatternData if it is considered pattern edit state; I would put only LED-visible
behavior in `ledHandler`.

### `Core/Scene/Pattern/PatternData.c/h`

Create the folder and files now.

Move from `sequencer.c`:

- `PatternSet seq_patternSet`
- `TempPattern seq_tmpPattern`
- `seq_resetNote()` as private `pat_resetStep()`
- most of `seq_init()`'s pattern initialization loop into `pat_init()`
- `seq_toggleStep()` -> `pat_toggleStep()`
- `seq_toggleMainStep()` -> `pat_toggleMainStep()`
- `seq_isStepActive()` -> `pat_isStepActive()`
- `seq_isMainStepActive()` -> `pat_isMainStepActive()`
- `seq_clearTrack()` -> `pat_clearTrack()`
- `seq_clearPattern()` -> `pat_clearPattern()`
- `seq_clearAutomation()` -> `pat_clearAutomation()`
- `seq_copyTrack()` -> `pat_copyTrack()`
- `seq_copyPattern()` -> `pat_copyPattern()`
- stored length/rotation accessors
- pattern setting accessors
- step note/velocity/probability/automation setters
- menu apply helpers for step/pattern/track settings

Keep wrappers in `sequencer.c/h` only where they are still scheduler concepts or
needed to avoid a too-large first pass:

- `seq_init()` calls `pat_init()` and still initializes automation nodes.
- `seq_recordAutomation()` may remain temporarily, but it should delegate data
  writes to a `pat_recordAutomation(...)` helper. Better: move arm/record state
  into PatternData now if practical.

Move `seq_setTrackRotation()` into PatternData now as `pat_setTrackRotation()`.
The implementation must carry over the current runtime compensation against
`seq_stepIndex[]` when the sequencer is running. If that requires a small
sequencer runtime hook, keep the hook narrow and private to the scheduler; the
public edit interface still belongs in PatternData.

Update all direct data users:

- `filesystem.c` should include `PatternData.h` and call `pat_*` accessors
  instead of reading `seq_patternSet` / `seq_tmpPattern` directly.
- `copyClearTools.c` should call `pat_clearTrack`, `pat_clearPattern`,
  `pat_clearAutomation`, `pat_copyTrack`, and `pat_copyPattern`.
- `EuklidGenerator.c` should call PatternData setters after it moves.
- `ledHandler.c` should call PatternData query helpers for LED refresh.
- `menu.c` should call PatternData menu-apply and mutation helpers.

### `Core/Sequencer/sequencer.c/h`

Replace `#include "frontPanelParser.h"` with:

- `PatternData.h`
- `ledHandler.h`
- `menu.h` only where the scheduler truly needs active UI state

Specific changes:

1. `seq_init()`
   - call `pat_init()` for pattern data initialization.
   - keep automation node initialization in `sequencer.c`.

2. `seq_triggerVoice()`
   - replace direct step pointer with `pat_stepPtr(seq_activePattern, voiceNr,
     seq_stepIndex[voiceNr])`.
   - replace velocity read with a PatternData helper or the returned `Step *`.

3. `seq_determineNextPattern()`
   - use `pat_patternSettingPtr(seq_activePattern)`.

4. `seq_nextStep()`
   - replace pattern length/main-step/step/prob/note reads with PatternData
     helpers or pointers.
   - replace `frontParser_activeTrack` in erase/chase logic with
     `menu_getActiveVoice()`.
   - replace `seq_notifyFront(FRONT_SEQ_CHANGE_PAT, ...)` with
     `led_notifyPatternChanged(seq_activePattern)` or a direct menu/LED pair.
   - write `seq_ledState` from `ledHandler.h`.

5. `seq_setRunning()`
   - reset stored rotation through `pat_setTrackRotation()`.
   - replace `seq_notifyFront(FRONT_SEQ_TRACK_ROTATION, ...)` with the direct
     menu/pattern update needed after PatternData resets rotation.

6. Delete front-panel sysex functions:
   - `seq_sendMainStepInfoToFront()`
   - `seq_sendStepInfoToFront()`
   - remove prototypes from `sequencer.h`

7. Replace record LED visibility condition:
   - old: `frontParser_shownPattern == targetPattern &&
     frontParser_activeTrack == trackNr`
   - new: `menu_getViewedPattern() == targetPattern &&
     menu_getActiveVoice() == trackNr`

8. Keep transport/playback APIs in `sequencer.h`.
   - `seq_setRunning`
   - `seq_setBpm`
   - keep shuffle storage only as an implementation detail if needed; expose
     user-facing set/get through `pat_setShuffle()` / `pat_getShuffle()`
   - `seq_setQuantisation`
   - `seq_setRoll`
   - `seq_setRollRate`
   - `seq_setNextPattern`
   - `seq_setMute`
   - `seq_setRecordingMode`
   - `seq_setErasingMode`

### `Core/Menu/menu.c`

Replace `#include "frontPanelParser.h"` with owner headers:

- `PatternData.h`
- moved `EuklidGenerator.h`
- moved `SomGenerator.h`
- `triggerJacks.h`
- `MidiParser.h`
- `presetManager.h`

Specific call replacements:

1. Pattern params after sound/pattern loads:
   - `frontPanel_sendData(SEQ_CC, SEQ_REQUEST_PATTERN_PARAMS, 0)`
   - becomes `pat_applyPatternSettingsToMenu(menu_getViewedPattern())`

2. Sound parameter sends:
   - `CC_VELO_TARGET` -> `preset_applyVelocityModTarget(...)`
   - `CC_LFO_TARGET` -> `preset_applyLfoModTarget(...)`
   - `MIDI_CC` / `CC_2` -> `preset_applySoundParameter(paramNr, value, 1)`

3. Voice-page LED query:
   - `LED_QUERY_SEQ_TRACK` -> `led_updatePatternTrack(track, pattern,
     buttonHandler_selectedStep)` and `pat_applyTrackSettingsToMenu(pattern,
     track)`

4. MIDI channel globals:
   - `SEQ_MIDI_CHAN` -> `midiParser_setChannel(voice, channel)`

5. SOM:
   - `PAR_POS_X` -> `som_setX(value)`
   - `PAR_POS_Y` -> `som_setY(value)`
   - `PAR_FLUX` -> `som_setFlux((float)value / 127.0f)`
   - `PAR_SOM_FREQ` -> `som_setFreq(value, menu_getActiveVoice())`

6. Track length:
   - `SEQ_TRACK_LENGTH` -> `pat_setTrackLength(menu_getViewedPattern(),
     menu_getActiveVoice(), value)`

7. Shuffle / automation lane / quantization:
   - `SEQ_SHUFFLE` -> `pat_setShuffle(menu_getViewedPattern(), value)`
   - `SEQ_SET_AUTOM_TRACK` -> PatternData automation-track setter if moved, or
     `seq_setActiveAutomationTrack(value)` temporarily
   - `SEQ_SET_QUANT` -> `seq_setQuantisation(value)`

8. Step automation:
   - active-step select -> direct `parameter_values[PAR_ACTIVE_STEP] = value`
     plus `seq_selectedStep = value` or `pat_setSelectedStep(value)`
   - P1/P2 destination -> `pat_setStepAutomationDestination(...)`
   - P1/P2 value -> `pat_setStepAutomationValue(...)`

9. BPM:
   - `SET_BPM` packing -> `seq_setBpm(value)`

10. Voice decimation:
   - `VOICE_CC/VOICE_DECIMATION` -> `preset_applySoundParameter(paramNr,
     value, 1)`

11. Roll:
   - `SEQ_ROLL_RATE` -> `seq_setRollRate(value)`

12. Euklid:
   - direct calls to `euklid_setLength`, `euklid_setSteps`,
     `euklid_setRotation`
   - after generation, call `led_updatePatternTrack()` if generated pattern is
     currently viewed.

13. Pattern settings:
   - `SEQ_SET_PAT_BEAT` -> `pat_setPatternChangeBar(menu_getViewedPattern(),
     value)`
   - `SEQ_SET_PAT_NEXT` -> `pat_setPatternNext(menu_getViewedPattern(), value)`

14. Step note/prob/volume:
   - `SEQ_REQUEST_STEP_PARAMS` -> `pat_applyStepToMenu(menu_getViewedPattern(),
     menu_getActiveVoice(), value)`
   - `SEQ_PROB` -> `pat_setStepProbability(...)`
   - `SEQ_NOTE` -> `pat_setStepNote(...)`
   - `SEQ_VOLUME` -> `pat_setStepVolume(...)`

15. MIDI routing/filter:
   - direct `midiParser_setRouting` / `midiParser_setFilter`

16. Trigger settings:
   - direct triggerJacks calls

17. Bar reset:
   - direct `seq_resetBarOnPatternChange = value`

18. `menu_setShownPattern()`
   - remove parser send.
   - `menu_shownPattern` is the state. Other code should read
     `menu_getViewedPattern()`.

19. `menu_init()`
   - remove parser sends for shown pattern and active track.
   - set `menu_shownPattern = 0` and `menu_activeVoice = 0` directly if needed.

### `Core/Hardware/frontPanel/buttonHandler.c`

Replace `#include "frontPanelParser.h"` with owner headers:

- `PatternData.h`
- `ledHandler.h`
- moved `EuklidGenerator.h`
- `sequencer.h`
- `presetManager.h` for sound parameter reset apply if needed

Specific replacements:

1. `buttonHandler_updateSubSteps()`
   - `LED_QUERY_SEQ_TRACK` -> `led_updatePatternTrack(track, pattern,
     buttonHandler_selectedStep)` plus `pat_applyTrackSettingsToMenu(pattern,
     track)` if menu values should refresh.

2. Long-press automation arm/disarm:
   - `ARM_AUTOMATION_STEP` -> PatternData automation arm API, e.g.
     `pat_armAutomationStep(step, menu_getActiveVoice(), armed)`.

3. Reset-lock sound parameter restore:
   - `MIDI_CC` / `CC_2` -> `preset_applySoundParameter(originalParam,
     originalValue, 1)`.

4. Main-step selection:
   - `SEQ_REQUEST_STEP_PARAMS` -> `pat_applyStepToMenu(pattern, track, step)`.

5. Main-step toggle:
   - `MAIN_STEP_CC` -> `pat_toggleMainStep(pattern, track, mainStep)` then
     update that LED directly from `pat_isMainStepActive(...)`.

6. Track rotation:
   - `SEQ_TRACK_ROTATION` -> `pat_setTrackRotation(menu_getViewedPattern(),
     menu_getActiveVoice(), seqButtonPressed, ...)`.
   - Preserve the current runtime compensation logic when the rotation mutation
     is applied.

7. Manual roll:
   - `SEQ_ROLL_ON_OFF` -> `seq_setRoll(seqButtonPressed, onOff)`.

8. Patgen enter:
   - `SEQ_REQUEST_EUKLID_PARAMS` -> read `euklid_get*()` into
     `parameter_values[]`.

9. Sub-step toggle:
   - `STEP_CC` -> `pat_toggleStep(pattern, track, step)` then update visible
     select LED directly.
   - `SEQ_REQUEST_STEP_PARAMS` -> `pat_applyStepToMenu(...)`.

10. Pattern view select:
   - `menu_setShownPattern(selectNr)` remains the state write.
   - `LED_QUERY_SEQ_TRACK` -> `led_updatePatternTrack(...)`
   - `SEQ_REQUEST_PATTERN_PARAMS` -> `pat_applyPatternSettingsToMenu(...)`
   - `SEQ_REQUEST_EUKLID_PARAMS` -> euklid direct reads

11. Performance pattern change:
   - `SEQ_CHANGE_PAT` -> `seq_setNextPattern(selectNr)`.
   - Future scene switching will replace this in a later phase.

12. Copy/clear refreshes:
   - `LED_QUERY_SEQ_TRACK` -> `led_updatePatternTrack(...)`

13. Mute/unmute:
   - `SEQ_MUTE_TRACK` / `SEQ_UNMUTE_TRACK` -> `seq_setMute(...)`.

14. Voice select:
   - `SEQ_SET_ACTIVE_TRACK` removed. `menu_setActiveVoice(voiceNr)` is the
     state write.
   - `SEQ_REQUEST_EUKLID_PARAMS` -> direct euklid reads.

15. Start/stop:
   - `SEQ_RUN_STOP` -> `seq_setRunning(bh_state.seqRunning)`.
   - local button LED state is already set before this call.

16. Record:
   - `SEQ_REC_ON_OFF` -> `seq_setRecordingMode(bh_state.seqRecording)`.

17. Erase:
   - `SEQ_ERASE_ON_OFF` -> `seq_setErasingMode(bh_state.seqErasing)`.

18. Shift patgen follow branch:
   - remove `frontParser_midiMsg.data2`.
   - call `pat_applyPatternSettingsToMenu(menu_getViewedPattern())`.

### `Core/Preset/presetManager.c/h`

Replace parser include with owner headers.

Add/own:

- `preset_applySoundParameter()`
- `preset_applyVelocityModTarget()`
- `preset_applyLfoModTarget()`

Replace:

- `preset_morphSendParameter()` uses `preset_applySoundParameter(index, value,
  1)`.
- Existing private `preset_sendModTarget()` either becomes the implementation
  of the two new public helpers or is deleted.

Why:

Morph and loaded-kit apply are Preset responsibilities. They should not reach a
front-panel protocol or a generic local-control module.

### `Core/MIDI/MidiParser.c/h`

Remove parser include.

Add:

```c
void midiParser_setChannel(uint8_t voice, uint8_t channel);
```

Replace:

- `seq_notifyFront(FRONT_SEQ_RUN_STOP, x)` with direct UI/transport owner call:
  - for button LED state, `buttonHandler_setRunStopState(x)` is acceptable,
    or create a small function in buttonHandler if including it is cleaner.
- `frontParser_activeTrack` with `menu_getActiveVoice()` for current behavior.

Keep:

- MIDI-originated CC parsing in `midiParser_ccHandler()`.
- Global-channel CC automation recording, but route recording to PatternData or
  the temporary sequencer wrapper using `menu_getActiveVoice()`.

### `Core/Menu/copyClearTools.c`

Replace sequencer data calls with PatternData calls:

- `seq_clearAutomation` -> `pat_clearAutomation`
- `seq_clearPattern` -> `pat_clearPattern`
- `seq_clearTrack` -> `pat_clearTrack`
- `seq_copyTrack` -> `pat_copyTrack`
- `seq_copyPattern` -> `pat_copyPattern`

Why:

These are pattern data operations, not transport operations.

### `Core/Hardware/SD/filesystem.c`

Replace direct `seq_patternSet` / `seq_tmpPattern` usage with PatternData
accessors.

Specific changes:

- Include `PatternData.h`, not `sequencer.h`, for pattern structures.
- `filesystem_patternStepPtr()` should call PatternData staging/active access.
- `filesystem_patternMainPtr()` should call PatternData.
- `filesystem_patternSettingPtr()` should call PatternData.
- `filesystem_patternLengthPtr()` should call PatternData.
- Save/load streaming should not reach `seq_patternSet` directly.

Why:

Pattern file serialization is part of Scene/Pattern storage. Filesystem can
stream bytes, but it should not own the pattern data layout by poking sequencer
globals.

### `Core/MIDI/Uart.h`

Remove front-panel UART declarations:

- `initFrontpanelUart`
- `uart_processFront`
- `uart_sendFrontpanelByte`
- `uart_sendFrontpanelSysExByte`
- `uart_clearFrontFifo`
- `uart_checkAndParse`

Then remove the no-op definitions when deleting `frontPanelParser.c`.

Also delete or rewrite any remaining callers:

- `MidiParser.c` has only commented-out `uart_sendFrontpanelByte()` lines.
- `sequencer.c` sysex serializer functions should be deleted.

### `main.c`

Replace parser include with `ledHandler.h` / PatternData include as needed.

Change:

- `seq_ledState_process()` -> `led_processSeqLedState()`

Keep the call in the same main-loop location.

### `Makefile`

Add include path:

- `-ICore/Scene/Pattern`

Add sources:

- `Core/Scene/Pattern/PatternData.c`
- `Core/Scene/Pattern/EuklidGenerator.c`
- `Core/Scene/Pattern/SomGenerator.c`
- `Core/Scene/Pattern/SomData.c`

Remove sources:

- `Core/MIDI/frontPanelParser.c`
- old `Core/Sequencer/EuklidGenerator.c`
- old `Core/Sequencer/SomGenerator.c`
- old `Core/Sequencer/SomData.c`

### `README.md` and `MEMORY.md`

After implementation, update layout notes:

- remove `Core/MIDI/frontPanelParser.c/h`
- add `Core/Scene/Pattern/PatternData.c/h`
- show moved Euklid/SOM files under `Core/Scene/Pattern`
- update warning about reverse `SEQ_CC` feedback; there should be no
  `seq_notifyFront()` left

## Things That Can Probably Be Removed

These appear removable in this session:

- `frontParser_newSeqDataAvailable`
- `frontParser_stepData`
- `frontParser_midiMsg`
- `frontPanel_sysexMode`
- `frontParser_activeFrontTrack`
- `frontParser_sysexActive`
- `frontParser_rxCnt`
- `frontParser_twoByteData`
- `frontParser_sysexBuffer`
- `frontParser_sysexSeqStepNr`
- `frontPanel_sendByte`
- `frontPanel_sendMidiMsg`
- `frontPanel_parseData`
- `frontParser_parseUartData`
- `seq_sendStepInfoToFront`
- `seq_sendMainStepInfoToFront`
- front-panel UART stubs in `Uart.h`
- `SAMPLE_CC/SAMPLE_COUNT` parser handling, unless implementation finds a live
  caller outside the current `rg` results

## Decisions From Follow-Up

1. `PAR_SOM_FREQ`
   - `menu.c` should call `som_setFreq(value, menu_getActiveVoice())`
     directly.
   - Do not preserve the parser's packed-byte decode for this menu path.

2. Track rotation
   - Move `seq_setTrackRotation()` into PatternData now as
     `pat_setTrackRotation()`.
   - Apply the same runtime compensation logic currently used when the mutation
     is applied. This will change later, but the interface belongs under
     `/Pattern/`.

3. Automation recording owner
   - Automation belongs in `PatternData.c`.
   - Current `seq_recordAutomation()` can remain as a temporary wrapper for
     quantization/runtime step-position context, but arm state and data writes
     should move into PatternData.

4. PatternData menu-apply helpers
   - `menu.c` should call `pat_` APIs for reading pattern data into menu edit
     params.

5. Shuffle
   - Add `pat_setShuffle()` / `pat_getShuffle()` now.
   - The value can still live in the existing sequencer storage temporarily,
     but shuffle should be treated as future per-pattern or per-track-pattern
     data, not as a pure sequencer setting.

## Verification

After implementation:

1. Search:
   - `rg -n "frontPanel_send|frontPanel_parse|frontParser_|frontPanelParser" Core main.c Makefile`
   - Expected: no live source hits.

2. Search:
   - `rg -n "SEQ_CC|LED_CC|SET_P1|SET_P2|CC_2|CC_LFO_TARGET|CC_VELO_TARGET|SAMPLE_CC" Core`
   - Expected: no live use of the short parser protocol constants.

3. Search:
   - `rg -n "seq_patternSet|seq_tmpPattern" Core`
   - Expected: only compatibility macros or deliberate transition points. New
     code should use `pat_*`.

4. Build:
   - `make`
   - `make img`

5. Hardware smoke:
   - voice page LED refresh
   - step page main-step/sub-step toggle
   - active step parameter load/edit
   - P1/P2 automation target/value edit
   - long-press automation arm/disarm
   - copy/clear track/pattern/automation
   - euklid generation and LED refresh
   - SOM controls, especially frequency
   - pattern load/save
   - kit/all/performance load sound apply
   - start/stop, record, erase, mute/unmute
