# Instrument Parameter Refactor Follow-up Patching Plan

## Current Diagnosis

The previous instrument refactor moved instrument file parsing and runtime
meaning into `Core/DSP/Instruments/`, but the two remaining user-visible
surfaces are still split:

1. Voice pages are blank because `Core/Menu/menuPages.h` intentionally replaced
   `VOICE1_PAGE` through `VOICE7_PAGE` with `{0}`, and most of `menu.c` still
   reads visible parameters directly from the static `menuPages[][]` table and
   `parameter_dtypes[]`.
2. The kit loader now parses `SD_CARD/Kit/.../*.drm|*.snr|*.cym|*.hat` into
   `SceneData` descriptor-index cells, but runtime application is only partly
   descriptor-aware. `instrumentManager_writeRuntime()` currently handles
   `INSTRUMENT_BIND_INSTANCE_OFFSET` and `INSTRUMENT_BIND_SLOT_DECIMATION`;
   the velocity/LFO binding kinds are still inert, and many offset bindings
   use a raw `value / 127.0f` write where the old MIDI path used a dedicated
   setter or value shaper.

The most important structural bug for menu population is that the old UI model
uses `uint16_t` parameter IDs for `ParameterArray`, while new instrument IDs are
also `uint16_t` and start at `slot * 64 + descriptor_index`. Those numeric
ranges overlap. A voice page cell can no longer be treated as "just a
ParameterArray ID"; it needs a resolved menu cell that knows whether it is a
static/global parameter or an instrument descriptor.

## Patch 1 - Add Instrument-owned Menu Layouts

Add a descriptor-local menu layout to each instrument parameter file:

- `Core/DSP/Instruments/Drum/DrumParameters.c`
- `Core/DSP/Instruments/Snare/SnareParameters.c`
- `Core/DSP/Instruments/Cymbal/CymbalParameters.c`
- `Core/DSP/Instruments/HiHat/HiHatParameters.c`

The layout should live beside the descriptor rows, not in `menuPages.h`.
`InstrumentManager.c` should keep only the generic page resolver and registry
shape. This preserves the ownership rule: instrument files define file keys,
runtime binding, text, dtype, and page position for their own parameters.

Recommended shape:

```c
typedef struct {
    uint8_t descriptor_index[8];
} instrument_menu_page_t;

typedef struct {
    const instrument_menu_page_t *pages;
    uint8_t page_count;
} instrument_menu_layout_t;
```

Use `0xff` as the empty-cell sentinel because descriptor index `0` is valid.
Keep `NUM_SUB_PAGES` as the upper bound so menu traversal behavior remains
familiar.

The layouts must reproduce the old voice-page positions from `menuPages.old`.
Treat that file as the positional reference and translate each old `PAR_*` cell
to the matching descriptor index in the relevant instrument file:

- `VOICE1_PAGE` through `VOICE3_PAGE` use the Drum layout.
- `VOICE4_PAGE` uses the Snare layout.
- `VOICE5_PAGE` uses the Cymbal layout.
- `VOICE6_PAGE` and `VOICE7_PAGE` use the HiHat layout, but must preserve the
  old closed/open hat difference from `menuPages.old`: page 1 position 1 is
  closed decay for `VOICE6_PAGE` and open decay for `VOICE7_PAGE`; the
  track MIDI channel/note cells on old page 7 remain track settings, not
  instrument descriptor cells.

The old static voice pages also mixed instrument parameters with non-instrument
cells such as audio output, pattern length, MIDI channel, and MIDI note. In the
new layout, keep instrument-owned cells in the instrument files and route
non-instrument cells through the menu resolver as static/Scene or Pattern
settings. Do not force track settings into `DrumParameters.c` or related
instrument files.

The key rule is that each instrument-owned cell stores a descriptor index, and
display text comes from that descriptor's `short_name`, `category`, and
`long_name` strings. Page order and empty/skip positions should match
`menuPages.old` unless the old cell is no longer an instrument parameter.

Implementation details:

- Extend `instrument_registry_entry_t` with `const instrument_menu_page_t *menu_pages`
  and `uint8_t menu_page_count`, or equivalent.
- Replace the current stub `instrumentManager_menuDescriptor()` with a real
  lookup that returns the descriptor for `(type, page, position)`.
