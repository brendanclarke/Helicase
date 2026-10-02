/*
 * clearOps.c — clear rules for Phase 6 clear (spec §5, §6, §9.9).
 *
 * Contract in clearOps.h. Pattern clears are sequenced by the service engine
 * (copyClearService.c); this file supplies the menus and runs the Scene-level
 * clears. `clear scene` and `clear scene settings` reset the Scene's own edit
 * mask and do not fan out; `clear send`, `clear fx`, `clear fx sequence`, the
 * EFFECTS SEQ clear and the FX-lane part of a pot clear fan out through the
 * edit mask (user, confirm 1). Names stay on every clear; changed rows lose
 * their refreshed flag.
 */

#include "clearOps.h"
#include "copyClearService.h"
#include "PatternData.h"
#include "SceneData.h"
#include "BankData.h"
#include "presetManager.h"
#include "EffectsManager.h"
#include "InstrumentManager.h"
#include "filesystem.h"
#include "menu.h"
#include "menuEffects.h"
#include "buttonHandler.h"
#include <string.h>

/* ---- menus (spec §5, §8.1) --------------------------------------------- */

static const char *const ccClear_stepLabels[] = {
    "cancel", "step", "step auto", "step notes"
};
static const char *const ccClear_barLabels[] = {
    "cancel", "bar", "bar auto", "bar notes"
};
static const char *const ccClear_trackLabels[] = {
    "cancel", "track", "track auto", "track notes", "send"
};
static const char *const ccClear_sceneLabels[] = {
    "cancel", "scene", "settings", "pattern", "automation", "notes", "fx",
    "fx sequence"
};

cc_menu_t ccClear_menuForObject(uint8_t mode, cc_kind_t kind)
{
    switch (kind) {
    case CC_KIND_STEP:  return CC_MENU_CLEAR_STEP;
    case CC_KIND_BAR:   return CC_MENU_CLEAR_BAR;
    case CC_KIND_TRACK: return (mode == SELECT_MODE_FX) ? CC_MENU_CLEAR_TRACK_FX
                                                        : CC_MENU_CLEAR_TRACK;
    case CC_KIND_SCENE: return CC_MENU_CLEAR_SCENE;
    default:            return CC_MENU_NONE;
    }
}

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

const char *ccClear_label(cc_menu_t menu, uint8_t selection)
{
    if (selection >= ccClear_selectionCount(menu))
        return "";
    switch (menu) {
    case CC_MENU_CLEAR_STEP:     return ccClear_stepLabels[selection];
    case CC_MENU_CLEAR_BAR:      return ccClear_barLabels[selection];
    case CC_MENU_CLEAR_TRACK:
    case CC_MENU_CLEAR_TRACK_FX: return ccClear_trackLabels[selection];
    case CC_MENU_CLEAR_SCENE:    return ccClear_sceneLabels[selection];
    default:                     return "";
    }
}

uint8_t ccClear_requestClear(const cc_source_t *object, uint8_t selection)
{
    cc_job_t job;

    if (!object || selection == 0u || object->kind == CC_KIND_NONE)
        return 0u;
    job.op = (uint8_t)(CC_JOB_CLEAR | (selection & CC_JOB_SEL_MASK));
    job.kind = object->kind;
    job.scene = object->scene;
    job.track = object->track;
    job.start = object->start;
    job.end = object->end;
    return ccSvc_enqueue(&job);
}

/* ---- helpers ------------------------------------------------------------ */

static uint16_t ccClear_bit(uint8_t scene)
{
    return (uint16_t)(1u << scene);
}

/* Effect rows of every Scene in a mask lose their refreshed flag. */
static void ccClear_effectRowsChanged(uint16_t mask)
{
    uint8_t m;

    for (m = 0u; m < SCENE_COUNT; m++)
        if ((mask & ccClear_bit(m)) != 0u)
            ccSvc_nameContentChanged(
                filesystem_identityRow(FS_ROW_EFFECT, m, 0u));
}

/* Repaint the Effect page and its SEQ LEDs after a sequence change. */
static void ccClear_effectUi(void)
{
    if (menu_activePage != EFFECT_PAGE)
        return;
    menuEffects_renderSeqLeds();
    menu_repaint();
}

