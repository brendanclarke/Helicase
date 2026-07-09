# Instrument Parameter Refactor - Code Editing Plan

## Implementation log

- **2026-07-09 - in progress:** implementation started from the ownership and
  descriptor foundation. Changes are being applied in dependency order:
  registry/Scene types, Pattern ownership, storage/apply/Morph, UI/MIDI
  affiliates, then source relocation and verification. Contract comments are
  kept beside each changed code path as required.

## 1. Purpose and source-of-truth decisions

This plan implements `KIT_INSTRUMENT_SPEC_FOLLOWUP_AUDIT.md` against the
firmware that exists now. It is not a restatement of older planning documents.
The affected source was traced through `ParameterArray`, Preset, Menu,
`Cc2Text`, MIDI CC/NRPN handling, modulation and automation nodes, PatternData,
Sequencer, filesystem serializers, the four current DSP instruments, and the
Makefile.

The implementation must preserve these decisions:

- Allocate `scene_t scenes[1]` now. All owner-level APIs take a Scene index so
  the allocation can later become 17 identical resident Scenes without another
  ownership rewrite.
- Each Scene owns Scene settings, one `PatternSet`, and one `kit_t`.
- `kit_t` owns an extensible `kit_settings_t` and six instrument slots.
- Each instrument slot owns exactly these three 64-byte images:
  `instrument_parameters[]`, `morph_instrument_parameters[]`, and
  `morph_interpolation[]`.
- `morph_interpolation[]` is retained independently for every resident Scene
  but is never serialized.
- `PatternSet` contains one 128-step pattern. The redundant `NUM_PATTERN`
  dimensions and `TempPattern` are removed.
- Kit settings initially contain only the six audio routings.
- Scene settings contain Morph amount, voice-wide decimation, and seven-track
  MIDI channel/note values. System MIDI input-channel configuration remains
  global.
- The four supplemental instrument fields are velocity target, velocity
  amount, LFO target voice, and LFO target parameter. They sit beside, not
  inside, the three images and are not morphed.
- Each voice slot owns 64 stable local IDs. A flat instrument ID is
  `slot * 64 + local_param`; IDs 384..511 remain reserved for FX/general use.
- Named instrument-file keys remain the physical SD format. Files do not gain
  padding rows or a `slot=` field.
- Descriptors are the canonical source for file-key lookup, menu metadata,
  default/range data, and modulation/automation validity.
- InstrumentManager is a registry and lookup layer. Preset remains responsible
  for applying values to live DSP objects.

## 2. Current-code findings that constrain the edit

These are facts from the live source that the implementation must account for.

1. `parameter_values[]` and `parameters2[]` are defined in `menu.c`, although
   their sound values are consumed by Preset and filesystem code.
2. `ParameterArray.c` is not stored parameter state. It is a 209-entry map from
   legacy `PAR_*` IDs to pointers inside `voiceArray`, `snareVoice`,
   `cymbalVoice`, `hatVoice`, mixer arrays, and MIDI note arrays.
3. `preset_applySoundParameter()` writes `parameter_values[]`, repacks the ID as
   MIDI CC/CC2, and calls `midiParser_ccHandler()`. The MIDI handler contains
   the actual byte-to-DSP conversions.
4. `modulationNode.c` directly depends on `ParameterArray` pointer/type records
   for saving, modulating, and restoring native DSP values every audio block.
   This hot path cannot be replaced with repeated descriptor scans.
5. `automationNode.c` also repacks destinations as MIDI CC/CC2. It restores from
   `midiParser_originalCcValues[]`, not from Scene state.
6. `Step.param1Nr` and `Step.param2Nr` are `uint8_t`, and the pattern file stores
   each as one byte. They cannot represent the new 0..511 parameter space.
7. `NUM_PATTERN` is already 1, but `PatternSet` still has a redundant pattern
   dimension. Filesystem still streams an eight-pattern legacy file and
   discards slots 1..7. `TempPattern` is still used for active-load staging.
8. `LengthRotate` currently owns MIDI channel/note even though those values are
   now Scene settings. Sequencer and MidiParser read them through PatternData.
9. `storageTypes.c` has six key-to-legacy-`PAR_*` tables. The parser writes
   directly into the two flat arrays.
10. The instrument files currently store legacy mod-target table indices in
    `velo_mod_dest` and `lfo_target_param`. A canonical flat target can exceed
    255, so these two fields need targeted integer parsing and conversion.
11. `menuPages.h` hardcodes seven voice pages with legacy parameter IDs and
    names. `parameter_dtypes[]`, `valueNames[]`, `modTargets[]`, and the page
    tables are independent sources that currently have to agree manually.
12. The packed Menu dtype byte has no free high-nibble list IDs. The existing
    runtime sample-waveform name path proves that a tagged static-list/runtime
    resolver is required.
13. Root Kit loading is directory-based, but Kit saving and legacy
    Morph/All/Performance paths still use flat binary serializers.
14. There is no Scene owner type in `Core/Scene/` today. PatternData and Preset
    each own globals independently.
15. The build explicitly lists every C source and has an `-Ofast` pattern rule
    only for `Core/DSPAudio/*.c`; moving instrument DSP files requires matching
    build-rule changes, not just include paths.

## 3. Target data types and ownership

### 3.1 Add `Core/Scene/SceneData.h` and `SceneData.c`

Define:

```c
#define SCENE_COUNT 1u
#define KIT_INSTRUMENT_SLOTS 6u
#define INSTRUMENT_PARAM_COUNT 64u
#define INSTRUMENT_ID_COUNT 512u
#define INSTRUMENT_VOICE_ID_COUNT 384u

typedef uint16_t instrument_param_id_t;

typedef struct {
    uint8_t instrument_parameters[INSTRUMENT_PARAM_COUNT];
    uint8_t morph_instrument_parameters[INSTRUMENT_PARAM_COUNT];
    uint8_t morph_interpolation[INSTRUMENT_PARAM_COUNT];
} instrument_parameter_images_t;

typedef struct {
    instrument_param_id_t velocity_target_param;
    uint8_t velocity_amount;
    uint8_t lfo_target_voice;
    instrument_param_id_t lfo_target_param;
} instrument_supplemental_t;

typedef struct {
    instrument_type_t type;
    instrument_parameter_images_t parameter_images;
    instrument_supplemental_t supplemental;
} kit_instrument_slot_t;

typedef struct {
    uint8_t audio_out[KIT_INSTRUMENT_SLOTS];
} kit_settings_t;

typedef struct {
    kit_settings_t settings;
    kit_instrument_slot_t instruments[KIT_INSTRUMENT_SLOTS];
} kit_t;

typedef struct {
    uint8_t morph_amount;
    uint8_t voice_decimation_all;
    uint8_t midi_channel[NUM_TRACKS];
    uint8_t midi_note[NUM_TRACKS];
} scene_settings_t;

typedef struct {
    scene_settings_t settings;
    PatternSet pattern;
    kit_t kit;
} scene_t;
```

`SceneData.c` defines the sole storage instance, `scene_t scenes[SCENE_COUNT]`,
and the selected Scene index. Do not define parameter images in Menu,
ParameterArray, Preset, filesystem, or InstrumentManager.

Add these functions:

- `void scene_initAll(void)`
  - Input: none.
  - Processing: clear all Scene records; initialize per-track MIDI channels to
    the current safe display defaults `track + 1`; initialize notes to zero,
    Morph to zero, and voice-wide decimation to 127; initialize each
    `PatternSet` through the PatternData owner; initialize kit slots through
    descriptor defaults once InstrumentManager is ready.
  - Output: all `scenes[]` records are valid and independent.
  - Why new: `menu_init()` currently clears unrelated flat arrays and
    `pat_init()` initializes a separate global. Neither function owns a Scene
    aggregate or can initialize future inactive Scenes.
  - Clients: `main.c` boot and tests.

