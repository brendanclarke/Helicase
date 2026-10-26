/*
 * Flanger Mix insert (S084 VX3 VX3-6.6).
 * What: writes a feedback delay and reads a modulated fractional tap. Why:
 * the one-unit ring gives a short, continuously gliding comb. Inputs: ring,
 * c0 depth, c1 rate, c2 wet. Output: block in place; tail is wet-only.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "InsertTypeCommon.h"
#include "DelayLine.h"
#include "wavetable.h"
#include <math.h>

typedef struct { uint32_t phase; float delay; float reserved; } flanger_state_t;
_Static_assert(sizeof(flanger_state_t) <= INSERT_STATE_BYTES,
               "Flanger state exceeds insert budget");

static void flanger_init(void *state, const insert_block_ctx_t *ctx)
{
    flanger_state_t *st = (flanger_state_t *)state;

    (void)ctx; st->phase = 0u; st->delay = 0.0005f * REAL_FS;
    st->reserved = 0.0f;
}

/*
 * Block-rate LFO, per-sample delay ramp (VX3-6.6; remediation R14).
 *
 * What: the LFO phase advances once per block and is read from the shared
 * sine_table; the delay then ramps linearly from its previous value to the
 * block's end value. Why: the first version called sinf() twice per sample,
 * far over the per-type budget; VX3-6.6 specified this block ramp.
 */
static void flanger_run(flanger_state_t *st, int16_t *buf, uint8_t size,
                        const insert_block_ctx_t *ctx, uint8_t wet_only)
{
    const float rate = 0.05f * powf(60.0f, ctx->c1);
    const float dmean = 0.0005f * REAL_FS + 0.0075f * REAL_FS * 0.5f;
    const float fb = ctx->c0 * fminf(0.9f,
                                     delay_feedbackCeiling(dmean / REAL_FS));
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
    d_end = 0.0005f * REAL_FS + 0.0075f * REAL_FS * lfo;
    d = st->delay;
    d_step = (d_end - d) * (1.0f / (float)size);
    for (i = 0u; i < size; i++) {
        const float tap = delay_tap(ctx->ring, ctx->ring_len, *ctx->wp, d);
        const int16_t input = wet_only ? 0 : buf[i];
        const int16_t written = insert_to16((float)input + fb * tap);

        delay_write(ctx->ring, ctx->ring_len, ctx->wp, written);
        if (wet_only)
            buf[i] = insert_to16(tap);
        else
            buf[i] = insert_wet16(input, insert_to16(tap), ctx->c2 * 0.5f);
        d += d_step;
    }
    st->delay = d_end;
}

static void flanger_process(void *state, int16_t *buf, uint8_t size,
                            const insert_block_ctx_t *ctx)
{
    flanger_run((flanger_state_t *)state, buf, size, ctx, 0u);
}

static void flanger_tail(void *state, int16_t *wet, uint8_t size,
                         const insert_block_ctx_t *ctx)
{
    flanger_run((flanger_state_t *)state, wet, size, ctx, 1u);
}

const insert_effect_type_t insert_typeFlanger = {
    "fla", { "fbk", "rte", "wet" }, { "Feedback", "Rate", "Wet" },
    1u, 1u, INSERT_PLACE_NONE, INSERT_FLAG_INPUT_GAIN | INSERT_FLAG_DISABLED,
    flanger_init, NULL, flanger_process, flanger_tail
};
