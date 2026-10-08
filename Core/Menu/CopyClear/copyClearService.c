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
#include "timebase.h"
#include <string.h>

#define CC_QUEUE_SIZE         4u
#define CC_REGISTER_SIZE      8u
#define CC_NO_SCENE           0xFFu
#define CC_STEPS_PER_TICK     8u
#define CC_SNAPSHOT_PER_TICK  16u
#define CC_REGION_PER_TICK    32u
/* Name write: give up after 2 s of refused requests (500 Hz ticks). */
#define CC_NAME_RETRY_TICKS   1000u
#define CC_REMAP_NONE         0xFFu

_Static_assert(CC_SCRATCH_REMAP_OFFSET + FS_HCNAMES_ROW_COUNT <=
                   FS_NAME_SCRATCH_BYTES,
               "HCNAMES remap must fit the borrowed name buffer");

/* FIFO of pending pastes/clears: 4 x 6 B plus head and count (26 B). */
static cc_job_t ccSvc_queue[CC_QUEUE_SIZE];
static uint8_t ccSvc_head;
static uint8_t ccSvc_count;

/*
 * Early-trigger masks per queue slot (F1-I §11.4; 129 B SRAM1).
 *
 * What: for a queued step/bar/track paste, earlySrc[i] is the source trigger
 * of paste step i and earlyPrev[i] is its previous destination trigger. The
 * masks are captured before any early write, so queued overlapping pastes do
 * not observe one another's foreground trigger edits. earlyFlags marks live
 * slots. Lifetime: from ccSvc_pasteTriggersNow() until the job is completed
 * or dropped; a dropped job restores only unchanged destination bits.
 */
static uint8_t ccSvc_earlySrc[CC_QUEUE_SIZE][16];
static uint8_t ccSvc_earlyPrev[CC_QUEUE_SIZE][16];
static uint8_t ccSvc_earlyFlags;

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
#define CC_SVC_TRICKLE      0x20u  /* an older writer still runs: governed  */
static uint8_t ccSvc_flags;
/* Scene held through patSvc_beginExclusive(), or CC_NO_SCENE (1 B). */
static uint8_t ccSvc_claimScene = CC_NO_SCENE;
/* Refused name-write requests so far (2 B). */
static uint16_t ccSvc_nameRetry;
/* Borrowed 9 kB name buffer, NULL while not borrowed (4 B). */
static uint8_t *ccSvc_buf;

/*
 * Paste source table for overlapping pastes (S077).
 *
 * What: a 128-entry uint16_t array that replaces the cast into the borrowed
 * name buffer at the former CC_SCRATCH_TABLE_OFFSET. Each entry encodes one
 * source step: bit 15 is the source trigger (CC_SNAP_TRIGGER), bit 14
 * announces stored block data (CC_SNAP_BLOCK), and bits 11..0 are a
 * four-byte-chunk offset into the background region's pool (CC_SNAP_OFFSET).
 * Why: the table must outlive the name-buffer borrow (which is now shorter)
 * and must accommodate a future PAT_STACK_SIZE of 512 (4,063 allocatable
 * chunks). SRAM cost: 256 bytes in SRAM1 .bss (approved). Lifetime: static;
 * valid from the SNAPSHOT phase of an overlapping paste until the PLACE phase
 * completes. Owner: copyClearService.c exclusively. Affiliates: ccSvc_table(),
 * ccSvc_snapBlock(), ccSvc_runPatternPaste().
 */
static uint16_t ccSvc_snapTable[NUM_STEPS];

#if DEV_MODE_LOGGING
/*
 * Copy/clear trace counters (S075 trace debug; DEV only; 22 B SRAM1).
 *
 * What: operation totals, per-job statistics and name/register counters. The
 * operation sequence, name result and one-per-job stall latch share bits so
 * the approved DEV trace allocation remains bounded. Reset: job fields at
 * JOB_START and operation fields in ccSvc_interactionStarted().
 */
static struct {
    uint16_t job_ticks;
    uint16_t scratch_wait;
    uint16_t reg_steps;
    uint16_t trickle_ticks;
    uint16_t trickle_calls;
    uint8_t bits;
    uint8_t jobs_done;
    uint8_t jobs_dropped;
    uint8_t reg_passes;
    uint8_t slides;
    uint8_t swaps;
    uint8_t retarget_dropped;
    uint8_t evacuations;
    uint8_t claim_wait;
    uint8_t drop_reason;
    uint8_t drop_detail;
} ccSvc_traceState;
#define CC_TRACE_SAT8(field) \
    do { if ((field) != 0xFFu) (field)++; } while (0)
#define CC_TRACE_SAT16(field) \
    do { if ((field) != 0xFFFFu) (field)++; } while (0)
#define CC_TRACE_STALL_TICKS 5000u
#else
#define CC_TRACE_SAT8(field)  do { } while (0)
#define CC_TRACE_SAT16(field) do { } while (0)
#endif

/* Copy/clear trickle governor: two microseconds of work per 2 ms tick. */
#define CC_TRICKLE_US_PER_TICK 2
#define CC_TRICKLE_CAP_US      40
#define CC_TRICKLE_FLOOR_US    (-30000)
static int16_t ccSvc_trickleCredit;

/* ---- claim, scratch, UI helpers --------------------------------------- */

/*
 * Take (or keep) the exclusive boundary on one Scene; nonzero when held.
 * A claim for another Scene is an invariant failure; waiting for Pattern
 * work to drain is counted for the DEV JOB_STATS record.
 */
