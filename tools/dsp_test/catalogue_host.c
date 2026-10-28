/*
 * Host sample-flash image and flash stubs (S087 stage C).
 *
 * What:       a 1.31 MB RAM image of sectors 7-11 plus the sampleFlash API
 *             implemented as plain memory writes (no bit-clear emulation is
 *             needed: every test writes erased areas or rewrites the same
 *             values).
 * Why:        the real catalogue and installer code must run on the host.
 * Inputs:     none.  Outputs: cat_host_image and the flash stubs.
 * Accessors:  test_catalogue.c, test_wav_install.c.
 * Affiliates: cat_host.h, SampleMemory.c, SampleInstall.c.
 */
#include <stdint.h>
#include <string.h>
#include "cat_host.h"

#define CAT_HOST_BASE 0x080C0000u
#define CAT_HOST_END  0x08200000u

static uint8_t cat_host_mem[CAT_HOST_END - CAT_HOST_BASE];
uint8_t *cat_host_image = cat_host_mem;

void catalogue_host_reset(void)
{
    memset(cat_host_mem, 0xFF, sizeof(cat_host_mem));
}

void sampleFlash_init(void) { }

int sampleFlash_eraseAllSamples(void)
{
    catalogue_host_reset();
    return 0;
}

int sampleFlash_writeWord(uint32_t address, uint32_t data)
{
    if (address < CAT_HOST_BASE || address + 4u > CAT_HOST_END)
        return -1;
    memcpy(&cat_host_mem[address - CAT_HOST_BASE], &data, 4u);
    return 0;
}

int sampleFlash_writeBytes(uint32_t *address, const uint8_t *data, uint32_t bytes)
{
    if (!address || !data || (*address & 3u) != 0u)
        return -1;
    for (uint32_t i = 0; i < bytes; i += 4u) {
        uint32_t word = 0xFFFFFFFFu;
        uint32_t left = bytes - i;
        if (left > 0u) word = (word & 0xFFFFFF00u) | data[i];
        if (left > 1u) word = (word & 0xFFFF00FFu) | ((uint32_t)data[i + 1u] << 8);
        if (left > 2u) word = (word & 0xFF00FFFFu) | ((uint32_t)data[i + 2u] << 16);
        if (left > 3u) word = (word & 0x00FFFFFFu) | ((uint32_t)data[i + 3u] << 24);
        if (sampleFlash_writeWord(*address, word) != 0)
            return -1;
        *address += 4u;
    }
    return 0;
}

void sampleFlash_invalidateRange(uint32_t address, uint32_t bytes)
{
    (void)address; (void)bytes;
}
