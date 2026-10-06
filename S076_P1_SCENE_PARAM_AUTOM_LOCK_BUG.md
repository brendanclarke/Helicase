# S076 Scene Parameter Automation Lock Bug

## 1. Problem Statement

When a Scene parameter is automated on a main-pattern track step, that parameter
becomes locked to the automated value. Menu edits appear to have no effect: the
display continues to show the automated value and the audible output does not
change. The lock persists across steps and even across Scene changes, until the
transport is stopped or restarted.

Confirmed with: voice FX send amount (`Nfx`), voice Morph amount (`Nvm`).

Expected behaviour: for Scene and Effect parameters automated on main-pattern
tracks, the automation is a **single set**. It fires once and does not otherwise
prevent any other source (menu, MIDI, etc.) from changing the parameter
immediately afterward.

## 2. Root Cause

`seq_applySceneAutomation()` (sequencer.c:944) dispatches main-pattern Scene-
target automation through persistent override structures:

| Kind | Override state | Set by | Cleared by |
|------|---------------|--------|------------|
| Voice Morph (`Nvm`) | `morph_step_override[slot]` (presetMorphEngine.c:59) | `presetMorph_setStepAutomationOverride()` | `presetMorph_clearAllStepAutomationOverrides()` |
| FX Send (`Nfx`) | `fx_send_step_override[slot]` (presetManager.c:180) | `preset_setFxSendStepOverride()` | `preset_clearAllFxSendStepOverrides()` |
| Audio Out (`Nou`) | `audio_out_step_override[slot]` (presetManager.c:175) | `preset_setAudioOutStepOverride()` | `preset_clearAllAudioOutStepOverrides()` |
| Effect Morph (`fxm`) | `effects_automation.morph_override_valid` (EffectsManager.c:195) | `effects_setMorphAutomation()` | `effects_automationReset()` |
| Slot6 Track7 Decay (`7dc`) | `slot6_track7_decay_step_active` (InstrumentManager.c) | `instrumentManager_setSlot6Track7StepDecayOverride()` | `instrumentManager_clearSlot6Track7StepDecayOverride()` |

Every clear path above runs **only** from `seq_restoreAllSceneAutomation()`
(sequencer.c:340), called from `seq_setStepIndexToStart()` — i.e., transport
stop/start, pattern boundary, and external reset. No per-step or per-trigger
clearing exists for Scene targets, and **no menu-edit or MIDI path clears them**.

When a menu edit writes a retained Scene value, the override remains active.
Display reads and DSP runtime reads check the override first and return the
automated value, so the menu edit is invisible in both sound and display.

### Why voice descriptor automation is different (and correct)

The F3 fix (S075) added `seq_automation_dirty[slot]` (per-voice 64-bit bitmaps)
and the `presetMorph_writeRuntimeBase()` guard for voice descriptor parameters.
These bits are cleared on the **next trigger** for that voice
(`seq_restoreAutomatedParameters()`). The hold-until-trigger rule is correct for
voice parameters and must not be changed.

The Scene-target overrides predate F3 but their persistent-until-transport-reset
lifetime was not a problem before F3 because the surrounding code did not have a
way to distinguish "automation set this" from "the user set this" — the F3
session's work on the voice side made the Scene-side gap actionable.

### Why Effect-parameter automation is not affected

Effect parameters automated on main-pattern tracks go through
`effects_applyAutomation()` and are managed by the per-step-marker
begin/flush lifecycle (`effects_automationStepBegin()` /
`effects_automationStepFlush()`). Their active bits are cleared when the owning
track's next step does not rewrite them. Menu edits of Effect parameters write
directly to the retained `effect_record_t` values, which are applied on the next
Effect service pass if the automation bit is not set. This per-step clearing is
the correct behaviour described by the user for Effect parameters.

The one exception is Effect Morph (`fxm`), which is a Scene-level target, NOT
an Effect-parameter target. It is routed through `seq_applySceneAutomation()`
and uses `effects_automation.morph_override_valid` — the same persistent
override pattern as the other Scene targets.

### Why the override structures exist (and must stay)

The overrides are the runtime-only layer between retained SceneData and DSP
consumption. Automation must change audible output **without writing retained
SceneData** because:

- Retained SceneData is what AutoSave persists. If automation wrote retained
  data, every step-automation event would burn transient pattern values into
  saved state.
- On transport reset, the user expects the Scene to revert to its retained
  values. If automation had overwritten them, there is nothing to revert to.

So the override says "use this value instead of the retained value" and the
transport-boundary restore path (`seq_restoreAllSceneAutomation()`) knows to
reapply retained values for every bit in `seq_scene_automation_dirty`.

The bug is not that the overrides exist — it is that they never yield to
anything except transport reset.

### Scene change does not clear overrides

