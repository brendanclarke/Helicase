# LFO Apply Path Audit

Date: 2026-07-11

Scope: investigate why the LFO target selected in the new instrument menu is not
modulating the target parameter in DSP, then track the backend implementation
that wires descriptor-backed targets into `ModulationNode`.

## Implementation Notes

2026-07-11 backend implementation pass:

- `Core/DSPAudio/modulationNode.h/.c` now support two target namespaces:
  legacy `parameterArray[]` destinations and descriptor-resolved direct runtime
  destinations.
- Added `modNode_clearDestination()`,
  `modNode_setDirectDestination()`, and
  `modNode_directOriginalValueChanged()` with adjacent comments documenting
  inputs, outputs, clients, affiliates, and why the functions cannot be folded
  into the legacy destination API.
- `ModulationNode` now stores `destinationMode`, a cached direct `Parameter`,
  and an optional waveform-interpolation target pointer. Legacy callers still
  use `modNode_setDestination()` with `parameterArray[]` ids.
- `modNode_resetTargets()` now restores both legacy and descriptor-backed
  targets through one restore helper instead of assuming every destination is a
  legacy array index.
- `modNode_updateValue()` now resolves the current target through the node mode
  and can apply the same modulation math to descriptor-backed instrument
  runtime pointers.
- `Core/DSP/Instruments/InstrumentManager.c` now resolves canonical descriptor
  target ids into live runtime `Parameter` targets, installs LFO targets through
  `modNode_setDirectDestination()`, and clears LFO targets through
  `modNode_clearDestination()`.
- Velocity targets were moved onto the same descriptor-backed backend because
  they had the same validation-only/non-applied bug shape.
- Dead legacy helpers `preset_applyLfoModTarget()` and
  `preset_applyVelocityModTarget()` were removed after confirming there were no
  live callers. Keeping them would leave a public API that accepts values shaped
  like descriptor ids but applies them as legacy `parameterArray[]` ids.
- `instrumentManager_targetValid()` now treats modulation eligibility as
  descriptor flag plus direct runtime binding. This keeps the menu picker from
  showing descriptor rows that cannot be restored and overlaid by
  `ModulationNode`.
- Ordinary descriptor runtime writes now notify
  `modNode_directOriginalValueChanged()` so a currently modulated target
  refreshes its captured original value after a base-value edit, morph apply,
  automation write, or kit apply.
- First verification: `make` passes. The build still emits the existing libc
  syscall and LTO address-space/type mismatch warnings seen elsewhere in this
  firmware tree.

## Result

Original root cause: the runtime write path stored and validated the new
descriptor-based target ID, but did not install non-off targets into the LFO
`ModulationNode`.

Implemented fix: `ModulationNode` now supports descriptor-resolved direct
runtime targets alongside legacy `parameterArray[]` targets, and
InstrumentManager now installs `lfo_target_param` and velocity targets through
that direct backend. A direct handoff from the descriptor ID to
`modNode_setDestination()` remains wrong; descriptor IDs now go through
`modNode_setDirectDestination()`.

## Current Target Value Flow

1. Target parameters are descriptor-backed instrument cells.
   - Example descriptors:
     `Core/DSP/Instruments/HiHat/HiHatParameters.c:143-153`
   - `lfo_amount` is a normal runtime-bound float field.
   - `lfo_target_voice` is `INSTRUMENT_BIND_LFO_TARGET_VOICE`.
   - `lfo_target_param` is `INSTRUMENT_BIND_LFO_TARGET_PARAM`.

2. The menu writes target edits through descriptor storage.
   - `Core/Menu/menu.c:932-993` resolves the source slot, sibling
     `lfo_target_voice` / `lfo_target_param` descriptor indices, selected target
     voice, and stored target parameter.
   - `Core/Menu/menu.c:995-1031` normalizes the stored target parameter into a
     canonical target ID for the selected target slot.
   - `Core/Menu/menu.c:1119-1175` steps only through modulatable descriptors and
     commits the final value with `preset_setSupplementalParameter()`.

3. Kit file load stores the same shape.
   - `Core/Hardware/SD/storageTypes.c:460-471` parses
     `INSTRUMENT_BIND_LFO_TARGET_PARAM` as a `uint16_t` and stores it in the
     instrument slot's generic parameter image.
   - `Core/Hardware/SD/storageTypes.c:478-485` clamps
     `lfo_target_voice` during parse.

