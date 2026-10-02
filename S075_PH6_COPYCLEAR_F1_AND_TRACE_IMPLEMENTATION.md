# S075 — Phase 6 Copy and Clear — F1 Follow-up and Trace Debug: Implementation Schedule

Combined code schedule for
`S075_PH6_COPYCLEAR_FOLLOWUP_F1.md` (revision 4, all decisions closed) and
`S075_PH6_COPYCLEAR_TRACE_DEBUG_IMPLEMENTATION.md` (decisions D1–D5, as
amended by F1 §13.1). This document is the implementation record as well as
the schedule; code and verification notes are appended to §15.

- Baseline: working tree of `dev-ph6-copyclear` after the S075 implementation
  pass. Every line number below was read from that tree on 2026-10-02 and is
  given with a function or text anchor; when lines move during the pass, use
  the anchor.
- Every code change carries its comment block. Blocks marked **(.h)** go
  above the declaration in the header; blocks marked **(.c)** go above the
  definition or inside the function as shown. Where both exist, the `.h`
  block is the contract and the `.c` block points to it and states the
  implementation detail.
- Order: Stage A … Stage K. Each stage builds on its own; the trace hooks are
  merged into the F1 code of the same function rather than added in a second
  pass, so no function is edited twice.

## Contents

0. Decisions and deviations from the two plans
1. RAM, stack and flash ledger
2. Stage A — `ledHandler.c/.h`: revert the source group blink (F1-A)
3. Stage B — `AutosaveTrace.h`: stage `c`, events, codes (trace T1 + F1 §13.1)
4. Stage C — `copyClearService.h`: new contracts, trace helper, shared types
5. Stage D — `copyClearService.c`: lazy borrow, live paste path, early
   triggers and restore, trickle governor, F1-J, names, trace hooks
6. Stage E — `copyClearSession.c/.h`: LEDs, row gestures, indicator, flash,
   kept selection, trace hooks
7. Stage F — `copyOps.c/.h`: labels, F1-K, paste request, name phases, FX
   stack snapshot, trace hooks
8. Stage G — `clearOps.c/.h`: early trigger-off, trace hooks
9. Stage H — `PatternStackService.c`: exclusive-move trace (T5)
10. Stage I — `filesystem.c`: suspension edges, refusal witness (T6)
11. Stage J — `tools/decode_devlogs.py` (T7)
12. Stage K — documentation
13. Build and verification
14. Record volume (trace)

---

## 0. Decisions and deviations from the two plans

| # | Item | Source | Where |
|---|---|---|---|
| 0.1 | All user decisions Q1–Q7, A1–A5, B1–B2, D1–D5 | F1 §16.1, trace §14 | throughout |
| 0.2 | **Early-trigger source mask is captured for every early-trigger paste, not only overlapping ones.** Reason found in the deep dive: a later paste's early trigger write can change the *source* of an earlier, still-queued paste (paste A: X→Y queued; paste B: Z→X pressed writes X at once). A's job must copy X's triggers as they were at A's press. The O1 masks exist per slot anyway, so this costs no extra RAM and also makes the drop-restore recomputation exact. | F1 §11.4 refined | D-07, D-08 |
| 0.3 | **Early paste triggers are written by the service, not by `copyOps.c`.** The masks, the paste geometry and the overlap test are private to `copyClearService.c`; `ccCopy_requestPaste()` calls `ccSvc_pasteTriggersNow(slot)`. | F1 §11.4 step 3 relocated | C-05, D-07, F-03 |
| 0.4 | **Trickle governor gates the whole job call per tick** instead of each work unit inside the engines: in trickle mode a job/register call runs only while the credit is positive and is charged its measured TIM2 time. The engines keep their existing per-tick limits (8/16/32), which bound one call. Average stays at 0.1 % CPU; the engines need no edits for it. | F1 §11.2 simplified | D-10 |
| 0.5 | The trickle mode flag is a bit of `ccSvc_flags` (no extra byte): governor RAM is the 2 B credit only. | F1 §14 | D-10 |
| 0.6 | DEV trace state packs `op_seq`, `names_result` and `stalled` into one byte so the trace RAM stays at the approved 24 B (22 B struct + 2 B filesystem latches). | trace §12, A4 | D-02 |
| 0.7 | `ccSvc_nameCopy()` returns 0 when the name buffer is not borrowed; Scene-level executors record names in a final phase that waits on `ccSvc_namesReady()`. `ccSvc_nameContentChanged()` never borrows. `CC_REMAP_CHANGED` (0xFE) is removed. | F1 §11.1 | D-11, F-05..F-09 |
| 0.8 | `cc_commitSource()` keeps an already-open (provisional) copy menu and its selection; it opens the menu only for TRACK/Scene sources (set on press). | F1-C step 3 | E-06 |

---

## 1. RAM, stack and flash ledger

| Item | Bytes (SRAM1, production) | Approval |
|---|---:|---|
| Stage A: `led_blinkGroupMask[3]` + `led_blinkPhase` removed | −7 | — |
| D-07: `ccSvc_earlySrc[4][16]` (O1) | +64 | A2 |
| D-07: `ccSvc_earlyPrev[4][16]` (restore) | +64 | A3 |
| D-07: `ccSvc_earlyFlags` | +1 | A3 ("any extra RAM O1 needs") |
| D-10: `ccSvc_trickleCredit` | +2 | F1 §14 (approved with F1) |
| **Net this pass** | **+124** | S075 total ≈ +216 B (+92 B before this pass) |

| Item | Bytes (DEV_MODE_LOGGING only) | Approval |
|---|---:|---|
| `ccSvc_traceState` (packed, D-02) | 22 | D1 (20 B) + A4 (+4 B) |
| `fs_cc_suspended_prev`, `fs_cc_refusal_reported` (I-01) | 2 | D1 |
| **Total DEV** | **24** | |

- Stack (foreground, main loop): paste live path ≈ 1.2 KB peak
  (`ccSvc_pasteBuild()` 132 + `ccSvc_sourceBlock()` 132 + 252 decode list +
  `ccCopy_buildStep()` 504 + caller 132); FX snapshot 288 B in
  `ccCopy_runFxSteps()`; early-trigger 32 B in `ccSvc_pasteTriggersNow()`.
- Flash: −≈1.5 KB (group blink removed), +≈2–3 KB (live path, early triggers,
  governor, row/flash helpers); DEV trace +≈2 KB. Measured in Stage K.

---

## 2. Stage A — `Core/Hardware/frontPanel/ledHandler.c` / `.h` (F1-A)

#### A-01 REVERT both files to `HEAD` (`00bd078`)

Command: `git checkout HEAD -- Core/Hardware/frontPanel/ledHandler.c Core/Hardware/frontPanel/ledHandler.h`.

Removes (current lines): `.c` 150–178 (group-blink state block,
`led_blinkGroupMask[]`, `led_blinkPhase`), 180–185 (prototypes
`led_blinkGroupMember`, `led_blinkSlotMember`, `led_baseValue`), 356–369
(group branch in `led_renderFromStack()`), 400–404 and 561–565 (resets in
`led_init()`/`led_clearAll()`), 654–724 (`led_blinkGroupIndex()`,
`led_blinkGroupMember()`, `led_baseValue()`, `led_blinkSlotMember()`), 940
(`led_clearAllBlinkLeds()` guard), 948–996 (`led_setBlinkGroup()`),
1047–1086 (phase toggle in `led_tickHandler()`); `.h` 141–151
(`led_setBlinkGroup()` block and prototype). No comment block is added: the
files return to their pre-S075 text. Why: the group blink existed only to
show the copy source, which the user never asked for (F1 feedback 1). The
S070 layer stack and `led_flashGroup()` (used by F1-F) are unchanged.

Callers to remove in the same pass: Stage E (E-09, E-10, E-14).

---

## 3. Stage B — `Core/Bank/Scene/AutosaveTrace.h` (trace T1 + F1 §13.1)

#### B-01 ADD stage `c` after `AUTOSAVE_TRACE_STAGE_BUDGET_REPORT = 'H',` (line 196) — (.h)

```c
    /*
     * c: Phase 6 copy/clear lifecycle and risk witness (S075 trace debug).
     *
     * What: flags carries one AUTOSAVE_TRACE_CC_EVT_* event; value32 carries
     * that event's layout (S075_PH6_COPYCLEAR_F1_AND_TRACE_IMPLEMENTATION.md
     * Stage B). Records are bounded per operation and per job (no per-step or
     * per-tick records). Why: copy/clear drops pastes and clears silently by
     * design and runs after the button is released, so hardware tests need a
     * durable witness of drops, stalls, guarded "cannot happen" paths,
     * suspension edges, early trigger writes, trickle-rate work and the name
     * write. While an operation runs the trace flush is suspended (user rule),
     * so these records reach the card after it ends. Inputs: producer events.
     * Outputs: one 8-byte record each. DEV logging only; no product state
     * depends on them (user D5: kept after S075 testing). Producers:
     * Core/Menu/CopyClear/*, filesystem.c. Consumer: tools/decode_devlogs.py
     * ('c' branch).
     */
    AUTOSAVE_TRACE_STAGE_COPY_CLEAR = 'c',
```

#### B-02 ADD event codes and layouts before `/* Append one timestamped stage record without performing filesystem I/O. */` (line 477) — (.h)

```c
/*
 * c (COPY_CLEAR) events, drop reasons and anomaly codes (S075 trace debug).
 *
 * What: the flags byte of a 'c' record selects the event; value32 layouts:
 *   JD (job descriptor): op 0..7, kind 8..10, Scene 11..14, track 15..17
 *     (7 = none), start 18..24, end 25..31.
 *   OP_START: phase 0..1, mode 2..4, op seq 8..11, queued jobs 16..23.
 *   OP_REFUSED: reason bits 0..5 (recording, erasing, storage busy,
 *     Instrument transaction, mode, previous jobs queued), mode 8..10,
 *     queued jobs 16..23.
 *   SOURCE_SET: kind 0..2, Scene 3..6, track 7..9, start 10..16, end 17..23,
 *     mode 24..26.
 *   OP_RELEASE: started 0, menu visible 1, queued jobs 8..15.
 *   OP_FINISH: jobs done 0..7, dropped 8..15, register passes 16..23, op seq
 *     24..27, names result 28..29 (0 none, 1 written, 2 error, 3 gave up).
 *   JOB_START, JOB_STALL, QUEUE_FULL, PASTE_NOOP: JD.
 *   JOB_STATS: slides 0..7, swap rewrites 8..15, retarget-dropped 16..23,
 *     evacuations 24..27, claim-wait ticks 28..31 (all saturating).
 *   JOB_TRICKLE: ticks in trickle mode 0..15, calls run in trickle mode
 *     16..31 (saturating; job end and register-pass end, when nonzero).
 *   JOB_END: result 0..1 (0 DONE, 2 DROP), reason 2..7, detail 8..15, job
 *     ticks 16..31.
 *   CHECK_FAIL: free chunks 0..10, step index 11..17, new chunks 18..23, old
 *     chunks 24..29.
 *   ANOMALY: code 0..7, track 8..11, Scene 12..15, step 16..23, extra 24..31.
 *   EARLY_RESTORED: restored 0..7, left alone 8..15, queue slot 16..17.
 *   REG_ADD: target 0..15, Scene 16..19, count 20..23, FX lane 24..27.
 *   REG_REFUSED: target 0..15, Scene 16..19, reason 20..23 (1 full, 2 other
 *     Scene, 3 pending, 4 no target).
 *   REG_DONE: target 0..15, steps rewritten 16..25, swap rewrites 26..30,
 *     dropped 31.
 *   FX_CLEAR: fan-out mask 0..15, Scene 16..19, step 20..23 (15 = all),
 *     lane 24..27 (15 = all), changed 28.
 *   EARLY_TRIG: steps written 0..7, paste 8 (0 clear), source mask stored 9,
 *     kind 16..18, Scene 19..22, track 23..25, queue slot 26..27.
 *   FANOUT: mask 0..15, destination Scene 16..19, kind 20..23 (1 instrument,
 *     2 kit, 3 effect, 4 send, 5 clear fx, 6 FX step paste), active 24,
 *     slot 25..28.
 *   MASK_SET: entry 0..15, Scene 16..19, reset 20, exchange source 21..24.
 *   SCRATCH: return 0, ticks waited 8..23.
 *   FS_REFUSED: refused op 0..7, current op 8..15 (first per loan).
 *   NAMES: event 0..1 (0 requested, 1 written, 2 error, 3 gave up), rows
 *     copied 8..15, bits 16..23 reserved 0, refusals 24..31.
 *   SUSPEND: end 0, facade busy 1, current op 8..15.
 * Why fixed codes: a raw dump stays readable and the decoder needs no
 * firmware symbols. Producers: copyClearSession.c, copyOps.c, clearOps.c,
 * copyClearService.c, filesystem.c. Consumer: tools/decode_devlogs.py.
 */
#define AUTOSAVE_TRACE_CC_EVT_OP_START       0x01u
#define AUTOSAVE_TRACE_CC_EVT_OP_REFUSED     0x02u
#define AUTOSAVE_TRACE_CC_EVT_SOURCE_SET     0x03u
#define AUTOSAVE_TRACE_CC_EVT_OP_RELEASE     0x04u
#define AUTOSAVE_TRACE_CC_EVT_OP_FINISH      0x05u
#define AUTOSAVE_TRACE_CC_EVT_JOB_START      0x10u
#define AUTOSAVE_TRACE_CC_EVT_JOB_STATS      0x11u
#define AUTOSAVE_TRACE_CC_EVT_JOB_END        0x12u
#define AUTOSAVE_TRACE_CC_EVT_JOB_STALL      0x13u
#define AUTOSAVE_TRACE_CC_EVT_CHECK_FAIL     0x14u
#define AUTOSAVE_TRACE_CC_EVT_QUEUE_FULL     0x15u
#define AUTOSAVE_TRACE_CC_EVT_PASTE_NOOP     0x16u
#define AUTOSAVE_TRACE_CC_EVT_JOB_TRICKLE    0x17u
#define AUTOSAVE_TRACE_CC_EVT_ANOMALY        0x18u
#define AUTOSAVE_TRACE_CC_EVT_EARLY_RESTORED 0x19u
#define AUTOSAVE_TRACE_CC_EVT_REG_ADD        0x20u
#define AUTOSAVE_TRACE_CC_EVT_REG_REFUSED    0x21u
#define AUTOSAVE_TRACE_CC_EVT_REG_DONE       0x22u
#define AUTOSAVE_TRACE_CC_EVT_FX_CLEAR       0x23u
#define AUTOSAVE_TRACE_CC_EVT_EARLY_TRIG     0x24u
#define AUTOSAVE_TRACE_CC_EVT_FANOUT         0x30u
#define AUTOSAVE_TRACE_CC_EVT_MASK_SET       0x31u
#define AUTOSAVE_TRACE_CC_EVT_SCRATCH        0x40u
#define AUTOSAVE_TRACE_CC_EVT_FS_REFUSED     0x41u
#define AUTOSAVE_TRACE_CC_EVT_NAMES          0x42u
#define AUTOSAVE_TRACE_CC_EVT_SUSPEND        0x50u

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
#define AUTOSAVE_TRACE_CC_ANOM_STALL_PHASE           9u
```

