/*
 * Autosave.c -- binary working-Bank format, live payload projection, and
 * CRC32C validation.
 *
 * This module maps explicit BankData/SceneData/InstrumentManager fields into
 * the fixed record without copying C structs or querying DSP runtime state.
 * It also provides pure caller-owned mask/chunk transformations. Filesystem
 * scheduling, SD I/O, the retained mutation mask, and patch cache remain
 * exclusively owned by filesystem.c.
 */
#include "Autosave.h"

#include "BankData.h"
#include "SceneData.h"
#include "InstrumentManager.h"

#include <string.h>

_Static_assert(INSTRUMENT_PARAM_COUNT <=
                   AUTOSAVE_INSTRUMENT_PARAMETER_BYTES,
               "autosave endpoint allocation must cover descriptor storage");
_Static_assert(INSTRUMENT_SLOT_COUNT == AUTOSAVE_INSTRUMENTS_PER_KIT,
               "autosave Kit geometry must match resident Instrument slots");
_Static_assert(SCENE_COUNT == AUTOSAVE_SCENE_COUNT,
               "autosave Scene geometry must match resident Scene count");

/* Return one little-endian byte after the owning field selected its width. */
static uint8_t autosave_u16Byte(uint16_t value, uint8_t byte_index)
{
    return (uint8_t)(value >> ((uint16_t)byte_index * 8u));
}

/* Return one little-endian byte after the owning field selected its width. */
static uint8_t autosave_u32Byte(uint32_t value, uint8_t byte_index)
{
    return (uint8_t)(value >> ((uint32_t)byte_index * 8u));
}

/*
 * Return one zero-padded display-name cell without temporary storage.
 *
 * Inputs: an eight-cell HCNAMES/BankData display field and byte index. Output:
 * embedded printable characters are retained while trailing spaces/NULs and
 * non-printable cells become zero. Affiliates: initial formatting and runtime
 * Bank-identity validation, which must normalize names identically.
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
 * Resolve one byte of a deterministic name-only baseline record.
 *
 * Inputs: absolute record offset, generation, Bank slot, CRC, Bank name, and
 * complete HCNAMES image. Output: the exact byte written during boot creation
 * or no-valid-record recovery. Mask/parameter/Effect/padding cells remain zero
 * by design; this milestone does not claim that a regenerated baseline is a
 * complete Bank snapshot.
 */
static uint8_t autosave_initialRecordByte(
    uint32_t record_offset,
    uint32_t generation,
    uint16_t bank_slot,
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
            /* New baselines begin with the diagnostic writer witness at zero. */
            return 0u;
        default:
            return 0u;
        }
    }

    if (record_offset >= AUTOSAVE_BANK_SLOT_OFFSET &&
        record_offset < AUTOSAVE_BANK_SLOT_OFFSET + 2u) {
        return autosave_u16Byte(
            bank_slot,
            (uint8_t)(record_offset - AUTOSAVE_BANK_SLOT_OFFSET));
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
            uint16_t name_row = (uint16_t)(
                AUTOSAVE_HCNAMES_INSTRUMENT_BASE +
                ((uint16_t)scene * AUTOSAVE_INSTRUMENTS_PER_KIT) +
                instrument);
            uint32_t instrument_name_offset = scene_offset +
                AUTOSAVE_KIT_OFFSET + AUTOSAVE_KIT_INSTRUMENTS_OFFSET +
                ((uint32_t)instrument * AUTOSAVE_INSTRUMENT_RECORD_BYTES) +
                AUTOSAVE_INSTRUMENT_NAME_OFFSET;

            if (record_offset >= instrument_name_offset &&
                record_offset <
                    instrument_name_offset + AUTOSAVE_NAME_BYTES) {
                return autosave_nameByte(
                    resident_names[name_row],
                    (uint8_t)(record_offset - instrument_name_offset));
            }
        }
    }
    return 0u;
}

/* CRC32C (Castagnoli, reflected) update with no table or retained RAM. */
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
    /* CRC32C starts inverted; callers retain this scalar between SD chunks. */
    return 0xffffffffu;
}

