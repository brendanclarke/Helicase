/*
 * copyOps.c — paste rules for Phase 6 copy (spec §4, §9.7, §9.9).
 *
 * Contract in copyOps.h. Pattern pastes are sequenced by the service engine
 * (copyClearService.c); this file supplies their policy (menus, merge,
 * retargeting) and runs the Scene-level pastes: `copy instrument`, `copy
 * kit`, `copy effect`, `copy scene settings`, `copy scene`, `copy pattern`
 * and FX step pastes. Retained Scene data is written only through SceneData,
 * Preset, EffectsManager and BankData commits.
 */

#include "copyOps.h"
#include "copyClearService.h"
#include "PatternData.h"
#include "SceneData.h"
#include "SceneModTargets.h"
#include "BankData.h"
#include "presetManager.h"
#include "EffectsManager.h"
#include "InstrumentManager.h"
#include "filesystem.h"
#include "Autosave.h"
#include "menu.h"
#include "menuEffects.h"
#include <string.h>

/* ---- menus (spec §4.1, §8.1) ------------------------------------------- */

/* Step and bar selections: replace/merge all or automation. */
static const char *const ccCopy_stepLabels[] = {
    "step -> repl", "step -> merge", "auto -> repl", "auto -> merge"
};
static const char *const ccCopy_barLabels[] = {
    "bar -> repl", "bar -> merge", "auto -> repl", "auto -> merge"
};
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
static const char *const ccCopy_fxLabels[] = { "step" };

cc_menu_t ccCopy_menuForSource(const cc_source_t *src)
{
    if (!src)
        return CC_MENU_NONE;
    switch (src->kind) {
    case CC_KIND_STEP:    return CC_MENU_COPY_STEP;
    case CC_KIND_BAR:     return CC_MENU_COPY_BAR;
    case CC_KIND_TRACK:   return CC_MENU_COPY_TRACK;
    case CC_KIND_SCENE:   return CC_MENU_COPY_SCENE;
    case CC_KIND_FX_STEP: return CC_MENU_COPY_FX;
    default:              return CC_MENU_NONE;
    }
}

uint8_t ccCopy_selectionCount(cc_menu_t menu)
{
    /*
     * What:       updated counts: COPY_TRACK 2 -> 3 (+inst -> morph),
     *             COPY_SCENE 5 -> 6 (+scene -> morph). The other menus are
     *             unchanged.
     * Why:        exposes the new selections at the end of each menu; the
     *             encoder clamps to selectionCount - 1.
     * Inputs:     cc_menu_t from ccCopy_menuForSource().
     * Outputs:    total entry count for the menu.
     * Affiliates: ccCopy_label(), ccCopy_runJob().
     */
    switch (menu) {
    case CC_MENU_COPY_STEP:  return 4u;
    case CC_MENU_COPY_BAR:   return 4u;
    case CC_MENU_COPY_TRACK: return 3u;  /* +inst -> morph */
    case CC_MENU_COPY_SCENE: return 6u;  /* +scene -> morph */
    case CC_MENU_COPY_FX:    return 1u;
    default:                 return 0u;
    }
}

const char *ccCopy_label(cc_menu_t menu, uint8_t selection)
{
    if (selection >= ccCopy_selectionCount(menu))
        return "";
    switch (menu) {
    case CC_MENU_COPY_STEP:  return ccCopy_stepLabels[selection];
    case CC_MENU_COPY_BAR:   return ccCopy_barLabels[selection];
    case CC_MENU_COPY_TRACK: return ccCopy_trackLabels[selection];
    case CC_MENU_COPY_SCENE: return ccCopy_sceneLabels[selection];
    case CC_MENU_COPY_FX:    return ccCopy_fxLabels[selection];
    default:                 return "";
    }
}

/* Instrument slot of a track: track 7 behaves as track 6 (spec §1). */
static uint8_t ccCopy_slotOf(uint8_t track)
{
    return (track < INSTRUMENT_SLOT_COUNT) ? track
                                           : (uint8_t)(INSTRUMENT_SLOT_COUNT - 1u);
}

