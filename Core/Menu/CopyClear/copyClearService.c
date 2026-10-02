/*
 * copyClearService.c — background service for Phase 6 copy and clear.
 *
 * Contract in copyClearService.h. One job runs at a time from the head of a
 * four-entry FIFO; when no job is queued the pot-clear register drains one
 * target per pass; when interaction has ended and both are empty, the one
 * HCNAMES write runs, the 9 kB name buffer is returned and the session is
 * told that the operation is complete (which ends the AutoSave / Pattern
 * maintenance suspension). Every Pattern write runs under
 * patSvc_beginExclusive() on exactly one Scene and keeps publication order
 * through the raw block API (spec §9.4–§9.8).
 */

#include "copyClearService.h"
#include "copyClearSession.h"
#include "copyOps.h"
#include "clearOps.h"
#include "PatternData.h"
#include "PatternStackService.h"
#include "filesystem.h"
#include "Autosave.h"
#include "buttonHandler.h"
#include "ledHandler.h"
#include "menu.h"
#include <string.h>

#define CC_QUEUE_SIZE         4u
#define CC_REGISTER_SIZE      8u
#define CC_NO_SCENE           0xFFu
#define CC_STEPS_PER_TICK     8u
#define CC_SNAPSHOT_PER_TICK  16u
#define CC_REGION_PER_TICK    32u
/* Name write: give up after 2 s of refused requests (500 Hz ticks). */
#define CC_NAME_RETRY_TICKS   1000u
/* remap[] value for "content changed, name kept" (never a row number). */
#define CC_REMAP_CHANGED      0xFEu
#define CC_REMAP_NONE         0xFFu

_Static_assert(CC_SCRATCH_REMAP_OFFSET + FS_HCNAMES_ROW_COUNT <=
                   CC_SCRATCH_TABLE_OFFSET,
               "remap must end before the paste table");
_Static_assert(CC_SCRATCH_BLOCK_OFFSET + PAT_POOL_ALLOC_CHUNKS * 4u <=
                   FS_NAME_SCRATCH_BYTES,
               "copy/clear scratch must fit the name buffer");

/* FIFO of pending pastes/clears: 4 x 6 B plus head and count (26 B). */
static cc_job_t ccSvc_queue[CC_QUEUE_SIZE];
static uint8_t ccSvc_head;
static uint8_t ccSvc_count;

/*
 * Pot-clear register (spec §6; 18 B, approved).
 *
 * What: up to eight Pattern targets waiting for removal from one Scene,
 * drained one at a time (about 224 ms each at 8 steps per tick).
 * Why: a Pattern-wide removal is bounded background work; the register keeps
 * the underline off until the target is gone and limits outstanding work.
 * Inputs: ccSvc_registerAdd(). Outputs: removed automation entries.
 * Accessors: ccClear_potTurned(), va_scanService() (ccSvc_targetPending()),
 * ccSvc_busy().
 */
static uint16_t ccSvc_regTarget[CC_REGISTER_SIZE];
static uint8_t ccSvc_regCount;
static uint8_t ccSvc_regScene = CC_NO_SCENE;

/* Run state of the job (or register pass) in progress (6 B). */
static cc_run_t ccSvc_runState;

/* ccSvc_flags bits (1 B). */
#define CC_SVC_JOB_ACTIVE   0x01u  /* queue head is running                 */
#define CC_SVC_REG_ACTIVE   0x02u  /* register pass is running              */
#define CC_SVC_ENDED        0x04u  /* interaction ended; teardown allowed   */
#define CC_SVC_NAMES_DIRTY  0x08u  /* an HCNAMES write is needed            */
#define CC_SVC_NAMES_BUSY   0x10u  /* the HCNAMES write is in flight        */
static uint8_t ccSvc_flags;
/* Scene held through patSvc_beginExclusive(), or CC_NO_SCENE (1 B). */
static uint8_t ccSvc_claimScene = CC_NO_SCENE;
/* Refused name-write requests so far (2 B). */
static uint16_t ccSvc_nameRetry;
/* Borrowed 9 kB name buffer, NULL while not borrowed (4 B). */
static uint8_t *ccSvc_buf;

/* ---- claim, scratch, UI helpers --------------------------------------- */

/* Take (or keep) the exclusive boundary on one Scene; nonzero when held. */
static uint8_t ccSvc_claim(uint8_t scene)
{
    if (ccSvc_claimScene != CC_NO_SCENE && ccSvc_claimScene != scene)
        return 0u;
    ccSvc_claimScene = scene;
    return patSvc_beginExclusive(scene);
}

/* Release the boundary (no-op when none is held). */
static void ccSvc_release(void)
{
    if (ccSvc_claimScene == CC_NO_SCENE)
        return;
    patSvc_endExclusive(ccSvc_claimScene);
    ccSvc_claimScene = CC_NO_SCENE;
}

/* Borrow the name buffer once; the remap starts empty. */
static uint8_t ccSvc_ensureScratch(void)
{
    if (ccSvc_buf)
        return 1u;
    ccSvc_buf = filesystem_borrowNameCacheScratch();
    if (!ccSvc_buf)
        return 0u;
    memset(&ccSvc_buf[CC_SCRATCH_REMAP_OFFSET], CC_REMAP_NONE,
           FS_HCNAMES_ROW_COUNT);
    return 1u;
}

