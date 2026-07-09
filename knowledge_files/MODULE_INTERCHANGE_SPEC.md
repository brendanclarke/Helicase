# Module Interchange Spec

Session 030 baseline, updated in Session 031 for the one-pattern bridge, STEP
track-settings front page, morph VOICE mode, per-track shuffle, and LED blink
idempotence. This spec records the live module API boundaries after
`frontPanelParser.c/h` removal, the PatternData storage-ownership pass, the
`Core/Preset` -> `Core/Scene/Preset` folder move, the first Phase 2
directory-kit filesystem boundary, and the current bridge pattern behavior. The
goal is to make the direct-call ownership clear so future work does not recreate
a generic bridge.

## Rules

- UI code calls the owner module directly.
- Pattern/track/step/automation edits go through `pat_*`.
- LED rendering goes through `led_*`.
- Transport/playback timing goes through `seq_*`.
- Sound parameter application goes through Preset.
- MIDI runtime configuration goes through MidiParser.
- Filesystem may serialize Pattern data through PatternData pointer accessors.
- Filesystem owns async SD/FAT access; storage text schemas and parameter maps
  live in `storageTypes.c/h`.
- Sequencer may read pattern data through narrow PatternData playback helpers;
  it must not index PatternData storage arrays directly.
- `pat_tmpPattern` is the only active-pattern load staging buffer and is
  retained until the 17th Scene/background-bank-load design replaces it.
- Preset code lives under `Core/Scene/Preset/`, but public API names remain
  `preset_*`, `parameterArray_*`, and `paramArray_*` for this mechanical move.
- Normal root Kit loads are directory-based; morph kit loads remain legacy
  `.SND` until instrument morph save/load is designed.
- Pattern/container storage is still a Phase 2 bridge shape. It does not
  preserve the old single/global shuffle byte; per-track shuffle is the only
  live shuffle storage, and final migration/backfill is expected to happen in
  external Python converters once storage settles.

## Core/Scene/Pattern/PatternData

Affiliate modules: Menu, buttonHandler, ledHandler, copyClearTools, filesystem,
Sequencer, EuklidGenerator, Preset/MidiParser indirectly through Sequencer
automation.

Purpose: owns pattern, track, step, length/rotation, pattern settings, and step
automation storage. Provides edit APIs and menu-refresh helpers.