uint8_t ccCopy_requestPaste(const cc_source_t *src, uint8_t selection,
                            uint8_t dst_scene, uint8_t dst_track,
                            uint8_t dst_start)
{
    cc_job_t job;
    uint8_t identical = 0u;
    uint8_t slot;

    if (!src || src->kind == CC_KIND_NONE)
        return 0u;
    /* A paste identical to its source does nothing (spec §4.3). */
    switch (src->kind) {
    case CC_KIND_STEP:
    case CC_KIND_BAR:
        identical = (uint8_t)(dst_scene == src->scene &&
                              dst_track == src->track &&
                              dst_start == src->start &&
                              src->start <= src->end);
        break;
    case CC_KIND_TRACK:
        /*
         * What:       identical-paste test for track-level pastes. Only
         *             `copy track` (same track) and `copy instrument` (same
         *             slot) are true no-ops. `copy morph` is deliberately not
         *             listed: it copies the slot's own Normal image onto its
         *             own Morph image (even for the same Scene/slot the two
         *             images differ in general), so it must run.
         * Why:        the identical test gates the PASTE_NOOP trace and the
         *             early return; morph copy is never a no-op.
         * Inputs:     src Scene/track, dst Scene/track, selection.
         * Outputs:    identical flag for the trace and early return.
         * Affiliates: ccCopy_runMorphTrack().
         */
        identical = (uint8_t)(dst_scene == src->scene &&
            ((selection == CC_COPY_TRACK && dst_track == src->track) ||
             (selection == CC_COPY_INSTRUMENT &&
              ccCopy_slotOf(dst_track) == ccCopy_slotOf(src->track))));
        break;
    case CC_KIND_SCENE:
        identical = (uint8_t)(dst_scene == src->scene);
        break;
    case CC_KIND_FX_STEP:
        identical = (uint8_t)(dst_scene == src->scene &&
                              dst_start == src->start &&
                              src->start <= src->end);
        break;
    default:
        return 0u;
    }
    job.op = (uint8_t)(CC_JOB_PASTE | (selection & CC_JOB_SEL_MASK));
    job.kind = src->kind;
    job.scene = dst_scene;
    job.track = dst_track;
    job.start = dst_start;
    job.end = dst_start;
    if (identical) {
        ccTrace(AUTOSAVE_TRACE_CC_EVT_PASTE_NOOP, ccTrace_job(&job));
        return 0u;
    }
    slot = ccSvc_enqueue(&job);
    if (slot == 0u)
        return 0u;
    /*
     * What:       early trigger bits are written only for Pattern pastes.
     *             `copy track` is a whole-Pattern replace (CC_COPY_TRACK ==
     *             CC_COPY_ALL) and gets them; `copy instrument` and
     *             `copy morph` change slot endpoints only and must not touch
     *             Pattern triggers.
     * Why:        ccSvc_pasteGeometry() normalizes every track-kind paste to
     *             CC_COPY_ALL, so the selection cannot be recovered once the
     *             geometry is built; `ccSvc_pasteTriggersNow()` therefore
     *             re-checks the raw selection (spec §12.6 lists only the
     *             step/bar/track -> repl/merge pastes). Affiliate:
     *             ccSvc_pasteTriggersNow(), CC_COPY_MORPH.
     */
    if (src->kind != CC_KIND_TRACK || selection == CC_COPY_TRACK)
        (void)ccSvc_pasteTriggersNow((uint8_t)(slot - 1u));
    return 1u;
}

/*
 * What:       bar-to-step paste request. Identical to ccCopy_requestPaste()
 *             except the job kind is CC_KIND_BAR_TO_STEP instead of
 *             src->kind (CC_KIND_BAR). This tells the geometry function to
 *             use bar source coordinates (src->start * 16) but step
 *             destination coordinates (job->start as an absolute step).
 * Why:        ccCopy_requestPaste() always sets job.kind = src->kind. For
 *             cross-kind pastes the job kind must differ from the source
 *             kind so the geometry function knows the destination is in step
 *             space, not bar space.
 * Inputs:     src (CC_KIND_BAR source), selection (cc_copy_step_sel_t),
 *             dst_scene, dst_track, dst_start (absolute step 0..127).
 * Outputs:    1 if a job was queued, 0 if dropped (queue full).
 * Accessors:  ccSvc_enqueue(), ccSvc_pasteTriggersNow().
 * Affiliates: ccCopy_requestPaste(), cc_copySeq().
 */
uint8_t ccCopy_requestBarToStep(const cc_source_t *src, uint8_t selection,
                                uint8_t dst_scene, uint8_t dst_track,
                                uint8_t dst_start)
{
    cc_job_t job;
    uint8_t slot;

    if (!src || src->kind != CC_KIND_BAR)
        return 0u;
    /*
     * Identical-paste check: a bar-to-step paste is never identical to its
     * source because the destination is in step space and the source is in
     * bar space. Even if the step happens to fall inside the source bar(s),
     * the operation is meaningful (it copies the bar content to a potentially
     * different alignment).
     */
    job.op = (uint8_t)(CC_JOB_PASTE | (selection & CC_JOB_SEL_MASK));
    job.kind = CC_KIND_BAR_TO_STEP;
    job.scene = dst_scene;
    job.track = dst_track;
    job.start = dst_start;
    job.end = dst_start;
    slot = ccSvc_enqueue(&job);
    if (slot == 0u)
        return 0u;
    (void)ccSvc_pasteTriggersNow((uint8_t)(slot - 1u));
    return 1u;
}

/* ---- retargeting (spec §9.7) -------------------------------------------- */

static instrument_type_t ccCopy_slotType(uint8_t scene, uint8_t slot)
{
    const scene_t *s = scene_getConst(scene);

    if (!s || slot >= INSTRUMENT_SLOT_COUNT)
        return INSTRUMENT_TYPE_UNKNOWN;
    return s->kit.instruments[slot].type;
}

static effect_type_id_t ccCopy_effectType(uint8_t scene)
{
    const effect_record_t *r = scene_effectConst(scene);

    return r ? r->type : (effect_type_id_t)0;
}

uint8_t ccCopy_sceneTypesDiffer(uint8_t scene_a, uint8_t scene_b)
{
    uint8_t slot;

    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        if (ccCopy_slotType(scene_a, slot) != ccCopy_slotType(scene_b, slot))
            return 1u;
    return (uint8_t)(ccCopy_effectType(scene_a) != ccCopy_effectType(scene_b));
}

uint8_t ccCopy_retargetNeeded(const cc_retarget_ctx_t *ctx)
{
    if (!ctx)
        return 0u;
    if (ctx->src_track != ctx->dst_track)
        return 1u;
    if (ctx->src_scene == ctx->dst_scene)
        return 0u;
    return ccCopy_sceneTypesDiffer(ctx->src_scene, ctx->dst_scene);
}

/* Nonzero when a descriptor key ends in "_choke" (a track-7 alternate). */
static uint8_t ccCopy_keyIsChoke(const char *key)
{
    size_t len;

    if (!key)
        return 0u;
    len = strlen(key);
    return (uint8_t)(len > 6u && strcmp(&key[len - 6u], "_choke") == 0);
}