- `uint8_t scene_indexValid(uint8_t scene_index)`
  - Input: candidate Scene index.
  - Output: nonzero only below `SCENE_COUNT`.
  - Why new: every asynchronous loader and UI accessor needs one common bounds
    rule before `SCENE_COUNT` becomes 17.
  - Clients: all Scene accessors, Preset, PatternData, filesystem.

- `scene_t *scene_get(uint8_t scene_index)` and
  `const scene_t *scene_getConst(uint8_t scene_index)`
  - Input: Scene index.
  - Output: pointer to the requested record or `NULL`.
  - Processing: bounds check only; no selection side effects.
  - Why new: direct use of the `scenes[]` symbol would spread array ownership
    and make the future 17th-slot rules impossible to enforce centrally.
  - Clients: owner-level Preset, PatternData, and filesystem code. Menu and
    Sequencer should use narrower accessors.

- `uint8_t scene_getActiveIndex(void)` and
  `uint8_t scene_selectActive(uint8_t scene_index)`
  - Input to select: a validated resident Scene index.
  - Processing: reject invalid indices; update the selected index; do not copy
    Scene data.
  - Output: current index, or success/failure for selection.
  - Why new: `seq_activePattern` is currently pretending to be both a data
    selector and sequencer state. The owner selection belongs at Scene level.
  - Clients: Sequencer transition code, Menu viewed-Scene logic, Preset live
    apply, MIDI input.

- `kit_instrument_slot_t *scene_instrumentSlot(uint8_t scene_index,
  uint8_t slot)` and const equivalent.
  - Inputs: Scene index and zero-based kit slot.
  - Output: slot pointer or `NULL`.
  - Why new: repeated nested indexing in storage/Menu/Preset would duplicate
    bounds checks and expose the complete Scene layout to every client.
  - Clients: InstrumentManager initialization, storage parser, Preset, Morph
    engine, Menu.

Do not create general setters for all Scene fields. PatternData, Preset, and
storage retain their domain rules; SceneData supplies ownership and bounded
record access rather than becoming a miscellaneous service layer.

### 3.2 Stable local parameter layout

Add `instrument_local_param_t` values to the shared instrument type header.
Use explicit numeric assignments so descriptor order never defines identity:

| Local ID | Semantic position |
|---:|---|
| 0 | `osc1_wave` |
| 1 | `osc1_pitch_coarse` |
| 2 | `osc1_pitch_fine` |
| 3 | `osc2_wave` |
| 4 | `osc2_pitch_coarse` |
| 5 | `osc2_mod_amount` |
| 6 | `osc3_wave` or Snare `noise_freq` |
| 7 | `osc3_pitch_coarse` or Snare `osc1_noise_mix` |
| 8 | `osc3_mod_amount` |
| 9 | Drum `osc2_mod_type` |
| 12 | `filter_freq` |
| 13 | `filter_reso` |
| 14 | `filter_drive` |
| 15 | `filter_type` |
| 16 | `amp_envelope_attack` |
| 17 | `amp_envelope_decay` or `amp_envelope_decay_closed` |
| 18 | `amp_envelope_decay_open` |
| 19 | `amp_envelope_slope` |
| 20 | `amp_attack_repeat` |
| 21 | `pitch_envelope_decay` |
| 22 | `pitch_envelope_amount` |
| 23 | `pitch_envelope_slope` |
| 24 | `instrument_vol` |
| 25 | `instrument_pan` |
| 26 | `instrument_drive` |
| 27 | `instrument_decimation` |
| 32 | `lfo_rate` |
| 33 | `lfo_amount` |
| 34 | `lfo_wave` |
| 35 | `lfo_retrigger_voice` |
| 36 | `lfo_sync` |
| 37 | `lfo_offset` |
| 40 | `velo_vol_on_off` |
| 41 | supplemental `velo_mod_amount` |
| 42 | supplemental `velo_mod_dest` |
| 43 | supplemental `lfo_target_voice` |
| 44 | supplemental `lfo_target_param` |
| 48 | `transient_vol` |
| 49 | `transient_wave` |
| 50 | `transient_freq` |

Unused IDs remain valid capacity but have no descriptor. Snare uses 6 and 7 for
its noise oscillator fields because it has no oscillator 2/3 set. This preserves
module alignment without forcing unused keys into files.

Add pure ID helpers:

- `instrument_param_id_t instrumentParam_make(uint8_t slot,
  uint8_t local_param)`: validate `slot < 6` and `local_param < 64`; return a
  dedicated invalid sentinel on failure.
- `uint8_t instrumentParam_slot(instrument_param_id_t id)` and
  `instrumentParam_local(...)`: split only voice IDs below 384.
- `uint8_t instrumentParam_isVoiceParameter(instrument_param_id_t id)`.

These are new because legacy `PAR_*` values are grouped by parameter family,
not `slot * 64`, so arithmetic on the old enum cannot implement the new
contract. Clients are Pattern automation, modulation routing, Menu target
display, Preset, and validators.

## 4. Instrument source tree and descriptor registry

### 4.1 Move the four DSP implementations

Move without renaming public DSP symbols:

- `Core/DSPAudio/DrumVoice.c/.h` to
  `Core/DSP/Instruments/Drum/DrumVoice.c/.h`.
- `Core/DSPAudio/Snare.c/.h` to
  `Core/DSP/Instruments/Snare/Snare.c/.h`.
- `Core/DSPAudio/CymbalVoice.c/.h` to
  `Core/DSP/Instruments/Cymbal/CymbalVoice.c/.h`.
- `Core/DSPAudio/HiHat.c/.h` to
  `Core/DSP/Instruments/HiHat/HiHat.c/.h`.

Keep `initDrumVoice`, `Drum_trigger`, `Snare_init`, `Cymbal_init`,
`HiHat_init`, render functions, existing structs, and globals unchanged in the
move commit. This is a source-layout change only. Update includes in `main.c`,
`mixer.c`, `lfo.c`, `modulationNode.c`, Sequencer, MidiVoiceControl, MidiParser,
and Preset. The existing functions can be retained because their DSP ownership
and calling conventions are not what this refactor changes.

Update `Makefile`:

- Add include paths for `Core/DSP/Instruments` and all four type directories.
- Replace the four old `DSP_SRCS` paths.
- Add `InstrumentManager.c`, four parameter-table C files, `SceneData.c`, and
  `presetMorphEngine.c`.
- Add an `-Ofast` object rule for
  `Core/DSP/Instruments/%/*.c` only for the moved voice render files. Compile
  descriptor/manager files with normal `-O2`; they are control code and do not
  benefit from fast-math assumptions.

### 4.2 Add shared descriptor types

Add `Core/DSP/Instruments/InstrumentManager.h` with:

- `instrument_type_t` values `INSTRUMENT_TYPE_DRM`, `SNR`, `CYM`, `HAT`, and
  `UNKNOWN`.
- `instrument_value_owner_t`: `PARAMETER_IMAGE`,
  `SUPPLEMENTAL_VELOCITY_TARGET`, `SUPPLEMENTAL_VELOCITY_AMOUNT`,
  `SUPPLEMENTAL_LFO_TARGET_VOICE`, and
  `SUPPLEMENTAL_LFO_TARGET_PARAM`.