void ccSvc_patternChangedUi(uint8_t scene, uint8_t track)
{
    uint8_t mode = buttonHandler_getMode();

    if (mode == SELECT_MODE_PERF)
        menu_refreshPerfSceneLeds();
    if (scene != menu_getViewedPattern())
        return;
    menu_patternContentChanged();
    if (mode == SELECT_MODE_VOICE || mode == SELECT_MODE_STEP)
        led_updatePatternTrack(menu_getActiveVoice(), scene,
                               buttonHandler_selectedStep);
    if (track < NUM_TRACKS && track == menu_getActiveVoice())
        pat_applyTrackSettingsToMenu(scene, track);
}

/*
 * Rewrite one step to a block that is never larger than the live one, in
 * publication order (spec §9.6): place below the reserve when a run exists,
 * else through the swap block (publish, free the old run, return it to the
 * freed run). Returns CC_RUN_DONE when the step is final, CC_RUN_WAIT when
 * the swap-return must be retried (run->aux bit 0 set).
 */
static uint8_t ccSvc_rewriteShrink(uint8_t scene, uint8_t track, uint8_t step,
                                   const uint8_t *block, uint8_t bytes,
                                   uint8_t trigger_mode)
{
    if (bytes == 0u) {
        pat_rawPublishEmpty(scene, track, step, trigger_mode);
        return CC_RUN_DONE;
    }
    if (pat_rawPlace(scene, track, step, block, trigger_mode))
        return CC_RUN_DONE;
    if (!pat_rawPlaceViaSwap(scene, track, step, block, trigger_mode))
        return CC_RUN_DONE;    /* swap occupied: impossible after evacuation */
    if (pat_rawSwapReturn(scene, track, step))
        return CC_RUN_DONE;
    ccSvc_runState.aux |= 1u;
    return CC_RUN_WAIT;
}

/* Retry a pending swap-return; compaction runs between attempts. */
static uint8_t ccSvc_swapReturnPending(uint8_t scene, uint8_t track,
                                       uint8_t step)
{
    if (pat_rawSwapReturn(scene, track, step) ||
        !patSvc_exclusiveCompactStep(scene)) {
        /* Done, or nothing left to move: leave the step where it is (the
         * next claim's evacuation moves it once room exists). */
        ccSvc_runState.aux &= (uint16_t)~1u;
        return CC_RUN_DONE;
    }
    return CC_RUN_WAIT;
}

/* Claim phase shared by the engines: claim, then empty the swap block. */
static uint8_t ccSvc_claimAndEvacuate(uint8_t scene)
{
    uint8_t r;

    if (!ccSvc_claim(scene))
        return CC_RUN_WAIT;
    r = patSvc_exclusiveEvacuateSwapStep(scene);
    if (r == 1u)
        return CC_RUN_WAIT;
    if (r == 2u) {
        ccSvc_release();
        return CC_RUN_DROP;
    }
    return CC_RUN_DONE;
}

/* ---- Pattern paste engine (spec §9.5) ---------------------------------- */

/* Geometry of a step/range/bar/track paste. */
typedef struct {
    uint8_t src_first;   /* first source step                         */
    int8_t dir;          /* +1 or -1                                  */
    uint8_t count;       /* number of steps, 1..128 (0 = none)        */
    uint8_t dst_first;   /* first destination step                    */
    uint8_t selection;   /* cc_copy_step_sel_t                        */
} cc_paste_geo_t;

static void ccSvc_pasteGeometry(const cc_job_t *job, cc_paste_geo_t *g)
{
    const cc_source_t *src = copyClear_source();
    uint8_t lo = (src->start < src->end) ? src->start : src->end;
    uint8_t hi = (src->start < src->end) ? src->end : src->start;

    g->selection = (uint8_t)(job->op & CC_JOB_SEL_MASK);
    switch (job->kind) {
    case CC_KIND_STEP:
        g->src_first = src->start;
        g->dir = (src->start <= src->end) ? 1 : -1;
        g->count = (uint8_t)(hi - lo + 1u);
        g->dst_first = job->start;
        break;
    case CC_KIND_BAR:
        /* Bar ranges reverse completely (spec §4.2). */
        if (src->start <= src->end) {
            g->src_first = (uint8_t)(src->start * NUM_STEPS_PER_BAR);
            g->dir = 1;
        } else {
            g->src_first = (uint8_t)(src->start * NUM_STEPS_PER_BAR +
                                     NUM_STEPS_PER_BAR - 1u);
            g->dir = -1;
        }
        g->count = (uint8_t)((hi - lo + 1u) * NUM_STEPS_PER_BAR);
        g->dst_first = (uint8_t)(job->start * NUM_STEPS_PER_BAR);
        break;
    default:            /* `copy track` */
        g->src_first = 0u;
        g->dir = 1;
        g->count = NUM_STEPS;
        g->dst_first = 0u;
        g->selection = CC_COPY_ALL;
        break;
    }
}

static uint8_t ccSvc_srcStep(const cc_paste_geo_t *g, uint8_t i)
{
    return (uint8_t)((int16_t)g->src_first + (int16_t)g->dir * (int16_t)i);
}

static uint8_t ccSvc_dstStep(const cc_paste_geo_t *g, uint8_t i)
{
    return (uint8_t)((g->dst_first + i) & (NUM_STEPS - 1u));
}

