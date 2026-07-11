# Per-Voice Morph Modulation Spec And Audit

Date: 2026-07-11

Status: specification and implementation audit only. No code changes have been
made for this feature in this pass.

## Goal

Per-voice Morph must become a modulation destination for both velocity
modulation and LFO modulation.

The target lists for both systems should expose the same non-voice sound
targets through a new Scene mod target namespace:

- Scene Decimation, displayed as `srt`
- Voice 1-6 Morph, displayed as `1vm` through `6vm`
- Future modulatable effects parameters

Per-instrument decimation is not a Scene mod target. It is a voice-local
instrument descriptor row, also displayed as `srt`, and should appear in the
voice-local portion of LFO/velocity target lists when the selected/source voice
contains that descriptor. Scene Decimation is deliberately placed at the end of
the Scene mod target list so the two `srt` entries are not adjacent in the
velocity target list.

Instrument parameters remain registry-driven. A voice slot can contain any
instrument type, so no Morph, LFO, or velocity path may hardcode drum/snare/
cymbal/hihat parameter lists.

## Behavior Contract

### Shared Scene Mod Targets

Scene mod targets are sound-affecting targets that are not owned by one
instrument descriptor table. They need their own metadata table, but this table
must describe Scene-level targets only. It is not a replacement for the
instrument descriptor registry.

Initial target order:

1. `1vm`: Voice 1 Morph
2. `2vm`: Voice 2 Morph
3. `3vm`: Voice 3 Morph
4. `4vm`: Voice 4 Morph
5. `5vm`: Voice 5 Morph
6. `6vm`: Voice 6 Morph
7. `srt`: Scene Decimation / global decimation

Future effects targets append after these entries.

If effects targets are added before Scene Decimation, the final position of
Scene Decimation should be revisited. The current requirement is practical UI
separation from voice-local `instrument_decimation`, so Scene Decimation should
remain after all initial voice Morph targets and away from the voice descriptor
list's own `srt` entry.

The Scene target IDs must not collide with canonical voice descriptor IDs. The
current voice descriptor ID space is packed as slot plus local descriptor index,
and `INSTRUMENT_TOTAL_ID_COUNT` already reserves a wider range. The clean
implementation is to define a Scene-target base at the first ID after the voice
descriptor space and keep helper functions responsible for all tests and
decoding.

### LFO Target List

`lfo_target_voice` / `lfo_target_voice_2` gain one extra value after voice 6:

- `1` through `6`: normal voice target pages
- `7`: Scene target page, displayed as `scn`

When `DstVoice` is `scn`, `DstParam` walks the Scene mod target list. It should
show a single `off` entry, then only valid Scene mod targets. When `DstVoice`
is `1` through `6`, `DstParam` keeps the existing behavior: one `off` entry,
then only modulatable descriptors for that selected target voice's current
instrument type.

Changing between a voice page and `scn` must reconcile the paired target:

- voice to voice: preserve the local descriptor only if the new voice's current
  instrument has a modulatable descriptor in that local slot; otherwise reset
  to `off`
- voice to `scn`: preserve only if the current target is already a valid Scene
  target; otherwise reset to `off`
- `scn` to voice: preserve only if the current target is a valid voice target
  for the newly selected voice; otherwise reset to `off`

### Velocity Target List

The current velocity list is wrong. It can expose targets across the whole kit
and can display multiple `off` entries through legacy/generic target traversal.

Correct velocity target order for a voice slot:

1. one `off` entry
2. modulatable descriptor targets for the source voice slot only, using that
   slot's current instrument type
3. Scene mod targets

The source voice is the voice whose `velo_mod_dest` cell is being edited. The
list must not browse all six voice slots. The voice portion must use
`instrumentManager_stepTargetForSlot(scene, source_slot, ...)`, so swappable
instrument slots continue to work.

Per-instrument decimation belongs in step 2 as a voice-local descriptor target.
Scene Decimation belongs in step 3 and appears after `1vm` through `6vm`.

### Velocity Apply Behavior

Velocity modulation of per-voice Morph is a retained Scene set operation.

On trigger, if that voice's velocity destination is a Scene Morph target:

- scale the trigger velocity by the velocity modulation amount and the target's
  range
