# 8-bit Instrument Parameter Refactor Plan

## Implementation Notes

Status as of this pass:

- Resident instrument images now use `instrument_param_value_t` byte arrays in
  `SceneData`.
- Target selector rows retain `instrument_target_token_t` values, with
  `0xff` as off.
- LFO `lfo_target_voice=7` is the retained Scene namespace and still displays
  as `scn` in Menu.
- Velocity destinations are self-scoped descriptor tokens plus one explicit
  source-voice Morph token (`0x40`). Velocity does not browse arbitrary Scene
  targets.
- `SceneModTargets` exposes ID/index helpers so LFO Scene namespace storage can
  keep a byte token while runtime/display code expands to canonical Scene target
  IDs.
- Instrument text parser/writer now treats every descriptor row as byte-domain.
  The checked-in `SD_CARD/` tree was migrated from `65535`/packed target values
  to byte tokens; broad legacy packed-ID compatibility was intentionally not
  kept because the project controls all current files.
- Pattern automation target IDs are unchanged and remain the Phase 4 rewrite's
  responsibility.

## Goal

Resident instrument parameter images should store byte values only. No ordinary
instrument parameter needs more than one byte. The current `uint16_t` storage is
only needed because modulation target identifiers were stored directly in the
same generic parameter image arrays.

This refactor removes that mixed-use storage. Velocity and LFO target selector
rows become explicit byte tokens:

- `0x00..0x3f`: local descriptor index within the selected target namespace.
- `0xff`: off.
- LFO target namespace comes from the paired `lfo_target_voice` or
  `lfo_target_voice_2` byte.
- Velocity target namespace is always the source instrument itself.
- Future non-voice targets, such as effects, use new `lfo_target_voice` namespace
  values above the six instrument voices. They do not require a wider target
  parameter cell.

Canonical 16-bit target IDs may still exist as transient runtime identities for
automation, display, and installed modulation nodes. They must not be stored in
resident instrument parameter images.

## Target Encoding Contract

Add this storage contract near the instrument target definitions in
`Core/DSP/Instruments/InstrumentManager.h`.

Comment-ready description:

```c
/*
 * Byte-sized retained instrument parameter values.
 *
 * Instrument files, SceneData endpoint images, Morph interpolation, and
 * descriptor-backed menu edits all store one byte per descriptor cell. Normal
 * musical parameters use their existing 0..127 or 0..255 UI domain. Target
 * selector cells store compact local tokens instead of packed canonical target
 * IDs so the same arrays never need to widen for routing metadata.
 */
typedef uint8_t instrument_param_value_t;

/*
 * Retained modulation target token.
 *
 * Target parameter selector rows store only a local descriptor index in the
 * selected namespace. 0xff is the sole off value. For LFO targets the namespace
 * is supplied by lfo_target_voice/lfo_target_voice_2. For velocity targets the
 * namespace is always the source instrument slot itself. Runtime code expands
 * the byte token into a canonical instrument_param_id_t only while installing or
 * displaying a modulation target.
 */
typedef uint8_t instrument_target_token_t;
#define INSTRUMENT_TARGET_TOKEN_OFF 0xffu
#define INSTRUMENT_TARGET_TOKEN_MAX_LOCAL ((uint8_t)(INSTRUMENT_PARAM_COUNT - 1u))

/*
 * LFO target namespace values stored in lfo_target_voice cells.
 *
 * Values 1..6 select instrument voices. Additional values are reserved for
 * non-voice target namespaces such as Scene/effect targets. Keeping namespace
 * selection in this byte lets lfo_target_param remain a local 0..63 token.
 */
#define INSTRUMENT_TARGET_VOICE_FIRST 1u
#define INSTRUMENT_TARGET_VOICE_LAST  INSTRUMENT_SLOT_COUNT
#define INSTRUMENT_TARGET_VOICE_SCENE ((uint8_t)(INSTRUMENT_SLOT_COUNT + 1u))
```

Keep these values wide:

- `instrument_param_id_t`: canonical runtime identity, currently slot/local and
  Scene/effect target namespace.
- `scene_mod_target_id_t`: Scene target runtime identity.
- Pattern automation target IDs in `PatternData.h` and future dynamic Pattern
  storage. Those are identity fields, not resident instrument parameter values.