/* Snapshot table entry layout (u16 at offset 256 + 2*i). */
#define CC_SNAP_TRIGGER  0x8000u
#define CC_SNAP_BLOCK    0x4000u
#define CC_SNAP_OFFSET   0x07FFu

static uint16_t *ccSvc_table(void)
{
    return (uint16_t *)(void *)&ccSvc_buf[CC_SCRATCH_TABLE_OFFSET];
}

/* Snapshot block of entry i, or NULL. */
static const uint8_t *ccSvc_snapBlock(uint16_t entry)
{
    if ((entry & CC_SNAP_BLOCK) == 0u)
        return 0;
    return &ccSvc_buf[CC_SCRATCH_BLOCK_OFFSET +
                      (uint16_t)(entry & CC_SNAP_OFFSET) * 4u];
}

/*
 * Build destination step i of a paste from the snapshot and the live
 * destination. Outputs: bytes (0 = no block), trigger mode, skip flag, and
 * the live destination block size.
 */
static uint8_t ccSvc_pasteBuild(const cc_job_t *job, const cc_paste_geo_t *g,
                                uint8_t i, uint8_t out[PAT_RAW_BLOCK_MAX],
                                uint8_t *mode, uint8_t *skip,
                                uint8_t *old_bytes)
{
    uint8_t dst_block[PAT_RAW_BLOCK_MAX];
    uint16_t entry = ccSvc_table()[i];
    uint16_t live;

    *old_bytes = pat_rawReadBlock(job->scene, job->track, ccSvc_dstStep(g, i),
                                  dst_block, &live);
    return ccCopy_buildStep(g->selection, ccSvc_snapBlock(entry),
                            (uint8_t)((entry & CC_SNAP_TRIGGER) != 0u),
                            *old_bytes ? dst_block : 0, out, mode, skip);
}

/*
 * Run one step/range/bar/track paste in bounded steps (spec §9.5).
 *
 * What: snapshot the source into the borrowed buffer (retargeted), check that
 * every step fits, then replace each destination step in publication order,
 * using the swap block for steps that do not grow and sliding compaction for
 * steps that grow, so a paste either completes or is dropped before any
 * change. The check is: a growing step needs free chunks >= its new size
 * before its old block is freed (the new block coexists with the old one);
 * a step that does not grow always fits through the swap block. Sliding
 * compaction makes all free space one run, so a growing step that passed the
 * check always finds its run. Why: user rule "always step by step"; the
 * reserve makes every non-growing step placeable. Inputs: the queue-head
 * job, the operation's source. Outputs: committed steps, track length or
 * settings, UI refresh. Returns DONE, WAIT or DROP. Run state: sub = phase,
 * cursor = step index, aux = snapshot chunk cursor / free-chunk count / bit 0
 * swap-return pending. Caller: copyOps.c ccCopy_runJob().
 */
