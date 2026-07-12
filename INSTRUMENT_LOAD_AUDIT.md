# Instrument Load Audit

Doc-only implementation plan for the root `Instrument/` pool, type-level
instrument flags, `_choke` parameter behavior, generated track-7 alternate
decay, and the Load-page single-instrument workflow.

This revision is based on the live code only. The reference/specification docs
are not used as source of truth here.

## Work Log

- Implemented slice 1: registry labels/flags, HiHat canonical
  `amp_envelope_decay` / `amp_envelope_decay_choke` keys, removal of
  `hihat_open_menu_pages[]`, generic VOICE7 `_choke` descriptor substitution,
  and storage aliases for old HiHat closed/open keys.
- Verification after slice 1: `make` succeeds. The only linker output is the
  existing nano libc syscall warnings for `_close`, `_lseek`, `_read`, and
  `_write`.
- Implemented slice 2 part A: generated slot-6/track-7 amp decay storage in
  `kit_settings_t`, SceneData accessors, Preset setter, Menu generated cell
  kind, VOICE7 non-Choke menu substitution, and optional kitset keys
  `slot6_track7_amp_envelope_decay` /
  `slot6_track7_morph_amp_envelope_decay`.
- Verification after slice 2 part A: `make` succeeds with the same existing
  nano libc syscall warnings.
- Implemented slice 3 backend: private per-type `Instrument/` scan cache,
  `filesystem_requestScanInstruments()`, Instrument cache accessors,
  `filesystem_requestLoadInstrument()`, async single-instrument loader,
  `PRESET_OP_INSTRUMENT_LOAD`, `preset_loadInstrument()`, one-slot Preset
  apply cursor, and Menu completion handling for that cursor.
- Verification after slice 3 backend: `make` succeeds with the same existing
  nano libc syscall warnings.
- Implemented slice 4 UI: boot-time `Instrument/` scan, nested Load
  Instrument state inside `LOAD_PAGE`, type/file encoder handling, immediate
  `preset_loadInstrument()` requests on selection changes, Instrument Load
  repaint, VOICE-button entry/selection, blinking destination voice, and
  Load/Save-button exit back to normal Load.
- Verification after slice 4 UI: `make` succeeds with the same existing nano
  libc syscall warnings.
- Corrected Instrument Load entry behavior: pressing a VOICE button now enters
  Instrument Load and selects/blinks the destination slot without immediately
  replacing the slot. Encoder movement in the type/file rows still performs the
  immediate load.
- Implemented slice 5 runtime dispatch: added pointer-based voice-engine entry
  points for Drum, Snare, Cymbal, and HiHat; added InstrumentManager-owned
  per-type runtime slot pools while preserving legacy native globals for their
  original slots; changed mixer LFO/filter/async/sync rendering to a six-slot
  type-dispatched loop; changed `MidiVoiceControl` trigger dispatch to
  `instrumentManager_triggerTrack()`, including track 7 as slot 6 alternate.
- Runtime host note: the coding phase did not use the exact union-shaped host
  proposed below. The live implementation uses per-type pools plus slot/type
  dispatch because that preserves old native globals for compatibility with
  remaining legacy callers while still allowing duplicate instrument types.
- Implemented slice 6 generated track-7 decay target: non-Choke slot 6 now
  swaps to the generated Scene kit setting when track 7 triggers it, restores
  the normal base/morph decay when track 6 triggers it, and exposes the hidden
  value as a Scene modulation target named `7dc`. LFO modulation uses a
  runtime-only override; velocity targeting writes the retained kit setting.
- Implemented slice 7 SD card/converter: `tools/convert_legacy_kits.py` now
  emits the new HiHat keys and slot-6 alternate decay kit settings during fresh
  conversion, has a safe upgrade fallback when the current headers no longer
  expose legacy sound `PAR_*` symbols, and populates `SD_CARD/Instrument/`.
  Running it upgraded 62 Kit files and copied 186 instrument files.
- Verification after runtime/data slices: `python3 tools/convert_legacy_kits.py`
  succeeds. `make -B` succeeds. Remaining build warnings are existing
  low-level/USB/libc warnings: unused `eraseCount` in asyncfatfs, ignored
  `packed` attribute in USB packet helpers, nano libc `_close/_lseek/_read/_write`,
  and LTO serial compilation notes.

## Executive Findings

The requested feature is larger than a browser plus parser operation.

At audit time, the Scene kit model was already generic enough to store six
slots with different `instrument_type_t` values, but the DSP runtime was still
type-fixed:

- slots 1-3 are `DrumVoice voiceArray[0..2]`
- slot 4 is the single global `snareVoice`
- slot 5 is the single global `cymbalVoice`
- slot 6/7 is the single global `hatVoice`

`instrumentManager_runtimeInstance()`, `instrumentManager_osc()`,
`instrumentManager_filter()`, `MidiVoiceControl.c`, and `mixer.c` all enforced
that layout. The coding phase has now replaced those hot paths with
InstrumentManager slot/type dispatch so a file load changes the storage image
and the audio object that trigger/render paths use.

## Current Code Anchors

- `Core/DSP/Instruments/InstrumentManager.h/.c` owns the immutable registry:
  type token, extension, descriptor table, descriptor count, menu pages, and
  menu page count.
- `Core/DSP/Instruments/*/*Parameters.c` owns descriptor keys, display text,
  menu layouts, and runtime binding offsets for each instrument type.
- `Core/Scene/SceneData.h/.c` owns `scene_t.kit`, including six
  `kit_instrument_slot_t` records. Each slot stores `type` and three 64-cell
  descriptor image arrays. It does not store filenames.
- `Core/Hardware/SD/storageTypes.c` parses `kitset.kcg` and
  `helicase.instrument` files into Scene kit storage through descriptor keys.
- `Core/Hardware/SD/filesystem.c` owns async FAT sequencing. The directory Kit
  loader already streams `kitset.kcg` plus six instrument files.
- `Core/Scene/Preset/presetManager.c` applies loaded Scene state into runtime
  DSP objects in bounded foreground chunks.
- `Core/Menu/menu.c` resolves dynamic VOICE cells from the current slot type.
  VOICE7 alternate cells now use generic `_choke` descriptor substitution or
  the generated slot-6 track-7 decay cell.
