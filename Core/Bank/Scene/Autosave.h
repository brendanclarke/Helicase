/*
 * Autosave.h -- fixed working-Bank record layout, live-byte projection, and
 * validation.
 *
 * This module owns the binary wire contract, the single retained dirty mask,
 * and transformations used by the background ping-pong writer. It does not
 * own a filesystem handle, scheduler, or transaction patch cache;
 * filesystem.c remains the sole AsyncFATFS owner and supplies caller-owned
 * stream/patch storage.
 */
#ifndef AUTOSAVE_H_
#define AUTOSAVE_H_

#include <stdint.h>

/* Hidden root filenames selected for the two ping-pong records. */
#define AUTOSAVE_RECORD_A_FILENAME ".hcprms1"
#define AUTOSAVE_RECORD_B_FILENAME ".hcprms2"
#define AUTOSAVE_RECORD_FILE_COUNT 2u

/*
 * The first 64 bytes are record-control data rather than mutable Bank payload.
 *
 * Inputs/outputs: validation and writer transforms address these absolute
 * offsets. Why: generation, checksum, and final commit state must never share
 * mutation bits with musical parameters. Affiliates: Autosave.c CRC handling
 * and filesystem.c's commit-last transaction.
 */
#define AUTOSAVE_HEADER_BYTES                64u
#define AUTOSAVE_HEADER_MAGIC_OFFSET          0u
#define AUTOSAVE_HEADER_VERSION_OFFSET        4u
#define AUTOSAVE_HEADER_COMMIT_OFFSET         5u
#define AUTOSAVE_HEADER_GENERATION_OFFSET     8u
#define AUTOSAVE_HEADER_CRC32C_OFFSET        12u
/* One-byte writer witness; generation, not this wrapping byte, selects A/B. */
#define AUTOSAVE_HEADER_PROBE_COUNTER_OFFSET 16u
#define AUTOSAVE_HEADER_FORMAT_VERSION        1u
#define AUTOSAVE_HEADER_COMMIT_VALID        0xa5u

/*
 * Fixed parameter-record geometry.
 *
 * The 3,856-byte mutation mask has one bit for each byte in the 30,848-byte
 * payload. Inputs are the requested 128-byte Bank plus sixteen 1,920-byte
 * Scenes. Output is one exact 34,768-byte wire record including the 64-byte
 * header. Initial creation clears every bit and parameter byte; runtime drain
 * copy-forwards the mask while clearing only sampled/nonexistent positions.
 */
#define AUTOSAVE_MASK_BYTES                3856u
#define AUTOSAVE_BANK_SECTION_BYTES         128u
#define AUTOSAVE_SCENE_COUNT                 16u
#define AUTOSAVE_SCENE_SECTION_BYTES       1920u
#define AUTOSAVE_EFFECT_SECTION_BYTES       512u
#define AUTOSAVE_KIT_SECTION_BYTES         1280u
#define AUTOSAVE_INSTRUMENTS_PER_KIT          6u
#define AUTOSAVE_INSTRUMENT_RECORD_BYTES    192u
#define AUTOSAVE_INSTRUMENT_PARAMETER_BYTES  72u
#define AUTOSAVE_PAYLOAD_BYTES \
    (AUTOSAVE_BANK_SECTION_BYTES + \
     (AUTOSAVE_SCENE_COUNT * AUTOSAVE_SCENE_SECTION_BYTES))
#define AUTOSAVE_RECORD_BYTES \
    (AUTOSAVE_HEADER_BYTES + AUTOSAVE_MASK_BYTES + AUTOSAVE_PAYLOAD_BYTES)

#define AUTOSAVE_NAME_BYTES                   8u
#define AUTOSAVE_HCNAMES_ROW_COUNT           129u
#define AUTOSAVE_HCNAMES_ROW_BYTES             9u