static uint8_t ccSvc_claim(uint8_t scene)
{
    if (ccSvc_claimScene != CC_NO_SCENE && ccSvc_claimScene != scene) {
        ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_CLAIM_OTHER_SCENE, scene, 7u,
                        0u, ccSvc_claimScene);
        return 0u;
    }
    ccSvc_claimScene = scene;
    if (patSvc_beginExclusive(scene))
        return 1u;
#if DEV_MODE_LOGGING
    if (ccSvc_traceState.claim_wait < 15u)
        ccSvc_traceState.claim_wait++;
#endif
    return 0u;
}

/* Release the boundary (no-op when none is held). */
static void ccSvc_release(void)
{
    if (ccSvc_claimScene == CC_NO_SCENE)
        return;
    patSvc_endExclusive(ccSvc_claimScene);
    ccSvc_claimScene = CC_NO_SCENE;
}

/*
 * Borrow the name buffer once; the remap starts empty. Name-buffer waiting is
 * counted only for paths that intentionally use the loan (F1-I).
 */
static uint8_t ccSvc_ensureScratch(void)
{
    if (ccSvc_buf)
        return 1u;
    ccSvc_buf = filesystem_borrowNameCacheScratch();
    if (!ccSvc_buf)
        return 0u;
    memset(&ccSvc_buf[CC_SCRATCH_REMAP_OFFSET], CC_REMAP_NONE,
           FS_HCNAMES_ROW_COUNT);
#if DEV_MODE_LOGGING
    ccTrace(AUTOSAVE_TRACE_CC_EVT_SCRATCH,
            (uint32_t)ccSvc_traceState.scratch_wait << 8u);
    ccSvc_traceState.scratch_wait = 0u;
#endif
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

/* Repaint only trigger-dependent LEDs after a foreground trigger edit. */
void ccSvc_triggersChangedUi(uint8_t scene)
{
    uint8_t mode = buttonHandler_getMode();

    if (mode == SELECT_MODE_PERF) {
        menu_refreshPerfSceneLeds();
        return;
    }
    if (scene == menu_getViewedPattern() &&
        (mode == SELECT_MODE_VOICE || mode == SELECT_MODE_STEP))
        led_updatePatternTrack(menu_getActiveVoice(), scene,
                               buttonHandler_selectedStep);
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
    CC_TRACE_SAT8(ccSvc_traceState.swaps);
    if (!pat_rawPlaceViaSwap(scene, track, step, block, trigger_mode)) {
        ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_SWAP_OCCUPIED, scene, track,
                        step, (uint8_t)(bytes / 4u));
        return CC_RUN_DONE;
    }
    if (pat_rawSwapReturn(scene, track, step))
        return CC_RUN_DONE;
    ccSvc_runState.aux |= 1u;
    return CC_RUN_WAIT;
}

/*
 * Retry a pending swap-return; if compaction cannot move anything, leave the
 * step in the swap block for the next claim and record the guarded path.
 */
static uint8_t ccSvc_swapReturnPending(uint8_t scene, uint8_t track,
                                       uint8_t step)
{
    if (pat_rawSwapReturn(scene, track, step)) {
        ccSvc_runState.aux &= (uint16_t)~1u;
        return CC_RUN_DONE;
    }
    if (patSvc_exclusiveCompactStep(scene)) {
        CC_TRACE_SAT8(ccSvc_traceState.slides);
        return CC_RUN_WAIT;
    }
    ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_SWAP_RETURN_ABANDONED, scene,
                    track, step, 0u);
    ccSvc_runState.aux &= (uint16_t)~1u;
    return CC_RUN_DONE;
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
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_EVACUATE_FAILED, scene);
        ccSvc_release();
        return CC_RUN_DROP;
    }
    return CC_RUN_DONE;
}

/* ---- Pattern paste engine (spec §9.5) ---------------------------------- */

/* Geometry of a step/range/bar/bar-to-step/track paste. */
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
    /*
     * What:       bar-to-step cross-kind paste geometry. The source uses bar
     *             coordinates (identical to CC_KIND_BAR: src->start * 16,
     *             direction and count scaled by 16). The destination uses
     *             step coordinates (job->start is an absolute step, used
     *             directly — NOT multiplied by 16).
     * Why:        the user copied a bar source and pasted to a step
     *             destination. The source data is the same bar(s); only the
     *             destination alignment changes.
     * Inputs:     job->start (absolute step 0..127), src->start/end (bar
     *             indices 0..7).
     * Outputs:    g->dst_first = job->start (absolute step); source fields
     *             identical to CC_KIND_BAR.
     * Affiliates: CC_KIND_BAR (source geometry), CC_KIND_STEP (destination
     *             geometry model).
     */
    case CC_KIND_BAR_TO_STEP:
        if (src->start <= src->end) {
            g->src_first = (uint8_t)(src->start * NUM_STEPS_PER_BAR);
            g->dir = 1;
        } else {
            g->src_first = (uint8_t)(src->start * NUM_STEPS_PER_BAR +
                                     NUM_STEPS_PER_BAR - 1u);
            g->dir = -1;
        }
        g->count = (uint8_t)((hi - lo + 1u) * NUM_STEPS_PER_BAR);
        g->dst_first = job->start;
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

/*
 * Snapshot table entry layout (one uint16_t in ccSvc_snapTable[NUM_STEPS]).
 *
 * Bits 15 and 14 mark a source trigger (CC_SNAP_TRIGGER) and stored block
 * data (CC_SNAP_BLOCK); bits 11..0 are a four-byte-chunk offset
 * (CC_SNAP_OFFSET) into the background region's pool.
 */
