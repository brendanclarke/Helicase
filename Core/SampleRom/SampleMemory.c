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

/*
 * Catalogue v2 in-place reader (S087 stage C).
 *
 * What:       validates and counts the flash catalogue on every refresh; no
 *             RAM copy of names or entries. `sample_generation` is the bind
 *             invalidation stamp.
 * Why:        S087 P0 §5, A11: the v1 manifest (120 x 12 B) and its name/loop
 *             caches (5,040 B) are released; the oscillators read one entry per
 *             block through the catalogue.
 * Inputs:     SampleInstall.c writes the catalogue; sampleFlash.c erases it.
 * Outputs:    counts, state and the read API.
 * Accessors:  sampleMemory_refresh() (boot, install, C-31).
 * Affiliates: SAMPLE_FLASH_PTR(), sampleFlash_invalidateRange().
 */

#define SAMPLE_ENTRIES_BASE (SAMPLE_CATALOGUE_START_ADDRESS + \
                             sizeof(sample_catalogue_header_t))
#define WAVETABLE_ENTRIES_BASE (SAMPLE_ENTRIES_BASE + \
                                SAMPLE_MAX_COUNT * SAMPLE_ENTRY_BYTES)

static uint8_t sample_count = 0;
static uint8_t wavetable_count = 0;
static uint32_t sample_generation = 1u;
static sample_catalogue_state_t catalogue_state = SAMPLE_CATALOGUE_EMPTY;

/* 2^(k/24), k = 0..23 (S087 C: computed once, no libm at playback). */
const float sampleMemory_gainMantissa[SAMPLE_GAIN_STEPS_PER_OCTAVE] = {
    1.00000000f, 1.02930224f, 1.05946309f, 1.09050773f,
    1.12246205f, 1.15535270f, 1.18920712f, 1.22405354f,
    1.25992105f, 1.29683955f, 1.33483985f, 1.37395363f,
    1.41421356f, 1.45565322f, 1.49830708f, 1.54221082f,
    1.58740105f, 1.63391548f, 1.68179283f, 1.73107318f,
    1.78179744f, 1.83400809f, 1.88774863f, 1.94306411f,
};

static const sample_catalogue_header_t *sampleMemory_header(void)
{
    return (const sample_catalogue_header_t *)
        SAMPLE_FLASH_PTR(SAMPLE_CATALOGUE_START_ADDRESS);
}

const sample_catalogue_entry_t *sampleMemory_sampleEntry(uint8_t index)
{
    if (index >= sample_count)
        return (const sample_catalogue_entry_t *)0;
    return (const sample_catalogue_entry_t *)
        SAMPLE_FLASH_PTR(SAMPLE_ENTRIES_BASE + (uint32_t)index * SAMPLE_ENTRY_BYTES);
}

const wavetable_catalogue_entry_t *sampleMemory_wavetableEntry(uint8_t index)
{
    if (index >= wavetable_count)
        return (const wavetable_catalogue_entry_t *)0;
    return (const wavetable_catalogue_entry_t *)
        SAMPLE_FLASH_PTR(WAVETABLE_ENTRIES_BASE +
                         (uint32_t)index * WAVETABLE_ENTRY_BYTES);
}

static uint8_t sampleMemory_sampleRangeValid(
    const sample_catalogue_entry_t *entry)
{
    uint64_t bytes;

    if (entry->frames == 0u || entry->frames > SAMPLE_MAX_FRAMES)
        return 0u;
    if ((entry->offset & 3u) != 0u)
        return 0u;
    if (entry->offset < SAMPLE_ROM_START_ADDRESS + 4u)
        return 0u;
    if (entry->loop_end > entry->frames ||
        entry->loop_start > entry->loop_end)
        return 0u;
    if ((entry->flags & 1u) && entry->loop_start >= entry->loop_end)
        return 0u;
    bytes = (uint64_t)entry->frames * 2u + 4u;   /* two zero frames */
    bytes = (bytes + 3u) & ~(uint64_t)3u;
    return (uint8_t)(entry->offset + bytes <= SAMPLE_CATALOGUE_START_ADDRESS);
}

static uint8_t sampleMemory_wavetableRangeValid(
    const wavetable_catalogue_entry_t *entry)
{
    uint64_t bytes;

    if (entry->frame_len < WAVETABLE_FRAME_MIN ||
        entry->frame_len > WAVETABLE_FRAME_MAX)
        return 0u;
    if (entry->frame_count == 0u || entry->frame_count > WAVETABLE_MAX_FRAMES)
        return 0u;
    if (entry->bits != 8u && entry->bits != 16u)
        return 0u;
    if ((entry->offset & 3u) != 0u)
        return 0u;
    if (entry->offset < SAMPLE_ROM_START_ADDRESS + 4u)
        return 0u;
    bytes = (uint64_t)entry->frame_len * entry->frame_count *
            (entry->bits / 8u);
    bytes = (bytes + 3u) & ~(uint64_t)3u;
    return (uint8_t)(entry->offset + bytes <= SAMPLE_CATALOGUE_START_ADDRESS);
}

