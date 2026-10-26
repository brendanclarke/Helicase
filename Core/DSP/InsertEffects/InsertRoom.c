/*
 * Room Mix inserts Rev/Rv2 (S084 VX3 VX3-6.10; remediation R9/R10).
 *
 * What: a half-rate room built from two parallel damped feedback combs and
 * two first-order allpasses. The ring is split into two sub-rings (0.565 and
 * 0.435 of its length, the 0.61 : 0.47 ratio of VX3-6.10); each comb reads
 * its oldest sample, damps it with a one-pole (`dmp` = c1) and writes the
 * input plus g_i times the damped sample back, with
 * g_i = 10^(-3 d_i / RT60), RT60 = 0.3 s + 2.7 s * c0 (3 s limit).
 * Why: the first version fed both taps of one ring into a single write, a
 * loop whose DC gain is g1 + g2 (about 1.77 at full decay); it ran away to
 * full scale and never decayed. Two separate combs each have a loop gain
 * below one, so the decay law holds at every setting.
 * Half rate: the network steps on every second sample from the average of
 * the two inputs (a two-tap anti-alias filter) and the output is linearly
 * interpolated between network outputs (half a network sample of latency),
 * replacing the zero-order hold.
 * Families: Rev and Rv2 are separate families (deviation from O1, logged in
 * S084_P3_IMPLEMENTATION.md §18): a split ring cannot grow in place without
 * moving both combs, so Rev<->Rv2 tails the old room and starts the new one.
 * Inputs: ring, c0 decay, c1 damping, c2 wet. Outputs: block in place or a
 * wet-only tail. Cost: no division and no library call in the loop.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "InsertTypeCommon.h"
#include "DelayLine.h"
#include <math.h>
#include <string.h>

typedef struct {
    uint32_t wp_b;      /* comb B position; comb A uses *ctx->wp */
    float damp_a;
    float damp_b;
    float ap1;
    float ap2;
    float wet_last;     /* last network output (interpolation endpoint) */
    float x_held;       /* first input of the current pair */
    uint8_t phase;
    uint8_t pad[3];
} room_state_t;
_Static_assert(sizeof(room_state_t) <= INSERT_STATE_BYTES,
               "Room state exceeds insert budget");

static void room_init(void *state, const insert_block_ctx_t *ctx)
{
    room_state_t *st = (room_state_t *)state;

    (void)ctx; memset(st, 0, sizeof(*st));
}

static void room_run(room_state_t *st, int16_t *buf, uint8_t size,
                     const insert_block_ctx_t *ctx, uint8_t wet_only)
{
    const uint32_t len = ctx->ring_len;
    const uint32_t len_a = (len * 9257u) >> 14;          /* 0.565 * len */
    const uint32_t len_b = len - len_a;
    const float rt60 = 0.3f + 2.7f * ctx->c0;
    /* One ring sample spans two output samples at half rate. */
    const float g_a = expf(-6.907755279f * (float)len_a /
                           ((float)REAL_FS * 0.5f * rt60));
    const float g_b = expf(-6.907755279f * (float)len_b /
                           ((float)REAL_FS * 0.5f * rt60));
    const float damp = 0.7f - 0.6f * ctx->c1;
    int16_t *ring_a;
    int16_t *ring_b;
    uint32_t wp_a;
    uint32_t wp_b;
    uint8_t i;

    if (!ctx->ring || !ctx->wp || len < 8u)
        return;
    ring_a = ctx->ring;
    ring_b = ctx->ring + len_a;
    wp_a = (*ctx->wp < len_a) ? *ctx->wp : 0u;
    wp_b = (st->wp_b < len_b) ? st->wp_b : 0u;
    for (i = 0u; i < size; i++) {
        const float x = wet_only ? 0.0f : (float)buf[i];
        float out;

        if (st->phase == 0u) {
            st->x_held = x;
            out = st->wet_last;
        } else {
            const float xin = 0.25f * (st->x_held + x);   /* avg, -6 dB */
            const float za = delay_onePole(&st->damp_a,
                                           (float)ring_a[wp_a], damp);
            const float zb = delay_onePole(&st->damp_b,
                                           (float)ring_b[wp_b], damp);
            const float wet = delay_allpass(&st->ap2,
                delay_allpass(&st->ap1, 0.5f * (za + zb), 0.35f), 0.2f);

            ring_a[wp_a] = insert_to16(xin + g_a * za);
            ring_b[wp_b] = insert_to16(xin + g_b * zb);
            wp_a = delay_wrap(wp_a + 1u, len_a);
            wp_b = delay_wrap(wp_b + 1u, len_b);
            out = 0.5f * (st->wet_last + wet);
            st->wet_last = wet;
        }
        buf[i] = wet_only ? insert_to16(out) :
            insert_wet16(buf[i], insert_to16(out), ctx->c2);
        st->phase ^= 1u;
    }
    *ctx->wp = wp_a;
    st->wp_b = wp_b;
}

static void room_process(void *state, int16_t *buf, uint8_t size,
                         const insert_block_ctx_t *ctx)
{
    room_run((room_state_t *)state, buf, size, ctx, 0u);
}

static void room_tail(void *state, int16_t *wet, uint8_t size,
                      const insert_block_ctx_t *ctx)
{
    room_run((room_state_t *)state, wet, size, ctx, 1u);
}

const insert_effect_type_t insert_typeRoom = {
    "rev", { "dcy", "dmp", "wet" }, { "Decay", "Damping", "Wet" },
    1u, 5u, INSERT_PLACE_NONE, INSERT_FLAG_INPUT_GAIN | INSERT_FLAG_HALF_RATE | INSERT_FLAG_DISABLED,
    room_init, NULL, room_process, room_tail
};

const insert_effect_type_t insert_typeRoom2 = {
    "rv2", { "dcy", "dmp", "wet" }, { "Decay", "Damping", "Wet" },
    2u, 13u, INSERT_PLACE_NONE, INSERT_FLAG_INPUT_GAIN | INSERT_FLAG_HALF_RATE | INSERT_FLAG_DISABLED,
    room_init, NULL, room_process, room_tail
};