The PERF Scene-switch path (`menu_perfModeSceneButtonPressed` →
`seq_selectActivePattern` → `preset_startDrumsetApply` →
`preset_applySceneSettings` → `presetMorph_rebuildScene`) does **not** clear
`morph_step_override[]`, `fx_send_step_override[]`, `audio_out_step_override[]`,
or `slot6_track7_decay_step_active`. So the new Scene's morph rebuild
(`presetMorph_effectiveVoiceBase()`) returns the previous Scene's stale Morph
override, and the mixer keeps the previous Scene's FX send override.

`effects_activateScene()` does clear `effects_automation.morph_override_valid`
(via `effects_automationReset()`), so Effect Morph alone is not affected across
Scene changes.

## 3. Fix Strategy

Two rules:

**Rule A — Non-automation writes clear the override.** When any non-automation
write sets a Scene parameter's retained or runtime value, clear the
corresponding step override for that slot/parameter.

**Rule B — Scene activation clears all Scene-target overrides.** When the active
Scene changes, every override from the previous Scene's automation is stale and
must be cleared before the new Scene's values are applied.

The `seq_scene_automation_dirty` bitmap is left unchanged — it controls
transport-boundary restore (which restores to retained values, correct after a
menu edit or Scene change).

### 3.1 FX Send — `preset_setVoiceFxSendAmount()` and `preset_setVoiceFxSendMorph()`

File: `Core/Bank/Scene/Preset/presetManager.c`

In `preset_setVoiceFxSendAmount()` (line 1248), after the `scene_setVoiceFxSendAmount()`
call, add:

```c
/* A non-automation write supersedes any active step override (S076). */
if (scene_index == scene_getActiveIndex())
    fx_send_step_override[slot].active = 0u;
```

Same in `preset_setVoiceFxSendMorph()` (line 1268): add the same clear after
`scene_setVoiceFxSendMorph()`. Editing either endpoint of a morphable FX send
should release the step override so the interpolated result takes effect.

### 3.2 Voice Morph — `preset_morphVoiceScene()`

File: `Core/Bank/Scene/Preset/presetMorphEngine.c` (new public helper) and
`Core/Bank/Scene/Preset/presetManager.c` (caller).

Add a single-slot clear in presetMorphEngine:

```c
void presetMorph_clearStepAutomationOverride(uint8_t slot)
{
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    morph_step_override[slot].active = 0u;
}
```

In `preset_morphVoiceScene()` (presetManager.c:3207), call it **before** the
morph request so the rebuild uses the new retained base:

```c
if (scene_index == scene_getActiveIndex())
    presetMorph_clearStepAutomationOverride(slot);
```

Also in `preset_morphScene()` (presetManager.c:3163, the bulk-set path), clear
all slots:

```c
if (scene_index == scene_getActiveIndex()) {
    for (uint8_t s = 0u; s < INSTRUMENT_SLOT_COUNT; s++)
        presetMorph_clearStepAutomationOverride(s);
}
```

Declare in `presetMorphEngine.h`.

### 3.3 Audio Out — `preset_setVoiceAudioOut()`

File: `Core/Bank/Scene/Preset/presetManager.c`

In `preset_setVoiceAudioOut()` (line 1229), after `scene_setVoiceAudioOut()`,
add:

```c
if (scene_index == scene_getActiveIndex())
    audio_out_step_override[slot].active = 0u;
```

### 3.4 Effect Morph — `effects_setMorphAmount()`

File: `Core/DSP/Effects/EffectsManager.c`

Add a public single-purpose clear:

```c
void effects_clearMorphAutomationOverride(void)
{
    effects_automation.morph_override_valid = 0u;
}
```

Call it from `effects_setMorphAmount()` (or `effects_setMorphAmountScene()` for
the active Scene only) so a menu or MIDI Morph edit supersedes the Pattern
override.

Alternatively, since `effects_setMorphAmount()` fans out to every masked Scene,
clear the override unconditionally inside `effects_setMorphAmountScene()` when
`scene_index == effects_state.scene_index` (the active Scene).

Declare in `EffectsManager.h`.

### 3.5 Slot6 Track7 Decay

File: `Core/DSP/Instruments/InstrumentManager.c`

In the menu-commit path for `MENU_KIT_SETTING_SLOT6_TRACK7_AMP_DECAY`
(menu.c:3785 calls `preset_setSlot6Track7AmpEnvelopeDecay()`), or inside
`preset_setSlot6Track7AmpEnvelopeDecay()` itself (presetManager.c:1309), clear
the override for the active Scene:

```c
if (scene_index == scene_getActiveIndex())
    instrumentManager_clearSlot6Track7StepDecayOverride();
```

The clear function already exists; it only needs to be called.

### 3.6 Scene activation — `preset_applySceneSettings()`

File: `Core/Bank/Scene/Preset/presetManager.c`

At the top of `preset_applySceneSettings()` (line 1338), before
`presetMorph_rebuildScene()` or any mirror sync, clear every Scene-target
override:

```c
preset_clearAllFxSendStepOverrides();
preset_clearAllAudioOutStepOverrides(scene_index);
presetMorph_clearAllStepAutomationOverrides(scene_index);
instrumentManager_clearSlot6Track7StepDecayOverride();
```

Effect Morph is already cleared by `effects_activateScene()` →
`effects_automationReset()`, called from `preset_startDrumsetApply()` at the
same Scene-switch boundary. No additional Effect Morph clear is needed here.

This also covers Bank Load Scene activation (which calls
`preset_startDrumsetApply()` → `preset_applySceneSettings()`) and boot (which
calls `preset_startDrumsetApply()` after `audioCodec_init()`).

The `seq_scene_automation_dirty` bitmap should also be cleared on Scene change,
since the dirty bits reference the previous Scene's targets and the new Scene's
retained values are what transport restore should use. Add:

```c
seq_clearSceneAutomationDirty();   /* new public accessor */
```

or call from the same `seq_selectActivePattern()` path. Since
`seq_scene_automation_dirty` is static in sequencer.c, either expose a clear
accessor or fold the clearing into a sequencer function already called during
Scene switch. The simplest site is `seq_selectActivePattern()` itself (called
from PERF Scene press and Bank Load alignment).

## 4. Files Changed

| File | Change | Rule |
|------|--------|------|
| `Core/Bank/Scene/Preset/presetManager.c` | Clear FX send and audio out overrides on retained writes; clear voice morph override before morph request; clear all overrides in `preset_applySceneSettings()`. | A, B |
| `Core/Bank/Scene/Preset/presetMorphEngine.c` | Add `presetMorph_clearStepAutomationOverride()` single-slot helper. | A |
| `Core/Bank/Scene/Preset/presetMorphEngine.h` | Declare the new helper. | A |
| `Core/DSP/Effects/EffectsManager.c` | Clear morph override on `effects_setMorphAmount()` for the active Scene. | A |
| `Core/DSP/Effects/EffectsManager.h` | Declare the new clear helper (if exposed; alternatively inline in the setter). | A |
| `Core/DSP/Instruments/InstrumentManager.c` or `presetManager.c` | Clear slot6 track7 decay override on the retained-value setter. | A |
| `Core/Sequencer/sequencer.c` or `sequencer.h` | Clear `seq_scene_automation_dirty` on Scene change (new accessor or in `seq_selectActivePattern()`). | B |

No new RAM. No wire-format, AutoSave, or storage change.

## 5. What NOT to Change

- **Voice descriptor parameter automation** (`seq_automation_dirty[]`,
  `presetMorph_writeRuntimeBase()`, `seq_automationHoldsParameter()`,
  `seq_restoreAutomatedParameters()`): the hold-until-trigger rule is correct
  and must not be changed. The user confirmed voice parameter automation works
  correctly.
- **FX sequencer Effect parameter automation** (`effects_automation.active`,
  `effects_automationStepBegin/Flush`): per-step lifecycle is correct.
- **`seq_scene_automation_dirty`** bitmap: leave it set. Transport-boundary
  restore uses retained values, which are correct after a menu edit.
- **`seq_restoreAllSceneAutomation()`**: no change. It continues to be the
  transport-boundary restore path.

## 6. Verification

1. **FX Send (`Nfx`)**: automate a voice FX send on one step. While playing,
   turn the encoder on the `Nfx` cell. The display should follow the encoder
   and the audible send should change. The automated step should still fire
   its value when it plays. Stopping and restarting transport should restore
   the current retained (menu-edited) value.

2. **Voice Morph (`Nvm`)**: same test with a voice Morph amount.

3. **Effect Morph (`fxm`)**: automate `fxm` on one step. While playing, edit
   `fxm` from the PERF page or Effect page. The edit should take effect.

4. **Audio Out (`Nou`)**: automate audio routing. Menu edit should take effect
   immediately.

5. **Scene change**: automate a Scene parameter, then change Scenes (PERF
   press). The new Scene's parameters must not be locked by the previous
   Scene's automation.

6. **Transport boundary**: after a menu edit supersedes automation, stop and
   restart. The retained (menu-edited) value should be the one that plays.

7. **Voice parameter automation (regression check)**: verify that voice
   descriptor automation still holds until the next trigger and is restored
   correctly. The F3 behavior must be unchanged.

## 7. Notes

- The override mechanisms were designed for transport-boundary lifetime, which
  is correct for the FX sequencer's held-step model. The bug is that main-
  pattern Scene automation fires through the same overrides but has no per-step
  or per-trigger clear. The fix does not change the override lifetime — it adds
  a "menu/MIDI edit supersedes" rule.
- Copy/clear's `clearOps.c` calls `preset_setVoiceFxSendAmount()` with zero.
  After this fix it will also clear the step override, which is correct: a
  clear operation should release automation holds.
- `InstrumentManager.c:2403` calls `preset_setSlot6Track7AmpEnvelopeDecay()`
  during type changes. Clearing the override there is correct: a type change
  should release any stale automation state.