/* HCNAMES' fixed Bank / Scene / Kit / Instrument row ownership. */
#define AUTOSAVE_HCNAMES_SCENE_BASE            1u
#define AUTOSAVE_HCNAMES_KIT_BASE \
    (AUTOSAVE_HCNAMES_SCENE_BASE + AUTOSAVE_SCENE_COUNT)
#define AUTOSAVE_HCNAMES_INSTRUMENT_BASE \
    (AUTOSAVE_HCNAMES_KIT_BASE + AUTOSAVE_SCENE_COUNT)

/*
 * Absolute top-level offsets.
 *
 * The payload begins after the header and its one-bit-per-byte mask. Mask bit
 * N describes payload-relative byte N, never absolute record byte N.
 */
#define AUTOSAVE_MASK_OFFSET                  AUTOSAVE_HEADER_BYTES
#define AUTOSAVE_PAYLOAD_OFFSET \
    (AUTOSAVE_MASK_OFFSET + AUTOSAVE_MASK_BYTES)
#define AUTOSAVE_BANK_OFFSET                  AUTOSAVE_PAYLOAD_OFFSET
#define AUTOSAVE_SCENES_OFFSET \
    (AUTOSAVE_BANK_OFFSET + AUTOSAVE_BANK_SECTION_BYTES)

/*
 * Bank section fields, expressed as absolute record offsets.
 *
 * The restore slot is little-endian. Scene-present and VOICE-edit masks are
 * also little-endian; each byte remains independently addressable by the
 * mutation register even though the logical value is uint16_t.
 */
#define AUTOSAVE_BANK_SLOT_OFFSET \
    (AUTOSAVE_BANK_OFFSET + 0u)
#define AUTOSAVE_BANK_NAME_OFFSET \
    (AUTOSAVE_BANK_OFFSET + 2u)
#define AUTOSAVE_BANK_SCENE_PRESENT_MASK_OFFSET \
    (AUTOSAVE_BANK_OFFSET + 10u)
#define AUTOSAVE_BANK_ACTIVE_SCENE_OFFSET \
    (AUTOSAVE_BANK_OFFSET + 12u)
#define AUTOSAVE_BANK_VOICE_EDIT_MASK_OFFSET \
    (AUTOSAVE_BANK_OFFSET + 13u)

/*
 * One Scene's relative regions and explicit parameter allocation.
 *
 * Scene parameters occupy bytes 8..127, of which indices 0..39 currently
 * exist. Effects reserve 512 bytes without a live owner. Kit begins at 640 so
 * its 128-byte header plus six 192-byte Instruments ends exactly at 1,920.
 */
#define AUTOSAVE_SCENE_NAME_OFFSET              0u
#define AUTOSAVE_SCENE_PARAMETERS_OFFSET        8u
#define AUTOSAVE_SCENE_PARAMETER_ALLOC_BYTES  120u
#define AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES    40u
#define AUTOSAVE_EFFECT_OFFSET                128u
#define AUTOSAVE_EFFECT_TYPE_OFFSET             0u
#define AUTOSAVE_EFFECT_NAME_OFFSET             1u
#define AUTOSAVE_EFFECT_PARAMETERS_OFFSET        9u
#define AUTOSAVE_EFFECT_PARAMETER_ALLOC_BYTES  503u
#define AUTOSAVE_EFFECT_PARAM_COUNT              0u
#define AUTOSAVE_KIT_OFFSET                   640u
#define AUTOSAVE_KIT_NAME_OFFSET                0u
#define AUTOSAVE_KIT_PARAMETERS_OFFSET          8u
#define AUTOSAVE_KIT_PARAMETER_ALLOC_BYTES    120u
#define AUTOSAVE_KIT_PARAMETER_LIVE_BYTES       2u
#define AUTOSAVE_KIT_INSTRUMENTS_OFFSET        128u

