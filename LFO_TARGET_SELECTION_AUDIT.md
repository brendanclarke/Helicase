# LFO Target Selection Audit

Date: 2026-07-11

## Scope

The LFO target selector is currently split across two descriptor-backed
supplemental parameters:

- `lfo_target_voice`
- `lfo_target_param`

Desired behavior:

- `lfo_target_voice` is the target voice number and must stay in `1..6`.
- `lfo_target_param` must select only `off` or a modulatable descriptor on the
  currently selected target voice.
- If the target voice changes and the same local descriptor index is not
  modulatable for the new voice slot's current instrument type,
`lfo_target_param` must reset to `off`.
- No hardcoded target lists. Instruments can change, and voice slots can change
  which instrument type they contain. All choices must come from the active
  Scene slot's current instrument registry entry and descriptor flags.
- When selecting `lfo_target_param`, non-modulatable parameters must be skipped
  entirely. They should not appear as repeated `off` entries. The picker should
  show exactly one `off` position: the zero/before-first-target position.

## Current Shape

Every instrument currently exposes the same two LFO target rows. Example from
`Core/DSP/Instruments/Drum/DrumParameters.c:151-152`:

```c
ROW_NOBIND("lfo_target_voice", "LFO", "DstVoice", "voi", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE),
ROW_NOBIND("lfo_target_param", "LFO", "DstParam", "dst", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM),
```

The values live in `SceneData` generic instrument storage:

- `menu_cellDisplayValue()` reads the current source slot's
  `instrument_parameters[descriptor_index]`
  (`Core/Menu/menu.c:842-853`).
- `menu_cellCommitValue()` writes supplemental descriptors through
  `preset_setSupplementalParameter()`
  (`Core/Menu/menu.c:860-879`).
- `preset_setSupplementalParameter()` writes the stored value before asking
  `InstrumentManager` to apply/validate runtime state
  (`Core/Scene/Preset/presetManager.c:469-471`).

Canonical instrument target IDs already exist:

```c
id = slot * INSTRUMENT_PARAM_COUNT + descriptor_index
```

with `INSTRUMENT_SLOT_COUNT == 6`, `INSTRUMENT_PARAM_COUNT == 64`, and
`INSTRUMENT_PARAM_INVALID == 0xffff`.

`instrumentManager_targetValid()` already validates a canonical target against
the active Scene slot's current instrument descriptor flags:

- it resolves the target slot from the canonical ID
- resolves that slot's current instrument type
- resolves the local descriptor index for that type
- requires `INSTRUMENT_PARAM_FLAG_MORPHABLE`
- for modulation targets, requires `INSTRUMENT_PARAM_FLAG_MODULATABLE`

See `Core/DSP/Instruments/InstrumentManager.c:286-306`.

## Current Bugs

### Voice Is Clamped Like A Generic 0-127 Parameter

`DTYPE_VOICE_LFO` falls through the default clamp in
`menu_clampCellValue()`:

```c
case DTYPE_VOICE_LFO:
    if (*value > 127u) *value = 127u;
```

See `Core/Menu/menu.c:1097-1103`.

That explains why the encoder can push `lfo_target_voice` above `6`.
Runtime validation later rejects values outside `1..6`
(`Core/DSP/Instruments/InstrumentManager.c:669-675`), but
`preset_setSupplementalParameter()` has already stored the bad value before that
validation runs.

### Target Param Is Edited As A Raw Canonical Number

Both encoder and knob edit paths treat `DTYPE_TARGET_SELECTION_LFO` as a raw
`uint16_t`:

- encoder path: `Core/Menu/menu.c:1786-1809`
- knob path: `Core/Menu/menu.c:2183-2204`

The only current clamp is:

```c
if (*value >= INSTRUMENT_VOICE_ID_COUNT)
    *value = INSTRUMENT_PARAM_INVALID;
```

See `Core/Menu/menu.c:1064-1070`.

That permits every canonical target ID from `0..383`, including:

- other voices than the selected `lfo_target_voice`
- descriptor indices that are empty for that target slot's current instrument
- descriptor indices that exist but are not modulatable
- supplemental rows such as target selectors

