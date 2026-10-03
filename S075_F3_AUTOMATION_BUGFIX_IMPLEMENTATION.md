# S075 F3 — Automation bug fix: Implementation Schedule

Code schedule for `S075_F3_AUTOMATION_BUG.md` (revision 2) and the user's
P1 decision (2026-10-03). This document is the implementation record as well
as the schedule; code and verification notes are appended to §11.

- **Baseline:** working tree of `dev-ph6-copyclear` after the F2
  implementation and the overlay follow-up (`ee3c665` + follow-up). Every line
  number below was read from that tree on 2026-10-03 and is given with a
  function or text anchor. When lines move during the pass, use the anchor.
- **Comment blocks:** every code change carries its block. **(.h)** blocks are
  the contract and go above the declaration; **(.c)** blocks go above the
  definition or inside the function as shown and state the implementation
  detail, pointing to the `.h` contract instead of repeating it.
- **Change ids:** `<stage>-<nn>`. **ADD** = new code, **MODIFY** = replace the
  quoted lines, **REMOVE** = delete the quoted lines.
- **Order:** Stage A → E. A (Sequencer accessor) comes first; B (Morph engine)
  uses it; C (Preset) uses B; D (Menu) and E (MIDI) use C. Stage F is
  documentation.
- **No commits** are part of this schedule.

## Contents

0. Rules and decisions
1. RAM, stack and flash ledger
2. Stage A — `sequencer.c/.h`: report a held automation value
3. Stage B — `presetMorphEngine.c/.h`: guarded base write, per-parameter apply
4. Stage C — `presetManager.c/.h`: menu edit applies one parameter; MIDI entry
5. Stage D — `menu.c/.h`: setter call; value clamp for MIDI
6. Stage E — `MidiParser.c`: external CCs enter the parameter
7. Stage F — documentation
8. Build and verification
9. Risks and notes
10. Change index
11. Work log

---

## 0. Rules and decisions

### 0.1 The three priority rules (user)

1. **Automation always wins.** While step automation holds a parameter
   (`seq_automation_dirty[slot]` bit set), nothing changes that parameter's
   runtime value until the voice's next trigger. Not the Morph sweep, not a
   menu edit, not MIDI. The menu marks automated parameters, so an edit with
   no audible change is explained on screen.
2. **A menu edit only sets an endpoint.** The runtime takes the edited
   parameter's interpolation at the voice's resolved Morph amount, never the
   raw edited value. With Morph above 0 the sound changes by less than the
   edit; that is correct. The edit is applied at once.
3. **MIDI takes the lowest priority (P1).** An incoming CC only enters the
   parameter: it stores the endpoint and queues the voice. The Morph sweep
   applies it whenever it reaches that parameter, and the sweep skips
   parameters held by automation.

