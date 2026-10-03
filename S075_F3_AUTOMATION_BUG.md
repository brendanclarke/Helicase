# S075 — Automation bug: a menu edit resets an automated parameter mid-note

Remediation plan. **Plan only; no code is changed by this document.** Line
numbers were read on 2026-10-03 from the working tree after the F2
implementation (`ee3c665` plus the overlay follow-up) and are given with a
function anchor.

**Revision 2 (user, 2026-10-03):** automation always wins over a menu edit
(Q1); a menu edit only sets an endpoint and never overrides the Morph
interpolation; an instrument type change is out of scope (Q2). This adds
R-08 … R-11 (§5), renumbers the comment updates to R-12, and resolves §6.

## Contents

1. Symptom
2. Root cause
3. What is affected
4. What is not affected
5. Remediation (code changes)
6. Decisions and follow-ups
7. Resources
8. Verification

---

## 1. Symptom (user, hardware)

Coarse pitch is automated in the Pattern. While the note plays, editing a
different parameter on the VOICE page (pan, filter cutoff, any parameter)
sets the pitch back to its un-automated value at once, mid-note.

## 2. Root cause

Step automation of an instrument parameter is a **runtime overlay**. It is
written into the voice runtime only and is meant to last until that voice's
next trigger:

- `seq_drainPendingAutomation()` (`Core/Sequencer/sequencer.c` lines
  1003–1090) writes the step value with `instrumentManager_writeRuntime()`
  and sets bit `local` in `seq_automation_dirty[slot]` (line 1054).
- `seq_restoreAutomatedParameters()` (lines 1100–1129) is called from the
  trigger funnel (`MidiVoiceControl.c` line 170, just before
  `instrumentManager_triggerTrack()`). It writes the base value
  (`morph_interpolation[local]`) back for each set bit, then clears the
  bitmap.
- `seq_restoreAllAutomation()` (line ~300) does the same for every slot at
  transport start/stop, Pattern boundaries and external reset.

The retained values are never changed by automation. The bitmap is the only
record that "this parameter is currently held by automation".

**The Morph worker ignores that record.** A menu edit of any morphable
instrument parameter goes:

1. `menu_cellCommitValue()` (`Core/Menu/menu.c` lines 3797–3820), then
2. `preset_setInstrumentParameter()` (`presetManager.c` lines 906–963). This
   stores the edited endpoint and calls
   `presetMorph_requestVoice(scene_index, slot)` (line 960), which queues the
   **whole voice** for Morph interpolation.
3. `presetMorph_tick()` (`presetMorphEngine.c` lines 439–539) then walks every
   morphable descriptor of that voice. For each one it computes the
   interpolated base, stores it in `morph_interpolation[local]`, and writes
   it to the runtime with `preset_applyInstrumentRuntimeValue()` (line 503).

Step 3 writes the base of the automated coarse pitch over its automation
overlay, while the note is still sounding. The dirty bit stays set, so the
next trigger restores the same base: the overlay is simply lost early.
`presetMorph_applyVoiceNow()` (lines 555–640, runtime write at line 608) has
the same unguarded write for whole-voice synchronous applies.

Both pieces predate S075 (the Morph worker write is from `a443bd38`, the
`requestVoice` call from `de736f56`; the overlay bitmap from session 065/070).
S075 did not introduce the bug.

### 2.1 Second defect: the menu edit's direct runtime write

`preset_setInstrumentParameter()` lines 939–948 also writes the edited value
straight to the runtime when the menu passes `record_automation` (Normal
view, active Scene) and the voice's **retained** Morph amount is 0:

```c
        if (record_automation && image == INSTRUMENT_IMAGE_MAIN &&
            scene && scene_getVoiceMorphAmount(scene_index, slot) == 0u) {
            (void)preset_applyInstrumentRuntimeValueInternal(
                scene_index, instrumentParam_make(slot, descriptor_index),
                value, 1u);
        }
```

This breaks both user rules (revision 2):

