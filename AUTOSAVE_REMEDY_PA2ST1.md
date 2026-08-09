# Autosave Remedy — Part 2, Step 1: Diagnostic Observability

## Scope

This document is the detailed implementation plan for **Part 2, Step 1** of
`AUTOSAVE_PHASE2_PLAN.md` ("Build observability before building features"). It
is grounded in the accepted Session 045 production baseline at commit
`326a8a1`:

- `Core/Bank/Scene/Autosave.c` / `.h`
- `Makefile`

At this planning checkpoint, `Autosave.c`, `Autosave.h`, and the Makefile
source entry remain byte-identical to that accepted baseline. `filesystem.c`,
`filesystem.h`, and `config.h` have subsequently received independent boot
and HCNAMES work, so every listed insertion point in those files must be
re-read immediately before implementation; line numbers and copied current
snippets are aids, not authority.

`AUTOSAVE_PARAM_HOOK.md` was a completed historical Phase 1 implementation
record and was intentionally compacted into the Session 045 handoff and
`SESSION_045_CONSOLIDATED_POST_MORTEM.md`. It is not a missing implementation
prerequisite. The authoritative Phase 1 architecture and accepted limits are
the current Autosave source plus those consolidated records.

It does **not** implement Phase 2 whole-object hooks or Load/Save exclusion
(Steps 5–6 of the parent plan). It builds only the tracing infrastructure
needed to make every later step diagnosable, and re-validates it against the
already-working Phase 1 baseline before anything else is allowed to depend on
it. Ordinary single-parameter hooks have hardware evidence for the testable
Scene, Kit, and Instrument types; this trace checkpoint does not claim the
remaining complete Phase 1 matrix is closed.

The six/seven stages this step must make observable were named explicitly in
`045_SESSION_HANDOFF_LOG.md`'s BLOCKERS section: *"No runtime trace currently
distinguishes a dirty producer, scheduler observation, operation admission,
winner validation, mask merge, parameter capture, or final commit."* Those
seven phrases map directly to the seven trace stages defined below.

---

## Step 0 (mandatory precondition): RAM allocation sign-off

`MEMORY.md`'s RAM Allocation Approval Policy applies here without exception:
*"Before implementing any new or enlarged RAM allocation in this project,
explicitly identify its exact byte count, memory region, lifetime, and owner,
then obtain the user's acknowledgement... This applies to globals, static
storage... in this and future sessions."* This step adds new static storage,
so it cannot be implemented until that acknowledgement is on record. The
request to make is:

| Field | Value |
| --- | --- |
| Byte count | 520 bytes (512-byte ring + 6 bytes of trace cursor/counter state + 2-byte trace-flush cadence) |
| Memory region | Normal SRAM1 (not DTCM — DTCM headroom is reserved for delay lines; this data is not audio-timing-critical) |
| Lifetime | Static/BSS for the life of a diagnostic build; the module compiles to zero bytes when `DEV_MODE_LOGGING` is 0 |
| Owner | `AutosaveTrace.c` owns 518 bytes; `filesystem.c` owns the 2-byte private cadence scratch |

This is a diagnostic-only allocation, gated solely by the established
`DEV_MODE_LOGGING` selection (see config.h changes below), and is not drawn
from the delay-line/Pattern-reserved headroom the policy protects — it is a
new, separate, small, explicitly disable-able allocation. State this plainly
when requesting sign-off; do not assume it is exempt from the policy because
it is diagnostic.

The user has approved this diagnostic allocation subject to the
`DEV_MODE_LOGGING == 1` gate. The corrected 520-byte total includes every
trace-specific static, including the scheduler cadence previously omitted from
the 518-byte subtotal. Do not allocate any part of this state or perform
trace/file logging in a build where `DEV_MODE_LOGGING == 0`.

---

## Design summary

### Ownership boundary (mirrors an existing, proven pattern in this codebase)

`Autosave.h`'s own module comment already states the boundary this design
copies: *"This module owns the binary wire contract... It does not own a
filesystem handle, scheduler, or transaction patch cache; filesystem.c
remains the sole AsyncFATFS owner."* The new trace module follows the same
split:

- **`AutosaveTrace.c`** owns the ring-buffer SRAM, the record encoding, and
  cursor bookkeeping. It performs no I/O and knows nothing about AsyncFATFS.
- **`filesystem.c`** owns writing the ring's contents to disk, using the
  identical bounded, non-blocking, single-owner-operation discipline already
  proven by `filesystem_writeBootLog_tick()` (open → write → close → sync,
  entered only from an idle facade, retried on back-pressure, abandoned only
  on genuine full-card error).
- **`Autosave.c`** gets exactly one new call site, at the one place every
  current and future dirty-bit producer already funnels through.

### Why one call site in Autosave.c is enough

Every typed marker in `Autosave.c` — `autosave_markBankFieldDirty()`,
`autosave_markSceneParameterDirty()`, `autosave_markKitParameterDirty()`,
the Instrument Normal/Morph markers, and every whole-region marker
(`autosave_markWholeInstrumentDirty()`, `autosave_markKitDirty()`,
`autosave_markSceneWithoutPatternDirty()`, `autosave_markResidentBankDirty()`,
etc.) — was confirmed by inspection to bottom out in exactly one static
helper:

```c
static void autosave_markPayloadOffsetDirty(uint16_t payload_offset)
{
    if (!autosave_mutation_tracking_enabled ||
        payload_offset >= AUTOSAVE_PAYLOAD_BYTES) {
        return;
    }
    autosave_maskByteOr(
        (uint16_t)(payload_offset >> 3u),
        (uint8_t)(1u << (payload_offset & 7u)));
}
```

This is the single funnel for the "dirty producer" trace stage. It is also
the reason this step needs no changes to `Autosave.h` at all: the trace call
is internal to the `.c` file, invisible to every caller in `BankData.c`,
`SceneData.c`, and `presetManager.c`.

One caveat to record explicitly, not engineer around: whole-region markers
call this helper in a loop (up to ~1,920 times for one Scene, more for a full
`autosave_markResidentBankDirty()` at boot/AutoSave-re-enable). Recovery mask
merge (`autosave_maskMergeChunk()`, used when OR-ing a winner's file-carried
mask into SRAM) bypasses this helper entirely and is **not** traced by the
dirty-producer stage — that merge already has its own dedicated stage
(`MASK_MERGED`, below). The dirty-producer stage is therefore most meaningful
for isolated Phase 1 scalar edits, which is exactly the Step 2 test matrix's
use case; bulk whole-region marks will legitimately overrun the ring and are
expected to. Do not add per-region flood mitigation in this step — it is out
of scope and adds RAM/complexity this step does not need.

### The seven stages and exactly where each one fires

Located by tracing `filesystem_autosaveWriterSchedule_tick()` and
`filesystem_autosaveParameterDrain_tick()` (the runtime drain state machine)
phase-by-phase in the current source:

| # | Stage | Fires in | At | Why this point |
| - | --- | --- | --- | --- |
| 1 | `DIRTY` | `Autosave.c` | inside `autosave_markPayloadOffsetDirty()`, after the enabled/range guard, i.e. only for bits that actually get set | The single funnel for every producer, current and future |
| 2 | `SCHEDULED` | `filesystem.c` | `filesystem_autosaveWriterSchedule_tick()`, the edge where `fs_autosave_writer_armed` transitions 0→1 | This is the scheduler *noticing* dirty work exists, distinct from actually starting a transaction |
| 3 | `ADMITTED` | `filesystem.c` | same function, immediately after `filesystem_start(FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN, ...)` returns true and `fs_autosave_transaction_active = 1u;` is set | This is the exact boundary the 045 postmortem could not observe — "the common writer admission/lifecycle path was no longer reaching the drain transaction" |
| 4 | `VALIDATED` | `filesystem.c` | `filesystem_autosaveParameterDrain_tick()` case `5`, at the `op_phase = op_autosave_writer.have_winner ? 50u : 30u;` transition | Captures whether a winner was found and which record/generation won, before any recovery or copy work begins |
| 5 | `MASK_MERGED` | `filesystem.c` | case `55`, right after the winner's mask is fully OR-merged and closed, before branching on `autosave_maskHasDirty()` | Distinguishes "merged and clean, completing read-only" from "merged and still dirty, continuing to classify" |
| 6 | `CAPTURED` | `filesystem.c` | case `56`, at both `op_phase = 10u;` transitions (full-scan-complete and budget-exhausted-continuing) | One call site, two outcomes; records how many bytes were actually captured this generation |
| 7 | `PUBLISHED` | `filesystem.c` | case `22`, immediately before `filesystem_finish(FS_STATUS_DONE);` | The commit byte is already durable and closed at this point; this is the "final commit" the postmortem asked for |
| 8 | `TERMINAL` | `filesystem.c` | `filesystem_autosaveWriterCompleted()`, at entry, before any state is reset | Closes the loop: records DONE/ERROR and whether a continuation was armed, which is exactly what root cause (f) (the Load/Save-exclusion regression) needed and did not have |

