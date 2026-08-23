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
 * Retained ring capacity is owned by config.h. Keep a 64-record fallback for
 * consumers that include this header before config.h; filesystem.c drains a
 * larger configured ring in bounded staging-buffer batches.
 */
#ifndef AUTOSAVE_TRACE_RECORD_COUNT
#define AUTOSAVE_TRACE_RECORD_COUNT  64u
#endif
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
    /* Terminal whole-Kit/Scene witness retained after a synchronous D-record burst. */
    AUTOSAVE_TRACE_STAGE_LOAD_MARK = 'L',
    /*
     * H: diagnostic-only Bank-child Scene-name scratch drift witness. The
     * filesystem compares shared op_scene_display_name against the frozen
     * Bank-owned child snapshot immediately before the Scene-name HCNAMES row
     * is published. flags is reserved and always zero. value32 packs the
     * Bank-local child slot in bits 0..7, the snapshot's first display byte in
     * bits 8..15, and the live/drifted value's first display byte in bits
     * 16..23. The HCNAMES write itself always uses the snapshot, so H proves
     * the shared-scratch hazard was observed but cannot itself indicate a
     * corrupted card. Affiliate: S056_NAMES_CORRUPTION.md.
     */
    AUTOSAVE_TRACE_STAGE_NAME_SCRATCH_DRIFT = 'H',
    AUTOSAVE_TRACE_STAGE_SCHEDULED = 'S',
    AUTOSAVE_TRACE_STAGE_ADMITTED = 'A',
    AUTOSAVE_TRACE_STAGE_VALIDATED = 'V',
    AUTOSAVE_TRACE_STAGE_MASK_MERGED = 'M',
    AUTOSAVE_TRACE_STAGE_CAPTURED = 'C',
    AUTOSAVE_TRACE_STAGE_PUBLISHED = 'P',
    AUTOSAVE_TRACE_STAGE_TERMINAL = 'T',
    /*
     * R: Scene-load completion callback entry; W: intentional writer
     * suppression on the Load/Save page; F: trace-flush suppression or
     * append error; G: changed ring dropped-count publication. These stages
     * are records only; they do not alter the production state machines.
     */
    AUTOSAVE_TRACE_STAGE_SCENE_LOAD_COMPLETE = 'R',
    AUTOSAVE_TRACE_STAGE_WRITER_SUPPRESSED = 'W',
    AUTOSAVE_TRACE_STAGE_TRACE_SUPPRESSED = 'F',
    AUTOSAVE_TRACE_STAGE_TRACE_DROPPED = 'G',
    /*
     * X: a foreground-pumped state machine crossed its cooperative stall
     * threshold. This is observation only: the owner may still complete
     * successfully. Flags select the call site; value32 carries its phase,
     * slot, and any site-specific progress coordinate.
     */
    AUTOSAVE_TRACE_STAGE_PHASE_STALL = 'X',
    /*
     * O: one Kit/Scene/Bank/Instrument Save lifecycle checkpoint. It proves
     * request, delete/create result, source staging, and terminal boundaries
     * without adding a new record shape or persistent storage class.
     */
    AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE = 'O',
    /*
     * E: universal error-completion witness. Emitted exactly once, from
     * filesystem_complete() itself, whenever ANY internal filesystem
     * operation (every Save, every Load, every scan, HCNAMES, index rebuild
     * -- not only the four Save paths 'O' otherwise tracks) reaches
     * FS_STATUS_ERROR. This is deliberately the one hook a future failure
     * cannot bypass by occurring in a phase nobody thought to instrument by
     * hand: it fires from the single terminal completion point every state
     * machine in this file funnels through, not from a per-branch call site.
     * Point-specific records ('O' checkpoints, 'X' stalls, the delete-slot
     * reason packed into 'O' DELETE_RESULT) remain more informative when
     * present; this is the backstop that guarantees a trace file always
     * shows at least current_op/op_phase/op_slot for every failure, even one
     * nobody anticipated when writing this header.
     */
    AUTOSAVE_TRACE_STAGE_OPERATION_ERROR = 'E',
    /*
     * Y: RETIRED -- no longer producible. Was a one-shot diagnostic probe
     * for AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_PARENT_FOR_SELF_EXHAUSTED,
     * emitted from filesystem.c whenever that asyncfatfs failure site
     * fired. That failure site's only producer (asyncfatfs.c's
     * AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF/_LOOP) was removed entirely
     * once its "re-scan the parent to re-find myself by identity" premise
     * turned out to be unnecessary -- afatfs_retireObjectNameRun() never
     * needed a live re-scan, only the already-known object identity, which
     * AFATFS_DELETE_TREE_REOPEN_PARENT now uses directly. See that phase's
     * doc comment in asyncfatfs.c. The layout below is kept only so
     * Session 054 rounds 5/6's already-captured card evidence (which used
     * it) stays decodable; the backing filesystem.c call site, its two
     * accessor functions, and the asyncfatfsDeleteTree_t fields it read are
     * all gone, so this stage can never appear in a fresh trace again.
     * flags: the re-scan's candidate count (0..255, saturating). value32:
     * numbered slot in bits 0..9 (0..999 fits), the last directory-kind
     * candidate's near-miss detail in bits 14..15 (bit 14: its SFN entry
     * pointer matched the target; bit 15: its cluster matched; neither bit
     * alone means the recorded target's cluster reappeared under a
     * different directory-entry pointer or vice versa, both clear with a
     * nonzero candidate count means no directory-kind candidate was seen
     * at all that re-scan), the target cluster the re-scan was looking for
     * (truncated to its low 16 bits) in bits 16..31.
     */
    AUTOSAVE_TRACE_STAGE_SCAN_PARENT_DIAG = 'Y',
    /*
     * B: Bank present-mask lifecycle witness for the Session 052 persistence
     * investigation. One retained RAM-only trace point is emitted at the Bank
     * Load metadata commit and at the writer drain's present-mask capture.
     * flags bit 0 selects the site (0 = commit, 1 = drain); value32 packs the
     * resident present mask in bits 16..31 and site-specific data in bits
     * 0..15. Why: scalar D records prove only which payload offsets were
     * marked, not the mask value at the commit/capture boundary.
     */
    AUTOSAVE_TRACE_STAGE_BANK_PRESENT = 'B',
    /*
     * K: unconditional Bank Load/Save completion witness. Bit 0 reports
     * whether the Preset callback observed FS_STATUS_DONE; bit 1 selects
     * Save (1) versus Load (0). The value is the requested Bank slot. This
     * mirrors R for root Scene Load and proves callback reachability even
     * when no later AutoSave or trace-flush record survives to disk.
     */
    AUTOSAVE_TRACE_STAGE_BANK_OP_COMPLETE = 'K',
} autosave_trace_stage_t;

