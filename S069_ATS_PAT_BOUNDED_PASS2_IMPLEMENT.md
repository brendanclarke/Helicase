# S069 Bounded-CPU Pass 2 — Implementation Schedule

Parent plan: `S069_ATS_PAT_BOUNDED_CLAUDE.md` (items 4A, 4B, 4C).
Prerequisite: Pass 1 (items 3.1, 3.2, 5) hardware-accepted 2026-09-20.

## Scope

Pass 2 implements the elapsed-time background CPU budget primitive and applies
it to three work classes:

1. **Repair epoch** in `patSvc_tick()` (PatternStackService.c) — item 4A.
2. **Scalar AutoSave drain** phases 56 and 13 in
   `filesystem_autosaveParameterDrain_tick()` (filesystem.c) — item 4B.
3. **Pattern AutoSave drain** staging in
   `filesystem_patternWriteRegionSection()` (filesystem.c) — item 4C.

Additionally, the deferred `SCOPING_TARGETS.md` finding from Pass 1 is folded
into item 4A: the repair section of `patSvc_tick()` gains a Load/Save menu gate
so that Pattern maintenance does not consume CPU while the user is browsing the
Load/Save menu.

## Budget primitive design

State in filesystem.c (owner of the scheduler ladder and majority of budgeted
work), grouped into one struct so the approved allocation has no linker-padding
ambiguity:

```
static struct {
    uint32_t last_refill_us;
    int32_t  credit_us;
#if DEV_MODE_LOGGING
    uint32_t charged_us[3];
    uint16_t denied_count[3];
    uint16_t max_slice_us[3];
    uint32_t report_last_us;
#endif
} budget_state;
```

Public API (four functions):

```
uint8_t filesystem_backgroundBudgetAvailable(void);
void    filesystem_backgroundBudgetCharge(uint32_t start_us, uint8_t work_class);
void    filesystem_backgroundBudgetDeny(uint8_t work_class);
void    filesystem_backgroundBudgetRefill(void);
```

Refill logic: on each call (from filesystem_tick()), compute elapsed
milliseconds since last refill, add `elapsed_ms × rate` to credit, cap positive
credit at one millisecond's allowance. Rate is
`BACKGROUND_CPU_BUDGET_US_PER_MS_PLAYING` (25µs = 2.5%) when `seq_isRunning()`
returns nonzero, `BACKGROUND_CPU_BUDGET_US_PER_MS_STOPPED` (50µs = 5%) when
stopped. First call seeds the timestamp without adding credit.

Charge logic: computes `timebase_tim2Delta(now, start_us)`, subtracts from
credit. Updates per-class accounting (total charged, max single-slice, deny
count) for periodic diagnostic trace.

Available: returns `(uint8_t)(budget_state.credit_us > 0)`; a zero configured
rate is a debugging override that admits work without consuming credit.

Work classes:

```
#define FS_BUDGET_CLASS_REPAIR   0u
#define FS_BUDGET_CLASS_SCALAR   1u
#define FS_BUDGET_CLASS_PATTERN  2u
```

## Load/Save menu gate design

The repair section of `patSvc_tick()` gains a separate early return before the
repair while-loop:

```c
if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)
    return;
```

This follows the established filesystem.c scheduler gate pattern. Queue drain
and handover continue to run during Load/Save (they are bounded by edit rate
and correct for lifecycle transitions). Only repair is suppressed.

The gate is separate from the budget rather than folded into the budget's
refill/deny logic because:
- Immediate suppression with no lag (no residual credit to drain)
- Clear intent (menu policy is distinct from CPU bounding)
- The budget remains a pure CPU limiter, not overloaded with menu state

---

## Change list

### Item 4A — Budget primitive + repair gating

---

#### C01 — config.h:484 — ADD — Budget CPU allowance defines

After `AUTOSAVE_PATTERN_MAX_LATENCY_MS` (line 483), before the Session-065
pending automation block (line 485).

```
/*
 * Background CPU budget for AutoSave drain and Pattern repair work.
 *
 * What: BACKGROUND_CPU_BUDGET_US_PER_MS_PLAYING is the microseconds of
 * background work allowed per millisecond of wall time while the sequencer
 * transport is running (2.5% CPU). BACKGROUND_CPU_BUDGET_US_PER_MS_STOPPED
 * is the allowance while stopped (5% CPU). Zero disables the budget
 * (unlimited — debugging only).
 *
 * Why: bounds aggregate background CPU so that audio processing, sequencer
 * timing, and UI responsiveness are not impacted by AutoSave or Pattern
 * maintenance work. The playing rate is tighter because audio has hard
 * real-time constraints; the stopped rate allows faster convergence.
 *
 * Inputs: compile-time microsecond-per-millisecond rates. Outputs:
 * filesystem_backgroundBudgetAvailable() predicate and
 * filesystem_backgroundBudgetCharge() accounting. Affiliates:
 * filesystem.c budget state/refill/drain gates,
 * PatternStackService.c repair epoch gate, seq_isRunning().
 *
 * These constants consume no RAM.
 */
#define BACKGROUND_CPU_BUDGET_US_PER_MS_PLAYING  25u
#define BACKGROUND_CPU_BUDGET_US_PER_MS_STOPPED  50u
```

---

#### C02 — filesystem.c:~1843 — ADD — Budget state variables and accounting

After `fs_pattern_scene_cursor` (line 1842), before `op_file_version`
(line 1843). Place with a comment block.