## Phase 1: Reshape Velocity Targeting

Velocity target storage should be self-only. It should no longer store Scene
target IDs or packed `slot * 64 + local` IDs.

### `Core/DSP/Instruments/InstrumentManager.h`

Change:

- `instrumentManager_targetValidForVelocitySource(..., uint16_t target_id)`
  to accept `instrument_target_token_t token`.
- `instrumentManager_stepVelocityTargetForSource(...)` to accept and return
  `instrument_target_token_t`.

Add helper prototypes:

```c
uint8_t instrumentManager_targetTokenValidForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_target_token_t token,
    instrument_target_use_t use);

instrument_param_id_t instrumentManager_targetIdFromTokenForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_target_token_t token,
    instrument_target_use_t use);

instrument_target_token_t instrumentManager_targetTokenFromIdForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_param_id_t id,
    instrument_target_use_t use);

instrument_target_token_t instrumentManager_stepTargetTokenForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_target_token_t current,
    int8_t direction,
    instrument_target_use_t use);
```

Comment-ready description for these helpers:

```c
/*
 * Convert between retained target tokens and canonical runtime target IDs.
 *
 * Retained storage keeps only a byte-sized local descriptor token. Runtime
 * modulation code still needs a canonical target ID because installed nodes,
 * automation, and display helpers share one target identity namespace. These
 * helpers are the only place that may combine a target slot with a local token
 * to build a wider id.
 */
```

### `Core/DSP/Instruments/InstrumentManager.c`

Update these functions:

- `instrumentManager_targetLocalValid()` remains useful, but should be called by
  the token helpers instead of by menu code that already has a canonical ID.
- `instrumentManager_stepTargetForSlot()` can remain for canonical runtime/UI
  callers, but `instrumentManager_stepTargetTokenForSlot()` should be the menu
  and storage-facing version.
- `instrumentManager_targetValidForVelocitySource()` should validate:
  - `INSTRUMENT_TARGET_TOKEN_OFF`
  - `0..63` only when that local descriptor is modulatable on `source_slot`
  - no Scene target IDs and no cross-slot target IDs.
- `instrumentManager_stepVelocityTargetForSource()` should walk only:
  - one off entry
  - valid modulatable local descriptors for the source slot.

Remove the Scene target branch from the velocity browse path:

- Remove calls to `sceneModTarget_valid(..., SCENE_MOD_TARGET_USE_VELOCITY)` from
  velocity validation and stepping.
- Remove Scene-target traversal from velocity stepping.
- Keep Scene target runtime support only if some other caller still uses it; it
  should not be reachable through `velo_mod_dest` storage.

Comment-ready replacement for velocity validation:

```c
/*
 * Validate a retained velocity destination token for one source voice.
 *
 * Velocity is intentionally self-scoped: the source voice's trigger velocity can
 * modulate only descriptor targets on that same instrument slot. The retained
 * byte is therefore either off or a local descriptor index. Cross-voice,
 * Scene-level, and future effect destinations belong to LFO target namespaces,
 * where a separate target-voice byte already exists.
 */
```

### `Core/Menu/menu.c`

Update these velocity helpers:

- `menu_velocityTargetNormalize()`
- `menu_velocityTargetEditParam()`
- `menu_velocityTargetDisplayValue()`

They should read and commit byte tokens, not canonical IDs. Display should expand
the token to a canonical ID only for formatting:

```c
instrument_param_id_t display_id =
    instrumentManager_targetIdFromTokenForSlot(scene_getActiveIndex(),
                                               cell->slot,
                                               token,
                                               INSTRUMENT_TARGET_MODULATION);
```

Comment-ready description:

```c
/*
 * Velocity target editing uses retained local tokens.
 *
 * The stored velo_mod_dest byte is not a global target ID. It is either off or a
 * local descriptor index on the same source voice. Display expands the token to
 * a canonical ID only long enough to reuse the existing descriptor label
 * formatter; commits write the compact token back to SceneData.
 */
```

### `Core/Bank/Scene/Preset/presetManager.c`

Update:

- `preset_normalizeSlotModulationTargets()`
- `preset_applyKitVoiceSupplemental()`
- `preset_setSupplementalParameter()`

For `INSTRUMENT_BIND_VELOCITY_TARGET`, validation should be token-based. Runtime
apply should expand:

```c
instrument_param_id_t target_id =
    instrumentManager_targetIdFromTokenForSlot(scene_index,
                                               slot,
                                               token,
                                               INSTRUMENT_TARGET_MODULATION);
```

Then pass `target_id` to the existing runtime installer.

Comment-ready description:

```c
/*
 * Supplemental target cells are retained as byte tokens.
 *
 * Velocity target rows store a local self-target token, and LFO target rows
 * store a local token interpreted through their paired target-voice row. Preset
 * validates and retains the byte value, then expands it to a canonical target ID
 * only while applying the active Scene's runtime modulation graph.
 */
```

## Phase 2: Reshape LFO Targeting

LFO target storage already has enough information split across two cells:

- `lfo_target_voice`: namespace selector, currently voice 1..6 plus future
  namespaces above 6.
- `lfo_target_param`: local descriptor token, `0..63` or `0xff` off.

No `lfo_target_param` cell should store a packed canonical target ID.

### `Core/DSP/Instruments/InstrumentManager.h`

Add helper prototypes:

```c
uint8_t instrumentManager_lfoTargetVoiceValid(uint8_t voice);

instrument_param_id_t instrumentManager_lfoTargetIdFromToken(
    uint8_t scene_index,
    uint8_t source_slot,
    uint8_t target_voice,
    instrument_target_token_t token,
    instrument_target_use_t use);

instrument_target_token_t instrumentManager_lfoTargetTokenFromId(
    uint8_t scene_index,
    uint8_t target_voice,
    instrument_param_id_t id,
    instrument_target_use_t use);

instrument_target_token_t instrumentManager_stepLfoTargetToken(
    uint8_t scene_index,
    uint8_t target_voice,
    instrument_target_token_t current,
    int8_t direction,
    instrument_target_use_t use);
```

Comment-ready description:

```c
/*
 * LFO target conversion uses an explicit namespace byte.
 *
 * lfo_target_voice chooses the target namespace. Voice values 1..6 address
 * instrument slots and lfo_target_param stores a local descriptor index in that
 * slot. Future values above the instrument voice range can address Scene,
 * effect, or other target tables while keeping lfo_target_param byte-sized.
 */
```

### `Core/DSP/Instruments/InstrumentManager.c`

Update:

- `instrumentManager_installLfoModulationTarget()`: it can keep accepting a
  canonical ID internally, but no retained storage caller should pass one
  directly.
- `instrumentManager_writeRuntimeInternal()` cases:
  - `INSTRUMENT_BIND_LFO_TARGET_VOICE`
  - `INSTRUMENT_BIND_LFO_TARGET_VOICE_2`
  - `INSTRUMENT_BIND_LFO_TARGET_PARAM`
  - `INSTRUMENT_BIND_LFO_TARGET_PARAM_2`

For `LFO_TARGET_PARAM`, find the paired target voice cell, expand token +
target_voice to a canonical ID, then call `instrumentManager_installLfoModulationTarget()`.

Implementation detail:

- Use `instrumentManager_descriptorIndexForBinding()` to find the sibling voice
  row for pair 1 or pair 2.
- Read the sibling byte from
  `scene->kit.instruments[source_slot].parameter_images.instrument_parameters[]`.
- Clamp invalid target voices to a defined default, preferably self or voice 1,
  and force the param token to off when the namespace is invalid.

Comment-ready description:

```c
/*
 * Install an LFO destination from retained byte cells.
 *
 * The parameter cell no longer carries a packed destination ID. The paired
 * target-voice cell supplies the namespace, and the parameter byte supplies a
 * local index or off. This function expands the pair only for runtime
 * installation, preserving one-byte SceneData storage.
 */
```

### `Core/Menu/menu.c`

Update the LFO context and edit helpers:

- `menu_lfo_target_context_t`
  - change `target_param` from `uint16_t` canonical value to
    `instrument_target_token_t target_param_token`.
  - keep optional local temporary canonical ID fields only for display.
