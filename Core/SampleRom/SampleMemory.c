/*
 * SampleMemory.c - LXR-02 sample-flash catalogue reader.
 *
 * The catalogue is read in place from flash (S084 VX2 Q8): this file keeps
 * only the counts, the generation stamp and the state flag, and resolves one
 * item at a time on request. The installer is intentionally modal and lives
 * in SampleInstall.c; the menu suspends audio before filesystem code touches
 * the single-bank internal flash region.
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

#include "SampleMemory.h"
#include "sampleFlash.h"
#include <string.h>

/* Guarded silence returned for every out-of-range sample index. */
static const int16_t stub_silence_buffer[256] = { 0 };

/*
 * Catalogue state (S084 VX2 VX2-1.3).
 *
 * What:       the installed counts, the catalogue state (empty / v2 / stale),
 *             the stock-block flag and the generation stamp. No item data is
 *             cached; the getters read the flash records directly.
 * Why:        Q8 removed the v1 RAM caches (5,040 B). The generation stamp is
 *             the only invalidation the DSP bindings need.
 * Writers:    sampleMemory_refresh() only.
 * Readers:    every getter, the DSP binds and the boot notice.
 */
static uint8_t  sample_count = 0u;
static uint8_t  sample_wavetable_count = 0u;
static uint8_t  sample_catalogue_state = SAMPLE_CATALOGUE_EMPTY;
static uint8_t  sample_stock_valid = 0u;
static uint32_t sample_generation = 1u;

/*
 * 2^(i/24) mantissas for the gain code (S084 VX2 VX2-1.3).
 *
 * What:       the fractional part of the catalogue gain; the code's octave
 *             part multiplies it by 2^n through the float exponent.
 * Why:        D10 / Q15 need about 8 instructions in a block-rate path,
 *             without libm. 96 B of flash.
 * Accessors:  sampleMemory_gainFromCode().
 */
const float sampleMemory_gainMantissa[SAMPLE_GAIN_STEPS_PER_OCTAVE] = {
    /* 2^(i/24), i = 0..23 */
    1.000000f, 1.029302f, 1.059463f, 1.090508f, 1.122462f, 1.155353f,
    1.189207f, 1.224054f, 1.259921f, 1.296840f, 1.334840f, 1.373954f,
    1.414214f, 1.455653f, 1.498307f, 1.542211f, 1.587401f, 1.633915f,
    1.681793f, 1.731073f, 1.781797f, 1.834008f, 1.887749f, 1.943064f,
};

static SampleInfo sampleMemory_silenceInfo(void)
{
    SampleInfo info;

    memset(&info, 0, sizeof(info));
    info.offset = (uint32_t)(uintptr_t)stub_silence_buffer;
    info.size = 0u;
    return info;
}

/*
 * Catalogue entry bounds (S084 VX2 VX2-1.3).
 *
 * What:       a sample entry is valid when its 4-aligned offset lies inside
 *             the item region and its frames fit before the catalogue; a
 *             wavetable entry additionally needs 2..128 frames of 8 or 16
 *             bits per frame and the whole table inside the region.
 * Why:        the reader stops at the first invalid entry, so a torn install
 *             can never hand the DSP a pointer outside the flash.
 * Inputs:     one flash record.  Outputs: 0/1.
 * Accessors:  sampleMemory_refresh().
 * Affiliates: SampleInstall.c (writes entries only inside these bounds).
 */
static uint8_t sampleMemory_entryValid(const sample_catalogue_sample_t *e)
{
    uint64_t end;

    if (!e)
        return 0u;
    if (e->offset < SAMPLE_DATA_START_ADDRESS ||
        e->offset >= SAMPLE_CATALOGUE_START_ADDRESS)
        return 0u;
    if ((e->offset & 3u) != 0u)
        return 0u;
    if (e->frames == 0u)
        return 0u;
    end = (uint64_t)e->offset + (uint64_t)e->frames * 2u;
    return (uint8_t)(end <= SAMPLE_CATALOGUE_START_ADDRESS);
}