```
/*
 * Background CPU budget state.
 *
 * What: elapsed-time budget that bounds aggregate background CPU across
 * all AutoSave drain phases and Pattern repair work. budget_state.last_refill_us
 * is the TIM2 timestamp of the last refill. budget_state.credit_us is a signed
 * microsecond credit: positive means work is allowed, negative means a
 * previous slice overshot and the deficit must be repaid before new work
 * is admitted.
 *
 * Why: concentrates CPU bounding in one primitive rather than ad-hoc
 * per-subsystem limits. The signed credit allows overshoot tracking
 * without separate deficit state. The one-millisecond cap in the refill
 * function prevents idle accumulation.
 *
 * Inputs: budget_state.last_refill_us seeded on first refill call;
 * budget_state.credit_us modified by refill (+) and charge (-).
 * Outputs: filesystem_backgroundBudgetAvailable() predicate.
 * Affiliates: filesystem_backgroundBudgetRefill() (called from
 * filesystem_tick()), filesystem_backgroundBudgetCharge() (called from
 * drain phases and PatternStackService.c repair), seq_isRunning()
 * (rate selection), config.h BACKGROUND_CPU_BUDGET_US_PER_MS_*.
 */
static struct {
    uint32_t last_refill_us;
    int32_t  credit_us;
#if DEV_MODE_LOGGING
    uint32_t charged_us[FS_BUDGET_CLASS_COUNT];
    uint16_t denied_count[FS_BUDGET_CLASS_COUNT];
    uint16_t max_slice_us[FS_BUDGET_CLASS_COUNT];
    uint32_t report_last_us;
#endif
} budget_state;

/*
 * Per-class budget accounting for diagnostic trace.
 *
 * What: accumulates total charged microseconds, maximum single-slice
 * duration, and deny count per work class since the last periodic trace
 * emission. Index: 0 = repair (FS_BUDGET_CLASS_REPAIR),
 * 1 = scalar drain (FS_BUDGET_CLASS_SCALAR),
 * 2 = pattern drain (FS_BUDGET_CLASS_PATTERN).
 *
 * Why: the periodic trace report (stage 'H') needs per-class evidence
 * that the configured CPU allowance is binding and that no single slice
 * is unexpectedly large. Counters reset after each emission.
 *
 * Inputs: filesystem_backgroundBudgetCharge() updates charged and max;
 * call sites increment denied on budget exhaustion.
 * Outputs: autosaveTrace_record() with stage 'H' every ~5 seconds.
 * Affiliates: budget_state.report_last_us,
 * filesystem_backgroundBudgetRefill().
 */
#define FS_BUDGET_CLASS_REPAIR   0u
#define FS_BUDGET_CLASS_SCALAR   1u
#define FS_BUDGET_CLASS_PATTERN  2u
#define FS_BUDGET_CLASS_COUNT    3u

```

RAM: 8 bytes (budget state) + 24 bytes (accounting) + 4 bytes (report
timestamp) = 36 bytes total.

---

#### C03 — filesystem.c — ADD — filesystem_backgroundBudgetRefill()

New function, placed near the budget state variables or in the scheduler
helper section (before filesystem_tick()).

```
/*
 * Refill the background CPU budget based on elapsed wall time.
 *
 * What: computes milliseconds elapsed since the last refill, adds
 * (elapsed_ms × rate) to budget_credit_us, and caps positive credit
 * at one millisecond's allowance. Rate is
 * BACKGROUND_CPU_BUDGET_US_PER_MS_PLAYING when seq_isRunning() returns
 * nonzero, BACKGROUND_CPU_BUDGET_US_PER_MS_STOPPED otherwise. On the
 * first call (budget_last_refill_us == 0), seeds the timestamp without
 * adding credit.
 *
 * Why: time-based refill decouples the budget from the filesystem_tick()
 * call rate. The one-millisecond cap prevents a long idle period from
 * accumulating a burst of credit that would allow an unbounded work spike.
 *
 * Inputs: timebase_tim2Now(), seq_isRunning(), config.h budget rates.
 * Outputs: budget_credit_us updated, budget_last_refill_us advanced.
 * Caller: filesystem_tick() at the top of the scheduler chain, once
 * per main-loop iteration.
 * Affiliates: filesystem_backgroundBudgetAvailable(),
 * filesystem_backgroundBudgetCharge().
 *
 * Also emits periodic budget trace reports (stage 'H') every ~5 seconds
 * under DEV_MODE_LOGGING. See C15.
 */
void filesystem_backgroundBudgetRefill(void)
{
    uint32_t now = timebase_tim2Now();

    /* Seed on first call — no credit awarded. */
    if (budget_last_refill_us == 0u) {
        budget_last_refill_us = now;
        budget_report_last_us = now;
        return;
    }

    uint32_t elapsed_us = timebase_tim2Delta(now, budget_last_refill_us);
    budget_last_refill_us = now;

    uint32_t elapsed_ms = elapsed_us / 1000u;
    if (elapsed_ms == 0u)
        return;

    uint32_t rate = seq_isRunning()
        ? BACKGROUND_CPU_BUDGET_US_PER_MS_PLAYING
        : BACKGROUND_CPU_BUDGET_US_PER_MS_STOPPED;

    budget_credit_us += (int32_t)(elapsed_ms * rate);

    /* Cap at one millisecond's allowance to prevent idle accumulation. */
    int32_t cap = (int32_t)rate;
    if (budget_credit_us > cap)
        budget_credit_us = cap;

    /* Periodic trace report — see C15. */
}
```