(Codes 7 and 8 of the trace plan are not produced and are not defined; the
decoder keeps their names for reading old captures — none exist yet.)

---

## 4. Stage C — `Core/Menu/CopyClear/copyClearService.h`

#### C-01 MODIFY includes (line 21–22): ADD `config.h` and `AutosaveTrace.h`

```c
#include <stdint.h>
#include "config.h"
#include "AutosaveTrace.h"
#include "copyClearSession.h"
```

No new block: the file header (lines 1–17) gains one sentence: "Diagnostic
trace helpers (stage 'c', DEV builds only) are declared at the end of this
header."

#### C-02 MODIFY the `ccSvc_enqueue()` contract (lines 67–68) — (.h)

```c
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
```

#### C-03 MODIFY the scratch block (lines 80–88) — (.h)

```c
/*
 * Borrowed 9 kB name buffer (S075, F1-I).
 *
 * What: the filesystem's name cache, lent to copy/clear only when it is
 * used: by a step/bar paste that overlaps its own source (snapshot), and by
 * the name remap and its end-of-operation HCNAMES write. Clears, pot clears,
 * whole-Pattern copy/reset, non-overlapping pastes and Scene-level data
 * commits never wait for it (F1 §2.2: the borrow waits for an in-flight
 * writer, which caused the 3–4 s start delay). Layout (spec §9.3):
 * [0..160] HCNAMES row remap (0xFF = unchanged); [256..511] paste source
 * table; [512..] source blocks. ccSvc_scratch() returns NULL while not
 * borrowed. ccSvc_namesReady() borrows if possible and returns nonzero once
 * the buffer is held (Scene-level executors wait on it before recording
 * names). Accessors: copyClearService.c, copyOps.c.
 */
uint8_t *ccSvc_scratch(void);
uint8_t ccSvc_namesReady(void);
#define CC_SCRATCH_REMAP_OFFSET   0u
#define CC_SCRATCH_TABLE_OFFSET   256u
#define CC_SCRATCH_BLOCK_OFFSET   512u
```

#### C-04 MODIFY the name-remap block (lines 116–128) — (.h)

```c
/*
 * Name remap for copy/clear (spec §9.10, F1-I).
 *
 * ccSvc_nameCopy(): remap[dst] = the original row whose name/source the
 * destination takes; chained pastes resolve to the first source (a paste of
 * B after A->B uses A's row; a paste back onto the original row clears the
 * entry). Clears the destination's refreshed flag. Returns 0 (nothing
 * recorded) when the name buffer is not borrowed: callers wait on
 * ccSvc_namesReady() first. ccSvc_nameContentChanged(): the row keeps its
 * name but its content changed; clears its refreshed flag in RAM and arms the
 * end-of-operation HCNAMES write; never borrows (the content's own AutoSave
 * markers already mark its source bytes). Rows are FS_HCNAMES rows; invalid
 * rows are ignored. Callers: copyOps.c and clearOps.c executors.
 */
uint8_t ccSvc_nameCopy(uint16_t dst_row, uint16_t src_row);
void ccSvc_nameContentChanged(uint16_t row);
```

#### C-05 ADD after `ccSvc_targetPending()` (line 114) — (.h)

```c
/* Number of targets waiting in the pot-clear register (0..8; trace use). */
uint8_t ccSvc_registerCount(void);

/*
 * Early trigger bits for pastes (F1-I §11.4; user Q4, A2 = O1, A3, B1).
 *
 * What: ccSvc_pasteTriggersNow() writes the destination trigger bits of the
 * just-queued step/bar/track paste in queue slot `slot` at the press:
 * `… -> repl` sets each destination trigger to the source's, `… -> merge`
 * ORs it in; automation pastes write nothing. Before writing it stores the
 * source triggers (range order) and the previous destination triggers
 * (destination order) in the slot's two 128-bit masks; the job's snapshot or
 * live path copies triggers from the source mask (not the live source,
 * which a later early write may already have changed), and a dropped job
 * restores the previous triggers on steps the user has not changed since.
 * Repaints the visible step LEDs. Why: SEQ LEDs and triggers update at once
 * even when the job waits for the name buffer or runs at the trickle rate.
 * Inputs: queue slot index of a job just accepted by ccSvc_enqueue().
 * Output: number of destination steps written (0 when the job does not
 * write triggers early). Caller: ccCopy_requestPaste(). Affiliates:
 * ccSvc_runPatternPaste(), the drop path in ccSvc_tick(),
 * pat_setStepActive().
 */
uint8_t ccSvc_pasteTriggersNow(uint8_t slot);

/*
 * Repaint step LEDs after trigger bits of one Scene changed outside a job
 * (F1-H, F1-I §11.4).
 *
 * What: VOICE/STEP: led_updatePatternTrack() for the active track when the
 * Scene is viewed; PERF: menu_refreshPerfSceneLeds(). Does not restart the
 * automation search (trigger bits carry no automation). Inputs: Scene.
 * Callers: ccSvc_pasteTriggersNow(), ccClear_triggersOffNow(), the drop
 * restore path. Affiliates: ccSvc_patternChangedUi().
 */
void ccSvc_triggersChangedUi(uint8_t scene);
```

#### C-06 ADD the trace helper before `#endif /* COPY_CLEAR_SERVICE_H_ */` (line 137) — (.h)

```c
/*
 * Copy/clear diagnostic trace (S075 trace debug; DEV_MODE_LOGGING only).
 *
 * What: ccTrace() writes one AutoSave trace record with stage 'c', the event
 * in flags and an event-specific value32 (layouts: AutosaveTrace.h, Stage
 * B). ccTrace_job() packs a job descriptor (JD); ccTrace_anomaly() packs an
 * anomaly record. Why inline: production builds (DEV_MODE_LOGGING 0) compile
 * every call to nothing, so the hooks cost no flash, RAM or cycles there.
 * Inputs: event code and value. Output: one record in the DEV trace ring; no
 * product state changes. Callers: copyClearSession.c, copyOps.c,
 * clearOps.c, copyClearService.c. Affiliates: autosaveTrace_record().
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
 * Per-job trace feeds (DEV only; empty in production).
 * ccSvc_traceRetargetDropped(): automation entries removed by retargeting.
 * ccSvc_traceDropReason(): reason and detail for the current job's JOB_END.
 * ccSvc_traceOpSequence(): 4-bit sequence number of the running operation.
 * Callers: copyOps.c, clearOps.c, copyClearSession.c, copyClearService.c.
 */
void ccSvc_traceRetargetDropped(uint8_t count);
void ccSvc_traceDropReason(uint8_t reason, uint8_t detail);
uint8_t ccSvc_traceOpSequence(void);
```

---

## 5. Stage D — `Core/Menu/CopyClear/copyClearService.c`

#### D-01 MODIFY includes (lines 14–25): ADD `#include "timebase.h"` after `menu.h`

Used by the trickle governor (`timebase_tim2Now()`). No block.

#### D-02 ADD DEV trace state after `static uint8_t *ccSvc_buf;` (line 81) — (.c)

```c
#if DEV_MODE_LOGGING
/*
 * Copy/clear trace counters (S075 trace debug; DEV_MODE_LOGGING only; 22 B
 * SRAM1, 0 B in production; approved D1 + A4).
 *
 * What: per-operation totals (jobs done/dropped, register passes; op
 * sequence, name-write result and the per-job stall latch packed in `bits`),
 * per-job statistics (ticks, compaction slides, swap-path rewrites, retarget
 * drops, evacuations, claim-wait ticks, drop reason/detail, trickle ticks and
 * calls), the borrow wait counter and the register-pass step count. Why:
 * summaries replace per-step records so a whole operation fits the trace ring
 * while flushes are suspended. Reset: job fields at JOB_START and register
 * start, operation fields in ccSvc_interactionStarted(). Accessors:
 * ccSvc_tick(), the engines, ccSvc_trace*().
 * bits: 0..3 op sequence, 4..5 names result, 6 stalled.
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
#define CC_TRACE_STALL_TICKS 5000u   /* 10 s at 500 Hz */
#else
#define CC_TRACE_SAT8(field)  do { } while (0)
#define CC_TRACE_SAT16(field) do { } while (0)
#endif
```

#### D-03 ADD the early-trigger masks and the governor credit after `static cc_job_t ccSvc_queue[CC_QUEUE_SIZE];` block (line 49) — (.c)

