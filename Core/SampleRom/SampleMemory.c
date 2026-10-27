/*
 * SampleMemory.c - LXR-02 user sample flash table/runtime API.
 *
 * The installer is intentionally modal. The menu suspends audio before
 * filesystem code erases/programs the single-bank internal flash region.
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


#include "SampleMemory.h"
#include "sampleFlash.h"
#include <string.h>

/* Guarded silence returned for every out-of-range sample index. */
static const int16_t stub_silence_buffer[256] = { 0 };

/*
 * Catalogue state (S085 C1.2).
 *
 * What:       installed counts, catalogue state, stock-block flag and the
 *             generation stamp. No item data is cached; the getters read the
 *             flash records directly.
 * Why:        S085 §4.3.3; the v1 RAM caches (5,040 B) are released and not
 *             reused (RAM policy). The generation stamp is the only
 *             invalidation the DSP binds need.
 * Writers:    sampleMemory_refresh() only.
 * Readers:    every getter, the DSP binds and the boot notice.
 */
static uint8_t  sample_count = 0u;
static uint8_t  sample_wavetable_count = 0u;
static uint8_t  sample_catalogue_state = SAMPLE_CATALOGUE_EMPTY;
static uint8_t  sample_stock_valid = 0u;
static uint32_t sample_generation = 1u;

/*
 * 2^(i/24) mantissas for the gain code (S085 C1.2).
 *
 * What:       the fractional octave of the catalogue gain; the integer octave
 *             is added to the float exponent by sampleMemory_gainFromCode().
 * Why:        block-rate gain without libm; 96 B of flash.
 * Accessors:  sampleMemory_gainFromCode().
 */
const float sampleMemory_gainMantissa[SAMPLE_GAIN_STEPS_PER_OCTAVE] = {
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
    return info;
}

static const sample_catalogue_sample_t *sampleMemory_sampleEntry(uint8_t i)
{
    return (const sample_catalogue_sample_t *)SAMPLE_FLASH_PTR(
        SAMPLE_CATALOGUE_SAMPLES_ADDRESS +
        (uint32_t)i * sizeof(sample_catalogue_sample_t));
}

static const sample_catalogue_wavetable_t *sampleMemory_tableEntry(uint8_t i)
{
    return (const sample_catalogue_wavetable_t *)SAMPLE_FLASH_PTR(
        SAMPLE_CATALOGUE_WAVETABLES_ADDRESS +
        (uint32_t)i * sizeof(sample_catalogue_wavetable_t));
}

/*
 * Catalogue entry bounds (S085 C1.2).
 *
 * What:       a sample entry is valid when its 4-aligned offset lies in the
 *             item region and its frames plus the 2-frame interpolation pad
 *             end before the catalogue. A wavetable entry also needs 1..128
 *             frames of 8 or 16 bits, frame_len >= 2, and the whole table
 *             inside the item region.
 * Why:        the reader stops at the first invalid entry, so a torn install
 *             can never hand the DSP a pointer outside the item region.
 * Inputs:     one flash record.  Outputs: 0/1.
 * Accessors:  sampleMemory_refresh().
 * Affiliates: SampleInstall.c (writes entries only inside these bounds).
 */
static uint8_t sampleMemory_entryValid(const sample_catalogue_sample_t *e)
{
    uint64_t end;

    if (e->offset < SAMPLE_DATA_START_ADDRESS ||
        e->offset >= SAMPLE_CATALOGUE_START_ADDRESS ||
        (e->offset & 3u) != 0u || e->frames == 0u)
        return 0u;
    end = (uint64_t)e->offset + ((uint64_t)e->frames + 2u) * 2u;
    return (uint8_t)(end <= SAMPLE_CATALOGUE_START_ADDRESS);
}

static uint8_t sampleMemory_tableValid(const sample_catalogue_wavetable_t *e)
{
    uint64_t end;

    if (e->offset < SAMPLE_DATA_START_ADDRESS ||
        e->offset >= SAMPLE_CATALOGUE_START_ADDRESS ||
        (e->offset & 3u) != 0u || e->frame_len < 2u ||
        e->frame_count == 0u || e->frame_count > WAVETABLE_MAX_FRAMES ||
        (e->bits != 8u && e->bits != 16u))
        return 0u;
    end = (uint64_t)e->offset + (uint64_t)e->frame_len *
          (uint64_t)e->frame_count * (uint64_t)(e->bits / 8u);
    return (uint8_t)(end <= SAMPLE_CATALOGUE_START_ADDRESS);
}

