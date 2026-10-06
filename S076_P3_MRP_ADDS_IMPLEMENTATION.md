# S076 P3 — Morph Copy/Clear Implementation Schedule

Full code implementation schedule for the five morph-targeted copy/clear
operations designed in `S076_P3_COPYCLEAR_MORPH_ADDS.md`. Every change is
cited by file, line, and add/remove/modify. Comment blocks alongside each
change serve as documentation-in-place.

- **Session:** 076 (2026-10-06), branch `dev-ph6-cleanup`.
- **Design document:** `S076_P3_COPYCLEAR_MORPH_ADDS.md` (all §8.2
  questions confirmed).

---

## Change 1 — Enum additions in `clearOps.h`

**File:** `Core/Menu/CopyClear/clearOps.h`

### 1A. Track-level clear enum — ADD after line 30

**Current line 30:** `CC_CLEAR_SEND`

**Add `CC_CLEAR_RESET_MORPH` after `CC_CLEAR_SEND`:**

```c
    CC_CLEAR_SEND,
    CC_CLEAR_RESET_MORPH
```

**Modify:** Change line 30 from `CC_CLEAR_SEND` to `CC_CLEAR_SEND,` (add
trailing comma).

```
/*
 * What:       CC_CLEAR_RESET_MORPH — selection enum for the "reset morph"
 *             track-level clear. Equalises a single voice slot's morph
 *             endpoints to its current Normal endpoints, plus the voice's
 *             correlated Scene morph endpoints (FX send morph, Kit slot-6
 *             decay morph). Fans out through the edit mask.
 * Why:        extends the cc_clear_obj_sel_t domain so the menu, the
 *             request path, and the dispatch table can address the new
 *             selection by value. Appended after CC_CLEAR_SEND so existing
 *             enum values are unchanged.
 * Inputs:     used by copyClearSession.c (menu selection), ccClear_requestClear(),
 *             and ccClear_runJob() dispatch.
 * Outputs:    none (enum constant).
 * Accessors:  ccClear_selectionCount(), ccClear_label(), ccClear_runJob().
 * Affiliates: CC_CLEAR_SCENE_RESET_MORPH below, CC_COPY_MORPH in copyOps.h.
 */
```

### 1B. Scene-level clear enum — ADD after line 41

**Current line 41:** `CC_CLEAR_SCENE_FX_SEQUENCE`

**Add two values after `CC_CLEAR_SCENE_FX_SEQUENCE`:**

```c
    CC_CLEAR_SCENE_FX_SEQUENCE,
    CC_CLEAR_SCENE_RESET_MORPH,
    CC_CLEAR_SCENE_RESET_FX_MORPH
```

**Modify:** Change line 41 from `CC_CLEAR_SCENE_FX_SEQUENCE` to
`CC_CLEAR_SCENE_FX_SEQUENCE,` (add trailing comma).

```
/*
 * What:       CC_CLEAR_SCENE_RESET_MORPH — "reset morph" for a whole Scene.
 *             Equalises all 6 instrument slots' morph endpoints, all
 *             correlated Scene morph endpoints (FX send morph ×6, Kit slot-6
 *             decay morph), and all morphable Effect morph endpoints to their
 *             current Normal values. Does NOT fan out (parallels `clear scene`).
 *             Does NOT clear the Bank-present bit or touch morph amounts.
 * Why:        whole-Scene morph reset for the PERF clear menu.
 * Inputs:     ccClear_requestClear(), ccClear_runJob() dispatch.
 * Outputs:    none (enum constant).
 * Accessors:  ccClear_selectionCount(), ccClear_label(), ccClear_runJob().
 * Affiliates: CC_CLEAR_RESET_MORPH above (track-level), CC_CLEAR_SCENE_RESET_FX_MORPH below.
 *
 * CC_CLEAR_SCENE_RESET_FX_MORPH — "reset fx morph". Equalises only the
 *             Effect's morphable parameter morph endpoints to their Normal
 *             values. Fans out through the edit mask (parallels `clear fx`).
 * Inputs:     ccClear_requestClear(), ccClear_runJob() dispatch.
 * Affiliates: effects_paramMorphable(), scene_effectRecordForWholeCommit().
 */
```

---

## Change 2 — Enum additions in `copyOps.h`

**File:** `Core/Menu/CopyClear/copyOps.h`

### 2A. Track-level copy enum — ADD after line 30

**Current line 30:** `typedef enum { CC_COPY_TRACK = 0u, CC_COPY_INSTRUMENT } cc_copy_track_sel_t;`

**Modify to:**

```c
typedef enum { CC_COPY_TRACK = 0u, CC_COPY_INSTRUMENT, CC_COPY_MORPH } cc_copy_track_sel_t;
```

```
/*
 * What:       CC_COPY_MORPH — selection enum for the "morph" track-level
 *             copy. Copies the source track's Normal endpoints onto the
 *             destination track's Morph endpoints (instrument images +
 *             correlated Scene params: FX send, Kit slot-6 decay). Silently
 *             skipped if the source and destination slot instrument types
 *             differ. Fans out through the edit mask.
 * Why:        extends cc_copy_track_sel_t so the menu, request path, and
 *             dispatch table can address the new selection. Appended after
 *             CC_COPY_INSTRUMENT so existing values are unchanged.
 * Inputs:     copyClearSession.c (menu), ccCopy_requestPaste(), ccCopy_runJob().
 * Outputs:    none (enum constant).
 * Accessors:  ccCopy_selectionCount(), ccCopy_label(), ccCopy_runJob().
 * Affiliates: CC_COPY_SCENE_MORPH below, CC_CLEAR_RESET_MORPH in clearOps.h.
 */
```

### 2B. Scene-level copy enum — ADD after line 36

**Current line 36:** `CC_COPY_PATTERN`

**Add `CC_COPY_SCENE_MORPH` after `CC_COPY_PATTERN`:**

```c
    CC_COPY_PATTERN,
    CC_COPY_SCENE_MORPH
```

**Modify:** Change line 36 from `CC_COPY_PATTERN` to `CC_COPY_PATTERN,`
(add trailing comma).

```
/*
 * What:       CC_COPY_SCENE_MORPH — "scene morph" Scene-level copy. Copies
 *             all Normal endpoints of the source Scene onto the destination
 *             Scene's Morph endpoints for all matching-type components
 *             (instruments, FX send ×6, Kit slot-6 decay, Effect).
 *             Silently skips instruments and/or the Effect whose types differ.
 *             Does NOT fan out (parallels `copy scene`). Does NOT exchange
 *             or reset the edit mask. Does NOT touch morph amounts.
 * Why:        whole-Scene morph copy for the PERF copy menu.
 * Inputs:     ccCopy_requestPaste(), ccCopy_runJob() dispatch.
 * Outputs:    none (enum constant).
 * Accessors:  ccCopy_selectionCount(), ccCopy_label(), ccCopy_runJob().
 * Affiliates: CC_COPY_MORPH above (track-level), CC_CLEAR_SCENE_RESET_MORPH
 *             in clearOps.h.
 */
```

---

## Change 3 — Label arrays and menu counts in `clearOps.c`

**File:** `Core/Menu/CopyClear/clearOps.c`

### 3A. Track label array — MODIFY lines 36–38

**Current (lines 36–38):**
```c
static const char *const ccClear_trackLabels[] = {
    "cancel", "track", "track auto", "track notes", "send"
};
```

**Problem:** `ccClear_trackLabels[]` is shared between `CC_MENU_CLEAR_TRACK`
and `CC_MENU_CLEAR_TRACK_FX` via `ccClear_label()` (line 76). The "send"
entry only appears in `CLEAR_TRACK_FX`. The new "reset morph" appears in
**both** menus. We need to split into two arrays, or add "reset morph" after
"send" and adjust `CLEAR_TRACK` to end before "send".

**Replace with:**

```c
/*
 * What:       split track clear labels. ccClear_trackLabels[] is the VOICE-
 *             mode track clear menu (cancel, track, track auto, track notes,
 *             reset morph). ccClear_trackFxLabels[] is the EFFECTS-mode menu
 *             (cancel, track, track auto, track notes, send, reset morph).
 *             The split is required because "send" appears only in the FX
 *             menu while "reset morph" appears in both.
 * Why:        the prior single-array trick (shared via ccClear_label, the FX
 *             menu seeing the same array with count+1) breaks when a selection
 *             appears in both menus but "send" does not. A split lets each
 *             menu have an independent label array.
 * Inputs:     ccClear_label() maps CC_MENU_CLEAR_TRACK to trackLabels and
 *             CC_MENU_CLEAR_TRACK_FX to trackFxLabels.
 * Outputs:    const label strings for the copyClearSession menu renderer.
 * Affiliates: ccClear_selectionCount(), ccClear_runJob() dispatch.
 */
static const char *const ccClear_trackLabels[] = {
    "cancel", "track", "track auto", "track notes", "reset morph"
};
static const char *const ccClear_trackFxLabels[] = {
    "cancel", "track", "track auto", "track notes", "send", "reset morph"
};
```

### 3B. Scene label array — MODIFY lines 39–42

**Current (lines 39–42):**
```c
static const char *const ccClear_sceneLabels[] = {
    "cancel", "scene", "settings", "pattern", "automation", "notes", "fx",
    "fx sequence"
};
```

**Replace with:**

```c
static const char *const ccClear_sceneLabels[] = {
    "cancel", "scene", "settings", "pattern", "automation", "notes", "fx",
    "fx sequence", "reset morph", "reset fx morph"
};
```

### 3C. Selection counts — MODIFY lines 56–66

**Current:**
```c
uint8_t ccClear_selectionCount(cc_menu_t menu)
{
    switch (menu) {
    case CC_MENU_CLEAR_STEP:     return 4u;
    case CC_MENU_CLEAR_BAR:      return 4u;
    case CC_MENU_CLEAR_TRACK:    return 4u;
    case CC_MENU_CLEAR_TRACK_FX: return 5u;
    case CC_MENU_CLEAR_SCENE:    return 8u;
    default:                     return 0u;
    }
}
```

