/*
 * SampleMemory.h — LXR-02 port.
 *
 * Adapted from original LXR mainboard SampleMemory.h. The public API is
 * preserved 1:1 so DSP code (Oscillator.c, voice files) ports verbatim:
 *
 *   sampleMemory_init()
 *   sampleMemory_loadSamples()
 *   sampleMemory_getSampleInfo(uint8_t index)
 *   sampleMemory_getNumSamples()
 *   sampleMemory_setNumSamples(uint8_t num)
 *   typedef struct SampleInfoStruct { ... } SampleInfo;
 *
 * What changed from the original:
 *
 * 1. ADDRESS LAYOUT corrected for STM32F765VIH (single-bank, 2MB):
 *
 *    Original (F407, 1MB):
 *      SAMPLE_ROM_START_ADDRESS = 0x08080000
 *      SAMPLE_INFO_START_ADDRESS = 0x080F9E70 (top of sector 11)
 *      SAMPLE_ROM_SIZE          = 0x00078E70 (~485 KB)
 *      SAMPLE_PAGE_SIZE         = 0x00020000 (128 KB / sector)
 *
 *    Ours (F765, 2MB, validated by memtest in Session 6):
 *      SAMPLE_ROM_START_ADDRESS = 0x080C0000   (start of sector 7, S073)
 *      SAMPLE_INFO_START_ADDRESS = metadata area below top of sector 11
 *      SAMPLE_ROM_SIZE          = ~1.25 MB minus info/name tables
 *      SAMPLE_PAGE_SIZE         = 0x00040000   (256 KB / sector)
 *
 *    Until Session 073 samples started at sector 6 (0x08080000, the
 *    original constant by coincidence). S073 gave sector 6 to the
 *    application; old installs are rejected by sampleMemory_infoValid()
 *    because their first entry sits below the new floor.
 *
 * 2. REAL IMPLEMENTATION with guarded silence fallback.
 *
 *    SampleMemory.c validates the count word and SampleInfo table at boot.
 *    Invalid or out-of-range entries return a small rodata silence buffer, so
 *    legacy oscillator code can never hard-fault on corrupt sample metadata.
 *
 * 3. CONTRACT preserved from original: erase/program operations stall
 *    flash reads on the same bank until they complete (~2s for a 256KB
 *    erase). DSP voices read sampleMemory_data during audio rendering;
 *    a sample install during audio playback would freeze audio. The
 *    convention from original LXR — sample install only when sequencer
 *    is stopped — must be honoured.
 *
 * 4. SD_Manager.h dependency REMOVED. Original used an AVR-side SD
 *    abstraction (sd_getNumSamples, sd_setActiveSample, etc.). The port's
 *    modal installer lives behind Core/Hardware/SD/filesystem.c.
 */

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef SAMPLEMEMORY_H_
#define SAMPLEMEMORY_H_

/* Host catalogue tests define SAMPLEMEMORY_HOST_TEST and map flash onto RAM. */
#ifndef SAMPLEMEMORY_HOST_TEST
#include "stm32f4xx.h"
#include "config.h"
#endif
#include <stdint.h>

/* ---- Flash layout for F765VI single-bank 2MB ----
 * Sectors 7..11 (5 × 256KB = 1.25 MB) reserved for sample storage; sectors
 * 1..6 are the application (S073). Sample audio data starts at sector 7
 * base. Info table sits at the top of sector 11 so it's adjacent to the
 * audio data — same single-sector-update optimization the original used.
 * SAMPLE_ROM_START_ADDRESS must equal the linker's __sample_flash_start
 * (STM32F765VIHx_FLASH.ld); sampleFlash.c refuses to erase or write if they
 * differ, and asserts that it is the base of SAMPLE_FIRST_SECTOR. */
#define SAMPLE_ROM_START_ADDRESS    ((uint32_t)0x080C0000)   /* sector 7 base */
#define SAMPLE_FIRST_SECTOR         7u   /* lowest sector sample code may erase */
#define SAMPLE_PAGE_SIZE            ((uint32_t)0x00040000)   /* 256 KB / sector on F765 */
#define SAMPLE_DISPLAY_NAME_LEN     8u

