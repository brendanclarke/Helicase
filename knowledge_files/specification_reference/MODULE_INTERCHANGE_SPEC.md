# Module Interchange Spec

Session 030 baseline, updated through the Session 050 bounded-CRC,
boot-capture and Scene-Load publication build after the Session 046 rollback,
for the one-pattern bridge,
STEP track-settings front page, per-track shuffle, LED blink idempotence,
descriptor-owned instrument files, Scene-owned instrument parameter images, and
dynamic VOICE menu pages, descriptor-aware LFO/velocity runtime targets,
descriptor Morph, per-voice Morph, Scene modulation targets, asyncfatfs
LFN/case expansion, restored Kit load/save, root Instrument Save, and
Kit/Instrument Morph Save, root Scene Load/Save, and the 16-Scene Bank
Load/Save workspace. This spec records the live module API boundaries
after `frontPanelParser.c/h` removal, the PatternData storage-ownership pass,
the `Core/Preset` -> `Core/Bank/Scene/Preset` folder move, the first Phase 2
directory-kit filesystem boundary, the HCNAMES/identity ownership pass,
independent filesystem payload staging, and the current bridge pattern
behavior, cold-boot tagged-runtime activation, accepted OK/OW command
ownership, and final Scene/Bank Load index restoration. The goal is to make
the direct-call ownership clear so future work
does not recreate a generic bridge or duplicate resident names.

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
- `pat_tmpPattern` is the active-pattern load staging buffer. The current
  16-Scene Bank workspace does not allocate a separate staging Scene.
- Preset code lives under `Core/Bank/Scene/Preset/`, but public API names remain
  `preset_*`, `parameterArray_*`, and `paramArray_*` for this mechanical move.
- Normal root Kit loads and saves are directory-based. Kit Morph Load and
  Instrument Morph Load copy normal source endpoints into morph endpoints for
  same-type slots; mismatched slots are no-change. Root Instrument Save writes
  one resident voice to the root Instrument pool. Kit/Instrument Morph Save
  writes the current per-voice interpolated value into both normal and morph
  endpoint fields.
- Scene and Bank load/save are directory-based. Bank is the Session 040
  16-Scene workspace.
  Root Scene is a library/pool. Root Bank is the 16-resident-Scene workspace
  selector; its present/edit masks and active Scene are BankData state.
- Numbered library slots are direct `000..999`; slot `000` is real. This does
  not change instrument file voice coordinates, which remain one-based `1..6`.
- Instrument, Kit, root Scene, and root Bank browser names use one physical
  shared SRAM cache of 1,000 nine-byte rows. Kit/Scene/Bank rows are direct
  slot positions; Instrument rows are sorted browser positions. Type changes
  replace the cache domain, and entering a type loads its `.hcindex`.
- Root `/.hcnames` is the authoritative fixed-order resident identity and
  provenance register. The 9,000-byte cache borrows its 129 names only during
  a name transaction; a filesystem-owned 258-byte source register retains the
  matching `inherit`/`unknown`/direct source tokens between transactions.
  The sole active identity block is 81 bytes: BankData's Bank row plus
  filesystem's Scene, Kit, and six Instrument rows. SceneData stores no name or
  filename text.
- HCNAMES creation requires a complete case-insensitive root absence proof.
  A NULL read open, duplicate match, or failed scan/close remains an error and
  cannot authorize creation or automatic repair.
- The 9,000-byte cache never aliases payload validation. A separate aligned
  2,048-byte union stages one Kit, one Instrument candidate, or Scene settings
  plus embedded Kit. Pattern is excluded and reads directly into final Scene
  SRAM after the non-Pattern commit.
- Boot writes `/Kit/.hcindex`, `/Scene/.hcindex`, `/Bank/.hcindex`, and each
  registry-owned Instrument index. After Instrument index generation disposes
  the shared cache, boot reloads `/Bank/.hcindex` before initial Bank load.
- Successful Kit, root Scene, and root Bank saves perform a physical parent
  rescan and complete `.hcindex` rewrite before the original save callback is
  released; Menu then refreshes the current Save slot display.
- Pure root Scene and Bank Loads never enter that Save-owned physical rebuild.
  After payload and HCNAMES commit, Preset/Menu applies the loaded active Scene,
  then Menu reloads the unchanged selected `.hcindex` read-only before ending
  `...` and releasing input. Its direct final-index callback snapshots the
  terminal result, calls `filesystem_ack()`, then releases Menu; without that
  acknowledgement the idle-only trace and AutoSave schedulers remain blocked
  at `FS_STATUS_DONE`.
- Load:Bank index completion is only the first browser boundary. Menu continues
  directly into a physical child preview for the highlighted Bank and holds
  input until that callback publishes the `00..15` destination mask.
- Combined Kit/Instrument menu entry reads one Scene's Kit plus six Instrument
  identities once and family exit performs at most one HCNAMES rewrite. Bank
  Load/Save own a full-register transaction; selective Bank Load overlays only
  requested/present children.
- Root Scene completion also accumulates its committed Kit-family identity mask,
  but the current physical Load/Save exit predicate fails to run that existing
  family rewrite for Scene/Bank sessions. This is a known deferred defect: do
  not claim that a changed root Scene row means its embedded Kit/six-Instrument
  rows are registered until the existing exit boundary is widened.
- asyncfatfs owns exact-case filename behavior. Product code should use
  filesystem/asyncfatfs object/LFN APIs instead of local FAT/LFN reconstruction.
  Dot-prefixed files are ordinary filesystem objects and must not be hidden by
  asyncfatfs. Product scans explicitly exclude `.hctmp.<ext>`.
- Filesystem shape, instrument file shape, descriptor tables, Scene storage,
  menu layout, and DSP propagation are specified in
  `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`.
- AutoSave wire geometry, dirty ownership, scheduling, and extension rules are
  specified only in `knowledge_files/specification_reference/AUTOSAVE.md`.
  Development-mode and diagnostic-file policy are specified only in
  `knowledge_files/specification_reference/DEV_MODES.md`.
- Instrument voice parameter values are Scene-owned descriptor images. Dynamic
  VOICE menu cells resolve through the active slot's instrument descriptor
  layout, not through static `menuPages.h` cells or `parameter_values[]`.
- `parameter_values[]` remains the legacy/global/menu byte store and the bridge
  for non-instrument sound parameters. It is not the canonical store for new
  descriptor-backed instrument parameters.
