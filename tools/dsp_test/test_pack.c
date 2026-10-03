/*
 * DMA pack old-vs-new comparison (S073 Step 3a).
 *
 * What:       packs deterministic stereo sample blocks with both functions and
 *             compares all bytes, including values beyond signed 24-bit range.
 * Why:        the word-store optimization is S0 only if the DMA memory image
 *             is byte-identical.
 * Inputs:     generated old_pack_half/new_pack_half fragments.
 * Outputs:    differing byte count; nonzero on failure.
 * Accessors:  tools/dsp_test/Makefile pack target.
 * Affiliates: AudioCodecManager.c sampleMix_toS24(), pack_half().
 */
#include <stdint.h>
#include <string.h>

typedef uint32_t __attribute__((may_alias)) dma_word_t;
#define SAMPLE_S24_MAX 8388607
#define SAMPLE_S24_MIN (-8388608)
#include "pack_old.c"
#include "pack_new.c"

int main(void)
{
    sample_mx_t src[AUDIO_DMA_FRAMES * 2];
    _Alignas(4) int16_t old_buf[AUDIO_DMA_FRAMES * 4];
    _Alignas(4) int16_t new_buf[AUDIO_DMA_FRAMES * 4];
    uint32_t rng = 0x51a073u;
    unsigned differences = 0u;

    for (unsigned round = 0u; round < 10000u; ++round) {
        for (unsigned i = 0u; i < AUDIO_DMA_FRAMES * 2u; ++i) {
            rng = rng * 1664525u + 1013904223u;
            src[i] = (sample_mx_t)((int32_t)rng ^ (int32_t)(round * 97u));
        }
        memset(old_buf, 0xa5, sizeof(old_buf));
        memset(new_buf, 0x5a, sizeof(new_buf));
        old_pack_half(old_buf, src);
        new_pack_half(new_buf, src);
        for (unsigned i = 0u; i < sizeof(old_buf); ++i)
            differences += ((unsigned char *)old_buf)[i] !=
                          ((unsigned char *)new_buf)[i];
    }
    printf("pack differing bytes: %u\n", differences);
    return differences ? 1 : 0;
}
