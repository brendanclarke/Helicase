# Helicase SD Card Filesystem Specification

This is the authoritative filesystem and instrument-file reference for the
Helicase/LXR-02 firmware after Session 032. It folds in the Session 032
instrument/kit file decisions that were previously captured in
`INSTRUMENT_FILE_SPEC.md`.

Use this document to distinguish three things:

- Implemented now: root `Kit/NNN Name/` directory loading into Scene-owned,
  descriptor-indexed instrument parameter images.
- Settled target shape: Bank, Scene, Kit, Pattern, Sample, Wavetable, Effect,
  Instrument, and `settings.cfg` filesystem layout.
- Not implemented yet: new-format saves, Scene loads/saves, Bank loads/saves,
  Effect loads/saves, root `settings.cfg`, and final new-format Morph save/load
  behavior.

Historical session logs and drafts may describe older flat `.SND`/`GLO.CFG`
behavior. This file is the current source of truth for the intended filesystem
and current implemented state.

## Current Implementation Status

Implemented after Session 032:

- Normal kit loading scans root `Kit/` for numbered folders.
- Preferred kit folder names are `NNN Name`, for example `001 Slak`.
- Compatibility kit folder names with a single underscore after the slot,
  `NNN_Name`, are accepted.
- FAT short-alias fallback accepts aliases beginning with a valid three-digit
  slot prefix, such as `001SLA~1`.
- The kit display name is the folder name after the three-digit slot prefix.
- `kitset.kcg` is parsed as the six-slot kit manifest.
- Six descriptor-keyed instrument text files are loaded from the kit folder.
- Loaded instrument values write into the active `scene_t.kit` descriptor
  images, not into the old flat `parameter_values[]` sound buffer.
- VOICE menu pages resolve through active instrument descriptor layouts in
  `Core/DSP/Instruments/*/*Parameters.c`.
- Preset/InstrumentManager applies descriptor image values back into the DSP
  runtime after load and menu edits.

Current bridges and limitations:

- `SCENE_COUNT` is currently `1`; the API is indexed so the Bank implementation
  can raise it to 17 later.
- Pattern/container storage remains a bridge shape and will be replaced by the
  later dynamic stack Pattern implementation.
- `FS_FILE_KIT` save still writes the legacy flat `.snd` format. New Kit folder
  save has not been implemented.
- `FS_FILE_MORPH` load/save still uses the legacy `.SND` morph-kit path.
- Globals still load/save through legacy `glo.cfg`; root `settings.cfg` is the
  settled future replacement but is not wired yet.
- Scene, Bank, Effect, Instrument-pool, Wavetable-pool, and new Pattern-pool
  load/save operations are not implemented yet.
- Descriptor Morph is known broken on hardware after Session 032.
- Descriptor-backed LFO/velocity modulation target assignment and step
  automation target assignment are stored/displayed but not yet applied through
  descriptor-aware runtime modulation/automation nodes.

## Root Layout

Settled target root directories:

```text
Bank/
Scene/
Kit/
Pattern/
Sample/
Wavetable/
Effect/
Instrument/
```

Settled target root file:

```text
settings.cfg
```

`settings.cfg` replaces legacy `GLO.CFG`/`glo.cfg` as the future system-settings
file. It stores system-level settings and a reference to the last loaded bank.
At boot, the future firmware should load the bank recorded there.

Current code note: boot still loads legacy `glo.cfg`. Do not document
`settings.cfg` as implemented until `FS_FILE_GLOBALS` has moved off `glo.cfg`.

Root-level entries outside the recognized list are ignored by normal
loader/browser code.

## Numbered Folders

`Bank`, `Scene`, `Kit`, and `Wavetable` contain meaningful numbered
subdirectories. Numbered folders use this form:

```text
001 <name>
002 <name>
003 <name>
...
```

The numeric prefix is the slot number shown in the UI. Numbers do not need to
be contiguous. Browsers should scan slots sequentially and show missing slots as
empty, for example `003: Empty` when slot 3 has no matching folder.

Names after the numeric prefix are user-facing labels. The preferred separator
after the three-digit slot number is a space, as in `001 Slak`, but loaders may
accept an underscore for compatibility with older generated folders, as in
`001_Slak`. Spaces inside the displayed name are valid. The numeric prefix is
authoritative for slot order; folders should not be sorted only by full
filename.

