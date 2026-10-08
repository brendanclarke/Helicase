# S077 P4 — Scene Morph Fan-Out Correction

**Parent:** S077 retest, item 3.15 (scene morph copy does not fan out — incorrect).
**Status:** plan only, no code change.

---

## 1. Problem Statement

Two scene-level morph operations do not fan out through the VOICE edit mask.
They should: morph endpoints are scene-child data (Instrument images, FX
send, Effect parameters), and every other scene-child edit fans out.

The original code comments say "parallels `copy scene`" / "parallels `clear
scene`", but those two non-fan-out operations exchange or reset the edit mask
itself — a structural scene-level action, not a child edit. Morph reset and
morph copy change only the data that the mask locks together, so the correct
model is `copy instrument` / `clear send` / `clear fx` / `copy kit`, all of
which fan out.

Additionally, the copy menu labels for morph operations should follow the
naming convention `[source -> morph]` to make clear what is being copied
onto the morph endpoints.

---

## 2. Affected Operations

### 2.1 Scene morph copy — `ccCopy_runSceneMorph()` (copyOps.c:1049)

**Current:** operates on one destination Scene. Does NOT fan out.
**Correct:** fan out through `bank_sceneFanoutMask(job->scene)`.
**Model:** `ccCopy_runMorphTrack()` (copyOps.c:975), which already fans out
per-slot with the same accessors.

What it does per destination: for all 6 slots, copy the source Scene's
Normal morphable bytes onto the destination's Morph bytes
(`preset_copySlotNormalToMorph()`); set the destination FX-send Morph to
the source FX-send Normal; copy slot-6 decay Normal→Morph; when Effect
types match, copy source Normal cells onto destination Morph cells.

With fan-out: the source stays `src->scene` (the PERF copy source). Each
member in `bank_sceneFanoutMask(job->scene)` becomes a destination. Per
member: the type-match checks (instrument and Effect) are already per-call,
so they correctly skip mismatched members. The active-Scene rebuild
(`preset_rebuildMorph()`, `effects_activateScene()`) runs once at the end if
any member was the active Scene. Name-content-changed fires per member.

### 2.2 Scene morph reset — `ccClear_runResetSceneMorph()` (clearOps.c:539)

**Current:** operates on one Scene. Does NOT fan out.
**Correct:** fan out through `bank_sceneFanoutMask(job->scene)`.
**Model:** `ccClear_runResetMorphTrack()` (clearOps.c:482), which fans out
per-slot with the same accessors.

What it does per destination: for all 6 slots, equalise Morph endpoints to
Normal (`preset_resetSlotMorphToNormal()`); set FX-send Morph to Normal;
set slot-6 decay Morph to Normal; reset Effect morph
(`effects_resetMorphToNormalSingle()`). Active-Scene rebuild once at end.

### 2.3 Other morph operations — verified correct

| Operation | Fans out | Status |
|---|---|---|
| Track "reset morph" (clear) | yes | correct |
| Track "morph" (copy) | yes | correct |
| Scene "reset fx morph" (clear) | yes | correct |
| PERF/EFFECTS pot clear of `fxm` | yes (FX lane part) | correct |
| VOICE/PERF pot clear of morph targets | no (Pattern part only) | correct — Pattern never fans out |

No other morph operation needs a change.

---

## 3. Label Corrections

All morph copy menu labels should read `[source -> morph]` to describe what
is being copied onto the morph endpoints.

### 3.1 Track copy label (copyOps.c:45)

**Current:** `"morph"`
**Correct:** `"inst -> morph"`

The source is the instrument's Normal image; the destination is the Morph
image. 13 characters — within the 14-character display limit.

### 3.2 Scene copy label (copyOps.c:54)

**Current:** `"scene morph"`
**Correct:** `"scene -> morph"`

The source is the whole Scene's Normal endpoints (all slots + FX send +
Kit decay + Effect); the destination is the Scene's Morph endpoints.
14 characters — exactly at the display limit.

### 3.3 Source indicator suffixes

The one-character indicator suffixes ("m" for both track and scene morph
copies) remain unchanged. These are positional abbreviations, not the
menu label.

---

## 4. Implementation Plan

### 4.1 copyOps.c

**Label change 1 (line 45):**
```
"morph"  →  "inst -> morph"
```

**Label change 2 (line 54):**
```
"scene morph"  →  "scene -> morph"
```

**`ccCopy_runSceneMorph()` rewrite (line 1049):**

Replace the single-destination body with a fan-out loop. Structure:

```
1. Get source from copyClear_source().
2. mask = bank_sceneFanoutMask(job->scene).
3. If mask includes the active Scene and apply workers not idle: CC_RUN_WAIT.
4. For each member m in mask:
   a. For each slot 0..5:
      - preset_copySlotNormalToMorph(src->scene, slot, m, slot)
      - preset_setVoiceFxSendMorph(m, slot, source FX-send Normal)
      - ccSvc_nameContentChanged(instrument row for m/slot)
   b. Slot-6 decay: copy source Normal → member Morph.
   c. Effect: if source and member Effect types match, copy Normal→Morph
      cells for each morphable parameter; call scene_finishEffectWholeCommit.
   d. ccSvc_nameContentChanged(scene row, effect row).