/*
 * Format-owned identifiers for every currently live scalar parameter domain.
 *
 * What: Bank fields select variable-width ranges, while Scene and Kit values
 * select one byte in their ordered allocations; Effect deliberately has a
 * zero live count. Why: retained owners must never repeat wire offsets, and a
 * newly added parameter must extend the same count used by both its getter and
 * dirty marker. Inputs/outputs: owner setters pass these identifiers to the
 * marker API below; Autosave converts them to canonical mask bits. Affiliates:
 * BankData, SceneData, Preset, and autosave_getLivePayloadByte().
 *
 * Future-owner rule: a new Bank field needs a field/width mapping and getter;
 * a new Scene or Kit parameter is appended before its COUNT and written only
 * through its owner setter; a future Effect parameter raises the zero live
 * count, adds retained ownership/getter logic, and uses the Effect marker.
 */
typedef enum {
    AUTOSAVE_BANK_FIELD_RESTORE_SLOT = 0,
    AUTOSAVE_BANK_FIELD_DISPLAY_NAME,
    AUTOSAVE_BANK_FIELD_SCENE_PRESENT_MASK,
    AUTOSAVE_BANK_FIELD_ACTIVE_SCENE,
    AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK,
    AUTOSAVE_BANK_FIELD_COUNT
} autosave_bank_field_t;

typedef enum {
    AUTOSAVE_SCENE_PARAM_MORPH_AMOUNT = 0,
    AUTOSAVE_SCENE_PARAM_VOICE_MORPH_BASE = 1,
    AUTOSAVE_SCENE_PARAM_DECIMATION_ALL = 7,
    AUTOSAVE_SCENE_PARAM_AUDIO_OUT_BASE = 8,
    AUTOSAVE_SCENE_PARAM_FX_SEND_BASE = 14,
    AUTOSAVE_SCENE_PARAM_FADER_BASE = 20,
    AUTOSAVE_SCENE_PARAM_MIDI_CHANNEL_BASE = 26,
    AUTOSAVE_SCENE_PARAM_MIDI_NOTE_BASE = 33,
    AUTOSAVE_SCENE_PARAM_COUNT = 40
} autosave_scene_parameter_t;

typedef enum {
    AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_DECAY = 0,
    AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_MORPH_DECAY,
    AUTOSAVE_KIT_PARAM_COUNT
} autosave_kit_parameter_t;

/*
 * One Instrument's fixed 192-byte relative layout.
 *
 * Type text and name are followed by separate descriptor-indexed normal and
 * Morph images. Indices without a live descriptor (and non-Morphable Morph
 * indices) are reserved cells rather than compacting later descriptors.
 */
#define AUTOSAVE_INSTRUMENT_TYPE_OFFSET          0u
#define AUTOSAVE_INSTRUMENT_TYPE_BYTES           3u
#define AUTOSAVE_INSTRUMENT_NAME_OFFSET          3u
#define AUTOSAVE_INSTRUMENT_NORMAL_OFFSET       11u
#define AUTOSAVE_INSTRUMENT_MORPH_OFFSET \
    (AUTOSAVE_INSTRUMENT_NORMAL_OFFSET + \
     AUTOSAVE_INSTRUMENT_PARAMETER_BYTES)
#define AUTOSAVE_INSTRUMENT_PADDING_OFFSET \
    (AUTOSAVE_INSTRUMENT_MORPH_OFFSET + \
     AUTOSAVE_INSTRUMENT_PARAMETER_BYTES)

_Static_assert(AUTOSAVE_MASK_BYTES * 8u == AUTOSAVE_PAYLOAD_BYTES,
               "autosave mask must cover exactly the Bank/Scene payload");
_Static_assert(AUTOSAVE_PAYLOAD_BYTES == 30848u,
               "autosave payload size is a fixed wire contract");
_Static_assert(AUTOSAVE_RECORD_BYTES == 34768u,
               "autosave record size is a fixed wire contract");
_Static_assert(AUTOSAVE_PAYLOAD_BYTES <= UINT16_MAX,
               "autosave patch offsets must fit uint16_t");
