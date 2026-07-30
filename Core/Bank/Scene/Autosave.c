/*
 * Autosave.c -- pure initial-register formatter and CRC32C validator.
 *
 * The rejected ledger, debounce scheduler, payload codec, and staged load
 * barrier remain absent. This file maps authoritative BankData/HCNAMES names
 * and the fixed control header into caller-owned byte ranges. No static or
 * global storage is introduced: the foreground filesystem operation remains
 * the sole owner of all I/O, buffering, and future write ordering.
 */
#include "Autosave.h"

#include <string.h>

/* Cast only after the field boundary has selected the required byte. */
static uint8_t autosave_u32Byte(uint32_t value, uint8_t byte_index)
{
    return (uint8_t)(value >> ((uint32_t)byte_index * 8u));
}

/*
 * Return one zero-padded display-name cell without allocating a temporary
 * string. HCNAMES and BankData normally space-pad eight cells; the binary
 * register retains embedded printable spaces but converts trailing blanks,
 * NULs, and non-printable cells to zero.
 */
static uint8_t autosave_nameByte(const char name[AUTOSAVE_NAME_BYTES],
                                 uint8_t byte_index)
{
    uint8_t length = AUTOSAVE_NAME_BYTES;

    while (length > 0u &&
           (name == NULL || name[length - 1u] == '\0' ||
            name[length - 1u] == ' ')) {
        length--;
    }
    if (byte_index >= length)
        return 0u;
    if ((uint8_t)name[byte_index] < 0x20u ||
        (uint8_t)name[byte_index] > 0x7eu) {
        return 0u;
    }
    return (uint8_t)name[byte_index];
}

/*
 * Resolve one byte of the deterministic creation image. crc32c is zero while
 * calculating the checksum and the resulting value while streaming it out;
 * this single byte source keeps the firmware formatter and card fixture byte
 * for byte identical without a record-sized staging allocation.
 */
static uint8_t autosave_initialRecordByte(
    uint32_t record_offset,
    uint32_t generation,
    uint32_t crc32c,
    const char bank_name[AUTOSAVE_NAME_BYTES],
    const char resident_names[AUTOSAVE_HCNAMES_ROW_COUNT]
                             [AUTOSAVE_HCNAMES_ROW_BYTES])
{
    uint8_t scene;

    if (record_offset < AUTOSAVE_HEADER_BYTES) {
        switch (record_offset) {
        case AUTOSAVE_HEADER_MAGIC_OFFSET + 0u: return 'H';
        case AUTOSAVE_HEADER_MAGIC_OFFSET + 1u: return 'C';
        case AUTOSAVE_HEADER_MAGIC_OFFSET + 2u: return 'P';
        case AUTOSAVE_HEADER_MAGIC_OFFSET + 3u: return 'R';
        case AUTOSAVE_HEADER_VERSION_OFFSET:
            return AUTOSAVE_HEADER_FORMAT_VERSION;
        case AUTOSAVE_HEADER_COMMIT_OFFSET:
            return AUTOSAVE_HEADER_COMMIT_VALID;
        case AUTOSAVE_HEADER_GENERATION_OFFSET + 0u:
        case AUTOSAVE_HEADER_GENERATION_OFFSET + 1u:
        case AUTOSAVE_HEADER_GENERATION_OFFSET + 2u:
        case AUTOSAVE_HEADER_GENERATION_OFFSET + 3u:
            return autosave_u32Byte(
                generation,
                (uint8_t)(record_offset - AUTOSAVE_HEADER_GENERATION_OFFSET));
        case AUTOSAVE_HEADER_CRC32C_OFFSET + 0u:
        case AUTOSAVE_HEADER_CRC32C_OFFSET + 1u:
        case AUTOSAVE_HEADER_CRC32C_OFFSET + 2u:
        case AUTOSAVE_HEADER_CRC32C_OFFSET + 3u:
            return autosave_u32Byte(
                crc32c,
                (uint8_t)(record_offset - AUTOSAVE_HEADER_CRC32C_OFFSET));
        case AUTOSAVE_HEADER_PROBE_COUNTER_OFFSET:
            /* New baseline records begin with the writer witness at zero. */
            return 0u;
        default:
            return 0u;
        }
    }

    if (record_offset >= AUTOSAVE_BANK_NAME_OFFSET &&
        record_offset < AUTOSAVE_BANK_NAME_OFFSET + AUTOSAVE_NAME_BYTES) {
        return autosave_nameByte(
            bank_name, (uint8_t)(record_offset - AUTOSAVE_BANK_NAME_OFFSET));
    }

    if (!resident_names)
        return 0u;

    for (scene = 0u; scene < AUTOSAVE_SCENE_COUNT; scene++) {
        uint32_t scene_offset = AUTOSAVE_SCENES_OFFSET +
            ((uint32_t)scene * AUTOSAVE_SCENE_SECTION_BYTES);
        uint32_t kit_name_offset = scene_offset + AUTOSAVE_KIT_OFFSET +
            AUTOSAVE_KIT_NAME_OFFSET;
        uint8_t instrument;

        if (record_offset >= scene_offset + AUTOSAVE_SCENE_NAME_OFFSET &&
            record_offset < scene_offset + AUTOSAVE_SCENE_NAME_OFFSET +
                            AUTOSAVE_NAME_BYTES) {
            return autosave_nameByte(
                resident_names[AUTOSAVE_HCNAMES_SCENE_BASE + scene],
                (uint8_t)(record_offset - scene_offset -
                          AUTOSAVE_SCENE_NAME_OFFSET));
        }
        if (record_offset >= kit_name_offset &&
            record_offset < kit_name_offset + AUTOSAVE_NAME_BYTES) {
            return autosave_nameByte(
                resident_names[AUTOSAVE_HCNAMES_KIT_BASE + scene],
                (uint8_t)(record_offset - kit_name_offset));
        }
        for (instrument = 0u;
             instrument < AUTOSAVE_INSTRUMENTS_PER_KIT;
             instrument++) {
            uint16_t name_row = (uint16_t)(AUTOSAVE_HCNAMES_INSTRUMENT_BASE +
                ((uint16_t)scene * AUTOSAVE_INSTRUMENTS_PER_KIT) + instrument);
            uint32_t instrument_name_offset = scene_offset +
                AUTOSAVE_KIT_OFFSET + AUTOSAVE_KIT_INSTRUMENTS_OFFSET +
                ((uint32_t)instrument * AUTOSAVE_INSTRUMENT_RECORD_BYTES) +
                AUTOSAVE_INSTRUMENT_NAME_OFFSET;

            if (record_offset >= instrument_name_offset &&
                record_offset < instrument_name_offset + AUTOSAVE_NAME_BYTES) {
                return autosave_nameByte(
                    resident_names[name_row],
                    (uint8_t)(record_offset - instrument_name_offset));
            }
        }
    }
    return 0u;
}