void sampleMemory_refresh(void)
{
    const sample_catalogue_header_t *header = sampleMemory_header();
    uint32_t first_word;

    sample_generation++;
    if (sample_generation == 0u)
        sample_generation = 1u;
    sample_count = 0u;
    wavetable_count = 0u;

    memcpy(&first_word, SAMPLE_FLASH_PTR(SAMPLE_ROM_START_ADDRESS),
           sizeof(first_word));
    if (header->magic != SAMPLE_CATALOGUE_MAGIC ||
        header->version != SAMPLE_CATALOGUE_VERSION ||
        header->sample_entry_bytes != SAMPLE_ENTRY_BYTES ||
        header->wavetable_entry_bytes != WAVETABLE_ENTRY_BYTES ||
        header->sample_count > SAMPLE_MAX_COUNT ||
        header->wavetable_count > WAVETABLE_MAX_COUNT) {
        if (header->version == SAMPLE_CATALOGUE_VERSION &&
            header->sample_entry_bytes == SAMPLE_ENTRY_BYTES &&
            header->wavetable_entry_bytes == WAVETABLE_ENTRY_BYTES) {
            /* A v2 install was started but never committed: magic absent. */
            catalogue_state = SAMPLE_CATALOGUE_EMPTY;
        } else {
            catalogue_state = (first_word == 0xFFFFFFFFu)
                ? SAMPLE_CATALOGUE_EMPTY : SAMPLE_CATALOGUE_STALE;
        }
        return;
    }
    catalogue_state = SAMPLE_CATALOGUE_V2;
    while (sample_count < header->sample_count) {
        const sample_catalogue_entry_t *entry =
            (const sample_catalogue_entry_t *)
            SAMPLE_FLASH_PTR(SAMPLE_ENTRIES_BASE +
                             (uint32_t)sample_count * SAMPLE_ENTRY_BYTES);

        if (!sampleMemory_sampleRangeValid(entry))
            break;
        sample_count++;
    }
    while (wavetable_count < header->wavetable_count) {
        const wavetable_catalogue_entry_t *entry =
            (const wavetable_catalogue_entry_t *)
            SAMPLE_FLASH_PTR(WAVETABLE_ENTRIES_BASE +
                             (uint32_t)wavetable_count * WAVETABLE_ENTRY_BYTES);

        if (!sampleMemory_wavetableRangeValid(entry))
            break;
        wavetable_count++;
    }
}

void sampleMemory_init(void)
{
    sampleFlash_init();
    sampleMemory_refresh();
}

uint8_t sampleMemory_getNumSamples(void)
{
    return sample_count;
}

uint8_t sampleMemory_getNumWavetables(void)
{
    return wavetable_count;
}

uint32_t sampleMemory_getGeneration(void)
{
    return sample_generation;
}

sample_catalogue_state_t sampleMemory_catalogueState(void)
{
    return catalogue_state;
}

static void sampleMemory_copyName(const char *src,
                                  char name[SAMPLE_DISPLAY_NAME_LEN + 1u])
{
    if (name == 0)
        return;
    if (src == 0) {
        memset(name, ' ', SAMPLE_DISPLAY_NAME_LEN);
        name[SAMPLE_DISPLAY_NAME_LEN] = '\0';
        return;
    }
    memcpy(name, src, SAMPLE_DISPLAY_NAME_LEN);
    name[SAMPLE_DISPLAY_NAME_LEN] = '\0';
}

void sampleMemory_getDisplayName(uint8_t index,
                                 char name[SAMPLE_DISPLAY_NAME_LEN + 1u])
{
    const sample_catalogue_entry_t *entry = sampleMemory_sampleEntry(index);

    sampleMemory_copyName(entry ? entry->name : 0, name);
}

void sampleMemory_getWavetableName(uint8_t index,
                                   char name[SAMPLE_DISPLAY_NAME_LEN + 1u])
{
    const wavetable_catalogue_entry_t *entry =
        sampleMemory_wavetableEntry(index);

    sampleMemory_copyName(entry ? entry->name : 0, name);
}

void sampleMemory_loadSamples(void)
{
    /* The SD-backed modal installer lives in filesystem.c so asyncfatfs
     * details stay behind the filesystem facade. */
}
