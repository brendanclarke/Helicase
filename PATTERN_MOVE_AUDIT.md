# Pattern Move Audit - SCOPING_TARGETS 1.3

Session target: audit every function that currently stores, mutates, serializes,
or reads pattern data so the remaining sequencer-owned pieces can be moved into
`Core/Scene/Pattern/PatternData.c` with `pat_*` names. This is a refactor/move
audit only. There is no data-structure rewrite in this session.

## Scope Read

Primary context read:

- `MEMORY.md`
- `README.md`
- all of `SCOPING_TARGETS.md`
- `Core/Sequencer/sequencer.c`
- `Core/Sequencer/sequencer.h`
- `Core/Scene/Pattern/PatternData.c`
- `Core/Scene/Pattern/PatternData.h`
- pattern-touching call sites in `filesystem.c`, `menu.c`, `copyClearTools.c`,
  `buttonHandler.c`, `ledHandler.c`, `EuklidGenerator.c`, `SomGenerator.c`,
  `presetManager.c`, and `MidiParser.c`

Current tree note: a large part of the 1.3 move has already happened. Pattern
storage types and most direct UI/edit APIs now live in `PatternData.*` with
`pat_*` functions. The remaining audit target is therefore the boundary cleanup:
remove legacy `seq_*` naming from PatternData-owned storage, expose missing
PatternData helpers for the last direct `seq_patternSet` writes/reads in
`sequencer.c`, and leave transport/timing behavior in `sequencer.c`.

Explicit follow-up decisions before implementation:

- Shuffle is Pattern-owned. Future cleanup should route user-facing and
  file-load shuffle writes through PatternData, even while sequencer playback
  still consumes the audible coefficient.
- `EuklidGenerator.c`, `SomData.c`, and `SomGenerator.c` are not move targets
  for 1.3. Do not relocate their functions into `PatternData.c`.
- Do not rename Euklid or SOM functions during this pass. They are owned by
  their current Pattern generator/data modules. Only update their calls if
  required by functions or fields moving out of `sequencer.c`.
- The only actual relocation target identified for this refactor is
  `sequencer.c` -> `PatternData.c`, plus corresponding declarations/comments in
  `PatternData.h` and call-site updates.

Implementation comment mandate:

- Every moved or changed function must land with detailed comments in both the
  `.c` implementation and the `.h` declaration.
- Comments must describe what the function/change/move does, why it must exist,
  all inputs and outputs, side effects, common callers/accessors/clients, and
  related confederate functions or modules.
- Comments must also call out ownership boundaries and risks, especially ISR
  safety, file-format visibility, staging-buffer behavior, and any remaining
  sequencer-runtime dependency.
- When code moves from `sequencer.c` to `PatternData.c`, preserve the useful
  existing commentary and expand it rather than replacing it with terse notes.

## Current Storage Model

Defined in `Core/Scene/Pattern/PatternData.h`:

- `NUM_TRACKS`, `NUM_PATTERN`, `NUM_STEPS`: 7 tracks, 8 pattern slots, 128
  sub-steps.
- `Step`: 7-byte step record: volume/active bit, probability, note, two
  automation destinations and values.
- `PatternSetting`: per-pattern automatic change rule: `changeBar` and
  `nextPattern`.
- `LengthRotate`: packed byte: 4-bit length and 4-bit rotation. Length `0`
  means 16 main steps.
- `PatternSet`: full pattern storage: steps, main-step masks, pattern settings,
  length/rotation.
- `TempPattern`: staging buffer for loading one pattern while playback can keep
  reading the active one.

Approximate visible payload:

- `PatternSet`: 50,360 bytes by field sizes.
- `TempPattern`: 6,295 bytes by field sizes, likely padded to 6,296 due the
  `uint16_t` member.

File-format risk: these layouts are serialized by `.pat`, `.all`, and
performance paths. Field renaming is safe for binary layout; field reordering,
type changes, and packing changes are not part of this pass.

## Naming Special Review

