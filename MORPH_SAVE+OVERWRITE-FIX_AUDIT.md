# Morph Save + Overwrite Indicator Fix Audit

Date: 2026-07-13

Scope:

- Fix Save-page overwrite confirmation text for Kit Save and Scene Save.
- Add new-format Morph Save as `Save:[KitMrp  ]`, between `Save:[Kit     ]`
  and `Save:[Scene   ]`.
- Morph Save writes the normal root `Kit/` directory format, not the legacy
  flat `.SND` morph file.

This is a plan/audit only. No firmware behavior is changed by this file.

## Summary

The overwrite indicator should be occupancy based: if confirming the selected
menu operation will write over any existing library data, the confirmation text
must be `OW`. `OK` is only for a target placement that writes into empty
library slot(s). The current name-comparison exemption is the wrong model for
the requested UI behavior because an occupied slot still contains data that the
operation will replace.

Morph Save should be a new directory-Kit save mode. It should use the same
root `Kit/NNN Name/` save machinery as normal Kit Save, but it should swap the
meaning of saved endpoint images:

- file `[params]` for morphable descriptors gets the current interpolation
  value at the retained per-voice Morph amount;
- file `[morph]` for morphable descriptors gets the normal main endpoint;
- non-morphable and supplemental `[params]` values remain the normal main
  values;
- non-morphable values stay absent from `[morph]`;
- the generated kit-owned slot-6/track-7 decay endpoint follows the same
  policy because it has main and Morph endpoints even though it is not a
  descriptor row.

This creates the requested "flip" behavior at Morph 255: morphable normal
file endpoints become the old Morph endpoint, while file Morph endpoints become
the old normal endpoint. Non-morphable parameters are saved as normal because
unmorphable means unmorphable.

## Current Code Findings

### Menu Save Type Ordering

`Core/Menu/menu.h` already contains:

- `SAVE_TYPE_KIT`
- `SAVE_TYPE_KIT_MORPH`
- `SAVE_TYPE_SCENE`
- `SAVE_TYPE_GLO`
- `SAVE_TYPE_SAMPLES`

`NUM_PRESET_LOCATIONS` is already `3`, so Kit, KitMrp, and Scene each have a
numbered browser cursor in `menu_currentPresetNr[]`.

`Core/Menu/menu.c` already displays `KitMrp` in `menu_repaintLoadSavePage()`,
but save-side encoder handling deliberately skips `SAVE_TYPE_KIT_MORPH` on
`SAVE_PAGE`. The current comment says KitMrp is load-only.

### Overwrite Text Rendering

`menu_repaintLoadSavePage()` paints save confirmation text at columns 14-15:

- it writes `OW` when `menu_currentSaveWouldOverwrite()` returns nonzero;
- otherwise it writes `menuText_ok`.

`menu_currentSaveWouldOverwrite()` currently compares the occupied slot's
cached display name against `preset_currentName`. That matches the older Scene
Save audit's name-reuse policy, but it does not match the requested behavior
here. The requested behavior is simpler and stricter: any occupied destination
slot means `OW`.

The observed "OW briefly, then OK" symptom is consistent with a stale or
post-refresh name comparison: scrolling onto an occupied slot first compares
the newly selected occupied slot against the previous edited name, then a later
name/display refresh makes the names match and the repaint goes back to `OK`.
The fix is to make the rendered indicator derive from the actual write plan
and slot occupancy, not from whether the displayed names match.

### Save Requests

`menu_handleLoadSaveMenu()` currently starts save operations from the OK branch:

- `SAVE_TYPE_KIT` calls `preset_saveDrumset(slot, 0)`;
- `SAVE_TYPE_SCENE` calls `preset_saveScene(...)`;
- `SAVE_TYPE_GLO` calls `preset_saveGlobals()`;
- `SAVE_TYPE_KIT_MORPH` has no save branch.

`preset_saveDrumset(presetNr, isMorph)` currently means:

- `isMorph == 0`: new-format directory Kit Save through
  `filesystem_requestSaveKitDirectory()`;
- `isMorph == 1`: legacy `FS_FILE_MORPH` flat `.SND` save through
  `filesystem_requestSave()`.

The new Morph Save must not reuse `preset_saveDrumset(..., 1)`, because that
API name still maps to the old compatibility payload.

### Kit Directory Writer

`filesystem_saveKitDirectory_tick()` writes:

- root `Kit/`;
- target `Kit/<NNN Name>/`;
- six LFN-visible instrument files;
- `kitset.kcg` with returned short aliases;
- Kit scan cache updates from the actually created display/open names.

Instrument text lines are produced by `storage_formatInstrumentLine()`.
Kitset text lines are produced by `storage_formatKitsetLine()`.

This is the correct machinery for Morph Save. Duplicating the directory state
machine would add risk without adding behavior.

### Storage Writer Endpoint Selection

`storage_formatInstrumentLine()` currently owns the descriptor walk and calls
`storage_descriptorValueForSection()`:

- `[params]` reads `parameter_images.instrument_parameters[]`;
- `[morph]` reads `parameter_images.morph_instrument_parameters[]`.

This is the right narrow place to add a save-image policy. The writer already
knows whether it is emitting `[params]` or `[morph]`, whether a descriptor is
morphable, and which descriptor index is being serialized.

### Morph Interpolation Source

`presetMorphEngine.c` owns descriptor Morph interpolation. Its private
`presetMorph_interpolate(a, b, amount)` uses the current descriptor Morph
contract:

- amount `0` returns main exactly;
- amount `255` returns Morph exactly;
- intermediate values use 255-domain rounded interpolation.

`morph_interpolation[]` is explicitly runtime-derived and not serialized in
normal saves. For Morph Save, the file wants a snapshot of the current retained
Morph position. The safest implementation is to compute the snapshot value from
retained main endpoint, retained Morph endpoint, and
`scene.settings.voice_morph_amount[slot]` while formatting the file. That
avoids serializing stale `morph_interpolation[]` if the rate-limited worker has
not drained after a recent Morph edit.