- `menu_lfoTargetNormalizeParam()`
  - rename to `menu_lfoTargetNormalizeToken()`
  - input/output should be `instrument_target_token_t`.
- `menu_lfoTargetCommitVoiceAndReconcile()`
  - changing target voice should preserve the local token only if valid in the
    new namespace; otherwise write `INSTRUMENT_TARGET_TOKEN_OFF`.
- `menu_lfoTargetEditParam()`
  - use `instrumentManager_stepLfoTargetToken()`.
- `menu_lfoTargetDisplayValue()`
  - expand token to canonical ID for label formatting only.

Comment-ready description:

```c
/*
 * LFO target editing stores local target tokens.
 *
 * The target parameter cell is interpreted only with its sibling target-voice
 * cell. Edits walk the selected namespace and commit one byte. Display expands
 * the byte token into the canonical target namespace only so existing target
 * label formatters can render the selected destination.
 */
```

## Phase 3: Update Storage Parser and Writer

Instrument text files currently contain old wide values such as:

```text
velo_mod_dest=65535
lfo_target_voice=0
lfo_target_param=65535
```

The new saved form should be:

```text
velo_mod_dest=255
lfo_target_voice=1
lfo_target_param=255
```

or, for a real target:

```text
lfo_target_voice=3
lfo_target_param=12
```

where parameter `12` means local descriptor 12 on voice 3.

### `Core/Hardware/SD/storageTypes.c`

Update:

- `storage_formatAssignmentU16()`
  - keep it if still used by non-parameter fields such as `active_scene`.
  - do not use it for instrument parameter rows.
- `storage_valueForInstrumentSaveSection()`
  - return `instrument_param_value_t` or `uint8_t`.
- `storage_formatInstrumentLineView()`
  - emit byte values for every descriptor row.
- `storage_instrumentParseLine()`
  - remove the special target-row direct `uint16_t` store.
  - parse target selector rows through a compatibility converter.

Add local helpers:

```c
static storage_status_t storage_parseInstrumentValueU8(
    const char *value,
    instrument_param_value_t *out);

static storage_status_t storage_parseLegacyTargetToken(
    const storage_instrument_state_t *state,
    const ParamDescriptor *descriptor,
    kit_instrument_slot_t *slot,
    const char *value,
    instrument_target_token_t *token_out);
```

Compatibility conversion rules:

- `65535` maps to `INSTRUMENT_TARGET_TOKEN_OFF`.
- `255` maps to `INSTRUMENT_TARGET_TOKEN_OFF`.
- `0..63` maps directly to that local token.
- Old packed voice parameter IDs map to `instrumentParam_local(old_id)`.
  - For LFO target rows, the paired `lfo_target_voice` determines the target
    slot. If the old packed ID's slot disagrees with the paired voice, prefer the
    explicit paired voice and preserve only the local descriptor index when valid.
  - For velocity target rows, accept only old IDs whose slot equals the source
    slot. Cross-slot IDs become off.
- Old Scene target IDs:
  - For velocity, become off because velocity is self-only.
  - For LFO, map to the reserved Scene namespace by writing the paired target
    voice cell to `INSTRUMENT_TARGET_VOICE_SCENE` and storing the Scene target
    index in the param token, if Scene targets are still kept as an LFO namespace.

Comment-ready description:

```c
/*
 * Parse retained byte target tokens with legacy compatibility.
 *
 * Version-1 files may contain packed 16-bit target IDs or 65535 for off. The
 * resident Scene image no longer stores those IDs. This parser converts old
 * target text into the byte token contract: off, local descriptor index, and an
 * explicit LFO namespace supplied by the paired target-voice row.
 */
```

### `Core/Hardware/SD/storageTypes.h`

Update comments for instrument parse/write:

- Instrument values are byte-domain.
- Target selector rows are byte tokens.
- Wider target IDs are a runtime compatibility/input concern only.

Comment-ready description:

```c
/*
 * Instrument files store byte-domain descriptor values.
 *
 * Normal parameters write their UI byte. Target selector rows write compact
 * local tokens instead of canonical runtime IDs. The loader accepts old 65535
 * and packed target IDs for compatibility, but parsed SceneData always receives
 * one-byte values.
 */
```