uint8_t ccSvc_runPatternPaste(const cc_job_t *job)
{
    cc_run_t *run = &ccSvc_runState;
    const cc_source_t *src = copyClear_source();
    cc_paste_geo_t g;
    uint8_t block[PAT_RAW_BLOCK_MAX];
    uint8_t n;

    ccSvc_pasteGeometry(job, &g);
    if (g.count == 0u || !ccSvc_buf)
        return CC_RUN_DROP;

    switch (run->sub) {
    case 0u: /* CLAIM + EVACUATE */
    {
        uint8_t r = ccSvc_claimAndEvacuate(job->scene);

        if (r != CC_RUN_DONE)
            return r;
        run->sub = 1u;
        run->cursor = 0u;
        run->aux = 0u;
        return CC_RUN_WAIT;
    }
    case 1u: /* SNAPSHOT (source is read live, spec §12) */
    {
        cc_retarget_ctx_t ctx;
        uint8_t retarget;

        ctx.src_scene = src->scene;
        ctx.dst_scene = job->scene;
        ctx.src_track = src->track;
        ctx.dst_track = job->track;
        retarget = ccCopy_retargetNeeded(&ctx);
        for (n = 0u; n < CC_SNAPSHOT_PER_TICK && run->cursor < g.count; n++) {
            uint8_t i = (uint8_t)run->cursor;
            uint16_t live;
            uint8_t bytes = pat_rawReadBlock(src->scene, src->track,
                                             ccSvc_srcStep(&g, i), block,
                                             &live);
            uint16_t entry = (uint16_t)((live & PAT_ADDR_TRIGGER_BIT)
                                            ? CC_SNAP_TRIGGER : 0u);

            if (bytes != 0u && retarget) {
                pat_step_specials_t sp;
                pat_automation_entry_t autos[PAT_BLOCK_AUTO_COUNT_MASK];
                uint8_t count = pat_rawDecode(block, &sp, autos,
                                              PAT_BLOCK_AUTO_COUNT_MASK);

                count = ccCopy_retargetEntries(&ctx, autos, count);
                bytes = pat_rawEncode(block, sp.flags, sp.note, sp.velocity,
                                      sp.probability, autos, count);
            }
            if (bytes != 0u) {
                memcpy(&ccSvc_buf[CC_SCRATCH_BLOCK_OFFSET +
                                  (uint16_t)run->aux * 4u], block, bytes);
                entry = (uint16_t)(entry | CC_SNAP_BLOCK |
                                   (run->aux & CC_SNAP_OFFSET));
                run->aux = (uint16_t)(run->aux + bytes / 4u);
            }
            ccSvc_table()[i] = entry;
            run->cursor++;
        }
        if (run->cursor < g.count)
            return CC_RUN_WAIT;
        run->sub = 2u;
        run->cursor = 0u;
        run->aux = pat_rawFreeChunks(job->scene);
        return CC_RUN_WAIT;
    }
    case 2u: /* CHECK (nothing has changed yet) */
        for (n = 0u; n < CC_SNAPSHOT_PER_TICK && run->cursor < g.count; n++) {
            uint8_t mode;
            uint8_t skip;
            uint8_t old_bytes;
            uint8_t new_c = (uint8_t)(ccSvc_pasteBuild(job, &g,
                                                       (uint8_t)run->cursor,
                                                       block, &mode, &skip,
                                                       &old_bytes) / 4u);
            uint8_t old_c = (uint8_t)(old_bytes / 4u);

            run->cursor++;
            if (skip)
                continue;
            if (new_c > old_c) {
                if (run->aux < new_c) {
                    ccSvc_release();
                    return CC_RUN_DROP;
                }
                run->aux = (uint16_t)(run->aux - new_c + old_c);
            } else {
                run->aux = (uint16_t)(run->aux + old_c - new_c);
            }
        }
        if (run->cursor < g.count)
            return CC_RUN_WAIT;
        run->sub = 3u;
        run->cursor = 0u;
        run->aux = 0u;
        return CC_RUN_WAIT;
    case 3u: /* PLACE, publication order */
        for (n = 0u; n < CC_STEPS_PER_TICK && run->cursor < g.count; n++) {
            uint8_t i = (uint8_t)run->cursor;
            uint8_t ds = ccSvc_dstStep(&g, i);
            uint8_t mode;
            uint8_t skip;
            uint8_t old_bytes;
            uint8_t bytes;

            if ((run->aux & 1u) != 0u) {
                if (ccSvc_swapReturnPending(job->scene, job->track, ds) ==
                    CC_RUN_WAIT)
                    return CC_RUN_WAIT;
                run->cursor++;
                continue;
            }
            bytes = ccSvc_pasteBuild(job, &g, i, block, &mode, &skip,
                                     &old_bytes);
            if (skip) {
                run->cursor++;
                continue;
            }
            if (bytes > old_bytes) {
                /* Growing step: needs a run of the new size (spec §9.5). */
                if (pat_rawPlace(job->scene, job->track, ds, block, mode)) {
                    run->cursor++;
                    continue;
                }
                if (patSvc_exclusiveCompactStep(job->scene))
                    return CC_RUN_WAIT;
                run->cursor++;     /* cannot happen after the check */
                continue;
            }
            if (ccSvc_rewriteShrink(job->scene, job->track, ds, block, bytes,
                                    mode) == CC_RUN_WAIT)
                return CC_RUN_WAIT;
            run->cursor++;
        }
        if (run->cursor < g.count)
            return CC_RUN_WAIT;
        run->sub = 4u;
        return CC_RUN_WAIT;
    default: /* FINISH: length extension or track settings */
    {
        const pat_scene_region_t *dst = pat_sceneRegion(job->scene);

        if (job->kind == CC_KIND_TRACK) {
            const pat_scene_region_t *s = pat_sceneRegion(src->scene);

            if (s) {
                pat_setTrackLength(job->scene, job->track,
                                   s->track_length[src->track]);
                pat_setTrackScale(job->scene, job->track,
                                  s->track_scale[src->track]);
                pat_setTrackShuffle(job->scene, job->track,
                                    s->track_shuffle[src->track]);
            }
        } else if (dst) {
            uint16_t last = (uint16_t)g.dst_first + g.count - 1u;
            uint8_t bar = (last >= NUM_STEPS)
                              ? (uint8_t)(NUM_BARS - 1u)
                              : (uint8_t)(last / NUM_STEPS_PER_BAR);
            uint8_t need = (uint8_t)(NUM_STEPS_PER_BAR * (bar + 1u));

            /* A paste never shortens a track (spec §4.4). */
            if (dst->track_length[job->track] < need)
                pat_setTrackLength(job->scene, job->track, need);
        }
        ccSvc_release();
        ccSvc_patternChangedUi(job->scene, job->track);
        return CC_RUN_DONE;
    }
    }
}

/* ---- Pattern clear engine (spec §5) ------------------------------------ */

/* First/count of a clear in flat step indices (track*128 + step). */
static void ccSvc_clearRange(const cc_job_t *job, uint16_t *first,
                             uint16_t *count)
{
    uint8_t lo = (job->start < job->end) ? job->start : job->end;
    uint8_t hi = (job->start < job->end) ? job->end : job->start;
    uint16_t base = (uint16_t)job->track * NUM_STEPS;

    switch (job->kind) {
    case CC_KIND_STEP:
        *first = (uint16_t)(base + lo);
        *count = (uint16_t)(hi - lo + 1u);
        break;
    case CC_KIND_BAR:
        *first = (uint16_t)(base + lo * NUM_STEPS_PER_BAR);
        *count = (uint16_t)((hi - lo + 1u) * NUM_STEPS_PER_BAR);
        break;
    case CC_KIND_TRACK:
        *first = base;
        *count = NUM_STEPS;
        break;
    default:            /* PERF Scene: all seven tracks */
        *first = 0u;
        *count = PAT_STEPS_PER_SCENE;
        break;
    }
}

