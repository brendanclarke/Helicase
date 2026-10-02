# S075 — Phase 6 Copy and Clear — Trace Debug Implementation Schedule

Diagnostic trace hook points around the copy/clear risks, for hardware testing
of the S075 implementation. Implemented on 2026-10-02; Decisions D1–D5 were
answered by the user on 2026-10-01 and remain folded in below (§14).

- Baseline: working tree of `dev-ph6-copyclear` after the S075 implementation
  pass (`S075_PH6_COPYCLEAR_IMPLEMENTATION.md` §14). Line numbers below are
  from that tree and are given with a function or text anchor; use the anchor
  if lines have moved.
- Related: spec `S075_PH6_COPY_CLEAR_FULL_SPEC.md`; trace conventions
  `knowledge_files/specification_reference/DEV_MODES.md` ("Adding or
  supplementing diagnostics"); decoder `tools/decode_devlogs.py`.

## Contents

0. Purpose and risk map
1. Design decisions
2. Record format (stage `c`)
3. Stage T1 — `AutosaveTrace.h`: stage code, event codes, value layouts
4. Stage T2 — `copyClearService.h/.c`: trace helper, counters, service hooks
5. Stage T3 — `copyClearSession.c`: operation hooks
6. Stage T4 — `copyOps.c` and `clearOps.c`: paste/clear policy hooks
7. Stage T5 — `PatternStackService.c`: exclusive-move trace
8. Stage T6 — `filesystem.c`: suspension edges and refusals while lent
9. Stage T7 — `tools/decode_devlogs.py`
10. Stage T8 — documentation
11. Record volume budget
12. Resources
13. Verification
14. Decisions to confirm before implementation

---

## 0. Purpose and risk map

The S075 implementation drops pastes and clears silently by design (spec
§3.3) and runs most of its work in the background after the copy/clear button
is released. Hardware testing therefore cannot tell from the panel alone
whether a paste was dropped, why, how long it took, or whether a guarded
"cannot happen" path was reached. This plan adds one trace record family that
answers those questions, at the points below.

| # | Risk (from `S075_PH6_COPYCLEAR_IMPLEMENTATION.md` §14.3, §14.5) | Evidence the trace gives | Hook (stage) |
|---|---|---|---|
| R1 | Silent drops (pool too full, Advanced limit, Effect type mismatch, swap block cannot be emptied, queue full, identical paste) | One `JOB_END` with result DROP and a reason code; `QUEUE_FULL`; `PASTE_NOOP`; `CHECK_FAIL` with the free/new/old chunk numbers of the failing step | T2, T4 |
| R2 | Growing-step check rejecting pastes the old net-growth check would accept (deviation 1) | `CHECK_FAIL` shows free chunks vs new/old size at the failing step | T2 |
| R3 | Slow compaction on a fragmented pool (one slide per tick) | `JOB_STATS` slide count; `JOB_END` tick count | T2 |
| R4 | Guarded paths that must never run: growing step unplaceable after the check; swap occupied on a shrink; swap-return abandoned; region rewrite that would grow; claim held for another Scene; teardown with a claim held | `ANOMALY` with a code and coordinates | T2 |
| R5 | A job stuck waiting (exclusive claim never granted, Preset apply worker never idle, borrow never granted) | One `JOB_STALL` per job after 10 s of waiting, with the run phase | T2 |
| R6 | Suspension of AutoSave/trace/settings/maintenance (user rule): when it starts and ends, and what was running at the start | `SUSPEND` begin/end edges from `filesystem_tick()` | T6 |
| R7 | Name buffer loan blocking other filesystem work (deviation 4) | `SCRATCH` borrow/return with wait ticks; first refused `filesystem_start()` per loan with the operation id | T2, T6 |
| R8 | Name write (HCNAMES `HNcU`): requested, result, give-up after 2 s | `NAMES` request/result/give-up | T2 |
| R9 | Pot-clear register: entries refused (full, other Scene), pass result | `REG_ADD`, `REG_REFUSED`, `REG_DONE` | T2, T4 |
| R10 | Edit-mask fan-out reaching more or fewer Scenes than expected | `FANOUT` with the written Scene mask per Scene-level paste/clear; `MASK_SET` for exchange/reset | T4 |
| R11 | Retargeting dropping automation entries | `JOB_STATS` retarget-dropped count | T2 |
| R12 | Operation refusals at the copy/clear press | `OP_REFUSED` with a reason mask | T3 |

No hook changes behaviour: every hook only records (user, D3: the teardown
claim check records only). The one change that is not a new record is T5,
which stops copy/clear's own block moves from filling the 32-record
PatternTrace ring (§7; user, D4).

---

## 1. Design decisions

1. **Channel: the AutoSave trace ring (`/asavetrc.bin`).** It is already the
   general DEV lifecycle trace (it carries button toggles `K`, event-ring
   overflow `U`, budget reports `H`), it is drained by the existing
   `filesystem_autosaveTraceFlush_tick()`, it publishes its own dropped count
   (`G`), and `tools/decode_devlogs.py` already decodes it. Its ring is
   currently 2,048 records (`config.h` `AUTOSAVE_TRACE_RECORD_COUNT`, the
   TEMPORARY approved expansion). PatternTrace (`/pattrace.bin`) is not used:
   its ring is 32 records and no decoder exists.
2. **One new stage code `c`** (all 26 upper-case letters are taken in
   `autosave_trace_stage_t`). The record's `flags` byte carries the event
   (`AUTOSAVE_TRACE_CC_EVT_*`), and `value32` carries an event-specific
   layout. One stage keeps the enum and the decoder change small and lets a
   reader filter all copy/clear records by one byte.
3. **Bounded volume.** No per-step or per-tick records. Each job produces at
   most four records (start, stats, end, and one failure/anomaly), each
   operation about eight more. Trace flushes are suspended while an operation
   runs (user rule), so records accumulate in RAM until it ends; §11 shows the
   budget against the 2,048 ring and the 64-record default.
4. **DEV-only.** Every hook goes through one `static inline` helper whose body
   is empty unless `DEV_MODE_LOGGING` is 1; counters live in a
   `DEV_MODE_LOGGING`-only struct. Production builds keep 0 B of new RAM and
   no new code (the helper's arguments are pure arithmetic and are optimised
   away).
   The records stay in the firmware after S075 hardware testing (user, D5).
5. **Correlation.** Each operation gets a 4-bit sequence number in its
   `OP_START`/`OP_FINISH` records; job records between them belong to that
   operation. Durations come from the records' `tick16` (ms) fields.

---

## 2. Record format (stage `c`)

Eight bytes as every AutoSave trace record: `stage='c'`, `flags=event`,
`tick16` (`time_sysTick`), `value32`.

### 2.1 Shared packings

**Job descriptor** (`JD`, 32 bits), used by `JOB_START`, `JOB_STALL`,
`QUEUE_FULL`, `PASTE_NOOP`:

| Bits | Field |
|---|---|
| 0..7 | `cc_job_t.op` (class `0x10` paste / `0x20` clear, low nibble selection) |
| 8..10 | `kind` (`cc_kind_t`: 1 step, 2 bar, 3 track, 4 Scene, 5 FX step) |
| 11..14 | `scene` (destination for pastes, object Scene for clears) |
| 15..17 | `track` (0..6; 7 = not applicable) |
| 18..24 | `start` (step 0..127, bar, track, Scene or FX step) |
| 25..31 | `end` (clears: object end; pastes: equals `start`) |

**Source** (`SRC`, used by `SOURCE_SET`):

| Bits | Field |
|---|---|
| 0..2 | kind |
| 3..6 | Scene |
| 7..9 | track |
| 10..16 | start |
| 17..23 | end |
| 24..26 | mode (`SELECT_MODE_*`) |
| 27..31 | reserved 0 |

### 2.2 Events (`flags`)

| Code | Name | `value32` layout | Producer |
|---:|---|---|---|
| 0x01 | `OP_START` | 0..1 phase (1 armed copy, 3 clear); 2..4 mode; 8..11 operation sequence; 16..23 queued jobs; 24..31 register entries | `copyClear_copyPressed()` |
| 0x02 | `OP_REFUSED` | 0 recording; 1 erasing; 2 storage busy; 3 Instrument transaction; 4 mode not allowed; 5 previous jobs queued; 8..10 mode; 16..23 queued jobs | `copyClear_copyPressed()` |
| 0x03 | `SOURCE_SET` | `SRC` | `cc_commitSource()` |
| 0x04 | `OP_RELEASE` | 0 operation started (suspension on); 1 menu was visible; 8..15 queued jobs; 16..23 register entries | `copyClear_copyReleased()` |
| 0x05 | `OP_FINISH` | 0..7 jobs done; 8..15 jobs dropped; 16..23 register passes; 24..27 operation sequence; 28..29 names result (0 none, 1 written, 2 error, 3 gave up) | `ccSvc_tick()` teardown |
| 0x10 | `JOB_START` | `JD` | `ccSvc_tick()` |
| 0x11 | `JOB_STATS` | 0..7 compaction slides (sat 255); 8..15 swap-path rewrites (sat 255); 16..23 retarget-dropped entries (sat 255); 24..27 evacuation moves (sat 15); 28..31 claim-wait ticks (sat 15) | `ccSvc_tick()` (only when any field is nonzero) |
| 0x12 | `JOB_END` | 0..1 result (0 DONE, 2 DROP); 2..7 drop reason (§2.3); 8..15 reason detail; 16..31 ticks the job ran (sat 65,535) | `ccSvc_tick()` |
| 0x13 | `JOB_STALL` | `JD` (one record per job once it has waited 5,000 ticks = 10 s) plus `flags` only; the run phase is in the following `ANOMALY` code 9 record | `ccSvc_tick()` |
| 0x14 | `CHECK_FAIL` | 0..10 free chunks before the step; 11..17 step index in the paste; 18..23 new chunks; 24..29 old chunks | `ccSvc_runPatternPaste()` check phase |
| 0x15 | `QUEUE_FULL` | `JD` of the dropped job | `ccSvc_enqueue()` |
| 0x16 | `PASTE_NOOP` | `JD` of the paste identical to its source | `ccCopy_requestPaste()` |
| 0x18 | `ANOMALY` | 0..7 code (§2.4); 8..11 track; 12..15 Scene; 16..23 step; 24..31 extra | engines |
| 0x20 | `REG_ADD` | 0..15 target; 16..19 Scene; 20..23 entries after the add; 24..27 FX lane (15 = none) | `ccClear_potTurned()` |
| 0x21 | `REG_REFUSED` | 0..15 target; 16..19 Scene; 20..23 reason (1 full, 2 other Scene's entries, 3 already pending, 4 no target) | `ccClear_potTurned()` |
| 0x22 | `REG_DONE` | 0..15 target; 16..25 steps rewritten (0..896); 26..30 swap-path rewrites (sat 31); 31 dropped (swap block could not be emptied) | `ccSvc_runRegister()` |
| 0x23 | `FX_CLEAR` | 0..15 fan-out Scene mask; 16..19 Scene; 20..23 step (15 = all steps); 24..27 lane (15 = all lanes); 28 changed | `ccClear_fxStepNow()`, `ccClear_potTurned()`, `ccClear_runFxSequence()` |
| 0x30 | `FANOUT` | 0..15 Scene mask written; 16..19 destination Scene; 20..23 kind (1 instrument, 2 kit, 3 effect, 4 send, 5 clear fx, 6 FX step paste); 24 active Scene touched; 25..28 slot (instrument/send) | Scene-level executors |
| 0x31 | `MASK_SET` | 0..15 resulting entry; 16..19 Scene; 20 0 exchange / 1 reset; 21..24 source Scene (exchange) | `copy scene`, `copy scene settings`, `clear scene`, `clear scene settings` executors |
| 0x40 | `SCRATCH` | 0 0 borrow / 1 return; 8..23 ticks waited for the borrow (sat 65,535) | `ccSvc_ensureScratch()`, `ccSvc_tick()` teardown |
| 0x41 | `FS_REFUSED` | 0..7 refused `fs_internal_op_t`; 8..15 `current_op` at the time; first refusal per loan only | `filesystem_start()` |
| 0x42 | `NAMES` | 0..1 event (0 requested, 1 written, 2 error, 3 gave up); 8..15 rows copied; 16..23 rows changed only; 24..31 refused requests before acceptance (sat 255) | `ccSvc_tick()`, `ccSvc_namesWritten()` |
| 0x50 | `SUSPEND` | 0 0 begin / 1 end; 1 facade busy at the edge; 8..15 `current_op` at the edge | `filesystem_tick()` |

### 2.3 Drop reasons (`JOB_END` bits 2..7)

| Code | Reason | Detail (bits 8..15) |
|---:|---|---|
| 0 | none (DONE) | run phase at end |
| 1 | `NO_ROOM` (paste check failed; a `CHECK_FAIL` precedes it) | step index |
| 2 | `EVACUATE_FAILED` (pre-S075 block in the swap block cannot move) | Scene |
| 3 | `ADVANCED_LIMIT` (`copy instrument`) | refusing member Scene (low nibble), slot (high nibble) |
| 4 | `FX_TYPE_MISMATCH` (FX step paste) | source type (low nibble), destination type (high nibble) |
| 5 | `NO_SOURCE` (source Scene/record missing) | — |
| 6 | `NO_SCRATCH` (Pattern engine started without the borrowed buffer) | — |
| 7 | `BAD_SELECTION` (dispatch default) | selection |
| 8 | `BAD_GEOMETRY` (paste count 0) | kind |

### 2.4 Anomaly codes (`ANOMALY` bits 0..7)

Each marks a path the implementation guards as "cannot happen" or that
indicates a broken invariant.

| Code | Name | Site | `extra` (bits 24..31) |
|---:|---|---|---|
| 1 | `GROW_UNPLACEABLE` | paste place phase: growing step, `pat_rawPlace()` failed and `patSvc_exclusiveCompactStep()` moved nothing | new chunks |
| 2 | `SWAP_RETURN_ABANDONED` | `ccSvc_swapReturnPending()`: return failed and compaction moved nothing; the step is left in the swap block | 0 |
| 3 | `SWAP_OCCUPIED` | `ccSvc_rewriteShrink()`: `pat_rawPlaceViaSwap()` failed after evacuation | new bytes / 4 |
| 4 | `REGION_REWRITE_GREW` | `ccSvc_runRegionCopy()`: `pat_rawRegionPublishRewritten()` returned 0 | 0 |
| 5 | `CLAIM_OTHER_SCENE` | `ccSvc_claim()`: another Scene's claim still held | held Scene |
| 6 | `TEARDOWN_CLAIM_HELD` | teardown found `ccSvc_claimScene` set | held Scene |
| 7 | `PLACE_SKIPPED_STEP` | paste place phase skipped a step after code 1 (data not written) | 0 |
| 8 | `RUN_STATE_RESET` | a job started while another was marked active (should not occur) | 0 |
| 9 | `STALL_PHASE` | follows `JOB_STALL`: track/step unused; Scene = job Scene; extra = `run.phase` (high nibble) and `run.sub` (low nibble) | phase/sub |

---

## 3. Stage T1 — `Core/Bank/Scene/AutosaveTrace.h`

#### T1-01 ADD the stage code after `AUTOSAVE_TRACE_STAGE_BUDGET_REPORT = 'H',` (line 196)

```c
    /*
     * c: Phase 6 copy/clear lifecycle and risk witness (S075 trace debug).
     *
     * What: flags carries one AUTOSAVE_TRACE_CC_EVT_* event; value32 carries
     * that event's layout below. Records are bounded per operation and per
     * job (no per-step or per-tick records). Why: copy/clear drops pastes and
     * clears silently by design and runs after the button is released, so
     * hardware tests need a durable witness of drops, stalls, guarded
     * "cannot happen" paths, suspension edges and the name write. While an
     * operation runs the trace flush is suspended (user rule), so these
     * records reach the card after it ends. DEV logging only; no product
     * state depends on them. Producers: Core/Menu/CopyClear/*,
     * filesystem.c. Consumer: tools/decode_devlogs.py ('c' branch).
     */
    AUTOSAVE_TRACE_STAGE_COPY_CLEAR = 'c',
```

#### T1-02 ADD event codes and layouts before `/* Append one timestamped stage record ...` (line 477)

```c
/*
 * c (COPY_CLEAR) events and value32 layouts (S075 trace debug).
 *
 * What: the flags byte of a 'c' record. Layouts are documented in
 * S075_PH6_COPYCLEAR_TRACE_DEBUG_IMPLEMENTATION.md §2 and mirrored by the
 * decoder. JD = job descriptor: op 0..7, kind 8..10, Scene 11..14, track
 * 15..17 (7 = none), start 18..24, end 25..31. Why fixed codes: a raw dump
 * stays readable and the decoder needs no firmware symbols. Producers:
 * copyClearSession.c, copyOps.c, clearOps.c, copyClearService.c,
 * filesystem.c. Consumer: tools/decode_devlogs.py.
 */
#define AUTOSAVE_TRACE_CC_EVT_OP_START     0x01u
#define AUTOSAVE_TRACE_CC_EVT_OP_REFUSED   0x02u
#define AUTOSAVE_TRACE_CC_EVT_SOURCE_SET   0x03u
#define AUTOSAVE_TRACE_CC_EVT_OP_RELEASE   0x04u
#define AUTOSAVE_TRACE_CC_EVT_OP_FINISH    0x05u
#define AUTOSAVE_TRACE_CC_EVT_JOB_START    0x10u
#define AUTOSAVE_TRACE_CC_EVT_JOB_STATS    0x11u
#define AUTOSAVE_TRACE_CC_EVT_JOB_END      0x12u
#define AUTOSAVE_TRACE_CC_EVT_JOB_STALL    0x13u
#define AUTOSAVE_TRACE_CC_EVT_CHECK_FAIL   0x14u
#define AUTOSAVE_TRACE_CC_EVT_QUEUE_FULL   0x15u
#define AUTOSAVE_TRACE_CC_EVT_PASTE_NOOP   0x16u
#define AUTOSAVE_TRACE_CC_EVT_ANOMALY      0x18u
#define AUTOSAVE_TRACE_CC_EVT_REG_ADD      0x20u
#define AUTOSAVE_TRACE_CC_EVT_REG_REFUSED  0x21u
#define AUTOSAVE_TRACE_CC_EVT_REG_DONE     0x22u
#define AUTOSAVE_TRACE_CC_EVT_FX_CLEAR     0x23u
#define AUTOSAVE_TRACE_CC_EVT_FANOUT       0x30u
#define AUTOSAVE_TRACE_CC_EVT_MASK_SET     0x31u
#define AUTOSAVE_TRACE_CC_EVT_SCRATCH      0x40u
#define AUTOSAVE_TRACE_CC_EVT_FS_REFUSED   0x41u
#define AUTOSAVE_TRACE_CC_EVT_NAMES        0x42u
#define AUTOSAVE_TRACE_CC_EVT_SUSPEND      0x50u

/* JOB_END drop reasons (bits 2..7) and ANOMALY codes (bits 0..7). */
#define AUTOSAVE_TRACE_CC_DROP_NONE             0u
#define AUTOSAVE_TRACE_CC_DROP_NO_ROOM          1u
#define AUTOSAVE_TRACE_CC_DROP_EVACUATE_FAILED  2u
#define AUTOSAVE_TRACE_CC_DROP_ADVANCED_LIMIT   3u
#define AUTOSAVE_TRACE_CC_DROP_FX_TYPE_MISMATCH 4u
#define AUTOSAVE_TRACE_CC_DROP_NO_SOURCE        5u
#define AUTOSAVE_TRACE_CC_DROP_NO_SCRATCH       6u
#define AUTOSAVE_TRACE_CC_DROP_BAD_SELECTION    7u
#define AUTOSAVE_TRACE_CC_DROP_BAD_GEOMETRY     8u
#define AUTOSAVE_TRACE_CC_ANOM_GROW_UNPLACEABLE      1u
#define AUTOSAVE_TRACE_CC_ANOM_SWAP_RETURN_ABANDONED 2u
#define AUTOSAVE_TRACE_CC_ANOM_SWAP_OCCUPIED         3u
#define AUTOSAVE_TRACE_CC_ANOM_REGION_REWRITE_GREW   4u
#define AUTOSAVE_TRACE_CC_ANOM_CLAIM_OTHER_SCENE     5u
#define AUTOSAVE_TRACE_CC_ANOM_TEARDOWN_CLAIM_HELD   6u
#define AUTOSAVE_TRACE_CC_ANOM_PLACE_SKIPPED_STEP    7u
#define AUTOSAVE_TRACE_CC_ANOM_RUN_STATE_RESET       8u
#define AUTOSAVE_TRACE_CC_ANOM_STALL_PHASE           9u
```

Header block of the file (lines 1–12) needs no change: it already states that
every API is a no-op when `DEV_MODE_LOGGING` is 0.

---

## 4. Stage T2 — `Core/Menu/CopyClear/copyClearService.h/.c`

### 4.1 Header: the trace helper and packers

#### T2-01 `copyClearService.h` — ADD includes after `#include "copyClearSession.h"` (line 22)

```c
#include "config.h"
#include "AutosaveTrace.h"
```

#### T2-02 `copyClearService.h` — ADD before `#endif /* COPY_CLEAR_SERVICE_H_ */`

```c
/*
 * Copy/clear diagnostic trace (S075 trace debug; DEV_MODE_LOGGING only).
 *
 * What: ccTrace() writes one AutoSave trace record with stage 'c', the event
 * in flags and an event-specific value32 (layouts: AutosaveTrace.h
 * AUTOSAVE_TRACE_CC_EVT_*, and
 * S075_PH6_COPYCLEAR_TRACE_DEBUG_IMPLEMENTATION.md §2). ccTrace_job() packs a
 * job descriptor; ccTrace_anomaly() packs an anomaly record. Why inline:
 * production builds (DEV_MODE_LOGGING 0) compile every call to nothing, so
 * the hooks cost no flash, RAM or cycles there. Inputs: event code and
 * value. Output: one record in the DEV trace ring; no product state changes.
 * Callers: copyClearSession.c, copyOps.c, clearOps.c, copyClearService.c.
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

/*
 * Per-job trace counters fed by copyOps.c/clearOps.c (DEV only; no-ops in
 * production). ccSvc_traceRetargetDropped(): automation entries removed by
 * retargeting. ccSvc_traceDropReason(): reason code and detail for the
 * JOB_END record of the current job. ccSvc_traceOpSequence(): the 4-bit
 * sequence number of the running operation.
 */
void ccSvc_traceRetargetDropped(uint8_t count);
void ccSvc_traceDropReason(uint8_t reason, uint8_t detail);
uint8_t ccSvc_traceOpSequence(void);
```

### 4.2 Source: DEV-only counters

#### T2-03 `copyClearService.c` — ADD after `static uint8_t *ccSvc_buf;` (line 82)

```c
#if DEV_MODE_LOGGING
/*
 * Copy/clear trace counters (S075 trace debug; DEV_MODE_LOGGING only,
 * 18 B SRAM1, 0 B in production).
 *
 * What: per-operation totals (sequence, jobs done/dropped, register passes,
 * name-write result) and per-job statistics (ticks, compaction slides,
 * swap-path rewrites, retarget drops, evacuations, claim-wait ticks, drop
 * reason/detail, stall latch), plus the borrow wait counter and the register
 * pass counters. Why: summaries replace per-step records so a whole
 * operation fits the trace ring while flushes are suspended. Reset: job
 * fields at JOB_START, operation fields at OP_START
 * (ccSvc_interactionStarted()). Accessors: ccSvc_tick(), the engines, and
 * the ccSvc_trace* functions.
 */
static struct {
    uint8_t op_seq;
    uint8_t jobs_done;
    uint8_t jobs_dropped;
    uint8_t reg_passes;
    uint8_t names_result;
    uint16_t job_ticks;
    uint8_t slides;
    uint8_t swaps;
    uint8_t retarget_dropped;
    uint8_t evacuations;
    uint8_t claim_wait;
    uint8_t drop_reason;
    uint8_t drop_detail;
    uint8_t stalled;
    uint16_t scratch_wait;
    uint16_t reg_steps;
} ccSvc_traceState;
#define CC_TRACE_SAT8(field) \
    do { if ((field) != 0xFFu) (field)++; } while (0)
#define CC_TRACE_STALL_TICKS 5000u   /* 10 s at 500 Hz */
#else
#define CC_TRACE_SAT8(field) do { } while (0)
#endif
```

#### T2-04 `copyClearService.c` — ADD the counter accessors after `ccSvc_scratch()` (line 913)

```c
/*
 * Trace counter feeds (contract in copyClearService.h). DEV only; empty in
 * production.
 */
void ccSvc_traceRetargetDropped(uint8_t count)
{
#if DEV_MODE_LOGGING
    uint16_t sum = (uint16_t)ccSvc_traceState.retarget_dropped + count;

    ccSvc_traceState.retarget_dropped = (uint8_t)((sum > 0xFFu) ? 0xFFu : sum);
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
    return (uint8_t)(ccSvc_traceState.op_seq & 0x0Fu);
#else
    return 0u;
#endif
}
```

### 4.3 Service hooks

#### T2-05 `ccSvc_claim()` (line 86) — ADD the other-Scene anomaly and the wait count

Replace the body with:

```c
static uint8_t ccSvc_claim(uint8_t scene)
{
    if (ccSvc_claimScene != CC_NO_SCENE && ccSvc_claimScene != scene) {
        /*
         * S075 trace: a claim for another Scene is still held. Jobs release
         * their claim on every exit, so this marks a broken invariant.
         */
        ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_CLAIM_OTHER_SCENE, scene, 7u,
                        0u, ccSvc_claimScene);
        return 0u;
    }
    ccSvc_claimScene = scene;
    if (patSvc_beginExclusive(scene))
        return 1u;
#if DEV_MODE_LOGGING
    /* S075 trace: ticks spent waiting for queued Pattern work to drain. */
    if (ccSvc_traceState.claim_wait < 15u)
        ccSvc_traceState.claim_wait++;
#endif
    return 0u;
}
```

#### T2-06 `ccSvc_ensureScratch()` (line 104) — ADD the borrow witness

```c
static uint8_t ccSvc_ensureScratch(void)
{
    if (ccSvc_buf)
        return 1u;
    ccSvc_buf = filesystem_borrowNameCacheScratch();
    if (!ccSvc_buf) {
#if DEV_MODE_LOGGING
        /* S075 trace: the facade is busy; count ticks until the loan. */
        if (ccSvc_traceState.scratch_wait != 0xFFFFu)
            ccSvc_traceState.scratch_wait++;
#endif
        return 0u;
    }
    memset(&ccSvc_buf[CC_SCRATCH_REMAP_OFFSET], CC_REMAP_NONE,
           FS_HCNAMES_ROW_COUNT);
    /*
     * S075 trace: SCRATCH borrow, with the ticks waited for an idle facade
     * (R7: the loan blocks other filesystem work until it is returned).
     */
#if DEV_MODE_LOGGING
    ccTrace(AUTOSAVE_TRACE_CC_EVT_SCRATCH,
            (uint32_t)ccSvc_traceState.scratch_wait << 8u);
    ccSvc_traceState.scratch_wait = 0u;
#endif
    return 1u;
}
```

#### T2-07 `ccSvc_rewriteShrink()` (line 139) — ADD swap counting and the swap-occupied anomaly

```c
    if (pat_rawPlace(scene, track, step, block, trigger_mode))
        return CC_RUN_DONE;
    CC_TRACE_SAT8(ccSvc_traceState.swaps);  /* S075 trace: swap path used */
    if (!pat_rawPlaceViaSwap(scene, track, step, block, trigger_mode)) {
        /*
         * S075 trace: the swap block is occupied although the claim phase
         * emptied it; the step keeps its old block (no change published).
         */
        ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_SWAP_OCCUPIED, scene, track,
                        step, (uint8_t)(bytes / 4u));
        return CC_RUN_DONE;
    }
```

(The existing comment `/* swap occupied: impossible after evacuation */` is
replaced by the block above.)

#### T2-08 `ccSvc_swapReturnPending()` (line 158) — ADD slide counting and the abandon anomaly

```c
static uint8_t ccSvc_swapReturnPending(uint8_t scene, uint8_t track,
                                       uint8_t step)
{
    if (pat_rawSwapReturn(scene, track, step)) {
        ccSvc_runState.aux &= (uint16_t)~1u;
        return CC_RUN_DONE;
    }
    if (patSvc_exclusiveCompactStep(scene)) {
        CC_TRACE_SAT8(ccSvc_traceState.slides);   /* S075 trace */
        return CC_RUN_WAIT;
    }
    /*
     * Nothing left to move: leave the step in the swap block (the next
     * claim's evacuation moves it once room exists). S075 trace: this is
     * not expected, because the freed old run is at least the block's size.
     */
    ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_SWAP_RETURN_ABANDONED, scene,
                    track, step, 0u);
    ccSvc_runState.aux &= (uint16_t)~1u;
    return CC_RUN_DONE;
}
```

#### T2-09 `ccSvc_claimAndEvacuate()` (line 172) — ADD evacuation counting and the drop reason

```c
    r = patSvc_exclusiveEvacuateSwapStep(scene);
    if (r == 1u) {
#if DEV_MODE_LOGGING
        if (ccSvc_traceState.evacuations < 15u)
            ccSvc_traceState.evacuations++;
#endif
        return CC_RUN_WAIT;
    }
    if (r == 2u) {
        /* S075 trace: a pre-S075 block cannot leave the swap block. */
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_EVACUATE_FAILED, scene);
        ccSvc_release();
        return CC_RUN_DROP;
    }
```

#### T2-10 `ccSvc_runPatternPaste()` (line 304) — ADD geometry/scratch reasons, retarget count, `CHECK_FAIL`, slides and the unplaceable anomaly

- At the guard `if (g.count == 0u || !ccSvc_buf) return CC_RUN_DROP;`:

```c
    if (g.count == 0u || !ccSvc_buf) {
        /* S075 trace: why the engine refused before touching anything. */
        ccSvc_traceDropReason(g.count == 0u
                                  ? AUTOSAVE_TRACE_CC_DROP_BAD_GEOMETRY
                                  : AUTOSAVE_TRACE_CC_DROP_NO_SCRATCH,
                              job->kind);
        return CC_RUN_DROP;
    }
```

- Snapshot phase, around `count = ccCopy_retargetEntries(&ctx, autos, count);`
  (line 353):

```c
                uint8_t before = count;

                count = ccCopy_retargetEntries(&ctx, autos, count);
                /* S075 trace: automation entries dropped by retargeting. */
                ccSvc_traceRetargetDropped((uint8_t)(before - count));
```

- Check phase, replace the drop at `if (run->aux < new_c) { ccSvc_release(); return CC_RUN_DROP; }` (lines 388–391):

```c
                if (run->aux < new_c) {
                    /*
                     * S075 trace (R1/R2): the growing step at cursor-1 needs
                     * new_c chunks while only aux are free (old block still
                     * live). Nothing has been changed yet.
                     */
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
```

- Place phase, growing step (lines 429–435):

```c
                if (patSvc_exclusiveCompactStep(job->scene)) {
                    CC_TRACE_SAT8(ccSvc_traceState.slides);  /* S075 trace */
                    return CC_RUN_WAIT;
                }
                /*
                 * S075 trace (R4): the check guaranteed a run; sliding
                 * compaction is finished and the block still does not fit.
                 * The step keeps its old content.
                 */
                ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_GROW_UNPLACEABLE,
                                job->scene, job->track, ds,
                                (uint8_t)(bytes / 4u));
                run->cursor++;
                continue;
```

  (The comment `/* cannot happen after the check */` is replaced.)

#### T2-11 `ccSvc_runRegionCopy()` (line 653) — ADD retarget count and the rewrite-grew anomaly

```c
            uint8_t before = count;

            count = ccCopy_retargetEntries(&ctx, autos, count);
            ccSvc_traceRetargetDropped((uint8_t)(before - count));
            bytes = pat_rawEncode(block, sp.flags, sp.note, sp.velocity,
                                  sp.probability, autos, count);
            if (!pat_rawRegionPublishRewritten(src_scene, dst_scene, index,
                                               bytes ? block : 0))
                /*
                 * S075 trace (R4): retargeting never grows a block; the
                 * copied block was published unchanged instead.
                 */
                ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_REGION_REWRITE_GREW,
                                dst_scene, (uint8_t)(index / NUM_STEPS),
                                (uint8_t)(index % NUM_STEPS), 0u);
```

#### T2-12 `ccSvc_runRegister()` (line 728) — ADD the pass record

- Count rewrites: after a successful `ccSvc_rewriteShrink(...)` call inside
  the `if (kept != count)` branch, `ccSvc_traceState.reg_steps++` (DEV only,
  saturating at 896).
- Before `ccSvc_release();` at the head-drop code (after `run->sub = 2u;`,
  line 792):

```c
    /*
     * S075 trace (R9): one REG_DONE per pass: target, steps rewritten,
     * swap-path rewrites, and whether the pass was dropped because the swap
     * block could not be emptied (sub reached 2 from the claim phase).
     */
#if DEV_MODE_LOGGING
    ccTrace(AUTOSAVE_TRACE_CC_EVT_REG_DONE,
            (uint32_t)target |
            ((uint32_t)(ccSvc_traceState.reg_steps & 0x3FFu) << 16u) |
            ((uint32_t)((ccSvc_traceState.swaps > 31u) ? 31u
                                                      : ccSvc_traceState.swaps)
             << 26u) |
            ((uint32_t)(run->cursor < PAT_STEPS_PER_SCENE) << 31u));
    ccSvc_traceState.reg_steps = 0u;
    ccSvc_traceState.swaps = 0u;
    CC_TRACE_SAT8(ccSvc_traceState.reg_passes);
#endif
```

#### T2-13 `ccSvc_namesWritten()` (line 840) — ADD the result record

After `filesystem_ack();`:

```c
    /* S075 trace (R8): HCNAMES copy update finished. */
#if DEV_MODE_LOGGING
    ccSvc_traceState.names_result = ok ? 1u : 2u;
    ccTrace(AUTOSAVE_TRACE_CC_EVT_NAMES, ok ? 1u : 2u);
#endif
```

#### T2-14 `ccSvc_enqueue()` (line 874) — ADD the queue-full record

```c
uint8_t ccSvc_enqueue(const cc_job_t *job)
{
    if (!job)
        return 0u;
    if (ccSvc_count >= CC_QUEUE_SIZE) {
        /* S075 trace (R1): a fifth paste/clear is dropped (spec §3.1). */
        ccTrace(AUTOSAVE_TRACE_CC_EVT_QUEUE_FULL, ccTrace_job(job));
        return 0u;
    }
    ...
```

#### T2-15 `ccSvc_interactionStarted()` (line 903) — ADD the operation counter reset

```c
void ccSvc_interactionStarted(void)
{
    ccSvc_flags &= (uint8_t)~CC_SVC_ENDED;
    /*
     * S075 trace: a new operation gets the next sequence number and fresh
     * per-operation totals (jobs, register passes, name result).
     */
#if DEV_MODE_LOGGING
    ccSvc_traceState.op_seq = (uint8_t)((ccSvc_traceState.op_seq + 1u) & 0x0Fu);
    ccSvc_traceState.jobs_done = 0u;
    ccSvc_traceState.jobs_dropped = 0u;
    ccSvc_traceState.reg_passes = 0u;
    ccSvc_traceState.names_result = 0u;
#endif
}
```

#### T2-16 `ccSvc_tick()` (line 959) — ADD job start/stats/end/stall, names request/give-up, teardown records

Job branch (lines 963–978) becomes:

```c
    if ((ccSvc_flags & CC_SVC_JOB_ACTIVE) != 0u) {
        const cc_job_t *job = &ccSvc_queue[ccSvc_head];

        r = ((job->op & CC_JOB_CLASS_MASK) == CC_JOB_PASTE)
                ? ccCopy_runJob(job)
                : ccClear_runJob(job);
#if DEV_MODE_LOGGING
        if (ccSvc_traceState.job_ticks != 0xFFFFu)
            ccSvc_traceState.job_ticks++;
        /*
         * S075 trace (R5): one stall witness per job after 10 s of waiting
         * (claim never granted, Preset worker never idle, endless swap
         * return). The phase record follows it.
         */
        if (r == CC_RUN_WAIT && !ccSvc_traceState.stalled &&
            ccSvc_traceState.job_ticks >= CC_TRACE_STALL_TICKS) {
            ccSvc_traceState.stalled = 1u;
            ccTrace(AUTOSAVE_TRACE_CC_EVT_JOB_STALL, ccTrace_job(job));
            ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_STALL_PHASE, job->scene,
                            7u, 0u,
                            (uint8_t)((ccSvc_runState.phase << 4u) |
                                      (ccSvc_runState.sub & 0x0Fu)));
        }
#endif
        if (r == CC_RUN_WAIT)
            return;
        /*
         * S075 trace (R1/R3/R11): per-job statistics when any is nonzero,
         * then the result with its drop reason and the job's tick count.
         */
#if DEV_MODE_LOGGING
        {
            uint32_t stats =
                (uint32_t)ccSvc_traceState.slides |
                ((uint32_t)ccSvc_traceState.swaps << 8u) |
                ((uint32_t)ccSvc_traceState.retarget_dropped << 16u) |
                ((uint32_t)ccSvc_traceState.evacuations << 24u) |
                ((uint32_t)ccSvc_traceState.claim_wait << 28u);

            if (stats != 0u)
                ccTrace(AUTOSAVE_TRACE_CC_EVT_JOB_STATS, stats);
            ccTrace(AUTOSAVE_TRACE_CC_EVT_JOB_END,
                    (uint32_t)r |
                    ((uint32_t)(ccSvc_traceState.drop_reason & 0x3Fu) << 2u) |
                    ((uint32_t)((r == CC_RUN_DROP)
                                    ? ccSvc_traceState.drop_detail
                                    : (uint8_t)((ccSvc_runState.phase << 4u) |
                                                (ccSvc_runState.sub & 0x0Fu)))
                     << 8u) |
                    ((uint32_t)ccSvc_traceState.job_ticks << 16u));
            if (r == CC_RUN_DROP)
                CC_TRACE_SAT8(ccSvc_traceState.jobs_dropped);
            else
                CC_TRACE_SAT8(ccSvc_traceState.jobs_done);
        }
#endif
        ccSvc_release();
        ...  (unchanged pop)
    }
```

Job start (lines 985–991):

```c
    if (ccSvc_count != 0u) {
        if (!ccSvc_ensureScratch())
            return;
        memset(&ccSvc_runState, 0, sizeof(ccSvc_runState));
        ccSvc_flags |= CC_SVC_JOB_ACTIVE;
        /*
         * S075 trace: JOB_START with the job descriptor; the per-job
         * statistics restart here.
         */
#if DEV_MODE_LOGGING
        ccSvc_traceState.job_ticks = 0u;
        ccSvc_traceState.slides = 0u;
        ccSvc_traceState.swaps = 0u;
        ccSvc_traceState.retarget_dropped = 0u;
        ccSvc_traceState.evacuations = 0u;
        ccSvc_traceState.claim_wait = 0u;
        ccSvc_traceState.drop_reason = AUTOSAVE_TRACE_CC_DROP_NONE;
        ccSvc_traceState.drop_detail = 0u;
        ccSvc_traceState.stalled = 0u;
        ccTrace(AUTOSAVE_TRACE_CC_EVT_JOB_START,
                ccTrace_job(&ccSvc_queue[ccSvc_head]));
#endif
        return;
    }
```

Register start (lines 992–998): reset `reg_steps` and `swaps` the same way
(no record; `REG_DONE` closes the pass).

Name write (lines 1001–1016): count rows before the request and record it;
record the give-up:

```c
    if ((ccSvc_flags & CC_SVC_NAMES_DIRTY) != 0u) {
        if (ccSvc_ensureScratch() &&
            filesystem_requestCopyResidentNames(ccSvc_namesWritten)) {
            /*
             * S075 trace (R8): request accepted; rows copied (remap to a
             * row) and rows changed only (0xFE), and refusals before it.
             */
#if DEV_MODE_LOGGING
            {
                uint8_t copied = 0u;
                uint8_t changed = 0u;
                uint16_t row;

                for (row = 0u; row < FS_HCNAMES_ROW_COUNT; row++) {
                    uint8_t v = ccSvc_buf[CC_SCRATCH_REMAP_OFFSET + row];

                    if (v == CC_REMAP_CHANGED)
                        changed++;
                    else if (v != CC_REMAP_NONE)
                        copied++;
                }
                ccTrace(AUTOSAVE_TRACE_CC_EVT_NAMES,
                        ((uint32_t)copied << 8u) | ((uint32_t)changed << 16u) |
                        ((uint32_t)((ccSvc_nameRetry > 255u) ? 255u
                                                             : ccSvc_nameRetry)
                         << 24u));
            }
#endif
            ...  (unchanged flag update)
        }
        if (++ccSvc_nameRetry < CC_NAME_RETRY_TICKS)
            return;
        /* Card absent or facade stuck: names stay as they are. */
#if DEV_MODE_LOGGING
        ccSvc_traceState.names_result = 3u;
        ccTrace(AUTOSAVE_TRACE_CC_EVT_NAMES, 3u | (255u << 24u));
#endif
        ...
    }
```

Teardown (lines 1017–1021):

```c
    /*
     * S075 trace: a claim must never survive the last job (R4); the loan
     * ends (SCRATCH return); OP_FINISH closes the operation with its totals.
     * Record only (user, D3): the anomaly is witnessed, nothing is repaired.
     */
    if (ccSvc_claimScene != CC_NO_SCENE)
        ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_TEARDOWN_CLAIM_HELD,
                        ccSvc_claimScene, 7u, 0u, ccSvc_claimScene);
    if (ccSvc_buf) {
        filesystem_returnNameCacheScratch();
        ccSvc_buf = 0;
        ccTrace(AUTOSAVE_TRACE_CC_EVT_SCRATCH, 1u);
    }
    ccSvc_flags &= (uint8_t)~CC_SVC_ENDED;
#if DEV_MODE_LOGGING
    ccTrace(AUTOSAVE_TRACE_CC_EVT_OP_FINISH,
            (uint32_t)ccSvc_traceState.jobs_done |
            ((uint32_t)ccSvc_traceState.jobs_dropped << 8u) |
            ((uint32_t)ccSvc_traceState.reg_passes << 16u) |
            ((uint32_t)(ccSvc_traceState.op_seq & 0x0Fu) << 24u) |
            ((uint32_t)(ccSvc_traceState.names_result & 0x3u) << 28u));
#endif
    copyClear_serviceFinished();
```

Decision D3 (user): record only. The teardown does not release a claim it
finds held; the `TEARDOWN_CLAIM_HELD` anomaly is the evidence for the bug.

#### T2-17 `ccSvc_tick()` — anomaly 8 (`RUN_STATE_RESET`)

Not needed in code: `CC_SVC_JOB_ACTIVE` and `CC_SVC_REG_ACTIVE` are tested
before a new job starts, so the state cannot be reset while active. The code
is reserved in the decoder for a future check.

---

## 5. Stage T3 — `Core/Menu/CopyClear/copyClearSession.c`

#### T3-01 `copyClear_copyPressed()` (line 443) — ADD refusal and start records

```c
uint8_t copyClear_copyPressed(uint8_t shift_held)
{
    uint8_t mode = buttonHandler_getMode();
    uint32_t refused = 0u;

    /*
     * Refusals are silent (spec §3.3). S075 trace (R12): OP_REFUSED records
     * every reason that applied, so a press that "did nothing" on hardware
     * can be explained.
     */
    if (seq_recordActive)                     refused |= 1u << 0u;
    if (seq_eraseActive)                      refused |= 1u << 1u;
    if (menu_isStorageBusy())                 refused |= 1u << 2u;
    if (menu_loadInstrumentTransactionBusy()) refused |= 1u << 3u;
    if (!cc_modeAllowed(mode))                refused |= 1u << 4u;
    if (ccSvc_jobCount() != 0u)               refused |= 1u << 5u;
    if (refused != 0u) {
        ccTrace(AUTOSAVE_TRACE_CC_EVT_OP_REFUSED,
                refused | ((uint32_t)(mode & 0x7u) << 8u) |
                ((uint32_t)ccSvc_jobCount() << 16u));
        return 0u;
    }
    ...  (unchanged arming)
    /*
     * S075 trace: OP_START with phase, mode, the new operation sequence
     * (ccSvc_interactionStarted() above advanced it), queued jobs and
     * register entries still running from an earlier operation.
     */
    ccTrace(AUTOSAVE_TRACE_CC_EVT_OP_START,
            (uint32_t)(cc_state.phase & 0x3u) |
            ((uint32_t)(mode & 0x7u) << 2u) |
            ((uint32_t)ccSvc_traceOpSequence() << 8u) |
            ((uint32_t)ccSvc_jobCount() << 16u));
    return 1u;
}
```

The register count is not reachable from the session without a new service
accessor; bits 24..31 stay 0 here (the decoder prints them only when nonzero).

#### T3-02 `cc_commitSource()` (line 188) — ADD the source record

```c
static void cc_commitSource(void)
{
    cc_state.phase = CC_OP_COPY;
    cc_start();
    led_setBlinkLed(LED_COPY, 1u);
    cc_openMenu(ccCopy_menuForSource(&cc_source));
    /* S075 trace: the final source after the range rule (spec §4.2). */
    ccTrace(AUTOSAVE_TRACE_CC_EVT_SOURCE_SET,
            (uint32_t)(cc_source.kind & 0x7u) |
            ((uint32_t)(cc_source.scene & 0xFu) << 3u) |
            ((uint32_t)(cc_source.track & 0x7u) << 7u) |
            ((uint32_t)(cc_source.start & 0x7Fu) << 10u) |
            ((uint32_t)(cc_source.end & 0x7Fu) << 17u) |
            ((uint32_t)(buttonHandler_getMode() & 0x7u) << 24u));
}
```

#### T3-03 `copyClear_copyReleased()` (line 467) — ADD the release record

Before `ccSvc_interactionEnded();`:

```c
    /*
     * S075 trace: end of menu interaction; whether the suspension started
     * and how much background work remains.
     */
    ccTrace(AUTOSAVE_TRACE_CC_EVT_OP_RELEASE,
            (uint32_t)(cc_state.started ? 1u : 0u) |
            ((uint32_t)(menu_was_visible ? 1u : 0u) << 1u) |
            ((uint32_t)ccSvc_jobCount() << 8u));
```

Includes: `copyClearService.h` is already included (it now brings
`AutosaveTrace.h` and `config.h`).

---

## 6. Stage T4 — `copyOps.c` and `clearOps.c`

### 6.1 `copyOps.c`

#### T4-01 `ccCopy_requestPaste()` (line 88) — ADD the identical-paste record

Each `return 0u;` in the identity `switch` (lines 102, 109, 113, 118) becomes
a jump to one exit that records the would-be job:

```c
    /* A paste identical to its source does nothing (spec §4.3). */
    ...  (job fields filled first, as below, so the record can show them)
    if (identical) {
        /* S075 trace (R1): the press was valid but changed nothing. */
        ccTrace(AUTOSAVE_TRACE_CC_EVT_PASTE_NOOP, ccTrace_job(&job));
        return 0u;
    }
```

Implementation steps: move the `job.*` assignments above the `switch`,
replace each `return 0u;` inside the identity cases with `identical = 1u;
break;`, keep the `default: return 0u;` for an invalid kind.

#### T4-02 `ccCopy_runInstrument()` (line 536) — ADD the Advanced-limit reason and the fan-out record

```c
    for (m = 0u; m < SCENE_COUNT; m++)
        if ((mask & ccCopy_bit(m)) != 0u &&
            !instrumentManager_typeSelectableForSceneSlot(m, d_slot, type)) {
            /* S075 trace (R1): member m would exceed two Advanced types. */
            ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_ADVANCED_LIMIT,
                                  (uint8_t)((m & 0x0Fu) | (d_slot << 4u)));
            return CC_RUN_DROP;
        }
    ...
    preset_startInstrumentCopy(src->scene, s_slot, mask, d_slot);
    /* S075 trace (R10): Scenes reached by the Instrument paste. */
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)mask | ((uint32_t)(job->scene & 0xFu) << 16u) |
            (1u << 20u) | ((uint32_t)ccCopy_activeIn(mask) << 24u) |
            ((uint32_t)(d_slot & 0xFu) << 25u));
```

#### T4-03 `ccCopy_runKit()` (line 588) — ADD the no-source reason and the fan-out record

- `if (!source) return CC_RUN_DROP;` → set reason `NO_SOURCE` first.
- Before `bank_revalidateVoiceEditMasks();`:

```c
    /* S075 trace (R10): Scenes that received the Kit (source excluded). */
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)(mask & (uint16_t)~ccCopy_bit(src->scene)) |
            ((uint32_t)(job->scene & 0xFu) << 16u) | (2u << 20u) |
            ((uint32_t)ccCopy_activeIn(mask) << 24u));
```

#### T4-04 `ccCopy_runEffect()` (line 629) — same pattern

`NO_SOURCE` reason at the `!record` drop; after `written =
effects_pasteRecord(...)`: `FANOUT` with `written`, kind 3.

#### T4-05 `ccCopy_runSceneSettings()` / `ccCopy_runScene()` (lines 656, 679) — ADD the mask record

After `bank_exchangeVoiceEditMask(src->scene, dst);` in both:

```c
    /* S075 trace (R10): the destination's edit-mask entry after exchange. */
    ccTrace(AUTOSAVE_TRACE_CC_EVT_MASK_SET,
            (uint32_t)bank_sceneMaskVoiceEditForScene(dst) |
            ((uint32_t)(dst & 0xFu) << 16u) |
            ((uint32_t)(src->scene & 0xFu) << 21u));
```

and `NO_SOURCE` at their `!source` drops.

#### T4-06 `ccCopy_runFxSteps()` (line 753) — ADD the type-mismatch reason and the fan-out record

Split the guard so the type mismatch is distinguishable:

```c
    if (!s || !d || !scratch ||
        src->start >= EFFECT_SEQ_STEP_COUNT || src->end >= EFFECT_SEQ_STEP_COUNT) {
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_NO_SOURCE, 0u);
        return CC_RUN_DROP;
    }
    if (s->type != d->type) {
        /* S075 trace (R1): FX step paste onto another Effect type. */
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_FX_TYPE_MISMATCH,
                              (uint8_t)((s->type & 0x0Fu) |
                                        ((d->type & 0x0Fu) << 4u)));
        return CC_RUN_DROP;
    }
```

After the paste loop: `FANOUT` with `bank_sceneFanoutMask(job->scene)`, kind 6.

#### T4-07 `ccCopy_runJob()` (line 790) — ADD the bad-selection reason

At `default: return CC_RUN_DROP;` (lines 811, 816):
`ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_BAD_SELECTION, sel);` first.

### 6.2 `clearOps.c`

#### T4-10 `ccClear_fxStepNow()` (line 139) — ADD the FX clear record

```c
void ccClear_fxStepNow(uint8_t scene, uint8_t step)
{
    uint8_t changed;

    if (step >= EFFECT_SEQ_STEP_COUNT)
        return;
    changed = effects_clearSeqLanes(scene, (uint16_t)(1u << step), 0xFFFFu);
    if (changed)
        ccClear_effectRowsChanged(bank_sceneFanoutMask(scene));
    /* S075 trace (R10): EFFECTS SEQ clear, fanned out. */
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FX_CLEAR,
            (uint32_t)bank_sceneFanoutMask(scene) |
            ((uint32_t)(scene & 0xFu) << 16u) |
            ((uint32_t)(step & 0xFu) << 20u) | (0xFu << 24u) |
            ((uint32_t)(changed ? 1u : 0u) << 28u));
    ccClear_effectUi();
}
```

#### T4-11 `ccClear_potTurned()` (line 148) — ADD register add/refusal and lane records

Each early return gets its reason:

| Return | Record |
|---|---|
| no target and no lane | `REG_REFUSED` reason 4 |
| target already pending (returns 1) | `REG_REFUSED` reason 3 |
| register full | `REG_REFUSED` reason 1 |
| register holds another Scene | `REG_REFUSED` reason 2 |
| lane cleared | `FX_CLEAR` with step 15 (all), lane, changed |
| target added | `REG_ADD` with target, Scene, entries after, lane |

```c
/* S075 trace helper: REG_REFUSED for one pot clear (R9). */
static void ccClear_traceRegRefused(uint16_t target, uint8_t scene,
                                    uint8_t reason)
{
    ccTrace(AUTOSAVE_TRACE_CC_EVT_REG_REFUSED,
            (uint32_t)target | ((uint32_t)(scene & 0xFu) << 16u) |
            ((uint32_t)(reason & 0xFu) << 20u));
}
```

`REG_ADD` needs the entry count after the add: ADD to `copyClearService.h`
and `.c` a read accessor `uint8_t ccSvc_registerCount(void);` with the block

```c
/* Number of targets waiting in the pot-clear register (0..8; trace use). */
```

#### T4-12 `ccClear_runSend()` (line 181) — `FANOUT` kind 4 with the slot

#### T4-13 `ccClear_runSceneSettings()` / `ccClear_runScene()` (lines 200, 223) — `MASK_SET` reset

After `bank_resetVoiceEditMaskToSelf(...)`:

```c
    /* S075 trace (R10): the Scene's edit-mask entry was reset to itself. */
    ccTrace(AUTOSAVE_TRACE_CC_EVT_MASK_SET,
            (uint32_t)bank_sceneMaskVoiceEditForScene(scene) |
            ((uint32_t)(scene & 0xFu) << 16u) | (1u << 20u));
```

#### T4-14 `ccClear_runFx()` (line 273) — `FANOUT` kind 5 with `written`

#### T4-15 `ccClear_runFxSequence()` (line 289) — `FX_CLEAR` step 15, lane 15

#### T4-16 `ccClear_runJob()` (line 297) — `BAD_SELECTION` reason at the defaults

---

## 7. Stage T5 — `Core/Bank/Scene/Pattern/PatternStackService.c`

#### T5-01 `patSvc_exclusiveMove()` (line 1399) — REMOVE the per-move PatternTrace record

Decision D4 (user): approved; the per-move record stops.

Remove:

```c
    patternTrace_record(PAT_TRACE_STAGE_TIER2_RELOC, (uint8_t)(scene & 0x0Fu),
                        patSvc_relocationValue(address_index, old_offset,
                                               new_offset));
```

and ADD in its place:

```c
    /*
     * S075 trace debug: no per-move PatternTrace record here. Copy/clear moves
     * run while trace flushes are suspended, and one fragmented paste can make
     * hundreds of moves; per-move records would overwrite the 32-record
     * PatternTrace ring (and every other record in it) before the next flush.
     * The copy/clear JOB_STATS / REG_DONE records carry the move counts
     * instead. Reactive and repair relocations keep their R/L records.
     */
```

Update the function's comment block (above line 1399): replace "layout-only
AutoSave dirty mark and a relocation trace record" with "layout-only AutoSave
dirty mark (the move count is traced by the copy/clear caller)".

`patSvc_relocationValue()` stays (used by reactive and repair relocations).

---

## 8. Stage T6 — `Core/Hardware/SD/filesystem.c`

#### T6-01 ADD two DEV-only statics after `static uint8_t fs_name_cache_borrowed;` (line 1675)

```c
#if DEV_MODE_LOGGING
/*
 * S075 trace debug (DEV only, 2 B): last observed copy/clear suspension
 * state for edge records in filesystem_tick(), and the one-per-loan latch
 * for the FS_REFUSED witness in filesystem_start().
 */
static uint8_t fs_cc_suspended_prev;
static uint8_t fs_cc_refusal_reported;
#endif
```

#### T6-02 `filesystem_borrowNameCacheScratch()` (line 1784) — reset the refusal latch

After `fs_name_cache_borrowed = 1u;`:

```c
#if DEV_MODE_LOGGING
    fs_cc_refusal_reported = 0u;   /* S075 trace: one FS_REFUSED per loan */
#endif
```

#### T6-03 `filesystem_tick()` — ADD the suspension edge record after `const uint8_t cc_suspended = copyClear_backgroundSuspended();` (line 25553)

```c
#if DEV_MODE_LOGGING
    /*
     * S075 trace debug (R6): one SUSPEND record per suspension edge, with
     * whether a writer was still running (it finishes normally) and which.
     */
    if (cc_suspended != fs_cc_suspended_prev) {
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_COPY_CLEAR, AUTOSAVE_TRACE_CC_EVT_SUSPEND,
            (uint32_t)(cc_suspended ? 0u : 1u) |
            ((uint32_t)(status == FS_STATUS_BUSY ? 1u : 0u) << 1u) |
            ((uint32_t)(current_op & 0xFFu) << 8u));
        fs_cc_suspended_prev = cc_suspended;
    }
#endif
```

#### T6-04 `filesystem_start()` — ADD the refusal witness at `if (fs_name_cache_borrowed && op != FS_INTERNAL_OP_UPDATE_HCNAMES_COPY)` (line 25919)

```c
    if (fs_name_cache_borrowed && op != FS_INTERNAL_OP_UPDATE_HCNAMES_COPY) {
#if DEV_MODE_LOGGING
        /*
         * S075 trace debug (R7): the first operation refused during this
         * loan, so a test can see what the loan blocked (Load/Save browser,
         * Instrument browsing). Callers retry, so only one record per loan.
         */
        if (!fs_cc_refusal_reported) {
            fs_cc_refusal_reported = 1u;
            autosaveTrace_record(AUTOSAVE_TRACE_STAGE_COPY_CLEAR,
                                 AUTOSAVE_TRACE_CC_EVT_FS_REFUSED,
                                 (uint32_t)(op & 0xFFu) |
                                 ((uint32_t)(current_op & 0xFFu) << 8u));
        }
#endif
        return false;
    }
```

`AutosaveTrace.h` is already included by `filesystem.c`.

---

## 9. Stage T7 — `tools/decode_devlogs.py`

#### T7-01 ADD to `STAGE_ENUM` (line 96 onward)

```python
    # c: S075 Phase 6 copy/clear lifecycle and risk witness (flags = event).
    "c": "AUTOSAVE_TRACE_STAGE_COPY_CLEAR",
```

#### T7-02 ADD to `STAGE_PRODUCER`

```python
    "c": "Core/Menu/CopyClear/* (session, copyOps, clearOps, service) and "
         "filesystem_tick()/filesystem_start() suspension/loan witnesses",
```

#### T7-03 ADD tables and a decoder function before `trace_record_text()` (line 529)

```python
# AUTOSAVE_TRACE_CC_* (AutosaveTrace.h; S075 trace debug plan §2).
CC_EVENTS = {
    0x01: "OP_START", 0x02: "OP_REFUSED", 0x03: "SOURCE_SET",
    0x04: "OP_RELEASE", 0x05: "OP_FINISH", 0x10: "JOB_START",
    0x11: "JOB_STATS", 0x12: "JOB_END", 0x13: "JOB_STALL",
    0x14: "CHECK_FAIL", 0x15: "QUEUE_FULL", 0x16: "PASTE_NOOP",
    0x18: "ANOMALY", 0x20: "REG_ADD", 0x21: "REG_REFUSED",
    0x22: "REG_DONE", 0x23: "FX_CLEAR", 0x30: "FANOUT", 0x31: "MASK_SET",
    0x40: "SCRATCH", 0x41: "FS_REFUSED", 0x42: "NAMES", 0x50: "SUSPEND",
}
CC_KINDS = {1: "step", 2: "bar", 3: "track", 4: "scene", 5: "fx-step"}
CC_DROP = {0: "none", 1: "NO_ROOM", 2: "EVACUATE_FAILED",
           3: "ADVANCED_LIMIT", 4: "FX_TYPE_MISMATCH", 5: "NO_SOURCE",
           6: "NO_SCRATCH", 7: "BAD_SELECTION", 8: "BAD_GEOMETRY"}
CC_ANOMALY = {1: "GROW_UNPLACEABLE", 2: "SWAP_RETURN_ABANDONED",
              3: "SWAP_OCCUPIED", 4: "REGION_REWRITE_GREW",
              5: "CLAIM_OTHER_SCENE", 6: "TEARDOWN_CLAIM_HELD",
              7: "PLACE_SKIPPED_STEP", 8: "RUN_STATE_RESET",
              9: "STALL_PHASE"}
CC_REFUSAL = ["recording", "erasing", "storage busy",
              "Instrument transaction", "mode", "previous jobs queued"]
CC_FANOUT = {1: "copy instrument", 2: "copy kit", 3: "copy effect",
             4: "clear send", 5: "clear fx", 6: "FX step paste"}


def cc_job_text(v: int) -> str:
    """Decode a copy/clear job descriptor (JD)."""
    op = v & 0xFF
    cls = "paste" if (op & 0xF0) == 0x10 else (
        "clear" if (op & 0xF0) == 0x20 else f"class0x{op & 0xF0:02x}")
    kind = CC_KINDS.get((v >> 8) & 0x7, f"kind{(v >> 8) & 0x7}")
    track = (v >> 15) & 0x7
    return (f"{cls} sel={op & 0xF} {kind} Scene{(v >> 11) & 0xF} "
            f"track={'-' if track == 7 else track + 1} "
            f"start={(v >> 18) & 0x7F} end={(v >> 25) & 0x7F}")


def cc_record_text(flags: int, v: int) -> str:
    """Decode one stage-'c' value32 by its event (flags)."""
    ev = CC_EVENTS.get(flags, f"event0x{flags:02x}")
    if flags in (0x10, 0x13, 0x15, 0x16):
        return f"{ev}: {cc_job_text(v)}"
    if flags == 0x01:
        return (f"{ev}: {'clear' if (v & 3) == 3 else 'copy'} mode={(v >> 2) & 7} "
                f"op#{(v >> 8) & 0xF} queued={(v >> 16) & 0xFF}")
    if flags == 0x02:
        why = [n for i, n in enumerate(CC_REFUSAL) if v & (1 << i)]
        return f"{ev}: {', '.join(why) or 'none'} mode={(v >> 8) & 7}"
    if flags == 0x03:
        return (f"{ev}: {CC_KINDS.get(v & 7, v & 7)} Scene{(v >> 3) & 0xF} "
                f"track={((v >> 7) & 7) + 1} start={(v >> 10) & 0x7F} "
                f"end={(v >> 17) & 0x7F} mode={(v >> 24) & 7}")
    if flags == 0x04:
        return (f"{ev}: started={v & 1} menu={(v >> 1) & 1} "
                f"queued={(v >> 8) & 0xFF}")
    if flags == 0x05:
        names = ["none", "written", "error", "gave up"][(v >> 28) & 3]
        return (f"{ev}: done={v & 0xFF} dropped={(v >> 8) & 0xFF} "
                f"register passes={(v >> 16) & 0xFF} op#{(v >> 24) & 0xF} "
                f"names={names}")
    if flags == 0x11:
        return (f"{ev}: slides={v & 0xFF} swap rewrites={(v >> 8) & 0xFF} "
                f"retarget dropped={(v >> 16) & 0xFF} "
                f"evacuations={(v >> 24) & 0xF} claim wait={(v >> 28) & 0xF}")
    if flags == 0x12:
        res = {0: "DONE", 2: "DROP"}.get(v & 3, f"result{v & 3}")
        return (f"{ev}: {res} reason={CC_DROP.get((v >> 2) & 0x3F, '?')} "
                f"detail=0x{(v >> 8) & 0xFF:02x} ticks={(v >> 16) & 0xFFFF}")
    if flags == 0x14:
        return (f"{ev}: free={v & 0x7FF} step#{(v >> 11) & 0x7F} "
                f"new={(v >> 18) & 0x3F} old={(v >> 24) & 0x3F} chunks")
    if flags == 0x18:
        return (f"{ev}: {CC_ANOMALY.get(v & 0xFF, v & 0xFF)} "
                f"Scene{(v >> 12) & 0xF} track={(v >> 8) & 0xF} "
                f"step={(v >> 16) & 0xFF} extra=0x{(v >> 24) & 0xFF:02x}")
    if flags in (0x20, 0x21):
        tail = (f"count={(v >> 20) & 0xF} lane={(v >> 24) & 0xF}"
                if flags == 0x20 else
                f"reason={['', 'full', 'other Scene', 'pending', 'no target'][(v >> 20) & 0xF] if ((v >> 20) & 0xF) < 5 else '?'}")
        return f"{ev}: target={v & 0xFFFF} Scene{(v >> 16) & 0xF} {tail}"
    if flags == 0x22:
        return (f"{ev}: target={v & 0xFFFF} steps={(v >> 16) & 0x3FF} "
                f"swap rewrites={(v >> 26) & 0x1F} dropped={(v >> 31) & 1}")
    if flags == 0x23:
        return (f"{ev}: mask=0x{v & 0xFFFF:04x} {scene_mask_text(v & 0xFFFF)} "
                f"Scene{(v >> 16) & 0xF} step={(v >> 20) & 0xF} "
                f"lane={(v >> 24) & 0xF} changed={(v >> 28) & 1}")
    if flags == 0x30:
        return (f"{ev}: {CC_FANOUT.get((v >> 20) & 0xF, '?')} "
                f"dst Scene{(v >> 16) & 0xF} mask=0x{v & 0xFFFF:04x} "
                f"{scene_mask_text(v & 0xFFFF)} active={(v >> 24) & 1} "
                f"slot={(v >> 25) & 0xF}")
    if flags == 0x31:
        how = "reset" if (v >> 20) & 1 else f"exchange from Scene{(v >> 21) & 0xF}"
        return (f"{ev}: Scene{(v >> 16) & 0xF} entry=0x{v & 0xFFFF:04x} "
                f"{scene_mask_text(v & 0xFFFF)} ({how})")
    if flags == 0x40:
        return (f"{ev}: {'return' if v & 1 else 'borrow'} "
                f"waited={(v >> 8) & 0xFFFF} ticks")
    if flags == 0x41:
        return f"{ev}: refused op={v & 0xFF} current op={(v >> 8) & 0xFF}"
    if flags == 0x42:
        what = ["requested", "written", "error", "gave up"][v & 3]
        return (f"{ev}: {what} copied rows={(v >> 8) & 0xFF} "
                f"changed rows={(v >> 16) & 0xFF} refusals={(v >> 24) & 0xFF}")
    if flags == 0x50:
        return (f"{ev}: {'end' if v & 1 else 'begin'} facade busy={(v >> 1) & 1} "
                f"current op={(v >> 8) & 0xFF}")
    return f"{ev}: raw value"
```

#### T7-04 `trace_record_text()` — ADD the branch before the final `else`

```python
    elif ch == "c":
        detail = f"{enum_name} via {producer}: {cc_record_text(flags, value)}"
```

#### T7-05 Module docstring (lines 1–27) — ADD after the `/asavetrc.bin` bullet

```text
  Stage 'c' records (S075) witness Phase 6 copy/clear operations, jobs,
  drops, anomalies, suspension edges and the name-buffer loan.
```

---

## 10. Stage T8 — documentation

| File | Change |
|---|---|
| `knowledge_files/specification_reference/DEV_MODES.md` | `/asavetrc.bin` section: add stage `c` with the event table of §2.2, drop reasons §2.3, anomaly codes §2.4; note that copy/clear records reach the card after the operation (flush suspended), and that copy/clear compaction no longer writes PatternTrace `R` records (T5). |
| `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md` | §8.3 conditional diagnostic SRAM: `ccSvc_traceState` 18 B and `fs_cc_*` 2 B, DEV_MODE_LOGGING only. |
| `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` | CopyClear section: `ccTrace()`, `ccSvc_trace*()`, `ccSvc_registerCount()`; AutosaveTrace stage `c`. |
| `knowledge_files/specification_reference/PATTERN_DYNAMIC_STACK.md` | §12.16 PatternTrace stage codes / §12.17: exclusive moves are not traced per move. |
| `S075_PH6_COPYCLEAR_IMPLEMENTATION.md` | §14: a "14.7 Trace debug" entry pointing to this plan, with build results. |

---

## 11. Record volume budget

Records accumulate in the AutoSave trace ring for the whole operation
(trace flushes are suspended) and are written after it.

| Scenario | Records |
|---|---:|
| Operation frame: `OP_START`, `SOURCE_SET`, `SUSPEND` begin, `SCRATCH` borrow, `OP_RELEASE`, `NAMES` request + result, `SCRATCH` return, `OP_FINISH`, `SUSPEND` end | 10 |
| Each paste or clear job: `JOB_START`, optional `JOB_STATS`, `JOB_END` | 2–3 |
| Each failed job: + `CHECK_FAIL` or a drop reason (in `JOB_END`) | +0–1 |
| Each pot clear: `REG_ADD` (+ `FX_CLEAR` for lanes) and later `REG_DONE` | 2–3 |
| Each refused press / queue-full press / identical paste | 1 |
| Each Scene-level paste/clear: + `FANOUT` or `MASK_SET` | +1 |
| Stall (once per job) | 2 |

Decision D2 (user): `AUTOSAVE_TRACE_RECORD_COUNT` stays at the temporary
2,048 while copy/clear is being tested.

Typical operation (one source, eight pastes, two of them Scene-level):
10 + 8 × 3 + 2 = **36 records**. Heavy operation (30 pastes plus 8 pot
clears): 10 + 30 × 3 + 8 × 3 = **124 records**. Against the current 2,048-
record ring both fit with ample room. Against the 64-record default ring a
heavy operation overflows; the existing `G` record then reports the drop
count after the next flush; that is why D2 keeps the 2,048 setting.

---

## 12. Resources

| Item | Production (`DEV_MODE_LOGGING 0`) | DEV build |
|---|---:|---:|
| SRAM1 | 0 B | +20 B (`ccSvc_traceState` 18 B, `fs_cc_suspended_prev`, `fs_cc_refusal_reported`) |
| DTCM / ITCM | 0 | 0 |
| Flash | 0 B (inline helper compiles away; one new 1-byte accessor `ccSvc_registerCount()` is used only by trace code and is removed by LTO) | about 1.5–2 KB (estimate; record calls and packing) |
| Ring storage | none new | uses the existing AutoSave trace ring |
| CPU | none | one `autosaveTrace_record()` (short PRIMASK section) per record; no per-step work; the names-request row count loop (161 bytes) runs once per operation |

RAM policy: the 20 B are DEV-only diagnostic SRAM (manifest §8.3). Approved
by the user on 2026-10-01 (D1).

---

## 13. Verification

1. `make all && make img` with `DEV_MODE_LOGGING 1` and with
   `DEV_MODE_LOGGING 0`: no new warnings; with 0, `arm-none-eabi-nm` shows no
   `ccSvc_traceState`/`fs_cc_*` symbols and `.bss`/`.text` equal the
   pre-trace build (±LTO noise of a few bytes).
2. `python3 tools/decode_devlogs.py` on a captured `asavetrc.bin` decodes every
   `c` record (no "no decoder for this stage" lines for `c`).
3. Hardware cases (DEV build), each followed by reading the decoded log:

| Case | Expected records |
|---|---|
| COPY pressed while recording | `OP_REFUSED` recording |
| Step paste, normal | `OP_START`, `SUSPEND` begin, `SOURCE_SET`, `SCRATCH` borrow, `JOB_START`, `JOB_END` DONE, `OP_RELEASE`, `SCRATCH` return, `OP_FINISH`, `SUSPEND` end |
| Fifth paste pressed quickly | `QUEUE_FULL` |
| Paste onto its own source | `PASTE_NOOP` |
| Merge into a nearly full pool | `CHECK_FAIL` then `JOB_END` DROP NO_ROOM, or `JOB_STATS` with slides |
| `copy instrument` beyond the Advanced limit | `JOB_END` DROP ADVANCED_LIMIT with Scene/slot |
| FX step paste onto another Effect type | `JOB_END` DROP FX_TYPE_MISMATCH |
| `copy kit` with an edit mask of three Scenes | `FANOUT` copy kit with that mask |
| `copy scene` | `MASK_SET` exchange |
| Nine pot turns | eight `REG_ADD`, one `REG_REFUSED` full, eight `REG_DONE` |
| Enter LOAD right after a long register drain | `FS_REFUSED` (Load/Save index op) before `SCRATCH` return |
| Card removed before the name write | `NAMES` gave up or error; `OP_FINISH` names=gave up/error |
| Any `ANOMALY` | none expected; each one is a bug report |

---

## 14. Decisions (answered by the user, 2026-10-01)

| # | Question | Decision | Where applied |
|---|---|---|---|
| D1 | +20 B SRAM1 in DEV builds only (0 B production) | **Approved** | §12; T2-03, T6-01; manifest §8.3 (T8) |
| D2 | Ring size while testing copy/clear | **Keep** `AUTOSAVE_TRACE_RECORD_COUNT` at the temporary 2,048 | §11; no `config.h` change |
| D3 | Teardown with a claim still held | **Record only**: `TEARDOWN_CLAIM_HELD` anomaly, no release | T2-16; §0 |
| D4 | Per-move PatternTrace `R` records for copy/clear's exclusive moves | **Stop** them; move counts go to `JOB_STATS` / `REG_DONE`; reactive and repair relocations keep their `R`/`L` records | T5-01; T8 (`DEV_MODES.md`, `PATTERN_DYNAMIC_STACK.md`) |
| D5 | Lifetime of the `c` records | **Keep** after S075 hardware testing (0 cost in production) | §1 item 4 |

Implementation has not started; this schedule is ready to implement as
written.
