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
 *  S085 C1.1: catalogue v2 (stock block, items, in-place catalogue).
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef SAMPLEMEMORY_H_
#define SAMPLEMEMORY_H_

#include "stm32f4xx.h"
#include "config.h"
#include <stdint.h>

/* ---- Flash floor (unchanged since S073) ----
 * Sectors 7..11 (5 x 256 KB) hold sample flash; sectors 1..6 are the
 * application. SAMPLE_ROM_START_ADDRESS must equal the linker's
 * __sample_flash_start; sampleFlash.c refuses every erase/write otherwise. */
#define SAMPLE_ROM_START_ADDRESS    ((uint32_t)0x080C0000)   /* sector 7 base */
#define SAMPLE_FIRST_SECTOR         7u
#define SAMPLE_PAGE_SIZE            ((uint32_t)0x00040000)   /* 256 KB */
#define SAMPLE_DISPLAY_NAME_LEN     8u

/*
 * Sample flash map, catalogue v2 (S085 C1.1).
 *
 * What:       three regions inside sectors 7..11:
 *             - stock-wave block  0x080C0000  61,120 B (5 waves x 11 levels,
 *               generated at install, C4.3);
 *             - item data         0x080CEEC0  1,243,456 B (samples and
 *               wavetable frames, 4-byte aligned, stored as supplied);
 *             - catalogue         0x081FE800  6,144 B (32 B header, 128
 *               sample entries, 128 wavetable entries, 20 B each = 5,152 B).
 *             Region starts are 32-byte (cache-line) aligned so a D-cache
 *             invalidation never straddles two regions.
 * Why:        plan §4.3.4 (stock waves in sample flash, Q6 (a)); §4.3.3
 *             (128 samples, Q7); the 6,144 B catalogue is the S085 D9 size.
 *             The catalogue is read in place: no RAM copy (the v1 caches,
 *             5,040 B, are released).
 * Inputs:     compile-time constants.
 * Outputs:    addresses for SampleMemory.c, SampleInstall.c, StockWaveGen.c,
 *             StockWaves.c, OscillatorSample.c, OscillatorWavetable.c.
 * Accessors:  the files above; tools/dsp_test/test_catalogue.c (host map).
 * Affiliates: sampleFlash.c floor checks; STORAGE_SRAM_MANIFEST.md flash map.
 */
#define SAMPLE_FLASH_END_ADDRESS        ((uint32_t)0x08200000)
#define SAMPLE_STOCK_START_ADDRESS      SAMPLE_ROM_START_ADDRESS
#define SAMPLE_STOCK_BYTES              ((uint32_t)61120)
#define SAMPLE_DATA_START_ADDRESS       ((uint32_t)0x080CEEC0)
#define SAMPLE_CATALOGUE_START_ADDRESS  ((uint32_t)0x081FE800)
#define SAMPLE_CATALOGUE_BYTES          ((uint32_t)6144)
#define SAMPLE_CATALOGUE_MAGIC          ((uint32_t)0x3243584Cu) /* "LXC2" */
#define SAMPLE_CATALOGUE_VERSION        2u
#define SAMPLE_MAX_COUNT                128u
#define WAVETABLE_MAX_COUNT             128u
#define WAVETABLE_MAX_FRAMES            128u
#define SAMPLE_GAIN_STEPS_PER_OCTAVE    24u

_Static_assert(SAMPLE_STOCK_START_ADDRESS + SAMPLE_STOCK_BYTES <=
               SAMPLE_DATA_START_ADDRESS, "stock block overlaps item data");
_Static_assert((SAMPLE_DATA_START_ADDRESS & 31u) == 0u &&
               (SAMPLE_CATALOGUE_START_ADDRESS & 31u) == 0u,
               "sample-flash regions must be cache-line aligned");
_Static_assert(SAMPLE_CATALOGUE_START_ADDRESS + SAMPLE_CATALOGUE_BYTES ==
               SAMPLE_FLASH_END_ADDRESS, "catalogue ends at the flash end");

