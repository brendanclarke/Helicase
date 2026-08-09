/*
 * AutosaveTrace.h -- bounded SRAM lifecycle trace for the autosave writer.
 *
 * This module owns a fixed-size ring of fixed-width diagnostic records and
 * the cursor bookkeeping that lets filesystem.c drain them to disk. It owns
 * no filesystem handle and performs no I/O. It exists only to show which
 * autosave lifecycle boundary was actually reached during a bench run.
 *
 * Every API is safe to call unconditionally. When DEV_MODE_LOGGING is 0 the
 * implementation supplies no-op/zero-return stubs, so production builds keep
 * no trace SRAM and perform no trace-file I/O while call sites stay simple.
 */
#ifndef AUTOSAVE_TRACE_H_
#define AUTOSAVE_TRACE_H_

#include <stdint.h>

/*
 * Fixed eight-byte record layout. Explicit offsets avoid packed-struct padding
 * questions in the on-card diagnostic stream: stage, flags, little-endian
 * tick16, and little-endian stage-specific value32 respectively.
 */
#define AUTOSAVE_TRACE_STAGE_OFFSET  0u
#define AUTOSAVE_TRACE_FLAGS_OFFSET  1u
#define AUTOSAVE_TRACE_TICK_OFFSET   2u
#define AUTOSAVE_TRACE_VALUE_OFFSET  4u
#define AUTOSAVE_TRACE_RECORD_BYTES  8u

/*
 * Sixty-four records fill filesystem.c's existing 512-byte staging buffer
 * exactly. Keep these values coupled: changing the record size requires a
 * fresh proof that one trace batch still fits that shared buffer.
 */
#define AUTOSAVE_TRACE_RECORD_COUNT  64u
#define AUTOSAVE_TRACE_FILENAME      "asavetrc.bin"

/* One-byte stage codes make a raw trace readable without a separate decoder. */
typedef enum {
    AUTOSAVE_TRACE_STAGE_DIRTY = 'D',
    AUTOSAVE_TRACE_STAGE_SCHEDULED = 'S',
    AUTOSAVE_TRACE_STAGE_ADMITTED = 'A',
    AUTOSAVE_TRACE_STAGE_VALIDATED = 'V',
    AUTOSAVE_TRACE_STAGE_MASK_MERGED = 'M',
    AUTOSAVE_TRACE_STAGE_CAPTURED = 'C',
    AUTOSAVE_TRACE_STAGE_PUBLISHED = 'P',
    AUTOSAVE_TRACE_STAGE_TERMINAL = 'T',
} autosave_trace_stage_t;

/* Append one timestamped stage record without performing filesystem I/O. */
void autosaveTrace_record(autosave_trace_stage_t stage, uint8_t flags,
                          uint32_t value);
/* Return the bounded number of records not yet acknowledged durable. */
uint16_t autosaveTrace_pendingCount(void);
/* Copy one pending record by oldest-relative index; zero reports an invalid index. */
uint8_t autosaveTrace_peekRecord(uint16_t index,
                                 uint8_t out[AUTOSAVE_TRACE_RECORD_BYTES]);
/* Acknowledge only records whose serialized bytes have passed a sync gate. */
void autosaveTrace_advanceFlushCursor(uint16_t count);
/* Return the saturated count of records overwritten before a durable flush. */
uint16_t autosaveTrace_droppedCount(void);

#endif /* AUTOSAVE_TRACE_H_ */