**Modify three return values:**

```c
/*
 * What:       updated counts: CLEAR_TRACK 4→5 (added reset morph),
 *             CLEAR_TRACK_FX 5→6 (added reset morph after send),
 *             CLEAR_SCENE 8→10 (added reset morph, reset fx morph).
 * Why:        the encoder clamps to selectionCount−1; raising each count
 *             exposes the new selections at the end of the menu.
 * Inputs:     cc_menu_t from ccClear_menuForObject().
 * Outputs:    total entry count including "cancel" at index 0.
 * Affiliates: ccClear_label(), copyClearSession.c encoder clamping.
 */
uint8_t ccClear_selectionCount(cc_menu_t menu)
{
    switch (menu) {
    case CC_MENU_CLEAR_STEP:     return 4u;
    case CC_MENU_CLEAR_BAR:      return 4u;
    case CC_MENU_CLEAR_TRACK:    return 5u;  /* +reset morph */
    case CC_MENU_CLEAR_TRACK_FX: return 6u;  /* +reset morph */
    case CC_MENU_CLEAR_SCENE:    return 10u; /* +reset morph, +reset fx morph */
    default:                     return 0u;
    }
}
```

### 3D. Label function — MODIFY lines 68–80

**Current `ccClear_label()` maps both `CC_MENU_CLEAR_TRACK` and
`CC_MENU_CLEAR_TRACK_FX` to `ccClear_trackLabels`.**

**After the split (Change 3A), modify the dispatch:**

```c
/*
 * What:       split the TRACK/TRACK_FX dispatch to use separate label arrays
 *             now that "reset morph" appears in both but "send" only in FX.
 * Why:        the old fallthrough of CLEAR_TRACK into CLEAR_TRACK_FX used a
 *             single array whose count difference exposed "send" only in FX.
 *             With the enum split (CC_CLEAR_RESET_MORPH = 5 in the base
 *             menu, after "send" = 4 in the FX menu), separate arrays are
 *             required.
 * Inputs:     cc_menu_t and uint8_t selection from the encoder.
 * Outputs:    const label string pointer.
 * Affiliates: ccClear_selectionCount(), copyClearSession.c display.
 */
const char *ccClear_label(cc_menu_t menu, uint8_t selection)
{
    if (selection >= ccClear_selectionCount(menu))
        return "";
    switch (menu) {
    case CC_MENU_CLEAR_STEP:     return ccClear_stepLabels[selection];
    case CC_MENU_CLEAR_BAR:      return ccClear_barLabels[selection];
    case CC_MENU_CLEAR_TRACK:    return ccClear_trackLabels[selection];
    case CC_MENU_CLEAR_TRACK_FX: return ccClear_trackFxLabels[selection];
    case CC_MENU_CLEAR_SCENE:    return ccClear_sceneLabels[selection];
    default:                     return "";
    }
}
```

**Note:** The current code at line 75–76 uses a fallthrough:
```c
    case CC_MENU_CLEAR_TRACK:
    case CC_MENU_CLEAR_TRACK_FX: return ccClear_trackLabels[selection];
```
This must be split into two separate cases.

---

## Change 4 — Label arrays and menu counts in `copyOps.c`

**File:** `Core/Menu/CopyClear/copyOps.c`

### 4A. Track copy label array — MODIFY line 36

**Current (line 36):**
```c
static const char *const ccCopy_trackLabels[] = { "track", "instrument" };
```

**Replace with:**

```c
/*
 * What:       add "morph" label for CC_COPY_MORPH selection.
 * Why:        the label array is indexed by cc_copy_track_sel_t value; the
 *             new CC_COPY_MORPH = 2 entry must be present at index 2.
 * Inputs:     ccCopy_label() maps CC_MENU_COPY_TRACK to this array.
 * Outputs:    const label string for the copy/clear session menu renderer.
 * Affiliates: ccCopy_selectionCount(), ccCopy_runJob().
 */
static const char *const ccCopy_trackLabels[] = { "track", "instrument", "morph" };
```

### 4B. Scene copy label array — MODIFY lines 37–39

**Current (lines 37–39):**
```c
static const char *const ccCopy_sceneLabels[] = {
    "scene", "settings", "kit", "effect", "pattern"
};
```

**Replace with:**

```c
static const char *const ccCopy_sceneLabels[] = {
    "scene", "settings", "kit", "effect", "pattern", "scene morph"
};
```

### 4C. Selection counts — MODIFY lines 56–66

**Current:**
```c
    case CC_MENU_COPY_TRACK: return 2u;
    case CC_MENU_COPY_SCENE: return 5u;
```

**Modify:**

```c
/*
 * What:       updated counts: COPY_TRACK 2→3 (+morph), COPY_SCENE 5→6
 *             (+scene morph).
 * Why:        exposes the new selections at the end of each menu.
 */
    case CC_MENU_COPY_TRACK: return 3u;  /* +morph */
    case CC_MENU_COPY_SCENE: return 6u;  /* +scene morph */
```

### 4D. Identical-paste check — MODIFY lines 108–113

**Current (lines 108–113):**
```c
    case CC_KIND_TRACK:
        identical = (uint8_t)(dst_scene == src->scene &&
            ((selection == CC_COPY_TRACK && dst_track == src->track) ||
             (selection == CC_COPY_INSTRUMENT &&
              ccCopy_slotOf(dst_track) == ccCopy_slotOf(src->track))));
        break;
```

**Replace with:**

```c
/*
 * What:       add CC_COPY_MORPH to the identical-paste check. A morph
 *             copy onto the same slot in the same Scene is an identity
 *             operation (Normal→Morph of the same slot, but this is NOT
 *             a no-op since Normal != Morph in general — HOWEVER, copying
 *             from the same Scene+slot is semantically a reset, which is
 *             valid). Actually: CC_COPY_MORPH from scene S slot X to
 *             scene S slot X copies the slot's own Normal onto its own
 *             Morph — this is identical to the reset morph operation and
 *             should be allowed (not treated as identical). Therefore
 *             CC_COPY_MORPH never triggers the identical-paste skip.
 * Why:        morph copy's source is the Normal image and its destination
 *             is the Morph image. Even when src and dst are the same
 *             slot, the images differ in general, so the paste is never
 *             a true no-op.
 * Inputs:     src scene/track, dst scene/track, selection.
 * Outputs:    identical flag for the PASTE_NOOP trace and early return.
 */
    case CC_KIND_TRACK:
        identical = (uint8_t)(dst_scene == src->scene &&
            ((selection == CC_COPY_TRACK && dst_track == src->track) ||
             (selection == CC_COPY_INSTRUMENT &&
              ccCopy_slotOf(dst_track) == ccCopy_slotOf(src->track))));
        /* CC_COPY_MORPH: never identical — src Normal → dst Morph. */
        break;
```

**Note:** No code change to the conditional itself — `CC_COPY_MORPH` is
simply not listed, so `identical` stays 0 for morph copies. Add only the
comment.

---

## Change 5 — Shared morph helpers in `presetManager.h` / `presetManager.c`

### 5A. Declarations in `presetManager.h` — ADD after line 571

**Current line 571:** `void    preset_startKitMorphApply(void);`

**Add before that line (or after line 570, after the comment block ending
at the `*/` on line 570):**

```c
/*
 * Reset one instrument slot's Morph endpoint to its current Normal endpoint.
 *
 * What:       for each descriptor index whose flags include
 *             INSTRUMENT_PARAM_FLAG_MORPHABLE, copies the Normal image byte
 *             onto the Morph image byte through SceneData's retained Kit
 *             store. Non-morphable descriptors are untouched.
 * Why:        the "reset morph" clear operations (track-level and Scene-level)
 *             and the "morph copy" operation need a shared per-slot morphable-
 *             copy loop. Three or more call sites justify a shared helper.
 * Inputs:     scene_index (0..15), slot (0..5). The slot's current instrument
 *             type determines which descriptor table is used.
 * Outputs:    the count of Morph bytes set. If the Scene or slot is invalid,
 *             returns 0 with no side effects. Marks AutoSave Instrument Morph
 *             dirty for the slot when at least one byte changed
 *             (autosave_markInstrumentMorphDirty()). Does NOT queue the morph
 *             worker — the caller does that after all slots are written.
 * Accessors:  instrumentManager_registryEntry(type)->descriptors,
 *             scene_getConst(scene_index)->kit.instruments[slot].
 * Affiliates: preset_copySlotNormalToMorph() below, ccClear_runResetMorphTrack(),
 *             ccClear_runResetSceneMorph(), presetMorph_requestVoice().
 */
uint8_t preset_resetSlotMorphToNormal(uint8_t scene_index, uint8_t slot);

/*
 * Copy one Scene/slot's Normal endpoints onto another Scene/slot's Morph endpoints.
 *
 * What:       for each morphable descriptor, copies the source Scene/slot's
 *             Normal image byte onto the destination Scene/slot's Morph image
 *             byte. Requires the source and destination slot instrument types
 *             to match; returns 0 with no side effects if they differ.
 * Why:        the "morph" track-level copy and the "scene morph" Scene-level
 *             copy need a shared cross-Scene per-slot morphable-copy loop.
 * Inputs:     src_scene/src_slot (source Normal), dst_scene/dst_slot
 *             (destination Morph). Both slots must be valid (0..5) and the
 *             Scene indices valid (0..15).
 * Outputs:    the count of Morph bytes set. 0 if types differ or coords
 *             are invalid. Marks autosave_markInstrumentMorphDirty(dst_scene,
 *             dst_slot) when at least one byte changed. Does NOT queue the
 *             morph worker.
 * Accessors:  instrumentManager_registryEntry(), scene_getConst().
 * Affiliates: preset_resetSlotMorphToNormal() above, ccCopy_runMorphTrack(),
 *             ccCopy_runSceneMorph().
 */
uint8_t preset_copySlotNormalToMorph(uint8_t src_scene, uint8_t src_slot,
                                     uint8_t dst_scene, uint8_t dst_slot);
```