The 1.3 rule says nothing in `PatternData.c` should carry `seq` naming unless it
explicitly relates back to sequencer runtime behavior. Current violations or
special cases:

- `PatternData.h:24` `SEQ_DEFAULT_NOTE`: pattern default value, not inherently
  sequencer-owned. Prefer `PAT_DEFAULT_NOTE`, unless the team wants the default
  note constant to remain a transport/playback convention.
- `PatternData.h:29-30` `SEQ_NEXT_RANDOM`, `SEQ_NEXT_RANDOM_PREV`: stored
  `PatternSetting.nextPattern` enum values but resolved by sequencer playback.
  Special review. Either keep with comments because they drive sequencer pattern
  transition policy, or rename to `PAT_NEXT_RANDOM*`.
- `PatternData.h:63-66` fields `seq_subStepPattern`, `seq_mainSteps`,
  `seq_patternSettings`, `seq_patternLengthRotate`: now PatternData-owned and
  should be renamed to `pat_subStepPattern`, `pat_mainSteps`,
  `pat_patternSettings`, `pat_patternLengthRotate`. This is a source-only rename
  with no binary layout effect.
- `PatternData.h:71-74` the same `seq_*` field names in `TempPattern`: same
  rename recommendation.
- `PatternData.h:88-89` macros `seq_patternSet` and `seq_tmpPattern`: transition
  aliases. These intentionally relate back to remaining sequencer/filesystem
  call sites, but should be removed once all direct users are converted.
- `PatternData.c:418-419` `seq_activePattern`, `seq_isRunning()`, and
  `seq_offsetTrackStepIndexForRotation()`: valid sequencer-runtime references.
  PatternData owns stored rotation, sequencer owns live step index.
- `PatternData.c:445` `seq_setShuffle()`: valid sequencer-runtime reference for
  now. PatternData stores the UI-facing shuffle value, sequencer consumes the
  audible coefficient.
- `PatternData.c:548` `seq_selectedStep`: compatibility state. Special review:
  if still needed, it relates back to legacy sequencer/UI state; otherwise move
  selected-step ownership fully to PatternData/Menu and remove the extern.

## PatternData.c Audit

These functions already have the correct `pat_*` prefix and are already in the
target file. The future work here is mostly header-comment expansion and naming
cleanup inside the storage structs.

### Storage Globals

- `pat_patternSet` (`PatternData.c:32`)
  - Owns the full pattern data array. Sequencer reads it during playback,
    PatternData mutates it, filesystem serializes it through accessors.
  - Special risk: binary file layout is visible through serializers.

- `pat_tmpPattern` (`PatternData.c:41`)
  - Staging buffer for active-pattern load while sequencer is running.
  - Sequencer commits it at a pattern boundary today.
  - Move target: commit logic should become a `pat_*` function so sequencer no
    longer memcpy's storage fields directly.

- `pat_armedAutomationStep`, `pat_armedAutomationTrack` (`PatternData.c:49-50`)
  - Stores the long-press automation target selected by `buttonHandler.c`.
  - Connected to `pat_armAutomationStep()` and `pat_recordArmedAutomation()`.

- `pat_activeAutomationTrack` (`PatternData.c:54`)
  - Selects automation lane 1 or 2 for step automation writes.
  - Written from `menu_parseGlobalParam(PAR_AUTOM_TRACK)`.

- `pat_shuffleValue[NUM_PATTERN]` (`PatternData.c:61`)
  - PatternData-facing storage for shuffle UI values.
  - Still mirrors to global sequencer shuffle via `seq_setShuffle()`.

### Validation and Pointers

- `pat_resetStep(Step *step)` (`PatternData.c:63`)
  - Static reset helper for one sub-step default state.
  - Duplicates `seq_resetNote()` in `sequencer.c`.
  - Recommendation: expose or add a second public helper for erase/record
    operations, then delete `seq_resetNote()`.

- `pat_trackValid()`, `pat_patternValid()`, `pat_stepValid()`
  (`PatternData.c:79`, `:84`, `:89`)
  - Bounds helpers used by PatternData, LED, button, and filesystem paths.
  - Header comments should spell out that invalid returns must stop writes.