/* Map a Scene clear selection onto the step selections. */
static uint8_t ccSvc_clearSelection(const cc_job_t *job)
{
    uint8_t sel = (uint8_t)(job->op & CC_JOB_SEL_MASK);

    if (job->kind != CC_KIND_SCENE)
        return sel;
    if (sel == CC_CLEAR_SCENE_AUTOMATION)
        return CC_CLEAR_AUTO;
    if (sel == CC_CLEAR_SCENE_NOTES)
        return CC_CLEAR_NOTES;
    return CC_CLEAR_ALL;
}

/*
 * Clear one step: `all` empties it (trigger off), `automation` strips the
 * automation and keeps trigger and specials, `notes` turns the trigger off
 * and strips the specials, keeping the automation. Rewrites never grow, so
 * they use the swap path. Returns CC_RUN_DONE or CC_RUN_WAIT (swap-return
 * pending).
 */
static uint8_t ccSvc_clearStep(uint8_t scene, uint8_t track, uint8_t step,
                               uint8_t sel)
{
    uint8_t block[PAT_RAW_BLOCK_MAX];
    pat_step_specials_t sp;
    pat_automation_entry_t autos[PAT_BLOCK_AUTO_COUNT_MASK];
    uint16_t live;
    uint8_t bytes;
    uint8_t count;

    if (sel == CC_CLEAR_ALL) {
        uint16_t entry;

        (void)pat_rawReadBlock(scene, track, step, block, &entry);
        if (entry != PAT_ADDR_SENTINEL)
            pat_rawPublishEmpty(scene, track, step, PAT_RAW_TRIGGER_OFF);
        return CC_RUN_DONE;
    }
    bytes = pat_rawReadBlock(scene, track, step, block, &live);
    if (bytes == 0u) {
        if (sel == CC_CLEAR_NOTES && (live & PAT_ADDR_TRIGGER_BIT) != 0u)
            pat_rawPublishEmpty(scene, track, step, PAT_RAW_TRIGGER_OFF);
        return CC_RUN_DONE;
    }
    count = pat_rawDecode(block, &sp, autos, PAT_BLOCK_AUTO_COUNT_MASK);
    if (sel == CC_CLEAR_AUTO) {
        if (count == 0u)
            return CC_RUN_DONE;
        bytes = pat_rawEncode(block, sp.flags, sp.note, sp.velocity,
                              sp.probability, 0, 0u);
        return ccSvc_rewriteShrink(scene, track, step, block, bytes,
                                   PAT_RAW_TRIGGER_KEEP);
    }
    /* notes */
    bytes = pat_rawEncode(block, 0u, 0u, 0u, 0u, autos, count);
    return ccSvc_rewriteShrink(scene, track, step, block, bytes,
                               PAT_RAW_TRIGGER_OFF);
}

/*
 * Run one Pattern clear in bounded steps (spec §5, §9.6).
 *
 * What: empties steps (all), strips automation (automation) or triggers and
 * specials (notes) over a step range, a track, or the whole Pattern; and
 * resets track settings for `clear track`. Every rewrite is
 * publication-safe and uses the swap block when no other run is free; `clear
 * … all` releases blocks only. Why: user clear rules; Pattern clears never
 * fan out. Inputs: the queue-head job (object Scene, track, range,
 * selection). Outputs: committed steps, track settings, UI refresh. Returns
 * DONE, WAIT or DROP (a pre-S075 block in the swap block that cannot move
 * drops a rewriting clear, spec §9.6). Caller: clearOps.c ccClear_runJob().
 */
uint8_t ccSvc_runPatternClear(const cc_job_t *job)
{
    cc_run_t *run = &ccSvc_runState;
    uint8_t sel = ccSvc_clearSelection(job);
    uint16_t first;
    uint16_t count;
    uint8_t n;

    ccSvc_clearRange(job, &first, &count);
    switch (run->sub) {
    case 0u:
    {
        uint8_t r = (sel == CC_CLEAR_ALL) ?
                        (ccSvc_claim(job->scene) ? CC_RUN_DONE : CC_RUN_WAIT) :
                        ccSvc_claimAndEvacuate(job->scene);

        if (r != CC_RUN_DONE)
            return r;
        run->sub = 1u;
        run->cursor = 0u;
        run->aux = 0u;
        return CC_RUN_WAIT;
    }
    case 1u:
        for (n = 0u; n < CC_STEPS_PER_TICK && run->cursor < count; n++) {
            uint16_t flat = (uint16_t)(first + run->cursor);
            uint8_t t = (uint8_t)(flat / NUM_STEPS);
            uint8_t s = (uint8_t)(flat % NUM_STEPS);

            if ((run->aux & 1u) != 0u) {
                if (ccSvc_swapReturnPending(job->scene, t, s) == CC_RUN_WAIT)
                    return CC_RUN_WAIT;
                run->cursor++;
                continue;
            }
            if (ccSvc_clearStep(job->scene, t, s, sel) == CC_RUN_WAIT)
                return CC_RUN_WAIT;
            run->cursor++;
        }
        if (run->cursor < count)
            return CC_RUN_WAIT;
        if (job->kind == CC_KIND_TRACK && sel == CC_CLEAR_ALL) {
            pat_setTrackLength(job->scene, job->track, NUM_STEPS_PER_BAR);
            pat_setTrackScale(job->scene, job->track, TRACK_SCALE_DEFAULT);
            pat_setTrackShuffle(job->scene, job->track, 0u);
        }
        ccSvc_release();
        ccSvc_patternChangedUi(job->scene,
                               (job->kind == CC_KIND_SCENE) ? NUM_TRACKS
                                                            : job->track);
        return CC_RUN_DONE;
    default:
        ccSvc_release();
        return CC_RUN_DONE;
    }
}