- Add `instrumentManager_menuDescriptorIndex()` or return both descriptor and
  descriptor index, because edit commits need the index.
- Add contract comments to the new layout type and resolver explaining that
  layout cells are descriptor indices, not `ParameterArray` IDs.

## Patch 2 - Add a Menu Cell Resolver

Do not make every `menu.c` call site manually branch on static pages versus
voice pages. Add a small internal resolver layer near the existing menu helper
functions.

Recommended internal type:

```c
typedef enum {
    MENU_CELL_EMPTY = 0,
    MENU_CELL_STATIC,
    MENU_CELL_INSTRUMENT,
} menu_cell_kind_t;

typedef struct {
    menu_cell_kind_t kind;
    uint16_t static_param;
    uint8_t text_id;
    uint8_t slot;
    uint8_t descriptor_index;
    const ParamDescriptor *descriptor;
} menu_cell_t;
```

Resolver contract:

- For non-voice pages, read `menuPages[menu_activePage][subpage]` exactly as
  today and return `MENU_CELL_STATIC`.
- For `VOICE1_PAGE` through `VOICE6_PAGE`, resolve `slot = menu_activePage`.
  Use `scene_instrumentSlotConst(scene_getActiveIndex(), slot)->type`, then
  ask `InstrumentManager` for the descriptor at the active subpage/position.
- `VOICE7_PAGE` currently aliases hi-hat/open behavior while storage has only
  six instrument slots. Decide explicitly whether page 6 should show slot 5
  with alternate title/trigger semantics or remain disabled until open-hat
  split storage exists. For this patch, safest is to show slot 5's hat
  descriptor layout on both `VOICE6_PAGE` and `VOICE7_PAGE`, with edits mapped
  to slot 5.
- Empty voice cells return `MENU_CELL_EMPTY`.

Replace direct `(&menuPages[...].top1)[...]` and `bot1` reads in these paths
with resolver calls:

- `menu_repaintGeneric()`
- `menu_encoderChangeParameter()`
- `menu_moveToMenuItem()`
- `menu_paramVisible()`
- `menu_updateEndlessPotScales()`
- `menu_parseKnobDelta()`
- `menu_cpuUseWidgetVisible()` can remain static-only unless generalized
  cheaply.
- `checkScrollSign()` and `has2ndPage()` also need resolver-backed empty checks
  so voice page scrolling works.

Keep the resolver narrow and documented. Static pages are still static; only
voice pages become dynamic.

## Patch 3 - Descriptor-backed Display Formatting

Teach `menu_repaintGeneric()` to format instrument cells from `ParamDescriptor`
metadata:

- Normal mode top row uses `descriptor->short_name` directly, copied/truncated
  to the existing 3-character cells.
- Edit mode top row uses `descriptor->category` in columns 0-7 and
  `descriptor->long_name` in columns 8-15.
- Dtype comes from `descriptor->dtype`, not `parameter_dtypes[paramNr]`.
- Value comes from the active scene slot image:
  - Normal voice mode: `instrument_parameters[descriptor_index]`.
  - `voiceModeShowMorph`: `morph_instrument_parameters[descriptor_index]`.
  - If showing the currently interpolated sound would be useful later, add a
    separate view mode; do not conflate it with morph endpoint editing.

Add helper functions:

```c
static uint8_t menu_cellDtype(const menu_cell_t *cell);
static uint16_t menu_cellDisplayValue(const menu_cell_t *cell);
static uint8_t menu_cellSetValue(const menu_cell_t *cell, uint16_t value);
```

For now the visible value can still be clamped to/displayed as a byte for most
dtypes, but target-selection cells need to preserve `uint16_t` values such as
`INSTRUMENT_PARAM_INVALID` (`65535`). The current `uint8_t *` edit-pointer model
cannot support those values safely.

## Patch 4 - Replace Voice-page Edit Pointers with Commit Helpers

The old edit path mutates a `uint8_t *` returned by `menu_getParameterEditPtr()`
and then calls `menu_sendEditedParameter()`. That works for `parameter_values[]`
but is a poor fit for descriptor cells and invalid 16-bit target IDs.