| API | Use | Usual callers / clients |
|---|---|---|
| `pat_init(void)` | Initialize all PatternData storage. | `seq_init()` |
| `pat_trackValid(track)` / `pat_patternValid(pattern)` / `pat_stepValid(step)` | Bounds checks before direct access. | PatternData internals, ledHandler, filesystem, buttonHandler |
| `pat_stepPtr(pattern, track, step)` | Bounded pointer to `Step`; supports `PATTERNDATA_STAGING_PATTERN`. | filesystem, PatternData owner-level code |
| `pat_mainStepsPtr(pattern, track)` | Bounded pointer to main-step mask. | filesystem, PatternData, Euklid |
| `pat_patternSettingPtr(pattern)` | Bounded pointer to per-pattern settings. | filesystem, Sequencer pattern-change logic |
| `pat_lengthRotatePtr(pattern, track)` | Bounded pointer to the per-track settings record (`LengthRotate`: length, rotation, scale, MIDI channel/note, shuffle). | filesystem, Euklid, PatternData |
| `pat_isStepActive(track, step, pattern)` | Read sub-step active bit. | ledHandler, Sequencer playback wrappers |
| `pat_isMainStepActive(track, mainStep, pattern)` | Read main-step active bit. | ledHandler, Sequencer playback wrappers, buttonHandler |
| `pat_readStep(pattern, track, step, out)` | Copy one `Step` snapshot for playback-side inspection. | Sequencer automation parsing and MIDI echo velocity |
| `pat_getStepProbability(pattern, track, step)` | Read one step probability with safe invalid default. | Sequencer playback |
| `pat_getStepNote(pattern, track, step)` | Read one step note with `PAT_DEFAULT_NOTE` fallback. | Sequencer playback, rolls, MIDI note echo |
| `pat_getStepVolume(pattern, track, step)` | Read lower 7-bit velocity. | Sequencer playback and MIDI note echo |
| `pat_setMainStep(pattern, track, mainStep, onOff)` | Set one main-step bit. | Sequencer recording/erase, PatternData operations |
| `pat_setMainStepsRaw(pattern, track, bits)` | Replace whole 16-bit main-step mask. | EuklidGenerator, file load paths |
| `pat_toggleStep(track, step, pattern)` | Toggle one sub-step active bit. | buttonHandler shift-select |
| `pat_toggleMainStep(track, mainStep, pattern)` | Toggle one main-step bit. | buttonHandler step edit |
| `pat_setStepNote(pattern, track, step, note)` | Mutate selected step note and menu mirror. | `menu_parseGlobalParam(PAR_STEP_NOTE)` |
| `pat_setStepVolume(pattern, track, step, volume)` | Mutate selected step velocity without clearing active bit. | `menu_parseGlobalParam(PAR_STEP_VOLUME)` |
| `pat_setStepProbability(pattern, track, step, prob)` | Mutate selected step probability. | `menu_parseGlobalParam(PAR_STEP_PROB)` |
| `pat_setStepAutomationDestination(pattern, track, step, slot, targetParam)` | Mutate step automation destination for lane 0/1. | `menu_parseGlobalParam(PAR_P1_DEST/PAR_P2_DEST)` |
| `pat_setStepAutomationValue(pattern, track, step, slot, value)` | Mutate step automation value for lane 0/1. | `menu_parseGlobalParam(PAR_P1_VAL/PAR_P2_VAL)` |
| `pat_setPatternChangeBar(pattern, value)` | Set pattern change-bar rule. | `menu_parseGlobalParam(PAR_PATTERN_BEAT)` |
| `pat_setPatternNext(pattern, value)` | Set pattern next-pattern rule. | `menu_parseGlobalParam(PAR_PATTERN_NEXT)` |
| `pat_getPatternChangeBar(pattern)` / `pat_getPatternNext(pattern)` | Read pattern settings. | Future callers; currently mostly direct pointer reads |
| `pat_setTrackLength(pattern, track, length)` | Set track length with 0==16 storage encoding. | `menu_parseGlobalParam(PAR_TRACK_LENGTH)` |
| `pat_getTrackLength(pattern, track)` | Read length in UI form. | `pat_applyTrackSettingsToMenu()` |
| `pat_getEffectiveTrackLength(pattern, track)` | Read nonzero playback length, converting stored 0 to 16. | Sequencer step walk, external clock, start-position reset |
| `pat_setTrackRotation(pattern, track, rotation)` | Set track rotation and invoke Sequencer runtime compensation if needed. | buttonHandler perf rotation, Sequencer stop reset |
| `pat_getTrackRotation(pattern, track)` | Read track rotation. | `pat_applyTrackSettingsToMenu()` |
| `pat_setTrackScale(pattern, track, scale)` / `pat_getTrackScale(pattern, track)` / `pat_getTrackScaleRatio(pattern, track)` | Store/read per-track timing scale and convert it to exact numerator/denominator timing for Sequencer. | `menu_parseGlobalParam(PAR_TRACK_SCALE)`, Sequencer scheduler |
| `pat_setTrackMidiChannel(pattern, track, channel)` / `pat_getTrackMidiChannel(pattern, track)` | Store/read per-track MIDI output channel in menu form (`1..16`). | `menu_parseGlobalParam(PAR_TRACK_MIDI_CHAN)`, Sequencer MIDI output/input matching |
| `pat_setTrackMidiNote(pattern, track, note)` / `pat_getTrackMidiNote(pattern, track)` | Store/read per-track MIDI note override (`0` means fallback/default). | `menu_parseGlobalParam(PAR_TRACK_MIDI_NOTE)`, Sequencer trigger/preview/MIDI output |
| `pat_setTrackShuffle(pattern, track, shuffle)` / `pat_getTrackShuffle(pattern, track)` | Store/read per-track shuffle amount (`0..127`); no legacy all-track shuffle import/export remains. | `menu_parseGlobalParam(PAR_SHUFFLE)`, filesystem per-track shuffle extension, Sequencer scheduler |
| `pat_clearTrack(pattern, track)` | Reset one track. | copyClearTools, `pat_clearPattern()` |
| `pat_clearPattern(pattern)` | Reset all tracks in one pattern. | copyClearTools, `pat_init()` |
| `pat_clearAutomation(pattern, track, automTrack)` | Clear automation lane 0/1 for a track. | copyClearTools |
| `pat_copyTrack(pattern, srcTrack, dstTrack)` | Copy one track inside a pattern. | copyClearTools |
| `pat_copyPattern(srcPattern, dstPattern)` | Copy whole pattern slot. | copyClearTools |
| `pat_setSelectedStep(step)` | Keep active step mirror in `PAR_ACTIVE_STEP`; `seq_selectedStep` no longer exists. | PatternData menu-apply and automation destination edit |
| `pat_setActiveAutomationTrack(track)` / `pat_getActiveAutomationTrack(void)` | Select automation lane. | Menu; future UI clients |
| `pat_armAutomationStep(step, track, armed)` | Arm/disarm held-step automation recording target. | buttonHandler long-press path |
| `pat_recordNote(pattern, track, step, velocity, note)` | Record quantized note/velocity/probability/active state and parent main step. | `seq_addNote()` |
| `pat_eraseMainStepSubSteps(pattern, track, mainStep)` | Erase one main-step cluster and restore first-sub-step invariant. | Sequencer live erase |
| `pat_recordAutomation(pattern, track, step, dest, value)` | Write automation into a concrete quantized step. | Sequencer |
| `pat_recordArmedAutomation(pattern, dest, value)` | Write automation into held/armed step if present. | Sequencer |
| `pat_applyStepToMenu(pattern, track, step)` | Copy step editable fields into `parameter_values`. | buttonHandler, Menu active-step edits |
| `pat_applyPatternSettingsToMenu(pattern)` | Copy pattern settings into menu params. | Menu load/apply paths, buttonHandler pattern view |
| `pat_applyTrackSettingsToMenu(pattern, track)` | Copy track settings into menu params. | buttonHandler, led follow paths, Menu voice page |