/*
 * X (PHASE_STALL) flags/value layout. Bits 0..2 select the observer; bit 3
 * identifies a stall inside native recursive delete. value32 stores phase in
 * bits 0..7, numbered slot in bits 8..17, and site-specific extra data in
 * bits 18..31.
 */
#define AUTOSAVE_TRACE_PHASE_STALL_SITE_MASK 0x07u
#define AUTOSAVE_TRACE_PHASE_STALL_SITE_DELETE_SLOT 0u
#define AUTOSAVE_TRACE_PHASE_STALL_SITE_BANK_ENTRY 1u
#define AUTOSAVE_TRACE_PHASE_STALL_SITE_DRAIN 2u
#define AUTOSAVE_TRACE_PHASE_STALL_FLAG_IN_NATIVE_DELETE (1u << 3u)
#define AUTOSAVE_TRACE_PHASE_STALL_PHASE_SHIFT 0u
#define AUTOSAVE_TRACE_PHASE_STALL_SLOT_SHIFT 8u
#define AUTOSAVE_TRACE_PHASE_STALL_EXTRA_SHIFT 18u

/*
 * O (SAVE_LIFECYCLE) flags/value layout. Bits 0..1 select element type;
 * bits 2..4 select checkpoint; bit 7 reports a failed result. value32 stores
 * the numbered slot in bits 0..9 and a CREATE_RESULT CRC16 in bits 16..31.
 */
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_MASK 0x03u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_KIT 0u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_SCENE 1u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_BANK 2u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_INSTRUMENT 3u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT 2u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_REQUEST 0u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_DELETE_RESULT 1u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_CREATE_RESULT 2u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SOURCE_STAGED 3u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_FINISH 4u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_FLAG_FAILED (1u << 7u)
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT 0u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CRC16_SHIFT 16u

/* Menu-only branch tags reuse O/REQUEST's otherwise-unused value high word. */
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_MENU_BRANCH_SHIFT 16u

/*
 * DELETE_RESULT-only fields, reusing the same otherwise-unused high word as
 * CRC16/the Menu branch tag above (each checkpoint owns its own high-word
 * meaning). Bits 16-19 carry filesystem.c's fs_delete_slot_reason_t (which
 * of the delete-slot resolver's several distinct failure branches actually
 * fired: scan I/O error, malformed LFN, wrong object kind, duplicate match,
 * the match-count backstop, a rejected native-delete start, a "." open
 * failure, a stall abandonment, or a non-OK native-delete result); bits
 * 20-23 carry the raw asyncfatfs afatfsResultCode_t when the reason is
 * DELETE_RESULT (0 for every other reason); bits 24-31 carry asyncfatfs's
 * own afatfsDeleteTreeFailureSite_e (asyncfatfs.c) -- which of the ~17
 * architecturally distinct checks inside afatfs_deleteTreeContinue()
 * produced that result code -- also only meaningful when the reason is
 * DELETE_RESULT. A DELETE_RESULT record with FAILED clear never sets any
 * of these three sub-fields.
 */
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_DELETE_REASON_SHIFT 16u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_DELETE_DETAIL_SHIFT 20u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_DELETE_SITE_SHIFT   24u