Keep `menu_getParameterEditPtr()` for static pages if desired, but make encoder
and endless-pot editing operate through a value/commit helper:

1. Resolve the active cell.
2. Read current value as `uint16_t`.
3. Apply increment and dtype clamp.
4. Commit:
   - Static cell: existing `parameter_values[]` / `parameters2[]` behavior.
   - Instrument main image:
     `preset_setInstrumentParameter(scene, slot, descriptor_index,
     INSTRUMENT_IMAGE_MAIN, value, 1)` for morphable descriptors.
   - Instrument morph endpoint:
     `preset_setInstrumentParameter(scene, slot, descriptor_index,
     INSTRUMENT_IMAGE_MORPH, value, 0)`.
   - Instrument non-morph/supplemental:
     `preset_setSupplementalParameter(scene, slot, descriptor_index, value)`.

Update comments around `voiceModeShowMorph`; it no longer redirects
`parameters2[]` for voice pages. It selects the descriptor-owned morph endpoint
image in `SceneData`.

## Patch 5 - Restore Runtime Shapers and Special Writers

The current runtime writer is the likely cause of "voices trigger but no useful
sound" after loaded kit apply. It writes most floats as `value / 127.0f` and
binds coarse/fine pitch rows to `modNodeValue`, so fine pitch can overwrite
coarse pitch and oscillator `midiFreq` may never be updated.

Add an instrument-owned value writer/shaper layer. Do not route back through
legacy `PAR_*`, but do preserve the old DSP semantics:

- Oscillator coarse/fine rows should update `OscInfo.midiFreq` high/low bytes
  and call `osc_recalcFreq()`.
- Filter frequency should call `SVF_directSetFilterValue(... valueShaperF2F(...))`.
- Filter resonance should call `SVF_setReso()`.
- Filter drive should call `SVF_setDrive()` when `UNIT_GAIN_DRIVE` is off, or
  the existing direct normalized write when enabled.
- Envelope attack/decay/slope rows should call the proper envelope setters
  (`slopeEg2_setAttack`, `slopeEg2_setDecay`, `DecayEg_setDecay`, etc.) rather
  than raw struct writes where setters exist.
- Pitch envelope amount should use `calcPitchModAmount()`.
- Transient wave should call `transient_setWaveform()`.
- Transient frequency should use the old `1.f + ((value / 33.9f) - 0.75f)`
  mapping.
- Filter type should preserve the old `value + 1` rule where DSP filter type
  `0` means off/silent.
- LFO and oscillator waveform values should be range-checked against available
  generated/user sample ranges where applicable.

Recommended implementation:

- Extend `instrument_runtime_binding_t` with a small writer enum, or add a
  function pointer per descriptor if code size is acceptable.
- Keep simple offset writes for truly linear fields like `vol`, `pan`,
  `mixOscs`, and `lfo.amount`.
- Add comments to every non-obvious writer explaining which old MIDI path it
  replaces.

Verification target for this patch: after loading `SD_CARD/Kit/001 Slak`, a
drum slot with `instrument_vol=127`, `amp_envelope_decay=35`, and
`osc1_pitch_coarse=31` should leave the runtime `DrumVoice` with nonzero volume,
nonzero/usable envelope decay, oscillator `midiFreq` high byte set to `31`, and
frequency recalculated.

## Patch 6 - Implement Supplemental Velocity/LFO Bindings

`INSTRUMENT_BIND_VELOCITY_AMOUNT`, `INSTRUMENT_BIND_VELOCITY_TARGET`,
`INSTRUMENT_BIND_LFO_TARGET_VOICE`, and `INSTRUMENT_BIND_LFO_TARGET_PARAM` are
declared and emitted by the kit converter, but they are not fully applied by
`instrumentManager_writeRuntime()`.

These binding kinds are not a second parameter list. They are per-descriptor
runtime writer modes. The descriptor index remains the storage address; the
binding kind only answers "when this stored value is applied, which runtime
object does it update?" For example, the descriptor row for `velo_mod_amount`
still occupies one ordinary slot cell in
`instrument_parameters[descriptor_index]`; its binding says that applying that
cell writes `velocityModulators[slot].amount` instead of an offset inside the
voice struct.

Implement them as descriptor-owned runtime behavior:

- `VELO_MOD_AMOUNT`: write `velocityModulators[slot].amount = value / 127.0f`.
- `VELO_MOD_DEST`: accept `INSTRUMENT_PARAM_INVALID` as off/no destination;
  otherwise validate canonical `slot * 64 + descriptor` target with
  `instrumentManager_targetValid(... INSTRUMENT_TARGET_MODULATION)`.
- `LFO_TARGET_VOICE`: store/apply the selected 1-based destination voice. The
  parser currently clamps to `1..6`, but generated files still contain zero in
  some places; either fix the converter or keep parser clamping and regenerate.
- `LFO_TARGET_PARAM`: pair with the stored target voice to set that voice's LFO
  `modTarget` destination. Invalid/off should clear the destination.

This exposes another deeper issue: `modulationNode.c` still indexes
`parameterArray[]`, which is now intentionally empty for instrument parameters.
There are two viable patch paths:

- Short compatibility path: rebuild `parameterArray[]` entries for canonical
  instrument IDs as runtime affiliates only, generated from descriptors during
  voice init/load apply. This gets velocity/LFO modulation working quickly but
  partially reintroduces a flat table.
- Cleaner path: teach `ModulationNode` to carry an `instrument_param_id_t`
  destination plus a descriptor/runtime pointer and call
  `instrumentManager_writeRuntime()` for updates/resets.

Prefer the cleaner path if the patch budget allows it. If not, document the
compatibility table clearly as an affiliate cache, not the source of parameter
meaning.

## Patch 7 - Fix Target Menus for Descriptor IDs

`Core/Menu/Cc2Text.c` currently collapsed `modTargets[]` to a placeholder, and
target dtypes still expect old target indices. The descriptor world needs a
fresh target list.

Do not rebuild `modTargets[]` as a global static list. Add an instrument target
enumerator/helper that derives the selectable targets from the active Scene's
instrument descriptors at display/edit time:

- Iterate active scene slots and each slot's descriptors.
- Include only descriptors with `INSTRUMENT_PARAM_FLAG_MODULATABLE` for
  velocity/LFO target menus.
- Include only descriptors with `INSTRUMENT_PARAM_FLAG_AUTOMATABLE` for step
  automation target menus.
- Display short target text as voice number plus descriptor short name, for
  example `1frq`, `4vol`, or another 3-character compromise that fits the
  existing screen.
- Display full target text from descriptor category and long name in edit mode.
- Map menu selection indices to canonical `instrument_param_id_t` values, with
  an explicit off entry mapped to `INSTRUMENT_PARAM_INVALID`.

The flags are capability bits on each descriptor, not storage selectors:

- `INSTRUMENT_PARAM_FLAG_MORPHABLE` means the descriptor has main/morph
  endpoints and participates in `morph_interpolation[]`.
- `INSTRUMENT_PARAM_FLAG_MODULATABLE` means the descriptor may appear in the
  runtime velocity/LFO target picker.
- `INSTRUMENT_PARAM_FLAG_AUTOMATABLE` means the descriptor may appear in
  sequencer automation target pickers.

All three flags live beside the descriptor because capability is part of the
parameter definition. They do not allocate extra storage and they do not create
a new static ordering. The stored value is still addressed by
`slot + descriptor_index`; target menus merely filter those descriptors into a
temporary user-facing selection list.

This can initially serve only `DTYPE_TARGET_SELECTION_VELO` and
`DTYPE_TARGET_SELECTION_LFO`. Step automation `DTYPE_AUTOM_TARGET` can follow
once sequencer automation storage is moved fully to descriptor IDs.

## Patch 8 - Comment and Contract Cleanup

Add in-place documentation while patching, not afterward. Priority areas:

- `InstrumentManager.h/.c`: registry ownership, menu layout resolver,
  descriptor ID packing, runtime writer/shaper contract, and supplemental
  binding behavior.
- Each `*Parameters.c`: descriptor rows and layout rows should state that file
  keys, menu text, storage cell identity, and runtime writers are owned here.
- `SceneData.h`: clarify that `instrument_parameters[]`,
  `morph_instrument_parameters[]`, and `morph_interpolation[]` are indexed by
  descriptor index for the slot's active instrument type.