## Core/Scene/Pattern/EuklidGenerator

Affiliate modules: Menu, buttonHandler, ledHandler, PatternData.

Purpose: owns Euclidean generator state and writes generated main-step masks to
PatternData.

| API | Use | Usual callers / clients |
|---|---|---|
| `euklid_init()` | Initialize generator defaults. | `main.c` boot |
| `euklid_getLength(trackNr)` / `euklid_getSteps(trackNr)` / `euklid_getRotation(trackNr)` | Read generator UI values. | buttonHandler PATGEN page entry |
| `euklid_setLength(trackNr, value)` | Set generator length; caller re-applies steps. | Menu |
| `euklid_setSteps(trackNr, value, patternNr)` | Set pulse count and regenerate PatternData main steps. | Menu |
| `euklid_setRotation(trackNr, value, patternNr)` | Set generator rotation and regenerate PatternData main steps. | Menu |
| `euklid_rotatePattern(length, amount)` | Rotate generated mask. | Euklid internals |
| `euklid_transferPattern(trackNr, patternNr)` | Transfer generated mask/length into PatternData. | Euklid internals |

## Core/Scene/Pattern/SomGenerator

Affiliate modules: Menu, Sequencer, random, SomData.

Purpose: owns SOM generator state. Menu configures it directly; Sequencer ticks
it during playback when SOM mode is active.

| API | Use | Usual callers / clients |
|---|---|---|
| `som_init()` | Initialize SOM coordinates/frequencies/flux. | `main.c` boot |
| `som_tick(stepNr, mutedTracks)` | Generate SOM voice triggers on main-step boundaries. | Sequencer |
| `som_setX(x)` / `som_setY(y)` | Set normalized interpolation coordinates from menu values. | Menu |
| `som_setFlux(flux)` | Set randomization amount. | Menu |
| `som_setFreq(freq, voice)` | Set per-voice trigger threshold/frequency. | Menu |

## Core/Hardware/frontPanel/ledHandler

Affiliate modules: Sequencer, Menu, buttonHandler, PatternData, dout.

Purpose: owns physical LED interpretation and rendering. Also owns deferred
Sequencer LED feedback via `SeqLedState`.

| API | Use | Usual callers / clients |
|---|---|---|
| `led_init()` | Initialize LED shadow/output state. | boot |
| `led_setValue(val, ledNr)` / `led_setValueTemp(val, ledNr)` | Set physical/logical LED value. | buttonHandler, Menu, ledHandler helpers |
| `led_reset(ledNr)` / `led_toggle(ledNr)` / `led_toggleTemp(ledNr)` | Direct LED edits. | buttonHandler, Menu |
| `led_clearAll()` | Clear all LED state. | UI flows |
| `led_tickHandler()` | Blink/pulse maintenance. | foreground/timer service |
| `led_pulseLed(ledNr)` | Pulse one LED. | voice trigger indication paths |
| `led_setBlinkLed(ledNr, onOff)` / `led_clearAllBlinkLeds()` | Blink management; starting blink for an already-blinking LED is idempotent so duplicate slots cannot cancel visually. | buttonHandler, Menu |
| `led_setActivePage(pageNr)` | Light one page/select LED. | buttonHandler load/save |
| `led_setActiveVoice(voiceNr)` / `led_setActiveVoiceLeds(pattern)` | Voice LED display. | buttonHandler, Menu |
| `led_setActiveSelectButton(butNr)` | SELECT LED display. | buttonHandler, Menu |
| `led_setMode2Leds(value)` / `led_setMode2(status)` | Mode LED display. | buttonHandler |
| `led_clearSequencerLeds()` / `led_clearSequencerLeds1_8()` / `led_clearSequencerLeds9_16()` | Clear step LEDs. | buttonHandler, Menu, copyClearTools |
| `led_clearSelectLeds()` / `led_clearVoiceLeds()` | Clear select/voice LED groups. | buttonHandler, Menu |
| `led_setActive_step(stepNr)` / `led_clearActive_step()` | Chase/current-step display helpers. | ledHandler internals/future callers |
| `led_initPerformanceLeds()` | Performance LED layout. | buttonHandler, pattern change notification |
| `led_updateCurrentStep(step)` | Repaint chase LED from Sequencer payload. | `led_processSeqLedState()` |
| `led_updateRecordedMainStep(activeTrack, shownPattern, subStep)` | Repaint recorded main step if visible. | `led_processSeqLedState()` |
| `led_updateRecordedSubStep(activeTrack, shownPattern, step, selectedStepBase, shiftHeld, selectMode)` | Repaint recorded sub-step if visible. | `led_processSeqLedState()` |
| `led_updatePatternTrack(track, pattern, selectedStepBase)` | Repaint all visible step/select LEDs from PatternData. | buttonHandler, Menu, pattern follow |
| `led_setBeatPulse(on)` | Apply beat pulse to START/STOP LED. | `led_processSeqLedState()` |
| `led_notifyPatternChanged(playedPattern)` | Sequencer pattern-change notification and follow-mode UI refresh. | Sequencer |
| `led_notifyTrackRotationReset(rotation)` | Update visible rotation parameter after Sequencer stop reset. | Sequencer |
| `led_processSeqLedState()` | Foreground drain of Sequencer LED dirty state. | `main.c` |

