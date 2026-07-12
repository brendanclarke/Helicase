# LOAD_MORPH_AUDIT

## Goal

Add new-format Kit Morph and Instrument Morph loading without changing the kit
or instrument file formats.

The browser lists stay exactly the same as the current Kit and Instrument
loaders. The only semantic difference is the commit destination:

- Normal Kit Load reads a kit directory and replaces the selected Scene kits.
- Kit Morph Load reads the same kit directory, then copies the source kit's
  normal endpoint values into the morph endpoints of the currently loaded kits.
- Normal Instrument Load reads an Instrument file and replaces the destination
  slot.
- Instrument Morph Load reads the same Instrument file, then copies the source
  instrument's normal endpoint values into the morph endpoint of the currently
  loaded destination slot.

No save work belongs in this pass. No legacy `.SND` morph path belongs in this
pass. The existing unmodulated parameter math and current normal load behavior
must remain unchanged.

## Current Code Shape

- `Core/Menu/menu.h`
  - `enum loadSaveEnum` currently exposes `SAVE_TYPE_KIT`, `SAVE_TYPE_GLO`,
    and `SAVE_TYPE_SAMPLES`.
  - Adding a visible Kit Morph load entry is therefore a menu enumeration and
    load-page routing change.

- `Core/Menu/menu.c`
  - Top-level Load page calls `menu_requestCurrentLoadSaveSelection()`.
  - Normal Kit directory load is posted through
    `preset_loadKitForScenes(slot, menu_kitLoadSceneMask)`.
  - Nested Instrument Load is controlled by:
    - `menu_instrumentLoadActive`
    - `menu_instrumentLoadSlot`
    - `menu_instrumentLoadScene`
    - `menu_instrumentLoadType`
    - `menu_instrumentLoadIndex[type]`
    - `menu_instrumentLoadStepType()`
    - `menu_instrumentLoadRequestSelection()`
  - Normal Instrument selection is posted through
    `preset_loadInstrument(scene, slot, type, index)`.

- `Core/Hardware/SD/filesystem.c`
  - `filesystem_requestLoadKitForScenes()` parses a new-format kit directory
    into `op_staged_kit`, then commits the full staged kit into selected Scenes
    inside the filesystem state machine.
  - `filesystem_requestLoadInstrument()` parses a new-format Instrument file
    into `op_staged_instrument` and leaves the commit to Preset.
  - This asymmetry is important: Kit Morph Load must reuse the kit parser but
    must not let filesystem replace the current kit.

- `Core/Scene/Preset/presetManager.c`
  - `preset_startInstrumentApply()` commits a staged Instrument and then queues
    the morph worker plus modulation target rebinds.
  - `preset_tickInstrumentApply()` and `preset_tickDrumsetApply()` keep runtime
    application bounded.
  - `preset_applyInstrumentRuntimeValueInternal()` already knows how to write a
    morph endpoint and rebuild that one interpolation cell.
  - `presetMorph_requestVoice()` and `presetMorph_requestAll()` are already the
    correct bounded application path for changed morph endpoints.

- `Core/Hardware/SD/storageTypes.c`
  - The parser already separates `[params]` and `[morph]`.
  - For this feature we want the selected file's `[params]` values as the source
    because morph loading means "load normal endpoint into current morph
    endpoint."
  - No storage schema change is needed.

## User-Facing Behavior

### Kit Morph Load

The Load page type encoder should show:

1. `Load:[Kit     ]`
2. `Load:[KitMrp  ]`
3. `Load:[Settings]`
4. `Load:[Samples ]`

`KitMrp` uses the same kit slot numbers and kit names as `Kit`. Selecting a kit
while `KitMrp` is active loads that kit's normal endpoint values into the morph
endpoints of the currently loaded selected Scene kits.

The selected Scene mask remains the same mask used by normal Kit Load.

### Instrument Morph Load

On entry to Instrument Load, capture the current destination slot type. Only
that type gets a morph option.

Example for a slot whose current type is Drum:

1. `Load:[Drum    ]`
2. `Load:[DrumMrp ]`
3. `Load:[Snare   ]`
4. `Load:[Cymbal  ]`
5. `Load:[Hihat   ]`

`SnareMrp`, `CymbalMrp`, and `HihatMrp` are not shown unless the destination
slot already has that type when Instrument Load is entered.

The file list for `DrumMrp` is exactly the Drum instrument list. Selecting a
file loads that file's normal endpoint values into the destination slot's morph
endpoint. It must not replace the slot type, display name, audio routing, LFO
target selectors, or any other non-morphable runtime binding.