The hidden LFO Morph layer should not be serialized by Morph Save. The user
request names the current Morph and individual voice Morph positions, which are
retained Scene settings. LFO Morph is a transient modulation overlay and should
remain runtime-only.

## Implementation Plan

### 1. Replace Name-Based Overwrite Detection With Placement Occupancy

Files:

- `Core/Menu/menu.c`
- possibly `Core/Menu/menu.h` only if a helper must become visible, but the
  preferred implementation keeps it `static` in `menu.c`.

Change:

- Replace `menu_currentSaveWouldOverwrite()`'s fixed single-slot name compare
  with a placement scan.
- For Save Kit and Save KitMrp, use `filesystem_kitSlotExists(target_slot)`.
- For Save Scene, use `filesystem_sceneSlotExists(target_slot)`.
- Walk the selected Scene mask in the same order as future multi-Scene save
  placement: the lowest selected resident Scene maps to the selected library
  slot, each additional selected resident Scene maps to the next library slot.
- With current `SCENE_COUNT == 1`, this loop still resolves to one slot.
- If the mask is empty, fall back to the active Scene exactly as
  `menu_firstSelectedSceneFromMask()` does.
- If any target slot is occupied, return `1`.
- If all target slots are empty, return `0`.
- Treat out-of-range subsequent slots conservatively as overwrite/error
  presentation only if the save UI can actually reach them; for current
  single-Scene behavior this does not occur.

Comment block to add/update above the helper:

```c
/*
 * Compute the Save-page overwrite indicator from the actual placement plan.
 *
 * Inputs: current Save type, selected root library slot, and the selected
 * resident Scene mask. Output: nonzero when confirming the operation will
 * write into any occupied root library slot. This intentionally ignores
 * display-name equality: an occupied slot already contains user data, so the
 * confirmation text must remain `OW` for every replacement.
 *
 * Kit and KitMrp both query the Kit/ scan cache because both write root Kit
 * directories. Scene queries the Scene/ scan cache. The loop mirrors the
 * future multi-Scene placement rule even while SCENE_COUNT is one, keeping the
 * UI warning aligned with the later Bank save expansion.
 */
```

Important loop comment inside the placement loop:

```c
/*
 * Map selected resident Scenes to consecutive library slots.
 *
 * The source Scene identity does not affect occupancy; it only advances the
 * target slot. Keeping this as a loop now prevents the overwrite indicator
 * from becoming single-Scene-special once SCENE_COUNT grows.
 */
```

### 2. Make The Repaint Path The Only Owner Of `OK`/`OW`

Files:

- `Core/Menu/menu.c`

Change:

- Keep all save confirmation text painting inside
  `menu_repaintLoadSavePage()`.
- Do not write `OK` or `OW` opportunistically from encoder handling, request
  callbacks, or name-load completion.
- After every encoder increment/decrement that can change Save type, target
  slot, selected Scene mask, or save name, let the normal repaint call clear
  the field and then paint `OK` or `OW` from
  `menu_currentSaveWouldOverwrite()`.
- This satisfies the requested order: each redraw starts from the cleared
  buffer/`OK` baseline, then the overwrite check paints `OW` when needed. The
  visible result persists because the next repaint recomputes the same
  occupancy result.

Comment block to add in `menu_repaintLoadSavePage()` near the save indicator:

```c
/*
 * Paint the confirmation text last from a single overwrite helper.
 *
 * The LCD buffer was cleared at function entry, and `OK` is the safe default
 * only until the placement scan proves that some existing library data will be
 * replaced. Keeping this decision in repaint prevents a later name refresh or
 * encoder handler from briefly writing `OW` and then clobbering it with a
 * generic `OK`.
 */
```

### 3. Expose `Save:[KitMrp  ]` Instead Of Skipping It

Files:

- `Core/Menu/menu.c`

Change:

- Remove the Save-page skip that maps `SAVE_TYPE_KIT_MORPH` backward to Kit
  when scrolling left and forward to Scene when scrolling right.
- Update the comment that currently says "KitMrp is load-only".
- `Save:[KitMrp  ]` remains between Kit and Scene because the enum is already
  ordered that way.
- Keep the top-row display string as `KitMrp  `.
- Bottom-row numbered browser should use the Kit cache for `SAVE_TYPE_KIT` and
  `SAVE_TYPE_KIT_MORPH`, because both write root `Kit/`.

Comment block for the type-step branch:

```c
/*
 * Save exposes KitMrp as a real directory-Kit save mode.
 *
 * KitMrp stays adjacent to Kit because both operate on root Kit/ slots and
 * share the same numbered browser. The difference is only the save-image
 * policy selected when the user confirms: normal Kit writes main->params and
 * morph->morph, while KitMrp writes current interpolation->params and
 * main->morph.
 */
```

### 4. Add A Preset-Level Morph Snapshot Save API

Files:

- `Core/Scene/Preset/presetManager.h`
- `Core/Scene/Preset/presetManager.c`

Change:

- Add a new public function, preferred name:
  `void preset_saveKitMorphSnapshot(uint16_t presetNr);`
- Add a new completion enum value, preferred name:
  `PRESET_OP_KIT_MORPH_SAVE`.
- Add a new completion callback in `presetManager.c` that completes with
  `PRESET_OP_KIT_MORPH_SAVE`.
- The new function should:
  - call `filesystem_ack()`;
  - set `pm_status = PRESET_LOAD_IN_PROGRESS`;
  - clear `pm_completed_op`;
  - set `pm_request_slot = presetNr`;
  - set `pm_request_type = SAVE_TYPE_KIT_MORPH`;
  - call the new filesystem save request described below.
- Do not overload `preset_saveDrumset(..., 1)`. That boolean still means the
  legacy flat morph compatibility path.

Comment block in `presetManager.h`:

```c
/*
 * Save a Morph snapshot as a new-format root Kit directory.
 *
 * Inputs: zero-based root Kit slot selected by Save:[KitMrp]. Output: an
 * asynchronous Kit/ directory save whose storage policy flips morphable
 * endpoints for file output. This is separate from preset_saveDrumset(..., 1)
 * because that legacy boolean still targets the old flat FS_FILE_MORPH .SND
 * compatibility writer.
 */
```

Comment block in `presetManager.c`:

```c
/*
 * Post the directory-backed KitMrp save request.
 *
 * Preset owns the UI/request bookkeeping while filesystem owns SD state. The
 * request type is SAVE_TYPE_KIT_MORPH so completion/current-selection checks
 * continue to distinguish a morph snapshot save from normal Kit Save even
 * though both write the root Kit/ directory format.
 */
```

### 5. Route The Save OK Branch To The New Preset API

Files:

- `Core/Menu/menu.c`

Change:

- Add a `SAVE_TYPE_KIT_MORPH` case in the Save OK branch:
  `preset_saveKitMorphSnapshot(menu_currentPresetNr[SAVE_TYPE_KIT_MORPH]);`
- Include `PRESET_OP_KIT_MORPH_SAVE` in the save-completion cases that reset
  save UI.
- Update `menu_isLoadSaveSelectionCurrent()` only if needed. It already uses
  `preset_getRequestType()` and `preset_getRequestSlot()` for numbered types,
  so it should work once Preset records `SAVE_TYPE_KIT_MORPH`.

Comment block for the OK branch:

```c
/*
 * KitMrp Save is a Kit-directory operation with a different image policy.
 *
 * It intentionally does not call preset_saveDrumset(..., 1), because that path
 * is the retained legacy .SND morph writer. The user-facing Save:[KitMrp]
 * command must produce a loadable Kit/NNN Name directory.
 */
```

### 6. Add A Filesystem Save Policy For Directory Kit Saves

Files:

- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/filesystem.c`

Change:

- Add a small save policy enum local to filesystem or shared with storageTypes,
  depending on where the compiler needs it:
  - `FS_KIT_SAVE_POLICY_NORMAL`
  - `FS_KIT_SAVE_POLICY_MORPH_SNAPSHOT`
- Add an operation field such as `op_kit_save_policy`.
- Initialize it to normal in `filesystem_start()`.
- Add public request:
  `bool filesystem_requestSaveKitMorphSnapshot(uint16_t slot,
                                               fs_completion_cb_t cb);`
- The new request validates the same slot range as
  `filesystem_requestSaveKitDirectory()`, starts the same
  `FS_INTERNAL_OP_SAVE_KIT` state machine, then sets
  `op_kit_save_policy = FS_KIT_SAVE_POLICY_MORPH_SNAPSHOT`.
- Do not add a second `FS_INTERNAL_OP_SAVE_KIT_MORPH` unless the shared state
  machine becomes unreadable. A policy field is enough because the path,
  phases, cache update, and LFN behavior are identical.

Comment block near the policy field:

```c
/*
 * Directory Kit save image policy.
 *
 * Normal Kit Save writes retained main endpoints to [params] and retained
 * Morph endpoints to [morph]. KitMrp Save writes the current retained Morph
 * interpolation snapshot to [params] and the normal main endpoints to [morph].
 * The policy lives in filesystem operation state because the async state
 * machine is shared while storageTypes owns the line-level schema.
 */
```

Comment block for the new request:

```c
/*
 * Post a Morph snapshot save through the normal Kit directory writer.
 *
 * Inputs: zero-based Kit slot and completion callback. Output: the same
 * Kit/NNN Name folder shape as normal Kit Save, with a save-image policy that
 * flips morphable endpoint roles in the emitted instrument files. Reusing
 * FS_INTERNAL_OP_SAVE_KIT keeps LFN creation, kitset alias handling, and cache
 * update behavior identical to normal Kit Save.
 */
```

### 7. Pass Save Policy Into Storage Writers

Files:

- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`
- `Core/Hardware/SD/filesystem.c`

Preferred shared enum in `storageTypes.h`:

```c
typedef enum {
    STORAGE_KIT_SAVE_NORMAL = 0,
    STORAGE_KIT_SAVE_MORPH_SNAPSHOT
} storage_kit_save_policy_t;
```

Change signatures:

- `storage_formatKitsetLine(..., storage_kit_save_policy_t policy,
  uint16_t line_index)`
- `storage_formatInstrumentLine(..., uint8_t one_based_voice,
  storage_kit_save_policy_t policy, uint8_t morph_amount,
  uint16_t line_index)`

Alternative:

- Put only the policy in storageTypes and pass `morph_amount` separately from
  filesystem's `scene->settings.voice_morph_amount[slot]`.

Avoid:

- Creating one-line wrappers such as `storage_formatMorphInstrumentLine()`
  that only forward to the real writer with a constant. The codebase already
  has a central writer; a policy parameter makes the changed behavior explicit
  at the actual section/value selection point.

Comment block for the enum:

```c
/*
 * Kit directory save image policy.
 *
 * Normal saves serialize SceneData exactly as retained: main endpoints in
 * [params] and Morph endpoints in [morph]. Morph snapshot saves keep the same
 * file grammar but change value selection for morphable endpoint rows:
 * retained current interpolation is emitted in [params], and retained main is
 * emitted in [morph]. Non-morphable rows remain normal [params] rows only.
 */
```

### 8. Share The Descriptor Morph Interpolation Formula

Files:

- `Core/Scene/Preset/presetMorphEngine.h`
- `Core/Scene/Preset/presetMorphEngine.c`
- `Core/Hardware/SD/storageTypes.c`

Change:

- Rename/export the private `presetMorph_interpolate()` as a public helper,
  preferred name:
  `uint16_t presetMorph_interpolateValue(uint16_t main_value,
                                         uint16_t morph_value,
                                         uint8_t amount);`
- Update `presetMorph_tick()` to call the exported helper.
- Use the same helper in the storage writer when policy is
  `STORAGE_KIT_SAVE_MORPH_SNAPSHOT`.

Why not duplicate the formula:

