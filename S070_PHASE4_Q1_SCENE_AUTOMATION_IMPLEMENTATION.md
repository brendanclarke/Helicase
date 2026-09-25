# Q1 — Scene-Target Automation Runtime Overlay

Session: S070 · Branch: `dev-ph5-effects` · Finding: F2.

## 1. Defect summary

`seq_applySceneAutomation()` dispatches step-automation values for Scene
targets (voice Morph, global decimation, per-voice audio out, per-voice FX
send, slot-6 track-7 decay) through the same retained setters that Menu and
MIDI use. Those setters write to `scene_settings_t` /
`kit_settings_t` via `scene_storeParameterByte()` /
`scene_storeKitParameterByte()`, which:

1. Overwrite the user's retained Scene value with the automation transient.
2. Call `autosave_markSceneParameterDirty()` /
   `autosave_markKitParameterDirty()`, keeping the scalar AutoSave writer
   busy for the entire duration of playback.
3. Call `bank_invalidateSdCleanScene()`, defeating the Bank Save skip
   optimisation.

Result: AutoSave never converges while Scene-target automation is playing,
and a Scene Save or Bank Save captures whatever automation last wrote instead
of the user's deliberate setting.

## 2. Fix principle

Scene-target step automation must be a runtime overlay that never touches
retained Scene/Kit storage and never marks AutoSave dirty.

The architecture mirrors the existing voice-parameter overlay:

| Concern | Voice parameters | Scene targets (new) |
|---|---|---|
| Base values | `morph_interpolation[]` | `scene_settings_t` / `kit_settings_t` (retained, untouched) |
| Overlay apply | `instrumentManager_writeRuntime()` | Per-kind DSP runtime functions |
| Dirty tracking | `seq_automation_dirty[slot]` (uint64\_t × 6) | `seq_scene_automation_dirty` (uint32\_t × 1) |
| Restore | `seq_restoreAllAutomation()` | `seq_restoreAllSceneAutomation()` (new) |
| Clear | `seq_clearAutomationDirty()` | Extended to clear Scene bitmap |

The retained Scene values are never written by automation. On transport
stop/restart, the restore function re-applies retained values to the DSP
runtime, and the dirty bitmap is cleared.

## 3. Existing separation audit

Each Scene target kind already has partial separation between retain and
apply. The table shows what exists and what is needed.

| Kind | Retain path | Apply path | Exists? | Restore path |
|---|---|---|---|---|
| `VOICE_MORPH` | `scene_setVoiceMorphAmount()` → dirty | `presetMorph_requestVoice()` + PERF mirror | Partial — worker reads retained amount at snapshot | Need override mechanism |
| `DECIMATION_ALL` | `scene_setVoiceDecimationAll()` → dirty | `preset_applyVoiceDecimationAllRuntime()` + PERF mirror | **Yes** — runtime function exists | Call with retained value |
| `SLOT6_TRACK7_AMP_DECAY` | `scene_setSlot6Track7AmpEnvelopeDecay()` → dirty | Trigger path reads from `kit_settings_t` | No runtime override | Need override (LFO already has one: `slot6_track7_decay_lfo_*`) |
| `AUDIO_OUT` | `scene_setVoiceAudioOut()` → dirty | `mixer_audioRouting[slot]` write inside `preset_applyKitAudioRouting()` | No standalone runtime function | Need `preset_applyVoiceAudioOutRuntime()` |
| `FX_SEND` | `scene_setVoiceFxSendAmount()` → dirty | None (no FX bus yet) | N/A | No-op — skip retain, nothing to apply or restore |

## 4. LFO precedent

The LFO modulation system already solves the same problem for three of the
five target kinds:

- **Morph**: `presetMorph_setVoiceLfoModulation()` writes a hidden
  contribution table, not SceneData. The morph worker resolves LFO
  contributions in `presetMorph_resolveLfoAmount()` using the retained base.
- **Decimation**: `instrumentManager_updateLfoSceneDestination()` calls
  `preset_applyVoiceDecimationAllRuntime()` directly — no retain.
- **Slot-6 track-7 decay**: sets `slot6_track7_decay_lfo_active/value`
  runtime overrides; the trigger path checks them before the retained
  `kit_settings_t` value.

Step automation should use the same runtime-only pattern. The difference is
that LFO contributions are additive deviations from a base, while step
automation IS the value. This means step automation overrides the base, and
LFO modulates around the override when both are active on the same target.