For root kits, `NNN` is one-based on disk and maps to zero-based internal preset
slot `NNN - 1`.

## Bank

Status: settled target, not implemented.

`Bank/` contains bank folders:

```text
Bank/
  001 <bank name>/
  002 <bank name>/
```

A bank represents all non-global data loaded as one performance set. The last
loaded bank is recorded in future `settings.cfg`.

Each bank folder contains exactly one bank-level config file:

```text
bankset.bcg
```

`bankset.bcg` stores bank-level metadata/configuration. It also acts as the
validator, guard, and version marker for identifying a folder as a bank. A
folder without a valid `bankset.bcg` must not be loaded as a bank.

Each bank folder also contains up to 16 scene folders:

```text
Bank/001 <bank name>/
  bankset.bcg
  001 <scene name>/
  002 <scene name>/
  ...
  016 <scene name>/
```

Scene slot numbers inside a bank do not need to be contiguous. Missing scene
slots are shown as empty in the UI. A user may exchange scene folders between
banks.

Future background bank loading uses 17 resident `scene_t` slots: 16 bank scene
slots plus one landing/staging scene so the currently playing scene can keep
playing while a new bank streams in.

## Scene

Status: Scene storage exists in SRAM for the active scene; Scene file
load/save is not implemented.

`Scene/` is a root-level pool of user-copyable scene folders:

```text
Scene/
  001 <scene name>/
  002 <scene name>/
```

Scene folders in this pool can be loaded into a bank scene slot. They use the
same folder structure as scene folders inside a bank.

A scene folder contains:

```text
sceneset.scg
Kit <kit name>/
pattern.pat
effect.fx
```

`sceneset.scg` stores scene-level metadata/configuration and validates the
folder as a scene.

`Kit <kit name>/` is the scene's embedded kit directory. It works like a kit
folder but is named without a numeric slot prefix because it belongs to the
scene. The word after `Kit` is the kit name. The kit name is not stored in
`kitset.kcg`, `sceneset.scg`, or any other metadata field.

`pattern.pat` stores the scene's pattern data.

`effect.fx` stores the scene's effect settings and effect automation sequence.

Current `scene_t` ownership:

- `scene_settings_t settings`
- `PatternSet pattern`
- `kit_t kit`

Current `scene_settings_t` fields:

- `morph_amount`
- `voice_decimation_all`
- `midi_channel[NUM_TRACKS]`
- `midi_note[NUM_TRACKS]`

Future Scene file work should move scene-level metadata and settings into
`sceneset.scg`, including MIDI note/channel and `voice_decimation_all`. These
do not belong in `kitset.kcg` or instrument files.

## Kit

Status: root Kit folder load is implemented; new-format Kit save is not
implemented.

`Kit/` is a root-level pool of numbered kit folders:

```text
Kit/
  001 <kit name>/
  002 <kit name>/
```

Kit folders can be loaded into the active scene. Slot numbers do not need to be
contiguous, and missing slots are shown as empty in the UI.

A kit folder contains:

```text
kitset.kcg
<instrument 1>.<type>
<instrument 2>.<type>
<instrument 3>.<type>
<instrument 4>.<type>
<instrument 5>.<type>
<instrument 6>.<type>
```

`kitset.kcg` is the kit folder guard/version file plus the six-slot instrument
manifest. The kit name comes only from the folder name:

- Root kit pool: `Kit/NNN <kit name>/`
- Scene embedded kit: `Kit <kit name>/`

The kit name is never stored inside `kitset.kcg`.

Users should not copy instrument files into a kit folder manually. Users may
copy instrument files out of a kit folder into the root `Instrument/` pool.
Kit membership is controlled by `kitset.kcg`.

Initial instrument file types:

```text
.drm  drum
.snr  snare
.cym  cymbal
.hat  hi-hat
```

These correspond to the four existing original LXR instrument types. Additional
instrument types may be added later.

Future Kit save behavior: saving a Kit writes a folder in this same shape:
`kitset.kcg` plus six descriptor-keyed instrument files. No new-format save
operations have been implemented yet.

### `kitset.kcg`

`kitset.kcg` owns only:

- Format/version validation.
- Slot membership.
- Per-slot instrument type.
- Per-slot instrument filename.
- Per-slot audio output routing.

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
- File extension must match declared type: `.drm`, `.snr`, `.cym`, or `.hat`.
- `audio_out` is clamped on apply if it exceeds `MIXER_ROUTING_DAC2_R`.