- Descriptor Morph uses a 255-domain formula with exact endpoint guards.
- Duplicating that math in storageTypes risks a future mismatch where runtime
  Morph sounds like one value but Morph Save writes a nearby value.

Comment block for the exported helper:

```c
/*
 * Interpolate one retained descriptor endpoint pair in the Morph value domain.
 *
 * Inputs: main endpoint, Morph endpoint, and a 0..255 Morph amount. Output:
 * the rounded retained value that the Morph worker writes to
 * morph_interpolation[] and runtime. Amount 0 and 255 return exact endpoints.
 * Storage uses this same helper for KitMrp Save so the saved snapshot matches
 * the Morph engine's audible descriptor math without copying the formula.
 */
```

### 9. Implement Morph Snapshot Value Selection For Instrument Files

Files:

- `Core/Hardware/SD/storageTypes.c`

Change:

- Extend `storage_descriptorValueForSection()` or replace it with a policy
  aware helper.
- Logic:
  - if policy is normal:
    - `[params]` -> `instrument_parameters[index]`;
    - `[morph]` -> `morph_instrument_parameters[index]`.
  - if policy is Morph snapshot and descriptor is morphable:
    - `[params]` -> interpolate
      `instrument_parameters[index]` toward
      `morph_instrument_parameters[index]` by this slot's
      `voice_morph_amount`;
    - `[morph]` -> `instrument_parameters[index]`.
  - if policy is Morph snapshot and descriptor is not morphable:
    - `[params]` -> `instrument_parameters[index]`;
    - `[morph]` remains not writable because existing
      `storage_descriptorWritableInSection()` filters it out.
- LFO target voice `self` handling remains `[params]` only and should continue
  to inspect the value actually being emitted in `[params]`. In Morph snapshot
  mode, LFO voice selectors are non-morphable supplemental rows, so this still
  resolves from normal main storage.

Comment block above the policy-aware value helper:

```c
/*
 * Select the value emitted for one descriptor row under the active save policy.
 *
 * Normal Kit Save serializes the two retained endpoints directly. KitMrp Save
 * serializes a snapshot: morphable [params] rows receive the current retained
 * interpolation at this voice's Morph amount, while morphable [morph] rows
 * receive the normal main endpoint so loading the saved Kit flips the range.
 * Non-morphable rows ignore the policy and write only their normal [params]
 * value because they do not participate in Morph.
 */
```

Important math comment inside the interpolation branch:

```c
/*
 * Use the Morph engine's 0..255 interpolation helper rather than
 * morph_interpolation[] so a save immediately after a Morph edit cannot
 * serialize a stale rate-limited runtime cache.
 */
```

### 10. Apply The Same Policy To Kit-Owned Generated Track-7 Decay

Files:

- `Core/Hardware/SD/storageTypes.c`

Change:

- `storage_formatKitsetLine()` currently writes:
  - `slot6_track7_amp_envelope_decay`
  - `slot6_track7_morph_amp_envelope_decay`
- Under normal policy, keep current behavior.
- Under Morph snapshot policy:
  - `slot6_track7_amp_envelope_decay` should be the interpolation between the
    normal and Morph generated decay endpoints using voice slot 6's retained
    Morph amount, because the generated alternate decay belongs to slot 6.
  - `slot6_track7_morph_amp_envelope_decay` should be the normal generated
    decay endpoint.
- This preserves the flip behavior for the generated non-Choke track-7
  endpoint.

Comment block near the two kitset generated-decay lines:

```c
/*
 * Apply the save-image policy to the generated slot-6/track-7 decay pair.
 *
 * This value is Kit-owned rather than descriptor-owned, but it has the same
 * main/Morph endpoint contract as a morphable descriptor. KitMrp Save therefore
 * writes the current slot-6 interpolation as the normal kitset endpoint and
 * writes the old normal endpoint as the kitset Morph endpoint. Non-Choke
 * track-7 behavior then flips consistently with descriptor-backed voices.
 */
```

### 11. Thread Morph Amounts From Filesystem Into Storage

Files:

- `Core/Hardware/SD/filesystem.c`

Change:

- Extend `filesystem_kitset_write_ctx_t` with:
  - `storage_kit_save_policy_t policy`;
  - `uint8_t slot6_morph_amount`;
- Extend `filesystem_instrument_write_ctx_t` with:
  - `storage_kit_save_policy_t policy`;
  - `uint8_t morph_amount`;
- When normal Kit Save or embedded Scene Kit Save calls these writers, pass
  `STORAGE_KIT_SAVE_NORMAL` and any amount value, preferably `0`.
- When root KitMrp Save calls these writers, pass:
  - policy `STORAGE_KIT_SAVE_MORPH_SNAPSHOT`;
  - per-instrument `scene->settings.voice_morph_amount[op_instrument_slot]`;
  - kitset slot-6 amount `scene->settings.voice_morph_amount[5]` when slot 6
    exists.
- Scene Save should remain normal unless a future explicit Scene Morph Save is
  requested. Scene Save is intended to preserve the Scene's actual endpoint
  range, not snapshot/flip it.

Comment block for the write context:

```c
/*
 * Instrument text writer context for asynchronous Kit saves.
 *
 * The filesystem state machine owns which slot is being streamed and which
 * save policy applies. storageTypes owns the schema and value selection. The
 * per-slot Morph amount is captured from Scene settings for KitMrp Save so the
 * writer can compute snapshot values without reading Preset's rate-limited
 * morph_interpolation[] cache.
 */
```

### 12. Keep Legacy Morph Save Intact But Hidden From This Feature

Files:

- `Core/Scene/Preset/presetManager.c`
- `Core/Hardware/SD/filesystem.c`
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` later, when
  implementation lands.

Change:

- Do not remove `FS_INTERNAL_OP_SAVE_MORPH` or `filesystem_saveKit_tick()` in
  this pass.
- Do not expose the legacy flat morph writer from `Save:[KitMrp]`.
- Consider renaming comments around `PRESET_OP_MORPH_SAVE` and
  `FS_INTERNAL_OP_SAVE_MORPH` to say "legacy flat morph" to avoid future
  confusion.

Comment block near legacy request use:

```c
/*
 * Legacy flat Morph save compatibility path.
 *
 * This operation still writes the old Pxxx.SND-style morph payload and is not
 * the Save:[KitMrp] command. New user-facing KitMrp Save uses the directory
 * Kit writer so descriptor-backed instruments, LFN names, kitset.kcg, and
 * [params]/[morph] sections remain loadable by the current Kit loader.
 */
```

## File-by-File Change List

### `Core/Menu/menu.c`

- Change `menu_currentSaveWouldOverwrite()` to occupancy/placement semantics.
- Include `SAVE_TYPE_KIT_MORPH` in Kit cache overwrite checks.
- Stop skipping `SAVE_TYPE_KIT_MORPH` on the Save page.
- Add Save OK branch for `preset_saveKitMorphSnapshot()`.
- Add `PRESET_OP_KIT_MORPH_SAVE` to save completion reset handling.
- Keep confirmation text painting centralized in `menu_repaintLoadSavePage()`.

### `Core/Menu/menu.h`

- Probably no enum change is needed because `SAVE_TYPE_KIT_MORPH` already
  exists in the right position.
- Update comments that still describe KitMrp as load-only.

### `Core/Scene/Preset/presetManager.h`

- Add `PRESET_OP_KIT_MORPH_SAVE`.
- Add `preset_saveKitMorphSnapshot(uint16_t presetNr)`.
- Update comments distinguishing directory KitMrp Save from legacy flat morph
  save.

### `Core/Scene/Preset/presetManager.c`

- Add KitMrp save completion callback.
- Implement `preset_saveKitMorphSnapshot()`.
- Keep `preset_saveDrumset(..., 1)` mapped to legacy flat morph only.

### `Core/Hardware/SD/filesystem.h`

- Add `filesystem_requestSaveKitMorphSnapshot()`.

### `Core/Hardware/SD/filesystem.c`

- Add `op_kit_save_policy`.
- Initialize policy to normal in `filesystem_start()`.
- Add `filesystem_requestSaveKitMorphSnapshot()`.
- Reuse `filesystem_saveKitDirectory_tick()` for both normal and morph
  snapshot saves.
- Pass policy and per-slot Morph amount through kitset/instrument write
  contexts.
- Do not alter Scene Save's embedded Kit writer except to pass normal policy.

### `Core/Hardware/SD/storageTypes.h`

- Add `storage_kit_save_policy_t`.
- Update prototypes for `storage_formatKitsetLine()` and
  `storage_formatInstrumentLine()`.

### `Core/Hardware/SD/storageTypes.c`

- Make kitset generated-decay lines policy-aware.
- Make descriptor value selection policy-aware.
- Use the Morph engine's shared interpolation helper.
- Preserve `self` emission behavior for LFO target voice selectors.

### `Core/Scene/Preset/presetMorphEngine.h`

- Export the interpolation helper.

### `Core/Scene/Preset/presetMorphEngine.c`

- Rename private `presetMorph_interpolate()` to
  `presetMorph_interpolateValue()` and keep `presetMorph_tick()` using it.

## Acceptance Tests

### Overwrite Indicator

1. Enter `Save:[Kit     ]`.
2. Scroll to an empty Kit slot.
3. Confirm bottom-right text is `OK`.
4. Scroll to an occupied Kit slot.
5. Confirm bottom-right text remains `OW` after any delayed name/display
   refresh.
6. Repeat for `Save:[KitMrp  ]`.
7. Repeat for `Save:[Scene   ]`.
8. Confirm the `>` cursor selects the persistent `OW` text on occupied slots.

### Morph Save UI

1. On the Save page, encoder type movement shows:
   - `Save:[Kit     ]`
   - `Save:[KitMrp  ]`
   - `Save:[Scene   ]`
2. `Save:[KitMrp  ]` uses the Kit slot browser and Kit overwrite cache.
3. Confirming `Save:[KitMrp  ]` creates or overwrites a root `Kit/NNN Name/`
   folder, not a flat `pNNN.snd` morph file.

### Morph Save File Semantics

1. Load a Kit with audible morphable parameter differences.
2. Set per-voice Morph amount to `0`.
3. Save with `KitMrp`.
4. Inspect saved instrument files:
   - morphable `[params]` values equal the original normal endpoints;
   - morphable `[morph]` values equal the original normal endpoints.
5. Set per-voice Morph amount to `255`.
6. Save with `KitMrp`.
7. Inspect saved instrument files:
   - morphable `[params]` values equal the old Morph endpoints;
   - morphable `[morph]` values equal the old normal endpoints.
8. Set per-voice Morph amount to an intermediate value.
9. Save immediately after the edit.
10. Confirm morphable `[params]` values match the Morph engine interpolation
    formula even if the runtime worker had not fully drained before save start.
11. Confirm non-morphable `[params]` values remain normal main values and do
    not appear in `[morph]`.
12. Confirm LFO `self` still emits only for own-slot LFO voice selector keys.
13. Confirm reloading the saved Kit at Morph 0 audibly matches the saved
    snapshot, and Morph 255 moves toward the original normal endpoint.

### Generated Track-7 Decay

1. Use a non-Choke instrument in slot 6 with generated track-7 decay.
2. Set different main and Morph generated decay endpoints.
3. Save with `KitMrp` at Morph 255.
4. Confirm `kitset.kcg` writes:
   - `slot6_track7_amp_envelope_decay` as the old Morph endpoint;
   - `slot6_track7_morph_amp_envelope_decay` as the old normal endpoint.

## Risks And Constraints

- The Kit directory writer overwrites authoritative files and may leave stale
  unreferenced files in the target folder because asyncfatfs still lacks
  recursive directory replace. This is already the normal Kit Save policy;
  KitMrp Save should share it.
- Hidden LFO Morph modulation is runtime-only and should not be serialized by
  KitMrp Save. The save snapshot should use retained per-voice Morph amounts.
- Avoid adding one-line forwarding APIs. New functions are justified only at
  module boundaries: Menu -> Preset, Preset -> Filesystem, and shared Morph
  interpolation.
- Do not alter normal Kit Save or Scene Save endpoint preservation. Only
  Save:[KitMrp] gets the flipped snapshot policy.

## Implementation Notes

Implemented on 2026-07-13:

- `Core/Menu/menu.c`
  - Replaced name-based overwrite detection with an occupancy/placement scan.
  - Included `SAVE_TYPE_KIT_MORPH` in Kit overwrite checks.
  - Kept `OK`/`OW` painting centralized in `menu_repaintLoadSavePage()`.
  - Added `Save:[KitMrp]` confirmation routing to
    `preset_saveKitMorphSnapshot()`.
  - Removed the Save-page encoder skip over `SAVE_TYPE_KIT_MORPH`.
  - Removed the reset-time collapse from `SAVE_TYPE_KIT_MORPH` back to
    `SAVE_TYPE_KIT`.
- `Core/Menu/menu.h`
  - Updated the `SAVE_TYPE_KIT_MORPH` enum comment so it documents both
    KitMrp Load and KitMrp Save semantics.
- `Core/Scene/Preset/presetManager.h/.c`
  - Added `PRESET_OP_KIT_MORPH_SAVE`.
  - Added `preset_saveKitMorphSnapshot()`.
  - Kept `preset_saveDrumset(..., 1)` on the legacy flat `.SND` morph path.
- `Core/Hardware/SD/filesystem.h/.c`
  - Added `filesystem_requestSaveKitMorphSnapshot()`.
  - Added `op_kit_save_policy` and reset it to normal in `filesystem_start()`.
  - Reused `FS_INTERNAL_OP_SAVE_KIT` for KitMrp Save, selecting
    `STORAGE_KIT_SAVE_MORPH_SNAPSHOT` only through the new request.
  - Threaded save policy and retained per-voice Morph amounts into the
    kitset/instrument text writer contexts.
  - Left embedded Scene Kit Save on `STORAGE_KIT_SAVE_NORMAL`.
- `Core/Hardware/SD/storageTypes.h/.c`
  - Added `storage_kit_save_policy_t`.
  - Made `storage_formatKitsetLine()` policy-aware for the generated
    slot-6/track-7 decay endpoint pair.
  - Made `storage_formatInstrumentLine()` policy-aware for morphable
    descriptor rows.
  - Preserved non-morphable `[params]` output and LFO `self` handling.
- `Core/Scene/Preset/presetMorphEngine.h/.c`
  - Exported the descriptor Morph interpolation helper as
    `presetMorph_interpolateValue()`.
  - Updated the Morph worker to call the exported helper so runtime Morph and
    KitMrp Save use the same 0..255 endpoint math.

Verification during implementation:

- `git diff --check` passed.
- `make` passed. Linker emitted the existing/newlib nano warnings for
  unimplemented `_close`, `_lseek`, `_read`, and `_write`, but produced
  `build/lxr02.elf` and `build/lxr02.bin`.
- `make img` passed and wrote `build/LXRV2_lxr02.img`.

## Post-Mortem: Failed KitMrp Save Implementation

Added after hardware/user test on 2026-07-13.

Observed behavior:

- The `OK` -> `OW` confirmation behavior appears to be correct.
- `Save:[KitMrp]` does not produce a usable Kit.
- On an empty target, the operation produces an empty folder.
- On an occupied target, the operation can create another visible folder with
  the same long display name, and that new folder is also empty.

Conclusion:

The implementation should be considered failed. The menu/UI portion is probably
salvageable, but the save implementation is not trustworthy. A reset of the
code changes is reasonable; this audit section should be retained as the record
of why the attempt failed.

### What I Got Wrong

#### 1. I treated the current Kit directory writer as a replace-capable writer.

The plan reused `filesystem_saveKitDirectory_tick()` because normal Kit Save
already wrote:

- `Kit/<NNN Name>/`
- six instrument files
- `kitset.kcg`

That was the wrong abstraction to trust without re-auditing its replacement
semantics. The writer calls `afatfs_mkdir_lfn()` for the target folder every
time. That is a create/open primitive with partial long-name behavior, not a
library-slot replacement primitive.

The existing filesystem spec already warned that asyncfatfs lacks:

- atomic rename/replace;
- recursive directory delete/replace.

I carried that warning forward in comments, but then implemented a feature that
requires reliable replacement anyway. That is the central design error.

#### 2. `afatfs_mkdir_lfn()` does not prove uniqueness by visible long name.

The LFN create/open code generates a short 8.3 alias from the requested display
name and scans for that short alias. It only considers an existing entry to be
the requested long name after the short alias matches and the reconstructed LFN
chain also matches.

That means a folder can already exist with the same visible long name but a
different short alias, for example:

- host-created folders whose SFN alias differs from firmware's generated alias;
- previous collision-generated aliases such as `FOO~1`;
- prior failed/duplicate firmware-created folders.

In that case the create scan does not say "the visible LFN already exists."
It says "my current SFN candidate was not found," then creates a new directory
entry. Host operating systems can then display two folders with the same long
name. This exactly matches the reported duplicate-folder symptom.

This is not KitMrp-specific. KitMrp exposed the flaw because it reused the same
target-folder create path, but any root Kit/Scene save that relies on
`*_mkdir_lfn()` as a replacement operation has the same class of risk.

#### 3. The save state machine leaves debris when a later phase fails.

`filesystem_saveKitDirectory_tick()` creates or opens the target folder before
it opens any child instrument file or `kitset.kcg`.

After phase 6/7 creates the target folder, any later failure leaves that folder
on the card. There is no rollback and no temp-directory promotion layer. This
means an empty folder is the natural failure artifact for any error in phases
8 through 18, including:

- failing to `chdir()` into the target directory;
- failing to open/create the first instrument file;
- failing inside the first instrument line writer;
- filesystem status becoming error without the UI surfacing the phase.

The user-observed empty folder proves the operation reached at least target
folder creation. It does not prove any instrument writer ran.

#### 4. The implementation had no useful runtime diagnostics for this failure.

The code has `filesystem_diagOp()`, `filesystem_diagPhase()`, and
`filesystem_diagBytesDone()`, but the implementation did not add temporary
diagnostic latching or UI reporting for the KitMrp save path. As a result,
hardware testing reports only the on-card artifact, not the failed phase.

Given the async state machine, that was not enough observability. The correct
debug pass should have recorded:

- operation kind;
- `op_phase` at failure;
- whether phase 7 returned an existing or newly-created directory;
- generated target folder alias;
- first child file name and alias attempted;
- line writer index when an instrument file fails;
- final `FS_STATUS_ERROR` vs apparent success.

#### 5. I widened the storage writer before proving the filesystem boundary.

The policy work in `storageTypes.c` may be logically correct, but it is not
the first thing that needed to be proved. The card never receives a valid file
set, so `[params]`/`[morph]` value selection is downstream of the actual
failure. I spent effort on the value-flip policy while assuming the directory
writer was dependable. That was backwards.

### Most Likely Failure Mechanics

The duplicate-folder behavior is most likely caused by `afatfs_mkdir_lfn()`
failing to match an existing visible long name whose short alias differs from
the newly generated alias candidate.

The empty-folder behavior is less precisely localized from code review alone.
The strongest code-level explanation is:

1. `filesystem_saveKitDirectory_tick()` successfully creates or opens the
   target folder.
2. A later phase fails before child files are committed.
3. The filesystem has no rollback, so the newly-created folder remains empty.

Possible failing points include the first `afatfs_fopen_lfn()` for an
instrument file or the first `storage_formatInstrumentLine()` call returning
zero because the slot/type/registry context is not valid in the actual
hardware state. The current code review did not prove which one happened.

### Why I Do Not Think There Is A Safe One-Line Fix

A small partial fix exists for occupied slots:

- if `filesystem_kitSlotExists(op_slot)` is true, phase 6 could open
  `kit_slot_open_name[op_slot]` with `afatfs_fopen(..., "r", ...)` instead of
  calling `afatfs_mkdir_lfn()`;
- only empty slots would call `afatfs_mkdir_lfn()`.

That would probably reduce duplicate-folder creation for folders already known
to the scan cache.

It is not sufficient as a safe fix because:

- it does not explain or repair the empty-folder result on new targets;
- it depends on the scan cache being fresh and truthful;
- it does not implement replacement semantics for stale child files;
- it does not solve visible-name duplicates that already exist on disk;
- it still writes directly into the authoritative folder with no temp/commit
  boundary.

The robust fix is not one line. It requires one of these approaches:

1. Implement a real replace operation:
   - find existing numbered folder by slot/open alias;
   - write into a temporary sibling directory;
   - verify all child files and `kitset.kcg`;
   - atomically promote or at least delete/rename in a controlled order.

2. Implement an in-place overwrite operation:
   - open the existing scanned slot by cached alias when occupied;
   - write/truncate the six files referenced by the new `kitset.kcg`;
   - write `kitset.kcg` last;
   - avoid `mkdir_lfn()` for occupied slots;
   - tolerate stale unreferenced files but never create duplicate folders.

3. First add diagnostics and reproduce:
   - latch the failing phase and attempted filename;
   - prove whether the first instrument open or writer is failing;
   - only then patch the save path.

### Recommended Reset Scope

If resetting this implementation, the code changes to discard are the files
touched for this feature:

- `Core/Menu/menu.c`
- `Core/Menu/menu.h`
- `Core/Scene/Preset/presetManager.c`
- `Core/Scene/Preset/presetManager.h`
- `Core/Scene/Preset/presetMorphEngine.c`
- `Core/Scene/Preset/presetMorphEngine.h`
- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/storageTypes.c`
- `Core/Hardware/SD/storageTypes.h`
- `build/LXRV2_lxr02.img`

