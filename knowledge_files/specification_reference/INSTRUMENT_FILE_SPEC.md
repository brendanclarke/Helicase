# Instrument and Kit File Specification

Session 032 reference. This document has been folded into
`knowledge_files/specification_reference/FILESYSTEM_SPEC.md`, which is now the
authoritative source for current filesystem, kit/instrument file, Scene
storage, menu descriptor, runtime apply, and future save/load decisions. Keep
this file only as a Session 032 source reference until it is deleted later.

## Status

- Normal root `Kit/NNN Name/` kit loading is directory-based.
- Instrument files are descriptor-keyed text files under each kit folder.
- Instrument parameter storage is Scene-owned and descriptor-indexed.
- Voice menu pages are generated from the active instrument descriptor layouts
  in `Core/DSP/Instruments/*/*Parameters.c`.
- The sequencer triggers voices and audio is produced after the Session 032
  runtime shaper/binding repair.
- Hardware testing after Session 032 reports that Morph does not work.
- LFO/velocity modulation target assignments and step automation assignments do
  not yet work reliably for descriptor IDs. They remain follow-up work.

## SD Card Shape

Normal kits use the Phase 2 root directory layout:

```text
SD_CARD/
  Kit/
    001 Slak/
      kitset.kcg
      slakd1.drm
      slakd2.drm
      slakd3.drm
      slaks1.snr
      slakc1.cym
      slakh1.hat
```

Folder naming:

- Preferred: `NNN Name`, for example `001 Slak`.
- Compatibility accepted by parser: `NNN_Name`.
- `NNN` is one-based on disk and maps to zero-based internal preset slot
  `NNN - 1`.
- The kit display name comes from the folder name, not from `kitset.kcg`.
- The scan cache stores both display name and FAT open name. Long filenames are
  display-only; asyncfatfs opens the cached short alias in the current directory.
- A short-alias fallback accepts FAT aliases beginning with a valid three-digit
  slot prefix, such as `001SLA~1`.

Morph kit loads (`FS_FILE_MORPH`) remain legacy `.SND` until a final instrument
morph save/load policy replaces them.

## `kitset.kcg`

`kitset.kcg` is the six-slot kit manifest. It owns slot membership, instrument
file names, instrument types, and audio output routing.

Example:

```text
format=helicase.kitset
version=1

[slot1]
type=drm
file=slakd1.drm
audio_out=2

[slot2]
type=drm
file=slakd2.drm
audio_out=0

[slot3]
type=drm
file=slakd3.drm
audio_out=0

[slot4]
type=snr
file=slaks1.snr
audio_out=0

[slot5]
type=cym
file=slakc1.cym
audio_out=0

[slot6]
type=hat
file=slakh1.hat
audio_out=1
```

Required top-level fields:

- `format=helicase.kitset`
- `version=1`

Required per-slot fields:

- `[slot1]` through `[slot6]`
- `type=drm|snr|cym|hat`
- `file=<8.3 instrument filename>`
- `audio_out=<0..5>`

Validation rules:

- All six slots must be present.
- Every slot must declare type, file, and audio route.
- File extension must match declared type:
  `.drm`, `.snr`, `.cym`, or `.hat`.
- `audio_out` is clamped on apply if it exceeds `MIXER_ROUTING_DAC2_R`.

`kitset.kcg` does not own:

- Kit name.
- Pattern data.
- MIDI channel or MIDI note.
- Scene settings.
- Instrument parameter values.

## Instrument Files

Instrument files are text key/value files with a fixed header and one or two
parameter sections.

Example:

```text
format=helicase.instrument
version=1
type=drm

[params]
osc1_wave=0
osc1_pitch_coarse=31
osc1_pitch_fine=126
instrument_vol=127
instrument_pan=63

[morph]
osc1_wave=0
osc1_pitch_coarse=31
osc1_pitch_fine=126
instrument_vol=127
instrument_pan=63
```

Required top-level fields:

- `format=helicase.instrument`
- `version=1`
- `type=drm|snr|cym|hat`

Section rules:

- `[params]` contains the main endpoint.
- `[morph]` contains the morph endpoint for morphable descriptor rows.
- Missing `[morph]` is allowed; the loader copies main values into morph values
  for descriptors flagged morphable.
- Unknown keys are skipped for forward compatibility.
- Known keys must parse as `uint8_t`, except descriptor target cells
  `velo_mod_dest` and `lfo_target_param`, which parse as `uint16_t`.
- `lfo_target_voice` is clamped during parse into `1..6`; converted legacy kits
  may contain zero.

Instrument file metadata deliberately does not include:

- `slot`
- `kit_name`
- `source_name`
- `source_file`

The slot comes from `kitset.kcg`. The kit name comes from the kit folder.

## Canonical Instrument Keys

The physical SD-card key vocabulary lives in each instrument descriptor table:

- `Core/DSP/Instruments/Drum/DrumParameters.c`
- `Core/DSP/Instruments/Snare/SnareParameters.c`
- `Core/DSP/Instruments/Cymbal/CymbalParameters.c`
- `Core/DSP/Instruments/HiHat/HiHatParameters.c`

Current descriptor counts:

- Drum: 35 descriptors.
- Snare: 34 descriptors.
- Cymbal: 35 descriptors.
- HiHat: 35 descriptors.

Descriptor key lookup is type-local. The same key may exist in multiple
instrument types but resolves against the loaded slot type.

Current keys by family:

- Oscillator and noise: `osc1_wave`, `osc1_pitch_coarse`,
  `osc1_pitch_fine`, `osc2_wave`, `osc2_pitch_coarse`,
  `osc2_mod_amount`, `osc2_mod_type`, `osc3_wave`,
  `osc3_pitch_coarse`, `osc3_mod_amount`, `noise_freq`,
  `osc1_noise_mix`.
- Filter: `filter_freq`, `filter_reso`, `filter_drive`, `filter_type`.
- Amp envelope: `amp_envelope_attack`, `amp_envelope_decay`,
  `amp_envelope_decay_closed`, `amp_envelope_decay_open`,
  `amp_envelope_slope`, `amp_attack_repeat`.
- Pitch envelope: `pitch_envelope_decay`, `pitch_envelope_amount`,
  `pitch_envelope_slope`.
- Voice: `instrument_vol`, `instrument_pan`, `instrument_drive`,
  `instrument_decimation`.
- LFO: `lfo_rate`, `lfo_amount`, `lfo_wave`, `lfo_retrigger_voice`,
  `lfo_sync`, `lfo_offset`, `lfo_target_voice`, `lfo_target_param`.
- Velocity: `velo_vol_on_off`, `velo_mod_amount`, `velo_mod_dest`.
- Transient: `transient_wave`, `transient_vol`, `transient_freq`.

The converter provides legacy compatibility by regenerating text files from
legacy `.SND` payloads. Firmware does not keep old text-key aliases.

## Descriptor Ownership

`ParamDescriptor` is the source of meaning for an instrument parameter:

- SD-card key.
- Short menu label.
- Long edit label.
- Category label.
- Display dtype.
- Capability flags.
- Runtime binding kind and offset/type payload.

The descriptor arrays live next to each instrument implementation. They own
instrument-local meaning. `InstrumentManager` is the registry/lookup layer, not
the owner of parameter text or per-instrument page layout.

Descriptor flags:

- `INSTRUMENT_PARAM_FLAG_MORPHABLE`: the descriptor has main and morph endpoint
  values and participates in the morph worker.
- `INSTRUMENT_PARAM_FLAG_MODULATABLE`: the descriptor is allowed in
  velocity/LFO target pickers.
- `INSTRUMENT_PARAM_FLAG_AUTOMATABLE`: the descriptor is allowed in step
  automation target pickers.

Normal `ROW` and `ROW_MENU` descriptors use `FLAGS_IMAGE`:

```c
INSTRUMENT_PARAM_FLAG_MORPHABLE |
INSTRUMENT_PARAM_FLAG_MODULATABLE |
INSTRUMENT_PARAM_FLAG_AUTOMATABLE
```

`ROW_NOBIND` descriptors have `flags=0` and are single-endpoint supplemental
values. Target selectors remain `ROW_NOBIND`.