/*
 * E (OPERATION_ERROR) flags/value layout. flags bit 0 is set when
 * filesystem.c's delete-slot resolver had already tagged a specific
 * fs_delete_slot_reason_t for this same failure (cross-reference the most
 * recent 'O' DELETE_RESULT record for that detail rather than duplicating
 * it here). flags bit 1 is set when this record came from
 * filesystem_completeLibraryIndexRebuild() (the deferred `.hcindex`/typed
 * Instrument index rebuild chain every Save path hands off to after its own
 * primary work succeeds) rather than from filesystem_complete() (every
 * operation's primary completion path); the two are separate terminal
 * functions with no shared call site, so a save whose main write succeeds
 * but whose subsequent index rebuild fails is only visible through the
 * bit-1 record. value32: bits 0..7 the fs_internal_op_t current_op that was
 * failing (filesystem.c's own FS_INTERNAL_OP_* enum -- 44 values as of this
 * writing, comfortably under 256; at a bit-1 record this is the rebuild
 * chain's own internal operation, e.g. a scan or index-write step, not
 * necessarily the original Save op that started the chain); bits 8..15 the
 * op_phase reached; bits 16..25 the numbered op_slot in effect (0..999,
 * matching every other packed slot field in this file).
 */
#define AUTOSAVE_TRACE_OPERATION_ERROR_FLAG_DELETE_REASON_SET (1u << 0u)
#define AUTOSAVE_TRACE_OPERATION_ERROR_FLAG_INDEX_REBUILD     (1u << 1u)
#define AUTOSAVE_TRACE_OPERATION_ERROR_OP_SHIFT    0u
#define AUTOSAVE_TRACE_OPERATION_ERROR_PHASE_SHIFT 8u
#define AUTOSAVE_TRACE_OPERATION_ERROR_SLOT_SHIFT  16u

/* Y (SCAN_PARENT_DIAG) value32 layout; flags is the raw seen-count byte. */
#define AUTOSAVE_TRACE_SCAN_PARENT_DIAG_SLOT_SHIFT    0u
#define AUTOSAVE_TRACE_SCAN_PARENT_DIAG_SFN_MATCH     (1u << 14u)
#define AUTOSAVE_TRACE_SCAN_PARENT_DIAG_CLUSTER_MATCH (1u << 15u)
#define AUTOSAVE_TRACE_SCAN_PARENT_DIAG_CLUSTER_SHIFT 16u

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

/* LOAD_MARK kind, tracking flag, and packed value shifts; Scene marks nest Kit marks. */
#define AUTOSAVE_TRACE_LOAD_MARK_KIND_KIT    0u
#define AUTOSAVE_TRACE_LOAD_MARK_KIND_SCENE  1u
#define AUTOSAVE_TRACE_LOAD_MARK_FLAG_TRACKING_ENABLED  (1u << 0u)
#define AUTOSAVE_TRACE_LOAD_MARK_KIND_SHIFT   0u
#define AUTOSAVE_TRACE_LOAD_MARK_SCENE_SHIFT  2u

/*
 * R flags: bit 0 means the Scene completion callback observed DONE. The R
 * value is the callback's immutable destination Scene mask, allowing the
 * callback-entry witness to be compared directly with later L/D/I records.
 */
#define AUTOSAVE_TRACE_SCENE_LOAD_COMPLETE_FLAG_STATUS_DONE (1u << 0u)

/* K flags: bit 0 is terminal DONE; bit 1 distinguishes Save from Load. */
#define AUTOSAVE_TRACE_BANK_OP_COMPLETE_FLAG_STATUS_DONE (1u << 0u)
#define AUTOSAVE_TRACE_BANK_OP_COMPLETE_FLAG_KIND_SAVE    (1u << 1u)

/*
 * F flags: bit 0 means the command-active gate retained pending trace records;
 * bit 1 means a started trace append terminated with ERROR. W uses bit 0 to
 * report that canonical mutation work was dirty while the writer was held by
 * the intentional Load/Save-page cache ownership rule. G carries no flags.
 */
#define AUTOSAVE_TRACE_TRACE_SUPPRESSED_FLAG_COMMAND_ACTIVE (1u << 0u)
#define AUTOSAVE_TRACE_TRACE_SUPPRESSED_FLAG_APPEND_ERROR   (1u << 1u)

/* B flags: bit 0 selects the writer-drain capture site; clear means the Bank
 * Load metadata commit site. */
#define AUTOSAVE_TRACE_BANK_PRESENT_FLAG_DRAIN (1u << 0u)

/* B value32 layout: bits 16..31 hold the resident Bank present mask. At the
 * commit site bits 0..15 hold the effective selected-child load mask; at the
 * drain site they hold the payload offset of the field's first byte (10). */
#define AUTOSAVE_TRACE_BANK_PRESENT_MASK_SHIFT 16u

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
