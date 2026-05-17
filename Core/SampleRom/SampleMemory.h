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
 *      SAMPLE_ROM_START_ADDRESS = 0x08080000   (start of sector 6)
 *      SAMPLE_INFO_START_ADDRESS = metadata area below top of sector 11
 *      SAMPLE_ROM_SIZE          = ~1.5 MB minus info/name tables
 *      SAMPLE_PAGE_SIZE         = 0x00040000   (256 KB / sector)
 *
 *    Sector 6 happens to start at 0x08080000 on F765 — the original
 *    constant is preserved by coincidence. Everything else changes.
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

#include "stm32f4xx.h"
#include "config.h"
#include <stdint.h>

/* ---- Flash layout for F765VI single-bank 2MB ----
 * Sectors 6..11 (6 × 256KB = 1.5 MB) reserved for sample storage.
 * Sample audio data starts at sector 6 base. Info table sits at the
 * top of sector 11 so it's adjacent to the audio data — same single-
 * sector-update optimization the original used. */
#define SAMPLE_ROM_START_ADDRESS    ((uint32_t)0x08080000)   /* sector 6 base */
#define SAMPLE_PAGE_SIZE            ((uint32_t)0x00040000)   /* 256 KB / sector on F765 */
#define SAMPLE_MAX_COUNT            120u
#define SAMPLE_DISPLAY_NAME_LEN     8u

/* SampleInfo struct: original comment claimed 7 bytes, but with default
 * struct alignment the original shape was already 12 bytes. The F765 port
 * widens size to 32 bits while preserving that 12-byte footprint:
 * 3-char name + 1 pad + 4-byte size + 4-byte offset.
 *
 * Playback still uses the original 32-bit oscillator phase path, so long
 * samples may loop until the larger playback-index spike lands. */
#define SAMPLE_INFO_SIZE            ((uint32_t)(SAMPLE_MAX_COUNT * sizeof(SampleInfo)))
#define SAMPLE_NAME_SIZE            ((uint32_t)(SAMPLE_MAX_COUNT * SAMPLE_DISPLAY_NAME_LEN))
#define SAMPLE_NAME_START_ADDRESS   ((uint32_t)(0x08200000 - SAMPLE_NAME_SIZE))
#define SAMPLE_INFO_START_ADDRESS   ((uint32_t)(SAMPLE_NAME_START_ADDRESS - SAMPLE_INFO_SIZE))

/* Audio region runs from sector 6 base to just below the info table. */
#define SAMPLE_ROM_SIZE             ((uint32_t)(SAMPLE_INFO_START_ADDRESS - SAMPLE_ROM_START_ADDRESS))

#define SAMPLE_INFO_LOOP_FLAG       ((uint32_t)0x80000000)
#define SAMPLE_INFO_SIZE_MASK       ((uint32_t)0x7fffffff)

typedef struct SampleInfoStruct
{
    char     name[3];   /* 3-char sample name */
    uint32_t size;      /* size in 16-bit samples (NOT bytes) */
    uint32_t offset;    /* absolute flash address of the sample data */
} SampleInfo;

/* ----------------- public API ----------------- */
void       sampleMemory_init(void);
void       sampleMemory_loadSamples(void);
SampleInfo sampleMemory_getSampleInfo(uint8_t index);
uint8_t    sampleMemory_getNumSamples(void);
uint32_t   sampleMemory_getGeneration(void);
void       sampleMemory_setNumSamples(uint8_t num);
void       sampleMemory_getDisplayName(uint8_t index, char name[SAMPLE_DISPLAY_NAME_LEN + 1u]);

int        sampleMemory_installBegin(void);
int        sampleMemory_installAppendBegin(void);
int        sampleMemory_installStartSample(const char name[3],
                                           const char displayName[SAMPLE_DISPLAY_NAME_LEN],
                                           uint8_t looped,
                                           uint32_t dataBytes);
int        sampleMemory_installWriteData(const uint8_t *data, uint32_t bytes);
int        sampleMemory_installFinishSample(void);
int        sampleMemory_installCommit(void);
uint32_t   sampleMemory_installBytesFree(void);
void       sampleMemory_refresh(void);
uint8_t    sampleMemory_isLooped(uint8_t index);

#endif /* SAMPLEMEMORY_H_ */