- `ParameterArray` remains the legacy runtime pointer map used by older flat
  parameter paths and by `AutomationNode`. It is no longer the canonical map
  for instrument file keys. `Step` destinations are already canonical 16-bit
  IDs, but AutomationNode still narrows them to legacy byte CC/CC2 routing;
  descriptor step automation is therefore deliberately unfinished.
- Descriptor-backed velocity/LFO target storage, menu display, and runtime
  application are live after Session 035 for direct descriptor targets,
  voice-local decimation, per-voice Morph, and Scene Decimation. Direct
  descriptor LFO overlays use InstrumentManager's descriptor-domain adapter and
  the normal descriptor runtime writer; step automation remains the unfinished
  descriptor/Scene target path.
- Direct descriptor modulation must use the descriptor owner's byte-domain
  runtime writer. Do not write raw DSP members such as `SlopeEg2.decay` for a
  byte descriptor: that bypasses the setter's unit conversion and can turn a
  zero byte value into an indefinitely held envelope. Session 035 fixed this
  for descriptor LFO targets; keep that adapter path as the model for future
  modulation/automation work.
- Modulation-owner scans must enumerate InstrumentManager's current dynamic
  runtime pools. Fixed global source scans are insufficient after freely
  replaceable instrument types; the Instrument Load transaction compensates by
  clearing/rebinding all sources, but normal reset/original-value paths still
  require this migration.
- Descriptor Morph is live after Session 033. The Morph engine walks
  Scene-owned descriptor images and the active slot's descriptor table instead
  of hardcoded parameter lists.
- Scene-level sound modulation targets live in
  `Core/Bank/Scene/SceneModTargets.c/h`. The first target set is `1vm..6vm` plus
  Scene Decimation `srt`; future FX parameters join that namespace instead of
  being inserted into per-instrument descriptor tables.
- Pattern/container storage is still a Phase 2 bridge shape. It does not
  preserve the old single/global shuffle byte; per-track shuffle is the only
  live shuffle storage, and final migration/backfill is expected to happen in
  external Python converters once storage settles.

## Core/Bank/Scene/Pattern/PatternData

### Session 043 current contract

The API roster below records the retired pre-Session-043 bridge and is not a
current implementation contract. `PatternData` now owns only one 112-byte
`PatternSet` bitmap per resident Scene: `step_on[7][16]`, with one bit per
chronological step. `pat_patternSetGetStep`/`pat_patternSetSetStep` are the
staged-or-resident representation boundary; `pat_isStepActive`,
`pat_setStepActive`, `pat_toggleStep`, clear/copy-track/copy-pattern/copy-bar,
and `pat_sceneHasActiveSteps` are the live Scene-indexed operations.

`pat_initPatternSet` and `pat_initScene` clear only bits. The remaining
menu-shaped setters are deliberately storage-free compatibility no-ops while
the menu ID table is compacted. There is no live `Step`, track timing,
automation, note, velocity, probability, pattern-next, main-step shadow, or
separate Pattern staging allocation. Filesystem v3 persistence reads/writes
the same seven sixteen-byte rows through this contract. Affiliates are
SceneData, Sequencer, UI/LED/copy-clear, Euklid/SOM, and filesystem; none may
add a parallel Pattern owner. See `SRAM_MANIFEST.md` for the linked 1,792-B
sixteen-Scene Pattern payload and the SRAM1 Pattern reservation.

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
| `pat_sceneHasActiveSteps(scene)` | Scan complete Scene PatternData for any active Step; hides the bridge storage shape from Load-menu LEDs. | Menu Load Scene LED helper |
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

## Core/Bank/Scene/Pattern/EuklidGenerator

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

## Core/Bank/Scene/Pattern/SomGenerator

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
MidiParser, EuklidGenerator, SomGenerator, filesystem, triggerJacks, mod nodes,
InstrumentManager.

Purpose: owns menu pages, visible/edit state, `parameter_values`, dynamic
descriptor-backed VOICE page resolution, load/save UI, encoder and endless-pot
edit dispatch, and post-load operation follow-up.

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
| `menu_setVoiceModeShowMorph(onOff)` | Toggle VOICE-page morph endpoint overlay for descriptor-backed instrument cells; repaint/edit helpers resolve the active slot's main or morph image through InstrumentManager. | buttonHandler `SHIFT+VOICE` mode |
| `menu_loadInstrumentVoicePressed(voice)` / `menu_loadInstrumentExit()` | Enter/select or leave nested Instrument Load/Save. Entry establishes the combined Kit/Instrument HCNAMES identity session; normal Instrument Load also writes the current voice to `.hctmp.<ext>`. Voice/exit boundaries invalidate that temporary session. | buttonHandler Load/Save VOICE gestures |
| `menu_loadSceneButtonPressed(scene)` | Consume Load/Save-context SEQ presses as Kit target toggles, Instrument Load one-Scene selection, or Instrument Save source-Scene selection. | buttonHandler SEQ press/release routing |
| `menu_loadInstrumentTransactionBusy()` | Report read/save-plus-commit Instrument transaction ownership. Accepted request coordinates remain immutable; number-only scrolling may coalesce the newest pool row, while Scene/voice/type/mode changes are boundaries rather than retargeting. | buttonHandler/Menu gates |
| `menu_paramUsesMorphView(paramNr)` / `menu_getParameterDisplayValue(paramNr)` / `menu_getParameterEditPtr(paramNr)` | Legacy display/edit helpers for static parameter IDs. Descriptor-backed VOICE cells use the internal dynamic cell resolver instead. | Menu repaint, encoder edits, endless-pot edits |
| `menu_showStepTrackSettingsFirstHalf()` / `menu_toggleStepTrackSettingsHalf()` | Select STEP front-page first half or toggle to the second half where per-track shuffle lives. | buttonHandler STEP-mode voice/track buttons |
| `menu_resetSaveParameters()` | Reset load/save UI state for the promoted Kit/Scene/Bank families and clear nested Instrument/HCNAMES/temp-session state. Retired File/Dir types are not restored by Dev Mode. | load/save page |
| `menu_setNumSamples(num)` | Sample count display state. | sample/filesystem paths |
| `menu_getActivePage()` / `menu_getSubPage()` | Read visible page/subpage. | buttonHandler, ledHandler |
| `menu_getActiveVoice()` / `menu_setActiveVoice(voiceNr)` | UI active voice. | buttonHandler, MidiParser, PatternData callers |
| `menu_areMuteLedsShown()` | Mute LED UI state query. | ledHandler/buttonHandler |
| `menu_setShownPattern(patternNr)` / `menu_getViewedPattern()` | UI viewed/edited pattern. | buttonHandler, ledHandler, PatternData callers |