- `pat_stepPtr()` (`PatternData.c:94`)
  - Bounded pointer accessor for a `Step`.
  - Handles `PATTERNDATA_STAGING_PATTERN` for active-load staging.
  - Connected to filesystem serializers and any owner-level storage code.
  - Risk: returns mutable live storage; not for arbitrary UI code.

- `pat_mainStepsPtr()` (`PatternData.c:111`)
  - Bounded pointer accessor for the 16-bit main-step mask.
  - Used by filesystem and PatternData raw writers.

- `pat_patternSettingPtr()` (`PatternData.c:124`)
  - Bounded pointer accessor for per-pattern change settings.
  - Used by filesystem and pattern-settings menu sync.

- `pat_lengthRotatePtr()` (`PatternData.c:136`)
  - Bounded pointer accessor for per-track length/rotation.
  - Used by filesystem and Euklid generation.

### Lifecycle and Step Editing

- `pat_init()` (`PatternData.c:149`)
  - Initializes shuffle backing and clears all pattern slots.
  - Called by `seq_init()` because sequencer startup is currently where pattern
    storage becomes usable.
  - Later Scene init can own this call.

- `pat_isStepActive()` (`PatternData.c:167`)
  - Reads the high active bit of a sub-step volume byte.
  - Used by sequencer playback wrappers and LED refresh.

- `pat_isMainStepActive()` (`PatternData.c:175`)
  - Reads one bit in a track's 16-bit main-step mask.
  - Used by sequencer playback wrappers, LEDs, and button feedback.

- `pat_setMainStep()` (`PatternData.c:186`)
  - Sets or clears one main-step bit.
  - Used by sequencer live recording and live erase, and by PatternData callers.
  - Does not change sub-step contents.

- `pat_setMainStepsRaw()` (`PatternData.c:204`)
  - Writes the full 16-bit main-step mask.
  - Used by Euklid generation and file loading.

- `pat_toggleStep()` (`PatternData.c:215`)
  - Toggles the sub-step active bit while preserving velocity.
  - Called by `buttonHandler` when editing sub-steps.

- `pat_toggleMainStep()` (`PatternData.c:228`)
  - Toggles one main-step bit.
  - Called by `buttonHandler` main-step gestures.

- `pat_setStepNote()` (`PatternData.c:241`)
  - Writes selected step note and mirrors `PAR_STEP_NOTE`.
  - Called from `menu_parseGlobalParam(PAR_STEP_NOTE)`.

- `pat_setStepVolume()` (`PatternData.c:252`)
  - Writes lower 7 velocity bits while preserving active bit; mirrors
    `PAR_STEP_VOLUME`.

- `pat_setStepProbability()` (`PatternData.c:264`)
  - Writes step probability and mirrors `PAR_STEP_PROB`.
  - Sequencer playback reads probability before triggering.

- `pat_setStepAutomationDestination()` (`PatternData.c:275`)
  - Writes `param1Nr` or `param2Nr`, including the legacy destination packing
    rule for parameters below 128.
  - Called from menu P1/P2 destination edits.

- `pat_setStepAutomationValue()` (`PatternData.c:300`)
  - Writes `param1Val` or `param2Val`.
  - Called from menu P1/P2 value edits.

### Pattern/Track Settings

- `pat_setPatternChangeBar()` (`PatternData.c:316`)
  - Writes `PatternSetting.changeBar` and mirrors `PAR_PATTERN_BEAT`.
  - Sequencer later reads this to decide automatic pattern changes.

- `pat_setPatternNext()` (`PatternData.c:338`)
  - Writes `PatternSetting.nextPattern` and mirrors `PAR_PATTERN_NEXT`.
  - Not the same as `seq_setNextPattern()`, which is immediate playback state.

- `pat_getPatternChangeBar()`, `pat_getPatternNext()`
  (`PatternData.c:357`, `:363`)
  - Simple accessors for pattern-settings display or future sequencer reads.