#define CC_SNAP_TRIGGER  0x8000u
#define CC_SNAP_BLOCK    0x4000u
#define CC_SNAP_OFFSET   0x0FFFu
/*
 * Offset field width guard (S077).
 *
 * What: CC_SNAP_OFFSET must represent every allocatable chunk index. Why: at
 * PAT_STACK_SIZE 256 the maximum is 2,015 chunks (fits in 11 bits), but at
 * PAT_STACK_SIZE 512 it is 4,063 chunks and would wrap in the old 11-bit mask.
 * The 12-bit field (0x0FFF, max 4,095) covers both. Bits 12-13 remain free
 * between CC_SNAP_BLOCK (bit 14) and CC_SNAP_OFFSET (bits 11..0). Inputs:
 * PAT_POOL_ALLOC_CHUNKS. Affiliate: config.h pool geometry.
 */
_Static_assert(PAT_POOL_ALLOC_CHUNKS <= CC_SNAP_OFFSET + 1u,
               "CC_SNAP_OFFSET must cover every allocatable chunk");

/*
 * Paste source table accessor (S077).
 *
 * What: returns the static 128-entry table that replaced the cast into the
 * name buffer. Why: the table is now independent of the name-buffer borrow;
 * the overlapping paste engine writes it during SNAPSHOT and reads it during
 * PLACE. Inputs: none. Outputs: pointer to ccSvc_snapTable[]. Affiliates:
 * ccSvc_runPatternPaste(), ccSvc_sourceBlock().
 */
static uint16_t *ccSvc_table(void)
{
    return ccSvc_snapTable;
}

/*
 * Snapshot block accessor (S077).
 *
 * What: returns a pointer into the background region's pool where the
 * overlapping-paste SNAPSHOT phase stored one retargeted source block. Why:
 * the pool is the same type and size as every live Scene pool, so it can hold
 * any set of source blocks without the name-buffer size constraint. The
 * offset field (CC_SNAP_OFFSET) is a four-byte-chunk index from pool byte 0.
 * Inputs: one uint16_t table entry. Outputs: const pointer to the block
 * bytes, or NULL when CC_SNAP_BLOCK is not set. Affiliates:
 * ccSvc_sourceBlock(), pat_backgroundPoolMut().
 */
static const uint8_t *ccSvc_snapBlock(uint16_t entry)
{
    if ((entry & CC_SNAP_BLOCK) == 0u)
        return 0;
    return &pat_backgroundPoolMut()[(uint16_t)(entry & CC_SNAP_OFFSET) * 4u];
}

/* Bit helpers for the 128-bit early-trigger masks. */
static uint8_t ccSvc_maskGet(const uint8_t m[16], uint8_t n)
{
    return (uint8_t)((m[n >> 3u] >> (n & 7u)) & 1u);
}

static void ccSvc_maskSet(uint8_t m[16], uint8_t n)
{
    m[n >> 3u] = (uint8_t)(m[n >> 3u] | (uint8_t)(1u << (n & 7u)));
}

/*
 * Does a paste write steps it also reads? Only this case needs a snapshot;
 * non-overlapping pastes use the live source and never wait for the name
 * buffer. A same-track track paste is identical and is rejected by copyOps.
 */
static uint8_t ccSvc_pasteOverlaps(const cc_job_t *job, const cc_paste_geo_t *g)
{
    const cc_source_t *src = copyClear_source();
    uint8_t set[16];
    uint8_t i;

    if (job->kind == CC_KIND_TRACK || src->scene != job->scene ||
        src->track != job->track)
        return 0u;
    memset(set, 0, sizeof(set));
    for (i = 0u; i < g->count; i++)
        ccSvc_maskSet(set, ccSvc_srcStep(g, i));
    for (i = 0u; i < g->count; i++)
        if (ccSvc_maskGet(set, ccSvc_dstStep(g, i)))
            return 1u;
    return 0u;
}

/*
 * Write a paste's trigger bits at press time and capture both masks before
 * writing. The source mask is also used later by the live/snapshot job so a
 * queued paste is independent of later foreground trigger edits.
 */
uint8_t ccSvc_pasteTriggersNow(uint8_t slot)
{
    const cc_job_t *job;
    const cc_source_t *src = copyClear_source();
    cc_paste_geo_t g;
    uint8_t i;

    if (slot >= CC_QUEUE_SIZE)
        return 0u;
    job = &ccSvc_queue[slot];
    /*
     * What:       early trigger gate. Accepts step, bar, bar-to-step and
     *             track paste jobs; other kinds (Scene, FX step) have no
     *             Pattern triggers to preview.
     * Why:        CC_KIND_BAR_TO_STEP is a Pattern paste and needs the same
     *             immediate trigger-bit write as CC_KIND_BAR and CC_KIND_STEP.
     * Affiliates: ccSvc_pasteGeometry(), ccCopy_requestBarToStep().
     */
    if ((job->op & CC_JOB_CLASS_MASK) != CC_JOB_PASTE ||
        (job->kind != CC_KIND_STEP && job->kind != CC_KIND_BAR &&
         job->kind != CC_KIND_BAR_TO_STEP && job->kind != CC_KIND_TRACK))
        return 0u;
    ccSvc_pasteGeometry(job, &g);
    if (g.count == 0u ||
        (g.selection != CC_COPY_ALL && g.selection != CC_COPY_MERGE_ALL))
        return 0u;
    memset(ccSvc_earlySrc[slot], 0, 16u);
    memset(ccSvc_earlyPrev[slot], 0, 16u);
    for (i = 0u; i < g.count; i++) {
        if (pat_isStepActive(src->track, ccSvc_srcStep(&g, i), src->scene))
            ccSvc_maskSet(ccSvc_earlySrc[slot], i);
        if (pat_isStepActive(job->track, ccSvc_dstStep(&g, i), job->scene))
            ccSvc_maskSet(ccSvc_earlyPrev[slot], i);
    }
    for (i = 0u; i < g.count; i++) {
        uint8_t on = ccSvc_maskGet(ccSvc_earlySrc[slot], i);

        if (g.selection == CC_COPY_MERGE_ALL)
            on = (uint8_t)(on | ccSvc_maskGet(ccSvc_earlyPrev[slot], i));
        pat_setStepActive(job->scene, job->track, ccSvc_dstStep(&g, i), on);
    }
    ccSvc_earlyFlags = (uint8_t)(ccSvc_earlyFlags | (uint8_t)(1u << slot));
    ccSvc_triggersChangedUi(job->scene);
    ccTrace(AUTOSAVE_TRACE_CC_EVT_EARLY_TRIG,
            (uint32_t)g.count | (1u << 8u) |
            (1u << 9u) | ((uint32_t)(job->kind & 7u) << 16u) |
            ((uint32_t)(job->scene & 0xFu) << 19u) |
            ((uint32_t)(job->track & 7u) << 23u) |
            ((uint32_t)(slot & 3u) << 26u));
    return g.count;
}