Shared object: `SeqLedState seq_ledState` is written by Sequencer and consumed
by ledHandler in foreground.

## Core/Hardware/frontPanel/buttonHandler

Affiliate modules: ledHandler, Menu, PatternData, EuklidGenerator, Sequencer,
Preset, MidiParser, copyClearTools.

Purpose: owns physical button event queue, select modes, shift state, selected
step UI state, held-step automation gesture, mute UI shadow, and direct button
dispatch to owners.

| API | Use | Usual callers / clients |
|---|---|---|
| `buttonHandler_buttonPressed(buttonNr)` / `buttonHandler_buttonReleased(buttonNr)` | ISR-safe event recording. | TIM6/front-panel service |
| `buttonHandler_processEvents()` | Drain one button event and execute foreground UI actions. | `main.c` |
| `buttonHandler_tick()` | Long-press timer promotion. | foreground timing path |
| `buttonHandler_getMode()` | Current SELECT mode. | ledHandler, Menu/UI checks |
| `buttonHandler_getShift()` | Current shift-held state. | ledHandler, button logic |
| `buttonHandler_getArmedAutomationStep()` | Read currently armed automation step. | diagnostics/future clients |
| `buttonHandler_setRunStopState(running)` | Sync UI transport bit and START/STOP LED. | MidiParser, local button path |
| `buttonHandler_showMuteLEDs()` | Show mute-state LEDs. | Menu/voice/performance paths |
| `buttonHandler_muteVoice(voice, isMuted)` | Update front-panel mute shadow. | buttonHandler local, MIDI/UI paths if needed |

Public state used by other modules:

- `buttonHandler_selectedStep`: selected sub-step base for LED/menu refresh.
- `buttonHandler_originalParameter`, `buttonHandler_originalValue`,
  `buttonHandler_resetLock`: reset-lock state used around automation edits.

## Core/Menu/menu

Affiliate modules: buttonHandler, ledHandler, PatternData, Preset, Sequencer,
MidiParser, EuklidGenerator, SomGenerator, filesystem, triggerJacks, mod nodes.

Purpose: owns menu pages, visible/edit state, `parameter_values`, load/save UI,
encoder and endless-pot edit dispatch, and post-load operation follow-up.

| API | Use | Usual callers / clients |
|---|---|---|
| `menu_init()` / `menu_start()` | Initialize menu state and enter first page. | `main.c` boot |
| `menu_repaintAll()` / `menu_repaint()` / `sendDisplayBuffer()` | LCD frame rendering. | UI modules |
| `menu_parseEncoder(inc, button)` | Main encoder edit/navigation. | main loop |
| `menu_parseKnobDelta(knobNr, delta)` | RV1-RV4 edit dispatch. | main loop |
| `menu_serviceKnobRepaint()` | Coalesced knob repaint. | main loop |
| `menu_notifyExternalParamChanged(paramNr)` | Mark externally changed param for repaint. | MidiParser |
| `menu_pollPresetStatus()` | Async preset/filesystem completion and sound/global apply. | main loop |
| `menu_parseGlobalParam(paramNr, value)` | Dispatch global/menu parameter side effects to owner modules. | Menu edits, globals load |
| `menu_sendAllParameters()` / `menu_sendAllGlobals()` | Apply all parameters/globals to owners. | load/apply paths |
| `menu_serviceRuntimeWidgets()` | Runtime UI widgets such as CPU. | main loop |
| `menu_switchPage(pageNr)` / `menu_switchSubPage(subPageNr)` | Page navigation and direct Pattern/LED refresh. | buttonHandler/Menu |
| `menu_resetActiveParameter()` | Keep active parameter valid for page. | buttonHandler/Menu |
| `menu_setVoiceModeShowMorph(onOff)` | Toggle VOICE-page morph endpoint overlay; repaint/edit helpers resolve sound parameters through `parameters2[]` when set. | buttonHandler `SHIFT+VOICE` mode |
| `menu_paramUsesMorphView(paramNr)` / `menu_getParameterDisplayValue(paramNr)` / `menu_getParameterEditPtr(paramNr)` | Resolve display/edit buffer for normal vs. morph VOICE pages. | Menu repaint, encoder edits, endless-pot edits |
| `menu_showStepTrackSettingsFirstHalf()` / `menu_toggleStepTrackSettingsHalf()` | Select STEP front-page first half or toggle to the second half where per-track shuffle lives. | buttonHandler STEP-mode voice/track buttons |
| `menu_resetSaveParameters()` | Reset load/save UI state. | load/save page |
| `menu_setNumSamples(num)` | Sample count display state. | sample/filesystem paths |
| `menu_getActivePage()` / `menu_getSubPage()` | Read visible page/subpage. | buttonHandler, ledHandler |
| `menu_getActiveVoice()` / `menu_setActiveVoice(voiceNr)` | UI active voice. | buttonHandler, MidiParser, PatternData callers |
| `menu_areMuteLedsShown()` | Mute LED UI state query. | ledHandler/buttonHandler |
| `menu_setShownPattern(patternNr)` / `menu_getViewedPattern()` | UI viewed/edited pattern. | buttonHandler, ledHandler, PatternData callers |

