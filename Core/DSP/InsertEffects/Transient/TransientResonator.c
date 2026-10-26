/*
 * Resonator transient buffer (S084 VX3 VX3-8.1).
 * What: excites a damped one-unit comb at a pitch-derived delay. Why: PRE
 * transient buffer processing stays inside the common VoiceBuffer contract.
 * Inputs: ring, c0 volume and c1 pitch. Output: block in place/tail state.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "DelayLine.h"
#include "InsertTypeCommon.h"
#include <math.h>

typedef struct { float damping; } resonator_state_t;
_Static_assert(sizeof(resonator_state_t) <= INSERT_STATE_BYTES,
               "Resonator state exceeds insert budget");

static void resonator_init(void *state, const insert_block_ctx_t *ctx)
{ (void)ctx; ((resonator_state_t *)state)->damping = 0.0f; }

static void resonator_run(resonator_state_t *st, int16_t *buf, uint8_t size,
                          const insert_block_ctx_t *ctx, uint8_t tail)
{
    const float freq = 20.0f * powf(100.0f, ctx->c1);
    float delay = REAL_FS / freq;
    const float gain = 0.98f * delay_feedbackCeiling(delay / REAL_FS);
    uint8_t i;

    if (!ctx->ring || !ctx->wp || ctx->ring_len < 4u)
        return;
    if (delay > (float)ctx->ring_len - 2.0f)
        delay = (float)ctx->ring_len - 2.0f;
    for (i = 0u; i < size; i++) {
        const float tap = delay_tap(ctx->ring, ctx->ring_len, *ctx->wp,
                                    delay);
        const int16_t input = tail ? 0 : buf[i];
        const float y = delay_onePole(&st->damping, tap, 0.4f);

        delay_write(ctx->ring, ctx->ring_len, ctx->wp,
                    insert_to16((float)input * 0.25f + gain * y));
        buf[i] = tail ? insert_to16(y) :
            insert_to16((float)input + ctx->c0 * y);
    }
}

static void resonator_process(void *state, int16_t *buf, uint8_t size,
                              const insert_block_ctx_t *ctx)
{ resonator_run((resonator_state_t *)state, buf, size, ctx, 0u); }
static void resonator_tail(void *state, int16_t *buf, uint8_t size,
                           const insert_block_ctx_t *ctx)
{ resonator_run((resonator_state_t *)state, buf, size, ctx, 1u); }

const insert_effect_type_t insert_typeResonator = {
    "res", { "wav", "vol", "tun" }, { "Wave", "Volume", "Tune" },
    1u, 6u, INSERT_PLACE_PRE, INSERT_FLAG_DISABLED,
    resonator_init, NULL, resonator_process, resonator_tail
};