- `Core/MIDI/MidiVoiceControl.c` and `Core/DSPAudio/mixer.c` now dispatch
  triggers/rendering through InstrumentManager's active slot type instead of
  the old fixed physical type layout.

## Implementation Order

1. Add registry metadata and instrument-type selection helpers.
2. Introduce per-slot runtime hosting and type-dispatched trigger/render/apply.
3. Rename HiHat decay keys and replace the special open-hat menu with generic
   `_choke` substitution.
4. Add kit-owned generated track-7 alternate decay storage for non-Choke slot-6
   instruments.
5. Extend storage parsing and kitset settings for the generated value.
6. Add root `Instrument/` scan/load operations.
7. Add Preset single-slot load/apply completion.
8. Add the Load-page Instrument submode and button routing.
9. Update the converter and generated `SD_CARD`.
10. Build and behavior-test the whole path.

## Code Change Plan

Each block below is written so the coding phase can turn it into nearby
commentary for the new code.

### 1. Registry Type Metadata

Files:

- `Core/DSP/Instruments/InstrumentManager.h`
- `Core/DSP/Instruments/InstrumentManager.c`
- `Core/DSP/Instruments/Drum/DrumParameters.h/.c`
- `Core/DSP/Instruments/Snare/SnareParameters.h/.c`
- `Core/DSP/Instruments/Cymbal/CymbalParameters.h/.c`
- `Core/DSP/Instruments/HiHat/HiHatParameters.h/.c`

Code changes:

- Add `INSTRUMENT_FLAG_BASIC`, `INSTRUMENT_FLAG_ADVANCED`, and
  `INSTRUMENT_FLAG_CHOKE`.
- Extend `instrument_registry_entry_t` with `display_label` and `type_flags`.
- Define labels/flags beside each parameter file:
  - Drum: label `Drum`, flags `BASIC`
  - Snare: label `Snare`, flags `BASIC`
  - Cymbal: label `Cymbl`, flags `ADVANCED`
  - HiHat: label `HiHat`, flags `ADVANCED | CHOKE`
- Keep existing `type_text` values `drm`, `snr`, `cym`, `hat` for file schema
  tokens.
- Add registry iteration accessors only where necessary:
  `instrumentManager_registryCount()` and
  `instrumentManager_registryEntryAt(index)`.
- Add `instrumentManager_typeFlags(type)` and
  `instrumentManager_typeDisplayLabel(type)`.
- Add `instrumentManager_advancedCount(const kit_t *kit, uint8_t ignore_slot)`
  and `instrumentManager_typeSelectableForSlot(const kit_t *kit,
  uint8_t destination_slot, instrument_type_t candidate)`.

Description block:

- What: This gives each instrument type immutable firmware metadata for user
  display and assignment policy without adding anything to `.drm`, `.snr`,
  `.cym`, or `.hat` files.
- Why it must exist: the Load Instrument top row must show human labels defined
  by the instrument definitions, and the menu must enforce the two-Advanced
  limit without hardcoding type names in UI code.
- Why separate functions are justified: the registry array is static/private to
  `InstrumentManager.c`. Menu and filesystem need read-only registry traversal,
  but they must not duplicate the registry or learn its layout. The two helper
  functions are thin, but they are the controlled boundary around private
  immutable data. Do not add additional one-line helpers such as
  `instrumentManager_isBasic()` or `instrumentManager_isAdvanced()`; callers
  should read the bitmask from `instrumentManager_typeFlags()`.
- Inputs: instrument type, registry index, active `kit_t`, destination slot,
  candidate type.
- Outputs: const registry entry, flags, label text, Advanced count, and
  selectable/not-selectable result.
- Clients: Load Instrument menu, root Instrument scanner, storage validation,
  future kit/scene/bank tooling.
- Accessors: `scene_getConst()`, `instrumentManager_registryEntry()`, new
  registry iteration helpers.
- Affiliates: all `*Parameters.c` files, `storageTypes.c`, `filesystem.c`,
  `menu.c`.

### 2. Per-Slot Runtime Host

Files:

- `Core/DSP/Instruments/InstrumentManager.h/.c`
- `Core/DSP/Instruments/Drum/DrumVoice.h/.c`
- `Core/DSP/Instruments/Snare/Snare.h/.c`
- `Core/DSP/Instruments/Cymbal/CymbalVoice.h/.c`
- `Core/DSP/Instruments/HiHat/HiHat.h/.c`
- `Core/DSPAudio/mixer.c`
- `Core/MIDI/MidiVoiceControl.c`
- `main.c`

Code changes:

- Add an `instrument_runtime_slot_t` host in `InstrumentManager.c`, with one
  active type and a union containing `DrumVoice`, `SnareVoice`, `CymbalVoice`,
  or `HiHatVoice`.
- Add `instrumentManager_initRuntimeSlots()` and call it from the boot path in
  `main.c` where the fixed globals are currently initialized with
  `initDrumVoice()`, `Snare_init()`, `Cymbal_init()`, and `HiHat_init()`.
  `scene_initAll()` is currently later in `main.c`, so the runtime init must
  either initialize safe default slot hosts independently or be called again
  after Scene defaults are established.
- Change instrument engines to support pointer-based instance APIs:
  - Drum: initialize, trigger, async calc, sync block, pan, phase by
    `DrumVoice *` plus runtime slot index.
  - Snare: initialize, trigger, async calc, sync block, pan by `SnareVoice *`
    plus runtime slot index.
  - Cymbal: initialize, trigger, async calc, sync block, pan by
    `CymbalVoice *` plus runtime slot index.
  - HiHat: initialize, trigger, async calc, sync block, pan by `HiHatVoice *`
    plus runtime slot index and alternate-track state.
- Replace direct global references in `mixer.c` with a slot loop that asks
  InstrumentManager to:
  - dispatch LFO for slot
  - recalc filter for slot
  - run async calc for slot
  - render sync block for slot
  - read pan for slot