- `pat_setTrackLength()` (`PatternData.c:369`)
  - Stores length with legacy `0 == 16` encoding and mirrors
    `PAR_TRACK_LENGTH`.

- `pat_getTrackLength()` (`PatternData.c:385`)
  - Returns UI form, converting stored `0` to 16.

- `pat_setTrackRotation()` (`PatternData.c:397`)
  - Stores track rotation and mirrors `PAR_TRACK_ROTATION`.
  - If editing the active running pattern, calls
    `seq_offsetTrackStepIndexForRotation()` so the live scheduler preserves
    legacy rotation behavior.
  - Valid sequencer dependency; keep documented.

- `pat_getTrackRotation()` (`PatternData.c:424`)
  - Returns stored rotation.

- `pat_setShuffle()` (`PatternData.c:430`)
  - Stores per-pattern shuffle value and forwards current value to
    `seq_setShuffle()`.
  - Ownership decision: shuffle belongs to PatternData. The call to
    `seq_setShuffle()` is only a runtime playback bridge while sequencer still
    consumes a global audible coefficient.

- `pat_getShuffle()` (`PatternData.c:448`)
  - Returns PatternData's stored shuffle value.

### Clear/Copy

- `pat_clearTrack()` (`PatternData.c:455`)
  - Resets all sub-steps on one track, re-enables the first sub-step of each
    main-step group, clears main steps, and resets length/rotation.
  - Called from init and copy/clear UI.

- `pat_clearPattern()` (`PatternData.c:478`)
  - Clears all tracks in one pattern.

- `pat_clearAutomation()` (`PatternData.c:489`)
  - Clears one automation lane across all sub-steps on one track.

- `pat_copyTrack()` (`PatternData.c:507`)
  - Copies sub-step data, main-step mask, and length/rotation from one track to
    another inside the same pattern.

- `pat_copyPattern()` (`PatternData.c:522`)
  - Copies all track data plus pattern settings from one pattern slot to another.

### Edit State and Automation Recording

- `pat_setSelectedStep()` (`PatternData.c:541`)
  - Writes `seq_selectedStep` and `PAR_ACTIVE_STEP`.
  - Special review: selected-step state is now PatternData/Menu edit context
    but still lives in a sequencer global.

- `pat_setActiveAutomationTrack()`, `pat_getActiveAutomationTrack()`
  (`PatternData.c:552`, `:559`)
  - Store/read the active automation lane.

- `pat_armAutomationStep()` (`PatternData.c:564`)
  - Arms or disarms held-step automation capture.
  - Called by `buttonHandler` long-press logic.

- `pat_recordAutomation()` (`PatternData.c:581`)
  - Writes automation destination/value into the active automation lane for one
    pattern/track/step.
  - Called by sequencer's quantized recording and armed-step capture.

- `pat_recordArmedAutomation()` (`PatternData.c:611`)
  - If a held step is armed, records a destination/value into it.
  - Called by `seq_recordAutomation()` even when live recording is off.

### Menu Synchronization

- `pat_applyStepToMenu()` (`PatternData.c:631`)
  - Reads one `Step` and mirrors editable fields into `parameter_values[]`.
  - Converts stored automation destination encoding back to `modTargets[]`
    indices.
  - Called by menu and button step-selection paths.

- `pat_applyPatternSettingsToMenu()` (`PatternData.c:664`)
  - Mirrors `PatternSetting` fields into menu parameters.

- `pat_applyTrackSettingsToMenu()` (`PatternData.c:684`)
  - Mirrors length, rotation, and shuffle into menu parameters.
  - Usually paired with `led_updatePatternTrack()`.

## Remaining Sequencer.c Pattern-Touching Functions

These functions still touch pattern data or pattern state from `sequencer.c`.
The move target is not always the whole function. Timing, transport, MIDI clock,
and voice triggering should remain sequencer-owned; raw storage access and
mutation should move behind `pat_*` helpers.

- `seq_init()` (`sequencer.c:160`)
  - Initializes automation nodes, runtime step indices, and calls `pat_init()`.
  - Keep as sequencer startup for now; later Scene init may call `pat_init()`.

