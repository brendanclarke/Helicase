# Helicase SD Card Filesystem Specification

This is the authoritative product-level filesystem and instrument-file
reference for the Helicase/LXR-02 firmware after Session 040. It includes the
full Session 032 instrument/kit file specification formerly kept in
`INSTRUMENT_FILE_SPEC.md`, plus the Session 033-039 runtime decisions for LFO,
velocity modulation, Morph, per-voice Morph, Scene modulation targets, Choke
behavior, Instrument Load, Kit/Instrument Morph Load, Kit/Instrument Morph
Save, Kit Save, root Instrument Save, Scene/Bank directory load/save, draft
Scene/Bank pattern persistence, and storage-only LFO `self` routing. Low-level
asyncfatfs API contracts and caller rules now live in
`ASYNCFATFS_REFERENCE.md`.

Use this document to distinguish three things:

- Implemented now: root `Kit/NNN Name/` directory loading/saving, root
  `Instrument/` pool replacement into Scene-owned descriptor-indexed
  instrument parameter images, Kit/Instrument Morph Load, Kit/Instrument Morph
  Save, normal new-format Kit Save, root Instrument Save, root Scene
  Load/Save, 16-Scene root Bank scan/load/save, keyed settings.cfg, and
  File/Dir/sDir asyncfatfs diagnostics behind Dev Mode, and the root `.hcindex`
  boot marker.
- Settled target shape: Bank, Scene, Kit, Pattern, Sample, Wavetable, Effect,
  Instrument, and `settings.cfg` filesystem layout.
- Not implemented yet: crash-recoverable Scene/Bank autosave promotion, real
  Effect load/save, final dynamic Pattern storage, and descriptor-backed step
  automation playback.

Historical session logs and drafts may describe older flat `.SND`/`GLO.CFG`
behavior. This file is the current source of truth for the intended filesystem
and current implemented state.

## Current Implementation Status

Implemented through Session 039:

- Normal kit loading scans root `Kit/` for numbered folders using asyncfatfs
  object iteration.
- Preferred kit folder names are `NNN Name`, for example `000 Init` or
  `001 Slak`.
- Compatibility kit folder names with a single underscore after the slot,
  `NNN_Name`, are accepted.
- FAT short-alias fallback accepts aliases beginning with a valid three-digit
  slot prefix, such as `000INI~1` or `001SLA~1`.
- The Kit scan cache stores both the eight-character display name and the
  asyncfatfs-returned FAT short alias. Long filenames are display/UI data; the
  cached short alias is scan-cache identity for reopening the exact object.
- The kit display name is the folder name after the three-digit slot prefix.
- `kitset.kcg` is parsed as the six-slot kit manifest.
- Six descriptor-keyed instrument text files are loaded from the kit folder.
- Root `Instrument/` is scanned with asyncfatfs object iteration into a per-type
  alphanumeric browser and a selected file can be loaded into one explicit
  Scene/voice slot.
- Loaded instrument values write into the active `scene_t.kit` descriptor
  images, not into the old flat `parameter_values[]` sound buffer.
- VOICE menu pages resolve through active instrument descriptor layouts in
  `Core/DSP/Instruments/*/*Parameters.c`.
- Preset/InstrumentManager applies descriptor image values back into the DSP
  runtime after load and menu edits.
- Descriptor-backed Morph works from Scene-owned main/morph endpoint images.
- PERF Morph has been split into one Scene global setter plus six per-voice
  Morph amounts. The global Morph control bulk-sets the six per-voice values.
- LFO and velocity modulation can target active-slot descriptor parameters
  without hardcoded per-instrument parameter lists.
- LFO and velocity modulation can target the shared Scene modulation namespace:
  per-voice Morph targets `1vm..6vm` and Scene Decimation `srt`.
- Per-instrument `instrument_decimation` is a voice-local descriptor target and
  is morphable, modulatable, and marked automatable for the future automation
  pass.
- LFOs now expose two target selector pairs with shared oscillator settings and
  shared polarity.
- VOICE sub-pages can expose up to 16 descriptor cells as four-cell screens.
- Instrument registry metadata declares Basic, Advanced, and Choke loading
  policy. Drum/Snare are Basic; Cymbal is Advanced; HiHat is Advanced|Choke.
- Kit Load uses SEQ buttons as Scene target toggles; Instrument Load uses them
  as one-Scene selection. Load-menu SEQ LEDs show Scene step activity and blink
  the current target selection.
- Root Instrument parsing is staged. An active-Scene commit clears all current
  modulation owners before type replacement, rebuilds all six runtime Morph
  images, then normalizes and reinstalls all source target relationships.
- Kit Morph Load is a Load menu entry `Load:[KitMrp  ]`. It parses the same
  root Kit directory as normal Kit Load, but Preset copies source normal
  endpoint values into resident morph endpoints only for slots whose instrument
  types match. Mismatched source/destination slots are no-change.
- Instrument Morph Load is the nested Instrument Load type-row sibling for the
  currently loaded slot type only, shown as `<Type>Mrp`. It loads the selected
  root Instrument file through normal staging, then copies staged normal
  endpoint values into the destination slot's morph endpoint only when the
  slot type still matches.
- Normal `Save:[Kit     ]` writes the active Scene kit to the directory Kit
  format: a numbered `Kit/` folder, `kitset.kcg`, and six descriptor-keyed
  instrument files containing `[params]` and `[morph]`.
- Root Kit/Scene-style scan/load/save slot range is now direct `000..999`.
  Slot `000` is a real library slot for all numbered filetypes. Firmware
  library slot variables are `uint16_t`; voice slots remain byte-sized `0..5`.
- Root Instrument Save is implemented from nested Save-page VOICE mode. It
  writes one resident Scene voice to `Instrument/<stem.ext>` using the same
  descriptor-keyed instrument text writer used by Kit Save member files.
- Kit and Instrument load retain per-slot source names in SceneData: an
  eight-character display field and a 16-character logical stem used by Kit
  Save filename generation. Defaults are `inst_vo1`..`inst_vo6`.
- Instrument text files accept `self` only for `lfo_target_voice` and
  `lfo_target_voice_2`. The parser resolves it immediately to the file's
  expected one-based destination slot; SceneData, Menu, Preset, and DSP runtime
  still see ordinary numeric voice selectors.
- Kit Save emits `self` for an LFO target voice when the stored numeric target
  equals the source instrument's own one-based slot. Cross-slot LFO targets
  remain decimal voice numbers.