- `instrument_dtype_t` for numeric, signed-center, on/off, note, static list,
  runtime list, LFO target, velocity target, and other currently used display
  behaviors.
- A tagged `dtype_source_t`. Static-list form contains table pointer and count;
  runtime form contains count and format callbacks. Do not use `const void *`.
- `ParamDescriptor` with the audit fields plus `local_param`,
  `value_owner`, `menu_page`, and `menu_position`.

`menu_page`/`menu_position` are necessary because the live voice UI has eight
subpages and sparse cells. Keeping this placement in `menuPages.h` would leave a
fourth instrument-specific table that could drift from descriptors. Audio
routing remains a synthetic kit-setting cell inserted at the established final
voice subpage position because it is not an instrument descriptor.

Define callback contracts:

```c
typedef uint8_t (*dtype_count_fn)(uint8_t scene_index, uint8_t slot);
typedef uint8_t (*dtype_format_fn)(uint8_t scene_index, uint8_t slot,
                                   uint8_t value, char out[3]);
```

Callbacks return bounded three-character values and do not write LCD state.
Menu owns rendering. They are new because `menu_formatSampleShortName()` is
hardcoded to the current global sample list and cannot represent arbitrary
future per-type runtime lists.

### 4.3 Add the per-type parameter modules

Create:

- `DrumParameters.c/.h`
- `SnareParameters.c/.h`
- `CymbalParameters.c/.h`
- `HiHatParameters.c/.h`

Each exports a `const ParamDescriptor[]` and count. Populate exactly the
35/34/35/35 canonical keys in Section 3.2b of the audit, using the local IDs in
Section 3.2 above.

For each descriptor:

- Preserve the current Menu `Name` short/category/long labels.
- Translate the current `parameter_dtypes[]` entry into `instrument_dtype_t`,
  min/max, and an optional tagged list source.
- Use the current `Cc2Text.c` membership as the initial
  `is_modulatable` set.
- Use that same membership as the initial `is_stepAutomatable` set because the
  current automation UI selects from `modTargets[]`. Keep the flags separate so
  later instruments can differ without a format change.
- Mark local IDs 41..44 with their supplemental owners. All other descriptors
  use `PARAMETER_IMAGE`.
- Set the three image-backed endpoint defaults in byte-domain values. Derive
  these from the current DSP initialization path and its existing byte-to-native
  conversion, then make a test assert that applying descriptor defaults
  produces the same native initialization. Do not infer defaults from a random
  converted kit.
- Assign menu positions matching the current seven voice-page layouts,
  including the different open/closed hat decay cell behavior. Voice page 7
  remains an alias of slot 6 but selects `amp_envelope_decay_open` where voice
  page 6 selects `amp_envelope_decay_closed`.

Do not add DSP pointers to descriptors. A descriptor is immutable metadata and
must be usable for all Scenes; a pointer would bind it to one live DSP instance
and recreate `ParameterArray`.

### 4.4 Add `InstrumentManager.c`

Define one registry entry per type containing its text token, extension,
display/type name, descriptor table, and count.

Add:

- `const instrument_registry_entry_t *instrumentManager_registryEntry(
  instrument_type_t type)`
  - Input: type enum.
  - Output: immutable entry or `NULL`.
  - Why new: storage, Menu, and Preset currently own separate type switches.
  - Clients: all descriptor/type lookup functions.

- `instrument_type_t instrumentManager_typeFromText(const char *text)` and
  `uint8_t instrumentManager_filenameMatchesType(const char *filename,
  instrument_type_t type)`
  - Inputs/outputs preserve current storage behavior, including
    case-insensitive extensions.
  - Why new instead of retaining storage versions: type identity is a DSP
    registry concern and must also serve Menu and Scene initialization.
  - Clients: `storageTypes.c`, kit scanner/loader.

- `const ParamDescriptor *instrumentManager_descriptor(
  instrument_type_t type, uint8_t local_param)`
  - Processing: scan the small compact type table for explicit `local_param`.
  - Output: descriptor or `NULL`.
  - Why new: table position is not a stable ID.
  - Clients: Preset, Menu, automation/modulation validation.

- `const ParamDescriptor *instrumentManager_descriptorByKey(
  instrument_type_t type, const char *file_key)`
  - Processing: exact canonical-key comparison.
  - Output: descriptor or `NULL`.
  - Why new: replaces all six `storageTypes.c` key maps.
  - Clients: instrument parser.

- `const ParamDescriptor *instrumentManager_menuDescriptor(
  instrument_type_t type, uint8_t page, uint8_t position)`
  - Output: descriptor occupying the requested cell or `NULL`.
  - Why new: replaces instrument portions of `menuPages.h`.
  - Clients: Menu page resolver.

- `uint8_t instrumentManager_targetValid(uint8_t scene_index,
  instrument_param_id_t id, instrument_target_use_t use)`
  - Processing: validate the Scene index, split the ID, read the destination slot's loaded type; find its
    descriptor; check `is_modulatable` or `is_stepAutomatable`.
  - Output: boolean.
  - Why new: bounds checking alone cannot distinguish unused local IDs or the
    two independent validity sets.
  - Clients: kit/instrument swap invalidation, Pattern import, modulation and
    automation destination setters.

Using a Scene index here avoids a header cycle: `SceneData.h` needs the
instrument type definitions, while `InstrumentManager.c` alone needs the full
Scene layout for target validation.

- `void instrumentManager_resetSlot(kit_instrument_slot_t *slot,
  instrument_type_t type)`
  - Processing: clear the slot, assign type, place descriptor defaults into
    `instrument_parameters[]`, copy those values into
    `morph_instrument_parameters[]` and `morph_interpolation[]`, and initialize
    supplemental targets to the invalid/off sentinel.
  - Output: deterministic slot state with no stale values from the previous
    instrument.
  - Why new: `memset()` cannot apply nonzero defaults and current filesystem
    parsing leaves absent keys stale.
  - Clients: Scene initialization and instrument load/swap.

InstrumentManager must not write DSP objects, schedule Morph work, own Scene
storage, or call filesystem APIs.

## 5. PatternData becomes a one-pattern Scene member

### 5.1 Change `PatternData.h`

- Remove `NUM_PATTERN`.
- Remove the first dimension from all `PatternSet` fields:
  `Step steps[NUM_TRACKS][NUM_STEPS]`, main-step masks per track, one
  `PatternSetting`, and track settings per track.
- Remove `TempPattern`, `pat_tmpPattern`, `pat_patternSet`,
  `PATTERNDATA_STAGING_PATTERN`, and `pat_commitStagedPattern()`.
- Remove `midiChannel` and `midiNote` from `LengthRotate`; retain length,
  rotation, scale, and shuffle.
- Widen `Step.param1Nr` and `Step.param2Nr` to
  `instrument_param_id_t`/`uint16_t`.
- Replace `NO_AUTOMATION == 0xff` for this data path with a typed
  `INSTRUMENT_PARAM_INVALID == 0xffff`. Keep legacy MIDI constants private to
  legacy import code.

Rename `pat_patternValid()` to Scene validation or remove it. Every PatternData
storage API changes its first `pattern` argument to `scene_index`, while
retaining track/step arguments. Examples:

```c
Step *pat_stepPtr(uint8_t scene_index, uint8_t track, uint8_t step);
uint8_t pat_readStep(uint8_t scene_index, uint8_t track, uint8_t step,
                     Step *out);
void pat_clearPattern(uint8_t scene_index);
```

This is a semantic signature change even where the C type remains `uint8_t`.
Update names and comments at every declaration/call so callers do not continue
passing an obsolete pattern number accidentally.

