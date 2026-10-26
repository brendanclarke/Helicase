/*
 * SampleMemory.h — LXR-02 port.
 *
 * Public read API for the sample-flash catalogue. The DSP (Oscillator.c and
 * the voice files) reads installed samples through it; the installer writes
 * through SampleInstall.h. The catalogue is read in place from flash (Q8),
 * so there is no RAM index of the installed items.
 *
 *  Modified on: 25.10.2026
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

#include "stm32f4xx.h"
#include "config.h"
#include <stdint.h>

/* ---- Flash layout for F765VI single-bank 2MB ----
 * Sectors 7..11 (5 x 256KB = 1.25 MB) are reserved for sample storage;
 * sectors 1..6 are the application (S073).
 *
 *    Ours (F765, 2MB), catalogue v2 (S084 VX2 VX2-1.1):
 *      0x080C0000  stock-wave block, 61,120 B (5 waves x 11 levels,
 *                  generated at install by StockWaveGen.c)
 *      0x080CEEC0  item data: samples and wavetables, packed upward
 *      0x081FE000  catalogue v2: 32 B header, 233 sample entries and
 *                  128 wavetable entries of 20 B (7,252 of 8,192 B)
 *    A v1 install (count word at 0x080C0000, info table near the top) is
 *    not read: the v2 header magic is absent, so the catalogue reports
 *    STALE until the next Load:[Samples] (plan Q7: nothing is retained).
 *
 * SAMPLE_ROM_START_ADDRESS must equal the linker's __sample_flash_start
 * (STM32F765VIHx_FLASH.ld); sampleFlash.c refuses to erase or write if they
 * differ, and asserts that it is the base of SAMPLE_FIRST_SECTOR.
 *
 * Erase/program operations stall flash reads on the same bank until they
 * complete (~2s for a 256KB erase). DSP voices read sample data during audio
 * rendering, so an install runs only with audio suspended (the Load:[Samples]
 * modal). */
#define SAMPLE_ROM_START_ADDRESS    ((uint32_t)0x080C0000)   /* sector 7 base */
#define SAMPLE_FIRST_SECTOR         7u   /* lowest sector sample code may erase */
#define SAMPLE_PAGE_SIZE            ((uint32_t)0x00040000)   /* 256 KB / sector on F765 */
#define SAMPLE_DISPLAY_NAME_LEN     8u

/*
 * Sample-flash map, catalogue v2 (S084 VX2 VX2-1.2).
 *
 * What:       fixed regions inside sectors 7..11: the stock-wave block at
 *             the floor, item data above it, and the catalogue in the top
 *             8 KB of sector 11. Every boundary is 32-byte aligned so cache
 *             invalidation never straddles a region.
 * Why:        P2-A: the flash must hold the generated stock waves, named and
 *             gain-tagged samples, and wavetables stored exactly as supplied
 *             (W1), with no RAM copy of the index (Q8).
 * Inputs:     none (compile-time constants).
 * Outputs:    addresses used by SampleMemory.c, SampleInstall.c,
 *             StockWaveGen.c and StockWaves.c.
 * Accessors:  the files above; tools/dsp_test/test_catalogue.c (host map).
 * Affiliates: sampleFlash.c floor checks (SAMPLE_ROM_START_ADDRESS,
 *             SAMPLE_FIRST_SECTOR), STORAGE_SRAM_MANIFEST.md flash map.
 */
#define SAMPLE_FLASH_END_ADDRESS        ((uint32_t)0x08200000)
#define SAMPLE_STOCK_START_ADDRESS      SAMPLE_ROM_START_ADDRESS
#define SAMPLE_STOCK_BYTES              ((uint32_t)61120)
#define SAMPLE_DATA_START_ADDRESS       ((uint32_t)0x080CEEC0)
#define SAMPLE_CATALOGUE_START_ADDRESS  ((uint32_t)0x081FE000)
#define SAMPLE_CATALOGUE_BYTES          ((uint32_t)8192)
#define SAMPLE_CATALOGUE_MAGIC          ((uint32_t)0x3243584Cu) /* "LXC2" */
#define SAMPLE_CATALOGUE_VERSION        2u
#define SAMPLE_MAX_COUNT                233u
#define WAVETABLE_MAX_COUNT             128u
#define WAVETABLE_MAX_FRAMES            128u
#define SAMPLE_GAIN_STEPS_PER_OCTAVE    24u

_Static_assert(SAMPLE_STOCK_START_ADDRESS + SAMPLE_STOCK_BYTES <=
               SAMPLE_DATA_START_ADDRESS, "stock block overlaps item data");
_Static_assert((SAMPLE_DATA_START_ADDRESS & 31u) == 0u &&
               (SAMPLE_CATALOGUE_START_ADDRESS & 31u) == 0u,
               "sample-flash regions must be cache-line aligned");

/*
 * Host-test address mapping (S084 VX2 VX2-1.2).
 *
 * What:       every catalogue and item read goes through SAMPLE_FLASH_PTR()
 *             so the host test can map flash addresses onto an array.
 * Why:        test_catalogue.c installs into a RAM image of sectors 7..11
 *             and reads it back through the unchanged read API.
 * Inputs:     an absolute flash address.  Outputs: a pointer.
 * Accessors:  SampleMemory.c, OscillatorSample.c, OscillatorWavetable.c,
 *             StockWaves.c, transientGenerator.c.
 * Affiliates: tools/dsp_test/test_catalogue.c (defines it first).
 */