- Replace `voiceControl_triggerNow()` fixed dispatch with
  `instrumentManager_triggerTrack(track, note, velocity)`. Tracks 6 and 7 both
  map to runtime slot 6; the trigger call still receives the original track so
  track-7 alternate behavior is available.
- Update private helpers in `InstrumentManager.c`:
  `instrumentManager_runtimeInstance()`, `instrumentManager_osc()`,
  `instrumentManager_filter()`, `instrumentManager_ampEg()`,
  `instrumentManager_pitchEg()`, `instrumentManager_distortion()`,
  `instrumentManager_transient()`, and LFO target resolution must use the
  active runtime slot type instead of physical slot number.

Description block:

- What: This replaces the physical “three drums, one snare, one cymbal, one
  hat” runtime layout with six slot-owned runtime instances whose active engine
  type follows `scene_t.kit.instruments[slot].type`.
- Why it must exist: without it, loading a Snare into slot 1 only changes
  descriptor storage; slot 1 still triggers and renders `voiceArray[0]` as a
  Drum. The user-facing requirement says the selected instrument file is loaded
  into the selected voice slot and becomes playable there.
- Why separate functions are justified: trigger/render/apply paths need the
  same slot-to-engine dispatch. Folding that dispatch into `mixer.c`,
  `MidiVoiceControl.c`, and every runtime writer would duplicate type switches
  and make future instrument types error-prone. The pointer-based engine
  functions are not thin accessors; they move the existing engines away from
  global singleton storage so multiple slots can host the same type.
- Inputs: zero-based runtime slot, original trigger track, note, velocity,
  descriptor key/runtime binding, active Scene slot type.
- Outputs: initialized runtime slot, triggered voice, rendered sample block,
  updated runtime parameter target.
- Clients: Preset apply, Morph worker, MIDI/trigger path, mixer render loop,
  modulation and velocity target installers.
- Accessors: `scene_instrumentSlotConst()`, existing descriptor lookup APIs,
  new runtime host internals.
- Affiliates: `velocityModulators[]`, `lfo_dispatchNextValue()`,
  `mixer_audioRouting[]`, `mixer_decimation_rate[]`, `modNode_*`,
  `preset_applyInstrumentRuntimeValue()`.

Thin-function review:

- Keep `instrumentManager_runtimeInstance(slot)` because descriptor offset
  writes require a `void *` instance pointer. Its implementation becomes
  type-aware and no longer merely maps fixed slots.
- Do not add one-line public getters for every subobject unless a nonlocal
  client genuinely needs them. Prefer private InstrumentManager helpers for
  filter/LFO/pan/render internals and public trigger/render entry points for
  `mixer.c` and `MidiVoiceControl.c`.

### 3. Runtime Reset On Type Change

Files:

- `Core/DSP/Instruments/InstrumentManager.c`
- `Core/Hardware/SD/filesystem.c`
- `Core/Scene/Preset/presetManager.c`

Code changes:

- Extend `instrumentManager_resetSlot()` so it resets both Scene descriptor
  images and the runtime host instance for the requested type.
- Ensure the directory Kit loader and single-instrument loader call this before
  parsing file lines, as the current Kit loader already does for Scene storage.
- Reinstall slot-local LFO and velocity target runtime bindings after the type
  change during Preset apply.

Description block:

- What: A type reset clears stale descriptor images and initializes the matching
  runtime engine for the slot before new file values are parsed or applied.
- Why it must exist: after loading a different type, old descriptor indices and
  runtime object state can contain unrelated values. The parser intentionally
  ignores unknown keys, so reset defaults are the only source for fields not
  present in older files.
- Why separate from filesystem: filesystem knows which file is being loaded,
  but it should not know how to initialize DSP engines or descriptor defaults.
  `InstrumentManager` already owns descriptor meaning and runtime binding.
- Inputs: mutable `kit_instrument_slot_t *`, destination type, destination
  slot index if required by runtime host reset.
- Outputs: Scene slot type/images reset, runtime instance initialized.
- Clients: Kit directory loader, Instrument root loader, future kit save/load
  replacement, tests.
- Accessors: `scene_instrumentSlot()`.
- Affiliates: all instrument init functions, `presetMorphEngine`.

### 4. HiHat Key Rename And Single Menu Layout

Files:

- `Core/DSP/Instruments/HiHat/HiHatParameters.c`
- `Core/DSP/Instruments/HiHat/HiHatParameters.h`
- `Core/DSP/Instruments/InstrumentManager.c`
- `Core/Hardware/SD/storageTypes.c`
- `tools/convert_legacy_kits.py`

Code changes:

- Rename enum entries:
  - `HIHAT_PARAM_AMP_ENVELOPE_DECAY_CLOSED` to
    `HIHAT_PARAM_AMP_ENVELOPE_DECAY`
  - `HIHAT_PARAM_AMP_ENVELOPE_DECAY_OPEN` to
    `HIHAT_PARAM_AMP_ENVELOPE_DECAY_CHOKE`
- Rename descriptor file keys:
  - `amp_envelope_decay_closed` to `amp_envelope_decay`
  - `amp_envelope_decay_open` to `amp_envelope_decay_choke`
- Do not change category, long label, short label, dtype, or runtime member
  fields for those descriptor rows.
- Delete `hihat_open_menu_pages[]`.
- Keep one `hihat_menu_pages[]` that includes the base decay descriptor.
- Remove the HiHat-specific `voice_page == 6` branch from
  `instrumentManager_voicePageDescriptorIndex()`.
- Add storage aliases for old HiHat keys during parsing, or guarantee all
  shipped files are regenerated before firmware depends on the new keys. The
  safer plan is to add aliases in `storageTypes.c` so user cards with old files
  still load.

Description block:

- What: HiHat open/closed decay becomes the generic base/`_choke` pair used by
  all Choke instruments, while the visible text stays exactly as it is today.
- Why it must exist: the current two-layout HiHat special case cannot scale to
  a type-level Choke flag or future `_choke` parameters. The key rename makes
  HiHat the first user of the generic rule.
- Why the alias helper is separate: descriptor lookup must stay canonical and
  return only current file keys. Legacy compatibility is a storage concern
  because it translates old on-card text into current descriptor keys during
  load. Do not pollute `hihat_param_descriptors[]` with duplicate legacy rows.