### 5.2 Change `PatternData.c`

All accessors obtain `scene_get(scene_index)->pattern`; no PatternData storage
global remains.

Change `pat_init()` to `pat_initScene(uint8_t scene_index)`:

- Input: Scene to initialize.
- Processing: initialize one `PatternSetting`, clear seven tracks, and reset
  Pattern edit context where appropriate.
- Output: the Scene's `PatternSet`.
- Why modified rather than adding a parallel initializer: the existing function
  already owns pattern defaults; only its storage target and loop dimension are
  wrong.
- Clients: `scene_initAll()`.

Move MIDI access to Scene settings:

- `scene_setTrackMidiChannel(scene_index, track, channel)` and corresponding
  getter belong in SceneData, not PatternData.
- Same for MIDI note.
- Delete `pat_set/getTrackMidiChannel/Note`.
- Update `pat_applyTrackSettingsToMenu()` to fetch those display values from
  SceneData while fetching length/scale/shuffle from PatternData.

Remove `pat_copyPattern(srcPattern, dstPattern)`. Scene copy/load will eventually
copy whole `scene_t` records. Retain track/bar copy helpers with a Scene index.

Update Euklid, SOM, copy/clear tools, button handler, LED handler, Menu,
Sequencer, and MidiParser call sites. With `SCENE_COUNT == 1`, they pass the
selected Scene index, never literal pattern slot 0 except in initialization
tests.

### 5.3 Simplify Sequencer selection and staging

- Replace `seq_activePattern` with the selected Scene index API.
- Remove `seq_pendingPattern`, `seq_newPatternAvailable`,
  `seq_loadPendigFlag`, `seq_armActivePatternReload()`, and the
  `pat_commitStagedPattern()` boundary copy path.
- For the one-Scene build, a running root Pattern load must be rejected as busy
  or require transport stop; it must not overwrite the playing Scene. When 17
  Scenes exist, filesystem will load a non-playing Scene and selection will
  switch at the existing bar boundary.
- Modify `seq_setNextPattern()` into the future-facing Scene-selection request
  only when multiple Scenes are enabled; for `SCENE_COUNT == 1`, validate zero
  and otherwise reject.
- Keep transport counters, per-track step indices, quantization, and timing in
  Sequencer. Only the selected data owner changes.

An additional pattern-only staging buffer must not be introduced: it would
recreate `TempPattern` under another name and violate the Scene-level loading
model.

## 6. Replace legacy sound storage and apply paths

### 6.1 Retire `ParameterArray.c`

Delete:

- `Parameter parameterArray[]`
- `parameterArray_init()`
- `paramArray_setParameter()`
- sound-DSP includes from ParameterArray
- the call from `main.c`

Keep `parameter_values[]` temporarily only for non-sound Menu/global/generator
compatibility, as allowed by the audit. Remove `parameters2[]` completely.
Mark the sound portion of the legacy enum private/deprecated and move any
remaining binary import offsets into a filesystem-private
`LegacySoundParameterIds.h`. New sound code must not index `parameter_values[]`
with sound `PAR_*` names.

This deletes an existing abstraction instead of modifying it because its core
model is “one global ID maps to one live DSP pointer.” That cannot represent
per-Scene endpoint images or runtime instrument type selection safely.

### 6.2 Add Preset byte-domain apply APIs

Change Preset public sound APIs to explicit Scene/slot/local coordinates:

- `uint8_t preset_setInstrumentParameter(uint8_t scene_index, uint8_t slot,
  uint8_t local_param, instrument_image_select_t image, uint8_t value,
  uint8_t record_automation)`
  - Inputs identify Scene, slot, local ID, instrument or Morph endpoint, byte
    value, and automation-record intent.
  - Processing: validate descriptor and clamp min/max; reject supplemental
    descriptors here; write the selected endpoint. If this is the active Scene,
    request a Morph recomputation so `morph_interpolation[]` and DSP remain
    coherent. Record the new flat ID only for normal endpoint edits when asked.
  - Output: success/failure plus Scene mutation and scheduled apply.
  - Why new: `preset_applySoundParameter()` accepts an ambiguous legacy global
    ID and recursively enters MidiParser; it cannot select a Scene or image.
  - Clients: Menu, external MIDI compatibility mapping, reset/load apply.

- `uint8_t preset_setSupplementalParameter(uint8_t scene_index, uint8_t slot,
  uint8_t local_param, uint16_t value)`
  - Processing: use descriptor `value_owner`; validate target IDs against the
    destination Scene/type and fall back to invalid/off; update the correct
    supplemental field. If active, update the corresponding modulation node.
  - Output: success/failure.
  - Why separate: supplemental values are not bytes in all cases, have no Morph
    image, and require target invalidation rather than interpolation.
  - Clients: storage parser, Menu routing controls, kit swap validation.

- `uint8_t preset_applyInstrumentRuntimeValue(uint8_t scene_index,
  instrument_param_id_t id, uint8_t value)`
  - Processing: validate Scene/ID/loaded type and dispatch by type/local ID to
    the exact conversion currently in `midiParser_ccHandler()`.
  - Output: live DSP object update only; endpoint images are untouched.
  - Why new: Morph and step automation need transient DSP application without
    rewriting saved endpoints. The existing function always writes
    `parameter_values[]`.
  - Clients: Morph worker, automation node, loaded-Scene apply.

- `uint8_t preset_applyKitAudioRouting(uint8_t scene_index, uint8_t slot)`
  and `preset_applySceneSettings(uint8_t scene_index)`
  - Inputs: selected stored owner.
  - Processing: copy audio route to `mixer_audioRouting`; convert per-instrument
    and voice-wide decimation bytes using the existing
    `valueShaperI2F(value, -0.7f)` rule; publish Scene MIDI note/channel values
    to their consumers.
  - Output: live runtime configuration.
  - Why separate: these values are not instrument descriptor images.
  - Clients: boot, Scene activation, load completion, Menu edits.

### 6.3 Port the live DSP conversions out of MidiParser

In `presetManager.c`, add one private type dispatcher and four type-specific
apply helpers:

- `preset_applyDrumRuntime(slot, local_param, value)`
- `preset_applySnareRuntime(local_param, value)`
- `preset_applyCymbalRuntime(local_param, value)`
- `preset_applyHiHatRuntime(local_param, value)`

Each ports the corresponding existing MIDI cases without changing conversion
math or DSP side effects:

- oscillator pitch updates the correct half of `midiFreq` and calls
  `osc_recalcFreq()`;
- filter frequency uses `valueShaperF2F(..., FILTER_SHAPER)`;
- resonance, drive, filter type, envelope timing/slope, pitch amount,
  oscillator mix/FM amount, waveform, transient, volume/pan, LFO
  rate/sync/wave/retrigger/offset, velocity-volume enable, and per-instrument
  decimation retain their current setters/scales;
- waveform application continues to support the sample-waveform range;
- pan uses the existing type-specific setter;
- modulation target/amount supplementals do not enter these helpers.

These are private functions rather than methods in InstrumentManager because
the audit assigns DSP writes to Preset. They are new rather than modifications
to `midiParser_ccHandler()` because internal edits, Morph, automation, and load
must no longer pretend to be external MIDI messages.

After all internal clients migrate, reduce `midiParser_ccHandler()` to:

- MIDI protocol/NRPN state handling;
- global MIDI controls;
- a table translating supported legacy external sound CC/CC2 numbers to the
  new `(slot, local_param)` ID, followed by
  `preset_setInstrumentParameter()` for genuine external edits.