- for Morph, write a 0-255 value to the target voice's retained Scene Morph
  amount
- update the PERF menu value, exactly as if the value had been changed from the
  menu
- queue Morph interpolation for that one voice through `preset_morphVoice()`

This means velocity Morph modulation is not an invisible secondary layer. It
overrides the currently displayed per-voice Morph base value. Later menu edits,
overall Morph edits, or MIDI CC1 edits override the value again by writing the
same retained Scene field.

Scene Decimation velocity modulation follows the same retained-set rule but
uses the target's 0-127 range and the Scene Decimation setter.

### LFO Apply Behavior

LFO modulation of per-voice Morph is not a retained Scene set operation.

An LFO targeting a voice Morph writes a hidden secondary Morph modulation
layer. The retained per-voice Morph value remains the base value and remains
what the PERF menu displays. Menu edits, velocity modulation, and MIDI CC1 set
the base. LFO modulation is centered on that current base.

LFO polarity follows the same range-relative contract as descriptor modulation:

- `neg`: moves from the base down toward the target minimum
- `pos`: moves from the base up toward the target maximum
- `bi`: moves equally around the base and clamps at the target min/max

The LFO dispatch path must not immediately interpolate every Morphable
parameter. It only updates the secondary Morph amount for the target voice and
queues or marks the Morph worker. The Morph worker remains responsible for
applying one unit of work per tick.

When at least one voice Morph is LFO-modulated, the Morph worker gains one
extra work item for each modulated voice. That work item resolves the effective
Morph amount for that voice before descriptor interpolation proceeds. The
descriptor interpolation budget stays one morphable parameter per tick.

## Current Code Findings

### Descriptor Target Validation Is Correct For Voice Targets

`instrumentManager_targetValid()` accepts canonical voice descriptor IDs only
when the current target slot's descriptor is morphable, modulatable, directly
bound to an instance offset, and has a stable modulation range. This is correct
for instrument descriptor targets and should remain unchanged for that class.

`instrumentManager_stepTargetForSlot()` already provides the correct
registry-driven traversal for one selected voice slot. It skips
non-modulatable descriptors and exposes one `off` position. LFO voice-target
selection should keep using it.

### Scene Targets Cannot Be Direct ModulationNode Targets

`instrumentManager_installLfoModulationTarget()` and
`instrumentManager_installVelocityModulationTarget()` currently resolve targets
to live descriptor-backed runtime pointers and install them into
`ModulationNode`.

Scene Morph and Scene Decimation do not fit that direct-pointer contract:

- per-voice Morph is retained in `SceneData.settings.voice_morph_amount[]`
  and applied through `presetMorphEngine`
- Scene Decimation is retained as the Scene/PERF `srt` value and applied
  through `preset_setVoiceDecimationAll()`
- future effects targets will likely have their own owners

Therefore Scene targets need an adapter layer, not a fake pointer.

### Per-Instrument Decimation Is A Supplemental Voice Target

The `instrument_decimation` rows in each `*Parameters.c` file now use an
explicit `ROW_SLOT_DECIMATION(...)` wrapper. That wrapper still expands to the
image flags contract, so the descriptor is marked morphable, modulatable, and
automatable, but its runtime binding is `INSTRUMENT_BIND_SLOT_DECIMATION`
rather than `INSTRUMENT_BIND_INSTANCE_OFFSET`.

Current mechanical blocker: `instrumentManager_targetValid()` still rejects
modulation targets unless they are direct instance-offset descriptors. This is
correct for the direct `ModulationNode` pointer backend, but it means
per-instrument decimation needs the same style of adapter treatment as Scene
targets. It is voice-local and descriptor-owned, so it must stay in the
instrument target list, not move into the Scene target namespace.

### Velocity Menu Editing Still Uses Generic Target Editing

The menu has dedicated LFO target helpers, but `DTYPE_TARGET_SELECTION_VELO`
still falls into generic target-selection paths in several places. That generic
path increments stored values numerically and still has display helpers tied to
the legacy `modTargets[]` table.

This explains the observed bad velocity target list: it is not browsing "the
current voice descriptor targets plus Scene targets"; it is still too close to
the old global target-index model.