### 5B. Implementations in `presetManager.c` — ADD after `preset_rebuildMorph()` (after line 3410)

**Current line 3410:** `}`  (end of `preset_rebuildMorph`)

**Add:**

```c
/*
 * preset_resetSlotMorphToNormal — equalise one slot's Morph to its Normal.
 * Contract in presetManager.h.
 */
uint8_t preset_resetSlotMorphToNormal(uint8_t scene_index, uint8_t slot)
{
    /*
     * What:       iterates the descriptor table for the slot's instrument type.
     *             For each descriptor whose flags include MORPHABLE, copies
     *             instrument_parameters[i] → morph_instrument_parameters[i]
     *             in the retained Scene image.
     * Why:        keeps endpoint byte writes inside the Preset owner path
     *             and guarantees the same descriptor iteration as the morph
     *             engine.
     * Inputs:     scene_index, slot. Reads the slot's type from the retained
     *             Kit, the descriptor table from instrumentManager_registryEntry().
     * Outputs:    count of bytes written. Side effects:
     *             autosave_markInstrumentMorphDirty() for the slot on change.
     * Affiliates: INSTRUMENT_PARAM_FLAG_MORPHABLE, instrumentManager_registryEntry(),
     *             scene_getConst(), autosave_markInstrumentMorphDirty().
     */
    const scene_t *sc = scene_getConst(scene_index);
    const instrument_registry_entry_t *reg;
    uint8_t count = 0u;
    uint8_t i;

    if (!sc || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    reg = instrumentManager_registryEntry(sc->kit.instruments[slot].type);
    if (!reg || !reg->descriptors)
        return 0u;
    for (i = 0u; i < reg->descriptor_count; i++) {
        if (!(reg->descriptors[i].flags & INSTRUMENT_PARAM_FLAG_MORPHABLE))
            continue;
        if (sc->kit.instruments[slot].parameter_images
                .morph_instrument_parameters[i] !=
            sc->kit.instruments[slot].parameter_images
                .instrument_parameters[i]) {
            /* Direct retained write: scene_t is the sole retained store. */
            ((scene_t *)sc)->kit.instruments[slot].parameter_images
                .morph_instrument_parameters[i] =
                sc->kit.instruments[slot].parameter_images
                    .instrument_parameters[i];
            count++;
        }
    }
    if (count > 0u)
        autosave_markInstrumentMorphDirty(scene_index, slot);
    return count;
}

/*
 * preset_copySlotNormalToMorph — copy src Normal onto dst Morph.
 * Contract in presetManager.h.
 */
uint8_t preset_copySlotNormalToMorph(uint8_t src_scene, uint8_t src_slot,
                                     uint8_t dst_scene, uint8_t dst_slot)
{
    /*
     * What:       reads the source slot's Normal image and writes matching
     *             morphable bytes into the destination slot's Morph image.
     *             Returns 0 immediately if the instrument types differ.
     * Why:        morph copy must respect the morphability flag and match
     *             descriptors by index (same type guarantees same layout).
     * Inputs:     src_scene/src_slot (Normal source), dst_scene/dst_slot
     *             (Morph target).
     * Outputs:    count of bytes written. Side effects:
     *             autosave_markInstrumentMorphDirty(dst_scene, dst_slot).
     * Affiliates: same as preset_resetSlotMorphToNormal() above.
     */
    const scene_t *src_sc = scene_getConst(src_scene);
    const scene_t *dst_sc = scene_getConst(dst_scene);
    const instrument_registry_entry_t *reg;
    uint8_t count = 0u;
    uint8_t i;

    if (!src_sc || !dst_sc || src_slot >= INSTRUMENT_SLOT_COUNT ||
        dst_slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (src_sc->kit.instruments[src_slot].type !=
        dst_sc->kit.instruments[dst_slot].type)
        return 0u;
    reg = instrumentManager_registryEntry(src_sc->kit.instruments[src_slot].type);
    if (!reg || !reg->descriptors)
        return 0u;
    for (i = 0u; i < reg->descriptor_count; i++) {
        if (!(reg->descriptors[i].flags & INSTRUMENT_PARAM_FLAG_MORPHABLE))
            continue;
        if (((scene_t *)dst_sc)->kit.instruments[dst_slot].parameter_images
                .morph_instrument_parameters[i] !=
            src_sc->kit.instruments[src_slot].parameter_images
                .instrument_parameters[i]) {
            ((scene_t *)dst_sc)->kit.instruments[dst_slot].parameter_images
                .morph_instrument_parameters[i] =
                src_sc->kit.instruments[src_slot].parameter_images
                    .instrument_parameters[i];
            count++;
        }
    }
    if (count > 0u)
        autosave_markInstrumentMorphDirty(dst_scene, dst_slot);
    return count;
}
```

**DESIGN NOTE on retained writes:** The helpers above cast away `const` to
write retained scene_t bytes directly. This follows the same pattern used by
`preset_startInstrumentCopy()` (line 549 of presetManager.c) which writes
through the retained Scene image and marks the AutoSave cells afterward.
Alternatively, these writes could go through
`preset_setInstrumentParameter()` per-byte, but that would:
1. Fire `presetMorph_applyParameterNow()` per-byte (wasteful — the caller
   will batch-rebuild afterward);
2. Check step-automation hold per-byte (unnecessary — endpoint writes are
   always retained);
3. Run the fan-out path per-byte (the caller owns fan-out).

So the bulk retained write + single dirty mark is the correct pattern.

---

## Change 6 — Effect morph reset helper in `EffectsManager.h` / `.c`

### 6A. Declaration in `EffectsManager.h` — ADD after line 359

**Current line 359:** `uint16_t effects_resetRecord(uint8_t dst_scene);`

**Add:**

```c
/*
 * Reset an Effect record's morphable Morph endpoints to their Normal values.
 *
 * What:       for each Effect parameter index where effects_paramMorphable()
 *             returns nonzero for the record's current type, copies
 *             effect_record.normal[i] → effect_record.morph[i]. Non-morphable
 *             parameters are untouched. Sequence data, type, and settings are
 *             untouched. Fans out through the destination Scene's edit mask
 *             (paralleling effects_resetRecord).
 * Why:        "clear scene reset morph" needs the Effect morph reset as part
 *             of its whole-Scene operation (without fan-out), and "clear scene
 *             reset fx morph" needs it as a standalone fanned-out operation.
 *             Keeping it in EffectsManager centralises the morphability check
 *             and the SceneData commit path.
 * Inputs:     dst_scene (0..15). The destination's current Effect type
 *             determines which parameters are morphable.
 * Outputs:    the Scene mask of Scenes whose Effect records were written
 *             (same as effects_resetRecord). 0 if nothing changed or the
 *             Scene is invalid.
 * Accessors:  effects_paramMorphable(), scene_effectRecordForWholeCommit(),
 *             scene_finishEffectWholeCommit(), effects_activateScene().
 * Affiliates: effects_resetRecord() (model), ccClear_runResetFxMorph(),
 *             ccClear_runResetSceneMorph().
 */
uint16_t effects_resetMorphToNormal(uint8_t dst_scene);

/*
 * Reset Effect morph endpoints for one specific Scene (no fan-out).
 *
 * What:       same morphable-parameter copy as effects_resetMorphToNormal()
 *             but operates on exactly one Scene with no edit-mask fan-out.
 *             Used by the whole-Scene morph reset executor which handles its
 *             own non-fanned-out loop.
 * Inputs:     scene_index (0..15).
 * Outputs:    nonzero if any byte changed; 0 otherwise. Commits through
 *             scene_effectRecordForWholeCommit()/scene_finishEffectWholeCommit()
 *             and activates if active.
 * Affiliates: effects_resetMorphToNormal() above, ccClear_runResetSceneMorph(),
 *             ccCopy_runSceneMorph().
 */
uint8_t effects_resetMorphToNormalSingle(uint8_t scene_index);
```

### 6B. Implementations in `EffectsManager.c` — ADD after `effects_resetRecord()` (after line 991)

**Current line 991:** `}` (end of `effects_resetRecord`)

**Add:**

```c
/*
 * Reset Effect morph endpoints to Normal values with fan-out.
 * Contract in EffectsManager.h.
 */
uint16_t effects_resetMorphToNormal(uint8_t dst_scene)
{
    /*
     * What:       iterates each edit-mask member's Effect record. For each
     *             morphable parameter, copies normal[i] → morph[i]. Uses the
     *             whole-record commit pair so one AutoSave marker covers all
     *             Effect cells.
     * Why:        "reset fx morph" fans out exactly like "clear fx" — same-
     *             type edit-mask members receive the same change.
     * Inputs:     dst_scene. The destination's type determines the fan-out
     *             mask (effects_fanoutMask with type gate).
     * Outputs:    Scene mask of Scenes written. Side effects: AutoSave marks
     *             from scene_finishEffectWholeCommit(), runtime activation on
     *             active Scene.
     * Affiliates: effects_paramMorphable(), effects_fanoutMask().
     */
    const effect_record_t *dst = scene_effectConst(dst_scene);
    effect_type_id_t type;
    uint16_t mask;
    uint16_t written = 0u;
    uint8_t member;

    if (!dst || dst_scene >= SCENE_COUNT)
        return 0u;
    type = dst->type;
    mask = effects_fanoutMask(dst_scene, 1u);
    for (member = 0u; member < SCENE_COUNT; member++) {
        effect_record_t *record;
        uint8_t changed = 0u;
        uint8_t i;

        if ((mask & (uint16_t)(1u << member)) == 0u)
            continue;
        record = scene_effectRecordForWholeCommit(member);
        if (!record)
            continue;
        for (i = 0u; i < EFFECT_PARAM_COUNT; i++) {
            if (!effects_paramMorphable(type, i))
                continue;
            if (record->morph[i] != record->normal[i]) {
                record->morph[i] = record->normal[i];
                changed = 1u;
            }
        }
        if (changed) {
            scene_finishEffectWholeCommit(member);
            written |= (uint16_t)(1u << member);
        }
        /* If nothing changed, the commit pair is harmless — finishEffect
         * marks dirty only when there are actual cell changes queued. But
         * to be safe, still call finish to close the pair. */
        else {
            scene_finishEffectWholeCommit(member);
        }
    }
    if ((written & (uint16_t)(1u << scene_getActiveIndex())) != 0u)
        effects_activateScene(scene_getActiveIndex());
    return written;
}

/*
 * Reset Effect morph endpoints for one Scene (no fan-out).
 * Contract in EffectsManager.h.
 */
uint8_t effects_resetMorphToNormalSingle(uint8_t scene_index)
{
    const effect_record_t *dst = scene_effectConst(scene_index);
    effect_record_t *record;
    effect_type_id_t type;
    uint8_t changed = 0u;
    uint8_t i;

    if (!dst || scene_index >= SCENE_COUNT)
        return 0u;
    type = dst->type;
    record = scene_effectRecordForWholeCommit(scene_index);
    if (!record)
        return 0u;
    for (i = 0u; i < EFFECT_PARAM_COUNT; i++) {
        if (!effects_paramMorphable(type, i))
            continue;
        if (record->morph[i] != record->normal[i]) {
            record->morph[i] = record->normal[i];
            changed = 1u;
        }
    }
    scene_finishEffectWholeCommit(scene_index);
    if (changed && scene_index == scene_getActiveIndex())
        effects_activateScene(scene_getActiveIndex());
    return changed;
}
```

