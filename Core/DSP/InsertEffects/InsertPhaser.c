/*
 * Phaser Mix insert (S084 VX3 VX3-6.5).
 * What: four block-rate-modulated first-order allpasses with bounded
 * feedback. Why: it provides a non-buffer ringless family member at the
 * common constant-cost Mix stage. Inputs: c0 depth, c1 rate, c2 wet. Output:
 * block in place.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "InsertTypeCommon.h"
#include "DelayLine.h"
#include <math.h>
#include <string.h>

typedef struct { float z[4]; float lfo; } phaser_state_t;
_Static_assert(sizeof(phaser_state_t) <= INSERT_STATE_BYTES,
               "Phaser state exceeds insert budget");

static void phaser_init(void *state, const insert_block_ctx_t *ctx)
{
    phaser_state_t *st = (phaser_state_t *)state;

    (void)ctx; memset(st, 0, sizeof(*st));
}

static void phaser_process(void *state, int16_t *buf, uint8_t size,
                           const insert_block_ctx_t *ctx)
{
    phaser_state_t *st = (phaser_state_t *)state;
    const float rate = 0.05f * powf(100.0f, ctx->c1);
    const float inc = rate * (float)size / REAL_FS;
    const float g = 0.2f + 0.7f * (0.5f + 0.5f * sinf(st->lfo));
    const float feedback = 0.7f * ctx->c0;
    uint8_t i;

    for (i = 0u; i < size; i++) {
        float x = (float)buf[i];
        float y;

        y = delay_allpass(&st->z[0], x, g);
        y = delay_allpass(&st->z[1], y, g);
        y = delay_allpass(&st->z[2], y, g);
        y = delay_allpass(&st->z[3], y + feedback * st->z[3], g);
        buf[i] = insert_to16(x + ctx->c2 * 0.5f * (y - x));
    }
    st->lfo += inc;
    if (st->lfo > 6.283185307f)
        st->lfo -= 6.283185307f;
}

const insert_effect_type_t insert_typePhaser = {
    "pha", { "dpt", "rte", "wet" }, { "Depth", "Rate", "Wet" },
    0u, 0u, INSERT_PLACE_NONE, INSERT_FLAG_INPUT_GAIN,
    phaser_init, NULL, phaser_process, NULL
};
