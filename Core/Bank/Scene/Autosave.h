/*
 * Autosave.h -- fixed working-Bank delta-register layout and validation.
 *
 * This module deliberately owns only the wire layout and pure byte/CRC
 * operations. It has no dirty ledger, scheduler, filesystem handle, reader,
 * or retained workspace. filesystem.c remains the sole AsyncFATFS owner and
 * lends the formatter its existing 512-byte stream buffer plus its existing
 * HCNAMES cache at boot.
 */
#ifndef AUTOSAVE_H_
#define AUTOSAVE_H_

#include <stdint.h>

/* Hidden root filenames selected for the two future ping-pong records. */
#define AUTOSAVE_RECORD_A_FILENAME ".hcprms1"
#define AUTOSAVE_RECORD_B_FILENAME ".hcprms2"
#define AUTOSAVE_RECORD_FILE_COUNT 2u

/*
 * The first 64 bytes are record-control data, not a delta payload. Keeping
 * them before the replacement mask means a later overlay never mistakes a
 * generation, validity marker, or checksum for a Bank parameter change.
 */
#define AUTOSAVE_HEADER_BYTES              64u
#define AUTOSAVE_HEADER_MAGIC_OFFSET        0u
#define AUTOSAVE_HEADER_VERSION_OFFSET      4u
#define AUTOSAVE_HEADER_COMMIT_OFFSET       5u
#define AUTOSAVE_HEADER_GENERATION_OFFSET   8u
#define AUTOSAVE_HEADER_CRC32C_OFFSET      12u
/* One-byte writer witness; generation, not this wrapping byte, selects A/B. */
#define AUTOSAVE_HEADER_PROBE_COUNTER_OFFSET 16u
#define AUTOSAVE_HEADER_FORMAT_VERSION      1u
#define AUTOSAVE_HEADER_COMMIT_VALID      0xa5u

/*
 * Fixed v1 register geometry. The 2,576-byte mask has one bit for every byte
 * in the 20,608-byte Bank/Scene payload that follows it. This creation pass
 * leaves every mask bit clear: names are baseline identity text only and must
 * not overlay the committed Bank until a later writer marks them changed.
 */
#define AUTOSAVE_MASK_BYTES              2576u
#define AUTOSAVE_BANK_SECTION_BYTES       128u
#define AUTOSAVE_SCENE_COUNT               16u
#define AUTOSAVE_SCENE_SECTION_BYTES     1280u
#define AUTOSAVE_PAYLOAD_BYTES \
    (AUTOSAVE_BANK_SECTION_BYTES + \
     (AUTOSAVE_SCENE_COUNT * AUTOSAVE_SCENE_SECTION_BYTES))
#define AUTOSAVE_RECORD_BYTES \
    (AUTOSAVE_HEADER_BYTES + AUTOSAVE_MASK_BYTES + AUTOSAVE_PAYLOAD_BYTES)

#define AUTOSAVE_NAME_BYTES                 8u
#define AUTOSAVE_HCNAMES_ROW_COUNT         129u
#define AUTOSAVE_HCNAMES_ROW_BYTES           9u
#define AUTOSAVE_INSTRUMENTS_PER_KIT          6u
#define AUTOSAVE_INSTRUMENT_RECORD_BYTES    128u

/* HCNAMES' fixed Bank / Scene / Kit / Instrument row ownership. */
#define AUTOSAVE_HCNAMES_SCENE_BASE          1u
#define AUTOSAVE_HCNAMES_KIT_BASE \
    (AUTOSAVE_HCNAMES_SCENE_BASE + AUTOSAVE_SCENE_COUNT)
#define AUTOSAVE_HCNAMES_INSTRUMENT_BASE \
    (AUTOSAVE_HCNAMES_KIT_BASE + AUTOSAVE_SCENE_COUNT)

