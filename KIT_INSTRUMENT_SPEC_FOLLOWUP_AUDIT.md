# Kit Instrument Spec — Follow-up Audit

## Status

This document is a plan only. No firmware code changes are made in this pass. It
follows on from `KIT_DIR_LOAD_AUDIT.md` (Phase 2 kit-directory loading, session 030)
and specifies the next architectural step: moving the concrete instrument DSP
source files under `Core/DSP/Instruments/`, adding per-instrument parameter
definition files beside them, and introducing
`Core/DSP/Instruments/InstrumentManager.c/.h` as the system's single registry
and aggregate include for known instrument types. This is a decisive rebuild,
not a compatibility-preserving split: per-instrument definitions and the new
Scene-owned parameter mirrors replace the sound-parameter portion of
`Core/Scene/Preset/ParameterArray.h`, `parameter_values[]`, `parameters2[]`,
and the `midiParser_ccHandler()` sound-apply bridge.

2026-07-09 implementation note: the immediate instrument-file metadata cleanup
is now applied. `tools/convert_legacy_kits.py` emits only `format`, `version`,
and `type` before `[params]`; it no longer emits `slot`, `kit_name`,
`source_name`, or `source_file`. `storageTypes.c/h` now requires only
`format`, `version`, `type`, and at least one `[params]` value when finalizing
an instrument file. The slot still comes from `kitset.kcg` and is used only to
select the destination parameter map.

2026-07-09 SD_CARD validation note: regenerated `SD_CARD/Kit/` contains 31 kit
folders and 217 files (31 `kitset.kcg` plus 186 instrument files). A schema
walk verified every kit has six manifest slots, every listed file exists with a
matching extension/type, every instrument header contains only
`format=helicase.instrument`, `version=1`, and `type=...` before `[params]`, no
forbidden metadata keys are present, all parameter values are bytes, and
`[morph]` mirrors `[params]` for these legacy-derived kits. A source-payload
comparison also verified all 186 generated instrument `[params]` sections match
the corresponding `Pxxx.SND` bytes through the live `ParameterArray.h` enum map.
The visible gap from kit 030 to kit 036 is expected because the converter
preserves source slot numbers rather than compacting deleted/missing legacy
kits.

2026-07-09 canonical-key implementation note: Section 3.2b now defines the
canonical on-card parameter names for each current instrument type.
`tools/convert_legacy_kits.py` emits these names from `param_rename.txt`, and
`storageTypes.c` reads the same names into the existing `PAR_*` bridge
destinations. The old file-key vocabulary is no longer accepted by these
tables; compatibility with legacy kits is provided by converting the legacy
binary `.SND` files, not by retaining text-key aliases.

---

## 1. Audit of KIT_DIR_LOAD_AUDIT.md against the real code

This holds up well. Specific findings from cross-checking the audit against
`storageTypes.c/h`, `filesystem.c/h`, and `menu.c`:

- The parameter mapping table in `storageTypes.c` is not a guess — the audit's
  "2026-07-07 Parameter Mapping Audit" entry states every generated kit folder was
  simulated and diffed byte-for-byte against the source `Pxxx.SND` files with zero
  mismatches. Spot-checking the mapping against the real `PAR_` enum in
  `Core/Scene/Preset/ParameterArray.h` confirms the non-obvious cases, e.g.
  `PAR_NOISE_FREQ1`/`PAR_MIX1` correctly resolve to the snare voice's noise
  oscillator (voice slot 4), not drum1, despite the `1` suffix.
- The `storageTypes.c`/`filesystem.c` split is clean; the stated reason (`filesystem.c`
  was accumulating unrelated responsibilities) matches what's in the file today.
- The asyncfatfs directory-handle `->type` fix documented as a "Discovery Fix" is a
  real, hardware-found bug with a real fix — evidence this pass was tested, not just
  compiled.
- `menu.c` already routes sound-parameter writes through `preset_applySoundParameter()`
  rather than `frontPanel_sendData()` — Phase 1.2 (frontPanelParser removal, session
  028) is further along than the audit assumes in places.
- Current `tools/convert_legacy_kits.py` emits only the instrument validators
  `format`, `version`, and `type` before `[params]`. `storageTypes.c` now
  validates the same three fields and uses the kitset-owned slot only to select
  the destination parameter map.
- Instrument files do not store `slot`, `kit_name`, `source_name`, or
  `source_file`. The kit name is owned by the kit folder, and the
  file/instrument name is owned by the directory entry plus `kitset.kcg`'s slot
  manifest.

One thing to flag explicitly rather than let pass silently: Step 6 of the audit
preserves the existing behavior of writing loaded instrument values into flat
`parameter_values[]`/`parameters2[]`. That was correct for a load-only first pass, but
it means the audit's data destination is exactly what this document proposes to
replace. Treat it as a known, temporary bridge, not something this follow-up needs to
unwind gently — Section 3 below replaces it directly.

---

## 2. Why a straight "split the array" refactor is not sufficient on its own

Before specifying the new layout, three real complications were found in the current
code that a naive per-type array split does not address by itself. Each is resolved
in Section 3; they're recorded here because they justify the shape of the design.

