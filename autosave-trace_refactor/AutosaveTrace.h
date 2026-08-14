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
    /*
     * One whole-Instrument dirty-mark outcome. Its flags and packed value are
     * defined below so a retained trace can distinguish "never requested"
     * from a rejected map/tracking gate even after its many D records wrap.
     */
    AUTOSAVE_TRACE_STAGE_INSTRUMENT_MARK = 'I',
    /*
     * One committed Instrument image's pre-marker gate. Preset emits this
     * after the SceneData assignment so it proves whether the caller requested
     * and invoked the whole-Instrument marker, independently of that marker's
     * own payload-map/tracking outcome record.
     */
    AUTOSAVE_TRACE_STAGE_INSTRUMENT_COMMIT = 'J',
    /*
     * One nested Instrument-menu entry milestone. It is a timing observer for
     * the `kit` label path, not an autosave mutation: Menu supplies the phase
     * in flags and the captured Scene/slot/type coordinate in value.
     */
    AUTOSAVE_TRACE_STAGE_INSTRUMENT_ENTRY = 'N',
    /*
     * One whole-Kit or whole-Scene-without-Pattern dirty-mark outcome,
     * emitted once at the end of autosave_markKitDirty() and
     * autosave_markSceneWithoutPatternDirty() respectively. A single call
     * into either function can emit far more D records (Scene settings plus
     * up to six full Instrument scopes) than the 64-record ring holds, all
     * within one synchronous call with no intervening flush opportunity, so
     * this terminal record is the only evidence guaranteed to survive that
     * wrap. It mirrors AUTOSAVE_TRACE_STAGE_INSTRUMENT_MARK's role for the
     * per-Instrument case. Root Scene Load, root Bank Load (which marks each
     * selected child Scene through the same Scene marker), and root Kit Load
     * all become observable through this one stage rather than requiring a
     * separate summary per call site.
     */
    AUTOSAVE_TRACE_STAGE_LOAD_MARK = 'L',
    AUTOSAVE_TRACE_STAGE_SCHEDULED = 'S',
    AUTOSAVE_TRACE_STAGE_ADMITTED = 'A',
    AUTOSAVE_TRACE_STAGE_VALIDATED = 'V',
    AUTOSAVE_TRACE_STAGE_MASK_MERGED = 'M',
    AUTOSAVE_TRACE_STAGE_CAPTURED = 'C',
    AUTOSAVE_TRACE_STAGE_PUBLISHED = 'P',
    AUTOSAVE_TRACE_STAGE_TERMINAL = 'T',
} autosave_trace_stage_t;

/*
 * Flags for AUTOSAVE_TRACE_STAGE_INSTRUMENT_MARK. The marker sets BASE_VALID
 * only after Scene presence, slot, live type, and descriptor zero resolve;
 * TRACKING_ENABLED snapshots the production gate; ALL_PUBLISHED proves every
 * expected byte reached the canonical dirty-mask funnel. Why: these three
 * independent facts identify the exact rejected boundary without enlarging
 * the fixed on-card record format.
 */
#define AUTOSAVE_TRACE_INSTRUMENT_MARK_FLAG_BASE_VALID        (1u << 0u)
#define AUTOSAVE_TRACE_INSTRUMENT_MARK_FLAG_TRACKING_ENABLED  (1u << 1u)
#define AUTOSAVE_TRACE_INSTRUMENT_MARK_FLAG_ALL_PUBLISHED     (1u << 2u)

/*
 * AUTOSAVE_TRACE_STAGE_INSTRUMENT_MARK value32 layout: Scene in bits 0..3,
 * slot in bits 4..6, expected dirty-byte count in bits 8..15, and accepted
 * dirty-byte count in bits 16..23. Bits 24..31 are reserved as zero. Why:
 * a terminal summary must remain readable in the existing eight-byte trace
 * record while the preceding per-byte D records may have wrapped.
 */
#define AUTOSAVE_TRACE_INSTRUMENT_MARK_SCENE_SHIFT      0u
#define AUTOSAVE_TRACE_INSTRUMENT_MARK_SLOT_SHIFT       4u
#define AUTOSAVE_TRACE_INSTRUMENT_MARK_EXPECTED_SHIFT   8u
#define AUTOSAVE_TRACE_INSTRUMENT_MARK_PUBLISHED_SHIFT  16u