## Phase 4: Change Resident Storage Types

### `Core/Bank/Scene/SceneData.h`

Change:

```c
uint16_t instrument_parameters[INSTRUMENT_PARAM_COUNT];
uint16_t morph_instrument_parameters[INSTRUMENT_PARAM_COUNT];
uint16_t morph_interpolation[INSTRUMENT_PARAM_COUNT];
```

to:

```c
instrument_param_value_t instrument_parameters[INSTRUMENT_PARAM_COUNT];
instrument_param_value_t morph_instrument_parameters[INSTRUMENT_PARAM_COUNT];
instrument_param_value_t morph_interpolation[INSTRUMENT_PARAM_COUNT];
```

Update the block comment.

Comment-ready replacement:

```c
/*
 * Descriptor-indexed byte images for one kit slot.
 *
 * instrument_parameters[] is the main endpoint loaded from [params] and edited
 * in normal VOICE mode. morph_instrument_parameters[] is the Morph endpoint
 * loaded from [morph] and edited through SHIFT+VOICE. morph_interpolation[] is
 * the runtime byte image produced by the Morph worker. Descriptor rows that
 * select modulation destinations store compact byte tokens; canonical target IDs
 * are expanded only by InstrumentManager when runtime targets are installed.
 */
```

Expected static saving:

- Current one resident Scene: `6 slots * 64 params * 3 arrays * 1 byte = 1152`
  bytes saved.
- Future 16 resident Scenes: `18,432` bytes saved.
- Future 17 resident/staging Scenes: `19,584` bytes saved.

### `Core/DSP/Instruments/InstrumentManager.c`

Update reset defaults:

- Normal bytes stay `0`.
- `lfo_target_voice` defaults to `1` or self, based on the desired default.
- target param rows default to `INSTRUMENT_TARGET_TOKEN_OFF`, not
  `INSTRUMENT_PARAM_INVALID`.

Current site:

- `instrumentManager_resetSlot()`.

Comment-ready description:

```c
/*
 * Supplemental target selector defaults.
 *
 * Target voice rows default to a valid namespace. Target parameter rows default
 * to the retained byte off token. Zero is a valid local descriptor token, so off
 * must be 0xff rather than the cleared memset value.
 */
```

### `Core/Bank/Scene/Preset/presetMorphEngine.c`

Update:

- `presetMorph_interpolate()` should accept and return
  `instrument_param_value_t`.
- Internal arithmetic may stay `int32_t`.
- The local `value` in `presetMorph_tick()` should be byte-sized.
- Calls to `preset_applyInstrumentRuntimeValue()` pass a byte.

Comment-ready description:

```c
/*
 * Interpolate Morph endpoints in byte storage space.
 *
 * Morph amount is 0..255, but descriptor endpoint values remain one byte. The
 * worker promotes to signed arithmetic only to round the interpolation and then
 * stores the byte result in morph_interpolation[].
 */
```

### `Core/Bank/Scene/Preset/presetManager.h`

Change signatures:

```c
uint8_t preset_setSupplementalParameter(uint8_t scene_index, uint8_t slot,
                                        uint8_t descriptor_index,
                                        instrument_param_value_t value);

uint8_t preset_applyInstrumentRuntimeValue(uint8_t scene_index,
                                           instrument_param_id_t id,
                                           instrument_param_value_t value);
```

`preset_setInstrumentParameter()` already accepts `uint8_t value`; optionally
switch that parameter to `instrument_param_value_t` for clarity.

### `Core/Bank/Scene/Preset/presetManager.c`

Update value locals and helper signatures:

- `preset_applyInstrumentRuntimeValueInternal()`
- `preset_applyInstrumentRuntimeValue()`
- `preset_setInstrumentParameter()`
- `preset_setSupplementalParameter()`
- `preset_storeSupplementalCell()`
- `preset_normalizeLfoTargetPair()`
- `preset_normalizeSlotModulationTargets()`
- `preset_applyKitVoiceSupplemental()`
- morph-copy code in `preset_commitStagedKitNormalToMorph()` and
  `preset_commitStagedInstrumentNormalToMorph()` if locals are currently
  inferred as `uint16_t`.

