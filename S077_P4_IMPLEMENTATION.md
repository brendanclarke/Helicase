# S077 P4 — Implementation Schedule

**Parent:** `S077_P4_SCN_MORPH_FAN_CORRECTION.md`
**Status (2026-10-08):** implemented — build clean, boot image written.

---

## Overview

Two executor rewrites (add edit-mask fan-out), two label string changes, and
comment/spec updates across 8 files.  No new RAM, no new functions, no new
enums, no ISR/DSP path, no file-format change.

---

## Implementation Log

### 2026-10-08 — complete

All 21 planned changes are applied; every code change carries its comment
block adjacent in the same file, and the paired `.c`/`.h` comment twins are
updated together. Build is clean and the boot image is written.

**Build (DEV config, `make all` then `make img`):**

```
   text     data     bss      dec      hex   filename
 538608      420  427008   966036    ebd94   build/lxr02.elf
Flash : 539,028 / 753,664 B used, headroom 214,636 B
ITCM  : 4,168 / 16,384 B;  DTCM statics 4,472 B
FXBUF : 126,592 B at 0x20001180 (min 122,880, margin 3,712)
Written: build/LXRV2_lxr02.img (539028b) OK
```

Compared with the S077 P3 close (`text=538,696`, image payload 539,116 B) the
P4 change is **text −88 B**, `data`/`bss` unchanged, RAM unchanged. The smaller
image comes from replacing the duplicated single-destination bodies with the
compact fan-out loop; the two label strings grow +11 characters total. No
build warnings from the changed translation units.

**Steps taken**

1. **Change 1 / 2 — label strings** (`copyOps.c`): `ccCopy_trackLabels[]`
   index 2 `"morph"` → `"inst -> morph"` (13 chars); `ccCopy_sceneLabels[]`
   index 5 `"scene morph"` → `"scene -> morph"` (14 chars, exactly the display
   limit). Both comment blocks replaced as specified.
2. **Change 3 — `ccCopy_runSceneMorph()` rewrite** (`copyOps.c`): the single
   destination is replaced by a `bank_sceneFanoutMask(job->scene)` loop;
   `active` is now the Scene index (not a boolean), `dst` is removed and `m`
   is the per-member destination, the Effect type check is per member, and the
   trace logs the full mask. Comment block replaced.
3. **Change 4 — `ccClear_runResetSceneMorph()` rewrite** (`clearOps.c`): same
   fan-out model; each member equalises its OWN Normal→Morph;
   `effects_resetMorphToNormalSingle(m)` per member (deliberately not the
   fan-out variant, to avoid double fan-out). Comment block replaced.
4. **Change 8 — `clearOps.c` file header** (line 7): `clear scene reset morph`
   added to the fan-out list.
5. **Change 5 / 6 / 7 — header enum comments** (`copyOps.h`, `clearOps.h`):
   `CC_COPY_SCENE_MORPH` and `CC_CLEAR_SCENE_RESET_MORPH` now describe
   fan-out; `CC_COPY_MORPH` first line reads `"inst -> morph"`.
6. **Change 9 / 10 — EffectsManager comments** (`.h` + `.c`): both now say the
   two Scene morph operations fan out and call
   `effects_resetMorphToNormalSingle()` per member.
7. **Change 11 / 12 / 13 — indicator and preset comments**
   (`copyClearSession.c`, `presetManager.h`).
8. **Changes 14–20 — `COPYCLEAR_UTILITIES.md`**: §6.1 menus, §6.5 (this table
   had no `copy morph` row to relabel, so the `copy scene -> morph` row was
   added), §7.2 clear table (`no` → `yes`), §10 fan-out column, §13 executor
   rows (871, 874, 875), §20 history entry.
9. **Change 21 — `S077_RETEST_CHECKLIST.md`** items 3.10, 3.14, 3.15 reset to
   blank result with the corrected expected behaviour.

**Additional comment-only edits beyond the schedule** (label/description
consistency; no behaviour change). The schedule scoped the preset comment to
`presetManager.h` only, and the round-trip requirement is that the twin
descriptions in `.c` and `.h` stay adjacent and agree, so the `.c` twin was
updated too:

- `presetManager.c` — comment above `preset_copySlotNormalToMorph()`:
  `"morph"/"scene morph"` → `"inst -> morph"/"scene -> morph"`.
- `copyOps.c` — `ccCopy_selectionCount()` header and the two inline
  `+morph` comments, plus the `ccCopy_runJob()` track-dispatch comment
  (`"morph"` → `"inst -> morph"`).
- `copyOps.h` — the `ccCopy_runJob()` section comment (`copy morph` →
  `copy inst -> morph`, `copy scene morph` → `copy scene -> morph`).

**One pre-existing spec error found and fixed while auditing §13** (flagged for
review — not part of the S077 P4 schedule): the `clear reset fx morph` row
previously read `effects_resetMorphToNormalSingle(scene)` per member, but
`ccClear_runResetFxMorph()` actually calls the fan-out variant
`effects_resetMorphToNormal(scene)`. The row now reads the correct call.

---

## Change 1 — Label: track copy "morph" → "inst -> morph"

**File:** `Core/Menu/CopyClear/copyOps.c`
**Location:** line 45, inside `ccCopy_trackLabels[]`
**Action:** MODIFY string literal

### Before

```c
static const char *const ccCopy_trackLabels[] = {
    "track", "instrument", "morph"
};
```

### After

```c
static const char *const ccCopy_trackLabels[] = {
    "track", "instrument", "inst -> morph"
};
```

### Comment block (replace lines 36–46)