```c
/*
 * Early-trigger masks per queue slot (F1-I §11.4; 129 B SRAM1, approved
 * 2026-10-02: O1 +64 B, A3 restore storage).
 *
 * What: for a queued step/bar/track paste that wrote its destination trigger
 * bits at the press, src[i] bit n is the source trigger of the paste's n-th
 * step (range order) and prev[i] bit n the destination trigger of the n-th
 * destination step, both read at the press before the early write.
 * Flags bit i: slot i has early triggers. Why: pasted steps light at once
 * (user Q4); the job copies the press-time source triggers even when the
 * paste overlaps its own source or a later paste's early write has changed
 * that source; a dropped paste is undone whole (user A3, B1). Lifetime:
 * written by ccSvc_pasteTriggersNow(), read by ccSvc_runPatternPaste() and
 * ccSvc_restoreEarlyTriggers(), cleared when the slot's job ends. Accessors:
 * those three functions and ccSvc_tick().
 */
static uint8_t ccSvc_earlySrc[CC_QUEUE_SIZE][16];
static uint8_t ccSvc_earlyPrev[CC_QUEUE_SIZE][16];
static uint8_t ccSvc_earlyFlags;

/*
 * Copy/clear trickle governor (F1-I §11.2; 2 B SRAM1).
 *
 * What: signed microsecond credit for copy/clear work while an older writer
 * still owns the filesystem facade (CC_SVC_TRICKLE set by ccSvc_tick()). Each
 * such tick adds CC_TRICKLE_US_PER_TICK (2 µs per 2 ms = 0.1 % CPU), capped
 * at CC_TRICKLE_CAP_US; a job or register call runs only while the credit is
 * positive and is charged its measured TIM2 time, so the average stays at
 * 0.1 %. With the facade idle (the pause in full effect) the governor is
 * bypassed and calls run every tick. Why: user request — start pot clears and
 * clears at once with a small CPU share instead of waiting for AutoSave to
 * finish. Inputs: filesystem_status(), timebase_tim2Now(). Accessors:
 * ccSvc_tick().
 */
#define CC_TRICKLE_US_PER_TICK 2
#define CC_TRICKLE_CAP_US      40
#define CC_TRICKLE_FLOOR_US    (-30000)
static int16_t ccSvc_trickleCredit;
```

ADD `#define CC_SVC_TRICKLE 0x20u  /* an older writer still runs: governed */`
after `CC_SVC_NAMES_BUSY` (line 74). REMOVE `CC_REMAP_CHANGED` (line 36) and
its comment (line 35).

#### D-04 MODIFY `ccSvc_claim()` (lines 85–92) — trace T2-05

```c
/*
 * Take (or keep) the exclusive boundary on one Scene; nonzero when held.
 * A claim for another Scene still held is a broken invariant (every job
 * releases on every exit) and is traced (ANOMALY CLAIM_OTHER_SCENE); waiting
 * for queued Pattern work to drain is counted in claim_wait (DEV).
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
```

#### D-05 MODIFY `ccSvc_ensureScratch()` (lines 103–114) — trace T2-06

```c
/*
 * Borrow the name buffer once; the remap starts empty (all 0xFF).
 *
 * Output: nonzero when held. Called only where the buffer is used (F1-I):
 * overlapping paste snapshot, ccSvc_namesReady(), the end-of-operation name
 * write. Ticks spent waiting for an idle facade are counted and reported
 * with the SCRATCH borrow record (DEV).
 */
static uint8_t ccSvc_ensureScratch(void)
{
    if (ccSvc_buf)
        return 1u;
    ccSvc_buf = filesystem_borrowNameCacheScratch();
    if (!ccSvc_buf) {
        CC_TRACE_SAT16(ccSvc_traceState.scratch_wait);
        return 0u;
    }
    memset(&ccSvc_buf[CC_SCRATCH_REMAP_OFFSET], CC_REMAP_NONE,
           FS_HCNAMES_ROW_COUNT);
#if DEV_MODE_LOGGING
    ccTrace(AUTOSAVE_TRACE_CC_EVT_SCRATCH,
            (uint32_t)ccSvc_traceState.scratch_wait << 8u);
    ccSvc_traceState.scratch_wait = 0u;
#endif
    return 1u;
}
```

#### D-06 ADD `ccSvc_triggersChangedUi()` after `ccSvc_patternChangedUi()` (line 130) — (.c)

```c
/* Contract in copyClearService.h (F1-H, F1-I §11.4). */
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
```

#### D-07 ADD the paste overlap test, early triggers and restore after `ccSvc_dstStep()` (line 244) — (.c)

```c
/* Bit helpers for the 128-bit early-trigger masks. */
static uint8_t ccSvc_maskGet(const uint8_t m[16], uint8_t n)
{
    return (uint8_t)((m[n >> 3] >> (n & 7u)) & 1u);
}

static void ccSvc_maskSet(uint8_t m[16], uint8_t n)
{
    m[n >> 3] = (uint8_t)(m[n >> 3] | (uint8_t)(1u << (n & 7u)));
}

/*
 * Does a paste write steps it also reads? (F1-I §11.1)
 *
 * What: same Scene and track, and at least one destination step is also a
 * source step. Why: only such a paste needs the buffer snapshot (it would
 * otherwise read steps it already rewrote); every other paste reads its
 * source live. A track paste onto its own track is an identical paste and
 * never reaches here. Inputs: job, geometry. Output: nonzero on overlap.
 * Caller: ccSvc_runPatternPaste().
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

/* Contract in copyClearService.h (F1-I §11.4). */
uint8_t ccSvc_pasteTriggersNow(uint8_t slot)
{
    const cc_job_t *job;
    const cc_source_t *src = copyClear_source();
    cc_paste_geo_t g;
    uint8_t i;

    if (slot >= CC_QUEUE_SIZE)
        return 0u;
    job = &ccSvc_queue[slot];
    if ((job->op & CC_JOB_CLASS_MASK) != CC_JOB_PASTE ||
        (job->kind != CC_KIND_STEP && job->kind != CC_KIND_BAR &&
         job->kind != CC_KIND_TRACK))
        return 0u;
    ccSvc_pasteGeometry(job, &g);
    if (g.count == 0u ||
        (g.selection != CC_COPY_ALL && g.selection != CC_COPY_MERGE_ALL))
        return 0u;
    memset(ccSvc_earlySrc[slot], 0, 16u);
    memset(ccSvc_earlyPrev[slot], 0, 16u);
    /* Read every source and previous destination bit before any write. */
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
    /* S075 trace: EARLY_TRIG (paste, source mask stored). */
    ccTrace(AUTOSAVE_TRACE_CC_EVT_EARLY_TRIG,
            (uint32_t)((g.count > 255u) ? 255u : g.count) | (1u << 8u) |
            (1u << 9u) | ((uint32_t)(job->kind & 7u) << 16u) |
            ((uint32_t)(job->scene & 0xFu) << 19u) |
            ((uint32_t)(job->track & 7u) << 23u) |
            ((uint32_t)(slot & 3u) << 26u));
    return g.count;
}

/*
 * Undo a dropped paste's early trigger writes (F1-I §11.4, user A3, B1).
 *
 * What: for each destination step of the dropped job, if its live trigger
 * still equals the value written at the press (recomputed from the slot's
 * source mask, OR the previous mask for `merge`), restores the previous value
 * from the slot's restore mask; a step changed since (user toggle) is left
 * alone. Then repaints the step LEDs. Why: a paste completes or is dropped
 * whole. Inputs: the dropped job and its slot. Output: restored triggers; one
 * EARLY_RESTORED trace record. Caller: ccSvc_tick() on CC_RUN_DROP.
 * Affiliates: ccSvc_pasteTriggersNow().
 */
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
```

`ccSvc_pasteGeometry()` must be declared before D-07: move D-07 below it
(it is at lines 199–234; D-07 goes after line 244 which is after it — no
move needed).

#### D-08 REPLACE `ccSvc_pasteBuild()` (lines 265–284) and ADD `ccSvc_sourceBlock()` before it — (.c)

```c
/*
 * Source block and trigger of paste step i (F1-I §11.1, §11.4).
 *
 * What: snapshot path (overlap): the table entry and its block in the
 * borrowed buffer. Live path: pat_rawReadBlock() of the source step, then
 * retargeting into `out` when the destination track/Scene differs. The
 * trigger comes from the slot's early-trigger source mask when the job wrote
 * triggers early, else from the snapshot/live entry. Why: a non-overlapping
 * paste does not need the buffer; press-time triggers must win over live
 * ones (deviation 0.2). Inputs: job, geometry, step i, the snapshot-path
 * flag, whether retarget drops are counted (only in the phase that
 * publishes, so each entry is counted once). Output: block bytes (0 = none)
 * in `out` or pointer to the snapshot block via *block_out, and *trigger.
 * Caller: ccSvc_pasteBuild().
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

/*
 * Build destination step i of a paste from its source (snapshot or live)
 * and the live destination. Outputs: bytes (0 = no block), trigger mode,
 * skip flag, and the live destination block size. `place` is nonzero in the
 * place phase (retarget drops are counted there).
 */
static uint8_t ccSvc_pasteBuild(const cc_job_t *job, const cc_paste_geo_t *g,
                                uint8_t i, uint8_t snapshot, uint8_t place,
                                uint8_t out[PAT_RAW_BLOCK_MAX],
                                uint8_t *mode, uint8_t *skip,
                                uint8_t *old_bytes)
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
```

#### D-09 MODIFY `ccSvc_runPatternPaste()` (lines 286–477) — F1-I live path + trace T2-10

Block (lines 286–303) REPLACE:

```c
/*
 * Run one step/range/bar/track paste in bounded steps (spec §9.5, F1-I).
 *
 * What: a paste that overlaps its own source snapshots the source into the
 * borrowed buffer (retargeted) first; every other paste reads its source
 * live in the check and place phases (no buffer, no wait for an in-flight
 * writer). Then: check that every step fits (a growing step needs free
 * chunks >= its new size while its old block still exists), and replace each
 * destination step in publication order, using the swap block for steps that
 * do not grow and sliding compaction for steps that grow, so a paste either
 * completes or is dropped before any block changes. Trigger bits come from
 * the press-time early-trigger mask when the paste wrote them early. Why:
 * user rules "always step by step" and "start at once". Inputs: the
 * queue-head job, the operation's source. Outputs: committed steps, track
 * length or settings, UI refresh; trace CHECK_FAIL / anomalies. Returns DONE,
 * WAIT or DROP. Run state: phase bit 0 = snapshot path; sub = phase (0
 * claim, 1 snapshot, 2 check, 3 place, 4 finish); cursor = step index; aux =
 * snapshot chunk cursor / free-chunk count / bit 0 swap-return pending.
 * Caller: copyOps.c ccCopy_runJob().
 */
```

Body changes:

1. Guard (lines 312–314):

```c
    ccSvc_pasteGeometry(job, &g);
    if (g.count == 0u) {
        ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_BAD_GEOMETRY, job->kind);
        return CC_RUN_DROP;
    }
```

2. Case 0 (lines 317–327) becomes:

```c
    case 0u: /* PATH, BUFFER (overlap only), CLAIM + EVACUATE */
    {
        uint8_t r;

        if (ccSvc_pasteOverlaps(job, &g)) {
            run->phase = 1u;                    /* snapshot path */
            if (!ccSvc_ensureScratch())
                return CC_RUN_WAIT;             /* claim not yet taken */
        }
        r = ccSvc_claimAndEvacuate(job->scene);
        if (r != CC_RUN_DONE)
            return r;
        run->cursor = 0u;
        if (run->phase & 1u) {
            run->sub = 1u;
            run->aux = 0u;
        } else {
            run->sub = 2u;                      /* live path: straight to check */
            run->aux = pat_rawFreeChunks(job->scene);
        }
        return CC_RUN_WAIT;
    }
```

3. Case 1 snapshot (lines 328–373): unchanged except the retarget drop count
   (trace T2-10) around line 353:

```c
                uint8_t before = count;

                count = ccCopy_retargetEntries(&ctx, autos, count);
                ccSvc_traceRetargetDropped((uint8_t)(before - count));
```

4. Case 2 check (lines 374–403): `ccSvc_pasteBuild(job, &g, cursor,
   run->phase & 1u, 0u, block, …)`; the drop at lines 389–392 becomes
   (trace T2-10):