Comment-ready description for runtime apply:

```c
/*
 * Apply one byte-domain descriptor value to runtime.
 *
 * SceneData stores one byte per descriptor cell. Wider canonical IDs are not
 * parameter values; target selector bytes are expanded by InstrumentManager only
 * for modulation installation. Ordinary descriptor values pass through here in
 * the same byte domain used by files, Menu, Morph, and MIDI.
 */
```

### `Core/DSP/Instruments/InstrumentManager.h`

Change value domains that describe stored values:

```c
typedef struct {
    instrument_param_value_t min_value;
    instrument_param_value_t max_value;
    uint8_t flags;
} instrument_mod_domain_t;
```

Change:

```c
uint8_t instrumentManager_writeRuntime(uint8_t slot,
                                       const ParamDescriptor *descriptor,
                                       instrument_param_value_t value);
```

Comment-ready description:

```c
/*
 * Descriptor modulation domains are byte domains.
 *
 * The domain describes legal stored descriptor values before DSP-specific
 * shaping. Current instrument descriptors expose byte values only. Modulation
 * math may promote to wider temporary integers, but the retained and applied
 * descriptor value remains one byte.
 */
```

### `Core/DSP/Instruments/InstrumentManager.c`

Update value-bearing functions:

- `instrumentManager_writeRuntimeInternal()`
- `instrumentManager_writeRuntime()`
- `instrumentManager_writeSpecialRuntime()`
- `instrumentManager_writeParameter()`
- `instrumentManager_noteRuntimeValueChanged()`
- `instrumentManager_descriptorImageBase()`
- `instrumentManager_slotDecimationBase()` if it reads image values.
- LFO adapter `base_value` can become `instrument_param_value_t`.
- Temporary shaped values from `modNode_shapeParameterU16()` may remain
  `uint16_t`, but clamp to byte before calling runtime writers.

Keep canonical target IDs wide in:

- `installed_mod_target_t::target_id`
- `instrument_lfo_target_adapter_t::id`
- target resolver functions.

Comment-ready description for LFO adapter base:

```c
/*
 * Cached LFO target base in descriptor byte space.
 *
 * The adapter stores the same byte value held in morph_interpolation[]. LFO
 * shaping can promote to wider math for amount/polarity, but each block writes a
 * clamped byte-domain descriptor value through the normal runtime writer.
 */
```

### `Core/Bank/Scene/SceneModTargets.h/.c`

Scene target IDs can remain `uint16_t`, but min/max values should be bytes:

```c
instrument_param_value_t min_value;
instrument_param_value_t max_value;
```

Add index helpers if LFO target voice uses a Scene/effects namespace:

```c
uint8_t sceneModTarget_indexFromId(uint16_t id, uint8_t *index_out);
uint16_t sceneModTarget_idFromIndex(uint8_t index);
uint8_t sceneModTarget_count(void);
```

Comment-ready description:

```c
/*
 * Scene target IDs are runtime identities; Scene target values are bytes.
 *
 * The ID can live in the wider canonical modulation namespace, but the target's
 * modulated value range is still byte-sized. LFO storage that points at the
 * Scene namespace stores a Scene target index token and expands it to this ID
 * only for display or runtime installation.
 */
```

## Phase 5: Update Menu Formatting and Clamp Logic

### `Core/Menu/menu.c`

Specific places to audit and update:

- `menu_lfo_target_context_t`
- `menu_cellDisplayValue()`
- `menu_cellCommitValue()`
- `menu_lfoTargetNormalizeParam()` -> token version.
- `menu_lfoTargetCommitVoiceAndReconcile()`
- `menu_lfoTargetEditVoice()`
- `menu_lfoTargetEditParam()`
- `menu_lfoTargetDisplayValue()`
- `menu_velocityTargetNormalize()`
- `menu_velocityTargetEditParam()`
- `menu_velocityTargetDisplayValue()`
- `menu_formatInstrumentTargetShort()`
- `menu_displayInstrumentTargetFull()`
- `menu_clampCellValue()`
- encoder and endless-pot branches that test `INSTRUMENT_PARAM_INVALID`.

