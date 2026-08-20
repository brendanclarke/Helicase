/*
 * Autosave.c -- binary working-Bank format, live payload projection, and
 * CRC32C validation.
 *
 * This module maps explicit BankData/SceneData/InstrumentManager fields into
 * the fixed record without copying C structs or querying DSP runtime state.
 * It owns the one retained mutation mask and transforms streamed chunks from
 * that canonical state. Filesystem scheduling, SD I/O, and transaction-local
 * captured-value patches remain exclusively owned by filesystem.c.
 */
#include "Autosave.h"
/* Supplies DEV_MODE_LOGGING, the trace-ring capacity, and the per-tick CRC cap. */
#include "config.h"
#include "AutosaveTrace.h"

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
/*
 * Bind every named Scene group width to its retained owner arrays.
 *
 * What/why: future parameters must append by extending the shared enum/getter,
 * not shift an existing group away from SceneData. Inputs are compile-time
 * Scene/track/slot counts; output is a build failure on ordering drift.
 * Affiliates: autosave_getSceneParameter() and SceneData scalar setters.
 */
_Static_assert(AUTOSAVE_SCENE_PARAM_DECIMATION_ALL -
                   AUTOSAVE_SCENE_PARAM_VOICE_MORPH_BASE ==
                   INSTRUMENT_SLOT_COUNT,
               "Scene voice Morph group must cover every instrument slot");
_Static_assert(AUTOSAVE_SCENE_PARAM_AUDIO_OUT_BASE ==
                   AUTOSAVE_SCENE_PARAM_DECIMATION_ALL + 1u,
               "Scene audio group must follow decimation");
_Static_assert(AUTOSAVE_SCENE_PARAM_FX_SEND_BASE -
                   AUTOSAVE_SCENE_PARAM_AUDIO_OUT_BASE ==
                   INSTRUMENT_SLOT_COUNT,
               "Scene audio group must cover every instrument slot");
_Static_assert(AUTOSAVE_SCENE_PARAM_FADER_BASE -
                   AUTOSAVE_SCENE_PARAM_FX_SEND_BASE ==
                   INSTRUMENT_SLOT_COUNT,
               "Scene FX-send group must cover every instrument slot");
_Static_assert(AUTOSAVE_SCENE_PARAM_MIDI_CHANNEL_BASE -
                   AUTOSAVE_SCENE_PARAM_FADER_BASE ==
                   INSTRUMENT_SLOT_COUNT,
               "Scene fader group must cover every instrument slot");
_Static_assert(AUTOSAVE_SCENE_PARAM_MIDI_NOTE_BASE -
                   AUTOSAVE_SCENE_PARAM_MIDI_CHANNEL_BASE == NUM_TRACKS,
               "Scene MIDI-channel group must cover every track");
_Static_assert(AUTOSAVE_SCENE_PARAM_COUNT -
                   AUTOSAVE_SCENE_PARAM_MIDI_NOTE_BASE == NUM_TRACKS,
               "Scene MIDI-note group must cover every track");

/*
 * Canonical retained autosave dirty record.
 *
 * What: one bit for every byte in the fixed Bank/Scene payload. Why: live SRAM
 * owns pending work; on-file masks are only recovery copies of incomplete
 * work, so filesystem transactions must not allocate, reset, or replace this
 * record. Inputs are OR-merged winner chunks, later retained-parameter change
 * producers, and rollback of captured offsets after an error. Outputs feed
 * bounded classification, transformed target chunks, and continuation
 * scheduling. Static BSS initialization clears it once at processor reset.
 */
static volatile uint8_t autosave_dirty_mask[AUTOSAVE_MASK_BYTES];
static volatile uint8_t autosave_mutation_tracking_enabled;

_Static_assert(sizeof(autosave_dirty_mask) == AUTOSAVE_MASK_BYTES,
               "autosave canonical dirty record must match the wire mask");

/*
 * Protect one canonical-mask byte without extending the critical section.
 *
 * What: save PRIMASK, disable interrupts, then restore the caller's exact
 * prior interrupt state. Why: retained setters are reachable from sequencer
 * timer work while the foreground writer classifies the same mask. Inputs and
 * outputs: save returns the prior PRIMASK; restore consumes it. Affiliates are
 * the atomic OR and take helpers below; parameter gets, loops, CRC, and SD I/O
 * must remain outside this boundary.
 */
static uint32_t autosave_irqSave(void)
{
    uint32_t primask;

    __asm volatile("mrs %0, primask\ncpsid i"
                   : "=r"(primask) :: "memory");
    return primask;
}

static void autosave_irqRestore(uint32_t primask)
{
    __asm volatile("msr primask, %0" :: "r"(primask) : "memory");
}

/*
 * Atomically OR one set of bits into one canonical mask byte.
 *
 * Inputs: bounded mask-byte index and set-bit pattern. Output: those bits are
 * retained without losing concurrent foreground/interrupt producers. Why:
 * every producer, recovery merge, and rollback has identical OR semantics.
 * The caller performs range checks so this helper stays one-byte and bounded.
 */
static void autosave_maskByteOr(uint16_t mask_byte, uint8_t bits)
{
    uint32_t primask = autosave_irqSave();

    autosave_dirty_mask[mask_byte] = (uint8_t)(
        autosave_dirty_mask[mask_byte] | bits);
    autosave_irqRestore(primask);
}

/*
 * Set one payload bit for an ordinary retained mutation when tracking is live.
 *
 * Inputs: validated payload-relative offset. Output: its LSB-first bit is
 * atomically ORed and this helper returns one, or it returns zero without a
 * change while the tracking/range guard rejects it. Why: initialization/load
 * setup must not manufacture mutations, and owner modules must not know mask
 * geometry. Affiliates: every typed marker below and the filesystem
 * boot-lifecycle tracking gate.
 */
