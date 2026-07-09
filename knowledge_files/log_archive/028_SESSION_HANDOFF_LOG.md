# Session 028 Handoff Log — frontPanelParser Removal and Scene/Pattern Split

DATE: 2026-07-05

SESSION GOAL: Remove `Core/MIDI/frontPanelParser.c` according to `SCOPING_TARGETS.md` goal 1.2. First audit the real code deeply, then remove the obsolete parser bridge without replacing it with another generic bridge. Pattern/track/automation work should begin moving under a new `Core/Scene/Pattern/` owner.

COMPLETED: Wrote `REMOVE_FPP_AUDIT.md`, then replaced it with `REMOVE_FPP_AUDIT_2.md` after correcting the architecture direction. Removed `frontPanelParser.c/h`, created `Core/Scene/Pattern/PatternData.c/h`, moved Euklid/SOM files under `Core/Scene/Pattern`, and rewired former front-panel opcodes into direct owner APIs. Added a second comments-only pass after user review to document the new direct-call boundaries in detail.

VERIFIED ON HARDWARE: No. Build-only verification this session.

CHANGES THIS SESSION:
- `REMOVE_FPP_AUDIT.md`: Initial removal audit. Superseded because it proposed a replacement bridge shape that was rejected.
- `REMOVE_FPP_AUDIT_2.md`: Corrected removal plan. Established direct owner calls instead of a bridge; PatternData/Scene ownership for pattern, track, step, automation, Euklid, and SOM; Preset ownership for sound parameter application; MidiParser ownership for MIDI config; ledHandler ownership for LED rendering.
- `Core/MIDI/frontPanelParser.c`: Deleted. The old local parser/dispatcher is gone.
- `Core/MIDI/frontPanelParser.h`: Deleted. Opcode constants and parser exports removed from the build.
- `Core/Scene/Pattern/PatternData.c`: New Pattern owner for pattern storage and edit APIs. Defines `pat_patternSet`, `pat_tmpPattern`, bounded pointer helpers, step/main-step setters, pattern settings, track length/rotation/shuffle APIs, copy/clear APIs, automation arm/record APIs, and menu-apply helpers.
- `Core/Scene/Pattern/PatternData.h`: New public Pattern API and data layout declarations. Provides temporary `seq_patternSet`/`seq_tmpPattern` compatibility macros for remaining playback/filesystem direct layout users.
- `Core/Scene/Pattern/EuklidGenerator.c/h`: Moved from `Core/Sequencer/`. Generator writes main-step masks through PatternData and remains a Pattern generator.
- `Core/Scene/Pattern/SomGenerator.c/h`: Moved from `Core/Sequencer/`. Menu calls SOM setters directly; Sequencer ticks SOM while playback runs.
- `Core/Scene/Pattern/SomData.c/h`: Moved from `Core/Sequencer/` beside the SOM generator.
- `Core/Sequencer/EuklidGenerator.c/h`, `Core/Sequencer/SomGenerator.c/h`, `Core/Sequencer/SomData.c/h`: Deleted from the old Sequencer location.
- `Core/Hardware/frontPanel/ledHandler.c/h`: Added `SeqLedState` dirty-payload ownership and `led_processSeqLedState()`. LED helpers now directly refresh current-step, beat pulse, recorded sub-step/main-step, full pattern track LEDs, pattern-change follow behavior, and track-rotation reset display. The comments-only pass added detailed ownership notes for each helper.
- `main.c`: Main loop now drains `led_processSeqLedState()` in foreground after TIM3-owned sequencer timing. This replaces the old reverse parser feedback path.
- `Core/Hardware/frontPanel/buttonHandler.c`: Replaced parser sends with direct calls to Menu, PatternData, Euklid, Sequencer, Preset, MidiParser, and ledHandler. Step selection/toggling, long-press automation arming, pattern view selection, copy/clear refresh, mute, transport, recording, erasing, roll, performance pattern queueing, Euklid param refresh, and rotation edits now call owner APIs directly. The comments-only pass documented caller context, owner boundary, inputs/outputs, and risks.
- `Core/Menu/menu.c`: Replaced global parameter opcode dispatch with direct owner calls: MidiParser for MIDI channels/routing/filter; SOM for POS_X/POS_Y/FLUX/SOM_FREQ; PatternData for track length/rotation/shuffle, active automation lane, automation destinations/values, pattern settings, step params, and step note/velocity/probability; Sequencer for quantisation/BPM/ext sync/roll/bar reset; Preset for sound parameters and voice decimation; Euklid for PATGEN generation; triggerJacks/trigger for trigger globals. Pattern settings are refreshed through PatternData after load/sound-apply paths.
- `Core/Menu/copyClearTools.c`: Copy/clear actions now call PatternData directly: `pat_clearTrack`, `pat_clearPattern`, `pat_clearAutomation`, `pat_copyTrack`, and `pat_copyPattern`.
- `Core/Preset/presetManager.c/h`: Added direct sound-apply helpers for local UI/preset code: `preset_applySoundParameter`, `preset_applyVelocityModTarget`, and `preset_applyLfoModTarget`. Sound parameters no longer pack fake front-panel protocol bytes. Optional automation recording still routes through Sequencer/PatternData.
- `Core/MIDI/MidiParser.c/h`: Added direct setters `midiParser_setRouting`, `midiParser_setChannel`, and `midiParser_setFilter`. MTC start/stop and timeout now directly update buttonHandler UI transport state and Sequencer running state. Global-channel note/CC recording uses Menu's active voice directly.
- `Core/MIDI/MidiMessages.h`: Removed obsolete front-panel protocol constants/opcode surface. The file now describes MIDI messages only, plus the legacy `MIDI_CC2` internal sound-parameter path.
- `Core/MIDI/Uart.h`: Removed front-panel UART stubs. USART3 is MIDI DIN only.
- `Core/MIDI/MidiVoiceControl.c`: Removed obsolete front-panel/parser include/dependency.
- `Core/Sequencer/sequencer.c/h`: Sequencer now includes PatternData. Playback still reads PatternData storage through compatibility macros in places, but pattern/automation mutations use `pat_*` where introduced. `seq_setRunning()` resets track rotations via `pat_setTrackRotation()` and notifies LED display. `seq_recordAutomation()` writes through PatternData. `seq_offsetTrackStepIndexForRotation()` is the narrow runtime hook used by PatternData when live rotation changes need step-index compensation.
- `Core/Hardware/SD/filesystem.c`: Pattern serializers now reach pattern data through PatternData pointer helpers. Active-pattern load staging uses `PATTERNDATA_STAGING_PATTERN`, leaving Sequencer to commit at a pattern boundary.
- `Makefile`: Added `Core/Scene/Pattern` include path and sources; removed old parser and old Sequencer generator source locations.
- `README.md`: Updated repository layout to show `Core/Scene/Pattern`.
- `MEMORY.md`: Updated layout and current reminders during the functional removal.
- `build/LXRV2_lxr02.img`: Regenerated after the functional removal build.