Retain this audit document.

If keeping only the `OK`/`OW` fix, the salvageable code is limited to
`menu_currentSaveWouldOverwrite()` and the save indicator repaint order in
`Core/Menu/menu.c`. Everything below the `Save:[KitMrp]` command path should be
treated as suspect.

## Reset-Baseline Implementation Trajectory

This section is the forward path from a reset code baseline, not a repair plan
for the failed uncommitted implementation above.

Assumptions:

- The failed code changes in `Core/` and `build/LXRV2_lxr02.img` have been
  discarded.
- This audit document remains.
- The baseline still includes the previously working normal Kit Save, Scene
  Save/Load, and the observed mostly-correct `OK` -> `OW` behavior may be
  reimplemented separately if reset removes it.
- No Morph Save code from the failed attempt should be reused blindly.

### Phase 0: Restore A Known Good Baseline

Goal:

- Return firmware behavior to the last user-tested state where normal Kit Save
  and Scene Save/Load mostly worked.

Required actions:

- Reset the failed implementation files listed in "Recommended Reset Scope".
- Rebuild with `make`.
- Rebuild image with `make img`.
- Hardware-smoke normal Kit Save and Scene Save/Load before touching Morph
  Save again.

Acceptance:

- Normal `Save:[Kit]` writes a loadable Kit folder.
- Scene Save/Load behavior returns to the pre-Morph-Save attempt state.
- No duplicate folder is created by simply exercising baseline Kit/Scene save.