- `presetManager.c`: update comments that still mention temporary legacy
  mirrors or `parameters2[]` for voice pages.
- `menu.c`: document the new resolver and why static `menuPages[][]` is no
  longer the voice-page source of truth.
- `storageTypes.c`: keep the parser comments aligned with descriptor-index
  storage and 16-bit target cells.

## Patch 9 - Verification

Minimum verification after the patches:

1. `make -B`
2. Run `python3 tools/convert_legacy_kits.py` only if descriptor order, target
   conversion, or generated target values change.
3. Add a small host-side validation script or compile-time test that:
   - Opens one generated kit folder.
   - Parses all six instrument files.
   - Verifies every file key maps to a descriptor index.
   - Verifies each instrument menu layout is a faithful descriptor-index
     translation of `menuPages.old` for Drum, Snare, Cymbal, and HiHat,
     except for cells intentionally still owned by Pattern/Scene/global code.
   - Verifies every non-empty menu layout cell maps to a valid descriptor.
   - Verifies every descriptor appears on at most one voice-page cell unless
     explicitly allowed.
4. Add a debug-only runtime probe that can be temporarily enabled after kit
   load:
   - Print or store one slot's key runtime fields after
     `preset_startDrumsetApply()`/`preset_tickDrumsetApply()` drains.
   - Check volume, waveform, oscillator coarse/fine, envelope decay, and mixer
     route.
5. Hardware smoke test:
   - Boot.
   - Navigate all voice pages and subpages.
   - Load `Kit/001 Slak`.
   - Confirm voice page values match `SD_CARD/Kit/001 Slak/*.drm|*.snr|*.cym|*.hat`.
   - Run sequencer and confirm audio.
   - Edit `instrument_vol`, envelope decay, filter frequency, and waveform from
     the voice page and confirm immediate sound change.
   - Enter `SHIFT+VOICE` morph endpoint view, edit a value, move morph, and
     confirm interpolation changes runtime sound without overwriting the main
     endpoint.

## Recommended Patch Order

1. Add descriptor-owned menu layout types and implement
   `InstrumentManager` menu lookup.
2. Add the `menu_cell_t` resolver and convert only read/navigation/display
   paths first; confirm voice pages populate.
3. Convert edit paths from pointer mutation to value/commit helpers; confirm
   voice page edits update `SceneData`.
4. Restore runtime shapers/writers for core audible params; confirm loaded kits
   produce sound.
5. Implement supplemental velocity/LFO bindings and target menus.
6. Sweep comments and stale references.
7. Run build, kit validation, and hardware smoke test.

This order gives visibility before deeper DSP repair: once the pages populate,
the loaded values can be inspected directly while fixing the runtime apply path.

## Progress Notes

### 2026-07-10 - Descriptor menu layout and Menu resolver started

- Added `instrument_menu_page_t` and menu-layout fields to
  `InstrumentManager`.
- Added descriptor-index voice layouts beside Drum, Snare, Cymbal, and HiHat
  descriptor tables. The layouts are direct translations of `menuPages.old`
  voice parameter positions, with non-instrument cells left empty for Menu or
  Scene/Pattern owners.
- Added a hihat open-page layout so `VOICE7_PAGE` maps to the open decay
  descriptor while sharing the same hihat slot as `VOICE6_PAGE`.
- Added an instrument-layout skip sentinel so Drum/Snare pitch/velocity pages
  preserve the old `TEXT_SKIP` navigation cell from `menuPages.old` without
  rendering a bogus editable value.
- Replaced the `instrumentManager_menuDescriptor()` stub with real descriptor
  lookup helpers.
- Started converting `menu.c` to a `menu_cell_t` resolver so static menu pages
  still read `menuPages[][]`, while voice pages resolve the active Scene slot's
  instrument descriptors.
- Converted generic repaint, encoder editing, endless-pot editing, visible
  checks, and second-half detection to use resolved cells.
- Build check attempted with `make -j4`, but this shell does not have `make`
  installed (`make: command not found`).

### 2026-07-10 - Runtime shaper bridge and supplemental binding pass