- Inputs: descriptor key text, instrument type, current file section.
- Outputs: descriptor index for canonical key, or legacy key translated to the
  canonical descriptor index.
- Clients: storage parser, converter, menu descriptor resolver.
- Accessors: `instrumentManager_descriptorIndexByKey()`.
- Affiliates: existing SD_CARD kit files, future user-created `.hat` files,
  mod target menus.

### 5. Generic Choke Descriptor Substitution

Files:

- `Core/DSP/Instruments/InstrumentManager.h/.c`
- `Core/Menu/menu.c`

Code changes:

- Add `instrumentManager_chokeDescriptorIndexForBase(type, base_index,
  choke_index_out)`.
- Implement it by appending `_choke` to the base descriptor key and looking up
  the sibling descriptor in the same type.
- Update `menu_resolveCellAbsolute()` or its current equivalent to:
  - resolve the normal descriptor for the current subpage/position
  - if active page is VOICE7 and mapped slot is slot index `5`
  - if the slot type has `INSTRUMENT_FLAG_CHOKE`
  - if the base descriptor has a `_choke` sibling
  - return the sibling descriptor/index instead of the base descriptor/index
- Preserve `_choke` descriptors in mod target lists because descriptor
  traversal already includes all descriptors with modulation flags.

Description block:

- What: VOICE7 menu cells become generic alternate views of slot-6 base cells
  when the assigned instrument type is flagged Choke and the selected base key
  has a `_choke` sibling.
- Why it must exist: the current HiHat-only alternate menu is hardcoded by type
  and page. The requested behavior is based on an instrument flag plus key
  suffix convention.
- Why separate from menu page lookup: InstrumentManager owns descriptor key
  structure and sibling discovery; Menu owns current page/slot context. Folding
  the suffix scan into Menu would make UI code parse descriptor naming rules.
  Folding page/slot logic into InstrumentManager would require passing UI page
  concepts deeper into the registry than necessary.
- Inputs: instrument type, base descriptor index, active voice page, resolved
  slot index.
- Outputs: descriptor index for the `_choke` sibling or no substitution.
- Clients: dynamic VOICE page resolver, future target browser display.
- Accessors: `instrumentManager_descriptor()`,
  `instrumentManager_descriptorIndexByKey()`, `scene_instrumentSlotConst()`.
- Affiliates: `hihat_menu_pages[]`, target menu traversal, Scene descriptor
  images.

Thin-function review:

- `instrumentManager_chokeDescriptorIndexForBase()` is narrow but not a thin
  accessor. It owns a naming convention and descriptor lookup rule needed by
  Menu and likely by storage/save validation later.

### 6. Generated Non-Choke Track-7 Decay Storage

Files:

- `Core/Scene/SceneData.h/.c`
- `Core/Scene/Preset/presetManager.h/.c`
- `Core/Menu/menu.c`
- `Core/DSP/Instruments/InstrumentManager.h/.c`
- `Core/Hardware/SD/storageTypes.h/.c`
- `Core/Hardware/SD/filesystem.c`

Code changes:

- Extend `kit_settings_t` with a generated alternate decay value and morph
  mirror for slot 6, for example:
  - `slot6_track7_amp_envelope_decay`
  - `slot6_track7_morph_amp_envelope_decay`
- Add SceneData accessors for these two values. Do not add a general
  `scene_getKit()` accessor; `scene_get()` and the specific kit-setting
  accessors are enough.
- Add a Menu cell kind for generated kit settings, e.g.
  `MENU_CELL_KIT_SETTING`.
  The current private enum in `menu.c` has only `MENU_CELL_EMPTY`,
  `MENU_CELL_STATIC`, and `MENU_CELL_INSTRUMENT`; the new kind must be handled
  by `menu_cellDtype()`, `menu_cellDisplayValue()`, `menu_cellCommitValue()`,
  compact paint, edit paint, value formatting, and navigation emptiness checks.
- When VOICE7 resolves slot 6 and no Choke substitution exists:
  - if the current slot-6 type has descriptor key `amp_envelope_decay`, expose
    the generated kit-setting cell at that base descriptor's menu position
  - otherwise show the same cell as VOICE6
- Display text/dtype for the generated cell should borrow the base
  `amp_envelope_decay` descriptor so the page looks like the normal parameter.
- Commit for the generated cell writes main or morph kit-setting value
  depending on `voiceModeShowMorph`, requests runtime apply for slot 6, and
  optionally records automation as a Scene/kit target once target IDs are added.
- Add an InstrumentManager runtime hook so track 7 uses the generated value
  instead of base `amp_envelope_decay` when slot 6 is triggered and the
  assigned instrument is not Choke.

Description block:

- What: Slot 6 gets a kit-owned alternate track-7 amp decay value only when its
  instrument does not provide a real `_choke` descriptor but does provide
  `amp_envelope_decay`.
- Why it must exist: the request says this alternate is generated, saved with
  kit settings, has a morph mirror, can be mod-targeted, and is used for track-7
  triggers. It is not part of the instrument file and cannot occupy a real
  descriptor index.
- Why separate from descriptor images: descriptor images belong to the loaded
  instrument file. Adding a fake descriptor image would make the generated
  value appear to be part of `.drm`/`.snr`/`.cym` files and would collide with
  the existing descriptor-count contract. A distinct Menu cell kind keeps
  generated kit settings explicit.
- Inputs: active Scene index, slot 6 current type, base descriptor lookup,
  current VOICE page, main/morph edit mode, edited value.
- Outputs: kit-setting main/morph value, runtime alternate decay cache,
  mod-target identity for the generated parameter.
- Clients: Menu display/edit, Preset apply, InstrumentManager trigger path,
  storage parser/save, future Scene mod target list.
- Accessors: new SceneData kit-setting getters/setters, descriptor lookup by
  key, `scene_getVoiceMorphAmount()`.
- Affiliates: Morph worker, Scene mod target namespace, kitset parser, future
  kit save.

Thin-function review:

- The generated-setting getters/setters are acceptable because they protect a
  storage field whose layout will be serialized and mod-targeted. Avoid adding
  separate one-line helpers for “has generated decay”; fold that check into the
  Menu resolver and runtime apply where the base descriptor lookup is already
  needed.

