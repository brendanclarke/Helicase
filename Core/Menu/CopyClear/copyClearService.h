/*
 * copyClearService.h — background service for Phase 6 copy and clear.
 *
 * What: a FIFO of up to four pastes/clears, the eight-entry pot-clear
 * register, the 9 kB name buffer borrowed from the filesystem for the length
 * of an operation, bounded Pattern work under the Pattern Stack Service's
 * exclusive boundary (one Scene at a time), Scene-level executors waiting for
 * idle Preset workers, and the end-of-operation name write. Why: pastes and
 * clears must finish after the user releases the copy/clear button, and every
 * Pattern change must be bounded per tick and publication-safe (spec §9).
 * Inputs: jobs from copyOps/clearOps, register entries from clearOps.
 * Outputs: Pattern/Scene changes, LED/menu refresh requests, the busy state
 * that holds the AutoSave/maintenance suspension. Cadence: ccSvc_tick() at
 * 500 Hz from timebase_serviceFrontPanel() (foreground), after patSvc_tick().
 * Affiliates: PatternStackService.h, PatternData.h, filesystem.h, copyOps.h,
 * clearOps.h, copyClearSession.h.
 */
#ifndef COPY_CLEAR_SERVICE_H_
#define COPY_CLEAR_SERVICE_H_

#include <stdint.h>
#include "config.h"
#include "AutosaveTrace.h"
#include "copyClearSession.h"

/* Job classes (high nibble of cc_job_t.op); the low nibble is the selection. */
#define CC_JOB_PASTE      0x10u
#define CC_JOB_CLEAR      0x20u
#define CC_JOB_CLASS_MASK 0xF0u
#define CC_JOB_SEL_MASK   0x0Fu

/*
 * One queued paste or clear (6 B). op: class | selection. kind: source kind
 * (paste) or object kind (clear). scene/track: destination (paste) or object
 * Scene/track (clear). start: destination start (paste: absolute step, bar,
 * track, Scene or FX step) or object start. end: object end (clear); unused
 * for pastes (the length comes from the operation's source).
 */
typedef struct {
    uint8_t op;
    uint8_t kind;
    uint8_t scene;
    uint8_t track;
    uint8_t start;
    uint8_t end;
} cc_job_t;

/* Executor results: finished, call again next tick, or dropped silently. */
#define CC_RUN_DONE 0u
#define CC_RUN_WAIT 1u
#define CC_RUN_DROP 2u

/*
 * Per-job run state (6 B), reset when a job starts.
 * phase: owned by the executor in copyOps.c/clearOps.c. sub/cursor/aux: owned
 * by the service engines (Pattern paste/clear, region copy/reset) that an
 * executor may call. Accessor: ccSvc_run().
 */
typedef struct {
    uint8_t phase;
    uint8_t sub;
    uint16_t cursor;
    uint16_t aux;
} cc_run_t;

void ccSvc_init(void);
void ccSvc_tick(void);

/*
 * Queue one paste or clear (spec §3.1).
 *
 * Inputs: a job. Output: queue slot index + 1 (1..4) when queued, 0 when the
 * queue already holds four jobs (dropped silently; a QUEUE_FULL trace record
 * is written). The slot index lets the caller attach early-trigger state to
 * the job (ccSvc_pasteTriggersNow()). Callers: ccCopy_requestPaste(),
 * ccClear_requestClear().
 */
uint8_t ccSvc_enqueue(const cc_job_t *job);
/* Number of queued or running jobs (0..4). */
uint8_t ccSvc_jobCount(void);
/* Nonzero while a job, the register, an apply wait or the name write remains. */
uint8_t ccSvc_busy(void);
/* Interaction ended (copy/clear released): allow the name write and teardown. */
void ccSvc_interactionEnded(void);
/* A new operation started: the teardown waits for its interaction to end. */
void ccSvc_interactionStarted(void);

/* Run state of the job at the queue head (valid only inside an executor). */
cc_run_t *ccSvc_run(void);
/*
 * Borrowed 9 kB name buffer (S075, F1-I).
 *
 * What: the filesystem's name cache, lent to copy/clear only when it is used:
 * by a step/bar paste that overlaps its own source (snapshot), and by the
 * name remap and its end-of-operation HCNAMES write. Clears, pot clears,
 * whole-Pattern copy/reset, non-overlapping pastes and Scene-level data
 * commits never wait for it. ccSvc_scratch() returns NULL while not borrowed.
 * Layout (spec §9.3): [0..160] HCNAMES row remap (0xFF = unchanged);
 * [256..511] paste source table; [512..] source blocks. Accessors:
 * copyClearService.c, copyOps.c.
 */
uint8_t *ccSvc_scratch(void);
/* Borrow scratch for the final name phase; zero while filesystem is busy. */
uint8_t ccSvc_namesReady(void);
#define CC_SCRATCH_REMAP_OFFSET   0u
#define CC_SCRATCH_TABLE_OFFSET   256u
#define CC_SCRATCH_BLOCK_OFFSET   512u

