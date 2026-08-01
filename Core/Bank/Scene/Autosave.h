/*
 * Autosave.h -- fixed working-Bank record layout, live-byte projection, and
 * validation.
 *
 * This module owns the binary wire contract and pure transformations used by
 * the background ping-pong writer. It does not own a filesystem handle,
 * scheduler, retained mask, or write cache. filesystem.c remains the sole
 * AsyncFATFS owner and supplies caller-owned stream/mask/patch storage.
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
#define AUTOSAVE_KIT_OFFSET                   640u
#define AUTOSAVE_KIT_NAME_OFFSET                0u
#define AUTOSAVE_KIT_PARAMETERS_OFFSET          8u
#define AUTOSAVE_KIT_PARAMETER_ALLOC_BYTES    120u
#define AUTOSAVE_KIT_PARAMETER_LIVE_BYTES       2u
#define AUTOSAVE_KIT_INSTRUMENTS_OFFSET        128u

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

/*
 * Format one initial sequential record chunk.
 *
 * Inputs: caller-owned destination interval, generation, Bank restore slot,
 * precomputed CRC, Bank name, and complete HCNAMES rows. Output: deterministic
 * header/slot/name bytes with zero mask, parameters, Effects, and padding.
 * `crc32c` must come from autosave_initialRecordCrc() with identical identity
 * inputs. This helper owns no storage and performs no I/O.
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
 * Calculate the deterministic creation image's CRC32C.
 *
 * Inputs match autosave_formatInitialChunk() except for the resulting CRC.
 * Output is the final Castagnoli value for one complete 34,768-byte baseline.
 * The caller retains only this scalar while filesystem.c streams its chunks.
 */
uint32_t autosave_initialRecordCrc(
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
 * update() accepts one bounded sequential chunk, parses intersecting control
 * and Bank identity cells, and updates CRC32C with stored CRC bytes 12..15
 * treated as zero. finish() accepts only one exact-size, magic/version/commit
 * valid record with a matching checksum. No helper owns I/O or persistent RAM.
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
 * Inspect the complete mutation mask, or read/clear one mutation bit.
 *
 * Inputs: caller-owned 3,856-byte SRAM mask and, for bit operations, a
 * payload-relative offset. Outputs: HasDirty reports whether any cached byte
 * contains a mutation; read returns the least-significant-bit-first state;
 * clear changes exactly one bounded bit. The whole-mask test lets the current
 * temporary file-to-SRAM drain fall through before a needless ping-pong write.
 * These helpers centralize the file/cache bit convention for fixture
 * generation and filesystem drain preparation.
 */
uint8_t autosave_maskHasDirty(const uint8_t mask[AUTOSAVE_MASK_BYTES]);
uint8_t autosave_maskBitIsSet(const uint8_t mask[AUTOSAVE_MASK_BYTES],
                              uint16_t payload_offset);
void autosave_maskBitClear(uint8_t mask[AUTOSAVE_MASK_BYTES],
                           uint16_t payload_offset);

/*
 * Compare two ping-pong generations modulo uint32_t wrap.
 *
 * A nonzero return means candidate is newer than reference; callers resolve
 * equal generations deterministically in favor of Record A.
 */
uint8_t autosave_generationIsNewer(uint32_t candidate, uint32_t reference);

/*
 * Transform one record chunk for parameter-drain CRC or output.
 *
 * Inputs: source chunk/absolute range, next header values, caller-owned updated
 * mask, sorted payload offset/value patches, and a caller-owned patch cursor.
 * Output: header intersections are replaced, all intersecting mask bytes come
 * from `updated_mask`, captured live payload values are substituted, and every
 * other payload byte remains copy-forwarded. Reset `*patch_cursor` to zero
 * before each complete record pass so CRC and output consume identical bytes.
 */
void autosave_transformDrainChunk(
    uint8_t *chunk,
    uint32_t absolute_offset,
    uint16_t byte_count,
    uint32_t generation,
    uint8_t probe_counter,
    uint32_t crc32c,
    uint8_t commit_value,
    const uint8_t updated_mask[AUTOSAVE_MASK_BYTES],
    const uint16_t *patch_offsets,
    const uint8_t *patch_values,
    uint16_t patch_count,
    uint16_t *patch_cursor);

/*
 * Incrementally calculate a record CRC from caller-owned chunks.
 *
 * Stored CRC bytes are always treated as zero. Begin returns the unfinalized
 * accumulator, update returns its next value, and finish returns the stored
 * CRC32C. Affiliates: candidate validation and transformed target CRC pass.
 */
uint32_t autosave_recordCrcBegin(void);
uint32_t autosave_recordCrcUpdate(uint32_t crc32c,
                                  uint32_t absolute_offset,
                                  const uint8_t *src,
                                  uint16_t byte_count);
uint32_t autosave_recordCrcFinish(uint32_t crc32c);

/*
 * Validate one complete in-memory record.
 *
 * Input is an exact 34,768-byte image. Output is nonzero only for a valid
 * header/commit/full-record CRC. This host/simple helper uses the same
 * streaming implementation as filesystem.c and owns no retained buffer.
 */
uint8_t autosave_validateRecord(const uint8_t *record, uint32_t record_bytes);

#endif /* AUTOSAVE_H_ */