`kitset.kcg` does not own:

- Kit name.
- Pattern data.
- MIDI channel or MIDI note.
- Scene settings.
- `voice_decimation_all`.
- Instrument parameter values.
- Instrument morph endpoint values.

## Instrument Files

Status: implemented for load from root Kit folders; save not implemented.

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

The converter provides legacy compatibility by regenerating text files from
legacy `.SND` payloads. Firmware does not keep old text-key aliases.

Morph kit loads (`FS_FILE_MORPH`) remain legacy `.SND` until final new-format
morph save/load policy replaces them.

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

`ROW_NOBIND_IMAGE` is used for image parameters that are
morphable/modulatable/automatable but do not write through an instrument-struct
offset:

- `instrument_decimation`
- `velo_mod_amount`

These apply through binding kinds instead:

- `INSTRUMENT_BIND_SLOT_DECIMATION`
- `INSTRUMENT_BIND_VELOCITY_AMOUNT`

## Scene Instrument Storage

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

Canonical packed instrument parameter IDs:

```c
id = slot * INSTRUMENT_PARAM_COUNT + descriptor_index
```

Current bounds:

- `INSTRUMENT_SLOT_COUNT`: 6.
- `INSTRUMENT_PARAM_COUNT`: 64.
- Voice parameter IDs: `0..383`.
- Higher IDs remain reserved for later FX/general parameter address space.

`morph_interpolation[]` is runtime-derived state and is not serialized.

## Current Kit Load Path

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
- Simple linear fields still use the generic offset writer.

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

## Morph, Modulation, and Automation

Morph values are user-facing 0..255 parameters. Menu edits and future file
storage should preserve that 0..255 range. MIDI CC and step automation are
7-bit input paths; they need explicit conversion into the morph range:

- Input `0..126` maps to `value * 2`.
- Input `127` maps to `255`, so the endpoint is reachable.

Current descriptor Morph state:

- Instrument files can carry `[morph]` endpoint values.
- Missing `[morph]` copies main endpoint values into morph endpoint values.
- Scene instrument slots store main endpoint, morph endpoint, and derived
  interpolation images.
- Hardware testing after Session 032 reports descriptor Morph is not working.

Target selection uses canonical descriptor IDs, not legacy `modTargets[]`
indices. Target display helpers enumerate active Scene descriptors and filter
by descriptor flags.

Current working target state:

- Off targets use `INSTRUMENT_PARAM_INVALID` (`65535`).
- Target menu display can show descriptor-based targets.
- Target cells can store descriptor IDs in Scene storage.
- `instrumentManager_targetValid()` validates by descriptor flags.

Current target limitations:

- `ModulationNode` still uses legacy `parameterArray[]` destination pointers.
- Non-off descriptor LFO/velocity destinations are validated/stored but not
  fully applied to DSP runtime targets.
- `AutomationNode` still plays back by emitting legacy MIDI CC/CC2 through
  `midiParser_ccHandler()`.
- `preset_applyInstrumentRuntimeValueInternal()` currently ignores its
  `recordAutomation` argument.
- `seq_recordAutomation()` still accepts/narrows destination as `uint8_t`.

Therefore, descriptor target assignment, LFO/velocity modulation, and step
automation require descriptor-aware `ModulationNode` and `AutomationNode`
follow-up work.

## Pattern

Status: current pattern load/save is a bridge shape; final storage is deferred
to the dynamic stack Pattern implementation.

`Pattern/` is a root-level pool of pattern files:

```text
Pattern/
  <pattern name>.pat
```

Files are browsed alphanumerically. A pattern file can be loaded into a scene.
Users may copy a scene's `pattern.pat` into this pool, and may copy a pool
pattern into a scene if they rename it to `pattern.pat`.

Current bridge notes:

- Live `NUM_PATTERN` is 1.
- Pattern files still serialize a bridge format derived from the old layout.
- The old single global shuffle byte is ignored/omitted.
- Per-track shuffle extension data is the only live shuffle storage.
- Final interchange migration/backfill should happen in external converters
  once the final Pattern storage shape settles.

## Sample

Status: legacy sample/loop install path exists; this target root pool naming is
part of the future typed layout.

`Sample/` contains an alphanumerically sorted list of `.wav` files to write to
flash:

```text
Sample/
  <sample name>.wav
```

