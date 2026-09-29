/*
 * Fused voice post-chain comparison (S073 Step 4).
 *
 * What:       compares the frozen multi-pass Drum/Snare/Cymbal/HiHat bodies
 *             against the new inline fused helpers over deterministic blocks,
 *             all distortion shapes, gain ramps and volumeMod choices.
 * Why:        Step 4 is S0: every stored int16 sample must be identical.
 * Inputs:     generated frozen/current fragments from the Makefile target.
 * Outputs:    differing sample count; nonzero on any mismatch.
 * Accessors:  tools/dsp_golden/Makefile postchain.
 * Affiliates: voicePostChain.h, BufferTools.h and distortion.h.
 */
#include <stdint.h>
#include <string.h>

#include "pc_sat.c"
#include "pc_old_bt.c"
#include "pc_old_dist.c"
#include "pc_new_bt.c"
#include "pc_new_dist.c"
#include "pc_new_chain.c"

typedef struct {
    float mix;
    float velo;
    float egValueOscVol;
    uint8_t volumeMod;
    Distortion distortion;
} VoiceStub;

static void old_snare(int16_t *buf, int16_t *transBuf, VoiceStub *voice,
                      uint8_t size)
{
#include "pc_old_snare_body.inc"
}

static void old_cymbal(int16_t *buf, int16_t *mod, VoiceStub *voice,
                       uint8_t size)
{
#include "pc_old_cym_body.inc"
}

static void old_hihat(int16_t *buf, int16_t *mod1, VoiceStub *voice,
                      uint8_t size)
{
#include "pc_old_hat_body.inc"
}

static int16_t next_sample(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (int16_t)((*state >> 16) ^ (*state & 0xffffu));
}

static void seed_block(int16_t *buf, int16_t *add, uint32_t *state,
                       unsigned round)
{
    for (unsigned i = 0u; i < OUTPUT_DMA_SIZE; ++i) {
        buf[i] = (round & 1u) ? next_sample(state) :
                 (i & 1u ? -32768 : 32767);
        add[i] = (round & 2u) ? next_sample(state) :
                 (i % 3u ? 0 : 32767);
    }
}

static unsigned compare_engine(unsigned engine, float shape, float mix,
                               float gain, float last_gain, float velo,
                               uint8_t volume_mod, uint32_t *state,
                               unsigned round)
{
    int16_t old_buf[OUTPUT_DMA_SIZE], new_buf[OUTPUT_DMA_SIZE];
    int16_t old_add[OUTPUT_DMA_SIZE], new_add[OUTPUT_DMA_SIZE];
    VoiceStub old_voice = { mix, velo, gain, volume_mod, { shape, 0.0f } };
    VoiceStub new_voice = old_voice;
    unsigned differences = 0u;

    seed_block(old_buf, old_add, state, round);
    /*
     * Host C leaves an out-of-range float-to-int16 conversion undefined.
     * Keep the 1.02 stress point, but use an in-range signal at that point so
     * the comparison exercises DSP arithmetic rather than host conversion
     * behaviour. The ARM check covers the real VCVT/narrowing sequence.
     */
    if (mix > 1.0f || gain > 1.0f || last_gain > 1.0f) {
        for (unsigned i = 0u; i < OUTPUT_DMA_SIZE; ++i) {
            old_buf[i] = (int16_t)(old_buf[i] / 3);
            old_add[i] = (int16_t)(old_add[i] / 3);
        }
    }
    memcpy(new_buf, old_buf, sizeof(new_buf));
    memcpy(new_add, old_add, sizeof(new_add));
    if (engine == 0u) {
        old_addGainInterpolated(old_buf, gain, last_gain, OUTPUT_DMA_SIZE);
        if (volume_mod)
            old_addGain(old_buf, velo, OUTPUT_DMA_SIZE);
        old_calcDistBlock(&old_voice.distortion, old_buf, OUTPUT_DMA_SIZE);
        voicePost_drum(new_buf, gain, last_gain,
                       volume_mod ? velo : 1.0f,
                       &new_voice.distortion, OUTPUT_DMA_SIZE);
    } else if (engine == 1u) {
        old_snare(old_buf, old_add, &old_voice, OUTPUT_DMA_SIZE);
        voicePost_mixAddGainDist(new_buf, new_add, mix,
                                 volume_mod ? velo * gain : gain,
                                 &new_voice.distortion, OUTPUT_DMA_SIZE);
    } else if (engine == 2u) {
        old_cymbal(old_buf, old_add, &old_voice, OUTPUT_DMA_SIZE);
        voicePost_addGainDist(new_buf, new_add,
                              volume_mod ? velo * gain : gain,
                              &new_voice.distortion, OUTPUT_DMA_SIZE);
    } else {
        old_hihat(old_buf, old_add, &old_voice, OUTPUT_DMA_SIZE);
        voicePost_addGainDist(new_buf, new_add,
                              volume_mod ? velo * gain : gain,
                              &new_voice.distortion, OUTPUT_DMA_SIZE);
    }
    for (unsigned i = 0u; i < OUTPUT_DMA_SIZE; ++i) {
        differences += (unsigned)(old_buf[i] != new_buf[i]);
    }
    return differences;
}

int main(void)
{
    const float gains[] = { 0.0f, 0.0001f, 0.3f, 0.7f, 1.0f, 1.02f };
    uint32_t state = 0x0732026u;
    unsigned differences = 0u;

    for (unsigned shape = 0u; shape < 128u; ++shape)
        for (unsigned g = 0u; g < sizeof(gains) / sizeof(gains[0]); ++g)
            for (unsigned lg = 0u; lg < sizeof(gains) / sizeof(gains[0]); ++lg)
                for (unsigned vm = 0u; vm < 2u; ++vm)
                    for (unsigned round = 0u; round < 32u; ++round)
                        for (unsigned engine = 0u; engine < 4u; ++engine)
                            differences += compare_engine(
                                engine, shape, gains[g], gains[g], gains[lg],
                                gains[g], (uint8_t)vm, &state, round);
    printf("post-chain differing samples: %u\n", differences);
    return differences ? 1 : 0;
}