**Cross-voice modulation routing.** `PAR_VOICE_LFO{n}` / `PAR_TARGET_LFO{n}` are not
per-voice properties the way `PAR_VOL{n}` is. `Core/Menu/Cc2Text.c`'s `modTargets[]`
is a single flat, kit-wide, `uint16_t`-addressed table (205 entries today) that any
voice's LFO or velocity modulator can point into, resolved via
`voiceFromModTargValue()` and `ModTargetVoiceOffset`. A per-type C enum alias
(`#define`/`typedef`) cannot serve this purpose, because slot-to-type binding is a
runtime choice loaded from `kitset.kcg`, not a compile-time one. "Alias" has to mean
runtime indirection: each type owns its own parameter definitions, and a separate,
stable, kit-wide numbering gives every `(type, that type's own parameter)` pair a
canonical ID usable by modulation routing, step automation, and FX addressing alike.

**Voice-type is hardcoded outside parameter storage too.**
`preset_applyLfoModTarget()` in `Core/Scene/Preset/presetManager.c` (and the
equivalent `preset_applyVelocityModTarget()`) switches on a fixed slot→type mapping:

```c
switch (lfo) {
case 0: case 1: case 2: modNode_setDestination(&voiceArray[lfo].lfo.modTarget, value); break;
case 3: modNode_setDestination(&snareVoice.lfo.modTarget, value); break;
case 4: modNode_setDestination(&cymbalVoice.lfo.modTarget, value); break;
case 5: modNode_setDestination(&hatVoice.lfo.modTarget, value); break;
}
```

This is not the same problem as parameter storage layout, and splitting parameter
storage does not by itself fix these call sites. Scoped as a separate, later pass in
Section 6 rather than folded silently into this document.

**The current morph engine assumes one flat array.** `preset_morphTick()` walks a single
cursor `morph_index` from `0` to `END_OF_SOUND_PARAMETERS`, one parameter per tick,
across the whole flat parameter space, and `preset_getMorphValue()` /
`preset_morphSendParameter()` index directly into `parameter_values[]`/`parameters2[]`
by that flat index. This whole concern moves out of `presetManager.c` and into
`presetMorphEngine.c/.h`. Once parameters live in per-slot mirrors, morph needs a
two-dimensional cursor `(slot, local_param)` instead of one flat bound.

---

## 3. Decided design

The current session goal supersedes earlier path/naming assumptions in this
draft: the firmware-side instrument tree is `Core/DSP/Instruments/`, not
`Core/Instrument/`, and the registry/aggregate include is
`InstrumentManager.c/.h`. The migration is allowed to break intermediate
behavior while the ground-up system rebuild is in progress.

### 3.1 Alias semantics

"Alias" means runtime indirection, confirmed: what the user sees in the menu, what
the DSP voice reads, and what the save/load system operates on all depend on which
instrument type currently occupies a given voice slot. There is no compile-time
enum aliasing.

### 3.2 Canonical kit-wide parameter ID space

One registry, not two. Phase 3's step-automation pool and Phase 6's FX addressing
already assume a flat, kit-wide parameter ID space; per-instrument parameters use the
same numbering rather than a second, independently-evolving one.

Layout:

- **64 parameter IDs per voice slot**, fixed offset (`slot × 64`), 6 voice slots =
  384 IDs.
- **128 IDs reserved for FX/general** parameters, addressed the same way: each FX
  type gets its own parameter definitions (general module-range lineup, plus
  modulation/automation validity flags) exactly like an instrument type does.
- Total: 512 IDs. Current real counts (35/34/35/35 params across the four existing
  instrument types, ~209 total including kit-level extras) are nowhere near this, so
  there is no near-term pressure on the space.
- A fixed `slot × 64` offset (rather than a variable-length per-slot lookup) keeps
  slot-start resolution O(1) and matches the existing style of `ModTargetVoiceOffset`
  in `Cc2Text.c`.
- This new parameter ID space replaces the legacy sound `PAR_*` enum for sound
  parameters. Non-sound menu/global/pattern parameters may remain in a separate
  compatibility enum during the transition, but sound storage and sound apply
  should stop depending on legacy `PAR_*` values.

### 3.2a Scene-owned parameter mirrors and aggregate layout

The parameter images belong to each Scene, not to a standalone Preset global.
The first implementation allocates `scene_t scenes[1]` and makes its APIs
scene-indexed from the start. The eventual Bank implementation changes the
scene-count constant to 17 identical SRAM-resident records: 16 Bank scene slots
plus the background-load landing scene described in `SCOPING_TARGETS.md`
Section 2.3.

Each Scene owns scene settings, one `PatternSet`, and one extensible kit record.
The kit record owns kit settings and six instrument slots. Each instrument slot
owns its type, three parameter images, and non-morphed supplemental routing
state:

```c
#define SCENE_COUNT                    1u  /* Later 17u. */
#define KIT_INSTRUMENT_SLOTS           6u
#define INSTRUMENT_PARAM_COUNT         64u

typedef struct {
    uint8_t instrument_parameters[INSTRUMENT_PARAM_COUNT];
    uint8_t morph_instrument_parameters[INSTRUMENT_PARAM_COUNT];
    uint8_t morph_interpolation[INSTRUMENT_PARAM_COUNT];
} instrument_parameter_images_t;

typedef struct {
    uint16_t velocity_target_param;
    uint8_t velocity_amount;
    uint8_t lfo_target_voice;
    uint16_t lfo_target_param;
} instrument_supplemental_t;

typedef struct {
    instrument_type_t type;
    instrument_parameter_images_t parameter_images;
    instrument_supplemental_t supplemental;
} kit_instrument_slot_t;

typedef struct {
    uint8_t audio_out[KIT_INSTRUMENT_SLOTS];
    /* Add future kit-wide settings here. */
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
    /* Other scene-owned settings are added here. */
} scene_settings_t;

typedef struct {
    scene_settings_t settings;
    PatternSet pattern;
    kit_t kit;
} scene_t;

extern scene_t scenes[SCENE_COUNT];
```

`instrument_parameters` is the normal instrument endpoint.
`morph_instrument_parameters` is the edited morph endpoint.
`morph_interpolation` is the Morph worker's derived output and is the parameter
image applied to the DSP engine. It remains resident separately for every Scene
while the firmware is running, but it is never written to an instrument or
Scene file. After load it is rebuilt from the two serialized endpoints and the
Scene's Morph amount.

`PatternSet` is the Scene's single 128-step pattern record. Its historical
`NUM_PATTERN` dimension is removed rather than embedded in a Scene that already
provides the selection dimension. `TempPattern` is deleted; background and
staged loading target a non-playing `scene_t` record, eventually including the
17th landing Scene, rather than a second pattern-only staging shape.

Only audio routing is currently known to be kit-wide state.
`kit_settings_t` remains a distinct extensible member so later kit-wide fields
do not require rearranging instrument-slot ownership. Voice-wide decimation,
the seven tracks' MIDI channels, and the seven tracks' MIDI notes are Scene
settings and must not be placed in `kit_settings_t`, `PatternSet`, or instrument
files. The existing `LengthRotate.midiChannel` and `LengthRotate.midiNote`
members therefore move out of PatternData into `scene_settings_t`. System MIDI
input-channel configuration remains global and is not affected.

`Core/Scene/Preset/ParameterArray.c/.h` may provide temporary compatibility
types/accessors while the legacy flat arrays are removed, but it does not own
the parameter images. The selected `scene_t` owns all stored sound state.

Menu display/edit rules:

- Normal voice editing reads/writes `instrument_parameters[]`.
- `SHIFT+VOICE` morph editing reads/writes `morph_instrument_parameters[]`.
- The menu never displays `morph_interpolation[]`; it is sound-engine state.
- These arrays replace `parameter_values[]` and `parameters2[]` for sound
  parameters. Existing non-sound uses of `parameter_values[]` can be migrated
  separately.

Morphed vs. supplemental rules:

- Velocity modulation target and amount are supplemental; they are not morphed.
- LFO target voice and LFO target parameter are supplemental; they are not
  morphed.
- All other LFO parameters, such as waveform, frequency, amount, retrigger,
  sync, and offset, remain ordinary mirrored voice parameters and are morphed.
- Supplemental routing still participates in validation when instrument type or
  target validity changes, but it does not have morph mirrors.

`preset_applySoundParameter()` remains in Preset conceptually, but its
implementation should apply from the selected Scene's slot/local parameter
model. It should not keep packing legacy MIDI CC/CC2 messages into
`midiParser_ccHandler()` as the long-term sound application path.

### 3.2b Canonical instrument file parameter names

The following lowercase ASCII names are the canonical physical keys in both
`[params]` and `[morph]`. They are the contract shared by instrument files, the
legacy converter, the current `storageTypes.c` bridge parser, and the future
per-instrument descriptors. Names are type-local: the same name may map to a
different internal destination for a different instrument type.

**Drum (`drm`, slots 1-3; 35 keys):**

```text
osc1_wave, osc1_pitch_coarse, osc1_pitch_fine, osc2_wave
filter_freq, filter_reso
amp_envelope_attack, amp_envelope_decay, amp_envelope_slope
pitch_envelope_decay, pitch_envelope_amount, pitch_envelope_slope
osc2_mod_amount, osc2_pitch_coarse, osc2_mod_type
instrument_vol, instrument_pan, instrument_drive, instrument_decimation
filter_drive, filter_type
lfo_rate, lfo_amount, lfo_wave, lfo_target_voice, lfo_target_param
lfo_retrigger_voice, lfo_sync, lfo_offset
velo_vol_on_off, velo_mod_amount, velo_mod_dest
transient_vol, transient_wave, transient_freq
```

**Snare (`snr`, slot 4; 34 keys):**

```text
osc1_wave, osc1_pitch_coarse, osc1_pitch_fine
noise_freq, osc1_noise_mix
filter_freq, filter_reso
amp_envelope_attack, amp_envelope_decay, amp_envelope_slope, amp_attack_repeat
pitch_envelope_decay, pitch_envelope_amount, pitch_envelope_slope
instrument_vol, instrument_pan, instrument_drive, instrument_decimation
filter_drive, filter_type
lfo_rate, lfo_amount, lfo_wave, lfo_target_voice, lfo_target_param
lfo_retrigger_voice, lfo_sync, lfo_offset
velo_vol_on_off, velo_mod_amount, velo_mod_dest
transient_vol, transient_wave, transient_freq
```