_Static_assert(AUTOSAVE_HEADER_CRC32C_OFFSET + 4u <= AUTOSAVE_HEADER_BYTES,
               "record validation fields must fit inside the control header");
_Static_assert(AUTOSAVE_HEADER_PROBE_COUNTER_OFFSET < AUTOSAVE_HEADER_BYTES,
               "autosave probe must remain inside the control header");
_Static_assert(AUTOSAVE_SCENE_PARAMETERS_OFFSET +
                   AUTOSAVE_SCENE_PARAMETER_ALLOC_BYTES ==
                   AUTOSAVE_EFFECT_OFFSET,
               "Scene parameter allocation must end at Effects");
_Static_assert(AUTOSAVE_EFFECT_OFFSET + AUTOSAVE_EFFECT_SECTION_BYTES ==
                   AUTOSAVE_KIT_OFFSET,
               "Effects reserve must end at Kit");
_Static_assert(AUTOSAVE_EFFECT_PARAMETERS_OFFSET +
                   AUTOSAVE_EFFECT_PARAMETER_ALLOC_BYTES ==
                   AUTOSAVE_EFFECT_SECTION_BYTES,
               "Effect type/name/parameter reserve must fill Effects");
_Static_assert(AUTOSAVE_EFFECT_PARAM_COUNT <=
                   AUTOSAVE_EFFECT_PARAMETER_ALLOC_BYTES,
               "live Effect parameters must fit their reserved cells");
_Static_assert(AUTOSAVE_KIT_OFFSET + AUTOSAVE_KIT_SECTION_BYTES ==
                   AUTOSAVE_SCENE_SECTION_BYTES,
               "Kit must end at the Scene boundary");
_Static_assert(AUTOSAVE_KIT_INSTRUMENTS_OFFSET +
                   (AUTOSAVE_INSTRUMENTS_PER_KIT *
                    AUTOSAVE_INSTRUMENT_RECORD_BYTES) ==
                   AUTOSAVE_KIT_SECTION_BYTES,
               "one Kit must end after its sixth Instrument");
_Static_assert(AUTOSAVE_INSTRUMENT_PADDING_OFFSET <=
                   AUTOSAVE_INSTRUMENT_RECORD_BYTES,
               "Instrument type/name/endpoint images must fit its record");
_Static_assert(AUTOSAVE_HCNAMES_INSTRUMENT_BASE +
                   (AUTOSAVE_SCENE_COUNT * AUTOSAVE_INSTRUMENTS_PER_KIT) ==
                   AUTOSAVE_HCNAMES_ROW_COUNT,
               "autosave name mapping must consume all HCNAMES rows");
_Static_assert(AUTOSAVE_SCENE_PARAM_COUNT ==
                   AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES,
               "Scene parameter identifiers must match live getter bytes");
_Static_assert(AUTOSAVE_SCENE_PARAM_COUNT <=
                   AUTOSAVE_SCENE_PARAMETER_ALLOC_BYTES,
               "live Scene parameters must fit their reserved cells");
_Static_assert(AUTOSAVE_KIT_PARAM_COUNT ==
                   AUTOSAVE_KIT_PARAMETER_LIVE_BYTES,
               "Kit parameter identifiers must match live getter bytes");
_Static_assert(AUTOSAVE_KIT_PARAM_COUNT <=
                   AUTOSAVE_KIT_PARAMETER_ALLOC_BYTES,
               "live Kit parameters must fit their reserved cells");

/*
 * Format one initial sequential record chunk.
 *
 * Inputs: caller-owned destination interval, generation, Bank restore slot,
 * precomputed CRC, Bank name, and complete HCNAMES rows. Output: deterministic
 * header/slot/name bytes with zero mask, parameters, Effects, and padding.
 * `crc32c` must come from a completed autosave_initialRecordCrcUpdate()
 * sequence with identical identity inputs. This helper owns no storage and
 * performs no I/O.
 */
