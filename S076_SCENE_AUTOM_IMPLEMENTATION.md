# S076 Scene Parameter Automation Lock — Implementation Schedule

Reference: `S076_SCENE_PARAM_AUTOM_LOCK_BUG.md` (root directory).

This document specifies every code change required to implement Rules A and B.
Each change is cited by file, line, and add/modify/remove. Every change also
carries a comment block suitable for insertion alongside the code.

---

## Change 1 — New single-slot Morph override clear helper

**File:** `Core/Bank/Scene/Preset/presetMorphEngine.c`  
**Location:** After `presetMorph_clearAllStepAutomationOverrides()` (after line 864)  
**Action:** ADD new function

```c
/*
 * Clear the step-automation Morph base for one voice (S076 Rule A).
 *
 * What: deactivates morph_step_override[slot] so the effective Morph base
 * falls back to the retained Scene amount. Unlike the clearAll helper this
 * does NOT queue a rebuild; the caller is a retained-value setter that will
 * queue its own rebuild or retained-base commit immediately after.
 * Why: a non-automation write (menu edit, MIDI CC1, copy/clear, type change)
 * must supersede any active step override for that slot so the user's edit
 * is audible and visible. The full transport-boundary clear
 * (presetMorph_clearAllStepAutomationOverrides) continues to own bulk
 * restore with rebuild.
 * Inputs: zero-based instrument slot 0..INSTRUMENT_SLOT_COUNT-1.
 * Output: the slot's morph_step_override.active is set to 0. Out-of-range
 * slot is a no-op.
 * Callers: preset_morphVoiceScene(), preset_morphScene().
 * Affiliates: presetMorph_setStepAutomationOverride() (the setter),
 * presetMorph_clearAllStepAutomationOverrides() (the transport clear),
 * presetMorph_effectiveVoiceBase() (the consumer that checks .active),
 * presetMorph_getEffectiveVoiceAmount() (the display bridge).
 */
void presetMorph_clearStepAutomationOverride(uint8_t slot)
{
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    morph_step_override[slot].active = 0u;
}
```

---

## Change 2 — Declare the new helper in the header

**File:** `Core/Bank/Scene/Preset/presetMorphEngine.h`  
**Location:** After the `presetMorph_clearAllStepAutomationOverrides` declaration (after line 164)  
**Action:** ADD declaration

```c
/*
 * Clear the step-automation Morph base for one voice (S076 Rule A).
 *
 * What: deactivates the per-slot Morph step override without queueing a
 * rebuild. The caller owns the subsequent retained-value commit or rebuild
 * request. Input: zero-based instrument slot. Output: override deactivated;
 * out-of-range is a no-op. Callers: preset_morphVoiceScene(),
 * preset_morphScene(). Affiliate: presetMorph_setStepAutomationOverride().
 */
void presetMorph_clearStepAutomationOverride(uint8_t slot);
```

---

## Change 3 — FX Send: clear override on retained amount write

**File:** `Core/Bank/Scene/Preset/presetManager.c`  
**Location:** Inside `preset_setVoiceFxSendAmount()`, after line 1264 (`scene_setVoiceFxSendAmount(scene_index, slot, amount);`)  
**Action:** ADD 7 lines before `return 1u;`

```c
    /*
     * S076 Rule A: a non-automation retained write supersedes any active
     * step override for this slot. The mixer reads the effective getter
     * each block; once the override is inactive the retained value takes
     * over on the next block. No rebuild is needed because FX send has no
     * interpolation worker — the mixer reads the value directly.
     * Input: active Scene guard. Output: fx_send_step_override[slot].active = 0.
     * Callers (upstream): menu_cellCommitValue() Scene-setting branch,
     * clearOps.c `clear send`. Affiliate: preset_setFxSendStepOverride()
     * (the automation setter), preset_getEffectiveFxSendAmount() (the
     * mixer consumer), preset_getFxSendDisplayAmount() (the display
     * bridge).
     */
    if (scene_index == scene_getActiveIndex())
        fx_send_step_override[slot].active = 0u;
```

The function body after modification (lines 1248–1267):

```c
uint8_t preset_setVoiceFxSendAmount(uint8_t scene_index, uint8_t slot,
                                    uint8_t amount)
{
    /* ... existing comment block ... */
    if (!scene_get(scene_index) || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (amount > 127u)
        amount = 127u;
    scene_setVoiceFxSendAmount(scene_index, slot, amount);
    /*
     * S076 Rule A: a non-automation retained write supersedes any active
     * step override for this slot. ...
     */
    if (scene_index == scene_getActiveIndex())
        fx_send_step_override[slot].active = 0u;
    return 1u;
}
```

---

## Change 4 — FX Send Morph: clear override on retained morph-endpoint write

**File:** `Core/Bank/Scene/Preset/presetManager.c`  
**Location:** Inside `preset_setVoiceFxSendMorph()`, after line 1283 (`scene_setVoiceFxSendMorph(scene_index, slot, amount);`)  
**Action:** ADD 7 lines before `return 1u;`

```c
    /*
     * S076 Rule A: editing either endpoint of a morphable FX send releases
     * the step override so the interpolated result from retained Normal
     * and Morph endpoints takes effect. The mixer consumer
     * (preset_getEffectiveFxSendAmount) interpolates using the resolved
     * voice Morph amount when no step override is active.
     * Input: active Scene guard. Output: fx_send_step_override[slot].active = 0.
     * Callers (upstream): menu_cellCommitValue() Scene-setting Morph
     * branch, clearOps.c `clear send`. Affiliate:
     * preset_setVoiceFxSendAmount() (the Normal-endpoint twin, also
     * clears — Change 3).
     */
    if (scene_index == scene_getActiveIndex())
        fx_send_step_override[slot].active = 0u;
```

---