/* Restore a dropped paste's early trigger writes unless the user changed it. */
static void ccSvc_restoreEarlyTriggers(const cc_job_t *job, uint8_t slot)
{
    cc_paste_geo_t g;
    uint8_t restored = 0u;
    uint8_t kept = 0u;
    uint8_t i;

    ccSvc_pasteGeometry(job, &g);
    for (i = 0u; i < g.count; i++) {
        uint8_t ds = ccSvc_dstStep(&g, i);
        uint8_t prev = ccSvc_maskGet(ccSvc_earlyPrev[slot], i);
        uint8_t wrote = ccSvc_maskGet(ccSvc_earlySrc[slot], i);

        if (g.selection == CC_COPY_MERGE_ALL)
            wrote = (uint8_t)(wrote | prev);
        if (wrote == prev)
            continue;
        if (pat_isStepActive(job->track, ds, job->scene) != wrote) {
            kept++;
            continue;
        }
        pat_setStepActive(job->scene, job->track, ds, prev);
        restored++;
    }
    ccSvc_triggersChangedUi(job->scene);
    ccTrace(AUTOSAVE_TRACE_CC_EVT_EARLY_RESTORED,
            (uint32_t)restored | ((uint32_t)kept << 8u) |
            ((uint32_t)(slot & 3u) << 16u));
}

/*
 * Source block and trigger for one paste step. Snapshot reads the borrowed
 * table; live reads the source step now and retargets it for the destination
 * during the engine phase. Press-time trigger masks override both source
 * representations.
 */
static uint8_t ccSvc_sourceBlock(const cc_job_t *job, const cc_paste_geo_t *g,
                                 uint8_t i, uint8_t snapshot,
                                 uint8_t count_drops,
                                 uint8_t out[PAT_RAW_BLOCK_MAX],
                                 const uint8_t **block_out, uint8_t *trigger)
{
    const cc_source_t *src = copyClear_source();
    uint8_t slot = ccSvc_head;
    uint8_t early = (uint8_t)((ccSvc_earlyFlags >> slot) & 1u);
    uint8_t bytes;

    if (snapshot) {
        uint16_t entry = ccSvc_table()[i];

        *block_out = ccSvc_snapBlock(entry);
        *trigger = early ? ccSvc_maskGet(ccSvc_earlySrc[slot], i)
                         : (uint8_t)((entry & CC_SNAP_TRIGGER) != 0u);
        return *block_out ? pat_rawBlockBytes(*block_out) : 0u;
    }
    {
        uint16_t live;
        cc_retarget_ctx_t ctx;

        bytes = pat_rawReadBlock(src->scene, src->track, ccSvc_srcStep(g, i),
                                 out, &live);
        *trigger = early ? ccSvc_maskGet(ccSvc_earlySrc[slot], i)
                         : (uint8_t)((live & PAT_ADDR_TRIGGER_BIT) != 0u);
        ctx.src_scene = src->scene;
        ctx.dst_scene = job->scene;
        ctx.src_track = src->track;
        ctx.dst_track = job->track;
        if (bytes != 0u && ccCopy_retargetNeeded(&ctx)) {
            pat_step_specials_t sp;
            pat_automation_entry_t autos[PAT_BLOCK_AUTO_COUNT_MASK];
            uint8_t count = pat_rawDecode(out, &sp, autos,
                                          PAT_BLOCK_AUTO_COUNT_MASK);
            uint8_t before = count;

            count = ccCopy_retargetEntries(&ctx, autos, count);
            if (count_drops)
                ccSvc_traceRetargetDropped((uint8_t)(before - count));
            bytes = pat_rawEncode(out, sp.flags, sp.note, sp.velocity,
                                  sp.probability, autos, count);
        }
        *block_out = bytes ? out : 0;
        return bytes;
    }
}

/* Build one destination step from a live or snapshot source. */
static uint8_t ccSvc_pasteBuild(const cc_job_t *job, const cc_paste_geo_t *g,
                                uint8_t i, uint8_t snapshot, uint8_t place,
                                uint8_t out[PAT_RAW_BLOCK_MAX], uint8_t *mode,
                                uint8_t *skip, uint8_t *old_bytes)
{
    uint8_t dst_block[PAT_RAW_BLOCK_MAX];
    uint8_t src_buf[PAT_RAW_BLOCK_MAX];
    const uint8_t *src_block;
    uint8_t trigger;
    uint16_t live;

    (void)ccSvc_sourceBlock(job, g, i, snapshot, place, src_buf, &src_block,
                            &trigger);
    *old_bytes = pat_rawReadBlock(job->scene, job->track, ccSvc_dstStep(g, i),
                                  dst_block, &live);
    return ccCopy_buildStep(g->selection, src_block, trigger,
                            *old_bytes ? dst_block : 0, out, mode, skip);
}