Stage 8 (`TERMINAL`) was not in the original six-stage list but is included
because it is the one boundary explicitly named in the parent plan's Step 6
preamble ("this was the terminal regression... reattempted last... once
Steps 1–5 give you the tracing... needed to see what breaks") — building it
now costs one more call site and means Step 6 does not need to reopen this
module later.

---

## New files

### `Core/Bank/Scene/AutosaveTrace.h`

```c
/*
 * AutosaveTrace.h -- bounded SRAM lifecycle trace for the autosave writer.
 *
 * This module owns a fixed-size ring of fixed-width diagnostic records and
 * the cursor bookkeeping that lets a separate, bounded filesystem.c writer
 * drain them to disk. It owns no filesystem handle, performs no I/O, and
 * knows nothing about AsyncFATFS -- the same split Autosave.h documents
 * between itself and filesystem.c. It exists only to answer the question the
 * Session 045 postmortem could not: which lifecycle stage did one specific
 * autosave transaction actually reach.
 *
 * Every function in this header is safe to call unconditionally, from any
 * build. When DEV_MODE_LOGGING is 0, every function compiles to a
 * trivial no-op/zero-return stub, matching the existing DEV_MODE_LOGGING/
 * DEV_MODE_DIAGNOSTIC convention of keeping call sites unconditional while
 * gating behavior inside the implementation.
 */
#ifndef AUTOSAVE_TRACE_H_
#define AUTOSAVE_TRACE_H_

#include <stdint.h>

/*
 * Fixed 8-byte record layout, documented as explicit offsets rather than a
 * packed struct -- the same convention Autosave.h uses for the wire format,
 * chosen so no compiler padding question can ever affect the on-disk size.
 *
 * Offset 0: stage code, one ASCII-ish byte from autosave_trace_stage_t.
 * Offset 1: stage-specific outcome/flags byte.
 * Offset 2..3: low 16 bits of time_sysTick at record time, little-endian.
 *              This is for ordering and gap-timing between stages within one
 *              transaction, not wall-clock time; it wraps roughly every 65 s.
 * Offset 4..7: stage-specific uint32 payload, little-endian.
 */
#define AUTOSAVE_TRACE_STAGE_OFFSET  0u
#define AUTOSAVE_TRACE_FLAGS_OFFSET  1u
#define AUTOSAVE_TRACE_TICK_OFFSET   2u
#define AUTOSAVE_TRACE_VALUE_OFFSET  4u
#define AUTOSAVE_TRACE_RECORD_BYTES  8u

/*
 * Ring capacity. Chosen so the entire ring serializes into filesystem.c's
 * existing 512-byte staging_buf in one pass: 64 * 8 = 512 exactly. This is a
 * deliberate sizing choice, not a coincidence to preserve -- if
 * AUTOSAVE_TRACE_RECORD_BYTES ever changes, re-derive this from staging_buf's
 * size rather than picking a new constant independently.
 */
#define AUTOSAVE_TRACE_RECORD_COUNT  64u

_Static_assert(
    (AUTOSAVE_TRACE_RECORD_COUNT & (AUTOSAVE_TRACE_RECORD_COUNT - 1u)) == 0u,
    "ring capacity must be a power of two for mask-based index wrap");
_Static_assert(
    AUTOSAVE_TRACE_RECORD_COUNT * AUTOSAVE_TRACE_RECORD_BYTES == 512u,
    "ring must serialize into exactly one filesystem.c staging_buf pass");

/*
 * Stage identifiers.
 *
 * Values are printable ASCII on purpose: the flushed file is meant to be
 * readable in a hex editor without a separate decoder table, the same
 * readability goal that chose bootlog.bin's eight-byte text codes.
 */
typedef enum {
    AUTOSAVE_TRACE_STAGE_DIRTY       = 'D', /* one payload bit was set */
    AUTOSAVE_TRACE_STAGE_SCHEDULED   = 'S', /* scheduler observed dirty work, armed debounce */
    AUTOSAVE_TRACE_STAGE_ADMITTED    = 'A', /* filesystem_start() accepted the drain op */
    AUTOSAVE_TRACE_STAGE_VALIDATED   = 'V', /* A/B validation finished (winner or none) */
    AUTOSAVE_TRACE_STAGE_MASK_MERGED = 'M', /* winner's file mask OR-merged into SRAM */
    AUTOSAVE_TRACE_STAGE_CAPTURED    = 'C', /* bounded classify/capture pass finished */
    AUTOSAVE_TRACE_STAGE_PUBLISHED   = 'P', /* commit byte durable; new generation valid */
    AUTOSAVE_TRACE_STAGE_TERMINAL    = 'T', /* writer completion callback ran */
} autosave_trace_stage_t;

/*
 * Root filename for the flushed trace. 8.3-safe on purpose -- the two
 * existing autosave hidden files already exposed a case-folded short-alias
 * collision bug (Session 045, ASYNCFATFS_REFERENCE.md); an 8.3-safe name
 * sidesteps that whole class of problem instead of re-testing around it.
 */
#define AUTOSAVE_TRACE_FILENAME "asavetrc.bin"

/*
 * Append one trace record.
 *
 * Inputs: a stage code, a stage-specific flags byte, and a stage-specific
 * uint32 value (see the stage table in AUTOSAVE_REMEDY_PA2ST1.md for the
 * exact per-stage meaning of flags/value). Output: the record is written into
 * the next ring slot and the write cursor advances; if the ring was already
 * full of unflushed records, the oldest unflushed record is overwritten and
 * the dropped-record counter increments, and the flush cursor is forced
 * forward so pendingCount() stays consistent. Why: this must never block --
 * it is reachable from timer/foreground mutation-marking paths, the same
 * contexts Autosave.c's own atomic mask helpers must tolerate. Affiliates:
 * Autosave.c's dirty markers and every filesystem.c call site listed in
 * AUTOSAVE_REMEDY_PA2ST1.md.
 */
void autosaveTrace_record(autosave_trace_stage_t stage,
                          uint8_t flags,
                          uint32_t value32);

/*
 * Report how many records are waiting to be flushed to disk.
 *
 * Output is the free-running write cursor minus the free-running flush
 * cursor, taken as unsigned 16-bit subtraction; it saturates at
 * AUTOSAVE_TRACE_RECORD_COUNT after an overwrite. Affiliate:
 * filesystem_autosaveTraceFlushSchedule_tick().
 */
uint16_t autosaveTrace_pendingCount(void);

/*
 * Copy one not-yet-flushed record without advancing the flush cursor.
 *
 * Input: index is 0-based, relative to the current flush cursor, and must be
 * less than a pendingCount() snapshot taken by the same caller before the
 * peek loop began -- the caller must not exceed the count it captured, since
 * a concurrent producer can keep advancing the write cursor. Output: the
 * eight-byte record is copied into out and 1 is returned; index out of range
 * against the caller-supplied bound is the caller's own bug, not detected
 * here, so callers must respect the snapshot they took. Affiliate:
 * filesystem_autosaveTraceFlush_tick().
 */
uint8_t autosaveTrace_peekRecord(uint16_t index,
                                 uint8_t out[AUTOSAVE_TRACE_RECORD_BYTES]);

/*
 * Acknowledge that count records starting at the current flush cursor have
 * been made durable on disk.
 *
 * Input: count must not exceed the pendingCount() snapshot the caller used
 * for its peek loop. Output: the flush cursor advances by count. Why: only
 * filesystem.c knows when a write is actually durable (post-sync), so the
 * cursor must not advance until that caller says so. Affiliate:
 * filesystem_autosaveTraceFlush_tick(), called only after afatfs_sync().
 */
void autosaveTrace_advanceFlushCursor(uint16_t count);

/*
 * Report how many records have been overwritten before they were flushed.
 *
 * Output is a monotonically increasing count, not cleared by reading it. Why:
 * a nonzero value tells a bench tester the flushed file has a gap and the
 * trace cannot be treated as complete for that run -- exactly the kind of
 * self-aware instrumentation the parent plan's Step 1 checkpoint requires
 * ("if the trace doesn't match known-good behavior, the instrumentation is
 * wrong -- fix it before proceeding").
 */
uint16_t autosaveTrace_droppedCount(void);

#endif /* AUTOSAVE_TRACE_H_ */
```

### `Core/Bank/Scene/AutosaveTrace.c`

```c
/*
 * AutosaveTrace.c -- bounded SRAM lifecycle trace, no I/O.
 *
 * See AutosaveTrace.h for the ownership boundary this module keeps against
 * filesystem.c. Every public function is unconditionally callable; behavior
 * is gated internally on DEV_MODE_LOGGING so call sites in Autosave.c
 * and filesystem.c never need their own #if.
 */
#include "AutosaveTrace.h"
#include "config.h"
#include "timebase.h"

#if DEV_MODE_LOGGING

#define AUTOSAVE_TRACE_INDEX_MASK (AUTOSAVE_TRACE_RECORD_COUNT - 1u)

static volatile uint8_t
    autosave_trace_ring[AUTOSAVE_TRACE_RECORD_COUNT][AUTOSAVE_TRACE_RECORD_BYTES];
static volatile uint16_t autosave_trace_write_cursor;
static volatile uint16_t autosave_trace_flush_cursor;
static volatile uint16_t autosave_trace_dropped_count;

/*
 * Identical critical-section pattern to Autosave.c's autosave_irqSave()/
 * autosave_irqRestore(). Duplicated rather than shared because Autosave.c
 * keeps those helpers static/private by design -- this module must not widen
 * that module's public surface just to borrow two instructions. Affiliates:
 * autosaveTrace_record(), the only function that needs it.
 */
static uint32_t autosaveTrace_irqSave(void)
{
    uint32_t primask;

    __asm volatile("mrs %0, primask\ncpsid i"
                   : "=r"(primask) :: "memory");
    return primask;
}

static void autosaveTrace_irqRestore(uint32_t primask)
{
    __asm volatile("msr primask, %0" :: "r"(primask) : "memory");
}

void autosaveTrace_record(autosave_trace_stage_t stage,
                          uint8_t flags,
                          uint32_t value32)
{
    uint32_t primask;
    uint16_t slot;
    uint16_t tick16 = (uint16_t)time_sysTick;

    primask = autosaveTrace_irqSave();

    slot = (uint16_t)(autosave_trace_write_cursor & AUTOSAVE_TRACE_INDEX_MASK);
    autosave_trace_ring[slot][AUTOSAVE_TRACE_STAGE_OFFSET] = (uint8_t)stage;
    autosave_trace_ring[slot][AUTOSAVE_TRACE_FLAGS_OFFSET] = flags;
    autosave_trace_ring[slot][AUTOSAVE_TRACE_TICK_OFFSET + 0u] =
        (uint8_t)tick16;
    autosave_trace_ring[slot][AUTOSAVE_TRACE_TICK_OFFSET + 1u] =
        (uint8_t)(tick16 >> 8u);
    autosave_trace_ring[slot][AUTOSAVE_TRACE_VALUE_OFFSET + 0u] =
        (uint8_t)value32;
    autosave_trace_ring[slot][AUTOSAVE_TRACE_VALUE_OFFSET + 1u] =
        (uint8_t)(value32 >> 8u);
    autosave_trace_ring[slot][AUTOSAVE_TRACE_VALUE_OFFSET + 2u] =
        (uint8_t)(value32 >> 16u);
    autosave_trace_ring[slot][AUTOSAVE_TRACE_VALUE_OFFSET + 3u] =
        (uint8_t)(value32 >> 24u);

    autosave_trace_write_cursor =
        (uint16_t)(autosave_trace_write_cursor + 1u);

    /*
     * The slot just written was the oldest unflushed one iff the ring was
     * already full. Detect that by comparing the post-increment span against
     * capacity, and force the flush cursor to stay consistent with what is
     * actually still readable.
     */
    if ((uint16_t)(autosave_trace_write_cursor - autosave_trace_flush_cursor) >
        AUTOSAVE_TRACE_RECORD_COUNT) {
        autosave_trace_flush_cursor = (uint16_t)(
            autosave_trace_write_cursor - AUTOSAVE_TRACE_RECORD_COUNT);
        autosave_trace_dropped_count =
            (uint16_t)(autosave_trace_dropped_count + 1u);
    }

    autosaveTrace_irqRestore(primask);
}

uint16_t autosaveTrace_pendingCount(void)
{
    /* Single-word volatile reads; no lock needed for a snapshot. */
    return (uint16_t)(autosave_trace_write_cursor - autosave_trace_flush_cursor);
}

uint8_t autosaveTrace_peekRecord(uint16_t index,
                                 uint8_t out[AUTOSAVE_TRACE_RECORD_BYTES])
{
    uint16_t slot;
    uint8_t i;

    slot = (uint16_t)((autosave_trace_flush_cursor + index) &
                      AUTOSAVE_TRACE_INDEX_MASK);
    for (i = 0u; i < AUTOSAVE_TRACE_RECORD_BYTES; i++)
        out[i] = autosave_trace_ring[slot][i];
    return 1u;
}

void autosaveTrace_advanceFlushCursor(uint16_t count)
{
    autosave_trace_flush_cursor =
        (uint16_t)(autosave_trace_flush_cursor + count);
}

uint16_t autosaveTrace_droppedCount(void)
{
    return autosave_trace_dropped_count;
}

#else /* !DEV_MODE_LOGGING */

void autosaveTrace_record(autosave_trace_stage_t stage,
                          uint8_t flags,
                          uint32_t value32)
{
    (void)stage;
    (void)flags;
    (void)value32;
}

uint16_t autosaveTrace_pendingCount(void)
{
    return 0u;
}

uint8_t autosaveTrace_peekRecord(uint16_t index,
                                 uint8_t out[AUTOSAVE_TRACE_RECORD_BYTES])
{
    (void)index;
    (void)out;
    return 0u;
}

void autosaveTrace_advanceFlushCursor(uint16_t count)
{
    (void)count;
}

uint16_t autosaveTrace_droppedCount(void)
{
    return 0u;
}

#endif /* DEV_MODE_LOGGING */
```

Notes on this implementation:

- The overwrite-oldest-on-full policy is deliberate: for a debugging tool,
  the most recent events (closest to whatever just went wrong) are more
  valuable than the oldest ones. `autosaveTrace_droppedCount()` makes the
  loss visible instead of silent.
- `autosaveTrace_record()`'s critical section is short and bounded (one slot
  write, one cursor increment, one comparison) — the same "must remain
  one-byte and bounded" discipline `autosave_maskByteOr()` already follows,
  chosen specifically because this function is reachable from the same
  timer-adjacent contexts as the mutation markers it instruments.