## 5. Changes

### Change 1 — `presetMorphEngine.c`: step-automation override layer

**What**: add a per-slot step-automation Morph override, analogous to the
existing `morph_lfo_contributions` layer.

**Where**: `Core/Bank/Scene/Preset/presetMorphEngine.c`, after the existing
`morph_lfo_contributions` declaration (line 29).

**Add static data** (after line 29):

```c
/*
 * Per-slot step-automation Morph override.
 *
 * Inputs: seq_applySceneAutomation() sets an override amount for one slot;
 * seq_restoreAllSceneAutomation() clears all overrides. Output: when active,
 * the override replaces the retained scene_settings_t::voice_morph_amount[]
 * as the Morph worker's base value. LFO contributions, when present, are
 * resolved around the override rather than the retained amount.
 *
 * This is not an LFO contribution — it is a base replacement. The LFO
 * contribution table uses additive deviation from a center; step automation
 * supplies the center itself. When the override is inactive, the retained
 * Scene value is the center as before.
 */
static struct {
    uint8_t active;
    uint8_t amount;
} morph_step_override[INSTRUMENT_SLOT_COUNT];
```

**Modify `presetMorph_init()`** (currently lines 207–230): add initialisation
of `morph_step_override[]` inside the existing `for (slot)` loop:

```c
/* Inside the existing slot loop, after pass_amount init (line 220): */
    morph_step_override[slot].active = 0u;
    morph_step_override[slot].amount = 0u;
```

**Modify `presetMorph_snapshotPassAmounts()`** (lines 158–174): when the step
override is active for a slot, use the override amount instead of the
retained Scene amount.

Current (line 172–173):
```c
    morph_worker.pass_amount[slot] =
        scene ? scene->settings.voice_morph_amount[slot] : 0u;
```

New:
```c
    /*
     * Use step-automation override as the Morph base when active.
     *
     * Inputs: morph_step_override[slot] set by step automation, or the
     * retained Scene amount otherwise. Output: pass_amount[] captures the
     * effective base for this slot's bounded interpolation pass.
     */
    if (morph_step_override[slot].active)
        morph_worker.pass_amount[slot] = morph_step_override[slot].amount;
    else
        morph_worker.pass_amount[slot] =
            scene ? scene->settings.voice_morph_amount[slot] : 0u;
```

**Modify `presetMorph_resolveLfoAmount()`** (line 141): when the step
override is active, use the override as the LFO base instead of the retained
amount.

Current (line 141):
```c
    base = scene->settings.voice_morph_amount[slot];
```

New:
```c
    /*
     * LFO base: step-automation override when active, retained otherwise.
     *
     * Input: morph_step_override[slot] or scene_settings_t. Output: LFO
     * contributions deviate from whichever base is effective, so step
     * automation and LFO compose correctly — automation sets the center,
     * LFO wobbles around it.
     */
    base = morph_step_override[slot].active
        ? morph_step_override[slot].amount
        : scene->settings.voice_morph_amount[slot];
```

**Modify `presetMorph_prioritizeVoice()`** (line 320): same override
check for the priority snapshot.

Current (line 320):
```c
    morph_worker.pass_amount[slot] = scene->settings.voice_morph_amount[slot];
```

New:
```c
    morph_worker.pass_amount[slot] = morph_step_override[slot].active
        ? morph_step_override[slot].amount
        : scene->settings.voice_morph_amount[slot];
```

**Modify `presetMorph_applyVoiceNow()`**: use the step-automation override
when a trigger-time synchronous slot apply is required. This keeps the
deferred worker, priority snapshot, and synchronous Scene-switch path on the
same effective Morph base.

**Add `presetMorph_getEffectiveVoiceAmount()`**: expose a read-only effective
base accessor to InstrumentManager so LFO shaping is centered on the active
step value when Morph step automation and LFO modulation overlap.

**Add new functions** (after `presetMorph_clearLfoSource()`, line 604):