```c
                if (run->aux < new_c) {
                    /*
                     * S075 trace (R1/R2): the growing step at cursor-1 needs
                     * new_c chunks while only aux are free (old block still
                     * live). Nothing has been changed yet (early triggers are
                     * restored by the drop path).
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

5. Case 3 place (lines 404–445): `ccSvc_pasteBuild(job, &g, i, run->phase
   & 1u, 1u, block, …)`; the growing branch (lines 432–435) becomes (trace
   T2-10):

```c
                if (patSvc_exclusiveCompactStep(job->scene)) {
                    CC_TRACE_SAT8(ccSvc_traceState.slides);
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

6. Case 4 finish: unchanged.

#### D-10 MODIFY `ccSvc_rewriteShrink()` (lines 139–155), `ccSvc_swapReturnPending()` (157–169), `ccSvc_claimAndEvacuate()` (171–186) — trace T2-07, T2-08, T2-09

`ccSvc_rewriteShrink()`, lines 147–150:

```c
    if (pat_rawPlace(scene, track, step, block, trigger_mode))
        return CC_RUN_DONE;
    CC_TRACE_SAT8(ccSvc_traceState.swaps);     /* S075 trace: swap path */
    if (!pat_rawPlaceViaSwap(scene, track, step, block, trigger_mode)) {
        /*
         * S075 trace (R4): the swap block is occupied although the claim
         * phase emptied it; the step keeps its old block (nothing published).
         */
        ccTrace_anomaly(AUTOSAVE_TRACE_CC_ANOM_SWAP_OCCUPIED, scene, track,
                        step, (uint8_t)(bytes / 4u));
        return CC_RUN_DONE;
    }
```

`ccSvc_swapReturnPending()` REPLACE body:

```c
/*
 * Retry a pending swap-return; compaction runs between attempts. When
 * compaction can move nothing more the step is left in the swap block (the
 * next claim's evacuation moves it once room exists) and an ANOMALY
 * SWAP_RETURN_ABANDONED is traced: the freed old run is at least the block's
 * size, so this is not expected.
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
```

`ccSvc_claimAndEvacuate()`, lines 179–184:

```c
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

#### D-11 MODIFY `ccSvc_clearStep()` (lines 523–567) — F1-J

Block (lines 523–529) REPLACE:

```c
/*
 * Clear one step (spec §5, F1-J).
 *
 * `all` empties it (trigger off, block released); `automation` strips the
 * automation and keeps trigger and specials; `notes` clears everything
 * except automation: trigger off, note and velocity specials removed, the
 * probability special kept when set (it gates the step's automation, user
 * A5). Rewrites never grow, so they use the swap path. Returns CC_RUN_DONE or
 * CC_RUN_WAIT (swap-return pending). Caller: ccSvc_runPatternClear().
 */
```

Line 564 REPLACE:

```c
    /*
     * notes (F1-J, user A5): everything except automation goes, but the
     * probability special stays when set, because it gates this step's
     * automation. Trigger off; the block shrinks or empties (swap path).
     */
    bytes = pat_rawEncode(block, (uint8_t)(sp.flags & PAT_SPECIAL_PROB_BIT),
                          0u, 0u, sp.probability, autos, count);
```

`ccSvc_runPatternClear()` block (lines 569–581): replace "strips … triggers
and specials (notes)" with "turns triggers off and removes the note and
velocity specials (notes; probability and automation kept, F1-J)".

#### D-12 MODIFY `ccSvc_runRegionCopy()` (lines 676–702) — trace T2-11

Lines 696–700 REPLACE:

```c
            {
                uint8_t before = count;

                count = ccCopy_retargetEntries(&ctx, autos, count);
                ccSvc_traceRetargetDropped((uint8_t)(before - count));
            }
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

#### D-13 MODIFY `ccSvc_runRegister()` (lines 721–803) — trace T2-12

- After a successful rewrite (lines 784–786, when `ccSvc_rewriteShrink()`
  did not return WAIT): `#if DEV_MODE_LOGGING if (ccSvc_traceState.reg_steps
  < 896u) ccSvc_traceState.reg_steps++; #endif`.
- Before `ccSvc_release();` at line 795:

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
    CC_TRACE_SAT8(ccSvc_traceState.reg_passes);
#endif
```

#### D-14 MODIFY names (lines 805–855) — F1-I §11.1, trace T2-13

```c
/* Contract in copyClearService.h (F1-I: never borrows). */
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

/* Contract in copyClearService.h (F1-I: refreshed flag only, no buffer). */
void ccSvc_nameContentChanged(uint16_t row)
{
    if (row >= FS_HCNAMES_ROW_COUNT)
        return;
    (void)filesystem_clearResidentRefreshed(row);
    ccSvc_flags |= CC_SVC_NAMES_DIRTY;
}

/* Contract in copyClearService.h. */
uint8_t ccSvc_namesReady(void)
{
    return ccSvc_ensureScratch();
}
```

`ccSvc_namesWritten()` (lines 835–855): block updated to "mark the source
bytes of every remapped row (rows only marked content-changed already had
their sources marked by their content's AutoSave markers)"; after
`filesystem_ack();` ADD (trace T2-13):

```c
#if DEV_MODE_LOGGING
    /* S075 trace (R8): HCNAMES copy update finished. */
    ccSvc_traceState.bits = (uint8_t)((ccSvc_traceState.bits & ~0x30u) |
                                      ((ok ? 1u : 2u) << 4u));
    ccTrace(AUTOSAVE_TRACE_CC_EVT_NAMES, ok ? 1u : 2u);
#endif
```

#### D-15 MODIFY `ccSvc_init()`, `ccSvc_enqueue()`, `ccSvc_interactionStarted()`, register accessors (lines 859–948)

`ccSvc_init()` ADD: `memset(ccSvc_earlySrc, 0, sizeof(ccSvc_earlySrc));
memset(ccSvc_earlyPrev, 0, sizeof(ccSvc_earlyPrev)); ccSvc_earlyFlags = 0u;
ccSvc_trickleCredit = 0;`.

`ccSvc_enqueue()` REPLACE (contract C-02, trace T2-14):

```c
/* Contract in copyClearService.h (returns slot + 1). */
uint8_t ccSvc_enqueue(const cc_job_t *job)
{
    uint8_t slot;

    if (!job)
        return 0u;
    if (ccSvc_count >= CC_QUEUE_SIZE) {
        /* S075 trace (R1): a fifth paste/clear is dropped (spec §3.1). */
        ccTrace(AUTOSAVE_TRACE_CC_EVT_QUEUE_FULL, ccTrace_job(job));
        return 0u;
    }
    slot = (uint8_t)((ccSvc_head + ccSvc_count) % CC_QUEUE_SIZE);
    ccSvc_queue[slot] = *job;
    ccSvc_earlyFlags = (uint8_t)(ccSvc_earlyFlags & (uint8_t)~(1u << slot));
    ccSvc_count++;
    return (uint8_t)(slot + 1u);
}
```

`ccSvc_interactionStarted()` ADD (trace T2-15):

```c
#if DEV_MODE_LOGGING
    /*
     * S075 trace: a new operation gets the next sequence number and fresh
     * per-operation totals (jobs, register passes, name result).
     */
    ccSvc_traceState.bits = (uint8_t)((ccSvc_traceState.bits + 1u) & 0x0Fu);
    ccSvc_traceState.jobs_done = 0u;
    ccSvc_traceState.jobs_dropped = 0u;
    ccSvc_traceState.reg_passes = 0u;
#endif
```

ADD after `ccSvc_registerScene()` (line 938):

```c
/* Contract in copyClearService.h. */
uint8_t ccSvc_registerCount(void)
{
    return ccSvc_regCount;
}
```

ADD after `ccSvc_scratch()` (line 916) the trace feeds (contract C-06):

```c
/* Trace counter feeds (contract in copyClearService.h); empty in production. */
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
    return (uint8_t)(ccSvc_traceState.bits & 0x0Fu);
#else
    return 0u;
#endif
}
```

#### D-16 REPLACE `ccSvc_tick()` (lines 950–1022) — F1-I lazy start, governor, restore; trace T2-16

```c
/*
 * One bounded service step (500 Hz, foreground).
 *
 * Order: an in-flight name write blocks everything (it uses the same scratch
 * offsets as a paste snapshot); the trickle governor decides whether work
 * may run this tick (F1-I §11.2: while an older writer still owns the
 * facade, 0.1 % CPU); a running job or register pass continues; a queued job
 * or register pass starts at once (no name-buffer wait, F1-I §11.1); a
 * dropped paste restores its early triggers (A3); once interaction has
 * ended: the name write, the buffer return and the end of the suspension.
 * Trace: JOB_START/STATS/TRICKLE/END/STALL, EARLY_RESTORED, NAMES, SCRATCH
 * return, TEARDOWN_CLAIM_HELD, OP_FINISH.
 */
void ccSvc_tick(void)
{
    uint8_t r;
    uint8_t work;
    uint32_t t0 = 0u;

    if ((ccSvc_flags & CC_SVC_NAMES_BUSY) != 0u)
        return;
    work = (uint8_t)((ccSvc_flags & (CC_SVC_JOB_ACTIVE | CC_SVC_REG_ACTIVE)) != 0u);

    /* F1-I §11.2: trickle while an older writer still runs. */
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
                ? ccCopy_runJob(job)
                : ccClear_runJob(job);
        ccSvc_chargeTrickle(t0);
        CC_TRACE_SAT16(ccSvc_traceState.job_ticks);
#if DEV_MODE_LOGGING
        /* S075 trace (R5): one stall witness per job after 10 s. */
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
        /* F1-I §11.4, user A3: a dropped paste is undone whole. */
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
        /* Card absent or facade stuck: names stay as they are. */
#if DEV_MODE_LOGGING
        ccSvc_traceState.bits = (uint8_t)(ccSvc_traceState.bits | 0x30u);
        ccTrace(AUTOSAVE_TRACE_CC_EVT_NAMES, 3u | (255u << 24u));
#endif
        ccSvc_flags &= (uint8_t)~CC_SVC_NAMES_DIRTY;
        ccSvc_nameRetry = 0u;
    }
    /*
     * S075 trace: a claim must never survive the last job (R4). Record only
     * (user D3): nothing is repaired here.
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
    ccSvc_traceOpFinish();
    copyClear_serviceFinished();
}
```

Static helpers ADD before `ccSvc_tick()` — (.c):

```c
/*
 * Charge one governed call (F1-I §11.2). In trickle mode subtracts the
 * call's TIM2 time from the credit (floored at CC_TRICKLE_FLOOR_US) and
 * counts the call (DEV); a no-op otherwise. Input: TIM2 start time.
 */
static void ccSvc_chargeTrickle(uint32_t t0)
{
    uint32_t elapsed;
    int32_t credit;

    if ((ccSvc_flags & CC_SVC_TRICKLE) == 0u)
        return;
    elapsed = timebase_tim2Now() - t0;
    credit = (int32_t)ccSvc_trickleCredit -
             (int32_t)((elapsed > 30000u) ? 30000u : elapsed);
    ccSvc_trickleCredit = (int16_t)((credit < CC_TRICKLE_FLOOR_US)
                                        ? CC_TRICKLE_FLOOR_US : credit);
    CC_TRACE_SAT16(ccSvc_traceState.trickle_calls);
}

/*
 * S075 trace helpers for ccSvc_tick() (DEV only; empty in production).
 * ccSvc_traceJobStart(): resets per-job counters, JOB_START with the JD (no
 * record for a register pass, job == NULL). ccSvc_traceTrickle(): JOB_TRICKLE
 * when the job/pass ran in trickle mode. ccSvc_traceJobEnd(): JOB_STATS when
 * nonzero, JOB_TRICKLE, JOB_END, totals. ccSvc_traceNamesRequested(): NAMES
 * requested with the remapped-row count and refusals. ccSvc_traceOpFinish():
 * OP_FINISH with the operation totals.
 */
static void ccSvc_traceJobStart(const cc_job_t *job)
{
#if DEV_MODE_LOGGING
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
#else
    (void)job;
#endif
}

static void ccSvc_traceTrickle(void)
{
#if DEV_MODE_LOGGING
    if (ccSvc_traceState.trickle_ticks != 0u ||
        ccSvc_traceState.trickle_calls != 0u)
        ccTrace(AUTOSAVE_TRACE_CC_EVT_JOB_TRICKLE,
                (uint32_t)ccSvc_traceState.trickle_ticks |
                ((uint32_t)ccSvc_traceState.trickle_calls << 16u));
#endif
}

static void ccSvc_traceJobEnd(uint8_t r)
{
#if DEV_MODE_LOGGING
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
                                        (ccSvc_runState.sub & 0x0Fu)))
             << 8u) |
            ((uint32_t)ccSvc_traceState.job_ticks << 16u));
    if (r == CC_RUN_DROP)
        CC_TRACE_SAT8(ccSvc_traceState.jobs_dropped);
    else
        CC_TRACE_SAT8(ccSvc_traceState.jobs_done);
#else
    (void)r;
#endif
}

static void ccSvc_traceNamesRequested(void)
{
#if DEV_MODE_LOGGING
    uint8_t copied = 0u;
    uint16_t row;

    for (row = 0u; row < FS_HCNAMES_ROW_COUNT; row++)
        if (ccSvc_buf[CC_SCRATCH_REMAP_OFFSET + row] != CC_REMAP_NONE)
            copied++;
    ccTrace(AUTOSAVE_TRACE_CC_EVT_NAMES,
            ((uint32_t)copied << 8u) |
            ((uint32_t)((ccSvc_nameRetry > 255u) ? 255u : ccSvc_nameRetry)
             << 24u));
#endif
}