```c
/*
 * What:       track copy labels: "track", "instrument", and "inst -> morph".
 *             The label names the source being copied onto the Morph
 *             endpoints: the instrument's Normal image.
 * Why:        the array is indexed by cc_copy_track_sel_t; the
 *             CC_COPY_MORPH = 2 entry must be present at index 2. The
 *             "[inst -> morph]" display label shows the user that Normal
 *             instrument endpoints are being copied onto the Morph endpoints.
 * Inputs:     ccCopy_label() maps CC_MENU_COPY_TRACK to this array.
 * Outputs:    const label string for the copy/clear session menu renderer.
 * Affiliates: ccCopy_selectionCount(), ccCopy_runMorphTrack().
 */
static const char *const ccCopy_trackLabels[] = {
    "track", "instrument", "inst -> morph"
};
```

---

## Change 2 — Label: Scene copy "scene morph" → "scene -> morph"

**File:** `Core/Menu/CopyClear/copyOps.c`
**Location:** line 54, inside `ccCopy_sceneLabels[]`
**Action:** MODIFY string literal

### Before

```c
static const char *const ccCopy_sceneLabels[] = {
    "scene", "settings", "kit", "effect", "pattern", "scene morph"
};
```

### After

```c
static const char *const ccCopy_sceneLabels[] = {
    "scene", "settings", "kit", "effect", "pattern", "scene -> morph"
};
```

### Comment block (replace lines 47–55)

```c
/*
 * What:       Scene copy labels: "scene -> morph" at index 5 copies all
 *             Normal endpoints of the source Scene onto the destination
 *             Scene's Morph endpoints, fanning out through the edit mask.
 * Why:        indexed by cc_copy_scene_sel_t; CC_COPY_SCENE_MORPH = 5 must be
 *             present at index 5. The "[scene -> morph]" display label shows
 *             the user that the whole Scene's Normal endpoints are being
 *             copied onto the Morph endpoints.
 * Affiliates: ccCopy_selectionCount(), ccCopy_runSceneMorph().
 */
static const char *const ccCopy_sceneLabels[] = {
    "scene", "settings", "kit", "effect", "pattern", "scene -> morph"
};
```

---

## Change 3 — Rewrite `ccCopy_runSceneMorph()` with edit-mask fan-out

**File:** `Core/Menu/CopyClear/copyOps.c`
**Location:** lines 1018–1113 (comment + function body)
**Action:** MODIFY — replace entire comment block and function body

### Before (lines 1018–1113)

The current function operates on a single destination Scene (`job->scene`)
with no fan-out.  It copies the source Scene's Normal endpoints onto one
destination's Morph endpoints.

### After — replacement comment block and function

```c
/*
 * `copy scene -> morph` (S076 P3, S077 P4 fan-out correction): copy all
 * Normal endpoints of the source Scene onto the destination Scene's Morph
 * endpoints for every matching-type component, fanning out through the edit
 * mask (parallels `copy kit`).
 *
 * What:       for each Scene in bank_sceneFanoutMask(job->scene):
 *             (a) for each of the six slots:
 *                 preset_copySlotNormalToMorph() copies the source Scene's
 *                 Normal morphable bytes onto the member's Morph bytes
 *                 (silent skip per slot on a type mismatch — the member's
 *                 Morph stays unchanged for that slot);
 *             (b) preset_setVoiceFxSendMorph() sets the member's FX-send
 *                 Morph endpoint to the source FX-send Normal endpoint;
 *             (c) the generated slot-6/track-7 decay Morph takes the source
 *                 Normal decay;
 *             (d) when the source and member Effect types match, each
 *                 morphable Effect Morph cell takes the source Normal cell,
 *                 otherwise the member's Effect is silently skipped;
 *             (e) the member's Instrument, Scene and Effect HCNAMES rows
 *                 lose their refreshed flag.
 *             When the active Scene is in the mask the Morph worker is
 *             requeued and the Effect runtime is rebuilt once after all
 *             writes.
 * Why:        morph endpoints are scene-child data (instrument images, FX
 *             send, Effect parameters). Scene-child edits fan out through
 *             the edit mask: `copy kit`, `copy instrument` and `copy effect`
 *             all fan out. The original single-destination version incorrectly
 *             paralleled `copy scene` (which exchanges the mask, not a child).
 * Inputs:     job->scene (destination, origin of the fan-out mask); source
 *             from copyClear_source().
 * Outputs:    CC_RUN_WAIT while the active Scene's apply workers drain, then
 *             CC_RUN_DONE. Side effects: Instrument Morph, FX-send Morph, Kit
 *             Morph-decay and Effect AutoSave marks; runtime rebuild.
 * Accessors:  copyClear_source(), bank_sceneFanoutMask(),
 *             preset_copySlotNormalToMorph(), preset_setVoiceFxSendMorph(),
 *             scene_getVoiceFxSendAmount(),
 *             scene_setSlot6Track7MorphAmpEnvelopeDecay(),
 *             scene_getSlot6Track7AmpEnvelopeDecay(), scene_effectConst(),
 *             effects_paramMorphable(), scene_effectRecordForWholeCommit(),
 *             scene_finishEffectWholeCommit(), effects_activateScene(),
 *             preset_rebuildMorph().
 * Affiliates: ccCopy_runKit() (fan-out copy model),
 *             ccCopy_runMorphTrack() (per-slot fan-out morph copy),
 *             ccClear_runResetSceneMorph() (analogous reset).
 */
static uint8_t ccCopy_runSceneMorph(const cc_job_t *job)
{
    const cc_source_t *src = copyClear_source();
    uint16_t mask;
    uint8_t active = scene_getActiveIndex();
    const effect_record_t *src_fx;
    uint8_t m;

    if (!src)
        return CC_RUN_DROP;
    mask = bank_sceneFanoutMask(job->scene);
    if ((mask & ccCopy_bit(active)) != 0u && !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    src_fx = scene_effectConst(src->scene);
    for (m = 0u; m < SCENE_COUNT; m++) {
        uint8_t slot;

        if ((mask & ccCopy_bit(m)) == 0u)
            continue;
        for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
            (void)preset_copySlotNormalToMorph(src->scene, slot, m, slot);
            (void)preset_setVoiceFxSendMorph(
                m, slot, scene_getVoiceFxSendAmount(src->scene, slot));
            ccSvc_nameContentChanged(
                filesystem_identityRow(FS_ROW_INSTRUMENT, m, slot));
        }
        scene_setSlot6Track7MorphAmpEnvelopeDecay(
            m, scene_getSlot6Track7AmpEnvelopeDecay(src->scene));
        /*
         * Effect: copy the source Normal cells onto this member's Morph
         * cells only when the two Effect types match; a different type
         * shares no descriptor layout, so the Effect is silently skipped.
         */
        {
            const effect_record_t *dst_fx = scene_effectConst(m);

            if (src_fx && dst_fx && src_fx->type == dst_fx->type) {
                effect_record_t *record =
                    scene_effectRecordForWholeCommit(m);

                if (record) {
                    uint8_t changed = 0u;
                    uint8_t i;

                    for (i = 0u; i < EFFECT_PARAM_COUNT; i++) {
                        if (!effects_paramMorphable(src_fx->type, i))
                            continue;
                        if (record->morph[i] != src_fx->normal[i]) {
                            record->morph[i] = src_fx->normal[i];
                            changed = 1u;
                        }
                    }
                    if (changed)
                        scene_finishEffectWholeCommit(m);
                }
            }
        }
        ccSvc_nameContentChanged(
            filesystem_identityRow(FS_ROW_SCENE, m, 0u));
        ccSvc_nameContentChanged(
            filesystem_identityRow(FS_ROW_EFFECT, m, 0u));
    }
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)mask |
            ((uint32_t)(job->scene & 0xFu) << 16u) | (11u << 20u));
    if ((mask & ccCopy_bit(active)) != 0u) {
        preset_rebuildMorph();
        effects_activateScene(active);
        menu_repaintAll();
    }
    return CC_RUN_DONE;
}
```