/* Find the (page, cell) position of a descriptor on the VOICE pages. */
static uint8_t ccCopy_pagePosition(instrument_type_t type, uint8_t local,
                                   uint8_t *page_out, uint8_t *cell_out)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    uint8_t page;
    uint8_t cell;

    if (!entry || !entry->menu_pages)
        return 0u;
    for (page = 0u; page < entry->menu_page_count; page++) {
        for (cell = 0u; cell < INSTRUMENT_MENU_PAGE_CELLS; cell++) {
            if (entry->menu_pages[page].descriptor_index[cell] == local) {
                *page_out = page;
                *cell_out = cell;
                return 1u;
            }
        }
    }
    return 0u;
}

/*
 * Match one Instrument parameter across types: same type -> same index;
 * else same storage key; else same VOICE page position. The match must be
 * automatable with the same dtype.
 */
static uint8_t ccCopy_mapInstrumentLocal(instrument_type_t src_type,
                                         uint8_t local,
                                         instrument_type_t dst_type,
                                         uint8_t *local_out)
{
    const ParamDescriptor *sd = instrumentManager_descriptor(src_type, local);
    const ParamDescriptor *dd = 0;
    uint8_t index = INSTRUMENT_MENU_EMPTY;

    if (!sd)
        return 0u;
    if (src_type == dst_type) {
        dd = sd;
        index = local;
    } else {
        dd = instrumentManager_descriptorIndexByKey(dst_type, sd->file_key,
                                                    &index);
        if (!dd) {
            uint8_t page;
            uint8_t cell;
            const instrument_registry_entry_t *de =
                instrumentManager_registryEntry(dst_type);

            if (de && de->menu_pages &&
                ccCopy_pagePosition(src_type, local, &page, &cell) &&
                page < de->menu_page_count) {
                index = de->menu_pages[page].descriptor_index[cell];
                dd = instrumentManager_descriptor(dst_type, index);
            }
        }
    }
    if (!dd || (dd->flags & INSTRUMENT_PARAM_FLAG_AUTOMATABLE) == 0u ||
        dd->dtype != sd->dtype)
        return 0u;
    *local_out = index;
    return 1u;
}

/*
 * The destination track 7's alternate decay: the `_choke` sibling of the
 * base amp decay on a Choke type, `7dc` on any other type with a base decay.
 */
static uint8_t ccCopy_track7Alternate(uint8_t dst_scene, uint16_t *out)
{
    const uint8_t slot = (uint8_t)(INSTRUMENT_SLOT_COUNT - 1u);
    instrument_type_t type = ccCopy_slotType(dst_scene, slot);
    uint8_t base;
    uint8_t choke;

    if (!instrumentManager_descriptorIndexByKey(type, "amp_envelope_decay",
                                                &base))
        return 0u;
    if ((instrumentManager_typeFlags(type) & INSTRUMENT_FLAG_CHOKE) != 0u) {
        const ParamDescriptor *d;

        if (!instrumentManager_chokeDescriptorIndexForBase(type, base, &choke))
            return 0u;
        d = instrumentManager_descriptor(type, choke);
        if (!d || (d->flags & INSTRUMENT_PARAM_FLAG_AUTOMATABLE) == 0u)
            return 0u;
        *out = instrumentParam_make(slot, choke);
        return 1u;
    }
    *out = sceneModTarget_slot6DecayId();
    return (uint8_t)(*out != INSTRUMENT_PARAM_INVALID);
}

/* Same per-voice Scene target kind on another slot. */
static uint8_t ccCopy_sceneTargetForSlot(scene_mod_target_kind_t kind,
                                         uint8_t slot, uint16_t *out)
{
    uint8_t i;

    for (i = 0u; i < sceneModTarget_count(); i++) {
        uint16_t id = sceneModTarget_idFromIndex(i);
        const scene_mod_target_descriptor_t *d = sceneModTarget_descriptor(id);

        if (d && d->kind == kind && d->voice_slot == slot) {
            *out = id;
            return 1u;
        }
    }
    return 0u;
}

uint8_t ccCopy_retarget(const cc_retarget_ctx_t *ctx, uint16_t src_target,
                        uint16_t *dst_target)
{
    uint8_t src_slot;
    uint8_t dst_slot;

    if (!ctx || !dst_target)
        return 0u;
    *dst_target = src_target;
    if (src_target == PAT_AUTOMATION_TARGET_OFF)
        return 1u;
    src_slot = ccCopy_slotOf(ctx->src_track);
    dst_slot = ccCopy_slotOf(ctx->dst_track);

    if (instrumentParam_isVoiceParameter(src_target)) {
        uint8_t s = instrumentParam_slot(src_target);
        uint8_t l = instrumentParam_local(src_target);
        instrument_type_t st = ccCopy_slotType(ctx->src_scene, s);
        const ParamDescriptor *sd = instrumentManager_descriptor(st, l);
        uint8_t d;
        uint8_t mapped;

        /* Track-7 alternates travel only track 7 -> track 7. */
        if (ctx->src_track == NUM_TRACKS - 1u && sd &&
            ccCopy_keyIsChoke(sd->file_key)) {
            if (ctx->dst_track != NUM_TRACKS - 1u)
                return 0u;
            return ccCopy_track7Alternate(ctx->dst_scene, dst_target);
        }
        d = (s == src_slot) ? dst_slot : s;
        if (!ccCopy_mapInstrumentLocal(st, l,
                                       ccCopy_slotType(ctx->dst_scene, d),
                                       &mapped))
            return 0u;
        *dst_target = instrumentParam_make(d, mapped);
        return 1u;
    }
    if (sceneModTarget_isSceneTarget(src_target)) {
        const scene_mod_target_descriptor_t *desc =
            sceneModTarget_descriptor(src_target);

        if (!desc || desc->kind == SCENE_MOD_TARGET_KIND_RETIRED)
            return 0u;
        if (desc->kind == SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY &&
            ctx->src_track == NUM_TRACKS - 1u) {
            if (ctx->dst_track != NUM_TRACKS - 1u)
                return 0u;
            return ccCopy_track7Alternate(ctx->dst_scene, dst_target);
        }
        if ((desc->kind == SCENE_MOD_TARGET_KIND_VOICE_MORPH ||
             desc->kind == SCENE_MOD_TARGET_KIND_AUDIO_OUT ||
             desc->kind == SCENE_MOD_TARGET_KIND_FX_SEND) &&
            desc->voice_slot == src_slot && src_slot != dst_slot)
            return ccCopy_sceneTargetForSlot(desc->kind, dst_slot, dst_target);
        return 1u;      /* Scene-wide (`fxm`, `7dc` elsewhere): unchanged */
    }
    if (effectTarget_isEffectId(src_target)) {
        effect_type_id_t st = ccCopy_effectType(ctx->src_scene);
        effect_type_id_t dt = ccCopy_effectType(ctx->dst_scene);
        uint8_t local = effectTarget_local(src_target);
        const effect_param_descriptor_t *sd;
        const effect_param_descriptor_t *dd;
        uint8_t index;

        if (st == dt)
            return 1u;
        sd = effects_descriptor(st, local);
        if (!sd)
            return 0u;
        dd = effects_descriptorByKey(dt, sd->base.file_key, &index);
        if (!dd || !effects_paramAutomatable(dt, index) ||
            dd->base.dtype != sd->base.dtype ||
            index >= EFFECT_TARGET_PATTERN_LOCAL_LIMIT)
            return 0u;
        *dst_target = effectTarget_id(index);
        return 1u;
    }
    return 1u;
}