---

#### C04 — filesystem.c — ADD — filesystem_backgroundBudgetAvailable()

New function, placed adjacent to C03.

```
/*
 * Query whether background CPU budget credit is available.
 *
 * What: returns nonzero if budget_credit_us > 0, zero otherwise. Pure
 * predicate with no side effects — does not modify credit or counters.
 *
 * Why: gates background work slices. Callers check before starting a
 * work slice and skip/yield if the budget is exhausted.
 *
 * Inputs: budget_credit_us (modified by refill and charge).
 * Outputs: uint8_t, nonzero = work allowed.
 * Callers: PatternStackService.c patSvc_tick() repair gate,
 * filesystem.c phase 56 scalar classification gate,
 * filesystem.c phase 13 scalar CRC/transform gate,
 * filesystem.c filesystem_patternWriteRegionSection() pattern drain gate.
 * Affiliates: filesystem_backgroundBudgetRefill(),
 * filesystem_backgroundBudgetCharge().
 */
uint8_t filesystem_backgroundBudgetAvailable(void)
{
    return (uint8_t)(budget_credit_us > 0);
}
```

---

#### C05 — filesystem.c — ADD — filesystem_backgroundBudgetCharge()

New function, placed adjacent to C04.

```
/*
 * Charge elapsed work time against the background CPU budget.
 *
 * What: computes elapsed microseconds from start_us to now via
 * timebase_tim2Delta, subtracts from budget_credit_us. Updates per-class
 * accounting: adds to budget_charged_us[work_class], updates
 * budget_max_slice_us[work_class] if this slice exceeds the current
 * maximum.
 *
 * Why: tracks actual CPU consumption against the allowed budget. The
 * signed credit allows overshoot: if a single work slice takes longer
 * than the remaining credit, the credit goes negative and subsequent
 * available() checks return 0 until the deficit is repaid by refill.
 *
 * Inputs: start_us — TIM2 timestamp recorded before the work slice.
 * work_class — FS_BUDGET_CLASS_REPAIR / _SCALAR / _PATTERN.
 * Outputs: budget_credit_us decremented, per-class accounting updated.
 * Callers: PatternStackService.c patSvc_tick() after each repair step,
 * filesystem.c phase 56 after classification slice,
 * filesystem.c phase 13 after CRC/transform slice,
 * filesystem.c filesystem_patternWriteRegionSection() after staging slice.
 * Affiliates: filesystem_backgroundBudgetAvailable(),
 * filesystem_backgroundBudgetRefill().
 */
void filesystem_backgroundBudgetCharge(uint32_t start_us, uint8_t work_class)
{
    uint32_t elapsed = timebase_tim2Delta(timebase_tim2Now(), start_us);
    budget_credit_us -= (int32_t)elapsed;

    if (work_class < FS_BUDGET_CLASS_COUNT) {
        budget_charged_us[work_class] += elapsed;
        if (elapsed > (uint32_t)budget_max_slice_us[work_class])
            budget_max_slice_us[work_class] = (uint16_t)(
                elapsed > 0xFFFFu ? 0xFFFFu : elapsed);
    }
}
```

---

#### C06 — filesystem.c:~24860 — MODIFY — Call budget refill in filesystem_tick()

Before the first budgeted scheduler (the scalar AutoSave writer scheduler at
line 24860), add a call to `filesystem_backgroundBudgetRefill()`. The settings
writer (line 24826) and trace flush (lines 24845-24849) are not budgeted, so
the refill goes between the trace flush and the scalar writer.

Insert between lines 24849 and 24850 (after the pattern trace flush scheduler,
before the scalar AutoSave writer comment block):

```
    /*
     * Refill the elapsed-time background CPU budget before any budgeted
     * scheduler runs.
     *
     * Input: TIM2 wall clock and sequencer running state. Output: bounded
     * positive credit for the current main-loop iteration. Why: time-based
     * refill concentrates CPU bounding in one primitive; the settings writer
     * and trace flush above are not budgeted because they are small and
     * critical. Affiliates: filesystem_backgroundBudgetAvailable(),
     * filesystem_backgroundBudgetCharge().
     */
    filesystem_backgroundBudgetRefill();
```

---

#### C07 — filesystem.h:~573 — ADD — Budget API declarations

After `filesystem_fastDrainActive()` (line 572) and before the Bank
child-progress comment block (line 574).

```
/*
 * Background CPU budget for bounded AutoSave drain and Pattern repair work.
 *
 * What: filesystem_backgroundBudgetRefill() adds elapsed-time credit;
 * filesystem_backgroundBudgetAvailable() returns nonzero when credit > 0;
 * filesystem_backgroundBudgetCharge() subtracts a measured work slice.
 *
 * Why: bounds aggregate background CPU across scalar drain, pattern drain,
 * and pattern repair to a configured percentage (2.5% playing, 5% stopped).
 * The budget is a shared global resource — all three work classes share the
 * same credit pool.
 *
 * Inputs: refill reads TIM2 and seq_isRunning(); charge reads TIM2.
 * Outputs: available predicate gates work admission.
 * work_class: 0 = repair, 1 = scalar drain, 2 = pattern drain.
 * Callers: filesystem_tick() (refill), filesystem drain phases (available/
 * charge), PatternStackService.c patSvc_tick() (available/charge).
 * Affiliates: config.h BACKGROUND_CPU_BUDGET_US_PER_MS_* constants.
 */
void        filesystem_backgroundBudgetRefill(void);
uint8_t     filesystem_backgroundBudgetAvailable(void);
void        filesystem_backgroundBudgetCharge(uint32_t start_us,
                                              uint8_t work_class);
void        filesystem_backgroundBudgetDeny(uint8_t work_class);
```