- Descriptor-backed LFO targets use descriptor-owned parameter-domain metadata
  and apply temporary LFO-shaped values through the normal descriptor runtime
  writer. Negative polarity matches original LXR value-relative behavior in
  parameter space instead of subtracting a full raw runtime range.
- Scene settings now own per-voice `audio_out[6]`, `fx_send_amount[6]`, and
  `fader_setting[6]`. `kitset.kcg` never emits these values; legacy
  `audio_out=` lines are parse-only side data for old embedded Kits.
- Root Scene Load/Save is implemented for `Scene/NNN Name/` folders containing
  `sceneset.scg`, embedded `Kit <name>/`, `pattern.pat`, and `effects.fx`.
  `sceneset.scg` never stores the Scene name.
- Resident Scene and embedded Kit names are retained from directory names:
  root `Scene/NNN Name/` and child `Kit <name>/`.
- Root Bank scan/load/save uses the 16 resident Scene slots. Boot scans Banks
  and tries the lowest valid Bank before root Scene/root Kit fallback. Empty
  Bank folders are valid and complete Bank selection before fallback.
- Bank-local Scene folders use two digits, `00..15`, not root-library
  three-digit numbering. Bank Save writes every child selected by its 16-bit
  save mask and Bank Load iterates every requested/present local child.
- Scene/Bank `pattern.pat` text v2 now stores the 128x7 active-step bitmap plus
  per-track length and scale. Version 1 placeholders remain accepted.
- `File`, `Dir`, and Save-only `sDir` diagnostics remain compiled but are
  hidden from the Load/Save type cycle unless `CONFIG_DEV_MODE != 0`.

Current bridges and limitations:

- `SCENE_COUNT` is 16. No separate resident staging Scene is currently
  defined; asynchronous save/load state is held in filesystem operation state.
- Pattern/container storage remains a bridge shape and will be replaced by the
  later dynamic stack Pattern implementation.
- `FS_FILE_KIT` save now routes to the new Kit directory writer. The old flat
  `.snd` Kit writer is no longer the normal Kit Save path.
- `FS_FILE_MORPH` load/save still uses the legacy `.SND` morph-kit path.
- Globals load/save through root keyed-text `settings.cfg` version 1. Legacy
  `glo.cfg` is retired and is not a fallback input.
- Effect, Wavetable-pool, and final dynamic Pattern-pool load/save operations
  are not implemented/promoted yet. Root Instrument, root Scene, and
  16-Scene root Bank load/save exist.
- Descriptor-backed LFO and velocity modulation runtime paths are in place for
  direct descriptor targets, voice-local decimation, per-voice Morph, and Scene
  Decimation. LFO direct descriptor overlays now go through descriptor-domain
  adapters; the remaining target-runtime gap is step automation.
- `AutomationNode` and the current step automation storage/playback path still
  use legacy/narrow target IDs and must be rebuilt for descriptor and Scene
  modulation targets.
- New Scene modulation target IDs are runtime/menu IDs; current Scene files
  persist Scene mix/routing settings but not the future full effect stack.
- The 16-Scene workspace, present/edit masks, and linked Scene/Pattern PERF
  selection are implemented. Crash-recoverable autosave/dot-file promotion
  and a separate background staging Scene remain future work.

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

Boot-created root marker:

```text
.hcindex
```

After a successful SD mount and before library scans, firmware creates or
truncates `.hcindex`, writes exactly four bytes from the STM32F765 hardware
RNG, closes it, and waits for the asyncfatfs flush boundary. It is an opaque
boot marker, not a settings/schema file; normal product scanners ignore it.

`settings.cfg` replaces legacy `GLO.CFG`/`glo.cfg` as the current system-settings
file. It stores allowlisted system-level settings and the active Bank number,
not the Bank display name. At boot, the current firmware reads this numbered
Bank selector; legacy glo.cfg is not attempted.

The current file is keyed text with:

    format=helicase.settings
    version=1
    active_bank=<0..999>

The remaining accepted/written keys are bpm, ext_sync, quantisation,
midi_chan_global, midi_filt_tx, midi_filt_rx, midi_routing,
screensaver_on_off, bar_reset_mode, prescaler_clock_in,
prescaler_clock_out1, prescaler_clock_out2, follow, and osc_wave_interp.
Unknown or out-of-scope keys are not a way to restore Scene state. In
particular, Morph, per-voice Morph, and Scene decimation belong to Scene
payloads, not global settings.

There is no implemented .settings.cfg backer or power-loss transaction for
settings.cfg. Do not claim dot-file autosave/promotion until the AsyncFATFS
transaction/recovery primitive exists.

Root-level entries outside the recognized list are ignored by normal
loader/browser code.

## AsyncFATFS Directory Navigation

The underlying asyncfatfs layer uses a state-machine approach to navigate directories and open files. When writing filesystem traversal logic, several critical rules apply:

- **Case Sensitivity vs Insensitivity**: FAT filesystems are fundamentally case-insensitive but preserve case in Long File Names (LFN). When opening directories created by a user (e.g., instrument type folders like `Drum` or `Snare`), use `AFATFS_MATCH_CASE_INSENSITIVE` with `afatfs_opendir_lfn()` to tolerate manual lowercasing. Use `AFATFS_MATCH_CASE_SENSITIVE` only when strictly requiring an exact UI string match.
- **Directory Creation**: `afatfs_mkdir_lfn()` behaves as "open or create". If the directory exists, it resolves the handle; if missing, it creates the LFN fragments and generates an 8.3 alias. `afatfs_opendir_lfn()` strictly searches for an existing directory and will safely fail (return a NULL handle) if it does not exist.
- **Asynchronous Parent Traversal (`afatfs_chdirParent`)**: 
  - **WARNING:** `afatfs_chdirParent()` returns an `afatfsOperationStatus_e` enum (`SUCCESS` = 0, `IN_PROGRESS` = 1, `FAILURE` = 2), NOT a boolean.
  - Do **not** evaluate it as `if (!afatfs_chdirParent())`. Because `SUCCESS` is `0`, `!0` evaluates to true, which can cause state machines to incorrectly early-return upon success and get trapped in infinite traversal loops.
  - Correct usage must check explicitly: `if (st == AFATFS_OPERATION_IN_PROGRESS) return;`.
- **Absolute Root**: To jump back to the absolute root of the SD card, use `afatfs_chdir(NULL)`. This synchronously resets the global `afatfs.currentDirectory` to the FAT root directory without requiring an asynchronous block.

## Numbered Folders