### 7. Scene/Kit Mod Target Identity For Generated Decay

Files:

- `Core/Scene/SceneModTargets.h/.c`
- `Core/DSP/Instruments/InstrumentManager.c`
- `Core/Menu/menu.c`
- `Core/Scene/Preset/presetManager.c`

Code changes:

- Add a Scene/kit mod target ID for the generated slot-6 track-7 decay value.
- Add display text that matches the borrowed base descriptor.
- Add runtime apply for LFO/velocity modulation to write the generated
  alternate decay cache without overwriting the base instrument descriptor.
- Ensure Morph interpolation for the generated setting is applied using the
  slot-6 voice morph amount.

Description block:

- What: The generated track-7 alternate decay becomes addressable in the same
  modulation target namespace used by Scene-level targets.
- Why it must exist: the request explicitly says the generated parameter and
  its morph mirror exist as a Scene parameter so it can be mod targeted.
- Why separate from instrument target traversal: there is no real descriptor
  index for this value. It must not appear as a normal voice-local descriptor
  target because saving/loading the instrument file should not include it.
- Inputs: Scene target ID, mod source slot, LFO/velocity value, morph amount.
- Outputs: generated decay runtime value for slot 6 track 7.
- Clients: LFO dispatch, velocity target apply, target display, target picker.
- Accessors: SceneData generated-setting accessors,
  `sceneModTarget_valid()`, `sceneModTarget_step()`.
- Affiliates: `instrumentManager_updateLfoSceneDestination()`,
  `instrumentManager_applyVelocityModulationTarget()`.

### 8. Track-7 Trigger Semantics

Files:

- `Core/MIDI/MidiVoiceControl.c`
- `Core/DSP/Instruments/InstrumentManager.h/.c`
- all four instrument engine `.h/.c` files

Code changes:

- Keep public track IDs 0..6 in `voiceControl_noteOn()` and sequencing.
- Add a helper inside InstrumentManager to map track to runtime slot:
  tracks 0..4 map to slots 0..4, tracks 5 and 6 map to slot 5.
- Pass the original track into the type-dispatched trigger function.
- For Choke instruments in slot 6, track 7 selects `_choke` runtime values.
- For non-Choke instruments in slot 6, track 7 temporarily applies the
  generated alternate amp decay before triggering, then leaves the retained
  base descriptor value unchanged.
- For non-slot-6 instruments, `_choke` descriptors remain file/mod targets but
  have no special track-7 menu access.

Description block:

- What: Trigger dispatch separates the UI/sequencer track from the runtime
  instrument slot, allowing track 7 to trigger the slot-6 engine with alternate
  decay behavior.
- Why it must exist: today HiHat gets this behavior only because
  `HiHat_trigger(vel, voice - 5, note)` receives `isOpen`. Generic loading
  needs the same concept without assuming slot 6 is a HiHat.
- Why separate from `voiceControl_noteOn()`: MIDI/voice control should enqueue
  valid track events and pulse LEDs. It should not know instrument type flags,
  descriptor suffixes, or generated kit settings.
- Inputs: track, note, velocity, current slot-6 type, generated/choke decay
  values.
- Outputs: triggered runtime slot with correct base or alternate decay.
- Clients: MIDI note-on, sequencer preview, sequencer trigger playback.
- Accessors: `scene_instrumentSlotConst()`, generated kit-setting accessors,
  descriptor lookup.
- Affiliates: LED pulse behavior, active voice bitmask, `seq_triggerVoice()`.

### 9. Storage Schema Updates

Files:

- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`
- `Core/Hardware/SD/filesystem.c`

Code changes:

- Add `STORAGE_ROOT_INSTRUMENT "Instrument"`.
- Extend kitset parsing to read/write generated kit settings, likely in a
  top-level `[settings]` or explicit key namespace in `kitset.kcg`.
- Keep instrument type flags out of instrument files.
- Add HiHat legacy key aliases:
  `amp_envelope_decay_closed -> amp_envelope_decay` and
  `amp_envelope_decay_open -> amp_envelope_decay_choke`.
- Update `STORAGE_INSTRUMENT_KEY_MAX` comment and keep the buffer large enough
  for current and future `_choke` keys.
- Add helpers for instrument-file display name extraction: first eight
  characters of the file stem, printable ASCII, padded. Prefer reusing
  `storage_copyDisplayName()` rather than adding a duplicate name copier.

Description block:

- What: Storage learns the new root folder name, generated kit-setting fields,
  and legacy-to-canonical HiHat key translation while keeping instrument files
  descriptor-based.
- Why it must exist: the root Instrument browser needs a stable folder literal,
  regenerated kits need to save generated track-7 decay, and existing old
  `.hat` files would otherwise silently lose open/closed decay values after the
  key rename.
- Why separate from filesystem: `storageTypes` already owns text schema
  parsing and validation. Filesystem should keep sequencing async FAT calls and
  pass complete lines to storage helpers.
- Inputs: text lines from `kitset.kcg` and instrument files, filename strings.
- Outputs: parsed Scene kit settings, descriptor image writes, display names,
  validation statuses.
- Clients: Kit loader, Instrument loader, converter parity tests, future Kit
  save.
- Accessors: `instrumentManager_typeFromText()`,
  `instrumentManager_filenameMatchesType()`,
  `instrumentManager_descriptorIndexByKey()`.
- Affiliates: `SD_CARD/Kit`, `SD_CARD/Instrument`,
  `tools/convert_legacy_kits.py`.

### 10. Root Instrument Scanner

Files:

- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/storageTypes.h/.c`

Code changes:

- Add public request `filesystem_requestScanInstruments(cb)`.
- Add public query helpers:
  - `filesystem_instrumentCount(type)`
  - `filesystem_instrumentName(type, index)`
  - `filesystem_instrumentDisplayIndex(type, index)`
  - internal/open helper for short filename by type/index
- Add `FS_INTERNAL_OP_SCAN_INSTRUMENTS`.
- Add a per-type instrument cache storing:
  - type
  - FAT-openable short filename
  - display name, first eight stem characters
  - sort key
