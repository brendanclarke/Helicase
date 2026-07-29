/*
 * Autosave.h -- fixed working-Bank delta-register layout.
 *
 * This module deliberately owns only byte layout.  It has no dirty ledger,
 * scheduler, filesystem handle, reader, or retained workspace.  filesystem.c
 * remains the sole AsyncFATFS owner and lends the formatter its existing
 * 512-byte stream buffer plus its existing HCNAMES cache at boot.
 */
#ifndef AUTOSAVE_H_
#define AUTOSAVE_H_

#include <stdint.h>

/*
 * Root register filenames selected for the two future ping-pong records.
 * The second name intentionally has no leading dot: retain the requested
 * spelling exactly instead of normalizing it to the first record's form.
 */
#define AUTOSAVE_RECORD_A_FILENAME ".hcprms1"
#define AUTOSAVE_RECORD_B_FILENAME "hcprms2"
#define AUTOSAVE_RECORD_FILE_COUNT 2u

/*
 * Fixed v1 register geometry.
 *
 * The 2,576-byte mask has one bit for every following byte.  This creation
 * pass leaves every mask bit clear: names are baseline identity text only and
 * must not overlay the committed Bank until a later writer explicitly marks
 * them changed.
 */
#define AUTOSAVE_MASK_BYTES              2576u
#define AUTOSAVE_BANK_SECTION_BYTES       128u
#define AUTOSAVE_SCENE_COUNT               16u
#define AUTOSAVE_SCENE_SECTION_BYTES     1280u
#define AUTOSAVE_RECORD_BYTES \
    (AUTOSAVE_MASK_BYTES + AUTOSAVE_BANK_SECTION_BYTES + \
     (AUTOSAVE_SCENE_COUNT * AUTOSAVE_SCENE_SECTION_BYTES))

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

/* Top-level absolute offsets. */
#define AUTOSAVE_BANK_OFFSET                AUTOSAVE_MASK_BYTES
#define AUTOSAVE_BANK_SLOT_OFFSET           (AUTOSAVE_BANK_OFFSET + 0u)
#define AUTOSAVE_BANK_NAME_OFFSET           (AUTOSAVE_BANK_OFFSET + 2u)
#define AUTOSAVE_SCENES_OFFSET \
    (AUTOSAVE_BANK_OFFSET + AUTOSAVE_BANK_SECTION_BYTES)

/* One Scene's fixed internal regions. */
#define AUTOSAVE_SCENE_NAME_OFFSET            0u
#define AUTOSAVE_KIT_OFFSET                  384u
#define AUTOSAVE_KIT_NAME_OFFSET              0u
#define AUTOSAVE_KIT_INSTRUMENTS_OFFSET      128u
#define AUTOSAVE_INSTRUMENT_NAME_OFFSET        3u

_Static_assert(AUTOSAVE_MASK_BYTES * 8u ==
                   AUTOSAVE_BANK_SECTION_BYTES +
                       (AUTOSAVE_SCENE_COUNT * AUTOSAVE_SCENE_SECTION_BYTES),
               "autosave mask must cover every subsequent byte");
_Static_assert(AUTOSAVE_RECORD_BYTES == 23184u,
               "autosave register size is a fixed wire contract");
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
 * interval [absolute_offset, absolute_offset + byte_count). bank_name is the
 * eight-cell BankData name; resident_names is filesystem's existing 129-row
 * HCNAMES cache. Output: every byte in the supplied interval is zero first,
 * then only Bank slot/name and Scene/Kit/Instrument names that intersect it
 * are inserted. The function owns no storage and performs no SD I/O.
 */
void autosave_formatInitialChunk(
    uint8_t *dst,
    uint32_t absolute_offset,
    uint16_t byte_count,
    uint16_t bank_slot,
    const char bank_name[AUTOSAVE_NAME_BYTES],
    const char resident_names[AUTOSAVE_HCNAMES_ROW_COUNT]
                             [AUTOSAVE_HCNAMES_ROW_BYTES]);

#endif /* AUTOSAVE_H_ */