uint8_t ccCopy_retargetEntries(const cc_retarget_ctx_t *ctx,
                               pat_automation_entry_t *autos, uint8_t count)
{
    uint8_t kept = 0u;
    uint8_t i;
    uint8_t j;

    if (!autos)
        return 0u;
    for (i = 0u; i < count; i++) {
        uint16_t target;
        uint8_t duplicate = 0u;

        if (!ccCopy_retarget(ctx, autos[i].target, &target))
            continue;
        /* Two entries that land on the same target keep the first. */
        for (j = 0u; j < kept; j++)
            if (autos[j].target == target)
                duplicate = 1u;
        if (duplicate)
            continue;
        autos[kept].target = target;
        autos[kept].value = autos[i].value;
        kept++;
    }
    return kept;
}

/* ---- merge rules (spec §4.4) ------------------------------------------- */

/* Union: source entries first, destination entries on new targets after. */
static uint8_t ccCopy_union(pat_automation_entry_t *src, uint8_t src_count,
                            const pat_automation_entry_t *dst,
                            uint8_t dst_count)
{
    uint8_t count = src_count;
    uint8_t i;
    uint8_t j;

    for (i = 0u; i < dst_count && count < PAT_BLOCK_AUTO_COUNT_MASK; i++) {
        uint8_t found = 0u;

        for (j = 0u; j < src_count; j++)
            if (src[j].target == dst[i].target)
                found = 1u;
        if (!found)
            src[count++] = dst[i];
    }
    return count;
}

uint8_t ccCopy_buildStep(uint8_t selection, const uint8_t *src_block,
                         uint8_t src_trigger, const uint8_t *dst_block,
                         uint8_t out[PAT_RAW_BLOCK_MAX], uint8_t *trigger_mode,
                         uint8_t *skip)
{
    pat_step_specials_t ss;
    pat_step_specials_t ds;
    pat_automation_entry_t sa[PAT_BLOCK_AUTO_COUNT_MASK];
    pat_automation_entry_t da[PAT_BLOCK_AUTO_COUNT_MASK];
    uint8_t sc;
    uint8_t dc;
    uint8_t flags;

    *skip = 0u;
    *trigger_mode = PAT_RAW_TRIGGER_KEEP;
    if (selection == CC_COPY_ALL) {
        uint8_t bytes = src_block ? pat_rawBlockBytes(src_block) : 0u;

        *trigger_mode = src_trigger ? PAT_RAW_TRIGGER_ON : PAT_RAW_TRIGGER_OFF;
        if (bytes)
            memcpy(out, src_block, bytes);
        return bytes;
    }
    sc = pat_rawDecode(src_block, &ss, sa, PAT_BLOCK_AUTO_COUNT_MASK);
    dc = pat_rawDecode(dst_block, &ds, da, PAT_BLOCK_AUTO_COUNT_MASK);
    switch (selection) {
    case CC_COPY_MERGE_ALL:
        /* An empty source step changes nothing under a merge. */
        if (!src_block && !src_trigger) {
            *skip = 1u;
            return 0u;
        }
        *trigger_mode = src_trigger ? PAT_RAW_TRIGGER_ON : PAT_RAW_TRIGGER_KEEP;
        flags = (uint8_t)(ss.flags | ds.flags);
        if ((ss.flags & PAT_SPECIAL_NOTE_BIT) == 0u)
            ss.note = ds.note;
        if ((ss.flags & PAT_SPECIAL_VEL_BIT) == 0u)
            ss.velocity = ds.velocity;
        if ((ss.flags & PAT_SPECIAL_PROB_BIT) == 0u)
            ss.probability = ds.probability;
        sc = ccCopy_union(sa, sc, da, dc);
        return pat_rawEncode(out, flags, ss.note, ss.velocity, ss.probability,
                             sa, sc);
    case CC_COPY_AUTO:
        /* Source automation and probability replace the destination's. */
        flags = (uint8_t)((ds.flags & (uint8_t)~PAT_SPECIAL_PROB_BIT) |
                          (ss.flags & PAT_SPECIAL_PROB_BIT));
        if (sc == 0u && dc == 0u && flags == ds.flags &&
            ((flags & PAT_SPECIAL_PROB_BIT) == 0u ||
             ss.probability == ds.probability)) {
            *skip = 1u;
            return 0u;
        }
        return pat_rawEncode(out, flags, ds.note, ds.velocity,
                             ss.probability, sa, sc);
    case CC_COPY_MERGE_AUTO:
    default:
        if (sc == 0u) {
            *skip = 1u;
            return 0u;
        }
        sc = ccCopy_union(sa, sc, da, dc);
        return pat_rawEncode(out, ds.flags, ds.note, ds.velocity,
                             ds.probability, sa, sc);
    }
}