KNOWN ISSUES INTRODUCED: None known from build verification. Hardware behavior was not tested.

KNOWN ISSUES RESOLVED:
- `Core/MIDI/frontPanelParser.c/h` no longer exists in the live build.
- Front-panel protocol opcodes are no longer the internal UI/sequencer/preset/MIDI boundary.
- Euklid and SOM no longer live under Sequencer; they live with Pattern under `Core/Scene/Pattern`.
- LED reverse feedback no longer routes through parser code; it is a small dirty state consumed by ledHandler in the foreground loop.

NEXT SESSION RECOMMENDED GOAL: Start the next Pattern refactor: shrink the remaining direct `seq_patternSet` / `seq_tmpPattern` compatibility usage in `sequencer.c` and `filesystem.c`, moving more playback-facing reads/writes behind typed PatternData APIs.

BLOCKERS: Hardware test needed. Also, deeper Pattern refactor decisions are still pending for whether shuffle becomes per-pattern or per-track-pattern and how much playback read logic moves into PatternData versus a later Scene layer.

CRITICAL REMINDERS FOR NEXT SESSION:
- Do not reintroduce `frontPanelParser` or a replacement bridge. Use direct owner APIs.
- New UI/pattern edits should call `pat_*`; `seq_patternSet` and `seq_tmpPattern` are compatibility macros only.
- `led_processSeqLedState()` is foreground-only because it reads Menu/button state and writes LED state.
- `seq_offsetTrackStepIndexForRotation()` is a narrow Sequencer runtime hook. UI code should call `pat_setTrackRotation()`, not the Sequencer hook.
- Pattern serializers may use PatternData pointer accessors; avoid whole-file staging in RAM.

