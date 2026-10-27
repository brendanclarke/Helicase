/*
 * Host prelude for the S085 oscillator tests (S085 C17.1).
 *
 * What:       target-independent types, placement macros and intrinsics for
 *             compiling the real oscillator/section sources natively.
 * Why:        the shared prelude defines sampleMix_fromInt16(), which conflicts
 *             with sample_mix.h when BufferTools.h is included; these tests
 *             need the DSP sources unchanged.
 * Inputs:     none.  Outputs: declarations and portable substitutes.
 * Accessors:  every S085 host target through -include prelude_osc.h.
 * Affiliates: prelude.h, config.h (INITCM), sample_mix.h.
 */
#ifndef DSP_OSC_PRELUDE_H
#define DSP_OSC_PRELUDE_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "config.h"

/*
 * Host catalogue addresses (S085 C17.1).
 *
 * What:       the host doubles keep sample and wavetable data in RAM but the
 *             firmware SampleInfo offset is a 32-bit flash address. This maps
 *             a small fake flash window onto those buffers.
 * Why:        a 64-bit host pointer cannot be stored in the 32-bit offset.
 * Accessors: host_osc_support.c and every S085 host test.
 */
#define HOST_SAMPLE_FLASH_BASE 0x10000000u
#define HOST_TABLE_FLASH_BASE  0x20000000u
#define HOST_SILENCE_FLASH     0x30000000u
const void *host_flash_ptr(uint32_t address);
#ifndef SAMPLE_FLASH_PTR
#define SAMPLE_FLASH_PTR(addr) host_flash_ptr((uint32_t)(addr))
#endif

#undef INITCM
#undef INITCM_EFFECT
#undef INITCM_EFFECT_NOINLINE
#undef INITCM_NOINLINE
#undef INCCM
#undef INCCMZ
#undef INDTCM
#undef INDTCMZ
#undef __SSAT

#define INITCM
#define INITCM_EFFECT
#define INITCM_EFFECT_NOINLINE
#define INITCM_NOINLINE
#define INCCM
#define INCCMZ
#define INDTCM
#define INDTCMZ

#define INITCM
#define INITCM_EFFECT
#define INITCM_EFFECT_NOINLINE
#define INITCM_NOINLINE
#define INCCM
#define INCCMZ
#define INDTCM
#define INDTCMZ
#define OUTPUT_DMA_SIZE 32
#define AUDIO_DMA_FRAMES 96
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static inline int32_t host_ssat(int32_t v, unsigned bits)
{
    const int32_t hi = (int32_t)((1u << (bits - 1u)) - 1u);
    const int32_t lo = -hi - 1;
    return v > hi ? hi : (v < lo ? lo : v);
}
#define __SSAT(v, b) host_ssat((int32_t)(v), (b))

static inline uint32_t golden_rand(uint32_t *s)
{
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x;
}
#endif