/* ---- Scene-level executors (spec §9.9) --------------------------------- */

static uint16_t ccCopy_bit(uint8_t scene)
{
    return (uint16_t)(1u << scene);
}

static uint8_t ccCopy_activeIn(uint16_t mask)
{
    return (uint8_t)((mask & ccCopy_bit(scene_getActiveIndex())) != 0u);
}

/* Apply the active Scene's settings to the runtime and its mirrors. */
static void ccCopy_applyActiveSettings(void)
{
    uint8_t active = scene_getActiveIndex();
    uint8_t slot;

    preset_applySceneSettings(active);
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        (void)preset_applyKitAudioRouting(active, slot);
    preset_syncBusCompMirrors();
    preset_syncEffectMorphMirror();
    menu_repaintAll();
}

/* Kit and six Instrument rows of dst take the source Scene's rows. */
static void ccCopy_nameKit(uint8_t dst, uint8_t src)
{
    uint8_t slot;

    ccSvc_nameCopy(filesystem_identityRow(FS_ROW_KIT, dst, 0u),
                   filesystem_identityRow(FS_ROW_KIT, src, 0u));
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        ccSvc_nameCopy(filesystem_identityRow(FS_ROW_INSTRUMENT, dst, slot),
                       filesystem_identityRow(FS_ROW_INSTRUMENT, src, slot));
}

/*
 * Paste one Instrument with edit-mask fan-out (spec §4.4, user F3).
 *
 * Copies the source track's slot onto the destination track's slot in the
 * destination Scene and every Scene in its edit mask; fails silently when any
 * of them would exceed two Advanced Instruments; track 7 acts as slot 6 and
 * the Kit-owned slot-6/track-7 decay pair travels only slot 6 -> slot 6.
 * Phases: 0 check/commit, 1 drive the bounded Instrument apply, 2 record
 * names after the lazy scratch loan is available.
 */
static uint8_t ccCopy_runInstrument(const cc_job_t *job)
{
    cc_run_t *run = ccSvc_run();
    const cc_source_t *src = copyClear_source();
    uint8_t s_slot = ccCopy_slotOf(src->track);
    uint8_t d_slot = ccCopy_slotOf(job->track);
    instrument_type_t type = ccCopy_slotType(src->scene, s_slot);
    uint16_t mask;
    uint8_t m;

    if (run->phase == 2u) {
        if (!ccSvc_namesReady())
            return CC_RUN_WAIT;
        for (m = 0u; m < SCENE_COUNT; m++)
            if ((run->aux & ccCopy_bit(m)) != 0u)
                (void)ccSvc_nameCopy(
                    filesystem_identityRow(FS_ROW_INSTRUMENT, m, d_slot),
                    filesystem_identityRow(FS_ROW_INSTRUMENT, src->scene,
                                           s_slot));
        return CC_RUN_DONE;
    }
    if (run->phase == 1u) {
        if (preset_tickInstrumentApply())
            return CC_RUN_WAIT;
        menu_repaintAll();
        run->phase = 2u;
        return CC_RUN_WAIT;
    }
    if (src->scene == job->scene && s_slot == d_slot)
        return CC_RUN_DONE;
    mask = bank_sceneFanoutMask(job->scene);
    for (m = 0u; m < SCENE_COUNT; m++)
        if ((mask & ccCopy_bit(m)) != 0u &&
            !instrumentManager_typeSelectableForSceneSlot(m, d_slot, type)) {
            ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_ADVANCED_LIMIT,
                                  (uint8_t)((m & 0x0Fu) | (d_slot << 4u)));
            return CC_RUN_DROP;
        }
    if (!preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    preset_startInstrumentCopy(src->scene, s_slot, mask, d_slot);
    for (m = 0u; m < SCENE_COUNT; m++) {
        if ((mask & ccCopy_bit(m)) == 0u)
            continue;
        if (s_slot == INSTRUMENT_SLOT_COUNT - 1u &&
            d_slot == INSTRUMENT_SLOT_COUNT - 1u && m != src->scene) {
            scene_setSlot6Track7AmpEnvelopeDecay(
                m, scene_getSlot6Track7AmpEnvelopeDecay(src->scene));
            scene_setSlot6Track7MorphAmpEnvelopeDecay(
                m, scene_getSlot6Track7MorphAmpEnvelopeDecay(src->scene));
        }
    }
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)mask | ((uint32_t)(job->scene & 0xFu) << 16u) |
            (1u << 20u) | ((uint32_t)ccCopy_activeIn(mask) << 24u) |
            ((uint32_t)(d_slot & 0xFu) << 25u));
    bank_revalidateVoiceEditMasks();
    run->aux = mask;
    run->phase = 1u;
    return CC_RUN_WAIT;
}

/*
 * Paste a whole Kit with edit-mask fan-out. Phase 0 commits retained data,
 * phase 1 drives the active Scene worker, and phase 2 records names after the
 * lazy scratch loan is available.
 */
