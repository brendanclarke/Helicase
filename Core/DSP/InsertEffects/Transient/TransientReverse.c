/*
 * Reverse transient buffer `Rvs` (S084 VX3 VX3-8.6; remediation R13).
 *
 * What: on a trigger, records the next N samples of the POST signal into
 * ring[0, N), N = 10 ms + 40 ms * c1 (`tim`), then plays them backwards once
 * with a 2 ms linear fade at each end. Output = input + c0 (`vol`) * reverse.
 * Why: the first version started reading at absolute ring[len - 1] at the
 * trigger, a random 50 ms of earlier audio rather than the hit, and had no
 * edge fades (clicks). VX3-8.6 specified record-then-reverse with fades.
 * Inputs: trigger, ring, c0, c1. Outputs: block in place; the tail finishes
 * the reverse read. Cost: no division and no library call in the loop.
 */
#include "InsertEffect.h"
#include "InsertEffectsManager.h"
#include "DelayLine.h"
#include "InsertTypeCommon.h"
#include <string.h>

#define REVERSE_IDLE    0u
#define REVERSE_RECORD  1u
#define REVERSE_PLAY    2u
#define REVERSE_FADE    88u          /* 2 ms at 44,108 Hz */

typedef struct {
    uint32_t length;
    uint32_t position;
    uint32_t played;
    uint8_t mode;
    uint8_t pad[3];
} reverse_state_t;
_Static_assert(sizeof(reverse_state_t) <= INSERT_STATE_BYTES,
               "Reverse state exceeds insert budget");

static void reverse_init(void *state, const insert_block_ctx_t *ctx)
{ (void)ctx; memset(state, 0, sizeof(reverse_state_t)); }

static void reverse_run(reverse_state_t *st, int16_t *buf, uint8_t size,
                        const insert_block_ctx_t *ctx, uint8_t tail)
{
    uint8_t i;

    if (!ctx->ring || ctx->ring_len < 2u * REVERSE_FADE)
        return;
    if (ctx->trigger && !tail) {
        uint32_t n = (uint32_t)((0.010f + ctx->c1 * 0.040f) * REAL_FS);

        if (n > ctx->ring_len)
            n = ctx->ring_len;
        st->length = n;
        st->position = 0u;
        st->played = 0u;
        st->mode = REVERSE_RECORD;
    }
    if (tail && st->mode == REVERSE_RECORD)
        st->mode = REVERSE_IDLE;
    for (i = 0u; i < size; i++) {
        const int16_t input = tail ? 0 : buf[i];
        float wet = 0.0f;

        if (st->mode == REVERSE_RECORD) {
            ctx->ring[st->position] = input;
            if (++st->position >= st->length) {
                st->position = st->length - 1u;
                st->played = 0u;
                st->mode = REVERSE_PLAY;
            }
        } else if (st->mode == REVERSE_PLAY) {
            const uint32_t left = st->length - 1u - st->played;
            const uint32_t edge = (st->played < left) ? st->played : left;
            const float fade = (edge >= REVERSE_FADE) ? 1.0f :
                (float)edge * (1.0f / (float)REVERSE_FADE);

            wet = fade * (float)ctx->ring[st->position];
            st->played++;
            if (st->position == 0u)
                st->mode = REVERSE_IDLE;
            else
                st->position--;
        }
        buf[i] = tail ? insert_to16(wet) :
            insert_to16((float)input + ctx->c0 * wet);
    }
}

static void reverse_process(void *state, int16_t *buf, uint8_t size,
                            const insert_block_ctx_t *ctx)
{ reverse_run((reverse_state_t *)state, buf, size, ctx, 0u); }
static void reverse_tail(void *state, int16_t *buf, uint8_t size,
                         const insert_block_ctx_t *ctx)
{ reverse_run((reverse_state_t *)state, buf, size, ctx, 1u); }

const insert_effect_type_t insert_typeReverse = {
    "rvs", { "wav", "vol", "tim" }, { "Wave", "Volume", "Time" },
    1u, 11u, INSERT_PLACE_POST, INSERT_FLAG_DISABLED,
    reverse_init, NULL, reverse_process, reverse_tail
};