/*
 * Run one step/range/bar/track paste in bounded steps (spec §9.5).
 *
 * What: an overlapping paste snapshots the source; every other paste reads it
 * live. Then the engine checks every step, publishes in order, and uses the
 * swap block or sliding compaction as required. Early trigger masks supply
 * press-time trigger values. Returns DONE, WAIT or DROP. Run state: phase bit
 * 0 = snapshot path; sub = claim/snapshot/check/place/finish; cursor = step;
 * aux = snapshot cursor/free chunks/swap-return bit.
 */
uint8_t ccSvc_runPatternPaste(const cc_job_t *job)
{
    cc_run_t *run = &ccSvc_runState;
    const cc_source_t *src = copyClear_source();
    cc_paste_geo_t g;
    uint8_t block[PAT_RAW_BLOCK_MAX];
    uint8_t n;

    ccSvc_pasteGeometry(job, &g);
    if (g.count == 0u) {
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_BAD_GEOMETRY, job->kind);
        return CC_RUN_DROP;
    }

    switch (run->sub) {
    case 0u: /* PATH, optional BUFFER, CLAIM + EVACUATE */
    {
        uint8_t r;

        if (ccSvc_pasteOverlaps(job, &g)) {
            run->phase = 1u;
            if (filesystem_patternSnapshotInUse()) {
                CC_TRACE_SAT16(ccSvc_traceState.scratch_wait);
                return CC_RUN_WAIT;
            }
#if DEV_MODE_LOGGING
            if (ccSvc_traceState.scratch_wait != 0u) {
                ccTrace(AUTOSAVE_TRACE_CC_EVT_SNAPSHOT_GATE,
                        (uint32_t)ccSvc_traceState.scratch_wait);
                ccSvc_traceState.scratch_wait = 0u;
            }
#endif
        }
        r = ccSvc_claimAndEvacuate(job->scene);
        if (r != CC_RUN_DONE)
            return r;
        run->cursor = 0u;
        if (run->phase & 1u) {
            run->sub = 1u;
            run->aux = 0u;
        } else {
            run->sub = 2u;
            run->aux = pat_rawFreeChunks(job->scene);
        }
        return CC_RUN_WAIT;
    }
    case 1u: /* SNAPSHOT (overlapping source) */
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

                {
                    uint8_t before = count;

                    count = ccCopy_retargetEntries(&ctx, autos, count);
                    ccSvc_traceRetargetDropped((uint8_t)(before - count));
                }
                bytes = pat_rawEncode(block, sp.flags, sp.note, sp.velocity,
                                      sp.probability, autos, count);
            }
            if (bytes != 0u) {
                memcpy(&pat_backgroundPoolMut()[(uint16_t)run->aux * 4u],
                       block, bytes);
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
            uint8_t new_c = (uint8_t)(ccSvc_pasteBuild(
                                           job, &g, (uint8_t)run->cursor,
                                           (uint8_t)(run->phase & 1u), 0u,
                                           block, &mode, &skip, &old_bytes) /
                                    4u);
            uint8_t old_c = (uint8_t)(old_bytes / 4u);

            run->cursor++;
            if (skip)
                continue;
            if (new_c > old_c) {
                if (run->aux < new_c) {
                    ccTrace(AUTOSAVE_TRACE_CC_EVT_CHECK_FAIL,
                            ((uint32_t)run->aux & 0x7FFu) |
                            ((uint32_t)((run->cursor - 1u) & 0x7Fu) << 11u) |
                            ((uint32_t)(new_c & 0x3Fu) << 18u) |
                            ((uint32_t)(old_c & 0x3Fu) << 24u));
                    ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_NO_ROOM,
                                          (uint8_t)(run->cursor - 1u));
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
            bytes = ccSvc_pasteBuild(job, &g, i, (uint8_t)(run->phase & 1u),
                                     1u, block, &mode, &skip, &old_bytes);
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
                {
                    CC_TRACE_SAT8(ccSvc_traceState.slides);
                    return CC_RUN_WAIT;
                }
                ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_GROW_UNPLACEABLE,
                                job->scene, job->track, ds,
                                (uint8_t)(bytes / 4u));
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
 * automation and keeps trigger and specials, `notes` turns the trigger off,
 * removes note/velocity specials, and keeps probability plus automation.
 * Rewrites never grow, so they use the swap path. Returns CC_RUN_DONE or
 * CC_RUN_WAIT (swap-return pending).
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
    /* notes: probability gates the retained automation and must stay. */
    bytes = pat_rawEncode(block, (uint8_t)(sp.flags & PAT_SPECIAL_PROB_BIT),
                          0u, 0u, sp.probability, autos, count);
    return ccSvc_rewriteShrink(scene, track, step, block, bytes,
                               PAT_RAW_TRIGGER_OFF);
}