## Type-Mismatch Policy For Kit Morph Load

Kit Morph Load can source a kit whose slot types do not match the currently
loaded kit. Replacing the destination slot type would violate the feature:
morph loading targets the currently loaded kit item.

Planned copy policy:

- If the staged source slot type matches the destination slot type, copy each
  morphable descriptor's source normal value into the destination morph value by
  descriptor index.
- If the types differ, copy only morphable descriptors whose file keys match in
  both source and destination descriptor tables.
- If a destination morphable descriptor has no matching source key, leave its
  current morph endpoint unchanged.
- Never copy non-morphable descriptors during morph load.
- Never copy source display names, type tags, audio routing, or supplemental LFO
  target bindings during morph load.

Why this policy: descriptor indices are only guaranteed to mean the same thing
within the same instrument type. Key matching preserves shared musical controls
across types where possible and avoids unsafe index-based writes when the types
differ.

Kit settings that have explicit morph endpoints should follow the same idea:
copy the source normal kit-setting endpoint into the destination morph kit
setting. The current known example is:

- source `slot6_track7_amp_envelope_decay`
- destination `slot6_track7_morph_amp_envelope_decay`

Normal routing/settings that do not have morph endpoints are not part of Kit
Morph Load.

## Code Plan

### 1. `Core/Menu/menu.h`

Add `SAVE_TYPE_KIT_MORPH` immediately after `SAVE_TYPE_KIT`.

Comment block to add adjacent to the enum change:

```c
/*
 * Kit Morph is a load-page mode, not a separate persisted file type.
 *
 * It intentionally sits immediately after SAVE_TYPE_KIT because the UI should
 * scroll from "Kit" to "KitMrp" before Settings/Samples. Both entries browse
 * the same Kit/ directory cache; the distinction is the Preset commit endpoint:
 * normal Kit replaces the selected Scene kit, KitMrp copies source normal
 * values into the selected Scenes' current morph endpoints.
 */
```

Expected enum order:

```c
enum loadSaveEnum {
    SAVE_TYPE_KIT = 0,
    SAVE_TYPE_KIT_MORPH,
    SAVE_TYPE_GLO,
    SAVE_TYPE_SAMPLES,
    NUM_SAVE_TYPES
};
```

Audit all `what < SAVE_TYPE_GLO` checks after this change. Any check that means
"kit browser backed entry" should continue to include both `SAVE_TYPE_KIT` and
`SAVE_TYPE_KIT_MORPH`. Any check that means "normal kit replacement" must be
changed to `what == SAVE_TYPE_KIT`.

### 2. `Core/Menu/menu.c`: Top-Level Kit Morph UI

Add `KitMrp` to the top-row label switch in `menu_repaintLoadSavePage()`.

Comment block:

```c
/*
 * KitMrp reuses the Kit browser presentation.
 *
 * The list index and displayed kit name come from the same Kit/ scan cache as
 * normal Kit Load. Only selection dispatch differs: Kit replaces the current
 * kit, while KitMrp copies the staged kit's normal endpoints into the current
 * kit's morph endpoints.
 */
```

Update `menu_requestCurrentLoadSaveSelection()`:

- If Load page is highlighting Kit or KitMrp and the caller is only refreshing
  the displayed name, return without posting a load. This preserves the current
  protection that avoids reading stale legacy names from new-format kit
  directories.
- If the user confirms `SAVE_TYPE_KIT`, call
  `preset_loadKitForScenes(slot, menu_kitLoadSceneMask)`.
- If the user confirms `SAVE_TYPE_KIT_MORPH`, call
  `preset_loadKitMorphForScenes(slot, menu_kitLoadSceneMask)`.

Comment block:

```c
/*
 * Kit and KitMrp share the same browser slot but have different commit
 * semantics.
 *
 * Normal Kit Load lets Preset/filesystem replace the selected Scene kits.
 * KitMrp asks Preset to stage the same directory and then copy staged normal
 * endpoint values into the already-loaded kits' morph endpoints. The display
 * name refresh path must not call filesystem_requestLoadName() for either
 * entry because new-format Kit directories do not use the legacy flat name
 * reader.
 */
```

Update Load-page type stepping so `KitMrp` appears after `Kit`. If Save page
still uses this enum before save operations exist, Save page should skip
`SAVE_TYPE_KIT_MORPH` until a later save pass defines it.

Comment block:

```c
/*
 * KitMrp is load-only in this pass.
 *
 * The enum value exists so the Load page can expose the morph-load endpoint.
 * Save operations are intentionally left unchanged until the dedicated kit and
 * instrument save pass defines what writing morph endpoints should mean.
 */
```

### 3. `Core/Menu/menu.c`: Instrument Morph Mode State

Add state next to the existing Instrument Load state:

- `menu_instrumentLoadBaseType`
- `menu_instrumentLoadMorphMode`

`menu_instrumentLoadBaseType` is captured from the destination slot when the
Instrument Load screen is entered. It does not change while the browser is
open. `menu_instrumentLoadMorphMode` means the visible type row is the `Mrp`
variant of `menu_instrumentLoadBaseType`.

Comment block:

```c
/*
 * Instrument Morph is a transient browser mode for the destination slot type.
 *
 * baseType is captured when Instrument Load opens so only the currently loaded
 * instrument type can expose an "...Mrp" row. The mode flag is UI/dispatch
 * state only; it is not a stored parameter value and it must not allow the
 * selected file to replace the destination slot type.
 */
```

On Instrument Load entry:

- Read the current destination slot type as today.
- Store it in both `menu_instrumentLoadType` and `menu_instrumentLoadBaseType`.
- Clear `menu_instrumentLoadMorphMode`.

### 4. `Core/Menu/menu.c`: Instrument Type Scroll

Replace or extend `menu_instrumentLoadStepType()` so it walks a logical list
containing:

- every selectable normal instrument type for the destination slot
- one extra morph row immediately after the captured base type

Forward example when base type is Drum:

- Drum normal
- Drum morph
- Snare normal
- Cymbal normal
- Hihat normal

Reverse stepping should be symmetrical.

Comment block:

```c
/*
 * The instrument type row is now a logical list, not a one-to-one walk over
 * registry types.
 *
 * The extra row is inserted immediately after the destination slot's captured
 * type and represents "load this same type into the morph endpoint." Other
 * types remain normal load targets only, because loading SnareMrp into a Drum
 * slot would either require type replacement or unsafe descriptor-index
 * interpretation.
 */
```

Top-row label helper:

- Normal mode uses `instrumentManager_typeDisplayLabel(type)` padded to 8 chars.
- Morph mode appends `Mrp` to the same base label and pads/truncates to 8 chars.

Comment block:

```c
/*
 * Build the fixed eight-character Load label used by the LCD.
 *
 * Normal rows show the instrument type label as before. The morph row appends
 * "Mrp" to the captured base type, producing labels such as "DrumMrp ". The
 * helper owns padding/truncation so the repaint path does not accidentally
 * resize or corrupt the LCD field.
 */
```

### 5. `Core/Menu/menu.c`: Instrument Selection Dispatch

Update `menu_instrumentLoadRequestSelection()`:

- Normal mode calls `preset_loadInstrument(...)` exactly as today.
- Morph mode calls `preset_loadInstrumentMorph(...)`.

The file count, file name, displayed index, and per-type browser cache remain
keyed by `menu_instrumentLoadType`, which will be the base type while morph
mode is active.

Comment block:

```c
/*
 * InstrumentMrp shares the normal Instrument/ browser cache.
 *
 * The source file is parsed exactly like a normal Instrument load. Dispatch is
 * the only difference: normal rows commit the staged slot as a replacement,
 * while the morph row copies staged normal endpoint values into the existing
 * destination slot's morph image.
 */
```

### 6. `Core/Scene/Preset/presetManager.h`

Add operation enum values:

- `PRESET_OP_KIT_MORPH_LOAD`
- `PRESET_OP_INSTRUMENT_MORPH_LOAD`

Add public APIs:

```c
uint8_t preset_loadKitMorphForScenes(uint8_t presetNr, uint16_t scene_mask);
uint8_t preset_loadInstrumentMorph(uint8_t destination_scene,
                                   uint8_t destination_slot,
                                   instrument_type_t type,
                                   uint8_t browser_index);
void preset_startInstrumentMorphApply(uint8_t scene_index, uint8_t slot);
```

If Kit Morph apply needs a public bounded starter, add:

```c
void preset_startKitMorphApply(uint16_t scene_mask);
uint8_t preset_tickKitMorphApply(void);
```

Prefer reusing `preset_tickDrumsetApply()` only if it can be done without
running normal-kit routing/type replacement paths.

Comment block for the APIs:

```c
/*
 * Morph-load requests stage normal files but commit only morph endpoints.
 *
 * These APIs are separate from the normal load entry points so callers cannot
 * accidentally replace slot type, display name, routing, or supplemental target
 * bindings when the UI is asking for a morph endpoint update. The source file
 * parser is shared; the commit policy is Preset-owned because it must compare
 * staged descriptor identity against the currently loaded Scene kit.
 */
```

### 7. `Core/Scene/Preset/presetManager.c`: Completion Callbacks

Add callbacks:

- `on_kit_morph_load_complete()`
- `on_instrument_morph_load_complete()`

These should set `pm_completed_op` to the new operation values.

Comment block:

```c
/*
 * Morph-load completion names the commit policy, not a different file parser.
 *
 * Filesystem has only staged the selected normal kit/instrument data at this
 * point. Preset handles the endpoint copy after completion so it can preserve
 * the destination Scene identity and queue the correct bounded Morph rebuild.
 */
```

### 8. `Core/Hardware/SD/filesystem.h`

Add a kit staging accessor:

```c
const struct kit *filesystem_loadedKit(void);
```

Use the actual project typedef name for the return type. If `kit_t` is visible
in this header, prefer:

```c
const kit_t *filesystem_loadedKit(void);
```

Add a Kit Morph staging request:

```c
bool filesystem_requestLoadKitMorphForScenes(uint8_t slot,
                                             uint16_t scene_mask,
                                             fs_completion_cb_t cb);
```

The `scene_mask` is still useful validation/context even though filesystem will
not commit the staged kit for morph loads.

Comment block:

```c
/*
 * Stage a Kit directory for a Preset-owned morph commit.
 *
 * This request walks the same Kit/ directory parser as normal Kit Load, but it
 * leaves op_staged_kit untouched for Preset to inspect after completion.
 * Filesystem cannot commit KitMrp itself because it does not know which
 * descriptor keys are safe to copy into the currently loaded destination kit.
 */
```

### 9. `Core/Hardware/SD/filesystem.c`: Kit Morph Staging

Add an internal load mode or distinct internal op:

- Conservative option: add `FS_INTERNAL_OP_LOAD_KIT_MORPH`.
- Alternative: keep `FS_INTERNAL_OP_LOAD_KIT` and add
  `op_kit_load_commit_mode`.

Recommended: use an explicit internal op or mode with a named predicate at the
phase where normal kit load currently commits:

```c
static bool filesystem_kitLoadCommitsToScene(void);
```

At the final phase of `filesystem_loadKitDirectory_tick()`:

- Normal Kit Load keeps the existing `target_scene->kit = op_staged_kit;`
  behavior.
- Kit Morph Load skips that assignment and completes with `op_staged_kit`
  available through `filesystem_loadedKit()`.

Comment block near the final-phase branch:

```c
/*
 * Normal Kit Load is the only directory-kit path that commits from filesystem.
 *
 * KitMrp uses the same staged image but intentionally skips Scene replacement.
 * Preset must compare the staged source descriptors against the currently
 * loaded destination descriptors, then copy only morphable endpoint values.
 */
```

Add `filesystem_loadedKit()`:

```c
/*
 * Borrow the validated Kit directory staging image.
 *
 * Output is read-only filesystem storage consumed by Preset immediately after
 * the matching Kit or KitMrp completion callback. The accessor exists for
 * morph-load commit policy; normal Kit Load keeps its existing atomic commit
 * behavior inside the filesystem state machine.
 */
```

### 10. `Core/Scene/Preset/presetManager.c`: Kit Morph Request

Implement `preset_loadKitMorphForScenes()`:

- Ack filesystem.
- Set `pm_status = PRESET_LOAD_IN_PROGRESS`.
- Set request slot and request type to `SAVE_TYPE_KIT_MORPH`.
- Call `filesystem_requestLoadKitMorphForScenes(presetNr, scene_mask,
  on_kit_morph_load_complete)`.
- Restore `PRESET_IDLE` on request failure.

Comment block:

```c
/*
 * Post a new-format Kit Morph load.
 *
 * The selected Kit directory is parsed exactly like normal Kit Load. The
 * difference is that filesystem only stages the source kit, and Preset copies
 * its normal endpoint values into the current selected Scene kits' morph
 * endpoints after completion.
 */
```

### 11. `Core/Scene/Preset/presetManager.c`: Instrument Morph Request

Implement `preset_loadInstrumentMorph()`:

- Validate destination scene/slot.
- Validate requested type equals the current destination slot type.
- Reuse `filesystem_requestLoadInstrument(destination_scene, destination_slot,
  type, browser_index, on_instrument_morph_load_complete)`.