---

#### C08 — PatternStackService.c:20 — ADD — Include filesystem.h and menu.h

After `#include "config.h"` (line 19), before the blank line (line 20).

```
#include "filesystem.h"
#include "menu.h"
```

`filesystem.h` provides `filesystem_backgroundBudgetAvailable()` and
`filesystem_backgroundBudgetCharge()`.
`menu.h` provides `menu_activePage`, `LOAD_PAGE`, and `SAVE_PAGE` for the
Load/Save menu gate.

---

#### C09 — PatternStackService.c:~1590 — ADD — Load/Save menu gate before repair

After the rebuild-wake block (lines 1585-1589), before the repair while-loop
(line 1591 comment / line 1592 `if`). Insert a new gate:

```
    /*
     * Suppress repair during Load/Save menu browsing.
     *
     * What: returns without entering the repair while-loop when the active
     * menu page is LOAD_PAGE or SAVE_PAGE. Queue drain and handover (above)
     * still run because they are bounded by edit rate and required for
     * lifecycle transitions. Only repair is suppressed.
     *
     * Why: the repair epoch's bounded scan consumes nonzero CPU on every
     * foreground pass. During Load/Save browsing, this CPU competes with
     * SD card operations that the user is waiting on, producing visible
     * sluggishness. All five filesystem.c AutoSave/trace schedulers already
     * carry this gate; this extends it to the repair path in
     * PatternStackService.c.
     *
     * Input: menu_activePage (extern in menu.h). Output: early return —
     * repair cursor is preserved and resumes after the user exits Load/Save.
     * Affiliate: filesystem.c scheduler gates, SCOPING_TARGETS.md § S069.
     */
    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)
        return;
```

---

#### C10 — PatternStackService.c:1592-1617 — MODIFY — Budget gate + charge in repair loop

Modify the existing repair while-loop to add:
1. A budget-exhaustion gate before entering the loop.
2. A TIM2 timestamp before each repair step.
3. A budget charge after each step that does work (patSvc_repairStep returns
   nonzero).
4. A budget-exhaustion break inside the loop after charging.

Before (lines 1592-1617):
```c
    if (tier1_scan_cursor < PATSVC_ADDRESS_COUNT) {
        uint8_t budget = patSvc_repairBudget();
        uint8_t inspected = 0u;

        while (inspected < budget &&
               tier1_scan_cursor < PATSVC_ADDRESS_COUNT) {
            ...
            if (patSvc_repairStep(...)) {
                // trace record
            }
        }
    }
```

After:
```c
    /*
     * Inspect a bounded number of address entries within the CPU budget.
     *
     * What: the existing entry-count limit (PAT_REPAIR_SCAN_IDLE/BUSY)
     * runs in parallel with the elapsed-time CPU budget. The loop breaks
     * on whichever limit fires first. After each repair step that does work,
     * the elapsed time is charged against the shared budget. If the budget
     * is exhausted at entry, the entire repair section is skipped this tick.
     *
     * Why: the entry-count limit is a fast deterministic bound that does
     * not require TIM2 reads. The time budget is the CPU-aware bound that
     * prevents aggregate background work from exceeding the configured
     * percentage. Both constraints apply simultaneously.
     *
     * Inputs: filesystem_backgroundBudgetAvailable(), patSvc_repairBudget(),
     * tier1_scan_cursor, PATSVC_ADDRESS_COUNT. Outputs: cursor advances,
     * repair relocations applied, budget charged. Affiliates:
     * filesystem_backgroundBudgetCharge() with FS_BUDGET_CLASS_REPAIR.
     */
    if (tier1_scan_cursor < PATSVC_ADDRESS_COUNT) {
        if (!filesystem_backgroundBudgetAvailable()) {
            /* Budget exhausted — skip repair this tick. Cursor preserved. */
            return;
        }

        uint8_t budget = patSvc_repairBudget();
        uint8_t inspected = 0u;

        while (inspected < budget &&
               tier1_scan_cursor < PATSVC_ADDRESS_COUNT) {
            uint32_t step_start_us = timebase_tim2Now();
            uint16_t address_index = tier1_scan_cursor++;
            uint16_t old_offset = 0u;
            uint16_t new_offset = 0u;

            inspected++;
            if (patSvc_repairStep(service_scene, address_index,
                                  &old_offset, &new_offset)) {
                if (old_offset != 0u || new_offset != 0u) {
                    patternTrace_record(
                        PAT_TRACE_STAGE_REPAIR_RELOC,
                        (uint8_t)(service_scene & 0x0Fu),
                        patSvc_relocationValue(address_index,
                                               old_offset, new_offset));
                } else {
                    patternTrace_record(PAT_TRACE_STAGE_REPAIR_RESERVE,
                                        (uint8_t)(service_scene & 0x0Fu),
                                        (uint32_t)address_index);
                }
                filesystem_backgroundBudgetCharge(step_start_us,
                                                  FS_BUDGET_CLASS_REPAIR);
                if (!filesystem_backgroundBudgetAvailable())
                    break;
            }
        }
    }
```