### Phase 1: Reapply Only The Occupancy-Based `OK`/`OW` Fix

Goal:

- Land the small UI behavior fix independently of Morph Save.

Implementation target:

- `Core/Menu/menu.c`

Rules:

- The save repaint should write `OK` first and then overwrite with `OW` if
  the selected operation will write into any occupied target slot.
- Occupancy means `filesystem_kitSlotExists()` for Kit-family operations and
  `filesystem_sceneSlotExists()` for Scene operations.
- Do not add `Save:[KitMrp]` yet.
- Do not touch Preset, Filesystem, storageTypes, or Morph code in this phase.

Acceptance:

- Scroll Save Kit to an empty slot: `OK`.
- Scroll Save Kit to an occupied slot: persistent `OW`.
- Scroll Save Scene to an empty slot: `OK`.
- Scroll Save Scene to an occupied slot: persistent `OW`.
- Existing Kit and Scene saves still produce valid files.

### Phase 2: Instrument The Existing Directory Save Writer

Goal:

- Make the current normal Kit Save state machine observable before adding any
  new save mode.

Implementation target:

- `Core/Hardware/SD/filesystem.c`
- optionally a tiny diagnostic accessor in `filesystem.h` if needed.

Add temporary or permanent debug latches:

- last save operation type;
- last `op_phase`;
- last attempted target folder display name;
- last target folder open alias;
- last attempted child display filename;
- last attempted child open alias;
- last write line index;
- final status.