The UI is therefore walking raw storage IDs, not a filtered target list.
This likely explains the observed repeated `off` entries: non-modulatable raw
IDs are being rendered as invalid/off positions instead of being skipped.

### Voice And Param Are Not Coupled

The two cells are stored independently. Changing `lfo_target_voice` does not
rewrite or invalidate `lfo_target_param`.

The intended invariant is currently unenforced:

```text
lfo_target_param == off
OR
instrumentParam_slot(lfo_target_param) == lfo_target_voice - 1
AND instrumentManager_targetValid(scene, lfo_target_param, MODULATION)
```

### Loaded Files Are Only Partially Normalized

The Kit parser clamps `lfo_target_voice` while parsing
(`Core/Hardware/SD/storageTypes.c:478-486`), but it parses
`lfo_target_param` as an arbitrary `uint16_t` and stores it directly
(`Core/Hardware/SD/storageTypes.c:460-471`).

Because file key order should not matter, pair validation should happen after
the instrument file has been parsed or during a later normalization/apply pass,
not while parsing either key independently.

### Runtime Non-Off LFO Targets Are Not Yet Applied

`INSTRUMENT_BIND_LFO_TARGET_PARAM` currently clears the legacy `ModulationNode`
destination when the value is `off`, but for non-off values it only validates:

```c
return instrumentManager_targetValid(scene_getActiveIndex(),
                                     value,
                                     INSTRUMENT_TARGET_MODULATION);
```

See `Core/DSP/Instruments/InstrumentManager.c:677-706`.

That means the target selection UI can be corrected independently, but actual
DSP modulation still needs a descriptor-target adapter for `ModulationNode`.
The old `modTargets[]` path is stubbed to `{TEXT_EMPTY, PAR_NONE}` and must not
be revived as a hardcoded target list.

## Required Invariant

For each source LFO slot, store:

- `lfo_target_voice`: `1..INSTRUMENT_SLOT_COUNT`
- `lfo_target_param`: `INSTRUMENT_PARAM_INVALID` or a canonical target ID

The pair must always satisfy:

```text
target_voice = clamp(target_voice, 1, INSTRUMENT_SLOT_COUNT)

if target_param != INSTRUMENT_PARAM_INVALID:
    instrumentParam_slot(target_param) == target_voice - 1
    instrumentManager_targetValid(scene, target_param,
                                  INSTRUMENT_TARGET_MODULATION) == 1
```

When changing target voice:

- preserve the same local descriptor index only if the new target voice slot's
  current instrument type has that descriptor and it is modulatable
- otherwise set `lfo_target_param` to `INSTRUMENT_PARAM_INVALID`

This preserves intent across compatible instrument types but fails closed when
the target slot's instrument does not support the same local target.

## Registry-Driven Correction Plan

Do not build a table of LFO target names or descriptor IDs. The picker must scan
the current active Scene and instrument registry at runtime.

Suggested helpers:

1. `instrumentManager_descriptorIndexForBinding(type, binding_kind, index_out)`

   Scan `instrumentManager_registryEntry(type)->descriptors` and find the
   descriptor whose `runtime.kind` is `INSTRUMENT_BIND_LFO_TARGET_VOICE` or
   `INSTRUMENT_BIND_LFO_TARGET_PARAM`.

   This avoids hardcoding that Drum/Snare/Cymbal/HiHat currently place those
   rows at local descriptor indices `30/31`.

2. `instrumentManager_targetLocalValid(scene_index, target_slot, local, use)`

   Build `instrumentParam_make(target_slot, local)` and call the existing
   `instrumentManager_targetValid()`. This keeps validity tied to the target
   slot's current instrument type.

3. `instrumentManager_nextTargetForSlot(scene_index, target_slot, current,
   direction, use)`

   Scan the target slot's current `entry->descriptor_count`, filter with
   `instrumentManager_targetValid(..., INSTRUMENT_TARGET_MODULATION)`, and
   return canonical IDs only for valid descriptors.

   The list order should be descriptor order. `off` is the sentinel before the
   first valid target.