The charge and break-on-exhaustion apply only to steps that did actual work
(`patSvc_repairStep` returned nonzero). No-op inspection steps do not charge
because their cost is negligible (~2-3µs) and the entry-count limit already
bounds the number of consecutive no-ops.

---

### Item 4B — Scalar AutoSave drain budgeting

---

#### C11 — filesystem.c:8099-8152 — MODIFY — Phase 56 budget gate + charge

Modify the phase 56 classification loop to add:
1. A `start_us` timestamp before the loop.
2. A per-iteration `filesystem_backgroundBudgetAvailable()` check that yields
   with the scan offset retained.
3. A slice charge after the loop.

Modified phase 56:

```c
    case 56: /* CLASSIFY/CAPTURE A BOUNDED NUMBER OF MASK POSITIONS */
    {
        uint16_t examined = 0u;
        uint32_t slice_start_us = timebase_tim2Now();

        /*
         * Build one stable sorted patch list within the CPU budget.
         *
         * What: the existing per-tick mask-position bound (AUTOSAVE_MASK_BITS
         * _PER_TICK = 256) is supplemented by an elapsed-time budget check on
         * each iteration. If the shared budget is exhausted (by prior repair
         * or pattern drain work in this main-loop iteration), the classification
         * yields immediately with payload_scan_offset retained for the next pass.
         *
         * Why: during playback, a repair step earlier in the same loop iteration
         * may have consumed the budget. The per-iteration check ensures the
         * classification loop does not add to an already-exceeded budget.
         *
         * Must NOT gate: the phase transition at scan completion (to phase 10),
         * file sync/close/flush, error recovery, or any committed operation.
         *
         * Inputs: filesystem_backgroundBudgetAvailable() predicate,
         * payload_scan_offset (retained across ticks), AUTOSAVE_MASK_BITS_PER_TICK.
         * Outputs: patch list extended, budget charged after the slice.
         * Affiliate: filesystem_backgroundBudgetCharge() with FS_BUDGET_CLASS_SCALAR.
         */
        while (op_autosave_writer.payload_scan_offset <
                   AUTOSAVE_PAYLOAD_BYTES &&
               examined < AUTOSAVE_MASK_BITS_PER_TICK) {

            if (!filesystem_backgroundBudgetAvailable()) {
                filesystem_backgroundBudgetCharge(slice_start_us,
                                                  FS_BUDGET_CLASS_SCALAR);
                return;  /* yield with scan offset retained */
            }

            /* ... existing iteration body unchanged ... */
        }

        filesystem_backgroundBudgetCharge(slice_start_us,
                                          FS_BUDGET_CLASS_SCALAR);

        if (op_autosave_writer.payload_scan_offset >=
            AUTOSAVE_PAYLOAD_BYTES) {
            filesystem_autosaveTraceCaptured(0u);
            op_phase = 10u;
        }
        return;
    }
```

The existing iteration body (lines 8117-8144) is unchanged. The budget check
is added before the existing loop body. The charge happens both on budget-
exhaustion yield (before the early return) and on normal loop completion (after
the while). The phase completion check (`payload_scan_offset >= PAYLOAD_BYTES`
→ phase 10) is NOT gated — it must always transition.

---

#### C12 — filesystem.c:8240-8325 — MODIFY — Phase 13 budget gate + charge

Modify phase 13 (TRANSFORM, CRC, AND COPY ONE UNPUBLISHED TARGET CHUNK) to
add a budget gate at the entry to the staging/CRC work. The partial-write
completion path (lines 8244-8261) must NOT be gated — it resumes an in-flight
afatfs_fwrite that is already committed. The CRC finalization path
(lines 8262-8273) must NOT be gated. Only the new-chunk staging path
(read + transform + CRC + write, lines 8275-8325) is gated.

```c
    case 13: /* TRANSFORM, CRC, AND COPY ONE UNPUBLISHED TARGET CHUNK */
    {
        uint32_t n;

        /* Resume any in-progress fwrite — NOT budget-gated. */
        if (op_autosave_writer.chunk_written <
            op_autosave_writer.chunk_bytes) {
            /* ... existing partial-write resume, unchanged ... */
            return;
        }

        /* CRC finalization — NOT budget-gated. */
        if (op_autosave_writer.stream_offset >= AUTOSAVE_RECORD_BYTES) {
            /* ... existing CRC finalization, unchanged ... */
            return;
        }

        /*
         * Budget gate for new-chunk staging/CRC work.
         *
         * What: checks the shared CPU budget before staging the next CRC
         * chunk (read from source + transform + CRC feed + prepare fwrite).
         * If exhausted, returns without advancing stream_offset; the existing
         * partial-progress mechanism re-enters phase 13 on the next tick.
         *
         * Why: the fread + transform + CRC work is the CPU-intensive portion.
         * The partial-write resume above and CRC finalization must not be gated
         * because they complete already-committed operations.
         *
         * Must NOT gate: partial fwrite resume (committed data in staging_buf),
         * CRC finalization (single accumulator finish), phase transition,
         * error paths.
         *
         * Input: filesystem_backgroundBudgetAvailable(). Output: deferred or
         * proceeded. Affiliate: filesystem_backgroundBudgetCharge() with
         * FS_BUDGET_CLASS_SCALAR.
         */
        if (!filesystem_backgroundBudgetAvailable()) {
            budget_denied_count[FS_BUDGET_CLASS_SCALAR]++;
            return;
        }

        {
            uint32_t chunk_start_us = timebase_tim2Now();

            /* ... existing read + transform + CRC + stage logic ... */

            filesystem_backgroundBudgetCharge(chunk_start_us,
                                              FS_BUDGET_CLASS_SCALAR);
        }
        return;
    }
```

