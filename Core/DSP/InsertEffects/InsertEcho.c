/*
 * Echo Mix inserts Ech/Ec2 (S084 VX3 VX3-6.8).
 * What: tape-gliding feedback delay with one-pole loop damping; Ec2 grows
 * from Ech at a write wrap. Why: this is the P3-B proof family for same-use,
 * tails and lease re-adoption. Inputs: one/two-unit ring and c0..c2.
 * Outputs: processed or wet-only tail block.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "InsertTypeCommon.h"
#include "DelayLine.h"
#include <math.h>

typedef struct {
    uint32_t active_len;
    uint32_t pending_len;
    float delay;
    float damp;
} echo_state_t;
_Static_assert(sizeof(echo_state_t) <= INSERT_STATE_BYTES,
               "Echo state exceeds insert budget");

static void echo_init(void *state, const insert_block_ctx_t *ctx)
{
    echo_state_t *st = (echo_state_t *)state;

    st->active_len = ctx ? ctx->ring_len : 0u;
    st->pending_len = 0u;
    st->delay = 0.005f * REAL_FS;
    st->damp = 0.0f;
}

static void echo_retarget(void *state, const insert_block_ctx_t *ctx,
                          uint8_t old_units)
{
    echo_state_t *st = (echo_state_t *)state;

    if (ctx && ctx->ring_len > st->active_len && old_units == 1u)
        st->pending_len = ctx->ring_len;
}

/*
 * Remediation §18 (cost): the tape glide now moves the delay once per block
 * (k = 0.062 per block, the per-sample 2e-3 glide compounded over 32
 * samples) and the read uses the block-constant fractional tap, so each
 * sample does two loads, one damping step and one multiply-add. The C3 grow
 * at the write wrap (R3) re-reads the length and restarts the read index.
 */
static void echo_run(echo_state_t *st, int16_t *buf, uint8_t size,
                     const insert_block_ctx_t *ctx, uint8_t wet_only)
{
    uint32_t len = st->active_len;
    const float max_delay = (len > 2u) ? (float)(len - 2u) : 1.0f;
    const float target = 0.005f * REAL_FS + ctx->c1 *
                        (max_delay - 0.005f * REAL_FS);
    const float safe_target = target < max_delay ? target : max_delay;
    const float feedback = ctx->c0 *
                           delay_feedbackCeiling(safe_target / REAL_FS);
    const float wet_gain = wet_only ? 1.0f : ctx->c2;
    const float dry_gain = wet_only ? 0.0f : 1.0f - ctx->c2;
    int16_t *ring = ctx->ring;
    uint32_t wp;
    uint32_t r0;
    float frac;
    uint8_t i;

    if (!ring || !ctx->wp || len < 4u)
        return;
    st->delay += 0.062f * (safe_target - st->delay);
    if (st->delay > max_delay)
        st->delay = max_delay;
    if (st->delay < 1.0f)
        st->delay = 1.0f;
    wp = *ctx->wp;
    r0 = delay_tapStart(len, wp, st->delay, &frac);
    /*
     * The block is processed in segments that end at the write wrap, so the
     * inner loop has no per-sample wrap or grow test for the write index.
     */
    i = 0u;
    while (i < size) {
        uint32_t seg = len - wp;
        uint32_t n;

        if (seg > (uint32_t)(size - i))
            seg = (uint32_t)(size - i);
        for (n = 0u; n < seg; n++, i++) {
            const float tap = delay_tapStep(ring, len, r0, frac);
            const float damped = delay_onePole(&st->damp, tap, 0.6f);
            const float input = wet_only ? 0.0f : (float)buf[i];

            ring[wp + n] = insert_to16(input + feedback * damped);
            r0 = delay_advance(r0, len);
            buf[i] = insert_to16(dry_gain * input + wet_gain * tap);
        }
        wp += seg;
        if (wp >= len) {
            wp = 0u;
            if (st->pending_len > len) {
                /*
                 * Grow at the write wrap (C3). Remediation R3: the ring base
                 * is unchanged, so samples [0, len) keep their positions and
                 * writing continues into the cleared upper unit; the length
                 * and the read index are re-derived for the rest of the block.
                 */
                wp = len;
                st->active_len = st->pending_len;
                st->pending_len = 0u;
                len = st->active_len;
                r0 = delay_tapStart(len, wp, st->delay, &frac);
            }
        }
    }
    *ctx->wp = wp;
}

static void echo_process(void *state, int16_t *buf, uint8_t size,
                         const insert_block_ctx_t *ctx)
{
    echo_run((echo_state_t *)state, buf, size, ctx, 0u);
}

static void echo_tail(void *state, int16_t *wet, uint8_t size,
                      const insert_block_ctx_t *ctx)
{
    echo_run((echo_state_t *)state, wet, size, ctx, 1u);
}

const insert_effect_type_t insert_typeEcho = {
    "ech", { "fbk", "tim", "wet" }, { "Feedback", "Time", "Wet" },
    1u, 3u, INSERT_PLACE_NONE, INSERT_FLAG_INPUT_GAIN,
    echo_init, echo_retarget, echo_process, echo_tail
};

const insert_effect_type_t insert_typeEcho2 = {
    "ec2", { "fbk", "tim", "wet" }, { "Feedback", "Time", "Wet" },
    2u, 3u, INSERT_PLACE_NONE, INSERT_FLAG_INPUT_GAIN,
    echo_init, echo_retarget, echo_process, echo_tail
};
