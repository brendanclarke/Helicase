/*
 * Spring transient buffer (S084 VX3 VX3-8.7).
 * What: a short allpass feedback spring with one-pole colour in POST.
 * Inputs: ring, c0 volume and c1 length. Output: block in place and tail.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "DelayLine.h"
#include "InsertTypeCommon.h"
#include <string.h>

typedef struct { float colour; float allpass; } spring_state_t;
_Static_assert(sizeof(spring_state_t) <= INSERT_STATE_BYTES,
               "Spring state exceeds insert budget");

static void spring_init(void *state, const insert_block_ctx_t *ctx)
{ (void)ctx; memset(state, 0, sizeof(spring_state_t)); }

static void spring_run(spring_state_t *st, int16_t *buf, uint8_t size,
                       const insert_block_ctx_t *ctx, uint8_t tail)
{
    float delay = (0.020f + ctx->c1 * 0.030f) * REAL_FS;
    const float g = 0.7f * delay_feedbackCeiling(0.05f);
    uint8_t i;

    if (!ctx->ring || !ctx->wp || ctx->ring_len < 4u)
        return;
    if (delay > (float)ctx->ring_len - 2.0f)
        delay = (float)ctx->ring_len - 2.0f;
    for (i = 0u; i < size; i++) {
        const float tap = delay_tap(ctx->ring, ctx->ring_len, *ctx->wp,
                                    delay);
        const int16_t input = tail ? 0 : buf[i];
        const float v = (float)input + g * tap;
        const float y = delay_onePole(&st->colour,
                                      delay_allpass(&st->allpass, tap, g),
                                      0.1f);

        delay_write(ctx->ring, ctx->ring_len, ctx->wp, insert_to16(v));
        buf[i] = tail ? insert_to16(y) :
            insert_to16((float)input + ctx->c0 * y);
    }
}

static void spring_process(void *state, int16_t *buf, uint8_t size,
                           const insert_block_ctx_t *ctx)
{ spring_run((spring_state_t *)state, buf, size, ctx, 0u); }
static void spring_tail(void *state, int16_t *buf, uint8_t size,
                        const insert_block_ctx_t *ctx)
{ spring_run((spring_state_t *)state, buf, size, ctx, 1u); }

const insert_effect_type_t insert_typeSpring = {
    "spr", { "wav", "vol", "tim" }, { "Wave", "Volume", "Time" },
    1u, 12u, INSERT_PLACE_POST, INSERT_FLAG_DISABLED,
    spring_init, NULL, spring_process, spring_tail
};
