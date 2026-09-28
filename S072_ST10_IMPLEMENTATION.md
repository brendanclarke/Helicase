# S072 Step 10: edit-mask selection gate and Effect fan-out (implementation schedule)

This schedule implements Step 10 of `EFFECTS_BUS_FEATURE_PLAN.md` §17.1:

> Edit-mask selection gate and Effect fan-out.
> Gate: mismatch rejection; fan-out correctness.

- **Authority:** plan §7.2 item 8, §7.4, §13.6, and decisions A3, A44, F5,
  G5; risk 9 (§17.2); and `S072_ST7` D3, which routes every Effect write
  through EffectsManager so the mask loop lives in one place.
- **Baseline:** the reviewed Step 9 tree (`S072_ST9_IMPLEMENTATION.md` §12).
  - text 482,632 B; flash 483,048 / 491,520 B;
  - **headroom 8,472 B**;
  - DTCM statics 4,448 B; FXBUF margin 3,744 B.
- **Code changes:** every change is listed below by file, current line and
  action (add / modify / remove). Each comes with the comment block to place
  in the source.
- **Line numbers** refer to the current tree. The named anchors are the
  authority, because earlier edits in a file shift the lines below them.

---

## 0. Decisions and notes

### What exists today (deep-dive summary)

- **The masks.**
  - Each resident Scene owns one 16-bit VOICE edit mask
    (`bank_scene_mask_voice_edit[16]`, `BankData.c:21`). The active Scene's
    entry is the fan-out set.
  - `bank_sceneMaskVoiceEdit()` returns it, and self-repairs the active-bit
    invariant, marking AutoSave on repair.
  - `bank_toggleSceneMaskVoiceEdit()` states: "Menu owns compatibility checks
    before allowing a Scene to be toggled on." No such check exists yet
    (`menu_voiceHeldSceneButtonPressed()`, `menu.c:6471`).
- **Mask users.** All of them read the active entry:
  - VOICE instrument, Kit-setting and Scene-setting commits
    (`menu_cellCommitValue()`, `menu.c` ~3368–3450);
  - PERF `mrp`, per-voice Morph and `srt` (`menu.c` ~12777–12836);
  - Kit/Instrument Load destination masks through Preset;
  - the VOICE-held LED view.
- **PERF `mrp` already fans out to Effect Morph.** The `PAR_MORPH` loop calls
  `preset_morphScene()` for every masked Scene, and that sets
  `scene_setEffectMorphAmount()` (`presetManager.c:3014`, A3). **No Step 10
  change is needed there.**
- **Effect writes.** Every user-facing Effect write already goes through
  EffectsManager: `effects_setParameter`, `effects_setSeqRunMode`,
  `effects_setSeqLength`, `effects_setSeqStepScale`, `effects_setMorphAmount`,
  `effects_setSeqLaneLock` and `effects_changeType`.
  - Their only callers are in `menuEffects.c`, which passes
    `scene_getActiveIndex()`.
  - Preset and AutoSave write SceneData directly, not through these functions.
- **Type-changing commits.** There is no single SceneData funnel for slot
  types: loaders `memcpy` whole kits, AutoSave writes `instrument->type`, and
  `instrumentManager_resetSlot()` is called from several places. There are,
  however, four commit **completion** funnels:

  | Funnel | Covers |
  |---|---|
  | `menu_startSoundApply()` (`menu.c:513`) | Kit, Scene, Bank (non-empty), All and Performance Load completions, including the pre-audio boot-synchronous path |
  | `menu_startInstrumentApply()` (`menu.c:691`) | Instrument Load. It commits synchronously to Preset's destination mask. |
  | the empty-Bank branch of `PRESET_OP_BANK_LOAD` (`menu.c` ~11616) | a Bank with no Scene; the masks come from `bankset.bcg` |
  | `main.c:1296`, just before `preset_startDrumsetApply()` | end of boot, after AutoSave/Bank/Scene restore of both types and masks |

  The Effect type changes only in `effects_changeType()`, from the `typ`
  click-out.

### D1: fan-out lives inside the public EffectsManager setters (ST7 D3)

- Each public setter becomes a thin loop over the Scenes an edit reaches.
- The old body becomes a `static` single-Scene worker, suffixed `Scene`.
- **Callers are unchanged.** `menuEffects.c` is not edited for fan-out.
- **Reach.**
  - If the edit originates from the **active** Scene, it reaches the active
    Scene's VOICE edit mask (`bank_sceneMaskVoiceEdit()`) plus the origin.
  - A future edit of an inactive Scene writes that Scene only. This matches
    VOICE editing, which always uses the active mask.
- **Return value.** The OR of the per-Scene "changed" results, so the caller
  still knows whether to repaint.

### D2: per-edit Effect-type guard (defense against risk 9)

- Parameter, lane-lock and sequence-setting fan-out skip any masked Scene
  whose Effect type differs from the origin's. Local descriptor and lane
  indices only mean the same thing between Scenes of the same type.
- After D4 this never triggers. It only guards against a commit path added
  later without a re-validation hook.
- **Exemptions:**
  - `effects_setMorphAmount()` is type-agnostic;
  - `effects_changeType()` must reach every masked Scene, because that is what
    keeps them matched (F5).
- **Cost:** 16 byte compares per edit.

### D3: the selection gate (§7.4)