4. Runtime apply enters Preset and then InstrumentManager.
   - `Core/Scene/Preset/presetManager.c:452-472`
     `preset_setSupplementalParameter()` stores the value in SceneData and, for
     the active Scene, calls `instrumentManager_writeRuntime()`.
   - `Core/Scene/Preset/presetManager.c:527-549`
     `preset_applyDrumsetVoice()` applies loaded non-morph descriptor cells the
     same way after a kit load.

5. InstrumentManager now installs the LFO target into DSP.
   - `Core/DSP/Instruments/InstrumentManager.c:783-813`
     `instrumentManager_installLfoModulationTarget()` resolves the source LFO
     node, clears off values through `modNode_clearDestination()`, resolves
     non-off canonical descriptor IDs, then calls
     `modNode_setDirectDestination()`.
   - `Core/DSP/Instruments/InstrumentManager.c:1057-1066`
     routes `INSTRUMENT_BIND_LFO_TARGET_PARAM` into that installer.
   - `Core/DSP/Instruments/InstrumentManager.c:815-844`
     moves velocity targets through the same direct backend.

The old symptom should now be gone at the runtime-install layer: a valid menu
selection or loaded kit target no longer stops at validation; it installs a
live runtime pointer into the source modulation node.

## DSP Dispatch Path

The audio block still executes the LFO modulation update:

- `Core/DSPAudio/mixer.c:489-502` resets modulation targets, reapplies velocity
  modulation, then dispatches all six LFOs.
- `Core/DSPAudio/lfo.c:131-135` calculates the next LFO value and calls
  `modNode_updateValue(&lfo->modTarget, val)`.

So the LFO calculation/dispatch path was already present. The implemented patch
adds the missing descriptor-aware destination installation and write/restore
backend that this dispatch path needed.

## Legacy ModulationNode Assumptions And Current Backend

Before the backend pass, `ModulationNode` was built only around
`parameterArray[]`.

Current backend:

- `Core/DSPAudio/modulationNode.h:44-59` adds target mode,
  `directParameter`, and `waveInterpTarget`.
- `Core/DSPAudio/modulationNode.h:73-111` declares the descriptor-aware clear,
  direct install, and original-value refresh APIs with namespace comments.
- `Core/DSPAudio/modulationNode.c:104-229` resolves active targets, captures
  original values, and restores either legacy or direct targets.
- `Core/DSPAudio/modulationNode.c:328-352` refreshes captured originals for
  descriptor-backed targets after ordinary runtime writes.
- `Core/DSPAudio/modulationNode.c:355-377` restores both target modes at the
  top of each block.
- `Core/DSPAudio/modulationNode.c:409-466` keeps
  `modNode_setDestination()` legacy-only and adds
  `modNode_setDirectDestination()` for canonical descriptor IDs.
- `Core/DSPAudio/modulationNode.c:469-520` applies modulation through the
  current resolved target instead of assuming `parameterArray[]`.

The new instrument runtime has deliberately moved away from this table:

- `Core/Scene/Preset/ParameterArray.c:1-10` says the file now preserves the
  legacy flat array for non-instrument callers, while instrument slot storage
  and runtime meaning have moved to `Core/DSP/Instruments`.
- `Core/Scene/Preset/ParameterArray.c:40-42` initializes the whole
  `parameterArray[]` to zero.
- `Core/Scene/Preset/ParameterArray.c:17-38` only writes through a
  `parameterArray[]` entry if that legacy entry has a non-null pointer.

Therefore, descriptor IDs must still never be passed to
`modNode_setDestination()`. The implemented direct backend keeps descriptor IDs
as identity keys and stores the resolved runtime pointer separately.

## Stale Apply Functions

Before the backend pass, two stale direct target helpers still existed:

- `Core/Scene/Preset/presetManager.c:264-293`
  `preset_applyLfoModTarget(uint8_t lfo, uint16_t targetParam)` narrows
  `targetParam` to `uint8_t` and passes it to `modNode_setDestination()`.
- `Core/Scene/Preset/presetManager.c:241-262`
  `preset_applyVelocityModTarget()` also writes old-style destination IDs into
  `ModulationNode`.

