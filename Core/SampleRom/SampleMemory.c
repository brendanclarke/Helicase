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

static const int16_t stub_silence_buffer[256] = { 0 };

static SampleInfo sample_info_cache[SAMPLE_MAX_COUNT];
static char sample_name_cache[SAMPLE_MAX_COUNT][SAMPLE_DISPLAY_NAME_LEN + 1u];
static uint8_t sample_loop_cache[SAMPLE_MAX_COUNT];
static uint8_t sample_count = 0;
/* Incremented on every manifest refresh. DSP oscillators cache sample metadata
** by generation so the audio hot path avoids repeated flash table reads but
** automatically invalidates stale pointers after sample/loop installation. */
static uint32_t sample_generation = 1u;

static SampleInfo install_info[SAMPLE_MAX_COUNT];
static char install_names[SAMPLE_MAX_COUNT][SAMPLE_DISPLAY_NAME_LEN];
static uint8_t install_count = 0;
static uint32_t install_write_addr = SAMPLE_ROM_START_ADDRESS + 4u;
static uint32_t install_current_start = 0;
static uint32_t install_current_size = 0;
static uint8_t install_current_loop = 0;
static uint8_t install_start_count = 0;
static uint8_t install_append_mode = 0;
static uint8_t install_sample_open = 0;

static SampleInfo sampleMemory_silenceInfo(void)
{
    SampleInfo info;
    memset(&info, 0, sizeof(info));
    info.offset = (uint32_t)stub_silence_buffer;
    info.size = 0;
    return info;
}

static uint8_t sampleMemory_infoValid(const SampleInfo *info)
{
    uint64_t end;

    if (info->offset < SAMPLE_ROM_START_ADDRESS + 4u)
        return 0;
    if (info->offset >= SAMPLE_INFO_START_ADDRESS)
        return 0;
    if ((info->offset & 1u) != 0)
        return 0;
    if ((info->size & SAMPLE_INFO_SIZE_MASK) == 0u)
        return 0;

    end = (uint64_t)info->offset + ((uint64_t)(info->size & SAMPLE_INFO_SIZE_MASK) * 2u);
    if (end > SAMPLE_INFO_START_ADDRESS)
        return 0;

    return 1;
}

void sampleMemory_refresh(void)
{
    sample_generation++;
    if (sample_generation == 0u)
        sample_generation = 1u;

    sample_count = 0;
    memset(sample_info_cache, 0, sizeof(sample_info_cache));
    memset(sample_name_cache, 0, sizeof(sample_name_cache));
    memset(sample_loop_cache, 0, sizeof(sample_loop_cache));

    for (uint8_t i = 0; i < SAMPLE_MAX_COUNT; i++) {
        const SampleInfo *flash_info =
            (const SampleInfo *)(SAMPLE_INFO_START_ADDRESS + ((uint32_t)i * sizeof(SampleInfo)));
        const char *flash_name =
            (const char *)(SAMPLE_NAME_START_ADDRESS + ((uint32_t)i * SAMPLE_DISPLAY_NAME_LEN));
        SampleInfo info = *flash_info;

        if (!sampleMemory_infoValid(&info))
            break;

        sample_loop_cache[i] = (uint8_t)((info.size & SAMPLE_INFO_LOOP_FLAG) != 0u);
        info.size &= SAMPLE_INFO_SIZE_MASK;
        sample_info_cache[i] = info;
        memcpy(sample_name_cache[i], flash_name, SAMPLE_DISPLAY_NAME_LEN);
        sample_name_cache[i][SAMPLE_DISPLAY_NAME_LEN] = '\0';
        sample_count++;
    }
}

void sampleMemory_init(void)
{
    sampleFlash_init();
    sampleMemory_refresh();
}

SampleInfo sampleMemory_getSampleInfo(uint8_t index)
{
    if (index >= sample_count)
        return sampleMemory_silenceInfo();
    return sample_info_cache[index];
}

uint8_t sampleMemory_getNumSamples(void)
{
    return sample_count;
}

uint32_t sampleMemory_getGeneration(void)
{
    return sample_generation;
}

void sampleMemory_setNumSamples(uint8_t num)
{
    if (num > SAMPLE_MAX_COUNT)
        return;
    (void)sampleFlash_writeWord(SAMPLE_ROM_START_ADDRESS, (uint32_t)num);
    sampleMemory_refresh();
}

uint8_t sampleMemory_isLooped(uint8_t index)
{
    if (index >= sample_count)
        return 0;
    return sample_loop_cache[index];
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

    memcpy(name, sample_name_cache[index], SAMPLE_DISPLAY_NAME_LEN);
    name[SAMPLE_DISPLAY_NAME_LEN] = '\0';
}

uint32_t sampleMemory_installBytesFree(void)
{
    if (install_write_addr >= SAMPLE_INFO_START_ADDRESS)
        return 0;
    return SAMPLE_INFO_START_ADDRESS - install_write_addr;
}