/* Apply the active Scene's settings to the runtime and its mirrors. */
static void ccClear_applyActiveSettings(void)
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

/* ---- immediate clears ---------------------------------------------------- */

void ccClear_fxStepNow(uint8_t scene, uint8_t step)
{
    if (step >= EFFECT_SEQ_STEP_COUNT)
        return;
    if (effects_clearSeqLanes(scene, (uint16_t)(1u << step), 0xFFFFu))
        ccClear_effectRowsChanged(bank_sceneFanoutMask(scene));
    ccClear_effectUi();
}

uint8_t ccClear_potTurned(const cc_pot_target_t *target)
{
    uint8_t scene = menu_getViewedPattern();
    uint8_t has_pattern;
    uint8_t has_lane;

    if (!target)
        return 0u;
    has_pattern = (uint8_t)(target->pattern_target != INSTRUMENT_PARAM_INVALID);
    has_lane = (uint8_t)(target->fx_lane < EFFECT_SEQ_LANE_COUNT);
    if (!has_pattern && !has_lane)
        return 0u;
    if (has_pattern && ccSvc_targetPending(target->pattern_target))
        return 1u;
    /* A ninth turn, or another Scene's register, does nothing (spec §6). */
    if (has_pattern &&
        (ccSvc_registerFull() ||
         (ccSvc_registerScene() != 0xFFu && ccSvc_registerScene() != scene)))
        return 0u;
    if (has_lane) {
        if (effects_clearSeqLanes(scene, 0xFFFFu,
                                  (uint16_t)(1u << target->fx_lane)))
            ccClear_effectRowsChanged(bank_sceneFanoutMask(scene));
        ccClear_effectUi();
    }
    if (has_pattern && ccSvc_registerAdd(target->pattern_target, scene))
        menu_automationTargetCleared(target->pattern_target);
    return 1u;
}

/* ---- queued Scene-level clears (spec §5, §9.9) ------------------------- */

/* `clear send`: FX send 0 and fader `pre` on the track's slot, fanned out. */
static uint8_t ccClear_runSend(const cc_job_t *job)
{
    uint8_t slot = (job->track < INSTRUMENT_SLOT_COUNT)
                       ? job->track
                       : (uint8_t)(INSTRUMENT_SLOT_COUNT - 1u);
    uint16_t mask = bank_sceneFanoutMask(job->scene);
    uint8_t m;

    for (m = 0u; m < SCENE_COUNT; m++) {
        if ((mask & ccClear_bit(m)) == 0u)
            continue;
        (void)preset_setVoiceFxSendAmount(m, slot, 0u);
        (void)preset_setVoiceFaderSetting(m, slot, 0u);
    }
    menu_repaint();
    return CC_RUN_DONE;
}