The existing fread/transform/CRC/stage body (lines 8284-8325) is wrapped
in a block with a TIM2 timestamp before and a charge after. Only the
CPU-intensive path is gated and charged. The partial-write resume and CRC
finalization paths remain unguarded.

---

### Item 4C — Pattern AutoSave drain budgeting

---

#### C13 — filesystem.c:14528-14580 — MODIFY — Pattern write section budget gate + charge

Modify `filesystem_patternWriteRegionSection()` to add a budget gate before
the memcpy/CRC staging work. The function already processes exactly one
512-byte chunk per call and returns 0 (not done) for re-entry on the next
tick, providing a natural yield point.

The gate goes before the `memcpy` at line 14562, inside the `if (op_bytes_done
== 0u)` block (line 14558). This ensures:
- The afatfs_fwrite resume path (lines 14569-14575) is NOT gated — it
  completes an in-progress write that is already committed.
- The memcpy + CRC staging (lines 14559-14567) IS gated — this is the
  CPU-intensive work.
- The section-complete check (line 14556-14557) is NOT gated.

```c
static uint8_t filesystem_patternWriteRegionSection(
    uint8_t phase, const pat_scene_region_t *region)
{
    /* ... existing variable declarations and section selection ... */

    if (op_stream_index >= section_bytes)
        return 1u;

    if (op_bytes_done == 0u) {
        /*
         * Budget gate for pattern staging/CRC work.
         *
         * What: checks the shared CPU budget before staging the next 512-byte
         * chunk. If exhausted, returns 0 (not done) without advancing
         * op_stream_index; the caller (phases 4/5/6) returns from the tick
         * function and re-enters on the next filesystem_tick() call.
         *
         * Why: the memcpy + CRC feed is the CPU-intensive portion of each
         * chunk. The afatfs_fwrite below resumes an already-staged chunk and
         * must not be gated.
         *
         * Must NOT gate: afatfs_fwrite resume (committed staging_buf data),
         * section completion return, error/full-card return.
         * Must NOT gate: pat_snapshotScene() (happens in the scheduler before
         * any phase runs — already atomic), transaction boundaries, sync/close.
         *
         * Input: filesystem_backgroundBudgetAvailable(). Output: 0u (deferred)
         * or staged chunk. Affiliate: filesystem_backgroundBudgetCharge() with
         * FS_BUDGET_CLASS_PATTERN.
         */
        if (!filesystem_backgroundBudgetAvailable()) {
            budget_denied_count[FS_BUDGET_CLASS_PATTERN]++;
            return 0u;
        }

        uint32_t staging_start_us = timebase_tim2Now();

        remaining = section_bytes - op_stream_index;
        chunk = (remaining > sizeof(staging_buf))
            ? (uint16_t)sizeof(staging_buf) : (uint16_t)remaining;
        memcpy(staging_buf, source + op_stream_index, chunk);
        op_bytes_done = chunk;
        op_item_offset = 0u;
        op_pattern_crc = filesystem_patternCrcFeed(
            op_pattern_crc, section_start + op_stream_index,
            staging_buf, chunk);

        filesystem_backgroundBudgetCharge(staging_start_us,
                                          FS_BUDGET_CLASS_PATTERN);
    }

    /* ... existing afatfs_fwrite resume and progress logic, unchanged ... */
}
```

The afatfs_fwrite path (lines 14569-14579) is completely unchanged.

---

### Trace requirements

---

#### C14 — AutosaveTrace.h:188 — ADD — Budget summary stage

After `AUTOSAVE_TRACE_STAGE_DIRTY_COUNT_MISMATCH = 'Z'` (line 188), before
the closing brace of the enum (line 189).

```
    /*
     * H: periodic budget usage summary per work class.
     *
     * What: emitted every ~5 seconds by filesystem_backgroundBudgetRefill()
     * under DEV_MODE_LOGGING. One record per work class per emission period.
     * flags carry the work class (bits 0-1: 0=repair, 1=scalar, 2=pattern)
     * and charged_ms (bits 2-7, capped at 63, in milliseconds). value32
     * carries denied_count in bits 0-15 and max_slice_us in bits 16-31.
     *
     * Why: proves the budget system is working — total charged CPU per class,
     * number of denied slices, and maximum single-slice duration are the three
     * metrics needed to verify the configured allowance is binding.
     *
     * Inputs: per-class accumulators reset after each emission. Outputs:
     * decoded by tools/decode_devlogs.py. Affiliate: filesystem.c budget
     * accounting state.
     */
    AUTOSAVE_TRACE_STAGE_BUDGET_REPORT = 'H',
```

---

#### C15 — filesystem.c — ADD — Periodic budget trace emission

Inside `filesystem_backgroundBudgetRefill()` (see C03), after the credit
cap logic, add a periodic trace emission gated by `DEV_MODE_LOGGING`. Emit
one record per work class every ~5 seconds, then reset the per-class
accumulators.