/* CRC32C (Castagnoli, reflected) update with no lookup table or retained RAM. */
static uint32_t autosave_crc32cUpdate(uint32_t crc, uint8_t value)
{
    uint8_t bit;

    crc ^= value;
    for (bit = 0u; bit < 8u; bit++) {
        crc = (crc & 1u) ? ((crc >> 1u) ^ 0x82f63b78u) : (crc >> 1u);
    }
    return crc;
}

uint32_t autosave_recordCrcBegin(void)
{
    /* CRC32C starts inverted; callers keep this scalar across SD chunks. */
    return 0xffffffffu;
}

uint32_t autosave_recordCrcUpdate(uint32_t crc32c,
                                  uint32_t absolute_offset,
                                  const uint8_t *src,
                                  uint16_t byte_count)
{
    uint16_t i;

    /*
     * Apply the one wire-level CRC exception centrally. Every caller sees the
     * stored CRC field as zero, preventing validation and copy-forward writes
     * from accidentally using different self-reference rules.
     */
    if (!src)
        return crc32c;
    for (i = 0u; i < byte_count; i++) {
        uint32_t record_offset = absolute_offset + i;
        uint8_t value = src[i];

        if (record_offset >= AUTOSAVE_HEADER_CRC32C_OFFSET &&
            record_offset < AUTOSAVE_HEADER_CRC32C_OFFSET + 4u) {
            value = 0u;
        }
        crc32c = autosave_crc32cUpdate(crc32c, value);
    }
    return crc32c;
}

uint32_t autosave_recordCrcFinish(uint32_t crc32c)
{
    return ~crc32c;
}

/*
 * Calculate the creation image's complete CRC32C. The CRC field is supplied
 * as zero to its byte resolver, preventing a self-referential checksum while
 * still covering magic, version, commit marker, generation, mask, names,
 * parameters, and padding.
 */
uint32_t autosave_initialRecordCrc(
    uint32_t generation,
    const char bank_name[AUTOSAVE_NAME_BYTES],
    const char resident_names[AUTOSAVE_HCNAMES_ROW_COUNT]
                             [AUTOSAVE_HCNAMES_ROW_BYTES])
{
    uint32_t crc = autosave_recordCrcBegin();
    uint32_t offset;

    for (offset = 0u; offset < AUTOSAVE_RECORD_BYTES; offset++) {
        uint8_t value = autosave_initialRecordByte(offset, generation, 0u,
                                                    bank_name, resident_names);

        crc = autosave_recordCrcUpdate(crc, offset, &value, 1u);
    }
    return autosave_recordCrcFinish(crc);
}