static uint8_t autosave_markPayloadOffsetDirty(uint16_t payload_offset)
{
    if (!autosave_mutation_tracking_enabled ||
        payload_offset >= AUTOSAVE_PAYLOAD_BYTES) {
        return 0u;
    }
    autosave_maskByteOr(
        (uint16_t)(payload_offset >> 3u),
        (uint8_t)(1u << (payload_offset & 7u)));
    /*
     * Record only dirty bits that passed this helper's tracking/range guard.
     * Input is the canonical payload offset just ORed into the retained mask;
     * output is a RAM-only trace point with no scheduler or filesystem side
     * effect. Why: this is the sole scalar dirty-production funnel, so it
     * proves an accepted Phase 1 mutation reached canonical autosave state.
     */
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_DIRTY, 0u,
                         (uint32_t)payload_offset);
    /*
     * Report the accepted publication to compound markers. Why: only this
     * scalar funnel can prove a requested cell survived both tracking and
     * range guards before the canonical mask was changed.
     */
    return 1u;
}

/* Return the validated payload-relative base for one resident Scene. */
static uint8_t autosave_scenePayloadBase(uint8_t scene_index,
                                         uint16_t *payload_base)
{
    if (!payload_base || scene_index >= AUTOSAVE_SCENE_COUNT ||
        !bank_scenePresent(scene_index)) {
        return 0u;
    }
    *payload_base = (uint16_t)(AUTOSAVE_BANK_SECTION_BYTES +
        ((uint16_t)scene_index * AUTOSAVE_SCENE_SECTION_BYTES));
    return 1u;
}

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
     * the checksum's self-reference rule. Oversized input is rejected without
     * advancing any state so an accidental caller cannot conceal unbounded
     * foreground CRC work behind this low-level helper.
     */
    if (!src || byte_count > AUTOSAVE_CRC_BYTES_PER_TICK)
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

uint32_t autosave_crc32cByteUpdate(uint32_t crc32c, uint8_t value)
{
    /* See Autosave.h: this is the raw-byte diagnostic view of the same
     * table-free CRC32C primitive used by the fixed AutoSave record. */
    return autosave_crc32cUpdate(crc32c, value);
}

