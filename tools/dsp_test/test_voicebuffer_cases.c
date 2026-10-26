/*
 * VoiceBuffer allocator cases (S084 VX3 VX3-11.5).
 *
 * What:       drives the real VoiceBuffer/FxBuffer code through same-use
 *             growth, lease tailing, two-runner Hold overflow, silent-tail
 *             expiry, and one regression case per remediation fix (R1-R8,
 *             S084_P3_IMPLEMENTATION.md §18). Why: these are the ownership cases
 *             that cannot be proven by descriptor compilation alone. Inputs:
 *             deterministic stub type descriptors and a host arena. Outputs:
 *             OK / failure count.
 * Accessors:  make -C tools/dsp_test test_voicebuffer_cases.
 * Affiliates: VoiceBuffer.c/.h, FxBuffer.c/.h and the P3 C1–C16 matrix.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "VoiceBuffer.h"
#include "InsertEffectsManager.h"

#define HOST_FXBUF_BYTES 126592u
#define FXBUF_HOST_ARENA_BYTES HOST_FXBUF_BYTES
uint8_t fxbuf_host_arena[HOST_FXBUF_BYTES];

static unsigned failures;
static unsigned notifications;

static void check(int condition, const char *what)
{
    if (!condition) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void onShare(const fx_share_t *share)
{
    if (share)
        notifications++;
}

static void fake_init(void *state, const insert_block_ctx_t *ctx)
{
    (void)ctx;
    memset(state, 0x5a, INSERT_STATE_BYTES);
}

static void fake_process(void *state, int16_t *buf, uint8_t size,
                         const insert_block_ctx_t *ctx)
{
    (void)state;
    (void)ctx;
    while (size-- != 0u)
        *buf++ = 0;
}

static void fake_tail(void *state, int16_t *buf, uint8_t size,
                      const insert_block_ctx_t *ctx)
{
    (void)state;
    (void)ctx;
    while (size-- != 0u)
        *buf++ = 0;
}

static const insert_effect_type_t fake_mix_one = {
    "a1", { "a", "b", "c" }, { "A", "B", "C" },
    1u, 1u, INSERT_PLACE_NONE, 0u,
    fake_init, NULL, fake_process, fake_tail
};
static const insert_effect_type_t fake_mix_two = {
    "a2", { "a", "b", "c" }, { "A", "B", "C" },
    2u, 1u, INSERT_PLACE_NONE, 0u,
    fake_init, NULL, fake_process, fake_tail
};
static const insert_effect_type_t fake_mix_other = {
    "b1", { "a", "b", "c" }, { "A", "B", "C" },
    1u, 2u, INSERT_PLACE_NONE, 0u,
    fake_init, NULL, fake_process, fake_tail
};
static const insert_effect_type_t fake_mix_third = {
    "c1", { "a", "b", "c" }, { "A", "B", "C" },
    1u, 3u, INSERT_PLACE_NONE, 0u,
    fake_init, NULL, fake_process, fake_tail
};
static const insert_effect_type_t fake_mix_off = {
    "off", { "-", "-", "-" }, { "-", "-", "-" },
    0u, 0u, INSERT_PLACE_NONE, 0u,
    fake_init, NULL, fake_process, NULL
};

/* VoiceBuffer resolves pending ids through the manager boundary. */
const insert_effect_type_t *insertFx_mixDescriptor(uint8_t id)
{
    switch (id) {
    case 0u: return &fake_mix_one;
    case 1u: return &fake_mix_two;
    case 2u: return &fake_mix_other;
    case 3u: return &fake_mix_one;
    case 4u: return &fake_mix_off;
    case 5u: return &fake_mix_third;
    default: return NULL;
    }
}

const insert_effect_type_t *insertFx_transientDescriptor(uint8_t id)
{
    (void)id;
    return &fake_mix_one;
}

#include "../../Core/DSP/Effects/FxBuffer.c"
#include "../../Core/DSP/InsertEffects/VoiceBuffer.c"

/* Render one block of a stage so its record becomes PRIMED (it holds audio). */
static void prime(uint8_t slot)
{
    int16_t block[32];
    insert_block_ctx_t ctx;

    memset(block, 0, sizeof(block));
    memset(&ctx, 0, sizeof(ctx));
    voiceBuffer_render(slot, VOICEBUF_STAGE_MIX, block, 32u, &ctx);
}

static voice_buffer_record_t *active(uint8_t slot)
{
    return &voiceBuffer_records[vb_activeIndex(slot, VOICEBUF_STAGE_MIX)];
}

