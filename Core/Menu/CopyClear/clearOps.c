/*
 * clearOps.c — clear rules for Phase 6 clear (spec §5, §6, §9.9).
 *
 * Contract in clearOps.h. Pattern clears are sequenced by the service engine
 * (copyClearService.c); this file supplies the menus and runs the Scene-level
 * clears. `clear scene` and `clear scene settings` reset the Scene's own edit
 * mask and do not fan out; `clear send`, `clear fx`, `clear fx sequence`,
 * `clear scene reset morph`, the
 * EFFECTS SEQ clear and the FX-lane part of a pot clear fan out through the
 * edit mask (user, confirm 1). Names stay on every clear; changed rows lose
 * their refreshed flag. `notes` turns triggers off, removes note/velocity
 * specials, and keeps probability plus automation (F1-J, user A5).
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
/*
 * What:       track clear labels, with "reset morph" before "send". The VOICE
 *             TRACK menu shows count 5 (indices 0..4: cancel, track,
 *             track auto, track notes, reset morph); the EFFECTS TRACK menu
 *             shows count 6 (0..5: adds "send" at the end). One shared array
 *             serves both because the VOICE encoder never reaches index 5.
 * Why:        "reset morph" appears in both menus while "send" is EFFECTS-only,
 *             so a single array with the shared selection first and "send"
 *             last avoids splitting the arrays and the label dispatch.
 * Inputs:     ccClear_label() indexes by selection, gated by selectionCount.
 * Outputs:    const label strings.
 * Affiliates: CC_CLEAR_RESET_MORPH (4), CC_CLEAR_SEND (5).
 */
static const char *const ccClear_trackLabels[] = {
    "cancel", "track", "track auto", "track notes", "reset morph", "send"
};
/*
 * What:       PERF Scene clear labels. "reset morph" (index 8) equalises the
 *             whole Scene's morph endpoints; "reset fx morph" (index 9)
 *             equalises only the Effect's morphable morph endpoints.
 *             "reload scene" (index 10) reloads the Scene from its HCNAMES
 *             source slot on the SD card.
 * Why:        extends the menu to expose the Scene reload operation that
 *             reverts the Scene to its saved state without page navigation.
 * Affiliates: CC_CLEAR_SCENE_RESET_MORPH, CC_CLEAR_SCENE_RESET_FX_MORPH,
 *             CC_CLEAR_SCENE_RELOAD, ccClear_runReloadScene().
 */