**Cymbal (`cym`, slot 5; 35 keys):**

```text
osc1_wave, osc1_pitch_coarse, osc1_pitch_fine
osc2_wave, osc2_pitch_coarse, osc2_mod_amount
osc3_wave, osc3_pitch_coarse, osc3_mod_amount
filter_freq, filter_reso
amp_envelope_attack, amp_envelope_decay, amp_envelope_slope, amp_attack_repeat
instrument_vol, instrument_pan, instrument_drive, instrument_decimation
filter_drive, filter_type
lfo_rate, lfo_amount, lfo_wave, lfo_target_voice, lfo_target_param
lfo_retrigger_voice, lfo_sync, lfo_offset
velo_vol_on_off, velo_mod_amount, velo_mod_dest
transient_vol, transient_wave, transient_freq
```

**Hi-hat (`hat`, slot 6; 35 keys):**

```text
osc1_wave, osc1_pitch_coarse, osc1_pitch_fine
osc2_wave, osc2_pitch_coarse, osc2_mod_amount
osc3_wave, osc3_pitch_coarse, osc3_mod_amount
filter_freq, filter_reso
amp_envelope_attack, amp_envelope_decay_closed, amp_envelope_decay_open
amp_envelope_slope
instrument_vol, instrument_pan, instrument_drive, instrument_decimation
filter_drive, filter_type
lfo_rate, lfo_amount, lfo_wave, lfo_target_voice, lfo_target_param
lfo_retrigger_voice, lfo_sync, lfo_offset
velo_vol_on_off, velo_mod_amount, velo_mod_dest
transient_vol, transient_wave, transient_freq
```

The velocity and LFO target/amount names remain part of the physical instrument
schema during the bridge period. Section 3.5 governs their eventual in-memory
ownership as supplemental, non-morphed routing fields.

### 3.2c Morph engine ownership

Morph code moves out of `Core/Scene/Preset/presetManager.c` into a new file
pair:

```
Core/Scene/Preset/presetMorphEngine.c
Core/Scene/Preset/presetMorphEngine.h
```

The morph engine takes a Scene index and walks `(slot, local_param)` across that
Scene's instrument arrays. It interpolates `instrument_parameters[]` to
`morph_instrument_parameters[]`, writes `morph_interpolation[]`, and asks
Preset's sound-apply layer to apply that local parameter to the DSP voice. It
skips supplemental fields by construction, because they are not in the mirrored
arrays.

### 3.3 Module ranges (categories)

The spec defines general module ranges within each type's own 64-ID block —
e.g. "parameters 0–12 relate to the oscillator", filter, envelope, LFO, mod, etc.
Beyond that, it is left to instrument authors to line up parameters across types in a
way that makes sense, and to the user to review whether an old mod-target/automation
assignment is still meaningful after an instrument swap. The system does not attempt
semantic preservation across swaps — only structural validity (Section 3.4).

### 3.4 Modulation and automation validity, invalidation

Each instrument type defines its own list of which of its parameters are valid
modulation targets and which are valid step-automation targets — two independent
flags per parameter, since the sets are not required to match.

- **Modulation (LFO/velocity) validity**: checked when an instrument is swapped into
  a slot. If the LFO's or velocity modulator's current target parameter is not a
  valid mod target for the newly-loaded type (or is out of range for it), the
  destination reverts to a none/off target — a completely acceptable, expected result. This
  is a structural bounds/flag check against the target instrument's own descriptor
  table, not a semantic "does the old target still make sense" solver.
- **Step-automation validity**: same treatment, a separate `is_stepAutomatable` flag
  per parameter, checked at swap time. This is a different validity index from
  modulation validity, because a parameter can be a legal automation target without
  being a legal LFO/velocity target or vice versa.
- **Why swap-time invalidation is sufficient**: this scenario is specifically the
  "Pattern reused across kits independently of the instruments it was recorded
  against" case. `FILESYSTEM_SPEC.md`'s `Pattern/` pool exists to allow exactly that
  kind of reuse; `Scene/` bundling exists to avoid the problem entirely by saving
  instruments and their automation/routing together as one atomic unit. Users who
  save/load `Scene/` files never hit this path. Users who mix-and-match from
  `Pattern/` are explicitly opting into the risk, and are warned about it; the
  runtime's job is to fail safely (invalidate + fall back to none), not to preserve
  meaning across an arbitrary type swap.

### 3.5 Supplemental routing fields

Velocity and LFO routing fields are Scene/kit-owned supplemental state for each
voice slot, not morphed instrument parameters. They live beside the three
mirrored arrays in `instrument_supplemental_t`:

- Velocity modulation target parameter.
- Velocity modulation amount.
- LFO target voice.
- LFO target parameter.