- `seq_activateTmpPattern()` (`sequencer.c:185`)
  - Commits `pat_tmpPattern` into active pattern storage with four direct
    `memcpy()` operations.
  - Move target: `pat_activateStagedPattern(uint8_t pattern)` or
    `pat_commitStagedPattern(uint8_t pattern)`.
  - Sequencer should keep the boundary-timing decision and call the new helper.

- `seq_setShuffle()` (`sequencer.c:209`)
  - Sets the global playback shuffle coefficient.
  - Shuffle ownership decision: the stored/user-facing/file-facing value belongs
    to PatternData. Keep this function only as the sequencer playback bridge
    while playback consumes a global coefficient.
  - Filesystem load paths should stop calling this directly and should use a
    PatternData API that updates Pattern-owned shuffle storage and then forwards
    the runtime coefficient as needed.

- `seq_offsetTrackStepIndexForRotation()` (`sequencer.c:213`)
  - Adjusts live `seq_stepIndex[]` when PatternData changes rotation on the
    active running pattern.
  - Keep sequencer-owned; it explicitly relates to scheduler runtime state.

- `seq_parseAutomationNodes()` (`sequencer.c:308`)
  - Reads automation fields from a `Step` and updates two `AutomationNode`s.
  - Storage access should be hidden by a PatternData read helper or by passing a
    `Step` returned from `pat_stepPtr()`.
  - Sequencer-owned part: applying automation to playback nodes.

- `seq_triggerVoice()` (`sequencer.c:318`)
  - Reads current step automation and volume from pattern storage, triggers DSP
    voice, and sends MIDI.
  - Keep voice triggering and MIDI output in sequencer. Replace direct
    `seq_patternSet.seq_subStepPattern[...]` reads with `pat_stepPtr()` or a
    narrower `pat_currentStep*` accessor.

- `seq_determineNextPattern()` (`sequencer.c:357`)
  - Reads `PatternSetting` for active pattern and resolves automatic next
    pattern.
  - Candidate split: PatternData exposes settings via `pat_getPatternNext()` and
    `pat_getPatternChangeBar()`; sequencer keeps random resolution and transport
    state. If moved wholesale, rename `pat_determineNextPattern()` and document
    RNG dependency.

- `seq_nextStep()` (`sequencer.c:366`)
  - Core scheduler step walk. Reads track length, main-step bits, sub-step
    active/probability/note/volume, and triggers live erase.
  - Keep scheduler and trigger timing here. Replace direct `seq_patternSet`
    reads with `pat_*` read helpers:
    - track length read at `:381` and `:481`
    - probability/note/volume reads at `:532-535`
    - roll note read at `:554`
  - Live erase call should delegate to PatternData helper after `seq_eraseStepAndSubSteps()`
    is moved.

- `seq_triggerNextMasterStep()` (`sequencer.c:658`)
  - External-clock positioning. Reads track length for active pattern.
  - Keep sequencer-owned because it manipulates `seq_stepIndex[]` and
    `seq_lastMasterStep[]`; use `pat_getTrackLength()` or a storage-form length
    accessor rather than direct field access.

- `seq_setRunning()` (`sequencer.c:812`)
  - On stop, resets transient rotations through `pat_setTrackRotation()`, clears
    transport counters, MIDI, and trigger state.
  - Keep sequencer-owned. Already uses PatternData for storage mutation.

- `seq_intIsStepActive()` (`sequencer.c:867`)
  - Local wrapper around `pat_isStepActive()`.
  - Delete or inline once direct call churn is acceptable. It does not need a
    `seq_*` wrapper.

- `seq_intIsMainStepActive()` (`sequencer.c:881`)
  - Local wrapper around `pat_isMainStepActive()`.
  - Same recommendation as `seq_intIsStepActive()`.

- `seq_quantize()` (`sequencer.c:1021`)
  - Converts current scheduler step to quantized record step.
  - Keep sequencer-owned. It uses `NUM_STEPS` but does not access pattern
    storage.