### LFO Dispatch Does Not Identify Its Source Slot

`lfo_dispatchNextValue(Lfo *lfo)` currently receives only the `Lfo` pointer and
updates `lfo->modTarget` and `lfo->modTarget2`. The mixer calls it explicitly
for the six voices.

Scene LFO targets need to know:

- which source slot emitted the LFO
- whether target pair 1 or pair 2 is being updated
- which Scene target is installed for that pair

The dispatch boundary must therefore either accept the source slot or store
owner identity in `Lfo`.

### Morph Worker Is Already The Correct Owner For Interpolation

`presetMorphEngine` now scans the current instrument descriptor table for each
slot and interpolates only descriptors flagged morphable. That is exactly the
right place to apply an effective per-voice Morph amount, because it does not
care which instrument type currently occupies a slot.

The worker currently snapshots retained base amounts from
`scene->settings.voice_morph_amount[]`. LFO Morph modulation should extend this
snapshot with a secondary effective amount without mutating the retained base.

## Required Code Changes

### 1. Add Scene Mod Target Metadata

Create `Core/Scene/SceneModTargets.h` and `Core/Scene/SceneModTargets.c`.

The module should define:

- a Scene target ID base outside the voice descriptor range
- a descriptor struct with ID, kind, target voice where applicable, category,
  long name, short name, min, max, and allowed use flags
- helpers to validate, step, display, and decode Scene targets

Suggested API:

```c
typedef uint16_t scene_mod_target_id_t;

typedef enum {
    SCENE_MOD_TARGET_KIND_DECIMATION_ALL = 0,
    SCENE_MOD_TARGET_KIND_VOICE_MORPH,
    SCENE_MOD_TARGET_KIND_EFFECT_PARAMETER
} scene_mod_target_kind_t;

typedef enum {
    SCENE_MOD_TARGET_USE_VELOCITY = 1u << 0,
    SCENE_MOD_TARGET_USE_LFO      = 1u << 1
} scene_mod_target_use_t;

typedef struct {
    scene_mod_target_id_t id;
    scene_mod_target_kind_t kind;
    uint8_t voice_slot;
    uint16_t min_value;
    uint16_t max_value;
    uint8_t use_flags;
    const char *category;
    const char *long_name;
    const char *short_name;
} scene_mod_target_descriptor_t;

uint8_t sceneModTarget_isSceneTarget(uint16_t id);
const scene_mod_target_descriptor_t *sceneModTarget_descriptor(uint16_t id);
uint8_t sceneModTarget_valid(uint16_t id, scene_mod_target_use_t use);
uint16_t sceneModTarget_step(uint16_t current, int8_t direction,
                             scene_mod_target_use_t use);
void sceneModTarget_formatShort(uint16_t id, char out[3]);
void sceneModTarget_formatFull(uint16_t id, char out_category[8],
                               char out_long[8]);
```

Comment text to carry into the header:

```c
/*
 * Scene mod targets are sound-affecting destinations that are not owned by an
 * instrument descriptor table. Voice descriptor targets stay in
 * InstrumentManager so swappable instruments keep their own parameter lists;
 * this module owns only Scene-level targets such as global decimation,
 * per-voice Morph, and future effects parameters.
 *
 * Inputs to the helpers are stored target IDs from menu/Scene parameter cells.
 * Outputs are validation, display text, and decoded range/kind metadata used by
 * Menu, InstrumentManager, velocity trigger handling, LFO dispatch, and the
 * Morph worker. Keeping the namespace here prevents hardcoded Scene target
 * lists from spreading into Menu or DSP code.
 */
```

Why this cannot live in InstrumentManager:

InstrumentManager owns instrument descriptor tables and direct runtime pointer
resolution. Scene targets are retained Scene settings and future effect
settings. Putting them in InstrumentManager would make instrument-target code
responsible for non-instrument storage and would encourage hardcoded special
cases beside descriptor traversal.

Scene Decimation order requirement:

The static Scene target metadata table should list `1vm` through `6vm` first
and Scene Decimation last. This prevents the velocity target picker from
showing voice-local `srt` immediately followed by Scene `srt` when a voice has
per-instrument decimation in its descriptor list.

### 2. Add Combined Target Browsing Helpers