Do not call MidiParser from Preset. Preserve external CC compatibility through
the explicit translation table, not through the storage model.

### 6.4 Replace modulation's pointer map with cached bindings

Add a Preset-owned native binding type:

```c
typedef struct {
    void *ptr;
    preset_native_value_type_t type;
    OscInfo *waveform_osc;
} preset_modulation_binding_t;
```

Add `uint8_t preset_getModulationBinding(uint8_t scene_index,
instrument_param_id_t id, preset_modulation_binding_t *out)`.

- Inputs: active Scene and canonical target ID.
- Processing: descriptor validity check, then type/local dispatch to the same
  live DSP field formerly listed in `ParameterArray.c`.
- Output: cached native pointer/type and optional oscillator metadata.
- Why new: modulation runs every DSP block and must operate on native float,
  integer, or waveform fields without byte-domain rescaling or a descriptor
  scan per sample block.
- Clients: `modNode_setDestination()`.

Modify `ModulationNode` to cache this binding and its canonical destination.
Rewrite `modNode_setDestination`, `modNode_originalValueChanged`,
`modNode_resetTargets`, and `modNode_updateValue` to use the cached binding.
Preserve waveform interpolation generation/budget behavior. Replace
`modNode_getWaveTargetOsc()`'s legacy-ID switch with `binding.waveform_osc`.

The binding is valid only for the currently active Scene/DSP instance. Scene
selection and instrument swap must rebind all active nodes before playback.
Inactive Scenes retain routing IDs and amounts, not DSP pointers.

### 6.5 Replace automation's MIDI bridge

Modify `AutomationNode` to store a canonical `instrument_param_id_t`.

- `autoNode_setDestination(node, scene_index, dest)` validates
  `is_stepAutomatable`; before switching, restores the previous target from the
  active Scene's `morph_interpolation[]`.
- `autoNode_updateValue(node, scene_index, value)` calls
  `preset_applyInstrumentRuntimeValue()` without altering endpoints.
- Invalid targets use `INSTRUMENT_PARAM_INVALID`.

These existing functions are modified because their node lifecycle remains
correct; only their MIDI transport and source-of-original-value are obsolete.
Delete sound use of `midiParser_originalCcValues[]`.

Widen all PatternData automation destination APIs and remove narrowing casts.

## 7. Add the Morph worker

Create `presetMorphEngine.h/.c`. Move all Morph cursor/generation state and
interpolation math out of `presetManager.c`.

Define a worker state containing Scene index, slot cursor, local-ID cursor,
requested/pass generation, requested/pass amount, and active flag.

Add:

- `void presetMorph_request(uint8_t scene_index, uint8_t morph_amount)`
  - Inputs: Scene and 0..127 amount.
  - Processing: store `scene.settings.morph_amount`; advance request generation;
    arm/restart a pass without doing DSP work synchronously.
  - Output: worker eventually refreshes the whole Scene.
  - Why new: current `preset_morph()` has one global flat-array cursor and does
    not retain Morph amount in Scene state.
  - Clients: Menu, MIDI mod wheel, endpoint edits, post-load activation.

- `uint8_t presetMorph_tick(void)`
  - Input: worker state.
  - Processing: at most one real image-backed descriptor per call; interpolate
    `instrument_parameters[local]` and
    `morph_instrument_parameters[local]` with the existing rounded fixed-point
    formula; write `morph_interpolation[local]`; if the Scene is active, call
    `preset_applyInstrumentRuntimeValue()`. Skip unused local IDs and all
    supplemental descriptors by descriptor owner.
  - Output: nonzero only when one unit of foreground work occurred.
  - Why new file/function: Morph has independent pacing/state and must no longer
    be entangled with filesystem status in Preset Manager.
  - Clients: main-loop foreground pump.

- `void presetMorph_rebuildScene(uint8_t scene_index)`
  - Processing: request a pass using the Scene's stored Morph amount.
  - Output: reconstruction of nonserialized `morph_interpolation[]`.
  - Why new: every Scene/instrument load needs the same explicit post-load rule.
  - Clients: kit load completion and future Scene load.

Remove `preset_morph`, `preset_morphTick`, `preset_getMorphValue`, the old
skip-by-legacy-ID function, and the flat global Morph cursor.

## 8. Storage parser and kit loading

### 8.1 Replace storage-local type/key maps

In `storageTypes.h/.c`:

- Use `instrument_type_t` from InstrumentManager; remove the duplicate
  `storage_instrument_type_t`.
- Delete all six `storage_param_map_t` arrays and
  `storage_paramsForInstrument()`.
- Delegate text type and extension checks to InstrumentManager.
- Change `storage_kitsetParseLine()` to accept `kit_t *`, writing `audio_out[]`
  and slot types into the load target rather than legacy `PAR_AUDIO_OUTn`.
- Change instrument parser initialization to accept Scene index and zero-based
  slot, or a bounded `kit_instrument_slot_t *`.

Modify `storage_instrumentParseLine()`:

- resolve the key with `instrumentManager_descriptorByKey()`;
- parse image-backed values as `uint8_t`;
- write `[params]` to `instrument_parameters[local_param]`;
- write `[morph]` only to
  `morph_instrument_parameters[local_param]`;
- write supplemental fields only from `[params]`; ignore or reject their
  duplicate `[morph]` entries consistently, because they are non-morphed;
- parse canonical target IDs as `uint16_t`, validate them, and store the
  invalid/off sentinel when incompatible;
- retain unknown-key forward compatibility;
- count recognized primary descriptor keys for finalization.

Change the missing-Morph fallback to copy only descriptor-owned image fields
from `instrument_parameters[]` to `morph_instrument_parameters[]`.
`morph_interpolation[]` is not touched by parsing; the Morph worker rebuilds it.

Existing parser functions are modified rather than replaced because their
incremental line handling, metadata validation, and asyncfatfs independence are
already correct. Only lookup and destination ownership change.

### 8.2 Target a Scene in filesystem operations

Extend filesystem request state with `op_scene_index`. Add Scene-indexed
request entry points for kit and pattern operations rather than using hidden
global destinations. Preset passes the selected/load-target Scene explicitly.

Update `filesystem_loadKitDirectory_tick()`:

- reset each destination slot before reading its instrument file;
- parse kitset and instruments into `scenes[op_scene_index].kit`;
- set Scene voice-wide decimation through Scene settings, not kit parsing;
- after all six files validate, run target invalidation and request Morph
  reconstruction;
- do not mutate the active DSP until the whole Kit has loaded successfully;
- on failure, leave the previous Scene/Kit intact by parsing into a caller-owned
  temporary `kit_t` scratch, then commit it once. This scratch is a Kit load
  transaction, not a forbidden pattern-only staging Scene.

Because `kit_t` is roughly 1.2KB in the one-Scene build, place the transaction
scratch in ordinary SRAM and include it in the map-file budget. When 17 Scenes
exist, load directly into a non-playing Scene instead and remove this scratch.

### 8.3 Save descriptor-driven directory Kits

Replace flat `pNNN.snd` Kit saving with an async directory state machine that
emits the existing `kitset.kcg` and six named instrument files:

- iterate descriptors rather than legacy enum ranges;
- write `[params]` from `instrument_parameters[]` or supplemental fields;
- write `[morph]` from `morph_instrument_parameters[]` for image-backed fields;
- either omit supplemental keys from `[morph]` or write them only in `[params]`;
  update the converter to follow the same rule;
- never serialize `morph_interpolation[]`;
- write audio routing only in `kitset.kcg`.