- Store request context as normal Instrument Load does.

Comment block:

```c
/*
 * Post an Instrument Morph load for the destination slot's current type.
 *
 * The type equality check is intentional. InstrumentMrp is an endpoint update
 * for the currently loaded slot, not a type-changing operation. The parser may
 * still stage a full Instrument file, but commit will copy only morphable
 * normal endpoint values into the destination slot's morph image.
 */
```

### 12. `Core/Scene/Preset/presetManager.c`: Endpoint Copy Helpers

Add static helpers near the existing instrument commit/apply code:

```c
static void preset_copyInstrumentNormalToMorphByIndex(
    kit_instrument_slot_t *destination,
    const kit_instrument_slot_t *source);

static void preset_copyInstrumentNormalToMorphByKey(
    kit_instrument_slot_t *destination,
    const kit_instrument_slot_t *source);
```

Use `instrumentManager_getDescriptor(type, index)` or the existing descriptor
iteration API. If no descriptor lookup by key exists, add one to
`InstrumentManager` rather than comparing display labels. File keys are the
storage identity.

Copy only descriptors where:

- destination descriptor exists
- destination descriptor is morphable
- source descriptor exists
- source descriptor key matches the destination descriptor key

The copied value is:

```c
destination->parameter_images.morph_instrument_parameters[dst_index] =
    source->parameter_images.instrument_parameters[src_index];
```

Then queue:

```c
presetMorph_requestVoice(scene_index, slot);
```

Comment block:

```c
/*
 * Copy a staged source normal endpoint into an existing destination morph
 * endpoint.
 *
 * Morph load never copies a staged morph endpoint because selecting KitMrp or
 * InstrumentMrp means "make this file's normal sound the morph destination."
 * The helper also refuses non-morphable descriptors so routing, target
 * selectors, and other single-endpoint bindings stay owned by the currently
 * loaded instrument.
 */
```

For the key-mapping helper:

```c
/*
 * Key-based copy is used when source and destination instrument types differ.
 *
 * Descriptor indices are type-local, so index copying across types can write a
 * pitch value into an envelope field or similar. File keys are the persisted
 * parameter identity; matching them preserves shared controls and leaves
 * destination-only controls unchanged.
 */
```

### 13. `Core/Scene/Preset/presetManager.c`: Kit Morph Commit

Implement a static commit helper called after `PRESET_OP_KIT_MORPH_LOAD`
completion:

```c
static void preset_commitStagedKitNormalToMorph(uint16_t scene_mask);
```

Algorithm:

1. Borrow `const kit_t *source = filesystem_loadedKit();`.
2. For each selected scene in the request mask:
   - get the resident destination scene
   - copy supported normal kit settings into their morph settings
   - for each instrument slot:
     - if source type equals destination type, copy by descriptor index
     - otherwise copy by descriptor key
     - request morph rebuild for that voice
3. If any selected scene is the active scene, run the bounded morph worker until
   clean using the same async/tick pattern as normal load.

Comment block:

```c
/*
 * Commit staged Kit normal endpoints into resident Kit morph endpoints.
 *
 * This deliberately preserves the destination kit's slot types, display names,
 * audio routing, and supplemental modulation bindings. Only morphable endpoint
 * values are copied. Matching source/destination types can use descriptor
 * indices; mismatched types fall back to file-key matching so the operation
 * remains musically useful without corrupting type-local parameter layouts.
 */
```

### 14. `Core/Scene/Preset/presetManager.c`: Instrument Morph Commit

Implement a commit helper:

```c
static void preset_commitStagedInstrumentNormalToMorph(uint8_t scene_index,
                                                       uint8_t slot);
```

Algorithm:

1. Borrow `filesystem_loadedInstrumentSlot()`.
2. Validate scene/slot.
3. Validate staged type equals destination type.
4. Copy staged normal morphable descriptor values into destination
   `morph_instrument_parameters`.
5. Do not copy display name.
6. Do not clear modulation targets.
7. Do not reset runtime slot.
8. Queue `presetMorph_requestVoice(scene_index, slot)`.
9. If the scene is active, start a small bounded morph-apply transaction.

Comment block:

```c
/*
 * Commit a staged Instrument file as a morph endpoint update.
 *
 * Unlike preset_startInstrumentApply(), this path does not replace the slot or
 * clear/rebind modulation. The destination instrument identity remains loaded;
 * only its morphable endpoint bytes change, and the Morph worker reapplies the
 * current morph amount to produce the new runtime interpolation.
 */
```