`Bank`, `Scene`, `Kit`, and `Wavetable` contain meaningful numbered
subdirectories. Numbered folders use this form:

```text
000 <name>
001 <name>
002 <name>
003 <name>
...
```

The numeric prefix is the direct library slot number shown in the UI. Slot
`000` is a real slot, not a sentinel. Numbers do not need to be contiguous.
Browsers should scan slots sequentially and show missing slots as empty, for
example `003: Empty` when slot 3 has no matching folder.

Names after the numeric prefix are user-facing labels. The preferred separator
after the three-digit slot number is a space, as in `000 Init` or `001 Slak`,
but loaders may accept an underscore for compatibility with older generated
folders, as in `000_Init` or `001_Slak`. Spaces inside the displayed name are
valid. The numeric prefix is authoritative for slot order; folders should not
be sorted only by full filename.

For root Kit/Scene-style libraries, `NNN` is direct on disk and maps to the
same firmware library slot number. Do not add or subtract 1 for browser/library
slot identity. This is separate from instrument file voice coordinates, which
remain one-based `1..6` inside instrument text schemas.

## Bank

Status: implemented as a 16-resident-Scene Bank workspace. It has selected
child save/load and a staged root-Bank promotion flow; it is not yet a
crash-recoverable autosave transaction.

`Bank/` contains bank folders:

```text
Bank/
  000 <bank name>/
  001 <bank name>/
```

A bank represents all non-global data loaded as one performance set. The active
bank number is recorded in current `settings.cfg`; the bank display name is not
the persistent selector.

Each bank folder contains exactly one bank-level config file:

```text
bankset.bcg
```

`bankset.bcg` stores bank-level metadata/configuration. It also acts as the
validator, guard, and version marker for identifying a folder as a bank. A
folder without a valid `bankset.bcg` must not be loaded as a bank.

Each bank folder also contains up to 16 scene folders:

```text
Bank/000 <bank name>/
  bankset.bcg
  00 <scene name>/
  01 <scene name>/
  ...
  15 <scene name>/
```

Scene slot numbers inside a bank do not need to be contiguous. Bank-local
Scene folders use direct two-digit slots `00..15` for the 16 resident bank
scenes. This is intentionally different from root `Scene/NNN` library folders.
Missing scene slots are valid and will be shown as empty in the future UI. A
user may exchange scene folders between banks.

`bankset.bcg` v2:

```text
format=helicase.bankset
version=2
active_scene=0
scene_mask_voice_edit=0x0001
```

`active_scene` is a Bank-local `00..15` slot number and is not zero-padded in
the file. The Bank name is never stored in `bankset.bcg`; it comes only from
the `Bank/NNN <bank name>/` directory.

Current behavior:

- Boot scans `Bank/` and loads the lowest valid Bank before root Scene/root Kit
  fallback.
- Bank Load applies the v2 active Scene and edit mask, then loads requested
  present children from the two-digit local namespace.
- An empty Bank containing only valid `bankset.bcg` is valid; it completes Bank
  selection and then falls back to root Scene, root Kit, then defaults.
- Bank Save writes bankset.bcg and every child selected by the 16-bit mask.
  If the active Scene is outside a nonempty save mask, the saved manifest
  selects the first saved child so it never points to an absent payload.
- Save builds a non-numbered temporary sibling, preflights temp/old-name
  collisions, renames any previous numbered Bank to a non-loadable old
  sibling, and promotes the completed temp directory to the numbered name.
  Promotion failure reports BProm. This prevents in-place stale-tree merges
  but is not a durable journal/recovery transaction.
- A full Bank Load resets Scene child discovery for every delegated child.
  Filenames discovered in one local folder must never be reused for another.

## Scene

Status: root Scene Load/Save is implemented and promoted. Sixteen resident
Scenes are allocated for Bank workspace use; root Scene remains an explicit
numbered library/import-export pool.

`Scene/` is a root-level pool of user-copyable scene folders:

```text
Scene/
  000 <scene name>/
  001 <scene name>/
```

Scene folders in this pool can be loaded into a bank scene slot. They use the
same folder structure as scene folders inside a bank. Root `Scene/` is a
library/pool like root `Kit/` and root `Instrument/`: explicit Scene Save writes
there, explicit Scene Load imports from there, and root Scene files are not
autosaved.

A scene folder contains:

```text
sceneset.scg
Kit <kit name>/
pattern.pat
effects.fx
```

`sceneset.scg` stores scene-level metadata/configuration and validates the
folder as a scene. Current v1 Scene settings include global/per-voice Morph
values, `voice_decimation_all`, seven MIDI channel/note values, and the
Scene-owned per-voice mix settings `audio_out[6]`, `fx_send_amount[6]`, and
`fader_setting[6]`.

`Kit <kit name>/` is the scene's embedded kit directory. It works like a kit
folder but is named without a numeric slot prefix because it belongs to the
scene. The word after `Kit` is the kit name. The kit name is not stored in
`kitset.kcg`, `sceneset.scg`, or any other metadata field.

`pattern.pat` is currently one of three accepted bridge shapes:

- legacy binary bridge-pattern payload;
- thin v1 text placeholder;
- draft v2 text payload emitted by new Scene/Bank saves.

The v1 placeholder:

```text
format=helicase.pattern
version=1
placeholder=1
```

The thin placeholder means the staged PatternSet uses PatternData's empty
bridge defaults.

The v2 draft payload:

```text
format=helicase.pattern
version=2
track1=<length>,<scale>,<128 active bits>
...
track7=<length>,<scale>,<128 active bits>
```

Only the step on/off bit (`STEP_ACTIVE_MASK`) is stored for each of 128 steps
on each of seven tracks. Per-track `length` and `scale` are retained.
Velocity, note, probability, automation, rotation, shuffle, next-pattern, and
change-bar use PatternData defaults on load. The loader rebuilds the legacy
16-bit main-step shadow from the 128-bit rows using `step % 16`.

`effects.fx` currently stores a guarded placeholder until real effect storage
exists.

Current `scene_t` ownership:

- `scene_settings_t settings`
- `PatternSet pattern`
- `kit_t kit`

Current `scene_settings_t` fields:

- `morph_amount`
- `voice_morph_amount[INSTRUMENT_SLOT_COUNT]`
- `voice_decimation_all`
- `midi_channel[NUM_TRACKS]`
- `midi_note[NUM_TRACKS]`
- `audio_out[INSTRUMENT_SLOT_COUNT]`
- `fx_send_amount[INSTRUMENT_SLOT_COUNT]`
- `fader_setting[INSTRUMENT_SLOT_COUNT]`

