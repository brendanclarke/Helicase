/*
 * AutosaveTrace.c -- RAM-only lifecycle trace with a durable-flush cursor.
 *
 * The trace deliberately has no dependency on filesystem.c. Its producer may
 * run beside interrupt-side parameter changes, while filesystem.c alone owns
 * serialization, file handles, and the point at which a flush becomes durable.
 */
#include "config.h"
#include "AutosaveTrace.h"
#include "timebase.h"

#if DEV_MODE_LOGGING

/*
 * Retained diagnostic storage: AUTOSAVE_TRACE_RECORD_COUNT * 8 bytes plus
 * three uint16_t cursors. The default is 518 bytes; this approved temporary
 * 2048-record experiment is 16,390 bytes. These objects exist only in a
 * logging build; the #else stubs retain neither ring nor cursor in production.
 */
static volatile uint8_t autosave_trace_records[AUTOSAVE_TRACE_RECORD_COUNT]
                                              [AUTOSAVE_TRACE_RECORD_BYTES];
static volatile uint16_t autosave_trace_write_cursor = 0u;
static volatile uint16_t autosave_trace_flush_cursor = 0u;
static volatile uint16_t autosave_trace_dropped = 0u;

/* Protect a short ring/cursor update without extending the critical section. */
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

/* Return a wrapping cursor distance bounded by the configured ring capacity. */
static uint16_t autosaveTrace_pendingCountUnsafe(void)
{
    return (uint16_t)(autosave_trace_write_cursor -
                      autosave_trace_flush_cursor);
}

void autosaveTrace_record(autosave_trace_stage_t stage, uint8_t flags,
                          uint32_t value)
{
    uint32_t primask = autosaveTrace_irqSave();
    uint16_t cursor = autosave_trace_write_cursor;
    uint8_t *record = (uint8_t *)autosave_trace_records[
        cursor % AUTOSAVE_TRACE_RECORD_COUNT];
    uint16_t tick = time_sysTick;

    /* Write every field before publishing the new producer cursor. */
    record[AUTOSAVE_TRACE_STAGE_OFFSET] = (uint8_t)stage;
    record[AUTOSAVE_TRACE_FLAGS_OFFSET] = flags;
    record[AUTOSAVE_TRACE_TICK_OFFSET] = (uint8_t)tick;
    record[AUTOSAVE_TRACE_TICK_OFFSET + 1u] = (uint8_t)(tick >> 8u);
    record[AUTOSAVE_TRACE_VALUE_OFFSET] = (uint8_t)value;
    record[AUTOSAVE_TRACE_VALUE_OFFSET + 1u] = (uint8_t)(value >> 8u);
    record[AUTOSAVE_TRACE_VALUE_OFFSET + 2u] = (uint8_t)(value >> 16u);
    record[AUTOSAVE_TRACE_VALUE_OFFSET + 3u] = (uint8_t)(value >> 24u);
    autosave_trace_write_cursor = (uint16_t)(cursor + 1u);

    /*
     * Keep all configured slots usable. A producer that laps the durable-flush cursor
     * discards only the oldest record, advances that cursor with it, and leaves
     * an explicit saturated loss witness rather than silently masking overflow.
     */
    if (autosaveTrace_pendingCountUnsafe() > AUTOSAVE_TRACE_RECORD_COUNT) {
        autosave_trace_flush_cursor = (uint16_t)(
            autosave_trace_write_cursor - AUTOSAVE_TRACE_RECORD_COUNT);
        if (autosave_trace_dropped != UINT16_MAX)
            autosave_trace_dropped++;
    }
    autosaveTrace_irqRestore(primask);
}

uint16_t autosaveTrace_pendingCount(void)
{
    uint32_t primask = autosaveTrace_irqSave();
    uint16_t count = autosaveTrace_pendingCountUnsafe();

    autosaveTrace_irqRestore(primask);
    return count > AUTOSAVE_TRACE_RECORD_COUNT ?
        AUTOSAVE_TRACE_RECORD_COUNT : count;
}

uint8_t autosaveTrace_peekRecord(uint16_t index,
                                 uint8_t out[AUTOSAVE_TRACE_RECORD_BYTES])
{
    uint32_t primask;
    uint16_t cursor;
    uint8_t i;

    if (!out)
        return 0u;
    primask = autosaveTrace_irqSave();
    if (index >= autosaveTrace_pendingCountUnsafe()) {
        autosaveTrace_irqRestore(primask);
        return 0u;
    }
    cursor = (uint16_t)(autosave_trace_flush_cursor + index);
    for (i = 0u; i < AUTOSAVE_TRACE_RECORD_BYTES; i++) {
        out[i] = autosave_trace_records[
            cursor % AUTOSAVE_TRACE_RECORD_COUNT][i];
    }
    autosaveTrace_irqRestore(primask);
    return 1u;
}

void autosaveTrace_advanceFlushCursor(uint16_t count)
{
    uint32_t primask = autosaveTrace_irqSave();
    uint16_t pending = autosaveTrace_pendingCountUnsafe();

    /* Never acknowledge more records than remain after an ISR-side overflow. */
    if (count > pending)
        count = pending;
    autosave_trace_flush_cursor = (uint16_t)(
        autosave_trace_flush_cursor + count);
    autosaveTrace_irqRestore(primask);
}

uint16_t autosaveTrace_droppedCount(void)
{
    uint32_t primask = autosaveTrace_irqSave();
    uint16_t dropped = autosave_trace_dropped;

    autosaveTrace_irqRestore(primask);
    return dropped;
}

#else

/* Production stubs retain no trace storage and cannot initiate trace I/O. */
void autosaveTrace_record(autosave_trace_stage_t stage, uint8_t flags,
                          uint32_t value)
{
    (void)stage;
    (void)flags;
    (void)value;
}

uint16_t autosaveTrace_pendingCount(void) { return 0u; }

uint8_t autosaveTrace_peekRecord(uint16_t index,
                                 uint8_t out[AUTOSAVE_TRACE_RECORD_BYTES])
{
    (void)index;
    (void)out;
    return 0u;
}

void autosaveTrace_advanceFlushCursor(uint16_t count) { (void)count; }
uint16_t autosaveTrace_droppedCount(void) { return 0u; }

#endif /* DEV_MODE_LOGGING */