Shared state used by clients:

- `parameter_values[]`, `parameters2[]`
- `menu_activePage`, `menu_activeVoice`, `menu_playedPattern`,
  `menu_shownPattern`, `menu_muteModeActive`
- `modTargets[]`, `paramToModTarget[]`

## Core/Menu/copyClearTools

Affiliate modules: Menu, ledHandler, PatternData, buttonHandler.

Purpose: owns copy/clear UI mode state and delegates actual pattern mutation to
PatternData.

| API | Use | Usual callers / clients |
|---|---|---|
| `copyClear_executeClear()` | Execute current clear target. | buttonHandler |
| `copyClear_clearCurrentTrack()` | Clear active voice in viewed pattern. | `copyClear_executeClear()` |
| `copyClear_clearCurrentPattern()` | Clear viewed pattern. | `copyClear_executeClear()` |
| `copyClear_copyTrack()` | Copy selected source track to destination track. | buttonHandler |
| `copyClear_copyPattern()` | Copy selected source pattern to destination pattern. | buttonHandler |
| `copyClear_getClearTarget()` / `copyClear_setClearTarget(mode)` | Clear target UI selection. | Menu encoder clear mode |
| `copyClear_isClearModeActive()` | Query clear mode. | Menu |
| `copyClear_armClearMenu(isShown)` | Show/hide clear confirmation UI. | buttonHandler/Menu |
| `copyClear_srcSet()` / `copyClear_setSrc(src, type)` / `copyClear_setDst(dst, type)` | Copy gesture source/destination state. | buttonHandler |
| `copyClear_reset()` | Leave copy/clear UI mode. | buttonHandler |

## Core/Sequencer/sequencer

Affiliate modules: PatternData, ledHandler, Menu, MidiParser, MidiVoiceControl,
triggerJacks, clockSync, SOM, USB/UART MIDI.

Purpose: owns playback timing, transport, mute, roll, quantization, external
sync state, MIDI note output, current playback pattern, runtime step indices,
and recording gates. Pattern storage reads/writes go through PatternData APIs;
Sequencer no longer exposes `seq_patternSet`, `seq_tmpPattern`, or
`seq_selectedStep` compatibility names.

| API | Use | Usual callers / clients |
|---|---|---|
| `seq_init()` | Initialize automation nodes, runtime indices, PatternData. | `main.c` boot |
| `seq_tick()` | Advance playback when due. | TIM3 owner |
| `seq_triggerVoice(voiceNr, vol, note)` | Trigger one voice from Sequencer/SOM. | Sequencer, SOM |
| `seq_previewVoice(voiceNr)` | Trigger the selected voice while transport is stopped without reading or advancing step state. | buttonHandler selected-voice re-press |
| Internal scaled scheduler helpers | Compute due events from absolute 96-PPQ tick time, per-track scale, and per-track shuffle. | Sequencer only; reads PatternData track settings |
| `seq_resetDeltaAndTick()` / `seq_resetToPatternStart()` / `seq_setDeltaT(delta)` | Clock/reset timing control. | trigger/MIDI sync paths |
| `seq_realignActivePatternToMasterClock()` | Recalculate per-track runtime positions from the master step clock, length, and scale. | buttonHandler/PERF pattern realign gesture |
| `seq_triggerNextMasterStep(stepSize)` | External clock master-step scheduling. | trigger/MIDI sync paths |
| `seq_setBpm(bpm)` / `seq_getBpm()` | Tempo. | Menu/global apply |
| `seq_sync()` | External MIDI clock tick. | MidiParser |
| `seq_getExtSync()` / `seq_setExtSync(isExt)` / `seq_setExtSyncSource(source)` / `seq_getExtSyncSource()` / `seq_noteExtSyncActivity(source, timestampUs)` | External sync mode/activity. | Menu, MidiParser, triggerJacks |
| `seq_setQuantisation(value)` | Recording quantization. | Menu |
| `seq_setNextPattern(patNr)` | Queue next playback pattern. | buttonHandler, MidiParser |
| `seq_armActivePatternReload()` | Mark active pattern for reload/commit. | filesystem/preset load paths |
| `seq_setRunning(isRunning)` / `seq_isRunning()` | Transport. | buttonHandler, MidiParser |
| `seq_setMute(trackNr, isMuted)` / `seq_isTrackMuted(trackNr)` | Playback mute state. | buttonHandler, MidiParser |
| `seq_setRoll(voice, onOff)` / `seq_setRollRate(rate)` | Roll performance behavior. | buttonHandler, Menu |
| `seq_addNote(trackNr, vel, note)` | Record played note into pattern when recording. | MidiParser, roll path |
| `seq_setRecordingMode(active)` / `seq_setErasingMode(active)` | Recording/erase gates. | buttonHandler |
| `seq_recordAutomation(voice, dest, value)` | Sequencer-gated and held-step automation recording. | Preset, MidiParser |
| `seq_midiNoteOff(chan)` / `seq_sendMidiNoteOn(channel, note, veloc)` | MIDI note output ownership. | MidiParser, Sequencer |
| `seq_offsetTrackStepIndexForRotation(trackNr, oldRot, newRot, len)` | Narrow runtime hook for live rotation compensation. | PatternData only |