uint32_t autosave_initialRecordCrcUpdate(
    uint32_t crc32c,
    uint32_t absolute_offset,
    uint16_t byte_count,
    uint32_t generation,
    uint16_t bank_slot,
    const char bank_name[AUTOSAVE_NAME_BYTES],
    const char resident_names[AUTOSAVE_HCNAMES_ROW_COUNT]
                             [AUTOSAVE_HCNAMES_ROW_BYTES])
{
    uint16_t i;

    /*
     * Synthesize only one bounded initial-image interval for the CRC stream.
     *
     * Inputs: the caller-retained accumulator/cursor and immutable creation
     * identity. Output: the accumulator after no more than the configured
     * CPU-work budget. Why: creation and recovery must yield between CRC
     * intervals instead of monopolizing a foreground pass; neither path needs
     * a record-sized image. The stored CRC field is synthesized as zero here,
     * matching autosave_recordCrcUpdate()'s wire exception.
     */
    if (!bank_name || !resident_names ||
        absolute_offset >= AUTOSAVE_RECORD_BYTES)
        return crc32c;
    if (byte_count > AUTOSAVE_CRC_BYTES_PER_TICK)
        byte_count = AUTOSAVE_CRC_BYTES_PER_TICK;
    if (byte_count > AUTOSAVE_RECORD_BYTES - absolute_offset)
        byte_count = (uint16_t)(AUTOSAVE_RECORD_BYTES - absolute_offset);

    for (i = 0u; i < byte_count; i++) {
        uint8_t value = autosave_initialRecordByte(
            absolute_offset + i, generation, bank_slot, 0u,
            bank_name, resident_names);

        crc32c = autosave_recordCrcUpdate(
            crc32c, absolute_offset + i, &value, 1u);
    }
    return crc32c;
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
        byte_count > AUTOSAVE_CRC_BYTES_PER_TICK ||
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
 * Inputs: retained Scene and a named index below AUTOSAVE_SCENE_PARAM_COUNT.
 * Output: one byte and success.
 * Affiliates: sceneset.scg's identical logical field order and the public live
 * payload getter below. No C struct layout is serialized.
 */
static uint8_t autosave_getSceneParameter(const scene_t *scene,
                                          uint8_t parameter_index,
                                          uint8_t *value)
{
    if (!scene || !value ||
        parameter_index >= AUTOSAVE_SCENE_PARAM_COUNT) {
        return 0u;
    }
    if (parameter_index == AUTOSAVE_SCENE_PARAM_MORPH_AMOUNT) {
        *value = scene->settings.morph_amount;
    } else if (parameter_index >= AUTOSAVE_SCENE_PARAM_VOICE_MORPH_BASE &&
               parameter_index < AUTOSAVE_SCENE_PARAM_DECIMATION_ALL) {
        *value = scene->settings.voice_morph_amount[
            parameter_index - AUTOSAVE_SCENE_PARAM_VOICE_MORPH_BASE];
    } else if (parameter_index == AUTOSAVE_SCENE_PARAM_DECIMATION_ALL) {
        *value = scene->settings.voice_decimation_all;
    } else if (parameter_index < AUTOSAVE_SCENE_PARAM_FX_SEND_BASE) {
        *value = scene->settings.audio_out[
            parameter_index - AUTOSAVE_SCENE_PARAM_AUDIO_OUT_BASE];
    } else if (parameter_index < AUTOSAVE_SCENE_PARAM_FADER_BASE) {
        *value = scene->settings.fx_send_amount[
            parameter_index - AUTOSAVE_SCENE_PARAM_FX_SEND_BASE];
    } else if (parameter_index < AUTOSAVE_SCENE_PARAM_MIDI_CHANNEL_BASE) {
        *value = scene->settings.fader_setting[
            parameter_index - AUTOSAVE_SCENE_PARAM_FADER_BASE];
    } else if (parameter_index < AUTOSAVE_SCENE_PARAM_MIDI_NOTE_BASE) {
        *value = scene->settings.midi_channel[
            parameter_index - AUTOSAVE_SCENE_PARAM_MIDI_CHANNEL_BASE];
    } else {
        *value = scene->settings.midi_note[
            parameter_index - AUTOSAVE_SCENE_PARAM_MIDI_NOTE_BASE];
    }
    return 1u;
}

/*
 * Future Effect live-byte owner stub.
 *
 * Inputs: resident Scene, parameter index, and result cell. Output: zero for
 * every request because Phase 1 has no retained Effect owner and the live
 * count is zero. Why: future Effect fields need an explicit getter append
 * point paired with the Effect marker instead of disappearing into generic
 * padding. Affiliates: Effect parameter geometry in Autosave.h, scene_t's
 * future-owner comment, and autosave_markEffectParameterDirty().
 */
static uint8_t autosave_getEffectParameter(const scene_t *scene,
                                           uint16_t parameter_index,
                                           uint8_t *value)
{
    (void)scene;
    (void)parameter_index;
    (void)value;
    return 0u;
}

uint8_t autosave_getLivePayloadByte(uint16_t payload_offset, uint8_t *value)
{
    uint16_t relative;
    uint8_t scene_index;
    const scene_t *scene;

    /*
     * Map every currently existing Bank-owned byte before repeated Scenes.
     *
     * Inputs are payload-relative positions 0..127. Outputs use explicit
     * little-endian BankData getters for the restore/presence/edit masks and
     * the same zero-padding normalization as creation/validation for the live
     * BankData display name. Why: a dirty Bank-name cell must capture an
     * in-system rename without reading HCNAMES, while reserved Bank padding
     * still reports nonexistent and can be closed by the drain.
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
        if (payload_offset >= 2u &&
            payload_offset < 2u + AUTOSAVE_NAME_BYTES) {
            *value = autosave_nameByte(
                bank_displayName(), (uint8_t)(payload_offset - 2u));
            return 1u;
        }
        if (payload_offset >= 10u && payload_offset < 12u) {
            bank_value = bank_scenePresentMask();
            /*
             * Witness the live present-mask value exactly when the drain
             * captures the field's first byte.
             *
             * Inputs: payload_offset and the resident mask. Output: one B
             * record with the drain-site flag set; value32 packs the resident
             * mask in bits 16..31 and payload offset 10 in bits 0..15. Why:
             * this is the boundary where SRAM becomes captured record bytes,
             * so the trace separates a zero resident value from later copy or
             * offset loss. The first-byte condition avoids duplicate records.
             * Affiliate: filesystem.c's patch application.
             */
            if (payload_offset ==
                (AUTOSAVE_BANK_SCENE_PRESENT_MASK_OFFSET -
                 AUTOSAVE_PAYLOAD_OFFSET)) {
                autosaveTrace_record(
                    AUTOSAVE_TRACE_STAGE_BANK_PRESENT,
                    AUTOSAVE_TRACE_BANK_PRESENT_FLAG_DRAIN,
                    ((uint32_t)bank_value <<
                     AUTOSAVE_TRACE_BANK_PRESENT_MASK_SHIFT) |
                        payload_offset);
            }
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
     * Route the reserved Effect parameter interval through its explicit stub.
     *
     * Inputs: Scene-relative bytes 137..639. Output: nonexistent while the
     * Effect live count is zero. Type/name and Scene name/padding also remain
     * unavailable because they have no resident owner. Why: adding Effect
     * ownership later extends one named branch instead of changing writer
     * classification. Pattern remains outside this wire layout entirely.
     */
    if (relative >= AUTOSAVE_EFFECT_OFFSET +
                        AUTOSAVE_EFFECT_PARAMETERS_OFFSET &&
        relative < AUTOSAVE_EFFECT_OFFSET +
                       AUTOSAVE_EFFECT_PARAMETERS_OFFSET +
                       AUTOSAVE_EFFECT_PARAMETER_ALLOC_BYTES) {
        return autosave_getEffectParameter(
            scene,
            (uint16_t)(relative - AUTOSAVE_EFFECT_OFFSET -
                       AUTOSAVE_EFFECT_PARAMETERS_OFFSET),
            value);
    }
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
        if (kit_parameter == AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_DECAY) {
            *value = scene->kit.settings.slot6_track7_amp_envelope_decay;
            return 1u;
        }
        if (kit_parameter ==
            AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_MORPH_DECAY) {
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

void autosave_setMutationTrackingEnabled(uint8_t enabled)
{
    uint32_t primask;

    /*
     * Publish the retained-owner producer gate as one atomic lifecycle change.
     *
     * Input: nonzero after successful boot autosave setup, zero before setup,
     * reset, no-Bank fallback, or setup failure. Output: ordinary Bank/Scene/
     * Preset markers are accepted or ignored; recovery merge and rollback are
     * deliberately unaffected. Why: boot population must not be mistaken for
     * user mutation. Affiliate: filesystem_ensureAutosaveFilesBlocking().
     */
    primask = autosave_irqSave();
    autosave_mutation_tracking_enabled = enabled ? 1u : 0u;
    autosave_irqRestore(primask);
}

void autosave_discardDirtyMask(void)
{
    /*
     * Clear the sole canonical record after producers and transforms stop.
     *
     * Inputs: filesystem lifecycle has disabled tracking and verified that no
     * autosave operation is consuming mask chunks. Output: every pending bit
     * is discarded in SRAM; SD records remain untouched. Why: stale work from
     * an intentionally disabled/retired Bank session must not reappear after
     * re-enable. Affiliates: filesystem's immediate/deferred OFF transition.
     */
    memset((void *)autosave_dirty_mask, 0, sizeof(autosave_dirty_mask));
}

void autosave_markBankFieldDirty(autosave_bank_field_t field)
{
    uint16_t payload_offset;
    uint8_t width;
    uint8_t i;

    /*
     * Convert one logical Bank field to its existing serialized byte range.
     *
     * Input: format-owned field identifier. Output: every byte of that field
     * is atomically marked, or an invalid identifier is ignored. Why: BankData
     * must not duplicate offsets/widths for the two-byte masks, slot, or
     * eight-byte name. Affiliates: BankData setters and Bank getter branches.
     */
    switch (field) {
    case AUTOSAVE_BANK_FIELD_RESTORE_SLOT:
        payload_offset = (uint16_t)(AUTOSAVE_BANK_SLOT_OFFSET -
                                    AUTOSAVE_PAYLOAD_OFFSET);
        width = 2u;
        break;
    case AUTOSAVE_BANK_FIELD_DISPLAY_NAME:
        payload_offset = (uint16_t)(AUTOSAVE_BANK_NAME_OFFSET -
                                    AUTOSAVE_PAYLOAD_OFFSET);
        width = AUTOSAVE_NAME_BYTES;
        break;
    case AUTOSAVE_BANK_FIELD_SCENE_PRESENT_MASK:
        payload_offset = (uint16_t)(AUTOSAVE_BANK_SCENE_PRESENT_MASK_OFFSET -
                                    AUTOSAVE_PAYLOAD_OFFSET);
        width = 2u;
        break;
    case AUTOSAVE_BANK_FIELD_ACTIVE_SCENE:
        payload_offset = (uint16_t)(AUTOSAVE_BANK_ACTIVE_SCENE_OFFSET -
                                    AUTOSAVE_PAYLOAD_OFFSET);
        width = 1u;
        break;
    case AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK:
        payload_offset = (uint16_t)(AUTOSAVE_BANK_VOICE_EDIT_MASK_OFFSET -
                                    AUTOSAVE_PAYLOAD_OFFSET);
        width = 2u;
        break;
    default:
        return;
    }
    for (i = 0u; i < width; i++)
        autosave_markPayloadOffsetDirty((uint16_t)(payload_offset + i));
}

void autosave_markSceneParameterDirty(uint8_t scene_index,
                                      uint8_t parameter_index)
{
    uint16_t scene_base;

    /*
     * Mark one named Scene-settings byte after validating its resident owner.
     *
     * Inputs: present Scene index and parameter index below the shared count.
     * Output: exactly SceneBase+8+index becomes dirty, otherwise no-op. Why:
     * the marker and getter must share ordering as future fields are appended.
     * Affiliates: SceneData's change-aware scalar store helper.
     */
    if (parameter_index >= AUTOSAVE_SCENE_PARAM_COUNT ||
        !autosave_scenePayloadBase(scene_index, &scene_base)) {
        return;
    }
    autosave_markPayloadOffsetDirty((uint16_t)(
        scene_base + AUTOSAVE_SCENE_PARAMETERS_OFFSET + parameter_index));
}

void autosave_markKitParameterDirty(uint8_t scene_index,
                                    uint8_t parameter_index)
{
    uint16_t scene_base;

    /*
     * Mark one named Kit-settings byte inside its owning Scene.
     *
     * Inputs: present Scene and Kit index below AUTOSAVE_KIT_PARAM_COUNT.
     * Output: exactly KitBase+8+index becomes dirty. Why: the two generated
     * track-7 endpoints and future Kit fields share one append/count contract.
     * Affiliate: SceneData's Kit scalar store helper.
     */
    if (parameter_index >= AUTOSAVE_KIT_PARAM_COUNT ||
        !autosave_scenePayloadBase(scene_index, &scene_base)) {
        return;
    }
    autosave_markPayloadOffsetDirty((uint16_t)(
        scene_base + AUTOSAVE_KIT_OFFSET +
        AUTOSAVE_KIT_PARAMETERS_OFFSET + parameter_index));
}

/*
 * Resolve a live Instrument coordinate for both endpoint marker variants.
 *
 * Inputs: present Scene, slot, and descriptor index. Outputs: payload base and
 * registry entry only when the destination type owns that descriptor. Why:
 * descriptor enums/tables are the canonical per-type order; Autosave must not
 * add type-specific maps. Affiliates: Preset's validated endpoint setters and
 * the 64-cell generic image/72-cell wire-capacity assertions.
 */
static const instrument_registry_entry_t *autosave_instrumentMarkerBase(
    uint8_t scene_index,
    uint8_t slot,
    uint8_t descriptor_index,
    uint16_t *instrument_base)
{
    uint16_t scene_base;
    const kit_instrument_slot_t *instrument;
    const instrument_registry_entry_t *entry;

    if (!instrument_base || slot >= AUTOSAVE_INSTRUMENTS_PER_KIT ||
        !autosave_scenePayloadBase(scene_index, &scene_base)) {
        return NULL;
    }
    instrument = scene_instrumentSlotConst(scene_index, slot);
    if (!instrument)
        return NULL;
    entry = instrumentManager_registryEntry(instrument->type);
    if (!entry || descriptor_index >= entry->descriptor_count ||
        descriptor_index >= INSTRUMENT_PARAM_COUNT)
        return NULL;
    *instrument_base = (uint16_t)(
        scene_base + AUTOSAVE_KIT_OFFSET +
        AUTOSAVE_KIT_INSTRUMENTS_OFFSET +
        ((uint16_t)slot * AUTOSAVE_INSTRUMENT_RECORD_BYTES));
    return entry;
}

void autosave_markInstrumentNormalParameterDirty(uint8_t scene_index,
                                                  uint8_t slot,
                                                  uint8_t descriptor_index)
{
    uint16_t instrument_base;

    /*
     * Mark one active-type normal endpoint descriptor.
     *
     * Inputs: Scene/slot/descriptor coordinates validated against the current
     * registry. Output: its fixed normal-image cell becomes dirty. Why: every
     * current and future descriptor uses Preset's generic setter and therefore
     * needs no per-instrument Autosave switch. Affiliate: normal live getter.
     */
    if (!autosave_instrumentMarkerBase(
            scene_index, slot, descriptor_index, &instrument_base)) {
        return;
    }
    autosave_markPayloadOffsetDirty((uint16_t)(
        instrument_base + AUTOSAVE_INSTRUMENT_NORMAL_OFFSET +
        descriptor_index));
}

void autosave_markInstrumentMorphParameterDirty(uint8_t scene_index,
                                                 uint8_t slot,
                                                 uint8_t descriptor_index)
{
    uint16_t instrument_base;
    const instrument_registry_entry_t *entry;

    /*
     * Mark one active-type Morph endpoint descriptor.
     *
     * Inputs: Scene/slot/descriptor coordinates. Output: its Morph-image cell
     * is set only when the registry owns and flags that descriptor Morphable;
     * selectors and reserve cells are ignored. Why: this exactly mirrors the
     * live getter/file policy. Affiliate: Preset's Morph endpoint setter.
     */
    entry = autosave_instrumentMarkerBase(
        scene_index, slot, descriptor_index, &instrument_base);
    if (!entry ||
        (entry->descriptors[descriptor_index].flags &
         INSTRUMENT_PARAM_FLAG_MORPHABLE) == 0u) {
        return;
    }
    autosave_markPayloadOffsetDirty((uint16_t)(
        instrument_base + AUTOSAVE_INSTRUMENT_MORPH_OFFSET +
        descriptor_index));
}

void autosave_markEffectParameterDirty(uint8_t scene_index,
                                       uint16_t parameter_index)
{
    uint16_t scene_base;
    uint16_t live_count = AUTOSAVE_EFFECT_PARAM_COUNT;

    /*
     * Preserve the exact single-Effect-parameter append point as a no-op.
     *
     * Inputs: Scene and future parameter index. Output: no bit in Phase 1
     * because AUTOSAVE_EFFECT_PARAM_COUNT is zero and no retained owner exists.
     * Once implemented, a valid parameter maps to SceneBase+128+9+index. Why:
     * future Effect setters must join the same dirty/get contract immediately.
     * Affiliates: Effect getter stub and future scene_t Effect ownership.
     */
    if (parameter_index >= live_count ||
        !autosave_scenePayloadBase(scene_index, &scene_base)) {
        return;
    }
    autosave_markPayloadOffsetDirty((uint16_t)(
        scene_base + AUTOSAVE_EFFECT_OFFSET +
        AUTOSAVE_EFFECT_PARAMETERS_OFFSET + parameter_index));
}

void autosave_markInstrumentNormalDirty(uint8_t scene_index, uint8_t slot)
{
    const kit_instrument_slot_t *instrument =
        scene_instrumentSlotConst(scene_index, slot);
    const instrument_registry_entry_t *entry;
    uint8_t descriptor_index;

    /*
     * Mark a future same-type normal-endpoint copy destination.
     *
     * Inputs: destination Scene/slot after copy code has verified source and
     * destination types match. Output: every live normal descriptor is dirty;
     * type/name/Morph remain untouched. Why: endpoint-only copy is not license
     * to reinterpret descriptor indices across types. Affiliate: future copy.
     */
    if (!instrument)
        return;
    entry = instrumentManager_registryEntry(instrument->type);
    if (!entry)
        return;
    for (descriptor_index = 0u;
         descriptor_index < entry->descriptor_count;
         descriptor_index++) {
        autosave_markInstrumentNormalParameterDirty(
            scene_index, slot, descriptor_index);
    }
}

void autosave_markInstrumentMorphDirty(uint8_t scene_index, uint8_t slot)
{
    const kit_instrument_slot_t *instrument =
        scene_instrumentSlotConst(scene_index, slot);
    const instrument_registry_entry_t *entry;
    uint8_t descriptor_index;

    /*
     * Mark one committed same-type Morph-endpoint import.
     *
     * Inputs: destination Scene/slot after a successful InstrumentMrp or KitMrp
     * matching-type Morph copy commit. Output: every Morphable endpoint
     * becomes dirty; Normal/type/name/HCNAMES provenance do not. Why: selectors
     * have no serialized Morph owner, and the marker must match exactly the
     * endpoint domain that the commit changed. Affiliates:
     * preset_startInstrumentMorphApply() and
     * preset_commitStagedKitNormalToMorph().
     */
    if (!instrument)
        return;
    entry = instrumentManager_registryEntry(instrument->type);
    if (!entry)
        return;
    for (descriptor_index = 0u;
         descriptor_index < entry->descriptor_count;
         descriptor_index++) {
        autosave_markInstrumentMorphParameterDirty(
            scene_index, slot, descriptor_index);
    }
}

void autosave_markWholeInstrumentDirty(uint8_t scene_index, uint8_t slot)
{
    uint16_t instrument_base;
    const instrument_registry_entry_t *entry;
    uint8_t type_byte;
    uint8_t descriptor_index;
    uint8_t expected_count = 0u;
    uint8_t published_count = 0u;
    uint8_t trace_flags = 0u;
    uint32_t trace_value;

    /*
     * Mark one committed root-pool whole-Instrument replacement.
     *
     * Inputs: destination Scene/slot. Output: three type-token bytes plus every
     * live normal and Morphable endpoint are dirty; the HCNAMES-owned name is
     * deliberately excluded. Why: type is required to interpret endpoints, but
     * identity/provenance remain HCNAMES-owned. The marker only records the
     * already-committed SceneData image; it copies no data and performs no I/O.
     * It also emits one RAM-only I summary after every request, including a
     * rejected request, so field traces retain its outcome after D-record
     * wrap. Affiliate: preset_startInstrumentApplyImage() after a root pool
     * commit.
     */
    entry = autosave_instrumentMarkerBase(scene_index, slot, 0u,
                                          &instrument_base);
    if (entry) {
        trace_flags |= AUTOSAVE_TRACE_INSTRUMENT_MARK_FLAG_BASE_VALID;
        expected_count = AUTOSAVE_INSTRUMENT_TYPE_BYTES;
        for (descriptor_index = 0u;
             descriptor_index < entry->descriptor_count;
             descriptor_index++) {
            expected_count++;
            if ((entry->descriptors[descriptor_index].flags &
                 INSTRUMENT_PARAM_FLAG_MORPHABLE) != 0u) {
                expected_count++;
            }
        }
        for (type_byte = 0u; type_byte < AUTOSAVE_INSTRUMENT_TYPE_BYTES;
             type_byte++) {
            published_count = (uint8_t)(published_count +
                autosave_markPayloadOffsetDirty((uint16_t)(instrument_base +
                                                           type_byte)));
        }
        for (descriptor_index = 0u;
             descriptor_index < entry->descriptor_count;
             descriptor_index++) {
            published_count = (uint8_t)(published_count +
                autosave_markPayloadOffsetDirty((uint16_t)(instrument_base +
                    AUTOSAVE_INSTRUMENT_NORMAL_OFFSET + descriptor_index)));
            if ((entry->descriptors[descriptor_index].flags &
                 INSTRUMENT_PARAM_FLAG_MORPHABLE) != 0u) {
                published_count = (uint8_t)(published_count +
                    autosave_markPayloadOffsetDirty((uint16_t)(instrument_base +
                        AUTOSAVE_INSTRUMENT_MORPH_OFFSET + descriptor_index)));
            }
        }
    }
    if (autosave_mutation_tracking_enabled)
        trace_flags |= AUTOSAVE_TRACE_INSTRUMENT_MARK_FLAG_TRACKING_ENABLED;
    if (expected_count != 0u && published_count == expected_count)
        trace_flags |= AUTOSAVE_TRACE_INSTRUMENT_MARK_FLAG_ALL_PUBLISHED;
    /*
     * Keep one terminal outcome even when the marker's D records exceed the
     * 64-record RAM ring. Why: a field capture must reveal whether the root
     * load called this marker, passed its map gate, and published every byte
     * instead of forcing diagnosis from the absence of wrapped D records.
     */
    trace_value = ((uint32_t)scene_index <<
                   AUTOSAVE_TRACE_INSTRUMENT_MARK_SCENE_SHIFT) |
                  ((uint32_t)slot <<
                   AUTOSAVE_TRACE_INSTRUMENT_MARK_SLOT_SHIFT) |
                  ((uint32_t)expected_count <<
                   AUTOSAVE_TRACE_INSTRUMENT_MARK_EXPECTED_SHIFT) |
                  ((uint32_t)published_count <<
                   AUTOSAVE_TRACE_INSTRUMENT_MARK_PUBLISHED_SHIFT);
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_INSTRUMENT_MARK, trace_flags,
                         trace_value);
}

void autosave_markKitDirty(uint8_t scene_index)
{
    uint8_t parameter_index;
    uint8_t slot;

    /*
     * Mark the implemented payload of one committed normal whole-Kit load.
     *
     * Input: destination Scene. Output: all live Kit settings and six complete
     * Instrument data regions are dirty; HCNAMES-owned Kit/Instrument names
     * remain excluded. Why: compound copy/load code needs one post-commit hook.
     * Affiliates: normal Kit Load completion, future Kit copy, and the six
     * complete Instrument scopes below.
     */
    /* Pack the terminal LOAD_MARK in locals only; no persistent state is added. */
    uint8_t trace_flags = 0u;
    uint32_t trace_value;

    for (parameter_index = 0u; parameter_index < AUTOSAVE_KIT_PARAM_COUNT;
         parameter_index++) {
        autosave_markKitParameterDirty(scene_index, parameter_index);
    }
    for (slot = 0u; slot < AUTOSAVE_INSTRUMENTS_PER_KIT; slot++)
        autosave_markWholeInstrumentDirty(scene_index, slot);

    /* Emit the durable whole-Kit witness after nested D/I records may wrap the ring. */
    if (autosave_mutation_tracking_enabled)
        trace_flags |= AUTOSAVE_TRACE_LOAD_MARK_FLAG_TRACKING_ENABLED;
    trace_value = ((uint32_t)AUTOSAVE_TRACE_LOAD_MARK_KIND_KIT <<
                   AUTOSAVE_TRACE_LOAD_MARK_KIND_SHIFT) |
                  ((uint32_t)scene_index << AUTOSAVE_TRACE_LOAD_MARK_SCENE_SHIFT);
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_LOAD_MARK, trace_flags,
                         trace_value);
}

void autosave_markEffectDirty(uint8_t scene_index)
{
    uint16_t parameter_index = 0u;
    uint16_t live_count = AUTOSAVE_EFFECT_PARAM_COUNT;

    /*
     * Preserve a future whole-Effect post-copy hook without fake state.
     *
     * Input: destination Scene. Output: zero parameter bits today because the
     * live Effect count is zero; future type/name ownership and parameters are
     * added here. Why: Scene scope must never silently omit Effects once they
     * exist. Affiliates: future Effect copy and Scene-without-Pattern marker.
     */
    while (parameter_index < live_count) {
        autosave_markEffectParameterDirty(scene_index, parameter_index);
        parameter_index++;
    }
}

void autosave_markSceneWithoutPatternDirty(uint8_t scene_index)
{
    uint8_t parameter_index;

    /*
     * Mark the implemented non-Pattern payload of one committed Scene load.
     *
     * Input: destination Scene. Output: all Scene settings, Effect scope, and
     * Kit scope become dirty; Scene name and Pattern are excluded. Why: direct
     * Scene replacements bypass scalar setters but must not imply Pattern
     * persistence. Affiliates: successful root Scene completion, exact-mask
     * Bank completion, future Scene copy, Effect stub, and Kit marker.
     */
    /* Pack the outer terminal LOAD_MARK in locals only; no persistent state is added. */
    uint8_t trace_flags = 0u;
    uint32_t trace_value;

    for (parameter_index = 0u; parameter_index < AUTOSAVE_SCENE_PARAM_COUNT;
         parameter_index++) {
        autosave_markSceneParameterDirty(scene_index, parameter_index);
    }
    autosave_markEffectDirty(scene_index);
    autosave_markKitDirty(scene_index);

    /* Emit the durable whole-Scene witness after the nested Kit marker completes. */
    if (autosave_mutation_tracking_enabled)
        trace_flags |= AUTOSAVE_TRACE_LOAD_MARK_FLAG_TRACKING_ENABLED;
    trace_value = ((uint32_t)AUTOSAVE_TRACE_LOAD_MARK_KIND_SCENE <<
                   AUTOSAVE_TRACE_LOAD_MARK_KIND_SHIFT) |
                  ((uint32_t)scene_index << AUTOSAVE_TRACE_LOAD_MARK_SCENE_SHIFT);
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_LOAD_MARK, trace_flags,
                         trace_value);
}