1. **Automation must always win.** The write ignores the overlay bitmap, so
   editing an automated parameter replaces its automation value mid-note.
2. **A menu edit only sets an endpoint; it never overrides the Morph
   interpolation.** The test uses the retained amount, not the amount the
   voice actually plays with. With a voice Morph step override (`Nvm`) or a
   Morph LFO active while the retained amount is 0, the raw Normal endpoint
   is written over the interpolated value. The worker corrects it a few
   ticks later, so there is a short wrong value in the sound.

In the Morph view, or with a retained amount above 0, nothing is written
directly. The edit is heard only when the worker reaches that parameter in
its pass over the whole voice, so the delay depends on its position.

## 3. What is affected

**Parameters:** every automatable **morphable instrument parameter** of any
voice: coarse/fine pitch, decays, filter, drive, pan, volume, modulation
amounts, and so on. These are all the cells the worker rewrites. Track 7
shares slot 6, so its overlays are affected the same way.

**Triggers that reset the overlays:** anything that queues a voice (or all
voices) for the Morph worker, or applies a voice synchronously, while an
automation overlay is held on that voice.

| Action | Path | Voices reset |
|---|---|---|
| Edit any morphable instrument parameter on a VOICE page (Normal or Morph view), including edits fanned out over the edit mask to the active Scene | `preset_setInstrumentParameter()` → `presetMorph_requestVoice()` (`presetManager.c` 960) | the edited voice |
| Change a voice's Morph amount (VOICE mix `vm` cell, PERF) | `preset_morphVoiceScene()` → `requestVoice` (3176) | that voice |
| Turn the global Morph | `preset_morphScene()` → `requestAll` (3141) | all voices |
| An LFO routed to a voice's Morph amount | `presetMorph_setVoiceLfoModulation()` → `requestVoice` (engine 674); clear (700) | the target voice, **continuously** while the LFO runs |
| Step automation of voice Morph (`Nvm`) on the same or a later step | `presetMorph_setStepAutomationOverride()` → `requestVoice` (engine 759) | that voice: a parameter automated on the same step as `Nvm` is overwritten a few ticks later |
| Track-7 alternate decay edit (Kit setting) | `requestVoice(scene, 5)` (`presetManager.c` 1946) | slot 6 |
| Instrument/Kit load or apply while playing | `preset_startInstrumentMorphApply()` (2014), `preset_startInstrumentApplyImage()` (2199), `preset_resetAndApplyKitVoiceImage()` → `applyVoiceNow` (1518), staged Kit commit (1919) | the loaded voices |
| `preset_applySceneSettings()`, `preset_rebuildMorph()` | `rebuildScene` (1327, 3194) | all voices |
| Scene switch at the quiet threshold | `presetMorph_applyVoiceNow()` (`sequencer.c` 380 and the deferred Scene-switch worker) | the switched voices |

The menu edit is the case you hit. The LFO-on-Morph and `Nvm` cases are
worse: there, automation of every other parameter of that voice barely
survives, with no user action at all.

## 4. What is not affected

| Automated target | Why it is safe |
|---|---|
| Effect parameters (Pattern block-7 entries) | `EffectsManager.c` composes each block from layers: Morph base, then FX lock, then Pattern overlay (`effects_automation.active`), then LFO (lines ~1557–1590). Edits change only the base layer. |
| Scene targets: FX send `Nfx`, audio out `Nou`, voice Morph `Nvm`, Effect Morph `fxm` | Separate overlays (`preset_setFxSendStepOverride()`, `preset_setAudioOutStepOverride()`, `morph_step_override[]`, `effects_setMorphAutomation()`) read through effective getters; edits change only retained values. |
| Track-7 alternate decay step override | Own override (`instrumentManager_setSlot6Track7StepDecayOverride()`). |
| Non-morphable (supplemental) instrument cells | The worker skips them. `preset_setSupplementalParameter()` writes only the edited cell. |
| A MIDI CC to one parameter | External CC enters the active Scene's clamped Normal endpoint and queues the Morph worker; the worker skips any automation-held runtime value. The legacy internal CC path remains runtime-only; see §6, P1. |

