# S072 Step 9: Effect automation and LFO (implementation schedule)

This schedule implements Step 9 of `EFFECTS_BUS_FEATURE_PLAN.md` §17.1:

> Automation and LFO: the `fx` category, overlay apply/restore, priority
> (§9), the LFO `fx` namespace, and rebind.
> Gate: automate, sequence, and LFO one parameter together.

- **Authority:** plan §9 (resolution, F1 third restore rule, G3, G4), §10.1–10.3
  (IDs, Pattern automation, LFO), §16.1 items 4, 5 and 7, and decisions A18,
  A21, F1, F2, G1–G4 and G7.
- **Baseline:** the reviewed Step 8 tree (`S072_ST8_IMPLEMENTATION.md` §12).
  - text 478,720 B; flash 479,136 / 491,520 B;
  - **headroom 12,384 B**;
  - DTCM statics 4,448 B; FXBUF margin 3,744 B.
- **Code changes:** every change is listed below by file, current line and
  action (add / modify / remove). Each comes with the comment block to place
  in the source.
- **Line numbers** refer to the current tree. The named anchors are the
  authority, because earlier edits in the same file shift the lines below them.

---

## 0. Decisions and notes

### D1: one owner for Effect overlay state (EffectsManager)

- The plan names a sequencer-side `seq_effect_automation_dirty` (u64, §10.1,
  §16.1 item 7). Its only writer and only reader are foreground Effect code:
  - the drain applies overlays;
  - `effects_service()` resolves them;
  - the reset paths clear them.
- Step 9 therefore keeps the overlay mask with its values and owning tracks in
  one EffectsManager struct, `effects_automation.active`.
- The sequencer keeps only two bytes:
  - the owner-track mask that TIM3 reads;
  - an automation-reset latch.
- A second u64 in the sequencer would duplicate the mask and would have to be
  kept in step across Scene activation and type change. The memory is the
  same as the plan's allocation (§0.2).

### D2: how "the writing track's automation ends" is detected (F1, G3)

**Rule.** An Effect overlay on parameter *p* is owned by the last track *T*
that wrote *p*. It ends when *T* plays its next automation step and that step
does not carry *p*. Consecutive steps that carry *p* keep it held. An entry on
a non-trigger step counts, because automation is queued independently of the
trigger bit.

**Mechanism.**

- **TIM3 side.** When a track that owns at least one overlay advances, TIM3
  queues one extra pending record, the **FX step marker** (`identity` bit 11,
  payload 0). It goes immediately **before** that step's automation entries.
- **Foreground side.** `seq_drainPendingAutomation()` handles each marker
  with `effects_automationStepBegin(track)`:
  1. It first closes the previous group.
  2. It then collects the overlays owned by *T* as end candidates.
  3. Each Effect entry that follows, from any track, removes its parameter
     from the candidates and re-holds it.
  4. At the next marker, or at the end of the drain pass,
     `effects_automationStepFlush()` ends the candidates that remain.
- **Why the groups are complete.** TIM3 appends one track's marker and entries
  inside one ISR, so the foreground never sees a partial group.
- **Traffic.** Markers are queued only for owner tracks
  (`seq_effectAutomationTracks`), so there is no extra queue traffic when no
  Effect overlay exists.

**Why not end the overlay directly in TIM3.** The Effect values are applied in
the foreground drain. A TIM3-side end could run before the drain applies the
previous step's value, and the late apply would then re-open an overlay nobody
owns. Carrying the boundary through the same ordered queue makes "end" and
"apply" strictly ordered. The cost (plan risk 7) stays bounded:

- TIM3 adds one byte test per advancing track;
- the foreground walks one 64-bit mask per marker.

**Race note.** The owner byte is published by the foreground after the apply.
It is read by TIM3 at least one step later, because the drain runs every
32-frame block. A lost marker requires a queue overflow, and PatternTrace
records it. The overlay then ends one step late.

### D3: the marker uses the automation gate

Markers are queued in the same branch as `seq_queueStepAutomations()`:

- step condition passed;
- track not muted;
- not the live-erase track;
- not SOM mode.