Important:

- Diagnostics must be passive. They must not change save behavior.
- Do not change LFN creation/open behavior in this phase.

Acceptance:

- Normal Kit Save still works.
- If a save fails, the failure phase can be read without inferring from card
  debris.

### Phase 3: Define And Prove Baseline Kit Save Replacement Semantics

Goal:

- Establish what normal Kit Save is actually allowed to do on occupied slots
  before Morph Save shares the path.

Preferred baseline policy:

- For an occupied numbered Kit slot, open the scanned existing folder by
  `kit_slot_open_name[slot]`.
- Do not call `afatfs_mkdir_lfn()` for occupied slots.
- For an empty numbered Kit slot, create the folder with `afatfs_mkdir_lfn()`.
- Write child instrument files first.
- Write `kitset.kcg` last because the loader treats it as authoritative.
- Stale unreferenced child files may remain until recursive delete exists, but
  the save must never create duplicate visible Kit folders for an occupied
  scanned slot.

Non-goals:

- Do not implement Morph Save yet.
- Do not implement recursive delete.
- Do not implement temp-directory promotion unless the project is ready to add
  real rename/delete primitives.

Acceptance:

- Saving over an occupied firmware-created Kit slot does not create a duplicate
  folder.
- Saving over an occupied host-created `NNN Name` Kit slot does not create a
  duplicate folder if the scan cache found that slot.
- Saving to an empty Kit slot creates one folder and all expected files.
- The resulting folder reloads.
- Diagnostics confirm child files and `kitset.kcg` were written.

### Phase 4: Add A Storage-Level Morph Snapshot Policy In Isolation

Goal:

- Implement the value-selection rule without adding a UI command yet.

Implementation targets:

- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`
- `Core/Scene/Preset/presetMorphEngine.h/.c` only if sharing the interpolation
  helper is still needed.

Rules:

- Add a save policy enum such as:
  - normal Kit save;
  - Morph snapshot Kit save.
- Normal policy must be byte-for-byte equivalent to baseline output.
- Morph snapshot policy:
  - morphable `[params]` = interpolation of main endpoint and Morph endpoint
    using the retained per-voice Morph amount;
  - morphable `[morph]` = normal main endpoint;
  - non-morphable rows remain normal `[params]` only;
  - LFO `self` behavior remains unchanged and applies only to non-morphable
    LFO voice selector rows.
- Do not read `morph_interpolation[]` for the saved snapshot. Compute from
  retained endpoints and retained per-voice Morph amount.

Acceptance:

- A small host/test harness or forced save path can format lines for one known
  instrument slot and show expected normal policy output.
- The same slot under Morph snapshot policy shows the expected flip at Morph
  0, 127/128, and 255.
- This phase does not expose `Save:[KitMrp]` in Menu.

### Phase 5: Add Filesystem Policy Plumbing To The Proven Kit Writer

Goal:

- Let the already-proven Kit directory writer choose normal or Morph snapshot
  value selection without changing folder replacement behavior.

Implementation targets:

- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/filesystem.h`

Rules:

- Reuse the proven normal Kit Save state machine only after Phase 3 passes.
- Add an operation save policy field.
- Reset policy to normal at operation start.
- Add a new request such as `filesystem_requestSaveKitMorphSnapshot()`.
- The new request must select the snapshot policy but otherwise follow the
  same occupied-slot/open-existing and empty-slot/create behavior proven in
  Phase 3.

Acceptance:

- Normal Kit Save output remains valid.
- A private/debug caller can run Morph snapshot policy and write a complete
  loadable Kit folder.
- Occupied-slot Morph snapshot save does not create a duplicate folder.

### Phase 6: Add Preset And Menu Exposure

Goal:

- Expose `Save:[KitMrp]` only after the lower layers are proven.

Implementation targets:

- `Core/Scene/Preset/presetManager.h/.c`
- `Core/Menu/menu.h/.c`

Rules:

- Add a dedicated Preset API, not `preset_saveDrumset(..., 1)`.
- Keep legacy flat `FS_FILE_MORPH` save untouched.
- Add `Save:[KitMrp]` between Kit and Scene.
- Save KitMrp uses the Kit browser and Kit overwrite occupancy.

Acceptance:

- Save page order is `Kit`, `KitMrp`, `Scene`.
- KitMrp save to empty slot writes a complete loadable Kit.
- KitMrp save to occupied slot shows `OW`, then overwrites in place without
  duplicate visible folders.
- Morph 255 flips morphable endpoints as intended.
- Non-morphable parameters remain normal.

### Phase 7: Documentation And Spec Update

Goal:

- Only after hardware validation, update project memory/specs to mark Morph
  Save implemented.

Files:

- `MEMORY.md`
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`
- this audit document

Acceptance:

- Docs distinguish:
  - KitMrp Load: source normal endpoints -> resident Morph endpoints;
  - KitMrp Save: retained interpolation snapshot -> file normal endpoints,
    retained normal endpoints -> file Morph endpoints.
- Docs state the replacement semantics actually implemented by the Kit writer.