void autosave_formatInitialChunk(
    uint8_t *dst,
    uint32_t absolute_offset,
    uint16_t byte_count,
    uint32_t generation,
    uint16_t bank_slot,
    uint32_t crc32c,
    const char bank_name[AUTOSAVE_NAME_BYTES],
    const char resident_names[AUTOSAVE_HCNAMES_ROW_COUNT]
                             [AUTOSAVE_HCNAMES_ROW_BYTES]);

/*
 * Update the deterministic creation image's CRC32C by one bounded interval.
 *
 * Inputs are the in-progress accumulator, next absolute record offset, at
 * most AUTOSAVE_CRC_BYTES_PER_TICK bytes, and the same identity inputs used by
 * autosave_formatInitialChunk(). Output is the next unfinalized accumulator;
 * callers finalize it only after the cursor reaches AUTOSAVE_RECORD_BYTES.
 * Why: initial creation and no-valid-record recovery must synthesize their CRC
 * without scanning a complete record in one foreground pass. Oversized input
 * is clamped here as a defensive boundary, while filesystem.c advances its
 * cursor only by its independently bounded request. This helper owns no I/O,
 * record buffer, or retained state.
 */
uint32_t autosave_initialRecordCrcUpdate(
    uint32_t crc32c,
    uint32_t absolute_offset,
    uint16_t byte_count,
    uint32_t generation,
    uint16_t bank_slot,
    const char bank_name[AUTOSAVE_NAME_BYTES],
    const char resident_names[AUTOSAVE_HCNAMES_ROW_COUNT]
                             [AUTOSAVE_HCNAMES_ROW_BYTES]);

/*
 * Caller-owned state for validating one sequential record stream.
 *
 * It retains only parsed header/Bank identity and a CRC accumulator. The
 * filesystem writer places it in its operation stage, so validating either
 * 34,768-byte candidate never requires a record-sized SRAM image.
 */
typedef struct {
    uint32_t crc32c;
    uint32_t stored_crc32c;
    uint32_t generation;
    uint32_t bytes_seen;
    char bank_name[AUTOSAVE_NAME_BYTES];
    uint16_t bank_slot;
    uint8_t probe_counter;
    uint8_t header_valid;
} autosave_stream_validation_t;

/*
 * Begin/update/finish a contiguous record validation stream.
 *
 * update() accepts only one bounded sequential chunk, parses intersecting
 * control and Bank identity cells, and updates CRC32C with stored CRC bytes
 * 12..15 treated as zero. An oversized update is rejected as invalid rather
 * than allowing a caller to hide unbounded CRC work. finish() accepts only one
 * exact-size, magic/version/commit valid record with a matching checksum. No
 * helper owns I/O or persistent RAM.
 */
void autosave_streamValidationBegin(autosave_stream_validation_t *state);
void autosave_streamValidationUpdate(autosave_stream_validation_t *state,
                                     uint32_t absolute_offset,
                                     const uint8_t *src,
                                     uint16_t byte_count);
uint8_t autosave_streamValidationFinish(
    const autosave_stream_validation_t *state);

/*
 * Match a finished record to the current resident Bank identity.
 *
 * Inputs: parsed record, live restore slot, and BankData's display name.
 * Output: nonzero only when both the little-endian slot and normalized
 * zero-padded name match. Affiliates: background winner selection and initial
 * creation's identical normalization.
 */
uint8_t autosave_streamValidationMatchesBank(
    const autosave_stream_validation_t *state,
    uint16_t bank_slot,
    const char bank_name[AUTOSAVE_NAME_BYTES]);

