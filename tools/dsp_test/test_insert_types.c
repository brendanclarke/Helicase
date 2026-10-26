/*
 * Insert type registry contract (S084 VX3 VX3-11.4; remediation §18 T1).
 *
 * What:       compiles and exercises every real Mix and transient descriptor:
 *             descriptor shape (units, placement, labels); the 3 s decay
 *             limit at every control corner (0.2 s of noise, then 3 s of
 *             silence: the last 0.1 s must stay at or below -60 dBFS, through
 *             process() and through tail()); and the trigger semantics of
 *             Plk (audible pluck from the trigger alone), Bzz (repeats the
 *             audio recorded after the trigger) and Rvs (plays the audio
 *             recorded after the trigger backwards).
 * Why:        Gate G3/G4. The first version only asserted that int16 values
 *             lie in the int16 range, which is always true, so an unstable
 *             room and three broken transient types passed.
 * Inputs:     deterministic noise and a two-unit host ring.
 * Outputs:    OK / failure count.
 * Accessors:  make -C tools/dsp_test test_insert_types.
 * Affiliates: the Core/DSP/InsertEffects C files and InsertEffect.h.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "InsertEffectsManager.h"
#include "FxBuffer.h"

/* Ring/Flanger/Chorus read the firmware's 4,096-point sine table symbol; a
** zero table holds their LFOs still, which does not affect decay. */
const int16_t sine_table[4097] = { 0 };

#define BLOCK          32u
#define FS_BLOCKS(s)   ((unsigned)((s) * 44108.0f / BLOCK))
#define QUIET_PEAK     33              /* -60 dBFS */

typedef union {
    max_align_t align;
    uint8_t bytes[INSERT_STATE_BYTES];
} type_state_t;

static unsigned failures;
static uint32_t noise_state = 0x8e5d1234u;
static int16_t ring[FXBUF_VOICE_UNIT_SAMPLES * 2u];

static void check(int condition, const char *type, const char *what)
{
    if (!condition) {
        printf("FAIL: %s: %s\n", type, what);
        failures++;
    }
}

static int16_t next_noise(void)
{
    noise_state = noise_state * 1664525u + 1013904223u;
    return (int16_t)((int16_t)(noise_state >> 16) / 2);
}

static void setup(const insert_effect_type_t *type, type_state_t *st,
                  insert_block_ctx_t *ctx, uint32_t *wp,
                  float c0, float c1, float c2)
{
    memset(st, 0, sizeof(*st));
    memset(ring, 0, sizeof(ring));
    memset(ctx, 0, sizeof(*ctx));
    *wp = 0u;
    ctx->ring = type->units ? ring : NULL;
    ctx->ring_len = (uint32_t)type->units * FXBUF_VOICE_UNIT_SAMPLES;
    ctx->wp = wp;
    ctx->c0 = c0;
    ctx->c1 = c1;
    ctx->c2 = c2;
    ctx->rate_shaped = 1.0f;
    type->init(st->bytes, ctx);
}

/* Peak of the final 0.1 s after 0.2 s of noise and 3 s of silence. */
static int decay_peak(const insert_effect_type_t *type, float c0, float c1,
                      uint8_t trigger, uint8_t via_tail)
{
    type_state_t st;
    insert_block_ctx_t ctx;
    uint32_t wp;
    int16_t block[BLOCK];
    unsigned b;
    unsigned i;
    int peak = 0;

    setup(type, &st, &ctx, &wp, c0, c1, 1.0f);
    for (b = 0u; b < FS_BLOCKS(0.2f); b++) {
        for (i = 0u; i < BLOCK; i++)
            block[i] = next_noise();
        ctx.trigger = (uint8_t)(trigger && b == 0u);
        type->process(st.bytes, block, BLOCK, &ctx);
    }
    ctx.trigger = 0u;
    for (b = 0u; b < FS_BLOCKS(3.0f); b++) {
        memset(block, 0, sizeof(block));
        if (via_tail)
            type->tail(st.bytes, block, BLOCK, &ctx);
        else
            type->process(st.bytes, block, BLOCK, &ctx);
        if (b >= FS_BLOCKS(2.9f))
            for (i = 0u; i < BLOCK; i++)
                if (abs(block[i]) > peak)
                    peak = abs(block[i]);
    }
    return peak;
}