```c
void presetMorph_setStepAutomationOverride(uint8_t scene_index,
                                           uint8_t slot, uint8_t amount)
{
    /*
     * Set one voice's step-automation Morph override and queue a morph pass.
     *
     * Inputs: active Scene index, zero-based instrument slot, and 0..255
     * Morph amount from step automation. Output: the override replaces the
     * retained Scene amount as the Morph worker's base for this slot. The
     * worker is queued so the runtime interpolation image updates within the
     * bounded foreground budget. SceneData is not touched; PERF mirrors are
     * the caller's responsibility.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT || !scene_getConst(scene_index))
        return;
    morph_step_override[slot].active = 1u;
    morph_step_override[slot].amount = amount;
    presetMorph_requestVoice(scene_index, slot);
}

void presetMorph_clearAllStepAutomationOverrides(uint8_t scene_index)
{
    uint8_t slot;
    uint8_t any = 0u;

    /*
     * Clear all step-automation Morph overrides and queue restore.
     *
     * Inputs: active Scene index. Output: every slot's override is cleared.
     * Slots that were overridden are queued so the Morph worker reverts to
     * the retained Scene base amount. Called at transport stop/restart.
     */
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        if (morph_step_override[slot].active) {
            morph_step_override[slot].active = 0u;
            morph_step_override[slot].amount = 0u;
            any = 1u;
        }
    }
    if (any && scene_getConst(scene_index))
        presetMorph_rebuildScene(scene_index);
}
```

### Change 2 — `presetMorphEngine.h`: declare new functions

**Where**: `Core/Bank/Scene/Preset/presetMorphEngine.h`, before the `#endif`
(line 89).

**Add**:

```c
/*
 * Set/clear step-automation Morph override for one voice.
 *
 * Step automation replaces the retained Morph base with a transient value;
 * LFO contributions modulate around it. On transport stop, clearing all
 * overrides reverts every slot to the retained Scene amount.
 */
void presetMorph_setStepAutomationOverride(uint8_t scene_index,
                                           uint8_t slot, uint8_t amount);
void presetMorph_clearAllStepAutomationOverrides(uint8_t scene_index);
```

### Change 3 — `InstrumentManager.c`: step-automation override for slot-6 track-7 decay

**What**: add a step-automation runtime override for the generated track-7
decay parameter, following the same pattern as the existing LFO override.

**Where**: `Core/DSP/Instruments/InstrumentManager.c`, after the existing
LFO override statics (line 123).

**Add static data**:

```c
/*
 * Step-automation runtime override for the generated slot-6 track-7 decay.
 *
 * Inputs: seq_applySceneAutomation() sets the value; transport stop clears
 * it. Output: when active, the trigger path uses this value instead of the
 * retained kit_settings_t field or the LFO override. Priority order at
 * trigger time: step automation > LFO > retained.
 *
 * Step automation wins over LFO because step automation defines per-step
 * target values that are more specific than the continuous LFO deviation.
 * If both are active, the LFO's contribution is suppressed for this
 * parameter until the step automation overlay is cleared.
 */
static uint8_t slot6_track7_decay_step_active;
static uint8_t slot6_track7_decay_step_value;
```

**Modify trigger path** (lines 1617–1620): add step-automation check before
LFO check.

Current:
```c
    value = alternate
        ? (slot6_track7_decay_lfo_active
              ? slot6_track7_decay_lfo_value
              : scene->kit.settings.slot6_track7_amp_envelope_decay)
        : slot_state->parameter_images.morph_interpolation[base_index];
```

New:
```c
    /*
     * Priority cascade for the generated track-7 decay: step automation
     * override, then LFO override, then retained kit setting.
     */
    value = alternate
        ? (slot6_track7_decay_step_active
              ? slot6_track7_decay_step_value
              : (slot6_track7_decay_lfo_active
                    ? slot6_track7_decay_lfo_value
                    : scene->kit.settings.slot6_track7_amp_envelope_decay))
        : slot_state->parameter_images.morph_interpolation[base_index];
```

**Add new functions** (after `instrumentManager_updateLfoSceneDestination`,
near line 2341):

```c
void instrumentManager_setSlot6Track7StepDecayOverride(uint8_t value)
{
    /*
     * Set step-automation runtime override for the generated track-7 decay.
     *
     * Inputs: 0..127 value from step automation. Output: the override is
     * active until cleared at transport stop. Kit settings are not touched.
     */
    if (value > 127u)
        value = 127u;
    slot6_track7_decay_step_active = 1u;
    slot6_track7_decay_step_value = value;
}

void instrumentManager_clearSlot6Track7StepDecayOverride(void)
{
    /*
     * Clear step-automation runtime override for the generated track-7 decay.
     *
     * Output: the trigger path falls back to LFO override or retained kit
     * setting. Called at transport stop/restart.
     */
    slot6_track7_decay_step_active = 0u;
}
```