## Change 5 — Audio Out: clear override on retained route write

**File:** `Core/Bank/Scene/Preset/presetManager.c`  
**Location:** Inside `preset_setVoiceAudioOut()`, after line 1244 (`scene_setVoiceAudioOut(scene_index, slot, route);`)  
**Action:** ADD 7 lines before `return preset_applyKitAudioRouting(scene_index, slot);`

```c
    /*
     * S076 Rule A: a non-automation retained write supersedes any active
     * step override for this slot. preset_applyKitAudioRouting() (called
     * next) writes the retained route to the DSP mixer, and once the
     * override is inactive the effective getter returns the retained route
     * for display.
     * Input: active Scene guard. Output: audio_out_step_override[slot].active = 0.
     * Callers (upstream): menu_cellCommitValue() Scene-setting branch.
     * Affiliate: preset_setAudioOutStepOverride() (the automation setter),
     * preset_getEffectiveAudioOut() (the display bridge),
     * preset_applyVoiceAudioOutRuntime() (the DSP write).
     */
    if (scene_index == scene_getActiveIndex())
        audio_out_step_override[slot].active = 0u;
```

The function body after modification (lines 1229–1246):

```c
uint8_t preset_setVoiceAudioOut(uint8_t scene_index, uint8_t slot,
                                uint8_t route)
{
    /* ... existing comment block ... */
    if (!scene_get(scene_index) || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (route > MIXER_ROUTING_DAC2_R)
        route = MIXER_ROUTING_DAC1_STEREO;
    scene_setVoiceAudioOut(scene_index, slot, route);
    /*
     * S076 Rule A: a non-automation retained write supersedes ...
     */
    if (scene_index == scene_getActiveIndex())
        audio_out_step_override[slot].active = 0u;
    return preset_applyKitAudioRouting(scene_index, slot);
}
```

---

## Change 6 — Voice Morph (single slot): clear override before morph request

**File:** `Core/Bank/Scene/Preset/presetManager.c`  
**Location:** Inside `preset_morphVoiceScene()`, after the `preset_ensureMorphInitialized()` call and `scene_setVoiceMorphAmount()` call (after line 3220), before the `scene_index == scene_getActiveIndex()` branch at line 3221  
**Action:** ADD 8 lines between lines 3220 and 3221

```c
    /*
     * S076 Rule A: a non-automation Morph write supersedes any active step
     * override for this slot. The clear precedes the active-Scene rebuild
     * request so presetMorph_effectiveVoiceBase() returns the newly retained
     * amount when the worker processes the queued slot.
     * Input: active Scene guard. Output: morph_step_override[slot].active = 0.
     * Callers (upstream): preset_morphVoice() (PERF, MIDI CC1 per-voice),
     * menu_cellCommitValue() Morph cells. Affiliate:
     * presetMorph_setStepAutomationOverride() (the automation setter),
     * presetMorph_effectiveVoiceBase() (the worker consumer).
     */
    if (scene_index == scene_getActiveIndex())
        presetMorph_clearStepAutomationOverride(slot);
```

The function body after modification (lines 3207–3225):

```c
void preset_morphVoiceScene(uint8_t scene_index, uint8_t slot, uint8_t morph)
{
    /* ... existing comment block ... */
    if (slot >= INSTRUMENT_SLOT_COUNT || !scene_get(scene_index))
        return;
    preset_ensureMorphInitialized();
    scene_setVoiceMorphAmount(scene_index, slot, morph);
    /*
     * S076 Rule A: a non-automation Morph write supersedes ...
     */
    if (scene_index == scene_getActiveIndex())
        presetMorph_clearStepAutomationOverride(slot);
    if (scene_index == scene_getActiveIndex()) {
        parameter_values[PAR_VOICE1_MORPH + slot] = morph;
        presetMorph_requestVoice(scene_index, slot);
    }
}
```

Note: the two `scene_index == scene_getActiveIndex()` guards can be folded into
one branch at implementation time; the schedule shows them separate for clarity.

---

## Change 7 — Voice Morph (bulk set): clear all slot overrides

**File:** `Core/Bank/Scene/Preset/presetManager.c`  
**Location:** Inside `preset_morphScene()`, after `scene_setEffectMorphAmount(scene_index, morph);` (line 3185), before the active-Scene branch at line 3186  
**Action:** ADD 11 lines between lines 3185 and 3186

```c
    /*
     * S076 Rule A: bulk Morph set from PERF / global MIDI CC1 supersedes
     * every active step-automation Morph override. Each slot is cleared
     * individually through the single-slot helper. The clear precedes the
     * active-Scene mirror sync and rebuild so the worker uses the newly
     * retained amounts.
     * Input: active Scene guard. Output: all morph_step_override[].active = 0.
     * Callers (upstream): preset_morph() (PERF global Morph, MIDI CC1
     * global). Affiliate: preset_morphVoiceScene() (the per-slot twin —
     * Change 6).
     */
    if (scene_index == scene_getActiveIndex()) {
        for (uint8_t s = 0u; s < INSTRUMENT_SLOT_COUNT; s++)
            presetMorph_clearStepAutomationOverride(s);
    }
```

The function body after modification (lines 3163–3190):

```c
void preset_morphScene(uint8_t scene_index, uint8_t morph)
{
    scene_t *scene;

    /* ... existing comment block ... */
    preset_ensureMorphInitialized();
    scene = scene_get(scene_index);
    if (!scene)
        return;
    scene_setMorphAmount(scene_index, morph);
    scene_setAllVoiceMorphAmounts(scene_index, morph);
    scene_setEffectMorphAmount(scene_index, morph);
    /*
     * S076 Rule A: bulk Morph set supersedes every step override ...
     */
    if (scene_index == scene_getActiveIndex()) {
        for (uint8_t s = 0u; s < INSTRUMENT_SLOT_COUNT; s++)
            presetMorph_clearStepAutomationOverride(s);
    }
    if (scene_index == scene_getActiveIndex()) {
        preset_syncSceneMorphMirrors(scene);
        presetMorph_requestAll(scene_index);
    }
}
```