Scene file work stores scene-level metadata and settings in `sceneset.scg`.
These do not belong in `kitset.kcg` or instrument files.

## Kit

Status: root Kit folder load and new-format Kit save are implemented on the
Session 036 asyncfatfs LFN/case foundation.

`Kit/` is a root-level pool of numbered kit folders:

```text
Kit/
  000 <kit name>/
  001 <kit name>/
```

Kit folders can be loaded into the active scene and saved from the active Scene
kit. Slot numbers do not need to be contiguous, and missing slots are shown as
empty in the UI. Root Kit slots are addressed as direct `000..999` on disk and
in firmware library-slot state.

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

Concrete current test-card example:

```text
SD_CARD/
  Kit/
    000 Init/
      kitset.kcg
      ...
    001 Slak/
      kitset.kcg
      slakd1.drm
      slakd2.drm
      slakd3.drm
      slaks1.snr
      slakc1.cym
      slakh1.hat
```

`kitset.kcg` is the kit folder guard/version file plus the six-slot instrument
manifest. The kit name comes only from the folder name:

- Root kit pool: `Kit/NNN <kit name>/` where `NNN` is direct `000..999`
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

Implemented Kit save behavior: saving a Kit writes a folder in this same shape:
`kitset.kcg` plus six descriptor-keyed instrument files. Session 036 adds
asyncfatfs LFN component creation/object iteration, so firmware-created Kit
folders and member instrument files preserve display spaces and mixed case
through VFAT LFN entries while returning generated 8.3 aliases for existing
open paths and `kitset.kcg` references.

### `kitset.kcg`

`kitset.kcg` owns only:

- Format/version validation.
- Slot membership.
- Per-slot instrument type.
- Per-slot instrument filename.

Example:

```text
format=helicase.kitset
version=1

[slot1]
type=drm
file=slakd1.drm

[slot2]
type=drm
file=slakd2.drm

[slot3]
type=drm
file=slakd3.drm

[slot4]
type=snr
file=slaks1.snr

[slot5]
type=cym
file=slakc1.cym

[slot6]
type=hat
file=slakh1.hat
```

Required top-level fields:

- `format=helicase.kitset`
- `version=1`

Required per-slot fields:

- `[slot1]` through `[slot6]`
- `type=drm|snr|cym|hat`
- `file=<8.3 instrument filename>`

Validation rules:

- All six slots must be present.
- Every slot must declare type and file.
- File extension must match declared type: `.drm`, `.snr`, `.cym`, or `.hat`.
- Legacy `audio_out=<0..5>` lines may still be parsed as compatibility side
  data. Scene Load imports them only when loading an embedded Kit inside an old
  Scene folder whose `sceneset.scg` lacks an `audio_out` line. Root Kit Load
  ignores them and preserves current Scene routing.

`kitset.kcg` does not own:

- Kit name.
- Pattern data.
- MIDI channel or MIDI note.
- Scene settings.
- Per-voice audio routing, FX send amount, or fader mode.
- `voice_decimation_all`.
- Instrument parameter values.
- Instrument morph endpoint values.

## Instrument Files

Status: implemented for Kit-folder files, root `Instrument/` pool load, and
root Instrument Save.

Instrument files are text key/value files with a fixed header and one or two
parameter sections. Kit Save member files and root Instrument Save use the same
schema and the same `storage_formatInstrumentLine()` writer.

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
- Known parameter and target rows parse as `uint8_t`. Target tokens use the
  compact byte selector domain; they are not packed 16-bit parameter IDs.
- `lfo_target_voice` and `lfo_target_voice_2` are menu/runtime destination
  selectors. Voices `1..6` select voice slots and the special display value
  `scn` selects the Scene modulation target namespace. The associated
  parameter value is a compact token: a local descriptor index, a Scene
  target index when the voice is scn, or 0xff for off. Runtime code resolves
  that token to a wide descriptor/Scene identity only at the apply boundary.
- `self` is accepted only for `lfo_target_voice` and `lfo_target_voice_2`.
  It is a storage-only relocation alias resolved by the parser with
  `storage_instrument_state_t.expected_slot` before writing Scene-owned
  descriptor images. It is never a Menu value, SceneData sentinel, packed
  wide parameter ID, or DSP runtime value.
- New save code must emit `self` only when the numeric LFO voice selector
  equals the source instrument's own one-based slot. Explicit cross-slot
  modulation remains a decimal voice number.

Instrument file metadata deliberately does not include:

- `slot`
- `kit_name`
- `source_name`
- `source_file`

The slot comes from `kitset.kcg`. The kit name comes from the kit folder.

The converter provides legacy compatibility by regenerating text files from
legacy `.SND` payloads. Storage keeps aliases for the prior HiHat decay text
keys because those names were shipped before the canonical Choke convention.

Legacy flat morph kit loads (`FS_FILE_MORPH`) remain legacy `.SND`.
`Load:[KitMrp]` and nested InstrumentMrp use new-format Kit/Instrument text
payloads for loading normal source endpoints into morph endpoints.
`Save:[KitMrp]` and nested InstrumentMrp Save also use the new-format text
payloads; their Morph Save projection writes the current interpolated value
into both normal and morph endpoint fields.

## Canonical Instrument Keys

The physical SD-card key vocabulary lives in each instrument descriptor table:

- `Core/DSP/Instruments/Drum/DrumParameters.c`
- `Core/DSP/Instruments/Snare/SnareParameters.c`
- `Core/DSP/Instruments/Cymbal/CymbalParameters.c`
- `Core/DSP/Instruments/HiHat/HiHatParameters.c`

Current descriptor counts:

- Drum: 39 descriptors.
- Snare: 38 descriptors.
- Cymbal: 39 descriptors.
- HiHat: 39 descriptors.

Descriptor key lookup is type-local. The same key may exist in multiple
instrument types but resolves against the loaded slot type.

HiHat is the first Choke instrument. Its visible closed-hat row is canonical
`amp_envelope_decay`; its alternate track-7 row is
`amp_envelope_decay_choke`. The old `_closed` and `_open` spellings are load
aliases only and must not be emitted by new conversion/save code. A Choke
instrument may expose any number of `<base>_choke` descriptors. When it is in
slot 6, VOICE7 substitutes each available sibling for its base descriptor;
those siblings remain separate normal modulation targets.

Current keys by family:

- Oscillator and noise: `osc1_wave`, `osc1_pitch_coarse`,
  `osc1_pitch_fine`, `osc2_wave`, `osc2_pitch_coarse`,
  `osc2_mod_amount`, `osc2_mod_type`, `osc3_wave`,
  `osc3_pitch_coarse`, `osc3_mod_amount`, `noise_freq`,
  `osc1_noise_mix`.
- Filter: `filter_freq`, `filter_reso`, `filter_drive`, `filter_type`.
- Amp envelope: `amp_envelope_attack`, `amp_envelope_decay`,
  `amp_envelope_decay_choke`, `amp_envelope_slope`, `amp_attack_repeat`.
- Pitch envelope: `pitch_envelope_decay`, `pitch_envelope_amount`,
  `pitch_envelope_slope`.
- Voice: `instrument_vol`, `instrument_pan`, `instrument_drive`,
  `instrument_decimation`.
- LFO: `lfo_rate`, `lfo_amount`, `lfo_amount_2`, `lfo_wave`,
  `lfo_polarity`, `lfo_retrigger_voice`, `lfo_sync`, `lfo_offset`,
  `lfo_target_voice`, `lfo_target_param`, `lfo_target_voice_2`,
  `lfo_target_param_2`.
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

The registry also owns immutable load-policy metadata, not serialized file
data: Basic types may appear without limit, Advanced types are limited to two
per Kit, and Choke types opt into slot-6 VOICE7 `_choke` substitution. Current
types are Drum/Basic, Snare/Basic, Cymbal/Advanced, and HiHat/Advanced|Choke.

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
offset. `ROW_SLOT_DECIMATION` is an explicit wrapper around this pattern so the
voice-local `instrument_decimation` row advertises its morph/mod/automation
contract at the descriptor site without creating a second flagging system:

- `instrument_decimation`
- `velo_mod_amount`

These apply through binding kinds instead:

- `INSTRUMENT_BIND_SLOT_DECIMATION`
- `INSTRUMENT_BIND_VELOCITY_AMOUNT`

## Scene Instrument Storage

Each `scene_t` owns one `kit_t`. Each `kit_t` owns six
`kit_instrument_slot_t` records.

`kit_t` also retains instrument source-name metadata:

- `instrument_display_name[6][9]`: eight-character, NUL-terminated LCD/display
  stem.
- `instrument_stem[6][17]`: first 16 stem characters retained for later Kit
  Save member filename generation.

These are UI/storage provenance only, not paths, file-open authority, or
instrument parameters. Defaults are `inst_vo1`..`inst_vo6`. Kit load derives
them from `kitset.kcg file=` entries; root Instrument load commits the selected
Instrument filename stem only after the staged payload succeeds. `kit_settings_t`
owns generated Kit-level values, including the non-Choke slot-6 track-7
alternate-decay main and Morph endpoints.

Each instrument slot owns:

- `type`
- `parameter_images.instrument_parameters[64]`
- `parameter_images.morph_instrument_parameters[64]`
- `parameter_images.morph_interpolation[64]`

Arrays are indexed by descriptor index for the slot's current instrument type.
Descriptor index `0` is valid; empty menu cells use `INSTRUMENT_MENU_EMPTY`
(`0xff`) and skip cells use `INSTRUMENT_MENU_SKIP` (`0xfe`).

Canonical wide instrument parameter IDs, used only for lookup/runtime
resolution rather than resident target storage:

```c
id = slot * INSTRUMENT_PARAM_COUNT + descriptor_index
```

Current bounds:

- `INSTRUMENT_SLOT_COUNT`: 6.
- `INSTRUMENT_PARAM_COUNT`: 64.
- Voice parameter IDs: `0..383`.
- Scene modulation IDs start at `INSTRUMENT_VOICE_ID_COUNT` (`384`) and
  currently occupy `384..390` for `1vm..6vm` plus Scene Decimation `srt`.
- Remaining higher IDs remain reserved for later FX/general parameter address
  space.

`morph_interpolation[]` is runtime-derived state and is not serialized.

Resident parameter values are `instrument_param_value_t` bytes. Resident
target selections are `instrument_target_token_t` bytes, with
`INSTRUMENT_TARGET_TOKEN_OFF` equal to 0xff. Local target values are compact
indices, LFO voice selection uses self/voice/Scene values, and storage/menu
code expands a token only when resolving or displaying it. A saved Scene/Kit
must not store the wide ID above as a target token.

## Current Kit Load Path

Boot normal-kit load path:

1. `main.c` initializes DSP objects.
2. `scene_initAll()` initializes Scene storage.
3. `filesystem_initCardAndMountBlocking()` mounts the card.
4. `filesystem_requestScanKits()` scans `Kit/` into the kit-slot cache.
5. `preset_loadDrumset(0, 0)` requests normal kit slot 0.
6. `filesystem_loadKitDirectory_tick()` opens the cached kit folder, parses
   `kitset.kcg`, resets slots in a private staged `kit_t`, then parses each
   listed instrument file into that staged descriptor-indexed storage.
7. After every file validates, filesystem copies the complete staged Kit into
   each selected Scene. It does not replace PatternData or Scene settings.
8. Completion callback sets `PRESET_OP_KIT_LOAD`.
9. `menu_pollPresetStatus()` starts sound apply.
10. Before audio starts, `menu_startSoundApply()` calls
   `preset_sendDrumsetParameters()` synchronously.

Runtime kit loads use the same Scene-owned apply logic, but the post-load apply
is chunked through `preset_startDrumsetApply()` /
`preset_tickDrumsetApply()` to avoid foreground bursts after audio is running.

## Current Root Instrument Load Transaction

Root Instrument Load is one explicit Scene, slot, type, and browser-index
request. Filesystem validates the selected `Instrument/` file into private
staging and keeps its display stem beside it; asynchronous parsing must never
reset or alter the live destination Scene slot.

For an inactive destination Scene, Preset commits retained Kit slot/name data
only. For the active Scene, the ordered transaction is:

1. clear all six current LFO target pairs and velocity targets while outgoing
   Scene slot types still resolve their old runtime nodes;
2. copy staged slot/name into SceneData;
3. reset only the new type's runtime instance and apply the loaded slot route;
4. rebuild all six retained descriptor Morph/runtime images;
5. normalize each source's LFO voice/parameter pairs and velocity target, then
   reinstall all six source relationships;
6. release Menu controls.