/*
 * Run one Pattern clear in bounded steps (spec §5, §9.6).
 *
 * What: empties steps (all), strips automation (automation), or turns
 * triggers off and removes note/velocity specials while keeping probability
 * and automation (notes), over a step range, a track, or the whole Pattern;
 * and
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
            {
                uint8_t before = count;

                count = ccCopy_retargetEntries(&ctx, autos, count);
                ccSvc_traceRetargetDropped((uint8_t)(before - count));
            }
            bytes = pat_rawEncode(block, sp.flags, sp.note, sp.velocity,
                                  sp.probability, autos, count);
            if (!pat_rawRegionPublishRewritten(src_scene, dst_scene, index,
                                               bytes ? block : 0))
                ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_REGION_REWRITE_GREW,
                                dst_scene, (uint8_t)(index / NUM_STEPS),
                                (uint8_t)(index % NUM_STEPS), 0u);
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
#if DEV_MODE_LOGGING
                if (ccSvc_traceState.reg_steps < 896u)
                    ccSvc_traceState.reg_steps++;
#endif
            }
            run->cursor++;
        }
        if (run->cursor < PAT_STEPS_PER_SCENE)
            return CC_RUN_WAIT;
        run->sub = 2u;
    }
    /* Drop the head entry; the next pass handles the next target. */
#if DEV_MODE_LOGGING
    ccTrace(AUTOSAVE_TRACE_CC_EVT_REG_DONE,
            (uint32_t)target |
            ((uint32_t)(ccSvc_traceState.reg_steps & 0x3FFu) << 16u) |
            ((uint32_t)((ccSvc_traceState.swaps > 31u) ? 31u
                                                        : ccSvc_traceState.swaps)
             << 26u) |
            ((uint32_t)(run->cursor < PAT_STEPS_PER_SCENE) << 31u));
    CC_TRACE_SAT8(ccSvc_traceState.reg_passes);
#endif
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

uint8_t ccSvc_nameCopy(uint16_t dst_row, uint16_t src_row)
{
    uint8_t *remap;
    uint8_t from;

    if (dst_row >= FS_HCNAMES_ROW_COUNT || src_row >= FS_HCNAMES_ROW_COUNT ||
        !ccSvc_buf)
        return 0u;
    remap = &ccSvc_buf[CC_SCRATCH_REMAP_OFFSET];
    from = remap[src_row];
    if (from >= FS_HCNAMES_ROW_COUNT)
        from = (uint8_t)src_row;
    remap[dst_row] = (from == dst_row) ? CC_REMAP_NONE : from;
    (void)filesystem_clearResidentRefreshed(dst_row);
    ccSvc_flags |= CC_SVC_NAMES_DIRTY;
    return 1u;
}

void ccSvc_nameContentChanged(uint16_t row)
{
    if (row >= FS_HCNAMES_ROW_COUNT)
        return;
    (void)filesystem_clearResidentRefreshed(row);
    ccSvc_flags |= CC_SVC_NAMES_DIRTY;
}

/* Borrow lazily for the final name phase; content changes never borrow. */
uint8_t ccSvc_namesReady(void)
{
    return ccSvc_ensureScratch();
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
#if DEV_MODE_LOGGING
    ccSvc_traceState.bits = (uint8_t)((ccSvc_traceState.bits & (uint8_t)~0x30u) |
                                      ((ok ? 1u : 2u) << 4u));
    ccTrace(AUTOSAVE_TRACE_CC_EVT_NAMES, ok ? 1u : 2u);
#endif
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
    memset(ccSvc_earlySrc, 0, sizeof(ccSvc_earlySrc));
    memset(ccSvc_earlyPrev, 0, sizeof(ccSvc_earlyPrev));
    ccSvc_earlyFlags = 0u;
    ccSvc_trickleCredit = 0;
#if DEV_MODE_LOGGING
    memset(&ccSvc_traceState, 0, sizeof(ccSvc_traceState));
#endif
}

/* Contract in copyClearService.h: returns queue slot + 1. */
uint8_t ccSvc_enqueue(const cc_job_t *job)
{
    uint8_t slot;

    if (!job)
        return 0u;
    if (ccSvc_count >= CC_QUEUE_SIZE) {
        ccTrace(AUTOSAVE_TRACE_CC_EVT_QUEUE_FULL, ccTrace_job(job));
        return 0u;
    }
    slot = (uint8_t)((ccSvc_head + ccSvc_count) % CC_QUEUE_SIZE);
    ccSvc_queue[slot] = *job;
    ccSvc_earlyFlags = (uint8_t)(ccSvc_earlyFlags & (uint8_t)~(1u << slot));
    ccSvc_count++;
    return (uint8_t)(slot + 1u);
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
#if DEV_MODE_LOGGING
    ccSvc_traceState.bits = (uint8_t)((ccSvc_traceState.bits + 1u) & 0x0Fu);
    ccSvc_traceState.jobs_done = 0u;
    ccSvc_traceState.jobs_dropped = 0u;
    ccSvc_traceState.reg_passes = 0u;
#endif
}

cc_run_t *ccSvc_run(void)
{
    return &ccSvc_runState;
}

uint8_t *ccSvc_scratch(void)
{
    return ccSvc_buf;
}

/* Trace counter feeds (empty in production). */
void ccSvc_traceRetargetDropped(uint8_t count)
{
#if DEV_MODE_LOGGING
    uint16_t sum = (uint16_t)ccSvc_traceState.retarget_dropped + count;

    ccSvc_traceState.retarget_dropped = (uint8_t)(sum > 0xFFu ? 0xFFu : sum);
#else
    (void)count;
#endif
}

void ccSvc_traceDropReason(uint8_t reason, uint8_t detail)
{
#if DEV_MODE_LOGGING
    ccSvc_traceState.drop_reason = reason;
    ccSvc_traceState.drop_detail = detail;
#else
    (void)reason;
    (void)detail;
#endif
}