## Core/Scene/Preset/presetManager

Affiliate modules: filesystem, Menu, MidiParser, Sequencer, DSP voice/mod nodes.

Purpose: owns preset load/save status and sound-parameter application. The
folder now lives under `Core/Scene/Preset/`; public function prefixes remain
`preset_*` for the mechanical move.

| API | Use | Usual callers / clients |
|---|---|---|
| `preset_init()` | Initialize preset operation status. | boot |
| `preset_getStatus()` / `preset_getCompletedOp()` / `preset_getRequestSlot()` / `preset_getRequestType()` / `preset_ackStatus()` | Async operation status protocol. | Menu |
| `preset_loadDrumset(presetNr, isMorph)` / `preset_saveDrumset(presetNr, isMorph)` | Async kit/morph load/save. | Menu |
| `preset_loadGlobals()` / `preset_saveGlobals()` | Async globals load/save. | Menu |
| `preset_loadPattern(presetNr)` / `preset_savePattern(presetNr)` | Async pattern load/save. | Menu |
| `preset_saveAll(presetNr, isAll)` / `preset_loadAll(presetNr, isAll)` | Async all/performance load/save. | Menu |
| `preset_loadName(presetNr, what)` / `preset_applyLoadedName()` | Async slot name browsing. | Menu |
| `preset_sendDrumsetParameters()` | Synchronous pre-audio sound/mod-target apply. | Menu boot/load path |
| `preset_applySoundParameter(paramNr, value, recordAutomation)` | Direct sound parameter application and optional automation recording. | Menu, morph, reset-lock |
| `preset_applyVelocityModTarget(voice, targetParam)` | Direct velocity mod destination update. | Menu, preset load apply |
| `preset_applyLfoModTarget(lfo, targetParam)` | Direct LFO mod destination update. | Menu, preset load apply |
| `preset_startDrumsetApply()` / `preset_tickDrumsetApply()` | Chunked runtime sound/mod-target apply. | Menu |
| `preset_morph(morph)` / `preset_morphTick()` / `preset_getMorphValue(index, morph)` | Rate-limited morph interpolation/application. | Menu, main loop |

## Core/Scene/Preset/ParameterArray

Affiliate modules: Menu, Preset manager, MidiParser, modulationNode, DSP voice
modules, mixer, filesystem, PatternData.

Purpose: owns the canonical numeric parameter id map and the runtime pointer map
from sound parameter ids to DSP engine fields. It does not yet own
`parameter_values[]` or `parameters2[]`; those arrays remain in Menu until the
later instrument/file redesign makes Preset/Scene the canonical endpoint
parameter owner.

| API / Data | Use | Usual callers / clients |
|---|---|---|
| `enum ParamEnums` / `PAR_*` / `END_OF_SOUND_PARAMETERS` / `NUM_PARAMS` | Canonical parameter id space for sound, menu, pattern, and globals. | Menu, filesystem, Preset, MidiParser, PatternData |
| `TYPE_UINT8`, `TYPE_FLT`, `TYPE_SPECIAL_F`, `TYPE_UINT32`, `TYPE_SPECIAL_P`, `TYPE_SPECIAL_FILTER_F` | Type tags for runtime parameter pointer entries. | ParameterArray, modulationNode |
| `ptrValue` | Float/integer value carrier for typed writes. | modulationNode, ParameterArray |
| `Parameter` / `parameterArray[]` | Map sound parameter id to target DSP field and value type. | ParameterArray, modulationNode |
| `paramArray_setParameter(idx, newValue)` | Write one typed value into the mapped DSP field when the id/pointer are valid. | modulationNode restore/apply paths |
| `parameterArray_init()` | Fill the sound-parameter pointer/type map. | `main.c` boot |
| `extern parameter_values[]` | Current permanent parameter byte store declaration. | Defined in Menu today; future Preset/Scene migration |

## Core/MIDI/MidiParser

Affiliate modules: Uart, USB, Sequencer, Menu, Preset, MidiVoiceControl,
buttonHandler, triggerJacks, MidiRealtime.

Purpose: owns incoming MIDI parsing, MIDI runtime config, global-channel
automation recording, MTC behavior, and CC application to the sound engine.

| API | Use | Usual callers / clients |
|---|---|---|
| `midiParser_parseUartData(data)` | Parse DIN byte stream. | Uart |
| `midiParser_parseMidiMessage(msg)` | Parse complete USB/DIN MIDI message. | main MIDI service, Uart |
| `midiParser_processRealtimeEvents()` | Drain timestamped realtime ring. | TIM3 |
| `midiParser_ccHandler(msg, updateOriginalValue)` | Apply sound/internal CC messages. | Preset, MidiParser |
| `midiParser_triggerVoice(voice, note, vel, do_rec)` | Trigger one voice through MIDI note path. | external clients |
| `midiParser_playVoiceMidiNote(voice, vel)` | BAR1/BAR2 voice note path. | buttonHandler |
| `midiParser_getVoiceMidiNote(voice)` | Resolve default/override voice note. | MidiParser, clients |
| `midiParser_calcDetune(value)` | Convert MIDI detune value. | sound/MIDI paths |
| `midiParser_checkMtc()` | Stop transport if MTC times out. | Sequencer |
| `midiParser_setRouting(value)` | Apply MIDI routing menu value. | Menu |
| `midiParser_setChannel(voice, channel)` | Apply per-voice/global MIDI channel. | Menu |
| `midiParser_setFilter(is_tx, value)` | Apply TX/RX MIDI filter nibble. | Menu |