uint32_t autosave_recordCrcUpdate(uint32_t crc32c,
                                  uint32_t absolute_offset,
                                  const uint8_t *src,
                                  uint16_t byte_count)
{
    uint16_t i;

    /*
     * Apply the wire-level CRC exception in one place.
     *
     * Inputs: running accumulator and one record interval. Output: updated
     * accumulator with stored CRC bytes logically zero. Why: validation,
     * initial formatting, and transformed copy-forward must never diverge on
     * the checksum's self-reference rule.
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

uint32_t autosave_initialRecordCrc(
    uint32_t generation,
    uint16_t bank_slot,
    const char bank_name[AUTOSAVE_NAME_BYTES],
    const char resident_names[AUTOSAVE_HCNAMES_ROW_COUNT]
                             [AUTOSAVE_HCNAMES_ROW_BYTES])
{
    uint32_t crc = autosave_recordCrcBegin();
    uint32_t offset;

    /*
     * Calculate the complete creation image before streaming it.
     *
     * The CRC input byte is resolved with crc32c zero, so bytes 12..15 never
     * self-reference. Output covers all 34,768 header/mask/payload bytes while
     * retaining only one accumulator and one byte of local storage.
     */
    for (offset = 0u; offset < AUTOSAVE_RECORD_BYTES; offset++) {
        uint8_t value = autosave_initialRecordByte(
            offset, generation, bank_slot, 0u, bank_name, resident_names);

        crc = autosave_recordCrcUpdate(crc, offset, &value, 1u);
    }
    return autosave_recordCrcFinish(crc);
}

void autosave_formatInitialChunk(
    uint8_t *dst,
    uint32_t absolute_offset,
    uint16_t byte_count,
    uint32_t generation,
    uint16_t bank_slot,
    uint32_t crc32c,
    const char bank_name[AUTOSAVE_NAME_BYTES],
    const char resident_names[AUTOSAVE_HCNAMES_ROW_COUNT]
                             [AUTOSAVE_HCNAMES_ROW_BYTES])
{
    uint32_t chunk_end;
    uint16_t i;

    /*
     * Recreate one bounded interval without a record-sized buffer.
     *
     * Inputs are the identity/control values used for the precomputed CRC.
     * Output is deterministic for any accepted subrange. Affiliates:
     * filesystem boot creation and no-valid-record recovery writers.
     */
    if (!dst || byte_count == 0u || absolute_offset >= AUTOSAVE_RECORD_BYTES)
        return;
    chunk_end = absolute_offset + byte_count;
    if (chunk_end > AUTOSAVE_RECORD_BYTES)
        byte_count = (uint16_t)(AUTOSAVE_RECORD_BYTES - absolute_offset);

    for (i = 0u; i < byte_count; i++) {
        dst[i] = autosave_initialRecordByte(
            absolute_offset + i, generation, bank_slot, crc32c,
            bank_name, resident_names);
    }
}

void autosave_streamValidationBegin(autosave_stream_validation_t *state)
{
    /*
     * Initialize one caller-owned candidate pass.
     *
     * Output begins optimistic and is invalidated by malformed, discontinuous,
     * short, or overlong input. No hidden state survives between A and B.
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
     * Parse one strictly sequential candidate interval.
     *
     * Inputs must begin exactly at bytes_seen and remain within the fixed
     * record. Output accumulates header/Bank identity and CRC. Discontinuity
     * fails closed rather than accepting a reordered or partially sought file.
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

        if (record_offset >= AUTOSAVE_BANK_SLOT_OFFSET &&
            record_offset < AUTOSAVE_BANK_SLOT_OFFSET + 2u) {
            state->bank_slot |= (uint16_t)value <<
                ((record_offset - AUTOSAVE_BANK_SLOT_OFFSET) * 8u);
        } else if (record_offset >= AUTOSAVE_BANK_NAME_OFFSET &&
                   record_offset <
                       AUTOSAVE_BANK_NAME_OFFSET + AUTOSAVE_NAME_BYTES) {
            state->bank_name[record_offset - AUTOSAVE_BANK_NAME_OFFSET] =
                (char)value;
        }
    }
    state->crc32c = autosave_recordCrcUpdate(
        state->crc32c, absolute_offset, src, byte_count);
    state->bytes_seen += byte_count;
}

uint8_t autosave_streamValidationFinish(
    const autosave_stream_validation_t *state)
{
    /*
     * Accept only after the exact final byte and CRC have arrived.
     *
     * This prevents a plausible header/early commit marker from accepting a
     * torn or old-size file before its fixed-length checksum is proven.
     */
    if (!state || !state->header_valid ||
        state->bytes_seen != AUTOSAVE_RECORD_BYTES) {
        return 0u;
    }
    return (uint8_t)(autosave_recordCrcFinish(state->crc32c) ==
                     state->stored_crc32c);
}