void autosave_markSceneWithPatternDirty(uint8_t scene_index)
{
    /*
     * Reserve the later Scene-with-Pattern copy boundary explicitly.
     *
     * Input: destination Scene. Output: Phase 1 marks only the implemented
     * non-Pattern scope. Why: Pattern is not in this autosave wire format, so
     * callers must not mistake this stub for persistence. Affiliate: future
     * Pattern autosave/copy work, which must extend this function deliberately.
     */
    autosave_markSceneWithoutPatternDirty(scene_index);
    /* TODO: mark Pattern only after Pattern persistence has a defined owner. */
}

void autosave_markResidentBankDirty(void)
{
    uint8_t field;
    uint8_t scene_index;
    uint16_t present_mask = bank_scenePresentMask();

    /*
     * Seed one complete live resident Bank snapshot into the canonical mask.
     *
     * Inputs: enabled mutation tracking, every typed Bank field, and the
     * current resident Scene-present mask. Outputs: all gettable Bank bytes and
     * non-Pattern scopes of present Scenes become dirty. Why: AutoSave OFF
     * intentionally ignores intervening mutations, so runtime re-enable needs
     * an explicit convergence boundary. Affiliates: BankData, the existing
     * whole-Scene marker, and filesystem runtime ensure completion.
     */
    for (field = 0u; field < AUTOSAVE_BANK_FIELD_COUNT; field++)
        autosave_markBankFieldDirty((autosave_bank_field_t)field);
    for (scene_index = 0u; scene_index < AUTOSAVE_SCENE_COUNT;
         scene_index++) {
        if ((present_mask & (uint16_t)(1u << scene_index)) != 0u)
            autosave_markSceneWithoutPatternDirty(scene_index);
    }
}