static voice_buffer_record_t *runner(uint8_t i)
{
    return &voiceBuffer_records[VOICEBUF_ACTIVE + i];
}

static unsigned runnersBusy(void)
{
    return (unsigned)((runner(0)->flags != 0u) + (runner(1)->flags != 0u));
}

/* Play all tails until they expire and are released. */
static void drainTails(void)
{
    int16_t block[32];
    unsigned b;
    uint8_t slot;

    memset(block, 0, sizeof(block));
    for (b = 0u; b < 200u; b++) {
        for (slot = 0u; slot < FXBUF_VOICE_SLOT_COUNT; slot++)
            voiceBuffer_addTails(slot, block, 32u);
        voiceBuffer_service();
    }
}

static void reset(void)
{
    fxbuf_init();
    fxbuf_setShareChangedCallback(onShare);
    voiceBuffer_init();
}

int main(void)
{
    uint8_t result;

    /* Lifecycle basics: fresh claim, change -> tail, Hold, expiry. */
    reset();
    result = voiceBuffer_select(0u, VOICEBUF_STAGE_MIX, &fake_mix_one, 0u);
    check(result == VOICEBUF_SELECT_CHANGED, "first buffer selection");
    prime(0u);
    result = voiceBuffer_select(0u, VOICEBUF_STAGE_MIX, &fake_mix_other, 2u);
    check(result == VOICEBUF_SELECT_CHANGED, "changed use becomes a tail");
    check(runnersBusy() == 1u, "primed ring tails on a runner");
    result = voiceBuffer_select(1u, VOICEBUF_STAGE_MIX, &fake_mix_one, 3u);
    prime(1u);
    result = voiceBuffer_select(1u, VOICEBUF_STAGE_MIX, &fake_mix_other, 2u);
    check(result == VOICEBUF_SELECT_CHANGED && runnersBusy() == 2u,
          "second runner is occupied");
    prime(1u);
    result = voiceBuffer_select(1u, VOICEBUF_STAGE_MIX, &fake_mix_third, 5u);
    check(result == VOICEBUF_SELECT_HELD, "third tail uses Hold overflow");
    check(notifications != 0u, "share callback observed allocator changes");
    drainTails();
    check(runnersBusy() == 0u, "silent tails expire and free their runners");
    check(voiceBuffer_fits(1u, VOICEBUF_STAGE_MIX, &fake_mix_third),
          "expired runner makes held selection fit");

    /* R7a: a ring that never rendered is released, not tailed. */
    reset();
    (void)voiceBuffer_select(0u, VOICEBUF_STAGE_MIX, &fake_mix_one, 0u);
    (void)voiceBuffer_select(0u, VOICEBUF_STAGE_MIX, &fake_mix_other, 2u);
    check(runnersBusy() == 0u, "R7a unprimed change spends no runner");
    check(fxbuf_unitsInUse() == 1u, "R7a unprimed ring released");

    /* R5: new units are cleared on the first render, not at claim time. */
    reset();
    memset(fxbuf_host_arena, 0x55, sizeof(fxbuf_host_arena));
    (void)voiceBuffer_select(0u, VOICEBUF_STAGE_MIX, &fake_mix_one, 0u);
    check(fxbuf_unitPtr(active(0u)->unit_pos[0])[100] == 0x5555,
          "R5 claim leaves the bytes for the Effect to compact");
    check((active(0u)->flags & VOICEBUF_FLAG_CLEAR0) != 0u,
          "R5 claim flags the unit for clearing");
    prime(0u);
    check(fxbuf_unitPtr(active(0u)->unit_pos[0])[100] == 0,
          "R5 first render clears the unit");

    /* R3/R8: growth keeps the ring base; shrink is a different use. */
    reset();
    (void)voiceBuffer_select(0u, VOICEBUF_STAGE_MIX, &fake_mix_one, 0u); /* pos 0 */
    (void)voiceBuffer_select(1u, VOICEBUF_STAGE_MIX, &fake_mix_one, 0u); /* pos 1 */
    (void)voiceBuffer_select(0u, VOICEBUF_STAGE_MIX, &fake_mix_off, 4u); /* unprimed: frees 0 */
    prime(1u);
    {
        const uint8_t base = active(1u)->unit_pos[0];

        result = voiceBuffer_select(1u, VOICEBUF_STAGE_MIX, &fake_mix_two, 1u);
        check(result == VOICEBUF_SELECT_SAME, "R3 growth continues the record");
        check(active(1u)->unit_pos[0] == base, "R3 growth keeps the ring base");
        check(fxbuf_unitPtr(active(1u)->unit_pos[0]) + FXBUF_VOICE_UNIT_SAMPLES ==
              fxbuf_unitPtr(active(1u)->unit_pos[1]),
              "R3 grown ring is contiguous upward");
        check((active(1u)->flags & VOICEBUF_FLAG_CLEAR1) != 0u,
              "R3 grown unit flagged for clearing");
    }
    prime(1u);
    result = voiceBuffer_select(1u, VOICEBUF_STAGE_MIX, &fake_mix_one, 0u);
    check(result == VOICEBUF_SELECT_CHANGED, "R8 shrink is a different use");
    check(runner(0)->units == 2u && active(1u)->units == 1u,
          "R8 long ring tails, short type starts fresh");

    /* Growth without a free higher neighbour falls back to a changed use. */
    reset();
    (void)voiceBuffer_select(0u, VOICEBUF_STAGE_MIX, &fake_mix_one, 0u); /* pos 0 */
    prime(0u);
    result = voiceBuffer_select(0u, VOICEBUF_STAGE_MIX, &fake_mix_two, 1u);
    check(result == VOICEBUF_SELECT_CHANGED && runnersBusy() == 1u,
          "R3 blocked growth tails the old ring and starts fresh");

    /* R1: re-adoption from a non-buffer record does not leak a runner. */
    reset();
    (void)voiceBuffer_select(2u, VOICEBUF_STAGE_MIX, &fake_mix_one, 0u);
    prime(2u);
    (void)voiceBuffer_select(2u, VOICEBUF_STAGE_MIX, &fake_mix_off, 4u);
    check(runnersBusy() == 1u, "R1 buffer -> non-buffer leaves a tail");
    result = voiceBuffer_select(2u, VOICEBUF_STAGE_MIX, &fake_mix_one, 0u);
    check(result == VOICEBUF_SELECT_CHANGED, "R1 returning type re-adopts");
    check(runnersBusy() == 0u, "R1 no runner is left holding a non-buffer");
    check(active(2u)->units == 1u, "R1 re-adopted ring is active again");

    /* R2: another slot's tail is never adopted. */
    reset();
    (void)voiceBuffer_select(3u, VOICEBUF_STAGE_MIX, &fake_mix_one, 0u);
    prime(3u);
    (void)voiceBuffer_select(3u, VOICEBUF_STAGE_MIX, &fake_mix_off, 4u);
    (void)voiceBuffer_select(4u, VOICEBUF_STAGE_MIX, &fake_mix_off, 4u);
    (void)voiceBuffer_select(4u, VOICEBUF_STAGE_MIX, &fake_mix_one, 0u);
    check(runner(0)->owner == 3u && (runner(0)->flags & VOICEBUF_FLAG_TAIL),
          "R2 slot 3's tail stays with slot 3");

    /* R4: a held change does not survive a later same-use choice. */
    reset();
    (void)voiceBuffer_select(0u, VOICEBUF_STAGE_MIX, &fake_mix_one, 0u);
    prime(0u);
    (void)voiceBuffer_select(0u, VOICEBUF_STAGE_MIX, &fake_mix_other, 2u);
    prime(0u);
    (void)voiceBuffer_select(0u, VOICEBUF_STAGE_MIX, &fake_mix_third, 5u);
    prime(0u);
    result = voiceBuffer_select(0u, VOICEBUF_STAGE_MIX, &fake_mix_two, 1u);
    check(result == VOICEBUF_SELECT_HELD, "R4 both runners busy: Hold");
    result = voiceBuffer_select(0u, VOICEBUF_STAGE_MIX, &fake_mix_third, 5u);
    check(result == VOICEBUF_SELECT_SAME, "R4 back to the running type");
    check(active(0u)->pending_type == VOICEBUF_PENDING_NONE,
          "R4 no pending selection is left on the record");
    drainTails();
    check(active(0u)->type_id == 5u, "R4 expiry does not apply a stale type");

    /* R6: the fit test models the lease (lease cap reached -> no fit). */
    reset();
    {
        uint8_t pos[2];
        uint8_t i;

        for (i = 0u; i < 4u; i++) {
            (void)fxbuf_unitAcquire(i, 1u, pos);
            (void)fxbuf_unitToLease(pos, 1u, (uint8_t)(i / 2u));
        }
        (void)fxbuf_unitAcquire(5u, 1u, pos);
        check(!fxbuf_unitFitsReplacing(5u, 1u, 1u),
              "R6 no fit when the old ring cannot be leased");
    }

    if (failures == 0u)
        printf("voicebuffer_cases OK (0 failures)\n");
    return failures != 0u;
}