## 5. Remediation (code changes)

**Rule:** while a parameter is held by step automation, a Morph-base write
updates `morph_interpolation[]` (the value the next trigger restores) but
does not touch that parameter's runtime value. The overlay then lasts until
the trigger, exactly as the overlay contract says. Every other parameter of
the voice updates as today.

The guard goes where the base is written to the runtime (the two
Morph-engine sites), not in each of the nine request paths. Every current
and future request is covered, and the restore logic stays in one owner.

### R-01 — `Core/Sequencer/sequencer.h` after line 155 (`seq_restoreAutomatedParameters`) — ADD

```c
/*
 * Report whether step automation currently holds one voice parameter.
 *
 * What: nonzero when seq_automation_dirty[slot] has bit `local` set, that
 * is, seq_drainPendingAutomation() wrote a step value into this
 * descriptor's runtime and the voice has not been triggered since.
 * Why: the overlay must last until the next trigger. Writers of the Morph
 * base (presetMorphEngine.c) use this to update morph_interpolation[]
 * without overwriting a held runtime value; the trigger restore then
 * applies the new base.
 * Inputs: instrument slot 0..5 (track 7 uses slot 5), descriptor-local index
 * 0..INSTRUMENT_PARAM_COUNT-1. Output: 0/1; 0 for out-of-range input.
 * Context: foreground only. The drain, the trigger funnel
 * (voiceControl_processPending()), the transport restores and the Morph
 * worker all run in the main loop, so the 64-bit bitmap is never read
 * while it is half-written.
 * Clients: presetMorph_tick(), presetMorph_applyVoiceNow().
 * Affiliates: seq_drainPendingAutomation(), seq_restoreAutomatedParameters(),
 * seq_restoreAllAutomation().
 */
uint8_t seq_automationHoldsParameter(uint8_t slot, uint8_t local);
```

### R-02 — `Core/Sequencer/sequencer.c` after `seq_restoreAutomatedParameters()` (ends line ~1129) — ADD

```c
uint8_t seq_automationHoldsParameter(uint8_t slot, uint8_t local)
{
    /* Contract in sequencer.h; read-only view of the overlay bitmap. */
    if (slot >= INSTRUMENT_SLOT_COUNT || local >= INSTRUMENT_PARAM_COUNT)
        return 0u;
    return (uint8_t)((seq_automation_dirty[slot] >> local) & 1u);
}
```

`INSTRUMENT_PARAM_COUNT` is 64 (`InstrumentManager.h` line 19), matching the
`uint64_t` bitmap.

### R-03 — `Core/Bank/Scene/Preset/presetMorphEngine.c` includes (lines 1–5) — ADD

```c
#include "sequencer.h"
```

(`presetManager.c` already includes it, line 39.)

### R-04 — `Core/Bank/Scene/Preset/presetMorphEngine.c` before `presetMorph_tick()` (line ~437) — ADD

```c
/*
 * Write one Morph-derived base value to the voice runtime, unless step
 * automation holds that parameter (S075 automation fix).
 *
 * What: for the active Scene, applies `value` through
 * preset_applyInstrumentRuntimeValue(), except when
 * seq_automationHoldsParameter(slot, local) is set. The caller has already
 * stored `value` in morph_interpolation[local], which is what the next
 * trigger restores, so a held parameter picks up the new base at that
 * trigger instead of losing its automation mid-note.
 * Why: a menu edit, a Morph change, an LFO on Morph, `Nvm` automation or an
 * Instrument/Kit apply queues the whole voice. Before this fix, every
 * automated parameter of that voice snapped back to its base while the note
 * was still sounding.
 * Inputs: Scene, slot, descriptor-local index, interpolated value.
 * Output: runtime write (or none). Callers: presetMorph_tick(),
 * presetMorph_applyVoiceNow(). Affiliates: seq_restoreAutomatedParameters(),
 * seq_restoreAllAutomation(), preset_applyInstrumentRuntimeValue().
 */
static void presetMorph_writeRuntimeBase(uint8_t scene_index, uint8_t slot,
                                         uint8_t local,
                                         instrument_param_value_t value)
{
    if (scene_index != scene_getActiveIndex())
        return;
    if (seq_automationHoldsParameter(slot, local))
        return;
    (void)preset_applyInstrumentRuntimeValue(
        scene_index, instrumentParam_make(slot, local), value);
}
```

