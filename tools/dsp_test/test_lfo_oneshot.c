/*
 * One-shot LFO waves (S084 P1 WP1).
 *
 * What:       proves lfo_calc()/lfo_retriggerPhase()/lfo_isOneShot() run one
 *             cycle per fire, hold the end value, apply the start delay from
 *             phaseOffset and keep noise inside 0..1.
 * Why:        plan §4.5 and r4 Q17; the host proof for the wave behaviour that
 *             hardware H2 checks by ear.
 * Inputs:     generated lfo fragment from the real lfo.c.
 * Outputs:    failure count; exits nonzero on any failure.
 * Accessors:  make -C tools/dsp_test lfo_oneshot.
 * Affiliates: Core/DSPAudio/lfo.c, lfo.h.
 */
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#define TABLESIZE 4096

/* Minimal Lfo mirror: the extracted functions touch only these fields. */
typedef struct LfoStruct {
    uint32_t phase;
    uint32_t phaseInc;
    uint8_t waveform;
    uint8_t retrigger;
    uint32_t phaseOffset;
    float rnd;
    uint8_t sync;
    float freq;
    float modNodeValue;
    uint8_t oneShotState;
    uint32_t delayBlocks;
} Lfo;

#define LFO_SINE        0x00
#define LFO_TRI         0x01
#define LFO_SAW_UP      0x02
#define LFO_SAW_DOWN    0x03
#define LFO_REC         0x04
#define LFO_NOISE       0x05
#define LFO_EXP_UP      0x06
#define LFO_EXP_DOWN    0x07
#define LFO_ONE_SHOT_BASE 0x08
#define LFO_NOISE_1     0x0D
#define LFO_EXP_DOWN_1  0x0F
#define LFO_XTRI        0x10
#define LFO_XTRI_1      0x11
#define LFO_ONESHOT_IDLE  0u
#define LFO_ONESHOT_DELAY 1u
#define LFO_ONESHOT_RUN   2u
#define LFO_ONESHOT_HOLD  3u
#define LFO_RETRIGGER_SCENE 7u   /* lfo.h; read by lfo_calc() IDLE (F7) */

static uint32_t host_rng_state = 0x12345678u;
static uint32_t GetRngValue(void)
{
    host_rng_state ^= host_rng_state << 13;
    host_rng_state ^= host_rng_state >> 17;
    host_rng_state ^= host_rng_state << 5;
    return host_rng_state;
}

#include "lfo_new.c"