Shared state used by clients:

- `parameter_values[]`, `parameters2[]` for legacy/static parameter paths.
- Active Scene instrument images for descriptor-backed VOICE cells.
- `menu_activePage`, `menu_activeVoice`, `menu_playedPattern`,
  `menu_shownPattern`, `menu_muteModeActive`
- `modTargets[]`, `paramToModTarget[]` for legacy/static target naming.
  Descriptor and Scene target cells display labels through InstrumentManager
  and SceneModTargets, with no hardcoded per-instrument target lists in Menu.

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

## Core/Bank/Scene/Preset/presetManager

Affiliate modules: filesystem, Menu, MidiParser, Sequencer, DSP voice/mod nodes,
InstrumentManager, SceneData.

Purpose: owns preset load/save status, Scene kit apply, and sound-parameter
application. The folder now lives under `Core/Bank/Scene/Preset/`; public function
prefixes remain `preset_*` for the mechanical move.

| API | Use | Usual callers / clients |
|---|---|---|
| `preset_init()` | Initialize preset operation status. | boot |
| `preset_getStatus()` / `preset_getCompletedOp()` / `preset_getRequestSlot()` / `preset_getRequestType()` / `preset_ackStatus()` | Async operation status protocol. | Menu |
| `preset_loadDrumset(presetNr, isMorph)` / `preset_saveDrumset(presetNr, isMorph, source_scene)` | Async kit/morph load/save. Normal Kit Save writes resident normal/morph endpoints; KitMrp Save writes current interpolated values into both endpoint sections. | Menu |
| `preset_loadKitMorphForScenes(presetNr, scene_mask)` | Parse a Kit directory through normal staging, then copy source normal endpoint values into selected resident morph endpoints only where source/destination instrument types match. | Menu KitMrp Load |
| `preset_loadGlobals()` / `preset_saveGlobals()` | Async globals load/save. | Menu |
| `preset_loadPattern(presetNr)` / `preset_savePattern(presetNr)` | Async pattern load/save. | Menu |
| `preset_saveAll(presetNr, isAll)` / `preset_loadAll(presetNr, isAll)` | Async all/performance load/save. | Menu |
| `preset_loadName(presetNr, what)` / `preset_applyLoadedName()` | Async slot name browsing. | Menu |
| `preset_loadInstrument(scene, slot, type, browser_index)` | Post one immutable root Instrument request; request coordinates publish only after filesystem accepts it. | Instrument Load lower-row browser |
| `preset_saveInstrumentTemp(scene, slot)` / `preset_loadInstrumentTemp(scene, slot, type)` | Save or restore the hidden typed `.hctmp.<ext>` used by the reversible Instrument Load `kit` row. These operations publish neither HCNAMES nor `.hcindex`. | Menu Instrument Load session |
| `preset_loadInstrumentMorph(scene, slot, type, browser_index)` | Post one root Instrument request for morph endpoint import; type must match the destination slot and only morphable source normal endpoint values are copied into the resident morph image. | Instrument Load `<Type>Mrp` lower-row browser |
| `preset_saveInstrument(scene, slot, display_name)` | Post one root Instrument Save request from a resident Scene/voice slot. The display stem is captured at request acceptance and filesystem writes `Instrument/<stem.ext>`. | Instrument Save nested Save-page OK |
| `preset_saveInstrumentMorph(scene, slot, display_name)` | Post one root InstrumentMrp Save request. The writer uses the normal Instrument schema but writes the current interpolated values into both endpoint sections and does not rename resident source metadata. | Instrument Save `<Type>Mrp` OK |
| `preset_loadSceneForScenes(presetNr, scene_mask)` / `preset_saveScene(presetNr, source_scene)` | Load/save root Scene library folders through the non-Pattern Scene stage and direct Pattern phase. | Load/Save Scene |
| `preset_loadBank(presetNr, scene_mask)` / `preset_saveBank(presetNr, scene_mask)` | Load/save root Bank folders with a 16-bit local-Scene mask. Load validates bankset.bcg v2, preserves unselected resident Scenes/HCNAMES rows, and can report a valid empty Bank; Save writes selected child payloads through a temporary sibling then promotes it. | Load/Save Bank, boot |
| `preset_loadFirstAvailableSceneOrKit()` | Fallback after absent/empty Bank: lowest root Scene, then lowest root Kit, then defaults. | boot, Bank Load completion |
| `preset_sendDrumsetParameters()` | Synchronous pre-audio clear plus six-slot tagged reset/routing/descriptor-image apply. It intentionally leaves target installation to the exact ordinary Scene worker started after audio initialization. | Menu boot/load path |
| `preset_applySoundParameter(paramNr, value, recordAutomation)` | Direct legacy/static sound parameter application and optional automation recording. | Menu, morph, reset-lock |
| `preset_setInstrumentParameter(scene, slot, descriptor_index, image, value, recordAutomation)` | Store one descriptor-backed instrument main/morph value and apply/record when appropriate. | Menu dynamic VOICE cells, storage |
| `preset_setSupplementalParameter(scene, slot, descriptor_index, value)` | Store one single-endpoint supplemental descriptor value. | Menu dynamic VOICE cells, storage |
| `preset_applyInstrumentRuntimeValue(scene, id, value)` | Apply one descriptor-backed instrument value to the DSP runtime through InstrumentManager. | Menu, morph/runtime apply |
| `preset_applyKitAudioRouting(scene, slot)` | Apply one Scene kit slot's audio route to mixer routing. | Kit load/apply paths |
| `preset_applySceneSettings(scene)` | Apply Scene settings/global runtime values. | Boot/load paths |
| `preset_applyVoiceDecimationAllRuntime(value)` | Apply a transient Scene Decimation value for LFO modulation without changing the retained PERF `srt` setting. | InstrumentManager LFO Scene target path |
| `preset_applyVelocityModTarget(voice, targetParam)` | Direct velocity mod destination update. | Menu, preset load apply |
| `preset_applyLfoModTarget(lfo, targetParam)` | Direct LFO mod destination update. | Menu, preset load apply |
| `preset_startDrumsetApply()` / `preset_tickDrumsetApply()` | Clear outgoing modulation, quiet/trigger-time reset and image-apply all six incoming tagged slots, then keep the Scene gate active while the existing Instrument cursor normalizes/rebinds every source's two LFO pairs and velocity against the final type vector. | Menu and `main.c` post-audio boot activation |
| `preset_startKitMorphApply()` | Drain same-type KitMrp endpoint copies and refresh active-scene Morph runtime images without replacing kit membership or routing. | Menu KitMrp completion |
| `filesystem_loadedInstrumentWasTemporary()` plus `preset_startInstrumentApply(scene, slot, mark_autosave_whole_instrument)` / `preset_tickInstrumentApply()` | Filesystem exposes the existing request-local root-pool versus hidden-`kit` origin during completion; Menu passes that immutable result as the mark flag. A root-pool commit immediately marks each destination's type/Normal/Morph payload for AutoSave; hidden restore supplies zero. Active Scene path clears all outgoing modulation owners, commits/resets the incoming runtime, rebuilds all six Morph images, then normalizes/rebinds all six source target relationships. | Filesystem, then Menu Instrument completion |
| `preset_startInstrumentMorphApply(scene, slot)` | Copy staged same-type Instrument normal endpoints into the destination morph image, immediately mark only the committed Morphable Morph payload for AutoSave, and refresh active-scene Morph runtime. | Menu InstrumentMrp completion |
| `preset_morph(morph)` / `preset_morphVoice(slot, morph)` / `preset_morphTick()` / `preset_getMorphValue(index, morph)` | Rate-limited descriptor Morph interpolation/application. Global Morph bulk-sets all six per-voice Morph values; per-voice Morph is the engine input. | Menu, MIDI, velocity modulation, main loop |
| `presetMorph_setVoiceLfoModulation(source_slot, target_slot, amount, polarity, lfo_value)` / `presetMorph_clearLfoSource(source_slot)` | Maintain the hidden per-voice Morph LFO overlay that is summed around retained per-voice Morph base values. | InstrumentManager/LFO dispatch |