### R-05 — `presetMorph_tick()` lines 501–504 — MODIFY

Replace

```c
            id = instrumentParam_make(morph_worker.slot, local);
            if (morph_worker.scene_index == scene_getActiveIndex())
                preset_applyInstrumentRuntimeValue(morph_worker.scene_index,
                                                   id, value);
```

with

```c
            /* Held automation keeps its runtime value until the trigger. */
            presetMorph_writeRuntimeBase(morph_worker.scene_index,
                                         morph_worker.slot, local, value);
```

and remove the now-unused `instrument_param_id_t id;` local (line ~491).

### R-06 — `presetMorph_applyVoiceNow()` lines 606–608 — MODIFY

Replace

```c
        id = instrumentParam_make(slot, local);
        if (scene_index == scene_getActiveIndex())
            preset_applyInstrumentRuntimeValue(scene_index, id, value);
```

with

```c
        /*
         * Held automation keeps its runtime value. At trigger time the
         * caller (preset_applyDeferredSceneSlotForTrigger()) is followed at
         * once by seq_restoreAutomatedParameters(), which writes the new base
         * from morph_interpolation[]; at the quiet threshold and at transport
         * restore, the next trigger or seq_restoreAllAutomation() does.
         */
        presetMorph_writeRuntimeBase(scene_index, slot, local, value);
```

and remove its `instrument_param_id_t id;` local. In the function block,
add: "Parameters held by step automation are skipped
(presetMorph_writeRuntimeBase())."

### R-08 — `Core/Bank/Scene/Preset/presetMorphEngine.h` after `presetMorph_getResolvedVoiceAmount()` — ADD

```c
/*
 * Re-interpolate and apply one voice parameter now (S075 automation fix).
 *
 * What: after a menu edit of one Normal or Morph endpoint, computes that
 * parameter's interpolated value with the amount the voice is playing with
 * (presetMorph_getResolvedVoiceAmount(): step override or retained amount,
 * plus any LFO layer), stores it in morph_interpolation[local], and writes it
 * to the runtime through presetMorph_writeRuntimeBase(), so a parameter held
 * by step automation is not touched (automation always wins, user).
 * Why: a menu edit only moves an endpoint. The sound follows the
 * interpolation, never the raw endpoint, so with Morph above 0 an edit
 * changes the sound by less than the edited value (correct, user). Only
 * this parameter's interpolation changes, so the whole voice is not queued.
 * Inputs: resident Scene, slot 0..5, descriptor-local index of a morphable
 * parameter. Output: morph_interpolation[local] (any Scene); runtime write
 * (active Scene, not held). Non-morphable or invalid input: no-op.
 * Client: preset_setInstrumentParameter(). Affiliates: presetMorph_tick(),
 * presetMorph_interpolate(), seq_automationHoldsParameter().
 */
void presetMorph_applyParameterNow(uint8_t scene_index, uint8_t slot,
                                   uint8_t local);
```

### R-09 — `Core/Bank/Scene/Preset/presetMorphEngine.c` after `presetMorph_getResolvedVoiceAmount()` — ADD