### Key differences from the old version

1. `mask = bank_sceneFanoutMask(job->scene)` replaces the fixed single
   destination.
2. The apply-worker wait tests the mask, not a single `active` boolean.
3. All per-slot and Effect work moves inside a `for (m = ...)` loop over
   the mask.
4. The Effect type check is per member (each member may have a different
   Effect type).
5. `ccSvc_nameContentChanged()` fires per member's Instrument, Scene and
   Effect rows.
6. The trace record logs the full mask instead of `ccCopy_bit(dst)`.
7. Active-Scene rebuild uses `(mask & ccCopy_bit(active))` instead of a
   simple boolean.
8. `dst` local removed; `m` is the per-member destination.
9. `active` changes from `uint8_t` boolean to `uint8_t` Scene index (used
   in the bit test and passed to `effects_activateScene()`).

---

## Change 4 — Rewrite `ccClear_runResetSceneMorph()` with edit-mask fan-out

**File:** `Core/Menu/CopyClear/clearOps.c`
**Location:** lines 514–569 (comment + function body)
**Action:** MODIFY — replace entire comment block and function body

### Before (lines 514–569)

The current function operates on a single Scene (`job->scene`) with no
fan-out.

### After — replacement comment block and function

```c
/*
 * `clear scene reset morph` (S076 P3, S077 P4 fan-out correction): equalise
 * the whole Scene's morph endpoints to Normal, fanning out through the edit
 * mask (parallels `clear fx`).
 *
 * What:       for each Scene in bank_sceneFanoutMask(job->scene):
 *             (a) for each of the six slots:
 *                 preset_resetSlotMorphToNormal() resets the morphable
 *                 instrument descriptors (each member uses its OWN Normal
 *                 endpoints, not a shared source);
 *             (b) preset_setVoiceFxSendMorph() sets the member's FX-send
 *                 Morph endpoint to that member's FX-send Normal endpoint;
 *             (c) when the slot is slot 6 the generated slot-6/track-7 Morph
 *                 decay takes that member's Normal decay;
 *             (d) effects_resetMorphToNormalSingle() resets the member's
 *                 morphable Effect endpoints;
 *             (e) the member's Instrument, Scene and Effect HCNAMES rows
 *                 lose their refreshed flag.
 *             When the active Scene is in the mask the Morph worker is
 *             requeued and the menu repaints once after all writes.
 * Why:        morph endpoints are scene-child data (instrument images, FX
 *             send, Effect parameters). Scene-child clears fan out through
 *             the edit mask: `clear fx`, `clear send` and `clear reset morph`
 *             (track) all fan out. The original single-Scene version
 *             incorrectly paralleled `clear scene` (which resets the mask,
 *             not a child). Each member equalises its OWN Normal to its OWN
 *             Morph, since reset makes endpoints equal within each Scene, not
 *             across Scenes.
 * Inputs:     job->scene (destination, origin of the fan-out mask).
 * Outputs:    CC_RUN_WAIT while the active Scene's apply workers drain, then
 *             CC_RUN_DONE. Side effects: Instrument Morph, FX-send Morph,
 *             Kit Morph-decay and Effect AutoSave marks; runtime rebuild.
 * Accessors:  bank_sceneFanoutMask(), preset_resetSlotMorphToNormal(),
 *             preset_setVoiceFxSendMorph(), scene_getVoiceFxSendAmount(),
 *             scene_setSlot6Track7MorphAmpEnvelopeDecay(),
 *             scene_getSlot6Track7AmpEnvelopeDecay(),
 *             effects_resetMorphToNormalSingle(), preset_rebuildMorph().
 * Affiliates: ccClear_runFx() (fan-out clear model),
 *             ccClear_runResetMorphTrack() (per-slot fan-out morph reset),
 *             ccCopy_runSceneMorph() (analogous copy).
 */
static uint8_t ccClear_runResetSceneMorph(const cc_job_t *job)
{
    uint16_t mask = bank_sceneFanoutMask(job->scene);
    uint8_t active = scene_getActiveIndex();
    uint8_t m;

    if ((mask & ccClear_bit(active)) != 0u && !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    for (m = 0u; m < SCENE_COUNT; m++) {
        uint8_t slot;

        if ((mask & ccClear_bit(m)) == 0u)
            continue;
        for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
            (void)preset_resetSlotMorphToNormal(m, slot);
            (void)preset_setVoiceFxSendMorph(
                m, slot, scene_getVoiceFxSendAmount(m, slot));
            ccSvc_nameContentChanged(
                filesystem_identityRow(FS_ROW_INSTRUMENT, m, slot));
        }
        scene_setSlot6Track7MorphAmpEnvelopeDecay(
            m, scene_getSlot6Track7AmpEnvelopeDecay(m));
        (void)effects_resetMorphToNormalSingle(m);
        ccSvc_nameContentChanged(
            filesystem_identityRow(FS_ROW_SCENE, m, 0u));
        ccSvc_nameContentChanged(
            filesystem_identityRow(FS_ROW_EFFECT, m, 0u));
    }
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)mask |
            ((uint32_t)(job->scene & 0xFu) << 16u) | (8u << 20u));
    if ((mask & ccClear_bit(active)) != 0u) {
        preset_rebuildMorph();
        menu_repaintAll();
    }
    return CC_RUN_DONE;
}
```