These fields are still part of a loaded instrument's/kit's sound definition, but
they do not have endpoint/morph/interpolation mirrors and they are not touched by
the morph engine. This is deliberately narrower than the earlier "generic bank"
wording: only the cross-cutting routing fields are supplemental. Other LFO
settings remain ordinary local instrument parameters and are morphed.

The current hardcoded dispatch in `preset_applyLfoModTarget()` and
`preset_applyVelocityModTarget()` still has to be replaced, but apply logic
stays in `Core/Scene/Preset/`; `InstrumentManager` is registry-only.

### 3.6 Descriptor struct

Each instrument type's `<Type>Parameters.c/.h` defines an array of descriptors, one
per parameter the type owns, replacing what are currently three separately
hand-maintained tables that must stay in sync by convention: `storageTypes.c`'s
file-key map, `menu.c`/`MenuText.h`/`Cc2Text.c`'s `Name`/`ModTarg` display tables, and
the not-yet-built automation/modulation validity rules.

```c
typedef struct {
    uint8_t        local_param;      // stable 0..63 ID inside the type/slot block
    const char    *file_key;         // e.g. "osc1_wave" - canonical instrument-file key
    uint8_t        category;         // module range: OSC / FILTER / ENV / LFO / MOD / ...
    uint8_t        short_name;       // existing Name.shortName convention (menu.h Name struct)
    uint8_t        long_name;        // existing Name.longName convention
    uint8_t        default_value;
    uint8_t        min, max;         // menu clamp range
    bool           is_modulatable;   // valid LFO/velocity destination (Section 3.4)
    bool           is_stepAutomatable; // valid step-automation target (Section 3.4)
    uint8_t        value_owner;      // parameter image or a named supplemental field
    uint8_t        menu_page;        // instrument subpage placement
    uint8_t        menu_position;    // cell within the subpage
    uint8_t        dtype_category;   // see Section 4 - replaces the packed menuId nibble
    dtype_source_t  dtype_source;    // static list or runtime resolver, never an untagged void pointer
} ParamDescriptor;
```

`min`/`max` and `default_value` were not explicitly requested but are needed
constantly by the real code today (menu clamping, `preset_resetKitToDefaults()`-style
logic) — including them means instrument authors do not have to hand-write clamp
logic separately per type.

`local_param` is explicit because descriptors are compact arrays containing
only real parameters, while module-aligned local IDs may contain gaps or omit a
parameter that another type has. Descriptor array position must not silently
become the saved/routed ID. `value_owner` tells storage/menu/apply code whether
the key addresses one of the three parameter images or one of the four
supplemental fields; without it, the descriptor-driven instrument parser could
not honor Section 3.5. `dtype_source` is a tagged static-list/runtime-resolver
union rather than `const void *`, resolving the open point found in the live
wavetable/sample-name path without unsafe casts. `menu_page` and
`menu_position` make descriptors own instrument-specific Menu placement as well
as labels; retaining placement in `menuPages.h` would leave another
independently synchronized instrument table.

The struct is intentionally close to a flat, human-readable table —
`{param_name, category, short_name, long_name, is_modulatable, is_stepAutomatable, ...}`
— so instrument authors can define a whole type's parameter set as one literal array
without cross-referencing separate menu/storage/routing tables by hand.

### 3.7 Directory layout

```
Core/DSP/Instruments/
├── InstrumentManager.c/.h   ← known instrument registry + aggregate include
├── Drum/
│   ├── DrumVoice.c/.h        ← moved from Core/DSPAudio/
│   └── DrumParameters.c/.h   ← new: ParamDescriptor table + dtype lists for Drum
├── Snare/
│   ├── Snare.c/.h            ← moved from Core/DSPAudio/
│   └── SnareParameters.c/.h
├── Cymbal/
│   ├── CymbalVoice.c/.h      ← was Core/DSPAudio/CymbalVoice.c/.h
│   └── CymbalParameters.c/.h
└── HiHat/
    ├── HiHat.c/.h            ← was Core/DSPAudio/HiHat.c/.h
    └── HiHatParameters.c/.h
```

The mechanical relocation itself is low-risk if it is kept as a source-layout
move: update include paths and the Makefile, then preserve existing public type
and function names. The parameter-definition relocation is the first real design
step. `storageTypes.c`'s existing parameter maps are already organized as one set
of named fields per instrument type and are already validated against real `.SND`
files (Section 1), so moving that data into `<Type>Parameters.c/.h` is mostly
relocation of already-correct storage keys. The redesign risk is in the
addressing/routing/apply mechanism above, especially because
`preset_applySoundParameter()` still applies values through `midiParser_ccHandler()`,
whose switch directly references fixed globals like `voiceArray[0..2]`,
`snareVoice`, `cymbalVoice`, and `hatVoice`.

File names are preserved. `InstrumentManager` catalogs those names and known
instrument types so future instruments can be added by registering a new folder
and descriptor, not by renaming the existing codebase.

`InstrumentManager` is registry-only in this phase: known type list,
type/extension/name lookup, descriptor lookup, and aggregate includes. Live
parameter application and DSP field writes remain owned by Preset.

Memory shape for "any type in any slot" still needs implementation detail, but
it does not belong in `InstrumentManager` for this phase.