```c
void presetMorph_applyParameterNow(uint8_t scene_index, uint8_t slot,
                                   uint8_t local)
{
    scene_t *scene = scene_get(scene_index);
    kit_instrument_slot_t *instrument;
    const ParamDescriptor *descriptor;
    instrument_param_value_t value;

    /* Contract in presetMorphEngine.h; same maths as presetMorph_tick(). */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT ||
        local >= INSTRUMENT_PARAM_COUNT)
        return;
    instrument = &scene->kit.instruments[slot];
    descriptor = instrumentManager_descriptor(instrument->type, local);
    if (!descriptor ||
        !(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE))
        return;
    value = presetMorph_interpolate(
        instrument->parameter_images.instrument_parameters[local],
        instrument->parameter_images.morph_instrument_parameters[local],
        presetMorph_getResolvedVoiceAmount(scene_index, slot));
    instrument->parameter_images.morph_interpolation[local] = value;
    presetMorph_writeRuntimeBase(scene_index, slot, local, value);
}
```

`presetMorph_writeRuntimeBase()` (R-04) is defined above it.

### R-10 — `Core/Bank/Scene/Preset/presetManager.c` lines 906–963, `preset_setInstrumentParameter()` — MODIFY

Signature: remove the `record_automation` argument. It now selects
nothing: the per-parameter apply is right in both views and at any Morph
amount.

```c
uint8_t preset_setInstrumentParameter(uint8_t scene_index, uint8_t slot,
                                      uint8_t descriptor_index,
                                      instrument_image_select_t image,
                                      uint8_t value)
```

Body after `preset_storeInstrumentEndpoint(…)`, replacing lines 937–961 (the
direct runtime write and `presetMorph_requestVoice()`):

```c
    if (scene_index == scene_getActiveIndex()) {
        /*
         * Apply the edited parameter's new interpolation (S075 automation
         * fix, user rules).
         *
         * A menu edit only sets an endpoint. The sound follows the
         * interpolation at the voice's resolved Morph amount, never the raw
         * endpoint, and a parameter held by step automation keeps its
         * automation value until the next trigger (automation always
         * wins; the trigger restore then applies the new interpolation).
         * Only this parameter's interpolation changes, so the whole voice is
         * not queued for the Morph worker: other parameters, including
         * automated ones, are not rewritten.
         * Affiliates: presetMorph_applyParameterNow(),
         * seq_restoreAutomatedParameters().
         */
        preset_ensureMorphInitialized();
        presetMorph_applyParameterNow(scene_index, slot, descriptor_index);
    }
    return 1u;
```

Block above the function (in its "Typed endpoint setter" comment): replace
"schedules Morph interpolation so the runtime image and DSP backend follow
the Scene state" with "re-interpolates only the edited parameter so the
runtime follows the Morph interpolation; automation-held parameters keep
their automation value until the next trigger".

### R-11 — `presetManager.h` line 373 and `menu.c` lines 3809–3815 — MODIFY

`presetManager.h`: drop `uint8_t record_automation` from the declaration and
add to its block: "Edits set an endpoint only: the active Scene's runtime
takes the re-interpolated value of the edited parameter, unless step
automation holds it (S075 automation fix)."

`menu.c` `menu_cellCommitValue()`, MENU_CELL_INSTRUMENT loop:

```c
                changed |= preset_setInstrumentParameter(
                    scene_index, cell->slot, cell->descriptor_index,
                    voiceModeShowMorph ? INSTRUMENT_IMAGE_MORPH
                                       : INSTRUMENT_IMAGE_MAIN,
                    (uint8_t)value);
```

### R-12 — comment updates

| File | Anchor | Text |
|---|---|---|
| `sequencer.c` line ~230, block above `seq_automation_dirty[]` | "Lifetime: …" | add "Morph-base writers consult it through seq_automationHoldsParameter() so a held value is not overwritten before the trigger (S075 automation fix)." |
| `knowledge_files/specification_reference/BANK_PRESET_ARCHITECTURE.md` | Morph worker / step automation section | one paragraph stating the rule in §5 |
| `S075_PH6_COPYCLEAR_IMPLEMENTATION.md` | §14 | short log entry pointing here |

## 6. Decisions and follow-ups

### 6.1 Decided (user, revision 2)