- Scan `Instrument/`, accept files whose extensions match registry entries,
  sort alphanumerically by name within each type, and rebuild the cache on
  request.
- Treat missing `Instrument/` as a successful empty scan, like missing `Kit/`.
- Use a bounded cache size and document the limit. The display counter may
  saturate at `999`, but RAM cache size still needs a real compile-time cap.

Description block:

- What: Filesystem gains a root `Instrument/` directory scanner and typed
  browser cache independent of numbered Kit slots.
- Why it must exist: the Load Instrument bottom row is populated by all files
  in `/Instrument/` matching the selected type, sorted alphanumerically by
  filename. Kit scanning is slot-number based and cannot provide this list.
- Why the query helpers are justified: Menu needs count/name/index display, but
  it must not access filesystem static arrays. Avoid adding
  `filesystem_instrumentSlotExists()` because instruments are list-indexed, not
  slot-indexed.
- Inputs: FAT directory entries, registry extensions, LFN/short names.
- Outputs: per-type sorted cache and display accessors.
- Clients: Load Instrument menu, single-instrument load request.
- Accessors: registry iteration helpers, `storage_copyDisplayName()`,
  FAT LFN helpers already used by Kit scan.
- Affiliates: asyncfatfs finder state, existing Kit scan LFN code, sample
  installer sort helpers.

Thin-function review:

- `filesystem_instrumentName()` and `filesystem_instrumentCount()` are thin but
  acceptable because the cache is private static storage. Do not expose the
  cache struct directly.

### 11. Single Instrument Load Operation

Files:

- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/filesystem.c`
- `Core/Scene/Preset/presetManager.h/.c`

Code changes:

- Add public request:
  `filesystem_requestLoadInstrument(destination_slot, type, browser_index, cb)`.
- Add `FS_INTERNAL_OP_LOAD_INSTRUMENT`.
- Add operation statics for destination slot, selected type, and browser index;
  do not overload the existing `op_slot` alone.
- State machine:
  1. chdir root
  2. open/chdir `Instrument/`
  3. open cached short filename for selected type/index
  4. reset destination Scene/runtime slot to selected type
  5. stream lines through `storage_instrumentParseLine()`
  6. finalize and apply main-to-morph fallback if needed
  7. return to root and finish
- Preserve `kit.settings.audio_out[]` and every other slot.
- On failure, leave a documented state. Preferred behavior: parse into the live
  destination only after reset, matching current Kit loader simplicity, and
  show an error if finalization fails. A fully transactional staging slot would
  require more RAM and broader apply changes.

Description block:

- What: Filesystem can load one selected instrument file from `/Instrument/`
  into one Scene kit slot without loading a whole Kit.
- Why it must exist: Load Instrument scroll behavior requires immediate file
  load as each instrument becomes selected.
- Why separate from generic `filesystem_requestLoad()`: the existing request
  signature has only file type and one slot byte. Instrument load needs three
  coordinates: destination voice slot, instrument type, and per-type browser
  index. Packing these into `op_slot` would hide constraints and make failures
  hard to diagnose.
- Inputs: destination slot, instrument type, sorted browser index.
- Outputs: Scene slot type/images, runtime reset/apply pending, filesystem
  completion status.
- Clients: Preset single-instrument load, Load Instrument menu.
- Accessors: instrument browser cache helpers, `scene_instrumentSlot()`,
  `storage_instrumentStateInit()`.
- Affiliates: `preset_currentName` should not be overwritten by instrument
  load; kit name remains current Kit name.

### 12. Preset Single-Slot Completion And Apply

Files:

- `Core/Scene/Preset/presetManager.h`
- `Core/Scene/Preset/presetManager.c`
- `Core/Menu/menu.c`

Code changes:

- Add `PRESET_OP_INSTRUMENT_LOAD`.
- Add `preset_loadInstrument(destination_slot, type, browser_index)`.
- Store request context: destination slot, type, browser index.
- Add completion callback `on_instrument_load_complete()`.
- Add one-slot apply API:
  - `preset_startInstrumentApply(slot)`
  - `preset_tickInstrumentApply()`
- Reuse the existing private `preset_applyDrumsetVoice(slot)` by renaming it to
  something slot-neutral, e.g. `preset_applyKitVoice(slot)`.
- Queue Morph rebuild only for the loaded slot.
- Reconcile LFO/velocity targets after the type swap so stale descriptor IDs
  become off rather than pointing at invalid local indices.
- In `menu_pollPresetStatus()`, handle `PRESET_OP_INSTRUMENT_LOAD` by starting
  the one-slot apply and repainting the Instrument Load page when complete.

Description block:

- What: PresetManager gets an async operation and apply cursor for one changed
  kit slot.
- Why it must exist: whole-Kit apply recalculates all six slots and Scene
  settings. Scrolling an instrument list can start many loads; applying all six
  voices for each selection is unnecessary foreground work and increases audio
  risk.
- Why separate from `preset_startDrumsetApply()`: whole-kit load applies Scene
  settings, all audio routes, all supplemental cells, and a scene-wide Morph
  rebuild. Instrument load changes one slot and should not reset Scene
  settings or other voices.
- Inputs: destination slot, filesystem completion status, active Scene index.
- Outputs: one runtime slot applied, one Morph slot queued/drained,
  status acknowledged.
- Clients: Load Instrument menu, filesystem completion callback.
- Accessors: `scene_instrumentSlotConst()`, `presetMorph_requestVoice()`,
  existing typed setters.
- Affiliates: `menu_storageBusy`, `menu_TargetVoiceGapIndex`, modulation
  target installers.

Thin-function review:

- Do not create a new one-line wrapper around `preset_applyDrumsetVoice()`.
  Rename it to a slot-neutral helper and call it from both whole-kit and
  single-instrument apply paths.

### 13. Load Instrument Menu State

Files:

- `Core/Menu/menu.h`
- `Core/Menu/menu.c`
- `Core/Hardware/frontPanel/buttonHandler.c`
- `Core/Hardware/ledHandler.*` if existing blink helpers are insufficient

Code changes:

- Add menu state for Instrument Load submode:
  - active/inactive
  - selected destination slot
  - selected instrument type
  - selected browser index per type or one current index
- Do not add Instrument as a normal `SAVE_TYPE_*` entry unless the bitfield
  widths are widened. `menu_saveOptions.what` is currently three bits and the
  Save/Load flow assumes the existing save types.
- Add public Menu helper for button handling:
  `menu_loadInstrumentVoicePressed(voice)` returning nonzero if consumed.
- In Load page, voice-button press enters Instrument Load, selects that voice,
  clears old blink LEDs, and blinks the selected voice LED.
- In Instrument Load:
  - top row shows `Load: <InstrumentLabel>`
  - encoder over type row steps selectable instrument types
  - if two Advanced instruments are already assigned and selected slot is not
    currently Advanced, type stepping shows only Basic instruments
  - bottom row shows `[  #]<name>` for the current type's sorted list
  - number is one-based and visually saturates at 999
  - name is first eight characters of file stem
  - encoder over bottom row immediately requests load of the newly selected
    instrument
- Pressing Load/Save again exits Instrument Load and returns to normal Load.
- While `menu_storageBusy` is set, preserve existing input block behavior.

Description block:

- What: The Load page gains a nested Instrument Load browser selected by voice
  buttons, with type selection on the top row and file selection on the bottom
  row.
- Why it must exist: the normal Load page browses numbered Kits and other file
  types. Instrument load is destination-slot aware and cannot fit the existing
  `SAVE_TYPE_*` model without widening state and disturbing save behavior.
- Why separate Menu helper is justified: ButtonHandler receives voice presses,
  but Menu owns Load-page state, selected type/index, and immediate load
  requests. A single helper avoids adding separate thin queries like
  `menu_isInstrumentLoadActive()` plus duplicated enter/select logic in
  ButtonHandler.
- Inputs: active page, pressed voice, encoder delta, edit mode, selected type,
  filesystem browser cache.
- Outputs: menu submode state, display buffer, load requests, selected voice
  blink intent.
- Clients: `buttonHandler.c`, `menu_parseEncoder()`, `menu_repaint()`,
  Preset completion polling.
- Accessors: filesystem instrument cache accessors, registry label/flags
  helpers, `scene_getConst()`.
- Affiliates: `editDisplayBuffer`, `menu_currentPresetNr[]` remains kit-only,
  `menu_storageBusy`, LED blink state.

### 14. Button Routing

Files:

- `Core/Hardware/frontPanel/buttonHandler.c`
- `Core/Menu/menu.h/.c`

Code changes:

- At the top of `handleVoiceButton()`, after copy/clear handling and before
  mute/preview logic, ask Menu whether this voice press is consumed by Load
  Instrument.
- If consumed, update active voice and blink LEDs as directed, then return
  without previewing, muting, switching VOICE pages, or applying Euklid params.
- Load/Save mode button press while Instrument Load is active should exit
  Instrument Load and stay on normal `LOAD_PAGE`.

Description block:

- What: Voice buttons become destination selectors while the Load page is in or
  entering Instrument Load mode.
- Why it must exist: the requested workflow starts Instrument Load by pressing
  a track/voice button from the Load menu and lets the user change selected
  voice with those same buttons.
- Why not fold into Menu only: physical button decoding and LED ownership live
  in ButtonHandler. Menu can own state, but ButtonHandler must prevent the
  normal preview/mute/page-switch side effects.
- Inputs: pressed voice button, current select mode, active page, Menu submode.
- Outputs: consumed/not-consumed result, active voice state, blink LED update.
- Clients: front-panel press processing.
- Accessors: `menu_loadInstrumentVoicePressed()`, `menu_getActiveVoice()`,
  `led_setBlinkLed()`.
- Affiliates: copy/clear mode, PERF mute behavior, stopped-transport preview.

### 15. Converter And Generated SD Card

Files:

- `tools/convert_legacy_kits.py`
- `SD_CARD/Kit/**`
- `SD_CARD/Instrument/**`

Code changes:

- Remove dependency on missing `param_rename.txt` by adding an explicit
  compatibility map or restoring that file. Prefer explicit map in the script
  for reviewability.
- Update HiHat descriptor keys to canonical names:
  - legacy closed value writes `amp_envelope_decay`
  - legacy open value writes `amp_envelope_decay_choke`
- Keep the script descriptor key lists sufficient for all legacy payload
  values. New descriptor rows that have no legacy source value, such as second
  LFO target cells, continue to default through firmware load/reset behavior.
- Emit kit settings for generated slot-6 track-7 decay for every generated or
  upgraded kit. Existing Choke instruments ignore the generated setting at
  trigger time, while non-Choke slot-6 replacements can use it immediately.
- Generate `SD_CARD/Instrument/`.
- Copy every generated instrument file from every kit directory into
  `SD_CARD/Instrument/`.
- Use deterministic collision-safe names when two kits contain the same
  filename, for example prefix with kit number or append a numeric suffix.
- Sort output deterministically.

Description block:

- What: The converter regenerates canonical Kit directories and builds the root
  Instrument pool from those generated instrument files.
- Why it must exist: shipped SD data currently contains old HiHat keys and no
  `Instrument/` root. The script also cannot run in this repo because
  `param_rename.txt` is missing.
- Why not parse C descriptors in Python now: a C parser would be fragile and
  oversized for this stage. An explicit table keeps conversion behavior
  reviewable and must be checked against C descriptor arrays during tests.
- Inputs: legacy flat kit files, explicit parameter rename map, current
  descriptor key lists.
- Outputs: regenerated `SD_CARD/Kit`, generated `SD_CARD/Instrument`, updated
  keys/settings.
- Clients: developer regeneration workflow, firmware SD-card tests.
- Accessors: filesystem paths only; no firmware code imports the script.
- Affiliates: storage schema, descriptor files, checked-in SD_CARD fixture.

### 16. Validation

Build checks:

- Compile after each major slice.
- Add or run any existing host/unit tests for storage parsing if available.

Manual behavior checks:

- Existing Kit load still loads all current kits.
- Old `.hat` files with `amp_envelope_decay_closed/open` still load through
  aliases; regenerated and upgraded SD files now use only canonical keys.
- HiHat in slot 6:
  - VOICE6 shows base `amp_envelope_decay`
  - VOICE7 shows `amp_envelope_decay_choke`
  - both appear separately in modulation target menus
- Non-Choke with `amp_envelope_decay` in slot 6:
  - VOICE7 shows generated alternate decay
  - generated main/morph values survive Kit reload
  - track 7 uses generated decay without changing track 6 base decay
- Non-Choke without `amp_envelope_decay` in slot 6:
  - VOICE7 mirrors VOICE6
- Choke type outside slot 6:
  - `_choke` target remains mod-accessible
  - `_choke` parameter is not exposed by VOICE7 substitution
- Instrument Load:
  - pressing voice button in Load page enters Instrument Load
  - selected voice LED blinks
  - encoder type list enforces the two-Advanced limit
  - bottom list is sorted by instrument filename
  - selecting a file loads it immediately into the selected slot
  - pressing Load/Save again returns to normal Load
- Runtime:
  - Drum, Snare, Cymbal, and HiHat can each render from each allowed slot
  - two Advanced instruments can render in two slots
  - more than two Advanced instruments cannot be selected through the menu
  - LFO and velocity targets are reconciled after type swaps

## Assumptions Carried Into Coding

- Type flags are firmware registry metadata only and will not be written into
  instrument files.
- The root Instrument pool is generated from current Kit instrument files first;
  hand-authored instrument import can come later.
- Collision-safe copying into `SD_CARD/Instrument/` is preferred over
  de-duplication because it preserves every kit's source instrument.
- The runtime host should be slot-owned, not one singleton per type, because
  Basic instruments may appear any number of times and Advanced instruments may
  appear twice.
- Generated non-Choke track-7 decay belongs to Kit settings, not instrument
  files and not global Scene settings.

## Implementation Notes: Instrument/Kit Load Refinements

Completed in this pass:

- Removed `Pattern`, `MorphKit`, `Perform`, and `All` from the public
  Load/Save enum and reduced numbered preset UI storage to the one remaining
  Kit location. The legacy persistence functions remain internally callable,
  but use Preset-private request identifiers so they cannot reappear as menu
  selections.
- Added `pat_sceneHasActiveSteps(scene_index)` as the PatternData-owned query
  used by Load Scene LEDs. It scans retained `PatternSet` step activity without
  exposing the `Step.volume` active-bit representation to Menu/front-panel
  callers.
- Routed SEQ presses through `menu_loadSceneButtonPressed()`. Kit Load now owns
  a Scene target mask and keeps the active Scene selected; Instrument Load owns
  a single destination Scene. ButtonHandler records consumed presses through
  release so a context change cannot turn the release into a normal step edit.
- Added scene-aware LED repaint: active pattern data lights a Scene LED, while
  selected Kit targets and the active/selected Instrument Scene blink. The
  helper clears only SEQ blink state, preserving unrelated LED ownership.
- Added staged Kit directory loading. `filesystem_requestLoadKitForScenes()`
  parses `kitset.kcg` and all six instruments into a private `kit_t`, then
  copies the complete result to selected Scenes only after validation succeeds.
  The ordinary boot loader now uses the same Preset entry point.
- Added retained eight-character instrument-file stems to `kit_t`. Kitset
  parsing derives them from each `file=` value; root Instrument loading updates
  the explicit Scene/slot on success. Instrument Load therefore opens on
  `kit  <name>` and does not need to invent a pool-file selection.
- Changed Instrument Load selection semantics: it enters on the type row with
  brackets, type changes are display-only, and lower-row encoder motion is the
  first action that selects and immediately loads a pool file. After a pool
  item is shown, changing type preserves that displayed item until lower-row
  movement selects a file for the new type.
- Forced build verification: `make -B` completed successfully. Existing
  toolchain/library warnings remain outside these changes.

Follow-up validation still recommended on hardware:

- Confirm Load Kit SEQ1 is lit/blinking with active pattern data and retains
  that state across Kit slot changes.
- Confirm Instrument Load opens as `[Type]` plus `kit  <slot file stem>`, and
  changing type leaves sound and lower-row source untouched.
- Confirm the first lower-row encoder step changes to the correct pool end
  (last for negative, first for positive) and loads exactly that file.
- When `SCENE_COUNT` expands, exercise multi-Scene Kit target toggles and an
  inactive-Scene Instrument load to confirm retained data changes without an
  unintended audible apply.

### Refinement Follow-Up

- Corrected Kit Load Scene selection so the active Scene is only the initial
  target. Every SEQ Scene button now toggles its own target bit, including the
  active Scene, allowing the load target set to be empty. Kit LEDs blink only
  selected targets.
- Preserved active-Scene SEQ blinking during Instrument Load. The root cause
  was ButtonHandler clearing all persistent blink LEDs immediately after Menu
  had drawn Scene status; it now clears only VOICE blink state. Instrument Load
  continues to support one selected destination Scene while the active Scene
  remains visibly blinking.
- Removed the Instrument Load `Loading instr` LCD takeover. A selected pool
  file still blocks concurrent input through `menu_storageBusy`, but the
  browser display stays visible through asynchronous read/apply completion.
- Narrowed lower-row brackets to the normal selector field: Kit source renders
  as `[kit]name`, and pooled source retains `[001]name` behavior.
- Restored stopped-transport preview for a repeated selected VOICE press while
  Instrument Load is active. Track 7 already used the ordinary preview path;
  the six Instrument destination buttons now match that behavior without
  resetting the nested load cursor.
## Parameter-Lock Transaction Follow-Up

Implemented the lifecycle correction identified in
`INSTRUMENT_LOAD_PARAM_LOCK_BUG.md`:

- root Instrument files parse into filesystem staging rather than live SceneData;
- Preset request coordinates publish only after request acceptance;
- active commit clears outgoing modulation ownership before slot type mutation;
- the incoming runtime is reset, all six Morph/runtime images are rebuilt, and
  all six normalized source target relationships are reinstalled;
- Instrument Load blocks encoder, Scene, VOICE preview/selection, and mode
  changes until the read-plus-apply transaction completes.

Verification: `make -j4` succeeds. Hardware stress testing remains pending.