/*
 * Host-test address mapping (S085 C1.1).
 *
 * What:       every catalogue, item and stock read goes through
 *             SAMPLE_FLASH_PTR(), so the host test can map flash addresses
 *             onto a RAM image by defining the macro first.
 * Why:        test_catalogue.c installs into a RAM image of sectors 7..11
 *             and reads it back through this unchanged read API.
 * Inputs:     an absolute flash address.  Outputs: a const pointer.
 * Accessors:  SampleMemory.c, OscillatorSample.c, OscillatorWavetable.c,
 *             StockWaves.c.
 * Affiliates: tools/dsp_test/test_catalogue.c.
 */
#ifndef SAMPLE_FLASH_PTR
#define SAMPLE_FLASH_PTR(addr) ((const void *)(uintptr_t)(addr))
#endif

/*
 * Catalogue v2 records (S085 C1.1).
 *
 * What:       the 32 B header and 20 B entries, naturally aligned (no
 *             packing). Entries are programmed one by one as each item
 *             finishes, into erased flash; the header is programmed at
 *             commit, its magic word last, so a catalogue is valid only
 *             after a completed install. Unwritten bytes read 0xFF.
 * Why:        names, gain codes and wavetable geometry are read in place by
 *             the DSP binds and the menu (no RAM index).
 * Inputs:     written by SampleInstall.c.  Outputs: read by SampleMemory.c.
 * Accessors:  SampleMemory.c, SampleInstall.c, test_catalogue.c.
 * Affiliates: FILESYSTEM_SPEC.md "Sample flash", STORAGE_SRAM_MANIFEST.md.
 */
typedef struct {
    uint32_t magic;            /* SAMPLE_CATALOGUE_MAGIC, programmed last */
    uint16_t version;          /* SAMPLE_CATALOGUE_VERSION */
    uint16_t entry_bytes;      /* 20 */
    uint8_t  sample_count;     /* 0..SAMPLE_MAX_COUNT */
    uint8_t  wavetable_count;  /* 0..WAVETABLE_MAX_COUNT */
    uint8_t  stock_valid;      /* 1 = stock block generated */
    uint8_t  reserved0;
    uint32_t data_end;         /* first byte after the last item */
    uint32_t stock_start;      /* SAMPLE_STOCK_START_ADDRESS */
    uint32_t reserved[3];
} sample_catalogue_header_t;

typedef struct {
    uint32_t offset;           /* absolute address of frame 0, 4-aligned */
    uint32_t frames;           /* 16-bit frames (excluding the 2-frame pad) */
    char     name[SAMPLE_DISPLAY_NAME_LEN];
    uint8_t  gain_code;        /* gain = 2^(code/24); never clips */
    uint8_t  reserved[3];
} sample_catalogue_sample_t;

typedef struct {
    uint32_t offset;           /* absolute address of frame 0, 4-aligned */
    uint16_t frame_len;        /* points per frame, >= 2 */
    uint8_t  frame_count;      /* 1..WAVETABLE_MAX_FRAMES */
    uint8_t  bits;             /* 8 or 16 */
    char     name[SAMPLE_DISPLAY_NAME_LEN];
    uint8_t  reserved[4];
} sample_catalogue_wavetable_t;

_Static_assert(sizeof(sample_catalogue_header_t) == 32u, "header is 32 B");
_Static_assert(sizeof(sample_catalogue_sample_t) == 20u, "sample entry is 20 B");
_Static_assert(sizeof(sample_catalogue_wavetable_t) == 20u,
               "wavetable entry is 20 B");

#define SAMPLE_CATALOGUE_SAMPLES_ADDRESS \
    (SAMPLE_CATALOGUE_START_ADDRESS + sizeof(sample_catalogue_header_t))
#define SAMPLE_CATALOGUE_WAVETABLES_ADDRESS \
    (SAMPLE_CATALOGUE_SAMPLES_ADDRESS + \
     SAMPLE_MAX_COUNT * sizeof(sample_catalogue_sample_t))
_Static_assert(SAMPLE_CATALOGUE_WAVETABLES_ADDRESS +
               WAVETABLE_MAX_COUNT * sizeof(sample_catalogue_wavetable_t) <=
               SAMPLE_FLASH_END_ADDRESS, "catalogue exceeds its region");