- `seq_recordAutomation()` (`sequencer.c:1060`)
  - Owns recording gate and quantization, then calls `pat_recordAutomation()` and
    `pat_recordArmedAutomation()`.
  - Keep as sequencer-owned capture policy unless recording ownership moves
    later. Storage mutation is already PatternData-owned.

- `seq_addNote()` (`sequencer.c:1092`)
  - Records note/velocity/probability/active bit into a quantized step and sets
    the parent main step.
  - Move storage mutation into PatternData:
    - clear first sub-step active bit when activating a later sub-step
    - write note, volume, probability, active bit
    - set main-step bit
  - Suggested helper:
    `pat_recordNote(uint8_t pattern, uint8_t track, uint8_t step, uint8_t vel,
    uint8_t note)`.
  - Sequencer should keep quantization, target-pattern decision near pattern
    boundary, and LED dirty-state emission.

- `seq_eraseStepAndSubSteps()` (`sequencer.c:1171`)
  - Clears one main step and resets its eight sub-steps, then re-enables the
    first sub-step.
  - Move wholesale into PatternData as
    `pat_eraseMainStepSubSteps(uint8_t pattern, uint8_t track,
    uint8_t mainStep)`.
  - Sequencer keeps the playback-time condition that decides when erase occurs.

- `seq_resetNote()` (`sequencer.c:1216`)
  - Duplicates PatternData's reset semantics.
  - Delete after `seq_eraseStepAndSubSteps()` moves to PatternData or replace
    with a public `pat_resetStepDefaults()` helper.

- `seq_setStepIndexToStart()` (`sequencer.c:1327`)
  - Reads track rotation/length and sets `seq_stepIndex[]` plus
    `seq_lastMasterStep[]`.
  - Keep sequencer-owned because it is scheduler runtime state. Replace direct
    field reads with `pat_getTrackRotation()` and a storage-form/effective length
    accessor.

## Filesystem Pattern Serialization

`filesystem.c` is not a move target for 1.3, but it is a major consumer of the
PatternData boundary.

- `filesystem_patternStepPtr()` (`filesystem.c:484`)
  - Wraps `pat_stepPtr()` and redirects active running pattern loads to staging.
  - Correct boundary: filesystem serializes through PatternData.

- `filesystem_patternMainPtr()` (`filesystem.c:509`)
  - Same staging-aware wrapper for main-step masks.

- `filesystem_patternSettingPtr()` (`filesystem.c:523`)
  - Same staging-aware wrapper for `PatternSetting`.

- `filesystem_patternLengthPtr()` (`filesystem.c:537`)
  - Same staging-aware wrapper for `LengthRotate`.

- `filesystem_packStep()` / `filesystem_unpackStep()` (`filesystem.c:551`,
  `:562`)
  - Encodes/decodes the fixed 7-byte step layout.
  - Layout-sensitive; update only with file-format changes, not with this move.

- `filesystem_savePattern_tick()` (`filesystem.c:799`)
  - Streams name, all steps, main steps, settings, shuffle, and lengths.
  - Uses PatternData pointer wrappers for pattern fields.
  - Special review: saves shuffle from `parameter_values[PAR_SHUFFLE]`, not
    `pat_getShuffle(pattern)`.

- `filesystem_loadPattern_tick()` (`filesystem.c:992`)
  - Loads the same layout, stages active pattern when sequencer is running, and
    arms `seq_newPatternAvailable`/`seq_armActivePatternReload()`.
  - Special review: loads shuffle through `seq_setShuffle()` directly instead of
    `pat_setShuffle()`, so PatternData's `pat_shuffleValue[]` may not mirror file
    loads.

- `filesystem_saveContainer_tick()` (`filesystem.c:1194`)
  - Saves `.all`/performance container pattern payload using the same PatternData
    wrappers.

- `filesystem_loadContainer_tick()` (`filesystem.c:1457`)
  - Loads container pattern payload; active running pattern loads go through the
    same staging/commit path.
  - Same shuffle special review as `.pat` load.

## Pattern Generator Modules