Reuse `storage_copyFilename`, type registry data, and the existing async
read/write pump. New encode helpers are warranted because current
`filesystem_saveKit_tick()` writes a fixed binary byte span and cannot emit
directory-scoped named records.

### 8.4 Convert legacy target values in the Python converter

`tools/convert_legacy_kits.py` must continue mapping legacy byte offsets to
canonical file keys, but additionally:

- translate legacy `modTargets[]` indices in `velo_mod_dest` and
  `lfo_target_param` to canonical flat instrument IDs;
- emit the invalid/off sentinel representation chosen by the text format for
  unmapped targets;
- keep `lfo_target_voice` consistent with the canonical target slot;
- emit supplemental keys only in `[params]`;
- continue emitting image-backed keys in both endpoint sections.

Do not parse `Cc2Text.c` at conversion time. Add an explicit, tested legacy
target conversion table generated from the current mapping before `Cc2Text.c`
is removed. This is historical format knowledge and belongs in the converter,
not in the runtime registry.

## 9. Menu becomes descriptor-driven for instrument pages

### 9.1 Preserve non-sound Menu compatibility separately

Keep static `menuPages` and `parameter_dtypes[]` only for global, performance,
pattern, generator, load/save, and other non-instrument controls during this
pass. Remove VOICE1..VOICE7 instrument definitions from `menuPages.h`.

Add a resolved cell type containing:

```c
typedef enum {
    MENU_VALUE_INSTRUMENT,
    MENU_VALUE_SUPPLEMENTAL,
    MENU_VALUE_KIT_AUDIO_OUT,
    MENU_VALUE_SCENE_SETTING,
    MENU_VALUE_LEGACY_NON_SOUND
} menu_value_owner_t;
```

plus descriptor/owner coordinates and display metadata.

Add `uint8_t menu_resolveCell(page, subpage, position,
menu_resolved_cell_t *out)`:

- Inputs: current page coordinates.
- Processing: for voice pages, map page 1..6 to slot 0..5, page 7 to slot 5
  open-hat layout, resolve descriptor by loaded type/menu position, and inject
  audio routing. For other pages, adapt the existing static `Page`.
- Output: populated cell or empty.
- Why new: direct `(&menuPages[...].bot1)[...]` indexing appears throughout
  repaint, navigation, encoder, knob, and visibility code. One resolver avoids
  duplicating dynamic-page branches in each path.
- Clients: `menu_repaintGeneric`, encoder navigation, endless-pot scaling/edit,
  external-change visibility, page availability checks.

### 9.2 Replace flat sound value access

Modify:

- `menu_paramUsesMorphView()` to accept a resolved instrument cell and return
  true only for image-backed descriptors.
- `menu_getParameterDisplayValue()` to read the selected Scene/slot/local
  endpoint or supplemental/kit/Scene field.
- `menu_getParameterEditPtr()` should be removed for sound values. Returning
  raw pointers lets encoder code bypass clamping and side effects. Use
  read-modify-commit through the resolved cell instead.
- `menu_sendEditedParameter()` to call the correct Preset, Scene-setting, or
  kit-setting API. Morph endpoint edits request a worker pass but never apply
  the endpoint directly.

Retain the existing non-sound `parameter_values[]` access behind the resolved
legacy owner.

### 9.3 Replace dtype/list and name switches

For instrument cells:

- use descriptor min/max and dtype directly;
- use static `dtype_source` tables or runtime count/format callbacks;
- use descriptor short/category/long labels;
- remove instrument dependence on `valueNames[]`,
  `parameter_dtypes[]`, and packed Menu IDs.

Modify `getMaxEntriesForMenu()` and `getMenuItemNameForValue()` into generic
dtype-source helpers that take a resolved cell/descriptor. Keep old wrappers
only for non-sound pages until their later migration. The new helpers exist
because the old `uint8_t menuId` interface cannot carry a pointer or resolver.

### 9.4 Replace `Cc2Text.c`

Delete:

- `modTargets[]`
- `modTargetVoiceOffsets[]`
- `paramToModTarget[]`
- `voiceFromModTargValue()`
- gap-index stubs

Add an InstrumentManager iterator that enumerates valid targets in stable
flat-ID order for the selected Scene. Menu target values should store the
canonical target ID, not an index into a changing filtered list.

Add Menu helpers to:

- count valid targets for LFO or velocity use;
- map a displayed ordinal to canonical ID;
- map canonical ID back to a displayed ordinal;
- render `slot number + descriptor short name` and full
  `category + long name`.

These must use the target slot's loaded type and descriptor flags. Instrument
swap invalidation therefore removes targets that no longer exist instead of
preserving a stale ordinal.

### 9.5 Scene-owned STEP settings

Update the STEP front page:

- channel/note edits call SceneData setters;
- length/scale/shuffle continue through PatternData;
- Sequencer and MidiParser read Scene MIDI settings directly;
- remove compatibility writes to `PAR_MIDI_CHAN_*`,
  `PAR_MIDI_NOTE*`, `midi_NoteOverride[]`, and PatternData MIDI fields.

System MIDI input-channel menu items continue to use
`midiParser_setChannel()` and global settings.

## 10. MIDI, Sequencer, modulation, and DSP affiliates

### 10.1 MIDI

- Remove `midi_NoteOverride[]`; Scene settings are authoritative.
- Change `midiParser_getVoiceMidiNote/Channel()` to accept/use the active Scene.
- Keep `midi_MidiChannels[]` only for system MIDI input routing where still
  required; do not mirror Scene track output channels into it.
- Map mod wheel directly to `scene.settings.morph_amount` through
  `presetMorph_request()`.
- Route supported external sound CC/NRPN through the explicit legacy-to-new ID
  table and Preset endpoint setter.
- Keep external global MIDI configuration and mute handling in MidiParser.

### 10.2 Sequencer

- Replace all `seq_activePattern` PatternData calls with active Scene index.
- Read output MIDI channel/note from Scene settings.
- Widen automation destinations end-to-end.
- Before applying a Step automation destination, validate it against the
  active Scene's current instrument types; invalid means no automation.
- Remove program-change behavior tied to old pattern indices unless it is
  explicitly redirected to Scene selection.

### 10.3 Mixer and DSP globals

The DSP render architecture remains fixed-type in this pass, as allowed by
Section 5 of the audit. Keep `voiceArray[0..2]`, `snareVoice`, `cymbalVoice`,
and `hatVoice`. Preset's type/local dispatcher binds the Scene parameter model
to those current instances.

Scene activation copies:

- kit audio routing into `mixer_audioRouting[]`;
- per-instrument decimation into `mixer_decimation_rate[0..5]`;
- Scene voice-wide decimation into index 6;
- all `morph_interpolation[]` values into the fixed DSP instances;
- supplemental routing into modulation nodes.

Do not move mixer scheduling or trigger dispatch into InstrumentManager.
Any-type-in-any-slot DSP allocation remains the named follow-up from the audit.

## 11. Pattern and container file migration

The current `.PAT`/All/Performance bridge cannot be left unchanged:

- it writes eight patterns although only one exists;
- it serializes automation IDs in one byte;
- it stores MIDI channel/note inside Pattern track extensions;
- it depends on `TempPattern`.

Create a new one-pattern version:

- one 8-byte name where the existing root Pattern UI still needs it;
- 7 * 128 Step records;
- Step record uses two-byte little-endian IDs for both automation lanes;
- one main-step compatibility mask per track while that UI remains;
- one PatternSetting;
- per-track length, rotation, scale, and shuffle;
- no MIDI channel/note in Pattern payload.