/* Top-level absolute offsets. The Bank payload no longer contains a slot. */
#define AUTOSAVE_MASK_OFFSET                AUTOSAVE_HEADER_BYTES
#define AUTOSAVE_BANK_OFFSET \
    (AUTOSAVE_MASK_OFFSET + AUTOSAVE_MASK_BYTES)
#define AUTOSAVE_BANK_NAME_OFFSET           (AUTOSAVE_BANK_OFFSET + 0u)
#define AUTOSAVE_SCENES_OFFSET \
    (AUTOSAVE_BANK_OFFSET + AUTOSAVE_BANK_SECTION_BYTES)

/* One Scene's fixed internal regions. */
#define AUTOSAVE_SCENE_NAME_OFFSET            0u
#define AUTOSAVE_KIT_OFFSET                  384u
#define AUTOSAVE_KIT_NAME_OFFSET              0u
#define AUTOSAVE_KIT_INSTRUMENTS_OFFSET      128u
#define AUTOSAVE_INSTRUMENT_NAME_OFFSET        3u

_Static_assert(AUTOSAVE_MASK_BYTES * 8u == AUTOSAVE_PAYLOAD_BYTES,
               "autosave mask must cover exactly the Bank/Scene payload");
_Static_assert(AUTOSAVE_RECORD_BYTES == 23248u,
               "autosave register size is a fixed wire contract");
_Static_assert(AUTOSAVE_HEADER_CRC32C_OFFSET + 4u <= AUTOSAVE_HEADER_BYTES,
               "record validation fields must fit inside the control header");
_Static_assert(AUTOSAVE_HEADER_PROBE_COUNTER_OFFSET < AUTOSAVE_HEADER_BYTES,
               "autosave probe must remain inside the control header");
_Static_assert(AUTOSAVE_KIT_OFFSET + AUTOSAVE_KIT_INSTRUMENTS_OFFSET +
                   (AUTOSAVE_INSTRUMENTS_PER_KIT *
                    AUTOSAVE_INSTRUMENT_RECORD_BYTES) ==
                   AUTOSAVE_SCENE_SECTION_BYTES,
               "one Scene must end immediately after its sixth Instrument");
_Static_assert(AUTOSAVE_HCNAMES_INSTRUMENT_BASE +
                   (AUTOSAVE_SCENE_COUNT * AUTOSAVE_INSTRUMENTS_PER_KIT) ==
                   AUTOSAVE_HCNAMES_ROW_COUNT,
               "autosave name mapping must consume all HCNAMES rows");

/*
 * Format one initial sequential file chunk.
 *
 * Inputs: dst is an existing caller-owned stream buffer representing the
 * interval [absolute_offset, absolute_offset + byte_count). generation is
 * the caller-selected ping-pong order; bank_name and resident_names supply
 * the identity cells. Output: every byte in the supplied interval is made
 * deterministic, including magic/version/commit/generation/CRC32C in the
 * header. crc32c must be the result of autosave_initialRecordCrc() for the
 * same generation/names. The function owns no storage and performs no SD I/O.
 */
void autosave_formatInitialChunk(
    uint8_t *dst,
    uint32_t absolute_offset,
    uint16_t byte_count,
    uint32_t generation,
    uint32_t crc32c,
    const char bank_name[AUTOSAVE_NAME_BYTES],
    const char resident_names[AUTOSAVE_HCNAMES_ROW_COUNT]
                             [AUTOSAVE_HCNAMES_ROW_BYTES]);

/*
 * Calculate the creation image's CRC32C exactly once before its chunks are
 * streamed. The caller retains the returned scalar in pre-existing operation
 * scratch; this prevents a repeated whole-record checksum pass for every
 * 512-byte output chunk without allocating a record image.
 */
uint32_t autosave_initialRecordCrc(
    uint32_t generation,
    const char bank_name[AUTOSAVE_NAME_BYTES],
    const char resident_names[AUTOSAVE_HCNAMES_ROW_COUNT]
                             [AUTOSAVE_HCNAMES_ROW_BYTES]);