- No malloc, no loops of unbounded length, no display access, no filesystem
  access — matches every constraint the parent plan's Step 1 laid out for
  observability code.

---

## Modified file: `config.h`

Do **not** add or redefine a trace-specific mode. `DEV_MODE_LOGGING` already
exists near the top of `config.h` and is the project-wide non-screen diagnostic
mode; amend that existing adjacent comment to state that it also gates the
Autosave trace ring and its file output. Value `1` compiles the 518-byte trace
allocation and trace file path, while value `0` must leave only zero-storage
stubs and no trace file activity. The current value is a bench-build choice;
this plan does not introduce another mode or silently change its value.

Add the trace cadence immediately after the existing
`AUTOSAVE_MASK_BITS_PER_TICK` block, retaining the following adjacent comment:

```c
/*
 * Ordinary pause between best-effort trace-file flush attempts.
 *
 * Input is the wrapping 16-bit time_sysTick, so this must stay below 32,768
 * ms like every other autosave scheduler interval. Output: the trace flush
 * scheduler will not attempt a new flush operation more often than this, even
 * if AutosaveTrace's ring already has pending records; it always yields to
 * the settings and autosave writers when both want the same idle tick. Why:
 * the trace file matters far less than autosave durability, so it must never
 * compete for filesystem ownership. Affiliate:
 * filesystem_autosaveTraceFlushSchedule_tick().
 */
#define AUTOSAVE_TRACE_FLUSH_INTERVAL_MS 500u
```

---

## Modified file: `Core/Bank/Scene/Autosave.c`

One include added near the top, alongside the existing includes:

```c
#include "Autosave.h"

#include "BankData.h"
#include "SceneData.h"
#include "InstrumentManager.h"
#include "AutosaveTrace.h"

#include <string.h>
```

One call added inside the existing single funnel function. Current body:

```c
static void autosave_markPayloadOffsetDirty(uint16_t payload_offset)
{
    if (!autosave_mutation_tracking_enabled ||
        payload_offset >= AUTOSAVE_PAYLOAD_BYTES) {
        return;
    }
    autosave_maskByteOr(
        (uint16_t)(payload_offset >> 3u),
        (uint8_t)(1u << (payload_offset & 7u)));
}
```

New body — one line added after the mask update, still inside the same
function so the guard clause above continues to suppress tracing exactly
when it suppresses the real mutation (boot population, disabled tracking, and
out-of-range offsets never appear in the trace, matching real behavior):

```c
static void autosave_markPayloadOffsetDirty(uint16_t payload_offset)
{
    if (!autosave_mutation_tracking_enabled ||
        payload_offset >= AUTOSAVE_PAYLOAD_BYTES) {
        return;
    }
    autosave_maskByteOr(
        (uint16_t)(payload_offset >> 3u),
        (uint8_t)(1u << (payload_offset & 7u)));
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_DIRTY, 0u,
                         (uint32_t)payload_offset);
}
```

No other line in `Autosave.c` changes. No line in `Autosave.h` changes. This
is the entire footprint in the production wire-format module.

---

## Modified file: `Core/Hardware/SD/filesystem.c`

### 1. New include

Add next to the existing `#include "Autosave.h"` (wherever that currently
sits in the include block):

```c
#include "AutosaveTrace.h"
```

### 2. New private operation identifier