uint8_t autosave_streamValidationMatchesBank(
    const autosave_stream_validation_t *state,
    uint16_t bank_slot,
    const char bank_name[AUTOSAVE_NAME_BYTES])
{
    uint8_t i;

    /*
     * Bind one validated candidate to the current resident Bank.
     *
     * Inputs are the parsed little-endian slot/name and BankData identity.
     * Output rejects either mismatch before copy-forward. Name normalization is
     * shared with baseline creation so display-space padding cannot disagree.
     */
    if (!state || state->bank_slot != bank_slot)
        return 0u;
    for (i = 0u; i < AUTOSAVE_NAME_BYTES; i++) {
        if ((uint8_t)state->bank_name[i] != autosave_nameByte(bank_name, i))
            return 0u;
    }
    return 1u;
}

/*
 * Project one existing Scene setting into its ordered binary parameter index.
 *
 * Inputs: retained Scene and index 0..39. Output: one byte and success.
 * Affiliates: sceneset.scg's identical logical field order and the public live
 * payload getter below. No C struct layout is serialized.
 */
static uint8_t autosave_getSceneParameter(const scene_t *scene,
                                          uint8_t parameter_index,
                                          uint8_t *value)
{
    if (!scene || !value ||
        parameter_index >= AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES) {
        return 0u;
    }
    if (parameter_index == 0u) {
        *value = scene->settings.morph_amount;
    } else if (parameter_index < 7u) {
        *value = scene->settings.voice_morph_amount[parameter_index - 1u];
    } else if (parameter_index == 7u) {
        *value = scene->settings.voice_decimation_all;
    } else if (parameter_index < 14u) {
        *value = scene->settings.audio_out[parameter_index - 8u];
    } else if (parameter_index < 20u) {
        *value = scene->settings.fx_send_amount[parameter_index - 14u];
    } else if (parameter_index < 26u) {
        *value = scene->settings.fader_setting[parameter_index - 20u];
    } else if (parameter_index < 33u) {
        *value = scene->settings.midi_channel[parameter_index - 26u];
    } else {
        *value = scene->settings.midi_note[parameter_index - 33u];
    }
    return 1u;
}