```
#if DEV_MODE_LOGGING
    /*
     * Periodic budget trace report.
     *
     * What: every ~5 seconds, emits one AUTOSAVE_TRACE_STAGE_BUDGET_REPORT
     * ('H') record per work class. flags encode work_class (bits 0-1) and
     * charged_ms (bits 2-7, capped at 63). value32 encodes denied_count
     * (bits 0-15) and max_slice_us (bits 16-31). Counters reset after each
     * emission.
     *
     * Why: the three per-class metrics (total charged, deny count, max slice)
     * prove the budget system is binding and identify any work class that
     * exceeds its expected share or has unexpectedly large slices.
     *
     * Inputs: budget_charged_us[], budget_denied_count[],
     * budget_max_slice_us[], budget_report_last_us.
     * Outputs: autosaveTrace_record() with stage 'H'.
     * Affiliate: tools/decode_devlogs.py 'H' decoder.
     */
    if (timebase_tim2Delta(now, budget_report_last_us) >= 5000000u) {
        uint8_t cls;
        for (cls = 0u; cls < FS_BUDGET_CLASS_COUNT; cls++) {
            uint8_t charged_ms = (uint8_t)(budget_charged_us[cls] / 1000u);
            if (charged_ms > 63u)
                charged_ms = 63u;
            uint8_t flags = (uint8_t)(cls | (uint8_t)(charged_ms << 2u));
            uint32_t value = (uint32_t)budget_denied_count[cls] |
                             ((uint32_t)budget_max_slice_us[cls] << 16u);
            autosaveTrace_record(AUTOSAVE_TRACE_STAGE_BUDGET_REPORT,
                                 flags, value);
            budget_charged_us[cls] = 0u;
            budget_denied_count[cls] = 0u;
            budget_max_slice_us[cls] = 0u;
        }
        budget_report_last_us = now;
    }
#endif
```

---

#### C16 — tools/decode_devlogs.py — ADD — 'H' stage decoder

Three additions:

1. In `STAGE_ENUM` dict (lines 94-118): add `"H"` entry.
2. In `STAGE_PRODUCER` dict (lines 120-153): add `"H"` entry.
3. In `trace_record_text()` (line 430+): add `elif ch == "H":` branch.

```python
# In STAGE_ENUM:
"H": "AUTOSAVE_TRACE_STAGE_BUDGET_REPORT",

# In STAGE_PRODUCER:
"H": "filesystem_backgroundBudgetRefill()",

# In trace_record_text(), after the 'Z' branch:
    elif ch == "H":
        work_class = flags & 0x03
        charged_ms = (flags >> 2) & 0x3F
        denied = value & 0xFFFF
        max_slice = (value >> 16) & 0xFFFF
        class_names = {0: "repair", 1: "scalar", 2: "pattern"}
        class_name = class_names.get(work_class, f"unknown({work_class})")
        detail = (f"{enum_name} via {producer}: class={class_name}, "
                  f"charged_ms={charged_ms}, denied_count={denied}, "
                  f"max_slice_us={max_slice}")
```

---

### Auxiliary changes

---

#### A01 — SCOPING_TARGETS.md — UPDATE — Mark repair Load/Save gate as addressed

Update the Session 069 deferred item (lines 1505-1523) about `patSvc_tick()`
repair epoch running during Load/Save. Add a note that this is addressed by
Pass 2 item 4A (C09: separate menu gate in `patSvc_tick()` before the repair
section).

---

#### A02 — PatternStackService.h — UPDATE — patSvc_tick() comment

Update the `patSvc_tick()` declaration comment to document both the CPU budget
gate and the Load/Save menu gate added in Pass 2.

```
/*
 * Run the Pattern stack service at the bounded 500 Hz foreground cadence.
 *
 * What: drains queued mutations, manages Scene handover, and runs the
 * bounded repair epoch within the shared background CPU budget.
 *
 * The repair section (address entry scan and reservation maintenance) is
 * suppressed when the Load/Save menu is active (menu_activePage ==
 * LOAD_PAGE || SAVE_PAGE) and gated by
 * filesystem_backgroundBudgetAvailable(). Both gates preserve the repair
 * cursor for continuation after the gate clears. The existing per-tick
 * entry-count limit (PAT_REPAIR_SCAN_IDLE/BUSY) runs in parallel with the
 * elapsed-time budget; the loop breaks on whichever fires first.
 *
 * Queue drain and Scene handover are not budget-gated because they are
 * bounded by edit rate and required for lifecycle transitions.
 *
 * Inputs: seq_activePattern, Autosave dirty predicates, menu_activePage,
 * filesystem_backgroundBudgetAvailable(). Outputs: mutation drain, Scene
 * handover, bounded repair progress. Caller: timebase_serviceFrontPanel()
 * at 500 Hz. Affiliates: config.h PAT_REPAIR_SCAN_*, filesystem.h budget API.
 */
```

---

#### A03 — filesystem.h — UPDATE — filesystem_tick() comment

Update the `filesystem_tick()` comment (lines 536-546) to note that the budget
refill is called from within it and that all budgeted schedulers respect the
shared budget.

Add after the existing quiet-window/max-latency sentence:

```
 * Background CPU budget (refilled at the top of the scheduler chain) bounds
 * aggregate work across scalar drain, pattern drain, and pattern repair to
 * 2.5% during playback and 5% while stopped.
```