Note: as with Change 6, the two active-Scene branches can be folded into one at
implementation time.

---

## Change 8 — Slot6 Track7 Decay: clear override on retained write

**File:** `Core/Bank/Scene/Preset/presetManager.c`  
**Location:** Inside `preset_setSlot6Track7AmpEnvelopeDecay()`, after the `scene_setSlot6Track7AmpEnvelopeDecay()` / `scene_setSlot6Track7MorphAmpEnvelopeDecay()` calls (after lines 1332–1334), before `return 1u;` at line 1335  
**Action:** ADD 11 lines between line 1334 and line 1335

```c
    /*
     * S076 Rule A: a non-automation retained write supersedes any active
     * step decay override. The generated track-7 trigger path reads the
     * step override first when active; clearing it makes the next trigger
     * use the retained Kit value (or the LFO layer, if one is active).
     * Input: active Scene guard. Output:
     * instrumentManager_clearSlot6Track7StepDecayOverride() sets
     * slot6_track7_decay_step_active = 0.
     * Callers (upstream): menu_cellCommitValue() Kit setting branch,
     * InstrumentManager type-change normalization. Affiliate:
     * instrumentManager_setSlot6Track7StepDecayOverride() (the automation
     * setter), instrumentManager_getSlot6Track7StepDecay() (the trigger
     * consumer).
     */
    if (scene_index == scene_getActiveIndex())
        instrumentManager_clearSlot6Track7StepDecayOverride();
```

The function body after modification (lines 1309–1337):

```c
uint8_t preset_setSlot6Track7AmpEnvelopeDecay(uint8_t scene_index,
                                              instrument_image_select_t image,
                                              uint8_t value,
                                              uint8_t record_automation)
{
    (void)record_automation;
    /* ... existing comment block ... */
    if (!scene_get(scene_index))
        return 0u;
    if (value > 127u)
        value = 127u;
    if (image == INSTRUMENT_IMAGE_MORPH)
        scene_setSlot6Track7MorphAmpEnvelopeDecay(scene_index, value);
    else
        scene_setSlot6Track7AmpEnvelopeDecay(scene_index, value);
    /*
     * S076 Rule A: a non-automation retained write supersedes ...
     */
    if (scene_index == scene_getActiveIndex())
        instrumentManager_clearSlot6Track7StepDecayOverride();
    return 1u;
}
```

---

## Change 9 — Effect Morph: new single-purpose clear helper

**File:** `Core/DSP/Effects/EffectsManager.c`  
**Location:** After `effects_setMorphAutomation()` (after line 1187)  
**Action:** ADD new function

```c
/*
 * Clear the Pattern `fxm` Morph-base override (S076 Rule A).
 *
 * What: deactivates effects_automation.morph_override_valid so the Effect
 * service loop falls back to the retained Scene Effect Morph amount.
 * Why: a non-automation write (menu edit, MIDI CC1 global, copy/clear)
 * must supersede any active Pattern fxm override so the user's edit is
 * audible. The full transport-boundary clear (effects_automationReset)
 * continues to own bulk clear; this API clears only the morph override
 * without touching per-parameter Effect overlays or owner tracks.
 * Inputs: none. Output: morph_override_valid = 0; the morph_override
 * value is left stale because it is not read while invalid.
 * Callers: effects_setMorphAmountScene() (the active-Scene retained
 * write path). Affiliates: effects_setMorphAutomation() (the setter),
 * effects_automationReset() (the transport/Scene clear),
 * effects_service() service loop line 1567–1568 (the consumer).
 */
void effects_clearMorphAutomationOverride(void)
{
    effects_automation.morph_override_valid = 0u;
}
```

---

## Change 10 — Declare the new Effect Morph clear helper

**File:** `Core/DSP/Effects/EffectsManager.h`  
**Location:** After `effects_setMorphAutomation()` declaration (after line 411)  
**Action:** ADD declaration

```c
/*
 * Clear the Pattern `fxm` Morph-base override (S076 Rule A).
 *
 * What: deactivates the morph_override_valid flag without touching per-
 * parameter Effect overlays. Input: none. Output: the Effect service loop
 * falls back to retained Scene Effect Morph on the next block. Caller:
 * effects_setMorphAmountScene(). Affiliate: effects_setMorphAutomation().
 */
void effects_clearMorphAutomationOverride(void);
```

---

## Change 11 — Effect Morph retained setter: clear override for active Scene

**File:** `Core/DSP/Effects/EffectsManager.c`  
**Location:** Inside `effects_setMorphAmountScene()` (line 642), after `scene_setEffectMorphAmount(scene_index, amount);` (line 646), before the return at line 647  
**Action:** ADD 9 lines between lines 646 and 647

```c
    /*
     * S076 Rule A: a non-automation retained write supersedes the Pattern
     * `fxm` Morph override for the active Scene. The service loop reads
     * morph_override_valid every block; once cleared it uses the retained
     * Scene Effect Morph amount for all LFO and base resolution.
     * Input: active Scene guard against effects_state.scene_index (the
     * runtime's own active Scene, which tracks scene_getActiveIndex()).
     * Output: effects_automation.morph_override_valid = 0.
     * Callers (upstream): effects_setMorphAmount() fan-out, called by
     * menu_commitEffectMorphParam() and clearOps.c. Affiliate:
     * effects_setMorphAutomation() (the automation setter).
     */
    if (scene_index == effects_state.scene_index)
        effects_clearMorphAutomationOverride();
```

The function body after modification (lines 642–649):