/*
 * Caller-owned state for validating one sequential record stream.
 *
 * It retains only parsed control/Bank-name cells and a CRC accumulator. The
 * filesystem writer stores this object in its existing per-operation stage
 * workspace, so validating either 23,248-byte record never requires an SRAM
 * image. `crc32c` is the unfinalized Castagnoli accumulator.
 */
typedef struct {
    uint32_t crc32c;
    uint32_t stored_crc32c;
    uint32_t generation;
    uint32_t bytes_seen;
    char bank_name[AUTOSAVE_NAME_BYTES];
    uint8_t probe_counter;
    uint8_t header_valid;
} autosave_stream_validation_t;

/*
 * Begin/update/finish a contiguous record validation stream.
 *
 * update() accepts any bounded chunk at its absolute record offset, parses the
 * header and Bank-name cells it intersects, and applies CRC32C with stored-CRC
 * bytes 12..15 treated as zero. finish() accepts only exactly one complete,
 * magic/version/commit-valid record with a matching final CRC. Neither helper
 * owns I/O, a file handle, or persistent storage.
 */
void autosave_streamValidationBegin(autosave_stream_validation_t *state);
void autosave_streamValidationUpdate(autosave_stream_validation_t *state,
                                     uint32_t absolute_offset,
                                     const uint8_t *src,
                                     uint16_t byte_count);
uint8_t autosave_streamValidationFinish(
    const autosave_stream_validation_t *state);

/*
 * Compare a finished stream's zero-padded Bank payload name with BankData's
 * eight-cell display name. The format intentionally stores no Bank slot, so
 * the background writer uses this identity gate before copy-forwarding a
 * record into the currently resident Bank context.
 */
uint8_t autosave_streamValidationMatchesBankName(
    const autosave_stream_validation_t *state,
    const char bank_name[AUTOSAVE_NAME_BYTES]);

/*
 * Compare two ping-pong generations modulo uint32_t wrap. A nonzero return
 * means candidate is newer than reference; callers resolve equal generations
 * deterministically (the writer keeps Record A because it validates first).
 */
uint8_t autosave_generationIsNewer(uint32_t candidate, uint32_t reference);

/*
 * Transform one caller-owned chunk in place while preserving every non-header
 * byte. It substitutes the future generation/probe/CRC/commit values only at
 * intersecting header offsets; mask and payload bytes remain byte-identical to
 * the selected winner. `commit_value` is zero for the pre-commit copy and
 * AUTOSAVE_HEADER_COMMIT_VALID while calculating the final record CRC.
 */
void autosave_transformChunk(uint8_t *chunk,
                             uint32_t absolute_offset,
                             uint16_t byte_count,
                             uint32_t generation,
                             uint8_t probe_counter,
                             uint32_t crc32c,
                             uint8_t commit_value);

/*
 * Incrementally calculate a record CRC from chunks. It uses the same stored
 * CRC-byte zeroing rule as validation and returns/accepts the unfinalized
 * accumulator so the filesystem can stream a source record without buffering
 * it. Finish with autosave_recordCrcFinish() after the final chunk.
 */
uint32_t autosave_recordCrcBegin(void);
uint32_t autosave_recordCrcUpdate(uint32_t crc32c,
                                  uint32_t absolute_offset,
                                  const uint8_t *src,
                                  uint16_t byte_count);
uint32_t autosave_recordCrcFinish(uint32_t crc32c);

/*
 * Validate a complete in-memory record before it can win a future ping-pong
 * selection. A valid record has this module's magic/version, a commit marker
 * written last by the future writer, and a CRC32C over all 23,248 bytes with
 * the four stored CRC bytes treated as zero. No persistent buffer is owned or
 * allocated by this helper; a future reader may instead use the same rule
 * while streaming from SD.
 */
uint8_t autosave_validateRecord(const uint8_t *record, uint32_t record_bytes);

#endif /* AUTOSAVE_H_ */