**Modify `instrumentManager_init()`** or equivalent initialisation: ensure
`slot6_track7_decay_step_active = 0u` at boot. If these are file-scope
statics in BSS, they are already zero-initialised; add an explicit init only
if the existing LFO overrides are explicitly initialised.

### Change 4 — `InstrumentManager.h`: declare new functions

**Where**: `Core/DSP/Instruments/InstrumentManager.h`, near the existing
Scene-target-related declarations.

**Add**:

```c
/*
 * Step-automation runtime override for the generated slot-6 track-7 decay.
 * Set by step automation; cleared at transport stop.
 */
void instrumentManager_setSlot6Track7StepDecayOverride(uint8_t value);
void instrumentManager_clearSlot6Track7StepDecayOverride(void);
```

### Change 5 — `presetManager.c`: runtime-only audio-out apply

**What**: add a function that writes one voice's audio routing to the mixer
runtime without touching SceneData. The existing
`preset_applyKitAudioRouting()` reads from SceneData (suitable for restore);
this new function takes a value parameter (suitable for automation overlay).

**Where**: `Core/Bank/Scene/Preset/presetManager.c`, after
`preset_applyKitAudioRouting()` (line 1013).

**Add**:

```c
void preset_applyVoiceAudioOutRuntime(uint8_t slot, uint8_t route)
{
    /*
     * Apply one voice's audio routing to the mixer runtime without retain.
     *
     * Inputs: zero-based instrument slot and route in the mixer enum domain.
     * Output: mixer_audioRouting[slot] is updated for the active Scene. No
     * SceneData write, no AutoSave dirty mark, no bank-clean invalidation.
     * Callers: step-automation overlay for AUDIO_OUT targets.
     * Restore affiliate: preset_applyKitAudioRouting() reads the retained
     * SceneData value and applies it, so the caller's restore path uses that
     * function instead of this one.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (route > MIXER_ROUTING_DAC2_R)
        route = MIXER_ROUTING_DAC1_STEREO;
    mixer_audioRouting[slot] = route;
}
```

### Change 6 — `presetManager.h`: declare new function

**Where**: `Core/Bank/Scene/Preset/presetManager.h`, near
`preset_applyKitAudioRouting()` (line 384).

**Add**:

```c
/*
 * Apply one voice's audio routing to the mixer without retaining in
 * SceneData. Used by step-automation overlay; restore uses
 * preset_applyKitAudioRouting() which reads the retained value.
 */
void preset_applyVoiceAudioOutRuntime(uint8_t slot, uint8_t route);
```

### Change 7 — `sequencer.c`: Scene-automation dirty bitmap, overlay apply, and restore

This is the central change. Three additions to `sequencer.c`:

#### 7A — Add Scene-automation dirty bitmap

**Where**: after `seq_automation_dirty[]` declaration (near line 180).

**Add**:

```c
/*
 * Scene-target step-automation dirty bitmap.
 *
 * Each bit corresponds to one Scene mod target index (0..19 in the current
 * table; up to 31 supported by the uint32_t). A set bit means the
 * sequencer's step automation has applied an overlay for that target and
 * the DSP runtime holds a transient value that must be restored on transport
 * stop.
 *
 * The bitmap is structurally parallel to seq_automation_dirty[] for voice
 * parameters: seq_applySceneAutomation() sets bits,
 * seq_restoreAllSceneAutomation() reads and restores,
 * seq_clearSceneAutomationDirty() zeros the word.
 */
static uint32_t seq_scene_automation_dirty;
```

#### 7B — Replace `seq_applySceneAutomation()` body

**Where**: `seq_applySceneAutomation()` (lines 704–737).

**Replace entire function body** with runtime-only dispatch:

```c
static uint8_t seq_applySceneAutomation(uint16_t target, uint8_t value)
{
    const scene_mod_target_descriptor_t *descriptor =
        sceneModTarget_descriptor(target);
    uint8_t index;

    /*
     * Apply one step-automation value as a runtime overlay, not a retain.
     *
     * Inputs: Scene target ID and seven-bit automation value. Output: the
     * DSP runtime is updated through kind-specific apply functions that do
     * not touch SceneData or mark AutoSave dirty. The corresponding bit in
     * seq_scene_automation_dirty is set so seq_restoreAllSceneAutomation()
     * can revert the overlay at transport stop.
     *
     * This replaces the previous implementation which called the retained
     * setters (preset_morphVoice, preset_setVoiceDecimationAll, etc.),
     * causing F2: AutoSave never converges during Scene-target playback.
     *
     * FX_SEND targets are intentionally skipped: no FX bus exists, so there
     * is no DSP runtime to overlay and nothing to restore.
     */
    if (!descriptor)
        return 0u;
    if (value > descriptor->max_value)
        value = (uint8_t)descriptor->max_value;
    if (!sceneModTarget_indexFromId(target, &index))
        return 0u;

    switch (descriptor->kind) {
    case SCENE_MOD_TARGET_KIND_VOICE_MORPH: {
        uint8_t morph = (value < 127u) ? (uint8_t)(value * 2u) : 255u;
        parameter_values[PAR_VOICE1_MORPH + descriptor->voice_slot] = morph;
        presetMorph_setStepAutomationOverride(
            scene_getActiveIndex(), descriptor->voice_slot, morph);
        break;
    }
    case SCENE_MOD_TARGET_KIND_DECIMATION_ALL:
        parameter_values[PAR_VOICE_DECIMATION_ALL] = value;
        preset_applyVoiceDecimationAllRuntime(value);
        break;
    case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
        instrumentManager_setSlot6Track7StepDecayOverride(value);
        break;
    case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
        preset_applyVoiceAudioOutRuntime(descriptor->voice_slot, value);
        break;
    case SCENE_MOD_TARGET_KIND_FX_SEND:
        return 1u;
    default:
        return 0u;
    }

    seq_scene_automation_dirty |= (1u << index);
    return 1u;
}
```

#### 7C — Add `seq_restoreAllSceneAutomation()`

**Where**: after `seq_restoreAllAutomation()` (line 245).

**Add**:

```c
static void seq_restoreAllSceneAutomation(void)
{
    uint32_t mask = seq_scene_automation_dirty;
    uint8_t scene_index = scene_getActiveIndex();
    const scene_t *scene = scene_getConst(scene_index);

    /*
     * Restore all Scene-target automation overlays to retained values.
     *
     * Inputs: seq_scene_automation_dirty bitmap and the active Scene's
     * retained settings. Output: each dirty target's DSP runtime is
     * restored from SceneData/kit_settings_t. The bitmap is left intact
     * for the caller to clear (same pattern as seq_restoreAllAutomation).
     *
     * Morph overrides are cleared via presetMorph, which queues a full
     * Scene rebuild from retained amounts. Decimation and audio-out are
     * restored through their existing retained-value apply functions.
     * Slot-6 track-7 decay override is cleared so the trigger path falls
     * back to the retained kit setting. FX_SEND bits are harmlessly
     * cleared without action (no DSP runtime exists).
     *
     * The unconditional clear calls (presetMorph, slot6 decay) are safe
     * even when their respective bits are not set: clearing an inactive
     * override is a no-op. This avoids walking the bitmap per-kind and
     * keeps the function simple.
     */
    if (!scene || !mask)
        return;

    presetMorph_clearAllStepAutomationOverrides(scene_index);
    instrumentManager_clearSlot6Track7StepDecayOverride();

    while (mask) {
        uint8_t index = (uint8_t)__builtin_ctz(mask);
        uint16_t id = sceneModTarget_idFromIndex(index);
        const scene_mod_target_descriptor_t *descriptor =
            sceneModTarget_descriptor(id);

        if (descriptor) {
            switch (descriptor->kind) {
            case SCENE_MOD_TARGET_KIND_VOICE_MORPH:
                parameter_values[PAR_VOICE1_MORPH + descriptor->voice_slot] =
                    scene_getVoiceMorphAmount(scene_index,
                                              descriptor->voice_slot);
                break;
            case SCENE_MOD_TARGET_KIND_DECIMATION_ALL:
                parameter_values[PAR_VOICE_DECIMATION_ALL] =
                    scene->settings.voice_decimation_all;
                preset_applyVoiceDecimationAllRuntime(
                    scene->settings.voice_decimation_all);
                break;
            case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
                (void)preset_applyKitAudioRouting(scene_index,
                                                  descriptor->voice_slot);
                break;
            case SCENE_MOD_TARGET_KIND_FX_SEND:
            case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
            default:
                break;
            }
        }
        mask &= (mask - 1u);
    }
}
```