static uint8_t ccCopy_runKit(const cc_job_t *job)
{
    cc_run_t *run = ccSvc_run();
    const cc_source_t *src = copyClear_source();
    const scene_t *source = scene_getConst(src->scene);
    uint16_t mask;
    uint8_t m;

    if (run->phase == 2u) {
        if (!ccSvc_namesReady())
            return CC_RUN_WAIT;
        for (m = 0u; m < SCENE_COUNT; m++)
            if ((run->aux & ccCopy_bit(m)) != 0u)
                ccCopy_nameKit(m, src->scene);
        return CC_RUN_DONE;
    }
    if (run->phase == 1u) {
        if (!preset_applyWorkersIdle())
            return CC_RUN_WAIT;
        ccSvc_patternChangedUi(job->scene, NUM_TRACKS);
        menu_repaintAll();
        /* The retained Kit commit is complete; names are the lazy final phase. */
        run->phase = 2u;
        return CC_RUN_WAIT;
    }
    if (!source) {
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_NO_SOURCE, 0u);
        return CC_RUN_DROP;
    }
    mask = bank_sceneFanoutMask(job->scene);
    if (ccCopy_activeIn(mask) && !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    for (m = 0u; m < SCENE_COUNT; m++) {
        uint8_t was_present;

        if ((mask & ccCopy_bit(m)) == 0u || m == src->scene)
            continue;
        was_present = bank_scenePresent(m);
        (void)scene_commitKit(m, &source->kit);
        (void)bank_setScenePresentMask(
            (uint16_t)(bank_scenePresentMask() | ccCopy_bit(m)));
        if (!was_present)
            autosave_markSceneWithPatternDirty(m);
    }
    if (ccCopy_activeIn(mask) && scene_getActiveIndex() != src->scene)
        preset_startDrumsetApply();
    bank_revalidateVoiceEditMasks();
    run->aux = (uint16_t)(mask & (uint16_t)~ccCopy_bit(src->scene));
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)run->aux | ((uint32_t)(job->scene & 0xFu) << 16u) |
            (2u << 20u) | ((uint32_t)ccCopy_activeIn(mask) << 24u));
    run->phase = 1u;
    return CC_RUN_WAIT;
}

/*
 * Paste a whole Effect record with edit-mask fan-out. Phase 1 records names
 * after the data commit once the scratch loan is available.
 */
static uint8_t ccCopy_runEffect(const cc_job_t *job)
{
    cc_run_t *run = ccSvc_run();
    const cc_source_t *src = copyClear_source();
    const effect_record_t *record = scene_effectConst(src->scene);
    uint16_t written;
    uint8_t m;

    if (run->phase == 1u) {
        if (!ccSvc_namesReady())
            return CC_RUN_WAIT;
        for (m = 0u; m < SCENE_COUNT; m++)
            if ((run->aux & ccCopy_bit(m)) != 0u)
                (void)ccSvc_nameCopy(
                    filesystem_identityRow(FS_ROW_EFFECT, m, 0u),
                    filesystem_identityRow(FS_ROW_EFFECT, src->scene, 0u));
        return CC_RUN_DONE;
    }
    if (!record) {
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_NO_SOURCE, 0u);
        return CC_RUN_DROP;
    }
    if (ccCopy_activeIn(bank_sceneFanoutMask(job->scene)) &&
        !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    written = effects_pasteRecord(job->scene, record);
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)written | ((uint32_t)(job->scene & 0xFu) << 16u) |
            (3u << 20u) | ((uint32_t)ccCopy_activeIn(written) << 24u));
    if (ccCopy_activeIn(written))
        menu_repaintAll();
    run->aux = (uint16_t)(written & (uint16_t)~ccCopy_bit(src->scene));
    run->phase = 1u;
    return CC_RUN_WAIT;
}

/*
 * Paste all Scene settings without fan-out; the destination's edit mask
 * becomes the source's with the two bits exchanged (spec §4.4).
 */
static uint8_t ccCopy_runSceneSettings(const cc_job_t *job)
{
    const cc_source_t *src = copyClear_source();
    const scene_t *source = scene_getConst(src->scene);
    uint8_t active = (uint8_t)(job->scene == scene_getActiveIndex());

    if (!source) {
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_NO_SOURCE, 0u);
        return CC_RUN_DROP;
    }
    if (active && !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    (void)scene_commitSettings(job->scene, &source->settings);
    bank_exchangeVoiceEditMask(src->scene, job->scene);
    ccTrace(AUTOSAVE_TRACE_CC_EVT_MASK_SET,
            (uint32_t)bank_sceneMaskVoiceEditForScene(job->scene) |
            ((uint32_t)(job->scene & 0xFu) << 16u) |
            ((uint32_t)(src->scene & 0xFu) << 21u));
    bank_revalidateVoiceEditMasks();
    if (active)
        ccCopy_applyActiveSettings();
    return CC_RUN_DONE;
}

/*
 * Paste a whole Scene (settings, Effect, Kit, Pattern) without fan-out.
 * Phases: 0 retained commits, 1 whole-Pattern copy, 2 activation, 3 names.
 */