### Key differences from the old version

1. `mask = bank_sceneFanoutMask(job->scene)` replaces the fixed single
   Scene.
2. The apply-worker wait tests the mask against the active Scene.
3. All per-slot work and the Effect reset move inside a `for (m = ...)`
   loop.
4. Each member resets its OWN Normal→Morph (the `m` index is passed
   everywhere the old `scene` was).
5. `effects_resetMorphToNormalSingle(m)` is called per member — NOT
   `effects_resetMorphToNormal()`, because the outer loop already
   handles the fan-out; using the fan-out variant would double-fan-out
   through the Effect's own mask and produce duplicated work.
6. The `scene` local is gone; `m` is the per-member Scene index.
7. `active` changes from a boolean to the Scene index (used in the
   bit test).

---

## Change 5 — Header comment: CC_COPY_SCENE_MORPH (fans out)

**File:** `Core/Menu/CopyClear/copyOps.h`
**Location:** lines 51–64 (comment block above `cc_copy_scene_sel_t`)
**Action:** MODIFY comment text

### Before (lines 51–64)

```c
/*
 * What:       CC_COPY_SCENE_MORPH (5) is the "scene morph" Scene-level copy. It
 *             copies all Normal endpoints of the source Scene onto the
 *             destination Scene's Morph endpoints for every matching-type
 *             component (instruments, FX send x6, Kit slot-6 decay, Effect).
 *             It silently skips instruments and/or the Effect whose types
 *             differ, does NOT fan out (parallels `copy scene`), does NOT
 *             exchange or reset the edit mask and does NOT touch morph amounts.
 * Why:        whole-Scene morph copy for the PERF copy menu.
 * Inputs:     ccCopy_requestPaste(), ccCopy_runJob() dispatch.
 * Outputs:    none (enum constant).
 * Accessors:  ccCopy_selectionCount(), ccCopy_label(), ccCopy_runJob().
 * Affiliates: CC_COPY_MORPH above, CC_CLEAR_SCENE_RESET_MORPH in clearOps.h.
 */
```

### After

```c
/*
 * What:       CC_COPY_SCENE_MORPH (5) is the "scene -> morph" Scene-level
 *             copy. It copies all Normal endpoints of the source Scene onto
 *             the destination Scene's Morph endpoints for every matching-type
 *             component (instruments, FX send x6, Kit slot-6 decay, Effect),
 *             fanning out through the destination's edit mask (parallels
 *             `copy kit`). It silently skips instruments and/or the Effect
 *             whose types differ per member, does NOT exchange or reset the
 *             edit mask and does NOT touch morph amounts.
 * Why:        whole-Scene morph copy for the PERF copy menu. Morph endpoints
 *             are scene-child data and fan out like every other scene-child
 *             edit.
 * Inputs:     ccCopy_requestPaste(), ccCopy_runJob() dispatch.
 * Outputs:    none (enum constant).
 * Accessors:  ccCopy_selectionCount(), ccCopy_label(), ccCopy_runJob().
 * Affiliates: CC_COPY_MORPH above, CC_CLEAR_SCENE_RESET_MORPH in clearOps.h.
 */
```

---

## Change 6 — Header comment: CC_COPY_MORPH (label correction only)

**File:** `Core/Menu/CopyClear/copyOps.h`
**Location:** lines 32–45 (comment block above `cc_copy_track_sel_t`)
**Action:** MODIFY comment text — first line only

### Before (line 32)

```c
 * What:       CC_COPY_MORPH (2) is the "morph" track-level copy. It copies the
```