static void exercise(const insert_effect_type_t *type, uint8_t transient)
{
    static const float corner[3] = { 0.0f, 0.5f, 1.0f };
    unsigned a;
    unsigned c;
    unsigned i;

    check(type && type->name3 && type->process && type->init,
          type ? type->name3 : "?", "descriptor operations exist");
    if (!type || !type->process || !type->init)
        return;
    check(type->units <= 2u, type->name3, "unit count is bounded");
    check(type->placement <= INSERT_PLACE_POST, type->name3,
          "placement is valid");
    check(!transient || type->placement != INSERT_PLACE_NONE, type->name3,
          "transient types are PRE or POST");
    for (i = 0u; i < 3u; i++)
        check(type->label3[i] && type->label8[i], type->name3,
              "labels are present");
    if (type->units == 0u)
        return;
    check(type->tail != NULL, type->name3, "buffer types render a tail");
    for (a = 0u; a < 3u; a++)
        for (c = 0u; c < 3u; c++) {
            char what[64];
            int peak = decay_peak(type, corner[a], corner[c], transient, 0u);

            snprintf(what, sizeof(what), "decay <= 3 s (c0 %.1f c1 %.1f): %d",
                     corner[a], corner[c], peak);
            check(peak <= QUIET_PEAK, type->name3, what);
            if (type->tail) {
                peak = decay_peak(type, corner[a], corner[c], transient, 1u);
                snprintf(what, sizeof(what),
                         "tail quiet <= 3 s (c0 %.1f c1 %.1f): %d",
                         corner[a], corner[c], peak);
                check(peak <= QUIET_PEAK, type->name3, what);
            }
        }
}

/* Plk: the trigger alone must produce an audible pluck. */
static void semantics_pluck(void)
{
    type_state_t st;
    insert_block_ctx_t ctx;
    uint32_t wp;
    int16_t block[BLOCK];
    double energy = 0.0;
    unsigned b;
    unsigned i;

    setup(&insert_typePluck, &st, &ctx, &wp, 1.0f, 0.2f, 0.0f);
    for (b = 0u; b < FS_BLOCKS(0.05f); b++) {
        memset(block, 0, sizeof(block));
        ctx.trigger = (uint8_t)(b == 0u);
        insert_typePluck.process(st.bytes, block, BLOCK, &ctx);
        for (i = 0u; i < BLOCK; i++)
            energy += (double)block[i] * block[i];
    }
    check(sqrt(energy / (FS_BLOCKS(0.05f) * BLOCK)) > 1000.0, "plk",
          "trigger alone gives an audible pluck (RMS > 1000 over 50 ms)");
}

/* Bzz/Rvs: what is repeated or reversed is the audio after the trigger. */
static void semantics_record(const insert_effect_type_t *type, uint8_t reverse)
{
    type_state_t st;
    insert_block_ctx_t ctx;
    uint32_t wp;
    int16_t block[BLOCK];
    unsigned n = 0u;
    unsigned b;
    unsigned i;
    int first_wet = 0;
    int last_wet = 0;
    unsigned wet_count = 0u;

    setup(type, &st, &ctx, &wp, 1.0f, 0.0f, 0.0f);
    for (i = 0u; i < FXBUF_VOICE_UNIT_SAMPLES; i++)
        ring[i] = 5000;                   /* stale audio from before */
    for (b = 0u; b < FS_BLOCKS(0.1f); b++) {
        for (i = 0u; i < BLOCK; i++, n++)
            block[i] = (n < 400u) ? (int16_t)(1000 + n) : 0;  /* a ramp */
        ctx.trigger = (uint8_t)(b == 0u);
        type->process(st.bytes, block, BLOCK, &ctx);
        for (i = 0u; i < BLOCK; i++) {
            const unsigned k = b * BLOCK + i;

            if (k >= 600u && block[i] != 0) {
                if (wet_count == 0u)
                    first_wet = block[i];
                last_wet = block[i];
                wet_count++;
            }
        }
    }
    check(wet_count > 0u, type->name3, "the recorded audio is played back");
    check(first_wet < 4000 && last_wet < 4000, type->name3,
          "playback is the post-trigger audio, not stale ring content");
    if (reverse)
        check(wet_count > 200u, type->name3, "reverse plays the segment");
}

int main(void)
{
    static const insert_effect_type_t *const mix[] = {
        &insert_typeDriveSrr, &insert_typeFold, &insert_typeCrush,
        &insert_typeRing, &insert_typePhaser, &insert_typeFlanger,
        &insert_typeChorus, &insert_typeEcho, &insert_typeEcho2,
        &insert_typeComb, &insert_typeRoom, &insert_typeRoom2
    };
    static const insert_effect_type_t *const transient[] = {
        &insert_typeResonator, &insert_typePluck, &insert_typeTube,
        &insert_typeFlam, &insert_typeStutter, &insert_typeReverse,
        &insert_typeSpring
    };
    unsigned i;

    check(INSERT_MIX_COUNT == 12u && INSERT_TRANSIENT_COUNT == 7u, "registry",
          "registry counts");
    for (i = 0u; i < INSERT_MIX_COUNT; i++)
        exercise(mix[i], 0u);
    for (i = 0u; i < INSERT_TRANSIENT_COUNT; i++)
        exercise(transient[i], 1u);
    semantics_pluck();
    semantics_record(&insert_typeStutter, 0u);
    semantics_record(&insert_typeReverse, 1u);

    if (failures == 0u)
        printf("insert_types OK (0 failures)\n");
    return failures != 0u;
}