Explicit 1.3 boundary: `EuklidGenerator.c`, `SomData.c`, and `SomGenerator.c`
are not move targets. Do not move any of their functions into `PatternData.c`,
and do not rename their functions. These modules are already under
`Core/Scene/Pattern/` and own their generator/data concerns separately from
generic PatternData storage. Update them only if necessary to call newly added
`pat_*` helpers after `sequencer.c` storage functions move.

- `euklid_init()` (`EuklidGenerator.c:50`)
  - Initializes generator state arrays, not pattern storage.
  - Already under `Core/Scene/Pattern`.

- `euklid_generate()` (`EuklidGenerator.c:156`)
  - Runs generator and transfers resulting main-step mask to PatternData.

- `euklid_setLength()` (`EuklidGenerator.c:192`)
  - Stores generator length only; menu later regenerates.

- `euklid_setSteps()` (`EuklidGenerator.c:210`)
  - Stores generator step count and regenerates pattern main-step mask via
    `euklid_generate()`.

- `euklid_setRotation()` (`EuklidGenerator.c:236`)
  - Stores generator rotation and regenerates pattern main-step mask.
  - Not the same as PatternData track rotation.

- `euklid_transferPattern()` (`EuklidGenerator.c:266`)
  - Writes generated 16-bit mask with `pat_setMainStepsRaw()` and writes length
    through `pat_lengthRotatePtr()`.
  - Special review: direct `LengthRotate` pointer write is currently owner-level
    code, but a `pat_setTrackLengthStorage()` or `pat_setTrackLengthFromGenerator()`
    helper would reduce direct field mutation.

- `som_tick()` (`SomGenerator.c:94`)
  - Generates live probabilistic triggers and calls `seq_triggerVoice()`.
  - Does not store pattern data; leave as generator/playback behavior.

- `som_setX()`, `som_setY()`, `som_setFlux()`, `som_setFreq()`
  (`SomGenerator.c:147`, `:162`, `:173`, `:185`)
  - Store SOM generator state, not pattern storage. Already in Pattern module.

## UI and MIDI Callers

These are not move targets but explain why the PatternData API exists.

- `copyClear_clearTrackAutom()` (`copyClearTools.c:24`)
  - Calls `pat_clearAutomation()`.

- `copyClear_clearCurrentPattern()` (`copyClearTools.c:49`)
  - Calls `pat_clearPattern()`.

- `copyClear_clearCurrentTrack()` (`copyClearTools.c:118`)
  - Calls `pat_clearTrack()`.

- `copyClear_copyTrack()` (`copyClearTools.c:137`)
  - Calls `pat_copyTrack()`.

- `copyClear_copyPattern()` (`copyClearTools.c:165`)
  - Calls `pat_copyPattern()`.

- Menu parameter dispatch (`menu.c:2471-2707`)
  - Track length, shuffle, automation lane, step automation, Euklid, pattern
    settings, active step, probability, note, and volume all call PatternData or
    Pattern generator APIs directly.
  - Quantization, BPM, external sync, and roll rate correctly remain sequencer
    runtime settings.

- `buttonHandler` direct PatternData calls (`buttonHandler.c:180-203`,
  `:254-355`, `:385-491`, `:640-699`, `:773-852`)
  - Handles UI gestures, then calls PatternData for storage mutation or menu
    sync. Correct ownership split: buttonHandler owns gestures/LED blink state,
    PatternData owns pattern edits.

- `ledHandler` PatternData reads (`ledHandler.c:444-538`, `:548-588`)
  - Reads PatternData only to display recorded steps, sub-steps, and visible
    tracks. Correct boundary: LED code does not mutate pattern storage.

- `preset_applySoundParameter()` (`presetManager.c:280`)
  - Applies sound params and optionally calls `seq_recordAutomation()`.
  - Correct for now because sequencer owns recording gate/quantization, but the
    final storage mutation is PatternData.

- `midiParser_noteOn()` (`MidiParser.c:1181`)
  - Calls `seq_addNote()` for MIDI recording.
  - After `seq_addNote()` storage writes move, MIDI caller should remain pointed
    at sequencer recording capture, not PatternData directly.