Add one member to the existing private `fs_internal_op_t` enum, immediately
after `FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN` (current location: inside the
`/* Operation types */` block, right after the drain op's doc comment):

```c
    /*
     * Best-effort background flush of AutosaveTrace's ring to a root file.
     *
     * Inputs: AutosaveTrace's pending-record snapshot. Output: at most one
     * ring's worth of 8-byte records appended to AUTOSAVE_TRACE_FILENAME,
     * closed and synced before the flush cursor advances. This operation
     * never competes with settings or autosave writer starts -- see
     * filesystem_tick()'s idle scheduling order. Affiliates: AutosaveTrace.c
     * and filesystem_autosaveTraceFlush_tick().
     */
    FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH,
```

### 3. New static scheduler state

Add next to the existing `fs_autosave_*` static block (near line 1296):

```c
#if DEV_MODE_LOGGING
/*
 * The trace flush cadence is diagnostic-only state.  Keep it inside the same
 * build gate as the ring so a non-logging image retains neither this two-byte
 * scheduler value nor any trace-file behavior.
 */
static uint16_t fs_autosave_trace_next_due_tick = 0u;
#endif
```

Only one new static is needed: the flush operation is stateless between
calls beyond this due-tick (it always flushes whatever is currently pending,
re-derived from `autosaveTrace_pendingCount()` each time it starts), so it
does not need its own "armed" latch the way the autosave writer does — there
is no debounce-vs-continuation distinction to track, only a fixed cadence.
The 2-byte cadence plus the trace module's 518 bytes make the exact approved
diagnostic allocation 520 bytes when logging is enabled and zero when it is
disabled.

### 4. Call site — `SCHEDULED` and `ADMITTED`

Current code in `filesystem_autosaveWriterSchedule_tick()`:

```c
    now = time_sysTick;
    if (!fs_autosave_writer_armed) {
        fs_autosave_next_due_tick = (uint16_t)(
            now + AUTOSAVE_WRITER_INTERVAL_MS);
        fs_autosave_writer_armed = 1u;
        return;
    }
    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE ||
        (uint16_t)(now - fs_autosave_next_due_tick) >= 0x8000u) {
        return;
    }
    if (filesystem_start(FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN,
                         FS_FILE_SETTINGS, 0u,
                         filesystem_autosaveWriterCompleted)) {
        fs_autosave_transaction_active = 1u;
    }
```

New code — two calls added, each at the exact edge it names:

```c
    now = time_sysTick;
    if (!fs_autosave_writer_armed) {
        fs_autosave_next_due_tick = (uint16_t)(
            now + AUTOSAVE_WRITER_INTERVAL_MS);
        fs_autosave_writer_armed = 1u;
        /*
         * SCHEDULED fires once per debounce edge, not once per idle tick:
         * this branch is only reached the tick the writer transitions from
         * disarmed to armed, so the ring cannot be flooded by ordinary idle
         * polling. Affiliate: AUTOSAVE_TRACE_STAGE_SCHEDULED in
         * AUTOSAVE_REMEDY_PA2ST1.md's stage table.
         */
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_SCHEDULED, 0u, 0u);
        return;
    }
    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE ||
        (uint16_t)(now - fs_autosave_next_due_tick) >= 0x8000u) {
        return;
    }
    if (filesystem_start(FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN,
                         FS_FILE_SETTINGS, 0u,
                         filesystem_autosaveWriterCompleted)) {
        fs_autosave_transaction_active = 1u;
        /*
         * ADMITTED is the exact boundary the 045 postmortem lacked: proof
         * that filesystem_start() accepted the drain operation and it is now
         * current_op. Affiliate: AUTOSAVE_TRACE_STAGE_ADMITTED.
         */
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_ADMITTED, 0u, 0u);
    }
```

### 5. Call site — `VALIDATED`

Current code in `filesystem_autosaveParameterDrain_tick()` case `5`:

```c
    case 5: /* RECORD BEST VALID CANDIDATE, THEN ADVANCE A -> B */
        if (op_autosave_writer.candidate_valid &&
            (!op_autosave_writer.have_winner ||
             autosave_generationIsNewer(
                 op_autosave_writer.validation.generation,
                 op_autosave_writer.winner_generation))) {
            op_autosave_writer.have_winner = 1u;
            op_autosave_writer.winner_index =
                op_autosave_writer.candidate_index;
            op_autosave_writer.winner_generation =
                op_autosave_writer.validation.generation;
            op_autosave_writer.winner_probe =
                op_autosave_writer.validation.probe_counter;
        }
        if (op_autosave_writer.candidate_index + 1u <
            AUTOSAVE_RECORD_FILE_COUNT) {
            op_autosave_writer.candidate_index++;
            op_phase = 1u;
            return;
        }
        op_phase = op_autosave_writer.have_winner ? 50u : 30u;
        return;
```

New code — one call added right before the final `op_phase` assignment, once
both candidates have been examined:

```c
    case 5: /* RECORD BEST VALID CANDIDATE, THEN ADVANCE A -> B */
        if (op_autosave_writer.candidate_valid &&
            (!op_autosave_writer.have_winner ||
             autosave_generationIsNewer(
                 op_autosave_writer.validation.generation,
                 op_autosave_writer.winner_generation))) {
            op_autosave_writer.have_winner = 1u;
            op_autosave_writer.winner_index =
                op_autosave_writer.candidate_index;
            op_autosave_writer.winner_generation =
                op_autosave_writer.validation.generation;
            op_autosave_writer.winner_probe =
                op_autosave_writer.validation.probe_counter;
        }
        if (op_autosave_writer.candidate_index + 1u <
            AUTOSAVE_RECORD_FILE_COUNT) {
            op_autosave_writer.candidate_index++;
            op_phase = 1u;
            return;
        }
        /*
         * VALIDATED fires once both A and B have been examined. flags bit 0
         * is have_winner; bit 1 is winner_index (0=A, 1=B), meaningful only
         * when bit 0 is set. value32 is winner_generation, 0 when no winner
         * exists (about to enter recovery at phase 30). Affiliate:
         * AUTOSAVE_TRACE_STAGE_VALIDATED.
         */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_VALIDATED,
            (uint8_t)((op_autosave_writer.have_winner ? 1u : 0u) |
                     (op_autosave_writer.have_winner ?
                          (uint8_t)(op_autosave_writer.winner_index << 1u) :
                          0u)),
            op_autosave_writer.have_winner ?
                op_autosave_writer.winner_generation : 0u);
        op_phase = op_autosave_writer.have_winner ? 50u : 30u;
        return;
```

### 6. Call site — `MASK_MERGED`

Current code in case `55`:

```c
    case 55: /* CLOSE COMPLETE: FALL THROUGH IF CANONICAL SRAM IS EMPTY */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!autosave_maskHasDirty()) {
            filesystem_complete(FS_STATUS_DONE);
            return;
        }
        op_autosave_writer.payload_scan_offset = 0u;
        op_autosave_writer.patch_count = 0u;
        op_phase = 56u;
        return;
```

New code:

```c
    case 55: /* CLOSE COMPLETE: FALL THROUGH IF CANONICAL SRAM IS EMPTY */
    {
        uint8_t has_dirty;

        if (!op_close_done)
            return;
        op_file = NULL;
        has_dirty = autosave_maskHasDirty();
        /*
         * MASK_MERGED fires exactly once per drain, after the winner's
         * complete 3,856-byte mask has been OR-merged into Autosave.c's
         * canonical record and the read handle is closed. flags bit 0 is
         * has_dirty: 0 means this generation completes read-only right here
         * (matches the documented "empty canonical mask falls through before
         * a periodic file write" behavior); 1 means classification continues
         * at phase 56. value32 is mask_bytes_read, which should always equal
         * AUTOSAVE_MASK_BYTES (3,856) here -- a different value is itself a
         * bug signal. Affiliate: AUTOSAVE_TRACE_STAGE_MASK_MERGED.
         */
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_MASK_MERGED,
                             (uint8_t)(has_dirty ? 1u : 0u),
                             op_autosave_writer.mask_bytes_read);
        if (!has_dirty) {
            filesystem_complete(FS_STATUS_DONE);
            return;
        }
        op_autosave_writer.payload_scan_offset = 0u;
        op_autosave_writer.patch_count = 0u;
        op_phase = 56u;
        return;
    }
```

### 7. Call site — `CAPTURED`

Current code in case `56` has two `op_phase = 10u;` transitions. Factor a
tiny static helper first so both sites share one trace call instead of
duplicating the encoding logic:

```c
/*
 * Record one CAPTURED trace event for either exit from case 56.
 *
 * Input: budget_exhausted distinguishes "AUTOSAVE_PARAMETER_GETS_PER_WRITE
 * was reached mid-scan, continuing next generation" (1) from "the complete
 * 30,848-byte payload was scanned this generation" (0). Output: one trace
 * record with patch_count as its value32. Affiliate: filesystem_
 * autosaveParameterDrain_tick() case 56, both transitions to phase 10.
 */
static void filesystem_autosaveTraceCaptured(uint8_t budget_exhausted)
{
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_CAPTURED,
                         budget_exhausted,
                         op_autosave_writer.patch_count);
}
```

Place this helper immediately above `filesystem_autosaveParameterDrain_tick()`
so it is visible to case 56 without a forward declaration.

Current code in case `56`:

```c
        while (op_autosave_writer.payload_scan_offset <
                   AUTOSAVE_PAYLOAD_BYTES &&
               examined < AUTOSAVE_MASK_BITS_PER_TICK) {
            uint16_t payload_offset =
                op_autosave_writer.payload_scan_offset;

            if (op_autosave_writer.patch_count >=
                AUTOSAVE_PARAMETER_GETS_PER_WRITE) {
                op_phase = 10u;
                return;
            }
            op_autosave_writer.payload_scan_offset++;
            examined++;
            if (!autosave_maskBitTake(payload_offset)) {
                continue;
            }
            if (autosave_getLivePayloadByte(
                    payload_offset,
                    &fs_autosave_parameter_cache.payload_values[
                        op_autosave_writer.patch_count])) {
                fs_autosave_parameter_cache.payload_offsets[
                    op_autosave_writer.patch_count] = payload_offset;
                op_autosave_writer.patch_count++;
            }
        }
        if (op_autosave_writer.payload_scan_offset >=
            AUTOSAVE_PAYLOAD_BYTES) {
            op_phase = 10u;
        }
        return;
```

New code — one trace call at each of the two existing transitions, nothing
else about the loop's logic changes:

```c
        while (op_autosave_writer.payload_scan_offset <
                   AUTOSAVE_PAYLOAD_BYTES &&
               examined < AUTOSAVE_MASK_BITS_PER_TICK) {
            uint16_t payload_offset =
                op_autosave_writer.payload_scan_offset;

            if (op_autosave_writer.patch_count >=
                AUTOSAVE_PARAMETER_GETS_PER_WRITE) {
                filesystem_autosaveTraceCaptured(1u);
                op_phase = 10u;
                return;
            }
            op_autosave_writer.payload_scan_offset++;
            examined++;
            if (!autosave_maskBitTake(payload_offset)) {
                continue;
            }
            if (autosave_getLivePayloadByte(
                    payload_offset,
                    &fs_autosave_parameter_cache.payload_values[
                        op_autosave_writer.patch_count])) {
                fs_autosave_parameter_cache.payload_offsets[
                    op_autosave_writer.patch_count] = payload_offset;
                op_autosave_writer.patch_count++;
            }
        }
        if (op_autosave_writer.payload_scan_offset >=
            AUTOSAVE_PAYLOAD_BYTES) {
            filesystem_autosaveTraceCaptured(0u);
            op_phase = 10u;
        }
        return;
```

### 8. Call site — `PUBLISHED`

Current code, case `22`:

```c
    case 22: /* WAIT COMMIT CLOSE, THEN ENTER THE EXISTING FINAL SYNC */
        if (!op_close_done)
            return;
        op_file = NULL;
        filesystem_finish(FS_STATUS_DONE);
        return;
```

New code:

```c
    case 22: /* WAIT COMMIT CLOSE, THEN ENTER THE EXISTING FINAL SYNC */
        if (!op_close_done)
            return;
        op_file = NULL;
        /*
         * PUBLISHED fires here because the commit byte is already durable
         * and its handle is already closed -- everything after this point is
         * the shared FS_INTERNAL_OP_FLUSH_FINISH gate every operation uses,
         * not autosave-specific work. flags is the newly active target index
         * (winner_index XOR 1); value32 is the new generation. Affiliate:
         * AUTOSAVE_TRACE_STAGE_PUBLISHED.
         */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_PUBLISHED,
            (uint8_t)(op_autosave_writer.winner_index ^ 1u),
            op_autosave_writer.winner_generation + 1u);
        filesystem_finish(FS_STATUS_DONE);
        return;
```

### 9. Call site — `TERMINAL`

Current code, start of `filesystem_autosaveWriterCompleted()`:

```c
static void filesystem_autosaveWriterCompleted(void)
{
    fs_autosave_transaction_active = 0u;
    if (!fs_autosave_enabled) {
```

New code — the trace call goes first, before anything is reset, so it always
reflects the state the drain actually finished in:

```c
static void filesystem_autosaveWriterCompleted(void)
{
    /*
     * TERMINAL fires before any scheduler flag is reset, so it reflects the
     * exact outcome the drain finished with. flags bit 0 is
     * (status == FS_STATUS_DONE); value32 is unused (0) -- this stage exists
     * to close the loop opened by ADMITTED, not to duplicate PUBLISHED's
     * payload. Affiliate: AUTOSAVE_TRACE_STAGE_TERMINAL, and directly
     * targets root cause (f) from AUTOSAVE_PHASE2_PLAN.md: the Load/Save
     * exclusion regression that silently stopped the writer without any
     * observable terminal signal.
     */
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_TERMINAL,
                         (uint8_t)(status == FS_STATUS_DONE ? 1u : 0u),
                         0u);
    fs_autosave_transaction_active = 0u;
    if (!fs_autosave_enabled) {
```

### 10. New flush-to-file state machine

Add near `filesystem_writeBootLog_tick()`, modeled directly on it but
streaming a variable, already-known length instead of a fixed eight bytes,
and using append mode (`"a"`, confirmed supported by
`afatfs_modeFromString()` as `AFATFS_FILE_MODE_APPEND | AFATFS_FILE_MODE_CREATE`)
so history accumulates across flushes instead of being truncated each time:

```c
/*
 * Serialize AutosaveTrace's pending ring into staging_buf.
 *
 * Input: a pendingCount() snapshot already bounded to
 * AUTOSAVE_TRACE_RECORD_COUNT (the ring cannot hold more than that regardless
 * of what pendingCount() reports, so this is always safe against
 * staging_buf's 512-byte size -- see AUTOSAVE_TRACE_RECORD_COUNT's sizing
 * comment in AutosaveTrace.h). Output: byte_count set to
 * record_count * AUTOSAVE_TRACE_RECORD_BYTES. Affiliate:
 * filesystem_autosaveTraceFlush_tick() phase 2.
 */
static void filesystem_autosaveTraceSerialize(uint16_t record_count,
                                              uint16_t *byte_count)
{
    uint16_t i;

    for (i = 0u; i < record_count; i++) {
        (void)autosaveTrace_peekRecord(
            i, staging_buf + (i * AUTOSAVE_TRACE_RECORD_BYTES));
    }
    *byte_count = (uint16_t)(record_count * AUTOSAVE_TRACE_RECORD_BYTES);
}

/*
 * Append AutosaveTrace's currently pending records to the root trace file.
 *
 * DEV_MODE_LOGGING's discipline applies here: never print to the screen, never delay other filesystem
 * work, never claim durability before afatfs_sync() confirms it.
 *
 * What: opens AUTOSAVE_TRACE_FILENAME in append mode (creating it on first
 * use), writes one serialized batch of currently-pending records, closes,
 * and syncs before acknowledging AutosaveTrace's flush cursor. Why: the
 * cursor must not advance until the bytes are durable, or a power cut
 * between write and sync would silently lose records the ring had already
 * discarded to make room for newer ones. Inputs: AutosaveTrace's ring.
 * Outputs: an appended, human-inspectable 8-byte-record file. Affiliates:
 * AutosaveTrace.c, filesystem_autosaveTraceFlushSchedule_tick(), and
 * filesystem_autosaveTraceFlushBlocking().
 */
static void filesystem_autosaveTraceFlush_tick(void)
{
    switch (op_phase) {
    case 0u: /* RETURN ROOT + OPEN/APPEND asavetrc.bin */
        if (!afatfs_chdir(NULL))
            return;
        op_stream_index = autosaveTrace_pendingCount();
        if (op_stream_index > AUTOSAVE_TRACE_RECORD_COUNT)
            op_stream_index = AUTOSAVE_TRACE_RECORD_COUNT;
        if (op_stream_index == 0u) {
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        filesystem_autosaveTraceSerialize((uint16_t)op_stream_index,
                                          &op_write_line_len);
        op_file_ready = false;
        op_file = NULL;
        op_bytes_done = 0u;
        if (!afatfs_fopen_lfn(AUTOSAVE_TRACE_FILENAME, "a",
                              AFATFS_MATCH_CASE_INSENSITIVE, NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 1u;
        return;

    case 1u: /* WAIT OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 2u;
        return;

    case 2u: /* WRITE THE SERIALIZED BATCH */
    {
        uint32_t n;

        if (op_bytes_done >= op_write_line_len) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 3u;
            return;
        }
        n = afatfs_fwrite(op_file, staging_buf + op_bytes_done,
                          op_write_line_len - op_bytes_done);
        op_bytes_done += n;
        if (n == 0u && afatfs_isFull()) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        return;
    }

    case 3u: /* WAIT CLOSE, THEN SYNC BEFORE ACKNOWLEDGING THE RING */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_sync())
            return;
        /*
         * Only advance the flush cursor after the shared sync gate confirms
         * durability. op_stream_index still holds the exact record count
         * serialized at phase 0, unmodified since -- no other phase in this
         * operation reuses that scratch field.
         */
        autosaveTrace_advanceFlushCursor((uint16_t)op_stream_index);
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}
```

### 11. New scheduler, lowest priority

Add next to `filesystem_autosaveWriterSchedule_tick()`:

```c
/*
 * Return a background trace append's unobserved terminal state to IDLE.
 *
 * This callback exists because a background diagnostic has no foreground
 * caller to consume DONE/ERROR. The ring's flush cursor advances only after
 * sync, so acknowledging ERROR releases the shared facade without hiding a
 * failed append: the next low-priority cadence retries the same records.
 */
static void filesystem_autosaveTraceFlushCompleted(void)
{
    filesystem_ack();
}

/*
 * Start a bounded trace-flush operation only when nothing else wants the
 * one idle filesystem owner and enough time has passed since the last
 * attempt.
 *
 * Inputs: AutosaveTrace's pending count and the fixed
 * AUTOSAVE_TRACE_FLUSH_INTERVAL_MS cadence. Output: at most one
 * FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH start per interval, and only when
 * filesystem_tick() reached this call, which happens only after both the
 * settings and autosave schedulers already declined the same idle tick --
 * see the call order in filesystem_tick(). Why: the trace file must never be
 * the reason autosave durability is delayed. Affiliate: filesystem_tick().
 */
static void filesystem_autosaveTraceFlushSchedule_tick(void)
{
#if DEV_MODE_LOGGING
    uint16_t now = time_sysTick;

    if (autosaveTrace_pendingCount() == 0u)
        return;
    /* A zero deadline is initialized from the current clock first, so a
     * first dirty event after tick16 has wrapped cannot be deferred for a
     * half-cycle by an uninitialized absolute-time comparison. */
    if (fs_autosave_trace_next_due_tick == 0u)
        fs_autosave_trace_next_due_tick = now;
    if ((uint16_t)(now - fs_autosave_trace_next_due_tick) >= 0x8000u)
        return;
    /* This autonomous append must self-ack on either terminal outcome. A
     * NULL callback would strand the shared facade at DONE/ERROR and block
     * the settings and autosave schedulers after the first trace batch. */
    if (filesystem_start(FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH,
                         FS_FILE_SETTINGS, 0u,
                         filesystem_autosaveTraceFlushCompleted)) {
        fs_autosave_trace_next_due_tick = (uint16_t)(
            now + AUTOSAVE_TRACE_FLUSH_INTERVAL_MS);
    }
#else
    /* Keep the idle scheduler call harmless in a production build.  The
     * trace module supplies zero-return stubs, but this explicit no-op also
     * guarantees no trace-only filesystem state or file operation exists. */
    return;
#endif
}
```

### 12. Wire the scheduler and the dispatcher into `filesystem_tick()`

Current code:

```c
    if (status == FS_STATUS_IDLE)
        filesystem_settingsWriterSchedule_tick();
    if (status == FS_STATUS_IDLE)
        filesystem_autosaveWriterSchedule_tick();
    if (status != FS_STATUS_BUSY) return;

    switch (current_op) {
    case FS_INTERNAL_OP_FLUSH_FINISH:
        filesystem_flushFinish_tick();
        break;
```

New code — one more idle-scheduler line, in explicit last-priority position,
plus one more `case` in the dispatcher:

```c
    if (status == FS_STATUS_IDLE)
        filesystem_settingsWriterSchedule_tick();
    if (status == FS_STATUS_IDLE)
        filesystem_autosaveWriterSchedule_tick();
    if (status == FS_STATUS_IDLE)
        filesystem_autosaveTraceFlushSchedule_tick();
    if (status != FS_STATUS_BUSY) return;

    switch (current_op) {
    case FS_INTERNAL_OP_FLUSH_FINISH:
        filesystem_flushFinish_tick();
        break;
    case FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH:
        filesystem_autosaveTraceFlush_tick();
        break;
```

(The new `case` can go anywhere in the switch; placing it right after
`FS_INTERNAL_OP_FLUSH_FINISH` keeps every autosave-adjacent case visually
grouped with `FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN` and
`FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES` further down, but exact ordering does
not matter functionally.)

---

## Modified file: `Core/Hardware/SD/filesystem.h`

One new public declaration, for deliberate use before a planned power-cycle
during bench testing — the Step 2 checkpoint in the parent plan needs
"exact starting/ending generation, CRC, mask bits, and payload offsets for
every run," and an unflushed trace tail at the moment of a deliberate
power-off would undermine that. This mirrors the existing
`filesystem_writeBootTimeoutLogBlocking()` pattern exactly:

```c
/*
 * Block until AutosaveTrace's currently pending records are durable on disk.
 *
 * Inputs: an idle boot/runtime filesystem facade. Output: pumps
 * filesystem_tick() until FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH completes, or
 * returns immediately if nothing is pending. Why: a staged hardware test
 * that intends to power-cycle immediately after one observed transaction
 * needs a deterministic "the trace file now contains everything traced so
 * far" boundary, not a best-effort background cadence. This is a bench-
 * testing convenience, not a production code path -- callers outside test
 * scaffolding should not need it. Affiliates: AutosaveTrace.c and
 * filesystem_autosaveTraceFlush_tick().
 */
uint8_t     filesystem_autosaveTraceFlushBlocking(void);
```

Implementation, added near `filesystem_ensureAutosaveFilesBlocking()` in
`filesystem.c`:

```c
uint8_t filesystem_autosaveTraceFlushBlocking(void)
{
    uint8_t trace_flushed;

    if (autosaveTrace_pendingCount() == 0u)
        return 1u;
    if (status == FS_STATUS_BUSY ||
        !filesystem_start(FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH,
                          FS_FILE_SETTINGS, 0u, NULL)) {
        return 0u;
    }
    while (status == FS_STATUS_BUSY)
        filesystem_tick();
    trace_flushed = (uint8_t)(status == FS_STATUS_DONE);
    /* Restore idle ownership: this helper must not strand autonomous writers
     * behind the terminal status it deliberately consumed for its return. */
    filesystem_ack();
    return trace_flushed;
}
```

This function is intentionally blocking and intentionally not called from
anywhere in `main.c`'s boot ladder — it exists to be called manually (e.g.
from a bench-test harness or a temporary debug hook) immediately before a
deliberate power-cycle, exactly the way a tester would use it per the parent
plan's Step 2 evidence checklist.