### After

```c
 * What:       CC_COPY_MORPH (2) is the "inst -> morph" track-level copy. It copies the
```

No other lines in this comment block change.

---

## Change 7 — Header comment: CC_CLEAR_SCENE_RESET_MORPH (fans out)

**File:** `Core/Menu/CopyClear/clearOps.h`
**Location:** lines 52–68 (comment block above `cc_clear_scene_sel_t`)
**Action:** MODIFY comment text

### Before (lines 52–68)

```c
/* PERF Scene clear selections.
 *
 * What:       CC_CLEAR_SCENE_RESET_MORPH (8) equalises all six instrument
 *             slots' morph endpoints, all correlated Scene morph endpoints
 *             (FX send morph x6, Kit slot-6 decay morph) and every morphable
 *             Effect morph endpoint to their current Normal values; it does
 *             NOT fan out (parallels `clear scene`), does NOT clear the
 *             Bank-present bit and does NOT touch any morph amount.
 *             CC_CLEAR_SCENE_RESET_FX_MORPH (9) equalises only the Effect's
 *             morphable morph endpoints and fans out through the edit mask
 *             (parallels `clear fx`).
 * Why:        whole-Scene and Effect-only morph resets for the PERF clear menu.
 * Inputs:     ccClear_requestClear(), ccClear_runJob() dispatch.
 * Outputs:    none (enum constants).
 * Accessors:  ccClear_selectionCount(), ccClear_label(), ccClear_runJob().
 * Affiliates: CC_CLEAR_RESET_MORPH above, effects_paramMorphable().
 */
```

### After

```c
/* PERF Scene clear selections.
 *
 * What:       CC_CLEAR_SCENE_RESET_MORPH (8) equalises all six instrument
 *             slots' morph endpoints, all correlated Scene morph endpoints
 *             (FX send morph x6, Kit slot-6 decay morph) and every morphable
 *             Effect morph endpoint to their current Normal values, fanning
 *             out through the destination's edit mask (parallels `clear fx`).
 *             It does NOT clear the Bank-present bit and does NOT touch any
 *             morph amount.
 *             CC_CLEAR_SCENE_RESET_FX_MORPH (9) equalises only the Effect's
 *             morphable morph endpoints and fans out through the edit mask
 *             (parallels `clear fx`).
 * Why:        whole-Scene and Effect-only morph resets for the PERF clear menu.
 *             Morph endpoints are scene-child data and fan out like every other
 *             scene-child clear.
 * Inputs:     ccClear_requestClear(), ccClear_runJob() dispatch.
 * Outputs:    none (enum constants).
 * Accessors:  ccClear_selectionCount(), ccClear_label(), ccClear_runJob().
 * Affiliates: CC_CLEAR_RESET_MORPH above, effects_paramMorphable().
 */
```

---

## Change 8 — Header comment: `clearOps.c` file header (line 7)

**File:** `Core/Menu/CopyClear/clearOps.c`
**Location:** line 7, inside the file-level comment block
**Action:** MODIFY — update fan-out description

### Before (line 7)

```c
 * mask and do not fan out; `clear send`, `clear fx`, `clear fx sequence`, the
```

### After

```c
 * mask and do not fan out; `clear send`, `clear fx`, `clear fx sequence`,
 * `clear scene reset morph`, the
```

The continuation on line 8 stays as is (`EFFECTS SEQ clear and the FX-lane
part of a pot clear fan out through the`).

---

## Change 9 — EffectsManager.h comment: morph reset Why line

**File:** `Core/DSP/Effects/EffectsManager.h`
**Location:** lines 370–372, inside the `effects_resetMorphToNormal` comment
**Action:** MODIFY comment text

### Before (lines 370–372)

```c
 * Why:        "clear scene reset fx morph" fans out exactly like "clear fx"
 *             (same-type members receive the change), while "clear scene reset
 *             morph" and "copy scene morph" own their own single-Scene loop.
```

### After

```c
 * Why:        "clear scene reset fx morph" fans out exactly like "clear fx"
 *             (same-type members receive the change). "clear scene reset morph"
 *             and "copy scene -> morph" fan out through the bank edit mask and
 *             call effects_resetMorphToNormalSingle() per member.
```

---

## Change 10 — EffectsManager.c comment: `effects_resetMorphToNormalSingle()` Why line

**File:** `Core/DSP/Effects/EffectsManager.c`
**Location:** lines 1057–1059, inside the function's comment block
**Action:** MODIFY comment text

### Before (lines 1057–1059)

```c
 * What:       the same morphable Normal -> Morph copy as
 *             effects_resetMorphToNormal() but applied to exactly one Scene.
 * Why:        the whole-Scene "reset morph" clear owns its own non-fanned-out
 *             loop and hands the Effect slice to this single-Scene helper.
```

### After

```c
 * What:       the same morphable Normal -> Morph copy as
 *             effects_resetMorphToNormal() but applied to exactly one Scene.
 * Why:        the whole-Scene "reset morph" clear and "scene -> morph" copy
 *             own their own edit-mask fan-out loop and hand each member's
 *             Effect slice to this single-Scene helper.
```

---

## Change 11 — copyClearSession.c: Scene copy indicator comment

**File:** `Core/Menu/CopyClear/copyClearSession.c`
**Location:** lines 831–836, inside the `CC_KIND_SCENE` branch
**Action:** MODIFY comment text

### Before (lines 831–836)

```c
            /*
             * What:       Scene copy suffix by selection: "" scene, "c"
             *             settings, "K" kit, "f" effect, "P" pattern, "m"
             *             scene morph (S076 P3).
             * Why:        the new CC_COPY_SCENE_MORPH = 5 entry needs its own
             *             indicator letter.
             */
```