- **Match rule.** `scene_editLayoutMatches(a, b)` (SceneData) is true when:
  - both Scenes exist;
  - `effect.type` is equal;
  - all six `kit.instruments[slot].type` are equal slot by slot.

  The raw Effect type byte is compared. Every loader and restore path already
  validates the token against the registry, and SceneData must not depend on
  EffectsManager.
- **Gate.** `menu_voiceHeldSceneButtonPressed()` **rejects turning a bit on**
  when the Scene does not match the active Scene:
  - no toggle;
  - no flash;
  - the SEQ LED stays unlit (the plan's "the SEQ LED stays unlit").

  Turning a bit **off** is always allowed. The active Scene always matches
  itself.
- **Scope.** This is a deliberate change to general VOICE-mask behavior
  (plan §7.4, last bullet). It governs VOICE, Kit-setting, Scene-setting and
  PERF Morph fan-out too, not only Effect edits.

### D4: re-validation after divergence (F5)

- `bank_revalidateVoiceEditMasks()` (BankData) walks all 16 owners. For each
  owner it drops every member bit (other than itself) whose Scene no longer
  matches that owner.
- Dropped bits are stored through `bank_setSceneMaskVoiceEditForScene()`,
  which marks the Bank VOICE-mask AutoSave field only on change. The VOICE-held
  LED view reads the live mask, so the LEDs go out with no extra call.
- **Call sites:**
  - the four completion funnels above;
  - the end of the public `effects_changeType()`.
- **Why `effects_changeType()` needs it.** Masks are directional. Scene X's
  mask can contain the active Scene A while A's mask does not contain X. When
  A changes type and fans out to its own mask, X's stale bit must still be
  dropped.
- **Cost:** at most 16 × 15 × 7 byte compares, only at commit time. This is
  risk 9's "re-validation runs on every type-changing commit path", made
  robust by hooking **completion funnels** rather than the many internal
  writers.

### D5: boot and AutoSave-restore timing

- The `main.c` hook runs after every pre-audio restore: AutoSave Case 1/2,
  `bankset.bcg`, and Scene/Kit fallback.
- If AutoSave tracking is still off at that point, a dropped bit is repaired
  in RAM only. The next boot repeats the same deterministic repair, and the
  first later mask write persists it. That is acceptable, because there is no
  observable divergence.
- Whether tracking is on at that line is recorded in §11.

### D6: §7.2 item 7 (`fx` LFO re-validation on type change)

- This is already satisfied functionally by Step 9 D7:
  - an invalid token contributes nothing at runtime;
  - it displays as `off`;
  - it is normalized at the next Scene-activation rebind.
- Step 10 adds nothing here (G5: no Preset change).

### D7: PERF Morph and other mask users

- These are unchanged. They automatically become type-safe because the masks
  they read are now gated and re-validated.

### D8: prerequisite fixes from the Step 9 review (P1, P2)

These are still unapplied in the tree:

- **ST9 F1:** `effects_seqSelect()` must latch rather than invalidate.
- **ST9 F2:** the `fx` detail label separator.

Both are listed in §2 so they land with this change set. P1 is in the same
file as the D1 edits.

### D9: out of scope

- Scene copy/clear (the later copy pass, §7.3);
- MIDI (A20);
- Library Load/Save UI (A35).

### 0.1 Code outside `Core/DSP/Effects` and `menuEffects` (G5 check)

| File | Why it is required |
|---|---|
| `SceneData.c/.h` | the §7.4 predicate ("via a BankData/SceneData predicate") |
| `BankData.c/.h` | F5 re-validation over the masks it owns |
| `menu.c` | the §7.4 gate in `menu_voiceHeldSceneButtonPressed()`; re-validation calls at the load-completion funnels; ST9 F2 |
| `main.c` | F5 "boot or AutoSave restore of masks" re-validation |

### 0.2 RAM

**None.** No new statics. `effects_state_t`, `effects_automation_t` and the
BankData arrays are unchanged.

### 0.3 Flash estimate

| Area | Estimate |
|---|---|
| EffectsManager: 7 fan-out wrappers + mask helper | +0.40 to +0.55 KB |
| SceneData predicate | +0.06 to +0.10 KB |
| BankData re-validation | +0.10 to +0.15 KB |
| menu.c gate + 3 hooks | +0.06 to +0.10 KB |
| main.c hook | about +0.01 KB |
| P1/P2 | about 0 |
| **Total** | **+0.65 to +0.9 KB** |

Headroom after Step 10 is expected at **about 7.6 to 7.8 KB**.

---

## 1. Change index

| # | File | Location (current) | Action | § |
|---|---|---|---|---|
| P1 | `Core/DSP/Effects/EffectsManager.c` | `effects_seqSelect()` line 656 | modify (ST9 F1) | 2 |
| P2 | `Core/Menu/menu.c` | `menu_stepAutomationEffectLabel()` line 8742 | remove (ST9 F2) | 2 |
| 1 | `Core/Bank/Scene/SceneData.h` | after `scene_instrumentSlotConst()` prototype (~305) | add | 3 |
| 2 | `Core/Bank/Scene/SceneData.c` | after `scene_instrumentSlotConst()` definition (~222) | add | 3 |
| 3 | `Core/Bank/BankData.h` | after `bank_toggleSceneMaskVoiceEdit()` prototype (56) | add | 4 |
| 4 | `Core/Bank/BankData.c` | after `bank_toggleSceneMaskVoiceEdit()` (ends ~426) | add | 4 |
| 5 | `Core/Bank/BankData.c` | `bank_toggleSceneMaskVoiceEdit()` comment | modify | 4 |
| 6 | `Core/DSP/Effects/EffectsManager.h` | edit-API block comment 230–245; lock-API comment | modify | 5.1 |
| 7 | `Core/DSP/Effects/EffectsManager.c` | includes (16–21) | add | 5.2 |
| 8 | `Core/DSP/Effects/EffectsManager.c` | before `effects_setParameter()` (~465) | add fan-out helper | 5.3 |
| 9 | `Core/DSP/Effects/EffectsManager.c` | `effects_setParameter()` 474, `effects_setSeqRunMode()` 505, `effects_setSeqLength()` 517, `effects_setSeqStepScale()` 529, `effects_setMorphAmount()` 541 | modify (rename + wrap) | 5.4 |
| 10 | `Core/DSP/Effects/EffectsManager.c` | `effects_setSeqLaneLock()` 701 | modify (rename + wrap) | 5.5 |
| 11 | `Core/DSP/Effects/EffectsManager.c` | `effects_changeType()` 1093 | modify (rename + wrap + re-validate) | 5.6 |
| 12 | `Core/Menu/menu.c` | `menu_voiceHeldSceneButtonPressed()` 6471 | modify | 6.1 |
| 13 | `Core/Menu/menu.c` | `menu_startSoundApply()` 513 | add | 6.2 |
| 14 | `Core/Menu/menu.c` | `menu_startInstrumentApply()` 691 | add | 6.3 |
| 15 | `Core/Menu/menu.c` | `PRESET_OP_BANK_LOAD` empty-Bank branch, after `preset_ackStatus();` (11616) | add | 6.4 |
| 16 | `main.c` | before `preset_startDrumsetApply();` (1296) | add | 7 |
| 17 | docs | see §8 | modify | 8 |

`menuEffects.c` is **not** modified. Its calls already carry
`scene_getActiveIndex()`, and the fan-out happens inside the setters (D1).

---

## 2. Prerequisite fixes (P1, P2)

**P1 (ST9 §12.4 F1).** In `effects_seqSelect()` (`EffectsManager.c` 654–657),
replace:

```c
    /* Service re-latches the newly selected Morph lock on its next pass. */
    if (record->seq_run_mode == EFFECT_SEQ_RUN_SEL)
        effects_state.held_morph_valid = 0u;
```

with:

```c
    /*
     * A Morph lock on the selected step replaces the held value. An unlocked
     * selection keeps the previous lock, per the plan §9 held-lane rule.
     * effects_service() still re-latches after RESET or a Scene switch.
     */
    if (record->seq_run_mode == EFFECT_SEQ_RUN_SEL)
        effects_seqLatchMorph(record, step);
```

**P2 (ST9 §12.4 F2).** In `menu_stepAutomationEffectLabel()`
(`menu.c` 8741–8742), delete:

```c
    if (i < 14u)
        editDisplayBuffer[1][2u + i++] = ' ';
```

Category and long name then run together, as the voice rows do, and fit the
14-character field (`FilterFrequncy`). If you prefer to keep the separator and
accept truncation, skip P2 and note it in §11.

---

## 3. SceneData: layout-match predicate (add)

### 3.1 `SceneData.h`

Add after the `scene_instrumentSlotConst()` prototype (~line 305):

```c
/*
 * Report whether two resident Scenes share one edit layout (plan §7.4).
 *
 * Inputs: two Scene indices. Output: nonzero when both exist, their Effect
 * record types are equal, and all six Kit instrument slot types are equal
 * slot by slot.
 *
 * Why: a VOICE edit-mask fan-out writes the same descriptor or lane index into
 * every masked Scene. That index names the same parameter only when the
 * layouts match, so this is the one rule behind the mask selection gate and
 * its re-validation (A44, F5).
 *
 * The raw Effect type byte is compared. Loaders and AutoSave validate the
 * token before commit, and SceneData stays independent of EffectsManager.
 * Clients: menu_voiceHeldSceneButtonPressed() and
 * bank_revalidateVoiceEditMasks(). Read-only; no AutoSave effect.
 */
uint8_t scene_editLayoutMatches(uint8_t scene_a, uint8_t scene_b);
```

### 3.2 `SceneData.c`

Add after the `scene_instrumentSlotConst()` definition (~line 222):

```c
uint8_t scene_editLayoutMatches(uint8_t scene_a, uint8_t scene_b)
{
    const scene_t *a = scene_getConst(scene_a);
    const scene_t *b = scene_getConst(scene_b);
    uint8_t slot;

    /* Contract in SceneData.h; identical indices trivially match. */
    if (!a || !b)
        return 0u;
    if (scene_a == scene_b)
        return 1u;
    if (a->effect.type != b->effect.type)
        return 0u;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        if (a->kit.instruments[slot].type != b->kit.instruments[slot].type)
            return 0u;
    }
    return 1u;
}
```

---

## 4. BankData: mask re-validation (add/modify)

### 4.1 `BankData.h`

Add after `void bank_toggleSceneMaskVoiceEdit(uint8_t scene_index);`
(line 56):

```c
/*
 * Drop VOICE edit-mask members whose layout no longer matches (plan §7.4, F5).
 *
 * What: for each of the 16 owner Scenes, every member bit other than the owner
 * itself is kept only while scene_editLayoutMatches(owner, member) holds. A
 * changed entry is stored through bank_setSceneMaskVoiceEditForScene(), which
 * marks the Bank VOICE-mask AutoSave field only on change. The active-bit
 * invariant is preserved, because an owner always matches itself.
 *
 * Why: the selection gate only prevents new mismatches. Any later commit that
 * changes an Instrument or Effect type in one Scene can make existing members
 * diverge, and fan-out would then write one descriptor index into a different
 * parameter.
 *
 * When: Menu's load-completion funnels (Kit/Scene/Bank/All/Performance/
 * Instrument), the end of boot restore, and effects_changeType(). Foreground
 * only. At most 16 × 15 layout compares, and only at commit time.
 */
void bank_revalidateVoiceEditMasks(void);
```

### 4.2 `BankData.c`

Add after `bank_toggleSceneMaskVoiceEdit()` (ends ~line 426):

```c
void bank_revalidateVoiceEditMasks(void)
{
    uint8_t owner;

    /* Contract in BankData.h (Session 072 step 10; plan §7.4 F5). */
    for (owner = 0u; owner < BANK_SCENE_SLOT_COUNT; owner++) {
        uint16_t mask = bank_scene_mask_voice_edit[owner];
        uint16_t kept = mask;
        uint8_t member;

        for (member = 0u; member < BANK_SCENE_SLOT_COUNT; member++) {
            uint16_t bit = bank_sceneBit(member);

            if (member == owner || (mask & bit) == 0u)
                continue;
            if (!scene_editLayoutMatches(owner, member))
                kept = (uint16_t)(kept & (uint16_t)~bit);
        }
        if (kept != mask)
            bank_setSceneMaskVoiceEditForScene(owner, kept);
    }
}
```

`bank_sceneBit()` is the existing file-local helper, defined above.

### 4.3 `bank_toggleSceneMaskVoiceEdit()` comment (modify)

Replace "Menu owns compatibility checks before allowing a Scene to be toggled
on." with:

```c
     * Menu owns the layout gate before a Scene is toggled on
     * (scene_editLayoutMatches(), Session 072 step 10). This function stays
     * policy-free so restore paths can use the indexed setter.
```

---

## 5. EffectsManager: fan-out inside the edit API

### 5.1 `EffectsManager.h` comments (modify)

**Edit-API block (230–245).** Replace the "Why here: … Step 7 writes the given
Scene only." sentences with:

```c
 * Why here: plan §13.6 routes edits through EffectsManager so the edit-mask
 * fan-out lives in one place (S072_ST7 D3).
 *
 * Fan-out (Session 072 step 10; plan §7.4, A44): an edit of the active Scene
 * reaches every Scene in its VOICE edit mask. Parameter, lock and sequence
 * edits also require the target Scene's Effect type to equal the origin's
 * (S072_ST10 D2). Effect Morph and type changes reach every masked Scene, and
 * a type change then re-validates all masks (F5). An inactive-Scene edit
 * writes that Scene only. Setters return nonzero when any reached Scene
 * changed.
```

**Lock API (the `effects_setSeqLaneLock` line in the Step 8 block).** Add:

```c
 * effects_setSeqLaneLock() fans out like the edit API: the same held-step mask
 * and lane are written into every same-type masked Scene.
```

### 5.2 Includes (add)

After `#include "SceneData.h"` (line 16), add:

```c
#include "BankData.h"
```

### 5.3 Fan-out helper (add before `effects_setParameter()`, ~line 465)

```c
/*
 * Resolve which resident Scenes one Effect edit reaches (plan §7.4, A44).
 *
 * Inputs: origin Scene and whether the Effect type must match.
 * Output: a Scene bit mask that always contains the origin when it exists.
 * - An active-Scene origin adds its VOICE edit mask. bank_sceneMaskVoiceEdit()
 *   self-repairs the active-bit invariant.
 * - With match_type set, masked Scenes whose Effect type differs from the
 *   origin's are removed (S072_ST10 D2). After gate and re-validation this is
 *   a no-op; it only protects against a future unhooked commit path, where a
 *   shared local index would address a different parameter.
 * - An inactive origin reaches itself only, matching VOICE editing.
 * Foreground only; 16 byte compares at most.
 */
static uint16_t effects_fanoutMask(uint8_t scene_index, uint8_t match_type)
{
    const effect_record_t *origin = scene_effectConst(scene_index);
    uint16_t mask;
    uint8_t s;

    if (!origin || scene_index >= SCENE_COUNT)
        return 0u;
    mask = (uint16_t)(1u << scene_index);
    if (scene_index == scene_getActiveIndex())
        mask = (uint16_t)(mask | bank_sceneMaskVoiceEdit());
    if (!match_type)
        return mask;
    for (s = 0u; s < SCENE_COUNT; s++) {
        const effect_record_t *record;

        if (s == scene_index || (mask & (uint16_t)(1u << s)) == 0u)
            continue;
        record = scene_effectConst(s);
        if (!record || record->type != origin->type)
            mask = (uint16_t)(mask & (uint16_t)~(1u << s));
    }
    return mask;
}
```

### 5.4 Parameter, sequence-setting and Morph setters (modify)

Apply the same pattern to each of the five functions:

1. Rename the existing function to a `static` single-Scene worker with the
   `Scene` suffix. **The body is unchanged.**
2. Add the public wrapper **after** it.

| Existing public (line) | Becomes static | `match_type` |
|---|---|---|
| `effects_setParameter` (474) | `effects_setParameterScene` | 1 |
| `effects_setSeqRunMode` (505) | `effects_setSeqRunModeScene` | 1 |
| `effects_setSeqLength` (517) | `effects_setSeqLengthScene` | 1 |
| `effects_setSeqStepScale` (529) | `effects_setSeqStepScaleScene` | 1 |
| `effects_setMorphAmount` (541) | `effects_setMorphAmountScene` | 0 |

**`effects_setParameter`.** Change the header comment above the (now static)
worker. Its last sentence, "Step 10 adds edit-mask fan-out here.", becomes
"Single-Scene worker; the public wrapper below fans out." Then add:

```c
uint8_t effects_setParameter(uint8_t scene_index, uint8_t index,
                             effect_image_t image, uint8_t value)
{
    uint16_t mask = effects_fanoutMask(scene_index, 1u);
    uint8_t changed = 0u;
    uint8_t s;

    /*
     * Edit-mask fan-out (Session 072 step 10; plan §7.4, A44). Every
     * same-type masked Scene receives the same row, image and value, clamped
     * by its own descriptor. SceneData setters mark AutoSave and the
     * card-clean bit per Scene. The active runtime follows on the next
     * effects_service() rescan.
     */
    for (s = 0u; s < SCENE_COUNT; s++) {
        if ((mask & (uint16_t)(1u << s)) != 0u)
            changed |= effects_setParameterScene(s, index, image, value);
    }
    return changed;
}
```

**Sequence settings.** Add one comment above the three wrappers:

```c
/*
 * Sequence-setting fan-out (Session 072 step 10). run/len/scl reach every
 * same-type masked Scene; each Scene's SceneData setter normalizes and marks
 * its own AutoSave cells.
 */
```

The three wrappers:

```c
uint8_t effects_setSeqRunMode(uint8_t scene_index, uint8_t mode)
{
    uint16_t mask = effects_fanoutMask(scene_index, 1u);
    uint8_t changed = 0u;
    uint8_t s;

    for (s = 0u; s < SCENE_COUNT; s++) {
        if ((mask & (uint16_t)(1u << s)) != 0u)
            changed |= effects_setSeqRunModeScene(s, mode);
    }
    return changed;
}
```

Write `effects_setSeqLength()` (argument `length`) and
`effects_setSeqStepScale()` (argument `scale`) the same way.

**Effect Morph.** Its wrapper has its own comment:

```c
uint8_t effects_setMorphAmount(uint8_t scene_index, uint8_t amount)
{
    uint16_t mask = effects_fanoutMask(scene_index, 0u);
    uint8_t changed = 0u;
    uint8_t s;

    /*
     * Effect Morph amount is a Scene setting with no type meaning, so it
     * reaches every masked Scene. This is the Effect-page counterpart of the
     * PERF `mrp` bulk-set, which already fans out through preset_morphScene()
     * (A3).
     */
    for (s = 0u; s < SCENE_COUNT; s++) {
        if ((mask & (uint16_t)(1u << s)) != 0u)
            changed |= effects_setMorphAmountScene(s, amount);
    }
    return changed;
}
```

The sequence-setting comment block above the old `effects_setSeqRunMode`
("Sequence-setting and Effect Morph setters …") stays on the static workers.

### 5.5 Lane locks (modify `effects_setSeqLaneLock()`, 701)

1. Rename the existing function to
   `static uint8_t effects_setSeqLaneLockScene(...)`, body unchanged.
2. Change its one-line comment to:

```c
/* Write-and-lock one lane across the held steps of one Scene. */
```

3. Add the public wrapper after it:

```c
uint8_t effects_setSeqLaneLock(uint8_t scene_index, uint16_t step_mask,
                               uint8_t lane, uint8_t value)
{
    uint16_t mask = effects_fanoutMask(scene_index, 1u);
    uint8_t changed = 0u;
    uint8_t s;

    /*
     * FX lock fan-out (Session 072 step 10; plan §7.4 "lane locks"). The same
     * held-step mask, lane and value land in every same-type masked Scene.
     * The lanes are registry-defined, so equal types mean equal lane meaning.
     * Each Scene clamps to its own descriptor. Steps beyond a masked Scene's
     * length are stored but not played, the same as the origin.
     */
    for (s = 0u; s < SCENE_COUNT; s++) {
        if ((mask & (uint16_t)(1u << s)) != 0u)
            changed |= effects_setSeqLaneLockScene(s, step_mask, lane, value);
    }
    return changed;
}
```

The worker's `effects_state.seq_serial++` on change stays. A serial bump from
an inactive Scene only costs one extra LED repaint.

### 5.6 Type change (modify `effects_changeType()`, 1093)

1. Rename the existing function to
   `static uint8_t effects_changeTypeScene(uint8_t scene_index,
   effect_type_id_t type)`, body unchanged. It already handles an inactive
   Scene (no runtime switch) and the active Scene (held Morph, overlays,
   runtime switch).
2. Update the comment above the worker (the "Type changes preserve common
   rows…" line stays inside) to begin:

```c
/*
 * One-Scene type-change transaction (plan §7.2 items 1-6, F3). The public
 * wrapper below fans it out and re-validates the masks (items 8-9).
 */
```

3. Add the public wrapper after it:

```c
uint8_t effects_changeType(uint8_t scene_index, effect_type_id_t type)
{
    uint16_t mask;
    uint8_t changed = 0u;
    uint8_t s;

    /*
     * Type-change fan-out (Session 072 step 10; plan §7.2 item 8, F5).
     *
     * Every Scene in the active edit mask receives the same type, so masked
     * Scenes stay type-matched. No match filter applies: this is the edit
     * that restores the match. Already-matching Scenes return 0 from the
     * worker.
     *
     * Masks are directional, though: another Scene's mask may still name one
     * of these Scenes. bank_revalidateVoiceEditMasks() therefore runs after
     * any change and drops those now-mismatched members (D4).
     * Unknown types are rejected before any Scene is touched.
     */
    if (!effects_registryEntry(type))
        return 0u;
    mask = effects_fanoutMask(scene_index, 0u);
    for (s = 0u; s < SCENE_COUNT; s++) {
        if ((mask & (uint16_t)(1u << s)) != 0u)
            changed |= effects_changeTypeScene(s, type);
    }
    if (changed)
        bank_revalidateVoiceEditMasks();
    return changed;
}
```

- `menuEffects_editModeChanged()` keeps calling
  `effects_changeType(scene_index, candidate)`. Its `if (!…) return 0u;`
  now means "no masked Scene changed", which also covers the active Scene,
  because the active Scene is always in its own mask.
- The `DEV_EFFECT_FORCE_TYPE` bench hook in `main.c` also goes through the
  wrapper. That is harmless, because boot masks are self-only unless restored.

---

## 6. `menu.c`

### 6.1 Selection gate: `menu_voiceHeldSceneButtonPressed()` (6471, modify)

Replace the function comment's opening sentence and insert the gate after the
`bank_scenePresent()` test:

```c
uint8_t menu_voiceHeldSceneButtonPressed(uint8_t scene_index)
{
    /*
     * Toggle one Scene in the VOICE edit fan-out mask.
     *
     * Input: physical SEQ button index while VOICE is held. Output: BankData's
     * scene_mask_voice_edit flips that Scene bit when the Scene is present.
     *
     * Turning a bit ON additionally requires the same edit layout as the
     * active Scene: equal Effect type and all six Instrument types
     * (scene_editLayoutMatches(); plan §7.4, A44; Session 072 step 10). A
     * mismatch is consumed silently, with no toggle and no flash, and the SEQ
     * LED stays unlit. Turning a bit OFF is always allowed. The active Scene
     * cannot be removed because BankData normalizes the mask after every
     * toggle. Returns nonzero when it consumes the button, so ButtonHandler
     * does not reinterpret the press as step editing.
     */
    if (scene_index >= SCENE_COUNT || scene_index >= 16u)
        return 0u;
    if (!bank_scenePresent(scene_index))
        return 1u;
    if (!bank_sceneInVoiceEditMask(scene_index) &&
        !scene_editLayoutMatches(scene_getActiveIndex(), scene_index))
        return 1u;
    bank_toggleSceneMaskVoiceEdit(scene_index);
    menu_refreshVoiceHeldSceneLeds();
    led_flashGroup(LED_FLASH_GROUP_SEQ, (uint16_t)(1u << scene_index));
    menu_repaintAll();
    return 1u;
}
```

### 6.2 `menu_startSoundApply()` (513, add first statement)

Before `if (audioCodec_renderCount == 0u) {`:

```c
    /*
     * Kit/Scene/Bank/All/Performance Load has committed retained types into
     * one or more Scenes. Drop any VOICE edit-mask member that no longer
     * matches its owner before fan-out can use it (plan §7.4 F5; Session 072
     * step 10). This covers the pre-audio boot-synchronous path too.
     */
    bank_revalidateVoiceEditMasks();
```

### 6.3 `menu_startInstrumentApply()` (691, add)

After `preset_startInstrumentApply(scene_index, slot, …);`:

```c
    /*
     * The staged Instrument has been committed synchronously to Preset's
     * destination Scene mask. A slot type change can break other owners'
     * masks (F5; Session 072 step 10).
     */
    bank_revalidateVoiceEditMasks();
```

### 6.4 Empty-Bank Load branch (~11616, add)

Inside `if (!preset_completedBankLoadedScene()) {`, after `preset_ackStatus();`:

```c
            /*
             * bankset.bcg masks were restored without a Scene commit. The
             * fallback ladder may or may not load one, so validate the masks
             * now (F5; Session 072 step 10).
             */
            bank_revalidateVoiceEditMasks();
```

`menu.c` already includes `BankData.h` (it calls `bank_sceneMaskVoiceEdit()`)
and `SceneData.h`, so no new include is needed.

---

## 7. `main.c`: end-of-boot re-validation (add)

Immediately before `preset_startDrumsetApply();` (line 1296), after the
existing comment block:

```c
    /*
     * Boot has restored every Scene's types and every VOICE edit mask
     * (AutoSave, bankset.bcg, or Scene/Kit fallback). Drop members whose
     * layout no longer matches before the first edit can fan out
     * (plan §7.4 F5; Session 072 step 10). Deterministic, so an unpersisted
     * repair simply recurs at the next boot (S072_ST10 D5).
     */
    bank_revalidateVoiceEditMasks();
```

`main.c` already includes `BankData.h` (line 77).

---

## 8. Documentation (same change set)

1. **`EFFECTS_BUS_FEATURE_PLAN.md`**
   - Status line: "Steps 1–10 are implemented in source, with the Step 9/10
     hardware matrices pending."
   - §7.4: add after "It goes in `menu_voiceHeldSceneButtonPressed()` via a
     BankData/SceneData predicate.":

     > "Implemented in Step 10 as `scene_editLayoutMatches()` and
     > `bank_revalidateVoiceEditMasks()`. Re-validation runs at Menu's load-
     > completion funnels, at the end of boot, and after `effects_changeType()`
     > (S072_ST10 D4)."

   - §17.1 row 10, Gate column: "Built/clean-link verified; hardware matrix
     pending".
2. **`knowledge_files/specification_reference/BANK_PRESET_ARCHITECTURE.md`**
   - In the VOICE edit-mask section, add the layout gate: turning a bit on
     requires the same Effect type and the same six Instrument types.
   - Add the re-validation points, and that dropped bits mark the Bank
     VOICE-mask AutoSave field.
   - Add that Effect edits (parameters, Morph endpoints, run/len/scl, lane
     locks, Effect Morph amount, and type) fan out through the active mask.
3. **`knowledge_files/specification_reference/SRAM_MANIFEST.md`**: record
   "Step 10: no SRAM change" and the new production link line.
4. **`MEMORY.md`**: add the "S072 Step 10" line:
   - the layout gate;
   - re-validation sites;
   - Effect fan-out inside the EffectsManager setters (`*Scene` workers +
     wrappers);
   - the D2 same-type guard.

---

## 9. Build and verification gates

### Build

1. `make clean && make all` succeeds with no new warnings. In particular,
   check for an unused-function warning if a `*Scene` worker is left
   unreferenced.
2. `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`:
   - Record the flash delta: expected +0.65 to +0.9 KB, leaving about 7.6 to
     7.8 KB of headroom.
   - `.bss` is unchanged.
   - DTCM and FXBUF are unchanged.

### Hardware

Setup: a Bank with Scenes 01–04.

- 01 and 02: identical Kit layout, Effect `flt`.
- 03: same Kit layout, Effect `off`.
- 04: one different Instrument type in slot 3, Effect `flt`.
- Scene 01 active.

3. **Gate (mismatch rejection).**
   - Hold VOICE and press SEQ 2: it lights (it matches).
   - SEQ 3: nothing happens (Effect type differs), no flash, LED unlit.
   - SEQ 4: nothing happens (Instrument type differs).
   - Pressing SEQ 2 again removes it (off is always allowed).
   - SEQ 1 (active) can never be removed.
4. **Effect fan-out.** With the mask 01+02, on the Effect page (Scene 01):
   - Edit `frq` and `res`; `run`/`len`/`scl`; `mrp`.
   - Edit a SHIFT Morph endpoint.
   - Lock `frq` on held steps 1 and 5.
   - Switch to Scene 02 from PERF: every value and lock is identical.
   - Scene 03 and 04 are unchanged.
5. **Type-change fan-out (F5).**
   - With the mask 01+02, change `typ` on 01 to `off`: Scene 02 also becomes
     `off`, with its sequence cleared and its common rows kept.
   - Switch back and restore `flt` on both.
6. **Directional re-validation (D4).**
   - Make Scene 02's own mask contain 01 (switch to 02, hold VOICE, press
     SEQ 1). Switch back to 01 with its mask = 01 only.
   - Change `typ` on 01.
   - Switch to 02 and hold VOICE: SEQ 1 is no longer lit. AutoSave marks the
     Bank field.
7. **Load re-validation.**
   - With the mask 01+02, load a Kit with a different slot-1 type into
     Scene 02 only.
   - Hold VOICE on 01: SEQ 2 has gone out.
   - Repeat with Instrument Load into Scene 02, and with a Scene Load into 02.
8. **Boot.**
   - Leave a matched mask (01+02) and reboot: the mask is kept.
   - Hand-edit `bankset.bcg` so 01's mask includes 04, then Bank Load/reboot:
     bit 04 is dropped.
9. **VOICE fan-out regression.**
   - With the mask 01+02: a VOICE parameter edit, a Kit-setting (track-7
     decay) edit, a Scene-setting (FX send / audio out / fader) edit, and a
     PERF `mrp`, per-voice Morph and `srt` edit all reach 02 as before.
   - Effect Morph follows PERF `mrp` on both (A3).
10. **Single-Scene default.** With the mask = self only, all Effect edits
    affect only the active Scene (Step 7–9 behavior).
11. **Step 9 matrix spot-check.** Check Pattern `fx` automation and LFO `fx`
    on the active Scene. Fan-out does not touch runtime overlays.
12. **P1/P2.**
    - `sel`: select a Morph-locked step, then an unlocked one; the Morph lock
      holds.
    - The `fx` detail label shows `FilterFrequncy`.

### Not testable yet

Scene copy/clear, which belongs to the later copy pass.

### Rollback

Revert the change set. Stored formats are unchanged. Masks that the gate or
re-validation trimmed stay trimmed, which is valid for any build.

---

## 10. Notes on behavior visible to the user

- The gate changes existing VOICE-mask behavior (plan §7.4: "deliberately
  changes general voice-mask behavior"). Scenes with different kits can no
  longer be grouped for editing.
- After any load that changes a type, previously grouped Scenes may silently
  leave the group. The VOICE-held LED view shows the result.

---

## 11. Implementation notes

### 11.1 Implementation checkpoint

- Added the read-only `scene_editLayoutMatches()` predicate and the
  `bank_revalidateVoiceEditMasks()` repair, with adjacent public/private
  comments in both source modules. The repair preserves owner bits and routes
  changed entries through the indexed Bank setter so AutoSave marking remains
  centralized.
- Added the VOICE-held selection gate and revalidation calls at the Menu load
  funnels, empty-Bank fallback, and end-of-boot restore boundary.
- Moved Effect parameter, sequence, Morph amount, lane-lock, and type edits
  into single-Scene `*Scene` workers plus public EffectsManager fan-out
  wrappers. Local row/lock/sequence writes use the same-type guard; Morph and
  type changes are type-agnostic as specified.
- Applied the deferred ST9 `sel` fix so an unlocked selected step retains the
  held Morph lock, and removed the `fx` detail-label separator so the full
  `FilterFrequncy` label fits. Existing ST3–ST9 changes were preserved.
- No new SRAM allocation was introduced; the user's approved SRAM-expansion
  policy is therefore not exercised by Step 10.

### 11.2 Build measurement

`make clean && make all` completed successfully. The final link is
`text=483,024`, `data=416`, `bss=426,336`, with a 483,440-byte flash payload
and 8,080 bytes of headroom in the 491,520-byte application region. ITCM is
3,768 bytes, DTCM statics are 4,448 bytes, and the FXBUF remains 126,624 bytes
with a 3,744-byte margin. The `.bss` total and SRAM1 ledger are unchanged from
ST9. No new compiler warning was emitted by the ST10 changes; remaining
warnings are the existing filesystem, AsyncFATFS, USB, and linker/toolchain
warnings.

### 11.3 Verification status

- `git diff --check` is clean after the source and documentation updates.
- Hardware gates for mismatch rejection, Effect fan-out, directional
  revalidation, load/boot repair, and the ST9 prerequisite checks remain
  pending on the device. AutoSave tracking state at the `main.c` hook is not
  observable from the host build and remains a hardware/runtime check.

---

## 12. Assessment (2026-09-28)

### 12.1 Result

**Accepted for hardware testing, with no findings.** Every scheduled change
is present and follows the schedule, and both Step 9 prerequisites are
applied.

### 12.2 Build

I ran a clean rebuild (`make clean && make all`) independently; it exited 0.

```text
text 483024   data 416   bss 426336   dec 909776
Flash 483,440 / 491,520 B   headroom 8,080 B   (Step 9 → Step 10: +392 B)
ITCM 3,768 / 16,384 B       DTCM statics 4,448 B
FXBUF 126,624 B at 0x20001160, margin 3,744 B (unchanged)
```

- **Warnings:** the same 20 pre-existing lines. No warning comes from
  BankData, SceneData, EffectsManager, `menu.c` or `main.c`, and no
  unused-`*Scene`-worker warning appears.
- **Flash:** +392 B, well under the +0.65–0.9 KB estimate. LTO folds the
  seven wrapper loops.
- **`.bss`:** unchanged, as scheduled (no SRAM).

### 12.3 Code against schedule

| § | Item | Status | Notes |
|---|---|---|---|
| 2 P1 | `effects_seqSelect()` latches the selected step's Morph lock | ✔ | An unlocked selection now keeps the held value (ST9 F1 closed). |
| 2 P2 | `fx` detail label separator removed | ✔ | `FilterFrequncy` fits (ST9 F2 closed). |
| 3 | `scene_editLayoutMatches()` | ✔ | Identical to the schedule; declared read-only, with no AutoSave effect. |
| 4 | `bank_revalidateVoiceEditMasks()`; toggle comment | ✔ | Owner bit kept; changed entries go through the indexed setter, which marks the Bank VOICE-mask field. |
| 5.1–5.2 | Header comments; `BankData.h` include | ✔ | |
| 5.3 | `effects_fanoutMask()` | ✔ | Origin always included; the active origin adds its mask; the same-type filter applies when `match_type` is set. |
| 5.4 | Five `*Scene` workers + wrappers | ✔ | Worker bodies unchanged. `match_type`: 1 for parameter/run/len/scl, 0 for Morph amount. |
| 5.5 | Lane-lock worker + wrapper | ✔ | |
| 5.6 | Type-change worker + wrapper + re-validation | ✔ | Unknown type rejected first; re-validation only when something changed. |
| 6.1 | Selection gate | ✔ | Rejects turning a bit on for a mismatch (no toggle, no flash); turning a bit off is always allowed. |
| 6.2–6.4 | Re-validation in `menu_startSoundApply()`, `menu_startInstrumentApply()`, empty-Bank branch | ✔ | I confirmed that `preset_startInstrumentApplyImage()` commits the destination Scenes synchronously before the hook runs. |
| 7 | `main.c` end-of-boot re-validation | ✔ | Placed immediately before `preset_startDrumsetApply()`. |
| 8 | Docs (plan status/§7.4/§17.1, BANK_PRESET_ARCHITECTURE, SRAM_MANIFEST, MEMORY) | ✔ | |

`menuEffects.c` is untouched for fan-out, as D1 intended.

### 12.4 Notes (no action for Step 10)

- **Non-present masked Scenes.** Fan-out, like the existing VOICE fan-out,
  does not test `bank_scenePresent()` for masked members. A present bit can
  only be added while the Scene is present (the gate path). A later partial
  Bank Load could leave a masked Scene non-present, and it would then receive
  edits as default data, exactly as VOICE edits already do. This is
  pre-existing behavior and is not introduced here.
- **AutoSave tracking at the `main.c` hook (D5)** remains a runtime check,
  as §11.3 notes.
- **Flash headroom is now 8,080 B.** Step 11 is documentation plus
  comment-only edits and should cost 0 B. `S073_FLASH_EXPANSION.md` (the
  user's plan) is the path beyond this.

### 12.5 Hardware status

The §9 matrix (gates 3–12) is pending. It exercises the ST9 fixes (gate 12)
together with the Step 9 matrix spot-check (gate 11).