---

## Modified file: `Makefile`

One line added to the existing explicit source list, immediately after
`Core/Bank/Scene/Autosave.c` (current line 95):

```makefile
  Core/Bank/Scene/Autosave.c \
  Core/Bank/Scene/AutosaveTrace.c \
  Core/Bank/Scene/SceneModTargets.c \
```

No `-I` path changes are needed: `-ICore/Bank/Scene` is already in
`CFLAGS`, so `#include "AutosaveTrace.h"` resolves flatly from any file,
matching how `#include "Autosave.h"` already works.

---

## Complete change inventory

| File | Change |
| --- | --- |
| `Core/Bank/Scene/AutosaveTrace.h` | new — public API, stage enum, record layout, sizing |
| `Core/Bank/Scene/AutosaveTrace.c` | new — ring buffer, cursors, IRQ-safe append |
| `config.h` | use existing `DEV_MODE_LOGGING` gate; add `AUTOSAVE_TRACE_FLUSH_INTERVAL_MS` |
| `Core/Bank/Scene/Autosave.c` | + 1 include, + 1 call in `autosave_markPayloadOffsetDirty()` |
| `Core/Bank/Scene/Autosave.h` | no change |
| `Core/Hardware/SD/filesystem.c` | + 1 include, + 1 private enum value, + 1 static, + 2 new static functions (`filesystem_autosaveTraceCaptured`, `filesystem_autosaveTraceSerialize`), + 2 new tick/scheduler functions, + 1 public function, + 7 call sites at existing phase transitions, + 1 dispatcher case, + 1 idle-scheduler line |
| `Core/Hardware/SD/filesystem.h` | + 1 public declaration (`filesystem_autosaveTraceFlushBlocking`) |
| `Makefile` | + 1 source line |