/* `clear scene settings`: defaults, mask to self, runtime if active. */
static uint8_t ccClear_runSceneSettings(const cc_job_t *job)
{
    scene_settings_t defaults;
    uint8_t active = (uint8_t)(job->scene == scene_getActiveIndex());

    if (active && !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    scene_settingsDefaults(&defaults);
    (void)scene_commitSettings(job->scene, &defaults);
    bank_resetVoiceEditMaskToSelf(job->scene);
    ccSvc_nameContentChanged(filesystem_identityRow(FS_ROW_SCENE, job->scene,
                                                    0u));
    if (active)
        ccClear_applyActiveSettings();
    return CC_RUN_DONE;
}

/*
 * `clear scene` (spec §5). Active Scene: everything except the Kit to
 * defaults (user B11). Another Scene: emptied, Kit included, and no longer
 * Bank-present. Both reset the edit mask to self and never fan out. Phases:
 * 0 settings, Effect, Kit, mask, present, names, runtime; 1 Pattern reset.
 */
static uint8_t ccClear_runScene(const cc_job_t *job)
{
    cc_run_t *run = ccSvc_run();
    uint8_t scene = job->scene;
    uint8_t active = (uint8_t)(scene == scene_getActiveIndex());

    if (run->phase == 0u) {
        scene_settings_t defaults;
        effect_record_t *effect;
        uint8_t slot;

        if (!preset_applyWorkersIdle())
            return CC_RUN_WAIT;
        scene_settingsDefaults(&defaults);
        (void)scene_commitSettings(scene, &defaults);
        effect = scene_effectRecordForWholeCommit(scene);
        if (effect) {
            scene_effectRecordDefaults(effect);
            scene_finishEffectWholeCommit(scene);
        }
        if (active) {
            effects_activateScene(scene);
        } else {
            scene_resetKitToDefaults(scene);
            (void)bank_setScenePresentMask(
                (uint16_t)(bank_scenePresentMask() & (uint16_t)~ccClear_bit(scene)));
            ccSvc_nameContentChanged(
                filesystem_identityRow(FS_ROW_KIT, scene, 0u));
            for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
                ccSvc_nameContentChanged(
                    filesystem_identityRow(FS_ROW_INSTRUMENT, scene, slot));
        }
        bank_resetVoiceEditMaskToSelf(scene);
        bank_revalidateVoiceEditMasks();
        ccSvc_nameContentChanged(filesystem_identityRow(FS_ROW_SCENE, scene,
                                                        0u));
        ccSvc_nameContentChanged(filesystem_identityRow(FS_ROW_EFFECT, scene,
                                                        0u));
        ccSvc_nameContentChanged(filesystem_identityRow(FS_ROW_PATTERN, scene,
                                                        0u));
        if (active)
            ccClear_applyActiveSettings();
        run->phase = 1u;
        run->sub = 0u;
        return CC_RUN_WAIT;
    }
    return ccSvc_runRegionReset(scene);
}

/* `clear fx`: the Effect record to defaults (`off`), fanned out. */
static uint8_t ccClear_runFx(const cc_job_t *job)
{
    uint16_t written;

    if ((bank_sceneFanoutMask(job->scene) &
         ccClear_bit(scene_getActiveIndex())) != 0u &&
        !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    written = effects_resetRecord(job->scene);
    ccClear_effectRowsChanged(written);
    if ((written & ccClear_bit(scene_getActiveIndex())) != 0u)
        menu_repaintAll();
    return CC_RUN_DONE;
}

/* `clear fx sequence`: all 16 steps empty; settings stay; fanned out. */
static uint8_t ccClear_runFxSequence(const cc_job_t *job)
{
    if (effects_clearSeqLanes(job->scene, 0xFFFFu, 0xFFFFu))
        ccClear_effectRowsChanged(bank_sceneFanoutMask(job->scene));
    ccClear_effectUi();
    return CC_RUN_DONE;
}

uint8_t ccClear_runJob(const cc_job_t *job)
{
    uint8_t sel;
    uint8_t r;

    if (!job)
        return CC_RUN_DROP;
    sel = (uint8_t)(job->op & CC_JOB_SEL_MASK);
    switch (job->kind) {
    case CC_KIND_STEP:
    case CC_KIND_BAR:
        return ccSvc_runPatternClear(job);
    case CC_KIND_TRACK:
        return (sel == CC_CLEAR_SEND) ? ccClear_runSend(job)
                                      : ccSvc_runPatternClear(job);
    case CC_KIND_SCENE:
        switch (sel) {
        case CC_CLEAR_SCENE_ALL:         return ccClear_runScene(job);
        case CC_CLEAR_SCENE_SETTINGS:    return ccClear_runSceneSettings(job);
        case CC_CLEAR_SCENE_PATTERN:
            r = ccSvc_runRegionReset(job->scene);
            if (r == CC_RUN_DONE)
                ccSvc_nameContentChanged(
                    filesystem_identityRow(FS_ROW_PATTERN, job->scene, 0u));
            return r;
        case CC_CLEAR_SCENE_AUTOMATION:
        case CC_CLEAR_SCENE_NOTES:       return ccSvc_runPatternClear(job);
        case CC_CLEAR_SCENE_FX:          return ccClear_runFx(job);
        case CC_CLEAR_SCENE_FX_SEQUENCE: return ccClear_runFxSequence(job);
        default:                         return CC_RUN_DROP;
        }
    default:
        return CC_RUN_DROP;
    }
}
