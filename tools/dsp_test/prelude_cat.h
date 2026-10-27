/*
 * Host prelude for the catalogue-v2 test (S085 C17.3).
 *
 * What:       maps the firmware's absolute sample-flash addresses onto a RAM
 *             image so the real SampleMemory.c/SampleInstall.c run on the host.
 * Why:        the catalogue is read in place; a host pointer cannot be stored
 *             in the 32-bit offset.
 * Inputs:     host_cat_image (the test's 1,310,720 B image).
 * Outputs:    SAMPLE_FLASH_PTR.
 * Accessors: test_catalogue.c, SampleMemory.c, SampleInstall.c.
 * Affiliates: SampleMemory.h, sampleFlash.h.
 */
#ifndef DSP_CAT_PRELUDE_H
#define DSP_CAT_PRELUDE_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t *host_cat_image;
#ifndef SAMPLE_FLASH_PTR
#define SAMPLE_FLASH_PTR(addr) \
    ((const void *)(uintptr_t)(host_cat_image + \
        ((uint32_t)(addr) - 0x080C0000u)))
#endif
#endif