## Core/Bank/Scene/Preset/ParameterArray

Affiliate modules: Menu, Preset manager, MidiParser, modulationNode, DSP voice
modules, mixer, filesystem, PatternData.

Purpose: owns the legacy numeric parameter id map and runtime pointer map from
flat sound parameter ids to DSP engine fields. After Session 032, instrument
file keys and dynamic VOICE pages are descriptor-backed through
InstrumentManager/SceneData instead of this table. `ParameterArray` remains
important for older static sound parameters and for the legacy AutomationNode
path. Session 033 added descriptor-aware LFO/velocity adapters; AutomationNode
still needs the same descriptor/Scene target migration.

| API / Data | Use | Usual callers / clients |
|---|---|---|
| `enum ParamEnums` / `PAR_*` / `END_OF_SOUND_PARAMETERS` / `NUM_PARAMS` | Legacy/static parameter id space for sound, menu, pattern, and globals. | Menu, filesystem, Preset, MidiParser, PatternData |
| `TYPE_UINT8`, `TYPE_FLT`, `TYPE_SPECIAL_F`, `TYPE_UINT32`, `TYPE_SPECIAL_P`, `TYPE_SPECIAL_FILTER_F` | Type tags for runtime parameter pointer entries. | ParameterArray, modulationNode |
| `ptrValue` | Float/integer value carrier for typed writes. | modulationNode, ParameterArray |
| `Parameter` / `parameterArray[]` | Map legacy sound parameter id to target DSP field and value type. | ParameterArray, modulationNode |
| `paramArray_setParameter(idx, newValue)` | Write one typed value into the mapped DSP field when the id/pointer are valid. | modulationNode restore/apply paths |
| `parameterArray_init()` | Fill the sound-parameter pointer/type map. | `main.c` boot |
| `extern parameter_values[]` | Legacy/static parameter byte store declaration. Descriptor-backed instrument values live in Scene storage. | Defined in Menu today |

## Core/DSP/Instruments/InstrumentManager

Affiliate modules: SceneData, Preset/Morph, Menu, filesystem/storageTypes,
MidiVoiceControl, mixer, modulationNode, LFO/velocity paths, and all four
instrument engines.

Purpose: owns the immutable instrument registry and the active Scene
slot-to-runtime bridge. Descriptor tables remain beside each instrument type;
InstrumentManager supplies registry lookup, assignment policy, target
validation, Choke sibling lookup, current type dispatch, and runtime lifecycle
operations that callers must not duplicate.

Resident Instrument parameter values use the byte
`instrument_param_value_t` domain. Target selectors use the byte
`instrument_target_token_t` domain with `0xff` as off. Wide descriptor and
Scene IDs are resolved only for validation, display, or runtime dispatch; they
are not the stored selector representation. Velocity target selection is
self-scoped plus its source-voice Morph token; LFO selection supports self,
voices, and the Scene namespace.

| API | Use | Usual callers / clients |
|---|---|---|
| `instrumentManager_registryEntry()` / `registryCount()` / `registryEntryAt()` / `typeDisplayLabel()` / `typeFlags()` | Immutable type metadata: token, extension, label, Basic/Advanced/Choke flags, descriptor and menu tables. | Menu, storage/filesystem, converter-aligned tooling |
| `instrumentManager_typeSelectableForSceneSlot(scene, slot, type)` | Enforce any-Basic/two-Advanced replacement policy. | Instrument Load type browser |
| `instrumentManager_chokeDescriptorIndexForBase(type, base, out)` | Resolve `<base>_choke` sibling within one type. | Menu VOICE7 resolver |
| `instrumentManager_runtimeInstance()` / trigger/filter/async/sync/pan/LFO dispatch family | Resolve the runtime object for the current active Scene slot type. | mixer, MIDI/Sequencer trigger paths, Preset |
| `instrumentManager_clearAllRuntimeModulationTargets()` | Restore/clear both LFO pairs and velocity target for every outgoing current source before a slot type changes. | Preset staged Instrument commit |
| `instrumentManager_resetRuntimeSlot(slot)` | Initialize only the incoming committed slot/type runtime object. | Preset staged Instrument commit |
| `instrumentManager_writeRuntime()` / target validation/stepping helpers | Apply descriptor/supplemental bindings and validate canonical targets against current slot types. | Preset, Menu, modulation paths |
| `instrumentManager_updateLfoAdapters(source_slot, pair, lfo, polarity, amount)` | Update InstrumentManager-owned LFO destinations: descriptor-domain adapters, slot decimation, and Scene targets. Descriptor adapters shape in parameter space and then call the normal runtime writer. | `lfo.c` |