int sampleMemory_installBegin(void)
{
    int rc;

    sample_count = 0;
    memset(sample_info_cache, 0, sizeof(sample_info_cache));
    memset(sample_name_cache, 0, sizeof(sample_name_cache));
    memset(sample_loop_cache, 0, sizeof(sample_loop_cache));
    memset(install_info, 0, sizeof(install_info));
    memset(install_names, ' ', sizeof(install_names));
    install_count = 0;
    install_write_addr = SAMPLE_ROM_START_ADDRESS + 4u;
    install_current_start = 0;
    install_current_size = 0;
    install_current_loop = 0;
    install_start_count = 0;
    install_append_mode = 0;
    install_sample_open = 0;

    sampleFlash_init();
    rc = sampleFlash_eraseAllSamples();
    if (rc != 0)
        return rc;

    return 0;
}

int sampleMemory_installAppendBegin(void)
{
    uint32_t end_addr = SAMPLE_ROM_START_ADDRESS + 4u;

    sampleMemory_refresh();
    memset(install_info, 0, sizeof(install_info));
    memset(install_names, ' ', sizeof(install_names));

    for (uint8_t i = 0; i < sample_count; i++) {
        uint32_t sample_end = sample_info_cache[i].offset +
            (((sample_info_cache[i].size * 2u) + 3u) & ~3u) + 4u;

        install_info[i] = sample_info_cache[i];
        if (sample_loop_cache[i])
            install_info[i].size |= SAMPLE_INFO_LOOP_FLAG;
        memcpy(install_names[i], sample_name_cache[i], SAMPLE_DISPLAY_NAME_LEN);
        if (sample_end > end_addr)
            end_addr = sample_end;
    }

    if (end_addr > SAMPLE_INFO_START_ADDRESS)
        return -1;

    install_count = sample_count;
    install_start_count = sample_count;
    install_write_addr = (end_addr + 3u) & ~3u;
    install_current_start = 0;
    install_current_size = 0;
    install_current_loop = 0;
    install_append_mode = 1;
    install_sample_open = 0;
    sampleFlash_init();
    return 0;
}

int sampleMemory_installStartSample(const char name[3],
                                    const char displayName[SAMPLE_DISPLAY_NAME_LEN],
                                    uint8_t looped,
                                    uint32_t dataBytes)
{
    uint32_t needed;

    if (install_sample_open)
        return -1;
    if (install_count >= SAMPLE_MAX_COUNT)
        return -2;

    needed = dataBytes + 4u;  /* one zero pad word after every sample */
    needed = (needed + 3u) & ~3u;
    if (needed > sampleMemory_installBytesFree())
        return -3;

    memcpy(install_info[install_count].name, name, 3);
    memcpy(install_names[install_count], displayName, SAMPLE_DISPLAY_NAME_LEN);
    install_info[install_count].offset = install_write_addr;
    install_info[install_count].size = dataBytes / 2u;
    install_current_start = install_write_addr;
    install_current_size = dataBytes / 2u;
    install_current_loop = looped ? 1u : 0u;
    install_sample_open = 1;
    return 0;
}

int sampleMemory_installWriteData(const uint8_t *data, uint32_t bytes)
{
    if (!install_sample_open)
        return -1;
    if ((bytes & 3u) != 0)
        return -2;
    if (bytes > sampleMemory_installBytesFree())
        return -3;
    return sampleFlash_writeBytes(&install_write_addr, data, bytes);
}

int sampleMemory_installFinishSample(void)
{
    static const uint8_t zero_pad[4] = { 0, 0, 0, 0 };
    int rc;

    if (!install_sample_open)
        return -1;

    rc = sampleFlash_writeBytes(&install_write_addr, zero_pad, sizeof(zero_pad));
    if (rc != 0)
        return rc;

    install_info[install_count].offset = install_current_start;
    install_info[install_count].size = install_current_size |
        (install_current_loop ? SAMPLE_INFO_LOOP_FLAG : 0u);
    install_count++;
    install_sample_open = 0;
    return 0;
}

int sampleMemory_installCommit(void)
{
    uint32_t addr = SAMPLE_INFO_START_ADDRESS;
    int rc;

    if (install_sample_open)
        return -1;

    addr += (uint32_t)install_start_count * sizeof(SampleInfo);
    rc = sampleFlash_writeBytes(&addr,
                                (const uint8_t *)&install_info[install_start_count],
                                (uint32_t)(install_count - install_start_count) * sizeof(SampleInfo));
    if (rc != 0)
        return rc;

    addr = SAMPLE_NAME_START_ADDRESS + ((uint32_t)install_start_count * SAMPLE_DISPLAY_NAME_LEN);
    rc = sampleFlash_writeBytes(&addr,
                                (const uint8_t *)&install_names[install_start_count],
                                (uint32_t)(install_count - install_start_count) * SAMPLE_DISPLAY_NAME_LEN);
    if (rc != 0)
        return rc;

    if (!install_append_mode) {
        rc = sampleFlash_writeWord(SAMPLE_ROM_START_ADDRESS, install_count);
        if (rc != 0)
            return rc;
    }

    sampleFlash_invalidateRange(SAMPLE_ROM_START_ADDRESS,
                                0x08200000UL - SAMPLE_ROM_START_ADDRESS);
    sampleMemory_refresh();
    return 0;
}

void sampleMemory_loadSamples(void)
{
    /* The SD-backed modal installer lives in filesystem.c so asyncfatfs
     * details stay behind the filesystem facade. */
}