/*
 * Resolved item descriptions (S085 C1.1).
 *
 * What:       SampleInfo: frames, absolute offset, gain code. WavetableInfo:
 *             offset, frame length, frame count, bit depth. An out-of-range
 *             sample index returns the silence buffer with size 0; an
 *             out-of-range wavetable index returns 0 from the getter.
 * Why:        the DSP binds read these on a catalogue generation or source
 *             change, never per sample.
 * Inputs:     an index.  Outputs: the structs.
 * Accessors:  OscillatorSample.c, OscillatorWavetable.c, InstrumentManager.c
 *             (maximum rules), InstrumentCellFormat.c, menu.c.
 * Affiliates: the catalogue records above.
 */
typedef struct {
    uint32_t size;      /* 16-bit frames; 0 = silence */
    uint32_t offset;    /* absolute flash address (or the silence buffer) */
    uint8_t  gain_code;
} SampleInfo;

typedef struct {
    uint32_t offset;
    uint16_t frame_len;
    uint8_t  frame_count;
    uint8_t  bits;
} WavetableInfo;

/*
 * Catalogue states (S085 C1.1).
 *
 * What:       EMPTY (erased, never installed, or an interrupted install),
 *             V2 (a committed catalogue), STALE (a v1 install from an older
 *             firmware is present).
 * Why:        the boot notice (C3.9) words EMPTY and STALE differently; the
 *             stock waves exist only in V2 (plan §4.3.4).
 * Accessors:  sampleMemory_catalogueState(), main.c.
 */
#define SAMPLE_CATALOGUE_EMPTY  0u
#define SAMPLE_CATALOGUE_V2     1u
#define SAMPLE_CATALOGUE_STALE  2u

/*
 * Catalogue read API (S085 C1.1).
 *
 * What:       init runs the flash probe and one refresh; refresh validates the
 *             header and counts the leading valid entries; the getters return
 *             counts, state, one resolved item, or a name.
 * Why:        Q8 of the S084 plan and S085 §4.3.3: the catalogue is read in
 *             place; the generation stamp is the only invalidation the DSP
 *             binds need.
 * Inputs:     an index / output pointers.  Outputs: values, counts, state.
 * Accessors:  main.c (boot), menu.c, InstrumentManager.c,
 *             InstrumentCellFormat.c, OscillatorSample.c,
 *             OscillatorWavetable.c, StockWaves.c, OscillatorPwm.c,
 *             Oscillator.c, SampleInstall.c.
 * Affiliates: the records above, SAMPLE_FLASH_PTR(), sampleFlash.c.
 */
void       sampleMemory_init(void);
void       sampleMemory_refresh(void);
uint8_t    sampleMemory_catalogueState(void);
uint32_t   sampleMemory_getGeneration(void);
uint8_t    sampleMemory_getNumSamples(void);
SampleInfo sampleMemory_getSampleInfo(uint8_t index);
void       sampleMemory_getDisplayName(uint8_t index,
                                       char name[SAMPLE_DISPLAY_NAME_LEN + 1u]);
uint8_t    sampleMemory_getNumWavetables(void);
uint8_t    sampleMemory_getWavetableInfo(uint8_t index, WavetableInfo *out);
void       sampleMemory_getWavetableName(uint8_t index,
                                         char name[SAMPLE_DISPLAY_NAME_LEN + 1u]);
uint8_t    sampleMemory_stockValid(void);

/*
 * Peak-normalisation gain from the catalogue byte (S085 C1.1).
 *
 * What:       2^(code/24): a 24-entry mantissa table plus an exponent add on
 *             the float bit pattern; about 8 instructions, no libm.
 * Why:        installed samples play at matched levels; applied once per
 *             block per sample oscillator (the per-sample loop already
 *             multiplies by the gain).
 * Inputs:     code 0..255.  Outputs: gain 1.0 .. ~1,600.
 * Accessors:  oscSample_render(), oscSample_renderFm().
 * Affiliates: SampleInstall.c sampleInstall_gainCode() (the inverse).
 */
extern const float sampleMemory_gainMantissa[SAMPLE_GAIN_STEPS_PER_OCTAVE];
static inline float sampleMemory_gainFromCode(uint8_t code)
{
    union { float f; uint32_t u; } g;

    g.f = sampleMemory_gainMantissa[code % SAMPLE_GAIN_STEPS_PER_OCTAVE];
    g.u += (uint32_t)(code / SAMPLE_GAIN_STEPS_PER_OCTAVE) << 23;
    return g.f;
}

#endif /* SAMPLEMEMORY_H_ */