### 15. `Core/Scene/Preset/presetManager.c`: Bounded Morph Apply

The active scene needs runtime application after morph endpoints change.

Preferred implementation:

- Add a small morph-apply transaction that queues the affected voices through
  `presetMorph_requestVoice()` or `presetMorph_requestAll()`.
- Its tick function calls `presetMorph_tick()` until clean.
- It does not call routing apply.
- It does not reset runtime slots.
- It does not clear/rebind modulation targets.

Comment block:

```c
/*
 * Morph-only apply drains the existing Morph worker without touching slot
 * identity.
 *
 * Loading morph endpoints changes interpolation targets, not instrument type,
 * routing, or target bindings. Reusing a bounded worker keeps the foreground
 * timing behavior of normal loads while avoiding the destructive parts of the
 * normal Instrument/Kit replacement transactions.
 */
```

### 16. `Core/Menu/menu.c`: Completion Handling

Extend `menu_pollPresetStatus()`:

- `PRESET_OP_KIT_MORPH_LOAD`
  - commit helper should already have copied endpoints or be called before menu
    observes completion, depending on the chosen Preset flow.
  - start/poll morph-only apply if active scene is affected.
  - repaint Load page.
- `PRESET_OP_INSTRUMENT_MORPH_LOAD`
  - call `preset_startInstrumentMorphApply(scene, slot)` or ensure Preset has
    already started it.
  - keep Instrument Load active.
  - repaint and unlock storage when apply drains.

Comment block:

```c
/*
 * Morph-load completion keeps the browser context alive.
 *
 * The selected file has updated the current morph endpoint, but it has not
 * changed the destination type or display name. Keeping the load screen open
 * matches normal Instrument Load browsing while allowing repeated morph-target
 * auditions from the same file list.
 */
```

## Files Expected To Change

- `Core/Menu/menu.h`
- `Core/Menu/menu.c`
- `Core/Scene/Preset/presetManager.h`
- `Core/Scene/Preset/presetManager.c`
- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/filesystem.c`

Possible small support change if no file-key descriptor lookup exists:

- `Core/DSP/Instruments/InstrumentManager.h`
- `Core/DSP/Instruments/InstrumentManager.c`

No parser changes are expected in:

- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`

## Verification Plan

1. Build:
   - `make -j4`
   - `git diff --check`

2. Top-level Load UI:
   - Confirm type scroll order: Kit, KitMrp, Settings, Samples.
   - Confirm Kit and KitMrp display the same kit numbers/names.
   - Confirm Save page does not expose KitMrp until save semantics are added.

3. Kit Morph behavior:
   - Load a normal kit into a scene.
   - Change morph amount to 0 and record current sound.
   - Use KitMrp to load a different kit.
   - Confirm morph amount 0 still plays the original current kit endpoint.
   - Confirm morph amount 255 reaches the loaded source kit's normal endpoint
     for matching parameters.
   - Confirm slot types, display names, routing, and target selectors did not
     change.

4. Kit Morph mismatched type behavior:
   - Use a source kit with at least one different slot type.
   - Confirm common keyed morphable parameters copy.
   - Confirm destination-only parameters remain unchanged.
   - Confirm no descriptor-index corruption occurs.

5. Instrument Load UI:
   - On a Drum slot, confirm the order begins `Drum`, `DrumMrp`, then other
     selectable types without their `Mrp` variants.
   - On a Snare slot, confirm the morph row follows `Snare` instead.
   - Confirm reverse encoder movement walks the same logical list backward.

6. Instrument Morph behavior:
   - Select `DrumMrp` and load a Drum file.
   - Confirm the destination slot type and display name do not change.
   - Confirm audio routing and LFO target selectors do not change.
   - Confirm morph amount 0 remains the current normal endpoint.
   - Confirm morph amount 255 reaches the selected file's normal endpoint for
     morphable parameters.

7. Regression:
   - Normal Kit Load still replaces selected Scene kits.
   - Normal Instrument Load still replaces the destination slot and rebinds
     modulation targets.
   - Legacy `.SND` morph load path remains untouched.

## Open Decision To Confirm Before Coding

For Kit Morph Load with mismatched source/destination instrument types, this
plan uses file-key matching for shared morphable parameters and leaves
unmatched destination morph endpoints unchanged. That is safer than replacing
types and more useful than skipping the whole slot, but it is still a design
choice worth confirming before implementation.
