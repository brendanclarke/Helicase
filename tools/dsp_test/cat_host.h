/*
 * Host catalogue harness prelude (S087 stage C).
 *
 * What:       force-included into the real SampleMemory.c/SampleInstall.c so
 *             flash addresses map onto a RAM image of sectors 7-11, and the
 *             firmware headers are skipped.
 * Why:        S087 P0 C-50: compile the real catalogue/installer code, not a
 *             transcription.
 * Inputs:     cat_host_image (catalogue_host.c).
 * Outputs:    SAMPLE_FLASH_PTR, SAMPLEMEMORY_HOST_TEST.
 * Accessors:  Makefile catalogue / wav_install targets.
 * Affiliates: catalogue_host.c, test_catalogue.c, test_wav_install.c.
 */
#ifndef CAT_HOST_H
#define CAT_HOST_H
#include <stdint.h>
#define SAMPLEMEMORY_HOST_TEST 1
extern uint8_t *cat_host_image;
#define SAMPLE_FLASH_PTR(address) \
    ((const uint8_t *)(cat_host_image + ((uint32_t)(address) - 0x080C0000u)))
#endif
