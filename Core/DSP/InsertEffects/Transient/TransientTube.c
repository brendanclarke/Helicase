/*
 * Tube transient buffer (S084 VX3 VX3-8.3).
 * What: negative-feedback damped comb for odd-harmonic transient colour.
 * Inputs: ring, c0 volume and c1 pitch. Output: PRE block and wet tail.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "DelayLine.h"
#include "InsertTypeCommon.h"
#include <math.h>

typedef struct { float damping; } tube_state_t;
_Static_assert(sizeof(tube_state_t) <= INSERT_STATE_BYTES,
               "Tube state exceeds insert budget");

static void tube_init(void *state, const insert_block_ctx_t *ctx)
{ (void)ctx; ((tube_state_t *)state)->damping = 0.0f; }

static void tube_run(tube_state_t *st, int16_t *buf, uint8_t size,
                     const insert_block_ctx_t *ctx, uint8_t tail)
{
    float delay = REAL_FS / (20.0f * powf(100.0f, ctx->c1));
    const float gain = 0.96f * delay_feedbackCeiling(delay / REAL_FS);
    uint8_t i;

    if (!ctx->ring || !ctx->wp || ctx->ring_len < 4u)
        return;
    if (delay > (float)ctx->ring_len - 2.0f)
        delay = (float)ctx->ring_len - 2.0f;
    for (i = 0u; i < size; i++) {
        const float tap = delay_onePole(&st->damping,
            delay_tap(ctx->ring, ctx->ring_len, *ctx->wp, delay), 0.4f);
        const int16_t input = tail ? 0 : buf[i];

        delay_write(ctx->ring, ctx->ring_len, ctx->wp,
                    insert_to16((float)input * 0.25f - gain * tap));
        buf[i] = tail ? insert_to16(tap) :
            insert_to16((float)input + ctx->c0 * tap);
    }
}

static void tube_process(void *state, int16_t *buf, uint8_t size,
                         const insert_block_ctx_t *ctx)
{ tube_run((tube_state_t *)state, buf, size, ctx, 0u); }
static void tube_tail(void *state, int16_t *buf, uint8_t size,
                      const insert_block_ctx_t *ctx)
{ tube_run((tube_state_t *)state, buf, size, ctx, 1u); }

const insert_effect_type_t insert_typeTube = {
    "tub", { "wav", "vol", "tun" }, { "Wave", "Volume", "Tune" },
    1u, 8u, INSERT_PLACE_PRE, INSERT_FLAG_DISABLED,
    tube_init, NULL, tube_process, tube_tail
};
