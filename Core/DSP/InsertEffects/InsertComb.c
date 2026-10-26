/*
 * Comb Mix insert (S084 VX3 VX3-6.9).
 * What: a pitch-controlled feedback comb with decay-limited feedback. Why:
 * it provides a compact one-unit resonator without a second control path.
 * Inputs: ring, c0 pitch/depth, c1 pitch, c2 wet. Output: block and tail.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "InsertTypeCommon.h"
#include "DelayLine.h"
#include <math.h>

typedef struct { float reserved[2]; } comb_state_t;
_Static_assert(sizeof(comb_state_t) <= INSERT_STATE_BYTES,
               "Comb state exceeds insert budget");

static void comb_init(void *state, const insert_block_ctx_t *ctx)
{
    (void)state; (void)ctx;
}

/*
 * Remediation §18 (cost): the comb delay is constant for the block, so the
 * read index and fraction are set up once (delay_tapStart) and each sample
 * does two loads and one multiply-add; the wet blend is done in float with
 * one saturation. Measured loop: see S084_P3_IMPLEMENTATION.md §18.4.
 */
static void comb_run(int16_t *buf, uint8_t size,
                     const insert_block_ctx_t *ctx, uint8_t wet_only)
{
    const float freq = 40.0f * powf(2.0f, 5.6f * ctx->c1);
    float delay = REAL_FS / freq;
    const float feedback = ctx->c0 *
        delay_feedbackCeiling(delay / REAL_FS);
    const float wet_gain = wet_only ? 1.0f : ctx->c2;
    const float dry_gain = wet_only ? 0.0f : 1.0f - ctx->c2;
    int16_t *ring = ctx->ring;
    uint32_t len;
    uint32_t wp;
    uint32_t r0;
    float frac;
    uint8_t i;

    if (!ring || !ctx->wp || ctx->ring_len < 4u)
        return;
    len = ctx->ring_len;
    if (delay > (float)len - 2.0f)
        delay = (float)len - 2.0f;
    wp = *ctx->wp;
    r0 = delay_tapStart(len, wp, delay, &frac);
    for (i = 0u; i < size; i++) {
        const float tap = delay_tapStep(ring, len, r0, frac);
        const float input = wet_only ? 0.0f : (float)buf[i];

        ring[wp] = insert_to16(input + feedback * tap);
        wp = delay_advance(wp, len);
        r0 = delay_advance(r0, len);
        buf[i] = insert_to16(dry_gain * input + wet_gain * tap);
    }
    *ctx->wp = wp;
}

static void comb_process(void *state, int16_t *buf, uint8_t size,
                         const insert_block_ctx_t *ctx)
{
    (void)state; comb_run(buf, size, ctx, 0u);
}

static void comb_tail(void *state, int16_t *wet, uint8_t size,
                      const insert_block_ctx_t *ctx)
{
    (void)state; comb_run(wet, size, ctx, 1u);
}

const insert_effect_type_t insert_typeComb = {
    "cmb", { "fbk", "ptc", "wet" }, { "Feedback", "Pitch", "Wet" },
    1u, 4u, INSERT_PLACE_NONE, INSERT_FLAG_INPUT_GAIN,
    comb_init, NULL, comb_process, comb_tail
};