/* ---- whole-Pattern copy and reset (spec §9.8) -------------------------- */

/*
 * Copy one whole Pattern region onto another Scene (spec §9.8).
 *
 * Publication order for the whole region: every destination entry is made
 * empty first, the body is copied, blocks are rewritten while still
 * unreferenced, and the address entries are published last, so a reader of
 * the destination never sees an entry pointing at bytes that are not yet the
 * new block. The literal case finishes in one pass; the retarget case (slot
 * or Effect types differ) publishes progressively (32 steps per tick, about
 * 56 ms for a full Pattern). Caller: copyOps.c for `copy pattern` and `copy
 * scene`.
 */
uint8_t ccSvc_runRegionCopy(uint8_t src_scene, uint8_t dst_scene)
{
    cc_run_t *run = &ccSvc_runState;
    uint8_t n;

    if (src_scene == dst_scene)
        return CC_RUN_DONE;
    if (run->sub == 0u) {
        if (!ccSvc_claim(dst_scene))
            return CC_RUN_WAIT;
        pat_rawRegionSilence(dst_scene);
        pat_rawRegionCopyBody(src_scene, dst_scene);
        if (!ccCopy_sceneTypesDiffer(src_scene, dst_scene)) {
            pat_rawRegionPublishSteps(src_scene, dst_scene, 0u,
                                      PAT_STEPS_PER_SCENE);
            ccSvc_release();
            ccSvc_patternChangedUi(dst_scene, NUM_TRACKS);
            return CC_RUN_DONE;
        }
        run->sub = 1u;
        run->cursor = 0u;
        return CC_RUN_WAIT;
    }
    for (n = 0u; n < CC_REGION_PER_TICK && run->cursor < PAT_STEPS_PER_SCENE;
         n++) {
        uint8_t block[PAT_RAW_BLOCK_MAX];
        uint16_t index = run->cursor++;
        uint8_t bytes = pat_rawRegionCopiedBlock(src_scene, dst_scene, index,
                                                 block);

        if (bytes == 0u) {
            pat_rawRegionPublishSteps(src_scene, dst_scene, index, 1u);
        } else {
            pat_step_specials_t sp;
            pat_automation_entry_t autos[PAT_BLOCK_AUTO_COUNT_MASK];
            cc_retarget_ctx_t ctx;
            uint8_t count = pat_rawDecode(block, &sp, autos,
                                          PAT_BLOCK_AUTO_COUNT_MASK);

            ctx.src_scene = src_scene;
            ctx.dst_scene = dst_scene;
            ctx.src_track = (uint8_t)(index / NUM_STEPS);
            ctx.dst_track = ctx.src_track;
            count = ccCopy_retargetEntries(&ctx, autos, count);
            bytes = pat_rawEncode(block, sp.flags, sp.note, sp.velocity,
                                  sp.probability, autos, count);
            (void)pat_rawRegionPublishRewritten(src_scene, dst_scene, index,
                                                bytes ? block : 0);
        }
    }
    if (run->cursor < PAT_STEPS_PER_SCENE)
        return CC_RUN_WAIT;
    ccSvc_release();
    ccSvc_patternChangedUi(dst_scene, NUM_TRACKS);
    return CC_RUN_DONE;
}

/* Empty one whole Pattern (`clear pattern`, `clear scene`) in one pass. */
uint8_t ccSvc_runRegionReset(uint8_t scene)
{
    if (!ccSvc_claim(scene))
        return CC_RUN_WAIT;
    pat_rawRegionReset(scene);
    ccSvc_release();
    ccSvc_patternChangedUi(scene, NUM_TRACKS);
    return CC_RUN_DONE;
}

/* ---- register drain (spec §6) ------------------------------------------ */

/*
 * One register pass: remove the head target from every step of the
 * register's Scene (7 x 128 steps, 8 per tick), then drop the head entry and
 * restart the presence search. Rewrites never grow (swap path).
 */