Total new production call sites outside the new module: **one** in
`Autosave.c`, **nine** in `filesystem.c` (seven stage calls + the scheduler
line + the dispatcher case). Every one of them is either a single function
call added at an already-existing phase boundary, or a single new line
extending an already-existing pattern (idle scheduler chain, dispatcher
switch, private op enum). Nothing existing is restructured, renumbered, or
reordered — this satisfies the parent plan's Step 5/6 discipline of "change
exactly one boundary per hardware test pass" one step early, by construction.

---

## Verifying the instrumentation itself (ties back to the parent plan's Step 1 checkpoint)

Before this module is trusted for anything in Steps 2–6, run the parent
plan's own check: *"run one full Phase 1 debounce/commit cycle from a known
clean mask, and confirm every stage fires exactly once with correct
ordering, with zero feature code changed."* Concretely, with
`DEV_MODE_LOGGING` set to 1 for this bench pass only:

1. Power on with both `.hcprms1`/`.hcprms2` valid and clean (no dirty bits).
2. Make exactly one Phase 1 scalar edit (e.g. one Scene parameter).
3. Let the writer run to completion, then call
   `filesystem_autosaveTraceFlushBlocking()` (temporarily wired to a debug
   trigger) before powering off.
4. Pull `asavetrc.bin` off the card. Expect exactly one record for each of
   `DIRTY, SCHEDULED, ADMITTED, VALIDATED, MASK_MERGED, CAPTURED, PUBLISHED,
   TERMINAL`, in that order, with monotonically non-decreasing `tick16`
   values (allowing for the ~65 s wrap) and `autosaveTrace_droppedCount()`
   reading 0.