/*
 * Re-read the catalogue (S085 C1.2).
 *
 * What:       validates the header (magic, version, entry size) and counts
 *             the leading valid sample and wavetable entries, records the
 *             stock flag, and bumps the generation (0 is skipped) so every
 *             DSP bind refreshes. Without a v2 header the state is STALE when
 *             the first word of sector 7 holds a v1 count (1..120), else
 *             EMPTY. An interrupted v2 install can read STALE by chance (its
 *             first word is stock data); that only changes the boot wording.
 * Why:        no RAM index; v1 installs are not read (no migration, Q8).
 * Inputs:     sample flash.  Outputs: counts, state, stock flag, generation.
 * Accessors:  sampleMemory_init() (boot), sampleInstall_commit().
 * Affiliates: oscSample_bind(), oscWavetable_bind(), osc_setFreq() (pwm key),
 *             main.c boot notice (C3.9).
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
        sample_stock_valid = (uint8_t)(header->stock_valid == 1u);
        limit = (header->sample_count > SAMPLE_MAX_COUNT)
            ? (uint8_t)SAMPLE_MAX_COUNT : header->sample_count;
        for (i = 0u; i < limit; i++) {
            if (!sampleMemory_entryValid(sampleMemory_sampleEntry(i)))
                break;
            sample_count++;
        }
        limit = (header->wavetable_count > WAVETABLE_MAX_COUNT)
            ? (uint8_t)WAVETABLE_MAX_COUNT : header->wavetable_count;
        for (i = 0u; i < limit; i++) {
            if (!sampleMemory_tableValid(sampleMemory_tableEntry(i)))
                break;
            sample_wavetable_count++;
        }
        return;
    }
    {
        const uint32_t first =
            *(const uint32_t *)SAMPLE_FLASH_PTR(SAMPLE_ROM_START_ADDRESS);

        if (first >= 1u && first <= 120u)
            sample_catalogue_state = SAMPLE_CATALOGUE_STALE;
    }
}

/*
 * Boot init (S085 C1.2).
 *
 * What:       sampleFlash_init() (the floor probe) then one refresh.
 * Why:        main.c reads the state right after this for the boot notice.
 * Accessors:  main.c, before dsp_init().
 */
void sampleMemory_init(void)
{
    sampleFlash_init();
    sampleMemory_refresh();
}

uint8_t  sampleMemory_catalogueState(void) { return sample_catalogue_state; }
uint8_t  sampleMemory_stockValid(void)     { return sample_stock_valid; }
uint8_t  sampleMemory_getNumSamples(void)  { return sample_count; }
uint8_t  sampleMemory_getNumWavetables(void) { return sample_wavetable_count; }
uint32_t sampleMemory_getGeneration(void)  { return sample_generation; }

/*
 * One installed sample (S085 C1.2).
 *
 * What:       frames, offset and gain code of entry `index`; the silence
 *             buffer (size 0) when out of range.
 * Accessors:  oscSample_bind().
 */
SampleInfo sampleMemory_getSampleInfo(uint8_t index)
{
    const sample_catalogue_sample_t *e;
    SampleInfo info;

    if (index >= sample_count)
        return sampleMemory_silenceInfo();
    e = sampleMemory_sampleEntry(index);
    info.size = e->frames;
    info.offset = e->offset;
    info.gain_code = e->gain_code;
    return info;
}

/*
 * Eight-character names (S085 C1.2).
 *
 * What:       copies the entry's name and terminates it; spaces when out of
 *             range.
 * Accessors:  menu.c single-parameter view (CELL_FMT_SAMPLE /
 *             CELL_FMT_WAVETABLE).
 */
void sampleMemory_getDisplayName(uint8_t index,
                                 char name[SAMPLE_DISPLAY_NAME_LEN + 1u])
{
    if (!name)
        return;
    if (index >= sample_count)
        memset(name, ' ', SAMPLE_DISPLAY_NAME_LEN);
    else
        memcpy(name, sampleMemory_sampleEntry(index)->name,
               SAMPLE_DISPLAY_NAME_LEN);
    name[SAMPLE_DISPLAY_NAME_LEN] = '\0';
}

void sampleMemory_getWavetableName(uint8_t index,
                                   char name[SAMPLE_DISPLAY_NAME_LEN + 1u])
{
    if (!name)
        return;
    if (index >= sample_wavetable_count)
        memset(name, ' ', SAMPLE_DISPLAY_NAME_LEN);
    else
        memcpy(name, sampleMemory_tableEntry(index)->name,
               SAMPLE_DISPLAY_NAME_LEN);
    name[SAMPLE_DISPLAY_NAME_LEN] = '\0';
}

/*
 * One installed wavetable (S085 C1.2).
 *
 * What:       fills *out for entry `index`; returns 0 when out of range.
 * Accessors:  oscWavetable_bind(), InstrumentCellFormat.c (frame count).
 */
uint8_t sampleMemory_getWavetableInfo(uint8_t index, WavetableInfo *out)
{
    const sample_catalogue_wavetable_t *e;

    if (!out || index >= sample_wavetable_count)
        return 0u;
    e = sampleMemory_tableEntry(index);
    out->offset = e->offset;
    out->frame_len = e->frame_len;
    out->frame_count = e->frame_count;
    out->bits = e->bits;
    return 1u;
}