Add helpers in `InstrumentManager.h/.c` or a small target-browser module used
by both Menu and runtime normalization:

```c
uint16_t instrumentManager_stepVelocityTargetForSource(
    uint8_t scene_index, uint8_t source_slot, uint16_t current,
    int8_t direction);

uint8_t instrumentManager_targetValidForVelocitySource(
    uint8_t scene_index, uint8_t source_slot, uint16_t target_id);
```

Behavior:

- `INSTRUMENT_PARAM_INVALID` is the only `off` value
- positive from `off` enters the source slot's descriptor list
- after the last valid source-slot descriptor, positive enters the Scene target
  list
- positive at the last Scene target stays there
- negative reverses that order and stops at `off`
- any stale current target normalizes to `off`

Comment text to carry into the header:

```c
/*
 * Walk the velocity target list for one source voice.
 *
 * Inputs: Scene index, zero-based source slot, current stored target ID or
 * INSTRUMENT_PARAM_INVALID, and signed direction. Output: the next legal
 * target in the velocity picker. The picker order is one off entry, the
 * source slot's current instrument descriptors that are valid modulation
 * targets, then Scene mod targets.
 *
 * This must stay separate from the generic descriptor stepper because velocity
 * has a mixed namespace: voice-local descriptor IDs plus Scene target IDs.
 * Menu callers need one stable traversal for encoder and knob edits, and load
 * normalization needs the same validity rule without knowing descriptor or
 * Scene-target internals.
 */
```

The existing `instrumentManager_stepTargetForSlot()` remains the lower-level
voice descriptor walker and should not learn about Scene targets.

Per-instrument decimation addition:

`instrumentManager_stepTargetForSlot()` should include
`instrument_decimation` once `instrumentManager_targetValid()` accepts
`INSTRUMENT_BIND_SLOT_DECIMATION` for modulation and automation. The stepper
does not need a hardcoded list; it should continue to scan descriptors and
trust the descriptor flags plus binding/range validation.

### 2a. Add A Supplemental Voice Mod Target Adapter

Per-instrument decimation cannot use the direct pointer backend because the
target is `mixer_decimation_rate[slot]`, reached through
`INSTRUMENT_BIND_SLOT_DECIMATION` and `instrumentManager_applyRuntimeValue()`.
It should nevertheless remain a voice-local descriptor target.

Add an adapter path in `InstrumentManager.c` for descriptor targets whose
runtime kind is `INSTRUMENT_BIND_SLOT_DECIMATION`:

- validation: accept the descriptor for modulation/automation when it has
  `INSTRUMENT_PARAM_FLAG_MODULATABLE` / `INSTRUMENT_PARAM_FLAG_AUTOMATABLE`,
  dtype `DTYPE_0B127`, and binding kind `INSTRUMENT_BIND_SLOT_DECIMATION`
- install: do not install a direct `Parameter` pointer into `ModulationNode`
- velocity apply: for a voice-local slot-decimation target, scale the trigger
  and velocity amount into 0-127 and call the existing runtime apply path for
  that descriptor/slot
- LFO apply: for a voice-local slot-decimation target, use the range-relative
  polarity helper with min 0, max 127, then call the existing runtime apply path
  for that descriptor/slot
- automation apply: step automation should write the descriptor image/runtime
  path exactly as normal descriptor automation, because the row is already an
  image parameter

Suggested installed target state:

```c
typedef enum {
    INSTALLED_MOD_TARGET_NONE = 0,
    INSTALLED_MOD_TARGET_DIRECT_PARAMETER,
    INSTALLED_MOD_TARGET_SLOT_DECIMATION,
    INSTALLED_MOD_TARGET_SCENE_TARGET
} installed_mod_target_kind_t;
```

Comment text to carry beside the adapter:

```c
/*
 * Slot decimation is a voice-local descriptor target with a supplemental
 * runtime binding.
 *
 * Inputs are the same canonical descriptor IDs used by normal voice targets.
 * Output is an installed modulation route that writes through
 * INSTRUMENT_BIND_SLOT_DECIMATION instead of a direct Parameter pointer.
 * This keeps per-instrument decimation in the source/selected voice target
 * list while avoiding a fake DrumVoice/SnareVoice/CymbalVoice/HiHatVoice
 * member pointer.
 *
 * This adapter cannot live in SceneModTargets because the target belongs to an
 * instrument descriptor row and changes with the instrument type loaded into a
 * slot. It also cannot be folded into ModulationNode's direct destination path
 * because ModulationNode restores pointer-backed parameters, while slot
 * decimation is applied through InstrumentManager's supplemental binding.
 */
```

### 3. Extend LFO Target Context For `scn`

Update `menu_lfoTargetContext()`,
`menu_lfoTargetNormalizeParam()`,
`menu_lfoTargetCommitVoiceAndReconcile()`,
`menu_lfoTargetEditVoice()`,
`menu_lfoTargetEditParam()`, and
`menu_lfoTargetDisplayValue()`.

Required behavior:

- clamp `lfo_target_voice` to `1..INSTRUMENT_SLOT_COUNT + 1`
- store `INSTRUMENT_SLOT_COUNT + 1` as the raw value for `scn`
- display `scn` for `DTYPE_VOICE_LFO` when the clamped value is the Scene page
- when the context is Scene, validate and step `lfo_target_param` through
  `SceneModTargets`
- when the context is voice, keep current descriptor-driven behavior

Comment text to carry beside the context struct/helper:

```c
/*
 * LFO destination voice has two namespaces. Values 1..INSTRUMENT_SLOT_COUNT
 * select a real target voice and make DstParam browse that voice's current
 * instrument descriptors. The extra value INSTRUMENT_SLOT_COUNT + 1 selects
 * the Scene target page, displayed as "scn", and makes DstParam browse
 * SceneModTargets.
 *
 * Inputs are the source slot's two sibling LFO target cells. Outputs describe
 * which namespace is active and provide either a target_slot for descriptor
 * traversal or a Scene-target browser for non-voice targets. This cannot be
 * folded into the generic cell editor because changing the voice cell must
 * reconcile the sibling parameter cell across namespaces.
 */
```

### 4. Replace Velocity Target Menu Editing

Add a velocity-specific context and edit/display helpers in `menu.c`.

Suggested helpers:

```c
static uint8_t menu_cellIsVelocityTargetParam(const menu_cell_t *cell);
static uint16_t menu_velocityTargetNormalize(const menu_cell_t *cell,
                                             uint16_t raw);
static uint8_t menu_velocityTargetEditParam(const menu_cell_t *cell,
                                            int16_t delta);
static uint16_t menu_velocityTargetDisplayValue(const menu_cell_t *cell,
                                                uint16_t raw);
```

Display helpers must use:

- `menu_formatInstrumentTargetShort()` / full descriptor display for voice
  descriptor targets
- `sceneModTarget_formatShort()` / `sceneModTarget_formatFull()` for Scene
  targets
- `off` for `INSTRUMENT_PARAM_INVALID`

Comment text to carry beside the edit helper:

```c
/*
 * Edit one velocity destination cell through the new mixed target list.
 *
 * Inputs: resolved velo_mod_dest cell and signed encoder/knob delta. Output:
 * source slot storage receives exactly one of: off, a modulatable descriptor
 * target on the same source slot, or a Scene mod target. Non-modulatable
 * descriptors are skipped and there is only one off entry.
 *
 * This cannot use the generic DTYPE_TARGET_SELECTION path because velocity
 * targets are not numeric ranges and are not the old modTargets[] table. The
 * traversal depends on the source slot's current instrument type and the
 * shared SceneModTargets list, so it must call the descriptor/Scene target
 * browsers instead of incrementing raw stored IDs.
 */
```

The old `menu_displayModTargetFull()` and `menu_displayModTargetShort()` should
remain only for legacy automation paths until Phase 4 removes them. New LFO and
velocity target display must not use `modTargets[]`.

### 5. Install Scene Targets Separately From Direct ModulationNode Targets

Extend `InstrumentManager.c` with storage for installed Scene targets:

```c
typedef struct {
    uint8_t active;
    uint16_t target_id;
} installed_scene_target_t;

static installed_scene_target_t velocity_scene_targets[INSTRUMENT_SLOT_COUNT];
static installed_scene_target_t lfo_scene_targets[INSTRUMENT_SLOT_COUNT][2];
```