#ifndef SAMPLE_FLASH_PTR
#define SAMPLE_FLASH_PTR(addr) ((const void *)(uintptr_t)(addr))
#endif

/*
 * Catalogue v2 records in flash (S084 VX2 VX2-1.2).
 *
 * What:       the 32 B header and 20 B entries, naturally aligned (no
 *             packing). The header's magic is programmed last, so a
 *             catalogue is valid only after a completed commit. Entries are
 *             programmed one by one as each item finishes, into erased
 *             flash; unwritten bytes read 0xFF.
 * Why:        P2-A: version header, sample entries (offset, size, name, gain
 *             byte), wavetable entries (name, frame count, frame length, bit
 *             depth, offset) and the stock block flag, read in place.
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
    uint32_t frames;           /* 16-bit frames */
    char     name[SAMPLE_DISPLAY_NAME_LEN];
    uint8_t  gain_code;        /* gain = 2^(code/24), never clips */
    uint8_t  reserved[3];
} sample_catalogue_sample_t;

typedef struct {
    uint32_t offset;           /* absolute address of frame 0 */
    uint16_t frame_len;        /* points per frame, >= 2 */
    uint8_t  frame_count;      /* 1..WAVETABLE_MAX_FRAMES */
    uint8_t  bits;             /* 8 or 16 */
    char     name[SAMPLE_DISPLAY_NAME_LEN];
    uint8_t  reserved[4];
} sample_catalogue_wavetable_t;

_Static_assert(sizeof(sample_catalogue_header_t) == 32u, "header is 32 B");
_Static_assert(sizeof(sample_catalogue_sample_t) == 20u, "sample entry is 20 B");
_Static_assert(sizeof(sample_catalogue_wavetable_t) == 20u, "wavetable entry is 20 B");

#define SAMPLE_CATALOGUE_SAMPLES_ADDRESS \
    (SAMPLE_CATALOGUE_START_ADDRESS + sizeof(sample_catalogue_header_t))
#define SAMPLE_CATALOGUE_WAVETABLES_ADDRESS \
    (SAMPLE_CATALOGUE_SAMPLES_ADDRESS + \
     SAMPLE_MAX_COUNT * sizeof(sample_catalogue_sample_t))
_Static_assert(SAMPLE_CATALOGUE_WAVETABLES_ADDRESS +
               WAVETABLE_MAX_COUNT * sizeof(sample_catalogue_wavetable_t) <=
               SAMPLE_FLASH_END_ADDRESS, "catalogue exceeds its region");

/*
 * Resolved sample and wavetable descriptions (S084 VX2 VX2-1.2).
 *
 * What:       SampleInfo: size in 16-bit frames, absolute offset, gain code.
 *             WavetableInfo: offset, frame length, frame count, bit depth.
 *             An out-of-range index returns the silence buffer with size 0
 *             (SampleInfo) or 0 (getWavetableInfo).
 * Why:        the DSP binds read these on a catalogue generation or source
 *             change, never per sample.
 * Inputs:     an index.  Outputs: the structs.
 * Accessors:  OscillatorSample.c, OscillatorWavetable.c,
 *             transientGenerator.c, InstrumentManager.c (max rules), menu.c.
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

#define SAMPLE_CATALOGUE_EMPTY  0u   /* erased, or no completed install */
#define SAMPLE_CATALOGUE_V2     1u
#define SAMPLE_CATALOGUE_STALE  2u   /* a v1 install is present */

/*
 * Catalogue read API (S084 VX2 VX2-1.2).
 *
 * What:       init runs the flash probe and one refresh; refresh validates
 *             the v2 header and counts the leading valid entries; the getters
 *             return counts, state and one resolved item.
 * Why:        Q8: the catalogue is read in place; there is no RAM cache. The
 *             generation stamp is the only invalidation DSP bindings need.
 * Inputs:     an index / output pointers.  Outputs: values, counts, state.
 * Accessors:  main.c (boot), menu.c, InstrumentManager.c, OscillatorSample.c,
 *             OscillatorWavetable.c, transientGenerator.c, SampleInstall.c.
 * Affiliates: the catalogue records, SAMPLE_FLASH_PTR(), sampleFlash.c.
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
 * Peak-normalisation gain from the catalogue byte (S084 VX2 VX2-1.2).
 *
 * What:       2^(code/24): a 24-entry mantissa table plus an exponent add
 *             on the float's bit pattern; about 8 instructions, no libm.
 * Why:        Q15: the catalogue gain is applied wherever a sample plays
 *             (smp/slp oscillators and sample transients). Block rate.
 * Inputs:     code 0..255.  Outputs: gain 1.0 .. ~1,600.
 * Accessors:  oscSample_render(_Fm), transient_calcBlock().
 * Affiliates: SampleInstall.c (computes the code), sampleMemory_gainMantissa.
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