/*
 * Bounded engines shared by the executors (each returns CC_RUN_*):
 * ccSvc_runPatternPaste(): step, range, bar and `copy track` pastes (spec
 * §9.5). ccSvc_runPatternClear(): step/bar/track and PERF Pattern clears
 * (spec §5). ccSvc_runRegionCopy(): whole-Pattern copy src -> dst with
 * retargeting when types differ (spec §9.8). ccSvc_runRegionReset(): empty
 * one whole Pattern. They take and release the exclusive boundary themselves
 * and use the run state's sub/cursor/aux fields.
 */
uint8_t ccSvc_runPatternPaste(const cc_job_t *job);
uint8_t ccSvc_runPatternClear(const cc_job_t *job);
uint8_t ccSvc_runRegionCopy(uint8_t src_scene, uint8_t dst_scene);
uint8_t ccSvc_runRegionReset(uint8_t scene);

/*
 * Pot-clear register (spec §6; 18 B). ccSvc_registerAdd(): append one target
 * for one Scene; zero when full, when the target is already waiting, or when
 * the register holds entries for another Scene. ccSvc_registerFull(),
 * ccSvc_registerScene() (0xFF when empty), ccSvc_targetPending() (Menu's
 * underline filter).
 */
uint8_t ccSvc_registerAdd(uint16_t target, uint8_t scene);
uint8_t ccSvc_registerFull(void);
uint8_t ccSvc_registerScene(void);
uint8_t ccSvc_targetPending(uint16_t target);

/* Number of targets waiting in the pot-clear register (0..8; trace use). */
uint8_t ccSvc_registerCount(void);

/*
 * Early trigger bits for pastes (F1-I §11.4; user Q4, A2, A3, B1).
 *
 * What: ccSvc_pasteTriggersNow() writes destination trigger bits for an
 * accepted step/bar/track paste at the press; `... -> repl` sets each bit to
 * the source's and `... -> merge` ORs it in. Automation pastes write nothing.
 * The slot retains press-time source and previous-destination masks so the
 * job is exact even when a later early paste changes the live source, and a
 * dropped job restores only steps the user has not changed since.
 */
uint8_t ccSvc_pasteTriggersNow(uint8_t slot);

/* Repaint step LEDs after foreground trigger changes for one Scene. */
void ccSvc_triggersChangedUi(uint8_t scene);

/*
 * Name remap for copy/clear (spec §9.10, F1-I).
 *
 * ccSvc_nameCopy() returns 0 when the scratch buffer is not borrowed; callers
 * wait on ccSvc_namesReady() before recording names. Chained pastes resolve
 * to the original source row. ccSvc_nameContentChanged() only clears the
 * resident refreshed flag and arms the final HCNAMES write; it never borrows.
 */
uint8_t ccSvc_nameCopy(uint16_t dst_row, uint16_t src_row);
void ccSvc_nameContentChanged(uint16_t row);

/*
 * Refresh the UI after Pattern content of one Scene changed: automation
 * presence search, VOICE/STEP step LEDs, PERF Scene LEDs. Only acts when the
 * Scene is viewed (or in PERF). Used by the engines and executors.
 */
void ccSvc_patternChangedUi(uint8_t scene, uint8_t track);

/*
 * Copy/clear diagnostic trace (S075 trace debug; DEV_MODE_LOGGING only).
 *
 * What: ccTrace() writes one stage-'c' AutoSave trace record; its helpers pack
 * jobs and guarded anomalies. In production every call is a no-op. Callers:
 * copyClearSession.c, copyOps.c, clearOps.c, copyClearService.c.
 */
static inline void ccTrace(uint8_t event, uint32_t value)
{
#if DEV_MODE_LOGGING
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_COPY_CLEAR, event, value);
#else
    (void)event;
    (void)value;
#endif
}

static inline uint32_t ccTrace_job(const cc_job_t *job)
{
    if (!job)
        return 0u;
    return (uint32_t)job->op |
           ((uint32_t)(job->kind & 0x07u) << 8u) |
           ((uint32_t)(job->scene & 0x0Fu) << 11u) |
           ((uint32_t)((job->track < 7u) ? job->track : 7u) << 15u) |
           ((uint32_t)(job->start & 0x7Fu) << 18u) |
           ((uint32_t)(job->end & 0x7Fu) << 25u);
}

static inline void ccTrace_anomaly(uint8_t code, uint8_t scene, uint8_t track,
                                   uint8_t step, uint8_t extra)
{
    ccTrace(AUTOSAVE_TRACE_CC_EVT_ANOMALY,
            (uint32_t)code | ((uint32_t)(track & 0x0Fu) << 8u) |
            ((uint32_t)(scene & 0x0Fu) << 12u) | ((uint32_t)step << 16u) |
            ((uint32_t)extra << 24u));
}

/* Per-job feeds; empty in production. */
void ccSvc_traceRetargetDropped(uint8_t count);
void ccSvc_traceDropReason(uint8_t reason, uint8_t detail);
uint8_t ccSvc_traceOpSequence(void);

#endif /* COPY_CLEAR_SERVICE_H_ */