A step whose probability fails, or a muted track, therefore holds its Effect
overlays. This is the existing rule in `seq_advanceTrackStep()` ("a failed
condition leaves previously held automation values unchanged"), and it matches
voice parameters, which are only restored by a trigger.

### D4: reset ordering (Scene rule and transport reset, G1, §9)

- `seq_fxPublishReset()` already runs on the common reset path in
  `seq_setStepIndexToStart()`:
  - transport start/stop;
  - Pattern/Scene boundary;
  - external reset.
- Step 9 sets a second one-byte latch there, `seq_effectAutomationReset`.
  The drain takes it **at the top of the pass, before any queued record**, and
  calls `effects_automationReset()`. That clears:
  - every Effect parameter overlay;
  - the pending marker group;
  - the Pattern `fxm` Morph override.
- **Why not in `effects_seqConsume()`.** The FX-sequencer RESET is consumed in
  `effects_service()`, which runs after the drain in the same block. Clearing
  overlays there would wipe the step-0 values the drain had just applied.
- **Why records are never stale.** Records queued before a reset come from an
  earlier step and have long been drained, since the drain runs every block.
  The only records after a reset belong to the new pass.
- `effects_activateScene()` also calls `effects_automationReset()`. A Scene
  change while stopped therefore never leaves an overlay.
- `effects_changeType()`, on the active Scene, clears only the parameter
  overlays. Their local indices mean nothing for the new type. `fxm` is a
  Scene-level target and keeps its override until the Scene rule clears it.

### D5: `fxm` (ID 404) goes live

- **Flags.** The SceneModTargets row gets
  `SCENE_MOD_TARGET_USE_LFO | SCENE_MOD_TARGET_USE_AUTOMATION`. It does not
  get velocity (A21). It therefore appears in the `scn` step-automation list
  and in the LFO `scn` namespace.
- **Pattern value.** The 7-bit value expands as voice Morph does (0..126 → ×2,
  127 → 255), via `effect_expand7Linear()`. `seq_applySceneAutomation()` calls
  `effects_setMorphAutomation()`, and sets its bit in
  `seq_scene_automation_dirty` like every Scene target.
- **Restore.** Restore follows the Scene rule through D4's latch.
  `seq_restoreAllSceneAutomation()` lists the kind as a no-op, so a reset in
  TIM3 never calls EffectsManager.
- **Display.** No PERF mirror is written. The Effect page `mrp` cell keeps
  showing the retained amount, as the voice PERF Morph does outside automation.
- **Menu storage.** The step editor treats `fxm` exactly like voice Morph
  (7-bit storage, expanded display, value max 127).

### D6: the LFO `fx` namespace (§10.3)

- **Namespace and token.** `lfo_target_voice = 8`
  (`INSTRUMENT_TARGET_VOICE_EFFECT`) is shown as `fx`. `lfo_target_param` is
  the Effect-local descriptor index, which must be MODULATABLE with a
  modulation domain.
- **Install.** InstrumentManager installs it as a new supplemental kind,
  `INSTALLED_MOD_TARGET_EFFECT`.
- **Per-sample update.** Each LFO sample is encoded as a **base-independent**
  direction and depth: toward min, toward max, or none, with depth 0..255.
  This is the S071 voice-Morph contract, and it is stored in EffectsManager
  per source slot and pair:
  - 6 × 2 entries of {target, direction, depth};
  - 36 B, plan item 5.
- **Resolution.** `effects_service()` sums the entries around the current
  **held** value (§9, G4) and clamps to the modulation domain.
- **Why this is exact.** `effect = base + depth·(max−base)` toward max, and
  `base − depth·(base−min)` toward min. This is the same algebra as
  `modNode_shapeParameterU16()` for positive, negative (original-LXR
  value-relative) and bipolar polarity, so Effect LFO feels the same as
  Instrument LFO.
- **`fxm` LFO.** It arrives through the `scn` namespace
  (`updateLfoSceneDestination`), uses the same table with target
  `EFFECT_LFO_TARGET_MORPH`, and resolves around the effective Morph base.
- **No restore write.** The service recomputes from the retained images every
  block, so clearing a source simply removes its entry.
- **Shared encoder.** The direction/depth encoder currently sits inline in
  InstrumentManager's voice-Morph case. Step 9 moves it into one static helper
  that both Morph kinds and the Effect kind call. The voice-Morph behavior is
  unchanged (same statements, same order), and this avoids about 150 B of
  duplicated float code on a tight flash budget.

### D7: when validation happens

- **Editing.** The Pattern writer (`pat_writeStepAutomation` →
  `instrumentManager_targetValid`, extended for AUTOMATION only) and the menu
  pickers validate against the **viewed Scene's** retained Effect type.
- **Runtime.**
  - The drain validates against the active runtime type on every apply, so a
    stale entry, for example after a type change, is ignored.
  - LFO entries apply only to rows the current type marks MODULATABLE.
- **LFO rebind.** Retained LFO tokens are normalized by the existing
  Scene-activation rebind: `preset_normalizeLfoTargetPair()` →
  `instrumentManager_lfoTargetIdFromToken()`, which gains the Effect branch.
  This satisfies "Scene activation's all-source rebind re-validates `fx`
  targets" **with no presetManager change**.
- **After a `typ` change.** A stale token contributes nothing and displays as
  `off`. It is normalized at the next rebind or edit.
- **Out of scope.** Rebinding on `typ` would mean editing Preset; that is
  outside Step 9 (G5).

### D8: WIDE8 rows

- The drain expands through `descriptor->expand7`. The value picker uses the
  inverse of the linear expansion (`menu_morphAutomationStore`) when it seeds
  a new entry, and displays the expanded value.
- `effects_paramAutomatable()` already hides WIDE8 rows without `expand7`.
- `flt` has no WIDE8 row, so this path is covered by review, not hardware.

### D9: Step 8 findings folded in

Step 9 edits the same functions, so both fixes land here:

- **ST8 F1** (`sel` lost on RESET) → §3.1.
- **ST8 F2** (held `mrp` display) → §7.8.

### D10: prerequisite fixes still outstanding (P1–P3)

These were found in the Step 6 and 7 reviews. They are relisted so they land
with this change set (§2).

### D11: `storageTypes.c` clamp

- The Instrument text parser clamps `lfo_target_voice` to 7 (`scn`).
  Without widening it to 8, an `fx` LFO destination would silently become
  `scn` on Kit/Instrument reload.
- This is the only storage touch. The value is a plain byte, and AutoSave
  stores the raw cell.

### D12: out of scope

- MIDI (A20);
- velocity to Effects (A21);
- a VOICE-style held-step automation overlay (View B) for the Effect page;
- lock removal (A15);
- edit-mask fan-out (Step 10).

### 0.1 Code outside `Core/DSP/Effects` and `menuEffects` (G5 check)

Each file below is touched only because the plan's Step 9 requires it:

| File | Why it is required |
|---|---|
| `sequencer.c/.h` | overlay apply/restore, §9 third rule, §10.2 drain branch |
| `SceneModTargets.c/.h` | `fxm` runtime path, §10.1 / §10.3 |
| `InstrumentManager.c/.h` | LFO `fx` namespace and adapter, §10.3; Pattern writer validation, §10.2 |
| `menu.c` | `fx` category / View A, §10.2; `fx` LFO namespace display, §10.3; ST8 F2 |
| `storageTypes.c` | D11 |
| `filesystem.c`, `menuEffects.c` | P1–P3 only |

`PatternData.c` and `presetManager.c` are **not** modified.

### 0.2 RAM

| Object | Region | Bytes | Plan item |
|---|---|---|---|
| `effects_automation` (new; `_Static_assert` 184): `active` u64, `pending_end` u64, `value[64]`, `owner[64]`, `lfo[6][2]` × 3 B, `pending_track`, `owner_tracks`, `morph_override`, `morph_override_valid` | SRAM1 `.bss` | 184 | 4 (overlay values, mask, owning track: 64 B, F1) + 5 (LFO → `fx`, about 48 B) |
| `seq_effectAutomationTracks`, `seq_effectAutomationReset` | SRAM1 `.bss` | 2 | 7 (realized per D1) |
| `effects_state_t` | SRAM1 | 84, unchanged | 4 |

The expected link result is `.bss` about +186 B (+ alignment). DTCM and
FXBUF are unchanged.

### 0.3 Flash estimate

| Area | Estimate |
|---|---|
| EffectsManager (target helpers, overlay API, LFO table, service) | +1.2 to +1.5 KB |
| InstrumentManager (namespace branches, adapter; the encoder helper offsets its own growth) | +0.3 to +0.45 KB |
| sequencer (marker, drain branches, reset latch) | +0.2 to +0.3 KB |
| menu.c (`fx` category editing/rendering, LFO `fx`, `fxm` Morph handling, F2) | +0.9 to +1.3 KB |
| **Total** | **+2.6 to +3.6 KB** |

- Headroom after Step 9 is expected at **about 8.8 to 9.8 KB**. That is well
  under `link_budget.py`'s 16 KiB soft warning, which is already active.
- Step 10 (fan-out) is expected at about 1 to 1.5 KB. Record the measured
  delta in §11.

---

## 1. Change index

| # | File | Location (current) | Action | § |
|---|---|---|---|---|
| P1 | `Core/Hardware/SD/filesystem.c` | ~19657–19661 and ~19886–19890 (`fx_stem`) | modify | 2 |
| P2 | `Core/Hardware/SD/filesystem.c` | 17960–17964 (Kit Save Effect row) | remove | 2 |
| P3 | `Core/Menu/menuEffects.c` | `menuEffects_selectPressed()` ~299 | add | 2 |
| 1 | `Core/DSP/Effects/EffectsManager.c` | `effects_seqStepFor()` 497, `effects_seqSelectedStep()` 535, `effects_seqSelect()` 543 | modify (ST8 F1) | 3.1 |
| 2 | `Core/DSP/Effects/EffectsManager.h` | after `effects_setSeqLaneLock()` prototype | add | 3.2 |
| 3 | `Core/DSP/Effects/EffectsManager.c` | after `static effects_state_t effects_state;` (125) | add state | 3.3 |
| 4 | `Core/DSP/Effects/EffectsManager.c` | after `effects_paramModulatable()` (ends ~245) | add target helpers | 3.4 |
| 5 | `Core/DSP/Effects/EffectsManager.c` | before `#if DEV_MODE_DIAGNOSTIC` at 633 | add overlay and LFO API | 3.5, 3.6 |
| 6 | `Core/DSP/Effects/EffectsManager.c` | `effects_init()` 757, `effects_activateScene()` 773, `effects_changeType()` 795 | modify | 3.7 |
| 7 | `Core/DSP/Effects/EffectsManager.c` | `effects_service()` 827 | modify | 3.8 |
| 8 | `Core/Bank/Scene/SceneModTargets.c` | `fxm` row (ID 20) | modify | 4 |
| 9 | `Core/Bank/Scene/SceneModTargets.h` | `SCENE_MOD_TARGET_KIND_EFFECT_MORPH` comment | modify | 4 |
| 10 | `Core/Sequencer/sequencer.h` | after `seq_fxTakeEvent()` (190) | add | 5.1 |
| 11 | `Core/Sequencer/sequencer.c` | includes (66), latch (97), `seq_fxPublishReset()` (112), after `seq_fxTakeEvent()`, pending bits (208) | add/modify | 5.2 |
| 12 | `Core/Sequencer/sequencer.c` | after `seq_queueStepAutomations()`; `seq_advanceTrackStep()` 866–869 | add/modify | 5.3 |
| 13 | `Core/Sequencer/sequencer.c` | `seq_restoreAllSceneAutomation()` 377; `seq_applySceneAutomation()` 937 | modify | 5.4 |
| 14 | `Core/Sequencer/sequencer.c` | `seq_drainPendingAutomation()` 960–1022 | modify | 5.5 |
| 15 | `Core/DSP/Instruments/InstrumentManager.h` | namespace block 51–60 | modify/add | 6.1 |
| 16 | `Core/DSP/Instruments/InstrumentManager.c` | include; installed kinds 76–80 | add | 6.2 |
| 17 | `Core/DSP/Instruments/InstrumentManager.c` | `instrumentManager_targetValid()` 755–760 | modify | 6.3 |
| 18 | `Core/DSP/Instruments/InstrumentManager.c` | `lfoTargetVoiceValid` 925, `lfoTargetIdFromToken` 938, `lfoTargetTokenFromId` 968, `stepLfoTargetToken` 996 | modify | 6.4 |
| 19 | `Core/DSP/Instruments/InstrumentManager.c` | before `updateLfoSceneDestination()` 2333; its VOICE_MORPH case; new EFFECT_MORPH case | add/modify | 6.5 |
| 20 | `Core/DSP/Instruments/InstrumentManager.c` | `restoreLfoSupplementalTarget()` 2604–2606; `installLfoModulationTarget()` 2658; `updateLfoAdapters()` 2822–2836 | modify | 6.6 |
| 21 | `Core/Menu/menu.c` | LFO namespace clamps/labels 3557–3565, 3640–3655, 3694, 4022, 4093, 4208, 9394, 9519 | modify | 7.1 |
| 22 | `Core/Menu/menu.c` | `menu_formatInstrumentTargetShort()` 3894; `menu_displayInstrumentTargetFull()` 3942 | modify | 7.2 |
| 23 | `Core/Menu/menu.c` | after `menu_morphAutomationExpand()` 3239 | add | 7.3 |
| 24 | `Core/Menu/menu.c` | after `menu_stepAutomationTargetUsed()` (ends 8605) | add | 7.4 |
| 25 | `Core/Menu/menu.c` | `menu_stepAutomationCategory()` comment 8764; `menu_stepAutomationCurrentValue()` 8798 | modify | 7.5 |
| 26 | `Core/Menu/menu.c` | `menu_stepAutomationEdit()` field 2 (8920–8950), field 3 (8993–9015) | modify | 7.6 |
| 27 | `Core/Menu/menu.c` | `menu_repaintStepAutomation()` 9118–9336 | modify | 7.7 |
| 28 | `Core/Menu/menu.c` | `menu_applyEffectMarkers()` 2559–2562 | modify (ST8 F2) | 7.8 |
| 29 | `Core/Hardware/SD/storageTypes.c` | 2098–2120 | modify | 8 |
| 30 | docs | see §9 | modify | 9 |

---

## 2. Prerequisite fixes (P1–P3)

These are unchanged from the earlier assessments. They are repeated here only
so the change set is complete.

- **P1 (`S072_ST6` §12.3 F1).**
  - **Where:** `filesystem.c` ~19661 (phase 33) and ~19890 (phase 82).
  - **Change:** pass `blank ? "        " : fx_stem` as the stem argument of
    `storage_makeSavedEffectDisplayFilename()`.
  - **Comment:**

    ```c
    /* A blank HCNAMES row may be cached as NUL bytes; pass explicit spaces so
     * the Instrument naming path produces the blank-name file (F4), not the
     * `inst` fallback that reloads as a non-blank name. */
    ```

- **P2 (`S072_ST6` §12.3 F2).**
  - **Change:** delete `filesystem.c` 17960–17964: the
    `filesystem_setResidentSource(filesystem_residentEffectRow(...),
    FS_RESIDENT_SOURCE_INHERIT)` call and its `autosave_markSourceDirty(...)`.
  - **Why:** Kit Save must never touch the Effect row (plan §7.3). The
    surrounding comment ("seven dirty source cells") becomes true again.

- **P3 (`S072_ST7` §13 F1).**
  - **Change:** first statement of `menuEffects_selectPressed()`, before the
    `button >= MENU_FX_SELECT_COUNT` test:

    ```c
    /* Any SELECT press abandons an unconfirmed `typ` transaction (F3). */
    menuEffects_typeEdit = 0u;
    ```

---

## 3. EffectsManager

### 3.1 ST8 F1: `sel` always applies (modify)

**`effects_seqStepFor()` (497): replace the `sel` branch.**

```c
    /*
     * `sel` always applies (A12; S072_ST8 §12.4 F1). The foreground cursor
     * is defined from boot, survives RESET and Scene switches, and is bounded
     * by the current length. seq_step_valid belongs to the clock step only.
     */
    if (record->seq_run_mode == EFFECT_SEQ_RUN_SEL)
        return (uint8_t)(effects_state.seq_sel_step % len);
```

**`effects_seqSelectedStep()` (535): replace the body.**

```c
uint8_t effects_seqSelectedStep(void)
{
    /* The `sel` cursor is always defined; LED callers gate on run mode. */
    return effects_state.seq_sel_step;
}
```

**`effects_seqSelect()` (543):**

- **Remove** `effects_state.seq_step_valid = 1u;`.
- Update the one-line comment above the function to:

```c
/* Select one retained step immediately; `sel` applies it whether or not the
 * transport runs. The clock-step validity flag is not touched. */
```

The `sel` Morph re-latch (F1 item 4) is placed in `effects_service()` (§3.8).

### 3.2 `EffectsManager.h`: Step 9 API (add)

Insert after the `effects_setSeqLaneLock()` prototype, before
`#if DEV_MODE_DIAGNOSTIC`.

```c
/*
 * Pattern automation and LFO on Effects (Session 072 step 9; plan §9, §10).
 *
 * Target identity: block-7 IDs 448 + local (EffectTypes.h).
 * - Pattern automation accepts AUTOMATABLE locals 0..62. Local 63 aliases the
 *   Pattern off sentinel, and WIDE8 rows require expand7.
 * - LFO accepts MODULATABLE locals that have a modulation domain.
 *
 * Validation is always against a Scene's retained Effect type:
 * - pickers and the Pattern writer pass the viewed Scene;
 * - runtime apply checks the active runtime type on every call, so a stale
 *   entry (for example after `typ`) is ignored rather than misapplied.
 *
 * Overlay lifetime (third restore rule, F1/G3): a Pattern value replaces the
 * FX-sequencer/menu value of one parameter. It lasts until the writing track
 * plays an automation step that does not carry that parameter. The Sequencer
 * queues an FX step marker ahead of each owning track's step entries, and
 * seq_drainPendingAutomation() brackets them with StepBegin, apply and
 * StepFlush.
 *
 * effects_automationReset() clears every overlay plus the `fxm` Morph
 * override. It runs from the drain on the common sequencer reset path
 * (Scene/Pattern change, transport start/stop, external reset; G1), ordered
 * ahead of the new pass's first records, and from Scene activation.
 *
 * LFO contributions are base-independent (direction, depth) entries per
 * source slot and pair, the S071 voice-Morph contract. effects_service()
 * resolves them every render block around the current held value.
 *
 * Context: foreground only (drain, LFO dispatch, Menu, Preset). The only
 * TIM3-visible state is the owner-track byte, which is published to the
 * Sequencer through seq_setEffectAutomationTracks().
 */
#define EFFECT_LFO_TARGET_MORPH      0xFEu  /* `fxm`, via the `scn` namespace */
#define EFFECT_LFO_TARGET_NONE       0xFFu
#define EFFECT_LFO_DIRECTION_NONE    0u     /* = PRESET_MORPH_LFO_DIRECTION_NONE  */
#define EFFECT_LFO_DIRECTION_DOWN    1u     /* = PRESET_MORPH_LFO_DIRECTION_MAIN  */
#define EFFECT_LFO_DIRECTION_UP      2u     /* = PRESET_MORPH_LFO_DIRECTION_MORPH */

uint8_t effects_targetValid(uint8_t scene_index, uint16_t id,
                            instrument_target_use_t use);
const effect_param_descriptor_t *effects_targetDescriptor(
    uint8_t scene_index, uint16_t id, instrument_target_use_t use);
uint16_t effects_stepTarget(uint8_t scene_index, uint16_t current,
                            int8_t direction, instrument_target_use_t use);

void effects_automationReset(void);
void effects_automationStepBegin(uint8_t track);
uint8_t effects_applyAutomation(uint8_t track, uint8_t local, uint8_t value7);
void effects_automationStepFlush(void);
void effects_setMorphAutomation(uint8_t amount);

void effects_setLfoContribution(uint8_t source_slot, uint8_t pair,
                                uint8_t target, uint8_t direction,
                                uint8_t depth);
void effects_clearLfoSource(uint8_t source_slot, uint8_t pair);
```

Also update the header's opening module comment (lines 45–58). In
"Affiliates: … Menu, and the later sequencer/LFO and FX-bus steps", replace
"the later sequencer/LFO" with "Sequencer (FX latch, automation drain) and
InstrumentManager (LFO adapters)".

### 3.3 Overlay and LFO state (add)

Insert after `static effects_state_t effects_state;` (line 125).

```c
/*
 * Pattern-automation and LFO overlay state (Session 072 step 9; plan §9,
 * §10.2-10.3, §16.1 items 4-5).
 *
 * active / value / owner: one Pattern overlay per Effect-local parameter.
 *   value is already in the descriptor's stored domain (WIDE8 expanded,
 *   max_value clamped). owner is the writing track 0..6, and the overlay ends
 *   when that track's automation for the parameter ends (F1/G3; last writer
 *   wins).
 * pending_end / pending_track: the open FX step-marker group. These are the
 *   overlays owned by pending_track that this step has not rewritten yet.
 * owner_tracks: one bit per track owning at least one overlay. It is mirrored
 *   to the Sequencer so TIM3 queues markers only when needed.
 * morph_override: the Pattern `fxm` overlay. It is the first Effect Morph
 *   base source and follows the Scene rule (G1, G4).
 * lfo: one base-independent contribution per LFO source slot and pair,
 *   targeting a local parameter or EFFECT_LFO_TARGET_MORPH. direction NONE
 *   is inactive, so a zeroed entry is harmless.
 *
 * Lifetime: SRAM1, foreground-only writers (drain, LFO dispatch, Scene
 * activation). The runtime never writes SceneData or AutoSave.
 */
typedef struct {
    uint8_t target;
    uint8_t direction;
    uint8_t depth;
} effects_lfo_entry_t;

#define EFFECT_AUTOMATION_TRACK_NONE  0xFFu
#define EFFECT_AUTOMATION_TRACK_LIMIT 8u     /* owner_tracks is one byte */

typedef struct {
    uint64_t active;
    uint64_t pending_end;
    uint8_t value[EFFECT_PARAM_COUNT];
    uint8_t owner[EFFECT_PARAM_COUNT];
    effects_lfo_entry_t lfo[INSTRUMENT_SLOT_COUNT][2];
    uint8_t pending_track;
    uint8_t owner_tracks;
    uint8_t morph_override;
    uint8_t morph_override_valid;
} effects_automation_t;

_Static_assert(sizeof(effects_automation_t) == 184u,
               "effects_automation_t size is recorded in SRAM_MANIFEST.md");

static effects_automation_t effects_automation;
```

### 3.4 Target helpers (add)

Insert after `effects_paramModulatable()` (ends ~line 245).

```c
/*
 * Resolve the retained Effect type of one resident Scene.
 *
 * Output: the record's type, or `off` when the Scene or its type is invalid.
 * Target validation always uses the Scene being viewed or edited, not the
 * live runtime, so the step editor and the Pattern writer agree with what
 * that Scene will play.
 */
static effect_type_id_t effects_sceneType(uint8_t scene_index)
{
    const effect_record_t *record = scene_effectConst(scene_index);

    return (record && effects_registryEntry(record->type))
        ? record->type : EFFECT_TYPE_OFF;
}

/* Apply the use-specific capability rule to one Effect-local index. */
static uint8_t effects_localValid(effect_type_id_t type, uint8_t local,
                                  instrument_target_use_t use)
{
    return (use == INSTRUMENT_TARGET_AUTOMATION)
        ? effects_paramAutomatable(type, local)
        : effects_paramModulatable(type, local);
}

/*
 * Validate one block-7 target for a Scene (plan §10.1-10.3).
 *
 * Inputs: Scene index, canonical ID and use. Output: nonzero only for an
 * Effect ID whose local row exists on that Scene's type and carries the
 * required capability. AUTOMATION excludes local 63 (the Pattern off
 * alias, F2) and WIDE8 rows without expand7. MODULATION requires
 * MODULATABLE plus a non-NONE modulation domain.
 * Clients: InstrumentManager (Pattern writer and LFO namespace) and Menu.
 */
uint8_t effects_targetValid(uint8_t scene_index, uint16_t id,
                            instrument_target_use_t use)
{
    if (!effectTarget_isEffectId(id))
        return 0u;
    return effects_localValid(effects_sceneType(scene_index),
                              effectTarget_local(id), use);
}

/* Return the descriptor of a valid target, or NULL (display/edit helper). */
const effect_param_descriptor_t *effects_targetDescriptor(
    uint8_t scene_index, uint16_t id, instrument_target_use_t use)
{
    if (!effects_targetValid(scene_index, id, use))
        return NULL;
    return effects_descriptor(effects_sceneType(scene_index),
                              effectTarget_local(id));
}

/*
 * Walk a Scene's valid Effect targets in descriptor order.
 *
 * Inputs: Scene, current canonical ID (INSTRUMENT_PARAM_INVALID for off),
 * signed direction and use. Output, matching
 * instrumentManager_stepTargetForSlot():
 * - forward from off selects the first valid row;
 * - backward from the first valid row returns off (INSTRUMENT_PARAM_INVALID);
 * - forward past the last valid row returns `current` unchanged;
 * - there is no wrap.
 * Why here: only the registry knows the row order and flags. Menu and
 * InstrumentManager must not iterate descriptor tables themselves.
 */
uint16_t effects_stepTarget(uint8_t scene_index, uint16_t current,
                            int8_t direction, instrument_target_use_t use)
{
    effect_type_id_t type = effects_sceneType(scene_index);
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    int16_t local;

    if (!entry || direction == 0)
        return current;
    if (!effectTarget_isEffectId(current)) {
        if (direction < 0)
            return INSTRUMENT_PARAM_INVALID;
        local = -1;
    } else {
        local = (int16_t)effectTarget_local(current);
    }
    for (;;) {
        local = (int16_t)(local + ((direction > 0) ? 1 : -1));
        if (local < 0)
            return INSTRUMENT_PARAM_INVALID;
        if (local >= (int16_t)entry->descriptor_count)
            return effectTarget_isEffectId(current)
                ? current : INSTRUMENT_PARAM_INVALID;
        if (effects_localValid(type, (uint8_t)local, use))
            return effectTarget_id((uint8_t)local);
    }
}
```

`effects_paramModulatable()`/`effects_paramAutomatable()` are defined above
this point, and `effectTarget_*` are EffectTypes.h inlines, so no prototypes
are needed.

### 3.5 Overlay API (add)

Insert before `#if DEV_MODE_DIAGNOSTIC` at line 633, after
`effects_setSeqLaneLock()`.

```c
/*
 * Publish the tracks that own at least one Effect overlay.
 *
 * Output: owner_tracks is rebuilt from active/owner, and the Sequencer is
 * told only when the byte changed. TIM3 reads that byte to decide whether an
 * advancing track needs an FX step marker (S072_ST9 D2). A walk of at most
 * 63 bits runs only on overlay changes, never per sample.
 */
static void effects_automationPublishOwners(void)
{
    uint64_t mask = effects_automation.active;
    uint8_t tracks = 0u;

    while (mask) {
        uint8_t local = (uint8_t)__builtin_ctzll(mask);

        tracks |= (uint8_t)(1u << effects_automation.owner[local]);
        mask &= (mask - 1ULL);
    }
    if (tracks != effects_automation.owner_tracks) {
        effects_automation.owner_tracks = tracks;
        seq_setEffectAutomationTracks(tracks);
    }
}

/*
 * Clear Pattern overlays.
 *
 * include_morph = 1: the Scene-rule reset. Every parameter overlay, the open
 * marker group and the `fxm` override are dropped.
 * include_morph = 0: a type change. Local indices lose their meaning, but the
 * Scene-level `fxm` override stays until the Scene rule clears it (D4).
 * The service falls back to seq(i)/menu(i) on the next block, and nothing is
 * written back.
 */
static void effects_automationClear(uint8_t include_morph)
{
    effects_automation.active = 0u;
    effects_automation.pending_end = 0u;
    effects_automation.pending_track = EFFECT_AUTOMATION_TRACK_NONE;
    if (include_morph)
        effects_automation.morph_override_valid = 0u;
    effects_automationPublishOwners();
}

void effects_automationReset(void)
{
    /*
     * Common reset path and Scene activation (plan §9 F1/G1; S072_ST9 D4).
     * The drain calls this before the first record of a new pass, so the
     * pass's step-0 values apply after the clear, never before it.
     */
    effects_automationClear(1u);
}

void effects_automationStepFlush(void)
{
    /*
     * Close the open FX step-marker group.
     *
     * Every overlay still in pending_end is owned by pending_track, and that
     * track's new step did not rewrite it, so the track's automation for the
     * parameter has ended (G3). Rewrites by any track have already removed
     * their bit. The value falls back on the next block, to the FX lock on the
     * current FX step or to the Morph-interpolated menu value (A18). Called by
     * StepBegin and at the end of every drain pass.
     */
    if (effects_automation.pending_end != 0u) {
        effects_automation.active &= ~effects_automation.pending_end;
        effects_automation.pending_end = 0u;
        effects_automationPublishOwners();
    }
    effects_automation.pending_track = EFFECT_AUTOMATION_TRACK_NONE;
}

void effects_automationStepBegin(uint8_t track)
{
    uint64_t mask;
    uint64_t owned = 0u;

    /*
     * Open the FX step-marker group of one track (S072_ST9 D2).
     *
     * Input: the track whose step TIM3 has just advanced. The marker precedes
     * that step's queued entries. Output: its owned overlays become end
     * candidates, and effects_applyAutomation() removes each one the step
     * re-holds. The previous group is closed first, so groups never overlap.
     */
    effects_automationStepFlush();
    if (track >= EFFECT_AUTOMATION_TRACK_LIMIT)
        return;
    mask = effects_automation.active;
    while (mask) {
        uint8_t local = (uint8_t)__builtin_ctzll(mask);

        if (effects_automation.owner[local] == track)
            owned |= (1ULL << local);
        mask &= (mask - 1ULL);
    }
    effects_automation.pending_end = owned;
    effects_automation.pending_track = track;
}

uint8_t effects_applyAutomation(uint8_t track, uint8_t local, uint8_t value7)
{
    const effect_param_descriptor_t *descriptor;
    uint64_t bit;
    uint8_t value;

    /*
     * Apply one Pattern entry as a runtime-only overlay (plan §10.2).
     *
     * Inputs: writing track, Effect-local index and the seven-bit Pattern
     * value. The entry is validated against the live runtime type, so a stale
     * entry after `typ` or a Scene edit is ignored. WIDE8 rows expand through
     * their descriptor's expand7. The result is clamped to max_value.
     * Output: the overlay value, its owner (last writer wins, G3), the active
     * bit, and removal from the open end-candidate set. SceneData, AutoSave
     * and the card-clean bit are never touched. Returns nonzero when applied.
     */
    if (track >= EFFECT_AUTOMATION_TRACK_LIMIT ||
        !effects_paramAutomatable(effects_state.runtime_type, local))
        return 0u;
    descriptor = effects_descriptor(effects_state.runtime_type, local);
    value = (uint8_t)(value7 & 0x7Fu);
    if ((descriptor->effect_flags & EFFECT_PARAM_FLAG_WIDE8) != 0u)
        value = descriptor->expand7(value);
    if (value > descriptor->max_value)
        value = descriptor->max_value;
    bit = 1ULL << local;
    effects_automation.value[local] = value;
    effects_automation.owner[local] = track;
    effects_automation.active |= bit;
    effects_automation.pending_end &= ~bit;
    effects_automationPublishOwners();
    return 1u;
}

void effects_setMorphAutomation(uint8_t amount)
{
    /*
     * Pattern `fxm` overlay (Scene target 404; plan §9 Effect Morph, G1, G4).
     *
     * Input: the already-expanded 0..255 amount from seq_applySceneAutomation().
     * It replaces the held FX Morph lane and the retained amount as the Effect
     * Morph base, and holds until the Scene-rule reset. Runtime-only.
     */
    effects_automation.morph_override = amount;
    effects_automation.morph_override_valid = 1u;
}
```

### 3.6 LFO API (add, directly after §3.5)

```c
void effects_setLfoContribution(uint8_t source_slot, uint8_t pair,
                                uint8_t target, uint8_t direction,
                                uint8_t depth)
{
    effects_lfo_entry_t *entry;

    /*
     * Store one LFO sample for an Effect destination (plan §10.3; S071 pattern).
     *
     * Inputs: LFO source slot, pair, destination (Effect local or
     * EFFECT_LFO_TARGET_MORPH), and a base-independent direction/depth
     * produced by InstrumentManager's shared encoder. No base is captured.
     * effects_service() applies the entry around the current held value
     * every block, so Pattern automation and FX locks under the LFO never
     * leave a stale base. Depth 0 is stored as inactive.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT || pair > 1u)
        return;
    entry = &effects_automation.lfo[source_slot][pair];
    if (direction > EFFECT_LFO_DIRECTION_UP || depth == 0u)
        direction = EFFECT_LFO_DIRECTION_NONE;
    entry->target = target;
    entry->direction = direction;
    entry->depth = (direction == EFFECT_LFO_DIRECTION_NONE) ? 0u : depth;
}

void effects_clearLfoSource(uint8_t source_slot, uint8_t pair)
{
    /*
     * Remove one LFO pair's Effect contribution (target clear, replacement,
     * or Scene teardown). The parameter returns to its held value on the next
     * block because the service recomputes from retained images, so no
     * restore write is needed.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT || pair > 1u)
        return;
    effects_automation.lfo[source_slot][pair].target = EFFECT_LFO_TARGET_NONE;
    effects_automation.lfo[source_slot][pair].direction =
        EFFECT_LFO_DIRECTION_NONE;
    effects_automation.lfo[source_slot][pair].depth = 0u;
}

/*
 * Sum every LFO contribution for one destination around a base.
 *
 * Inputs: destination id, current held value, and the inclusive
 * modulation domain. Output: base + Σ contributions, clamped to the domain.
 * - UP scales the remaining distance to max.
 * - DOWN scales the distance above min.
 * This is presetMorph_resolveLfoAmount()'s rounded integer math
 * generalized to a non-zero min. It is algebraically the same as
 * modNode_shapeParameterU16() for positive, negative (original-LXR) and
 * bipolar polarity, so Effect LFO feels identical to Instrument LFO.
 */
static uint8_t effects_lfoResolve(uint8_t target, uint8_t base,
                                  uint8_t min_value, uint8_t max_value)
{
    int32_t effective;
    uint8_t source;
    uint8_t pair;

    if (base < min_value)
        base = min_value;
    else if (base > max_value)
        base = max_value;
    effective = base;
    for (source = 0u; source < INSTRUMENT_SLOT_COUNT; source++) {
        for (pair = 0u; pair < 2u; pair++) {
            const effects_lfo_entry_t *entry =
                &effects_automation.lfo[source][pair];

            if (entry->target != target)
                continue;
            if (entry->direction == EFFECT_LFO_DIRECTION_UP)
                effective += ((int32_t)(max_value - base) * entry->depth +
                              127) / 255;
            else if (entry->direction == EFFECT_LFO_DIRECTION_DOWN)
                effective -= ((int32_t)(base - min_value) * entry->depth +
                              127) / 255;
        }
    }
    if (effective < (int32_t)min_value)
        effective = min_value;
    else if (effective > (int32_t)max_value)
        effective = max_value;
    return (uint8_t)effective;
}

/* One bit per Effect-local row that has an active LFO entry this block. */
static uint64_t effects_lfoTargetMask(void)
{
    uint64_t mask = 0u;
    uint8_t source;
    uint8_t pair;

    for (source = 0u; source < INSTRUMENT_SLOT_COUNT; source++) {
        for (pair = 0u; pair < 2u; pair++) {
            const effects_lfo_entry_t *entry =
                &effects_automation.lfo[source][pair];

            if (entry->direction != EFFECT_LFO_DIRECTION_NONE &&
                entry->target < EFFECT_PARAM_COUNT)
                mask |= (1ULL << entry->target);
        }
    }
    return mask;
}
```

`effects_lfoResolve()` and `effects_lfoTargetMask()` are `static` and defined
before `effects_service()`, so no prototypes are needed. `effects_service()`
is at line 827, after this block.

### 3.7 Lifecycle hooks (modify)

**`effects_init()` (757).** After `memset(&effects_state, 0, …);`, add:

```c
    /* Step 9 overlay/LFO state starts empty; LFO entries target nothing. */
    memset(&effects_automation, 0, sizeof(effects_automation));
    effects_automation.pending_track = EFFECT_AUTOMATION_TRACK_NONE;
    {
        uint8_t source;

        for (source = 0u; source < INSTRUMENT_SLOT_COUNT; source++) {
            effects_clearLfoSource(source, 0u);
            effects_clearLfoSource(source, 1u);
        }
    }
```

`effects_clearLfoSource()` is defined later in the file (§3.5 block, before
line 633, which is above `effects_init()`), so ordering is satisfied.

**`effects_activateScene()` (773).** After
`effects_state.held_morph_valid = 0u;`, add:

```c
    /*
     * Pattern overlays and the `fxm` override belong to the outgoing Scene
     * (Scene rule, G1). A Scene change while stopped never passes the
     * sequencer reset path, so activation clears them too (S072_ST9 D4).
     */
    effects_automationReset();
```

Also extend the comment block above it (the "Scene rule (S072_ST8 D3)"
lines) with:

```c
     * Pattern automation overlays follow the same Scene rule (Step 9).
```

**`effects_changeType()` (795).** Inside the existing
`if (scene_index == effects_state.scene_index) { … }` block that clears
`held_morph_valid`, add:

```c
        /* Old-type local overlays are meaningless; fxm is Scene-level (D4). */
        effects_automationClear(0u);
```

### 3.8 `effects_service()`: §9 resolution (modify)

Replace the function's locals, its leading comment, and its section from
`effects_seqConsume(record);` through the per-descriptor `effects_seqOverride`
call. Everything from `if (value > descriptor->max_value)` onward
(buffer clamp, last-applied compare, writes) stays unchanged.

```c
void effects_service(void)
{
    const effect_record_t *record =
        scene_effectConst(effects_state.scene_index);
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_state.runtime_type);
    fx_share_t share;
    uint64_t lfo_mask;
    uint8_t morph;
    uint8_t active_step;
    uint8_t index;
    const effect_seq_step_t *step;

    /* Full descriptor rescan avoids requiring every retained writer to notify. */
    if (!record || !entry)
        return;
    /*
     * Plan §9 resolution (S072 steps 8-9), once per render block:
     *   1. Consume the TIM3 FX latch and find the active FX step.
     *   2. Effect Morph base: the Pattern `fxm` overlay, else the held FX Morph
     *      lane, else the retained effect_morph_amount. Then LFO `fxm` around
     *      that base (G4).
     *   3. Per parameter:
     *      menu(i) = Morph interpolation;
     *      seq(i)  = FX lock;
     *      held(i) = Pattern overlay;
     *      effective(i) = held(i) + Σ LFO(i), clamped to the modulation domain,
     *                     then to max_value, then by the buffer-dependent clamp.
     * Only a changed effective value reaches write_param.
     */
    effects_seqConsume(record);
    active_step = effects_seqStepFor(record);
    step = (active_step != EFFECT_SEQ_STEP_NONE)
        ? &record->steps[active_step] : NULL;
    /* `sel` always applies, so its Morph lock re-latches after RESET/switch. */
    if (record->seq_run_mode == EFFECT_SEQ_RUN_SEL && step &&
        !effects_state.held_morph_valid)
        effects_seqLatchMorph(record, active_step);
    if (effects_automation.morph_override_valid)
        morph = effects_automation.morph_override;
    else if (effects_state.held_morph_valid)
        morph = effects_state.held_morph;
    else
        morph = scene_getEffectMorphAmount(effects_state.scene_index);
    morph = effects_lfoResolve(EFFECT_LFO_TARGET_MORPH, morph, 0u, 255u);
    lfo_mask = effects_lfoTargetMask();
    fxbuf_effectShare(&share);
    for (index = 0u; index < entry->descriptor_count; index++) {
        const effect_param_descriptor_t *descriptor =
            &entry->descriptors[index];
        uint64_t bit = 1ULL << index;
        uint8_t value = (descriptor->base.flags &
                         INSTRUMENT_PARAM_FLAG_MORPHABLE) != 0u ?
            effects_interpolate(record->normal[index], record->morph[index], morph) :
            record->normal[index];

        /* A locked lane replaces the Morph-interpolated menu value (A16). */
        if (step && (step->lock_mask & 0xFFFEu) != 0u)
            (void)effects_seqOverride(entry, step, index, &value);
        /* Pattern automation is held ahead of the FX lock (A18). */
        if ((effects_automation.active & bit) != 0u)
            value = effects_automation.value[index];
        /* LFO modulates around the held value inside its domain (G4). */
        if ((lfo_mask & bit) != 0u &&
            (descriptor->base.flags & INSTRUMENT_PARAM_FLAG_MODULATABLE) != 0u &&
            descriptor->base.mod_domain.flags != INSTRUMENT_MOD_DOMAIN_NONE)
            value = effects_lfoResolve(index, value,
                                       descriptor->base.mod_domain.min_value,
                                       descriptor->base.mod_domain.max_value);
        if (value > descriptor->max_value)
            value = descriptor->max_value;
        /* … unchanged: BUFFER_DEPENDENT clamp, last_applied, writes … */
```

`instrument_param_value_t` is `uint8_t`, so the domain fields pass without a
cast. `effects_seqLatchMorph()` is the existing static helper at line 454.

---

## 4. SceneModTargets: `fxm` goes live (modify)

**`SceneModTargets.c`, the `fxm` row (ID 20) and its comment.** Replace the
comment and the flags word:

```c
    /*
     * Scene Effect Morph `fxm` (ID 404; Session 072 step 9).
     *
     * Pattern automation (seven-bit, expanded like voice Morph) and LFO (the
     * `scn` namespace) reach EffectsManager as runtime-only Morph-base layers
     * that follow the Scene rule (plan §9, G1, G4). Velocity is deliberately
     * absent (A21). Retained edits stay on the Effect page `mrp` and the PERF
     * global Morph bulk-set.
     */
    { SCENE_MOD_TARGET_ID(20u), SCENE_MOD_TARGET_KIND_EFFECT_MORPH, 0xffu,
      0u, 255u, SCENE_MOD_TARGET_USE_LFO | SCENE_MOD_TARGET_USE_AUTOMATION,
      "Effect", "FX Morph", "fxm" },
```

**`SceneModTargets.h`, the `SCENE_MOD_TARGET_KIND_EFFECT_MORPH` comment
(40–45).** Replace it:

```c
    /*
     * Scene Effect Morph amount, `fxm` (ID 404).
     *
     * Effect parameter cells are separate block-7 targets; this kind is only
     * the Scene-level Morph amount. Step automation and LFO apply runtime
     * overlays through EffectsManager (Session 072 step 9); velocity is not
     * offered (A21).
     */
```

---

## 5. Sequencer

### 5.1 `sequencer.h` (add after `uint8_t seq_fxTakeEvent(void);`, line 190)

```c
/*
 * Effect-overlay owner tracks (Session 072 step 9; plan §9 third rule).
 *
 * EffectsManager publishes one bit per track that currently owns at least one
 * Effect Pattern overlay. TIM3 reads the byte to decide whether an advancing
 * track needs an FX step marker in the pending automation queue. It is a
 * single byte store/load: foreground writer, TIM3 reader.
 */
void seq_setEffectAutomationTracks(uint8_t mask);
```

### 5.2 `sequencer.c`: includes, latches, reset (add/modify)

**Includes.** After `#include "StepScale.h"` (line 66), add:

```c
#include "EffectsManager.h"
```

**New latches.** After `static volatile uint8_t seq_fxEvent = 0u;` (line 97),
add:

```c
/*
 * Effect automation handshake (Session 072 step 9; +2 B SRAM1).
 *
 * seq_effectAutomationTracks: foreground-published owner-track mask; TIM3
 *   queues an FX step marker only for these tracks (S072_ST9 D2).
 * seq_effectAutomationReset: set with every FX RESET on the common reset
 *   path. seq_drainPendingAutomation() takes it before the first queued
 *   record, so the new pass's step-0 Effect values apply after the clear
 *   (D4).
 */
static volatile uint8_t seq_effectAutomationTracks = 0u;
static volatile uint8_t seq_effectAutomationReset = 0u;
```

**`seq_fxPublishReset()` (line 112).** Inside the PRIMASK section, after
`seq_fxEvent = SEQ_FX_EVENT_RESET;`, add:

```c
    /* The same boundary ends every Effect Pattern overlay and `fxm` (G1). */
    seq_effectAutomationReset = 1u;
```

Update its one-line comment to:

```c
/* Publish a foreground reset for the next FX boundary and Effect overlays. */
```

**New setter.** After `seq_fxTakeEvent()` (ends ~line 132), add:

```c
void seq_setEffectAutomationTracks(uint8_t mask)
{
    /* Single-byte store; TIM3 reads it in seq_queueEffectStepMarker(). */
    seq_effectAutomationTracks = mask;
}
```

**Pending identity bits.** After
`#define SEQ_PENDING_TYPE_AUTOMATION_BIT (1u << 10u)` (line 208), add:

```c
/*
 * FX step marker (Session 072 step 9). The identity keeps the 10-bit
 * track*NUM_STEPS+step id. Bit 11 marks a payload-free boundary record that
 * precedes one owning track's automation entries (S072_ST9 D2).
 */
#define SEQ_PENDING_TYPE_FX_STEP_BIT    (1u << 11u)
#define SEQ_PENDING_STEP_ID_MASK        0x03FFu
```

### 5.3 Marker producer (add/modify)

**Add** directly after `seq_queueStepAutomations()` (ends ~line 760):

```c
/*
 * Queue one Effect-overlay step boundary for a track (Session 072 step 9).
 *
 * Inputs: track and its new step, in TIM3 context, on the same gate as
 * seq_queueStepAutomations() (condition passed, not muted, not the
 * live-erase track; S072_ST9 D3). Output: a payload-free record queued ahead
 * of that step's entries, only when the track owns an Effect overlay. The
 * foreground drain turns it into StepBegin, so parameters this step does not
 * rewrite end their overlay (plan §9 third rule, G3).
 * Cost: one byte test per advancing track. There is no queue traffic when no
 * Effect overlay exists. A full queue records a PatternTrace overflow witness;
 * the overlay then ends one step late (D2).
 */
static void seq_queueEffectStepMarker(uint8_t track, uint8_t step)
{
    uint16_t step_id = (uint16_t)(track * NUM_STEPS + step);

    if ((seq_effectAutomationTracks & (uint8_t)(1u << track)) == 0u)
        return;
    if (seq_pending_automation_count < SEQ_PENDING_BUF_COUNT) {
        uint8_t pending_index = seq_pending_automation_count;

        seq_pending_automation[pending_index].identity =
            (uint16_t)(step_id | SEQ_PENDING_TYPE_FX_STEP_BIT);
        seq_pending_automation[pending_index].payload = 0u;
        seq_pending_automation_count = (uint8_t)(pending_index + 1u);
        seq_pending_automation_drain = 1u;
    } else {
        patternTrace_recordOverflow(
            (uint16_t)(step_id | SEQ_PENDING_TYPE_FX_STEP_BIT), 0u);
    }
}
```

**Modify** `seq_advanceTrackStep()` lines 866–869. The single-statement `if`
becomes a block:

```c
		if (step_allowed &&
		    (!seq_eraseActive || track != menu_getActiveVoice())) {
			/*
			 * Effect overlay boundary first, then this step's entries, so
			 * an entry on the same parameter re-holds it (S072_ST9 D2).
			 */
			seq_queueEffectStepMarker(track,
			                          (uint8_t)seq_stepIndex[track]);
			seq_queueStepAutomations(track, (uint8_t)seq_stepIndex[track]);
		}
```

Extend the function's header comment ("Affiliates: …") with:
"`seq_queueEffectStepMarker()` (Effect overlay end boundary, S072 step 9)".

### 5.4 Scene-target apply and restore (modify)

**`seq_applySceneAutomation()`.** After the `SCENE_MOD_TARGET_KIND_FX_SEND`
case (937–940), before `default:`, add:

```c
	case SCENE_MOD_TARGET_KIND_EFFECT_MORPH:
		/*
		 * Effect Morph base overlay (plan §9, G1/G4). Stored 0..126 doubles and
		 * 127 reaches 255, exactly like voice Morph. EffectsManager holds it
		 * until the Scene-rule reset; retained Scene data is untouched.
		 */
		effects_setMorphAutomation(effect_expand7Linear(value));
		break;
```

Also extend the function header comment's output sentence with:
"`fxm` sets the EffectsManager Morph-base override."

**`seq_restoreAllSceneAutomation()` (line 377).** Extend the no-op case list:

```c
            case SCENE_MOD_TARGET_KIND_FX_SEND:
            case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
            /*
             * `fxm` is cleared by the foreground drain through the FX reset
             * latch (S072_ST9 D4). This path may run in TIM3, so it never
             * calls EffectsManager.
             */
            case SCENE_MOD_TARGET_KIND_EFFECT_MORPH:
            default:
                break;
```

### 5.5 `seq_drainPendingAutomation()` (modify, 960–1022)

**(a) Header comment.** Append to the existing block:

```c
 * Session 072 step 9: an FX RESET latch taken first clears Effect overlays
 * and `fxm` before this pass's records apply. FX step markers open one
 * track's Effect end-candidate group. Block-7 entries apply through
 * EffectsManager, and the pass ends by closing the last marker group.
```

**(b) Top of the function.** Between `uint8_t i = 0u;` and
`if (!seq_pending_automation_drain)`, add:

```c
    /*
     * Effect overlay reset (plan §9 F1/G1; S072_ST9 D4). It is taken before
     * any queued record, so a new pass's step-0 values apply after the clear.
     * It runs even when the queue is empty.
     */
    if (seq_effectAutomationReset) {
        uint32_t primask = __get_PRIMASK();

        __disable_irq();
        seq_effectAutomationReset = 0u;
        __set_PRIMASK(primask);
        effects_automationReset();
    }
```

**(c) Record dispatch.** In the `while` loop, replace the
`if ((identity & SEQ_PENDING_TYPE_AUTOMATION_BIT) != 0u) { … }` structure:

```c
            if ((identity & SEQ_PENDING_TYPE_FX_STEP_BIT) != 0u) {
                /* One owning track's step boundary (S072_ST9 D2). */
                effects_automationStepBegin((uint8_t)(
                    (identity & SEQ_PENDING_STEP_ID_MASK) / NUM_STEPS));
            } else if ((identity & SEQ_PENDING_TYPE_AUTOMATION_BIT) != 0u) {
                if (instrumentParam_isVoiceParameter(target) && …) {
                    … unchanged voice branch …
                } else if (sceneModTarget_isSceneTarget(target)) {
                    (void)seq_applySceneAutomation(target, value);
                } else if (effectTarget_isEffectId(target)) {
                    /*
                     * Block-7 Effect parameter (plan §10.2). The writing track
                     * is decoded from the step id and becomes the overlay
                     * owner. EffectsManager validates against the live type.
                     */
                    (void)effects_applyAutomation(
                        (uint8_t)((identity & SEQ_PENDING_STEP_ID_MASK) /
                                  NUM_STEPS),
                        effectTarget_local(target), value);
                }
            }
```

**(d) Successful exit.** In the PRIMASK section, after the
`__asm volatile("msr primask, %0" …)` that follows
`seq_pending_automation_drain = 0u;` (line 1014), and before its `return;`,
add:

```c
                /* Close the last marker group of this pass (D2). */
                effects_automationStepFlush();
```

The flush sits after PRIMASK is restored. The marker/entry groups consumed so
far are complete, because TIM3 appends a group inside one ISR.

---

## 6. InstrumentManager

### 6.1 `InstrumentManager.h`: LFO namespace block (51–60, modify/add)

```c
/*
 * LFO target namespace values stored in lfo_target_voice cells.
 *
 * Values 1..6 select instrument voices. Value 7 selects the Scene namespace
 * shown by Menu as `scn`. Value 8 selects the active Scene's Effect parameters
 * shown as `fx` (Session 072 step 9); lfo_target_param is then the
 * Effect-local descriptor index. The retained parameter byte is always a
 * local token; the canonical ID exists only at install/display time.
 */
#define INSTRUMENT_TARGET_VOICE_FIRST 1u
#define INSTRUMENT_TARGET_VOICE_LAST  INSTRUMENT_SLOT_COUNT
#define INSTRUMENT_TARGET_VOICE_SCENE ((uint8_t)(INSTRUMENT_SLOT_COUNT + 1u))
#define INSTRUMENT_TARGET_VOICE_EFFECT ((uint8_t)(INSTRUMENT_SLOT_COUNT + 2u))
/* Highest valid lfo_target_voice value; every picker/parser clamp uses it. */
#define INSTRUMENT_TARGET_VOICE_NAMESPACE_LAST INSTRUMENT_TARGET_VOICE_EFFECT
```

### 6.2 `InstrumentManager.c`: include and installed kind (add)

- After `#include "presetMorphEngine.h"` (line 9), add
  `#include "EffectsManager.h"`.
- In `installed_mod_target_kind_t` (76–80), add after
  `INSTALLED_MOD_TARGET_SCENE_TARGET`:

```c
    /*
     * Effect parameter LFO destination (Session 072 step 9). target_id is a
     * block-7 ID. Samples become base-independent EffectsManager entries,
     * resolved around the held value every render block.
     */
    INSTALLED_MOD_TARGET_EFFECT
```

### 6.3 `instrumentManager_targetValid()` (755–760, modify)

Replace the non-voice branch:

```c
    if (!instrumentParam_isVoiceParameter(id)) {
        /*
         * Effect block 7 (Session 072 step 9) is valid for Pattern automation
         * when the Scene's retained Effect type marks the row AUTOMATABLE.
         * Modulation keeps returning 0 here: LFO Effect targets install through
         * their own supplemental adapter and never reach the descriptor-pointer
         * backends that call this validator with MODULATION.
         */
        if (use == INSTRUMENT_TARGET_AUTOMATION && effectTarget_isEffectId(id))
            return effects_targetValid(scene_index, id, use);
        if (use == INSTRUMENT_TARGET_AUTOMATION &&
            id >= INSTRUMENT_VOICE_ID_COUNT &&
            id < INSTRUMENT_TOTAL_ID_COUNT)
            return sceneModTarget_isSceneTarget(id);
        return 0u;
    }
```

The comment above the function ("Scene IDs are validated by the Scene-target
table …") gains:

```c
     * Effect IDs (448..510) are validated by EffectsManager against the
     * Scene's Effect type for automation only.
```

This single change lets `pat_writeStepAutomation()` accept `fx` entries, and
lets the menu `valid` checks recognize them. PatternData itself is not
edited.

### 6.4 LFO namespace functions (modify)

**`instrumentManager_lfoTargetVoiceValid()` (925).** Change the bound and
the comment:

```c
    /*
     * Validate the retained LFO target namespace byte.
     *
     * Values 1..6 address instrument slots, 7 is the Scene namespace (`scn`)
     * and 8 is the Effect namespace (`fx`, Session 072 step 9).
     */
    return (uint8_t)(voice >= INSTRUMENT_TARGET_VOICE_FIRST &&
                     voice <= INSTRUMENT_TARGET_VOICE_NAMESPACE_LAST);
```

**`instrumentManager_lfoTargetIdFromToken()` (938).** After the `scn`
branch, add:

```c
    if (target_voice == INSTRUMENT_TARGET_VOICE_EFFECT) {
        /*
         * `fx`: the token is an Effect-local row index. It is valid only when
         * the Scene's Effect type marks it MODULATABLE with a modulation
         * domain (plan §10.3). This check is the Scene-activation rebind's
         * re-validation.
         */
        uint16_t id;

        if (token >= EFFECT_TARGET_ID_COUNT)
            return INSTRUMENT_PARAM_INVALID;
        id = effectTarget_id(token);
        return effects_targetValid(scene_index, id, use)
            ? id : INSTRUMENT_PARAM_INVALID;
    }
```

**`instrumentManager_lfoTargetTokenFromId()` (968).** After the `scn`
branch, add:

```c
    if (target_voice == INSTRUMENT_TARGET_VOICE_EFFECT) {
        /* Effect IDs collapse to their local index only in the `fx` namespace. */
        return effects_targetValid(scene_index, id, use)
            ? (instrument_target_token_t)effectTarget_local(id)
            : INSTRUMENT_TARGET_TOKEN_OFF;
    }
```

**`instrumentManager_stepLfoTargetToken()` (996).** After the `scn`
branch, add:

```c
    if (target_voice == INSTRUMENT_TARGET_VOICE_EFFECT) {
        /* Registry-ordered walk of the Scene's MODULATABLE Effect rows. */
        uint16_t current_id = instrumentManager_lfoTargetIdFromToken(
            scene_index, 0u, target_voice, current, use);
        uint16_t next_id = effects_stepTarget(scene_index, current_id,
                                              direction, use);
        return instrumentManager_lfoTargetTokenFromId(scene_index, target_voice,
                                                      next_id, use);
    }
```

Update the comments in all three functions: "Voice namespaces … the Scene
namespace …" gains "… and the Effect namespace stores an Effect-local row
index."

### 6.5 Shared direction/depth encoder and Scene destinations (add/modify)

**Add** before `instrumentManager_updateLfoSceneDestination()` (line 2333):

```c
_Static_assert(EFFECT_LFO_DIRECTION_NONE == PRESET_MORPH_LFO_DIRECTION_NONE &&
               EFFECT_LFO_DIRECTION_DOWN == PRESET_MORPH_LFO_DIRECTION_MAIN &&
               EFFECT_LFO_DIRECTION_UP == PRESET_MORPH_LFO_DIRECTION_MORPH,
               "Effect and voice-Morph LFO directions share one encoding");

/*
 * Encode one LFO sample as a base-independent endpoint direction and depth.
 *
 * Inputs: normalized source, the shared LFO polarity, and normalized amount.
 * Output:
 * - positive polarity moves toward the maximum;
 * - negative polarity keeps original-LXR value-relative motion toward the
 *   minimum;
 * - bipolar polarity picks the endpoint from the sign of the centered source.
 * depth_out receives 0..255, and depth 0 is NONE.
 *
 * The base is deliberately not read. Each owner (voice Morph worker,
 * EffectsManager) applies the depth to its own current base, so there is no
 * stale-base gap between LFO dispatch and resolution.
 *
 * Why shared: voice Morph (S071), Effect Morph `fxm` and Effect parameters
 * (Session 072 step 9) need the identical transfer curve. This is the former
 * VOICE_MORPH-case body, moved unchanged.
 */
static PresetMorphLfoDirection instrumentManager_lfoDirectionDepth(
    float lfo_value_0_1, uint8_t polarity, float amount, uint8_t *depth_out)
{
    float signed_depth;
    float magnitude;
    PresetMorphLfoDirection direction;
    uint8_t depth;

    if (lfo_value_0_1 < 0.f)
        lfo_value_0_1 = 0.f;
    else if (lfo_value_0_1 > 1.f)
        lfo_value_0_1 = 1.f;
    if (amount < 0.f)
        amount = 0.f;
    else if (amount > 1.f)
        amount = 1.f;

    switch (polarity) {
    case MOD_NODE_POLARITY_POSITIVE:
        signed_depth = amount * lfo_value_0_1;
        break;
    case MOD_NODE_POLARITY_BIPOLAR:
        signed_depth = amount * (2.f * lfo_value_0_1 - 1.f);
        break;
    default:
        signed_depth = -(amount * (1.f - lfo_value_0_1));
        break;
    }

    if (signed_depth > 0.f) {
        direction = PRESET_MORPH_LFO_DIRECTION_MORPH;
        magnitude = signed_depth;
    } else if (signed_depth < 0.f) {
        direction = PRESET_MORPH_LFO_DIRECTION_MAIN;
        magnitude = -signed_depth;
    } else {
        direction = PRESET_MORPH_LFO_DIRECTION_NONE;
        magnitude = 0.f;
    }
    if (magnitude > 1.f)
        magnitude = 1.f;
    depth = (uint8_t)(magnitude * 255.f + 0.5f);
    if (depth == 0u)
        direction = PRESET_MORPH_LFO_DIRECTION_NONE;
    *depth_out = depth;
    return direction;
}
```

**Modify** the `SCENE_MOD_TARGET_KIND_VOICE_MORPH` case of
`instrumentManager_updateLfoSceneDestination()` (2350–2420). Replace the
whole case with the following. The existing explanatory block is kept in
shortened form; its full text now lives on the helper above.

```c
    case SCENE_MOD_TARGET_KIND_VOICE_MORPH: {
        /*
         * Voice Morph LFO (S071): direction/depth from the shared encoder,
         * resolved by presetMorph_resolveLfoAmount() around the
         * step-automation-or-retained base. Behavior unchanged by Step 9.
         */
        uint8_t depth;
        PresetMorphLfoDirection direction =
            instrumentManager_lfoDirectionDepth(lfo_value_0_1, polarity,
                                                amount, &depth);

        presetMorph_setVoiceLfoModulation(scene_getActiveIndex(),
                                          descriptor->voice_slot,
                                          source_slot, target_pair,
                                          direction, depth);
        return 1u;
    }
    case SCENE_MOD_TARGET_KIND_EFFECT_MORPH: {
        /*
         * Effect Morph `fxm` LFO (Session 072 step 9; plan §9 G4). It uses the
         * same encoding as voice Morph and resolves in effects_service() around
         * the Pattern-`fxm`, held-lane or retained base.
         */
        uint8_t depth;
        PresetMorphLfoDirection direction =
            instrumentManager_lfoDirectionDepth(lfo_value_0_1, polarity,
                                                amount, &depth);

        effects_setLfoContribution(source_slot, target_pair,
                                   EFFECT_LFO_TARGET_MORPH,
                                   (uint8_t)direction, depth);
        return 1u;
    }
```

The function's `float signed_depth; float magnitude; …` locals move into the
helper. Remove any locals left unused, so no `-Wunused-variable` warning
appears.

### 6.6 Install, restore, and per-sample update (modify)

**`instrumentManager_restoreLfoSupplementalTarget()`.** After
`presetMorph_clearLfoSource(source_slot, target_pair);` (line 2604), add:

```c
    /* Any Effect `fx`/`fxm` contribution from this pair ends too (Step 9). */
    effects_clearLfoSource(source_slot, target_pair);
```

`instrumentManager_clearAllRuntimeModulationTargets()` already calls this
for every source and pair, so Scene teardown clears the Effect LFO table.

**`instrumentManager_installLfoModulationTarget()`.** After the
`if (target_id == INSTRUMENT_PARAM_INVALID) { … return 1u; }` block (~2658)
and **before** the slot-decimation test, add:

```c
    if (effectTarget_isEffectId(target_id)) {
        /*
         * Effect parameter destination (Session 072 step 9; plan §10.3).
         * There is no ModulationNode pointer and no captured base.
         * updateLfoAdapters() feeds direction/depth entries that EffectsManager
         * resolves around the held value every block. The target is
         * re-validated against the active Scene's Effect type here; the
         * all-source rebind reaches this path after every Scene activation.
         */
        modNode_clearDestination(node);
        if (!effects_targetValid(scene_getActiveIndex(), target_id,
                                 INSTRUMENT_TARGET_MODULATION))
            return 0u;
        lfo_installed_targets[source_slot][target_index].kind =
            INSTALLED_MOD_TARGET_EFFECT;
        lfo_installed_targets[source_slot][target_index].target_id = target_id;
        return 1u;
    }
```

**`instrumentManager_updateLfoAdapters()`.** In the `switch
(installed->kind)` (2822), add before `default:`:

```c
    case INSTALLED_MOD_TARGET_EFFECT: {
        /* Effect parameter LFO: encode only; EffectsManager applies (Step 9). */
        uint8_t depth;
        PresetMorphLfoDirection direction =
            instrumentManager_lfoDirectionDepth(lfo_value_0_1, polarity,
                                                amount, &depth);

        effects_setLfoContribution(source_slot, target_pair,
                                   effectTarget_local(installed->target_id),
                                   (uint8_t)direction, depth);
        break;
    }
```

Extend the function comment's "Output:" sentence with ", and Effect adapters
(`fx` rows)".

The velocity switch (`instrumentManager_applyVelocityModulationTarget`) keeps
its `default:`. Velocity never installs the Effect kind (A21).

---

## 7. `menu.c`

### 7.1 LFO `fx` namespace: clamps and labels (modify)

In every upper-bound clamp of an LFO target-voice value, replace
`INSTRUMENT_TARGET_VOICE_SCENE` with `INSTRUMENT_TARGET_VOICE_NAMESPACE_LAST`:

| Line | Function |
|---|---|
| 3557–3558 | `menu_lfoTargetContext()` (`raw_voice > …`) |
| 3640–3641 | `menu_lfoTargetCommitVoiceAndReconcile()` |
| 3694–3695 | `menu_lfoTargetEditVoice()`; also its comment "clamped to 1..INSTRUMENT_TARGET_VOICE_SCENE, where the final value is displayed as `scn`" → "clamped to 1..INSTRUMENT_TARGET_VOICE_NAMESPACE_LAST (`scn` = 7, `fx` = 8)" |
| 4022–4023 | `menu_formatCellValue3()` DTYPE_VOICE_LFO |
| 4208–4209 | `menu_clampCellValue()` |
| 9394–9395 | `menu_repaintGeneric()` edit view |

The non-voice flag lines 3561–3565 and 3651–3655 become:

```c
    /* Non-voice namespaces (`scn`, `fx`) have no target slot. */
    ctx->target_is_scene =
        (uint8_t)(ctx->target_voice > INSTRUMENT_TARGET_VOICE_LAST);
```

(and the same with `next.`). The field is write-only today; the name is kept
to avoid touching the context struct.

**Labels.** At 4093–4096 (compact) and 9519–9523 (edit view):

```c
        if (menu_cellIsLfoTargetVoice(cell) &&
            value == INSTRUMENT_TARGET_VOICE_SCENE) {
            memcpy(valueAsText, "scn", 3);
        } else if (menu_cellIsLfoTargetVoice(cell) &&
                   value == INSTRUMENT_TARGET_VOICE_EFFECT) {
            /* Effect parameter namespace (Session 072 step 9). */
            memcpy(valueAsText, "fx ", 3);
        } else {
```

The 9519 site is the same, with `&cell` and `&editDisplayBuffer[1][13]`.

### 7.2 Target renderers (modify)

**`menu_formatInstrumentTargetShort()` (3894).** Insert at the top of the
body, before the `target == INSTRUMENT_PARAM_INVALID` guard:

```c
    /*
     * Effect destination (`fx` namespace, Session 072 step 9): the short label
     * of the active Scene's Effect row, or off when the row is no longer
     * modulatable (for example after `typ`, until the next rebind).
     */
    if (effectTarget_isEffectId(target)) {
        const effect_param_descriptor_t *effect_descriptor =
            effects_targetDescriptor(scene_getActiveIndex(), target,
                                     INSTRUMENT_TARGET_MODULATION);

        if (effect_descriptor)
            menu_copyPaddedField(valueAsText,
                                 effect_descriptor->base.short_name, 3u);
        else
            memcpy(valueAsText, menuText_off, 3);
        return;
    }
```

**`menu_displayInstrumentTargetFull()` (3942).** Insert at the top of the
body:

```c
    /* Effect destination: category and long name, like descriptor rows. */
    if (effectTarget_isEffectId(target)) {
        const effect_param_descriptor_t *effect_descriptor =
            effects_targetDescriptor(scene_getActiveIndex(), target,
                                     INSTRUMENT_TARGET_MODULATION);

        if (!effect_descriptor) {
            memcpy(&editDisplayBuffer[1][0], menuText_off, 3);
            return;
        }
        menu_copyPaddedField(&editDisplayBuffer[1][0],
                             effect_descriptor->base.category, 8u);
        menu_copyPaddedField(&editDisplayBuffer[1][8],
                             effect_descriptor->base.long_name, 8u);
        return;
    }
```

`menu_lfoTargetDisplayValue()` needs no change: it already expands through
`instrumentManager_lfoTargetIdFromToken()`, which now returns Effect IDs.

### 7.3 Scene Morph helper (add after `menu_morphAutomationExpand()`, 3239)

```c
/*
 * Scene Morph step-automation targets: voice Morph and Effect `fxm`.
 *
 * Both store seven bits in Pattern and expand to 0..255 at runtime
 * (seq_applySceneAutomation). The step editor must therefore cap the value
 * at 127 and display the expanded amount for both. Without this, fxm's 255
 * descriptor maximum would let a stored value exceed seven bits.
 */
static uint8_t menu_sceneTargetIsMorph(
    const scene_mod_target_descriptor_t *descriptor)
{
    return (uint8_t)(descriptor &&
        (descriptor->kind == SCENE_MOD_TARGET_KIND_VOICE_MORPH ||
         descriptor->kind == SCENE_MOD_TARGET_KIND_EFFECT_MORPH));
}
```

### 7.4 `fx` step-automation helpers (add after `menu_stepAutomationTargetUsed()`, ends 8605)

```c
/*
 * `fx` step-automation helpers (Session 072 step 9; plan §10.2, View A).
 *
 * Category 7 lists the viewed Scene's AUTOMATABLE Effect rows in registry
 * order. EffectsManager owns validity, and WIDE8 rows without expand7 never
 * appear. Pattern stores seven bits:
 * - WIDE8 rows display the expanded 0..255 value;
 * - all other rows use the ordinary dtype formatter, capped at the row's
 *   max_value.
 * PAT_AUTOMATION_TARGET_OFF (511) lies inside block 7's numeric range, so
 * every test excludes it first.
 */
static uint8_t menu_stepAutomationIsEffect(uint16_t target)
{
    return (uint8_t)(target != PAT_AUTOMATION_TARGET_OFF &&
                     effectTarget_isEffectId(target));
}

static const effect_param_descriptor_t *menu_stepAutomationEffectDescriptor(
    uint8_t scene, uint16_t target)
{
    return menu_stepAutomationIsEffect(target)
        ? effects_targetDescriptor(scene, target, INSTRUMENT_TARGET_AUTOMATION)
        : 0;
}

/* Nonzero for a WIDE8 row with an expansion hook. */
static uint8_t menu_effectAutomationIsWide(
    const effect_param_descriptor_t *descriptor)
{
    return (uint8_t)(descriptor &&
                     (descriptor->effect_flags & EFFECT_PARAM_FLAG_WIDE8) != 0u &&
                     descriptor->expand7);
}

/* Three-character value text for one `fx` entry (expanded for WIDE8). */
static void menu_formatEffectAutomationValue3(
    const effect_param_descriptor_t *descriptor, uint8_t value, char *buf)
{
    if (menu_effectAutomationIsWide(descriptor))
        numtostrpu(buf, descriptor->expand7(value), ' ');
    else
        menu_formatAutomationValue3(descriptor ? &descriptor->base : 0,
                                    value, buf);
}

/* Seven-bit editor maximum for one `fx` entry. */
static uint8_t menu_effectAutomationMax(
    const effect_param_descriptor_t *descriptor)
{
    uint8_t max_val;

    if (!descriptor || menu_effectAutomationIsWide(descriptor))
        return 127u;
    max_val = menu_automationValueMax(&descriptor->base);
    return (descriptor->max_value < max_val) ? descriptor->max_value : max_val;
}

/*
 * Step the `fx` target list while skipping targets already on this step.
 *
 * Inputs: viewed Scene, current target (off/INVALID starts before the first
 * row), signed direction, decoded list and the page being edited.
 * Output:
 * - the next unused Effect ID;
 * - INSTRUMENT_PARAM_INVALID (off) when moving backward past the first row,
 *   matching the `scn` path;
 * - otherwise `current` when no unused row lies in that direction.
 * Order and validity come from effects_stepTarget(); Menu owns only the
 * one-target-per-step filter.
 */
static instrument_param_id_t menu_stepAutomationEffectNext(
    uint8_t scene, instrument_param_id_t current, int8_t direction,
    const pat_automation_entry_t *autos, uint8_t count, uint8_t exclude)
{
    instrument_param_id_t candidate = menu_stepAutomationIsEffect(current)
        ? current : INSTRUMENT_PARAM_INVALID;
    uint8_t i;

    for (i = 0u; i < EFFECT_PARAM_COUNT; i++) {
        instrument_param_id_t next = effects_stepTarget(
            scene, candidate, direction, INSTRUMENT_TARGET_AUTOMATION);

        if (next == candidate)
            return current;
        if (next == INSTRUMENT_PARAM_INVALID)
            return (direction < 0) ? INSTRUMENT_PARAM_INVALID : current;
        if (!menu_stepAutomationTargetUsed(autos, count, next, exclude))
            return next;
        candidate = next;
    }
    return current;
}

/* Category + long name of an Effect row into the detail row (voice style). */
static void menu_stepAutomationEffectLabel(
    const effect_param_descriptor_t *descriptor)
{
    uint8_t i = 0u;
    uint8_t j = 0u;

    while (i < 14u && descriptor->base.category &&
           descriptor->base.category[i]) {
        editDisplayBuffer[1][2u + i] = descriptor->base.category[i];
        i++;
    }
    while (i < 14u && descriptor->base.long_name &&
           descriptor->base.long_name[j]) {
        editDisplayBuffer[1][2u + i] = descriptor->base.long_name[j++];
        i++;
    }
}
```

### 7.5 Category comment and current value (modify)

**`menu_stepAutomationCategory()` comment (8764–8770).** Replace "and 7 for
the empty Phase-5 fx category" with:

"and 7 for the `fx` Effect category (block 7, Session 072 step 9). The
Pattern off sentinel also lands in 7, which is why callers test off first."

The code is unchanged.

**`menu_stepAutomationCurrentValue()` (8798).**

- Add at the top of the body:

```c
    /*
     * `fx`: the viewed Scene's normal-image value in Pattern's seven-bit
     * domain. WIDE8 rows use the inverse of the linear expansion (D8).
     */
    if (menu_stepAutomationIsEffect(target)) {
        const effect_param_descriptor_t *descriptor =
            menu_stepAutomationEffectDescriptor(scene, target);
        uint8_t value;

        if (!descriptor)
            return 0u;
        value = effects_getParameter(scene, effectTarget_local(target),
                                     EFFECT_IMAGE_NORMAL);
        if (menu_effectAutomationIsWide(descriptor))
            return menu_morphAutomationStore(value);
        if (value > descriptor->max_value)
            value = descriptor->max_value;
        return (value > 127u) ? 127u : value;
    }
```

- In its Scene `switch`, add before `default:`:

```c
        case SCENE_MOD_TARGET_KIND_EFFECT_MORPH:
            /* `fxm` stores seven bits like voice Morph (Step 9). */
            return menu_morphAutomationStore(
                scene_getEffectMorphAmount(scene));
```

- Extend the function comment:

"Effect rows read the normal image; `fxm` converts the retained Effect Morph
amount with the voice-Morph halving."

### 7.6 `menu_stepAutomationEdit()` (modify)

**Field 2, the off-sentinel branch (8929–8947).** Replace the final
`} else { return 0u; }` of the `menu_stepAutoCategory` chain:

```c
            } else if (menu_stepAutoCategory == 7u) {
                /* `fx`: first unused AUTOMATABLE row of the viewed Effect. */
                new_target = menu_stepAutomationEffectNext(
                    scene, INSTRUMENT_PARAM_INVALID, 1, autos, count, page);
            } else {
                return 0u;
            }
```

**Field 2, target-kind chain.** After the
`} else if (sceneModTarget_isSceneTarget(old_target)) { … }` branch, before
the final `} else { return 0u; }`, add:

```c
        } else if (menu_stepAutomationIsEffect(old_target)) {
            new_target = menu_stepAutomationEffectNext(
                scene, old_target, inc, autos, count, page);
```

Update the field-2 comment: "…filtered Scene-target traversal for `scn`,
and registry-ordered Effect traversal for `fx` (Step 9)."

**Field 3 (8993–9015).** Replace the Scene max block and add the Effect
branch:

```c
        if (sceneModTarget_isSceneTarget(vt)) {
            const scene_mod_target_descriptor_t *scene_desc =
                sceneModTarget_descriptor(vt);
            if (scene_desc)
                max_val = menu_sceneTargetIsMorph(scene_desc)
                    ? 127u : (uint8_t)scene_desc->max_value;
        } else if (menu_stepAutomationIsEffect(vt)) {
            /* `fx`: seven-bit cap; menu tables and max_value bound the rest. */
            max_val = menu_effectAutomationMax(
                menu_stepAutomationEffectDescriptor(scene, vt));
        }
```

### 7.7 `menu_repaintStepAutomation()` (modify)

**Detail view, category sync (9118–9121).** Add a third arm:

```c
        else if (menu_stepAutomationIsEffect(target))
            menu_stepAutoCategory = 7u;
```

**Cursor 2 "Target Voice" (9122–9144).** After the `scn` branch, add:

```c
            } else if (menu_stepAutomationIsEffect(target)) {
                menu_copyPaddedField(&editDisplayBuffer[1][2], "fx", 3u);
```

**Cursor 3 "Target Parametr" (9145–9212).** Before the final
`} else { label = "Invalid"; … }`, add:

```c
            } else if (menu_stepAutomationIsEffect(target)) {
                const effect_param_descriptor_t *effect_descriptor =
                    menu_stepAutomationEffectDescriptor(scene, target);

                if (effect_descriptor) {
                    menu_stepAutomationEffectLabel(effect_descriptor);
                } else {
                    label = "Invalid";
                    label_width = 7u;
                }
```

**Amount (9230–9246).**

- Replace the Scene condition with `menu_sceneTargetIsMorph(scene_descriptor)`.
- Before the final `} else { menu_formatAutomationValue3(desc, …); }`, add:

```c
                } else if (menu_stepAutomationIsEffect(target)) {
                    menu_formatEffectAutomationValue3(
                        menu_stepAutomationEffectDescriptor(scene, target),
                        amt_value, amt_out);
```

**Compact view (9277–9336).**

- **Category sync (9282–9285).** Add
  `else if (menu_stepAutomationIsEffect(target)) menu_stepAutoCategory = 7u;`.
- **`voi`/`par` columns.** After the voice branch
  `} else if (valid && instrumentParam_isVoiceParameter(target)) { … }`, add:

```c
        } else if (menu_stepAutomationIsEffect(target)) {
            /* View A `fx` row: category plus the row's short label (§10.2). */
            const effect_param_descriptor_t *effect_descriptor =
                menu_stepAutomationEffectDescriptor(scene, target);

            memcpy(&editDisplayBuffer[1][5], "fx ", 3u);
            menu_copyPaddedField(&editDisplayBuffer[1][9],
                                 effect_descriptor
                                     ? effect_descriptor->base.short_name
                                     : "inv", 3u);
```

- **`amt` column (9326–9336).**
  - Replace the Scene condition with
    `menu_sceneTargetIsMorph(scene_descriptor)`.
  - Before the final `} else { menu_formatAutomationValue3(descriptor, …); }`,
    add:

```c
        } else if (menu_stepAutomationIsEffect(target)) {
            menu_formatEffectAutomationValue3(
                menu_stepAutomationEffectDescriptor(scene, target),
                autos[page].value, &editDisplayBuffer[1][13]);
```

Extend the function header comment's affiliates with "EffectsManager (`fx`
rows)".

### 7.8 ST8 F2: held `mrp` display (modify `menu_applyEffectMarkers()`, 2559–2562)

```c
        if (cell.fx.kind == MENU_FX_CELL_PARAM)
            va_formatValue3(&cell, value, field);
        else if (cell.fx.kind == MENU_FX_CELL_MORPH_AMOUNT)
            /* mrp is a plain 0..255 number; show the held lane lock (ST8 F2). */
            numtostrpu(field, value, ' ');
        else
            (void)menuEffects_formatValue3(&cell.fx, field);
```

---

## 8. `storageTypes.c`: the `fx` namespace survives reload (modify, 2098–2120)

In the comment, replace "Numeric value 7 is retained as the Scene namespace
displayed by Menu as `scn`" with:

```c
             * Numeric value 7 is the Scene namespace (`scn`) and 8 the Effect
             * namespace (`fx`, Session 072 step 9);
```

In the clamp:

```c
                else if (parsed > INSTRUMENT_TARGET_VOICE_NAMESPACE_LAST)
                    parsed = INSTRUMENT_TARGET_VOICE_NAMESPACE_LAST;
```

Kit/Instrument writers emit the stored number unchanged; no writer edit is
needed.

---

## 9. Documentation (same change set)

1. **`EFFECTS_BUS_FEATURE_PLAN.md`**
   - **§10.1** block-6 row: "404 = `fxm` … its runtime apply/overlay path
     remains a later step" → "404 = `fxm` Effect Morph (Pattern automation and
     LFO live since Step 9; Scene rule)".
   - **§10.1:** add after "`seq_effect_automation_dirty` (u64) tracks Effect
     overlays.":

     > "Realized in Step 9 as EffectsManager's `effects_automation.active`
     > (single owner, S072_ST9 D1)."

   - **§10.3:** replace the "`fxm` remains a retained Scene target with no
     runtime overlay path until Step 9" bullet with the live behavior.
   - **§16.1:**
     - item 4: add `effects_automation` 184 B;
     - item 5: 36 B, inside item 4's struct;
     - item 7: 2 B (+ latch 1 B from Step 8).
   - **§17.1** row 9 Gate column: "Built/clean-link verified; hardware matrix
     pending".
2. **`knowledge_files/specification_reference/SRAM_MANIFEST.md`**
   - Add a row: `EffectsManager.c: effects_automation` | 184 | per §3.3.
   - Add a row: `sequencer.c: seq_effectAutomationTracks/Reset` | 2.
   - Update the ST9 production link line and totals.
3. **`knowledge_files/specification_reference/PATTERN_DYNAMIC_STACK.md`**
   - Line 129: the target domain adds "or an Effect parameter ID 448..510
     (block 7, Session 072 step 9)".
   - Near 243 and 267, add the Effect restore rule:
     - FX step marker (identity bit 11);
     - ownership and end on the writing track's next automation step;
     - held on failed/muted steps;
     - cleared by the reset latch.
   - Add `fxm` to the Scene target list.
4. **`knowledge_files/specification_reference/FILESYSTEM_SPEC.md`**
   - At 1244–1249 and 1656, add namespace value 8 = `fx`: token = Effect-local
     row index, normalized against the Scene's Effect type at rebind.
5. **`MEMORY.md`**: add the "S072 Step 9" line after Step 8:
   - `fx` automation category with the end-of-automation rule;
   - `fxm` live;
   - LFO `fx` namespace (8);
   - the §9 resolution order in `effects_service()`.

---

## 10. Build and verification gates

### Build

1. `make clean && make all` succeeds with no new warnings. In particular,
   check the `updateLfoSceneDestination` locals after the §6.5 move.
2. `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`:
   - Record the flash delta: expected +2.6 to +3.6 KB, leaving about 8.8 to
     9.8 KB of headroom.
   - `.bss` about +186 B.
   - DTCM and FXBUF are unchanged.

### Hardware

Setup: a Scene with `flt`, one voice's FX send raised, and track 1 playing
steps 1–16.

3. **`fx` category.**
   - STEP page → automation → add → VOI `fx`.
   - PAR cycles `out vol pan frq res drv typ`. A duplicate on the same step
     is skipped, and backward past `out` returns to off.
   - AMT domains: `frq` 0..127; `pan` ±63; `typ` filter names (max 7);
     `out` route names (max 5).
4. **End rule (F1).**
   - Track 1 step 1 `frq` = 10, nothing on steps 2–16: the filter closes on
     step 1 and returns to the menu value on step 2.
   - Steps 1–4 all carry `frq`: it holds through 4 and releases on 5.
   - An entry on a non-trigger step applies.
5. **Fallback to the FX lock (A18).**
   - FX `fwd` 1/16; lock `frq` = 120 on FX step 2.
   - Track 1 step 1 `frq` = 10: step 1 plays 10, and step 2 plays 120 (the
     lock), not the menu value.
   - Remove the lock: step 2 plays the menu value.
6. **Two tracks (G3).** Track 1 step 1 `frq` = 10 and track 2 step 1
   `frq` = 100: step 1 plays 100. At step 2 (neither track carries `frq`) it
   falls back.
7. **Probability hold (D3).** On a probability-25 step carrying `frq` after a
   `frq` step: a failed roll keeps the previous overlay.
8. **Reset.** Stop/start and a Pattern change each clear all Effect overlays
   and the `fxm` override. The first step of the new pass still applies.
9. **`fxm` automation (D5).**
   - It appears in the `scn` list as `fxm`, and AMT shows the expanded 0..255
     (max stored 127 → 255).
   - Morph moves to the endpoint and holds until stop or a Pattern change.
   - While active, FX Morph-lane locks are masked (G4 consequence).
10. **LFO `fx` (§10.3).**
    - Voice 1 LFO DstVoice `fx`, Param `frq`, amount 127: an audible sweep in
      positive, negative and bipolar polarity, like a voice-filter LFO.
    - Amount 0 does nothing.
    - DstVoice cycles `1…6 scn fx`.
11. **Together (the plan gate).**
    - Set up all three on `frq`:
      - Pattern `frq` = 20 on track 1 step 1;
      - FX lock `frq` = 110 on FX step 5;
      - LFO `frq` bipolar, small amount.
    - Expected: the wobble is centered on 20 at Pattern step 1, on 110 at FX
      step 5 (when step 5 carries no Pattern `frq`), and on the menu value
      otherwise.
12. **LFO `fxm`.** Through `scn` → `fxm`: Morph sweeps around the
    retained/held/automated base.
13. **Rebind (D7).**
    - With LFO → `fx` `drv`, switch to a Scene whose Effect is `off`: that
      Scene's pair normalizes to off.
    - Back in the `flt` Scene, its own pair is intact.
    - Change `typ` to `off` on the live Scene: modulation stops and the
      display shows off.
    - `vol`/`pan` destinations (common rows) survive a type change.
14. **Kit Save/Load.** An `fx` destination survives the round trip
    (`lfo_target_voice 8`, D11).
15. **Step 8 fixes.**
    - `sel`: select step 5 while stopped, press play: step 5 keeps applying,
      and the Morph-lane lock re-latches.
    - A held `mrp` lock shows its lock value (F2).
16. **Regression.**
    - Voice automation and restore-on-trigger; voice LFO on a descriptor.
    - Voice-Morph LFO (the §6.5 refactor must sound identical).
    - `scn` targets (Morph, decimation, FX send, audio out); FX send step
      automation.
    - The Step 8 matrix spot-check.
17. **CPU.** With `flt`, two LFOs → `fx`, and six `fx` Pattern entries, the
    `cpu` widget matches Step 8 within noise.
18. **P1–P3.**
    - Blank-name Scene Save writes `' .fx'`.
    - Kit Save leaves the Effect source row unchanged.
    - A SELECT press during a `typ` edit abandons the edit.

### Not testable yet

- WIDE8 expansion: no `flt` row uses it; covered by review.
- Fan-out (Step 10).
- Lock removal (A15).

### Rollback

Revert the change set. No retained format changes:

- Pattern entries at 448..510 written during the test are ignored by an older
  build: the drain skips non-voice/non-Scene IDs.
- The Kit byte 8 clamps to `scn` on older parsers.

---

## 11. Implementation notes

### 11.1 Code checkpoint

- Implemented EffectsManager's 184-byte `effects_automation` owner block:
  Effect Pattern overlays, owner-track end candidates, `fxm` reset overlay,
  and 6 × 2 base-independent LFO entries.
- Implemented block-7 validation/traversal, `fxm` Scene automation, the
  Sequencer FX step-marker/reset handshake, and foreground overlay drain.
- Added InstrumentManager's namespace value 8 (`fx`) and Effect LFO adapter;
  moved voice/Effect Morph direction-depth encoding into one helper.
- Added Menu `fx` category editing/display, WIDE8-aware seven-bit handling,
  `fxm` Morph display handling, storage clamp, and P1–P3 prerequisite fixes.
- All new public/private runtime additions carry adjacent descriptive
  comments in their `.c`/`.h` locations. Existing ST8 changes were preserved.

### 11.2 First link measurement

`make all` completed successfully after the ST9 source changes. The final
clean link is `text=482,632`, `data=416`, `bss=426,336`, with a 483,048-byte
flash payload. `tools/link_budget.py` reports 483,048 / 491,520 bytes used and
8,472 bytes headroom; ITCM is 3,768 bytes, DTCM statics are 4,448 bytes, and
the FXBUF is unchanged at 126,624 bytes with a 3,744-byte margin. The first
incremental build exposed one new `-Wsequence-point` warning in the Menu
Effect-label helper; that expression was corrected before the final clean
verification pass.

Hardware acceptance remains pending; the matrix in §10 is the next check.

### 11.3 Final verification

- `git diff --check` is clean.
- Required clean `make clean && make all` completed; the final build produced
  `build/lxr02.elf`, `build/lxr02.bin`, and `build/LXRV2_lxr02.img`.
- `make img` wrapped the final payload successfully at 483,048 bytes.
- No new compiler warning remains from the ST9 changes; remaining build
  warnings are pre-existing project/toolchain warnings.

---

## 12. Assessment (2026-09-28)

### 12.1 Result

**Accepted for hardware testing.** One low-severity behavioral deviation
(F1) should be fixed; it is a one-line change. The rest are cosmetic or
notes. The code follows the schedule closely, and prerequisites P1–P3 and the
ST8 F1/F2 fixes are all in.

### 12.2 Build

I ran a clean rebuild (`make clean && make all`) independently; it exited 0.

```text
text 482632   data 416   bss 426336   dec 909384
Flash 483,048 / 491,520 B   headroom 8,472 B   (Step 8 → Step 9: +3,912 B)
ITCM 3,768 / 16,384 B       DTCM statics 4,448 B
FXBUF 126,624 B at 0x20001160, margin 3,744 B (unchanged)
```

- **Warnings:** the same 20 pre-existing lines as Step 8, and nothing from
  any file this step touched. The `-Wsequence-point` noted in §11.2 is gone.
- **Flash:** +3,912 B is about 300 B above the §0.3 upper estimate (3.6 KB).
  Headroom is now **8,472 B**. Step 10 (fan-out, estimated 1 to 1.5 KB) fits,
  but every later step should be sized against this figure.
- **`.bss`:** +176 B, against the expected +186 B. The difference is
  alignment padding absorbed by neighboring objects. The `_Static_assert` pins
  `effects_automation_t` at 184 B.

### 12.3 Code against schedule

| § | Area | Status | Notes |
|---|---|---|---|
| 2 | P1 blank-name `.fx` stem (both phases) | ✔ | |
| 2 | P2 Kit Save no longer touches the Effect row | ✔ | The five lines are removed. |
| 2 | P3 SELECT abandons `typ` | ✔ | |
| 3.1 | ST8 F1: `sel` always applies | ✔ with deviation | See **F1**. |
| 3.2–3.3 | API and the 184 B state struct | ✔ | As scheduled. The long comment blocks are condensed to one-liners (F4). |
| 3.4 | `effects_targetValid/Descriptor/stepTarget` | ✔ | Walk semantics match `instrumentManager_stepTargetForSlot()`. |
| 3.5 | Overlay API (publish owners, clear, StepBegin/Flush, apply, `fxm`) | ✔ | WIDE8 expand, `max_value` clamp, last-writer ownership, and candidate removal are all correct. |
| 3.6 | LFO table, resolver, mask | ✔ | The resolver is the S071 integer math with the min generalization. |
| 3.7 | init / activateScene / changeType hooks | ✔ | Type change keeps the `fxm` override (D4). |
| 3.8 | `effects_service()` §9 order | ✔ | Order: Morph base (Pattern `fxm` > held lane > retained) → LFO `fxm` → interpolate → FX lock → Pattern overlay → LFO within the domain → `max_value` → buffer clamp. The `sel` Morph re-latch is present. |
| 4 | `fxm` flags LFO + AUTOMATION, no velocity | ✔ | |
| 5.1–5.2 | Owner/reset latches, reset set inside `seq_fxPublishReset` PRIMASK | ✔ | |
| 5.3 | Marker producer; marker before entries on the automation gate | ✔ | The modified `if` block is space-indented inside a tab-indented function (cosmetic). |
| 5.4 | `fxm` apply (`effect_expand7Linear`), no-op restore case | ✔ | |
| 5.5 | Drain: reset before the early return, marker branch, Effect branch, flush after PRIMASK | ✔ | The track decode `(identity & 0x3FF) / NUM_STEPS` is correct for tracks 0..6. |
| 6.1–6.6 | Namespace 8, AUTOMATION-only `targetValid` branch, three token functions, shared encoder, `fxm` case, restore clear, install, adapter | ✔ | The voice-Morph case is the old body moved verbatim, and the `_Static_assert` ties the encodings together. |
| 7.1–7.2 | LFO clamps, `fx` labels, short/full renderers | ✔ | The only remaining `INSTRUMENT_TARGET_VOICE_SCENE` uses are equality tests, which is correct. |
| 7.3–7.7 | `fx` category helpers, current value, fields 2 and 3, render | ✔ | `fxm` shares voice-Morph 7-bit storage, max and display through `menu_sceneTargetIsMorph()`. See F2. |
| 7.8 | ST8 F2: held `mrp` display | ✔ | |
| 8 | Kit parser clamp to 8 | ✔ | |
| 9 | Docs (plan, SRAM_MANIFEST, PATTERN_DYNAMIC_STACK, FILESYSTEM_SPEC, MEMORY) | ✔ | |

`presetManager.c` and `PatternData.c` are untouched, as planned. The
Scene-activation rebind validates `fx` tokens through the extended
`instrumentManager_lfoTargetIdFromToken()`.

### 12.4 Findings

**F1 (low, behavior): selecting an unlocked step in `sel` drops the held
Morph.**

- **Where:** `effects_seqSelect()` (`EffectsManager.c` ~654).
- **What changed:** the function now clears `held_morph_valid` and leaves
  `effects_service()` to re-latch the Morph lock. The Step 8 code and the §3.1
  schedule called `effects_seqLatchMorph(record, step)` instead.
- **Effect:**
  - Selecting a step **with** a Morph lock behaves as before.
  - Selecting a step **without** one now returns Effect Morph to the retained
    `mrp`. Plan §9 says a Morph-lane lock "holds through later unlocked steps
    … until another Morph lock replaces it, or until the Scene-rule restore".
  - In `sel`, choosing an unlocked step is the equivalent of playing one, so
    the previous lock should hold.
- **Fix:** replace `effects_state.held_morph_valid = 0u;` with
  `effects_seqLatchMorph(record, step);`. That function writes only when the
  selected step has a Morph lock. Keep the `effects_service()` re-latch, which
  still covers RESET and Scene switches.

**F2 (cosmetic): `fx` detail labels truncate.**

- **Where:** `menu_stepAutomationEffectLabel()`.
- **What happens:** it inserts a space between the category and the long name
  (Scene style). The field is 14 characters, so `Filter Frequncy` shows as
  `Filter Frequnc` and `Effect AudioOut` as `Effect AudioOu`.
- **Why:** the schedule used the voice-row style with no separator, where both
  fit.
- **Fix:** drop the `if (i < 14u) … = ' ';` line, or accept the truncation.

**F3 (note): flash.** The measured +3,912 B leaves **8,472 B**. See §12.2.

**F4 (cosmetic): condensed comment blocks.** Most scheduled comment blocks
became one-line summaries. Examples:

- `effects_applyAutomation`;
- `effects_automationStepBegin`;
- the Step 9 state struct;
- the `lfoDirectionDepth` rationale;
- the drain header.

They are accurate. The fuller rationale lives in this document and in the
updated PATTERN_DYNAMIC_STACK / SRAM_MANIFEST text. No action is required
unless you want the full blocks in the source.

I found no correctness defects in these areas:

- the marker/entry ordering;
- the reset-latch ordering;
- the owner-byte publication;
- the WIDE8 path;
- the namespace clamps;
- the Pattern-writer validation, which is AUTOMATION-only, so modulation
  backends are unaffected.

### 12.5 Hardware status

The §10 hardware matrix (gates 3–18) is pending. Gate 15 (`sel` after play,
held `mrp` display) now exercises the ST8 fixes. With F1 unfixed, one extra
case in gate 15 will show the deviation: select a Morph-locked step, then an
unlocked one, and Morph returns to the retained `mrp`.