uint8_t autosave_getLivePayloadByte(uint16_t payload_offset, uint8_t *value)
{
    uint16_t relative;
    uint8_t scene_index;
    const scene_t *scene;

    /*
     * Map Bank-owned scalar bytes before entering the repeated Scene geometry.
     *
     * Inputs are payload-relative positions 0..127. Outputs use explicit
     * little-endian BankData getters; Bank/HCNAMES name cells and reserved
     * padding deliberately report nonexistent to this parameter-only drain.
     */
    if (!value || payload_offset >= AUTOSAVE_PAYLOAD_BYTES)
        return 0u;
    if (payload_offset < AUTOSAVE_BANK_SECTION_BYTES) {
        uint16_t bank_value;

        if (payload_offset < 2u) {
            *value = autosave_u16Byte(
                bank_restoreBankSlot(), (uint8_t)payload_offset);
            return 1u;
        }
        if (payload_offset >= 10u && payload_offset < 12u) {
            bank_value = bank_scenePresentMask();
            *value = autosave_u16Byte(
                bank_value, (uint8_t)(payload_offset - 10u));
            return 1u;
        }
        if (payload_offset == 12u) {
            *value = bank_activeSceneSlot();
            return 1u;
        }
        if (payload_offset >= 13u && payload_offset < 15u) {
            bank_value = bank_sceneMaskVoiceEdit();
            *value = autosave_u16Byte(
                bank_value, (uint8_t)(payload_offset - 13u));
            return 1u;
        }
        return 0u;
    }

    relative = (uint16_t)(payload_offset - AUTOSAVE_BANK_SECTION_BYTES);
    scene_index = (uint8_t)(relative / AUTOSAVE_SCENE_SECTION_BYTES);
    relative = (uint16_t)(relative % AUTOSAVE_SCENE_SECTION_BYTES);

    /*
     * An absent Scene has no logical parameter owner even though SceneData
     * retains initialized storage for every slot.
     *
     * Input is BankData's presence mask. Output closes any fixture/stale dirty
     * bit for an unavailable Scene without sampling defaults that are not part
     * of the resident Bank.
     */
    if (scene_index >= AUTOSAVE_SCENE_COUNT ||
        !bank_scenePresent(scene_index)) {
        return 0u;
    }
    scene = scene_getConst(scene_index);
    if (!scene)
        return 0u;

    if (relative >= AUTOSAVE_SCENE_PARAMETERS_OFFSET &&
        relative < AUTOSAVE_SCENE_PARAMETERS_OFFSET +
                   AUTOSAVE_SCENE_PARAMETER_ALLOC_BYTES) {
        return autosave_getSceneParameter(
            scene,
            (uint8_t)(relative - AUTOSAVE_SCENE_PARAMETERS_OFFSET),
            value);
    }

    /*
     * Effect bytes and Scene name/padding have no live parameter source in
     * this milestone.
     *
     * They intentionally return nonexistent. Pattern is outside the wire
     * layout entirely, so no relative range can reach scene->pattern.
     */
    if (relative < AUTOSAVE_KIT_OFFSET)
        return 0u;

    relative = (uint16_t)(relative - AUTOSAVE_KIT_OFFSET);
    if (relative >= AUTOSAVE_KIT_PARAMETERS_OFFSET &&
        relative < AUTOSAVE_KIT_PARAMETERS_OFFSET +
                   AUTOSAVE_KIT_PARAMETER_ALLOC_BYTES) {
        uint8_t kit_parameter =
            (uint8_t)(relative - AUTOSAVE_KIT_PARAMETERS_OFFSET);

        /*
         * Only the two slot-6/track-7 endpoint bytes currently exist.
         *
         * Inputs: Kit parameter indices 0..119. Output: indices 0/1 sample the
         * retained normal/Morph decay; later reserved indices report absent.
         */
        if (kit_parameter == 0u) {
            *value = scene->kit.settings.slot6_track7_amp_envelope_decay;
            return 1u;
        }
        if (kit_parameter == 1u) {
            *value =
                scene->kit.settings.slot6_track7_morph_amp_envelope_decay;
            return 1u;
        }
        return 0u;
    }

    if (relative >= AUTOSAVE_KIT_INSTRUMENTS_OFFSET) {
        uint16_t instrument_relative =
            (uint16_t)(relative - AUTOSAVE_KIT_INSTRUMENTS_OFFSET);
        uint8_t instrument_index = (uint8_t)(
            instrument_relative / AUTOSAVE_INSTRUMENT_RECORD_BYTES);
        uint8_t field_offset = (uint8_t)(
            instrument_relative % AUTOSAVE_INSTRUMENT_RECORD_BYTES);
        const kit_instrument_slot_t *instrument;
        const instrument_registry_entry_t *entry;
        uint8_t descriptor_index;

        if (instrument_index >= AUTOSAVE_INSTRUMENTS_PER_KIT)
            return 0u;
        instrument = &scene->kit.instruments[instrument_index];
        entry = instrumentManager_registryEntry(instrument->type);
        if (!entry || !entry->type_text)
            return 0u;

        /*
         * The type token is resident registry metadata, not HCNAMES.
         *
         * Inputs: Instrument-relative bytes 0..2. Output: the registered
         * lowercase three-character type text required to interpret both
         * endpoint images. Unknown types were rejected above.
         */
        if (field_offset < AUTOSAVE_INSTRUMENT_TYPE_BYTES) {
            if (entry->type_text[field_offset] == '\0')
                return 0u;
            *value = (uint8_t)entry->type_text[field_offset];
            return 1u;
        }

        /*
         * Normal endpoint cells map directly by descriptor/enum index.
         *
         * Inputs: the fixed 72-byte normal allocation. Output: a byte only
         * when the live type owns that descriptor index; reserve cells beyond
         * descriptor_count report nonexistent without reading array padding.
         */
        if (field_offset >= AUTOSAVE_INSTRUMENT_NORMAL_OFFSET &&
            field_offset < AUTOSAVE_INSTRUMENT_NORMAL_OFFSET +
                           AUTOSAVE_INSTRUMENT_PARAMETER_BYTES) {
            descriptor_index = (uint8_t)(
                field_offset - AUTOSAVE_INSTRUMENT_NORMAL_OFFSET);
            if (descriptor_index >= entry->descriptor_count ||
                descriptor_index >= INSTRUMENT_PARAM_COUNT) {
                return 0u;
            }
            *value = instrument->parameter_images
                .instrument_parameters[descriptor_index];
            return 1u;
        }

        /*
         * Morph endpoint cells use the same descriptor index and flag policy
         * as the Instrument text writer's [morph] section.
         *
         * Inputs: fixed Morph cell plus registry metadata. Output: retained
         * Morph endpoint only for an in-range Morphable descriptor. Selector
         * rows and reserve cells report nonexistent; derived interpolation is
         * never stored.
         */
        if (field_offset >= AUTOSAVE_INSTRUMENT_MORPH_OFFSET &&
            field_offset < AUTOSAVE_INSTRUMENT_MORPH_OFFSET +
                           AUTOSAVE_INSTRUMENT_PARAMETER_BYTES) {
            descriptor_index = (uint8_t)(
                field_offset - AUTOSAVE_INSTRUMENT_MORPH_OFFSET);
            if (descriptor_index >= entry->descriptor_count ||
                descriptor_index >= INSTRUMENT_PARAM_COUNT ||
                (entry->descriptors[descriptor_index].flags &
                 INSTRUMENT_PARAM_FLAG_MORPHABLE) == 0u) {
                return 0u;
            }
            *value = instrument->parameter_images
                .morph_instrument_parameters[descriptor_index];
            return 1u;
        }
    }
    return 0u;
}

