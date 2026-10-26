/*
 * Stutter transient buffer `Bzz` (S084 VX3 VX3-8.5; remediation R12).
 *
 * What: on a trigger, records the next N samples of the POST signal into
 * ring[0, N), N = 5 ms + 45 ms * c1 (`tim`); then repeats that segment,
 * multiplying its gain by r = 10^(-N / FS) after each repeat (60 dB in 3 s),
 * and stops below -60 dB. The ring is written only while recording, so the
 * repeats cannot be overwritten. Output = input + c0 (`vol`) * repeat.
 * Why: the first version read absolute ring indices that were not the
 * recorded segment (it looped stale audio), used a per-sample `%` (an integer
 * division, against the G4 rule) and called expf() inside the loop.
 * Inputs: trigger, ring, c0, c1. Outputs: block in place; the tail plays the
 * remaining repeats. Cost: no division and no library call in the loop.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "DelayLine.h"
#include "InsertTypeCommon.h"
#include <string.h>

#define STUTTER_IDLE    0u
#define STUTTER_RECORD  1u
#define STUTTER_REPEAT  2u
#define STUTTER_FLOOR   0.001f

typedef struct {
    uint32_t segment;
    uint32_t position;
    float gain;
    float decay;
    uint8_t mode;
    uint8_t pad[3];
} stutter_state_t;
_Static_assert(sizeof(stutter_state_t) <= INSERT_STATE_BYTES,
               "Stutter state exceeds insert budget");

static void stutter_init(void *state, const insert_block_ctx_t *ctx)
{
    (void)ctx; memset(state, 0, sizeof(stutter_state_t));
}

static void stutter_run(stutter_state_t *st, int16_t *buf, uint8_t size,
                        const insert_block_ctx_t *ctx, uint8_t tail)
{
    uint8_t i;

    if (!ctx->ring || ctx->ring_len < 64u)
        return;
    if (ctx->trigger && !tail) {
        uint32_t n = (uint32_t)((0.005f + ctx->c1 * 0.045f) * REAL_FS);

        if (n > ctx->ring_len)
            n = ctx->ring_len;
        st->segment = n;
        st->position = 0u;
        st->gain = 1.0f;
        st->decay = delay_feedbackCeiling((float)n / REAL_FS);
        st->mode = STUTTER_RECORD;
    }
    if (tail && st->mode == STUTTER_RECORD)
        st->mode = STUTTER_IDLE;      /* nothing more will be recorded */
    /* Remediation §18 (cost): idle blocks leave the signal untouched. */
    if (st->mode == STUTTER_IDLE) {
        if (tail)
            memset(buf, 0, (size_t)size * sizeof(int16_t));
        return;
    }
    for (i = 0u; i < size; i++) {
        const int16_t input = tail ? 0 : buf[i];
        float wet = 0.0f;

        if (st->mode == STUTTER_RECORD) {
            ctx->ring[st->position] = input;
            if (++st->position >= st->segment) {
                st->position = 0u;
                st->mode = STUTTER_REPEAT;
            }
        } else if (st->mode == STUTTER_REPEAT) {
            wet = st->gain * (float)ctx->ring[st->position];
            if (++st->position >= st->segment) {
                st->position = 0u;
                st->gain *= st->decay;
                if (st->gain < STUTTER_FLOOR)
                    st->mode = STUTTER_IDLE;
            }
        }
        buf[i] = (int16_t)__SSAT((int32_t)input +
                                 (int32_t)(ctx->c0 * wet), 16);
    }
}

static void stutter_process(void *state, int16_t *buf, uint8_t size,
                            const insert_block_ctx_t *ctx)
{ stutter_run((stutter_state_t *)state, buf, size, ctx, 0u); }
static void stutter_tail(void *state, int16_t *buf, uint8_t size,
                         const insert_block_ctx_t *ctx)
{ stutter_run((stutter_state_t *)state, buf, size, ctx, 1u); }

const insert_effect_type_t insert_typeStutter = {
    "bzz", { "wav", "vol", "tim" }, { "Wave", "Volume", "Time" },
    1u, 10u, INSERT_PLACE_POST, 0u,
    stutter_init, NULL, stutter_process, stutter_tail
};