4. `menu_lfoTargetContext(cell, ...)`

   Given either LFO target cell, resolve:

   - source slot being edited
   - source instrument type
   - sibling `lfo_target_voice` descriptor index
   - sibling `lfo_target_param` descriptor index
   - selected target voice value
   - selected target slot
   - current target param value

   This helper reads sibling cells from the source slot's Scene storage; it must
   not assume fixed descriptor indices.

## Encoder And Knob Behavior

Both edit paths should call the same LFO-specific helpers so encoder and knobs
cannot diverge.

### Editing `lfo_target_voice`

For `DTYPE_VOICE_LFO`:

1. Read current voice, clamp missing/zero values to `1`.
2. Apply encoder/knob delta.
3. Clamp to `1..INSTRUMENT_SLOT_COUNT`.
4. Commit `lfo_target_voice`.
5. Reconcile sibling `lfo_target_param`:
   - if current param is `off`, leave it `off`
   - otherwise take `local = instrumentParam_local(current_param)`
   - build `candidate = instrumentParam_make(new_voice - 1, local)`
   - if candidate is valid for modulation, commit candidate
   - otherwise commit `INSTRUMENT_PARAM_INVALID`

This is the rule the user described: a voice change keeps the target only when
the equivalent local parameter is modulatable on the new voice's current
instrument.

### Editing `lfo_target_param`

For `DTYPE_TARGET_SELECTION_LFO`:

1. Read selected `lfo_target_voice` from the sibling cell and clamp to `1..6`.
2. Resolve `target_slot = selected_voice - 1`.
3. Treat the current target as `off` if:
   - it is `INSTRUMENT_PARAM_INVALID`
   - its encoded slot is not `target_slot`
   - it is not valid for modulation
4. On positive delta from `off`, select the first modulatable descriptor for the
   selected target slot's current instrument type.
5. On negative delta from the first valid target, select `off`.
6. For larger deltas, step through valid modulatable descriptors only.
7. Never present non-modulatable descriptors as extra `off` entries; there is
   only one `off`, before the first valid target.
8. Commit only `off` or a valid canonical target ID.

This prevents the picker from ever landing on a non-modulatable descriptor or a
descriptor from another voice slot.

## Display Behavior

Compact view can keep using `menu_formatInstrumentTargetShort()` after the
stored value is normalized, because that formatter resolves the canonical ID
through the active Scene slot type.

Single-parameter view for `lfo_target_param` should continue showing either:

- `off`
- `<descriptor category><descriptor long_name>`

But if stored data is stale or mismatched, the display path should treat it as
`off` until the value is normalized. It should not display a target from a voice
other than `lfo_target_voice`.

## Load And Instrument-Type Change Normalization

Because instruments and voice slots are dynamic, normalization is needed beyond
interactive menu edits.

Normalize each source slot's LFO target pair:

1. Find the source slot's `lfo_target_voice` and `lfo_target_param` descriptor
   indices by scanning the source instrument registry entry for binding kinds.
2. Clamp voice to `1..INSTRUMENT_SLOT_COUNT`.
3. If param is `off`, keep it off.
4. If param is non-off, derive the local descriptor index and rebuild the
   canonical ID using `voice - 1`.
5. Validate the rebuilt ID against the target slot's current instrument type.
6. Store rebuilt ID if valid; otherwise store `off`.

Normalization should run:

- after an instrument file is parsed, because key order is not guaranteed
- after a voice slot changes instrument type
- during kit apply before runtime writes install LFO destinations

This keeps loaded kits and dynamic instrument swaps consistent with the same
rules as menu edits.

## Runtime Adapter Still Needed

Correct target selection will produce clean canonical descriptor IDs, but the
current DSP modulation path still targets legacy `parameterArray` indices.

`ModulationNode` stores a `destination` and looks up `parameterArray[destination]`
when setting or updating modulation. Non-off descriptor IDs are not currently
adapted to a runtime pointer/original value, so `INSTRUMENT_BIND_LFO_TARGET_PARAM`
validates but does not install a non-off destination.