Samples play from normal oscillators. Looping is an oscillator-level option,
not a directory-level distinction.

## Wavetable

Status: settled target, not implemented.

`Wavetable/` contains numbered wavetable folders:

```text
Wavetable/
  001 <wavetable name>/
  002 <wavetable name>/
```

Each wavetable folder contains an alphanumerically sorted set of `.wav` files:

```text
Wavetable/001 <wavetable name>/
  <sample a>.wav
  <sample b>.wav
  <sample c>.wav
```

Wavetables are loaded during the sample-load process and written to flash. They
behave like normal samples in storage, but are only read by wavetable
oscillators. A wavetable oscillator operates on one wavetable at a time and can
be modulated across all samples inside that wavetable. Wavetable samples always
play looped. The menu shows the wavetable name when selecting the wavetable
used by the oscillator.

## Effect

Status: settled target, not implemented.

`Effect/` is a root-level pool of effect files:

```text
Effect/
  <effect name>.fx
```

Files are browsed alphanumerically. An effect file can be loaded into a scene.
Users may copy a scene's `effect.fx` into this pool, and may copy a pool effect
into a scene if they rename it to `effect.fx`.

Scene `effect.fx` stores the scene's effect settings and effect automation
sequence. Effects and effect file formats are future DSP work.

## Instrument

Status: settled target, not implemented as a browser/load/save pool.

`Instrument/` is a root-level pool of instrument files:

```text
Instrument/
  <instrument name>.<type>
```

Files are browsed alphanumerically by type when loading into a kit in a scene.
Users may copy instrument files from a kit folder into this pool. Users should
not copy files from this pool directly into a kit folder; kit membership is
controlled by `kitset.kcg`.

Initial recognized instrument types:

```text
.drm
.snr
.cym
.hat
```

## Save Operations

New-format save operations have not been implemented yet.

Settled future behavior:

- Kit save writes a `Kit/NNN <kit name>/` folder in the same shape the current
  loader already accepts: `kitset.kcg` plus six instrument files.
- Scene save writes `sceneset.scg`, `Kit <kit name>/`, `pattern.pat`, and
  `effect.fx`.
- Bank save writes `bankset.bcg` plus up to 16 numbered Scene folders.
- Instrument save writes descriptor-keyed text files.
- Pattern save writes the final dynamic-stack pattern format once implemented.
- Effect save writes the selected effect stack/settings format once effects
  exist.
- `settings.cfg` save writes system/global settings and the last loaded bank.

The current legacy save paths are implementation leftovers and should not be
used as the new-format specification.

## Debounced Autosave and Reload Target

Status: settled target, not implemented.

Future debounced autosave applies to files inside a loaded bank:

- Per-instrument files.
- Scene `effect.fx`.
- Scene `pattern.pat`.
- `sceneset.scg`.
- Embedded kit `kitset.kcg`.
- `bankset.bcg` as needed.

Root-library files and folders are explicit load/save/copy/import only, not
autosaved.

Mechanism:

- A parameter edit marks its owning file stale and starts or resets a 5-second
  idle timer.
- If edits continue for 30 seconds without a 5-second gap, force a write.
- Debounced writes go to the live working file.
- A dot-shadow file keeps the last state committed by explicit menu SAVE.
- RELOAD restores the working file from the dot-shadow.
- Writes should use a `.tmp` file and replace the live file only after the
  temporary file is complete.

Implementation note: confirm or add the required asyncfatfs rename/replace
primitive before relying on `.tmp` replacement for power-loss safety.

## Example Target Layout

```text
settings.cfg
Bank/
  001 Factory/
    bankset.bcg
    001 Breakbeat/
      sceneset.scg
      Kit 909ish/
        kitset.kcg
        909kik.drm
        dark.drm
        click.drm
        snap.snr
        metal.cym
        tight.hat
      pattern.pat
      effect.fx
Scene/
  001 Loose Jam/
    sceneset.scg
    Kit Loose/
      kitset.kcg
      ...
    pattern.pat
    effect.fx
Kit/
  001 909ish/
    kitset.kcg
    909kik.drm
    dark.drm
    click.drm
    snap.snr
    metal.cym
    tight.hat
Pattern/
  four_on_floor.pat
Sample/
  glass_hit.wav
Wavetable/
  001 Vowels/
    a.wav
    e.wav
    i.wav
Effect/
  short_room.fx
Instrument/
  909kik.drm
  snap.snr
```