- **Q1 — automation always wins.** A menu edit never changes the runtime of
  a parameter held by step automation (R-04, R-10). The menu marks
  automated parameters, so an edit with no audible change is explained on
  screen. A menu edit only sets an endpoint: the runtime takes the
  interpolation at the resolved Morph amount, never the raw value (R-09,
  R-10). With Morph above 0 the edit changes the sound by less than the edit,
  which is correct.
- **Q2 — instrument type change.** Out of scope ("all bets are off"). A held
  bit may name a parameter of the old type until the next trigger. No
  change.
- **Q3 — restore order at transport boundaries.** Both orders are correct.
  If `presetMorph_applyVoiceNow()` (`sequencer.c` 380) runs while the bits
  are still set, it skips the held parameters and `seq_restoreAllAutomation()`
  then writes the same new base; after the clear, nothing is skipped. No
  change.

### 6.2 Problems found while folding in (for the user)

- **P1 — MIDI CC overrides automation and Morph.** `midiParser_writeTaggedRuntime()`
  (`MidiParser.c` lines 95–106) writes an incoming CC value straight to the
  runtime. It replaces a held automation value and the Morph interpolation
  until the next worker write or trigger. That breaks both rules above for
  external control. **Decided (user, 2026-10-03): MIDI takes the lowest
  priority.** A CC only enters the parameter (stored Normal endpoint of the
  active Scene); the Morph sweep applies it whenever it gets to it, and the
  sweep skips automation-held parameters. Implementation:
  `S075_F3_AUTOMATION_BUGFIX_IMPLEMENTATION.md` Stages C–E.
- **P1 status:** implemented in S075 F3. External MIDI now enters the active
  Scene endpoint at the lowest priority; internal legacy CC calls retain their
  runtime-only behavior.
- **P2 — the menu edit no longer queues the whole voice.** Today every edit
  re-walks every morphable parameter of the voice, which is what clobbered
  the automation. After R-10 an edit re-interpolates only the edited
  parameter, which is the only one whose interpolation can change. Morph,
  LFO, `Nvm`, Scene and load paths still queue the worker as before (with
  the R-04 guard). Side effects:
  - an edit is heard at once in both views;
  - the Morph-view endpoint edit no longer waits for the worker's pass.
- **P3 — edits to other Scenes in the edit mask** store the endpoint only, as
  before. Their interpolation is rebuilt when that Scene becomes active
  (Scene apply queues all voices).

## 7. Resources

RAM 0 B. Flash about +150 B (R-01 … R-12). CPU: one bit test per
Morph-worker parameter write; a menu edit now does one interpolation
instead of queueing up to 64 parameter writes.

## 8. Verification

| # | Case | Expected |
|---|---|---|
| 1 | Coarse pitch automated on a long note; edit pan, filter cutoff or decay of the same voice while it sounds | pitch stays at its automated value until the next trigger; the edited parameter changes at once |
| 2 | Same with voice Morph above 0, Normal-view and Morph-view edits | same; the edited parameter changes by its interpolated amount, not the raw edit |
| 3 | Same, turning the global Morph and the voice's `vm` | automated pitch holds; other parameters morph |
| 4 | LFO on the voice's Morph plus coarse pitch automation | pitch follows the automation, not the LFO-driven base, until each trigger |
| 5 | `Nvm` and coarse pitch automated on the same step | both apply; pitch is not overwritten a few ticks later |
| 6 | Next trigger without automation on that step | pitch returns to the (newly edited or morphed) base |
| 7 | Edit the automated parameter itself while it is held | no audible change until the next trigger; then the new endpoint's interpolation, or the next step's automation |
| 8 | Retained voice Morph 0, `Nvm` step override 200 active; Normal-view edit of filter cutoff | the cutoff moves to the interpolation at 200, not to the raw edited value |
| 9 | Morph-view edit with voice Morph 255 | heard at once |
| 10 | Transport stop/start, Pattern change | all overlays restored to the base; no stuck values |
| 11 | Effect parameter automation and `Nfx`/`Nou` while editing | unchanged (already correct) |
| 12 | `make all` | clean; RAM unchanged |