To complete LFO modulation after the picker is corrected, add a descriptor-aware
runtime target adapter. It should be registry-driven:

- resolve canonical target ID to target slot and local descriptor
- resolve target slot's current instrument instance
- use descriptor runtime binding to locate the target value
- preserve/restore original target value like the existing `ModulationNode`
  flow
- avoid `modTargets[]` and hardcoded legacy `PAR_*` lists

This can be a later implementation step, but it is why target selection may
still not produce audible modulation even after the UI picker is constrained.

## Implementation Order

Recommended order:

1. Add registry helpers for binding-kind lookup and next-valid modulation target
   lookup.
2. Add LFO target pair normalization helper.
3. Use the normalization helper in load/apply and instrument-type reset paths.
4. Route encoder and knob edits for `DTYPE_VOICE_LFO` and
   `DTYPE_TARGET_SELECTION_LFO` through LFO-specific edit helpers.
5. Adjust display to treat stale mismatched target-param values as `off`.
6. Add descriptor-aware `ModulationNode` runtime application for non-off LFO
   targets.

The important constraint for all six steps: no hardcoded per-instrument target
lists. The active Scene slot type and that type's descriptor flags are the only
source of truth.

## 2026-07-11 Menu Selection Fix Notes

Implemented in this pass:

- Added registry-driven helper APIs in `InstrumentManager`:
  - `instrumentManager_descriptorIndexForBinding()`
  - `instrumentManager_targetLocalValid()`
  - `instrumentManager_stepTargetForSlot()`
- Added detailed comments for those APIs in both
  `Core/DSP/Instruments/InstrumentManager.h` and
  `Core/DSP/Instruments/InstrumentManager.c`.
- Added Menu-side LFO target context/edit/display helpers in
  `Core/Menu/menu.c`.
- Routed both main encoder edits and endless-pot edits through the same
  LFO-specific helpers before the generic numeric edit path.
- `lfo_target_voice` is now clamped to `1..INSTRUMENT_SLOT_COUNT` in the menu
  edit path.
- Editing `lfo_target_voice` now reconciles sibling `lfo_target_param`:
  - preserves the same local descriptor on the new target voice only when that
    descriptor is modulatable for the target voice slot's current instrument
  - otherwise stores `INSTRUMENT_PARAM_INVALID`
- Editing `lfo_target_param` now steps through the selected target voice slot's
  active instrument descriptor table and filters with
  `INSTRUMENT_PARAM_FLAG_MODULATABLE`.
- Non-modulatable descriptors are skipped entirely. They are not displayed as
  repeated `off` positions.
- Compact and single-parameter display now treat stale/mismatched stored
  `lfo_target_param` values as the single `off` state without mutating storage
  during repaint.

Not implemented in this pass:

- Descriptor-aware DSP application for non-off LFO targets. The runtime path
  still validates non-off descriptor targets but does not install them into
  `ModulationNode`.
- File-load or instrument-swap normalization outside interactive menu edits.
  The display path hides stale values as `off`, and the edit path commits
  normalized values, but a separate load/apply normalization pass is still
  needed.

## 2026-07-11 Target Display Cleanup Notes

Implemented after hardware retest:

- Compact descriptor-target display no longer prefixes the target voice number.
  It now shows only the target descriptor's plain three-character short name,
  for example `wav` or `coa` instead of `1wa` or `1co`.
- Single-parameter descriptor-target display no longer shows `VoiceN` on the
  value row. It now shows the target descriptor's category in the left half and
  long name in the right half, matching normal parameter title semantics.
- The legacy static automation compact target formatter is no longer called
  with a voice-prefix option; that option was removed from the helper, so the
  old `1wa`/`1co` display style should not appear through that path either
  while Phase 4 replacement work is pending.

Reason:

- `lfo_target_voice` is already visible as its own menu parameter. Repeating
  the selected voice inside `lfo_target_param` display is redundant and hides
  the more useful descriptor category.
- The old voice-prefixed short text came from step automation conventions that
  are scheduled to be removed/replaced in Phase 4. Current descriptor-target
  displays should be descriptor-name based, not voice-prefix based.
