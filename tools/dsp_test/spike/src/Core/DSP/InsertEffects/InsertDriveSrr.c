/*
 * D+S Mix insert (S084 VX3 VX3-6.1).
 * What: applies the engine distortion curve, then the former sample-and-hold
 * decimator. Why: this is the P2-compatible default and the S0 bit-identity
 * reference. Inputs: drive and rate_shaped. Output: int16 block in place.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "InsertTypeCommon.h"

typedef struct { float counter; int16_t held; uint16_t pad; } drive_srr_state_t;
_Static_assert(sizeof(drive_srr_state_t) <= INSERT_STATE_BYTES,
               "D+S state exceeds insert budget");

static void driveSrr_init(void *state, const insert_block_ctx_t *ctx)
{
    drive_srr_state_t *st = (drive_srr_state_t *)state;

    (void)ctx;
    st->counter = 0.0f;
    st->held = 0;
    st->pad = 0u;
}

static void driveSrr_process(void *state, int16_t *buf, uint8_t size,
                             const insert_block_ctx_t *ctx)
{
    drive_srr_state_t *st = (drive_srr_state_t *)state;
    uint8_t i;

    for (i = 0u; i < size; i++) {
        const int16_t driven = ctx->drive
            ? distortion_curveSample16(ctx->drive, buf[i]) : buf[i];

        buf[i] = insert_decimate(&st->counter, &st->held, driven,
                                 ctx->rate_shaped);
    }
}

const insert_effect_type_t insert_typeDriveSrr = {
    "d+s", { "drv", "srt", "---" }, { "Drive", "SampleRt", "-" },
    0u, 0u, INSERT_PLACE_NONE, 0u,
    driveSrr_init, NULL, driveSrr_process, NULL
};