---

## Change 7 — Clear executors in `clearOps.c`

**File:** `Core/Menu/CopyClear/clearOps.c`

### 7A. `ccClear_runResetMorphTrack()` — ADD before `ccClear_runJob()` (before line 418)

**Insert after `ccClear_runFxSequence()` (after line 416):**

```c
/*
 * `clear track reset morph`: equalise one voice slot's Morph endpoints to
 * its current Normal endpoints, plus correlated Scene morph params (FX send
 * morph, Kit slot-6 decay morph), fanned out through the edit mask.
 *
 * What:       for each Scene in the fan-out mask:
 *             (a) preset_resetSlotMorphToNormal(scene, slot) — morphable
 *                 instrument descriptors;
 *             (b) preset_setVoiceFxSendMorph(scene, slot, fx_send_amount) —
 *                 FX send morph ← FX send normal;
 *             (c) if slot == 5 (slot-6/track-7 pair):
 *                 scene_setSlot6Track7MorphAmpEnvelopeDecay(scene,
 *                     scene_getSlot6Track7AmpEnvelopeDecay(scene)).
 *             Then, if the active Scene was touched, rebuild morph.
 * Why:        per-track morph reset. Fans out like `clear send` (the slot
 *             is a Scene child, user F3).
 * Inputs:     job->scene, job->track (maps to slot via ccCopy_slotOf-style
 *             clamping). The fan-out mask comes from bank_sceneFanoutMask().
 * Outputs:    CC_RUN_DONE (or CC_RUN_WAIT if the active Scene is in the
 *             fan-out mask and apply workers are busy). Side effects:
 *             AutoSave marks for instrument morph, FX send morph, Kit decay
 *             morph. Name content changed for instrument row.
 * Accessors:  bank_sceneFanoutMask(), preset_resetSlotMorphToNormal(),
 *             preset_setVoiceFxSendMorph(), scene_getVoiceFxSendAmount(),
 *             scene_setSlot6Track7MorphAmpEnvelopeDecay(),
 *             scene_getSlot6Track7AmpEnvelopeDecay().
 * Affiliates: ccClear_runSend() (model for fan-out clear),
 *             ccCopy_runInstrument() (model for slot-level fan-out with
 *             worker wait), preset_rebuildMorph().
 */
static uint8_t ccClear_runResetMorphTrack(const cc_job_t *job)
{
    uint8_t slot = (job->track < INSTRUMENT_SLOT_COUNT)
                       ? job->track
                       : (uint8_t)(INSTRUMENT_SLOT_COUNT - 1u);
    uint16_t mask = bank_sceneFanoutMask(job->scene);
    uint8_t active = scene_getActiveIndex();
    uint8_t m;

    if ((mask & (uint16_t)(1u << active)) != 0u && !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    for (m = 0u; m < SCENE_COUNT; m++) {
        if ((mask & (uint16_t)(1u << m)) == 0u)
            continue;
        (void)preset_resetSlotMorphToNormal(m, slot);
        (void)preset_setVoiceFxSendMorph(
            m, slot, scene_getVoiceFxSendAmount(m, slot));
        if (slot == INSTRUMENT_SLOT_COUNT - 1u)
            scene_setSlot6Track7MorphAmpEnvelopeDecay(
                m, scene_getSlot6Track7AmpEnvelopeDecay(m));
        ccSvc_nameContentChanged(
            filesystem_identityRow(FS_ROW_INSTRUMENT, m, slot));
    }
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)mask | ((uint32_t)(job->scene & 0xFu) << 16u) |
            (7u << 20u) | ((uint32_t)(slot & 0xFu) << 25u));
    if ((mask & (uint16_t)(1u << active)) != 0u)
        preset_rebuildMorph();
    menu_repaint();
    return CC_RUN_DONE;
}
```

### 7B. `ccClear_runResetSceneMorph()` — ADD after 7A

```c
/*
 * `clear scene reset morph`: equalise the entire Scene's morph endpoints to
 * Normal, including all 6 instrument slots, FX send morph ×6, Kit slot-6
 * decay morph, and all morphable Effect morph endpoints. Does NOT fan out
 * (parallels `clear scene`). Does NOT clear the present bit or touch morph
 * amounts.
 *
 * What:       (a) for each slot 0..5: preset_resetSlotMorphToNormal(scene, slot);
 *             (b) for each slot 0..5: FX send morph ← FX send normal;
 *             (c) Kit decay morph ← Kit decay normal;
 *             (d) effects_resetMorphToNormalSingle(scene);
 *             (e) if active: preset_rebuildMorph() + effects_activateScene().
 * Why:        whole-Scene morph reset for PERF clear. No fan-out because it
 *             parallels the existing `clear scene` which is always single-Scene.
 * Inputs:     job->scene.
 * Outputs:    CC_RUN_DONE (or CC_RUN_WAIT if the Scene is active and apply
 *             workers are busy). Side effects: AutoSave marks for all
 *             instruments' morph bytes, FX send morph cells, Kit decay morph,
 *             Effect region. Name content changed for instrument rows + Scene
 *             + Effect rows.
 * Accessors:  preset_resetSlotMorphToNormal(), preset_setVoiceFxSendMorph(),
 *             scene_getVoiceFxSendAmount(), scene_setSlot6Track7MorphAmpEnvelopeDecay(),
 *             scene_getSlot6Track7AmpEnvelopeDecay(),
 *             effects_resetMorphToNormalSingle(), preset_rebuildMorph().
 * Affiliates: ccClear_runScene() (model for whole-Scene clear without
 *             fan-out), ccClear_runResetMorphTrack() above.
 */
static uint8_t ccClear_runResetSceneMorph(const cc_job_t *job)
{
    uint8_t scene = job->scene;
    uint8_t active = (uint8_t)(scene == scene_getActiveIndex());
    uint8_t slot;

    if (active && !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        (void)preset_resetSlotMorphToNormal(scene, slot);
        (void)preset_setVoiceFxSendMorph(
            scene, slot, scene_getVoiceFxSendAmount(scene, slot));
        ccSvc_nameContentChanged(
            filesystem_identityRow(FS_ROW_INSTRUMENT, scene, slot));
    }
    scene_setSlot6Track7MorphAmpEnvelopeDecay(
        scene, scene_getSlot6Track7AmpEnvelopeDecay(scene));
    (void)effects_resetMorphToNormalSingle(scene);
    ccSvc_nameContentChanged(
        filesystem_identityRow(FS_ROW_SCENE, scene, 0u));
    ccSvc_nameContentChanged(
        filesystem_identityRow(FS_ROW_EFFECT, scene, 0u));
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)(1u << scene) |
            ((uint32_t)(scene & 0xFu) << 16u) | (8u << 20u));
    if (active) {
        preset_rebuildMorph();
        menu_repaintAll();
    }
    return CC_RUN_DONE;
}
```

### 7C. `ccClear_runResetFxMorph()` — ADD after 7B

```c
/*
 * `clear scene reset fx morph`: equalise only the Effect's morphable Morph
 * endpoints to their Normal values. Fans out through the edit mask
 * (paralleling `clear fx`).
 *
 * What:       delegates to effects_resetMorphToNormal(scene) which handles
 *             the fan-out, the morphability check, and the runtime activation.
 * Why:        thin wrapper matching the existing `clear fx` pattern
 *             (ccClear_runFx), adding only the apply-worker wait gate and
 *             trace.
 * Inputs:     job->scene.
 * Outputs:    CC_RUN_DONE (or CC_RUN_WAIT while apply workers are busy and
 *             the active Scene is in the fan-out mask). Side effects: AutoSave
 *             Effect marks, name content changed for Effect rows.
 * Accessors:  effects_resetMorphToNormal(), bank_sceneFanoutMask(),
 *             preset_applyWorkersIdle().
 * Affiliates: ccClear_runFx() (model), effects_resetRecord().
 */
static uint8_t ccClear_runResetFxMorph(const cc_job_t *job)
{
    uint16_t written;

    if ((bank_sceneFanoutMask(job->scene) &
         (uint16_t)(1u << scene_getActiveIndex())) != 0u &&
        !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    written = effects_resetMorphToNormal(job->scene);
    if (written)
        ccClear_effectRowsChanged(written);
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)(written ? written : bank_sceneFanoutMask(job->scene)) |
            ((uint32_t)(job->scene & 0xFu) << 16u) | (9u << 20u));
    if ((written & (uint16_t)(1u << scene_getActiveIndex())) != 0u)
        menu_repaintAll();
    return CC_RUN_DONE;
}
```

### 7D. Dispatch in `ccClear_runJob()` — MODIFY lines 418–457