static const char *const ccClear_sceneLabels[] = {
    "cancel", "scene", "settings", "pattern", "automation", "notes", "fx",
    "fx sequence", "reset morph", "reset fx morph", "reload scene"
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
    /*
     * What:       updated counts: CLEAR_TRACK 4 -> 5 (added "reset morph"),
     *             CLEAR_TRACK_FX 5 -> 6 (added "reset morph"; "send" is now
     *             the last shared label), CLEAR_SCENE 8 -> 10 (added
     *             "reset morph" and "reset fx morph").
     * Why:        the encoder clamps to selectionCount - 1; raising each count
     *             exposes the new selections at the end of its menu.
     * Inputs:     cc_menu_t from ccClear_menuForObject().
     * Outputs:    total entry count including "cancel" at index 0.
     * Affiliates: ccClear_label(), copyClearSession.c encoder clamping.
     */
    switch (menu) {
    case CC_MENU_CLEAR_STEP:     return 4u;
    case CC_MENU_CLEAR_BAR:      return 4u;
    case CC_MENU_CLEAR_TRACK:    return 5u;  /* +reset morph */
    case CC_MENU_CLEAR_TRACK_FX: return 6u;  /* +reset morph, send last */
    case CC_MENU_CLEAR_SCENE:    return 11u; /* +reset morph, +reset fx morph, +reload scene */
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

static void ccClear_triggersOffNow(const cc_source_t *object,
                                   uint8_t selection);

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
    if (!ccSvc_enqueue(&job))
        return 0u;
    ccClear_triggersOffNow(object, selection);
    return 1u;
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

/*
 * Turn trigger bits off at acceptance for clear selections whose final state
 * is trigger-off (F1-H). Pool work later republishes the same state; if that
 * work is dropped, the accepted foreground trigger edit remains.
 */
static void ccClear_triggersOffNow(const cc_source_t *object,
                                   uint8_t selection)
{
    uint8_t lo = (object->start < object->end) ? object->start : object->end;
    uint8_t hi = (object->start < object->end) ? object->end : object->start;
    uint8_t t_first = object->track;
    uint8_t t_last = object->track;
    uint8_t s_first = 0u;
    uint8_t s_last = NUM_STEPS - 1u;
    uint16_t written = 0u;
    uint8_t t;
    uint16_t s;

    if (object->kind == CC_KIND_SCENE) {
        if (selection != CC_CLEAR_SCENE_ALL &&
            selection != CC_CLEAR_SCENE_PATTERN &&
            selection != CC_CLEAR_SCENE_NOTES)
            return;
        t_first = 0u;
        t_last = NUM_TRACKS - 1u;
    } else {
        if (selection != CC_CLEAR_ALL && selection != CC_CLEAR_NOTES)
            return;
        if (object->kind == CC_KIND_STEP) {
            s_first = lo;
            s_last = hi;
        } else if (object->kind == CC_KIND_BAR) {
            s_first = (uint8_t)(lo * NUM_STEPS_PER_BAR);
            s_last = (uint8_t)(hi * NUM_STEPS_PER_BAR +
                               NUM_STEPS_PER_BAR - 1u);
        } else if (object->kind != CC_KIND_TRACK) {
            return;
        }
    }
    for (t = t_first; t <= t_last; t++)
        for (s = s_first; s <= s_last; s++) {
            pat_setStepActive(object->scene, t, (uint8_t)s, 0u);
            written++;
        }
    ccSvc_triggersChangedUi(object->scene);
    ccTrace(AUTOSAVE_TRACE_CC_EVT_EARLY_TRIG,
            (uint32_t)((written > 255u) ? 255u : written) |
            ((uint32_t)(object->kind & 7u) << 16u) |
            ((uint32_t)(object->scene & 0xFu) << 19u) |
            ((uint32_t)(object->track & 7u) << 23u));
}

/* ---- immediate clears ---------------------------------------------------- */

void ccClear_fxStepNow(uint8_t scene, uint8_t step)
{
    uint8_t changed;

    if (step >= EFFECT_SEQ_STEP_COUNT)
        return;
    changed = effects_clearSeqLanes(scene, (uint16_t)(1u << step), 0xFFFFu);
    if (changed)
        ccClear_effectRowsChanged(bank_sceneFanoutMask(scene));
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FX_CLEAR,
            (uint32_t)bank_sceneFanoutMask(scene) |
            ((uint32_t)(scene & 0xFu) << 16u) |
            ((uint32_t)(step & 0xFu) << 20u) | (0xFu << 24u) |
            ((uint32_t)(changed ? 1u : 0u) << 28u));
    ccClear_effectUi();
}

/* Trace one pot-register refusal (R9). */
static void ccClear_traceRegRefused(uint16_t target, uint8_t scene,
                                    uint8_t reason)
{
    ccTrace(AUTOSAVE_TRACE_CC_EVT_REG_REFUSED,
            (uint32_t)target | ((uint32_t)(scene & 0xFu) << 16u) |
            ((uint32_t)(reason & 0xFu) << 20u));
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
    if (!has_pattern && !has_lane) {
        ccClear_traceRegRefused(target ? target->pattern_target : 0u, scene, 4u);
        return 0u;
    }
    if (has_pattern && ccSvc_targetPending(target->pattern_target)) {
        ccClear_traceRegRefused(target->pattern_target, scene, 3u);
        return 1u;
    }
    /* A ninth turn, or another Scene's register, does nothing (spec §6). */
    if (has_pattern && ccSvc_registerFull()) {
        ccClear_traceRegRefused(target->pattern_target, scene, 1u);
        return 0u;
    }
    if (has_pattern && ccSvc_registerScene() != 0xFFu &&
        ccSvc_registerScene() != scene) {
        ccClear_traceRegRefused(target->pattern_target, scene, 2u);
        return 0u;
    }
    if (has_lane) {
        uint8_t changed = effects_clearSeqLanes(
            scene, 0xFFFFu, (uint16_t)(1u << target->fx_lane));

        if (changed)
            ccClear_effectRowsChanged(bank_sceneFanoutMask(scene));
        ccTrace(AUTOSAVE_TRACE_CC_EVT_FX_CLEAR,
                (uint32_t)bank_sceneFanoutMask(scene) |
                ((uint32_t)(scene & 0xFu) << 16u) | (0xFu << 20u) |
                ((uint32_t)(target->fx_lane & 0xFu) << 24u) |
                ((uint32_t)(changed ? 1u : 0u) << 28u));
        ccClear_effectUi();
    }
    if (has_pattern && ccSvc_registerAdd(target->pattern_target, scene)) {
        menu_automationTargetCleared(target->pattern_target);
        ccTrace(AUTOSAVE_TRACE_CC_EVT_REG_ADD,
                (uint32_t)target->pattern_target |
                ((uint32_t)(scene & 0xFu) << 16u) |
                ((uint32_t)(ccSvc_registerCount() & 0xFu) << 20u) |
                ((uint32_t)(has_lane ? target->fx_lane : 0xFu) << 24u));
    }
    return 1u;
}

/* ---- queued Scene-level clears (spec §5, §9.9) ------------------------- */

/*
 * `clear send`: both FX-send endpoints 0 and fader `pre` on the track's slot,
 * fanned out over the destination Scene's edit mask (S075 F2-Q9). Clearing
 * the Morph endpoint keeps the send at 0 for every resolved Morph amount.
 */
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
        (void)preset_setVoiceFxSendMorph(m, slot, 0u);
        (void)preset_setVoiceFaderSetting(m, slot, 0u);
    }
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)mask | ((uint32_t)(job->scene & 0xFu) << 16u) |
            (4u << 20u) | ((uint32_t)(slot & 0xFu) << 25u));
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
    ccTrace(AUTOSAVE_TRACE_CC_EVT_MASK_SET,
            (uint32_t)bank_sceneMaskVoiceEditForScene(job->scene) |
            ((uint32_t)(job->scene & 0xFu) << 16u) | (1u << 20u));
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
        ccTrace(AUTOSAVE_TRACE_CC_EVT_MASK_SET,
                (uint32_t)bank_sceneMaskVoiceEditForScene(scene) |
                ((uint32_t)(scene & 0xFu) << 16u) | (1u << 20u));
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
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)written | ((uint32_t)(job->scene & 0xFu) << 16u) |
            (5u << 20u));
    if ((written & ccClear_bit(scene_getActiveIndex())) != 0u)
        menu_repaintAll();
    return CC_RUN_DONE;
}