uint8_t autosave_maskBitIsSet(const uint8_t mask[AUTOSAVE_MASK_BYTES],
                              uint16_t payload_offset)
{
    /*
     * Decode the fixed least-significant-bit-first mask convention.
     *
     * Inputs: caller-owned mask and payload offset. Output: zero for null/out
     * of range or the one addressed bit. No neighbor or caller state changes.
     */
    if (!mask || payload_offset >= AUTOSAVE_PAYLOAD_BYTES)
        return 0u;
    return (uint8_t)((mask[payload_offset >> 3u] >>
                      (payload_offset & 7u)) & 1u);
}

void autosave_maskBitClear(uint8_t mask[AUTOSAVE_MASK_BYTES],
                           uint16_t payload_offset)
{
    /*
     * Close exactly one bounded dirty position.
     *
     * Inputs: mutable caller-owned mask and payload offset. Output: the
     * addressed least-significant-bit-first cell is clear; all neighbors are
     * preserved. Affiliates: filesystem's capped drain preparation.
     */
    if (!mask || payload_offset >= AUTOSAVE_PAYLOAD_BYTES)
        return;
    mask[payload_offset >> 3u] = (uint8_t)(
        mask[payload_offset >> 3u] &
        (uint8_t)~(uint8_t)(1u << (payload_offset & 7u)));
}