static uint8_t ccCopy_runScene(const cc_job_t *job)
{
    cc_run_t *run = ccSvc_run();
    const cc_source_t *src = copyClear_source();
    const scene_t *source = scene_getConst(src->scene);
    uint8_t dst = job->scene;
    uint8_t r;

    if (!source) {
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_NO_SOURCE, 0u);
        return CC_RUN_DROP;
    }
    switch (run->phase) {
    case 0u:
        if (!preset_applyWorkersIdle())
            return CC_RUN_WAIT;
        (void)scene_commitSettings(dst, &source->settings);
        bank_exchangeVoiceEditMask(src->scene, dst);
        ccTrace(AUTOSAVE_TRACE_CC_EVT_MASK_SET,
                (uint32_t)bank_sceneMaskVoiceEditForScene(dst) |
                ((uint32_t)(dst & 0xFu) << 16u) |
                ((uint32_t)(src->scene & 0xFu) << 21u));
        (void)scene_commitEffectRecord(dst, &source->effect);
        (void)scene_commitKit(dst, &source->kit);
        (void)bank_setScenePresentMask(
            (uint16_t)(bank_scenePresentMask() | ccCopy_bit(dst)));
        autosave_markSceneWithPatternDirty(dst);
        autosave_markEffectDirty(dst);
        run->phase = 1u;
        run->sub = 0u;
        return CC_RUN_WAIT;
    case 1u:
        r = ccSvc_runRegionCopy(src->scene, dst);
        if (r != CC_RUN_DONE)
            return r;
        if (dst == scene_getActiveIndex())
            preset_startDrumsetApply();
        run->phase = 2u;
        return CC_RUN_WAIT;
    case 2u:
        if (!preset_applyWorkersIdle())
            return CC_RUN_WAIT;
        bank_revalidateVoiceEditMasks();
        ccSvc_patternChangedUi(dst, NUM_TRACKS);
        if (dst == scene_getActiveIndex())
            menu_repaintAll();
        run->phase = 3u;
        return CC_RUN_WAIT;
    default:
        if (!ccSvc_namesReady())
            return CC_RUN_WAIT;
        (void)ccSvc_nameCopy(filesystem_identityRow(FS_ROW_SCENE, dst, 0u),
                             filesystem_identityRow(FS_ROW_SCENE, src->scene,
                                                    0u));
        ccCopy_nameKit(dst, src->scene);
        (void)ccSvc_nameCopy(filesystem_identityRow(FS_ROW_PATTERN, dst, 0u),
                             filesystem_identityRow(FS_ROW_PATTERN, src->scene,
                                                    0u));
        (void)ccSvc_nameCopy(filesystem_identityRow(FS_ROW_EFFECT, dst, 0u),
                             filesystem_identityRow(FS_ROW_EFFECT, src->scene,
                                                    0u));
        return CC_RUN_DONE;
    }
}

/* `copy pattern`: region first, Pattern-row name in a final lazy phase. */
static uint8_t ccCopy_runPatternOnly(const cc_job_t *job)
{
    cc_run_t *run = ccSvc_run();
    const cc_source_t *src = copyClear_source();
    uint8_t r;

    if (run->phase == 0u) {
        r = ccSvc_runRegionCopy(src->scene, job->scene);
        if (r != CC_RUN_DONE)
            return r;
        run->phase = 1u;
    }
    if (!ccSvc_namesReady())
        return CC_RUN_WAIT;
    (void)ccSvc_nameCopy(filesystem_identityRow(FS_ROW_PATTERN, job->scene, 0u),
                         filesystem_identityRow(FS_ROW_PATTERN, src->scene, 0u));
    return CC_RUN_DONE;
}

/*
 * Paste FX sequence steps (spec §4.4 FX step).
 *
 * What: copies each source step's lock mask and lane values onto the
 * destination steps in range order with wrap, fanning out to same-type Scenes
 * in the destination's edit mask; a destination whose Effect type differs
 * from the source's is dropped silently. The source range is snapshotted on
 * the stack (≤ 288 B) so overlapping ranges need no name-buffer loan. Why:
 * user rules B9, F3. Output: DONE or DROP.
 * Affiliates: effects_pasteSeqStep().
 */
static uint8_t ccCopy_runFxSteps(const cc_job_t *job)
{
    const cc_source_t *src = copyClear_source();
    const effect_record_t *s = scene_effectConst(src->scene);
    const effect_record_t *d = scene_effectConst(job->scene);
    effect_seq_step_t snap[EFFECT_SEQ_STEP_COUNT];
    uint8_t lo = (src->start < src->end) ? src->start : src->end;
    uint8_t hi = (src->start < src->end) ? src->end : src->start;
    uint8_t count = (uint8_t)(hi - lo + 1u);
    int8_t dir = (src->start <= src->end) ? 1 : -1;
    uint16_t mask;
    uint8_t i;

    if (!s || !d || src->start >= EFFECT_SEQ_STEP_COUNT ||
        src->end >= EFFECT_SEQ_STEP_COUNT) {
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_NO_SOURCE, 0u);
        return CC_RUN_DROP;
    }
    if (s->type != d->type) {
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_FX_TYPE_MISMATCH,
                              (uint8_t)((s->type & 0x0Fu) |
                                        ((d->type & 0x0Fu) << 4u)));
        return CC_RUN_DROP;
    }
    for (i = 0u; i < count; i++)
        snap[i] = s->steps[(uint8_t)((int8_t)src->start + dir * (int8_t)i)];
    for (i = 0u; i < count; i++)
        (void)effects_pasteSeqStep(job->scene,
                                   (uint8_t)((job->start + i) %
                                             EFFECT_SEQ_STEP_COUNT),
                                   &snap[i]);
    mask = bank_sceneFanoutMask(job->scene);
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)mask | ((uint32_t)(job->scene & 0xFu) << 16u) |
            (6u << 20u) | ((uint32_t)ccCopy_activeIn(mask) << 24u));
    for (i = 0u; i < SCENE_COUNT; i++)
        if ((mask & ccCopy_bit(i)) != 0u)
            ccSvc_nameContentChanged(
                filesystem_identityRow(FS_ROW_EFFECT, i, 0u));
    if (menu_activePage == EFFECT_PAGE) {
        menuEffects_renderSeqLeds();
        menu_repaint();
    }
    return CC_RUN_DONE;
}

