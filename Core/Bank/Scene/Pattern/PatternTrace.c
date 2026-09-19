/*
 * PatternTrace.c -- DEV-only ISR automation trace ring.
 *
 * The producer performs only a short protected fixed-record write. The
 * filesystem owns serialization, append I/O, sync, and cursor acknowledgement.
 */
#include "config.h"
#include "PatternTrace.h"
#include "timebase.h"

#if DEV_MODE_LOGGING

/*
 * Static diagnostic ring: 32 * 8 = 256 bytes plus three cursors.
 * Inputs: TIM3 trace publications. Outputs: oldest-first records for the
 * bounded foreground append state machine. Affiliate: SRAM manifest.
 */
static volatile uint8_t pattern_trace_records[PAT_TRACE_RECORD_COUNT]
                                               [PAT_TRACE_RECORD_BYTES];
static volatile uint16_t pattern_trace_write_cursor = 0u;
static volatile uint16_t pattern_trace_flush_cursor = 0u;
static volatile uint16_t pattern_trace_dropped = 0u;

/* Protect one record publication without extending the surrounding ISR work. */
static uint32_t patternTrace_irqSave(void)
{
    uint32_t primask;

    __asm volatile("mrs %0, primask\n\tcpsid i"
                   : "=r"(primask) :: "memory");
    return primask;
}

static void patternTrace_irqRestore(uint32_t primask)
{
    __asm volatile("msr primask, %0" :: "r"(primask) : "memory");
}

/* Return the wrapping producer-to-flush distance. */
static uint16_t patternTrace_pendingUnsafe(void)
{
    return (uint16_t)(pattern_trace_write_cursor -
                      pattern_trace_flush_cursor);
}

/* Publish one fixed-size event into the protected DEV-only ring. */
void patternTrace_record(pat_trace_stage_t stage, uint8_t flags,
                         uint32_t value)
{
    uint32_t primask = patternTrace_irqSave();
    uint16_t cursor = pattern_trace_write_cursor;
    uint8_t *record = (uint8_t *)pattern_trace_records[
        cursor % PAT_TRACE_RECORD_COUNT];
    uint16_t tick = time_sysTick;

    record[PAT_TRACE_TYPE_OFFSET] = (uint8_t)stage;
    record[PAT_TRACE_FLAGS_OFFSET] = flags;
    record[PAT_TRACE_TICK_OFFSET] = (uint8_t)tick;
    record[PAT_TRACE_TICK_OFFSET + 1u] = (uint8_t)(tick >> 8u);
    record[PAT_TRACE_VALUE_OFFSET] = (uint8_t)value;
    record[PAT_TRACE_VALUE_OFFSET + 1u] = (uint8_t)(value >> 8u);
    record[PAT_TRACE_VALUE_OFFSET + 2u] = (uint8_t)(value >> 16u);
    record[PAT_TRACE_VALUE_OFFSET + 3u] = (uint8_t)(value >> 24u);
    pattern_trace_write_cursor = (uint16_t)(cursor + 1u);
    if (patternTrace_pendingUnsafe() > PAT_TRACE_RECORD_COUNT) {
        pattern_trace_flush_cursor = (uint16_t)(
            pattern_trace_write_cursor - PAT_TRACE_RECORD_COUNT);
        if (pattern_trace_dropped != UINT16_MAX)
            pattern_trace_dropped++;
    }
    patternTrace_irqRestore(primask);
}

/*
 * Record one dropped pending automation event without making the ISR caller
 * know the trace record's packed value layout. Inputs: pending identity and
 * raw payload words. Output: one H-stage overflow record in the DEV ring.
 */
void patternTrace_recordOverflow(uint16_t identity, uint16_t payload)
{
    uint8_t flags = (uint8_t)((identity >> 10u) & 1u);
    uint32_t value = (uint32_t)identity |
                     ((uint32_t)payload << 16u);

    patternTrace_record(PAT_TRACE_STAGE_PENDING_OVERFLOW, flags, value);
}

/* Return the number of retained records awaiting durable acknowledgment. */
uint16_t patternTrace_pendingCount(void)
{
    uint32_t primask = patternTrace_irqSave();
    uint16_t count = patternTrace_pendingUnsafe();

    patternTrace_irqRestore(primask);
    return count > PAT_TRACE_RECORD_COUNT ? PAT_TRACE_RECORD_COUNT : count;
}

/* Copy one oldest-relative record while holding off the TIM3 producer. */
uint8_t patternTrace_peekRecord(uint16_t index,
                                uint8_t out[PAT_TRACE_RECORD_BYTES])
{
    uint32_t primask;
    uint16_t cursor;
    uint8_t i;

    if (!out)
        return 0u;
    primask = patternTrace_irqSave();
    if (index >= patternTrace_pendingUnsafe()) {
        patternTrace_irqRestore(primask);
        return 0u;
    }
    cursor = (uint16_t)(pattern_trace_flush_cursor + index);
    for (i = 0u; i < PAT_TRACE_RECORD_BYTES; i++)
        out[i] = pattern_trace_records[
            cursor % PAT_TRACE_RECORD_COUNT][i];
    patternTrace_irqRestore(primask);
    return 1u;
}

/* Acknowledge a successfully synced oldest prefix of the ring. */
void patternTrace_advanceFlushCursor(uint16_t count)
{
    uint32_t primask = patternTrace_irqSave();
    uint16_t pending = patternTrace_pendingUnsafe();

    if (count > pending)
        count = pending;
    pattern_trace_flush_cursor = (uint16_t)(pattern_trace_flush_cursor + count);
    patternTrace_irqRestore(primask);
}

/* Return the saturated count of records overwritten before a flush. */
uint16_t patternTrace_droppedCount(void)
{
    uint32_t primask = patternTrace_irqSave();
    uint16_t dropped = pattern_trace_dropped;

    patternTrace_irqRestore(primask);
    return dropped;
}

#else

/* Production stubs retain no PatternTrace storage or runtime file activity. */
void patternTrace_record(pat_trace_stage_t stage, uint8_t flags,
                         uint32_t value)
{
    (void)stage;
    (void)flags;
    (void)value;
}

/* Production builds do not retain or serialize the optional trace ring. */
void patternTrace_recordOverflow(uint16_t identity, uint16_t payload)
{
    (void)identity;
    (void)payload;
}

/* Production builds expose an empty durable queue. */
uint16_t patternTrace_pendingCount(void) { return 0u; }

/* Production builds never expose a serialized trace record. */
uint8_t patternTrace_peekRecord(uint16_t index,
                                uint8_t out[PAT_TRACE_RECORD_BYTES])
{
    (void)index;
    (void)out;
    return 0u;
}

/* Production builds have no ring cursor to advance. */
void patternTrace_advanceFlushCursor(uint16_t count) { (void)count; }

/* Production builds have no overwritten trace records to report. */
uint16_t patternTrace_droppedCount(void) { return 0u; }

#endif /* DEV_MODE_LOGGING */