`ROW_NOBIND_IMAGE` is used for image parameters that are morphable/modulatable
/automatable but do not write through an instrument-struct offset:

- `instrument_decimation`
- `velo_mod_amount`

These apply through binding kinds instead:

- `INSTRUMENT_BIND_SLOT_DECIMATION`
- `INSTRUMENT_BIND_VELOCITY_AMOUNT`

## Scene Storage

Each `scene_t` owns one `kit_t`. Each `kit_t` owns six
`kit_instrument_slot_t` records.

Each instrument slot owns:

- `type`
- `parameter_images.instrument_parameters[64]`
- `parameter_images.morph_instrument_parameters[64]`
- `parameter_images.morph_interpolation[64]`

Arrays are indexed by descriptor index for the slot's current instrument type.
Descriptor index `0` is valid; empty menu cells use `INSTRUMENT_MENU_EMPTY`
(`0xff`) and skip cells use `INSTRUMENT_MENU_SKIP` (`0xfe`).

Canonical packed instrument parameter IDs are:

```c
id = slot * INSTRUMENT_PARAM_COUNT + descriptor_index
```

Current bounds:

- `INSTRUMENT_SLOT_COUNT`: 6.
- `INSTRUMENT_PARAM_COUNT`: 64.
- Voice parameter IDs: 0..383.
- Higher IDs remain reserved for later FX/general parameter address space.

`morph_interpolation[]` is runtime-derived state and is not serialized.

## Load Path

Boot normal-kit load path:

1. `main.c` initializes DSP objects.
2. `scene_initAll()` initializes Scene storage.
3. `filesystem_initCardAndMountBlocking()` mounts the card.
4. `filesystem_requestScanKits()` scans `Kit/` into the kit-slot cache.
5. `preset_loadDrumset(0, 0)` requests normal kit slot 0.
6. `filesystem_loadKitDirectory_tick()` opens the cached kit folder, parses
   `kitset.kcg`, resets each destination Scene instrument slot to the declared
   type, then parses each listed instrument file into descriptor-indexed Scene
   storage.
7. Completion callback sets `PRESET_OP_KIT_LOAD`.
8. `menu_pollPresetStatus()` starts sound apply.
9. Before audio starts, `menu_startSoundApply()` calls
   `preset_sendDrumsetParameters()` synchronously.

Runtime kit loads use the same Scene-owned apply logic, but the post-load apply
is chunked through `preset_startDrumsetApply()` /
`preset_tickDrumsetApply()` to avoid foreground bursts after audio is running.

## Runtime Apply Path

Loaded or edited descriptor values are applied through Preset and
InstrumentManager:

- Menu instrument-cell edits call `preset_setInstrumentParameter()` when the
  descriptor is morphable.
- Non-morphable cells call `preset_setSupplementalParameter()`.
- `presetMorph_tick()` calls `preset_applyInstrumentRuntimeValue()` for each
  morphable descriptor.
- `preset_applyInstrumentRuntimeValue()` resolves the slot type and descriptor
  and calls `instrumentManager_writeRuntime()`.
- `instrumentManager_writeRuntime()` applies either a runtime instance offset
  or a supplemental binding kind.

Runtime writer coverage added in Session 032:

- Oscillator coarse/fine rows update `OscInfo.midiFreq` high/low bytes and call
  `osc_recalcFreq()`.
- Snare `noise_freq` writes the noise oscillator frequency.
- Filter frequency/resonance/drive/type use the old value shapers/setters.
- Filter type preserves the old `value + 1` rule so DSP type `0` remains off.
- Amp envelope attack/decay/slope use envelope setters.
- HiHat closed/open decay use `slopeEg2_calcDecay()`.
- Pitch envelope decay/slope/amount use the existing pitch-envelope semantics.
- Transient waveform/frequency use transient setter/old pitch formula.
- Instrument drive uses `setDistortionShape()`.
- LFO rate uses `lfo_setFreq()`.
- Decimation writes `mixer_decimation_rate[slot]` through the old taper.
- Velocity amount writes `velocityModulators[slot].amount`.