#### 7D — Extend `seq_clearAutomationDirty()` and `seq_setStepIndexToStart()`

**Modify `seq_clearAutomationDirty()`** (lines 186–192): add Scene bitmap
clear.

Current:
```c
static void seq_clearAutomationDirty(void)
{
    uint8_t slot;

    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        seq_automation_dirty[slot] = 0u;
}
```

New:
```c
static void seq_clearAutomationDirty(void)
{
    uint8_t slot;

    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        seq_automation_dirty[slot] = 0u;
    seq_scene_automation_dirty = 0u;
}
```

**Modify `seq_setStepIndexToStart()`** (lines 1515–1543): add Scene
automation restore before the existing voice automation restore.

Current (lines 1536–1537):
```c
    seq_restoreAllAutomation();
    seq_clearAutomationDirty();
```

New:
```c
    seq_restoreAllSceneAutomation();
    seq_restoreAllAutomation();
    seq_clearAutomationDirty();
```

Scene restore runs first because it is the broader overlay; voice restore
then cleans up per-instrument runtime images. The order is not load-bearing
— they operate on disjoint state — but matching the outer-to-inner
nesting reads better.

### Change 8 — `sequencer.c`: include `presetMorphEngine.h`

**Where**: includes block (line 63, after `#include "presetManager.h"`).

**Add**:

```c
#include "presetMorphEngine.h"
```

Required for `presetMorph_setStepAutomationOverride()` and
`presetMorph_clearAllStepAutomationOverrides()`.

---

## 6. Interaction model

### Step automation + LFO on the same Scene target

When both are active simultaneously:

| Target kind | Interaction |
|---|---|
| VOICE_MORPH | Step automation overrides the Morph base. LFO contributions are resolved around the override, not the retained value. The morph worker composes them naturally: `presetMorph_resolveLfoAmount()` reads the override as `base`. |
| DECIMATION_ALL | Last writer wins per foreground frame. Step automation writes at step boundaries; LFO writes every block. In practice the LFO continuously overwrites the step value. This matches the current behavior (both write through `preset_applyVoiceDecimationAllRuntime()`). |
| SLOT6_TRACK7_AMP_DECAY | Step automation override has priority over LFO override at trigger time (Change 3 cascade). Per-step decay values take precedence over continuous LFO modulation. |
| AUDIO_OUT | LFO cannot target AUDIO_OUT (no `SCENE_MOD_TARGET_USE_LFO` flag). No interaction. |
| FX_SEND | LFO cannot target FX_SEND. No interaction. |

### Scene switch during playback

`seq_selectActivePattern()` changes `seq_activePattern` immediately without
calling `seq_setStepIndexToStart()`. The Scene automation dirty bits from the
old Scene would persist until the next transport stop/restart or bar-boundary
Scene switch (which calls `seq_setStepIndexToStart()`).

This matches the existing voice-automation behavior: voice dirty bits also
persist across an immediate Scene switch and are restored at the next
transport boundary. No additional handling is required for consistency.

The bar-boundary Scene switch path (sequencer.c line 872) calls
`seq_setStepIndexToStart()`, which triggers both voice and Scene automation
restore. This is the primary Scene-switch path during playback.

## 7. Files changed

| File | Changes |
|---|---|
| `Core/Bank/Scene/Preset/presetMorphEngine.c` | Add `morph_step_override[]`; modify snapshot/resolve/priority/synchronous apply to use override; add effective-base, set, and clear functions |
| `Core/Bank/Scene/Preset/presetMorphEngine.h` | Declare effective-base, set, and clear step-automation Morph APIs |
| `Core/DSP/Instruments/InstrumentManager.c` | Add `slot6_track7_decay_step_active/value`; modify trigger cascade; add set/clear functions |
| `Core/DSP/Instruments/InstrumentManager.h` | Declare `instrumentManager_setSlot6Track7StepDecayOverride()`, `instrumentManager_clearSlot6Track7StepDecayOverride()` |
| `Core/Bank/Scene/Preset/presetManager.c` | Add `preset_applyVoiceAudioOutRuntime()` |
| `Core/Bank/Scene/Preset/presetManager.h` | Declare `preset_applyVoiceAudioOutRuntime()` |
| `Core/Sequencer/sequencer.c` | Add `seq_scene_automation_dirty`; replace `seq_applySceneAutomation()` body; add `seq_restoreAllSceneAutomation()`; extend `seq_clearAutomationDirty()` and `seq_setStepIndexToStart()`; add `presetMorphEngine.h` include |