Transaction rule: clear all current owners before changing Scene slot type;
commit the staged slot; reset the incoming runtime; rebuild retained runtime
images; then normalize and rebind all sources. Clearing after the type swap
cannot recover a dynamic-pool source owned by the outgoing identity.

## Core/Bank/Scene/SceneModTargets

Affiliate modules: Menu, InstrumentManager, Preset/Morph, future FX modules.

Purpose: owns the canonical ID/name/range list for Scene-level sound
parameters that are modulation targets but are not parameters of a swappable
instrument in a voice slot. This keeps per-instrument descriptor tables dynamic:
voice-local targets come from the currently installed instrument descriptor
table, while non-voice sound targets come from this Scene namespace.

Current target order:

- `1vm`, `2vm`, `3vm`, `4vm`, `5vm`, `6vm`
- Scene Decimation `srt`

Scene Decimation deliberately appears after the six Morph targets so the
velocity target list does not place it directly beside a voice-local
`instrument_decimation` row, which also uses short label `srt`.

| API | Use | Usual callers / clients |
|---|---|---|
| `sceneModTarget_count()` | Number of Scene target descriptors. | Menu/InstrumentManager list traversal |
| `sceneModTarget_idAt(index)` | Convert list index to canonical Scene target ID. | Menu stepping/display |
| `sceneModTarget_valid(id, use_mask)` | Validate a target for velocity and/or LFO use. | Menu, InstrumentManager |
| `sceneModTarget_get(id)` | Read descriptor metadata: kind, voice slot, range, labels, flags. | Menu, InstrumentManager |
| `sceneModTarget_first(use_mask)` / `sceneModTarget_step(current, dir, use_mask)` | Non-looping target-list traversal with skipped invalid entries. | Menu target pickers |

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

## Core/Bank/Scene/Autosave and AutosaveTrace

Affiliate modules: BankData, SceneData, Preset, filesystem, config.

Purpose: `Autosave.c/.h` owns the hidden-record wire contract, live-byte
projection, one canonical dirty mask, atomic dirty operations, typed scalar and
whole-region marker vocabulary, and CRC helpers. It owns no file handle or
scheduler. `AutosaveTrace.c/.h` is a logging-only observer with no filesystem
ownership. Exact format and trace-field semantics remain authoritative in
`AUTOSAVE.md` and `DEV_MODES.md` rather than being duplicated here.

| API family | Interchange rule | Usual callers / clients |
| --- | --- | --- |
| `autosave_mark*Dirty(...)` | Retained owners store/commit first, then mark typed coordinates. Producers perform SRAM-only work and never calculate wire offsets. | BankData, SceneData, Preset; future successful whole-object commits |
| `autosave_maskHasDirty()` / atomic take and merge helpers | One canonical mask coordinates foreground capture and interrupt-reachable producers; filesystem may consume through the documented API but never owns a second mask. | filesystem AutoSave scheduler/writer |
| `autosave_getLivePayloadByte()` and format/CRC helpers | Project final resident bytes and serialize/validate v1 records without copying C structs as the wire format. | filesystem AutoSave setup/validation/copy |
| `autosave_setMutationTrackingEnabled()` / complete-resident dirty boundary | Filesystem policy enables tracking only after successful setup; AutoSave OFF/new-session transitions preserve active-transaction safety. | filesystem policy lifecycle |
| `autosaveTrace_record()` and ring read/ack APIs | Producers append bounded RAM records; only filesystem may append them to the diagnostic file and acknowledge them after durable sync. | AutoSave and filesystem under `DEV_MODE_LOGGING` |

## Core/Hardware/SD/filesystem

Affiliate modules: Preset, Menu, PatternData, SampleMemory, storageTypes,
SceneData, AutoSave, AutoSaveTrace, and development-mode boot logging.

Purpose: public typed async filesystem facade. It serializes pattern data
through PatternData accessors after Session 028. Normal kit load/save scans,
opens, and writes root `Kit/NNN Name/` directory-format data, root Instrument
Load/Save operates on registry-owned `Instrument/<type>/` pools, HCNAMES owns
resident display identity, and the retired File/Dir compatibility surface
performs no work. Storage text parsing/formatting and descriptor-key validation
stay in `storageTypes.c/h`.