/*
 * Sample flash catalogue v2 (S087 stage C).
 *
 * What:       the item region from SAMPLE_ROM_START_ADDRESS to
 *             SAMPLE_CATALOGUE_START_ADDRESS, and an 8 KB catalogue (32 B
 *             header, 128 x 28 B sample entries, 128 x 20 B wavetable entries)
 *             read in place, no RAM copy. The header magic is programmed
 *             last, so a catalogue is valid only after a completed install.
 * Why:        S087 P0 §5, A11: per-item loop points, gain code, wavetable
 *             geometry and flags for the bind; the v1 120-entry manifest and
 *             its RAM caches (5,040 B) go away.
 * Inputs:     SampleInstall.c writes it during the modal install.
 * Outputs:    read by the oscillator bind, the menu and InstrumentManager.
 * Accessors:  the read API below.
 * Affiliates: sampleFlash.c (erase/program), FILESYSTEM_SPEC.md,
 *             STORAGE_SRAM_MANIFEST.md, tools/dsp_test/test_catalogue.c.
 */
#define SAMPLE_CATALOGUE_START_ADDRESS ((uint32_t)0x081FE000)
#define SAMPLE_CATALOGUE_END_ADDRESS   ((uint32_t)0x08200000)
#define SAMPLE_CATALOGUE_SIZE          ((uint32_t)(SAMPLE_CATALOGUE_END_ADDRESS - \
                                                  SAMPLE_CATALOGUE_START_ADDRESS))
#define SAMPLE_CATALOGUE_MAGIC         ((uint32_t)0x3243584C) /* "LXC2" */
#define SAMPLE_CATALOGUE_VERSION       2u
#define SAMPLE_ENTRY_BYTES             28u
#define WAVETABLE_ENTRY_BYTES          20u
#define SAMPLE_MAX_COUNT               128u
#define WAVETABLE_MAX_COUNT            128u
#define WAVETABLE_MAX_FRAMES           128u
#define WAVETABLE_FRAME_MIN            16u
#define WAVETABLE_FRAME_MAX            2048u
#define SAMPLE_GAIN_STEPS_PER_OCTAVE   24u
/* 2^19 - 2 points keeps every Q19.12 sample phase below 2^31. */
#define SAMPLE_MAX_FRAMES              524286u

/*
 * Catalogue records (S087 stage C).
 *
 * What:       the header, one sample entry and one wavetable entry, exactly as
 *             stored in sample flash (little-endian, native layout).
 * Why:        the oscillator bind reads these in place; the entry sizes are
 *             pinned so a layout change is a compile error, not a silent
 *             flash-format change.
 * Inputs:     SampleInstall.c.  Outputs: the read API below.
 * Accessors:  sampleMemory_sampleEntry(), sampleMemory_wavetableEntry().
 * Affiliates: S087 P0 A11; STORAGE_SRAM_MANIFEST.md.
 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t  sample_entry_bytes;
    uint8_t  wavetable_entry_bytes;
    uint8_t  sample_count;
    uint8_t  wavetable_count;
    uint8_t  flags;
    uint8_t  reserved0;
    uint32_t data_end;
    uint32_t reserved[4];
} sample_catalogue_header_t;

typedef struct {
    uint32_t offset;
    uint32_t frames;
    uint32_t loop_start;
    uint32_t loop_end;      /* exclusive */
    char     name[SAMPLE_DISPLAY_NAME_LEN];
    uint8_t  gain_code;
    uint8_t  flags;         /* bit 0: smpl loop present; bit 1: bitmap */
    uint8_t  reserved[2];
} sample_catalogue_entry_t;

typedef struct {
    uint32_t offset;
    uint16_t frame_len;
    uint8_t  frame_count;
    uint8_t  bits;          /* 8 or 16 */
    char     name[SAMPLE_DISPLAY_NAME_LEN];
    uint8_t  flags;         /* bit 1: bitmap; bits 4-7: mip levels */
    uint8_t  reserved[3];
} wavetable_catalogue_entry_t;