/* `clear fx sequence`: all 16 steps empty; settings stay; fanned out. */
static uint8_t ccClear_runFxSequence(const cc_job_t *job)
{
    uint8_t changed = effects_clearSeqLanes(job->scene, 0xFFFFu, 0xFFFFu);

    if (changed)
        ccClear_effectRowsChanged(bank_sceneFanoutMask(job->scene));
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FX_CLEAR,
            (uint32_t)bank_sceneFanoutMask(job->scene) |
            ((uint32_t)(job->scene & 0xFu) << 16u) | (0xFu << 20u) |
            (0xFu << 24u) | ((uint32_t)(changed ? 1u : 0u) << 28u));
    ccClear_effectUi();
    return CC_RUN_DONE;
}

/*
 * `clear track reset morph` (S076 P3): equalise one voice slot's Morph
 * endpoints to its current Normal endpoints, plus the voice's correlated Scene
 * morph params, fanned out through the edit mask.
 *
 * What:       for each Scene in the fan-out mask:
 *             (a) preset_resetSlotMorphToNormal() resets the morphable
 *                 instrument descriptors;
 *             (b) preset_setVoiceFxSendMorph() sets the FX-send Morph endpoint
 *                 to that Scene's FX-send Normal endpoint;
 *             (c) when the slot is slot 6 (index 5) the generated
 *                 slot-6/track-7 Morph decay takes the Normal decay;
 *             (d) the instrument row loses its refreshed flag.
 *             When the active Scene is in the mask the Morph worker is
 *             requeued once after all writes.
 * Why:        per-track morph reset is a Scene-child edit, so it fans out like
 *             `clear send` (user F3). No morph amount is touched: equal
 *             endpoints make any Morph amount a no-op.
 * Inputs:     job->scene (destination Scene), job->track (clamped to a slot).
 * Outputs:    CC_RUN_WAIT while the active Scene's apply workers drain, then
 *             CC_RUN_DONE. Side effects: Instrument Morph, FX-send Morph and
 *             Kit Morph-decay AutoSave marks; Morph worker requeue.
 * Accessors:  bank_sceneFanoutMask(), preset_resetSlotMorphToNormal(),
 *             preset_setVoiceFxSendMorph(), scene_getVoiceFxSendAmount(),
 *             scene_setSlot6Track7MorphAmpEnvelopeDecay(),
 *             scene_getSlot6Track7AmpEnvelopeDecay(), preset_rebuildMorph().
 * Affiliates: ccClear_runSend() (fan-out clear model),
 *             ccCopy_runMorphTrack() (analogous copy).
 */