void autosave_formatInitialChunk(
    uint8_t *dst,
    uint32_t absolute_offset,
    uint16_t byte_count,
    uint32_t generation,
    uint32_t crc32c,
    const char bank_name[AUTOSAVE_NAME_BYTES],
    const char resident_names[AUTOSAVE_HCNAMES_ROW_COUNT]
                             [AUTOSAVE_HCNAMES_ROW_BYTES])
{
    uint32_t chunk_end;
    uint16_t i;

    /*
     * The caller streams only bounded ranges from the fixed record. Each range
     * is recreated from the same source rather than retaining an image: this
     * keeps the creation pass inside the existing filesystem memory budget.
     */
    if (!dst || byte_count == 0u || absolute_offset >= AUTOSAVE_RECORD_BYTES)
        return;
    chunk_end = absolute_offset + byte_count;
    if (chunk_end > AUTOSAVE_RECORD_BYTES)
        byte_count = (uint16_t)(AUTOSAVE_RECORD_BYTES - absolute_offset);

    for (i = 0u; i < byte_count; i++) {
        dst[i] = autosave_initialRecordByte(absolute_offset + i, generation,
                                            crc32c, bank_name, resident_names);
    }
}

void autosave_streamValidationBegin(autosave_stream_validation_t *state)
{
    /*
     * Initialize one caller-owned sequential validation pass. `header_valid`
     * starts optimistic and is cleared by a malformed byte, missing required
     * cell, discontinuity, or overlong chunk; no hidden module state survives
     * between the A and B record scans.
     */
    if (!state)
        return;
    memset(state, 0, sizeof(*state));
    state->crc32c = autosave_recordCrcBegin();
    state->header_valid = 1u;
}

void autosave_streamValidationUpdate(autosave_stream_validation_t *state,
                                     uint32_t absolute_offset,
                                     const uint8_t *src,
                                     uint16_t byte_count)
{
    uint16_t i;

    /*
     * Filesystem.c presents accepted reads in strict cursor order. Rejecting a
     * discontinuity here makes an accidental seek/partial-range bug fail
     * validation instead of silently calculating a CRC over a reordered image.
     */
    if (!state || !src || absolute_offset != state->bytes_seen ||
        absolute_offset > AUTOSAVE_RECORD_BYTES ||
        byte_count > AUTOSAVE_RECORD_BYTES - absolute_offset) {
        if (state)
            state->header_valid = 0u;
        return;
    }

    for (i = 0u; i < byte_count; i++) {
        uint32_t record_offset = absolute_offset + i;
        uint8_t value = src[i];

        switch (record_offset) {
        case AUTOSAVE_HEADER_MAGIC_OFFSET + 0u:
            if (value != 'H') state->header_valid = 0u;
            break;
        case AUTOSAVE_HEADER_MAGIC_OFFSET + 1u:
            if (value != 'C') state->header_valid = 0u;
            break;
        case AUTOSAVE_HEADER_MAGIC_OFFSET + 2u:
            if (value != 'P') state->header_valid = 0u;
            break;
        case AUTOSAVE_HEADER_MAGIC_OFFSET + 3u:
            if (value != 'R') state->header_valid = 0u;
            break;
        case AUTOSAVE_HEADER_VERSION_OFFSET:
            if (value != AUTOSAVE_HEADER_FORMAT_VERSION)
                state->header_valid = 0u;
            break;
        case AUTOSAVE_HEADER_COMMIT_OFFSET:
            if (value != AUTOSAVE_HEADER_COMMIT_VALID)
                state->header_valid = 0u;
            break;
        case AUTOSAVE_HEADER_GENERATION_OFFSET + 0u:
        case AUTOSAVE_HEADER_GENERATION_OFFSET + 1u:
        case AUTOSAVE_HEADER_GENERATION_OFFSET + 2u:
        case AUTOSAVE_HEADER_GENERATION_OFFSET + 3u:
            state->generation |= (uint32_t)value <<
                ((record_offset - AUTOSAVE_HEADER_GENERATION_OFFSET) * 8u);
            break;
        case AUTOSAVE_HEADER_CRC32C_OFFSET + 0u:
        case AUTOSAVE_HEADER_CRC32C_OFFSET + 1u:
        case AUTOSAVE_HEADER_CRC32C_OFFSET + 2u:
        case AUTOSAVE_HEADER_CRC32C_OFFSET + 3u:
            state->stored_crc32c |= (uint32_t)value <<
                ((record_offset - AUTOSAVE_HEADER_CRC32C_OFFSET) * 8u);
            break;
        case AUTOSAVE_HEADER_PROBE_COUNTER_OFFSET:
            state->probe_counter = value;
            break;
        default:
            break;
        }

        if (record_offset >= AUTOSAVE_BANK_NAME_OFFSET &&
            record_offset < AUTOSAVE_BANK_NAME_OFFSET + AUTOSAVE_NAME_BYTES) {
            state->bank_name[record_offset - AUTOSAVE_BANK_NAME_OFFSET] =
                (char)value;
        }
    }
    state->crc32c = autosave_recordCrcUpdate(state->crc32c, absolute_offset,
                                              src, byte_count);
    state->bytes_seen += byte_count;
}