Shared config:

- `midi_MidiChannels[8]`: voice channels plus global channel.
- `midi_NoteOverride[7]`: per-voice note override.
- `midiParser_txRxFilter`: packed TX/RX filter.
- `midiParser_originalCcValues[]`: return targets for automation/mod nodes.

## Core/MIDI/Uart

Affiliate modules: MidiParser, USB/MIDI service, Sequencer realtime output.

Purpose: USART3 MIDI DIN only. Front-panel UART stubs are gone.

| API | Use | Usual callers / clients |
|---|---|---|
| `initMidiUart()` | Initialize USART3 MIDI. | `main.c` boot |
| `uart_sendMidiByte(data)` | Queue one DIN byte. | Sequencer/MidiParser |
| `uart_sendMidi(msg)` | Queue complete MIDI message. | Sequencer/MidiParser |
| `uart_processMidi()` | Drain received DIN bytes into MidiParser. | main MIDI service |
| `uart_getMidiTxDropCount()` / `uart_getMidiRealtimeTxDropCount()` | Diagnostics. | diagnostics/future UI |

## Core/MIDI/MidiMessages

Affiliate modules: MidiParser, Uart, USB, Sequencer, Preset.

Purpose: defines MIDI message struct, MIDI status bytes, real MIDI CC values,
and sound-engine CC enumerations. It no longer defines front-panel protocol
opcodes.

Key API/data:

- `MidiMsg`: `{ status, data1, data2, bits }`
- `enum MidiSource`: `midiSourceMIDI`, `midiSourceUSB`
- `NOTE_ON`, `NOTE_OFF`, `MIDI_CC`, `MIDI_CC2`, `PROG_CHANGE`,
  realtime/system status constants
- Sound CC enums used by `midiParser_ccHandler()`
- `NO_AUTOMATION`

## Core/MIDI/MidiVoiceControl

Affiliate modules: Sequencer, MidiParser, audio render boundary.

Purpose: owns voice note-on/off dispatch and pending trigger drain at the audio
render boundary. Session 028 removed obsolete front-panel dependency.

| API | Use | Usual callers / clients |
|---|---|---|
| `voiceControl_noteOn(voice, note, vel)` | Trigger/queue voice-on behavior. | Sequencer, MidiParser |
| `voiceControl_noteOff(voice)` | Stop one voice or all voices with `0xff`. | Sequencer, MidiParser |
| `voiceControl_processPending()` | Drain pending triggers at audio render boundary. | `audio_check_and_render()` |
| `voiceControl_isVoicePlaying(voice)` | Query voice state. | clients/future UI |

## Core/Hardware/SD/filesystem

Affiliate modules: Preset, Menu, kitBrowser, PatternData, SampleMemory,
storageTypes.

Purpose: public typed async filesystem facade. It serializes pattern data
through PatternData accessors after Session 028. After Session 030, normal kit
load also scans and opens root `Kit/NNN Name/` directories, while leaving
storage text parsing to `storageTypes.c/h`.

| API | Use | Usual callers / clients |
|---|---|---|
| `filesystem_initCardAndMountBlocking()` / `filesystem_initAfterCardReady()` | Boot card init/mount. | `main.c` |
| `filesystem_tick()` | Pump asyncfatfs work. | main loop |
| `filesystem_status()` / `filesystem_ack()` | Operation status protocol. | Preset/Menu |
| `filesystem_requestLoad(type, slot, cb)` / `filesystem_requestSave(type, slot, cb)` | Async typed load/save. For `FS_FILE_KIT`, load is now `Kit/NNN Name/kitset.kcg` plus instruments; for `FS_FILE_MORPH`, load remains legacy `.SND`. Saves remain legacy for now. | Preset |
| `filesystem_requestLoadName(type, slot, cb)` | Async name load. For `FS_FILE_KIT`, returns the cached directory scan name instead of opening a `.SND` header. | Preset/Menu |
| `filesystem_requestScanKits(cb)` | Scan root `Kit/` directories into the new cache and legacy `kitBrowser` map. | main startup, kitBrowser/Menu |
| `filesystem_installSamplesBlocking()` / `filesystem_installLoopsBlocking()` | Blocking sample/loop install under audio suspend. | Menu |
| `filesystem_loadedName()` | Read loaded name buffer. | Preset |
| `filesystem_kitSlotExists(zero_based_slot)` | Query the Phase 2 Kit scan cache for a numbered folder. | Menu/future browsers |
| `filesystem_kitSlotName(zero_based_slot)` | Return cached eight-character Kit display name or `Empty   `. | Menu Load page |
| `filesystem_diagOp()` / `filesystem_diagPhase()` / `filesystem_diagBytesDone()` | Diagnostics. | diagnostics/future UI |
| `filesystem_lastMountResult()` / `filesystem_bootDetectedUnsupportedCard()` | Boot/card status. | main/Menu |
| `filesystem_takeStaleGlobalsWarning()` | One-shot stale globals warning source. | Menu |

