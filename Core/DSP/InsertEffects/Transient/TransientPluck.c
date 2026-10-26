/*
 * Pluck transient buffer (S084 VX3 VX3-8.2; remediation R11).
 * What: trigger-seeds a Karplus-Strong one-unit ring, then feeds a damped
 * fractional tap back into the PRE-filter signal. Inputs: trigger, ring, c0/c1.
 * Output: block in place and wet-only tail.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "DelayLine.h"
#include "InsertTypeCommon.h"
#include "transientTables.h"
#include <string.h>

typedef struct { uint8_t armed; uint8_t pad[3]; float gain; } pluck_state_t;
_Static_assert(sizeof(pluck_state_t) <= INSERT_STATE_BYTES,
               "Pluck state exceeds insert budget");

static void pluck_init(void *state, const insert_block_ctx_t *ctx)
{ (void)ctx; memset(state, 0, sizeof(pluck_state_t)); }

static void pluck_run(pluck_state_t *st, int16_t *buf, uint8_t size,
                      const insert_block_ctx_t *ctx, uint8_t tail)
{
    const float freq = 55.0f + 1200.0f * ctx->c1;
    float delay = REAL_FS / freq;
    const float gain = delay_feedbackCeiling(delay / REAL_FS);
    uint8_t i;

    if (!ctx->ring || !ctx->wp || ctx->ring_len < 4u)
        return;
    if (delay > (float)ctx->ring_len - 2.0f)
        delay = (float)ctx->ring_len - 2.0f;
    if (ctx->trigger && !tail) {
        uint32_t j;

        /*
         * Remediation R11: seed with the first d samples of the stock PCM
         * `Clk` (VX3-8.2) at half scale. The first version wrote an
         * alternating +/-12000 sequence: that is pure Nyquist, which the
         * two-tap average below cancels, so the pluck was a click. `delay`
         * is clamped below ring_len above, so no modulo is needed.
         */
        for (j = 0u; j < (uint32_t)delay && j < TRANSIENT_SAMPLE_LENGTH; j++)
            ctx->ring[j] = (int16_t)(transientData[0][j] * 128);
        for (; j < (uint32_t)delay; j++)
            ctx->ring[j] = 0;
        *ctx->wp = (uint32_t)delay;
        st->armed = 1u;
    }
    for (i = 0u; i < size; i++) {
        const float tap = 0.5f *
            (delay_tap(ctx->ring, ctx->ring_len, *ctx->wp, delay) +
             delay_tap(ctx->ring, ctx->ring_len, *ctx->wp, delay + 1.0f));
        const int16_t input = tail ? 0 : buf[i];

        delay_write(ctx->ring, ctx->ring_len, ctx->wp,
                    insert_to16((float)input * 0.1f + gain * tap));
        buf[i] = tail ? insert_to16(tap) :
            insert_to16((float)input + ctx->c0 * tap);
    }
}

static void pluck_process(void *state, int16_t *buf, uint8_t size,
                          const insert_block_ctx_t *ctx)
{ pluck_run((pluck_state_t *)state, buf, size, ctx, 0u); }
static void pluck_tail(void *state, int16_t *buf, uint8_t size,
                       const insert_block_ctx_t *ctx)
{ pluck_run((pluck_state_t *)state, buf, size, ctx, 1u); }

const insert_effect_type_t insert_typePluck = {
    "plk", { "wav", "vol", "tun" }, { "Wave", "Volume", "Tune" },
    1u, 7u, INSERT_PLACE_PRE, INSERT_FLAG_DISABLED,
    pluck_init, NULL, pluck_process, pluck_tail
};