**Add to the `CC_KIND_TRACK` case (after line 434):**

```c
    case CC_KIND_TRACK:
        if (sel == CC_CLEAR_SEND)
            return ccClear_runSend(job);
        if (sel == CC_CLEAR_RESET_MORPH)
            return ccClear_runResetMorphTrack(job);
        return ccSvc_runPatternClear(job);
```

**Modify:** Replace the current ternary at line 433–434 with the three-way
dispatch above.

**Add to the `CC_KIND_SCENE` switch (after line 448, before `default`):**

```c
        case CC_CLEAR_SCENE_RESET_MORPH:     return ccClear_runResetSceneMorph(job);
        case CC_CLEAR_SCENE_RESET_FX_MORPH:  return ccClear_runResetFxMorph(job);
```

---

## Change 8 — Copy executors in `copyOps.c`

**File:** `Core/Menu/CopyClear/copyOps.c`

### 8A. `ccCopy_runMorphTrack()` — ADD before `ccCopy_runJob()` (before line 887)

**Insert after `ccCopy_runFxSteps()` (after line 885):**

```c
/*
 * `copy track morph`: copy the source track's Normal endpoints onto the
 * destination track's Morph endpoints (instrument images + correlated Scene
 * params). Fans out through the edit mask. Silently drops if types differ.
 *
 * What:       for each Scene in the fan-out mask:
 *             (a) preset_copySlotNormalToMorph(src_scene, s_slot, m, d_slot)
 *                 — returns 0 and does nothing if types differ;
 *             (b) FX send morph ← source Normal FX send amount;
 *             (c) if d_slot == 5: Kit decay morph ← source Kit decay normal.
 *             The type-mismatch skip is silent (no trace event, same as
 *             retargeting drop-all in the existing Pattern paste).
 * Why:        per-track morph copy. Fans out like `copy instrument` (user F3).
 *             No phase machine needed — all writes are immediate retained
 *             commits with no bounded apply (the morph rebuild is one call).
 * Inputs:     job->scene (destination), job->track (destination track).
 *             Source from copyClear_source(). Slot mapping uses the same
 *             track→slot clamping as ccCopy_runInstrument().
 * Outputs:    CC_RUN_DONE (or CC_RUN_WAIT while apply workers are busy and
 *             the active Scene is in the fan-out mask). Side effects: AutoSave
 *             Instrument Morph dirty, FX send morph, Kit decay morph. Name
 *             content changed for instrument rows.
 * Accessors:  copyClear_source(), bank_sceneFanoutMask(),
 *             preset_copySlotNormalToMorph(), preset_setVoiceFxSendMorph(),
 *             scene_getVoiceFxSendAmount(), scene_setSlot6Track7MorphAmpEnvelopeDecay(),
 *             scene_getSlot6Track7AmpEnvelopeDecay(), preset_rebuildMorph().
 * Affiliates: ccCopy_runInstrument() (model for fan-out track-level copy),
 *             ccClear_runResetMorphTrack() (analogous reset).
 */
static uint8_t ccCopy_runMorphTrack(const cc_job_t *job)
{
    const cc_source_t *src = copyClear_source();
    uint8_t s_slot = ccCopy_slotOf(src->track);
    uint8_t d_slot = ccCopy_slotOf(job->track);
    uint16_t mask = bank_sceneFanoutMask(job->scene);
    uint8_t active = scene_getActiveIndex();
    uint8_t m;

    if ((mask & (uint16_t)(1u << active)) != 0u && !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    for (m = 0u; m < SCENE_COUNT; m++) {
        if ((mask & (uint16_t)(1u << m)) == 0u)
            continue;
        /* Silent skip if types differ — preset_copySlotNormalToMorph returns 0. */
        (void)preset_copySlotNormalToMorph(src->scene, s_slot, m, d_slot);
        (void)preset_setVoiceFxSendMorph(
            m, d_slot, scene_getVoiceFxSendAmount(src->scene, s_slot));
        if (s_slot == INSTRUMENT_SLOT_COUNT - 1u &&
            d_slot == INSTRUMENT_SLOT_COUNT - 1u)
            scene_setSlot6Track7MorphAmpEnvelopeDecay(
                m, scene_getSlot6Track7AmpEnvelopeDecay(src->scene));
        ccSvc_nameContentChanged(
            filesystem_identityRow(FS_ROW_INSTRUMENT, m, d_slot));
    }
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)mask | ((uint32_t)(job->scene & 0xFu) << 16u) |
            (10u << 20u) | ((uint32_t)(d_slot & 0xFu) << 25u));
    if ((mask & (uint16_t)(1u << active)) != 0u)
        preset_rebuildMorph();
    menu_repaint();
    return CC_RUN_DONE;
}
```

### 8B. `ccCopy_runSceneMorph()` — ADD after 8A

```c
/*
 * `copy scene morph`: copy all Normal endpoints of the source Scene onto
 * the destination Scene's Morph endpoints for all matching-type components.
 * Does NOT fan out (parallels `copy scene`). Does NOT exchange or reset the
 * edit mask. Does NOT touch morph amounts.
 *
 * What:       (a) for each slot 0..5: preset_copySlotNormalToMorph(src, slot,
 *                 dst, slot) — silent skip if types differ;
 *             (b) for each slot 0..5: dst FX send morph ← src FX send normal;
 *             (c) dst Kit decay morph ← src Kit decay normal;
 *             (d) if src Effect type == dst Effect type:
 *                 for each morphable Effect param, dst morph ← src normal;
 *                 else skip Effect entirely (silent).
 *             (e) if active: rebuild morph + Effect activation.
 * Why:        whole-Scene morph copy for PERF. No fan-out because `copy scene`
 *             is always single-destination.
 * Inputs:     job->scene (destination). Source from copyClear_source().
 * Outputs:    CC_RUN_DONE (or CC_RUN_WAIT while apply workers drain for an
 *             active destination). Side effects: AutoSave marks for all
 *             changed Instrument morph, FX send morph, Kit decay morph,
 *             Effect region. Name content changed for instrument + Scene +
 *             Effect rows.
 * Accessors:  copyClear_source(), preset_copySlotNormalToMorph(),
 *             preset_setVoiceFxSendMorph(), scene_getVoiceFxSendAmount(),
 *             scene_setSlot6Track7MorphAmpEnvelopeDecay(),
 *             scene_getSlot6Track7AmpEnvelopeDecay(),
 *             scene_effectConst(), effects_paramMorphable(),
 *             scene_effectRecordForWholeCommit(), scene_finishEffectWholeCommit(),
 *             effects_activateScene(), preset_rebuildMorph().
 * Affiliates: ccCopy_runScene() (model for whole-Scene copy),
 *             ccClear_runResetSceneMorph() (analogous reset).
 */
static uint8_t ccCopy_runSceneMorph(const cc_job_t *job)
{
    const cc_source_t *src = copyClear_source();
    uint8_t dst = job->scene;
    uint8_t active = (uint8_t)(dst == scene_getActiveIndex());
    const effect_record_t *src_fx = scene_effectConst(src->scene);
    const effect_record_t *dst_fx = scene_effectConst(dst);
    uint8_t slot;

    if (!src)
        return CC_RUN_DROP;
    if (active && !preset_applyWorkersIdle())
        return CC_RUN_WAIT;

    /* Instrument slots: copy src Normal → dst Morph (silent skip per slot). */
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        (void)preset_copySlotNormalToMorph(src->scene, slot, dst, slot);
        (void)preset_setVoiceFxSendMorph(
            dst, slot, scene_getVoiceFxSendAmount(src->scene, slot));
        ccSvc_nameContentChanged(
            filesystem_identityRow(FS_ROW_INSTRUMENT, dst, slot));
    }

    /* Kit slot-6 decay morph ← src Kit decay normal. */
    scene_setSlot6Track7MorphAmpEnvelopeDecay(
        dst, scene_getSlot6Track7AmpEnvelopeDecay(src->scene));

    /* Effect: copy src Normal → dst Morph if types match. */
    if (src_fx && dst_fx && src_fx->type == dst_fx->type) {
        effect_record_t *record = scene_effectRecordForWholeCommit(dst);

        if (record) {
            uint8_t i;

            for (i = 0u; i < EFFECT_PARAM_COUNT; i++) {
                if (!effects_paramMorphable(src_fx->type, i))
                    continue;
                record->morph[i] = src_fx->normal[i];
            }
            scene_finishEffectWholeCommit(dst);
        }
    }
    /* If types differ, the Effect is silently skipped. */

    ccSvc_nameContentChanged(
        filesystem_identityRow(FS_ROW_SCENE, dst, 0u));
    ccSvc_nameContentChanged(
        filesystem_identityRow(FS_ROW_EFFECT, dst, 0u));
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)(1u << dst) |
            ((uint32_t)(dst & 0xFu) << 16u) | (11u << 20u));
    if (active) {
        preset_rebuildMorph();
        effects_activateScene(dst);
        menu_repaintAll();
    }
    return CC_RUN_DONE;
}
```

### 8C. Dispatch in `ccCopy_runJob()` — MODIFY lines 887–920

**Add to the `CC_KIND_TRACK` case (after line 902):**

```c
    case CC_KIND_TRACK:
        if (sel == CC_COPY_INSTRUMENT)
            return ccCopy_runInstrument(job);
        if (sel == CC_COPY_MORPH)
            return ccCopy_runMorphTrack(job);
        return ccSvc_runPatternPaste(job);
```

**Modify:** Replace the current ternary at line 901–902 with the three-way
dispatch above.

**Add to the `CC_KIND_SCENE` switch (after line 909, before `default`):**

```c
        case CC_COPY_SCENE_MORPH:    return ccCopy_runSceneMorph(job);
```

---

## Change 9 — Track/FX clear enum value alignment

**Issue:** With the label array split (Change 3A), the enum values of
`CC_CLEAR_RESET_MORPH` must be consistent between the two menus. In the
base track menu (`ccClear_trackLabels[]`), "reset morph" is at index 4. In
the FX track menu (`ccClear_trackFxLabels[]`), "reset morph" is at index 5
(after "send" at index 4).

