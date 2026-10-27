/*
 * Host sample-flash double for the catalogue test (S085 C17.3).
 *
 * What:       AND-wise programming into the RAM image (erased = 0xFF), a full
 *             erase, and the same sector-7 bounds refusal as sampleFlash.c.
 * Why:        SampleInstall.c programs through this API; the test needs a
 *             byte-accurate image, not real flash.
 * Inputs:     the test's host_cat_image.  Outputs: programmed bytes.
 * Accessors: SampleMemory.c, SampleInstall.c.
 * Affiliates: sampleFlash.h.
 */
#include "prelude_cat.h"
#include "sampleFlash.h"
#include "SampleMemory.h"

#define HOST_CAT_END (0x080C0000u + 1310720u)

void sampleFlash_init(void) {}
void sampleFlash_lock(void) {}

int sampleFlash_eraseAllSamples(void)
{
    memset(host_cat_image, 0xff, 1310720u);
    return 0;
}

int sampleFlash_writeWord(uint32_t address, uint32_t data)
{
    if (address < SAMPLE_ROM_START_ADDRESS || address + 4u > HOST_CAT_END)
        return -1;
    for (unsigned i = 0u; i < 4u; i++)
        host_cat_image[address - SAMPLE_ROM_START_ADDRESS + i] &=
            (uint8_t)((data >> (8u * i)) & 0xffu);
    return 0;
}

int sampleFlash_writeBytes(uint32_t *address, const uint8_t *data,
                           uint32_t bytes)
{
    if (!address || !data)
        return -1;
    while (bytes-- != 0u) {
        if (*address < SAMPLE_ROM_START_ADDRESS || *address >= HOST_CAT_END)
            return -1;
        host_cat_image[*address - SAMPLE_ROM_START_ADDRESS] &= *data++;
        (*address)++;
    }
    return 0;
}

void sampleFlash_invalidateRange(uint32_t address, uint32_t bytes)
{
    (void)address;
    (void)bytes;
}

int stockWaveGen_programAll(uint32_t base)
{
    (void)base;
    return 0;
}