static void ccSvc_traceOpFinish(void)
{
#if DEV_MODE_LOGGING
    ccTrace(AUTOSAVE_TRACE_CC_EVT_OP_FINISH,
            (uint32_t)ccSvc_traceState.jobs_done |
            ((uint32_t)ccSvc_traceState.jobs_dropped << 8u) |
            ((uint32_t)ccSvc_traceState.reg_passes << 16u) |
            ((uint32_t)(ccSvc_traceState.bits & 0x0Fu) << 24u) |
            ((uint32_t)((ccSvc_traceState.bits >> 4u) & 0x3u) << 28u));
    ccSvc_traceState.bits = (uint8_t)(ccSvc_traceState.bits & 0x0Fu);
#endif
}
```

---

## 6. Stage E — `Core/Menu/CopyClear/copyClearSession.c` / `.h`

#### E-01 MODIFY the row-stack block (lines 51–56) — (.c)

```c
/*
 * Press-order stack for the held row (9 B): 16 four-bit row indices (SEQ
 * 0..15, SELECT 0..7, EFFECTS SEQ 0..15), oldest first, plus a count. Top =
 * most recent button still held. Indices, not absolute steps: steps 16..127
 * do not fit four bits (F1 §2.1: ranges in bars 2..8 broke and copy sources
 * never committed). Absolute coordinates are formed per press by
 * cc_rowAbsolute(). Accessors: cc_rowPush/cc_rowRemove/cc_rowTop.
 */
```

#### E-02 ADD `cc_rowAbsolute()` before `cc_rowPush()` (line 95) and MODIFY `cc_rowPush()` (lines 95–117) — (.c)

```c
/*
 * Absolute coordinate of one held row button (F1-C).
 *
 * What: a SEQ index becomes 16 * visible bar + index for step objects;
 * SELECT (bars) and EFFECTS SEQ (FX steps) indices are already absolute.
 * Why: the stack keeps 4-bit indices (E-01). The visible bar cannot change
 * while a row is held (BAR is consumed then). Inputs: row index; the kind
 * already set in cc_source. Output: start/end coordinate. Caller:
 * cc_rowPush().
 */
static uint8_t cc_rowAbsolute(uint8_t index)
{
    return (cc_source.kind == CC_KIND_STEP)
               ? (uint8_t)buttonHandler_visibleStep(index)
               : index;
}

/*
 * Range rule, press side (spec §4.2, F1-C): a press while another button of
 * the same row is held makes a pair (start = most recent still-held button,
 * end = this one); later pairs replace earlier ones. The first press of a
 * hold is the single object until a pair exists. Stores the row index.
 */
static void cc_rowPush(uint8_t index)
{
    if (cc_rowCount == 0u) {
        cc_source.start = cc_rowAbsolute(index);
        cc_source.end = cc_source.start;
        cc_state.flags = (uint8_t)(cc_state.flags & (uint8_t)~CC_FLAG_PAIR);
    } else {
        cc_source.start = cc_rowAbsolute(cc_rowTop());
        cc_source.end = cc_rowAbsolute(index);
        cc_state.flags = (uint8_t)(cc_state.flags | CC_FLAG_PAIR);
    }
    if (cc_rowCount < 16u) {
        cc_rowSet(cc_rowCount, index);
        cc_rowCount++;
    }
}
```

#### E-03 ADD `cc_applyButtonLeds()` after `cc_start()` (line 154) — (.c) (F1-B)

```c
/*
 * Drive the copy/clear and SHIFT LEDs from the operation state (F1-B).
 *
 * What: copy with no menu -> copy/clear steady; copy menu open (provisional
 * or set source) -> copy/clear blinking; clear -> copy/clear and SHIFT
 * blinking. No other LED is touched; no LED shows the source (F1-A). Why:
 * user rule; one function so press, menu open, mode change and release
 * cannot disagree. Inputs: cc_state.phase, cc_state.menu. Output: LED
 * state. Callers: copyClear_copyPressed(), cc_openMenu(), cc_clearObject(),
 * copyClear_postEvent().
 */
static void cc_applyButtonLeds(void)
{
    if (cc_state.phase == CC_OP_CLEAR) {
        led_setBlinkLed(LED_SHIFT, 1u);
        led_setBlinkLed(LED_COPY, 1u);
    } else if (cc_state.menu != CC_MENU_NONE) {
        led_setBlinkLed(LED_COPY, 1u);
    } else {
        led_setBlinkLed(LED_COPY, 0u);
        led_setValue(1u, LED_COPY);
    }
}
```

#### E-04 MODIFY `cc_openMenu()` (lines 172–178)

```c
/* Open a copy menu at its default selection (copy menus always reset). */
static void cc_openMenu(cc_menu_t menu)
{
    cc_state.menu = (uint8_t)menu;
    cc_state.selection = 0u;
    cc_applyButtonLeds();
    menu_copyClearMenuChanged();
}
```

#### E-05 REPLACE `cc_flashDestination()` (lines 196–200) with `cc_flashObject()` and `cc_sourceLength()` — (.c) (F1-F)

```c
/*
 * Flash every visible LED of a paste destination or clear object once
 * (F1-F; user Q7, A1).
 *
 * What: steps — the SEQ LEDs of the object steps in the visible bar when the
 * object is the viewed Scene's active track (VOICE/STEP); bars — the SELECT
 * LEDs of the bars and all 16 SEQ LEDs when the visible bar is one of them
 * (STEP); track — the TRACK LED only; Scene — its SEQ LED in PERF; FX steps
 * — their SEQ LEDs on the viewed Scene (EFFECTS). Indices wrap modulo the
 * row size (128 steps, 8 bars, 16 FX steps). Why: user rule — a single
 * flashed step does not show what a range covers. Inputs: kind, Scene,
 * track, first element, element count. Output: one led_flashGroup() per
 * affected row (existing pre-S075 flash). Callers: cc_copySeq(),
 * cc_copySelect(), cc_copyTrack(), cc_clearPress(), cc_clearReleaseObject().
 */
static void cc_flashObject(cc_kind_t kind, uint8_t scene, uint8_t track,
                           uint8_t first, uint8_t count)
{
    uint8_t mode = buttonHandler_getMode();
    uint8_t viewed = (uint8_t)(scene == cc_activeScene());
    uint16_t seq = 0u;
    uint8_t sel = 0u;
    uint8_t voice = 0u;
    uint8_t i;

    switch (kind) {
    case CC_KIND_STEP:
        if ((mode == SELECT_MODE_VOICE || mode == SELECT_MODE_STEP) &&
            viewed && track == menu_getActiveVoice())
            for (i = 0u; i < count; i++) {
                uint8_t s = (uint8_t)((first + i) & (NUM_STEPS - 1u));

                if ((uint8_t)(s / NUM_STEPS_PER_BAR) == menu_currentBar)
                    seq = (uint16_t)(seq | (uint16_t)(1u << (s % 16u)));
            }
        break;
    case CC_KIND_BAR:
        if (mode == SELECT_MODE_STEP && viewed)
            for (i = 0u; i < count; i++) {
                uint8_t b = (uint8_t)((first + i) & (NUM_BARS - 1u));

                sel = (uint8_t)(sel | (uint8_t)(1u << b));
                if (b == menu_currentBar && track == menu_getActiveVoice())
                    seq = 0xFFFFu;
            }
        break;
    case CC_KIND_TRACK:
        voice = (uint8_t)(1u << (track & 7u));
        break;
    case CC_KIND_SCENE:
        if (mode == SELECT_MODE_PERF)
            seq = (uint16_t)(1u << (scene & 15u));
        break;
    case CC_KIND_FX_STEP:
        if (mode == SELECT_MODE_FX && viewed)
            for (i = 0u; i < count; i++)
                seq = (uint16_t)(seq | (uint16_t)(1u << ((first + i) & 15u)));
        break;
    default:
        break;
    }
    if (seq)
        led_flashGroup(LED_FLASH_GROUP_SEQ, seq);
    if (sel)
        led_flashGroup(LED_FLASH_GROUP_SELECT, sel);
    if (voice)
        led_flashGroup(LED_FLASH_GROUP_VOICE, voice);
}

/* Elements in the current source/object (steps, bars or FX steps). */
static uint8_t cc_sourceLength(void)
{
    uint8_t lo = (cc_source.start < cc_source.end) ? cc_source.start
                                                   : cc_source.end;
    uint8_t hi = (cc_source.start < cc_source.end) ? cc_source.end
                                                   : cc_source.start;

    return (uint8_t)(hi - lo + 1u);
}
```

#### E-06 MODIFY `cc_commitSource()` (lines 187–194) — F1-C step 3, deviation 0.8, trace T3-02

```c
/*
 * Set the copy source and start the operation (spec §3.1, F1-C).
 *
 * What: phase becomes CC_OP_COPY. Row sources (steps, bars, FX steps)
 * already show their menu from the first press, so the menu and the
 * selection the user chose are kept and nothing repaints (user: "release
 * all -> nothing on screen, source is set"); TRACK and Scene sources open
 * their menu here (set on press). Trace SOURCE_SET (final source only, Q5).
 * Callers: copyClear_buttonReleased(), cc_copySeq() (PERF), cc_copyTrack().
 */
static void cc_commitSource(void)
{
    cc_state.phase = CC_OP_COPY;
    cc_start();
    if (cc_state.menu == CC_MENU_NONE)
        cc_openMenu(ccCopy_menuForSource(&cc_source));
    ccTrace(AUTOSAVE_TRACE_CC_EVT_SOURCE_SET,
            (uint32_t)(cc_source.kind & 0x7u) |
            ((uint32_t)(cc_source.scene & 0xFu) << 3u) |
            ((uint32_t)(cc_source.track & 0x7u) << 7u) |
            ((uint32_t)(cc_source.start & 0x7Fu) << 10u) |
            ((uint32_t)(cc_source.end & 0x7Fu) << 17u) |
            ((uint32_t)(buttonHandler_getMode() & 0x7u) << 24u));
}
```

#### E-07 ADD `cc_copyRowPress()` before `cc_copySeq()` (line 204) — (.c) (F1-C step 2)

```c
/*
 * One press of a copy source row before the source is set (F1-C).
 *
 * What: the first press of a hold sets kind/Scene/track, pushes the row
 * index, starts the operation and opens the copy menu at its default with
 * the press as the provisional source; further presses of the same row push
 * (range rule) and repaint the indicator. Presses of another row while one
 * is held are consumed and ignored. Why: user sequence (F1 §5.1) — the menu
 * appears on the first press, the range follows each press, release of all
 * sets the source (copyClear_buttonReleased()). Inputs: row family, kind,
 * track, row index. Output: 1 (consumed). Callers: cc_copySeq(),
 * cc_copySelect().
 */
static uint8_t cc_copyRowPress(uint8_t row, cc_kind_t kind, uint8_t track,
                               uint8_t index)
{
    if (cc_state.row != CC_ROW_NONE && cc_state.row != row)
        return 1u;
    if (cc_rowCount == 0u) {
        cc_source.kind = (uint8_t)kind;
        cc_source.scene = cc_activeScene();
        cc_source.track = track;
    }
    cc_state.row = row;
    cc_rowPush(index);
    if (cc_state.menu == CC_MENU_NONE) {
        cc_start();
        cc_openMenu(ccCopy_menuForSource(&cc_source));
    } else {
        menu_copyClearMenuChanged();
    }
    return 1u;
}
```

#### E-08 MODIFY `cc_copySeq()` (lines 204–271), `cc_copySelect()` (273–304), `cc_copyTrack()` (306–336)

- `cc_copySeq()` VOICE/STEP no-source branch (lines 210–221) → `return
  cc_copyRowPress(CC_ROW_SEQ, CC_KIND_STEP, menu_getActiveVoice(), index);`
- VOICE/STEP paste (lines 222–227):

```c
        if (cc_source.kind == CC_KIND_STEP) {
            uint8_t dst = (uint8_t)buttonHandler_visibleStep(index);

            if (ccCopy_requestPaste(&cc_source, cc_state.selection,
                                    cc_activeScene(), menu_getActiveVoice(),
                                    dst))
                cc_flashObject(CC_KIND_STEP, cc_activeScene(),
                               menu_getActiveVoice(), dst, cc_sourceLength());
        }
