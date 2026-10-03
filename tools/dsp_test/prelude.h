/*
 * Host prelude for extracted DSP fragments (S073 Step 0).
 *
 * What:       supplies target-independent types, placement macros and DSP
 *             mirrors needed by the extracted firmware functions.
 * Why:        the complete firmware include tree cannot be host-compiled.
 * Inputs:     none.
 * Outputs:    declarations and portable intrinsic substitutes.
 * Accessors:  every host test through -include prelude.h.
 * Affiliates: ResonantFilter.h, distortion.h, sample_mix.h and mixer.h.
 */
#ifndef DSP_GOLDEN_PRELUDE_H
#define DSP_GOLDEN_PRELUDE_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

typedef int32_t sample_mx_t;
#define SAMPLE_MIX_SHIFT_16_TO_24 8
static inline sample_mx_t sampleMix_fromInt16(int16_t x)
{
    return ((sample_mx_t)x) << SAMPLE_MIX_SHIFT_16_TO_24;
}

typedef struct DistStruct { float shape; float inv_shape; } Distortion;
#define ENABLE_NONLINEAR_INTEGRATORS 1
#define FILTER_GAIN 0x70ff
#define USE_SHAPER_NONLINEARITY 0
enum filterTypeEnum { FILTER_LP = 1, FILTER_HP, FILTER_BP, FILTER_UNITY_BP,
                      FILTER_NOTCH, FILTER_PEAK, FILTER_NAIVE_2_POLE };
typedef struct ResoFilterStruct {
    float f, g, q, s1, s2, a, b, zi, drive;
} ResonantFilter;

enum { MIXER_ROUTING_DAC1_STEREO = 0, MIXER_ROUTING_DAC2_STEREO,
       MIXER_ROUTING_DAC1_L, MIXER_ROUTING_DAC1_R,
       MIXER_ROUTING_DAC2_L, MIXER_ROUTING_DAC2_R };
typedef union {
    sample_mx_t mx[2][OUTPUT_DMA_SIZE];
    float f[2][OUTPUT_DMA_SIZE];
} mixer_fx_bus_t;

static inline uint32_t golden_rand(uint32_t *s)
{
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x;
}
#endif