Those functions were removed in the implementation pass after confirming there
were no live callers. Preset still owns ordinary legacy sound-parameter apply,
but descriptor modulation target installation now belongs to InstrumentManager.

## Why `lfo_amount` Can Still Work

`lfo_amount` is not the failing part. It is a direct descriptor runtime binding:

- `Core/DSP/Instruments/HiHat/HiHatParameters.c:144` binds `lfo_amount` to
  `lfo.modTarget.amount`.
- `Core/DSP/Instruments/InstrumentManager.c:775-783` writes ordinary
  `INSTRUMENT_BIND_INSTANCE_OFFSET` descriptors into the current runtime
  instance.

So the LFO amount can be written successfully while the destination remains
unset or still points at a null legacy target.

## Required Fix Shape

Do not revive a hardcoded `modTargets[]` or `PAR_*` list. Instruments can
change, and voice slots can hold different instrument types. The correct path
has to resolve descriptor targets dynamically through InstrumentManager and
SceneData.

A proper fix needs a descriptor-aware modulation backend with these pieces:

1. Source LFO node resolver.
   - Given a source slot `0..5`, return the correct node:
     `voiceArray[0..2].lfo.modTarget`, `snareVoice.lfo.modTarget`,
     `cymbalVoice.lfo.modTarget`, or `hatVoice.lfo.modTarget`.
   - This should live beside the other runtime slot accessors in
     InstrumentManager, not in Menu.

2. Target descriptor resolver.
   - Given a canonical `instrument_param_id_t`, resolve:
     - target slot,
     - current target instrument type,
     - local descriptor index,
     - `ParamDescriptor`,
     - runtime instance pointer,
     - runtime offset/type for the modulation overlay target.
   - This must reuse descriptor flags so only
     `INSTRUMENT_PARAM_FLAG_MODULATABLE` targets are accepted.

3. Descriptor-backed destination install.
   - `INSTRUMENT_BIND_LFO_TARGET_PARAM` should install a non-off descriptor
     destination, not merely validate it.
   - Off should still clear the source node.
   - Loaded-kit apply and menu edits should both hit the same install path
     through `preset_setSupplementalParameter()`.

4. Descriptor-aware update and restore.
   - The modulation node must be able to capture the original value, write the
     modulated value, and restore it each block without relying on
     `parameterArray[]`.
   - `modNode_resetTargets()` currently restores through
     `paramArray_setParameter()`, which is insufficient for descriptor targets.

5. Preserve modulation-time semantics, not ordinary edit-time semantics.
   - The descriptor runtime pointer is the modulation target. For example,
     pitch rows use `osc.modNodeValue`, not `osc.midiFreq`; normal parameter
     edits update `midiFreq` and recalculate oscillator frequency, while LFO
     modulation changes the multiplier that the oscillator reads while
     rendering.
   - This means the descriptor-aware modulation backend should not call
     `instrumentManager_writeSpecialRuntime()` on every LFO tick. That function
     is the stored-value edit/load path, not the modulation overlay path.
   - The backend must instead resolve the descriptor's runtime pointer/type and
     apply the existing `ModulationNode` math to that pointer. It must preserve
     existing special-type behavior, especially `TYPE_SPECIAL_F` original value
     handling, where old `modNode_setDestination()` uses `1.0f` as the baseline.

## Implementation Direction

Recommended path:

1. Extend the modulation runtime with a descriptor target mode rather than
   trying to reinterpret descriptor IDs as `parameterArray[]` indices.
2. Add InstrumentManager helpers for:
   - resolving a source slot's `ModulationNode`,
   - resolving a canonical target ID to a live runtime target,
   - installing that live runtime target into `ModulationNode` as a direct
     modulation overlay target.
3. Route `INSTRUMENT_BIND_LFO_TARGET_PARAM` non-off values through that new
   install helper.
4. Once LFO works, migrate velocity modulation to the same backend so both
   target systems share descriptor semantics.
5. After the DSP path is working, remove or clearly quarantine stale helpers
   such as `preset_applyLfoModTarget()` / `preset_applyVelocityModTarget()` if
   they no longer have valid callers.

## Exact Code Change Blueprint

This section is the intended implementation plan for the backend pass. It is
written at the detail level needed for adjacent comments when the code is
changed.