```

- PERF paste (lines 241–246): `cc_flashObject(CC_KIND_SCENE, index, 0u,
  index, 1u)`.
- EFFECTS no-source branch (lines 251–262) → `return
  cc_copyRowPress(CC_ROW_SEQ, CC_KIND_FX_STEP, 0u, index);`; paste (263–267):
  `cc_flashObject(CC_KIND_FX_STEP, cc_activeScene(), 0u, index,
  cc_sourceLength())`.
- `cc_copySelect()` no-source branch (lines 279–290) → `return
  cc_copyRowPress(CC_ROW_SELECT, CC_KIND_BAR, menu_getActiveVoice(),
  index);`; bar paste (295–300): `cc_flashObject(CC_KIND_BAR,
  cc_activeScene(), menu_getActiveVoice(), index, cc_sourceLength())`.
- `cc_copyTrack()` paste (328–332): `cc_flashObject(CC_KIND_TRACK,
  cc_activeScene(), index, index, 1u)` (TRACK LED only, Q7).

#### E-09 MODIFY `cc_clearObject()` (lines 340–352) — F1-G

```c
/*
 * Capture a clear object and show its menu (F1-G; user Q2, Q6).
 *
 * What: stores the object, marks it held, starts the operation, and keeps
 * the clear selection when the object is in the same button group as the
 * previous object (SEQ steps, SELECT bars, TRACK, PERF SEQ Scenes; each
 * group is exactly one object kind), otherwise starts at `cancel`. A kept
 * selection that does not exist in the new menu (TRACK `send` outside
 * EFFECTS) falls back to `cancel`. The first object of an operation always
 * starts at `cancel` (cc_source.kind is NONE after copyClear_copyPressed()).
 * Why: user rule — further objects of one group are real clears with the
 * selection shown; only another group or releasing copy/clear returns to
 * `cancel`. Inputs: kind, Scene, track, first index. Output: menu state,
 * repaint. Callers: cc_clearPress(), copyClear_buttonPressed() (TRACK).
 */
static void cc_clearObject(cc_kind_t kind, uint8_t scene, uint8_t track,
                           uint8_t index)
{
    cc_kind_t previous_kind = (cc_kind_t)cc_source.kind;
    cc_menu_t menu = ccClear_menuForObject(buttonHandler_getMode(), kind);

    cc_source.kind = (uint8_t)kind;
    cc_source.scene = scene;
    cc_source.track = track;
    cc_source.start = index;
    cc_source.end = index;
    cc_state.flags = (uint8_t)(cc_state.flags | CC_FLAG_OBJECT);
    cc_start();
    if (kind != previous_kind ||
        cc_state.selection >= ccClear_selectionCount(menu))
        cc_state.selection = 0u;
    cc_state.menu = (uint8_t)menu;
    cc_applyButtonLeds();
    menu_copyClearMenuChanged();
}
```

#### E-10 MODIFY `cc_clearPress()` (lines 354–403)

- EFFECTS SEQ (line 364): `cc_flashObject(CC_KIND_FX_STEP, cc_activeScene(),
  0u, index, 1u);`
- Range push (lines 371–372): `cc_rowPush(index);` (raw index).
- First step press (line 383): `cc_rowPush(index);` (raw index). The
  `cc_clearObject(..., 0u)` call before it stays (start/end are then set by
  the push).

#### E-11 REPLACE `cc_clearReleaseObject()` (lines 405–428) — F1-F, F1-H

```c
/*
 * Queue the clear for the released object unless `cancel` is shown (spec
 * §3.2, F1-H).
 *
 * What: ccClear_requestClear() queues the job and turns the object's trigger
 * bits off at once when the selection ends with triggers off (repainting the
 * step LEDs), then every visible LED of the object flashes once above the
 * repainted base. The menu stays up with its selection (F1-G). Why: user
 * order — blink, trigger bits, then the background pool work, so the clear
 * never looks hung. Inputs: the held object, the selection. Output: queued
 * job, triggers, flash. Callers: copyClear_buttonReleased().
 */
static void cc_clearReleaseObject(void)
{
    uint8_t lo;

    if ((cc_state.flags & CC_FLAG_OBJECT) == 0u)
        return;
    cc_state.flags = (uint8_t)(cc_state.flags & (uint8_t)~CC_FLAG_OBJECT);
    if (cc_state.selection == 0u)
        return;
    if (!ccClear_requestClear(&cc_source, cc_state.selection))
        return;
    lo = (cc_source.start < cc_source.end) ? cc_source.start : cc_source.end;
    cc_flashObject((cc_kind_t)cc_source.kind, cc_source.scene,
                   cc_source.track,
                   (cc_source.kind == CC_KIND_TRACK) ? cc_source.track : lo,
                   cc_sourceLength());
}
```

#### E-12 MODIFY `copyClear_copyPressed()` (lines 443–465) — F1-B, trace T3-01

```c
uint8_t copyClear_copyPressed(uint8_t shift_held)
{
    uint8_t mode = buttonHandler_getMode();
    uint32_t refused = 0u;

    /*
     * Refusals are silent (spec §3.3). S075 trace (R12): OP_REFUSED records
     * every reason that applied, so a press that "did nothing" can be
     * explained.
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
    cc_rowReset();
    cc_state.menu = CC_MENU_NONE;
    cc_state.selection = 0u;
    cc_state.flags = 0u;
    memset(&cc_source, 0, sizeof(cc_source));
    ccSvc_interactionStarted();
    cc_state.phase = shift_held ? CC_OP_CLEAR : CC_OP_ARMED_COPY;
    cc_applyButtonLeds();
    /* S075 trace: OP_START with phase, mode, operation sequence, queued work. */
    ccTrace(AUTOSAVE_TRACE_CC_EVT_OP_START,
            (uint32_t)(cc_state.phase & 0x3u) |
            ((uint32_t)(mode & 0x7u) << 2u) |
            ((uint32_t)ccSvc_traceOpSequence() << 8u) |
            ((uint32_t)ccSvc_jobCount() << 16u));
    return 1u;
}
```

#### E-13 MODIFY `copyClear_copyReleased()` (lines 467–492) — F1-A, trace T3-03

- REMOVE lines 484–486 (`led_setBlinkGroup(…, 0u)` ×3).
- Before `ccSvc_interactionEnded();` (line 489) ADD:

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

#### E-14 MODIFY `copyClear_buttonReleased()` (lines 583–598) — F1-C

```c
    if (cc_state.row == row && row != CC_ROW_NONE) {
        /* F1-C: the stack holds row indices; remove the raw index. */
        if (!cc_rowRemove(index))
            return 1u;
        cc_state.row = CC_ROW_NONE;
        /* Last button of the row released: the source/object is final. */
        if (cc_state.phase == CC_OP_ARMED_COPY)
            cc_commitSource();
        else if (cc_state.phase == CC_OP_CLEAR)
            cc_clearReleaseObject();
        return 1u;
    }
```

#### E-15 REPLACE `copyClear_postEvent()` body (lines 605–670) — F1-A, F1-B

```c
void copyClear_postEvent(void)
{
    /* F1-A/F1-B: re-assert the button LEDs only; no source indication. */
    if (cc_state.phase == CC_OP_NONE)
        return;
    cc_applyButtonLeds();
}
```

#### E-16 MODIFY `copyClear_menuVisible()` (lines 683–690) — F1-C step 4

```c
/* Contract in copyClearSession.h: any held phase with a menu set (F1-C). */
uint8_t copyClear_menuVisible(void)
{
    return (uint8_t)(cc_state.phase != CC_OP_NONE &&
                     cc_state.menu != CC_MENU_NONE);
}
```

#### E-17 MODIFY `copyClear_formatMenu()` line 807 — F1-D

```c
    cc_put(row0, &pos, (cc_state.phase == CC_OP_CLEAR) ? "CLR" : "COPY");
    /* F1-D (user): the source indicator always starts at the 9th character. */
    pos = 8u;
    cc_formatIndicator(row0, &pos);
```

#### E-18 MODIFY `copyClear_serviceFinished()` (lines 866–875) — F1-A

REMOVE lines 872–874 (`led_setBlinkGroup(…, 0u)` ×3).

#### E-19 `copyClearSession.h` — contract blocks

- `cc_menu_t` block (lines 61–62) REPLACE:

```c
/*
 * Which selection list the menu shows (copyOps.h / clearOps.h own labels).
 * Copy menus open at their default on the first source press (F1-C). Clear
 * menus (F1-G): a menu opens at `cancel` for the first object of a button
 * group (SEQ steps, SELECT bars, TRACK, PERF SEQ Scenes); further objects of
 * the same group keep the selection, so each is a real clear, until
 * copy/clear is released or another group is pressed.
 */
```

- `copyClear_copyPressed()` block (lines 94–104): "LEDs: copy -> copy/clear
  LED steady until the copy menu opens, then blinking; clear -> SHIFT and
  copy/clear blink, latched until release (F1-B). Refusals write an
  OP_REFUSED trace record (DEV)."
- `copyClear_buttonPressed()` block (lines 117–125) ADD the F1-C paragraph:

```c
 * SEQ/SELECT/FX rows (F1-C): the first press opens the menu with that button
 * as the (provisional) source or clear object; a press while another button
 * of the row is held makes a range from the most recently pressed held
 * button to the new one; releases change nothing until the last button is
 * released, which sets the copy source or queues the clear (unless
 * `cancel`). Row buttons are stored as 0..15 indices; absolute steps are
 * formed per press, so ranges work in every bar. Accepted pastes and clears
 * flash every visible LED of their destination/object once (F1-F).
```

- `copyClear_postEvent()` block (lines 129–134) REPLACE:

```c
/*
 * After every processed button event (consumed or not).
 * Output: re-asserts the copy/clear and SHIFT LED blinks, because a mode
 * change clears blink slots (F1-B). No LED shows the source: the menu's
 * source indicator does (user, F1-A). Cheap and idempotent.
 * Caller: buttonHandler_processEvents().
 */
```

- `copyClear_menuVisible()` block (lines 143–149): "… nonzero while a menu is
  set in any held phase (copy: from the first source press, provisional or
  set; clear: from the first object press until copy/clear is released) …
  row 0 `COPY`/`CLR` at column 0 and the source indicator from column 8 (9th
  character, F1-D) …".
- `copyClear_potTurned()` sentence in the ownership block (lines 153–163):
  "… starts a pot clear in a clear operation when no menu is shown; pot clears
  never open a menu; once an object has opened a clear menu, pots do nothing
  until copy/clear is released (F1-G-02, user Q3) …".

---

## 7. Stage F — `Core/Menu/CopyClear/copyOps.c` / `.h`

#### F-01 MODIFY labels (lines 29–34) — F1-E

```c
/*
 * Step and bar copy selections (F1-E; user feedback 7, Q1), default first:
 * replace all / merge all / replace automation / merge automation. Values
 * are cc_copy_step_sel_t; labels ≤ 14 characters.
 */