static uint8_t ccSvc_runRegister(void)
{
    cc_run_t *run = &ccSvc_runState;
    uint8_t scene = ccSvc_regScene;
    uint16_t target = ccSvc_regTarget[0];
    uint8_t n;

    if (ccSvc_regCount == 0u || scene == CC_NO_SCENE)
        return CC_RUN_DONE;
    if (run->sub == 0u) {
        uint8_t r = ccSvc_claimAndEvacuate(scene);

        if (r == CC_RUN_WAIT)
            return CC_RUN_WAIT;
        if (r == CC_RUN_DROP) {
            run->sub = 2u;          /* cannot rewrite: drop the entry */
        } else {
            run->sub = 1u;
            run->cursor = 0u;
            run->aux = 0u;
            return CC_RUN_WAIT;
        }
    }
    if (run->sub == 1u) {
        for (n = 0u; n < CC_STEPS_PER_TICK &&
                     run->cursor < PAT_STEPS_PER_SCENE; n++) {
            uint8_t t = (uint8_t)(run->cursor / NUM_STEPS);
            uint8_t s = (uint8_t)(run->cursor % NUM_STEPS);
            uint8_t block[PAT_RAW_BLOCK_MAX];
            pat_step_specials_t sp;
            pat_automation_entry_t autos[PAT_BLOCK_AUTO_COUNT_MASK];
            uint16_t live;
            uint8_t count;
            uint8_t kept = 0u;
            uint8_t i;

            if ((run->aux & 1u) != 0u) {
                if (ccSvc_swapReturnPending(scene, t, s) == CC_RUN_WAIT)
                    return CC_RUN_WAIT;
                run->cursor++;
                continue;
            }
            if (pat_rawReadBlock(scene, t, s, block, &live) == 0u) {
                run->cursor++;
                continue;
            }
            count = pat_rawDecode(block, &sp, autos,
                                  PAT_BLOCK_AUTO_COUNT_MASK);
            for (i = 0u; i < count; i++)
                if (autos[i].target != target)
                    autos[kept++] = autos[i];
            if (kept != count) {
                uint8_t bytes = pat_rawEncode(block, sp.flags, sp.note,
                                              sp.velocity, sp.probability,
                                              autos, kept);

                if (ccSvc_rewriteShrink(scene, t, s, block, bytes,
                                        PAT_RAW_TRIGGER_KEEP) == CC_RUN_WAIT)
                    return CC_RUN_WAIT;
            }
            run->cursor++;
        }
        if (run->cursor < PAT_STEPS_PER_SCENE)
            return CC_RUN_WAIT;
        run->sub = 2u;
    }
    /* Drop the head entry; the next pass handles the next target. */
    ccSvc_release();
    memmove(&ccSvc_regTarget[0], &ccSvc_regTarget[1],
            (size_t)(CC_REGISTER_SIZE - 1u) * sizeof(ccSvc_regTarget[0]));
    ccSvc_regCount--;
    if (ccSvc_regCount == 0u)
        ccSvc_regScene = CC_NO_SCENE;
    ccSvc_patternChangedUi(scene, NUM_TRACKS);
    return CC_RUN_DONE;
}

/* ---- names (spec §9.10) ------------------------------------------------ */

void ccSvc_nameCopy(uint16_t dst_row, uint16_t src_row)
{
    uint8_t *remap;
    uint8_t from;

    if (dst_row >= FS_HCNAMES_ROW_COUNT || src_row >= FS_HCNAMES_ROW_COUNT ||
        !ccSvc_ensureScratch())
        return;
    remap = &ccSvc_buf[CC_SCRATCH_REMAP_OFFSET];
    from = remap[src_row];
    if (from >= FS_HCNAMES_ROW_COUNT)
        from = (uint8_t)src_row;
    remap[dst_row] = (from == dst_row) ? CC_REMAP_CHANGED : from;
    (void)filesystem_clearResidentRefreshed(dst_row);
    ccSvc_flags |= CC_SVC_NAMES_DIRTY;
}

void ccSvc_nameContentChanged(uint16_t row)
{
    if (row >= FS_HCNAMES_ROW_COUNT)
        return;
    (void)filesystem_clearResidentRefreshed(row);
    if (ccSvc_ensureScratch() &&
        ccSvc_buf[CC_SCRATCH_REMAP_OFFSET + row] == CC_REMAP_NONE)
        ccSvc_buf[CC_SCRATCH_REMAP_OFFSET + row] = CC_REMAP_CHANGED;
    ccSvc_flags |= CC_SVC_NAMES_DIRTY;
}

/*
 * HCNAMES write completion: release the facade, then mark the source bytes
 * of every changed row for AutoSave (spec §9.10). On a card error the old
 * `.hcnames` stays and the operation still ends.
 */
static void ccSvc_namesWritten(void)
{
    uint8_t ok = (uint8_t)(filesystem_status() == FS_STATUS_DONE);
    uint16_t row;

    filesystem_ack();
    if (ok && ccSvc_buf) {
        for (row = 0u; row < FS_HCNAMES_ROW_COUNT; row++)
            if (ccSvc_buf[CC_SCRATCH_REMAP_OFFSET + row] != CC_REMAP_NONE)
                autosave_markSourceDirty(row);
    }
    if (ccSvc_buf)
        memset(&ccSvc_buf[CC_SCRATCH_REMAP_OFFSET], CC_REMAP_NONE,
               FS_HCNAMES_ROW_COUNT);
    ccSvc_flags &= (uint8_t)~CC_SVC_NAMES_BUSY;
}

/* ---- public API -------------------------------------------------------- */

void ccSvc_init(void)
{
    memset(ccSvc_queue, 0, sizeof(ccSvc_queue));
    ccSvc_head = 0u;
    ccSvc_count = 0u;
    memset(ccSvc_regTarget, 0, sizeof(ccSvc_regTarget));
    ccSvc_regCount = 0u;
    ccSvc_regScene = CC_NO_SCENE;
    memset(&ccSvc_runState, 0, sizeof(ccSvc_runState));
    ccSvc_flags = 0u;
    ccSvc_claimScene = CC_NO_SCENE;
    ccSvc_nameRetry = 0u;
    ccSvc_buf = 0;
}

