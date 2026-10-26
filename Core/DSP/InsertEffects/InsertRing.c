/*
 * Ring Mix insert (S084 VX3 VX3-6.4).
 * What: multiplies the input by a block-rate carrier and blends it. Why: the
 * phase starts at the P2 sine-table zero crossing and never changes CPU path.
 * Inputs: c0 depth, c1 carrier and c2 wet. Output: block in place.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "InsertTypeCommon.h"
#include "wavetable.h"
#include <math.h>

typedef struct { uint32_t phase; } ring_state_t;
_Static_assert(sizeof(ring_state_t) <= INSERT_STATE_BYTES,
               "Ring state exceeds insert budget");

static void ring_init(void *state, const insert_block_ctx_t *ctx)
{
    ring_state_t *st = (ring_state_t *)state;

    (void)ctx; st->phase = 1024u << 20;
}

static void ring_process(void *state, int16_t *buf, uint8_t size,
                         const insert_block_ctx_t *ctx)
{
    ring_state_t *st = (ring_state_t *)state;
    const float hz = 20.0f * powf(256.0f, ctx->c1);
    const uint32_t inc = (uint32_t)(hz * (4294967296.0f / REAL_FS));
    const float depth = ctx->c0;
    uint8_t i;

    for (i = 0u; i < size; i++) {
        const float s = -(float)sine_table[st->phase >> 20] /
                        32767.0f;
        const float mult = (1.0f - depth) + depth * s;
        const int16_t wet = insert_to16((float)buf[i] * mult);

        buf[i] = insert_wet16(buf[i], wet, ctx->c2);
        st->phase += inc;
    }
}

const insert_effect_type_t insert_typeRing = {
    "rng", { "dpt", "frq", "wet" }, { "Depth", "Carrier", "Wet" },
    0u, 0u, INSERT_PLACE_NONE, INSERT_FLAG_INPUT_GAIN,
    ring_init, NULL, ring_process, NULL
};