static const char *const ccCopy_stepLabels[] = {
    "step -> repl", "step -> merge", "auto -> repl", "auto -> merge"
};
static const char *const ccCopy_barLabels[] = {
    "bar -> repl", "bar -> merge", "auto -> repl", "auto -> merge"
};
```

#### F-02 MODIFY `ccCopy_requestPaste()` (lines 88–130) — F1-I §11.4, trace T4-01

```c
uint8_t ccCopy_requestPaste(const cc_source_t *src, uint8_t selection,
                            uint8_t dst_scene, uint8_t dst_track,
                            uint8_t dst_start)
{
    cc_job_t job;
    uint8_t identical = 0u;
    uint8_t slot;

    if (!src || src->kind == CC_KIND_NONE)
        return 0u;
    job.op = (uint8_t)(CC_JOB_PASTE | (selection & CC_JOB_SEL_MASK));
    job.kind = src->kind;
    job.scene = dst_scene;
    job.track = dst_track;
    job.start = dst_start;
    job.end = dst_start;
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
    if (identical) {
        /* S075 trace (R1): the press was valid but changes nothing. */
        ccTrace(AUTOSAVE_TRACE_CC_EVT_PASTE_NOOP, ccTrace_job(&job));
        return 0u;
    }
    slot = ccSvc_enqueue(&job);
    if (slot == 0u)
        return 0u;
    /*
     * F1-I §11.4 (user Q4): step/bar/track replace and merge pastes write
     * their destination trigger bits now; the job copies the blocks later.
     */
    (void)ccSvc_pasteTriggersNow((uint8_t)(slot - 1u));
    return 1u;
}
```

`copyOps.h` contract for `ccCopy_requestPaste()` (lines 49–55) gains: "On
acceptance, step/bar/track `… -> repl`/`… -> merge` pastes write their
destination trigger bits at once (ccSvc_pasteTriggersNow()). An identical
paste writes a PASTE_NOOP trace record (DEV)."

#### F-03 MODIFY `ccCopy_buildStep()` `CC_COPY_AUTO` branch (lines 469–475) — F1-K

```c
    case CC_COPY_AUTO:
        /*
         * auto -> repl (F1-K, user B2): automation and the probability
         * special come from the source; note/velocity specials and the
         * trigger stay. Skip only when nothing would change.
         */
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
```

`copyOps.h` `ccCopy_buildStep()` block (lines 95–111): "`copy … automation`
keeps the destination note/velocity specials and trigger and takes the source
automation **and probability special** (set, changed or removed, F1-K);
`merge automation` keeps all destination specials, probability included, and
forms the union."

#### F-04 Name phases in Scene-level executors (F1-I §11.1, deviation 0.7)

Common rule, added to the `copyOps.h` `ccCopy_runJob()` block (lines
114–127): "Scene-level executors commit their data first and record the name
remap in a final phase that waits (CC_RUN_WAIT) on ccSvc_namesReady(), so a
running writer delays only the names, never the data."

#### F-05 MODIFY `ccCopy_runInstrument()` (lines 526–579) — trace T4-02

Block (526–535) ADD: "Phases: 0 Advanced check, wait for idle workers,
commit (fan-out mask kept in run.aux), decay pair; 1 drive the bounded
Instrument apply; 2 record names once the buffer is held. Trace: FANOUT kind
1; JOB_END reason ADVANCED_LIMIT with the refusing member and slot."

Code:

```c
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
    ...
    for (m = 0u; m < SCENE_COUNT; m++)
        if ((mask & ccCopy_bit(m)) != 0u &&
            !instrumentManager_typeSelectableForSceneSlot(m, d_slot, type)) {
            /* S075 trace (R1): member m would exceed two Advanced types. */
            ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_ADVANCED_LIMIT,
                                  (uint8_t)((m & 0x0Fu) | (d_slot << 4u)));
            return CC_RUN_DROP;
        }
    if (!preset_applyWorkersIdle())
        return CC_RUN_WAIT;
    preset_startInstrumentCopy(src->scene, s_slot, mask, d_slot);
    /* S075 trace (R10): Scenes reached by the Instrument paste. */
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)mask | ((uint32_t)(job->scene & 0xFu) << 16u) |
            (1u << 20u) | ((uint32_t)ccCopy_activeIn(mask) << 24u) |
            ((uint32_t)(d_slot & 0xFu) << 25u));
    for (m = 0u; m < SCENE_COUNT; m++) {
        if ((mask & ccCopy_bit(m)) == 0u)
            continue;
        ... (decay pair unchanged; the ccSvc_nameCopy() call moves to phase 2)
    }
    bank_revalidateVoiceEditMasks();
    run->aux = mask;
    run->phase = 1u;
    return CC_RUN_WAIT;
```

#### F-06 MODIFY `ccCopy_runKit()` (lines 581–626) — trace T4-03

- `if (!source) return CC_RUN_DROP;` → reason `NO_SOURCE` first.
- Loop: remove `ccCopy_nameKit(m, src->scene);` (line 619).
- After the loop, before `bank_revalidateVoiceEditMasks();`:

```c
    run->aux = (uint16_t)(mask & (uint16_t)~ccCopy_bit(src->scene));
    /* S075 trace (R10): Scenes that received the Kit (source excluded). */
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)run->aux | ((uint32_t)(job->scene & 0xFu) << 16u) |
            (2u << 20u) | ((uint32_t)ccCopy_activeIn(mask) << 24u));
```

- Phase 1 (lines 596–602) ends with `run->phase = 2u; return CC_RUN_WAIT;`;
  ADD phase 2: wait on `ccSvc_namesReady()`, then `ccCopy_nameKit(m,
  src->scene)` for every member in `run->aux`, `return CC_RUN_DONE;`. Block
  (581–587) ADD "Phases: 0 commit, 1 wait for the Scene worker, 2 names."

#### F-07 MODIFY `ccCopy_runEffect()` (lines 628–650) — trace T4-04

```c
/*
 * Paste a whole Effect record with edit-mask fan-out (spec §4.4). Phases:
 * 0 commit (written mask kept in run.aux; trace FANOUT kind 3), 1 names
 * once the buffer is held.
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
```

#### F-08 MODIFY `ccCopy_runSceneSettings()` (lines 652–672) and `ccCopy_runScene()` (674–728) — trace T4-05

- `ccCopy_runSceneSettings()`: `NO_SOURCE` reason at its drop; after
  `bank_exchangeVoiceEditMask(src->scene, job->scene);`:

```c
    /* S075 trace (R10): the destination's edit-mask entry after exchange. */
    ccTrace(AUTOSAVE_TRACE_CC_EVT_MASK_SET,
            (uint32_t)bank_sceneMaskVoiceEditForScene(job->scene) |
            ((uint32_t)(job->scene & 0xFu) << 16u) |
            ((uint32_t)(src->scene & 0xFu) << 21u));
```

- `ccCopy_runScene()`: `NO_SOURCE` reason; the same `MASK_SET` record after
  its exchange (line 694); REMOVE the name calls from phase 0 (lines
  701–707); phase 2 (default, lines 719–726) ends with `run->phase = 3u;
  return CC_RUN_WAIT;`; ADD phase 3: wait on `ccSvc_namesReady()`, then the
  four name copies (Scene row, `ccCopy_nameKit()`, Pattern row, Effect row),
  `return CC_RUN_DONE;`. Block (674–678): "Phases: 0 retained commits; 1
  whole-Pattern copy; 2 Scene activation if active; 3 names once the buffer
  is held."

#### F-09 MODIFY `ccCopy_runPatternOnly()` (lines 730–740)

```c
/*
 * `copy pattern`: whole region, retargeted when types differ (spec §9.8).
 * Phases: 0 region copy (engine sub-state), 1 Pattern-row name once the
 * buffer is held.
 */