### After

```c
            /*
             * What:       Scene copy suffix by selection: "" scene, "c"
             *             settings, "K" kit, "f" effect, "P" pattern, "m"
             *             scene -> morph (S076 P3, label S077 P4).
             * Why:        CC_COPY_SCENE_MORPH = 5 needs its own indicator
             *             letter; the suffix "m" stays (one-character
             *             positional abbreviation, not the menu label).
             */
```

---

## Change 12 — copyClearSession.c: track copy indicator comment

**File:** `Core/Menu/CopyClear/copyClearSession.c`
**Location:** lines 808–813, inside the `CC_KIND_TRACK` branch
**Action:** MODIFY comment text

### Before (lines 808–813)

```c
         *             copy selection: "T" for a whole track, "i" for
         *             instrument copy, "m" for the new morph copy (S076 P3).
         * Why:        the menu label already distinguishes the selection, but
         *             the indicator keeps the source readable at a glance and
         *             matches the design's SNN{L}N form.
         * Affiliates: CC_COPY_TRACK, CC_COPY_INSTRUMENT, CC_COPY_MORPH.
```

### After

```c
         *             copy selection: "T" for a whole track, "i" for
         *             instrument copy, "m" for inst -> morph (S076 P3,
         *             label S077 P4).
         * Why:        the menu label already distinguishes the selection, but
         *             the indicator keeps the source readable at a glance and
         *             matches the design's SNN{L}N form.
         * Affiliates: CC_COPY_TRACK, CC_COPY_INSTRUMENT, CC_COPY_MORPH.
```

---

## Change 13 — presetManager.h: affiliates comment update

**File:** `Core/Bank/Scene/Preset/presetManager.h`
**Location:** line 643, inside the morph helpers comment block
**Action:** MODIFY comment text — one line only

### Before (line 643)

```c
 *             the "morph"/"scene morph" copy operations share this
```

### After

```c
 *             the "inst -> morph"/"scene -> morph" copy operations share this
```

---

## Change 14 — COPYCLEAR_UTILITIES.md: §6.1 copy menus table

**File:** `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md`
**Location:** lines 250–251, inside the copy menus table
**Action:** MODIFY two table rows

### Before (lines 250–251)

```
| track | `CC_MENU_COPY_TRACK` | `track`, `instrument`, `morph` |
| Scene | `CC_MENU_COPY_SCENE` | `scene`, `settings`, `kit`, `effect`, `pattern`, `morph` |
```

### After

```
| track | `CC_MENU_COPY_TRACK` | `track`, `instrument`, `inst -> morph` |
| Scene | `CC_MENU_COPY_SCENE` | `scene`, `settings`, `kit`, `effect`, `pattern`, `scene -> morph` |
```

---

## Change 15 — COPYCLEAR_UTILITIES.md: §6.5 Scene copy table

**File:** `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md`
**Location:** after line 319 (the `copy pattern` row), insert a new row
**Action:** ADD one table row

### Insert after line 319

```
| `copy scene -> morph` | all 6 slots' Normal → Morph, FX send × 6 Normal → Morph, slot-6 decay Normal → Morph, Effect Normal → Morph (same type only) | yes | — |
```

---

## Change 16 — COPYCLEAR_UTILITIES.md: §7.2 clear effect table

**File:** `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md`
**Location:** line 398, the `reset morph (PERF Scene)` row
**Action:** MODIFY table row

### Before (line 398)

```
| `reset morph` (PERF Scene) | all 6 slots' Morphable Morph := Normal, plus all correlated Scene morph endpoints and every morphable Effect morph endpoint; does NOT clear morph amounts | no |
```

### After

```
| `reset morph` (PERF Scene) | all 6 slots' Morphable Morph := Normal, plus all correlated Scene morph endpoints and every morphable Effect morph endpoint; does NOT clear morph amounts | yes |
```

---

## Change 17 — COPYCLEAR_UTILITIES.md: §10 fan-out table

**File:** `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md`
**Location:** line 518, the fan-out column
**Action:** MODIFY table row

### Before (line 518)

```
| `copy instrument`, `copy kit`, `copy effect`, FX step pastes, `clear fx`, `clear fx sequence`, EFFECTS SEQ clear, the FX-lane part of an EFFECTS/PERF pot clear, `clear send` | every Pattern paste and clear (step, range, bar, track, `copy pattern`, `clear pattern`, `clear automation`, `clear notes`, the Pattern part of any pot clear); `copy scene` and `copy scene settings` (they **set** the mask); `clear scene` and `clear scene settings` (they **reset** it) |
```

### After

```
| `copy instrument`, `copy kit`, `copy effect`, `copy scene -> morph`, FX step pastes, `clear fx`, `clear fx sequence`, `clear reset morph` (track and Scene), `clear reset fx morph`, EFFECTS SEQ clear, the FX-lane part of an EFFECTS/PERF pot clear, `clear send` | every Pattern paste and clear (step, range, bar, track, `copy pattern`, `clear pattern`, `clear automation`, `clear notes`, the Pattern part of any pot clear); `copy scene` and `copy scene settings` (they **set** the mask); `clear scene` and `clear scene settings` (they **reset** it) |
```

(Note: `clear reset morph` track was already fanning out but was not listed
in this table; both track and Scene are now listed together.)

---

## Change 18 — COPYCLEAR_UTILITIES.md: §13 executor table

**File:** `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md`
**Location:** lines 871, 875
**Action:** MODIFY two table rows

### Before (line 871)

