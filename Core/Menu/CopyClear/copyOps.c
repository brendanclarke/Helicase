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

static const char *const ccCopy_stepLabels[] = {
    "step all", "merge all", "automation", "merge auto"
};
static const char *const ccCopy_barLabels[] = {
    "bar all", "merge all", "automation", "merge auto"
};
static const char *const ccCopy_trackLabels[] = { "track", "instrument" };
static const char *const ccCopy_sceneLabels[] = {
    "scene", "settings", "kit", "effect", "pattern"
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
    switch (menu) {
    case CC_MENU_COPY_STEP:  return 4u;
    case CC_MENU_COPY_BAR:   return 4u;
    case CC_MENU_COPY_TRACK: return 2u;
    case CC_MENU_COPY_SCENE: return 5u;
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

    if (!src || src->kind == CC_KIND_NONE)
        return 0u;
    /* A paste identical to its source does nothing (spec §4.3). */
    switch (src->kind) {
    case CC_KIND_STEP:
    case CC_KIND_BAR:
        if (dst_scene == src->scene && dst_track == src->track &&
            dst_start == src->start && src->start <= src->end)
            return 0u;
        break;
    case CC_KIND_TRACK:
        if (dst_scene == src->scene &&
            ((selection == CC_COPY_TRACK && dst_track == src->track) ||
             (selection == CC_COPY_INSTRUMENT &&
              ccCopy_slotOf(dst_track) == ccCopy_slotOf(src->track))))
            return 0u;
        break;
    case CC_KIND_SCENE:
        if (dst_scene == src->scene)
            return 0u;
        break;
    case CC_KIND_FX_STEP:
        if (dst_scene == src->scene && dst_start == src->start &&
            src->start <= src->end)
            return 0u;
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
    return ccSvc_enqueue(&job);
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
        if (sc == 0u && dc == 0u) {
            *skip = 1u;
            return 0u;
        }
        return pat_rawEncode(out, ds.flags, ds.note, ds.velocity,
                             ds.probability, sa, sc);
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
 * Phases: 0 check, wait for idle workers, commit, names; 1 drive the bounded
 * Instrument apply (Menu does not tick copy-started applies).
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

    if (run->phase == 1u) {
        if (preset_tickInstrumentApply())
            return CC_RUN_WAIT;
        menu_repaintAll();
        return CC_RUN_DONE;
    }
    if (src->scene == job->scene && s_slot == d_slot)
        return CC_RUN_DONE;
    mask = bank_sceneFanoutMask(job->scene);
    for (m = 0u; m < SCENE_COUNT; m++)
        if ((mask & ccCopy_bit(m)) != 0u &&
            !instrumentManager_typeSelectableForSceneSlot(m, d_slot, type))
            return CC_RUN_DROP;
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
        ccSvc_nameCopy(filesystem_identityRow(FS_ROW_INSTRUMENT, m, d_slot),
                       filesystem_identityRow(FS_ROW_INSTRUMENT, src->scene,
                                              s_slot));
    }
    bank_revalidateVoiceEditMasks();
    run->phase = 1u;
    return CC_RUN_WAIT;
}

/*
 * Paste a whole Kit with edit-mask fan-out (spec §4.4). Same retained and
 * runtime path as Kit Load: whole-Kit AutoSave marker, Bank-present, Scene
 * worker for the active Scene, mask revalidation; a Scene that becomes
 * present gets a whole-Scene marker so settings/Effect pasted while it had no
 * Kit are captured (spec §9.9).
 */
static uint8_t ccCopy_runKit(const cc_job_t *job)
{
    cc_run_t *run = ccSvc_run();
    const cc_source_t *src = copyClear_source();
    const scene_t *source = scene_getConst(src->scene);
    uint16_t mask;
    uint8_t m;

    if (run->phase == 1u) {
        if (!preset_applyWorkersIdle())
            return CC_RUN_WAIT;
        ccSvc_patternChangedUi(job->scene, NUM_TRACKS);
        menu_repaintAll();
        return CC_RUN_DONE;
    }
    if (!source)
        return CC_RUN_DROP;
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
        ccCopy_nameKit(m, src->scene);
    }
    if (ccCopy_activeIn(mask) && scene_getActiveIndex() != src->scene)
        preset_startDrumsetApply();
    bank_revalidateVoiceEditMasks();
    run->phase = 1u;
    return CC_RUN_WAIT;
}