Display rule:

- Target selector cells store byte tokens.
- Target display functions may still receive canonical IDs.
- Therefore display paths should expand tokens before calling
  `menu_formatInstrumentTargetShort()` or `menu_displayInstrumentTargetFull()`.

Commit rule:

- `menu_cellCommitValue()` should pass byte-domain values to Preset.
- `INSTRUMENT_PARAM_INVALID` checks in target edit paths should become
  `INSTRUMENT_TARGET_TOKEN_OFF` checks for storage/edit values.
- Only display/runtime temporary IDs should compare against
  `INSTRUMENT_PARAM_INVALID`.

Comment-ready description:

```c
/*
 * Menu target cells edit retained tokens, not canonical IDs.
 *
 * Target selectors are displayed through the canonical target-label helpers, but
 * the committed SceneData value is a byte token. This keeps the UI expressive
 * while preventing display/runtime target IDs from leaking back into resident
 * parameter images.
 */
```

## Phase 6: Descriptor Tables

The descriptor rows do not need structural changes except for comments and type
expectations. These files define the target selector rows:

- `Core/DSP/Instruments/Drum/DrumParameters.c`
- `Core/DSP/Instruments/Snare/SnareParameters.c`
- `Core/DSP/Instruments/Cymbal/CymbalParameters.c`
- `Core/DSP/Instruments/HiHat/HiHatParameters.c`

Rows to keep as byte selector rows:

- `velo_mod_dest`
- `lfo_target_voice`
- `lfo_target_param`
- `lfo_target_voice_2`
- `lfo_target_param_2`

Comment-ready description near `ROW_NOBIND` usage:

```c
/*
 * Target selector rows store byte tokens.
 *
 * lfo_target_voice selects the namespace, lfo_target_param stores a local
 * descriptor token in that namespace, and velo_mod_dest stores a self-local
 * descriptor token. These rows deliberately do not store packed canonical target
 * IDs.
 */
```

## Phase 7: Compatibility and Fixture Migration

After code changes, update fixture files under `SD_CARD/`:

- Replace `65535` target values with `255`.
- Replace old packed target IDs with local descriptor indices.
- For LFO targets, ensure `lfo_target_voice` carries the target namespace.
- For velocity targets, ensure `velo_mod_dest` is either `255` or a local
  descriptor index on the same instrument.
- Replace legacy `lfo_target_voice=0` with the chosen valid default, likely `1`
  or `self` emitted as the current slot number.

Important: parser compatibility should land before fixture migration so old
cards still load.

## Phase 8: Verification

Build and image:

```sh
make
make img
```

Focused runtime checks:

- Load an old kit with `lfo_target_param=65535`; confirm it becomes off.
- Load an old kit with packed LFO target IDs; confirm only the local descriptor
  index is retained and target voice supplies the slot.
- Load an old kit with packed velocity target IDs; confirm same-slot IDs become
  local tokens and cross-slot IDs become off.
- Save Kit and Instrument files; confirm no instrument parameter line writes a
  value above `255`.
- Edit LFO target voice and confirm the target param preserves local index only
  when valid in the new namespace.
- Edit velocity target and confirm it browses only the source instrument's own
  modulatable parameters.
- Confirm Morph Save still writes byte endpoint values.
- Confirm LFO and velocity runtime modulation still install, clear, and restore
  targets correctly.

Memory checks:

```sh
arm-none-eabi-size -A build/lxr02.elf
arm-none-eabi-nm --print-size --size-sort build/lxr02.elf
```

Expected storage reduction:

- `instrument_parameter_images_t` shrinks from `384` bytes per slot to `192`
  bytes per slot.
- One Scene shrinks by `1152` bytes.
- Sixteen resident Scenes would shrink by `18432` bytes.

## Non-goals

- Do not shrink `instrument_param_id_t`; it is a runtime identity.
- Do not shrink PatternData automation target fields in this pass. The dynamic
  Pattern rewrite owns automation target packing.
- Do not remove descriptor-aware runtime target IDs; remove them only from
  resident parameter storage.
- Do not change DSP runtime structs just because they contain wider audio or
  fixed-point fields. This refactor is about stored instrument parameter images.