## Detailed Notes

### Architecture Correction

The first audit identified opcode groups correctly but initially proposed replacing the old parser with a generic bridge. That was rejected. The corrected architecture is:

- Menu owns visible/edit context such as active voice, viewed pattern, active page, and menu parameter mirrors.
- ledHandler owns physical LED rendering and sequencer LED feedback.
- buttonHandler owns button gestures, held/shift state, selected-step UI state, and direct dispatch to owners.
- PatternData owns pattern, track, step, automation, track length/rotation, and pattern setting storage.
- Euklid and SOM live under Pattern because they generate or service pattern-related data.
- Sequencer owns playback timing, transport, muting, roll, quantization, external sync, MIDI note output timing, and the remaining runtime playback step walk.
- Preset owns sound-parameter application and modulation target application.
- MidiParser owns MIDI config and external MIDI interpretation.
- Filesystem owns serialization and reaches pattern storage through PatternData accessors.

### PatternData

New storage:

- `PatternSet pat_patternSet`
- `TempPattern pat_tmpPattern`

The old names remain as compatibility macros:

- `#define seq_patternSet pat_patternSet`
- `#define seq_tmpPattern pat_tmpPattern`

This is temporary. It keeps the large Sequencer/filesystem playback surface compiling while the front-panel parser is removed. New code should call `pat_*`.

Important PatternData APIs:

- Validation: `pat_trackValid`, `pat_patternValid`, `pat_stepValid`
- Pointer access: `pat_stepPtr`, `pat_mainStepsPtr`, `pat_patternSettingPtr`, `pat_lengthRotatePtr`
- Step/main-step mutation: `pat_setMainStep`, `pat_setMainStepsRaw`, `pat_toggleStep`, `pat_toggleMainStep`
- Step edit fields: `pat_setStepNote`, `pat_setStepVolume`, `pat_setStepProbability`
- Step automation fields: `pat_setStepAutomationDestination`, `pat_setStepAutomationValue`
- Pattern settings: `pat_setPatternChangeBar`, `pat_setPatternNext`
- Track settings: `pat_setTrackLength`, `pat_setTrackRotation`, `pat_setShuffle`
- Copy/clear: `pat_clearTrack`, `pat_clearPattern`, `pat_clearAutomation`, `pat_copyTrack`, `pat_copyPattern`
- Automation record/arm: `pat_setActiveAutomationTrack`, `pat_armAutomationStep`, `pat_recordAutomation`, `pat_recordArmedAutomation`
- Menu refresh: `pat_applyStepToMenu`, `pat_applyPatternSettingsToMenu`, `pat_applyTrackSettingsToMenu`

`pat_setTrackRotation()` preserves the old live-rotation compensation by calling `seq_offsetTrackStepIndexForRotation()` only when the edited pattern is active and Sequencer is running.

Shuffle now has a PatternData-facing API even though playback still consumes a global Sequencer coefficient. This was intentional so future per-pattern or per-track-pattern shuffle can change behind `pat_setShuffle()`.

### LED Feedback

`SeqLedState` moved to ledHandler and contains:

- `dirty`
- `chaseStep`
- `beatPulse`
- `recordSubStep`
- `recordMainStep`