### 1. Extend `ModulationNode` To Support Direct Descriptor Targets

Files:

- `Core/DSPAudio/modulationNode.h`
- `Core/DSPAudio/modulationNode.c`

Why this change must happen:

- `ModulationNode` currently stores only a legacy `parameterArray[]` index.
- Descriptor target IDs are not legacy indices, and `parameterArray[]` no
  longer owns instrument parameter pointers.
- The audio path already calls `modNode_updateValue()` for every LFO, so the
  least invasive backend is to teach `ModulationNode` one additional target
  mode while preserving the legacy mode for callers not yet migrated.

Required `modulationNode.h` shape:

```c
typedef enum {
    MOD_NODE_DEST_LEGACY_PARAM_ARRAY = 0,
    MOD_NODE_DEST_DIRECT_PARAMETER
} mod_node_destination_mode_t;

typedef struct ModulatorStruct
{
    uint16_t destination;
    uint8_t type;
    ptrValue originalValue;
    float amount;
    float lastVal;
    uint8_t destinationMode;
    Parameter directParameter;
    void *waveInterpTarget;
} ModulationNode;
```

Required public functions:

```c
void modNode_clearDestination(ModulationNode *vm);
uint8_t modNode_setDirectDestination(ModulationNode *vm,
                                     uint16_t destination,
                                     Parameter parameter,
                                     void *waveInterpTarget);
void modNode_directOriginalValueChanged(uint16_t destination);
```

Function details to document beside declarations:

- `modNode_clearDestination()`
  - Inputs: a modulation node pointer.
  - Outputs: restores any old target value, clears the target identity, clears
    any cached direct pointer, and leaves amount/last value untouched.
  - Why separate from `modNode_setDestination(..., 0)`: clearing a descriptor
    target is not the same as selecting legacy `parameterArray[0]`. The clear
    operation must work for both target modes and must not imply that zero is a
    meaningful descriptor destination.
  - Clients: InstrumentManager off handling, `modNode_init()`, future velocity
    target migration.
  - Affiliates: `modNode_resetTargets()`, the source LFO nodes in the voice
    structs, and legacy `modNode_setDestination()`.

- `modNode_setDirectDestination()`
  - Inputs:
    - `vm`: source modulation node to retarget.
    - `destination`: canonical descriptor ID. This is stored only as identity
      for refresh/restore matching; it is not interpreted as a `parameterArray`
      index.
    - `parameter`: resolved live runtime pointer and `TYPE_*` tag from the
      target descriptor.
    - `waveInterpTarget`: optional `OscInfo *` carried as `void *` so waveform
      interpolation can keep working without making the public header include
      `Oscillator.h`.
  - Outputs: returns nonzero when the pointer is valid and the direct
    destination was installed. On success it restores the old target, stores
    direct target state, captures the original value, and uses
    `MOD_NODE_DEST_DIRECT_PARAMETER`.
  - Why separate from `modNode_setDestination()`: the existing function's input
    is a legacy flat parameter id. Overloading it with descriptor IDs would make
    call sites ambiguous and reintroduce the bug this patch is meant to remove.
  - Clients: new InstrumentManager descriptor target installer.
  - Affiliates: `instrumentManager_targetValid()`,
    `instrumentManager_runtimeInstance()`, descriptor runtime bindings, and
    SceneData's selected instrument type.

- `modNode_directOriginalValueChanged()`
  - Inputs: canonical descriptor ID whose live runtime value has just been
    changed by an ordinary edit/load/morph apply.
  - Outputs: scans all velocity/LFO modulation nodes and refreshes
    `originalValue` for direct targets whose stored destination matches.
  - Why this cannot be folded into `modNode_originalValueChanged()`: the
    existing function receives a legacy `parameterArray[]` index. Descriptor
    targets use a different namespace and may point at runtime fields that never
    appear in `parameterArray[]`.
  - Clients: InstrumentManager after applying a descriptor runtime value.
  - Affiliates: `preset_applyInstrumentRuntimeValueInternal()`,
    `preset_setInstrumentParameter()`, morph apply, kit load apply, and future
    automation writes.

Required `modulationNode.c` internals:

- Add a static target resolver helper:

```c
static const Parameter *modNode_currentParameter(ModulationNode *vm,
                                                 Parameter *scratch);
```

  - For `MOD_NODE_DEST_LEGACY_PARAM_ARRAY`, bounds-check
    `vm->destination < END_OF_SOUND_PARAMETERS`, then return
    `&parameterArray[vm->destination]`.
  - For `MOD_NODE_DEST_DIRECT_PARAMETER`, return `&vm->directParameter`.
  - If no valid pointer exists, return null.
  - Why: `setDestination`, `resetTargets`, `updateValue`, and original-value
    refresh all need the same "what live parameter does this node currently
    mean?" logic.

- Add a static capture helper:

```c
static uint8_t modNode_captureOriginalValue(ModulationNode *vm);
```

  - Inputs: a node whose target mode/pointer are already set.
  - Outputs: captures the current target value into `vm->originalValue`; returns
    zero when there is no valid target pointer.
  - Required behavior: preserve old `TYPE_SPECIAL_F` behavior by setting
    `originalValue.flt = 1.0f`, because pitch/LFO-rate style modulation is a
    multiplier overlay, not a copy of the stored edit value.
  - Affiliates: `modNode_setDestination()`, `modNode_setDirectDestination()`,
    `modNode_originalValueChanged()`, and the new
    `modNode_directOriginalValueChanged()`.

- Add a static restore helper:

```c
static void modNode_restoreTarget(ModulationNode *vm);
```

  - Inputs: any modulation node.
  - Outputs: writes `vm->originalValue` back to the active target, using
    `paramArray_setParameter()` for legacy mode and direct pointer assignment
    for descriptor mode.
  - Why: `modNode_resetTargets()` currently assumes every target is a legacy
    `parameterArray[]` destination. Descriptor targets need the same restore
    cycle without going through the legacy array.

- Update `modNode_init()`:
  - Call `modNode_clearDestination(vm)` instead of implying legacy destination
    zero.
  - Preserve initialization of `amount` and `lastVal`.

- Update `modNode_setDestination()`:
  - Keep it as the legacy API only.
  - It should set `destinationMode = MOD_NODE_DEST_LEGACY_PARAM_ARRAY`, clear
    `directParameter`, clear `waveInterpTarget`, bounds-check the legacy
    destination before reading `parameterArray[]`, then capture original value.
  - This prevents old callers from accidentally using descriptor IDs through
    the legacy function.

- Update `modNode_resetTargets()`:
  - Keep the waveform interpolation generation reset.
  - Replace the repeated `paramArray_setParameter(node.destination,
    node.originalValue)` calls with `modNode_restoreTarget(&node)`.

- Update `modNode_updateValue()`:
  - Get the target through `modNode_currentParameter()`.
  - Keep the existing modulation math and type switch.
  - For waveform interpolation, use `vm->waveInterpTarget` in direct mode rather
    than `modNode_getWaveTargetOsc(vm->destination)`. The legacy helper can
    remain for legacy mode.

### 2. Add InstrumentManager Target Resolution Helpers

Files:

- `Core/DSP/Instruments/InstrumentManager.c`
- `Core/DSP/Instruments/InstrumentManager.h`, only if the target resolver or
  installer is made public for velocity/automation reuse in the same patch.

Why this change must happen:

- Only InstrumentManager knows how to turn a canonical descriptor ID into a
  target slot, current instrument type, descriptor, runtime instance, and
  offset/type.
- Menu must remain a selector only. It should not know DSP object addresses.
- `modulationNode.c` should not include every instrument voice type or
  understand descriptor tables.

Required public or private helper types:

```c
typedef struct {
    instrument_param_id_t id;
    uint8_t slot;
    uint8_t descriptor_index;
    const ParamDescriptor *descriptor;
    Parameter parameter;
    void *waveInterpTarget;
} instrument_runtime_target_t;
```

This can be private in `InstrumentManager.c` unless another module needs it
later.

Required helpers:

```c
static ModulationNode *instrumentManager_lfoModNodeForSlot(uint8_t slot);
static void *instrumentManager_waveInterpTarget(uint8_t slot,
                                                const ParamDescriptor *descriptor);
static uint8_t instrumentManager_resolveModulationTarget(
    uint8_t scene_index,
    instrument_param_id_t id,
    instrument_runtime_target_t *target_out);
static uint8_t instrumentManager_installLfoModulationTarget(
    uint8_t source_slot,
    instrument_param_id_t target_id);
```