uint8_t autosave_streamValidationFinish(
    const autosave_stream_validation_t *state)
{
    /*
     * A record is valid only after its final byte has arrived. This prevents a
     * plausible header and an early commit marker from accepting a torn file
     * before the CRC and fixed-size contract have both been checked.
     */
    if (!state || !state->header_valid ||
        state->bytes_seen != AUTOSAVE_RECORD_BYTES) {
        return 0u;
    }
    return (uint8_t)(autosave_recordCrcFinish(state->crc32c) ==
                     state->stored_crc32c);
}

uint8_t autosave_streamValidationMatchesBankName(
    const autosave_stream_validation_t *state,
    const char bank_name[AUTOSAVE_NAME_BYTES])
{
    uint8_t i;

    /*
     * BankData rows are display-space padded while the register is zero
     * padded. Reuse the formatter's normalization so selection has the exact
     * same identity convention as baseline creation without storing a slot.
     */
    if (!state)
        return 0u;
    for (i = 0u; i < AUTOSAVE_NAME_BYTES; i++) {
        if ((uint8_t)state->bank_name[i] != autosave_nameByte(bank_name, i))
            return 0u;
    }
    return 1u;
}

uint8_t autosave_generationIsNewer(uint32_t candidate, uint32_t reference)
{
    /* Two records cannot advance by half the uint32_t space between scans. */
    return (uint8_t)((int32_t)(candidate - reference) > 0);
}

void autosave_transformChunk(uint8_t *chunk,
                             uint32_t absolute_offset,
                             uint16_t byte_count,
                             uint32_t generation,
                             uint8_t probe_counter,
                             uint32_t crc32c,
                             uint8_t commit_value)
{
    uint16_t i;

    /*
     * The writer reads a winning sector into its existing shared buffer, then
     * calls this in-place transformation before checksum or output. All mask
     * and payload cells are deliberately untouched, allowing the dummy writer
     * to preserve future real mutations without an HCNAMES/cache dependency.
     */
    if (!chunk || absolute_offset >= AUTOSAVE_RECORD_BYTES ||
        byte_count > AUTOSAVE_RECORD_BYTES - absolute_offset) {
        return;
    }
    for (i = 0u; i < byte_count; i++) {
        uint32_t record_offset = absolute_offset + i;

        if (record_offset == AUTOSAVE_HEADER_COMMIT_OFFSET) {
            chunk[i] = commit_value;
        } else if (record_offset >= AUTOSAVE_HEADER_GENERATION_OFFSET &&
                   record_offset < AUTOSAVE_HEADER_GENERATION_OFFSET + 4u) {
            chunk[i] = autosave_u32Byte(
                generation,
                (uint8_t)(record_offset - AUTOSAVE_HEADER_GENERATION_OFFSET));
        } else if (record_offset >= AUTOSAVE_HEADER_CRC32C_OFFSET &&
                   record_offset < AUTOSAVE_HEADER_CRC32C_OFFSET + 4u) {
            chunk[i] = autosave_u32Byte(
                crc32c,
                (uint8_t)(record_offset - AUTOSAVE_HEADER_CRC32C_OFFSET));
        } else if (record_offset == AUTOSAVE_HEADER_PROBE_COUNTER_OFFSET) {
            chunk[i] = probe_counter;
        }
    }
}

uint8_t autosave_validateRecord(const uint8_t *record, uint32_t record_bytes)
{
    autosave_stream_validation_t state;

    /* Keep the host/simple whole-image helper on the runtime streaming path. */
    if (!record || record_bytes != AUTOSAVE_RECORD_BYTES)
        return 0u;
    autosave_streamValidationBegin(&state);
    autosave_streamValidationUpdate(&state, 0u, record,
                                    (uint16_t)record_bytes);
    return autosave_streamValidationFinish(&state);
}