Update install behavior:

- off clears both the direct `ModulationNode` destination and the matching
  Scene target slot
- slot decimation installs the voice-local supplemental target adapter and
  clears direct and Scene target state
- voice descriptor target installs the direct `ModulationNode` destination and
  clears the matching Scene target slot
- Scene target clears the direct `ModulationNode` destination and stores the
  Scene target ID in the matching Scene target slot

Comment text to carry beside the installed target struct:

```c
/*
 * Scene modulation targets are installed beside, not inside, ModulationNode.
 *
 * Direct descriptor targets resolve to a live Parameter pointer and can be
 * restored every audio block by ModulationNode. Slot decimation and Scene
 * targets are descriptor/sound targets with supplemental owners, so they need
 * owner-specific setters instead of fake pointers. InstrumentManager owns this
 * small routing table because it already translates menu/Scene target IDs into
 * runtime modulation backends.
 */
```

Why this should not be added as another `ModulationNode` destination mode:

`ModulationNode` is currently a direct parameter writer with range metadata.
Velocity Scene Morph is a retained set operation and LFO Scene Morph is a
secondary Morph-worker layer. If `ModulationNode` learned Preset and Scene
setters, it would become a cross-system dispatcher and would blur the distinct
velocity/LFO semantics.

### 6. Apply Velocity Scene Targets On Triggers

Add an InstrumentManager API:

```c
void instrumentManager_applyVelocitySceneTarget(uint8_t source_slot,
                                                float velocity_0_1);
```

Trigger paths should call it immediately after the existing
`modNode_updateValue(&velocityModulators[slot], velocity_0_1)` call. It should
no-op when the installed velocity target for that source slot is not a Scene
target.

For `SCENE_MOD_TARGET_KIND_VOICE_MORPH`:

- read `velocityModulators[source_slot].amount`
- compute `round(max_value * amount * velocity_0_1)`, with max `255`
- call `preset_morphVoice(target_voice_slot, value)`

For `SCENE_MOD_TARGET_KIND_DECIMATION_ALL`:

- compute the target's 0-127 value with the same amount and velocity rule
- call `preset_setVoiceDecimationAll(scene_getActiveIndex(), value)`

Comment text to carry into the header/API:

```c
/*
 * Apply a velocity-triggered Scene modulation target.
 *
 * Inputs: source_slot is the voice that was triggered, and velocity_0_1 is the
 * normalized trigger velocity already used by velocityModulators[source_slot].
 * Output: no direct DSP pointer is written. Instead, the installed Scene target
 * for that source slot is converted into an owner-specific retained set
 * operation, such as preset_morphVoice() for per-voice Morph.
 *
 * This function must stay separate from modNode_updateValue() because velocity
 * Scene targets intentionally update the retained PERF/Menu base value, while
 * direct descriptor targets are transient pointer writes. Keeping the Scene
 * path here lets trigger clients use one no-op-safe call without teaching each
 * instrument voice about Scene target kinds.
 */
```

Affected trigger clients:

- `Core/DSP/Instruments/Drum/DrumVoice.c`
- `Core/DSP/Instruments/Snare/Snare.c`
- `Core/DSP/Instruments/Cymbal/CymbalVoice.c`
- `Core/DSP/Instruments/HiHat/HiHat.c`

### 7. Expose Source Slot To LFO Dispatch

Change the LFO dispatch boundary so Scene targets can be updated with source
identity.

Preferred low-risk API:

```c
void lfo_dispatchNextValue(Lfo *lfo, uint8_t source_slot);
```

`mixer.c` already calls the six LFOs explicitly, so the source slots can be
passed without reverse-mapping pointers.

Inside `lfo_dispatchNextValue()`:

- calculate the waveform once
- update the two direct `ModulationNode` targets exactly as now
- call an InstrumentManager no-op-safe Scene target updater for pair 0 and pair
  1, passing source slot, pair, waveform value, polarity, and the pair amount

Suggested InstrumentManager API:

```c
void instrumentManager_updateLfoSceneTarget(uint8_t source_slot,
                                            uint8_t target_pair,
                                            float lfo_value_0_1,
                                            uint8_t polarity,
                                            float amount);
```