Function details to document beside definitions:

- `instrumentManager_lfoModNodeForSlot()`
  - Inputs: zero-based source slot `0..5`.
  - Outputs: pointer to the source slot's LFO `modTarget`, or null for an
    invalid slot.
  - Mapping:
    - slots `0..2`: `&voiceArray[slot].lfo.modTarget`
    - slot `3`: `&snareVoice.lfo.modTarget`
    - slot `4`: `&cymbalVoice.lfo.modTarget`
    - slot `5`: `&hatVoice.lfo.modTarget`
  - Why separate from `instrumentManager_writeRuntime()`: LFO target install,
    off clearing, loaded-kit apply, and future velocity/source modulation work
    all need a single source-node resolver. Keeping it in one helper avoids
    duplicating the six-slot voice mapping.
  - Clients: `instrumentManager_installLfoModulationTarget()` and the off case
    of `INSTRUMENT_BIND_LFO_TARGET_PARAM`.
  - Affiliates: `voiceArray`, `snareVoice`, `cymbalVoice`, `hatVoice`, and
    `Lfo.modTarget`.

- `instrumentManager_waveInterpTarget()`
  - Inputs: target slot and descriptor.
  - Outputs: optional `OscInfo *` as `void *` when the descriptor targets an
    oscillator waveform field; null otherwise.
  - Why separate from generic target resolution: waveform interpolation is an
    optimization/detail for `TYPE_UINT8` oscillator waveform targets. Most
    parameters do not need an affiliated oscillator pointer.
  - Clients: `instrumentManager_resolveModulationTarget()`.
  - Affiliates: existing `instrumentManager_osc(slot, key)`,
    `modNode_setWaveInterpEnabled()`, and the `OscInfo` interpolation fields.
  - Constraint: this must be derived from the descriptor/runtime accessor
    pattern, not from a hardcoded modulation target list.

- `instrumentManager_resolveModulationTarget()`
  - Inputs:
    - `scene_index`: usually active Scene, but kept explicit for consistency
      with existing target validators.
    - `id`: canonical descriptor target ID.
    - `target_out`: output record.
  - Outputs: nonzero only when:
    - `id` is a voice parameter ID,
    - the target slot exists in SceneData,
    - the current instrument type exposes the local descriptor,
    - the descriptor is morphable and `INSTRUMENT_PARAM_FLAG_MODULATABLE`,
    - the descriptor runtime kind is `INSTRUMENT_BIND_INSTANCE_OFFSET`,
    - `instrumentManager_runtimeInstance(target_slot)` returns a live instance,
    - the resolved pointer is non-null.
  - On success, fills `target_out` with canonical identity, descriptor pointer,
    runtime `Parameter`, and optional waveform interpolation target.
  - Why this cannot be part of Menu: it resolves live DSP pointers, current
    instrument-slot type, and descriptor runtime offsets. Menu should only
    commit the stored target ID.
  - Why this cannot be part of `modulationNode.c`: modulationNode should apply
    to an already resolved `Parameter`; it must not know SceneData or instrument
    registries.
  - Clients: LFO target install now; velocity modulation install later.
  - Affiliates: `scene_instrumentSlotConst()`, `instrumentParam_slot()`,
    `instrumentParam_local()`, `instrumentManager_descriptor()`,
    `instrumentManager_targetValid()`, `instrumentManager_runtimeInstance()`,
    and descriptor flags.

- `instrumentManager_installLfoModulationTarget()`
  - Inputs:
    - `source_slot`: slot whose LFO is the modulation source.
    - `target_id`: canonical target ID or `INSTRUMENT_PARAM_INVALID`.
  - Outputs: returns nonzero if the source slot exists and the target was
    either cleared or installed. Off clears the node. Non-off resolves the
    target and calls `modNode_setDirectDestination()`.
  - Why separate from `instrumentManager_writeRuntime()`: the runtime writer is
    a dispatcher for many binding kinds. Installing a target requires source
    node resolution, target descriptor resolution, direct modulation node
    retargeting, and off behavior; that is enough logic to deserve a named
    helper and a detailed comment.
  - Clients: the `INSTRUMENT_BIND_LFO_TARGET_PARAM` case in
    `instrumentManager_writeRuntime()`.
  - Affiliates: `ModulationNode`, `lfo_target_voice` / `lfo_target_param`
    descriptor storage, SceneData, and loaded-kit apply through Preset.