```
| `clear reset morph` (Scene) | `ccClear_runResetSceneMorph()` | loops 6 slots `preset_resetSlotMorphToNormal()` + `effects_resetMorphToNormal()` for the Scene's Effect; does NOT fan out | repaint |
```

### After

```
| `clear reset morph` (Scene) | `ccClear_runResetSceneMorph()` | loops 6 slots `preset_resetSlotMorphToNormal()` + `effects_resetMorphToNormalSingle()` per member (fans out through `bank_sceneFanoutMask()`): each member's own Morph := own Normal, FX send morph, slot-6 decay morph, Effect morph | repaint |
```

### Before (line 875)

```
| `copy morph` (Scene) | `ccCopy_runSceneMorph()` | loops 6 slots `preset_copySlotNormalToMorph()` for the destination Scene; does NOT fan out | repaint |
```

### After

```
| `copy scene -> morph` | `ccCopy_runSceneMorph()` | `preset_copySlotNormalToMorph(src, slot, m, slot)` per member (fans out through `bank_sceneFanoutMask()`): source Normal → member Morph, FX send, slot-6 decay, Effect (same type only) | repaint |
```

---

## Change 19 — COPYCLEAR_UTILITIES.md: §13 executor table track morph label

**File:** `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md`
**Location:** line 874
**Action:** MODIFY table row — label only

### Before (line 874)

```
| `copy morph` (track) | `ccCopy_runMorphTrack()` | `preset_copySlotNormalToMorph(scene, slot)` per member (fans out) | repaint |
```

### After

```
| `copy inst -> morph` | `ccCopy_runMorphTrack()` | `preset_copySlotNormalToMorph(scene, slot)` per member (fans out) | repaint |
```

---

## Change 20 — COPYCLEAR_UTILITIES.md: §20 History

**File:** `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md`
**Location:** after line 1186 (the S076 P4 history entry), insert a new entry
**Action:** ADD history line

### Insert after line 1186

```
- **S077 P4 (2026-10-08):** scene morph fan-out correction. `copy scene ->
  morph` (was `scene morph`) and `clear scene reset morph` now fan out through
  the edit mask (parallels `copy kit` / `clear fx`). Track morph copy label
  renamed `inst -> morph`. Comments and §10 fan-out table updated.
```

---

## Change 21 — S077_RETEST_CHECKLIST.md: items 3.10, 3.14, 3.15

**File:** `S077_RETEST_CHECKLIST.md`
**Location:** lines 69, 73–74
**Action:** MODIFY three checklist rows

### Before (line 69)

```
| 3.10 | Verify Scene-level "reset morph" does NOT fan out (only the pressed Scene is affected, not edit-mask members). | I | PASS |
```

### After

```
| 3.10 | Verify Scene-level "reset morph" DOES fan out through the edit mask: set up a 2-Scene edit mask, clear "reset morph" on a Scene, verify all mask members' Morph endpoints are equalised to their own Normal. | I | |
```

### Before (lines 73–74)

```
| 3.14 | Select "morph" (Scene copy). Verify all 6 slots' Normal → Morph, applied to the destination Scene. | I | PASS |
| 3.15 | Verify Scene-level "morph" copy does NOT fan out. | I | PASS |
```

### After

```
| 3.14 | Select "[scene -> morph]" (Scene copy). Verify all 6 slots' Normal → Morph, applied to the destination Scene AND every edit-mask member. | I | |
| 3.15 | Verify Scene-level "[scene -> morph]" copy DOES fan out through the edit mask. | I | |
```

---

## Summary of all changed files

| # | File | Lines | Action |
|---|------|-------|--------|
| 1 | `Core/Menu/CopyClear/copyOps.c` | 36–55 | MODIFY label arrays + comments |
| 3 | `Core/Menu/CopyClear/copyOps.c` | 1018–1113 | MODIFY `ccCopy_runSceneMorph()` (rewrite) |
| 4 | `Core/Menu/CopyClear/clearOps.c` | 7 | MODIFY file-level comment |
| 4 | `Core/Menu/CopyClear/clearOps.c` | 514–569 | MODIFY `ccClear_runResetSceneMorph()` (rewrite) |
| 5 | `Core/Menu/CopyClear/copyOps.h` | 32, 51–64 | MODIFY enum comment blocks |
| 7 | `Core/Menu/CopyClear/clearOps.h` | 52–68 | MODIFY enum comment block |
| 8 | `Core/Menu/CopyClear/copyClearSession.c` | 808–813, 831–836 | MODIFY indicator comments |
| 9 | `Core/DSP/Effects/EffectsManager.h` | 370–372 | MODIFY comment |
| 10 | `Core/DSP/Effects/EffectsManager.c` | 1057–1059 | MODIFY comment |
| 13 | `Core/Bank/Scene/Preset/presetManager.h` | 643 | MODIFY comment |
| 14–20 | `knowledge_files/.../COPYCLEAR_UTILITIES.md` | 250–251, 319, 398, 518, 871, 874–875, 1186 | MODIFY spec text |
| 21 | `S077_RETEST_CHECKLIST.md` | 69, 73–74 | MODIFY checklist |

**Total: 10 source files (4 .c, 3 .h, 1 .md spec, 1 .md checklist, 1 .md plan); 2 function rewrites, 2 string changes, 10 comment updates, 8 spec text edits.**

No new RAM. No new functions. No new enums. No ISR/DSP path. No file-format
change.

---

## Post-Implementation Assessment

**Date:** 2026-10-08
**Reviewer:** Claude (automated code verification)

### Verification method

Every file named in the 21-change schedule was read and compared against the
schedule's before/after blocks plus the additional comment-only edits listed
in the implementation log.