**The enum `cc_clear_obj_sel_t` has one value `CC_CLEAR_RESET_MORPH`.** The
job's `op` field encodes the selection index from the label array, not the
enum value. The dispatch in `ccClear_runJob()` reads `sel` as
`job->op & CC_JOB_SEL_MASK`.

**Critical check:** When `ccClear_requestClear()` queues a job (line 91),
it encodes `selection` into the op:
```c
job.op = (uint8_t)(CC_JOB_CLEAR | (selection & CC_JOB_SEL_MASK));
```

The `selection` is the encoder index from the menu (0=cancel, 1=all, etc.).
For `CC_MENU_CLEAR_TRACK`, index 4 = "reset morph". For
`CC_MENU_CLEAR_TRACK_FX`, index 5 = "reset morph".

**Therefore `CC_CLEAR_RESET_MORPH` cannot be a single enum value used in
the dispatch.** The dispatch must check the selection index, which differs
by menu. Two solutions:

**Solution A (recommended):** Use a single enum value
`CC_CLEAR_RESET_MORPH = 5` that appears at the same position in both label
arrays. Change the track label array to have "send" as a hidden entry that
the track-mode count skips:

```c
static const char *const ccClear_trackLabels[] = {
    "cancel", "track", "track auto", "track notes", "send", "reset morph"
};
```

With `CC_MENU_CLEAR_TRACK` count = 5 (indices 0–4: cancel, track, auto,
notes, **reset morph** shown), the encoder never reaches index 4 ("send") in
VOICE mode. Wait — that doesn't work because count=5 means the encoder goes
0..4, and "reset morph" is at index 5.

**Solution B (correct):** Keep a single shared array with "reset morph" at
the end. The track-mode count skips "send":

```c
static const char *const ccClear_trackLabels[] = {
    "cancel", "track", "track auto", "track notes", "send", "reset morph"
};
```

`CC_MENU_CLEAR_TRACK` count = 5: indices 0..4, but we need to skip index 4
("send") and show index 5 ("reset morph") instead. This requires a remapping
in the label function, which is fragile.

**Solution C (simplest):** Keep the split arrays from Change 3A. In the
dispatch, handle the selection index based on the object kind and the mode
that created the job. BUT the job does not encode the mode — only the
selection index and the object kind.

**Actual resolution:** The job's `kind` is `CC_KIND_TRACK` for both menus.
The selection index for "reset morph" is 4 in VOICE mode and 5 in EFFECTS
mode. The dispatch in `ccClear_runJob()` receives the raw selection index.
We need both indices to dispatch to the same executor.

**Final solution:** In the `CC_KIND_TRACK` case of `ccClear_runJob()`, test:

```c
    case CC_KIND_TRACK:
        if (sel == CC_CLEAR_SEND)
            return ccClear_runSend(job);
        if (sel == CC_CLEAR_RESET_MORPH || sel == CC_CLEAR_RESET_MORPH + 1u)
            return ccClear_runResetMorphTrack(job);
        return ccSvc_runPatternClear(job);
```

No — this is fragile. **Better:** define both indices as enum values:

```c
typedef enum {
    CC_CLEAR_CANCEL = 0u,
    CC_CLEAR_ALL,
    CC_CLEAR_AUTO,
    CC_CLEAR_NOTES,
    CC_CLEAR_SEND,           /* 4 — EFFECTS TRACK only */
    CC_CLEAR_RESET_MORPH     /* 5 — both menus; in VOICE TRACK menu, at index 4
                              *      because "send" is not shown. */
} cc_clear_obj_sel_t;
```

With a single array:
```c
static const char *const ccClear_trackLabels[] = {
    "cancel", "track", "track auto", "track notes", "send", "reset morph"
};
```

- `CC_MENU_CLEAR_TRACK` count = 5. The encoder sees indices 0..4. But index
  4 is "send", not "reset morph". **This is wrong for VOICE mode.**

**The root problem:** we need "reset morph" visible in the VOICE TRACK menu
(where "send" is NOT visible) and in the EFFECTS TRACK menu (where "send"
IS visible). With a single array and simple count gating, this is impossible.

**DEFINITIVE SOLUTION:** Split the label arrays as in Change 3A. Each array
has its own index space. The enum values capture the **VOICE TRACK** index
space (matching `ccClear_trackLabels[]`), and we add a second enum or define
for the FX-TRACK index space.

Actually, looking more carefully: the enum `cc_clear_obj_sel_t` is used in
`ccClear_requestClear()` only to check `selection == 0u` (cancel). The
dispatch uses the raw selection index. So we just need the dispatch to
handle both indices:

```c
    case CC_KIND_TRACK:
        if (sel == 4u && job was from FX menu)  /* can't distinguish */
```

**SIMPLEST CORRECT SOLUTION:** Since the job encodes the raw selection
index and the kind `CC_KIND_TRACK` but NOT the menu, and since "send" is
index 4 in both arrays, we make "reset morph" index 4 in the VOICE array
and index 5 in the FX array. The dispatch handles both:

```c
    case CC_KIND_TRACK:
        if (sel == CC_CLEAR_SEND)        /* 4 */
            return ccClear_runSend(job);
        if (sel == CC_CLEAR_RESET_MORPH) /* 5 from FX, or... */
            return ccClear_runResetMorphTrack(job);
```