Important private Phase 2 kit helpers:

- `filesystem_scanKits_tick()` enters root `Kit/`, reconstructs LFN display
  names when available, records FAT short aliases for opening, and populates
  `kb_map[]`/`kb_numKits` for legacy `kitBrowser` compatibility.
- `filesystem_loadKitDirectory_tick()` opens the selected kit folder, parses
  `kitset.kcg`, loads six listed instrument files, and writes into
  `parameter_values[]` / `parameters2[]`.
- Kit folders prefer `NNN Name` and accept `NNN_Name`; scan has a short-alias
  fallback for FAT aliases like `001SLA~1`.

Private but important pattern serialization helpers:

- `filesystem_patternStepPtr()`
- `filesystem_patternMainPtr()`
- `filesystem_patternSettingPtr()`
- `filesystem_patternLengthPtr()`

These select normal PatternData storage or `PATTERNDATA_STAGING_PATTERN` when
loading the currently active pattern.

## Core/Hardware/SD/storageTypes

Affiliate modules: filesystem, ParameterArray, generated `SD_CARD/Kit` data,
`tools/convert_legacy_kits.py`.

Purpose: pure Phase 2 storage-format layer. It owns text schema parsing,
validation, numbered folder parsing, filename/type checks, display-name
normalization, and instrument-key-to-`ParameterArray` maps. It must not call
`asyncfatfs`; filesystem owns I/O and passes complete text lines/names into this
layer. All functions in this layer use the `storage_` prefix.

| API / Data | Use | Usual callers / clients |
|---|---|---|
| `storage_status_t` | Parser/validator result codes. | filesystem |
| `storage_instrument_type_t` | Format-level type enum for `.drm`, `.snr`, `.cym`, `.hat`. | kitset/instrument parser |
| `storage_kitset_t` | Incremental parse state for `kitset.kcg`. | filesystem directory kit loader |
| `storage_instrument_state_t` | Incremental parse state for one instrument file. | filesystem directory kit loader |
| `storage_kitsetInit()` / `storage_kitsetParseLine()` / `storage_kitsetFinalize()` | Validate `kitset.kcg`, collect instrument filenames/types, and write kit-level parameters such as audio outputs. | `filesystem_loadKitDirectory_tick()` |
| `storage_instrumentStateInit()` / `storage_instrumentParseLine()` / `storage_instrumentFinalize()` | Validate one instrument file and write mapped `[params]`/`[morph]` values. | `filesystem_loadKitDirectory_tick()` |
| `storage_instrumentCopyMainToMorphFallback()` | Copy mapped main values into morph buffer when an instrument has no `[morph]` section. | `filesystem_loadKitDirectory_tick()` |
| `storage_instrumentTypeFromText()` / `storage_instrumentFilenameMatchesType()` | Convert/validate type strings and extensions. | kitset/instrument validation |
| `storage_parseNumberedFolder()` | Parse visible numbered folders `NNN Name` or `NNN_Name` into zero-based slot plus eight-character display name. | Kit scan |
| `storage_copyDisplayName()` / `storage_copyFilename()` | Fixed-width display-name normalization and short filename copying. | filesystem/parser code |

Current ownership decisions:

- `kitset.kcg` owns kit membership, instrument filenames, instrument types,
  `audio_out`, `voice_decimation_all`, and kit metadata.
- Instrument files own per-voice sound parameters, including volume and pan.
- MIDI note/channel values do not belong in kitset or instrument files; they
  belong in future scene settings.
- Instrument morph data is optional during this load pass. Missing morph data is
  treated as "copy main parameters into morph" until save-format work defines
  explicit per-instrument morph persistence.

## main.c

Purpose: boot ordering and foreground service scheduler.

Relevant Session 028 interchange point:

- `led_processSeqLedState()` is called in the foreground loop after audio render
  opportunities and after TIM3-owned sequencer timing can mark LED state dirty.
  Do not move this into TIM3 without auditing Menu/button/LED access.

## Removed Front-Panel Protocol Surface

Deleted modules:

- `Core/MIDI/frontPanelParser.c`
- `Core/MIDI/frontPanelParser.h`

Removed from live API surface:

- `frontPanel_sendData`
- `frontPanel_sendByte`
- `frontPanel_sendMidiMsg`
- `frontPanel_parseData`
- `frontParser_parseUartData`
- `frontParser_updateTrackLeds`
- front-panel UART stubs in `Uart.h`
- protocol opcodes such as `LED_CC`, `SEQ_CC`, `SET_P1`, `SET_P2`, `STEP_CC`,
  `MAIN_STEP_CC`, `ARM_AUTOMATION_STEP`, `SAMPLE_CC`, and `VOICE_CC`

Historical logs may still mention these names. In current code, they should
only appear in comments, documentation, or old session logs.