Bump the pattern/container version and make loaders reject newer unknown
versions. Add a legacy reader for the current 8-pattern/7-byte-Step format:

- import only old pattern slot 0;
- map old automation IDs through the same historical conversion table as other
  legacy sound IDs;
- migrate track MIDI channel/note into destination Scene settings;
- discard old slots 1..7 exactly once during import;
- never allocate a discard pattern or `TempPattern`.

Update `filesystem_patternStepAddress`, count constants, pack/unpack helpers,
save/load Pattern state machines, and container phases to operate on a supplied
Scene. Remove `filesystem_discardStep`, discard settings, and all
`FS_PATTERN_FILE_PATTERN_COUNT == 8` logic from new saves.

Legacy All/Performance imports must decode their kit area into the selected
Scene through a private legacy map. New saves should use the new Scene-aware
representation or be disabled until the directory Scene writer exists; they
must not silently write the obsolete flat sound array. Make that behavior
explicit in Menu rather than producing corrupt compatibility files.

The root Pattern loader cannot safely replace `scenes[0].pattern` while it is
playing now that `TempPattern` is forbidden. In the one-Scene build, require
stopped transport for Pattern load. The later 17-Scene loader uses a non-playing
Scene and performs an atomic Scene selection at the Sequencer boundary.

## 12. Initialization and foreground scheduling

Update `main.c` boot order:

1. Initialize DSP objects.
2. Initialize InstrumentManager invariants if runtime validation is enabled.
3. Call `scene_initAll()`.
4. Initialize Menu's non-sound state.
5. Load Kit/Pattern data into Scene 0.
6. Rebuild Morph interpolation.
7. Apply Scene 0 to DSP synchronously before audio starts.
8. Start audio.

Remove `parameterArray_init()`.

In the foreground loop, call `presetMorph_tick()` with the same bounded
one-parameter cadence as the current Morph tick. Retain the chunked loaded-Kit
apply, but change its cursor to `(scene, slot, descriptor)` and apply at most one
real parameter or one supplemental routing update per pass.

Add a Scene activation apply cursor rather than six-voice routing only:

- `preset_startSceneApply(scene_index)`
- `preset_tickSceneApply()`

The start function records a validated Scene and resets cursors. The tick
function applies one descriptor/setting per pass and reports work done. This is
new because `preset_startDrumsetApply()` currently updates only six routing
destinations and assumes the bulk sound values were already applied by
MidiParser while loading.

## 13. Validation and failure behavior

At boot in a debug/validation build, validate every registry:

- type tokens and extensions are unique;
- descriptor local IDs and file keys are unique within a type;
- IDs are below 64;
- menu positions are unique;
- default lies within min/max;
- supplemental local IDs have the correct `value_owner`;
- static dtype lists have nonzero count and runtime sources have both callbacks;
- every canonical key listed in the audit appears exactly once;
- descriptor counts are Drum 35, Snare 34, Cymbal 35, Hat 35.

Instrument load failure must not partially replace the active Kit. Invalid
types, extensions, values, missing required metadata, or impossible target IDs
return a storage error and retain the prior committed Scene.

On instrument type change:

- reset the slot before parsing;
- validate every Scene supplemental target against destination descriptors;
- validate every Pattern automation destination;
- replace invalid values with the off/invalid sentinel;
- do not attempt semantic remapping by matching names.

## 14. Tests and verification

Add host-testable pure modules where possible and a `make check` target that
does not require target hardware:

1. Descriptor registry test:
   - all invariants above;
   - exact canonical key sets match `param_rename.txt`;
   - local IDs form the intended semantic layout.
2. Converter/schema test:
   - convert representative `.SND` data;
   - verify all six files parse through descriptors;
   - verify `[params]` and `[morph]` endpoint bytes;
   - verify supplemental targets convert to canonical IDs and are not morphed.
3. Scene isolation test:
   - write all three images in Scene A;
   - prove Scene B remains unchanged when `SCENE_COUNT` is temporarily built as
     2 for the test.
4. Morph test:
   - endpoints 0/127 at Morph 0, midpoint, and 127;
   - verify rounded result and `morph_interpolation[]`;
   - verify supplementals are untouched;
   - verify interpolation is reconstructed after simulated load.
5. Storage transaction test:
   - malformed sixth instrument leaves the previous Kit intact.
6. Pattern test:
   - `sizeof` and field-width assertions;
   - one-pattern round trip with IDs below and above 255;
   - legacy eight-pattern import keeps slot 0 only;
   - MIDI settings land in Scene, not Pattern.
7. Target invalidation test:
   - swap each current type into its allowed current slot;
   - invalid modulation and automation IDs become off;
   - valid IDs survive.
8. Menu resolver test:
   - all current voice cells resolve to the same labels/ranges as before;
   - open/closed hat decay differs correctly;
   - runtime waveform list formatting remains bounded.
9. DSP apply regression:
   - for every descriptor, compare the old MIDI-handler result captured before
     removal with the new Preset apply result on initialized DSP fixtures.
10. Build verification:
    - `make`;
    - `make img`;
    - inspect the linker map for Scene, transaction scratch, descriptor tables,
      and moved `-Ofast` voice objects;
    - confirm no `parameterArray`, sound `parameter_values[]`, `parameters2`,
      `modTargets`, `TempPattern`, or eight-pattern discard symbols remain.

Hardware verification:

- boot and load `001 Slak`;
- compare all six voices against the pre-refactor build;
- normal and SHIFT+VOICE edits;
- Morph sweep while audio runs;
- velocity and LFO target changes;
- step automation above ID 255;
- Kit load while stopped and while running;
- Pattern load stopped, with running-load rejection clearly displayed;
- MIDI input note/channel behavior;
- audio routing and per-voice/global decimation;
- CPU/audio underrun observation during Scene apply and Morph passes.

## 15. Ordered implementation commits

Keep the work bisectable in this order:

1. Add descriptor/common ID types and registry validation without changing
   runtime behavior.
2. Move the four DSP source pairs and update the build mechanically.
3. Add SceneData and make PatternSet one-dimensional inside `scenes[1]`;
   remove `TempPattern` and obsolete selection/staging code.
4. Widen automation IDs and land the new versioned one-pattern serializer plus
   legacy import.
5. Redirect storageTypes and directory Kit loading into Scene/descriptor
   storage.
6. Port Preset DSP apply conversions and external MIDI translation; remove
   internal MIDI round trips.
7. Replace modulation bindings and automation application; delete
   ParameterArray pointer maps.
8. Add the Morph worker and remove `parameters2[]`.
9. Convert instrument Menu pages, dtype sources, and target enumeration to
   descriptors; remove `Cc2Text` sound tables.
10. Add descriptor-driven directory Kit save and update the converter's
    supplemental/target handling.
11. Migrate remaining Scene settings and legacy container affiliates.
12. Run source-level, host, target-build, map-budget, and hardware regression
    gates; then update `MEMORY.md`, module interchange documentation, and the
    Phase 2 status only with behavior actually verified.

Do not combine the mechanical DSP file move with parameter behavior changes.
Do not delete a legacy path until the source search and regression comparison
show that all of its clients have moved.

## 16. Exhaustive live-file edit matrix

This matrix is the final source-search checklist. It supplements the functional
sections above so small affiliates are not lost during implementation.