Wait, in VOICE mode "reset morph" would be at index 4 (replacing "send"
which isn't shown). But the enum value `CC_CLEAR_SEND = 4`. So the dispatch
would confuse "send" and "reset morph".

**ACTUAL SIMPLEST SOLUTION:** Put "reset morph" BEFORE "send" in the shared
array. Reorder:

```c
static const char *const ccClear_trackLabels[] = {
    "cancel", "track", "track auto", "track notes", "reset morph", "send"
};
```

- `CC_MENU_CLEAR_TRACK` count = 5 (0..4): cancel, track, auto, notes,
  **reset morph**. ✓
- `CC_MENU_CLEAR_TRACK_FX` count = 6 (0..5): cancel, track, auto, notes,
  **reset morph**, send. ✓

Enum values:
```c
    CC_CLEAR_NOTES,        /* 3 */
    CC_CLEAR_RESET_MORPH,  /* 4 — both menus */
    CC_CLEAR_SEND          /* 5 — FX only */
```

Dispatch:
```c
    case CC_KIND_TRACK:
        if (sel == CC_CLEAR_SEND)
            return ccClear_runSend(job);
        if (sel == CC_CLEAR_RESET_MORPH)
            return ccClear_runResetMorphTrack(job);
        return ccSvc_runPatternClear(job);
```

This works with a single shared array! The existing `clear send` executor
is triggered by selection index 5 (was 4), which is the new
`CC_CLEAR_SEND` value.

**This supersedes the split approach in Change 3A.** Update Change 3
accordingly.

---

## Change 9 — REVISED Change 3A/3C/3D (supersedes Change 3A)

### Revised 3A. Track label array — MODIFY lines 36–38

**Replace with (single shared array, reordered):**

```c
/*
 * What:       track clear labels with "reset morph" before "send". The VOICE
 *             track clear menu shows count 5 (indices 0..4: cancel, track,
 *             track auto, track notes, reset morph). The EFFECTS track menu
 *             shows count 6 (0..5: adds "send" at the end). This ordering
 *             preserves a single shared array: the encoder in VOICE mode
 *             never reaches index 5 ("send"), and in EFFECTS mode it does.
 * Why:        avoids split label arrays while making "reset morph" visible
 *             in both menus and "send" visible only in EFFECTS mode.
 * Inputs:     ccClear_label() indexes by selection, gated by selectionCount.
 * Outputs:    const label strings.
 * Affiliates: CC_CLEAR_RESET_MORPH (now = 4), CC_CLEAR_SEND (now = 5).
 */
static const char *const ccClear_trackLabels[] = {
    "cancel", "track", "track auto", "track notes", "reset morph", "send"
};
```

### Revised 1A. Enum reorder in `clearOps.h`

```c
typedef enum {
    CC_CLEAR_CANCEL = 0u,
    CC_CLEAR_ALL,
    CC_CLEAR_AUTO,
    CC_CLEAR_NOTES,
    CC_CLEAR_RESET_MORPH,   /* 4 — visible in both TRACK and TRACK_FX menus */
    CC_CLEAR_SEND           /* 5 — visible only in TRACK_FX (EFFECTS mode) */
} cc_clear_obj_sel_t;
```

### Revised 3C. Selection counts

```c
    case CC_MENU_CLEAR_TRACK:    return 5u;  /* +reset morph (was 4) */
    case CC_MENU_CLEAR_TRACK_FX: return 6u;  /* +reset morph, send moves to end (was 5) */
```

### Revised 3D. No change needed

`ccClear_label()` continues to use the shared `ccClear_trackLabels` array
for both `CC_MENU_CLEAR_TRACK` and `CC_MENU_CLEAR_TRACK_FX` (the existing
fallthrough at line 75–76 is preserved).

### Impact on existing code

`CC_CLEAR_SEND` changes from value 4 to value 5. Check all references:

1. `ccClear_runJob()` line 433: `sel == CC_CLEAR_SEND` — uses the enum, OK.
2. `ccClear_triggersOffNow()` line 170: does not reference `CC_CLEAR_SEND`.
3. `ccClear_requestClear()` line 89: checks `selection == 0u`, not a named
   value.

No other code references `CC_CLEAR_SEND` by value. The reorder is safe.

---

## Change 10 — `#include` additions

### 10A. `clearOps.c` — verify existing includes

The file already includes `InstrumentManager.h` (line 21) and
`presetManager.h` (line 19). `preset_resetSlotMorphToNormal()` and
`preset_rebuildMorph()` are in `presetManager.h`. `effects_resetMorphToNormal()`
and `effects_resetMorphToNormalSingle()` are in `EffectsManager.h` (line 20).
`scene_getVoiceFxSendAmount()` is in `SceneData.h` (line 18).

**No new `#include` needed in `clearOps.c`.**

### 10B. `copyOps.c` — verify existing includes

The file already includes `InstrumentManager.h` (line 20),
`presetManager.h` (line 19), `EffectsManager.h` (line 19), `SceneData.h`
(line 16). `preset_copySlotNormalToMorph()` is in `presetManager.h`.
`effects_paramMorphable()` is in `EffectsManager.h`.

**No new `#include` needed in `copyOps.c`.**

### 10C. `presetManager.c` — verify existing includes

`autosave_markInstrumentMorphDirty()` is in `Autosave.h`. Check:

```
grep -n '#include.*Autosave' presetManager.c
```

If not present, add `#include "Autosave.h"`. (The file includes
`AutosaveTrace.h` which may or may not include `Autosave.h`.)

---

## Change 11 — Trace kind codes

**File:** `Core/Bank/Scene/AutosaveTrace.h` (or wherever
`AUTOSAVE_TRACE_CC_EVT_FANOUT` kind codes are documented)

The existing `AUTOSAVE_TRACE_CC_EVT_FANOUT` event uses a kind code in
bits 20..24. Existing kind codes (from the existing executors):

| Code | Operation |
|------|-----------|
| 1    | copy instrument |
| 2    | copy kit |
| 3    | copy effect |
| 4    | clear send |
| 5    | clear fx |
| 6    | copy fx steps |

**Add:**

| Code | Operation |
|------|-----------|
| 7    | clear track reset morph |
| 8    | clear scene reset morph |
| 9    | clear scene reset fx morph |
| 10   | copy track morph |
| 11   | copy scene morph |

No code change needed — the kind codes are immediate constants in the
`ccTrace()` calls within the executors (already shown in Changes 7A–8B).

---

## Summary of files changed

| File | Changes |
|------|---------|
| `Core/Menu/CopyClear/clearOps.h` | ADD 3 enum values (1A revised, 1B) |
| `Core/Menu/CopyClear/clearOps.c` | MODIFY labels (3A revised), MODIFY scene labels (3B), MODIFY counts (3C revised), ADD 3 executors (7A, 7B, 7C), MODIFY dispatch (7D) |
| `Core/Menu/CopyClear/copyOps.h` | ADD 2 enum values (2A, 2B) |
| `Core/Menu/CopyClear/copyOps.c` | MODIFY labels (4A, 4B), MODIFY counts (4C), ADD comment to identical check (4D), ADD 2 executors (8A, 8B), MODIFY dispatch (8C) |
| `Core/Bank/Scene/Preset/presetManager.h` | ADD 2 function declarations (5A) |
| `Core/Bank/Scene/Preset/presetManager.c` | ADD 2 function implementations (5B) |
| `Core/DSP/Effects/EffectsManager.h` | ADD 2 function declarations (6A) |
| `Core/DSP/Effects/EffectsManager.c` | ADD 2 function implementations (6B) |

**Total new executable code:** ~350 lines (5 executors + 4 helpers).
**Total label/enum/count changes:** ~30 lines.
**No new static RAM. No new includes. No new files.**

---

## Implementation order

1. **Changes 1A-revised + 1B** — enum values in `clearOps.h`.
2. **Changes 2A + 2B** — enum values in `copyOps.h`.
3. **Changes 3A-revised + 3B + 3C-revised** — labels and counts in
   `clearOps.c`. Build check: compiles with unused enum values.
4. **Changes 4A + 4B + 4C + 4D** — labels, counts, identical check in
   `copyOps.c`. Build check: compiles with new enum values referenced.
5. **Changes 5A + 5B** — shared helpers in `presetManager.h/c`. Build
   check: helpers compile standalone.
6. **Changes 6A + 6B** — Effect helpers in `EffectsManager.h/c`. Build
   check: Effect helpers compile standalone.
7. **Changes 7A + 7B + 7C + 7D** — clear executors and dispatch in
   `clearOps.c`. Build check: all 3 clear operations link.
8. **Changes 8A + 8B + 8C** — copy executors and dispatch in `copyOps.c`.
   Build check: all 2 copy operations link.
9. **Change 10C** — verify `#include` for `Autosave.h` in `presetManager.c`.
10. `make all && make img` — full clean build, verify link budget.

---

## Verification after implementation

Per `S076_P3_COPYCLEAR_MORPH_ADDS.md` §10, the 14-item checklist confirms
each operation on hardware with a known Scene/Kit/Morph state. No additional
verification items are needed beyond those already specified.

---

## Implementation Log (Session 076, 2026-10-06)

Worked on branch `dev-ph6-cleanup`. The working tree already carried the S076
P2 (LFO Scene reset) changes; those were preserved untouched. Every change
below landed with an adjacent comment block in both the declaring header and
the defining source (or the single source for the static executors).

### Step 1 — Enums (`clearOps.h`, `copyOps.h`) — DONE

- `CC_CLEAR_RESET_MORPH` added to `cc_clear_obj_sel_t` **before**
  `CC_CLEAR_SEND`. Final layout: cancel 0, all 1, auto 2, notes 3,
  reset morph 4, send 5. `CC_CLEAR_SEND` therefore moved 4 -> 5.
- `CC_CLEAR_SCENE_RESET_MORPH` (8) and `CC_CLEAR_SCENE_RESET_FX_MORPH` (9)
  appended to `cc_clear_scene_sel_t`.
- `CC_COPY_MORPH` (2) appended to `cc_copy_track_sel_t`.
- `CC_COPY_SCENE_MORPH` (5) appended to `cc_copy_scene_sel_t`.

### Step 2 — Shared morph helpers (`presetManager.h/.c`) — DONE

- `preset_resetSlotMorphToNormal(scene, slot)` and
  `preset_copySlotNormalToMorph(src_scene, src_slot, dst_scene, dst_slot)`.
- Both use `scene_instrumentSlot()`/`scene_instrumentSlotConst()` and the
  registry descriptor table, iterate `INSTRUMENT_PARAM_FLAG_MORPHABLE` rows
  only, count changed bytes, and mark `autosave_markInstrumentMorphDirty()`
  plus `bank_invalidateSdCleanScene()` only on change.

### Step 3 — Effect morph helpers (`EffectsManager.h/.c`) — DONE

- `effects_resetMorphToNormal(dst_scene)` (fans out via
  `effects_fanoutMask(dst_scene, 1u)`, returns the written Scene mask).
- `effects_resetMorphToNormalSingle(scene_index)` (no fan-out).
- Both use the existing in-place whole-record commit pair.

### Step 4 — Clear executors (`clearOps.c`) — DONE

- `ccClear_runResetMorphTrack()` (trace kind 7),
  `ccClear_runResetSceneMorph()` (kind 8), `ccClear_runResetFxMorph()`
  (kind 9). Dispatch extended for `CC_KIND_TRACK` (reset morph / send /
  pattern) and the `CC_KIND_SCENE` switch.

### Step 5 — Copy executors (`copyOps.c`) — DONE

- `ccCopy_runMorphTrack()` (kind 10) and `ccCopy_runSceneMorph()` (kind 11).
  Dispatch extended for `CC_KIND_TRACK` (morph / instrument / pattern) and the
  `CC_KIND_SCENE` switch.

### Step 6 — Labels, counts, indicators — DONE

- `clearOps.c`: track labels `{cancel, track, track auto, track notes,
  reset morph, send}` served by one shared array; scene labels add index 8/9;
  counts 5 / 6 / 10.
- `copyOps.c`: track labels `{track, instrument, morph}`; scene labels add
  index 5; counts 3 / 6.
- `copyClearSession.c`: the `CC_KIND_TRACK` source indicator prints `m`
  for `CC_COPY_MORPH`, and the `CC_KIND_SCENE` copy suffix array gained
  `m` at index 5 (design section 9).

### Step 7 — Build — DONE

- `make all` and `make img` both succeed with no warnings in the touched
  files.

### Deviations from the schedule (all deliberate)

1. **Change 9 applied as written** (its later, definitive revision): the enum
   is reordered so `CC_CLEAR_RESET_MORPH` = 4 and `CC_CLEAR_SEND` = 5, and
   the shared `ccClear_trackLabels[]` array is kept (**the Change 3A split is
   NOT used**). Consequence: in the EFFECTS TRACK menu the order is
   `reset morph` then `send`, versus the design document's `send` then
   `reset morph`. This is the schedule's explicitly chosen resolution of the
   shared-array problem; the menu still exposes exactly the designed actions.
2. **Early-trigger guard added** in `ccCopy_requestPaste()`: `copy
   instrument` and `copy morph` no longer call
   `ccSvc_pasteTriggersNow()`. `ccSvc_pasteGeometry()` normalizes every
   track-kind paste to `CC_COPY_ALL`, so without this guard a track-kind slot
   paste would write the destination track's Pattern triggers from the source
   (spec sections 6.4 and 12.6 list early triggers only for step/bar and
   `copy track` repl/merge). This also corrects a pre-existing latent
   `copy instrument` trigger write.
3. **Effect commit gating**: the two new Effect helpers call
   `scene_finishEffectWholeCommit()` only when a Morph byte actually changed.
   The schedule's draft comment assumed the close is harmless when nothing
   changed; in fact it always re-marks the whole Effect region, so gating
   avoids spurious AutoSave work.
4. **Card-clean invalidation** added to both instrument helpers
   (`bank_invalidateSdCleanScene()`), matching the established
   KitMrp/InstrumentMrp/`copy instrument` rule when resident Morph bytes are
   written directly rather than through `preset_storeInstrumentEndpoint()`.
5. **`copyClearSession.c` indicator edits** were not in the schedule's file
   list, but are part of the goal document's section 9 table; they are tiny and
   keep the morph copies visually distinct.
6. **`ccCopy_runSceneMorph()` reads the source pointer before using it**
   (the schedule's draft dereferenced `src->scene` before its `!src` check).

### Build results (DEV config)

| Metric | Before | After | Delta |
|--------|-------:|------:|------:|
| text | 532,336 | 535,184 | +2,848 |
| data | 416 | 416 | 0 |
| bss | 426,744 | 426,744 | 0 |
| Flash payload | 532,752 / 753,664 | 535,600 / 753,664 | headroom 218,064 B |

- `build/LXRV2_lxr02.img` written as a 535,600 B payload (535,616 B file).
- **No new static RAM**: `data` and `bss` are unchanged, consistent with
  the design section 7 claim of no new static RAM allocations. The executors
  use the existing foreground stack budget (loop counters and Scene pointers
  only).
- The +2,848 B text delta is larger than the schedule's 500-800 B estimate; the
  new code is five executors plus four helpers plus label strings, and LTO
  re-optimized several translation units whose inlining decisions changed. It
  is comfortably inside the flash headroom.

### Remaining verification (hardware, user)

The 14-item checklist in `S076_P3_COPYCLEAR_MORPH_ADDS.md` section 10 is
unchanged and is the acceptance test. In particular the user should confirm:

1. The new menu labels appear at the end of each menu (`reset morph`,
   `reset fx morph`, `morph`, `scene morph`).
2. The EFFECTS TRACK clear-menu order (`reset morph` before `send`), the
   one intentional deviation from the design document's ordering.
3. `copy instrument` no longer alters the destination track's step triggers
   (the early-trigger fix), if that was ever observed.
4. AutoSave captures the equalised endpoints after each operation and they
   survive a reboot.

---

## Post-Implementation Code Assessment

Date: 2026-10-06. Scope: all files changed in the S076 P3 morph-adds
implementation, verified against the 11-change schedule above.

### Method

Every changed file was read in its implemented state and cross-referenced
against the schedule's specification (change descriptions, line targets,
accessor/affiliate lists, fan-out/no-fan-out rules). The copyClearSession.c
indicator edits (implementation deviation 5) were independently verified.

### Verdict: PASS — all 5 operations correctly implemented

No blocking issues. One minor indicator gap noted below (non-blocking).

### Per-file findings

**clearOps.h** (135 lines)
- `cc_clear_obj_sel_t`: CC_CLEAR_RESET_MORPH=4, CC_CLEAR_SEND=5 — correct
  reordering with full comment block. Existing values 0–3 unchanged.
- `cc_clear_scene_sel_t`: CC_CLEAR_SCENE_RESET_MORPH=8,
  CC_CLEAR_SCENE_RESET_FX_MORPH=9 — correct placement and documentation.

**clearOps.c** (660 lines)
- Track labels: single shared array
  `{"cancel","track","track auto","track notes","reset morph","send"}` —
  correct. VOICE count=5 (indices 0–4), EFFECTS count=6 (indices 0–5).
  The label function's TRACK/TRACK_FX fallthrough is preserved.
- Scene labels: `"reset morph","reset fx morph"` appended at indices 8, 9.
  Scene count=10. Correct.
- `ccClear_runResetMorphTrack()` (line 478): fans out via
  `bank_sceneFanoutMask()`, calls `preset_resetSlotMorphToNormal()` +
  `preset_setVoiceFxSendMorph()` + conditional slot-6 decay, HCNAMES R flag
  dropped per member, `preset_rebuildMorph()` only when active is in the
  mask, trace kind 7. Correct.
- `ccClear_runResetSceneMorph()` (line 535): no fan-out, iterates all 6
  slots with the same instrument + FX-send + name-drop pattern, then
  slot-6 decay outside the loop (correct: always applied, not gated on
  slot==5 inside the loop), then `effects_resetMorphToNormalSingle()`,
  HCNAMES R dropped for Scene and Effect rows, `preset_rebuildMorph()` +
  `effects_activateScene()` equivalent (through the single helper) +
  `menu_repaintAll()` when active, trace kind 8. Correct.
- `ccClear_runResetFxMorph()` (line 585): fans out via
  `effects_resetMorphToNormal()` (delegates fan-out to the Effect helper),
  `ccClear_effectRowsChanged()` on written mask, trace kind 9. Correct.
- Dispatch in `ccClear_runJob()` (line 604):
  - CC_KIND_TRACK: `reset morph` tested first, then `send`, then Pattern
    fallthrough. Correct three-way.
  - CC_KIND_SCENE: two new `case` entries at the end of the `switch`.
    Correct.

**copyOps.h** (169 lines)
- `cc_copy_track_sel_t`: CC_COPY_MORPH=2 appended. Correct.
- `cc_copy_scene_sel_t`: CC_COPY_SCENE_MORPH=5 appended. Correct.
- `ccCopy_runJob()` header comment updated to list the new executors. Correct.

**copyOps.c** (1163 lines)
- Track labels: `{"track","instrument","morph"}`, count=3. Correct.
- Scene labels: `"scene morph"` appended at index 5, count=6. Correct.
- Identical-paste check: CC_COPY_MORPH deliberately not listed (Normal→Morph
  is never a same-object no-op). Comment explains the rationale. Correct.
- `ccCopy_runMorphTrack()` (line 975): null-checks `src` before
  dereferencing (deviation 6), fans out via `bank_sceneFanoutMask()`,
  `preset_copySlotNormalToMorph()` with silent type-mismatch skip
  (returns 0 and no side effects), FX-send reads source Normal, slot-6
  decay gated on both `s_slot` and `d_slot` being slot 5, HCNAMES R
  dropped per member, `preset_rebuildMorph()` when active in mask,
  trace kind 10. Correct.
- `ccCopy_runSceneMorph()` (line 1049): no fan-out, null-checks `src` first,
  iterates 6 slots with `preset_copySlotNormalToMorph()` + FX-send + name
  drop, slot-6 decay outside loop (always applied), Effect block: type
  match gated, `scene_effectRecordForWholeCommit()` /
  `scene_finishEffectWholeCommit()` pair with commit gated on `changed`
  (deviation 3), `effects_activateScene()` when active, HCNAMES R dropped
  for Scene and Effect rows, trace kind 11. Correct.
- Dispatch in `ccCopy_runJob()` (line 1115):
  - CC_KIND_TRACK: `morph` tested first, then `instrument`, then Pattern
    fallthrough. Correct three-way.
  - CC_KIND_SCENE: `CC_COPY_SCENE_MORPH` case added to the switch. Correct.

**presetManager.h** (lines 630–631)
- Both declarations present with correct signatures. Correct.

**presetManager.c** (lines 3440, 3495)
- `preset_resetSlotMorphToNormal()`: iterates only MORPHABLE descriptors,
  copies Normal→Morph, counts changes, marks `autosave_markInstrumentMorphDirty()`
  + `bank_invalidateSdCleanScene()` only on change. Does NOT queue the
  Morph worker (caller's responsibility). Full comment block. Correct.
- `preset_copySlotNormalToMorph()`: uses `scene_instrumentSlotConst()` for
  source (read-only), type-mismatch guard returns 0 with no side effects,
  copies source Normal→destination Morph for MORPHABLE descriptors only,
  marks destination dirty on change. Full comment block. Correct.

**EffectsManager.h** (lines 391–392)
- Both declarations present with correct signatures and full contract
  comment block. Correct.

**EffectsManager.c** (lines 1012, 1067)
- `effects_resetMorphToNormal()`: modelled after `effects_resetRecord()`,
  uses `effects_fanoutMask()` with same-type mask, iterates
  `effects_paramMorphable()` for each index, copies Normal→Morph, commits
  via `scene_finishEffectWholeCommit()` only when `changed`, rebuilds
  active runtime, returns written Scene mask. Correct.
- `effects_resetMorphToNormalSingle()`: single-Scene version, same
  morphable-only loop, conditional commit, conditional runtime rebuild.
  Correct.

**copyClearSession.c** (indicators, deviation 5)
- Track-level: CC_COPY_MORPH tested, indicator suffix "m". Correct.
- Scene copy: `copy_suffix[]` extended to 6 entries with "m" for
  CC_COPY_SCENE_MORPH=5, guard `selection < 6u`. Correct.

### Noted items

1. **Scene-clear indicator gap (cosmetic, non-blocking).**
   `copyClearSession.c` line 844: the Scene clear `clear_suffix[]` array
   covers indices 0–7, guarded by `selection < 8u`. The two new Scene clear
   selections CC_CLEAR_SCENE_RESET_MORPH=8 and CC_CLEAR_SCENE_RESET_FX_MORPH=9
   fall outside the guard and produce no indicator suffix, rendering the same
   as "clear scene" ("SNN" with no suffix letter). Functionally harmless —
   the correct executor dispatches regardless — but the indicator does not
   distinguish these operations. Fix: extend `clear_suffix[]` to 10 entries
   (e.g. `"M"` and `"Mf"` for indices 8 and 9) and update the guard to
   `selection < 10u`.

2. **Implementation deviations (all sound).** The six deliberate deviations
   documented in the Implementation Log above are confirmed correct:
   - (1) early-trigger guard fix — also fixes a latent issue with
     `copy instrument`.
   - (2) Effect commit gated on `changed` — avoids unnecessary AutoSave
     marks when all Morph bytes already equal Normal.
   - (3) card-clean invalidation via `bank_invalidateSdCleanScene()` —
     correct scope for retained writes.
   - (4) copyClearSession.c indicator edits — verified as described.
   - (5) source pointer null-check ordering — guards dereference.
   - (6) Kit slot-6 decay placement — `ccCopy_runMorphTrack()` gates on
     both `s_slot` and `d_slot` being slot 5, which is correct (the decay
     is generated only for track 7→track 7 copies).

3. **Build budget.** +2,848 B text is ~3.5× the 500–800 B estimate. The
   excess is accounted for by the five executors (not the originally
   estimated three), four helpers (two preset, two Effect), the comment
   blocks' string contributions, and LTO re-optimisation of touched
   translation units. 218 KB flash headroom remains. No new static RAM.

### Summary

All 11 change groups implemented correctly. The Change 9 resolution (single
shared label array with `CC_CLEAR_RESET_MORPH` before `CC_CLEAR_SEND`) is
applied as the definitive solution. The only actionable item is the
cosmetic Scene-clear indicator gap (item 1 above), which can be addressed
in a follow-up commit.