The all-source pass is required because any source may target the replaced
slot. Menu holds the Instrument transaction lock across filesystem read,
commit, Morph rebuild, and rebind: encoder, Scene selection, VOICE
selection/preview, and mode changes are consumed without changing request
context. Filesystem remains single-operation; Preset publishes completion
coordinates only after filesystem accepts the request.

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
- HiHat base/Choke decay use `slopeEg2_calcDecay()`.
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

- `VOICE1_PAGE` through `VOICE6_PAGE` resolve the descriptor layout for the
  instrument type currently assigned to that logical slot.
- `VOICE7_PAGE` remains the alternate trigger/menu view for slot 6.
- For a slot-6 Choke type, VOICE7 replaces a displayed base descriptor with its
  available `_choke` sibling.
- For a non-Choke slot-6 type with `amp_envelope_decay`, VOICE7 exposes the
  generated Scene Kit alternate-decay setting; without that descriptor it uses
  the ordinary slot-6 page.

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

Current descriptor Morph state after Session 033:

- Instrument files can carry `[morph]` endpoint values.
- Missing `[morph]` copies main endpoint values into morph endpoint values.
- Scene instrument slots store main endpoint, morph endpoint, and derived
  interpolation images.
- The Morph worker runs against Scene-owned descriptor images and applies one
  descriptor per foreground pass.
- The worker uses the active slot's current instrument type and descriptor
  table, so instrument swapping remains dynamic and the Morph engine does not
  own hardcoded parameter lists.
- Per-voice Morph amounts live in `scene_settings_t.voice_morph_amount[6]`.
- PERF shows two four-cell screens: `mrp 1vm 2vm 3vm` and `4vm 5vm 6vm srt`.
- Setting global `mrp` bulk-sets all six per-voice Morph values.
- Setting `srt` controls Scene/global decimation and defaults to `127` when
  Scene state has no explicit value yet.
- Per-voice Morph is the actual Morph-engine control. Global Morph is only a
  convenience set operation.

Target selection stores compact byte tokens, not wide descriptor IDs or legacy
`modTargets[]` indices. Target display/resolution helpers enumerate active
Scene descriptors and convert the token to a wide ID only at that boundary.

Current working target state:

- Off targets use `INSTRUMENT_TARGET_TOKEN_OFF` (`0xff`).
- Target menu display expands compact local or Scene tokens to names only for
  presentation; SceneData stores the byte token.
- InstrumentManager validates resolved targets by descriptor/Scene flags.
- The velocity target picker is self-scoped: it offers one `off`, modulatable
  descriptors for the source voice's current Instrument, and the source-voice
  Morph token 0x40 where applicable. It does not browse arbitrary other voices
  or the general Scene namespace.
- The LFO target picker shows self, voice destinations `1..6`, and `scn`. For
  a voice destination, the compact parameter picker shows modulatable local
  descriptors; for scn it uses the Scene modulation target token domain.
- The parameter picker skips non-modulatable descriptor rows. It does not show
  repeated `off` placeholders for skipped rows.
- If the selected target voice changes and the previous target parameter is not
  valid for the new destination, the parameter resets to `off`.

Current LFO shape:

- Each voice owns one LFO oscillator/configuration.
- That LFO has two target selector pairs and two amounts:
  `lfo_target_voice/lfo_target_param/lfo_amount` and
  `lfo_target_voice_2/lfo_target_param_2/lfo_amount_2`.
- `lfo_polarity` is shared by both target pairs and displays only `neg`, `pos`,
  and `bi`.
- Negative polarity applies original-LXR value-relative math in descriptor
  parameter space: `base * (1 - amount + amount * source)` for zero-based
  domains. It does not subtract a full raw runtime range.
- Positive polarity applies upward from the base/default value.
- Bipolar polarity applies equally around the base/default value where the
  destination range allows it.
- LFO VOICE menu short pages are:
  `frq snc wav ofs`, `rtg pol am1 am2`, and `vo1 ds1 vo2 ds2`.

Current velocity modulation behavior:

- Direct descriptor targets write through the descriptor-aware runtime path.
- Voice-local `instrument_decimation` is a descriptor target using the special
  `INSTRUMENT_BIND_SLOT_DECIMATION` binding.
- Scene per-voice Morph targets are retained set operations on
  `voice_morph_amount[slot]`, scaled by velocity and amount, and update the
  PERF menu value.
- Scene Decimation is a retained set operation on `voice_decimation_all` and
  updates the PERF menu value.

Current LFO modulation behavior:

- Direct descriptor targets install InstrumentManager adapters, not raw
  runtime pointers. The adapter captures the Scene/Morph base descriptor value
  and descriptor modulation domain, shapes the temporary parameter-domain value,
  and applies it through `instrumentManager_writeRuntime()` so envelopes,
  filters, pitch, transient, distortion, and LFO-rate writers keep their normal
  scaling and side effects.
- Voice-local `instrument_decimation` uses the special supplemental binding.
- Per-voice Morph LFO modulation is a hidden secondary layer centered around
  the retained per-voice Morph base value. It does not move the PERF menu value.
- Multiple LFO sources targeting the same voice Morph sum their signed deltas
  around the retained base value, then clamp to `0..255`.
- The Morph worker adds one extra foreground pass for each voice whose Morph is
  currently LFO-modulated. It still interpolates one descriptor per pass rather
  than trying to recalculate a whole voice immediately.
- Scene Decimation LFO modulation is runtime-only; it does not move the retained
  `voice_decimation_all` menu value.

Current target limitations:

- `AutomationNode` still plays back by emitting legacy MIDI CC/CC2 through
  `midiParser_ccHandler()`.
- `preset_applyInstrumentRuntimeValueInternal()` currently ignores its
  `recordAutomation` argument.
- `seq_recordAutomation()` still accepts/narrows destination as `uint8_t`.

Therefore, the remaining descriptor target follow-up is step automation:
`AutomationNode`, step target storage, automation recording, and automation
display must preserve descriptor/Scene target IDs and apply through the same
descriptor-aware runtime routes.

The main remaining modulation correctness follow-up is dynamic owner enumeration
for non-adapter paths: `modNode_resetTargets()` and
`modNode_directOriginalValueChanged()` still enumerate fixed global nodes rather
than all dynamic runtime-pool sources. Descriptor LFO targets no longer use the
raw runtime-float backend, but any future direct backend must not write shaped
DSP members such as `SlopeEg2.decay` for byte-domain descriptors.

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
  000 <wavetable name>/
  001 <wavetable name>/