### 3.7a SD instrument file slot ownership

Target format: instrument files do not store slot. Slot assignment belongs only
in `kitset.kcg`, through `[slotN] type=<type> file=<filename> audio_out=<n>`.
The instrument file proves only its own file schema/type and carries `[params]`
and `[morph]` values by local parameter key.

Current reality as of this audit: generated instrument files do not contain
`slot=N`, and `storageTypes.c` no longer requires it. The parser selects the
destination slot from `kitset.kcg`, not from the instrument file body.

- map file keys through the loaded instrument type's local descriptor table,
  then into the kit slot's `slot * 64 + local_param` storage.

### 3.7b SD instrument file metadata and payload shape

Current `tools/convert_legacy_kits.py` writes the following non-parameter
content into each instrument file:

```text
format=helicase.instrument
version=1
type=<drm|snr|cym|hat>

[params]
...
[morph]
...
```

Instrument files must not store `kit_name`, `source_name`, or `source_file`.
Kit display names are directory-owned; instrument/file names are directory
entries referenced by `kitset.kcg`; legacy provenance is conversion debug data,
not runtime sound data. `type=...` remains as the schema-level cross-check.

Execution rule for this audit: do not change the on-card kit/instrument text
schema. The current named-key sections remain the file format:

- `kitset.kcg` stays as `format`, `version`, and six `[slotN]` sections with
  `type`, `file`, and `audio_out`.
- Instrument files stay as `format`, `version`, `type`, `[params]` named
  values, and `[morph]` named values.
- No `_padNN`, `p00`...`p63`, or other physical 64-entry rows are added to SD
  files in this pass.

The 64-entry concept in this document is internal storage/addressing capacity:
each loaded slot gets a fixed 64-ID block in its Scene-owned images and the kit-wide
parameter ID space. Descriptor tables map today's stable file keys (for example
`osc1_pitch_coarse`, `filter_freq`, `transient_vol`) into those local internal indices.
Unused local indices simply do not appear in the file.

### 3.8 Parameter index width

The current bridge `Step.param1Nr`/`param2Nr` fields and pattern-file records
are eight-bit, so they cannot represent the decided 0..511 canonical ID space.
Until Phase 3 replaces `Step` with the dynamic event pool, the bridge fields
must be widened to `uint16_t`, their no-automation sentinel must become
`0xffff`, and the one-pattern file format must serialize each destination as
two little-endian bytes. Legacy eight-pattern files retain a dedicated import
mapping from old eight-bit IDs.

- The old `menu.h` `ModTarg.param` field is already `uint16_t`, but `ModTarg`
  itself is removed when descriptors become the target registry.
- The actual cost is Phase 3's dynamic-event-pool automation entries, packed as
  9-bit param + 7-bit value in exactly 2 bytes. If the registry in 3.2 needs to grow
  past 512 entries, that entry either grows to 3 bytes (param ID gets full room,
  value stays 7-bit) or the value field shrinks (unattractive — 128 levels is already
  the coarse end for smooth automation). At 3 bytes, the realistic "every step has at
  least one automation" capacity drops from roughly 7,295 to roughly 4,863 entries —
  about a third less. This is only worth revisiting if/when instrument-type count
  growth pushes total parameters toward 512; today's ~209 leaves substantial headroom.

---

## 4. Code dive: current menu text/list rendering system

Requested to determine what it takes to make instrument descriptors (Section 3.6) the
canonical source for menu rendering, and to surface risks/affiliates in doing so.

### 4.1 What exists today

- `Core/Menu/menu.h` already defines `Name { shortName, category, longName }` and
  `ModTarg { nameIdx, param }` structs, plus `ModTargetVoiceOffset { start, end }`.
  These are effectively an early, partial version of the descriptor concept in 3.6,
  but scoped only to mod-target display, not general parameter definition.
- `Core/Menu/Cc2Text.c` holds `modTargets[]` — the single flat, 205-entry, kit-wide
  table described in Section 2 — plus `modTargetVoiceOffsets[6]` (fixed
  per-voice start/end ranges into that table) and `voiceFromModTargValue()`, which
  resolves a mod-target index back to its owning voice slot via a binary-search-style
  cascade of range comparisons.
- `Core/Menu/MenuText.h` holds ~15 separate `static const char xxxNames[][4]` tables
  (`transientNames`, `filterTypes`, `lfoWaveNames`, `waveformNames`, `ppqNames`,
  etc.), each self-describing its own length in slot `[0][0]`.
- Every sound parameter has one packed byte in `parameter_dtypes[NUM_PARAMS]`
  (`Core/Menu/menu.c`): the low nibble is `enum Datatypes` (`DTYPE_0B255` … 
  `DTYPE_0B15`, 15 values), the high nibble is a `menuId` (1–15, defined in
  `MenuText.h` as `MENU_FILTER` … `MENU_EXT_SYNC`) used only when the low nibble is
  `DTYPE_MENU`. `getMaxEntriesForMenu(menuId)` and `getMenuItemNameForValue(menuId, …)`
  in `menu.c` are plain `switch (menuId)` statements dispatching into the static
  tables in `MenuText.h`.