Sequencer writes small payloads and dirty bits while advancing playback or recording. `main.c` calls `led_processSeqLedState()` in the foreground. That function reads Menu/button state and calls LED helpers:

- `led_setBeatPulse()`
- `led_updateCurrentStep()`
- `led_updateRecordedSubStep()`
- `led_updateRecordedMainStep()`

Pattern change feedback is direct:

- Sequencer calls `led_notifyPatternChanged(seq_activePattern)`.
- ledHandler updates `menu_playedPattern`.
- If follow mode is active, ledHandler calls `menu_setShownPattern()`, clears sequencer LEDs, refreshes track LEDs, and refreshes track params through PatternData.

### Button/Menu Direct Calls

Former button opcodes were replaced with direct owner calls:

- START/STOP: `buttonHandler_setRunStopState()` plus `seq_setRunning()`
- REC: `seq_setRecordingMode()`
- SHIFT+COPY erase: `seq_setErasingMode()`
- Mute: `buttonHandler_muteVoice()` plus `seq_setMute()`
- Performance pattern queue: `seq_setNextPattern()`
- Roll: `seq_setRoll()`
- Track rotation: `pat_setTrackRotation()`
- Step selection params: `pat_applyStepToMenu()`
- Main/sub-step toggles: `pat_toggleMainStep()`, `pat_toggleStep()`
- Long-press automation arm: `pat_armAutomationStep()`
- Euklid page refresh: `euklid_getLength()`, `euklid_getSteps()`, `euklid_getRotation()`

Former menu global opcodes now call direct owners:

- MIDI channel/routing/filter: MidiParser
- SOM X/Y/flux/frequency: SomGenerator
- Track length/shuffle/automation lane: PatternData
- Step automation destination/value: PatternData
- Quantization/BPM/ext sync/roll/bar reset: Sequencer
- Voice decimation and other sound params: Preset
- Euklid length/steps/rotation: EuklidGenerator plus LED refresh
- Pattern beat/next and active step/step fields: PatternData
- Trigger prescalers/gate: triggerJacks/trigger

### Preset and Sound Parameters

`preset_applySoundParameter(paramNr, value, recordAutomation)` now owns direct sound parameter application. It updates `parameter_values`, builds the legacy MIDI CC/CC2 internal message for `midiParser_ccHandler()`, and optionally records automation through `seq_recordAutomation()`.

`preset_applyVelocityModTarget()` and `preset_applyLfoModTarget()` replace fake parser protocol packing for velocity and LFO destinations.

Parameter 127 remains guarded because the legacy CC packing maps it to CC0 and causes underflow in the CC handler path.

### MidiParser and Uart

`MidiParser` now exposes direct config setters:

- `midiParser_setRouting()`
- `midiParser_setChannel()`
- `midiParser_setFilter()`

MTC start/stop and timeout update both:

- buttonHandler UI transport bit/LED
- Sequencer playback state

Global-channel note and CC recording uses `menu_getActiveVoice()` directly.

`Uart.h` no longer exports front-panel UART stubs. It is MIDI DIN only.

### Filesystem

Pattern serialization uses PatternData pointer helpers instead of direct `seq_patternSet`/`seq_tmpPattern` ownership. When a file load targets the currently active playing pattern, filesystem writes into `PATTERNDATA_STAGING_PATTERN`; Sequencer commits that buffer at a safe pattern boundary through `seq_activateTmpPattern()`.

This keeps the existing safe active-pattern load behavior while moving storage ownership to PatternData.

### Comment Pass

After functional removal, the user rejected insufficient comments. A comments-only pass expanded comments in the changed code to document:

- Who calls each new direct boundary.
- Why the function lives in that module.
- What inputs and outputs mean.
- What old parser opcode or hidden side effect it replaces.
- What risks remain.
- Which behavior is temporary compatibility for later Pattern/Scene refactors.

Files substantially expanded with explanatory comments:

- `Core/Hardware/frontPanel/ledHandler.c/h`
- `Core/Hardware/frontPanel/buttonHandler.c`
- `Core/Menu/menu.c`
- `Core/Menu/copyClearTools.c`
- `Core/Scene/Pattern/PatternData.c`
- `Core/Scene/Pattern/EuklidGenerator.c`
- `Core/Scene/Pattern/SomGenerator.c`
- `Core/Sequencer/sequencer.c`
- `Core/Preset/presetManager.c`
- `Core/MIDI/MidiParser.c`
- `Core/Hardware/SD/filesystem.c`
- `main.c`

### Verification

Functional removal pass:

```sh
make
make img
rg -n "frontPanel_send|frontPanel_parse|frontParser_|frontPanelParser" Core main.c Makefile README.md MEMORY.md
rg -n "SEQ_CC|LED_CC|SET_P1|SET_P2|CC_2|CC_LFO_TARGET|CC_VELO_TARGET|SAMPLE_CC|VOICE_CC|MAIN_STEP_CC|STEP_CC|ARM_AUTOMATION_STEP" Core main.c Makefile
```

Comments-only pass:

```sh
git diff --check
make
```

Results:

- Firmware build passed.
- Image packaging passed after the functional removal.
- Comment pass build passed.
- `git diff --check` passed.
- Final parser/protocol greps only found explanatory comments/documentation, not live parser code paths.

## End of Session Block

```
DATE: 2026-07-05
SESSION GOAL: Remove Core/MIDI/frontPanelParser.c for SCOPING_TARGETS 1.2 without replacing it with another bridge.
COMPLETED: Removed frontPanelParser.c/h; created Core/Scene/Pattern/PatternData.c/h; moved Euklid/SOM files under Core/Scene/Pattern; rewired parser opcodes to direct owner APIs; added detailed comments for the new boundaries.
VERIFIED ON HARDWARE: No. Build-only verification.

CHANGES THIS SESSION:
- REMOVE_FPP_AUDIT_2.md: corrected removal plan and owner map.
- Core/MIDI/frontPanelParser.c/h: deleted.
- Core/Scene/Pattern/PatternData.c/h: new Pattern storage/edit API.
- Core/Scene/Pattern/EuklidGenerator.c/h, SomGenerator.c/h, SomData.c/h: moved from Sequencer.
- Core/Hardware/frontPanel/ledHandler.c/h: Sequencer LED dirty-state drain and direct LED helpers.
- Core/Hardware/frontPanel/buttonHandler.c: direct Pattern/Menu/Sequencer/Preset/MIDI/LED calls.
- Core/Menu/menu.c and copyClearTools.c: direct owner dispatch for globals, pattern/step edits, copy/clear.
- Core/Preset/presetManager.c/h: direct sound/mod-target apply helpers.
- Core/MIDI/MidiParser.c/h, MidiMessages.h, Uart.h, MidiVoiceControl.c: removed front-panel protocol dependency; MIDI config direct setters.
- Core/Sequencer/sequencer.c/h: PatternData integration and narrow rotation runtime hook.
- Core/Hardware/SD/filesystem.c: PatternData pointer access for pattern serialization/staging.
- main.c, Makefile, README.md, MEMORY.md, build/LXRV2_lxr02.img: build/layout/main-loop updates.

KNOWN ISSUES INTRODUCED: None known; hardware test still needed.
KNOWN ISSUES RESOLVED: frontPanelParser is removed from live code; parser opcodes replaced by direct owner APIs.

NEXT SESSION RECOMMENDED GOAL: Continue Pattern refactor by reducing direct compatibility usage of seq_patternSet/seq_tmpPattern in sequencer.c and filesystem.c.
BLOCKERS: Hardware test and future decisions for shuffle ownership/per-track storage.

CRITICAL REMINDERS FOR NEXT SESSION:
- Do not reintroduce frontPanelParser or any generic replacement bridge.
- Use pat_* for new pattern/track/step/automation work.
- led_processSeqLedState() belongs in the foreground main loop.
- seq_offsetTrackStepIndexForRotation() is only a narrow PatternData-to-Sequencer runtime hook.
```