---

## Summary

| Change | File | Action | Item |
|--------|------|--------|------|
| C01 | config.h:484 | ADD | 4A |
| C02 | filesystem.c:~1843 | ADD | 4A |
| C03 | filesystem.c (new fn) | ADD | 4A |
| C04 | filesystem.c (new fn) | ADD | 4A |
| C05 | filesystem.c (new fn) | ADD | 4A |
| C06 | filesystem.c:~24849 | MODIFY | 4A |
| C07 | filesystem.h:~573 | ADD | 4A |
| C08 | PatternStackService.c:20 | ADD | 4A |
| C09 | PatternStackService.c:~1590 | ADD | 4A |
| C10 | PatternStackService.c:1592-1617 | MODIFY | 4A |
| C11 | filesystem.c:8099-8152 | MODIFY | 4B |
| C12 | filesystem.c:8240-8325 | MODIFY | 4B |
| C13 | filesystem.c:14558-14567 | MODIFY | 4C |
| C14 | AutosaveTrace.h:188 | ADD | Trace |
| C15 | filesystem.c (in C03 fn) | ADD | Trace |
| C16 | tools/decode_devlogs.py | ADD | Trace |
| A01 | SCOPING_TARGETS.md | UPDATE | 4A |
| A02 | PatternStackService.h | UPDATE | 4A |
| A03 | filesystem.h | UPDATE | 4A |

16 primary changes + 3 auxiliary = 19 total, plus the diagnostic deny-counter
helper added to the C07 API so PatternStackService.c can report rejected repair
slices without reaching into filesystem.c private state.

### RAM / ROM budget

| Component | RAM | ROM |
|-----------|-----|-----|
| `budget_last_refill_us` | 4 B | — |
| `budget_credit_us` | 4 B | — |
| `budget_charged_us[3]` | 12 B | — |
| `budget_denied_count[3]` | 6 B | — |
| `budget_max_slice_us[3]` | 6 B | — |
| `budget_report_last_us` | 4 B | — |
| **Total** | **36 B** | **code delta** |

### Files touched

| File | Changes |
|------|---------|
| config.h | C01 |
| Core/Hardware/SD/filesystem.c | C02, C03, C04, C05, C06, C11, C12, C13, C15 |
| Core/Hardware/SD/filesystem.h | C07, A03 |
| Core/Bank/Scene/Pattern/PatternStackService.c | C08, C09, C10 |
| Core/Bank/Scene/Pattern/PatternStackService.h | A02 |
| Core/Bank/Scene/AutosaveTrace.h | C14 |
| tools/decode_devlogs.py | C16 |
| SCOPING_TARGETS.md | A01 |

---

## Work log

### 2026-09-22 — pre-implementation RAM gate

Read `MEMORY.md`, the parent S069 goal, `S069_ATS_PROBLEMS.md`, and this
Pass-2 schedule. The proposed budget primitive requires a new persistent
normal-SRAM1 `.bss` allocation owned by `Core/Hardware/SD/filesystem.c` for
the firmware lifetime:

- 8 B always-on budget state: `budget_last_refill_us` and
  `budget_credit_us`.
- 28 B diagnostic accounting state: three 32-bit charged totals, three
  16-bit deny counters, three 16-bit maximum-slice values, and one 32-bit
  report timestamp. This is retained in the current `DEV_MODE_LOGGING=1`
  build and should be compiled out when logging is disabled.
- Exact requested allocation in the current development build: **36 B in
  normal SRAM1 `.bss`, firmware-lifetime, filesystem scheduler owner**.

The linker places ordinary static zero-initialized globals in SRAM1 `.bss`
(`0x20020000` region; current `.bss` begins at `0x20020dc0`). The project RAM
policy in `MEMORY.md` and `SRAM_MANIFEST.md` requires user acknowledgement
of this exact byte count, region, lifetime, and owner before implementing a
new allocation. The user explicitly approved the 36-byte allocation on
2026-09-22, so implementation proceeded.

### 2026-09-22 — implementation pass started

Implemented the budget configuration/API surface, shared filesystem-owned
credit/refill/charge state, DEV-only per-class accounting and `H` trace report,
Pattern repair Load/Save and budget gates, scalar phases 56/13 gates, Pattern
staging gate, and the decoder/scoping documentation updates. Added the small
`filesystem_backgroundBudgetDeny()` API because PatternStackService.c cannot
otherwise update filesystem.c's private per-class deny counter. This helper
adds no storage; logging-off builds compile its accounting to a no-op.

### 2026-09-22 — clean-link verification

`make clean && make -j2` passed. The linked symbol
`budget_state.lto_priv.0` is exactly 36 bytes in normal SRAM1 `.bss`, matching
the approved allocation: 8 bytes always-on credit state plus 28 bytes of
DEV-only accounting in the current `DEV_MODE_LOGGING=1` build. The resulting
image reports `text=450,140`, `data=416`, `bss=291,756`. `git diff --check`
passed and `tools/decode_devlogs.py` passed `py_compile` with its cache placed
outside the repository.

The final incremental rebuild and `make img` also passed after the transport-
rate cap correction. The generated `build/LXRV2_lxr02.img` is 450,572 bytes;
the H-stage decoder smoke test decoded class, charged milliseconds, deny
count, and maximum-slice fields correctly. Hardware execution and the Session
069 CPU snapshot remain pending.