### 4.2 The existing precedent for a runtime-populated list

`MENU_WAVEFORM` is the one case in the current code that already needs exactly what
the user asked about — a list populated at runtime, not fully known at compile time
(the wavetable-oscillator-name-list example). It works like this:

```c
case MENU_WAVEFORM:
    return (uint8_t)((uint8_t)waveformNames[0][0] + menu_numSamples);
```

`menu_numSamples` is a module-global (`Core/Menu/menu.c`), set via
`menu_setNumSamples(n)` whenever the sample library is (re)scanned. Values below the
static `waveformNames[]` count resolve to a compiled-in name; values at or above it
fall through to `menu_formatSampleShortName()`, which synthesizes a `s01`…`sZ9`-style
label from the runtime sample index instead of a table lookup.

This confirms the pattern is workable, but it is a one-off special case
hand-written into the `switch`, not a generic mechanism — there's no way today to add
a second runtime-populated list (e.g. a per-instrument wavetable list) without adding
another hardcoded branch of the same shape.

### 4.3 Risk found: the menuId nibble is already at capacity

`MENU_FILTER` through `MENU_EXT_SYNC` in `MenuText.h` already number 1–15 — every
value a 4-bit nibble can hold besides 0. This is the single largest blocker to
folding `dtype_category`/`dtype_source` (Section 3.6) into the existing
`parameter_dtypes[]` packed-byte scheme as-is: there is no room left in the current
encoding to add new dtype/list categories, whether general or per-instrument, without
widening the field. This needs to be resolved as part of this refactor, not treated
as pre-existing headroom.

**Recommended resolution**: don't keep a single kit-wide `menuId` nibble at all.
`ParamDescriptor.dtype_category` (Section 3.6) becomes an enum scoped per instrument
type (or a small shared set of generic categories — numeric ranges, on/off, note
name — plus an `INSTRUMENT_LIST` category), and
`ParamDescriptor.dtype_source` is a tagged static-table/runtime-resolver source
rather than an index into one shared, capacity-limited registry. This removes
the ceiling and supports runtime-populated lists without unsafe function/data
pointer casts.

### 4.4 Affiliates that need to change to make descriptors canonical

