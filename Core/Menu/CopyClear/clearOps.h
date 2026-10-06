/*
 * clearOps.h — clear rules for Phase 6 clear (spec §5, §6).
 *
 * What: the clear menus (labels, counts; first object of a group opens at
 * `cancel`), the
 * object-to-menu mapping per mode, turning a released object into a queued
 * clear, the immediate EFFECTS SEQ clear, the pot-clear front end, and the
 * Scene-level clear executors (Scene, Scene settings, Effect, FX sequence,
 * send, reset morph, reset fx morph). Why: clear rules are policy; the service
 * owns sequencing and pool
 * work. Inputs: held objects from copyClearSession, pot targets from Menu.
 * Outputs: queued jobs, immediate FX sequence clears, register entries.
 * Affiliates: copyClearService.h, EffectsManager.h, SceneData.h,
 * presetManager.h, BankData.h, menu.h.
 */
#ifndef CLEAR_OPS_H_
#define CLEAR_OPS_H_

#include <stdint.h>
#include "copyClearSession.h"
#include "copyClearService.h"

/* Step, bar and track clear selections. `send` is EFFECTS TRACK only. NOTES
 * turns triggers off, removes note/velocity specials, and keeps probability
 * plus automation (F1-J).
 *
 * What:       CC_CLEAR_RESET_MORPH (4) is the "reset morph" track-level clear,
 *             visible in both the VOICE and the EFFECTS TRACK menus. It
 *             equalises a single voice slot's morph endpoints to its current
 *             Normal endpoints, plus the voice's correlated Scene morph
 *             endpoints (FX send morph, Kit slot-6 decay morph), fanning out
 *             through the edit mask.
 * Why:        extends cc_clear_obj_sel_t so the menu, the request path and the
 *             dispatch table can address the new selection. It is placed before
 *             CC_CLEAR_SEND so one shared ccClear_trackLabels[] array serves
 *             both menus: the VOICE count stops at "reset morph", the EFFECTS
 *             count appends "send". CC_CLEAR_SEND therefore moves from 4 to 5.
 * Inputs:     copyClearSession.c (menu selection), ccClear_requestClear(),
 *             ccClear_runJob() dispatch.
 * Outputs:    none (enum constant).
 * Accessors:  ccClear_selectionCount(), ccClear_label(), ccClear_runJob().
 * Affiliates: CC_CLEAR_SCENE_RESET_MORPH below, CC_COPY_MORPH in copyOps.h.
 */
typedef enum {
    CC_CLEAR_CANCEL = 0u,
    CC_CLEAR_ALL,
    CC_CLEAR_AUTO,
    CC_CLEAR_NOTES,
    CC_CLEAR_RESET_MORPH,   /* 4 — VOICE and EFFECTS TRACK menus */
    CC_CLEAR_SEND           /* 5 — EFFECTS TRACK only */
} cc_clear_obj_sel_t;
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
typedef enum {
    CC_CLEAR_SCENE_CANCEL = 0u,
    CC_CLEAR_SCENE_ALL,
    CC_CLEAR_SCENE_SETTINGS,
    CC_CLEAR_SCENE_PATTERN,
    CC_CLEAR_SCENE_AUTOMATION,
    CC_CLEAR_SCENE_NOTES,
    CC_CLEAR_SCENE_FX,
    CC_CLEAR_SCENE_FX_SEQUENCE,
    CC_CLEAR_SCENE_RESET_MORPH,     /* 8 */
    CC_CLEAR_SCENE_RESET_FX_MORPH   /* 9 */
} cc_clear_scene_sel_t;

/*
 * Clear menus (spec §5, §8.1). ccClear_menuForObject(): the menu for an
 * object kind in a mode (SELECT_MODE_*), CC_MENU_NONE when the object has
 * no menu (EFFECTS SEQ clears at once). ccClear_selectionCount() and
 * ccClear_label() as in copyOps.h. Caller: copyClearSession.c.
 */
cc_menu_t ccClear_menuForObject(uint8_t mode, cc_kind_t kind);
uint8_t ccClear_selectionCount(cc_menu_t menu);
const char *ccClear_label(cc_menu_t menu, uint8_t selection);

/*
 * Queue one clear for a released object (spec §5).
 * Inputs: the held object and the selection shown at release. Output:
 * nonzero when queued; zero for `cancel` or a full queue (dropped silently).
 * On acceptance, trigger bits are turned off immediately for selections whose
 * final state is trigger-off, and step LEDs repaint (F1-H). Caller: router.
 */
uint8_t ccClear_requestClear(const cc_source_t *object, uint8_t selection);

/*
 * Clear FX sequence steps or the whole Effect (spec §5).
 *
 * ccClear_fxStepNow(): EFFECTS SEQ — that step's locks and lane values
 * return to empty at once, fanning out through the edit mask (user,
 * confirm 1); the Effect page LEDs and display refresh. Callers: the router.
 */
void ccClear_fxStepNow(uint8_t scene, uint8_t step);

/*
 * Start one pot clear (spec §6).
 *
 * What: clears the parameter's FX sequence lane at once (fanning out through
 * the edit mask) and registers its Pattern target for background removal
 * from the viewed Scene's Pattern (never fanned out). Why: user rules B14, F4.
 * Inputs: resolved targets. Output: nonzero when something was cleared or
 * registered; zero when the register is full or holds another Scene's
 * entries (nothing happens; the underline stays).
 * Caller: copyClear_potTurned(). Affiliates: ccSvc_registerAdd(),
 * effects_clearSeqLanes(), menu_automationTargetCleared().
 */
uint8_t ccClear_potTurned(const cc_pot_target_t *target);

/*
 * Run one queued clear (the job at the queue head). Pattern clears go to
 * the service engine; `clear send`, `clear scene`, `clear scene settings`,
 * `clear fx`, `clear fx sequence`, `clear track reset morph`,
 * `clear scene reset morph` and `clear scene reset fx morph` run here
 * (spec §5, §9.9). Returns CC_RUN_DONE, CC_RUN_WAIT or CC_RUN_DROP.
 * Caller: ccSvc_tick().
 */
uint8_t ccClear_runJob(const cc_job_t *job);

#endif /* CLEAR_OPS_H_ */