static uint8_t sampleMemory_tableValid(const sample_catalogue_wavetable_t *e)
{
    uint64_t bytes;
    uint64_t end;

    if (!e)
        return 0u;
    if (e->offset < SAMPLE_DATA_START_ADDRESS ||
        e->offset >= SAMPLE_CATALOGUE_START_ADDRESS)
        return 0u;
    if ((e->offset & 3u) != 0u)
        return 0u;
    if (e->frame_len < 2u)
        return 0u;
    if (e->frame_count == 0u || e->frame_count > WAVETABLE_MAX_FRAMES)
        return 0u;
    if (e->bits != 8u && e->bits != 16u)
        return 0u;
    bytes = (uint64_t)e->frame_len * (uint64_t)e->frame_count *
            (uint64_t)(e->bits / 8u);
    end = (uint64_t)e->offset + bytes;
    return (uint8_t)(end <= SAMPLE_CATALOGUE_START_ADDRESS);
}

/*
 * Re-read catalogue v2 from flash (S084 VX2 VX2-1.3).
 *
 * What:       validates the header (magic, version, entry size), counts
 *             the leading valid sample and wavetable entries, records the
 *             stock flag and bumps the generation so every DSP binding
 *             refreshes. Without a v2 header it reports STALE when a v1
 *             count word is present, else EMPTY.
 * Why:        Q8: no RAM index; Q7: v1 installs are not read. The
 *             generation stamp is the only invalidation DSP caches need.
 * Inputs:     sample flash.  Outputs: counts, state, generation.
 * Accessors:  sampleMemory_init() (boot), sampleInstall_commit().
 * Affiliates: oscSample_bind(), oscWavetable_bind(), transient bind,
 *             main.c boot notice (VX2-3.12).
 */
void sampleMemory_refresh(void)
{
    const sample_catalogue_header_t *header =
        (const sample_catalogue_header_t *)
            SAMPLE_FLASH_PTR(SAMPLE_CATALOGUE_START_ADDRESS);
    uint8_t limit;
    uint8_t i;

    sample_generation++;
    if (sample_generation == 0u)
        sample_generation = 1u;

    sample_count = 0u;
    sample_wavetable_count = 0u;
    sample_stock_valid = 0u;
    sample_catalogue_state = SAMPLE_CATALOGUE_EMPTY;

    if (header->magic == SAMPLE_CATALOGUE_MAGIC &&
        header->version == SAMPLE_CATALOGUE_VERSION &&
        header->entry_bytes == (uint16_t)sizeof(sample_catalogue_sample_t)) {
        sample_catalogue_state = SAMPLE_CATALOGUE_V2;
        sample_stock_valid = (uint8_t)(header->stock_valid ? 1u : 0u);

        limit = header->sample_count;
        if (limit > SAMPLE_MAX_COUNT)
            limit = (uint8_t)SAMPLE_MAX_COUNT;
        for (i = 0u; i < limit; i++) {
            const sample_catalogue_sample_t *entry =
                (const sample_catalogue_sample_t *)
                    SAMPLE_FLASH_PTR(SAMPLE_CATALOGUE_SAMPLES_ADDRESS +
                                     (uint32_t)i *
                                         sizeof(sample_catalogue_sample_t));

            if (!sampleMemory_entryValid(entry))
                break;
            sample_count++;
        }

        limit = header->wavetable_count;
        if (limit > WAVETABLE_MAX_COUNT)
            limit = (uint8_t)WAVETABLE_MAX_COUNT;
        for (i = 0u; i < limit; i++) {
            const sample_catalogue_wavetable_t *entry =
                (const sample_catalogue_wavetable_t *)
                    SAMPLE_FLASH_PTR(SAMPLE_CATALOGUE_WAVETABLES_ADDRESS +
                                     (uint32_t)i *
                                         sizeof(sample_catalogue_wavetable_t));

            if (!sampleMemory_tableValid(entry))
                break;
            sample_wavetable_count++;
        }
    } else {
        const uint32_t first =
            *(const uint32_t *)SAMPLE_FLASH_PTR(SAMPLE_ROM_START_ADDRESS);

        /* A v1 install stored its sample count in the first word. */
        if (first >= 1u && first <= 120u)
            sample_catalogue_state = SAMPLE_CATALOGUE_STALE;
    }
}