```

Each wavetable folder contains an alphanumerically sorted set of `.wav` files:

```text
Wavetable/000 <wavetable name>/
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
Users may copy a scene's `effects.fx` into this pool, and may copy a pool
effect into a scene if they rename it to `effects.fx`.

Scene `effects.fx` stores the scene's effect settings and effect automation
sequence. Effects and effect file formats are future DSP work.

## Instrument

Status: root browser, one-slot load, Instrument Morph Load, and standalone
root Instrument Save are implemented.

`Instrument/` is a root-level pool of instrument files:

```text
Instrument/
  <instrument name>.<type>
```

Files are browsed alphanumerically by type when loading into a kit in a scene.
Users may copy instrument files from a kit folder into this pool, or save one
resident voice to the pool from nested Save-page Instrument Save. Users should
not copy files from this pool directly into a kit folder; kit membership is
controlled by `kitset.kcg`.

Entering Instrument Load on a voice shows the current Kit membership stem.
Changing the type row is a non-destructive filter/policy operation. Moving the
lower row selects one root pool file and immediately starts the staged
transaction described above. Basic/Advanced assignment policy is defined by
the firmware registry, never by file contents.

Entering Instrument Save on a voice from the Save page shows the source slot's
current type and an eight-character editable stem. OK writes the selected
resident Scene/voice to `Instrument/<stem.ext>`, where the extension comes from
the source slot type. The source Scene, source voice slot, type, and visible
target filename are captured when the request is accepted so later UI movement
cannot retarget an in-flight save.

Initial recognized instrument types:

```text
.drm
.snr
.cym
.hat
```

## Current Load/Save Menu Reachability

Status after Session 039:

- `Load:[Kit     ]`, `Load:[KitMrp  ]`, `Load:[Scene   ]`, and
  `Load:[Bank    ]` are promoted top-level entries.
- `Save:[Kit     ]`, `Save:[KitMrp  ]`, `Save:[Scene   ]`, and
  `Save:[Bank    ]` are promoted top-level entries.
- `Load:[File    ]`, `Save:[File    ]`, `Load:[Dir     ]`,
  `Save:[Dir     ]`, and Save-only `Save:[sDir    ]` remain compiled
  diagnostic asyncfatfs test entries, but they appear in the normal type cycle
  only when `CONFIG_DEV_MODE != 0`.
- Scene and Bank load/save use explicit OK/OW confirmation. They do not
  live-load on scroll.
- Kit and KitMrp keep live-on-scroll load behavior.
- VOICE press on the Load page enters nested Instrument Load.
- VOICE press on the Save page enters nested Instrument Save/InstrumentMrp Save.

Still compiled but intentionally gated from the normal type cycler:

- `Settings` / Globals
- `Samples`
- Pattern
- All
- Performance
- legacy Morph

Do not widen the type cycler by enum order. Promote one operation at a time
after retesting it on the Session 036 asyncfatfs foundation.

## Save Operations

Implemented:

- Kit save writes a `Kit/<NNN Name>/` folder in the same logical shape the
  current loader accepts: `kitset.kcg` plus six instrument files. The folder and
  member files are created through asyncfatfs LFN primitives, with returned 8.3
  aliases used for `kitset.kcg` references/open paths.
- Instrument save writes one resident Scene/voice slot to the root
  `Instrument/` pool. It creates/opens the root with LFN/case-sensitive
  asyncfatfs APIs, opens the target display filename with `afatfs_fopen_lfn()`,
  streams the descriptor-keyed instrument schema, and updates the root
  Instrument browser cache from the returned display/alias pair.
- KitMrp and InstrumentMrp Save use the same text schemas as normal saves, but
  for morphable parameters they write the current per-voice interpolated value
  into both `[params]` and `[morph]`. This is a flattened snapshot of the
  current morph position, not an inverted endpoint pair. Morph Save does not
  rename the resident kit or instruments.
- Scene Save writes a root `Scene/<NNN Name>/` directory. The writer streams
  `sceneset.scg`, creates `Kit <kit name>/`, streams embedded `kitset.kcg`
  without `audio_out`, writes six embedded Instrument files, writes draft text
  `pattern.pat`, and writes placeholder `effects.fx`. Scene and embedded Kit
  names are directory-owned.
- Bank Save writes bankset.bcg version 2 and one local `SS <scene name>/`
  payload for every selected Scene bit. A zero child-scene mask is valid and
  creates an empty Bank. The completed payload is written to a unique
  non-numbered temporary Bank sibling before promotion to the numbered slot.

Still future:

- Pattern save writes the final dynamic-stack pattern format once implemented.
- Effect save writes the selected effect stack/settings format once effects
  exist.
- `settings.cfg` save writes the strict allowlisted system/global settings and
  active Bank number. There is no current .settings.cfg backer.

The current legacy non-Kit save paths are implementation leftovers and should
not be used as the new-format specification.

### Save/Overwrite Safety

Root library replacement must be scoped by parent directory and product parser:

- Enter the correct root directory first, such as `/Kit/`, `/Scene/`, or
  `/Bank/`.
- Scan only immediate child objects in that parent.
- Parse visible display names with the correct parser:
  - root libraries use three-digit `NNN <name>`;
  - Bank-local child Scenes use two-digit `SS <name>`.
- Delete or replace only physical objects whose parsed slot equals the target
  slot.
- Never run a recursive delete from the filesystem root using a broad target
  string.

Kit Save may use short-alias fallback for older/converted Kit folders. Scene
Save deliberately disables short-alias fallback and deletes only visible names
that parse as the exact Scene slot, preventing the root Scene wipe class of
bug. Bank-local selection uses storage_parseBankSceneFolder and carries the
captured afatfsObjectId_t to native deletion, avoiding a second ambiguous LFN
lookup. Bank Save promotes a complete temporary root tree; it does not claim
to preserve unselected old children across a replacement.

### asyncfatfs Boundary

The low-level asyncfatfs API, LFN/SFN alias rules, object iteration behavior,
filename sanitization, and caller checklist live in
`ASYNCFATFS_REFERENCE.md`. This product-level spec assumes callers go through
`filesystem.c` or those documented asyncfatfs primitives instead of rebuilding
FAT/VFAT traversal locally.

Current production replacement captures the selected object from an
LFN-aware scan and uses native afatfs_deleteTree for same-slot cleanup. Bank
Save additionally uses temporary/old sibling naming and promotion preflight.
No current path has an atomic or crash-recoverable replace primitive, so none
may claim power-loss-safe commit semantics.

## Verification Anchors

