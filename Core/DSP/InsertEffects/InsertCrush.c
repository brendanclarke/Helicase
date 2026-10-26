/*
 * Crush Mix insert (S084 VX3 VX3-6.3).
 * What: quantizes to a block-resolved bit depth and blends the held result.
 * Why: the mask is constant over the block and the loop remains constant
 * cost. Inputs: c0 bits and c2 wet. Output: block in place.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "InsertTypeCommon.h"

typedef struct { float counter; int16_t held; uint16_t mask; } crush_state_t;
_Static_assert(sizeof(crush_state_t) <= INSERT_STATE_BYTES,
               "Crush state exceeds insert budget");

static void crush_init(void *state, const insert_block_ctx_t *ctx)
{
    crush_state_t *st = (crush_state_t *)state;

    (void)ctx; st->counter = 0.0f; st->held = 0; st->mask = 0xFFFFu;
}

static void crush_process(void *state, int16_t *buf, uint8_t size,
                          const insert_block_ctx_t *ctx)
{
    crush_state_t *st = (crush_state_t *)state;
    const uint8_t bits = (uint8_t)(16u - (uint8_t)(ctx->c0 * 12.0f + 0.5f));
    const uint16_t mask = (uint16_t)(0xFFFFu << (16u - bits));
    uint8_t i;

    st->mask = mask;
    for (i = 0u; i < size; i++) {
        const int16_t q = (int16_t)((uint16_t)buf[i] & mask);
        const int16_t held = insert_decimate(&st->counter, &st->held, q,
                                             ctx->rate_shaped);

        buf[i] = insert_wet16(buf[i], held, ctx->c2);
    }
}

const insert_effect_type_t insert_typeCrush = {
    "crs", { "bit", "srt", "wet" }, { "Bits", "SampleRt", "Wet" },
    0u, 0u, INSERT_PLACE_NONE, INSERT_FLAG_INPUT_GAIN,
    crush_init, NULL, crush_process, NULL
};