void sampleMemory_init(void)
{
    sampleFlash_init();
    sampleMemory_refresh();
}

uint8_t sampleMemory_catalogueState(void)
{
    return sample_catalogue_state;
}

uint8_t sampleMemory_stockValid(void)
{
    return sample_stock_valid;
}

uint8_t sampleMemory_getNumSamples(void)
{
    return sample_count;
}

uint8_t sampleMemory_getNumWavetables(void)
{
    return sample_wavetable_count;
}

uint32_t sampleMemory_getGeneration(void)
{
    return sample_generation;
}

SampleInfo sampleMemory_getSampleInfo(uint8_t index)
{
    const sample_catalogue_sample_t *entry;

    if (index >= sample_count)
        return sampleMemory_silenceInfo();
    entry = (const sample_catalogue_sample_t *)
        SAMPLE_FLASH_PTR(SAMPLE_CATALOGUE_SAMPLES_ADDRESS +
                         (uint32_t)index *
                             sizeof(sample_catalogue_sample_t));
    {
        SampleInfo info;

        info.size = entry->frames;
        info.offset = entry->offset;
        info.gain_code = entry->gain_code;
        return info;
    }
}

void sampleMemory_getDisplayName(uint8_t index, char name[SAMPLE_DISPLAY_NAME_LEN + 1u])
{
    if (name == 0)
        return;

    if (index >= sample_count) {
        memset(name, ' ', SAMPLE_DISPLAY_NAME_LEN);
        name[SAMPLE_DISPLAY_NAME_LEN] = '\0';
        return;
    }
    {
        const sample_catalogue_sample_t *entry =
            (const sample_catalogue_sample_t *)
                SAMPLE_FLASH_PTR(SAMPLE_CATALOGUE_SAMPLES_ADDRESS +
                                 (uint32_t)index *
                                     sizeof(sample_catalogue_sample_t));

        memcpy(name, entry->name, SAMPLE_DISPLAY_NAME_LEN);
        name[SAMPLE_DISPLAY_NAME_LEN] = '\0';
    }
}

uint8_t sampleMemory_getWavetableInfo(uint8_t index, WavetableInfo *out)
{
    const sample_catalogue_wavetable_t *entry;

    if (!out || index >= sample_wavetable_count)
        return 0u;
    entry = (const sample_catalogue_wavetable_t *)
        SAMPLE_FLASH_PTR(SAMPLE_CATALOGUE_WAVETABLES_ADDRESS +
                         (uint32_t)index *
                             sizeof(sample_catalogue_wavetable_t));
    out->offset = entry->offset;
    out->frame_len = entry->frame_len;
    out->frame_count = entry->frame_count;
    out->bits = entry->bits;
    return 1u;
}

void sampleMemory_getWavetableName(uint8_t index, char name[SAMPLE_DISPLAY_NAME_LEN + 1u])
{
    if (name == 0)
        return;

    if (index >= sample_wavetable_count) {
        memset(name, ' ', SAMPLE_DISPLAY_NAME_LEN);
        name[SAMPLE_DISPLAY_NAME_LEN] = '\0';
        return;
    }
    {
        const sample_catalogue_wavetable_t *entry =
            (const sample_catalogue_wavetable_t *)
                SAMPLE_FLASH_PTR(SAMPLE_CATALOGUE_WAVETABLES_ADDRESS +
                                 (uint32_t)index *
                                     sizeof(sample_catalogue_wavetable_t));

        memcpy(name, entry->name, SAMPLE_DISPLAY_NAME_LEN);
        name[SAMPLE_DISPLAY_NAME_LEN] = '\0';
    }
}