Use these as smoke tests when changing filesystem, descriptor storage, or
instrument runtime propagation:

- Build with `make` and package with `make img` when an image is needed.
- Boot with `SD_CARD/Kit/001 Slak`.
- Confirm the Kit scan shows the folder-derived kit name.
- Confirm `kitset.kcg` slot type/file/audio routing is honored.
- Confirm canonical HiHat keys `amp_envelope_decay` and
  `amp_envelope_decay_choke` parse; legacy `_closed/_open` aliases remain
  compatible input only.
- Confirm VOICE pages populate from active instrument descriptors.
- Confirm Slak file values are visible on VOICE pages.
- Confirm loaded voices produce audio.
- Confirm editing `instrument_vol`, filter frequency, envelope decay, and
  waveform changes sound immediately.
- Confirm Morph reaches both endpoints for a simple audible descriptor.
- Confirm LFO and velocity targets show one `off`, skip non-modulatable
  descriptors, and apply to direct descriptor, voice-local decimation, and
  Scene targets.
- Confirm Instrument Load starts at `kit <stem>`, does not load while changing
  type, loads immediately only from lower-row pool movement, and respects the
  two-Advanced limit.
- Confirm a completed Instrument transaction survives rapid encoder, Scene,
  VOICE, and mode presses without changing its captured destination or leaving
  a stale modulation target.
- Confirm Kit Save creates/opens the target Kit folder, writes `kitset.kcg`,
  writes six instrument files with one header, one `[params]`, and one `[morph]`
  section each, and can be loaded again.
- Confirm saved LFO self-targets emit `self` only on LFO voice selector keys
  whose numeric value equals the source slot.
- Confirm Kit slots `000`, above 255, and `999` display and save/load without
  wrapping or off-by-one mapping.
- Confirm LFO negative polarity on envelope decay follows the visible parameter
  direction and amount scale through the descriptor writer.
- Treat step automation as pending until the descriptor-aware AutomationNode
  pass is complete.

## Debounced Autosave and Reload Target

Status: settled target, not implemented.

Bank is the only autosaved workspace. Root-library folders such as `Scene/`,
`Kit/`, `Instrument/`, `Pattern/`, and `Effect/` are explicit
load/save/copy/import/export pools and are not autosaved.

The active Bank is a resident workspace containing 16 editable Scenes. The
currently playing/viewed Scene is only the audition/playback focus. Voice mode
also has a Scene edit target set, toggled with SEQ buttons, that may contain any
subset of the 16 Scenes. Voice/Kit/Instrument parameter edits apply to every
Scene in that edit target set as one logical batch edit. Pattern edits are
excluded from this multi-Scene parameter behavior and remain active/viewed-Scene
scoped until the final Pattern model says otherwise.

This multi-Scene edit behavior is binding. It supports workflows where the Bank
is treated as one conceptual Kit with 16 Patterns, or where a selected Scene
range receives the same parameter reconciliation. Storage still remains
Scene-local: identical Kits or Instruments across Scenes are separate copies on
disk unless a future feature explicitly introduces linked/shared files.

Inside the active Bank, autosave applies to dot-file backers for:

- Per-instrument files.
- Scene `effects.fx`.
- Scene `pattern.pat`.
- `sceneset.scg`.
- Embedded kit `kitset.kcg`.
- `bankset.bcg` as needed.

Committed and autosaved filenames:

- The non-dot filename is the committed save/load file. Examples:
  `sceneset.scg`, `kitset.kcg`, `slakd1.drm`, `pattern.pat`, `effects.fx`, and
  `bankset.bcg`.
- The matching dot-file is the autosave working backer. Examples:
  `.sceneset.scg`, `.kitset.kcg`, `.slakd1.drm`, `.pattern.pat`, `.effects.fx`,
  and `.bankset.bcg`.
- Autosave writes dirty retained-memory state to dot-file backers only.
- Bank SAVE waits for selected autosave writes to finish, then copies/promotes
  the selected dot-file backers over the matching non-dot committed files.
- Bank LOAD reads the non-dot committed files.
- Bank load/save operations start with all 16 Scenes selected. SEQ buttons can
  restrict the operation to a subset of Scenes before commit.
- Startup/resume normally loads valid dot-file backers for the active Bank, so
  autosaved working changes return without requiring explicit SAVE. If a
  dot-file is missing or fails validation, fall back to the matching non-dot
  committed file.

Mechanism:

- A parameter edit marks its owning dot-file backer stale and starts or resets a
  5-second idle timer.
- A multi-Scene Voice/Kit/Instrument edit marks the owning dot-file backer stale
  for each selected Scene affected by that batch edit. Repeated knob motion
  refreshes the same dirty records rather than enqueueing per-tick writes.
- If edits continue for 30 seconds without a 5-second gap, force a dot-file
  write.
- The autosave scheduler is bank-wide and tracks dirty records by Scene, file
  domain, and optional instrument slot.
- A successful autosave clears only the dirty record for the dot-file that was
  written. It does not update the committed non-dot file.
- Instrument, Kit, and Scene copy/paste within the active Bank are resident
  memory batch mutations. They dirty destination dot-file backers and do not
  change committed non-dot files until explicit Bank SAVE.
- RELOAD applies to Scene scope. It reads the selected Scene's non-dot committed
  files into resident memory and resets the selected Scene's dot-file backers to
  match those committed files.
- Dot-file autosave should use a temp-file-then-rename/replace sequence when
  the asyncfatfs primitive exists. On startup, a leftover `.tmp` means the temp
  write was incomplete; ignore/delete it, then use the previous dot-file if it
  validates. Only fall back to non-dot when the dot-file itself is missing or
  invalid.
- Root `settings.cfg` records the active Bank number and has a `.settings.cfg`
  backer. Closing the global settings menu or loading/saving a Bank rewrites
  both settings files.

Implementation note: confirm or add the required asyncfatfs rename/replace or
safe copy/replace primitive before relying on dot-file promotion for
power-loss-safe Bank SAVE.

## Example Target Layout

```text
settings.cfg
Bank/
  000 Factory/
    bankset.bcg
    00 Breakbeat/
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
      effects.fx
Scene/
  000 Loose Jam/
    sceneset.scg
    Kit Loose/
      kitset.kcg
      ...
    pattern.pat
    effects.fx
Kit/
  000 909ish/
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
  000 Vowels/
    a.wav
    e.wav
    i.wav
Effect/
  short_room.fx
Instrument/
  909kik.drm
  snap.snr
```