void autosave_maskMergeChunk(uint16_t mask_byte_offset,
                             const uint8_t *src,
                             uint16_t byte_count)
{
    uint16_t i;

    /*
     * Recover file-carried incomplete work without replacing live SRAM state.
     *
     * Inputs: one mask-relative interval streamed through filesystem.c's
     * existing 512-byte buffer. Output: set file bits are ORed into the one
     * canonical record; prior SRAM bits and every byte outside the interval
     * remain unchanged. Why: either side saying "dirty" must keep the payload
     * cell pending. Affiliates: drain phase 54 and power-loss recovery.
     */
    if (!src || mask_byte_offset >= AUTOSAVE_MASK_BYTES ||
        byte_count > AUTOSAVE_MASK_BYTES - mask_byte_offset) {
        return;
    }
    for (i = 0u; i < byte_count; i++) {
        autosave_maskByteOr((uint16_t)(mask_byte_offset + i), src[i]);
    }
}

uint8_t autosave_maskHasDirty(void)
{
    uint16_t byte_index;

    /*
     * Test the canonical SRAM completeness register without changing it.
     *
     * Input is the retained Autosave-owned record. Output is nonzero on the
     * first dirty byte, or zero when no mutation requires a parameter get or
     * ping-pong write. Why: generation/copy work must not run merely to
     * reproduce an already-empty record. Affiliates: filesystem drain phase 55
     * and the autonomous-writer completion callback.
     */
    for (byte_index = 0u; byte_index < AUTOSAVE_MASK_BYTES; byte_index++) {
        if (autosave_dirty_mask[byte_index] != 0u)
            return 1u;
    }
    return 0u;
}

