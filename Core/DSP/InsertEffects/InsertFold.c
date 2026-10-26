/*
 * Fold Mix insert (S084 VX3 VX3-6.2).
 * What: folds a depth-scaled input around a symmetry offset and applies the
 * shared sample-and-hold rate. Why: constant-cost alternative to D+S.
 * Inputs: c0 depth, c2 symmetry/wet, rate_shaped. Output: block in place.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "InsertTypeCommon.h"
#include "DriveShaper.h"

typedef struct { float counter; int16_t held; uint16_t pad; } fold_state_t;
_Static_assert(sizeof(fold_state_t) <= INSERT_STATE_BYTES,
               "Fold state exceeds insert budget");

static void fold_init(void *state, const insert_block_ctx_t *ctx)
{
    fold_state_t *st = (fold_state_t *)state;

    (void)ctx; st->counter = 0.0f; st->held = 0; st->pad = 0u;
}

static void fold_process(void *state, int16_t *buf, uint8_t size,
                         const insert_block_ctx_t *ctx)
{
    fold_state_t *st = (fold_state_t *)state;
    const float depth = 1.0f + 7.0f * ctx->c0;
    const float offset = 0.6f * (ctx->c2 - 0.5f);
    uint8_t i;

    for (i = 0u; i < size; i++) {
        const float x = (float)buf[i] * (1.0f / 32767.0f) * depth;
        const float y = driveShaper_triFold(x + offset, 1.0f) -
                        driveShaper_triFold(offset, 1.0f);
        const int16_t folded = insert_to16(y * 32767.0f);

        buf[i] = insert_decimate(&st->counter, &st->held, folded,
                                 ctx->rate_shaped);
    }
}

const insert_effect_type_t insert_typeFold = {
    "fld", { "dpt", "srt", "sym" }, { "Depth", "SampleRt", "Symmetry" },
    0u, 0u, INSERT_PLACE_NONE, INSERT_FLAG_INPUT_GAIN,
    fold_init, NULL, fold_process, NULL
};
