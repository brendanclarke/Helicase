/*
 * FxBuffer positions, leases and passes (S084 VX3 VX3-11.1).
 *
 * What:       drives the real allocator implementation through its public
 *             position, lease and pass APIs. Why: Gate G1 needs an executable
 *             witness for adjacency, the sixteen-position floor and one
 *             coalesced share callback. Inputs: a host-owned arena aliased to
 *             the firmware linker symbols. Outputs: OK / failure count.
 * Accessors:  make -C tools/dsp_test test_fxbuf.
 * Affiliates: Core/DSP/Effects/FxBuffer.c/.h and VoiceBuffer.c.
 */
#include <stdint.h>
#include <stdio.h>

#include "FxBuffer.h"

#define HOST_FXBUF_BYTES 126592u

/* The firmware source reads linker symbols; the host boundary supplies the
 * same contiguous span without relying on platform-specific assembler names. */
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

/* Include the firmware implementation so the test cannot drift to a model. */
#include "../../Core/DSP/Effects/FxBuffer.c"

int main(void)
{
    fx_share_t share;
    uint8_t pair0[2] = { 0u, 0u };
    uint8_t pair1[2] = { 0u, 0u };
    int16_t *base;
    int16_t *next;

    fxbuf_init();
    fxbuf_setShareChangedCallback(onShare);
    check(fxbuf_arenaBytes() == HOST_FXBUF_BYTES, "arena size");
    check(fxbuf_effectFloorBytes() == HOST_FXBUF_BYTES -
          FXBUF_POSITION_COUNT * FXBUF_VOICE_UNIT_BYTES, "sixteen-position floor");

    fxbuf_beginPass();
    check(fxbuf_unitAcquire(0u, 2u, pair0), "first adjacent pair");
    check(fxbuf_unitAcquire(1u, 2u, pair1), "second adjacent pair");
    check(notifications == 0u, "pass suppresses intermediate callback");
    fxbuf_endPass();
    check(notifications == 1u, "pass emits one callback");

    base = fxbuf_unitPtr(pair0[0]);
    next = fxbuf_unitPtr(pair0[1]);
    check(base && next && next == base + FXBUF_VOICE_UNIT_SAMPLES,
          "pair pointers are contiguous in ring order");

    check(fxbuf_unitToLease(pair0, 2u, 0u), "retag pair as lease");
    check(fxbuf_leaseToSlot(pair0, 2u, 0u), "re-adopt lease pair");
    check(fxbuf_unitsInUse() == 4u, "retagging preserves unit count");

    fxbuf_beginPass();
    fxbuf_unitRelease(pair0, 2u);
    fxbuf_unitRelease(pair1, 2u);
    fxbuf_endPass();
    check(notifications == 2u, "release pass emits one callback");
    fxbuf_effectShare(&share);
    check(share.bytes == HOST_FXBUF_BYTES, "released share returns to arena");
    check(fxbuf_unitsInUse() == 0u, "all positions released");
    check(fxbuf_unitPtr(0u) == NULL, "free position has no audio pointer");

    check(fxbuf_unitAcquire(0u, 2u, pair0), "final pair for release check");
    fxbuf_unitRelease(pair0, 2u);
    check(fxbuf_unitsInUse() == 0u, "final pair released");

    if (failures == 0u)
        printf("fxbuf OK (0 failures)\n");
    return failures != 0u;
}