5. Trace record with the full mask.
6. If active Scene was in the mask: preset_rebuildMorph(),
   effects_activateScene(), menu_repaintAll().
```

The Effect type check moves inside the per-member loop — each member may
have a different Effect type. A mismatch silently skips that member's
Effect (same rule as today, applied per member).

### 4.2 clearOps.c

**`ccClear_runResetSceneMorph()` rewrite (line 539):**

Replace the single-scene body with a fan-out loop. Structure:

```
1. mask = bank_sceneFanoutMask(job->scene).
2. If mask includes the active Scene and apply workers not idle: CC_RUN_WAIT.
3. For each member m in mask:
   a. For each slot 0..5:
      - preset_resetSlotMorphToNormal(m, slot)
      - preset_setVoiceFxSendMorph(m, slot, that Scene's FX-send Normal)
      - ccSvc_nameContentChanged(instrument row for m/slot)
   b. Slot-6 decay: set member's Morph decay = member's Normal decay.
   c. effects_resetMorphToNormalSingle(m).
   d. ccSvc_nameContentChanged(scene row, effect row).
4. Trace record with the full mask.
5. If active Scene was in the mask: preset_rebuildMorph(), menu_repaintAll().
```

Note: the reset reads each member's OWN Normal endpoints (not a source
Scene), since reset equalises a Scene's Morph to its own Normal.

### 4.3 COPYCLEAR_UTILITIES.md

Update the following sections:

**§6.5 Scene copy table (line ~313):**
- `copy morph` → `[scene -> morph]` label; change "Fans out" from "no" to
  "yes".

**§7.2 Clear effect table (line ~398):**
- Scene `reset morph`: change "no" to "yes" in the fan-out column.
- Remove the "does NOT fan out" parenthetical.

**§10 Fan-out table (line ~518):**
- Add `copy [scene -> morph]` and `clear reset morph (Scene)` to the
  "Fans out" column.
- Remove them from the "Does not fan out" column (they aren't currently
  listed there explicitly, but the `copy scene` parallel wording implies
  they belong with the non-fan-out set).

### 4.4 Comment updates in code

Each rewritten function's comment header needs:
- Remove "Does NOT fan out (parallels `copy scene`)" / "(parallels `clear
  scene`)".
- Add "fans out through the edit mask (parallels `copy kit`)" /
  "(parallels `clear fx`)".
- Update the Accessors line to include `bank_sceneFanoutMask()`.

### 4.5 S077_RETEST_CHECKLIST.md

**Item 3.10:** change expected behavior from "does NOT fan out" to "DOES
fan out through the edit mask." Result column: clear (needs re-test).

**Item 3.14:** change label from `[morph]` to `[scene -> morph]` in the
check description if it appears there. (Currently 3.14 says "Select
'morph' (Scene copy). Verify all 6 slots' Normal → Morph, applied to the
destination Scene." — this needs to say "applied to the destination Scene
AND every edit-mask member.")

**Item 3.15:** change expected behavior from "does NOT fan out" to "DOES
fan out through the edit mask." Result column: clear (needs re-test).

---

## 5. What Does Not Change

- **Track-level morph operations:** already fan out correctly.
- **Scene "reset fx morph":** already fans out correctly.
- **Morph amounts:** no morph operation touches the morph amount parameter.
- **Edit mask:** morph operations never exchange or reset the edit mask
  (they fan out through it but don't modify it).
- **Pattern data:** never touched by morph operations.
- **Bank-present bit:** morph operations never change a Scene's present
  state.
- **Source indicators:** the "m" suffix stays.
- **Clear labels:** "reset morph" and "reset fx morph" names are unchanged.
- **RAM:** no new allocations.
- **ISR/DSP path:** no changes.

---

## 6. Risk

Low. The fan-out model is already proven in the same files
(`ccCopy_runMorphTrack`, `ccClear_runResetMorphTrack`, `ccCopy_runKit`,
`ccClear_runFx`). The rewrite follows the exact same pattern: get the mask,
loop over members, per-member type checks, one active-Scene rebuild at the
end. Every accessor called today is called in the fan-out version; no new
accessor is needed.

The label changes are pure string swaps within the display width limit.

---

## 7. Verification

After implementation, re-test the corrected checklist items:

| # | Check | Observe |
|---|-------|---------|
| 3.10 | Scene-level "reset morph" DOES fan out: set up a 2-Scene edit mask, clear "reset morph" on a Scene, verify all mask members' Morph endpoints are equalised to their Normal. | I |
| 3.14 | Scene copy label reads "[scene -> morph]". Destination Scene AND every edit-mask member receive the source Normal as their Morph. | I |
| 3.15 | Scene-level morph copy DOES fan out through the edit mask. | I |
| new | Track copy label reads "[inst -> morph]". | I |
| new | Fan-out with mixed Effect types: source has Effect A, member has Effect B. Verify the member's instrument Morph is still written (only the Effect part is skipped). | I |