/*
 * Read one logical live payload byte by its wire offset.
 *
 * Input is payload-relative 0..30,847 plus caller-owned output storage.
 * Output 1 supplies an existing Bank/Scene/Kit/Instrument byte; output 0 means
 * the cell has no live parameter owner in this milestone and its dirty bit may
 * be closed without a get. Bank identity includes the restore slot and
 * normalized BankData display name, so an already-dirty Bank-name cell samples
 * the current in-system name without borrowing HCNAMES. The helper never reads
 * HCNAMES, Pattern, Effects, or derived Morph interpolation and performs no I/O
 * or mutation.
 */
uint8_t autosave_getLivePayloadByte(uint16_t payload_offset, uint8_t *value);

/*
 * Control ordinary mutation production, merge recovery work, inspect the
 * canonical mask, or atomically take one mutation bit for classification.
 *
 * Inputs: tracking accepts a boot-lifecycle enable flag; MergeChunk accepts a
 * mask-relative interval streamed from the selected file; take accepts a
 * payload-relative offset. Outputs: enabled owner mutations may set bits,
 * recovery bits are always ORed into the one 3,856-byte SRAM record, HasDirty
 * reports pending work, and take returns/clears one prior bit atomically. Why:
 * boot initialization must not look like user mutation, while foreground
 * classification must not erase an interrupt-side re-dirty. Affiliates:
 * filesystem boot recovery and drain phases 54-56.
 */
void autosave_setMutationTrackingEnabled(uint8_t enabled);
/*
 * Discard pending SRAM work only at a safe autosave transaction boundary.
 *
 * Input: mutation tracking already disabled and no transform consuming the
 * canonical mask. Output: all 3,856 bytes become clean; neither hidden file is
 * opened or changed. Why: OFF/new-session transitions must not carry stale
 * owner bits into a later enable, but clearing beneath an active CRC/copy is
 * forbidden. Affiliates: filesystem_setAutosaveEnabled() and its deferred
 * active-transaction completion path.
 */
void autosave_discardDirtyMask(void);
void autosave_maskMergeChunk(uint16_t mask_byte_offset,
                             const uint8_t *src,
                             uint16_t byte_count);
uint8_t autosave_maskHasDirty(void);
uint8_t autosave_maskBitTake(uint16_t payload_offset);

/*
 * Mark one logical retained parameter dirty without exposing wire arithmetic.
 *
 * Inputs: a format-owned Bank field, Scene/Kit/Effect coordinate, or active
 * Instrument descriptor coordinate. Outputs: when tracking is enabled, the
 * exact gettable payload bit is atomically set; invalid/absent/unimplemented
 * coordinates are no-ops. Instrument Morph marking also requires a Morphable
 * descriptor. Why: setters may run from foreground or timer-reachable paths
 * and must coalesce safely into the single canonical record. Affiliates:
 * BankData, SceneData, Preset, InstrumentManager, and the live-byte getter.
 */
void autosave_markBankFieldDirty(autosave_bank_field_t field);
void autosave_markSceneParameterDirty(uint8_t scene_index,
                                      uint8_t parameter_index);
void autosave_markKitParameterDirty(uint8_t scene_index,
                                    uint8_t parameter_index);
void autosave_markInstrumentNormalParameterDirty(uint8_t scene_index,
                                                  uint8_t slot,
                                                  uint8_t descriptor_index);
void autosave_markInstrumentMorphParameterDirty(uint8_t scene_index,
                                                 uint8_t slot,
                                                 uint8_t descriptor_index);
void autosave_markEffectParameterDirty(uint8_t scene_index,
                                       uint16_t parameter_index);

/*
 * Preserve named dirty scopes for future validated copy/paste and Phase 2
 * whole-object commits; these functions mark data but never copy it.
 *
 * Inputs: destination Scene/slot after a successful future commit. Outputs:
 * currently gettable cells in that scope become dirty. Whole Instrument adds
 * type plus normal/Morph endpoints but not its HCNAMES-owned name; endpoint-
 * only copies require matching types before calling their marker. Kit includes
 * all six Instruments; Scene includes settings, the Effect stub, and Kit.
 * SceneWithPattern is intentionally only the non-Pattern alias until Pattern
 * persistence exists. Affiliates: future copy/paste and load-region hooks.
 */