/* Paste a whole Effect record with edit-mask fan-out (spec §4.4). */
static uint8_t ccCopy_runEffect(const cc_job_t *job)
{
    const cc_source_t *src = copyClear_source();
    const effect_record_t *record = scene_effectConst(src->scene);
    uint16_t written;
    uint8_t m;

    if (!record)
        return CC_RUN_DROP;
    if (ccCopy_activeIn(bank_sceneFanoutMask(job->scene)) &&
        !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    written = effects_pasteRecord(job->scene, record);
    for (m = 0u; m < SCENE_COUNT; m++)
        if ((written & ccCopy_bit(m)) != 0u && m != src->scene)
            ccSvc_nameCopy(filesystem_identityRow(FS_ROW_EFFECT, m, 0u),
                           filesystem_identityRow(FS_ROW_EFFECT, src->scene,
                                                  0u));
    if (ccCopy_activeIn(written))
        menu_repaintAll();
    return CC_RUN_DONE;
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

    if (!source)
        return CC_RUN_DROP;
    if (active && !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    (void)scene_commitSettings(job->scene, &source->settings);
    bank_exchangeVoiceEditMask(src->scene, job->scene);
    bank_revalidateVoiceEditMasks();
    if (active)
        ccCopy_applyActiveSettings();
    return CC_RUN_DONE;
}

/*
 * Paste a whole Scene (settings, Effect, Kit, Pattern) without fan-out
 * (spec §4.4, §9.8). Phases: 0 retained commits and names; 1 whole-Pattern
 * copy (literal: the types now match); 2 one Scene activation if active.
 */
static uint8_t ccCopy_runScene(const cc_job_t *job)
{
    cc_run_t *run = ccSvc_run();
    const cc_source_t *src = copyClear_source();
    const scene_t *source = scene_getConst(src->scene);
    uint8_t dst = job->scene;
    uint8_t r;

    if (!source)
        return CC_RUN_DROP;
    switch (run->phase) {
    case 0u:
        if (!preset_applyWorkersIdle())
            return CC_RUN_WAIT;
        (void)scene_commitSettings(dst, &source->settings);
        bank_exchangeVoiceEditMask(src->scene, dst);
        (void)scene_commitEffectRecord(dst, &source->effect);
        (void)scene_commitKit(dst, &source->kit);
        (void)bank_setScenePresentMask(
            (uint16_t)(bank_scenePresentMask() | ccCopy_bit(dst)));
        autosave_markSceneWithPatternDirty(dst);
        autosave_markEffectDirty(dst);
        ccSvc_nameCopy(filesystem_identityRow(FS_ROW_SCENE, dst, 0u),
                       filesystem_identityRow(FS_ROW_SCENE, src->scene, 0u));
        ccCopy_nameKit(dst, src->scene);
        ccSvc_nameCopy(filesystem_identityRow(FS_ROW_PATTERN, dst, 0u),
                       filesystem_identityRow(FS_ROW_PATTERN, src->scene, 0u));
        ccSvc_nameCopy(filesystem_identityRow(FS_ROW_EFFECT, dst, 0u),
                       filesystem_identityRow(FS_ROW_EFFECT, src->scene, 0u));
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
    default:
        if (!preset_applyWorkersIdle())
            return CC_RUN_WAIT;
        bank_revalidateVoiceEditMasks();
        ccSvc_patternChangedUi(dst, NUM_TRACKS);
        if (dst == scene_getActiveIndex())
            menu_repaintAll();
        return CC_RUN_DONE;
    }
}

/* `copy pattern`: whole region, retargeted when types differ (spec §9.8). */
static uint8_t ccCopy_runPatternOnly(const cc_job_t *job)
{
    const cc_source_t *src = copyClear_source();
    uint8_t r = ccSvc_runRegionCopy(src->scene, job->scene);

    if (r == CC_RUN_DONE)
        ccSvc_nameCopy(filesystem_identityRow(FS_ROW_PATTERN, job->scene, 0u),
                       filesystem_identityRow(FS_ROW_PATTERN, src->scene, 0u));
    return r;
}

/*
 * Paste FX sequence steps (spec §4.4 FX step).
 *
 * What: copies each source step's lock mask and lane values onto the
 * destination steps in range order with wrap, fanning out to same-type Scenes
 * in the destination's edit mask; a destination whose Effect type differs
 * from the source's is dropped silently. The source range is snapshotted into
 * the name buffer first (≤ 288 B) so overlapping ranges on one Scene are
 * safe. Why: user rules B9, F3. Output: DONE or DROP.
 * Affiliates: effects_pasteSeqStep().
 */
static uint8_t ccCopy_runFxSteps(const cc_job_t *job)
{
    const cc_source_t *src = copyClear_source();
    const effect_record_t *s = scene_effectConst(src->scene);
    const effect_record_t *d = scene_effectConst(job->scene);
    uint8_t *scratch = ccSvc_scratch();
    effect_seq_step_t *snap;
    uint8_t lo = (src->start < src->end) ? src->start : src->end;
    uint8_t hi = (src->start < src->end) ? src->end : src->start;
    uint8_t count = (uint8_t)(hi - lo + 1u);
    int8_t dir = (src->start <= src->end) ? 1 : -1;
    uint16_t mask;
    uint8_t i;

    if (!s || !d || !scratch || s->type != d->type ||
        src->start >= EFFECT_SEQ_STEP_COUNT || src->end >= EFFECT_SEQ_STEP_COUNT)
        return CC_RUN_DROP;
    snap = (effect_seq_step_t *)(void *)&scratch[CC_SCRATCH_BLOCK_OFFSET];
    for (i = 0u; i < count; i++)
        snap[i] = s->steps[(uint8_t)((int8_t)src->start + dir * (int8_t)i)];
    for (i = 0u; i < count; i++)
        (void)effects_pasteSeqStep(job->scene,
                                   (uint8_t)((job->start + i) %
                                             EFFECT_SEQ_STEP_COUNT),
                                   &snap[i]);
    mask = bank_sceneFanoutMask(job->scene);
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

uint8_t ccCopy_runJob(const cc_job_t *job)
{
    uint8_t sel;

    if (!job)
        return CC_RUN_DROP;
    sel = (uint8_t)(job->op & CC_JOB_SEL_MASK);
    switch (job->kind) {
    case CC_KIND_STEP:
    case CC_KIND_BAR:
        return ccSvc_runPatternPaste(job);
    case CC_KIND_TRACK:
        return (sel == CC_COPY_INSTRUMENT) ? ccCopy_runInstrument(job)
                                           : ccSvc_runPatternPaste(job);
    case CC_KIND_SCENE:
        switch (sel) {
        case CC_COPY_SCENE:          return ccCopy_runScene(job);
        case CC_COPY_SCENE_SETTINGS: return ccCopy_runSceneSettings(job);
        case CC_COPY_KIT:            return ccCopy_runKit(job);
        case CC_COPY_EFFECT:         return ccCopy_runEffect(job);
        case CC_COPY_PATTERN:        return ccCopy_runPatternOnly(job);
        default:                     return CC_RUN_DROP;
        }
    case CC_KIND_FX_STEP:
        return ccCopy_runFxSteps(job);
    default:
        return CC_RUN_DROP;
    }
}