/*
 * Flags for AUTOSAVE_TRACE_STAGE_INSTRUMENT_COMMIT. REQUESTED carries Menu's
 * immutable root-pool-versus-temporary decision into the committed-image
 * witness; CALLED proves Preset actually entered autosave_markWholeInstrumentDirty.
 * Why: an absent I record otherwise cannot distinguish a false provenance gate
 * from a marker that rejected its own map or tracking precondition.
 */
#define AUTOSAVE_TRACE_INSTRUMENT_COMMIT_FLAG_REQUESTED  (1u << 0u)
#define AUTOSAVE_TRACE_INSTRUMENT_COMMIT_FLAG_CALLED     (1u << 1u)

/*
 * AUTOSAVE_TRACE_STAGE_INSTRUMENT_COMMIT value32 layout matches the entry
 * observer: Scene in bits 0..3, slot in bits 4..6, and committed type in bits
 * 8..15. Higher bits remain zero so the two records can be joined without a
 * second diagnostic coordinate format.
 */
#define AUTOSAVE_TRACE_INSTRUMENT_COMMIT_SCENE_SHIFT  0u
#define AUTOSAVE_TRACE_INSTRUMENT_COMMIT_SLOT_SHIFT   4u
#define AUTOSAVE_TRACE_INSTRUMENT_COMMIT_TYPE_SHIFT   8u

/*
 * Low-seven-bit phases for AUTOSAVE_TRACE_STAGE_INSTRUMENT_ENTRY. A phase is
 * recorded only when its foreground request is accepted or its callback is
 * reached; FAILED means that request/callback reported failure. Why: paired
 * request/completion ticks expose which serialized HCNAMES, temporary-save,
 * or index step owns a visible `kit` label delay without adding Menu state.
 */
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_REQUEST           1u
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_HCNAMES_REQUEST   2u
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_HCNAMES_COMPLETE  3u
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_HCNAMES_FLUSH     4u
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_HCNAMES_FLUSHED   5u
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_TEMP_REQUEST      6u
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_TEMP_COMPLETE     7u
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_INDEX_REQUEST     8u
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_INDEX_COMPLETE    9u
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_FLAG_FAILED             (1u << 7u)

/*
 * AUTOSAVE_TRACE_STAGE_INSTRUMENT_ENTRY value32 layout: Scene in bits 0..3,
 * slot in bits 4..6, and registered Instrument type in bits 8..15. Higher
 * bits are zero. Why: each phase must retain its immutable-looking UI
 * coordinate while free encoder or Scene-button input may change Menu state
 * before the next asynchronous callback runs.
 */
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_SCENE_SHIFT  0u
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_SLOT_SHIFT   4u
#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_TYPE_SHIFT   8u

/*
 * Kind codes distinguishing which whole-object marker emitted a LOAD_MARK
 * record. A whole-Scene mark internally calls the whole-Kit mark, so one
 * Scene Load can legitimately emit both a KIT and a SCENE record for the
 * same scene_index; that nesting is intentional and mirrors the existing
 * INSTRUMENT_MARK-inside-Kit relationship rather than being a duplicate.
 */
#define AUTOSAVE_TRACE_LOAD_MARK_KIND_KIT    0u
#define AUTOSAVE_TRACE_LOAD_MARK_KIND_SCENE  1u

/*
 * Flag for AUTOSAVE_TRACE_STAGE_LOAD_MARK. Snapshots the production
 * mutation-tracking gate at the moment marking ran, matching
 * AUTOSAVE_TRACE_INSTRUMENT_MARK_FLAG_TRACKING_ENABLED's role.
 */
#define AUTOSAVE_TRACE_LOAD_MARK_FLAG_TRACKING_ENABLED  (1u << 0u)

/*
 * AUTOSAVE_TRACE_STAGE_LOAD_MARK value32 layout: kind in bits 0..1, Scene
 * index in bits 2..5. Higher bits are reserved as zero. This record proves
 * only that the marker ran for this Scene/kind; per-byte expected/accepted
 * counts for the nested whole-Instrument calls remain in their own I records.
 */
#define AUTOSAVE_TRACE_LOAD_MARK_KIND_SHIFT   0u
#define AUTOSAVE_TRACE_LOAD_MARK_SCENE_SHIFT  2u

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