Comment text to carry beside the LFO dispatch declaration:

```c
/*
 * Dispatch one LFO block with explicit source-slot identity.
 *
 * Inputs: lfo is the oscillator/modulation state for one voice, and source_slot
 * identifies which instrument slot owns it. Output: the shared waveform updates
 * both direct ModulationNode targets and any installed Scene target adapters
 * for the same LFO pair slots.
 *
 * The source slot cannot be inferred reliably by Scene target code from the
 * Lfo pointer without hardcoding voice object addresses. Passing it from
 * mixer.c keeps LFO math generic while allowing InstrumentManager to route
 * Scene modulation by the same source slot used when the target was installed.
 */
```

### 8. Add LFO Morph Secondary Layer To The Morph Worker

Extend `presetMorphEngine.h/.c` with APIs for hidden LFO Morph modulation:

```c
void presetMorph_setVoiceLfoModulation(uint8_t scene_index,
                                       uint8_t target_slot,
                                       uint8_t source_slot,
                                       uint8_t target_pair,
                                       uint8_t active,
                                       uint8_t amount);
void presetMorph_clearLfoSource(uint8_t source_slot, uint8_t target_pair);
```

Implementation model:

- Keep retained base amounts in `SceneData.settings.voice_morph_amount[]`
- Store LFO Morph contributions separately from SceneData
- Track which voices have active LFO Morph modulation
- When an LFO contribution changes, queue that target voice for Morph work
- At the beginning of a pass, snapshot the base amount plus active LFO layer
  into `pass_amount[slot]`
- Spend one extra worker tick per active LFO-modulated voice to resolve the
  effective Morph amount before descriptor interpolation for that voice

Multiple LFOs targeting the same voice need a deterministic combine rule. The
recommended rule is to store per-source/pair contributions as signed deltas
from the current base, sum them, and clamp the effective amount to 0..255. This
is more predictable for a hidden secondary layer than last-writer-wins. If the
desired musical behavior is to match existing direct `ModulationNode` overwrite
ordering instead, this is the one point that should be decided before coding.

Comment text to carry into `presetMorphEngine.h`:

```c
/*
 * Set the hidden LFO Morph layer for one target voice.
 *
 * Inputs: Scene index, zero-based target voice slot, source LFO slot, target
 * pair index, active flag, and already-shaped effective Morph amount or delta
 * according to the implementation contract. Output: the Morph worker marks the
 * target voice dirty but does not mutate SceneData.settings.voice_morph_amount
 * and does not update PERF menu values.
 *
 * This API exists because LFO Morph modulation is not the same operation as
 * preset_morphVoice(). Menu, velocity, and MIDI CC1 set the retained base
 * Morph value. LFO modulation is a secondary layer centered on that base and
 * must be consumed by the bounded Morph worker so it never interpolates an
 * entire voice immediately from the audio/LFO dispatch path.
 */
```

Comment text to carry beside the worker tick extension:

```c
/*
 * Resolve one active LFO Morph amount as its own worker item.
 *
 * Inputs: the current pass slot and the stored LFO contribution table. Output:
 * pass_amount[slot] receives the effective base-plus-LFO Morph amount that
 * later descriptor interpolation will use. The function returns after one
 * voice-level resolve so an LFO-modulated Morph voice adds one bounded work
 * item, while actual descriptor interpolation remains one parameter per tick.
 *
 * This cannot be folded into every descriptor interpolation because that would
 * recalculate the same LFO/base combination for every morphable parameter in
 * the voice. It also cannot run in LFO dispatch because dispatch must not walk
 * descriptor tables or update all morphed parameters immediately.
 */
```

### 9. Share Range-Relative Polarity Math

`modNode_rangeValue()` already implements the correct descriptor-backed
negative/positive/bipolar shape, but it is static to `modulationNode.c` and
reads `ModulationNode` internals.

Add a small public helper, either in `modulationNode.h/.c` or a neutral
modulation range module:

```c
uint16_t modNode_shapeRangeU16(uint16_t base,
                               uint16_t min_value,
                               uint16_t max_value,
                               float source_0_1,
                               float amount_0_1,
                               uint8_t polarity);
```