/*
 * `copy track morph` (S076 P3): copy the source track's Normal endpoints onto
 * the destination track's Morph endpoints (instrument images plus the
 * correlated Scene params), fanning out through the edit mask.
 *
 * What:       for each Scene in the fan-out mask:
 *             (a) preset_copySlotNormalToMorph() copies the morphable
 *                 instrument Normal bytes onto the Morph bytes; it returns 0
 *                 and changes nothing when the slot types differ (silent skip,
 *                 user-confirmed);
 *             (b) preset_setVoiceFxSendMorph() sets the FX-send Morph endpoint
 *                 to the source's FX-send Normal endpoint (always, no type
 *                 dependency);
 *             (c) when both slots are slot 6 the generated slot-6/track-7 Morph
 *                 decay takes the source's Normal decay;
 *             (d) the destination instrument row loses its refreshed flag.
 *             When the active Scene is in the mask the Morph worker is
 *             requeued once after all writes.
 * Why:        per-track morph copy is a Scene-child edit, so it fans out like
 *             `copy instrument` (user F3). No morph amount and no Pattern data
 *             is touched, and no phase machine is needed because every write is
 *             an immediate retained commit.
 * Inputs:     job->scene/job->track (destination Scene/track); source from
 *             copyClear_source().
 * Outputs:    CC_RUN_WAIT while the active Scene's apply workers drain, then
 *             CC_RUN_DONE. Side effects: Instrument Morph, FX-send Morph and
 *             Kit Morph-decay AutoSave marks; Morph worker requeue.
 * Accessors:  copyClear_source(), ccCopy_slotOf(), bank_sceneFanoutMask(),
 *             preset_copySlotNormalToMorph(), preset_setVoiceFxSendMorph(),
 *             scene_getVoiceFxSendAmount(),
 *             scene_setSlot6Track7MorphAmpEnvelopeDecay(),
 *             scene_getSlot6Track7AmpEnvelopeDecay(), preset_rebuildMorph().
 * Affiliates: ccCopy_runInstrument() (fan-out copy model),
 *             ccClear_runResetMorphTrack() (analogous reset).
 */
static uint8_t ccCopy_runMorphTrack(const cc_job_t *job)
{
    const cc_source_t *src = copyClear_source();
    uint8_t s_slot;
    uint8_t d_slot;
    uint16_t mask;
    uint8_t active = scene_getActiveIndex();
    uint8_t m;

    if (!src)
        return CC_RUN_DROP;
    s_slot = ccCopy_slotOf(src->track);
    d_slot = ccCopy_slotOf(job->track);
    mask = bank_sceneFanoutMask(job->scene);
    if ((mask & ccCopy_bit(active)) != 0u && !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    for (m = 0u; m < SCENE_COUNT; m++) {
        if ((mask & ccCopy_bit(m)) == 0u)
            continue;
        /*
         * Silent type-mismatch skip: preset_copySlotNormalToMorph() returns 0
         * and leaves the member unchanged when the slot instrument types
         * differ (no cross-type descriptor remapping, user-confirmed).
         */
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
    if ((mask & ccCopy_bit(active)) != 0u)
        preset_rebuildMorph();
    menu_repaint();
    return CC_RUN_DONE;
}

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

uint8_t ccCopy_runJob(const cc_job_t *job)
{
    uint8_t sel;

    if (!job) {
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_BAD_SELECTION, 0u);
        return CC_RUN_DROP;
    }
    sel = (uint8_t)(job->op & CC_JOB_SEL_MASK);
    /*
     * What:       CC_KIND_BAR_TO_STEP is a Pattern paste (bar source, step
     *             destination) and uses the same engine as CC_KIND_STEP and
     *             CC_KIND_BAR.
     * Why:        ccSvc_runPatternPaste() dispatches entirely on the resolved
     *             geometry; the new kind produces correct geometry through
     *             ccSvc_pasteGeometry(). No per-kind logic inside the engine
     *             depends on the value.
     * Affiliates: ccSvc_pasteGeometry(), CC_KIND_BAR_TO_STEP.
     */
    switch (job->kind) {
    case CC_KIND_STEP:
    case CC_KIND_BAR:
    case CC_KIND_BAR_TO_STEP:
        return ccSvc_runPatternPaste(job);
    case CC_KIND_TRACK:
        /*
         * What:       three-way track copy dispatch. CC_COPY_MORPH is the new
         *             "inst -> morph" selection; CC_COPY_INSTRUMENT keeps its slot
         *             paste; every other selection is a Pattern paste.
         * Why:        morph copy must reach its own endpoint executor rather
         *             than the Pattern engine (its job selection shares the
         *             low nibble space with the step selections).
         * Affiliates: ccCopy_runInstrument(), ccCopy_runMorphTrack(),
         *             ccSvc_runPatternPaste().
         */
        if (sel == CC_COPY_MORPH)
            return ccCopy_runMorphTrack(job);
        if (sel == CC_COPY_INSTRUMENT)
            return ccCopy_runInstrument(job);
        return ccSvc_runPatternPaste(job);
    case CC_KIND_SCENE:
        switch (sel) {
        case CC_COPY_SCENE:          return ccCopy_runScene(job);
        case CC_COPY_SCENE_SETTINGS: return ccCopy_runSceneSettings(job);
        case CC_COPY_KIT:            return ccCopy_runKit(job);
        case CC_COPY_EFFECT:         return ccCopy_runEffect(job);
        case CC_COPY_PATTERN:        return ccCopy_runPatternOnly(job);
        case CC_COPY_SCENE_MORPH:    return ccCopy_runSceneMorph(job);
        default:
            ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_BAD_SELECTION, sel);
            return CC_RUN_DROP;
        }
    case CC_KIND_FX_STEP:
        return ccCopy_runFxSteps(job);
    default:
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_BAD_SELECTION, sel);
        return CC_RUN_DROP;
    }
}