5. Cross-check `VALIDATED`'s generation/winner fields and `PUBLISHED`'s
   generation/target-index fields against the actual file generations read
   independently off the card, the same way `045_SESSION_HANDOFF_LOG.md`
   §4.1/§4.2 cross-checked payload bytes against `SD_CARD/Bank/000 Full/`.
6. Repeat with a re-dirty injected mid-drain (edit a second parameter while
   the first drain is still between `ADMITTED` and `PUBLISHED`) and confirm
   the trace shows a second full `SCHEDULED → ... → TERMINAL` sequence
   starting only after the first `TERMINAL`, with `CAPTURED`'s budget-
   exhausted flag correctly reflecting whether it completed in one
   generation or continued.

If any stage is missing, duplicated, or out of order, the instrumentation —
not the autosave writer — is the suspect, per the parent plan's own rule.
Fix `AutosaveTrace.c`/the call sites before using this module for Step 2's
Phase 1 matrix or any later step.

---

## Explicit non-goals for this step

To keep this step's blast radius exactly as small as the parent plan
requires:

- No Load/Save-page gate observability is added yet. That instrumentation
  belongs to Step 6 (Load/Save exclusion), which does not exist yet; adding
  hooks for a feature that isn't built would be guessing at its shape.
- No Menu/UI surface is added to view the trace live. The file is meant to
  be pulled off the card after a bench run, matching how the existing
  `bootlog.bin` is used.
- No attempt is made to make the `DIRTY` stage lossless during bulk
  whole-region marks (boot `autosave_markResidentBankDirty()`, a future
  Phase 2 whole-object commit). That is a known, accepted limitation, not a
  bug to fix here.
- No change to `AUTOSAVE_WRITER_INTERVAL_MS`, `AUTOSAVE_WRITER_CONTINUATION_
  INTERVAL_MS`, `AUTOSAVE_PARAMETER_GETS_PER_WRITE`, or
  `AUTOSAVE_MASK_BITS_PER_TICK` — this step only observes the existing
  schedule, it does not tune it.
- The trace uses the established `DEV_MODE_LOGGING` build selection. It must
  allocate no trace SRAM and issue no trace-file I/O when that mode is `0`;
  when it is `1`, it is a deliberate bench diagnostic alongside the existing
  non-screen boot logger. Do not add a second trace mode or alter the current
  logging selection incidentally while implementing this step.

---

## Implementation record — 2026-08-09

**Status: implemented, but the initial hardware build exposed a scheduler
lifecycle defect in the diagnostic writer; corrected below and awaiting a new
hardware verification run.** No Phase 2 whole-object hook, Load/Save
exclusion, autosave writer timing, or parameter ownership behavior was
intended to change.

- Added `AutosaveTrace.c/.h`: 64 fixed eight-byte records (512 bytes), two
  wrapping cursors and one saturated dropped-record counter (6 bytes). The
  ring is IRQ-protected and records `D/S/A/V/M/C/P/T` stages with tick16 and
  stage payloads. An overwrite advances only the oldest undurable cursor and
  increments `autosaveTrace_droppedCount()`; it is therefore visible rather
  than treated as a successful trace.
- Added the approved 2-byte logging-only trace cadence in `filesystem.c`, for
  **520 trace-specific static bytes** while logging is enabled. `DEV_MODE_LOGGING
  == 0` compiles trace APIs to no-op/zero stubs, omits the ring and cadence, and
  the scheduler cannot start a trace file operation.
- Added the eight lifecycle call sites exactly at the documented scalar dirty,
  scheduler-arm, drain-admission, candidate-validation, mask-merge,
  capture-to-copy, closed-final-commit, and terminal-callback boundaries.
  Trace failure is not conflated with success: append open/write/sync failures
  leave the ring pending, and a full-card write closes its owned file before
  publishing `FS_STATUS_ERROR`.
- Added root append file `asavetrc.bin`, scheduled after settings and autosave
  work only. It serializes at most one 512-byte batch, closes, syncs, and only
  then advances the durable-flush cursor. Added the documented bench-only
  `filesystem_autosaveTraceFlushBlocking()` helper; it acknowledges its own
  terminal facade status before returning so it cannot stall later schedulers.
- Verified with `make -B -j4` for both logging configurations, then restored
  `DEV_MODE_LOGGING` to its project value of `1`. Logging-on image: text
  372,876, data 400, BSS 78,996 bytes. Logging-off image: text 367,212, data
  396, BSS 78,444 bytes; `arm-none-eabi-nm` found no `autosave_trace` or
  `fs_autosave_trace` symbol in the logging-off image. The normal build's
  existing warnings remain, with no new warning from these files.

### Post-hardware correction — 2026-08-09

The first bench fixture proved a defect in this Step 1 implementation, not a
failure of the parameter hook: `asavetrc.bin` contained one durable
`SCHEDULED` (`S`) record, with its five-second deadline elapsed during the
test, but no subsequent records and no autosave update. The background trace
flush had been started with a `NULL` completion callback. Generic filesystem
completion therefore left the shared facade in `FS_STATUS_DONE`; all three
background schedulers run only while it is `FS_STATUS_IDLE`. The first trace
append consequently disabled settings persistence, autosave admission, and
every later trace flush. This also means the prior absence of `DIRTY` (`D`)
records is **not evidence** about parameter producers: records generated after
the first append remained trapped in SRAM and could not reach the card.

The correction adds the private
`filesystem_autosaveTraceFlushCompleted()` callback. It acknowledges either
the trace append's `DONE` or `ERROR` terminal state back to `IDLE`; the trace
ring retains unsynced records on error, so this does not mask a failed append
or claim durability that did not occur. The explicit blocking bench helper
continues to use `NULL` intentionally because it observes the terminal result
and performs its own acknowledgement immediately afterwards.

Rebuild and retest before drawing any conclusion about Stage 1 instrumentation:
preserve the parameter records for comparison, replace or archive only
`asavetrc.bin`, boot, play/change a scene parameter, and leave the unit
running for at least ten seconds. Preserve the resulting trace and both dot
records. A working run must show the initial `S`, then an admitted drain and
its terminal path (`A`, `V`, `M`, and `T`, with `C`/`P` when a write is needed)
without preventing subsequent trace batches from appearing. Only that new
fixture can assess whether the expected scalar `D` records were produced.