```c
static uint8_t effects_setMorphAmountScene(uint8_t scene_index, uint8_t amount)
{
    uint8_t before = scene_getEffectMorphAmount(scene_index);

    scene_setEffectMorphAmount(scene_index, amount);
    /*
     * S076 Rule A: a non-automation retained write supersedes ...
     */
    if (scene_index == effects_state.scene_index)
        effects_clearMorphAutomationOverride();
    return (uint8_t)(scene_getEffectMorphAmount(scene_index) != before);
}
```

---

## Change 12 — New sequencer accessor: clear Scene automation dirty bitmap

**File:** `Core/Sequencer/sequencer.c`  
**Location:** After `seq_clearAutomationDirty()` (after line 273)  
**Action:** ADD new function

```c
/*
 * Clear the Scene-target automation dirty bitmap (S076 Rule B).
 *
 * What: zeroes seq_scene_automation_dirty. Why: the dirty bits reference
 * the previous Scene's mod-target table indices. After a Scene switch the
 * new Scene's retained values are the correct transport-restore baseline,
 * and old dirty bits that indexed into the previous Scene's target table
 * could restore wrong entries or alias into the new table. Clearing here
 * means a subsequent transport reset restores retained values for only
 * those targets that the new Scene's automation actually wrote.
 * Inputs: none. Output: seq_scene_automation_dirty = 0.
 * Caller: seq_selectActivePattern() and seq_alignActivePatternToScene()
 * on Scene change. Affiliates: seq_applySceneAutomation() (the setter),
 * seq_restoreAllSceneAutomation() (the transport-boundary consumer),
 * seq_clearAutomationDirty() (the boot/transport clear that zeroes both
 * voice and Scene bitmaps).
 */
void seq_clearSceneAutomationDirty(void)
{
    seq_scene_automation_dirty = 0u;
}
```

---

## Change 13 — Declare the new sequencer accessor

**File:** `Core/Sequencer/sequencer.h`  
**Location:** After `seq_automationHoldsParameter()` declaration (after line 176)  
**Action:** ADD declaration

```c
/*
 * Clear the Scene-target automation dirty bitmap (S076 Rule B).
 *
 * What: zeroes the bitmap that tracks which Scene mod targets were written
 * by step automation during the current transport pass. After a Scene
 * switch the previous Scene's dirty bits are stale and could alias into
 * the new Scene's target table on transport restore.
 * Input: none. Output: seq_scene_automation_dirty = 0.
 * Callers: seq_selectActivePattern(), seq_alignActivePatternToScene().
 * Affiliate: seq_applySceneAutomation().
 */
void seq_clearSceneAutomationDirty(void);
```

---

## Change 14 — Scene activation: clear all overrides in `preset_applySceneSettings()`

**File:** `Core/Bank/Scene/Preset/presetManager.c`  
**Location:** Inside `preset_applySceneSettings()`, after the Scene validity guard (line 1341–1342), before `preset_ensureMorphInitialized()` at line 1358  
**Action:** ADD 20 lines between lines 1342 and 1344 (between the guard and the existing comment)

```c
    /*
     * S076 Rule B: Scene activation clears every Scene-target step override
     * left by the previous Scene's automation.
     *
     * What: deactivates all five persistent override families before the
     * new Scene's mirrors, routing, and Morph are applied. Without this
     * clear the new Scene's presetMorph_rebuildScene() reads stale
     * morph_step_override values from the previous Scene, the mixer
     * continues using the previous Scene's FX send override, and audio
     * routing does not switch.
     * Why: main-pattern automation sets these overrides per step, but the
     * overrides carry no Scene identity. A Scene switch means every
     * override from the previous Scene is stale; the new Scene's retained
     * values are the correct baseline.
     * Inputs: none (this function already validated that scene_index is the
     * active Scene). Outputs: all override .active flags are 0.
     * Effect Morph (effects_automation.morph_override_valid) is already
     * cleared by effects_activateScene() → effects_automationReset(),
     * which runs in preset_startDrumsetApply() at the same Scene-switch
     * boundary before this function is called. No duplicate clear needed.
     * Callers (upstream of this function): preset_startDrumsetApply()
     * (Scene switch), Bank Load, boot. Affiliates:
     * seq_restoreAllSceneAutomation() (the transport-boundary clear path,
     * unchanged).
     */
    preset_clearAllFxSendStepOverrides();
    preset_clearAllAudioOutStepOverrides(scene_index);
    presetMorph_clearAllStepAutomationOverrides(scene_index);
    instrumentManager_clearSlot6Track7StepDecayOverride();
```

The function body after modification (lines 1338–1378):

```c
void preset_applySceneSettings(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);
    if (!scene || scene_index != scene_getActiveIndex())
        return;

    /*
     * S076 Rule B: Scene activation clears every Scene-target step override ...
     */
    preset_clearAllFxSendStepOverrides();
    preset_clearAllAudioOutStepOverrides(scene_index);
    presetMorph_clearAllStepAutomationOverrides(scene_index);
    instrumentManager_clearSlot6Track7StepDecayOverride();

    /*
     * Apply immediate Scene-wide settings that still have legacy mirrors.
     * ... (existing comment) ...
     */
    preset_ensureMorphInitialized();
    preset_syncSceneMorphMirrors(scene);
    /* ... remainder unchanged ... */
```

---

## Change 15 — Scene activation: clear Scene automation dirty bitmap

**File:** `Core/Sequencer/sequencer.c`  
**Location:** Inside `seq_selectActivePattern()`, after `voiceControl_noteOff(0xFF);` (line 661)  
**Action:** ADD 11 lines after line 661