uint8_t autosave_maskBitTake(uint16_t payload_offset)
{
    uint16_t mask_byte;
    uint8_t bit;
    uint8_t was_set;
    uint32_t primask;

    /*
     * Atomically claim one LSB-first dirty cell for foreground classification.
     *
     * Input: payload offset. Output: its prior bit state; a set bit is cleared
     * in the same one-byte critical section. Why: a later interrupt mutation
     * re-sets the bit and survives for continuation, eliminating the former
     * test/get/clear loss window. Parameter get remains outside this section.
     * Affiliate: filesystem autosave phase 56.
     */
    if (payload_offset >= AUTOSAVE_PAYLOAD_BYTES)
        return 0u;
    mask_byte = (uint16_t)(payload_offset >> 3u);
    bit = (uint8_t)(1u << (payload_offset & 7u));
    primask = autosave_irqSave();
    was_set = (uint8_t)((autosave_dirty_mask[mask_byte] & bit) != 0u);
    autosave_dirty_mask[mask_byte] = (uint8_t)(
        autosave_dirty_mask[mask_byte] & (uint8_t)~bit);
    autosave_irqRestore(primask);
    return was_set;
}

void autosave_maskRestoreCaptured(const uint16_t *payload_offsets,
                                  uint16_t patch_count)
{
    uint16_t i;

    /*
     * Roll captured work back into the canonical record after writer failure.
     *
     * Inputs: the transaction's sorted captured offsets and valid count.
     * Output: every bounded offset is dirty again; duplicate/already-dirty
     * positions remain harmless. Why: classification atomically takes a bit
     * before its get, so a failed target must restore every successfully
     * captured SRAM-only offset. This is writer rollback, not a retained-
     * parameter mutation hook.
     */
    if (!payload_offsets)
        return;
    for (i = 0u; i < patch_count; i++) {
        uint16_t payload_offset = payload_offsets[i];

        if (payload_offset < AUTOSAVE_PAYLOAD_BYTES) {
            autosave_maskByteOr(
                (uint16_t)(payload_offset >> 3u),
                (uint8_t)(1u << (payload_offset & 7u)));
        }
    }
}