- Added descriptor-keyed runtime writers in `InstrumentManager.c` for the old
  DSP paths that were not simple normalized offset writes:
  oscillator coarse/fine tuning, snare noise frequency, filter frequency,
  filter resonance, filter drive, filter type, amp envelope attack/decay/slope,
  hihat closed/open decay, pitch envelope decay/slope/amount, transient
  waveform/frequency, instrument drive, LFO rate, and decimation taper.
- Implemented `INSTRUMENT_BIND_VELOCITY_AMOUNT` as a direct write to the
  velocity modulator bank.
- Implemented off/validation behavior for velocity and LFO target descriptor
  cells without rebuilding a static `modTargets[]` parameter list. Non-off
  canonical descriptor IDs remain stored in `SceneData`; applying them fully
  still requires moving `ModulationNode` from legacy `parameterArray[]`
  destinations to descriptor destinations.
- Added descriptor-target compact/full display helpers in Menu so target cells
  render from active Scene descriptors instead of indexing the old collapsed
  `modTargets[]` table.
- `git diff --check` passes for the files touched in this pass. Full-tree
  `git diff --check` is not useful in this workspace because unrelated files
  have existing CRLF/trailing-whitespace noise.

### 2026-07-10 - Slak boot-load and DSP-readthrough audit

Goal: check whether `SD_CARD/Kit/001 Slak` can fail to enter the new
descriptor-indexed Scene storage at boot, or fail to be read from that storage
into the DSP runtime structs.

Boot/load path traced:

- `main.c` initializes DSP objects first (`initDrumVoice`, `Snare_init`,
  `Cymbal_init`, `HiHat_init`, `mixer_init`, `parameterArray_init`), then
  initializes `SceneData`, scans kits, loads kit slot 0, applies the completed
  kit, and only then starts `audioCodec_init()`.
- `filesystem_requestScanKits()` populates `kit_slot_present`,
  `kit_slot_name`, and `kit_slot_open_name`. The display name may be the LFN
  (`001 Slak`), while `kit_slot_open_name` stores the FAT short name used by
  `afatfs_fopen()`. This should make `001 Slak` loadable as long as the scan
  sees either the long name or a valid generated alias beginning with `001`.
- `preset_loadDrumset(0, 0)` dispatches `FS_INTERNAL_OP_LOAD_KIT`, which calls
  `filesystem_loadKitDirectory_tick()`. That loader validates the scan cache,
  opens `Kit/<cached-open-name>/kitset.kcg`, parses all six slot records, resets
  each Scene instrument slot to the declared type, then parses each instrument
  file into `scene_get(active)->kit.instruments[slot]`.
- `menu_pollPresetStatus()` sees `PRESET_OP_KIT_LOAD` and calls
  `menu_startSoundApply()`. At boot `audioCodec_renderCount == 0`, so the
  apply is synchronous and calls `preset_sendDrumsetParameters()` before audio
  starts.
- `preset_sendDrumsetParameters()` applies Scene morph amount, applies
  per-slot kit routing and supplemental non-morph bindings, then drains
  `presetMorph_tick()`. Because boot morph amount is zero, morphable Slak
  parameters should be applied from the main `[params]` image.

Slak file validation:

- `kitset.kcg` declares six valid slots:
  drums in slots 1-3, snare in slot 4, cymbal in slot 5, hihat in slot 6.
  Each file extension matches its declared type, and audio routes `0`, `1`,
  and `2` are all valid mixer route enum values.
- A descriptor-key scan over `SD_CARD/Kit/001 Slak/*` found that every Slak
  parameter key maps to the corresponding Drum/Snare/Cymbal/HiHat descriptor.
  There are no unknown Slak keys after descriptor lookup.
- A parser-buffer bug did show up: `storage_instrumentParseLine()` used
  `char key[24]`, but hihat keys `amp_envelope_decay_closed` and
  `amp_envelope_decay_open` are 25 bytes before the terminator. Before this
  patch, those keys were truncated and treated as unknown, so Slak hihat
  closed/open decay values would not enter Scene storage or the DSP hihat
  decay fields. Fixed by adding `STORAGE_INSTRUMENT_KEY_MAX 32u` and using it
  for instrument parameter keys.
- No Slak key is longer than 31 bytes after that fix.

DSP readthrough findings:

- Core audible parameters are morphable descriptor rows, so they are applied by
  `presetMorph_tick()` through `preset_applyInstrumentRuntimeValue()` and
  `instrumentManager_writeRuntime()`.
- Non-morph supplemental bindings (`instrument_decimation`,
  `velo_mod_amount`, `velo_mod_dest`, `lfo_target_voice`,
  `lfo_target_param`) are applied from the main image in
  `preset_applyDrumsetVoice()` before the morph worker drains. `[morph]`
  values for these are intentionally ignored.
- The runtime writer now covers the old non-linear DSP setters needed by Slak:
  oscillator coarse/fine recalc, snare noise frequency, filter frequency/resonance
  /drive/type, envelope attack/decay/slope, hihat closed/open decay, pitch
  envelope decay/slope/amount, transient waveform/frequency, instrument drive,
  LFO rate, volume/pan/waveform/mix through generic offsets, and decimation.
- I did not find a remaining boot-time reason for all Slak voices to be silent
  in the kit scan, kit parse, Scene storage, boot apply, or mixer route path.
  If silence persists after the key-buffer fix, the next most likely suspects
  are hardware/audio-output path state, slider gain/ADC state, or a runtime
  writer field that still compiles differently on target than it reads here.

Verification notes:

- `make -j4` still cannot be run in this shell because `make` is unavailable.
- `git diff --check -- Core/Hardware/SD/storageTypes.c` reports CRLF/trailing
  whitespace noise across the whole file; `git diff --ignore-space-at-eol`
  shows the logical storage change is only the key-buffer define and the parser
  scratch buffer update.

### 2026-07-10 - Instrument-local descriptor enums for menu layouts

- Added instrument-local descriptor-index enums immediately above each
  `const ParamDescriptor ..._param_descriptors[]` table in Drum, Snare, Cymbal,
  and HiHat parameter files.
- Replaced the raw numeric descriptor indices in `..._menu_pages[]` with enum
  names such as `DRUM_PARAM_OSC1_PITCH_FINE` and
  `HIHAT_PARAM_AMP_ENVELOPE_DECAY_OPEN`.
- The enum names remain instrument-local rather than voice-slot-local: Drum
  slots 1-3 all use the same `DRUM_PARAM_*` descriptor indices, and Menu/Scene
  still supplies the slot identity separately.

### 2026-07-10 - Image flags for decimation and velocity amount

- Added `ROW_NOBIND_IMAGE` to Drum, Snare, Cymbal, and HiHat parameter files.
  This is for parameters that need `FLAGS_IMAGE` but do not write through an
  instrument-struct offset. Runtime apply still uses the descriptor binding
  kind.
- Switched `instrument_decimation` and `velo_mod_amount` from `ROW_NOBIND` to
  `ROW_NOBIND_IMAGE` for all four instrument types.
- Morph compatibility: these rows now have `INSTRUMENT_PARAM_FLAG_MORPHABLE`,
  so `preset_setInstrumentParameter()` accepts main/morph edits, storage parser
  keeps their `[morph]` values, and `presetMorph_tick()` applies them through
  existing runtime writers:
  `INSTRUMENT_BIND_SLOT_DECIMATION` and `INSTRUMENT_BIND_VELOCITY_AMOUNT`.
- LFO/velocity modulation target compatibility: these rows now pass
  `instrumentManager_targetValid(..., INSTRUMENT_TARGET_MODULATION)` because
  they are morphable and modulatable. The remaining limitation is the older
  `ModulationNode` destination backend: non-off descriptor targets are still
  validated/stored but not fully applied until modulation nodes are migrated
  from legacy `parameterArray[]` targets to descriptor IDs.
- Step automation compatibility: these rows now pass
  `instrumentManager_targetValid(..., INSTRUMENT_TARGET_AUTOMATION)` because
  they are automatable. Recording/playback is still limited by the current
  sequencer automation path: `preset_applyInstrumentRuntimeValueInternal()`
  ignores `recordAutomation`, `seq_recordAutomation()` still narrows
  destinations to `uint8_t`, and `AutomationNode` playback still emits legacy
  MIDI CC destinations through `midiParser_ccHandler()`. A descriptor-aware
  automation-node patch is still needed for reliable step automation playback
  of descriptor IDs.
