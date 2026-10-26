/*
 * Chorus Mix insert (S084 VX3 VX3-6.7).
 * What: reads a slow modulated 12 ms tap without feedback. Why: the one-unit
 * ring provides a longer, clean voice chorus at constant cost. Inputs: ring,
 * c0 depth, c1 rate, c2 wet. Output: block in place; tail is wet-only.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "InsertTypeCommon.h"
#include "DelayLine.h"
#include "wavetable.h"
#include <math.h>

typedef struct { uint32_t phase; float delay; } chorus_state_t;
_Static_assert(sizeof(chorus_state_t) <= INSERT_STATE_BYTES,
               "Chorus state exceeds insert budget");

static void chorus_init(void *state, const insert_block_ctx_t *ctx)
{
    chorus_state_t *st = (chorus_state_t *)state;

    (void)ctx; st->phase = 0u; st->delay = 0.012f * REAL_FS;
}

/* Block-rate sine_table LFO and per-sample delay ramp (VX3-6.7; R14). */
static void chorus_run(chorus_state_t *st, int16_t *buf, uint8_t size,
                       const insert_block_ctx_t *ctx, uint8_t wet_only)
{
    const float rate = 0.1f * powf(30.0f, ctx->c1);
    float lfo;
    float d_end;
    float d;
    float d_step;
    uint8_t i;

    if (!ctx->ring || !ctx->wp || ctx->ring_len < 4u || size == 0u)
        return;
    st->phase += (uint32_t)(rate * (float)size * (4294967296.0f / REAL_FS));
    lfo = 0.5f + 0.5f * ((float)sine_table[st->phase >> 20] *
                         (1.0f / 32767.0f));
    d_end = 0.012f * REAL_FS + ctx->c0 * 0.006f * REAL_FS * lfo;
    d = st->delay;
    d_step = (d_end - d) * (1.0f / (float)size);
    for (i = 0u; i < size; i++) {
        const int16_t input = wet_only ? 0 : buf[i];
        const int16_t tap = insert_to16(delay_tap(ctx->ring, ctx->ring_len,
                                                   *ctx->wp, d));

        delay_write(ctx->ring, ctx->ring_len, ctx->wp, input);
        buf[i] = wet_only ? tap : insert_wet16(input, tap, ctx->c2 * 0.5f);
        d += d_step;
    }
    st->delay = d_end;
}

static void chorus_process(void *state, int16_t *buf, uint8_t size,
                           const insert_block_ctx_t *ctx)
{
    chorus_run((chorus_state_t *)state, buf, size, ctx, 0u);
}

static void chorus_tail(void *state, int16_t *wet, uint8_t size,
                        const insert_block_ctx_t *ctx)
{
    chorus_run((chorus_state_t *)state, wet, size, ctx, 1u);
}

const insert_effect_type_t insert_typeChorus = {
    "cho", { "dpt", "rte", "wet" }, { "Depth", "Rate", "Wet" },
    1u, 2u, INSERT_PLACE_NONE, INSERT_FLAG_INPUT_GAIN | INSERT_FLAG_DISABLED,
    chorus_init, NULL, chorus_process, chorus_tail
};