uint8_t autosave_generationIsNewer(uint32_t candidate, uint32_t reference)
{
    /* Two records cannot advance by half the uint32_t space between scans. */
    return (uint8_t)((int32_t)(candidate - reference) > 0);
}

/*
 * Apply only the control-header fields owned by the target transaction.
 *
 * Inputs: one mutable chunk plus next generation/probe. Output: intersecting
 * header cells describe the prospective valid record with CRC bytes zero;
 * mask and payload remain untouched for the drain-aware wrapper below. Why:
 * filesystem.c calculates CRC from this final logical header, then clears the
 * physical commit byte until data and CRC publication are durable.
 */
static void autosave_transformHeaderChunk(uint8_t *chunk,
                                          uint32_t absolute_offset,
                                          uint16_t byte_count,
                                          uint32_t generation,
                                          uint8_t probe_counter)
{
    uint16_t i;

    if (!chunk || absolute_offset >= AUTOSAVE_RECORD_BYTES ||
        byte_count > AUTOSAVE_RECORD_BYTES - absolute_offset) {
        return;
    }
    for (i = 0u; i < byte_count; i++) {
        uint32_t record_offset = absolute_offset + i;

        if (record_offset == AUTOSAVE_HEADER_COMMIT_OFFSET) {
            chunk[i] = AUTOSAVE_HEADER_COMMIT_VALID;
        } else if (record_offset >= AUTOSAVE_HEADER_GENERATION_OFFSET &&
                   record_offset < AUTOSAVE_HEADER_GENERATION_OFFSET + 4u) {
            chunk[i] = autosave_u32Byte(
                generation,
                (uint8_t)(record_offset - AUTOSAVE_HEADER_GENERATION_OFFSET));
        } else if (record_offset >= AUTOSAVE_HEADER_CRC32C_OFFSET &&
                   record_offset < AUTOSAVE_HEADER_CRC32C_OFFSET + 4u) {
            chunk[i] = 0u;
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
     * Inputs: one source chunk, canonical mask, and immutable captured patch
     * list. Output: prospective final header, complete intersecting canonical
     * mask bytes, and captured live payload patches. Why: filesystem.c updates
     * CRC from this exact one-pass output before clearing only the physical
     * commit marker in the unpublished target.
     */
    if (!chunk || byte_count == 0u ||
        absolute_offset >= AUTOSAVE_RECORD_BYTES ||
        byte_count > AUTOSAVE_RECORD_BYTES - absolute_offset ||
        !patch_cursor ||
        (patch_count > 0u && (!patch_offsets || !patch_values))) {
        return;
    }

    chunk_end = absolute_offset + byte_count;
    autosave_transformHeaderChunk(
        chunk, absolute_offset, byte_count, generation, probe_counter);

    mask_begin = (absolute_offset > AUTOSAVE_MASK_OFFSET)
        ? absolute_offset : AUTOSAVE_MASK_OFFSET;
    mask_end = (chunk_end <
                AUTOSAVE_MASK_OFFSET + AUTOSAVE_MASK_BYTES)
        ? chunk_end : AUTOSAVE_MASK_OFFSET + AUTOSAVE_MASK_BYTES;
    if (mask_begin < mask_end) {
        uint32_t record_offset;

        /*
         * Snapshot each volatile canonical byte into the stable write chunk.
         *
         * Inputs: this chunk's intersecting mask interval. Output: one ordinary
         * staging byte per current volatile mask byte. Why: memcpy cannot
         * express volatile source ownership, and a later producer must remain
         * in SRAM for continuation without changing bytes already fed to CRC.
         * Affiliates: atomic producers and filesystem's streamed CRC/write.
         */
        for (record_offset = mask_begin; record_offset < mask_end;
             record_offset++) {
            chunk[record_offset - absolute_offset] =
                autosave_dirty_mask[
                    record_offset - AUTOSAVE_MASK_OFFSET];
        }
    }

    /*
     * Consume the sorted patch list monotonically for this complete pass.
     *
     * Input cursor is reset once by filesystem.c before the copy stream. Output
     * cursor advances past patches below/inside the current chunk while patches
     * above it remain for later chunks.
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