static uint8_t ccCopy_runPatternOnly(const cc_job_t *job)
{
    cc_run_t *run = ccSvc_run();
    const cc_source_t *src = copyClear_source();

    if (run->phase == 0u) {
        uint8_t r = ccSvc_runRegionCopy(src->scene, job->scene);

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
```

#### F-10 MODIFY `ccCopy_runFxSteps()` (lines 742–788) — F1-I, trace T4-06

Block (742–752): "The source range is snapshotted on the stack (≤ 288 B) so
overlapping ranges on one Scene are safe and no name buffer is needed
(F1-I)."

Code:

```c
    effect_seq_step_t snap[EFFECT_SEQ_STEP_COUNT];
    ...
    if (!s || !d ||
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
    for (i = 0u; i < count; i++)
        snap[i] = s->steps[(uint8_t)((int8_t)src->start + dir * (int8_t)i)];
    ... (paste loop unchanged)
    mask = bank_sceneFanoutMask(job->scene);
    ccTrace(AUTOSAVE_TRACE_CC_EVT_FANOUT,
            (uint32_t)mask | ((uint32_t)(job->scene & 0xFu) << 16u) |
            (6u << 20u) | ((uint32_t)ccCopy_activeIn(mask) << 24u));
```

REMOVE `uint8_t *scratch = ccSvc_scratch();` and the `snap` pointer into
the buffer (lines 758–759, 770).

#### F-11 MODIFY `ccCopy_runJob()` (lines 790–818) — trace T4-07

At both `default: return CC_RUN_DROP;` (lines 811, 815–816):
`ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_BAD_SELECTION, sel);` first.

---

## 8. Stage G — `Core/Menu/CopyClear/clearOps.c` / `.h`

#### G-01 ADD `ccClear_triggersOffNow()` before `ccClear_requestClear()` (line 81) and MODIFY `ccClear_requestClear()` (lines 81–94) — F1-H

```c
/*
 * Turn the clear's trigger bits off at once (F1-H; user feedback 11, A1).
 *
 * What: for selections that end with triggers off — `clear step|bar|track`
 * (all, notes), `clear scene`, `clear pattern`, `clear notes` — writes bit 15
 * of every object step to 0 through pat_setStepActive() (one aligned
 * halfword RMW each; bits 14..0 untouched), then repaints the step LEDs
 * (ccSvc_triggersChangedUi(): for `clear track` on the current track its
 * step LEDs go dark at once, A1). `… automation`, `clear send`, `clear scene
 * settings` and the FX clears leave triggers alone. Why: user order — the
 * clear must look accepted immediately; the queued pool work follows and
 * publishes the same trigger-off state. A clear dropped later (only a
 * pre-S075 block in the swap block of a nearly full pool) leaves the
 * triggers off (accepted, A5). Safety: trigger-bit writes are ordinary
 * foreground edits (step toggles); the pool engine re-reads the live trigger
 * inside its PRIMASK publish. Cost: at most 896 RMWs (PERF Scene), about
 * 20 µs. Inputs: object, selection. Output: triggers, LEDs, one EARLY_TRIG
 * trace record. Caller: ccClear_requestClear().
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
            s_last = (uint8_t)(hi * NUM_STEPS_PER_BAR + NUM_STEPS_PER_BAR - 1u);
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
```

`clearOps.h` `ccClear_requestClear()` block (lines 51–56) gains: "On
acceptance the object's trigger bits are turned off at once for selections
that end with triggers off, and the step LEDs repaint (F1-H)."

`pat_setStepActive()` needs `PatternData.h` (already included, line 15).

#### G-02 MODIFY file and enum contract blocks for `notes` — F1-J

- `clearOps.c` header (lines 1–11) and `clearOps.h` header (lines 1–13): add
  "`… notes` turns triggers off and removes note and velocity specials,
  keeping the probability special and the automation (F1-J, user A5)."
- `clearOps.h` `cc_clear_obj_sel_t` comment (line 21): "Step, bar and track
  clear selections (`send` only in EFFECTS TRACK). NOTES: trigger off, note
  and velocity removed, probability and automation kept (F1-J)."

#### G-03 MODIFY `ccClear_fxStepNow()` (lines 139–146) — trace T4-10

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

#### G-04 MODIFY `ccClear_potTurned()` (lines 148–176) — trace T4-11

ADD helper before it:

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

Records: no target → `ccClear_traceRegRefused(target->pattern_target, scene,
4u)` before `return 0u` (line 159); pending → reason 3 (line 161); full →
reason 1, other Scene → reason 2 (split the test at lines 163–166); lane
cleared → `FX_CLEAR` with step 15 (all), lane, changed (after line 170);
target added → after `ccSvc_registerAdd()` succeeds:

```c
        ccTrace(AUTOSAVE_TRACE_CC_EVT_REG_ADD,
                (uint32_t)target->pattern_target |
                ((uint32_t)(scene & 0xFu) << 16u) |
                ((uint32_t)(ccSvc_registerCount() & 0xFu) << 20u) |
                ((uint32_t)(has_lane ? target->fx_lane : 0xFu) << 24u));
```

#### G-05 MODIFY Scene-level clears — trace T4-12..T4-16

- `ccClear_runSend()` (lines 180–197): after the loop, `FANOUT` kind 4 with
  `mask`, slot in bits 25..28.
- `ccClear_runSceneSettings()` (lines 199–215) and `ccClear_runScene()`
  (lines 223–270): after `bank_resetVoiceEditMaskToSelf(…)`:

```c
    /* S075 trace (R10): the Scene's edit-mask entry was reset to itself. */
    ccTrace(AUTOSAVE_TRACE_CC_EVT_MASK_SET,
            (uint32_t)bank_sceneMaskVoiceEditForScene(scene) |
            ((uint32_t)(scene & 0xFu) << 16u) | (1u << 20u));
```

  (`scene` is `job->scene` in `ccClear_runSceneSettings()`.)
- `ccClear_runFx()` (lines 272–286): `FANOUT` kind 5 with `written`.
- `ccClear_runFxSequence()` (lines 288–295): `FX_CLEAR` with
  `bank_sceneFanoutMask(job->scene)`, step 15, lane 15, changed.
- `ccClear_runJob()` (lines 297–331): `BAD_SELECTION` reason at both
  `default: return CC_RUN_DROP;` (lines 326, 328–329).

---

## 9. Stage H — `Core/Bank/Scene/Pattern/PatternStackService.c` (T5, D4)

#### H-01 `patSvc_exclusiveMove()` (line 1399): REMOVE the per-move PatternTrace record (lines ~1415–1417)

REMOVE:

```c
    patternTrace_record(PAT_TRACE_STAGE_TIER2_RELOC, (uint8_t)(scene & 0x0Fu),
                        patSvc_relocationValue(address_index, old_offset,
                                               new_offset));
```

ADD in its place:

```c
    /*
     * S075 trace debug (user D4): no per-move PatternTrace record. Copy/clear
     * moves run while trace flushes are suspended, and one fragmented paste
     * can make hundreds of moves; per-move records would overwrite the
     * 32-record PatternTrace ring (and every other record in it) before the
     * next flush. The copy/clear JOB_STATS / REG_DONE records carry the move
     * counts instead. Reactive and repair relocations keep their R/L records.
     */
```

Function block above line 1399: replace "layout-only AutoSave dirty mark and
a relocation trace record" with "layout-only AutoSave dirty mark (the move
count is traced by the copy/clear caller)".

---

## 10. Stage I — `Core/Hardware/SD/filesystem.c` (T6)

#### I-01 ADD after `static uint8_t fs_name_cache_borrowed;` (line 1675) — (.c)

```c
#if DEV_MODE_LOGGING
/*
 * S075 trace debug (DEV only, 2 B; approved D1): last observed copy/clear
 * suspension state for the SUSPEND edge records in filesystem_tick(), and
 * the one-per-loan latch for the FS_REFUSED witness in filesystem_start().
 */
static uint8_t fs_cc_suspended_prev;
static uint8_t fs_cc_refusal_reported;
#endif
```

#### I-02 `filesystem_borrowNameCacheScratch()` (line 1784): after `fs_name_cache_borrowed = 1u;` ADD

```c
#if DEV_MODE_LOGGING
    fs_cc_refusal_reported = 0u;   /* S075 trace: one FS_REFUSED per loan */
#endif
```

#### I-03 `filesystem_tick()`: ADD after `const uint8_t cc_suspended = copyClear_backgroundSuspended();` (line 25553)

```c
#if DEV_MODE_LOGGING
    /*
     * S075 trace debug (R6): one SUSPEND record per suspension edge, with
     * whether a writer was still running (it finishes normally; copy/clear
     * runs at the trickle rate meanwhile) and which.
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

#### I-04 `filesystem_start()`: MODIFY the refusal at line 25919

```c
    if (fs_name_cache_borrowed && op != FS_INTERNAL_OP_UPDATE_HCNAMES_COPY) {
#if DEV_MODE_LOGGING
        /*
         * S075 trace debug (R7): the first operation refused during this
         * loan, so a test can see what the loan blocked (Load/Save browser,
         * Instrument browsing). Callers retry, so only one record per loan.
         * With F1-I the loan is taken only by overlapping pastes and names.
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

---

## 11. Stage J — `tools/decode_devlogs.py` (T7)

#### J-01 Module docstring (lines 1–27): after the `/asavetrc.bin` bullet ADD

```text
  Stage 'c' records (S075) witness Phase 6 copy/clear operations, jobs,
  drops, anomalies, early trigger writes and restores, trickle-rate work,
  suspension edges and the name-buffer loan.
```

#### J-02 `STAGE_ENUM` (line 96 onward) and `STAGE_PRODUCER` (line 123 onward) ADD

```python
    # c: S075 Phase 6 copy/clear lifecycle and risk witness (flags = event).
    "c": "AUTOSAVE_TRACE_STAGE_COPY_CLEAR",
```

```python
    "c": "Core/Menu/CopyClear/* (session, copyOps, clearOps, service) and "
         "filesystem_tick()/filesystem_start() suspension/loan witnesses",
```

#### J-03 ADD before `def trace_record_text(` (line 529)

The tables and `cc_job_text()` / `cc_record_text()` from the trace plan §9
T7-03, with these changes:

- `CC_EVENTS` adds `0x17: "JOB_TRICKLE"`, `0x19: "EARLY_RESTORED"`,
  `0x24: "EARLY_TRIG"`.
- `CC_ANOMALY` keeps 1–6 and 9.
- `cc_record_text()` adds:

```python
    if flags == 0x17:
        return f"{ev}: trickle ticks={v & 0xFFFF} calls={(v >> 16) & 0xFFFF}"
    if flags == 0x19:
        return (f"{ev}: restored={v & 0xFF} left alone={(v >> 8) & 0xFF} "
                f"slot={(v >> 16) & 3}")
    if flags == 0x24:
        what = "paste" if (v >> 8) & 1 else "clear"
        return (f"{ev}: {what} steps={v & 0xFF} "
                f"src mask={(v >> 9) & 1} "
                f"{CC_KINDS.get((v >> 16) & 7, '?')} Scene{(v >> 19) & 0xF} "
                f"track={((v >> 23) & 7) + 1} slot={(v >> 26) & 3}")
```

- The `NAMES` branch prints the reserved field only when nonzero:

```python
    if flags == 0x42:
        what = ["requested", "written", "error", "gave up"][v & 3]
        extra = (f" reserved=0x{(v >> 16) & 0xFF:02x}"
                 if (v >> 16) & 0xFF else "")
        return (f"{ev}: {what} copied rows={(v >> 8) & 0xFF}{extra} "
                f"refusals={(v >> 24) & 0xFF}")
```

#### J-04 `trace_record_text()` — ADD before the final `else:` (line ~801)

```python
    elif ch == "c":
        detail = f"{enum_name} via {producer}: {cc_record_text(flags, value)}"
```

---

## 12. Stage K — documentation

| File | Section | Change |
|---|---|---|
| `S075_PH6_COPY_CLEAR_FULL_SPEC.md` | §3.1 | Copy/clear LED steady when pressed; blinks while the copy menu is open (from the first source press). |
| | §3.2 steps 4–5 | Kept selection per button group; pot clears never open a menu; once an object opened a menu, pots do nothing until release. |
| | §4.1, §4.2 | Menu on the first row press (provisional source); the literal sequence (F1 §5.1); ranges in every bar. |
| | §4.4 | `copy … automation` takes the source probability special (F1-K); `merge automation` does not. Early trigger bits for `… -> repl`/`… -> merge` pastes; a dropped paste restores them (except steps the user changed since). |
| | §5 | `… notes` keeps probability (F1-J); clear order blink → trigger bits → pool work; dropped clears may leave triggers off. |
| | §6 | Pot clears start at once (trickle rate while an older writer finishes). |
| | §8.1 | Indicator at column 9 (`COPY    03T2s005`); labels `step -> repl` … `bar -> merge`. |
| | §8.2 | No source LED; destination flash covers every visible pasted/cleared LED; `copy track`/`clear track` flash only the TRACK LED. |
| | §9.3 | Name buffer used only by overlapping pastes and names. |
| | §9.5 | Live-source path; trickle governor; early-trigger masks. |
| `S075_PH6_COPYCLEAR_IMPLEMENTATION.md` | §14 | New "14.7 F1 follow-up and trace debug" entry: changes, deviations (§0 here), RAM/flash, build results. |
| `S075_PH6_COPYCLEAR_FOLLOWUP_F1.md`, `S075_PH6_COPYCLEAR_TRACE_DEBUG_IMPLEMENTATION.md` | headers | "Implemented by `S075_PH6_COPYCLEAR_F1_AND_TRACE_IMPLEMENTATION.md` (date)". |
| `knowledge_files/specification_reference/DEV_MODES.md` | `/asavetrc.bin` | Stage `c` table (events, drop reasons, anomaly codes); records reach the card after the operation; exclusive moves not traced in PatternTrace. |
| `…/STORAGE_SRAM_MANIFEST.md` | header, §5, §8.2, §8.3, §11 | +124 B production (masks 129, credit 2, LED −7); DEV +24 B (§8.3). |
| `…/MODULE_INTERCHANGE_SPEC.md` | ledHandler, CopyClear, AutosaveTrace | `led_setBlinkGroup()` removed; new `ccSvc_*` functions (`pasteTriggersNow`, `triggersChangedUi`, `namesReady`, `registerCount`, trace feeds); stage `c`. |
| `…/PATTERN_DYNAMIC_STACK.md` | §12.16, §12.17 | Exclusive moves not traced per move; early trigger writes are foreground trigger edits outside the claim. |
| `MEMORY.md` | Volatile Notes | S075 state line: F1 + trace implemented (unverified on hardware). |
| `SCOPING_TARGETS.md` | Phase 6 note | Remove "§4.11 LED priority stack implemented for the copy/clear LEDs (group blink)". |

---

## 13. Build and verification

1. `make all && make img` with `DEV_MODE_LOGGING 1` and again with
   `DEV_MODE_LOGGING 0`: no new warnings.
2. `arm-none-eabi-size -A build/lxr02.elf` against the pre-pass build:
   production `.bss` ≈ +124 B (§1); DEV build additionally +24 B;
   `arm-none-eabi-nm` shows no `ccSvc_traceState` / `fs_cc_*` with
   `DEV_MODE_LOGGING 0`.
3. `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`.
4. `grep -rn "led_setBlinkGroup\|CC_REMAP_CHANGED\|cc_flashDestination" Core`
   returns nothing.
5. `python3 tools/decode_devlogs.py <asavetrc.bin>`: no "no decoder" lines for
   stage `c`.
6. Hardware: F1 §15 verification table, plus the trace plan §13 table, plus:

| Case | Expected trace |
|---|---|
| Step paste during a running AutoSave drain | `SUSPEND` begin with facade busy, `JOB_START`, `JOB_TRICKLE` (nonzero), `JOB_END` DONE |
| `step -> repl` paste | `EARLY_TRIG` paste |
| Paste dropped for room | `CHECK_FAIL`, `EARLY_RESTORED`, `JOB_END` DROP NO_ROOM |
| Clear step range | `EARLY_TRIG` clear before `JOB_START` |
| Overlapping paste while a drain runs | `SCRATCH` borrow with a nonzero wait |

---

## 14. Record volume (trace)

Per operation frame ≈ 10 records; per paste/clear job 3–5 (`EARLY_TRIG`,
`JOB_START`, optional `JOB_STATS`/`JOB_TRICKLE`, `JOB_END`); per pot clear
2–3. A typical operation (one source, eight pastes) ≈ 50 records; a heavy one
(30 pastes, 8 pot clears) ≈ 180. Both fit the 2,048-record ring kept for
testing (user D2).

## 15. Work log

- 2026-10-02: Read `MEMORY.md`, the F1 follow-up, the trace plan, and this
  combined schedule. The working tree is the pre-F1 baseline at `b1216db`;
  implementation and hardware verification are still pending.
- 2026-10-02: Began implementation from the schedule's Stage A/B order; the
  completed pass and compiler-driven corrections are recorded below.
- 2026-10-02: Completed Stages A–J. The source group-blink state was removed;
  stage `c` and the event/drop/anomaly layouts were added; the service now has
  lazy scratch use, live non-overlap paste reads, early trigger capture and
  restore, and the whole-call trickle governor; the session, copy, and clear
  layers implement the F1 gesture, LED, label, notes/probability, and
  selection decisions; exclusive-move tracing is summary-only; filesystem
  suspension/refusal witnesses and the decoder were added.
- 2026-10-02: Stage K updated the full spec, F1/trace plans, SRAM/DEV mode,
  module, Pattern stack, scoping, and memory records. DEV and production
  builds both passed. Production (`DEV_MODE_LOGGING=0`) measured
  `text=516688`, `data=408`, `bss=409840`, and no trace-state symbols;
  DEV (`=1`) measured `text=530592`, `data=416`, `bss=426616`, flash used
  531008 B with 222656 B headroom. Hardware verification is still pending.
- 2026-10-02: Compile review found the Kit executor could finish before its
  lazy HCNAMES phase; it now advances through the worker phase to phase 2 and
  waits for `ccSvc_namesReady()` before completing. Both configurations were
  rebuilt after that correction and the DEV image was repackaged.