- Global-channel MIDI CC recording (`MidiParser.c:1529`)
  - Calls `seq_recordAutomation()`.
  - Correct for now for same recording-gate reason.

## Recommended Refactor Sequence

1. Rename PatternData storage fields away from `seq_*`.
   - `seq_subStepPattern` -> `pat_subStepPattern`
   - `seq_mainSteps` -> `pat_mainSteps`
   - `seq_patternSettings` -> `pat_patternSettings`
   - `seq_patternLengthRotate` -> `pat_patternLengthRotate`
   - Apply to both `PatternSet` and `TempPattern`.

2. Remove transition macros.
   - Replace all remaining `seq_patternSet` and `seq_tmpPattern` references with
     PatternData helpers or direct `pat_patternSet` references only inside
     `PatternData.c`.
   - Delete `#define seq_patternSet pat_patternSet` and
     `#define seq_tmpPattern pat_tmpPattern`.

3. Add missing PatternData helpers.
   - `pat_commitStagedPattern(pattern)`
   - `pat_effectiveTrackLength(pattern, track)` or storage/effective length pair
   - `pat_recordNote(pattern, track, step, vel, note)`
   - `pat_eraseMainStepSubSteps(pattern, track, mainStep)`
   - Optional: `pat_stepAutomationToNodes()` only if PatternData should know
     `AutomationNode`; otherwise keep automation-node application in sequencer
     and just fetch the `Step` through `pat_stepPtr()`.

4. Convert `sequencer.c` direct storage accesses.
   - Keep timing, transport, MIDI, voice trigger, quantization, and LED dirty
     signaling in `sequencer.c`.
   - Move direct `Step` mutation/reset/copy commit into PatternData.
   - Replace direct length/rotation/settings reads with `pat_*` accessors.

5. Normalize shuffle loading.
   - Shuffle is Pattern-owned.
   - Change filesystem load paths from direct `seq_setShuffle()` to a PatternData
     API.
   - That PatternData API should update Pattern-owned shuffle storage and bridge
     to sequencer runtime playback only as an implementation detail.

6. Expand `PatternData.h` comments.
   - Copy the high-value comments from `PatternData.c` into the header near the
     public prototypes.
   - Public comments should identify caller classes, side effects, ISR safety,
     and file-format risks.

7. Preserve generator module ownership.
   - Do not move or rename `EuklidGenerator.c`, `SomData.c`, or
     `SomGenerator.c` functions.
   - Limit those files to call-site updates required by new/renamed `pat_*`
     helpers.

8. Add detailed comments for every touched function.
   - In every touched `.c` and `.h` file, comments must describe what changed,
     why it exists, inputs, outputs, side effects, common callers/accessors,
     clients, and confederate functions/modules.
   - This applies to functions moved into `PatternData.c`, new `pat_*`
     declarations in `PatternData.h`, and sequencer-side functions left behind
     after storage mutation is extracted.

## Build/Verification Targets For The Later Refactor

- `make`
- `make img`
- Grep gates:
  - `rg -n "\\bseq_" Core/Scene/Pattern/PatternData.c Core/Scene/Pattern/PatternData.h`
  - `rg -n "seq_patternSet|seq_tmpPattern" Core`
  - `rg -n "seq_subStepPattern|seq_mainSteps|seq_patternSettings|seq_patternLengthRotate" Core`

Expected final state after 1.3 implementation:

- `PatternData.c/.h` have no `seq` naming except documented sequencer-runtime
  hooks.
- `sequencer.c` no longer writes `Step`, `PatternSetting`, `LengthRotate`, or
  PatternSet fields directly.
- Transport functions still live in `sequencer.c`.
- Pattern storage/edit/serialization helpers live behind `pat_*` APIs.
- Euklid and SOM generator/data modules remain in place with their current
  function names.
- Touched `.c` and `.h` files contain detailed ownership/caller/input/output
  comments for every moved or changed function.