- **`MenuText.h`**: the ~15 static tables that are genuinely generic (filter types,
  LFO waveforms, sync rates, MIDI modes, PPQ, etc. — not instrument-specific) stay
  as shared lists referenced by descriptor `dtype_source`. Tables that are really per-instrument
  concerns (e.g. any list describing a specific voice's oscillator waveform set)
  move into that type's `<Type>Parameters.c/.h` instead.
- **`Cc2Text.c`**: `modTargets[]`/`modTargetVoiceOffsets[]`/`voiceFromModTargValue()`
  are superseded by the per-type `is_modulatable` flag (3.4/3.6) plus the kit-wide ID
  space (3.2) — a target's "which voice does this belong to" becomes `id / 64`
  instead of a fixed-range table walk, and "is this a legal target" becomes a
  descriptor-table lookup on whatever type currently owns that slot, instead of a
  static, compile-time-fixed list.
- **`menu.c`**: `getMaxEntriesForMenu()` / `getMenuItemNameForValue()` /
  `menu_displayModTargetFull()` / `menu_displayModTargetShort()` all need to resolve
  through "whatever instrument type is loaded in this voice slot's descriptor table"
  instead of a flat switch on a global id. `menu_numSamples` /
  `menu_setNumSamples()` / `menu_formatSampleShortName()` generalize into the
  tagged `dtype_source` runtime resolver described in 4.3, rather than remaining a
  wavetable-only special case.
- **`Core/Scene/Preset/ParameterArray.c/.h`**: the old flat pointer map and
  sound-parameter enum are retired as sound-state owners. Temporary
  compatibility accessors may live here during migration, but the per-slot
  images described in Section 3.2a are physically owned by `scene_t`. Sound
  menu display/edit paths read/write the selected Scene instead of the sound
  portion of `parameter_values[]`/`parameters2[]`.
- **`Core/Scene/Preset/presetManager.c`**: live sound apply remains in Preset,
  but stops packing legacy CC/CC2 messages into `midiParser_ccHandler()` as the
  core sound path. `preset_applyLfoModTarget()` and
  `preset_applyVelocityModTarget()` apply supplemental routing fields from the
  selected Scene's kit/instrument slot records.
- **`Core/Scene/Preset/presetMorphEngine.c/.h`**: new owner for morph walking,
  interpolation, and active-interpolated array updates. `presetManager.c`
  delegates morph work to this file pair.
- **`Core/Hardware/SD/storageTypes.c` and `tools/convert_legacy_kits.py`**:
  keep the current on-card text schema stable. The parser should use
  descriptor tables as the canonical key map, but generated files remain named
  `[params]`/`[morph]` sections rather than physical 64-row dumps.

This is scoped work, not a small edit — flagged explicitly as its own migration
step rather than something that falls out for free once `ParamDescriptor` exists.

---

## 5. Scope not covered by this document

Per Section 2, splitting parameter storage does not by itself fix every hardcoded
`switch (voiceSlot)`-style assumption elsewhere in the codebase. A dedicated later
audit — "find every place that assumes a fixed type occupies a fixed voice slot,"
covering at minimum `menu.c`, `sequencer.c`, and `mixer.c` beyond what Section 4.4
already lists for the menu/preset layer — is needed before slot-flexible instrument
swapping is fully realized. This document intentionally does not claim to deliver
that pass.

This does not soften the decision to rebuild the parameter system now. It only
records that DSP scheduling/mixing and trigger dispatch can be converted after
the registry, storage mirrors, parser, menu display/edit, and Preset apply
interfaces exist.

---

## 6. Summary of decisions locked in this pass

| Question | Decision |
|---|---|
| Alias meaning | Runtime indirection via per-type descriptor tables; not a compile-time C alias |
| Parameter ID space | One canonical, kit-wide, flat ID space shared by parameters, mod routing, step automation, and FX addressing |
| ID layout | 64 IDs/voice × 6 voices = 384, + 128 for FX/general = 512 total, fixed `slot × 64` offset |
| Mod-target/automation validity | Two independent per-parameter flags (`is_modulatable`, `is_stepAutomatable`) owned by each instrument type |
| Invalidation timing | Swap-time only; falls back to none/off. Not a semantic-preserving remap |
| Invalidation scope | Applies to `Pattern/`-pool reuse across kits; `Scene/` bundling avoids the issue by design |
| Decisiveness | Replace the legacy sound parameter path now; intermediate breakage is acceptable during the rebuild |
| Scene allocation | Start with `scene_t scenes[1]` and scene-indexed APIs; later increase the identical resident record count to 17 |
| Scene aggregate | Each Scene owns scene settings, one single-pattern `PatternSet`, and an extensible kit containing kit settings plus six instrument slots |
| Pattern storage | Remove the historical `NUM_PATTERN` dimensions from `PatternSet`; delete `TempPattern`; loading stages through a non-playing Scene |
| Scene settings | Voice-wide decimation and seven-track MIDI channel/note values are Scene-owned; system MIDI input channels remain global |
| Kit settings | Audio routing is the only current kit-wide setting; retain a distinct extensible `kit_settings_t` |
| Instrument parameter storage | Each Scene's kit/instrument slot owns `instrument_parameters[]`, `morph_instrument_parameters[]`, and runtime-only `morph_interpolation[]` |
| Interpolation persistence | `morph_interpolation[]` is retained per resident Scene but is derived runtime state and is never serialized |
| Descriptor addressing | Every descriptor carries an explicit stable `local_param`; compact descriptor-array position is not an ID |
| Descriptor value ownership | Every descriptor identifies parameter-image vs. supplemental storage so named file keys land in the correct Scene field |
| Supplemental routing | Velocity target/amount and LFO target voice/parameter are non-morphed supplemental fields inside the per-slot record |
| Morphed LFO fields | All other LFO parameters stay in mirrored voice parameter arrays and are morphed |
| Morph engine | Move morph walking/interpolation out of `presetManager.c` into `presetMorphEngine.c/.h` |
| Live apply ownership | Preset owns live sound apply; `InstrumentManager` is registry-only |
| InstrumentManager scope | Known type registry, file-name/type catalog, descriptor lookup, aggregate include |
| Descriptor struct | `ParamDescriptor` per Section 3.6, replacing `storageTypes.c` key map + menu display tables + (new) validity rules as three independently-synced tables |
| Directory layout | `Core/DSP/Instruments/InstrumentManager.c/.h` plus `Core/DSP/Instruments/<Type>/{existing voice filename.c/.h, <Type>Parameters.c/.h}` |
| File names | Preserve existing voice source/header names; register them in `InstrumentManager` |
| SD instrument slot | Format has no instrument-file `slot=`; only `kitset.kcg` assigns files to slots |
| SD instrument metadata | Instrument files store only `format`, `version`, and `type` before section data |
| SD file schema during this audit | Stable: no on-card schema change; current named-key `kitset.kcg` and instrument `[params]`/`[morph]` files remain valid |
| SD instrument payload | Named parameter keys remain the physical file payload; descriptor tables map those keys into internal 64-slot blocks |
| Bridge parameter index | Widen current `Step` automation destinations and the new one-pattern file record to 16-bit; Phase 3 later packs the decided 9-bit ID |
| Menu dtype/list encoding | Replace the full packed 4-bit `menuId` nibble with descriptor `dtype_category` plus a tagged static-list/runtime-resolver `dtype_source` |

---

## 7. Remaining open questions and required follow-ups

1. Whether FX types (Section 3.2's 128-ID reserved block) are specified in this same
   pass or deferred to a Phase 6-scoped follow-up — this document assumes the same
   descriptor shape applies but does not enumerate any FX parameters.
2. Required storage follow-up: keep converter/parser schema-compatible while
   moving the key maps into descriptor tables; do not add physical 64-row
   instrument files in this audit.
3. Track the Section 5 hardcoded voice-slot switch audit as a named follow-up,
   especially for `mixer.c`, `sequencer.c`, `MidiVoiceControl.c`, `lfo.c`, and
   `modulationNode.c`.