uint8_t autosave_generationIsNewer(uint32_t candidate, uint32_t reference)
{
    /* Two records cannot advance by half the uint32_t space between scans. */
    return (uint8_t)((int32_t)(candidate - reference) > 0);
}

/*
 * Apply only the control-header fields owned by the target transaction.
 *
 * Inputs: one mutable chunk and next generation/probe/CRC/commit. Output:
 * intersecting header cells are replaced; mask and payload remain untouched
 * for the drain-aware wrapper below.
 */
static void autosave_transformHeaderChunk(uint8_t *chunk,
                                          uint32_t absolute_offset,
                                          uint16_t byte_count,
                                          uint32_t generation,
                                          uint8_t probe_counter,
                                          uint32_t crc32c,
                                          uint8_t commit_value)
{
    uint16_t i;

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
    uint16_t *patch_cursor)
{
    uint32_t chunk_end;
    uint32_t mask_begin;
    uint32_t mask_end;
    uint16_t cursor;

    /*
     * Produce the exact target bytes from a streamed winner interval.
     *
     * Inputs: immutable cached drain result plus one source chunk. Output:
     * updated header, complete intersecting mask bytes, and captured live
     * payload patches. Why: both CRC and output passes call this same function,
     * so later live edits cannot make the checksummed image differ from disk.
     */
    if (!chunk || byte_count == 0u ||
        absolute_offset >= AUTOSAVE_RECORD_BYTES ||
        byte_count > AUTOSAVE_RECORD_BYTES - absolute_offset ||
        !updated_mask || !patch_cursor ||
        (patch_count > 0u && (!patch_offsets || !patch_values))) {
        return;
    }

    chunk_end = absolute_offset + byte_count;
    autosave_transformHeaderChunk(
        chunk, absolute_offset, byte_count, generation, probe_counter,
        crc32c, commit_value);

    mask_begin = (absolute_offset > AUTOSAVE_MASK_OFFSET)
        ? absolute_offset : AUTOSAVE_MASK_OFFSET;
    mask_end = (chunk_end <
                AUTOSAVE_MASK_OFFSET + AUTOSAVE_MASK_BYTES)
        ? chunk_end : AUTOSAVE_MASK_OFFSET + AUTOSAVE_MASK_BYTES;
    if (mask_begin < mask_end) {
        memcpy(chunk + (mask_begin - absolute_offset),
               updated_mask + (mask_begin - AUTOSAVE_MASK_OFFSET),
               mask_end - mask_begin);
    }

    /*
     * Consume the sorted patch list monotonically for this complete pass.
     *
     * Input cursor is reset by filesystem.c before CRC and copy streams.
     * Output cursor advances past patches below/inside the current chunk while
     * patches above it remain for later chunks.
     */
    cursor = *patch_cursor;
    while (cursor < patch_count) {
        uint32_t patch_record_offset =
            AUTOSAVE_PAYLOAD_OFFSET + patch_offsets[cursor];

        if (patch_record_offset >= chunk_end)
            break;
        if (patch_record_offset >= absolute_offset) {
            chunk[patch_record_offset - absolute_offset] =
                patch_values[cursor];
        }
        cursor++;
    }
    *patch_cursor = cursor;
}

uint8_t autosave_validateRecord(const uint8_t *record, uint32_t record_bytes)
{
    autosave_stream_validation_t state;

    /*
     * Keep whole-image verification on the runtime streaming implementation.
     *
     * Input is one exact record image. Output is the same header/CRC decision
     * used for A/B selection, without Bank identity matching.
     */
    if (!record || record_bytes != AUTOSAVE_RECORD_BYTES)
        return 0u;
    autosave_streamValidationBegin(&state);
    autosave_streamValidationUpdate(
        &state, 0u, record, (uint16_t)record_bytes);
    return autosave_streamValidationFinish(&state);
}
