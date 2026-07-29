/*
 * Autosave.c -- pure first-pass register formatter.
 *
 * The earlier record ledger, debounce scheduler, payload codec, and staged
 * load barrier were rejected.  This replacement is intentionally smaller: it
 * maps authoritative BankData/HCNAMES names into a caller-owned byte range and
 * leaves every parameter/mask/effect/padding byte zero.  No static or global
 * storage is introduced here.
 */
#include "Autosave.h"

#include <string.h>

static void autosave_writeByte(uint8_t *dst, uint32_t chunk_offset,
                               uint16_t chunk_bytes, uint32_t record_offset,
                               uint8_t value)
{
    /*
     * Copy one logical record byte only when it falls inside the caller's
     * current sequential stream range.  A 32-bit subtraction avoids forming a
     * temporary whole-record buffer and remains safe because the formatter
     * rejects an offset beyond the fixed 23,184-byte register first.
     */
    if (record_offset >= chunk_offset &&
        (record_offset - chunk_offset) < chunk_bytes) {
        dst[record_offset - chunk_offset] = value;
    }
}

static void autosave_writeZeroPaddedName(uint8_t *dst, uint32_t chunk_offset,
                                         uint16_t chunk_bytes,
                                         uint32_t record_offset,
                                         const char name[AUTOSAVE_NAME_BYTES])
{
    uint8_t length = AUTOSAVE_NAME_BYTES;
    uint8_t i;

    /*
     * HCNAMES/BankData display rows are eight cells and normally space padded.
     * The binary register uses zero padding instead: retain printable embedded
     * spaces but trim only trailing spaces/NULs before copying the name.
     */
    while (length > 0u &&
           (name == NULL || name[length - 1u] == '\0' ||
            name[length - 1u] == ' ')) {
        length--;
    }

    for (i = 0u; i < length; i++) {
        uint8_t value = (uint8_t)name[i];

        autosave_writeByte(dst, chunk_offset, chunk_bytes,
                           record_offset + i,
                           (value >= 0x20u && value <= 0x7eu) ? value : 0u);
    }
}

void autosave_formatInitialChunk(
    uint8_t *dst,
    uint32_t absolute_offset,
    uint16_t byte_count,
    uint16_t bank_slot,
    const char bank_name[AUTOSAVE_NAME_BYTES],
    const char resident_names[AUTOSAVE_HCNAMES_ROW_COUNT]
                             [AUTOSAVE_HCNAMES_ROW_BYTES])
{
    uint32_t chunk_end;
    uint8_t scene;

    /*
     * The caller streams only bounded ranges from the fixed record.  Clearing
     * first is the complete creation policy: it gives the replacement mask,
     * all parameters, Effects, future metadata, and all padding their required
     * zero baseline before the small set of identity bytes is overlaid.
     */
    if (!dst || byte_count == 0u || absolute_offset >= AUTOSAVE_RECORD_BYTES)
        return;
    chunk_end = absolute_offset + byte_count;
    if (chunk_end > AUTOSAVE_RECORD_BYTES)
        byte_count = (uint16_t)(AUTOSAVE_RECORD_BYTES - absolute_offset);
    memset(dst, 0, byte_count);

    /* The slot is deliberately the validated loaded Bank slot, little-endian. */
    autosave_writeByte(dst, absolute_offset, byte_count,
                       AUTOSAVE_BANK_SLOT_OFFSET, (uint8_t)bank_slot);
    autosave_writeByte(dst, absolute_offset, byte_count,
                       AUTOSAVE_BANK_SLOT_OFFSET + 1u,
                       (uint8_t)(bank_slot >> 8u));
    autosave_writeZeroPaddedName(dst, absolute_offset, byte_count,
                                 AUTOSAVE_BANK_NAME_OFFSET, bank_name);

    if (!resident_names)
        return;

    for (scene = 0u; scene < AUTOSAVE_SCENE_COUNT; scene++) {
        uint32_t scene_offset = AUTOSAVE_SCENES_OFFSET +
            ((uint32_t)scene * AUTOSAVE_SCENE_SECTION_BYTES);
        uint8_t instrument;

        /* HCNAMES rows are Bank, 16 Scenes, 16 Kits, then 16 × 6 Instruments. */
        autosave_writeZeroPaddedName(
            dst, absolute_offset, byte_count,
            scene_offset + AUTOSAVE_SCENE_NAME_OFFSET,
            resident_names[AUTOSAVE_HCNAMES_SCENE_BASE + scene]);
        autosave_writeZeroPaddedName(dst, absolute_offset, byte_count,
                                     scene_offset + AUTOSAVE_KIT_OFFSET +
                                         AUTOSAVE_KIT_NAME_OFFSET,
                                     resident_names[AUTOSAVE_HCNAMES_KIT_BASE +
                                                    scene]);
        for (instrument = 0u;
             instrument < AUTOSAVE_INSTRUMENTS_PER_KIT;
             instrument++) {
            uint16_t name_row = (uint16_t)(AUTOSAVE_HCNAMES_INSTRUMENT_BASE +
                ((uint16_t)scene * AUTOSAVE_INSTRUMENTS_PER_KIT) + instrument);
            uint32_t instrument_offset = scene_offset + AUTOSAVE_KIT_OFFSET +
                AUTOSAVE_KIT_INSTRUMENTS_OFFSET +
                ((uint32_t)instrument * AUTOSAVE_INSTRUMENT_RECORD_BYTES);

            autosave_writeZeroPaddedName(
                dst, absolute_offset, byte_count,
                instrument_offset + AUTOSAVE_INSTRUMENT_NAME_OFFSET,
                resident_names[name_row]);
        }
    }
}