| File | Required edit |
|---|---|
| `main.c` | Add Scene/InstrumentManager/Morph initialization and tick calls; remove `parameterArray_init()`; update moved DSP includes and boot Scene apply. |
| `Makefile` | Add new sources/include paths and moved DSP paths; preserve `-Ofast` only for render sources; add host `check` target. |
| `Core/Scene/SceneData.c/.h` | New Scene/kit/image owner, selection, initialization, bounds access, and Scene MIDI-setting accessors. |
| `Core/Scene/Preset/ParameterArray.c` | Delete after native modulation bindings and Preset apply replace every client. |
| `Core/Scene/Preset/ParameterArray.h` | Remove pointer-map types/APIs; retain or split only non-sound compatibility IDs/state declarations and private legacy import IDs. |
| `Core/Scene/Preset/presetManager.c/.h` | Replace legacy global-ID/MIDI apply with Scene/slot/local endpoint, supplemental, runtime, binding, Scene-apply, and setting-apply APIs. |
| `Core/Scene/Preset/presetMorphEngine.c/.h` | New per-Scene Morph request/generation/cursor worker and interpolation reconstruction. |
| `Core/Scene/Pattern/PatternData.c/.h` | Embed one-dimensional PatternSet in Scene; remove globals, TempPattern, MIDI settings, pattern dimension, staging API; widen automation IDs and change APIs to Scene indices. |
| `Core/Scene/Pattern/EuklidGenerator.c/.h` | Pass selected Scene to PatternData mutations instead of a pattern slot; retain generator-owned edit state. |
| `Core/Scene/Pattern/SomGenerator.c/.h` | Pass selected Scene to generated Pattern writes; do not create separate Pattern storage. |
| `Core/Menu/menu.c` | Remove sound arrays/flat target tables; add resolved cells and descriptor-driven voice rendering/editing; route Scene/kit/supplemental/endpoint changes to owners; retain non-sound compatibility state only. |
| `Core/Menu/menu.h` | Remove `ParameterArray` sound dependency, `ModTarg`, target offsets, `parameters2`, and sound pointer-edit declarations; add resolved-cell and Scene-aware interfaces. |
| `Core/Menu/menuPages.h` | Remove seven static instrument page definitions; retain non-sound pages and use dynamic descriptor resolution for VOICE pages. |
| `Core/Menu/MenuText.h` | Keep genuinely shared static lists; expose them through typed list descriptors; remove packed Menu-ID requirements for instrument values. |
| `Core/Menu/Cc2Text.c` | Delete flat sound target/name tables after descriptor target enumeration lands. |
| `Core/Menu/CcNr2Text.h` | Remove declarations for `modTargets`, count/gap/voice lookup, and `paramToModTarget`; retain unrelated MIDI text declarations if any. |
| `Core/Menu/copyClearTools.c/.h` | Change Pattern arguments to Scene indices and remove pattern-to-pattern copy assumptions; Kit/instrument copy must use Scene owners if exposed in this pass. |
| `Core/Menu/screensaver.c` | Continue reading the non-sound screensaver setting, but include the new non-sound settings declaration rather than `ParameterArray.h` transitively. No Scene sound access is added. |
| `Core/Hardware/frontPanel/buttonHandler.c` | Pass Scene indices to PatternData/LED calls; replace held sound-parameter reset state with resolved `(scene, slot, local, image)` identity and restore through Preset; update Morph-view comments and remove `seq_activePattern` checks. |
| `Core/Hardware/frontPanel/ledHandler.c` | Treat former pattern arguments as Scene indices; remove `pat_patternValid`; retain non-sound `PAR_FOLLOW`/rotation display mirrors until their later migration. |
| `Core/Hardware/SD/storageTypes.c/.h` | Remove duplicate type enum and key maps; parse descriptor-owned Scene kit/slot destinations, uint16 supplementals, and endpoint-only Morph data. |
| `Core/Hardware/SD/filesystem.c` | Add operation Scene target and transactional Kit scratch; rewrite Kit save, Pattern/container formats, legacy import, and all flat sound-array references; remove discard patterns and TempPattern staging. |
| `Core/Hardware/SD/filesystem.h` | Add Scene-indexed requests/status contracts and document stopped-transport Pattern-load restriction. |
| `Core/Hardware/SD/kitBrowser.c/.h` | No parameter storage change; update request signatures only if it directly invokes a newly Scene-indexed load API. |
| `Core/MIDI/MidiParser.c/.h` | Remove note override storage and internal sound apply switch usage; add active-Scene note/channel reads and explicit legacy CC/NRPN-to-canonical-ID translation; preserve protocol/global behavior. |
| `Core/MIDI/MidiMessages.h` | Stop exporting eight-bit `NO_AUTOMATION` to PatternData; retain wire-level constants and add no Scene storage IDs here. |
| `Core/MIDI/MidiVoiceControl.c` | Update moved instrument includes. Trigger behavior stays fixed-type in this pass. |
| `Core/Sequencer/sequencer.c/.h` | Replace active/pending pattern globals with selected Scene use; remove staging commit API; widen automation destinations; read Scene MIDI settings; retain transport/timing state. |
| `Core/DSPAudio/automationNode.c/.h` | Replace MIDI repacking/original-CC restore with canonical IDs, Scene interpolation restore, validity checks, and Preset runtime apply. |
| `Core/DSPAudio/modulationNode.c/.h` | Remove ParameterArray include/pointer lookup and legacy wave-ID switch; cache Preset native bindings and canonical targets; preserve block-time math and interpolation budget. |
| `Core/DSPAudio/lfo.c` | Update moved instrument includes and any modulation destination initialization signature requiring active Scene. |
| `Core/DSPAudio/mixer.c` | Update moved includes; keep fixed render topology; receive routing/decimation from Scene activation. |
| `Core/DSP/Instruments/Drum/DrumVoice.c/.h` | Mechanical move only, followed by any include-path adjustments required by Preset binding. |
| `Core/DSP/Instruments/Snare/Snare.c/.h` | Mechanical move only. |
| `Core/DSP/Instruments/Cymbal/CymbalVoice.c/.h` | Mechanical move only. |
| `Core/DSP/Instruments/HiHat/HiHat.c/.h` | Mechanical move only. |
| `Core/DSP/Instruments/*/*Parameters.c/.h` | New canonical descriptor tables, typed value lists, explicit local IDs, menu placement, defaults/ranges, ownership, and validity flags. |
| `Core/DSP/Instruments/InstrumentManager.c/.h` | New registry, type/extension/key/local/menu lookup, target validation, ID helpers, and slot reset. |
| `Core/DSPAudio/DrumVoice.c/.h`, `Snare.c/.h`, `CymbalVoice.c/.h`, `HiHat.c/.h` | Removed after their history-preserving moves; no forwarding duplicate sources remain. |
| `tools/convert_legacy_kits.py` | Keep canonical rename validation; convert target values to canonical IDs; emit supplementals only in `[params]`; update whole-output tests. |
| `param_rename.txt` | Remains the old-key-to-canonical-key conversion input; no runtime ownership or numeric IDs are added to it. |
| `SD_CARD/Kit/**` | Regenerate after converter target/supplemental changes and validate every manifest/key/value against descriptors. |
| `MEMORY.md` | Record only completed and verified ownership/file-format changes at the end of implementation. |
| `knowledge_files/MODULE_INTERCHANGE_SPEC.md` | Replace PatternData global/TempPattern and Preset flat-array boundaries with Scene, descriptor, and runtime-apply contracts. |
| `FILESYSTEM_SPEC.md` | Clarify runtime-only interpolation omission, Scene-owned MIDI/decimation settings, and the one-pattern Scene payload if implementation changes its stated file contract. |
| `KIT_INSTRUMENT_SPEC_FOLLOWUP_AUDIT.md` | Mark plan items implemented only after code/tests land; keep canonical names and ownership definitions synchronized. |