static uint8_t ccClear_runResetMorphTrack(const cc_job_t *job)
{
    uint8_t slot = (job->track < INSTRUMENT_SLOT_COUNT)
                       ? job->track
                       : (uint8_t)(INSTRUMENT_SLOT_COUNT - 1u);
    uint16_t mask = bank_sceneFanoutMask(job->scene);
    uint8_t active = scene_getActiveIndex();
    uint8_t m;

    if ((mask & ccClear_bit(active)) != 0u && !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    for (m = 0u; m < SCENE_COUNT; m++) {
        if ((mask & ccClear_bit(m)) == 0u)
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
    if ((mask & ccClear_bit(active)) != 0u)
        preset_rebuildMorph();
    menu_repaint();
    return CC_RUN_DONE;
}

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

/*
 * `clear scene reset fx morph` (S076 P3): equalise only the Effect's
 * morphable Morph endpoints to their Normal values; fans out through the edit
 * mask (parallels `clear fx`).
 *
 * What:       gates on the active Scene's apply workers, then delegates the
 *             fan-out, morphability check and runtime activation to
 *             effects_resetMorphToNormal().
 * Why:        thin PERF clear wrapper matching ccClear_runFx() with the added
 *             worker wait and the new trace kind.
 * Inputs:     job->scene.
 * Outputs:    CC_RUN_WAIT while the active Scene's apply workers drain, then
 *             CC_RUN_DONE. Side effects: Effect AutoSave marks, Effect row
 *             refreshed flags cleared, runtime reactivated.
 * Accessors:  effects_resetMorphToNormal(), bank_sceneFanoutMask(),
 *             preset_applyWorkersIdle(), ccClear_effectRowsChanged().
 * Affiliates: ccClear_runFx() (model), effects_resetRecord().
 */
static uint8_t ccClear_runResetFxMorph(const cc_job_t *job)
{
    uint16_t written;

    if ((bank_sceneFanoutMask(job->scene) &
         ccClear_bit(scene_getActiveIndex())) != 0u &&
        !preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    written = effects_resetMorphToNormal(job->scene);
    if (written)
        ccClear_effectRowsChanged(written);
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)(written ? written : bank_sceneFanoutMask(job->scene)) |
            ((uint32_t)(job->scene & 0xFu) << 16u) | (9u << 20u));
    if ((written & ccClear_bit(scene_getActiveIndex())) != 0u)
        menu_repaintAll();
    return CC_RUN_DONE;
}

/*
 * `reload scene`: reload this Scene from its HCNAMES source slot on the SD
 * card.
 *
 * What:       reads the Scene's source slot from the filesystem-owned HCNAMES
 *             resident source register. If the source is a valid numeric
 *             library slot (0..999), issues a full Scene load through the
 *             Preset API. If the source is a non-numeric token (INHERIT,
 *             UNKNOWN, DIRECT, PATTERN_AUTOSAVE) or the Preset layer refuses
 *             the request (already busy, filesystem facade unavailable), the
 *             operation is silently dropped.
 * Why:        gives users a fast "revert to saved" from the PERF clear menu
 *             without navigating to the Load page. The Preset path is used
 *             (not a direct filesystem call) because it owns pm_status,
 *             the Scene-load completion callback, Bank-present promotion,
 *             Pattern AutoSave marking, and the menu_pollPresetStatus()
 *             handoff that applies the runtime surface and repaints the
 *             display.
 * Inputs:     job->scene is the clear object's Scene index (0..15).
 * Outputs:    CC_RUN_DONE unconditionally (fire-and-forget). If accepted by
 *             Preset, the load lifecycle proceeds asynchronously: filesystem
 *             reads sceneset.scg, embedded Kit, Pattern, and Effect;
 *             on_scene_load_complete() commits and marks AutoSave dirty;
 *             menu_pollPresetStatus() picks up PRESET_OP_SCENE_LOAD and
 *             calls menu_startSoundApply() with reset_save=0 (because the
 *             active page is not LOAD_PAGE/SAVE_PAGE), applying the Scene
 *             runtime and repainting the current display in place.
 * Accessors:  filesystem_identityRow(FS_ROW_SCENE, scene, 0),
 *             filesystem_residentSource(), FS_RESIDENT_SOURCE_VALUE_MASK,
 *             preset_loadSceneForScenes().
 * Affiliates: on_scene_load_complete() (presetManager.c:467),
 *             menu_pollPresetStatus() PRESET_OP_SCENE_LOAD (menu.c:12288),
 *             menu_startSoundApply() (menu.c:512).
 */
static uint8_t ccClear_runReloadScene(const cc_job_t *job)
{
    uint16_t row = filesystem_identityRow(FS_ROW_SCENE, job->scene, 0u);
    uint16_t src = (uint16_t)(filesystem_residentSource(row) &
                              FS_RESIDENT_SOURCE_VALUE_MASK);

    if (src <= 999u)
        (void)preset_loadSceneForScenes(src, (uint16_t)(1u << job->scene));

    return CC_RUN_DONE;
}

uint8_t ccClear_runJob(const cc_job_t *job)
{
    uint8_t sel;
    uint8_t r;

    if (!job) {
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_BAD_SELECTION, 0u);
        return CC_RUN_DROP;
    }
    sel = (uint8_t)(job->op & CC_JOB_SEL_MASK);
    switch (job->kind) {
    case CC_KIND_STEP:
    case CC_KIND_BAR:
        return ccSvc_runPatternClear(job);
    case CC_KIND_TRACK:
        /*
         * What:       three-way track clear dispatch. CC_CLEAR_RESET_MORPH is
         *             the shared "reset morph" selection (4 in both TRACK
         *             menus); CC_CLEAR_SEND (5) is EFFECTS-only; every other
         *             selection is a Pattern clear.
         * Why:        `send` now sits after `reset morph` in the shared
         *             label array, so both named selections must be tested
         *             before falling through to the Pattern engine.
         * Affiliates: ccClear_runSend(), ccClear_runResetMorphTrack(),
         *             ccClear_trackLabels[].
         */
        if (sel == CC_CLEAR_RESET_MORPH)
            return ccClear_runResetMorphTrack(job);
        if (sel == CC_CLEAR_SEND)
            return ccClear_runSend(job);
        return ccSvc_runPatternClear(job);
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
        case CC_CLEAR_SCENE_RESET_MORPH:    return ccClear_runResetSceneMorph(job);
        case CC_CLEAR_SCENE_RESET_FX_MORPH: return ccClear_runResetFxMorph(job);
        case CC_CLEAR_SCENE_RELOAD:         return ccClear_runReloadScene(job);
        default:
            ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_BAD_SELECTION, sel);
            return CC_RUN_DROP;
        }
    default:
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_BAD_SELECTION, sel);
        return CC_RUN_DROP;
    }
}