void autosave_markWholeInstrumentDirty(uint8_t scene_index, uint8_t slot);
void autosave_markInstrumentNormalDirty(uint8_t scene_index, uint8_t slot);
void autosave_markInstrumentMorphDirty(uint8_t scene_index, uint8_t slot);
void autosave_markKitDirty(uint8_t scene_index);
void autosave_markEffectDirty(uint8_t scene_index);
void autosave_markSceneWithoutPatternDirty(uint8_t scene_index);
void autosave_markSceneWithPatternDirty(uint8_t scene_index);
/*
 * Mark the complete currently gettable resident Bank parameter image.
 *
 * Inputs: BankData fields and its present-Scene mask after tracking has been
 * enabled. Output: all live Bank fields plus every present Scene's implemented
 * non-Pattern scope become dirty in the one canonical mask. Why: runtime
 * AutoSave re-enable must capture changes made while tracking was OFF rather
 * than waiting only for later scalar edits. Names remain owned by the existing
 * HCNAMES/baseline identity path. Affiliates: filesystem runtime setup and the
 * whole-region marker family above.
 */
void autosave_markResidentBankDirty(void);

/*
 * Restore captured live offsets after an unsuccessful target transaction.
 *
 * Inputs: the sorted transaction-local patch offsets and valid count. Output:
 * every bounded offset is set again in the canonical mask. Why: classification
 * atomically takes a bit before its get, so an error must restore each
 * successfully captured offset that may have originated only in SRAM. This is
 * rollback for the writer, not the retained-parameter producer. Affiliate:
 * filesystem.c's autosave error close path.
 */
void autosave_maskRestoreCaptured(const uint16_t *payload_offsets,
                                  uint16_t patch_count);

/*
 * Compare two ping-pong generations modulo uint32_t wrap.
 *
 * A nonzero return means candidate is newer than reference; callers resolve
 * equal generations deterministically in favor of Record A.
 */
uint8_t autosave_generationIsNewer(uint32_t candidate, uint32_t reference);

/*
 * Transform one record chunk for the single parameter-drain output stream.
 *
 * Inputs: source chunk/absolute range, next generation/probe, sorted payload
 * offset/value patches, and a caller-owned patch cursor. Output: header
 * intersections describe the prospective final record with zero CRC and valid
 * commit, mask bytes come from the canonical SRAM record, captured payload
 * values are substituted, and every other payload byte remains copy-forwarded.
 * filesystem.c updates CRC from this exact chunk and then physically clears
 * the staged commit byte until post-copy CRC publication is durable.
 */
void autosave_transformDrainChunk(
    uint8_t *chunk,
    uint32_t absolute_offset,
    uint16_t byte_count,
    uint32_t generation,
    uint8_t probe_counter,
    const uint16_t *patch_offsets,
    const uint8_t *patch_values,
    uint16_t patch_count,
    uint16_t *patch_cursor);

/*
 * Incrementally calculate a record CRC from caller-owned chunks.
 *
 * Stored CRC bytes are always treated as zero. Begin returns the unfinalized
 * accumulator, update returns its next value only for a bounded caller chunk,
 * and finish returns the stored CRC32C. Oversized update input is rejected by
 * returning the unchanged accumulator; filesystem.c never advances a cursor
 * beyond its own requested budget. Affiliates: candidate validation, bounded
 * initial-image synthesis, and the single transformed target copy followed by
 * post-copy CRC publication.
 */
uint32_t autosave_recordCrcBegin(void);
uint32_t autosave_recordCrcUpdate(uint32_t crc32c,
                                  uint32_t absolute_offset,
                                  const uint8_t *src,
                                  uint16_t byte_count);
uint32_t autosave_recordCrcFinish(uint32_t crc32c);

#endif /* AUTOSAVE_H_ */