Simple linear fields still use the generic offset writer.

## Voice Menu Pages

Static non-voice pages still use `Core/Menu/menuPages.h`.

Voice pages are now dynamic descriptor cells:

- `VOICE1_PAGE` through `VOICE3_PAGE`: Drum layout.
- `VOICE4_PAGE`: Snare layout.
- `VOICE5_PAGE`: Cymbal layout.
- `VOICE6_PAGE`: HiHat closed layout.
- `VOICE7_PAGE`: HiHat open layout, still editing the same hihat slot.

The menu resolver produces a `menu_cell_t`:

- `MENU_CELL_STATIC`
- `MENU_CELL_INSTRUMENT`
- `MENU_CELL_EMPTY`

Instrument cells carry:

- slot
- descriptor index
- descriptor pointer

Display text comes from the descriptor:

- normal view: `short_name`
- edit view: `category` and `long_name`
- dtype: `descriptor->dtype`

Values come from Scene storage:

- normal voice mode: `instrument_parameters[]`
- `SHIFT+VOICE` morph endpoint view: `morph_instrument_parameters[]`

The layouts are stored in instrument files as `instrument_menu_page_t` arrays.
They use instrument-local enum names such as `DRUM_PARAM_OSC1_PITCH_FINE`, not
raw descriptor numbers and not voice-instance-specific names.

Non-instrument cells from old voice pages, such as track MIDI channel/note,
pattern length, or audio output, are not forced into instrument files. They
remain owned by Menu/Scene/Pattern areas.

## Target Menus, Modulation, and Automation

Target selection uses canonical descriptor IDs, not legacy `modTargets[]`
indices. Target display helpers enumerate active Scene descriptors and filter
by descriptor flags.

Current working state:

- Off targets use `INSTRUMENT_PARAM_INVALID` (`65535`).
- Target menu display can show descriptor-based targets.
- Target cells can store descriptor IDs in Scene storage.
- `instrumentManager_targetValid()` validates by descriptor flags.

Current limitations:

- `ModulationNode` still uses legacy `parameterArray[]` destination pointers.
- Non-off descriptor LFO/velocity destinations are validated/stored but not
  fully applied to DSP runtime targets.
- `AutomationNode` still plays back by emitting legacy MIDI CC/CC2 through
  `midiParser_ccHandler()`.
- `preset_applyInstrumentRuntimeValueInternal()` currently ignores its
  `recordAutomation` argument.
- `seq_recordAutomation()` still accepts/narrows destination as `uint8_t`.

Therefore, descriptor target assignment, LFO/velocity modulation, and step
automation require a descriptor-aware `ModulationNode` and `AutomationNode`
follow-up. Session 032 hardware testing confirms these are not working.

## Known Session 032 Gaps

1. Morph is currently broken on hardware despite the intended Scene
   main/morph/interpolation path. Next session should instrument:
   - menu main/morph image writes
   - `scene->settings.morph_amount`
   - `presetMorph_request()` / `presetMorph_tick()`
   - `morph_interpolation[]`
   - `instrumentManager_writeRuntime()` calls
   - one audible runtime field such as volume or filter frequency
2. LFO/velocity target assignment is descriptor-aware at storage/display level
   but not at runtime modulation level.
3. Step automation is descriptor-aware at PatternData storage width level, but
   record/playback still uses legacy CC-oriented automation nodes.
4. `make` was unavailable in the Session 032 shell, so final compile validation
   must be run in the normal firmware build environment.
5. `git diff --check` on the full tree remains noisy because of unrelated
   CRLF/trailing-whitespace churn; focused checks on touched files passed.

## Verification Anchors

Recommended next-session checks:

- Build with `make -B` and `make img`.
- Boot with `SD_CARD/Kit/001 Slak`.
- Confirm voice pages populate from descriptors.
- Confirm Slak file values are visible on voice pages.
- Confirm loaded voices produce audio.
- Confirm editing `instrument_vol`, filter frequency, envelope decay, and
  waveform changes sound immediately.
- Debug Morph using one simple audible parameter.
- Debug descriptor modulation/automation only after Morph is understood.
