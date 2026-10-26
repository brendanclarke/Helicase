/*
 * Flam transient buffer (S084 VX3 VX3-8.4).
 * What: one short delayed copy in the POST-filter stage. Inputs: one-unit
 * ring, c0 volume and c1 delay. Output: block in place and wet tail.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "DelayLine.h"
#include "InsertTypeCommon.h"

typedef struct { uint32_t reserved; } flam_state_t;
_Static_assert(sizeof(flam_state_t) <= INSERT_STATE_BYTES,
               "Flam state exceeds insert budget");

static void flam_init(void *state, const insert_block_ctx_t *ctx)
{ (void)state; (void)ctx; }

/*
 * Remediation §18 (cost): a 5-50 ms flam needs no fractional delay, so the
 * copy is read at an integer offset set once per block. Per sample: one
 * load, one store, two index wraps and one multiply-add.
 */
static void flam_run(int16_t *buf, uint8_t size, const insert_block_ctx_t *ctx,
                     uint8_t tail)
{
    uint32_t delay = (uint32_t)((0.005f + ctx->c1 * 0.045f) * REAL_FS);
    int16_t *ring = ctx->ring;
    uint32_t len;
    uint32_t wp;
    uint32_t rp;
    uint8_t i;

    if (!ring || !ctx->wp || ctx->ring_len < 4u)
        return;
    len = ctx->ring_len;
    if (delay > len - 2u)
        delay = len - 2u;
    wp = *ctx->wp;
    rp = (wp >= delay) ? wp - delay : wp + len - delay;
    for (i = 0u; i < size; i++) {
        const int32_t input = tail ? 0 : buf[i];
        const int32_t tap = ring[rp];

        ring[wp] = (int16_t)input;
        wp = delay_advance(wp, len);
        rp = delay_advance(rp, len);
        buf[i] = tail ? (int16_t)tap :
            insert_to16((float)input + ctx->c0 * (float)tap);
    }
    *ctx->wp = wp;
}

static void flam_process(void *state, int16_t *buf, uint8_t size,
                         const insert_block_ctx_t *ctx)
{ (void)state; flam_run(buf, size, ctx, 0u); }
static void flam_tail(void *state, int16_t *buf, uint8_t size,
                      const insert_block_ctx_t *ctx)
{ (void)state; flam_run(buf, size, ctx, 1u); }

const insert_effect_type_t insert_typeFlam = {
    "flm", { "wav", "vol", "tim" }, { "Wave", "Volume", "Time" },
    1u, 9u, INSERT_PLACE_POST, 0u,
    flam_init, NULL, flam_process, flam_tail
};
