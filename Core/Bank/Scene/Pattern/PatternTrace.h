/*
 * PatternTrace.h -- bounded DEV-only pattern pool event trace.
 *
 * PatternTrace owns a fixed eight-byte ring and no filesystem state. Sequencer
 * may publish records from the TIM3 timing owner; filesystem.c snapshots and
 * acknowledges them after a durable append. Production builds retain no ring
 * storage and all APIs become no-ops/zero-return stubs.
 */
#ifndef PATTERN_TRACE_H_
#define PATTERN_TRACE_H_

#include <stdint.h>

/*
 * Fixed record layout: type, flags, little-endian tick16, little-endian value32.
 * Inputs/outputs: these offsets define the on-card `pattrace.bin` stream and
 * are shared by PatternTrace.c and filesystem.c without packed structs.
 */
#define PAT_TRACE_TYPE_OFFSET   0u
#define PAT_TRACE_FLAGS_OFFSET  1u
#define PAT_TRACE_TICK_OFFSET   2u
#define PAT_TRACE_VALUE_OFFSET  4u
#define PAT_TRACE_RECORD_BYTES  8u
#define PAT_TRACE_FILENAME      "pattrace.bin"

#ifndef PAT_TRACE_RECORD_COUNT
#define PAT_TRACE_RECORD_COUNT 32u
#endif

/*
 * Trace stage codes.
 *
 * Inputs: producer-side event kind and optional flags/value. Output: a stable
 * one-byte stage in each eight-byte diagnostic record. H identifies a
 * pending-buffer overflow witness; Q/C/F/R/M/G/X identify S067 queue,
 * capacity, fragmentation, relocation, retired gap, and scene-admission
 * outcomes. V/L/D identify S069 reservation repair, repair relocation, and
 * direct-path retention without changing the on-card record geometry.
 */
typedef enum {
    PAT_TRACE_STAGE_PENDING_OVERFLOW = 'H',
    /* S067 queue admission and pool-service diagnostics. */
    PAT_TRACE_STAGE_QUEUE_OVERFLOW = 'Q',
    PAT_TRACE_STAGE_CAPACITY_DROP = 'C',
    PAT_TRACE_STAGE_FRAG_DROP = 'F',
    PAT_TRACE_STAGE_TIER2_RELOC = 'R',
    PAT_TRACE_STAGE_TIER1_GAP = 'M',
    PAT_TRACE_STAGE_GAP_FALLBACK = 'G',
    PAT_TRACE_STAGE_WRONG_SCENE = 'X',
    PAT_TRACE_STAGE_REPAIR_RESERVE = 'V',
    PAT_TRACE_STAGE_REPAIR_RELOC = 'L',
    PAT_TRACE_STAGE_DIRECT_RETAIN = 'D'
} pat_trace_stage_t;

/* Record one timestamped event. Safe to call from TIM3/foreground context. */
void patternTrace_record(pat_trace_stage_t stage, uint8_t flags,
                         uint32_t value);

/*
 * Pack one dropped pending record into a trace event.
 *
 * Inputs: the pending queue identity and raw payload words. Output: one H
 * overflow record with identity in value32 bits 0..15 and payload in bits
 * 16..31. Production builds compile this to a no-op.
 */
void patternTrace_recordOverflow(uint16_t identity, uint16_t payload);

/*
 * Durable-flush ring interface.
 *
 * Inputs: oldest-first index or number of acknowledged records. Outputs:
 * pending count, copied record bytes, cursor advancement, and dropped count.
 * Filesystem.c calls these only from its foreground state machine.
 */
/* Return the bounded number of records not yet acknowledged durable. */
uint16_t patternTrace_pendingCount(void);

/* Copy one pending record by oldest-relative index. */
uint8_t patternTrace_peekRecord(uint16_t index,
                                uint8_t out[PAT_TRACE_RECORD_BYTES]);

/* Acknowledge records whose serialized bytes have passed a sync gate. */
void patternTrace_advanceFlushCursor(uint16_t count);

/* Return the saturated count of records overwritten before durable flush. */
uint16_t patternTrace_droppedCount(void);

#endif /* PATTERN_TRACE_H_ */