uint8_t ccSvc_traceOpSequence(void)
{
#if DEV_MODE_LOGGING
    return (uint8_t)(ccSvc_traceState.bits & 0x0Fu);
#else
    return 0u;
#endif
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

uint8_t ccSvc_registerCount(void)
{
    return ccSvc_regCount;
}

/* Charge one governed job/register call by its TIM2 duration. */
static void ccSvc_chargeTrickle(uint32_t t0)
{
    uint32_t elapsed;
    int32_t credit;

    if ((ccSvc_flags & CC_SVC_TRICKLE) == 0u)
        return;
    elapsed = timebase_tim2Now() - t0;
    credit = (int32_t)ccSvc_trickleCredit -
             (int32_t)((elapsed > 30000u) ? 30000u : elapsed);
    ccSvc_trickleCredit = (int16_t)(credit < CC_TRICKLE_FLOOR_US
                                        ? CC_TRICKLE_FLOOR_US : credit);
    CC_TRACE_SAT16(ccSvc_traceState.trickle_calls);
}

#if DEV_MODE_LOGGING
static void ccSvc_traceJobStart(const cc_job_t *job)
{
    ccSvc_traceState.job_ticks = 0u;
    ccSvc_traceState.reg_steps = 0u;
    ccSvc_traceState.trickle_ticks = 0u;
    ccSvc_traceState.trickle_calls = 0u;
    ccSvc_traceState.slides = 0u;
    ccSvc_traceState.swaps = 0u;
    ccSvc_traceState.retarget_dropped = 0u;
    ccSvc_traceState.evacuations = 0u;
    ccSvc_traceState.claim_wait = 0u;
    ccSvc_traceState.drop_reason = AUTOSAVE_TRACE_CC_DROP_NONE;
    ccSvc_traceState.drop_detail = 0u;
    ccSvc_traceState.bits = (uint8_t)(ccSvc_traceState.bits & (uint8_t)~0x40u);
    if (job)
        ccTrace(AUTOSAVE_TRACE_CC_EVT_JOB_START, ccTrace_job(job));
}

static void ccSvc_traceTrickle(void)
{
    if (ccSvc_traceState.trickle_ticks != 0u ||
        ccSvc_traceState.trickle_calls != 0u)
        ccTrace(AUTOSAVE_TRACE_CC_EVT_JOB_TRICKLE,
                (uint32_t)ccSvc_traceState.trickle_ticks |
                ((uint32_t)ccSvc_traceState.trickle_calls << 16u));
}

static void ccSvc_traceJobEnd(uint8_t r)
{
    uint32_t stats = (uint32_t)ccSvc_traceState.slides |
                     ((uint32_t)ccSvc_traceState.swaps << 8u) |
                     ((uint32_t)ccSvc_traceState.retarget_dropped << 16u) |
                     ((uint32_t)ccSvc_traceState.evacuations << 24u) |
                     ((uint32_t)ccSvc_traceState.claim_wait << 28u);

    if (stats != 0u)
        ccTrace(AUTOSAVE_TRACE_CC_EVT_JOB_STATS, stats);
    ccSvc_traceTrickle();
    ccTrace(AUTOSAVE_TRACE_CC_EVT_JOB_END,
            (uint32_t)r |
            ((uint32_t)(ccSvc_traceState.drop_reason & 0x3Fu) << 2u) |
            ((uint32_t)((r == CC_RUN_DROP)
                            ? ccSvc_traceState.drop_detail
                            : (uint8_t)((ccSvc_runState.phase << 4u) |
                                        (ccSvc_runState.sub & 0x0Fu))) << 8u) |
            ((uint32_t)ccSvc_traceState.job_ticks << 16u));
    if (r == CC_RUN_DROP)
        CC_TRACE_SAT8(ccSvc_traceState.jobs_dropped);
    else
        CC_TRACE_SAT8(ccSvc_traceState.jobs_done);
}

static void ccSvc_traceNamesRequested(void)
{
    uint8_t copied = 0u;
    uint16_t row;

    for (row = 0u; row < FS_HCNAMES_ROW_COUNT; row++)
        if (ccSvc_buf[CC_SCRATCH_REMAP_OFFSET + row] != CC_REMAP_NONE)
            copied++;
    ccTrace(AUTOSAVE_TRACE_CC_EVT_NAMES,
            ((uint32_t)copied << 8u) |
            ((uint32_t)((ccSvc_nameRetry > 255u) ? 255u : ccSvc_nameRetry)
             << 24u));
}

static void ccSvc_traceOpFinish(void)
{
    ccTrace(AUTOSAVE_TRACE_CC_EVT_OP_FINISH,
            (uint32_t)ccSvc_traceState.jobs_done |
            ((uint32_t)ccSvc_traceState.jobs_dropped << 8u) |
            ((uint32_t)ccSvc_traceState.reg_passes << 16u) |
            ((uint32_t)(ccSvc_traceState.bits & 0x0Fu) << 24u) |
            ((uint32_t)((ccSvc_traceState.bits >> 4u) & 0x3u) << 28u));
    ccSvc_traceState.bits = (uint8_t)(ccSvc_traceState.bits & 0x0Fu);
}
#else
static void ccSvc_traceJobStart(const cc_job_t *job) { (void)job; }
static void ccSvc_traceTrickle(void) { }
static void ccSvc_traceJobEnd(uint8_t r) { (void)r; }
static void ccSvc_traceNamesRequested(void) { }
static void ccSvc_traceOpFinish(void) { }
#endif

/*
 * One bounded service step (500 Hz, foreground). Non-overlapping Pattern
 * work starts without a scratch wait; while an older filesystem writer is
 * busy, the governor gates complete engine calls to 0.1% average CPU.
 */
void ccSvc_tick(void)
{
    uint8_t r;
    uint8_t work;
    uint32_t t0 = 0u;

    if ((ccSvc_flags & CC_SVC_NAMES_BUSY) != 0u)
        return;
    work = (uint8_t)((ccSvc_flags & (CC_SVC_JOB_ACTIVE | CC_SVC_REG_ACTIVE)) != 0u);
    if (filesystem_status() == FS_STATUS_BUSY) {
        ccSvc_flags |= CC_SVC_TRICKLE;
        if (ccSvc_trickleCredit < CC_TRICKLE_CAP_US)
            ccSvc_trickleCredit = (int16_t)(ccSvc_trickleCredit +
                                            CC_TRICKLE_US_PER_TICK);
        if (work)
            CC_TRACE_SAT16(ccSvc_traceState.trickle_ticks);
        if (work && ccSvc_trickleCredit <= 0)
            return;
        t0 = timebase_tim2Now();
    } else {
        ccSvc_flags &= (uint8_t)~CC_SVC_TRICKLE;
    }
    if ((ccSvc_flags & CC_SVC_JOB_ACTIVE) != 0u) {
        const cc_job_t *job = &ccSvc_queue[ccSvc_head];
        uint8_t slot = ccSvc_head;

        r = ((job->op & CC_JOB_CLASS_MASK) == CC_JOB_PASTE)
                ? ccCopy_runJob(job) : ccClear_runJob(job);
        ccSvc_chargeTrickle(t0);
        CC_TRACE_SAT16(ccSvc_traceState.job_ticks);
#if DEV_MODE_LOGGING
        if (r == CC_RUN_WAIT && (ccSvc_traceState.bits & 0x40u) == 0u &&
            ccSvc_traceState.job_ticks >= CC_TRACE_STALL_TICKS) {
            ccSvc_traceState.bits |= 0x40u;
            ccTrace(AUTOSAVE_TRACE_CC_EVT_JOB_STALL, ccTrace_job(job));
            ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_STALL_PHASE, job->scene,
                            7u, 0u,
                            (uint8_t)((ccSvc_runState.phase << 4u) |
                                      (ccSvc_runState.sub & 0x0Fu)));
        }
#endif
        if (r == CC_RUN_WAIT)
            return;
        if (r == CC_RUN_DROP && ((ccSvc_earlyFlags >> slot) & 1u) != 0u)
            ccSvc_restoreEarlyTriggers(job, slot);
        ccSvc_earlyFlags = (uint8_t)(ccSvc_earlyFlags & (uint8_t)~(1u << slot));
        ccSvc_traceJobEnd(r);
        ccSvc_release();
        ccSvc_head = (uint8_t)((ccSvc_head + 1u) % CC_QUEUE_SIZE);
        ccSvc_count--;
        ccSvc_flags &= (uint8_t)~CC_SVC_JOB_ACTIVE;
        return;
    }
    if ((ccSvc_flags & CC_SVC_REG_ACTIVE) != 0u) {
        r = ccSvc_runRegister();
        ccSvc_chargeTrickle(t0);
        if (r == CC_RUN_WAIT)
            return;
        ccSvc_traceTrickle();
        ccSvc_release();
        ccSvc_flags &= (uint8_t)~CC_SVC_REG_ACTIVE;
        return;
    }
    if (ccSvc_count != 0u) {
        memset(&ccSvc_runState, 0, sizeof(ccSvc_runState));
        ccSvc_flags |= CC_SVC_JOB_ACTIVE;
        ccSvc_traceJobStart(&ccSvc_queue[ccSvc_head]);
        return;
    }
    if (ccSvc_regCount != 0u) {
        memset(&ccSvc_runState, 0, sizeof(ccSvc_runState));
        ccSvc_flags |= CC_SVC_REG_ACTIVE;
        ccSvc_traceJobStart(0);
        return;
    }
    if ((ccSvc_flags & CC_SVC_ENDED) == 0u)
        return;
    if ((ccSvc_flags & CC_SVC_NAMES_DIRTY) != 0u) {
        if (ccSvc_ensureScratch() &&
            filesystem_requestCopyResidentNames(ccSvc_namesWritten)) {
            ccSvc_traceNamesRequested();
            ccSvc_flags = (uint8_t)((ccSvc_flags & (uint8_t)~CC_SVC_NAMES_DIRTY) |
                                    CC_SVC_NAMES_BUSY);
            ccSvc_nameRetry = 0u;
            return;
        }
        if (++ccSvc_nameRetry < CC_NAME_RETRY_TICKS)
            return;
#if DEV_MODE_LOGGING
        ccSvc_traceState.bits = (uint8_t)(ccSvc_traceState.bits | 0x30u);
        ccTrace(AUTOSAVE_TRACE_CC_EVT_NAMES, 3u | (255u << 24u));
#endif
        ccSvc_flags &= (uint8_t)~CC_SVC_NAMES_DIRTY;
        ccSvc_nameRetry = 0u;
    }
    if (ccSvc_claimScene != CC_NO_SCENE)
        ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_TEARDOWN_CLAIM_HELD,
                        ccSvc_claimScene, 7u, 0u, ccSvc_claimScene);
    if (ccSvc_buf) {
        filesystem_returnNameCacheScratch();
        ccSvc_buf = 0;
        ccTrace(AUTOSAVE_TRACE_CC_EVT_SCRATCH, 1u);
    }
    ccSvc_flags &= (uint8_t)~CC_SVC_ENDED;
    ccSvc_traceOpFinish();
    copyClear_serviceFinished();
}