```c
    /*
     * S076 Rule B: clear the Scene-target automation dirty bitmap.
     *
     * What: the dirty bits reference mod-target table indices from the
     * previous Scene. The new Scene's retained values are the correct
     * transport-restore baseline, and stale bits could restore wrong
     * entries or alias into the new Scene's table. Clearing here means a
     * subsequent transport reset only restores targets that the new
     * Scene's own automation wrote.
     * Affiliate: seq_restoreAllSceneAutomation() (transport-boundary
     * restore, uses this bitmap).
     */
    seq_clearSceneAutomationDirty();
```

---

## Change 16 — Bank Load alignment: clear Scene automation dirty bitmap

**File:** `Core/Sequencer/sequencer.c`  
**Location:** Inside `seq_alignActivePatternToScene()` — need to read current end of function  
**Action:** ADD same `seq_clearSceneAutomationDirty()` call at end of function body

This function serves the same role as `seq_selectActivePattern()` but for Bank
Load (no LED/MIDI/noteOff side effects). The dirty bitmap references the
previous Scene's mod-target indices and must be cleared here too.

The function body ends at line 700 (`}`), with
`seq_realignActivePatternToMasterClock();` at line 699. Insert after line 699,
before the closing brace:

```c
    /*
     * S076 Rule B: clear the Scene-target automation dirty bitmap.
     * Same rationale as seq_selectActivePattern() — see Change 15.
     */
    seq_clearSceneAutomationDirty();
```

---

## Summary Table

| # | File | Line | Action | Rule | What |
|---|------|------|--------|------|------|
| 1 | `presetMorphEngine.c` | after 864 | ADD function | A | `presetMorph_clearStepAutomationOverride()` single-slot helper |
| 2 | `presetMorphEngine.h` | after 164 | ADD declaration | A | Declare single-slot clear |
| 3 | `presetManager.c` | 1264→1265 | ADD 7 lines | A | Clear FX send override in `preset_setVoiceFxSendAmount()` |
| 4 | `presetManager.c` | 1283→1284 | ADD 7 lines | A | Clear FX send override in `preset_setVoiceFxSendMorph()` |
| 5 | `presetManager.c` | 1244→1245 | ADD 7 lines | A | Clear audio out override in `preset_setVoiceAudioOut()` |
| 6 | `presetManager.c` | 3220→3221 | ADD 8 lines | A | Clear morph override in `preset_morphVoiceScene()` |
| 7 | `presetManager.c` | 3185→3186 | ADD 11 lines | A | Clear all morph overrides in `preset_morphScene()` |
| 8 | `presetManager.c` | 1334→1335 | ADD 11 lines | A | Clear slot6 track7 decay override in `preset_setSlot6Track7AmpEnvelopeDecay()` |
| 9 | `EffectsManager.c` | after 1187 | ADD function | A | `effects_clearMorphAutomationOverride()` helper |
| 10 | `EffectsManager.h` | after 411 | ADD declaration | A | Declare Effect Morph clear |
| 11 | `EffectsManager.c` | 646→647 | ADD 9 lines | A | Clear morph override in `effects_setMorphAmountScene()` |
| 12 | `sequencer.c` | after 273 | ADD function | B | `seq_clearSceneAutomationDirty()` accessor |
| 13 | `sequencer.h` | after 176 | ADD declaration | B | Declare bitmap clear |
| 14 | `presetManager.c` | 1342→1344 | ADD 20 lines | B | Clear all overrides in `preset_applySceneSettings()` |
| 15 | `sequencer.c` | after 661 | ADD 11 lines | B | Clear dirty bitmap in `seq_selectActivePattern()` |
| 16 | `sequencer.c` | end of `seq_alignActivePatternToScene()` | ADD 4 lines | B | Clear dirty bitmap for Bank Load path |

---

## Files Modified (7 files)

| File | Changes |
|------|---------|
| `Core/Bank/Scene/Preset/presetMorphEngine.c` | +1 new function (Change 1) |
| `Core/Bank/Scene/Preset/presetMorphEngine.h` | +1 declaration (Change 2) |
| `Core/Bank/Scene/Preset/presetManager.c` | +6 inline additions (Changes 3, 4, 5, 6, 7, 8, 14) |
| `Core/DSP/Effects/EffectsManager.c` | +1 new function, +1 inline addition (Changes 9, 11) |
| `Core/DSP/Effects/EffectsManager.h` | +1 declaration (Change 10) |
| `Core/Sequencer/sequencer.c` | +1 new function, +2 inline additions (Changes 12, 15, 16) |
| `Core/Sequencer/sequencer.h` | +1 declaration (Change 13) |

No files removed. No existing lines modified or deleted — every change is an
addition. No new RAM (all changes flip existing .active flags). No wire-format,
AutoSave, or storage change.

---

## What is NOT changed

- **Voice descriptor automation** (`seq_automation_dirty[]`,
  `presetMorph_writeRuntimeBase()`, `seq_automationHoldsParameter()`,
  `seq_restoreAutomatedParameters()`): unchanged.
- **FX sequencer Effect parameter automation** (`effects_automation.active`,
  `effects_automationStepBegin/Flush`): unchanged.
- **`seq_restoreAllSceneAutomation()`**: unchanged; it remains the
  transport-boundary restore path.
- **`effects_activateScene()`**: unchanged; it already clears
  `morph_override_valid` via `effects_automationReset()`.
- **MIDI paths**: `midiParser_setMorphFromModWheel()` calls `preset_morph()` →
  `preset_morphScene()` (Change 7 covers it). `midiParser_setVoiceMorphFromModWheel()`
  calls `preset_morphVoice()` → `preset_morphVoiceScene()` (Change 6 covers it).
  No direct MIDI parser changes needed.
- **Menu paths**: `menu_commitEffectMorphParam()` calls
  `effects_setMorphAmount()` → `effects_setMorphAmountScene()` (Change 11
  covers it). `menu_cellCommitValue()` Scene-setting branch calls
  `preset_setVoiceAudioOut()`, `preset_setVoiceFxSendAmount()`,
  `preset_morphVoiceScene()` etc. (Changes 3–8 cover them). No direct Menu
  changes needed.