| API | Use | Usual callers / clients |
|---|---|---|
| `filesystem_initCardAndMountBlocking()` / `filesystem_initAfterCardReady()` | Boot card init/mount. | `main.c` |
| `filesystem_bootLoggingBegin()` / `filesystem_bootLoggingArm()` / `filesystem_bootLoggingTimedOut()` / `filesystem_writeBootFailureLogBlocking()` / `filesystem_bootLoggingEnd()` | Own the pre-audio ten-second filesystem deadline and one bounded best-effort retained-code recovery write. Private detail hooks may change only the label inside an armed deadline. | `main.c`; filesystem boot operations under `DEV_MODE_LOGGING` |
| `filesystem_tick()` | Pump asyncfatfs work. | main loop |
| `filesystem_status()` / `filesystem_ack()` | Operation status protocol. A direct Menu callback must snapshot its terminal result before acknowledging it; if it does not immediately post another filesystem request, it must acknowledge `DONE`/`ERROR` before releasing its UI owner so autonomous idle-only schedulers can run. | Preset/Menu |
| `filesystem_requestLoad(type, slot, cb)` / `filesystem_requestSave(type, slot, cb)` | Async typed load/save. For `FS_FILE_KIT`, load is `Kit/NNN Name/kitset.kcg` plus instruments and save routes to the new Kit directory writer. For `FS_FILE_MORPH`, load/save remains legacy `.SND`. | Preset |
| `filesystem_requestLoadKitForScenes(slot, scene_mask, cb)` | Parse one direct Kit library slot `000..999` into staging and fan the completed Kit payload into selected resident Scenes. | Preset/Menu Kit Load |
| `filesystem_requestLoadKitMorphForScenes(slot, scene_mask, cb)` | Parse one Kit directory into staging only so Preset can copy matching source normal endpoints into resident morph endpoints. | Preset/Menu KitMrp Load |
| `filesystem_requestSaveKitDirectory(slot, source_scene, display_name, morph_projection, cb)` | Create/open visible `Kit/<NNN Name>/` with asyncfatfs LFN creation, stream six descriptor-keyed instrument files with visible LFN stems, then stream `kitset.kcg` with returned 8.3 aliases. `morph_projection` writes current interpolated values into both endpoint sections. | Preset/Menu Kit Save |
| `filesystem_requestLoadSceneForScenes(slot, scene_mask, cb)` | Parse `sceneset.scg` plus embedded Kit into the independent non-Pattern stage, commit them after validation, then read Pattern directly into final Scene SRAM and validate the Effect placeholder. Pattern is intentionally non-atomic. On successful terminal root completion, Preset marks the implemented Scene-without-Pattern AutoSave scope before reporting `PRESET_OP_SCENE_LOAD`. | Preset/Menu Scene Load, boot |
| `filesystem_requestSaveSceneDirectory(slot, source_scene, display_name, cb)` | Replace one root Scene slot and stream `sceneset.scg`, embedded `Kit <name>/`, six Instrument files, thin `pattern.pat`, and placeholder `effects.fx` from a resident Scene. | Preset/Menu Scene Save |
| `filesystem_requestScanInstruments(cb)` / `filesystem_instrumentCount()` / `filesystem_instrumentName()` / `filesystem_instrumentDisplayIndex()` | Scan/query the single shared 1,000-entry root Instrument browser cache for the currently loaded type. | Menu Instrument Load; boot uses one type-at-a-time scan/index passes |
| `filesystem_requestLoadInstrument(scene, slot, type, browser_index, cb)` | Capture one typed index selection into immutable operation scratch and validate it into the one Instrument candidate stage without mutating live SceneData. | Preset Instrument request |
| `filesystem_requestSaveInstrument(scene, slot, display_name, cb)` / `filesystem_requestSaveInstrumentMorph(scene, slot, display_name, cb)` | Save one resident Scene/voice slot to root `Instrument/<stem.ext>` using LFN/case-sensitive create and the descriptor-keyed instrument text writer. The Morph variant writes current interpolated values into both endpoint sections and preserves resident source naming. | Preset Instrument Save |
| `filesystem_requestSaveInstrumentTemp(scene, slot, cb)` / `filesystem_requestLoadInstrumentTemp(scene, slot, type, cb)` | Write/read the hidden typed `.hctmp.<ext>` through the normal serializer/parser without publishing it to HCNAMES or `.hcindex`. | Preset/Menu reversible `kit` row |
| `filesystem_ensureAutosaveFilesBlocking()` / `filesystem_setAutosaveEnabled(enabled)` / `filesystem_autosaveEnabled()` | Establish the hidden pair at boot, apply runtime policy, and authorize mutation tracking/background work only after successful setup. Format and failure rules are in `AUTOSAVE.md`. | `main.c`, Menu/settings policy |
| `filesystem_autosaveTraceFlushBlocking()` | Bench-only durable boundary for currently pending lifecycle records; ordinary runtime trace flushing is autonomous and lower priority. | temporary test harness only |
| `filesystem_markSettingsDirty()` | Increment the keyed-settings change revision; the one-second writer acknowledges only the revision it actually serialized and synced. | Menu settings policy |
| `filesystem_loadedInstrumentSlot()` | Borrow the validated candidate payload for Preset's ordered commit. Names are exchanged through identity rows, not staged filename/stem accessors. | Preset only |
| `filesystem_requestLoadName(type, slot, cb)` | Async name load. For `FS_FILE_KIT`, returns the cached directory scan name instead of opening a `.SND` header. | Preset/Menu |
| `filesystem_requestScanKits(cb)` | Scan root `Kit/` directories into the shared slot-indexed name cache; non-blank rows provide occupancy. | main startup, Menu |
| `filesystem_requestReloadLibraryIndex(kind, cb)` / domain-specific Kit/Scene/Bank wrappers | Read an existing slot-ordered root `.hcindex` into the one shared cache; blank rows remain slot positions. This is read-only cache restoration and never scans or rewrites the namespace. The accepted root Scene/Bank terminal callback acknowledges its captured result before Menu teardown. | Menu entry/type changes and post-DSP root Scene/Bank Load terminal work |
| `filesystem_createLibraryIndexBlocking(kind)` | Boot-only repair/scan and slot-ordered `.hcindex` rebuild for one root library. Runtime numbered-root Saves use the common asynchronous scan/rebuild continuation instead. | boot |
| `filesystem_clearNameCache()` / `filesystem_libraryNameCacheLoaded(kind)` | Dispose/query the one active Instrument/Kit/Scene/Bank browser-name cache. | Menu lifecycle and index gating |
| `filesystem_setIdentityName(row, name)` / `filesystem_identityName(row)` / `filesystem_identityNameMutable(row)` / `filesystem_clearIdentityNames()` | Own the logical Bank/Scene/Kit/six-Instrument identity interface. Bank aliases BankData; the other eight rows occupy 72 bytes. | Menu and filesystem HCNAMES/load/save completion |
| `filesystem_residentSource(row)` / `filesystem_setResidentSource(row, source)` / `filesystem_resolveResidentSource(row, resolved_row)` | Read, stage, and resolve the paired HCNAMES provenance token. The resolver follows Instrument -> Kit -> Scene -> Bank and does no I/O; the future AutoSave reader owns target-open/fallback retries. | filesystem successful-load boundaries; future AutoSave boot reader |
| `filesystem_requestLoadResidentKitName()` / `filesystem_requestUpdateResidentKitNames()` | Borrow HCNAMES for one Scene's Kit-plus-six block or preserve/overlay full seven-row blocks for a dirty Scene mask. | combined Kit/Instrument Menu session |
| `filesystem_requestLoadResidentInstrumentName()` / `filesystem_requestUpdateResidentInstrumentNames()` | Legacy/narrow one-Instrument HCNAMES row operations; the combined Menu entry normally loads the seven-row Kit block. | Menu/filesystem compatibility paths |
| `filesystem_requestLoadResidentSceneName()` / `filesystem_requestUpdateResidentSceneNames()` | Borrow or update only selected Scene identity rows. | Scene menu and Scene completion |
| `filesystem_repairLibraryNamesBlocking()` / `filesystem_repairInstrumentNamesBlocking()` / `filesystem_requestRepairBankNames()` | Canonicalize one namespace with one-candidate rename/sync/rescan before index publication or Bank payload load. No `.hcrepair` journal exists. | boot index wrappers and Bank Load |
| `filesystem_installSamplesBlocking()` / `filesystem_installLoopsBlocking()` | Blocking sample/loop install under audio suspend. | Menu |
| `filesystem_loadedName()` | Read loaded name buffer. | Preset |
| `filesystem_kitSlotExists(slot)` | Query the Kit scan cache for a direct `000..999` numbered folder. | Menu/future browsers |
| `filesystem_kitSlotName(slot)` | Return the shared-cache-backed eight-character Kit name or `Empty   `. The slot number is not part of the cached row. | Menu Load/Save page |
| `filesystem_sceneSlotName(slot)` | Return the shared-cache-backed root Scene name or `Empty   `; Bank-local Scenes are excluded. | Menu Load/Save page |
| `filesystem_bankSlotName(slot)` | Return the shared-cache-backed root Bank name or `Empty   `; Bank-local child Scenes are excluded. | Menu Load/Save page |
| `filesystem_requestScanTestFiles()` / `filesystem_requestScanTestDirs()` and File/Dir test accessors | Retired compatibility API: returns empty/failure and starts no operation or cache. | stale developer callers only |
| `filesystem_diagOp()` / `filesystem_diagPhase()` / `filesystem_diagBytesDone()` | Diagnostics. | diagnostics/future UI |