## 8. Change map

```
presetMorphEngine.c  ←── Change 1 (override layer, snapshot, resolve, set/clear)
presetMorphEngine.h  ←── Change 2 (effective-base, set/clear declarations)
InstrumentManager.c  ←── Change 3 (slot6 decay override, trigger cascade, set/clear)
InstrumentManager.h  ←── Change 4 (declarations)
presetManager.c      ←── Change 5 (audio out runtime apply)
presetManager.h      ←── Change 6 (declaration)
sequencer.c          ←── Change 7 (bitmap, overlay apply, restore, clear, step-to-start)
                     ←── Change 8 (include)
```

## 9. Implementation order

1. Changes 1–2 (morph engine override layer) — no external callers yet.
2. Changes 3–4 (slot6 decay override) — no external callers yet.
3. Changes 5–6 (audio out runtime) — no external callers yet.
4. Changes 7–8 (sequencer integration) — wires everything together.

Build after step 4. All intermediate steps produce no behavioral change
because the new functions have no callers until Change 7 connects them.

## 10. Test plan

### Verify fix: AutoSave quiesces during Scene-target playback

1. Load a Scene with step automation on at least voice Morph (`1vm`) and
   audio out (`1ou`) targets.
2. Start transport. Let it run for 2 minutes.
3. Stop. Wait 30 s. Power off. Copy card.
4. Decode trace: after the initial convergence burst, the quiet tail should
   show zero scalar/Kit charge in `H` groups. No `D` records with Scene
   parameter indices should appear during playback.

### Verify runtime: automation values still audible

1. Same Scene as above. Verify that the Morph gesture is audible on each
   step and that audio routing changes are heard (if the Pattern has
   per-step route automation).
2. Stop/restart 10 times. Verify step-0 automation fires correctly each
   time (same as T3).

### Verify restore: retained values intact after stop

1. Note the user-set Morph, decimation, and audio-out values before
   playback.
2. Play for several bars with Scene-target automation. Stop.
3. Check PERF page: `1vm`..`6vm` and `srt` should show the pre-playback
   user-set values, not the last automation value.
4. Power off, reboot. Verify the same values are retained.

### Verify interaction: LFO + step automation on Morph

1. Set an LFO to target `1vm` (voice 1 Morph). Set step automation to also
   target `1vm` with a per-step value.
2. Play. The Morph gesture should reflect the step value modulated by the
   LFO wobble.
3. Stop. The Morph value should revert to the user-set base.

## 11. Implementation notes and verification

### 2026-09-25 — source implementation complete

- Implemented the runtime-only Scene-target overlay path in the seven files
  listed above. Retained Scene/Kit setters are no longer called by
  `seq_applySceneAutomation()`; Morph, decimation, generated track-7 decay,
  and audio routing use dedicated runtime owners. FX-send remains a deliberate
  no-op until its Phase 5 bus exists.
- Added one 32-bit Scene-target dirty bitmap (+4 B normal SRAM1), six two-byte
  Morph override records, and two bytes for the generated-decay overlay. No
  persisted record, Pattern region, DTCM, or delay-line allocation changed.
- Transport/Pattern reset now restores Scene-target overlays before clearing
  both Scene and voice automation tracking. Morph restore queues a retained
  Scene rebuild; decimation and audio routing restore directly from retained
  Scene settings; generated decay falls back through its existing LFO/Kit
  priority chain.
- Added the effective Morph-base accessor so LFO shaping composes around a
  step-automation value instead of briefly reverting to the retained base.
  Synchronous trigger-time Morph application also uses the overlay.
- Clean source/build/image verification passed with:
  `arm-none-eabi-size`: `text=455804`, `data=416`, `bss=291820`; packaged
  `build/LXRV2_lxr02.img`: 456,236 bytes (456,220-byte firmware payload plus
  the 16-byte image header). Hardware verification is pending.