---

## Implementation Order

The changes have no ordering dependencies between them — each addition is
self-contained. However, for a clean build at each step:

1. **Headers first:** Changes 2, 10, 13 (declarations for new functions)
2. **New function bodies:** Changes 1, 9, 12
3. **Rule A inline additions:** Changes 3, 4, 5, 6, 7, 8, 11 (any order)
4. **Rule B inline additions:** Changes 14, 15, 16 (any order)

---

## Implementation Log

Running record of what actually landed, in order. Line references are the
pre-change figures from the schedule above unless noted.

### Step 1 — presetMorphEngine.c / .h (Changes 1, 2) — DONE

- Added presetMorph_clearStepAutomationOverride(uint8_t slot) after
  presetMorph_clearAllStepAutomationOverrides() (former line 864), with the
  full contract comment block adjacent in the .c file.
- Declared it in presetMorphEngine.h immediately after
  presetMorph_clearAllStepAutomationOverrides() (former line 164), with the
  matching contract comment block adjacent in the header. Placed before the
  trailing #endif.

### Step 2 — presetManager.c Rule A (Changes 3, 4, 5, 6, 7, 8) — DONE

Every insertion carries its contract comment block immediately above the
guard+clear pair. All clears are guarded by
scene_index == scene_getActiveIndex().

- Change 5 — preset_setVoiceAudioOut(): clears
  audio_out_step_override[slot].active after scene_setVoiceAudioOut(),
  before the existing preset_applyKitAudioRouting() return.
- Change 3 — preset_setVoiceFxSendAmount(): clears
  fx_send_step_override[slot].active after scene_setVoiceFxSendAmount().
- Change 4 — preset_setVoiceFxSendMorph(): clears
  fx_send_step_override[slot].active after scene_setVoiceFxSendMorph().
- Change 8 — preset_setSlot6Track7AmpEnvelopeDecay(): calls
  instrumentManager_clearSlot6Track7StepDecayOverride() after the
  Normal/Morph scene_set... branch.
- Change 6 — preset_morphVoiceScene(): calls
  presetMorph_clearStepAutomationOverride(slot) after
  scene_setVoiceMorphAmount(), before the active-Scene mirror/request
  branch. Kept as the schedule's separate guard (the note permits folding;
  the separate form is retained for traceability).
- Change 7 — preset_morphScene(): clears all six slots through the
  single-slot helper after scene_setEffectMorphAmount(), before the
  active-Scene mirror sync/rebuild branch.

### Step 3 — presetManager.c Rule B (Change 14) — DONE

- preset_applySceneSettings(): immediately after the
  !scene || scene_index != scene_getActiveIndex() guard and before the
  "Apply immediate Scene-wide settings" comment, added the four clears
  (preset_clearAllFxSendStepOverrides(),
  preset_clearAllAudioOutStepOverrides(scene_index),
  presetMorph_clearAllStepAutomationOverrides(scene_index),
  instrumentManager_clearSlot6Track7StepDecayOverride()) under one contract
  comment block that states the Effect-Morph clear is already owned by
  effects_activateScene().

### Step 4 — EffectsManager.c / .h (Changes 9, 10, 11) — DONE

- Change 9: added effects_clearMorphAutomationOverride() after
  effects_setMorphAutomation(), with its full contract comment block. It is a
  non-static public function (the header forward-declares it, so the earlier
  effects_setMorphAmountScene() call site resolves).
- Change 10: declared it in EffectsManager.h after the
  effects_setMorphAutomation() declaration, with the matching contract block.