### Results by file

| File | Changes | Status |
|------|---------|--------|
| `copyOps.c` — label arrays (changes 1–2) | `"inst -> morph"` at index 2, `"scene -> morph"` at index 5; comment blocks match schedule | **OK** |
| `copyOps.c` — `ccCopy_runSceneMorph()` (change 3) | Full rewrite confirmed: `bank_sceneFanoutMask()` loop, per-member Effect type check, `src_fx` from source, trace logs mask, active-Scene rebuild gated on mask bit | **OK** |
| `copyOps.c` — `selectionCount` and `runJob` dispatch comments | `+inst -> morph`, `+scene -> morph` inline; dispatch comment updated | **OK** |
| `clearOps.c` — file header (change 8) | `clear scene reset morph` added to fan-out list | **OK** |
| `clearOps.c` — `ccClear_runResetSceneMorph()` (change 4) | Full rewrite confirmed: `bank_sceneFanoutMask()` loop, each member resets its OWN Normal→Morph, `effects_resetMorphToNormalSingle(m)` per member (not the fan-out variant), trace logs mask | **OK** |
| `copyOps.h` — `CC_COPY_MORPH` comment (change 6) | First line reads `"inst -> morph"` | **OK** |
| `copyOps.h` — `CC_COPY_SCENE_MORPH` comment (change 5) | Now says "fanning out through the destination's edit mask (parallels `copy kit`)" with per-member type-mismatch language | **OK** |
| `clearOps.h` — `CC_CLEAR_SCENE_RESET_MORPH` comment (change 7) | Now says "fanning out through the destination's edit mask (parallels `clear fx`)" with scene-child rationale | **OK** |
| `copyClearSession.c` — track indicator (change 12) | `"m" for inst -> morph (S076 P3, label S077 P4)` | **OK** |
| `copyClearSession.c` — Scene indicator (change 11) | `"m" scene -> morph (S076 P3, label S077 P4)` with suffix clarification | **OK** |
| `EffectsManager.h` — morph reset Why (change 9) | Both Scene morph operations now described as fanning out through the bank edit mask, calling `effects_resetMorphToNormalSingle()` per member | **OK** |
| `EffectsManager.c` — single helper Why (change 10) | Correctly says both "reset morph" clear and "scene -> morph" copy own their fan-out loop | **OK** |
| `presetManager.h` — affiliates (change 13) | `"inst -> morph"/"scene -> morph"` | **OK** |
| `COPYCLEAR_UTILITIES.md` — §6.1 (change 14) | Track row: `inst -> morph`; Scene row: `scene -> morph` | **OK** |
| `COPYCLEAR_UTILITIES.md` — §6.5 (change 15) | New `copy scene -> morph` row with `yes` fan-out | **OK** |
| `COPYCLEAR_UTILITIES.md` — §7.2 (change 16) | `reset morph (PERF Scene)` fan-out column: `yes` | **OK** |
| `COPYCLEAR_UTILITIES.md` — §10 (change 17) | Fan-out column includes `copy scene -> morph`, `clear reset morph` (track and Scene), `clear reset fx morph` | **OK** |
| `COPYCLEAR_UTILITIES.md` — §13 (changes 18–19) | Scene morph rows updated with fan-out description and correct function calls; track label `copy inst -> morph` | **OK** |
| `COPYCLEAR_UTILITIES.md` — §13 bonus fix | `clear reset fx morph` row corrected from `effects_resetMorphToNormalSingle(scene)` to `effects_resetMorphToNormal(scene)` (the fan-out variant) — matches actual code in `ccClear_runResetFxMorph()` | **OK** |
| `COPYCLEAR_UTILITIES.md` — §20 (change 20) | S077 P4 history entry present, dated 2026-10-08 | **OK** |
| `S077_RETEST_CHECKLIST.md` (change 21) | Items 3.10, 3.14, 3.15 rewritten to expect fan-out; result columns cleared for re-test | **OK** |

### Correctness notes

1. **No double fan-out risk.** `ccClear_runResetSceneMorph()` correctly calls
   `effects_resetMorphToNormalSingle(m)` per member, not `effects_resetMorphToNormal()`
   which has its own internal Effect fan-out. The copy side (`ccCopy_runSceneMorph()`)
   handles Effects inline with a per-member type check — also no double fan-out.

2. **Reset uses each member's OWN Normal.** The clear rewrite passes `m` (the
   member index) to every accessor that reads Normal endpoints
   (`scene_getVoiceFxSendAmount(m, slot)`, `scene_getSlot6Track7AmpEnvelopeDecay(m)`).
   The copy rewrite passes `src->scene` for reads (the shared source). This is
   the correct asymmetry: copy applies one source to many destinations; reset
   equalises each destination within itself.

3. **Effect type check is per member.** In the copy rewrite, `dst_fx` is
   fetched inside the `for (m ...)` loop, so each member's Effect type is
   compared independently against the source. A member with a mismatched Effect
   type still receives the instrument, FX-send, and decay copies — only the
   Effect part is skipped.

4. **Build delta.** text −88 B confirms the fan-out loop is more compact than
   the duplicated single-destination bodies. No data or bss change.

5. **Bonus fix.** The pre-existing §13 error (`clear reset fx morph` row citing
   the wrong function) was caught and corrected. This is a spec-only fix with
   no code change — the actual code was already correct.

### Verdict

All 21 scheduled changes plus the additional comment-only edits and the bonus
spec fix are correctly applied. The build is clean with a net text reduction.
Ready for hardware re-test of checklist items 3.10, 3.14, and 3.15.