static unsigned failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    Lfo lfo;
    uint32_t i;
    float last = -1.0f;
    unsigned drops = 0u;
    float minValue = 2.0f;
    float maxValue = -1.0f;

    /* One-shot saw-up: exactly one cycle, then holds 1. */
    lfo.phase = 0u;
    lfo.phaseInc = 0x10000000u;   /* 16 blocks per cycle */
    lfo.waveform = LFO_SAW_UP + LFO_ONE_SHOT_BASE;
    lfo.retrigger = 0u;
    lfo.phaseOffset = 0u;
    lfo.rnd = 0.0f;
    lfo.sync = 0u;
    lfo.freq = 1.0f;
    lfo.modNodeValue = 1.0f;
    lfo.oneShotState = LFO_ONESHOT_IDLE;
    lfo.delayBlocks = 0u;
    check(lfo_isOneShot(&lfo) == 1u, "su1 is a one-shot");
    lfo_retriggerPhase(&lfo);
    check(lfo.oneShotState == LFO_ONESHOT_RUN, "no delay starts RUN");
    for (i = 0u; i < 40u; i++) {
        const float v = lfo_calc(&lfo);

        if (v + 0.001f < last)
            drops++;
        last = v;
    }
    check(drops == 0u, "a one-shot rises once and never wraps");
    check(lfo.oneShotState == LFO_ONESHOT_HOLD, "holds after one cycle");
    check(fabsf(lfo_calc(&lfo) - 1.0f) < 0.001f, "held value is the cycle end");

    /* Start delay: phaseOffset of half a cycle = 8 blocks. */
    lfo.phaseOffset = 0x80000000u;
    lfo_retriggerPhase(&lfo);
    check(lfo.oneShotState == LFO_ONESHOT_DELAY, "offset arms the delay");
    check(lfo.delayBlocks == 8u, "delay = offset / increment");
    {
        uint32_t delayed = 0u;

        while (lfo.oneShotState == LFO_ONESHOT_DELAY && delayed < 16u) {
            (void)lfo_calc(&lfo);
            delayed++;
        }
        check(delayed == 8u, "delay lasts 8 blocks");
    }

    /* xt1 is a one-shot; sq1 starts high and holds low. */
    lfo.waveform = LFO_XTRI_1;
    lfo.phaseOffset = 0u;
    lfo.phaseInc = 0x00001000u;
    lfo_retriggerPhase(&lfo);
    check(fabsf(lfo_calc(&lfo)) < 0.01f, "xt1 starts near zero");
    lfo.waveform = LFO_REC + LFO_ONE_SHOT_BASE;
    lfo_retriggerPhase(&lfo);
    check(fabsf(lfo_calc(&lfo) - 1.0f) < 0.001f, "sq1 starts high");

    /* Noise stays in 0..1 and rn1 draws once per fire. */
    lfo.waveform = LFO_NOISE;
    lfo.phaseInc = 0x10000000u;
    lfo.phase = 0u;
    for (i = 0u; i < 200u; i++) {
        const float v = lfo_calc(&lfo);

        if (v < minValue)
            minValue = v;
        if (v > maxValue)
            maxValue = v;
    }
    check(minValue >= 0.0f && maxValue <= 1.0f, "rnd spans 0..1");
    lfo.waveform = LFO_NOISE_1;
    lfo_retriggerPhase(&lfo);
    last = lfo.rnd;
    for (i = 0u; i < 40u; i++)
        (void)lfo_calc(&lfo);
    check(lfo.rnd == last, "rn1 draws once per fire");

    /*
     * S084 P1 review fix F7: a looping LFO switched to a one-shot wave is
     * IDLE. With retrigger off or scn it fires on its next block; with a
     * voice retrigger it waits at phase 0 (its start value) instead of
     * freezing at the old mid-cycle phase.
     */
    lfo.waveform = LFO_SAW_UP;
    lfo.phaseInc = 0x01000000u;
    lfo.phaseOffset = 0u;
    lfo.retrigger = 0u;
    lfo.phase = 0x60000000u;            /* mid-cycle on the looping wave */
    lfo.oneShotState = LFO_ONESHOT_IDLE;
    lfo.waveform = LFO_SAW_UP + LFO_ONE_SHOT_BASE;   /* menu: sup -> su1 */
    (void)lfo_calc(&lfo);
    check(lfo.oneShotState == LFO_ONESHOT_RUN, "retrigger off: fires when selected");
    check(lfo.phase == 0u, "the fire starts at phase 0");

    lfo.phase = 0x60000000u;
    lfo.oneShotState = LFO_ONESHOT_IDLE;
    lfo.retrigger = LFO_RETRIGGER_SCENE;
    (void)lfo_calc(&lfo);
    check(lfo.oneShotState == LFO_ONESHOT_RUN, "retrigger scn: fires when selected");

    lfo.phase = 0x60000000u;
    lfo.oneShotState = LFO_ONESHOT_IDLE;
    lfo.retrigger = 3u;                 /* v3 */
    check(fabsf(lfo_calc(&lfo)) < 0.001f, "voice retrigger: waits at the start value");
    check(lfo.oneShotState == LFO_ONESHOT_IDLE && lfo.phase == 0u,
          "voice retrigger: stays idle at phase 0");
    lfo_retriggerPhase(&lfo);
    check(lfo.oneShotState == LFO_ONESHOT_RUN, "the voice trigger then fires it");

    printf("lfo_oneshot %s (%u failures)\n", failures ? "FAILED" : "OK",
           failures);
    return failures ? 1 : 0;
}
