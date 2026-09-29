/*
 * One-pass mixer comparison (S073 Step 5).
 *
 * What:       compares the frozen dry-plus-send calls against the current
 *             combined function across routing, stereo, pan, ramp and bus
 *             saturation cases.
 * Why:        the combined loop is S0: it must preserve every output and FX
 *             bus sample while reading each voice sample once.
 * Inputs:     generated mx_sat.c, mx_lut.c, mx_old.c and mx_new.c.
 * Outputs:    differing sample count; nonzero on any mismatch.
 * Accessors:  make -C tools/dsp_test mixer.
 * Affiliates: mixer.c mixer_addVoiceInt16ToOutputAndFx(), sample_mix.h and
 *             bufferTool_satAdd32().
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mx_sat.c"
#include "mx_lut.c"

static mixer_fx_bus_t old_bus;
static mixer_fx_bus_t new_bus;

#include "mx_old.c"
#include "mx_new.c"

static uint32_t next_state(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static int16_t next_sample(uint32_t *state)
{
    return (int16_t)(next_state(state) >> 16);
}

static void seed_block(int16_t *data, sample_mx_t *old_output,
                       sample_mx_t *new_output, uint32_t *state,
                       unsigned round)
{
    for (unsigned i = 0u; i < OUTPUT_DMA_SIZE; ++i) {
        data[i] = (round & 1u) ? next_sample(state) :
                   (i & 1u ? INT16_MIN : INT16_MAX);
    }
    for (unsigned i = 0u; i < OUTPUT_DMA_SIZE * 2u; ++i) {
        const sample_mx_t near_limit = (i & 1u) ? INT32_MIN : INT32_MAX;
        const sample_mx_t offset = (sample_mx_t)(next_state(state) & 2047u);
        old_output[i] = near_limit + ((near_limit > 0) ? -offset : offset);
        new_output[i] = old_output[i];
    }
}

static unsigned compare_case(uint8_t routing, uint8_t stereo, uint8_t pan,
                             float gain, float last_gain, float send_gain,
                             float send_last_gain, uint32_t *state,
                             unsigned round)
{
    int16_t data[OUTPUT_DMA_SIZE];
    sample_mx_t old_output[OUTPUT_DMA_SIZE * 2u];
    sample_mx_t new_output[OUTPUT_DMA_SIZE * 2u];
    const float pan_l = squareRootLut[127u - pan];
    const float pan_r = squareRootLut[pan];
    unsigned differences = 0u;

    seed_block(data, old_output, new_output, state, round);
    for (unsigned channel = 0u; channel < 2u; ++channel)
        for (unsigned i = 0u; i < OUTPUT_DMA_SIZE; ++i) {
            old_bus.mx[channel][i] = (channel & 1u) ? INT32_MIN : INT32_MAX;
            new_bus.mx[channel][i] = old_bus.mx[channel][i];
        }

    old_dry(routing, pan_l, pan_r, data, gain, last_gain,
            &old_output[0], &old_output[1],
            &old_output[0], &old_output[1]);
    old_send(data, send_gain, send_last_gain, pan_l, pan_r, stereo);

    new_combined(routing, pan_l, pan_r, data, gain, last_gain,
                 send_gain, send_last_gain, stereo,
                 &new_output[0], &new_output[1],
                 &new_output[0], &new_output[1]);

    for (unsigned i = 0u; i < OUTPUT_DMA_SIZE * 2u; ++i)
        differences += (unsigned)(old_output[i] != new_output[i]);
    for (unsigned channel = 0u; channel < 2u; ++channel)
        for (unsigned i = 0u; i < OUTPUT_DMA_SIZE; ++i)
            differences += (unsigned)(old_bus.mx[channel][i] !=
                                      new_bus.mx[channel][i]);
    if (differences != 0u) {
        printf("mixer mismatch route=%u stereo=%u pan=%u gain=%.6g "
               "last=%.6g send=%.6g sendLast=%.6g round=%u diffs=%u\n",
               (unsigned)routing, (unsigned)stereo, (unsigned)pan,
               gain, last_gain, send_gain, send_last_gain, round,
               differences);
    }
    return differences;
}

int main(void)
{
    const float dry_gains[] = { 0.0f, 0.3f, 0.999f, 1.0f };
    const float send_gains[] = { 0.0f, 0.2f, 1.0f };
    uint32_t state = 0x0732026u;
    unsigned differences = 0u;

    for (unsigned round = 0u; round < 128u; ++round)
        for (uint8_t routing = 0u; routing <= 6u; ++routing)
            for (uint8_t stereo = 0u; stereo <= 1u; ++stereo)
                for (uint8_t pan = 0u; pan < 128u; ++pan)
                    for (unsigned gain = 0u; gain < 4u; ++gain)
                        for (unsigned last = 0u; last < 4u; ++last)
                            for (unsigned send = 0u; send < 3u; ++send)
                                for (unsigned send_last = 0u;
                                     send_last < 3u; ++send_last)
                                    differences += compare_case(
                                        routing, stereo, pan,
                                        dry_gains[gain], dry_gains[last],
                                        send_gains[send],
                                        send_gains[send_last], &state, round);

    printf("mixer differing samples: %u\n", differences);
    return differences ? 1 : 0;
}