- Change 11: effects_setMorphAmountScene() clears the override when
  scene_index == effects_state.scene_index (the runtime's active Scene), after
  scene_setEffectMorphAmount() and before the changed-value return.

Ordering note: effects_setMorphAmountScene() is defined ~line 642 while the
helper body is ~line 1190; the header declaration makes the earlier call
legal and the -Wall -Wextra build is clean.

### Step 5 — sequencer.c / .h (Changes 12, 13, 15, 16) — DONE

- Change 12: added the public (non-static) seq_clearSceneAutomationDirty()
  after the private seq_clearAutomationDirty(), with its contract block. It is
  intentionally non-static so the header can declare it for callers.
- Change 13: declared it in sequencer.h immediately after
  seq_automationHoldsParameter(), with the matching contract block.
- Change 15: seq_selectActivePattern() calls seq_clearSceneAutomationDirty()
  after voiceControl_noteOff(0xFF).
- Change 16: seq_alignActivePatternToScene() calls it at the end of the body,
  after seq_realignActivePatternToMasterClock(), for the Bank Load alignment
  path.

Rule B pairing verified: every Scene-switch path that reaches
seq_selectActivePattern()/seq_alignActivePatternToScene() also reaches
preset_applySceneSettings() (PERF press → preset_startDrumsetApply(); Bank Load
→ shared Scene worker / pre-audio preset_sendDrumsetParameters), so clearing
the bitmap without an inline restore is safe — the overrides themselves are
cleared by Change 14 in the same switch.

### Step 6 — Build and memory verification — DONE

- make all (DEV config, DEV_MODE_LOGGING 1) compiles clean with -Wall -Wextra;
  no new warnings or errors.
- Linked result: text 532,784, data 416, bss 426,712 (dec 959,912). data and
  bss are unchanged from the S075 close figures
  (416 / 426,712), confirming zero new RAM: every change flips an existing
  .active / valid flag or reuses an existing bitmap. No new SRAM or memory
  allocation was introduced, so the approved-allocation clause is not engaged.
- Flash: 533,200 / 753,664 B used, headroom 220,464 B. FX arena unchanged at
  126,592 B, margin 3,712 B; ITCM 4,168 / 16,384 B; DTCM statics 4,472 B.

### Remaining verification (hardware, user)

The seven checks in S076_SCENE_PARAM_AUTOM_LOCK_BUG.md section 6 are hardware
listening tests and were not run here (no device access). They remain the
acceptance criteria; the code review plus clean build above is the extent of
what can be proven in-session.

---

## Post-Implementation Assessment

Independent review of all 16 change sites against the source files as they
exist after implementation. Each change is checked for: correct placement,
correct guard logic, no unintended side effects, and completeness.

### Change 1 — `presetMorph_clearStepAutomationOverride()` (presetMorphEngine.c:887)

**Correct.** Body is a guarded single-field clear of `morph_step_override[slot].active`.
Does not zero `.amount` — this is intentional: the amount is not read while inactive
(confirmed by `presetMorph_effectiveVoiceBase()` which checks `.active` first).
Does not queue a rebuild — correct: callers (`preset_morphVoiceScene`,
`preset_morphScene`) queue their own `presetMorph_requestVoice/All` afterward.
Range guard matches the existing `presetMorph_setStepAutomationOverride`.

### Change 2 — Header declaration (presetMorphEngine.h:174)

**Correct.** Placed after the `clearAll` declaration and before `#endif`. Signature
matches the body. Contract comment is accurate.

### Change 3 — `preset_setVoiceFxSendAmount()` (presetManager.c:1292–1293)

**Correct.** Placed after `scene_setVoiceFxSendAmount()` and before `return 1u`.
Guarded by `scene_index == scene_getActiveIndex()` — correct: an inactive-Scene
edit (e.g. edit-mask fan-out) must not clear the active runtime override.
The FX send override is per-slot and carries no Scene identity, so the active-
Scene guard is the right discriminator.

### Change 4 — `preset_setVoiceFxSendMorph()` (presetManager.c:1325–1326)

**Correct.** Same pattern as Change 3, for the Morph endpoint of the same
FX send. Both endpoints feed the same slot's interpolated send amount, so
clearing the override from either endpoint is correct: the mixer will
recompute from the retained Normal/Morph pair on its next block.

### Change 5 — `preset_setVoiceAudioOut()` (presetManager.c:1257–1258)

**Correct.** Placed after `scene_setVoiceAudioOut()` and before
`preset_applyKitAudioRouting()`. The sequence is correct: clear the override,
then write the retained route to the DSP mixer. If done in reverse, one block
could read the stale override before it was cleared. As placed, the clear
precedes the DSP write, so the next effective-getter read returns the
retained value. The `preset_applyKitAudioRouting()` return propagates
correctly (it was the existing return value).

### Change 6 — `preset_morphVoiceScene()` (presetManager.c:3336–3337)

**Correct.** Placed after `scene_setVoiceMorphAmount()` and before the
active-Scene rebuild branch. The clear happens before
`presetMorph_requestVoice()`, so when the worker picks up the queued slot,
`presetMorph_effectiveVoiceBase()` returns the newly retained amount.
The two separate `scene_getActiveIndex()` calls are redundant but harmless
(the log notes the option to fold them). No ordering hazard.

### Change 7 — `preset_morphScene()` (presetManager.c:3286–3289)

**Correct.** Clears all six slots through the single-slot helper, placed after
`scene_setEffectMorphAmount()` and before the mirror sync / rebuild block.
The loop matches `INSTRUMENT_SLOT_COUNT` (6). The active-Scene guard
prevents clearing overrides when an inactive Scene receives a fan-out
morph write. Same fold-opportunity note as Change 6 — no correctness issue.

### Change 8 — `preset_setSlot6Track7AmpEnvelopeDecay()` (presetManager.c:1392–1393)

**Correct.** Placed after both branches of the Normal/Morph image write and
before `return 1u`. Calls the existing
`instrumentManager_clearSlot6Track7StepDecayOverride()` under the active-Scene
guard. This function has no slot parameter because there is only one slot6
track7 decay override in the system (it is a singleton, not per-slot).

### Change 9 — `effects_clearMorphAutomationOverride()` (EffectsManager.c:1220)

**Correct.** Body clears exactly one field: `morph_override_valid`. Does not
touch `morph_override` — correct: the value is not read while invalid
(confirmed by the service loop's `if (morph_override_valid)` guard at line
1567–1568). Does not touch `effects_automation.active` or `pending_end` —
correct: those are per-parameter Effect overlays with their own lifecycle.
The function is non-static (public) because it is called from the earlier
`effects_setMorphAmountScene()` before its own definition; the header
declaration (Change 10) resolves the forward reference.

### Change 10 — Header declaration (EffectsManager.h:420)

**Correct.** Placed after `effects_setMorphAutomation()` and before the LFO
contribution block. Signature matches the body.

### Change 11 — `effects_setMorphAmountScene()` (EffectsManager.c:659–660)

**Correct.** Uses `effects_state.scene_index` as the active-Scene reference
rather than `scene_getActiveIndex()`. This is the right choice:
`effects_state.scene_index` is the runtime's own scene pointer, set by
`effects_activateScene()`, and is the discriminator the service loop uses.
The clear is placed after `scene_setEffectMorphAmount()` (the retained write)
and before the changed-value return — the return comparison re-reads the
retained value, so the clear does not affect the return logic.

### Change 12 — `seq_clearSceneAutomationDirty()` (sequencer.c:292)

**Correct.** Zeroes only `seq_scene_automation_dirty`, leaving
`seq_automation_dirty[]` (the per-voice bitmap) untouched — correct: voice
automation hold-until-trigger is not affected by Scene changes. The function
is non-static because callers are in the same file (Changes 15–16) *and*
the header declares it for potential future external use.

### Change 13 — Header declaration (sequencer.h:188)

**Correct.** Placed after `seq_automationHoldsParameter()` and before
`seq_setRunning()`. The grouping keeps all automation-related API together.

### Change 14 — `preset_applySceneSettings()` (presetManager.c:1428–1431)

**Correct.** All four override families are cleared:
- `preset_clearAllFxSendStepOverrides()` — no parameter needed
- `preset_clearAllAudioOutStepOverrides(scene_index)` — takes the new Scene
  index, which is correct (the existing clear helper uses it for retained-
  value routing restore)
- `presetMorph_clearAllStepAutomationOverrides(scene_index)` — takes the new
  Scene index for rebuild (the rebuild reads the *new* Scene's Morph values)
- `instrumentManager_clearSlot6Track7StepDecayOverride()` — singleton, no param

Placed immediately after the Scene validity guard and before any mirror sync
or rebuild. The comment correctly notes that Effect Morph is already cleared
upstream by `effects_activateScene()` → `effects_automationReset()` in
`preset_startDrumsetApply()`, so no duplicate Effect Morph clear is needed.

**Ordering note:** `presetMorph_clearAllStepAutomationOverrides(scene_index)`
queues a `presetMorph_rebuildScene()` internally when any slot was active.
This rebuild runs before the explicit `presetMorph_rebuildScene(scene_index)`
at line 1463. The double rebuild is harmless (the second rebuild is a no-op
if the worker already processed all slots) but could be optimized. Not a
correctness issue.

### Change 15 — `seq_selectActivePattern()` (sequencer.c:696)

**Correct.** Placed after `voiceControl_noteOff(0xFF)` — the note-off fires
first so any voice that was sounding with the old Scene's overrides gets
silenced before the dirty bitmap is cleared. The dirty bitmap clear is the
last operation in the function, which is the right position: all other
sequencer state (active pattern, per-track pattern, clock realignment)
is already committed.

### Change 16 — `seq_alignActivePatternToScene()` (sequencer.c:739)

**Correct.** Same `seq_clearSceneAutomationDirty()` call, placed after
`seq_realignActivePatternToMasterClock()` at the end of the function body.
This function has no `voiceControl_noteOff()` call (by design — it is a
state restore for Bank Load, not a performance action), so the bitmap clear
follows the clock realignment. This is correct: the Bank Load path calls
`preset_startDrumsetApply()` separately, which clears the overrides
themselves (Change 14) and the Effect automation (via `effects_activateScene`).

### Coverage completeness

**All five override families are covered by Rule A:**

| Override | Retained setters that clear it |
|----------|-------------------------------|
| `fx_send_step_override[slot]` | `preset_setVoiceFxSendAmount()` (Change 3), `preset_setVoiceFxSendMorph()` (Change 4) |
| `audio_out_step_override[slot]` | `preset_setVoiceAudioOut()` (Change 5) |
| `morph_step_override[slot]` | `preset_morphVoiceScene()` (Change 6), `preset_morphScene()` (Change 7) |
| `slot6_track7_decay_step_active` | `preset_setSlot6Track7AmpEnvelopeDecay()` (Change 8) |
| `effects_automation.morph_override_valid` | `effects_setMorphAmountScene()` (Change 11) |

**All five override families are covered by Rule B** (Change 14 in
`preset_applySceneSettings()`), plus `seq_scene_automation_dirty` is cleared
in both Scene-switch paths (Changes 15–16).

**Caller coverage for Rule A — MIDI:** `midiParser_setMorphFromModWheel()` →
`preset_morph()` → `preset_morphScene()` (Change 7).
`midiParser_setVoiceMorphFromModWheel()` → `preset_morphVoice()` →
`preset_morphVoiceScene()` (Change 6). MIDI CC1 is covered without any
direct MidiParser changes.

**Caller coverage for Rule A — Menu:** `menu_cellCommitValue()` Scene-setting
branch calls `preset_setVoiceAudioOut()` (Change 5),
`preset_setVoiceFxSendAmount()` (Change 3), `preset_morphVoiceScene()` (Change 6),
etc. `menu_commitEffectMorphParam()` → `effects_setMorphAmount()` →
`effects_setMorphAmountScene()` (Change 11). Menu is covered without any
direct menu.c changes.

**Caller coverage for Rule A — copy/clear:** `clearOps.c` calls
`preset_setVoiceFxSendAmount()` with zero (Change 3 covers it).
`InstrumentManager.c:2403` calls `preset_setSlot6Track7AmpEnvelopeDecay()`
during type changes (Change 8 covers it).

### What was NOT changed (verified unchanged)

- `seq_automation_dirty[]` — voice descriptor bitmaps: not touched by any change.
- `presetMorph_writeRuntimeBase()` — F3 guard: not touched.
- `seq_automationHoldsParameter()` — read-only view: not touched.
- `seq_restoreAutomatedParameters()` — per-trigger restore: not touched.
- `effects_automationStepBegin/Flush` — FX sequencer lifecycle: not touched.
- `effects_activateScene()` — already clears Effect Morph: not touched.
- `seq_restoreAllSceneAutomation()` — transport-boundary restore: not touched.

### Risk assessment

**Low risk.** All changes are single-field flag clears guarded by active-Scene
checks. No new data structures, no new allocations, no ISR-context changes,
no new mutex/lock usage, no changes to the TIM3 audio path. The worst-case
failure mode is a cleared override that should have stayed set — which would
cause the retained value to be heard one step early (the next automation step
would re-set it). This is preferable to the current bug where the override
permanently locks the parameter.

### Verdict

All 16 changes match the implementation schedule. Build is clean, RAM is
unchanged, and every Rule A/B change site is correctly placed, guarded, and
covers its documented callers. Ready for hardware verification per section 6
of the bug plan.