An instrument type change while values are held is out of scope ("all bets
are off", Q2).

### 0.2 Decisions taken in the deep dive (check these)

| # | Item | Where |
|---|---|---|
| 0.2.1 | **"Enter the parameter" means the active Scene's Normal endpoint.** MIDI has no Morph view and no edit mask, so a CC stores only the Normal endpoint of the active Scene. It is retained like a menu edit: the AutoSave cell is marked and the card-clean bit is cleared, so CC values are saved. Before this change a CC was runtime-only and was lost on reload or Scene switch. | C-04, E-04 |
| 0.2.2 | **Only external MIDI changes path.** `midiParser_ccHandler()` is also called internally by the legacy flat-parameter path `preset_applySoundParameter()` (menu.c line 814; buttonHandler.c lines 574/577, armed-step reset). Those keep their runtime-only write. External CCs (global-channel CC, MidiParser.c line ~1837, plus NRPN data entry reached from it) take the new path. The distinction is passed as an argument, so no RAM is added. | E-01 … E-05 |
| 0.2.3 | **MIDI values are clamped to the parameter's domain before they are stored.** Before, a raw 0..127 CC only reached the runtime. Now it is retained, so 127 sent to an on/off or list parameter would persist an out-of-range byte in the Scene, AutoSave and Kit files. The menu already owns the descriptor-domain clamp (`menu_clampCellValue()`); a small public wrapper exposes it for one instrument parameter. | D-02, D-03, E-04 |
| 0.2.4 | **Non-morphable parameters from MIDI** (for example `lfo_wave`, `lfo_sync`, `velo_vol_on_off`, if non-morphable in the registry) are entered through `preset_setSupplementalParameter()`, the same path as a menu edit. It stores the value and writes the runtime directly, because the Morph sweep never visits non-morphable cells. These parameters cannot be automated (`instrumentManager_targetValid()` requires MORPHABLE), so rule 1 is not involved. | C-04 |
| 0.2.5 | **A menu edit no longer queues the whole voice.** Only the edited parameter's interpolation can change, so `presetMorph_applyParameterNow()` handles that one parameter. Morph, LFO, `Nvm`, Scene and load paths still queue the sweep, now with the guard. | B-05, C-01 |
| 0.2.6 | **`record_automation` is removed** from `preset_setInstrumentParameter()`. It only selected the old raw direct write; the internal `recordAutomation` arguments of the legacy path stay as they are. | C-01, C-02, D-01 |
| 0.2.7 | **No menu repaint on MIDI input.** Stored values change, so a VOICE page showing that parameter shows the new value at its next repaint (encoder/pot/page action or live refresh). A CC stream does not drive the LCD (lowest priority). | E-04 |

---

## 1. RAM, stack and flash ledger

| Item | Bytes |
|---|---:|
| SRAM (all sections) | **0** |
| Stack: `menu_clampInstrumentValue()` builds one `menu_cell_t` (~24 B) on the stack, MIDI path only | ~24 |
| Flash (estimate) | +0.4 kB |

CPU:
- One bit test per Morph-sweep parameter write.
- A menu edit now does one interpolation instead of queueing up to 64
  parameter writes.
- A MIDI CC adds a key lookup (already done before), a clamp and an
  endpoint compare.

---

## 2. Stage A — `Core/Sequencer/sequencer.c` / `.h`: report a held automation value

### A-01 — `Core/Sequencer/sequencer.h` after line 155 (`seq_restoreAutomatedParameters`) — ADD (.h)

```c
/*
 * Report whether step automation currently holds one voice parameter
 * (S075 F3).
 *
 * What: nonzero when seq_automation_dirty[slot] has bit `local` set, that is,
 * seq_drainPendingAutomation() wrote a step value into this descriptor's
 * runtime and the voice has not been triggered since.
 * Why: automation always wins (user rule). The overlay lasts until the next
 * trigger, so writers of the Morph base update morph_interpolation[] but
 * leave a held runtime value alone; seq_restoreAutomatedParameters() then
 * applies the new base at the trigger.
 * Inputs: instrument slot 0..5 (track 7 uses slot 5), descriptor-local index
 * 0..INSTRUMENT_PARAM_COUNT-1. Output: 0/1; 0 for out-of-range input.
 * Context: foreground only. The drain, the trigger funnel
 * (voiceControl_processPending()), the transport restores and the Morph sweep
 * all run in the main loop, so the 64-bit bitmap is never read half-written.
 * Clients: presetMorph_writeRuntimeBase() (presetMorphEngine.c).
 * Affiliates: seq_drainPendingAutomation(), seq_restoreAutomatedParameters(),
 * seq_restoreAllAutomation().
 */
uint8_t seq_automationHoldsParameter(uint8_t slot, uint8_t local);
```

### A-02 — `Core/Sequencer/sequencer.c` after `seq_restoreAutomatedParameters()` (ends line 1129) — ADD (.c)

```c
uint8_t seq_automationHoldsParameter(uint8_t slot, uint8_t local)
{
    /*
     * Contract in sequencer.h. Read-only view of the overlay bitmap;
     * INSTRUMENT_PARAM_COUNT is 64, one bit per descriptor-local index.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT || local >= INSTRUMENT_PARAM_COUNT)
        return 0u;
    return (uint8_t)((seq_automation_dirty[slot] >> local) & 1u);
}
```

### A-03 — `sequencer.c` lines 228–239, block above `seq_automation_dirty[]` — MODIFY

Append after "Affiliate: seq_drainPendingAutomation() and
seq_restoreAutomatedParameters().":

```c
 * S075 F3: the bitmap is also the "automation holds this value" record that
 * enforces "automation always wins": the Morph sweep, the per-parameter menu
 * apply and synchronous voice applies consult it through
 * seq_automationHoldsParameter() and never overwrite a held runtime value
 * before the trigger.
```

---

## 3. Stage B — `Core/Bank/Scene/Preset/presetMorphEngine.c` / `.h`

### B-01 — `presetMorphEngine.c` includes (lines 1–5) — ADD

```c
#include "sequencer.h"
```

(`presetManager.c` already includes it, line 39.)

### B-02 — `presetMorphEngine.c` before `presetMorph_tick()` (line 439) — ADD (.c, static)

```c
/*
 * Write one Morph-derived base value to the voice runtime, unless step
 * automation holds that parameter (S075 F3).
 *
 * What: for the active Scene, applies `value` through
 * preset_applyInstrumentRuntimeValue(), except when
 * seq_automationHoldsParameter(slot, local) is set. The caller has already
 * stored `value` in morph_interpolation[local], which is what the next
 * trigger restores, so a held parameter picks up the new base at that trigger
 * instead of losing its automation mid-note.
 * Why: automation always wins (user rule). A menu edit, a Morph change, an LFO
 * on Morph, `Nvm` automation, a MIDI CC or an Instrument/Kit apply can queue
 * the whole voice; before S075 F3 every automated parameter of that voice
 * snapped back to its base while the note was still sounding.
 * Inputs: Scene, slot 0..5, descriptor-local index, interpolated value.
 * Output: one runtime write, or none (inactive Scene, held parameter).
 * Callers: presetMorph_tick(), presetMorph_applyVoiceNow(),
 * presetMorph_applyParameterNow(). Affiliates: seq_restoreAutomatedParameters(),
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

### B-03 — `presetMorph_tick()` lines 490 and 501–504 — MODIFY / REMOVE

REMOVE the local at line 490:

```c
            instrument_param_id_t id;
```

MODIFY lines 501–504:

```c
            id = instrumentParam_make(morph_worker.slot, local);
            if (morph_worker.scene_index == scene_getActiveIndex())
                preset_applyInstrumentRuntimeValue(morph_worker.scene_index,
                                                   id, value);
```

to

```c
            /* Held automation keeps its runtime value until the trigger. */
            presetMorph_writeRuntimeBase(morph_worker.scene_index,
                                         morph_worker.slot, local, value);
```

### B-04 — `presetMorph_applyVoiceNow()` lines ~595 and 606–608 — MODIFY / REMOVE; block lines 566–572 — MODIFY

REMOVE the loop local `instrument_param_id_t id;`. MODIFY:

```c
        id = instrumentParam_make(slot, local);
        if (scene_index == scene_getActiveIndex())
            preset_applyInstrumentRuntimeValue(scene_index, id, value);
```

to

```c
        /*
         * Held automation keeps its runtime value (S075 F3). At trigger time
         * preset_applyDeferredSceneSlotForTrigger() is followed at once by
         * seq_restoreAutomatedParameters(), which writes the new base from
         * morph_interpolation[]; at the quiet threshold and at transport
         * restore, the next trigger or seq_restoreAllAutomation() does.
         */
        presetMorph_writeRuntimeBase(scene_index, slot, local, value);
```

Function block: append to the paragraph ending "…cannot fire with
half-old instrument parameters.":

```c
     * Parameters held by step automation keep their runtime value; only
     * morph_interpolation[] is updated for them (S075 F3,
     * presetMorph_writeRuntimeBase()).
```

### B-05 — `presetMorphEngine.h` after line 130 (`presetMorph_getResolvedVoiceAmount`) — ADD (.h)

```c
/*
 * Re-interpolate and apply one voice parameter now (S075 F3).
 *
 * What: after a menu edit of one Normal or Morph endpoint, computes that
 * parameter's interpolated value with the amount the voice is playing with
 * (presetMorph_getResolvedVoiceAmount(): step override or retained amount,
 * plus any LFO layer on the active Scene), stores it in
 * morph_interpolation[local], and writes it to the runtime unless step
 * automation holds the parameter.
 * Why: user rules. A menu edit only sets an endpoint, and the sound follows
 * the interpolation, never the raw edited value, so with Morph above 0 the
 * sound changes by less than the edit. Automation always wins. Only this
 * parameter's interpolation can change, so the whole voice is not queued and
 * no other parameter is rewritten. The edit is heard at once in both views.
 * Inputs: resident Scene, slot 0..5, descriptor-local index of a morphable
 * parameter. Outputs: morph_interpolation[local] (any Scene); a runtime write
 * (active Scene, parameter not held). Non-morphable or invalid input: no-op.
 * Client: preset_setInstrumentParameter() (menu edits). Affiliates:
 * presetMorph_tick() (same maths), seq_automationHoldsParameter().
 */
void presetMorph_applyParameterNow(uint8_t scene_index, uint8_t slot,
                                   uint8_t local);
```

### B-06 — `presetMorphEngine.c` after `presetMorph_getResolvedVoiceAmount()` (ends line 741) — ADD (.c)

```c
void presetMorph_applyParameterNow(uint8_t scene_index, uint8_t slot,
                                   uint8_t local)
{
    scene_t *scene = scene_get(scene_index);
    kit_instrument_slot_t *instrument;
    const ParamDescriptor *descriptor;
    instrument_param_value_t value;

    /*
     * Contract in presetMorphEngine.h. Same interpolation as the sweep
     * (presetMorph_interpolate() over the retained Normal/Morph images) at
     * the resolved amount, so the sweep later writes the same value.
     */
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

`presetMorph_writeRuntimeBase()` (B-02) and `presetMorph_interpolate()`
(line 82) are defined above it.

---

## 4. Stage C — `Core/Bank/Scene/Preset/presetManager.c` / `.h`

### C-01 — `presetManager.c` lines 906–963, `preset_setInstrumentParameter()` — MODIFY

Signature (lines 906–910):

```c
uint8_t preset_setInstrumentParameter(uint8_t scene_index, uint8_t slot,
                                      uint8_t descriptor_index,
                                      instrument_image_select_t image,
                                      uint8_t value)
```

"Typed endpoint setter" block (lines 924–935): replace "and schedules Morph
interpolation so the runtime image and DSP backend follow the Scene state"
with:

```c
     * and re-interpolates only the edited parameter, so the runtime follows
     * the Morph interpolation and never the raw edited value; a parameter held
     * by step automation keeps its automation value until the next trigger
     * (S075 F3 user rules: a menu edit only sets an endpoint; automation
     * always wins).
```

Body: replace lines 937–961 (the `if (scene_index == scene_getActiveIndex())`
block containing the `record_automation` direct write and
`presetMorph_requestVoice()`) with:

```c
    if (scene_index == scene_getActiveIndex()) {
        /*
         * Apply the edited parameter's new interpolation now (S075 F3).
         *
         * Inputs: the endpoint stored above. Output: morph_interpolation[]
         * and, unless automation holds the parameter, the runtime value of
         * this one parameter at the voice's resolved Morph amount. The former
         * raw write (retained Morph amount 0 only) is gone: it ignored the
         * automation overlay and a `Nvm`/LFO Morph amount. The whole voice is
         * no longer queued: no other parameter's interpolation changes, and
         * re-walking the voice is what overwrote automated parameters.
         * Affiliates: presetMorph_applyParameterNow(),
         * seq_restoreAutomatedParameters().
         */
        preset_ensureMorphInitialized();
        presetMorph_applyParameterNow(scene_index, slot, descriptor_index);
    }
    return 1u;
```

The local `scene_t *scene` inside the removed block goes with it.

### C-02 — `presetManager.h` lines 373–377 — MODIFY declaration; block lines 348–372 — MODIFY

```c
uint8_t preset_setInstrumentParameter(uint8_t scene_index, uint8_t slot,
                                      uint8_t descriptor_index,
                                      instrument_image_select_t image,
                                      uint8_t value);
```

Append to the block (after the "Retained mutation contract" paragraph):

```c
 * Runtime contract (S075 F3): an endpoint edit on the active Scene applies
 * only the edited parameter's interpolation at the voice's resolved Morph
 * amount (presetMorph_applyParameterNow()), never the raw value, and never
 * over a parameter held by step automation (automation always wins; the
 * next trigger applies the new interpolation).
```

### C-03 — `presetManager.h` after `preset_setSupplementalParameter()` (line 378) — ADD (.h)

```c
/*
 * Enter one instrument parameter from external MIDI (S075 F3, user P1:
 * MIDI takes the lowest priority).
 *
 * What: stores `value` as the active Scene's Normal endpoint of one
 * descriptor and queues that voice for the Morph sweep. Nothing is written
 * to the runtime here: the sweep applies the new interpolation whenever it
 * reaches the parameter, and skips it while step automation holds it.
 * Non-morphable parameters (never automatable, never swept) go through
 * preset_setSupplementalParameter(), which stores and applies them, as a
 * menu edit does.
 * Why: automation wins, then menu edits, then MIDI. A CC is an endpoint
 * entry, not a runtime override, so it can override neither automation nor
 * the Morph interpolation.
 * Inputs: slot 0..5, descriptor-local index for the active Scene's slot type,
 * and a value already clamped to the descriptor domain
 * (menu_clampInstrumentValue()). Output: 1 when stored/queued, 0 for an
 * invalid slot/descriptor. Retention: a changed byte marks its AutoSave
 * Normal cell and clears the Scene's card-clean bit, like a menu edit.
 * Active Scene only (no edit-mask fan-out). Caller: MidiParser.c
 * midiParser_enterTaggedParameter(). Affiliates:
 * preset_storeInstrumentEndpoint(), presetMorph_requestVoice(),
 * seq_automationHoldsParameter().
 */
uint8_t preset_setInstrumentParameterFromMidi(uint8_t slot,
                                              uint8_t descriptor_index,
                                              uint8_t value);
```

### C-04 — `presetManager.c` after `preset_setSupplementalParameter()` (ends line ~999) — ADD (.c)

```c
uint8_t preset_setInstrumentParameterFromMidi(uint8_t slot,
                                              uint8_t descriptor_index,
                                              uint8_t value)
{
    const uint8_t scene_index = scene_getActiveIndex();
    const kit_instrument_slot_t *instrument =
        scene_instrumentSlotConst(scene_index, slot);
    const ParamDescriptor *descriptor;

    /*
     * Contract in presetManager.h. Lowest priority: store and queue only.
     * The sweep's guarded write (presetMorph_writeRuntimeBase()) decides
     * when, and whether, the runtime changes.
     */
    if (!instrument)
        return 0u;
    descriptor = instrumentManager_descriptor(instrument->type,
                                              descriptor_index);
    if (!descriptor)
        return 0u;
    if (!(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE))
        return preset_setSupplementalParameter(scene_index, slot,
                                               descriptor_index, value);
    preset_storeInstrumentEndpoint(scene_index, slot, descriptor_index,
                                   INSTRUMENT_IMAGE_MAIN, value);
    preset_ensureMorphInitialized();
    presetMorph_requestVoice(scene_index, slot);
    return 1u;
}
```

`preset_storeInstrumentEndpoint()` (line 866) and
`preset_ensureMorphInitialized()` (line 700) are static and defined above.
`preset_setSupplementalParameter()` returns 0 for a non-morphable
instance-offset descriptor (line 978), exactly as for a menu edit; see §9.

---

## 5. Stage D — `Core/Menu/menu.c` / `.h`

### D-01 — `menu.c` lines 3809–3815, `menu_cellCommitValue()` MENU_CELL_INSTRUMENT loop — MODIFY

```c
            if (cell->descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE) {
                /*
                 * S075 F3: an edit sets the endpoint shown by the view; the
                 * runtime follows the interpolation, and automation-held
                 * parameters are not touched (preset_setInstrumentParameter()).
                 */
                changed |= preset_setInstrumentParameter(
                    scene_index, cell->slot, cell->descriptor_index,
                    voiceModeShowMorph ? INSTRUMENT_IMAGE_MORPH
                                       : INSTRUMENT_IMAGE_MAIN,
                    (uint8_t)value);
```

(The removed last argument was
`(uint8_t)(!voiceModeShowMorph && scene_index == scene_getActiveIndex())`.)

### D-02 — `menu.h` near the other public helpers (after line 463, the overlay declarations) — ADD (.h)

```c
/*
 * Clamp one value to an instrument parameter's menu domain (S075 F3).
 *
 * What: applies the same descriptor-domain clamp as a VOICE-page edit
 * (menu_clampCellValue() for an instrument cell: dtype ranges, list sizes,
 * on/off, target-selector tokens, LFO target voice range) to one parameter
 * of the active Scene's slot.
 * Why: external MIDI now enters values into the Scene (stored, saved); a raw
 * 0..127 CC must not persist an out-of-domain byte, for example 127 on an
 * on/off parameter. The clamp rules live in Menu; this wrapper keeps one
 * copy of them.
 * Inputs: slot 0..5, descriptor-local index, raw value 0..255. Output: the
 * clamped value, or the input unchanged when the slot/descriptor is invalid.
 * Caller: MidiParser.c midiParser_enterTaggedParameter(). Affiliates:
 * menu_clampCellValue(), menu_cellDtype().
 */
uint8_t menu_clampInstrumentValue(uint8_t slot, uint8_t descriptor_index,
                                  uint8_t value);
```

(`menu.h` does not include `InstrumentManager.h`; the wrapper takes indices so
that it does not need the anonymous `ParamDescriptor` type.)

### D-03 — `menu.c` after `menu_clampCellValue()` (lines 4561–4685) — ADD (.c)

```c
uint8_t menu_clampInstrumentValue(uint8_t slot, uint8_t descriptor_index,
                                  uint8_t value)
{
    const kit_instrument_slot_t *instrument =
        scene_instrumentSlotConst(scene_getActiveIndex(), slot);
    menu_cell_t cell;
    uint16_t clamped = value;

    /*
     * Contract in menu.h. A transient instrument cell carries only what the
     * clamp reads: kind, slot, descriptor index and descriptor (dtype,
     * runtime kind for the LFO target checks).
     */
    if (!instrument)
        return value;
    memset(&cell, 0, sizeof(cell));
    cell.kind = MENU_CELL_INSTRUMENT;
    cell.slot = slot;
    cell.descriptor_index = descriptor_index;
    cell.descriptor = instrumentManager_descriptor(instrument->type,
                                                   descriptor_index);
    if (!cell.descriptor)
        return value;
    menu_clampCellValue(&cell, &clamped);
    return (uint8_t)clamped;
}
```

`menu.c` already uses `memset()`, `scene_instrumentSlotConst()` and
`instrumentManager_descriptor()`.

---

## 6. Stage E — `Core/MIDI/MidiParser.c`: external CCs enter the parameter

The CC path today: global-channel CC (line ~1837) → `midiParser_ccHandler()`
(line 451) → `midiParser_applyTaggedInstrumentCc()` (lines 119–260) →
`midiParser_writeTaggedRuntime()` (lines 95–106), a direct runtime write.
`midiParser_ccHandler()` is also called by the legacy internal path
(`preset_applySoundParameter()`, presetManager.c line 791), so the external
origin is carried as an argument (0.2.2).

### E-01 — `MidiParser.c` before `midiParser_nrpnHandler()` (line 441) — ADD forward declaration

```c
/*
 * CC dispatcher shared by external MIDI input and the legacy internal path
 * (S075 F3). `external` is nonzero only for CCs received from MIDI input:
 * they enter the parameter (lowest priority); internal legacy calls keep the
 * runtime-only write. Public entry for internal callers:
 * midiParser_ccHandler().
 */
static void midiParser_ccDispatch(MidiMsg msg, uint8_t updateOriginalValue,
                                  uint8_t external);
```

### E-02 — `midiParser_nrpnHandler()` lines 441–448 — MODIFY

```c
static void midiParser_nrpnHandler(uint16_t value, uint8_t external)
{
	MidiMsg msg2;
	msg2.status = MIDI_CC2;
	msg2.data1 = midiParser_activeNrpnNumber;
	msg2.data2 = value;
	/* S075 F3: an NRPN keeps the origin of its data-entry CC. */
	midiParser_ccDispatch(msg2, true, external);
}
```

The call at line 488 is inside `#if 0` (retired field map) and is not
touched.

### E-03 — `midiParser_ccHandler()` line 451 and its body — MODIFY

Rename the definition to the dispatcher, then add the unchanged public
wrapper after it (signature in `MidiParser.h` line 53 stays):

```c
static void midiParser_ccDispatch(MidiMsg msg, uint8_t updateOriginalValue,
                                  uint8_t external)
{
	/* … existing body of midiParser_ccHandler(), with these three edits: … */
```

- line 461: `midiParser_nrpnHandler(msg.data2, external);`
- line 475: `midiParser_applyTaggedInstrumentCc(0u, msg.data1, msg.data2, external);`
- line ~1118 (CC2 branch): `midiParser_applyTaggedInstrumentCc(1u, msg.data1, msg.data2, external);`

After the dispatcher's closing brace:

```c
/*
 * Internal CC entry (legacy flat sound parameters, armed-step reset).
 *
 * Inputs/outputs as before S075 F3: instrument keys get a runtime-only
 * write. External MIDI input uses midiParser_ccDispatch(…, 1) instead, which
 * enters the parameter (MIDI takes the lowest priority).
 * Callers: preset_applySoundParameter().
 */
void midiParser_ccHandler(MidiMsg msg, uint8_t updateOriginalValue)
{
	midiParser_ccDispatch(msg, updateOriginalValue, 0u);
}
```

### E-04 — `MidiParser.c` after `midiParser_writeTaggedRuntime()` (ends line 106) — ADD (.c, static)

```c
/*
 * Enter one external MIDI CC into an instrument parameter (S075 F3, user P1).
 *
 * What: resolves the CC's descriptor key on the active Scene's slot type,
 * clamps the value to the parameter's menu domain, and hands it to
 * preset_setInstrumentParameterFromMidi(): the Normal endpoint is stored and
 * the voice is queued; the Morph sweep applies the interpolation whenever it
 * reaches the parameter and skips it while step automation holds it.
 * Why: MIDI takes the lowest priority: automation, then menu edits, then
 * MIDI. A direct runtime write (midiParser_writeTaggedRuntime()) overrode
 * both automation and the Morph interpolation.
 * Inputs: visible slot 0..5, descriptor file key, MIDI value 0..127.
 * Output: stored endpoint (retained, AutoSave-marked) or nothing when the
 * slot's type has no such key. The LCD is not repainted here (0.2.7).
 * Caller: midiParser_applyTaggedInstrumentCc() with external != 0.
 * Affiliates: instrumentManager_descriptorIndexByKey(),
 * menu_clampInstrumentValue(), preset_setInstrumentParameterFromMidi().
 */
static void midiParser_enterTaggedParameter(uint8_t slot, const char *key,
											   uint8_t value)
{
	const kit_instrument_slot_t *instrument =
		scene_instrumentSlotConst(scene_getActiveIndex(), slot);
	uint8_t index = 0xffu;

	if (!instrument || !key)
		return;
	if (!instrumentManager_descriptorIndexByKey(instrument->type, key, &index) ||
		index >= INSTRUMENT_PARAM_COUNT)
		return;
	(void)preset_setInstrumentParameterFromMidi(
		slot, index, menu_clampInstrumentValue(slot, index, value));
}
```

All the needed headers are already included (`SceneData.h`,
`InstrumentManager.h`, `presetManager.h`, `menu.h`).

### E-05 — `midiParser_applyTaggedInstrumentCc()` lines 109–122 and 259 — MODIFY

Block (lines 109–118): append

```c
 * S075 F3: `external` selects the destination. External MIDI enters the
 * parameter (midiParser_enterTaggedParameter(): stored endpoint, applied by
 * the Morph sweep; automation and menu edits win). The internal legacy path
 * keeps the runtime-only write (midiParser_writeTaggedRuntime()).
```

Signature:

```c
static void midiParser_applyTaggedInstrumentCc(uint8_t cc2, uint8_t cc,
													 uint8_t value,
													 uint8_t external)
```

Last statement (line 259):

```c
	if (external)
		midiParser_enterTaggedParameter(slot, key, value);
	else
		midiParser_writeTaggedRuntime(slot, key, value);
```

Block of `midiParser_writeTaggedRuntime()` (lines 86–94): append "Internal
legacy callers only since S075 F3; external MIDI uses
midiParser_enterTaggedParameter()."

### E-06 — `MidiParser.c` line ~1837, global-channel CC in `midiParser_parseMidiMessage()` — MODIFY

```c
				} else if(chanonly == midi_MidiChannels[7]) {
					/*
					 * Global-channel CC from MIDI input (S075 F3): instrument
					 * keys enter the parameter at the lowest priority
					 * (stored endpoint, applied by the Morph sweep; automation
					 * and menu edits win). Menu owns the active voice;
					 * Sequencer owns the recording gate.
					 */
					midiParser_ccDispatch(msg, 1u, 1u);
				}
```

The CC1 mod-wheel branch above it (Morph amount) is unchanged; it queues
voices through the guarded sweep.

---

## 7. Stage F — documentation

| File | Anchor | Change |
|---|---|---|
| `knowledge_files/specification_reference/BANK_PRESET_ARCHITECTURE.md` | Morph worker / step automation section | add "Priority (S075 F3): step automation, then menu edits, then MIDI. Held automation is never overwritten before the voice's next trigger; a menu edit sets an endpoint and applies that parameter's interpolation at once; a MIDI CC stores the Normal endpoint of the active Scene and the Morph sweep applies it." |
| same | MIDI section (if present) / FILESYSTEM_SPEC "Morph, Modulation, and Automation" (line ~1721) | MIDI CCs are now retained endpoint entries (saved, AutoSave-marked), clamped to the menu domain |
| `S075_F3_AUTOMATION_BUG.md` | §6.2 P1 | mark decided: "MIDI takes the lowest priority: enter the parameter only; the Morph sweep applies it" and point here |
| `S075_PH6_COPYCLEAR_IMPLEMENTATION.md` | §14 | add "§14.9 F3 automation fix" pointing to this schedule and its §11 |

---

## 8. Build and verification

### 8.1 Build

1. `make all`: clean apart from the existing newlib/LTO warnings. Watch for
   unused `id` locals (B-03, B-04) and all callers of the changed signatures
   (`preset_setInstrumentParameter()`, `midiParser_nrpnHandler()`,
   `midiParser_applyTaggedInstrumentCc()`).
2. `arm-none-eabi-size`: bss unchanged (426,712 B baseline).
3. Registry check before E-04 is relied on: every key in
   `midiParser_applyTaggedInstrumentCc()` resolves, for each instrument type
   that has it, to a morphable descriptor or a non-instance-offset
   supplemental descriptor. Otherwise that CC becomes a no-op, the same as a
   menu edit of it (§9). A grep over the four `*Parameters.c` registries is
   enough; record the result in §11.

### 8.2 Hardware

| # | Case | Expected |
|---|---|---|
| 1 | Coarse pitch automated on a long note; edit pan, cutoff or decay of the same voice while it sounds | pitch holds its automated value until the next trigger; the edited parameter changes at once |
| 2 | Same with voice Morph above 0, Normal view and Morph view | same; the edited parameter moves by its interpolated amount, not the raw edit |
| 3 | Same while turning global Morph and the voice's `vm` | automated pitch holds; other parameters morph |
| 4 | LFO on the voice's Morph plus coarse pitch automation | pitch follows the automation until each trigger |
| 5 | `Nvm` and coarse pitch automated on the same step | both apply; pitch is not overwritten a few ticks later |
| 6 | Edit the automated parameter itself while it is held | no audible change until the next trigger (automation wins); afterwards the new endpoint's interpolation or the next step's automation |
| 7 | Retained voice Morph 0, `Nvm` override 200 active; Normal-view edit of cutoff | cutoff moves to the interpolation at 200, not to the raw edit |
| 8 | Next trigger without automation | parameter returns to the (edited or morphed) base |
| 9 | MIDI CC (global channel) to coarse pitch of voice 1 while voice 1's pitch is automated and sounding | no change until the next trigger; after it, the CC's value (interpolated) unless the step automates pitch again |
| 10 | MIDI CC to cutoff, no automation, voice Morph 0 | heard after the sweep reaches it (short delay); VOICE page shows the value at its next repaint |
| 11 | MIDI CC with voice Morph 128 | sound moves to the interpolation between the CC value and the Morph endpoint |
| 12 | MIDI CC 127 to an on/off or list parameter | stored as the domain maximum; saved and restored after power cycle |
| 13 | MIDI CC, then Scene Save / power cycle | the CC value is retained (new behaviour) |
| 14 | Legacy internal path: armed automation step reset (`buttonHandler_resetLock`) | unchanged runtime-only restore |
| 15 | Transport stop/start, Pattern change | all overlays restored; no stuck values |
| 16 | Effect automation, `Nfx`/`Nou` while editing | unchanged |

---

## 9. Risks and notes

- **MIDI CCs are now saved (0.2.1).** A controller sweep changes the Scene,
  marks AutoSave and clears the card-clean bit, like turning the pot. This
  follows from "enter the parameter".
- **CC to a non-morphable instance-offset descriptor** would be dropped,
  because `preset_setSupplementalParameter()` refuses it, as it does for a
  menu edit. §8.1 item 3 checks that none of the mapped keys hits this case.
- **Sweep latency for MIDI.** The sweep writes one parameter per tick across
  the queued voice, so a CC is heard after up to a voice's worth of ticks.
  That is intended (lowest priority).
- **Deferred Scene handoff.** The MIDI key is resolved on the active Scene's
  slot type, not the outgoing runtime type, because the value is stored in
  the active Scene. The sweep applies it with the same type rules as every
  other queued write.
- **Instrument type change while values are held:** out of scope (Q2).

---

## 10. Change index

| Id | File | Kind |
|---|---|---|
| A-01 | `Core/Sequencer/sequencer.h` | ADD declaration |
| A-02, A-03 | `Core/Sequencer/sequencer.c` | ADD function, MODIFY block |
| B-01 … B-04, B-06 | `Core/Bank/Scene/Preset/presetMorphEngine.c` | ADD include, helper, function; MODIFY two write sites |
| B-05 | `Core/Bank/Scene/Preset/presetMorphEngine.h` | ADD declaration |
| C-01, C-04 | `Core/Bank/Scene/Preset/presetManager.c` | MODIFY setter; ADD MIDI entry |
| C-02, C-03 | `Core/Bank/Scene/Preset/presetManager.h` | MODIFY declaration; ADD declaration |
| D-01, D-03 | `Core/Menu/menu.c` | MODIFY call; ADD clamp wrapper |
| D-02 | `Core/Menu/menu.h` | ADD declaration |
| E-01 … E-06 | `Core/MIDI/MidiParser.c` | ADD dispatcher/entry; MODIFY NRPN, CC handler, tagged CC, global-channel CC |
| F | docs | text |

---

## 11. Work log

(To be appended during implementation: per-stage status, build sizes, the
§8.1 registry check, deviations, hardware results.)