uint8_t ccSvc_enqueue(const cc_job_t *job)
{
    if (!job || ccSvc_count >= CC_QUEUE_SIZE)
        return 0u;
    ccSvc_queue[(uint8_t)(ccSvc_head + ccSvc_count) % CC_QUEUE_SIZE] = *job;
    ccSvc_count++;
    return 1u;
}

uint8_t ccSvc_jobCount(void)
{
    return ccSvc_count;
}

uint8_t ccSvc_busy(void)
{
    return (uint8_t)(ccSvc_count != 0u || ccSvc_regCount != 0u ||
                     (ccSvc_flags & (CC_SVC_JOB_ACTIVE | CC_SVC_REG_ACTIVE |
                                     CC_SVC_NAMES_DIRTY |
                                     CC_SVC_NAMES_BUSY)) != 0u ||
                     ccSvc_buf != 0 ||
                     ccSvc_claimScene != CC_NO_SCENE);
}

void ccSvc_interactionEnded(void)
{
    ccSvc_flags |= CC_SVC_ENDED;
}

void ccSvc_interactionStarted(void)
{
    ccSvc_flags &= (uint8_t)~CC_SVC_ENDED;
}

cc_run_t *ccSvc_run(void)
{
    return &ccSvc_runState;
}

uint8_t *ccSvc_scratch(void)
{
    return ccSvc_buf;
}

uint8_t ccSvc_registerAdd(uint16_t target, uint8_t scene)
{
    if (ccSvc_targetPending(target))
        return 0u;
    if (ccSvc_regCount >= CC_REGISTER_SIZE ||
        (ccSvc_regCount != 0u && ccSvc_regScene != scene))
        return 0u;
    ccSvc_regScene = scene;
    ccSvc_regTarget[ccSvc_regCount++] = target;
    return 1u;
}

uint8_t ccSvc_registerFull(void)
{
    return (uint8_t)(ccSvc_regCount >= CC_REGISTER_SIZE);
}

uint8_t ccSvc_registerScene(void)
{
    return ccSvc_regCount ? ccSvc_regScene : CC_NO_SCENE;
}

uint8_t ccSvc_targetPending(uint16_t target)
{
    uint8_t i;

    for (i = 0u; i < ccSvc_regCount; i++)
        if (ccSvc_regTarget[i] == target)
            return 1u;
    return 0u;
}

/*
 * One bounded service step (500 Hz, foreground).
 *
 * Order: an in-flight name write blocks everything (it uses the same scratch
 * offsets as a paste snapshot); a running job or register pass continues; a
 * queued job starts once the name buffer is borrowed; then the register;
 * finally, once interaction has ended, the name write, the buffer return and
 * the end of the suspension.
 */
void ccSvc_tick(void)
{
    uint8_t r;

    if ((ccSvc_flags & CC_SVC_NAMES_BUSY) != 0u)
        return;
    if ((ccSvc_flags & CC_SVC_JOB_ACTIVE) != 0u) {
        const cc_job_t *job = &ccSvc_queue[ccSvc_head];

        r = ((job->op & CC_JOB_CLASS_MASK) == CC_JOB_PASTE)
                ? ccCopy_runJob(job)
                : ccClear_runJob(job);
        if (r == CC_RUN_WAIT)
            return;
        ccSvc_release();
        ccSvc_head = (uint8_t)((ccSvc_head + 1u) % CC_QUEUE_SIZE);
        ccSvc_count--;
        ccSvc_flags &= (uint8_t)~CC_SVC_JOB_ACTIVE;
        return;
    }
    if ((ccSvc_flags & CC_SVC_REG_ACTIVE) != 0u) {
        if (ccSvc_runRegister() == CC_RUN_WAIT)
            return;
        ccSvc_release();
        ccSvc_flags &= (uint8_t)~CC_SVC_REG_ACTIVE;
        return;
    }
    if (ccSvc_count != 0u) {
        if (!ccSvc_ensureScratch())
            return;
        memset(&ccSvc_runState, 0, sizeof(ccSvc_runState));
        ccSvc_flags |= CC_SVC_JOB_ACTIVE;
        return;
    }
    if (ccSvc_regCount != 0u) {
        if (!ccSvc_ensureScratch())
            return;
        memset(&ccSvc_runState, 0, sizeof(ccSvc_runState));
        ccSvc_flags |= CC_SVC_REG_ACTIVE;
        return;
    }
    if ((ccSvc_flags & CC_SVC_ENDED) == 0u)
        return;
    if ((ccSvc_flags & CC_SVC_NAMES_DIRTY) != 0u) {
        if (ccSvc_ensureScratch() &&
            filesystem_requestCopyResidentNames(ccSvc_namesWritten)) {
            ccSvc_flags = (uint8_t)((ccSvc_flags & (uint8_t)~CC_SVC_NAMES_DIRTY) |
                                    CC_SVC_NAMES_BUSY);
            ccSvc_nameRetry = 0u;
            return;
        }
        if (++ccSvc_nameRetry < CC_NAME_RETRY_TICKS)
            return;
        /* Card absent or facade stuck: names stay as they are. */
        ccSvc_flags &= (uint8_t)~CC_SVC_NAMES_DIRTY;
        ccSvc_nameRetry = 0u;
    }
    if (ccSvc_buf) {
        filesystem_returnNameCacheScratch();
        ccSvc_buf = 0;
    }
    ccSvc_flags &= (uint8_t)~CC_SVC_ENDED;
    copyClear_serviceFinished();
}
