/*
 * copyOps.h — paste rules for Phase 6 copy (spec §4).
 *
 * What: the copy menus (labels, defaults, counts), turning a destination
 * press into a queued paste, automation retargeting and merge rules used by
 * the background service, and the Scene-level paste executors (Instrument,
 * Kit, Effect, Scene settings, Scene, Pattern, FX steps) including edit-mask
 * fan-out and the edit-mask exchange. Why: copy rules are policy; the service
 * owns only sequencing, bounded pool work and the name write. Inputs: the
 * source from copyClearSession, destinations from the router, resident Scene
 * data. Outputs: queued jobs; committed Scene data through SceneData/Preset/
 * EffectsManager; name remaps. Affiliates: copyClearService.h, PatternData.h,
 * SceneData.h, presetManager.h, EffectsManager.h, BankData.h.
 */
#ifndef COPY_OPS_H_
#define COPY_OPS_H_

#include <stdint.h>
#include "copyClearSession.h"
#include "copyClearService.h"
#include "PatternData.h"

/* Step and bar copy selections (bar menus use the same values). */
typedef enum {
    CC_COPY_ALL = 0u,
    CC_COPY_MERGE_ALL,
    CC_COPY_AUTO,
    CC_COPY_MERGE_AUTO
} cc_copy_step_sel_t;
typedef enum { CC_COPY_TRACK = 0u, CC_COPY_INSTRUMENT } cc_copy_track_sel_t;
typedef enum {
    CC_COPY_SCENE = 0u,
    CC_COPY_SCENE_SETTINGS,
    CC_COPY_KIT,
    CC_COPY_EFFECT,
    CC_COPY_PATTERN
} cc_copy_scene_sel_t;

/*
 * Copy menus (spec §4.1, §8.1). ccCopy_menuForSource(): the menu of a source
 * kind. ccCopy_selectionCount(): entries (default is entry 0).
 * ccCopy_label(): the 14-character padded label of one entry ("" when out of
 * range). Caller: copyClearSession.c.
 */
cc_menu_t ccCopy_menuForSource(const cc_source_t *src);
uint8_t ccCopy_selectionCount(cc_menu_t menu);
const char *ccCopy_label(cc_menu_t menu, uint8_t selection);

/*
 * Queue one paste. Inputs: the operation's source, the selection shown at
 * the press, and the destination Scene/track/start (absolute step, bar,
 * track, Scene or FX step as the source kind requires). Output: nonzero when
 * the job was queued; zero when it was a no-op (identical to the source) or
 * the queue was full (dropped silently, spec §3.1). Accepted step/bar/track
 * replace and merge-all pastes write trigger bits immediately; an identical
 * paste is traced as PASTE_NOOP in DEV builds. Caller: the router.
 */
uint8_t ccCopy_requestPaste(const cc_source_t *src, uint8_t selection,
                            uint8_t dst_scene, uint8_t dst_track,
                            uint8_t dst_start);

/*
 * Retarget automation for a different destination track or Scene (spec §9.7).
 *
 * What: maps one source target ID to the destination: the source track's own
 * slot moves to the destination track's slot (track 7 = slot 6); other slots
 * stay; per-voice Scene targets (Nvm, Nou, Nfx) move with the slot; `7dc` and
 * `_choke` parameters recorded on track 7 map only to the destination track
 * 7's alternate decay; Scene-wide targets and the off value stay; the retired
 * `srt` row drops; Effect locals map by key when the Effect types differ.
 * Instrument parameters match by same type and index, else same file key,
 * else same VOICE page position; the match must be automatable and have the
 * same dtype, otherwise the entry is dropped. Why: user rule (A3, F8): best
 * effort, a slightly different parameter is fine. Inputs: context
 * (source/destination Scene and track; slots derive from tracks), source
 * target. Outputs: 1 + destination target, or 0 (drop).
 * ccCopy_retargetEntries() applies it to a decoded list in place, removes
 * dropped entries, keeps the first of any duplicates, and returns the new
 * count. ccCopy_retargetNeeded() is zero when the mapping is the identity
 * (same Scene and slot, or identical types). Clients: copyClearService.c
 * snapshot and whole-Pattern rewrite.
 */
typedef struct {
    uint8_t src_scene;
    uint8_t dst_scene;
    uint8_t src_track;
    uint8_t dst_track;
} cc_retarget_ctx_t;
uint8_t ccCopy_retarget(const cc_retarget_ctx_t *ctx, uint16_t src_target,
                        uint16_t *dst_target);
uint8_t ccCopy_retargetEntries(const cc_retarget_ctx_t *ctx,
                               pat_automation_entry_t *autos, uint8_t count);
uint8_t ccCopy_retargetNeeded(const cc_retarget_ctx_t *ctx);
/* Nonzero when any slot type or the Effect type of two Scenes differs. */
uint8_t ccCopy_sceneTypesDiffer(uint8_t scene_a, uint8_t scene_b);

/*
 * Build the destination block for one step under a selection (spec §4.4).
 *
 * What: `copy … all` takes the source block and trigger; `merge … all`
 * combines triggers (OR), lets each source special win and keeps
 * destination-only specials, and forms the automation union with the source
 * winning on equal targets, source entries first, destination entries beyond
 * 63 dropped silently; `copy … automation` keeps the destination specials and
 * trigger and takes the source automation and probability special; `merge
 * automation` keeps all destination specials and forms the union. An empty
 * source step under a merge changes nothing. Inputs: selection, the (already
 * retargeted) source block or NULL, the source trigger, the live destination
 * block or NULL. Outputs: encoded block bytes, *trigger_mode and *skip. Uses
 * two 63-entry stack lists (~504 B). Caller: copyClearService.c check/place.
 */
uint8_t ccCopy_buildStep(uint8_t selection, const uint8_t *src_block,
                         uint8_t src_trigger, const uint8_t *dst_block,
                         uint8_t out[PAT_RAW_BLOCK_MAX], uint8_t *trigger_mode,
                         uint8_t *skip);

/*
 * Run one queued paste (the job at the queue head). Dispatches by source
 * kind and selection: Pattern pastes to the service engine, `copy
 * instrument`, the PERF Scene pastes and FX step pastes to the executors
 * below. Returns CC_RUN_DONE, CC_RUN_WAIT or CC_RUN_DROP. Caller: ccSvc_tick().
 *
 * Scene-level executors (spec §9.9) wait (CC_RUN_WAIT) while
 * preset_applyWorkersIdle() is zero before writing anything that touches the
 * active Scene, and after starting an apply keep waiting until it is done.
 * Fan-out uses bank_sceneFanoutMask() of the destination Scene (user F3/F5).
 * Scene-level data commits precede a final name-remap phase, which waits for
 * ccSvc_namesReady() and therefore never delays the data commit.
 */
uint8_t ccCopy_runJob(const cc_job_t *job);

#endif /* COPY_OPS_H_ */