Required `instrumentManager_writeRuntime()` changes:

- Replace the current non-off validation-only branch at
  `Core/DSP/Instruments/InstrumentManager.c:826-855` with:
  - `return instrumentManager_installLfoModulationTarget(slot, value);`
- The helper should handle both off and non-off so the switch no longer repeats
  the six source-node cases inline.
- Leave `INSTRUMENT_BIND_LFO_TARGET_VOICE` as validation-only. The actual DSP
  destination is fully identified by `lfo_target_param`; the voice cell is a
  paired UI/storage helper and is reconciled by Menu before target param commit.

### 3. Refresh Descriptor Target Original Values After Ordinary Writes

Files:

- `Core/DSP/Instruments/InstrumentManager.c`
- `Core/DSPAudio/modulationNode.h`
- `Core/DSPAudio/modulationNode.c`

Why this change must happen:

- Existing legacy modulation has `modNode_originalValueChanged(uint16_t idx)`
  for legacy `parameterArray[]` targets.
- Descriptor targets need the same behavior in their own ID namespace.
- If the user edits the base value of a parameter that is currently being
  modulated, the node must update its stored original value or the next block
  will restore the old value.

Required helper:

```c
static void instrumentManager_noteRuntimeValueChanged(uint8_t slot,
                                                      const ParamDescriptor *descriptor);
```

Inputs/outputs:

- Inputs: target slot and descriptor that was just applied to live runtime.
- Output: no direct return. If the descriptor has a valid local index and
  canonical ID, call `modNode_directOriginalValueChanged(id)`.

Why this helper should exist:

- `instrumentManager_writeRuntime()` receives a descriptor pointer, not a local
  descriptor index. To produce a canonical ID, InstrumentManager must either
  locate the descriptor's local index in the current registry entry or the
  caller must pass the index. A helper centralizes that lookup and keeps the
  notification optional/non-fatal.

Where to call it:

- After successful ordinary `INSTRUMENT_BIND_INSTANCE_OFFSET` writes.
- After successful `instrumentManager_writeSpecialRuntime()` writes for
  morphable descriptors whose runtime pointer may be a modulation target.
- Do not call it for `INSTRUMENT_BIND_LFO_TARGET_PARAM`; retargeting captures
  original value directly.
- Do not call it for `INSTRUMENT_BIND_LFO_TARGET_VOICE`; it has no direct DSP
  target.

Comment requirement:

- The adjacent comment should explicitly say this is not automation recording
  and not a SceneData write. It is a modulation-node cache refresh so active
  descriptor targets restore the current base value at the top of each audio
  block.

### 4. Preserve Loaded-Kit Behavior

Files:

- `Core/Scene/Preset/presetManager.c`
- `Core/DSP/Instruments/InstrumentManager.c`

Why this change must happen:

- Loaded kits already call `preset_setSupplementalParameter()` for non-morph
  descriptor rows in `preset_applyDrumsetVoice()`.
- Once `INSTRUMENT_BIND_LFO_TARGET_PARAM` installs the destination in
  InstrumentManager, kit load should automatically install LFO targets without
  adding filesystem code.

Required behavior:

- Do not add LFO-target apply logic to `filesystem.c` or `storageTypes.c`.
- Keep file parse as storage-only.
- Let Preset's post-load apply call the same runtime path used by menu edits.

Comment requirement:

- Any comment added near `preset_applyDrumsetVoice()` should say that target
  rows are descriptor runtime bindings and are applied through
  `preset_setSupplementalParameter()` so menu edits and loaded kits cannot
  drift into separate DSP paths.

### 5. Quarantine Legacy Target Helpers

Status: completed in the backend implementation pass.

Files:

- `Core/Scene/Preset/presetManager.h`
- `Core/Scene/Preset/presetManager.c`

Why this change had to happen:

- `preset_applyLfoModTarget()` and `preset_applyVelocityModTarget()` described
  old-style resolved modulation target IDs and called
  `modNode_setDestination()`.
- They were incompatible with descriptor target IDs.

Completed change after the new LFO backend was working:

- Searched for live callers.
- Removed the prototypes and definitions after the search found no valid live
  callers.

Comment requirement:

- The namespace mismatch is now documented in the audit and in the remaining
  `modNode_setDestination()` comments: `parameterArray[]` destination IDs are
  not `instrument_param_id_t` descriptor IDs.

### 6. Migrate Velocity Targets To The Same Path

Status: completed in the backend implementation pass.

Files:

- `Core/DSP/Instruments/InstrumentManager.c`
- `Core/DSPAudio/modulationNode.c`
- `Core/Scene/Preset/presetManager.c`

Why this change happened:

- Velocity targets had the same limitation as LFO targets:
  `INSTRUMENT_BIND_VELOCITY_TARGET` cleared off but only validated non-off
  descriptor IDs.
- Once `ModulationNode` supported direct descriptor destinations, velocity could
  share the same resolver/installer instead of keeping a split target backend.

Required helper:

```c
static uint8_t instrumentManager_installVelocityModulationTarget(
    uint8_t source_slot,
    instrument_param_id_t target_id);
```

Inputs/outputs:

- Inputs: source voice slot and canonical target ID/off sentinel.
- Outputs: clears or installs `velocityModulators[source_slot]`.
- Clients: `INSTRUMENT_BIND_VELOCITY_TARGET` in
  `instrumentManager_writeRuntime()`.
- Affiliates: same descriptor target resolver used by LFO.

This was not required to prove the LFO fix by itself, but doing it immediately
avoided leaving two target systems that look the same in storage and menus but
behave differently in DSP.

## Documentation Checklist For The Backend Patch

When implementing the backend, add comments beside the code changes that cover:

1. The two target namespaces.
   - Legacy `parameterArray[]` indices are still supported by
     `modNode_setDestination()`.
   - Canonical descriptor IDs are installed through
     `modNode_setDirectDestination()` and must not be passed to the legacy API.

2. Descriptor runtime pointers.
   - Instrument descriptors own the runtime offset/type.
   - SceneData owns the stored value.
   - ModulationNode owns temporary block-level overlay and restore.

3. Source versus target slot.
   - The source slot owns the LFO node.
   - The target slot is encoded inside `instrument_param_id_t`.
   - The two can differ, so helpers must name `source_slot` and `target_slot`
     explicitly.

4. Original value refresh.
   - Destination install captures the current runtime value.
   - Ordinary parameter edits must refresh captured originals for matching
     descriptor targets.
   - This is needed so `modNode_resetTargets()` restores the latest base value,
     not the value that happened to exist when the target was first selected.

5. No hardcoded target lists.
   - Target eligibility comes from descriptor flags.
   - Runtime addresses come from descriptor offsets and current slot type.
   - Optional oscillator waveform interpolation targets come from runtime
     accessors such as `instrumentManager_osc()`, not from `modTargets[]`.

## Backend Readiness

I have enough local code context to implement the backend. The only design
choice to confirm before coding is whether to migrate velocity targets in the
same patch as LFO. The code structure strongly favors doing both after the
direct descriptor target mode exists, but LFO can be proven first if the change
needs to stay smaller.

## Verification Plan

Manual hardware checks after implementation:

1. Select an LFO source voice and set `lfo_target_voice` to a different slot.
2. Select a clearly audible modulatable target for that target voice, such as
   oscillator pitch, waveform, filter frequency, or volume.
3. Set `lfo_amount` above zero and confirm audible modulation.
4. Change target parameter to off and confirm modulation stops.
5. Change target voice to a slot with a different instrument type and confirm
   stale/non-modulatable targets resolve to off.
6. Load a kit folder containing the same target settings and confirm the LFO
   destination is installed during kit apply, not only after touching the menu.

Code-level checks:

1. Confirm no new hardcoded target list is introduced.
2. Confirm descriptor flags drive eligibility.
3. Confirm descriptor target IDs are never passed directly to legacy
   `modNode_setDestination()` as `parameterArray[]` indices.
4. Confirm modulated `TYPE_SPECIAL_F` targets use modulation overlay semantics
   such as `modNodeValue` baseline `1.0f`, not ordinary stored-value edit
   semantics.
5. Confirm ordinary edits to a currently modulated descriptor target refresh the
   node's captured original value.