_Static_assert(sizeof(sample_catalogue_header_t) == 32u,
               "catalogue header must stay 32 bytes");
_Static_assert(sizeof(sample_catalogue_entry_t) == SAMPLE_ENTRY_BYTES,
               "sample entry must stay 28 bytes");
_Static_assert(sizeof(wavetable_catalogue_entry_t) == WAVETABLE_ENTRY_BYTES,
               "wavetable entry must stay 20 bytes");
_Static_assert(SAMPLE_CATALOGUE_START_ADDRESS +
               sizeof(sample_catalogue_header_t) +
               SAMPLE_MAX_COUNT * SAMPLE_ENTRY_BYTES +
               WAVETABLE_MAX_COUNT * WAVETABLE_ENTRY_BYTES <=
               SAMPLE_CATALOGUE_END_ADDRESS,
               "catalogue v2 does not fit its 8 KB region");
/* The last playable point's phase stays below 2^31; the two pad frames are
 * read after the end, never phased. */
_Static_assert((SAMPLE_MAX_FRAMES << 12u) < 0x7FFFFFFFu,
               "Q19.12 sample phase must stay below 2^31");

/*
 * Flash-to-pointer map (S087 stage C).
 *
 * What:       converts a catalogue/item flash address to a readable pointer.
 * Why:        the host catalogue test compiles SampleMemory.c against a RAM
 *             image of sectors 7-11 and overrides this macro; on the device it
 *             is the identity cast.
 * Inputs:     an absolute sample-flash address. Output: a const pointer.
 * Accessors:  SampleMemory.c, SampleInstall.c, the oscillator bind.
 * Affiliates: test_catalogue.c.
 */
#ifndef SAMPLE_FLASH_PTR
#define SAMPLE_FLASH_PTR(address) ((const uint8_t *)(uintptr_t)(address))
#endif

/* Catalogue state for the boot notice (C-21). */
typedef enum {
    SAMPLE_CATALOGUE_EMPTY = 0,
    SAMPLE_CATALOGUE_V2,
    SAMPLE_CATALOGUE_STALE
} sample_catalogue_state_t;

/* 2^(k/24) for k = 0..23; the gain code adds whole octaves on top. */
extern const float sampleMemory_gainMantissa[SAMPLE_GAIN_STEPS_PER_OCTAVE];

/*
 * Gain from a catalogue gain code (S087 stage C).
 *
 * What:       returns 2^(code/24) as a float, using the mantissa table and a
 *             power-of-two shift for the octave part.
 * Why:        the installer normalises the peak at install; playback applies
 *             one gain per block with no level analysis.
 * Inputs:     code 0..255 (0 = unity). Outputs: gain >= 1.
 * Accessors:  the typed renderers. Affiliates: sampleInstall_gainCode().
 */
static inline float sampleMemory_gainFromCode(uint8_t code)
{
    return sampleMemory_gainMantissa[code % SAMPLE_GAIN_STEPS_PER_OCTAVE] *
           (float)(1u << (code / SAMPLE_GAIN_STEPS_PER_OCTAVE));
}

/* ----------------- public API ----------------- */
void       sampleMemory_init(void);
void       sampleMemory_loadSamples(void);
void       sampleMemory_refresh(void);
uint8_t    sampleMemory_getNumSamples(void);
uint8_t    sampleMemory_getNumWavetables(void);
uint32_t   sampleMemory_getGeneration(void);
sample_catalogue_state_t sampleMemory_catalogueState(void);
const sample_catalogue_entry_t *sampleMemory_sampleEntry(uint8_t index);
const wavetable_catalogue_entry_t *sampleMemory_wavetableEntry(uint8_t index);
void       sampleMemory_getDisplayName(uint8_t index,
                                       char name[SAMPLE_DISPLAY_NAME_LEN + 1u]);
void       sampleMemory_getWavetableName(uint8_t index,
                                         char name[SAMPLE_DISPLAY_NAME_LEN + 1u]);

#endif /* SAMPLEMEMORY_H_ */