Use it for LFO Scene Morph shaping so Morph modulation uses the same polarity
semantics as descriptor LFO modulation.

Comment text to carry beside the helper:

```c
/*
 * Shape a normalized modulation source against an explicit integer range.
 *
 * Inputs: retained base value, target min/max, normalized source, normalized
 * amount, and MOD_NODE_POLARITY_* selector. Output: a clamped integer target
 * value. This is the range-only form of the descriptor modulation math used by
 * ModulationNode, provided for Scene targets that have a real range but no
 * direct runtime Parameter pointer.
 *
 * This helper must be separate from modNode_rangeValue() because Scene targets
 * are not ModulationNode destinations and should not construct fake nodes or
 * fake Parameters just to reuse polarity math.
 */
```

## Expected File Touches On Implementation Pass

- `Core/Scene/SceneModTargets.h` and `.c`: new Scene target namespace,
  metadata, validation, stepping, display helpers
- `Core/DSP/Instruments/InstrumentManager.h` and `.c`: mixed target validation,
  velocity target stepping, slot-decimation supplemental target adapter, Scene
  target installation tables, velocity Scene apply function, LFO Scene update
  function
- `Core/Menu/menu.c`: LFO `scn` target voice, LFO Scene target browsing,
  velocity target browsing and display, removal of new target paths from
  legacy `modTargets[]` display
- `Core/DSPAudio/lfo.h` and `.c`: source-slot-aware dispatch and Scene target
  update calls
- `Core/DSPAudio/mixer.c`: pass source slots into `lfo_dispatchNextValue()`
- `Core/DSPAudio/modulationNode.h` and `.c`: exported range-shaping helper, or
  equivalent neutral helper if the implementation chooses a new module
- `Core/Scene/Preset/presetMorphEngine.h` and `.c`: secondary LFO Morph layer,
  effective amount snapshot, extra per-voice worker item
- `Core/Scene/Preset/presetManager.h` and `.c`: likely no new public Morph set
  API beyond existing `preset_morphVoice()` for velocity, but may need a
  no-op-safe Scene Decimation set wrapper for Scene target adapters
- Trigger clients: add a no-op-safe InstrumentManager velocity supplemental/
  Scene apply call beside existing velocity `ModulationNode` updates

## Testing Plan

1. Build firmware.
2. In each instrument type, open `velo_mod_dest`.
3. Confirm velocity target order:
   - one `off`
   - only that voice slot's modulatable descriptor targets
   - voice-local `srt` appears with the voice descriptor targets when the
     current instrument exposes per-instrument decimation
   - `1vm`, `2vm`, `3vm`, `4vm`, `5vm`, `6vm`, then Scene `srt`
   - no duplicate `off` entries
   - no targets from other voice slots
4. Swap a voice slot to a different instrument type and confirm the voice-local
   velocity target list changes without code changes.
5. In LFO DstVoice, scroll `1 2 3 4 5 6 scn` and clamp at both ends.
6. In LFO DstParam with DstVoice `scn`, confirm one `off` and the Scene target
   list only.
7. Set velocity target to `1vm` or another voice Morph target, trigger with
   different velocities and amount settings, and confirm PERF per-voice Morph
   display updates.
8. Set velocity and LFO targets to voice-local `srt` and confirm they modulate
   per-instrument decimation, not Scene Decimation.
9. Set LFO target to a voice Morph Scene target and confirm PERF display does
   not move, while Morph sound changes around the displayed base value.
10. Test LFO `neg`, `pos`, and `bi` polarity on Morph with base values near 0,
   middle, and 255 to confirm clamping and centering.
11. Set two LFOs to the same Morph target if multiple-source combine is
    implemented; confirm the chosen combine rule is deterministic.

## Open Implementation Decision

Multiple LFOs can theoretically target the same per-voice Morph. The spec
recommends summing signed deltas around the retained base and clamping to
0..255. This is likely the most musically useful behavior for a hidden
secondary layer, but it differs from the practical "last writer wins" behavior
that two direct `ModulationNode`s may have when aimed at the same raw parameter.

Decide this before implementation if exact parity with existing direct
modulation conflicts is more important than predictable layering.