The retired File/Dir facade has no multi-entry filesystem cache. `menu.c`
nevertheless still links `menu_testEditName[49]`,
`menu_testResultName[49]`, and nine result-screen bytes from its unreachable
compatibility renderer: 107 bytes total. Those are residual Menu UI state, not
filesystem API output, musical identity, or permission to restore the old
diagnostics.
| `filesystem_lastMountResult()` / `filesystem_bootDetectedUnsupportedCard()` | Boot/card status. | main/Menu |
| `filesystem_takeStaleGlobalsWarning()` | One-shot stale globals warning source. | Menu |

Important private Phase 2 kit helpers:

- `filesystem_scanKits_tick()` opens root `Kit/` by exact display component,
  iterates asyncfatfs objects, and records display names in the shared cache;
  no per-slot alias, occupancy array, or compatibility slot map is retained.
- `filesystem_loadKitDirectory_tick()` opens the selected kit folder, parses
  `kitset.kcg`, and loads six listed instrument files into the 2,048-byte
  `kit_t` stage. It fans out the complete Kit payload only after every file
  validates; runtime apply is handled later through Preset/InstrumentManager.
- `filesystem_loadInstrument_tick()` parses one root Instrument into private
  `kit_instrument_slot_t` candidate staging. It must never reset a live Scene
  slot during asynchronous I/O; Preset owns the post-completion transaction and
  identity publication is separate.
- `filesystem_saveInstrument_tick()` writes one resident Scene/voice slot into
  root `Instrument/` with `afatfs_mkdir_lfn()` plus `afatfs_fopen_lfn()`,
  streams `storage_formatInstrumentLine()`, and updates the Instrument browser
  cache from the actual display/alias pair.
- `filesystem_saveKitDirectory_tick()` creates/opens root `Kit/`, creates/opens
  the target LFN Kit folder after deleting every physical directory for that
  slot, streams six instrument files first, and streams `kitset.kcg` after
  member aliases are known. `kitset.kcg` remains authoritative for load, but
  Session 038 Kit Save no longer relies on leaving stale unreferenced files in
  place.
- `filesystem_loadSceneDirectory_tick()` validates complete Scene folders in
  the Scene settings-plus-Kit stage, commits that non-Pattern payload, then
  reads Pattern directly into final Scene SRAM. It imports legacy embedded-kit
  `audio_out` only when `sceneset.scg` lacks Scene-owned `audio_out`.
- `filesystem_saveSceneDirectory_tick()` writes the current Scene folder shape:
  `sceneset.scg`, embedded Kit without `audio_out`, six instruments, thin
  `pattern.pat`, and placeholder `effects.fx`.
- `filesystem_saveBankDirectory_tick()` promotes the staged Bank tree, then
  parks the original callback while the root Bank directory is rescanned and
  `/Bank/.hcindex` is rewritten. The same Save-owned rebuild chain is used for Kit and
  root Scene saves.
- `filesystem_loadLibraryIndex_tick()` reads one slot-preserving Kit, root
  Scene, or root Bank `.hcindex` into the shared cache. It never compacts blank
  rows and never retains per-slot aliases.
- `filesystem_residentNames_tick()` reads the 129-row fixed HCNAMES register,
  optionally overlays exactly the action-owned rows, rewrites the complete
  variable-length file, and finishes only that metadata transaction. It does
  not infer whether a root namespace changed: Scene Save declares its own
  rebuild, while Scene/Bank Load defers read-only browser restoration until
  after DSP apply.
- Bank Load retains only a child-present mask. It rescans the selected Bank
  parent for each requested child and keeps one display/open component in
  operation scratch; it never rebuilds a 16-child name or alias cache. Its
  completed loaded-Scene result becomes true only after shared Scene validation
  and commit and is consumed before the later index-reload request.
- Kit folders prefer `NNN Name` and accept `NNN_Name`; scan has a short-alias
  fallback for FAT aliases like `000INI~1` or `001SLA~1`.

asyncfatfs boundary:

- Low-level asyncfatfs API rules live in
  `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`.
  Module-level code should reuse `filesystem.c` or those documented
  component/object primitives instead of creating one-off FAT writers.
- Dot-prefixed files/directories are ordinary objects. Product scanners filter
  after object iteration.
- Native `afatfs_deleteTree()` provides filesystem-level recursive cleanup for
  one captured object, but its present recursive replacement behavior is a
  known defect: an overwrite may leave the old Bank, root Scene, or Kit
  directory. Repair that primitive; do not add temporary/old sibling promotion
  or boot cleanup as a workaround. Atomic/journaled replace is also missing,
  so no flow may be described as power-loss recoverable.
- `afatfs_getDiagnosticSnapshot()` and `sdcard_getTransportSnapshot()` are
  logging-only, read-only observation helpers for a frozen boot-time
  `ASENSURE` failure capsule. They copy live allocator/cache/file and SD
  transport state without polling, allocating, issuing I/O, changing callbacks,
  retries, or filesystem ownership. `filesystem.c` may call them only while
  freezing the capsule immediately before existing boot recovery destroys the
  active state; ordinary product code must not use them as control flow.

Private but important pattern serialization helpers:

- `filesystem_patternStepPtr()`
- `filesystem_patternMainPtr()`
- `filesystem_patternSettingPtr()`
- `filesystem_patternLengthPtr()`

These select normal PatternData storage or `PATTERNDATA_STAGING_PATTERN` when
loading the currently active pattern.

## Core/Hardware/SD/storageTypes

Affiliate modules: filesystem, SceneData, InstrumentManager, generated
`SD_CARD/Kit` data, `tools/convert_legacy_kits.py`.

Purpose: pure Phase 2/3 storage-format layer. It owns text schema parsing and
formatting, validation, numbered folder parsing, filename/type checks,
display-name normalization, descriptor-key-to-Scene-storage writes, and
descriptor-image text emission. It must not call `asyncfatfs`; filesystem owns
I/O and passes complete text lines/names into this layer. All functions in this
layer use the `storage_` prefix.

| API / Data | Use | Usual callers / clients |
|---|---|---|
| `storage_status_t` | Parser/validator result codes. | filesystem |
| `storage_instrument_type_t` | Format-level type enum for `.drm`, `.snr`, `.cym`, `.hat`. | kitset/instrument parser |
| `storage_kitset_t` | Incremental parse state for `kitset.kcg`. | filesystem directory kit loader |
| `storage_instrument_state_t` | Incremental parse state for one instrument file. | Kit and root-Instrument filesystem loaders |
| `storage_kitsetInit()` / `storage_kitsetParseLine()` / `storage_kitsetFinalize()` | Validate `kitset.kcg`, collect instrument filenames/types, and retain legacy `audio_out` side data without making it required. | Kit and Scene filesystem loaders |
| `storage_kitsetHasCompleteLegacyAudioOut()` / `storage_kitsetLegacyAudioOut()` | Expose complete legacy embedded-kit routing only for Scene Load fallback when `sceneset.scg` has no `audio_out`. | `filesystem_loadSceneDirectory_tick()` |
| `storage_instrumentStateInit()` / `storage_instrumentParseLine()` / `storage_instrumentFinalize()` | Validate one instrument file and write descriptor-indexed `[params]`/`[morph]` values into caller-owned Kit/slot staging. | Kit and root-Instrument loaders |
| `storage_instrumentCopyMainToMorphFallback()` | Copy mapped main values into morph buffer when an instrument has no `[morph]` section. | `filesystem_loadKitDirectory_tick()` |
| `storage_instrumentTypeFromText()` / `storage_instrumentFilenameMatchesType()` | Convert/validate type strings and extensions. | kitset/instrument validation |
| `storage_instrumentTypeToText()` / `storage_instrumentTypeExtension()` | Convert type enum back into schema token/extension for save. | Kit Save writer |
| `storage_formatKitsetLine()` / `storage_formatInstrumentLine()` | Emit one bounded schema line at a time for streaming Kit Save and root Instrument Save. Instrument emission writes `self` for own-slot LFO voice selectors. | filesystem Kit/Instrument Save |
| `storage_makeSavedInstrumentDisplayFilename()` | Generate one visible Instrument component from the active fixed-width identity stem, slot type, and optional voice suffix. No Scene-retained stem exists; asyncfatfs returns any required short alias. | filesystem Kit/Instrument Save |
| `storage_patternStubStateInit()` / `storage_patternStubParseLine()` / `storage_patternStubFinalize()` | Validate thin Scene `pattern.pat` placeholders. | Scene Load |
| `storage_formatPatternStubLine()` / `storage_formatEffectPlaceholderLine()` | Emit thin Scene placeholder child files one line at a time. | Scene Save |
| `storage_parseNumberedFolder()` | Parse visible numbered folders `NNN Name` or `NNN_Name` into direct `000..999` slot plus eight-character display name. Slot `000` is real. | Kit/Scene/Bank scan |
| `storage_copyDisplayName()` / `storage_copyFilename()` | Fixed-width display-name normalization and short filename copying. | filesystem/parser code |

Current ownership decisions:

- `kitset.kcg` owns only format/version validation plus per-slot kit
  membership, instrument filenames, and instrument types.
- Legacy `audio_out` lines in `kitset.kcg` are compatibility-only side data.
  New writers do not emit them. Root Kit Load ignores them; Scene Load may
  import them only for old embedded Kits when `sceneset.scg` lacks `audio_out`.
- The kit name comes only from the kit folder name. It is not stored in
  `kitset.kcg`.
- `storage_kitset_t` retains the six parsed `file=` components only for the
  lifetime of one active manifest parse. They are never copied into `scene_t`
  or `kit_t`.
- Instrument files own per-voice descriptor values, including volume, pan, and
  optional `[morph]` endpoint values.
- MIDI note/channel values, `voice_decimation_all`, per-voice audio routing,
  FX send amount, and fader mode do not belong in `kitset.kcg` or instrument
  files; they belong in Scene settings.
- Missing instrument `[morph]` data is treated as "copy main parameters into
  morph" for morphable descriptors.
- Kit Save, KitMrp Save, root Instrument Save, and InstrumentMrp Save write the same instrument file schema the
  loader accepts. Instrument files emit metadata once, then one `[params]`
  section and one `[morph]` section. Supplemental target selectors are written
  in `[params]` only; `[morph]` writes morphable endpoint descriptors only.
- `self` is a file-only LFO voice selector token. storageTypes resolves it on
  load and emits it on save only when the selector points at the saved
  instrument's own slot.

## main.c

Purpose: boot ordering and foreground service scheduler.

Relevant interchange points:

- `led_processSeqLedState()` is called in the foreground loop after audio render
  opportunities and after TIM3-owned sequencer timing can mark LED state dirty.
  Do not move this into TIM3 without auditing Menu/button/LED access.
- `scene_initAll()` and `bank_init()` run before `dsp_init()` so
  InstrumentManager constructs each tagged runtime member from a defined
  retained type rather than zeroed BSS (`DRM == 0`).
- Pre-audio loading establishes a coherent six-slot image synchronously.
  Immediately after `audioCodec_init()`, `preset_startDrumsetApply()` starts the
  same live clear/image/all-source-rebind worker used by a manual Scene switch.
- `timebase_holdPreAudioMs()` is used only for the current SD pre-init,
  post-mount, and pre-Bank pacing experiment. It must not be used after audio
  startup, from an ISR, or as runtime filesystem pacing. The intermittent hang
  that motivated these holds remains unlocalized.

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
