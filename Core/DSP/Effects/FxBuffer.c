/*
 * Core/DSP/Effects/FxBuffer.c
 *
 *  Created on: 27.09.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR02 Open-Source, hardware driver code, or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions that are modified from the original source must include the complete
 *         source code, including the source code for all components used by a binary built
 *         from the modified sources. However, as a special exception, the source code distributed
 *         need not include anything that is normally distributed (in either source or binary form)
 *         with the major components (compiler, kernel, and so on) of the operating system on which
 *         the executable runs, unless that component itself accompanies the executable.
 *
 *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
 *         following disclaimer in the documentation and/or other materials provided with the distribution.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#include "FxBuffer.h"
#if defined(FXBUF_HOST_TEST)
/*
 * Host allocator-test boundary (S084 VX3 VX3-11.1).
 *
 * What:       supplies only the compile-time slot-count witness when the
 *             allocator is included by test_fxbuf.c. Why: the production
 *             InstrumentManager header intentionally pulls the complete DSP
 *             runtime contract, which is not needed to exercise FxBuffer and
 *             conflicts with the host DSP prelude. Production builds never
 *             define this symbol and keep the normal include below.
 */
#define INSTRUMENT_SLOT_COUNT FXBUF_VOICE_SLOT_COUNT
extern uint8_t fxbuf_host_arena[];
#ifndef FXBUF_HOST_ARENA_BYTES
#define FXBUF_HOST_ARENA_BYTES 126592u
#endif
#else
#include "InstrumentManager.h"
#endif
#include <stddef.h>
#include <string.h>

/* Linker-defined .dtcm_fxbuf bounds; their addresses are never dereferenced
 * as objects. The host test supplies an equivalent byte array instead. */
#if !defined(FXBUF_HOST_TEST)
extern uint8_t _sfxbuf[];
extern uint8_t _efxbuf[];
#endif

_Static_assert(FXBUF_VOICE_SLOT_COUNT == INSTRUMENT_SLOT_COUNT,
               "FxBuffer slot count must match InstrumentManager");
_Static_assert((FXBUF_VOICE_UNIT_BYTES % FXBUF_ALIGN_BYTES) == 0u,
               "voice unit must be a whole number of cache lines");
_Static_assert(FXBUF_VOICE_UNIT_COUNT <= 16u,
               "unit_written_mask is 16 bits");
_Static_assert(FXBUF_VOICE_UNIT_COUNT ==
                   FXBUF_VOICE_SLOT_COUNT * FXBUF_VOICE_UNITS_PER_SLOT,
               "12 units = 6 slots x 2 (user decision A29)");
_Static_assert(sizeof(fxbuf_handoff_t) == 180u,
               "handoff size is recorded in STORAGE_SRAM_MANIFEST.md; update both");

/*
 * FxBuffer resident bookkeeping (SRAM1 .bss, 28 B including padding).
 *
 * What: arena base/size captured once from the linker, the owner slot of each
 * unit, the claimed-unit count, and the one share-change callback. Why SRAM1
 * not DTCM: this is control-rate state, while every remaining DTCM byte belongs
 * to the audio arena itself. Only this module writes it.
 */
typedef struct {
    uint8_t *base;
    uint32_t bytes;
    fxbuf_share_changed_fn on_share_changed;
    uint8_t unit_owner[FXBUF_POSITION_COUNT];
    uint8_t units_in_use;
    uint8_t lease_in_use;
    uint8_t pass_depth;
    uint32_t pass_start_bytes;
} fxbuf_state_t;

static fxbuf_state_t fxbuf_state;

/* Handoff record (SRAM1 .bss, 180 B); audio contents remain in the arena. */
static fxbuf_handoff_t fxbuf_handoffRecord;

#if DEV_MODE_DIAGNOSTIC
/* Diagnostic-only boot self-test result (1 B). */
static uint8_t fxbuf_selfTestResult;
/* Self-test callback witness; it has no production storage or call path. */
static uint8_t fxbuf_testNotifyCount;

/* Count coalesced callbacks during the allocator self-test. */
static void fxbuf_testNotify(const fx_share_t *share)
{
    (void)share;
    fxbuf_testNotifyCount++;
}
#endif

/* Unit u's arena-relative offset. Unit zero is the topmost unit. */
static uint32_t fxbuf_unitOffset(uint8_t unit)
{
    return fxbuf_state.bytes -
           ((uint32_t)unit + 1u) * FXBUF_VOICE_UNIT_BYTES;
}

/* Return the contiguous Effect share below the highest claimed unit. */
static uint32_t fxbuf_shareBytes(void)
{
    int8_t u;

    for (u = (int8_t)(FXBUF_POSITION_COUNT - 1u); u >= 0; u--) {
        if (fxbuf_state.unit_owner[(uint8_t)u] != FXBUF_UNIT_FREE)
            return fxbuf_unitOffset((uint8_t)u);
    }
    return fxbuf_state.bytes;
}

/* Notify the active Effect only when the share length really changed. */
static void fxbuf_notifyIfShareChanged(uint32_t before_bytes)
{
    fx_share_t share;

    /*
     * Share notification gate (S084 VX3 VX3-1.3).
     *
     * What:       suppresses intermediate callbacks while an apply pass is
     *             changing several positions. Why: CrumpBit must compact at
     *             most once per pass, at the final share. Inputs: the share
     *             length before one allocator operation. Outputs: no callback
     *             inside a pass; the outer endPass owns the notification.
     * Accessors: fxbuf_beginPass()/fxbuf_endPass(). Affiliates: CrumpBit.
     */
    if (fxbuf_state.pass_depth != 0u)
        return;
    if (!fxbuf_state.on_share_changed)
        return;
    fxbuf_effectShare(&share);
    if (share.bytes != before_bytes)
        fxbuf_state.on_share_changed(&share);
}

/* Reset every handoff field to the safe boot state: no valid audio. */
static void fxbuf_handoffResetAll(void)
{
    uint8_t i;

    memset(&fxbuf_handoffRecord, 0, sizeof(fxbuf_handoffRecord));
    for (i = 0u; i < FXBUF_HANDOFF_POINTER_COUNT; i++) {
        fxbuf_handoffRecord.read_offset[i] = FXBUF_OFFSET_NONE;
        fxbuf_handoffRecord.write_offset[i] = FXBUF_OFFSET_NONE;
    }
    for (i = 0u; i < FXBUF_VOICE_UNIT_COUNT; i++)
        fxbuf_handoffRecord.unit_owner_slot[i] = FXBUF_UNIT_FREE;
    fxbuf_handoffRecord.effect_type = FXBUF_EFFECT_TYPE_NONE;
    fxbuf_handoffRecord.effect_share_bytes = fxbuf_state.bytes;
}

/* Reset only the owner table; the registered callback is deliberately kept. */
static void fxbuf_clearOwners(void)
{
    memset(fxbuf_state.unit_owner, FXBUF_UNIT_FREE,
           sizeof(fxbuf_state.unit_owner));
    fxbuf_state.units_in_use = 0u;
    fxbuf_state.lease_in_use = 0u;
}

/* Count regular positions; lease positions are identified by their tag. */
static uint8_t fxbuf_regularInUse(void)
{
    uint8_t n = 0u;
    uint8_t u;

    for (u = 0u; u < FXBUF_POSITION_COUNT; u++)
        if (fxbuf_state.unit_owner[u] < FXBUF_VOICE_SLOT_COUNT)
            n++;
    return n;
}

/* Return the regular count owned by one slot. */
static uint8_t fxbuf_positionCountForSlot(uint8_t slot)
{
    uint8_t n = 0u;
    uint8_t u;

    if (slot >= FXBUF_VOICE_SLOT_COUNT)
        return 0u;
    for (u = 0u; u < FXBUF_POSITION_COUNT; u++)
        if (fxbuf_state.unit_owner[u] == slot)
            n++;
    return n;
}

/* Validate one physical position without changing state. */
static uint8_t fxbuf_positionValid(uint8_t unit)
{
    return (uint8_t)(unit < FXBUF_POSITION_COUNT);
}

#if DEV_MODE_DIAGNOSTIC
/*
 * Allocation-logic self-test. It exercises bookkeeping only and never reads
 * or writes arena bytes. The result is the first failing check number:
 * 1 geometry, 2 empty share, 3 two-unit acquire, 4 cap refusal, 5 full arena,
 * 6 refill, 7 non-compacting release, 8 full release, 9 unit pointer bounds.
 */
static uint8_t fxbuf_selfTest(void)
{
    const uint32_t unit = FXBUF_VOICE_UNIT_BYTES;
    fxbuf_share_changed_fn saved = fxbuf_state.on_share_changed;
    fx_share_t share;
    uint8_t result = 0u;
    uint8_t s;

    fxbuf_state.on_share_changed = NULL;
    fxbuf_clearOwners();

    if (fxbuf_state.bytes < FXBUF_MIN_ARENA_BYTES ||
        (((uintptr_t)fxbuf_state.base) & (FXBUF_ALIGN_BYTES - 1u)) != 0u ||
        (fxbuf_state.bytes & (FXBUF_ALIGN_BYTES - 1u)) != 0u) {
        result = 1u; goto done;
    }
    fxbuf_effectShare(&share);
    if (share.bytes != fxbuf_state.bytes || share.base != fxbuf_state.base) {
        result = 2u; goto done;
    }
    if (!fxbuf_voiceAcquire(0u, 2u) ||
        fxbuf_shareBytes() != fxbuf_state.bytes - 2u * unit) {
        result = 3u; goto done;
    }
    if (fxbuf_voiceAcquire(0u, 1u)) {
        result = 4u; goto done;
    }
    for (s = 1u; s < FXBUF_VOICE_SLOT_COUNT; s++) {
        if (!fxbuf_voiceAcquire(s, 2u)) { result = 5u; goto done; }
    }
    if (fxbuf_unitsInUse() != FXBUF_VOICE_UNIT_COUNT ||
        fxbuf_shareBytes() != fxbuf_state.bytes -
                              FXBUF_VOICE_UNIT_COUNT * unit) {
        result = 5u; goto done;
    }
    fxbuf_voiceRelease(1u);
    if (!fxbuf_voiceAcquire(1u, 2u) || fxbuf_voiceAcquire(1u, 1u)) {
        result = 6u; goto done;
    }
    fxbuf_voiceRelease(0u);
    if (fxbuf_shareBytes() != fxbuf_state.bytes -
                              FXBUF_VOICE_UNIT_COUNT * unit) {
        result = 7u; goto done;
    }
    for (s = 0u; s < FXBUF_VOICE_SLOT_COUNT; s++)
        fxbuf_voiceRelease(s);
    if (fxbuf_unitsInUse() != 0u || fxbuf_shareBytes() != fxbuf_state.bytes) {
        result = 8u; goto done;
    }
    if (!fxbuf_voiceAcquire(5u, 2u)) { result = 9u; goto done; }
    for (s = 0u; s < 2u; s++) {
        uint8_t *p = (uint8_t *)fxbuf_voiceUnit(5u, s);
        if (!p || p < fxbuf_state.base ||
            p + unit > fxbuf_state.base + fxbuf_state.bytes ||
            (((uintptr_t)p) & (FXBUF_ALIGN_BYTES - 1u)) != 0u) {
            result = 9u; goto done;
        }
    }

    /* S084 VX3 VX3-1.5: two positions must map to one contiguous ring. */
    fxbuf_clearOwners();
    {
        uint8_t pair[2];
        int16_t *high;
        int16_t *low;

        if (!fxbuf_unitAcquire(0u, 2u, pair)) {
            result = 10u; goto done;
        }
        high = fxbuf_unitPtr(pair[1]);
        low = fxbuf_unitPtr(pair[0]);
        if (!high || !low || low + FXBUF_VOICE_UNIT_SAMPLES != high) {
            result = 10u; goto done;
        }
        fxbuf_unitRelease(pair, 2u);
    }

    /* S084 VX3 VX3-1.5: four leases are allowed, a fifth is refused. */
    fxbuf_clearOwners();
    {
        uint8_t one[2];
        uint8_t i;

        for (i = 0u; i < FXBUF_LEASE_UNIT_MAX; i++) {
            if (!fxbuf_unitAcquire(i, 1u, one) ||
                !fxbuf_unitToLease(one, 1u, (uint8_t)(i / 2u))) {
                result = 11u; goto done;
            }
        }
        if (!fxbuf_unitAcquire(0u, 1u, one) ||
            fxbuf_unitToLease(one, 1u, 0u)) {
            result = 11u; goto done;
        }
        for (i = 0u; i < FXBUF_POSITION_COUNT; i++) {
            uint8_t pos = i;
            fxbuf_unitRelease(&pos, 1u);
        }
    }

    /* S084 VX3 VX3-1.5: a pass emits no more than one share callback. */
    fxbuf_clearOwners();
    fxbuf_testNotifyCount = 0u;
    fxbuf_state.on_share_changed = fxbuf_testNotify;
    fxbuf_beginPass();
    {
        uint8_t position[2];

        if (!fxbuf_unitAcquire(0u, 1u, position)) {
            result = 12u; goto done;
        }
    }
    fxbuf_endPass();
    if (fxbuf_testNotifyCount > 1u) {
        result = 12u; goto done;
    }
    fxbuf_clearOwners();

    /* S084 VX3 VX3-1.5: the floor is the full sixteen-position bound. */
    if (fxbuf_effectFloorBytes() != fxbuf_state.bytes -
                                  FXBUF_POSITION_COUNT * unit ||
        fxbuf_effectFloorBytes() < 20608u) {
        result = 13u; goto done;
    }

done:
    fxbuf_clearOwners();
    fxbuf_state.pass_depth = 0u;
    fxbuf_state.pass_start_bytes = 0u;
    fxbuf_state.on_share_changed = saved;
    return result;
}
#endif

/*
 * Initialise arena bookkeeping at boot.
 *
 * Inputs: linker symbols `_sfxbuf`/`_efxbuf`. Output: all units free, Effect
 * share = the whole arena, and handoff = "nothing valid". Arena bytes are
 * NOT touched. Diagnostic builds run the self-test before applying
 * DEV_FXBUF_FORCE_VOICE_UNITS, so the knob cannot mask an allocation failure.
 * Must run pre-audio, before Instrument runtime or an Effect is constructed.
 */
void fxbuf_init(void)
{
#if defined(FXBUF_HOST_TEST)
    fxbuf_state.base = fxbuf_host_arena;
    fxbuf_state.bytes = FXBUF_HOST_ARENA_BYTES;
#else
    fxbuf_state.base = _sfxbuf;
    fxbuf_state.bytes = (uint32_t)(_efxbuf - _sfxbuf);
#endif
    fxbuf_state.on_share_changed = NULL;
    fxbuf_state.pass_depth = 0u;
    fxbuf_state.pass_start_bytes = 0u;
    fxbuf_clearOwners();
    /*
     * Reset the handoff record before any unit is claimed (S072 debt 1,
     * closed in S074).
     *
     * What: clears the handoff snapshot to "nothing valid" immediately after
     * the owner table. Why: fxbuf_voiceAcquire() stamps each claimed unit's
     * default store rate into the handoff record. With the reset after the
     * diagnostic forced-unit loop, those units were left with rate 0, and the
     * minimum-share test (DEV_FXBUF_FORCE_VOICE_UNITS 12) that the first
     * buffer-using Effect needs ran against a malformed record. Inputs: none.
     * Output: a zeroed record whose pointers are FXBUF_OFFSET_NONE. The
     * self-test and forced claims below now write valid unit rates, and
     * fxbuf_handoffBeginExit() at the end clears free units. Affiliates:
     * fxbuf_handoffResetAll(), fxbuf_voiceAcquire(), DEV_MODES.md.
     */
    fxbuf_handoffResetAll();

#if DEV_MODE_DIAGNOSTIC
    fxbuf_selfTestResult = fxbuf_selfTest();
    {
        uint8_t n;
        for (n = 0u; n < (uint8_t)DEV_FXBUF_FORCE_VOICE_UNITS; n++)
            (void)fxbuf_voiceAcquire((uint8_t)(n / FXBUF_VOICE_UNITS_PER_SLOT), 1u);
    }
#endif
    (void)fxbuf_handoffBeginExit();
}

/*
 * Return the link-time size of the shared DTCM arena in bytes.
 *
 * Output: `_efxbuf - _sfxbuf`. Clients use this for diagnostics and capacity
 * reporting; Effect types use fxbuf_effectShare() for their active share.
 */
uint32_t fxbuf_arenaBytes(void)
{
    return fxbuf_state.bytes;
}

/*
 * Return the elastic Effect floor (S084 VX3 VX3-1.4).
 *
 * What:       subtracts the complete sixteen-position voice/lease geometry
 *             from the link-time arena. Why: this is the minimum share that
 *             any registered Effect must support after all approved voice
 *             reservations. Inputs: linker-derived arena size. Outputs: the
 *             32-byte-aligned floor. Accessors: EffectsManager, diagnostics,
 *             and VoiceBuffer tests. Affiliates: FxBuffer position constants.
 */
uint32_t fxbuf_effectFloorBytes(void)
{
    const uint32_t used = FXBUF_POSITION_COUNT * FXBUF_VOICE_UNIT_BYTES;

    return (fxbuf_state.bytes > used) ? fxbuf_state.bytes - used : 0u;
}

/*
 * Return the current contiguous Effect share.
 *
 * Input: non-NULL `out`. Output: arena base, offset zero, and the contiguous
 * bytes below the highest claimed unit. No compaction occurs when a lower unit
 * is released while a higher unit remains claimed.
 */
void fxbuf_effectShare(fx_share_t *out)
{
    if (!out)
        return;
    out->base = fxbuf_state.base;
    out->offset = 0u;
    out->bytes = fxbuf_shareBytes();
}

/*
 * Claim voice units for one Instrument slot, all-or-nothing.
 *
 * Inputs: slot 0..5 and count 1..2. Output: 1 on success, 0 for invalid
 * arguments, the per-slot cap, or insufficient free units. Successful claims
 * set only the default handoff rate; offsets remain the handoff snapshot's
 * responsibility and unit contents remain owner-managed.
 */
uint8_t fxbuf_voiceAcquire(uint8_t slot, uint8_t count)
{
    uint32_t before;
    uint8_t owned;
    uint8_t u;

    if (slot >= FXBUF_VOICE_SLOT_COUNT || count == 0u ||
        count > FXBUF_VOICE_UNITS_PER_SLOT)
        return 0u;
    owned = fxbuf_voiceUnitCount(slot);
    if ((uint8_t)(owned + count) > FXBUF_VOICE_UNITS_PER_SLOT ||
        (uint8_t)(fxbuf_regularInUse() + count) > FXBUF_VOICE_UNIT_COUNT)
        return 0u;

    before = fxbuf_shareBytes();
    for (u = 0u; u < FXBUF_VOICE_UNIT_COUNT && count; u++) {
        if (fxbuf_state.unit_owner[u] == FXBUF_UNIT_FREE) {
            fxbuf_state.unit_owner[u] = slot;
            fxbuf_state.units_in_use++;
            fxbuf_handoffRecord.unit_rate_hz[u] = FXBUF_VOICE_RATE_HZ_DEFAULT;
            count--;
        }
    }
    fxbuf_notifyIfShareChanged(before);
    return 1u;
}

/*
 * Release every unit owned by an Instrument slot.
 *
 * Input: slot 0..5; other values are ignored. Units become free and any share
 * growth is reported synchronously. Contents and the previous handoff fields
 * are not cleared here; the next exit snapshot resets free-unit metadata.
 */
void fxbuf_voiceRelease(uint8_t slot)
{
    uint32_t before;
    uint8_t u;

    if (slot >= FXBUF_VOICE_SLOT_COUNT)
        return;
    before = fxbuf_shareBytes();
    for (u = 0u; u < FXBUF_VOICE_UNIT_COUNT; u++) {
        if (fxbuf_state.unit_owner[u] == slot) {
            fxbuf_state.unit_owner[u] = FXBUF_UNIT_FREE;
            fxbuf_state.units_in_use--;
        }
    }
    fxbuf_notifyIfShareChanged(before);
}

/* Return the number of units currently owned by a slot, or zero if invalid. */
uint8_t fxbuf_voiceUnitCount(uint8_t slot)
{
    uint8_t n = 0u;
    uint8_t u;

    if (slot >= FXBUF_VOICE_SLOT_COUNT)
        return 0u;
    for (u = 0u; u < FXBUF_VOICE_UNIT_COUNT; u++)
        if (fxbuf_state.unit_owner[u] == slot)
            n++;
    return n;
}

/* Return the total number of claimed voice units. */
uint8_t fxbuf_unitsInUse(void)
{
    return fxbuf_state.units_in_use;
}

/*
 * Borrow one owned voice unit as an aligned sample pointer.
 *
 * Inputs: slot 0..5 and an ordinal in ascending owned-unit order. Output: a
 * pointer to the unit's 16-bit mono samples, or NULL if the ordinal is absent.
 */
int16_t *fxbuf_voiceUnit(uint8_t slot, uint8_t ordinal)
{
    uint8_t u;

    if (slot >= FXBUF_VOICE_SLOT_COUNT)
        return NULL;
    for (u = 0u; u < FXBUF_VOICE_UNIT_COUNT; u++) {
        if (fxbuf_state.unit_owner[u] != slot)
            continue;
        if (ordinal == 0u)
            return (int16_t *)(void *)(fxbuf_state.base + fxbuf_unitOffset(u));
        ordinal--;
    }
    return NULL;
}

/*
 * Would a regular position claim fit (S084 VX3 VX3-1.4).
 *
 * What:       checks the slot cap, the twelve-position regular cap and the
 *             requested free position or adjacent pair without mutating the
 *             allocator. Why: menu hiding and VoiceBuffer's load downgrade
 *             need an exact answer before they start an apply. Inputs: slot
 *             0..5 and count 1..2. Outputs: nonzero when a claim is possible.
 * Accessors: fxbuf_unitAcquire(), VoiceBuffer.c. Affiliates: the 16-position
 *             owner map.
 */
uint8_t fxbuf_unitFits(uint8_t slot, uint8_t count)
{
    uint8_t owned;
    uint8_t u;

    if (slot >= FXBUF_VOICE_SLOT_COUNT || count == 0u ||
        count > FXBUF_VOICE_UNITS_PER_SLOT)
        return 0u;
    owned = fxbuf_positionCountForSlot(slot);
    if ((uint8_t)(owned + count) > FXBUF_VOICE_UNITS_PER_SLOT ||
        (uint8_t)(fxbuf_regularInUse() + count) > FXBUF_REGULAR_UNIT_MAX)
        return 0u;
    for (u = 0u; u < FXBUF_POSITION_COUNT; u++) {
        if (fxbuf_state.unit_owner[u] != FXBUF_UNIT_FREE)
            continue;
        if (count == 1u)
            return 1u;
        if (u + 1u < FXBUF_POSITION_COUNT &&
            fxbuf_state.unit_owner[u + 1u] == FXBUF_UNIT_FREE)
            return 1u;
    }
    return 0u;
}

/*
 * Would a regular claim fit after this slot's old units become a tail lease
 * (S084 VX3 VX3-1.2; remediation R6).
 *
 * What: models the change exactly as VoiceBuffer performs it. The `released`
 * positions owned by `slot` are retagged as a lease, so they leave the slot
 * and regular caps but stay physically occupied and count against the
 * four-unit lease cap; the new claim must then find a free position, or an
 * adjacent free pair, among the positions that are free right now.
 * Why: the first version treated the slot's own positions as free. A
 * fragmented two-unit request could then pass the test, lease the old ring,
 * and fail the claim, leaving the stage silent and skipping the C13
 * Ec2/Rv2 downgrade. Inputs: slot, requested count, released count.
 * Output: nonzero when the lease-then-claim sequence will succeed.
 * Callers: voiceBuffer_fits(), vb_applyChanged().
 */
uint8_t fxbuf_unitFitsReplacing(uint8_t slot, uint8_t count, uint8_t released)
{
    uint8_t regular = fxbuf_regularInUse();
    uint8_t owned = fxbuf_positionCountForSlot(slot);
    uint8_t u;

    if (slot >= FXBUF_VOICE_SLOT_COUNT || count == 0u ||
        count > FXBUF_VOICE_UNITS_PER_SLOT || released > owned ||
        (uint8_t)(owned - released + count) > FXBUF_VOICE_UNITS_PER_SLOT ||
        (uint8_t)(regular - released + count) > FXBUF_REGULAR_UNIT_MAX ||
        (uint8_t)(fxbuf_state.lease_in_use + released) > FXBUF_LEASE_UNIT_MAX)
        return 0u;
    for (u = 0u; u < FXBUF_POSITION_COUNT; u++) {
        if (fxbuf_state.unit_owner[u] != FXBUF_UNIT_FREE)
            continue;
        if (count == 1u)
            return 1u;
        if (u + 1u < FXBUF_POSITION_COUNT &&
            fxbuf_state.unit_owner[u + 1u] == FXBUF_UNIT_FREE)
            return 1u;
    }
    return 0u;
}

/*
 * Claim one or two regular positions (S084 VX3 VX3-1.4).
 *
 * What:       claims the first free position, or the first adjacent free pair
 *             for a two-unit ring. Pair outputs are lowest-address first so
 *             callers can use one stable ring base. Why: Ec2/Rv2 must never
 *             use split addressing and every reservation is all-or-nothing.
 * Inputs:     slot, count and units_out. Outputs: nonzero on success; the
 *             share callback is emitted immediately unless inside a pass.
 * Accessors: VoiceBuffer.c. Affiliates: fxbuf_unitFits()/fxbuf_unitPtr().
 */
uint8_t fxbuf_unitAcquire(uint8_t slot, uint8_t count, uint8_t units_out[2])
{
    uint32_t before;
    uint8_t first = FXBUF_UNIT_FREE;
    uint8_t u;

    if (!units_out || !fxbuf_unitFits(slot, count))
        return 0u;
    if (count == 1u) {
        for (u = 0u; u < FXBUF_POSITION_COUNT; u++) {
            if (fxbuf_state.unit_owner[u] == FXBUF_UNIT_FREE) {
                first = u;
                break;
            }
        }
    } else {
        for (u = 0u; u + 1u < FXBUF_POSITION_COUNT; u++) {
            if (fxbuf_state.unit_owner[u] == FXBUF_UNIT_FREE &&
                fxbuf_state.unit_owner[u + 1u] == FXBUF_UNIT_FREE) {
                first = u;
                break;
            }
        }
    }
    if (first == FXBUF_UNIT_FREE)
        return 0u;
    before = fxbuf_shareBytes();
    if (count == 1u) {
        fxbuf_state.unit_owner[first] = slot;
        units_out[0] = first;
    } else {
        /* u is the higher address; u+1 is the lower address. */
        fxbuf_state.unit_owner[first] = slot;
        fxbuf_state.unit_owner[first + 1u] = slot;
        units_out[0] = (uint8_t)(first + 1u);
        units_out[1] = first;
    }
    fxbuf_state.units_in_use = (uint8_t)(fxbuf_state.units_in_use + count);
    for (u = 0u; u < count; u++) {
        if (units_out[u] < FXBUF_VOICE_UNIT_COUNT)
            fxbuf_handoffRecord.unit_rate_hz[units_out[u]] =
                FXBUF_VOICE_RATE_HZ_DEFAULT;
    }
    fxbuf_notifyIfShareChanged(before);
    return 1u;
}

/*
 * Add the higher-address neighbour to a one-unit same-use ring
 * (S084 VX3 VX3-1.2; remediation R3).
 *
 * What: claims position `existing - 1` (positions grow toward lower arena
 * addresses, so `existing - 1` sits immediately above `existing`) only when
 * it is free, and returns the pair lowest-address first: `existing` stays
 * units_out[0], the ring base, and the new position is units_out[1].
 * Why: Ech->Ec2 and Rev->Rv2 continue the same ring. Keeping the base keeps
 * every stored write position and sample offset valid; the old code claimed
 * `existing + 1`, which moved the base down one unit and left the live ring
 * running inside an uncleared unit. When the higher neighbour is not free the
 * call fails and VoiceBuffer falls back to a changed use (the old ring tails
 * on a lease and the new use starts fresh). The caller clears the new unit.
 * Inputs: slot, existing owned position and output pair. Output: success.
 * Caller: VoiceBuffer's same-use continuation path.
 */
uint8_t fxbuf_unitAcquireAbove(uint8_t slot, uint8_t existing,
                               uint8_t units_out[2])
{
    uint32_t before;
    uint8_t above;

    if (!units_out || slot >= FXBUF_VOICE_SLOT_COUNT ||
        existing == 0u || existing >= FXBUF_POSITION_COUNT ||
        fxbuf_state.unit_owner[existing] != slot ||
        fxbuf_positionCountForSlot(slot) != 1u ||
        fxbuf_regularInUse() >= FXBUF_REGULAR_UNIT_MAX)
        return 0u;
    above = (uint8_t)(existing - 1u);
    if (fxbuf_state.unit_owner[above] != FXBUF_UNIT_FREE)
        return 0u;
    before = fxbuf_shareBytes();
    fxbuf_state.unit_owner[above] = slot;
    fxbuf_state.units_in_use++;
    units_out[0] = existing;
    units_out[1] = above;
    if (above < FXBUF_VOICE_UNIT_COUNT)
        fxbuf_handoffRecord.unit_rate_hz[above] = FXBUF_VOICE_RATE_HZ_DEFAULT;
    fxbuf_notifyIfShareChanged(before);
    return 1u;
}

/*
 * Retag regular positions as one tail lease (S084 VX3 VX3-1.4).
 *
 * What:       changes owner bytes only; the physical samples and share stay
 *             in place. Why: a different insert use must keep the old tail
 *             audible while the slot obtains fresh units. Inputs: positions,
 *             count 1..2 and runner id 0..1. Outputs: all-or-nothing success.
 * Accessors: VoiceBuffer.c. Affiliates: FXBUF_LEASE_UNIT_MAX.
 */
uint8_t fxbuf_unitToLease(const uint8_t *units, uint8_t count, uint8_t tail_id)
{
    uint8_t i;
    uint8_t j;
    uint8_t tagged = 0u;

    if (!units || count == 0u || count > FXBUF_VOICE_UNITS_PER_SLOT ||
        tail_id >= (FXBUF_LEASE_UNIT_MAX / 2u) ||
        (uint8_t)(fxbuf_state.lease_in_use + count) > FXBUF_LEASE_UNIT_MAX)
        return 0u;
    if (count == 2u &&
        (units[0] > units[1] ? units[0] - units[1] : units[1] - units[0]) != 1u)
        return 0u;
    for (i = 0u; i < FXBUF_POSITION_COUNT; i++)
        if (fxbuf_state.unit_owner[i] == FXBUF_OWNER_LEASE_BASE + tail_id)
            tagged++;
    if ((uint8_t)(tagged + count) > FXBUF_VOICE_UNITS_PER_SLOT)
        return 0u;
    for (i = 0u; i < count; i++) {
        if (!fxbuf_positionValid(units[i]) ||
            fxbuf_state.unit_owner[units[i]] >= FXBUF_VOICE_SLOT_COUNT)
            return 0u;
        for (j = 0u; j < i; j++)
            if (units[i] == units[j])
                return 0u;
    }
    for (i = 0u; i < count; i++)
        fxbuf_state.unit_owner[units[i]] =
            (uint8_t)(FXBUF_OWNER_LEASE_BASE + tail_id);
    fxbuf_state.lease_in_use = (uint8_t)(fxbuf_state.lease_in_use + count);
    return 1u;
}

/*
 * Re-adopt lease positions for a regular slot (S084 VX3 VX3-1.4).
 *
 * What:       retags one runner's lease back to its returning slot. Why: C9
 *             restores a returning tail without a gap or audio copy. Inputs:
 *             lease positions, count and slot. Outputs: all-or-nothing
 *             success; the share does not move. Accessors: VoiceBuffer.c.
 */
uint8_t fxbuf_leaseToSlot(const uint8_t *units, uint8_t count, uint8_t slot)
{
    uint8_t i;
    uint8_t j;
    uint8_t lease_owner = FXBUF_UNIT_FREE;

    if (!units || slot >= FXBUF_VOICE_SLOT_COUNT || count == 0u ||
        count > FXBUF_VOICE_UNITS_PER_SLOT ||
        (uint8_t)(fxbuf_regularInUse() + count) > FXBUF_REGULAR_UNIT_MAX ||
        (uint8_t)(fxbuf_positionCountForSlot(slot) + count) >
            FXBUF_VOICE_UNITS_PER_SLOT ||
        count > fxbuf_state.lease_in_use)
        return 0u;
    if (count == 2u &&
        (units[0] > units[1] ? units[0] - units[1] : units[1] - units[0]) != 1u)
        return 0u;
    for (i = 0u; i < count; i++) {
        if (!fxbuf_positionValid(units[i]) ||
            fxbuf_state.unit_owner[units[i]] < FXBUF_OWNER_LEASE_BASE ||
            fxbuf_state.unit_owner[units[i]] >=
                FXBUF_OWNER_LEASE_BASE + FXBUF_LEASE_UNIT_MAX)
            return 0u;
        if (i == 0u)
            lease_owner = fxbuf_state.unit_owner[units[i]];
        else if (fxbuf_state.unit_owner[units[i]] != lease_owner)
            return 0u;
        for (j = 0u; j < i; j++)
            if (units[i] == units[j])
                return 0u;
    }
    for (i = 0u; i < count; i++)
        fxbuf_state.unit_owner[units[i]] = slot;
    fxbuf_state.lease_in_use = (uint8_t)(fxbuf_state.lease_in_use - count);
    return 1u;
}

/*
 * Release positions without clearing their audio (S084 VX3 VX3-1.4).
 *
 * What:       returns valid regular or lease positions to the free map. Why:
 *             the next owner decides whether old contents are adopted or
 *             cleared. Inputs: position array and count. Outputs: owner map
 *             and share state; callbacks remain pass-coalesced. Accessors:
 *             VoiceBuffer.c and the tests.
 */
void fxbuf_unitRelease(const uint8_t *units, uint8_t count)
{
    uint32_t before;
    uint8_t i;

    if (!units || count == 0u)
        return;
    before = fxbuf_shareBytes();
    for (i = 0u; i < count; i++) {
        uint8_t owner;

        if (!fxbuf_positionValid(units[i]))
            continue;
        owner = fxbuf_state.unit_owner[units[i]];
        if (owner == FXBUF_UNIT_FREE)
            continue;
        if (owner >= FXBUF_OWNER_LEASE_BASE &&
            owner < FXBUF_OWNER_LEASE_BASE + FXBUF_LEASE_UNIT_MAX &&
            fxbuf_state.lease_in_use != 0u)
            fxbuf_state.lease_in_use--;
        fxbuf_state.unit_owner[units[i]] = FXBUF_UNIT_FREE;
        if (fxbuf_state.units_in_use != 0u)
            fxbuf_state.units_in_use--;
    }
    fxbuf_notifyIfShareChanged(before);
}

/*
 * Return one position's aligned audio pointer (S084 VX3 VX3-1.4).
 *
 * What:       maps an occupied physical position into the arena. Why: insert
 *             types use the same contiguous units without knowing arena
 *             geometry. Inputs: position 0..15. Outputs: 2,208 samples or
 *             NULL for free/out-of-range. Accessors: VoiceBuffer.c.
 */
int16_t *fxbuf_unitPtr(uint8_t unit)
{
    if (!fxbuf_positionValid(unit) ||
        fxbuf_state.unit_owner[unit] == FXBUF_UNIT_FREE)
        return NULL;
    return (int16_t *)(void *)(fxbuf_state.base + fxbuf_unitOffset(unit));
}

/* Begin a nested share-change pass (S084 VX3 VX3-1.4). */
void fxbuf_beginPass(void)
{
    if (fxbuf_state.pass_depth == 0u)
        fxbuf_state.pass_start_bytes = fxbuf_shareBytes();
    if (fxbuf_state.pass_depth != 0xFFu)
        fxbuf_state.pass_depth++;
}

/* End the outer pass and notify the active Effect once when needed. */
void fxbuf_endPass(void)
{
    fx_share_t share;

    if (fxbuf_state.pass_depth == 0u)
        return;
    fxbuf_state.pass_depth--;
    if (fxbuf_state.pass_depth != 0u || !fxbuf_state.on_share_changed)
        return;
    fxbuf_effectShare(&share);
    if (share.bytes != fxbuf_state.pass_start_bytes)
        fxbuf_state.on_share_changed(&share);
}

#if DEV_MODE_DIAGNOSTIC
/*
 * Dump all physical owners (S084 VX3 VX3-1.4).
 *
 * What:       emits '.' for free, '0'..'5' for regular slots and 'a'..'d'
 *             for lease runners. Why: the DEV screen needs a direct witness
 *             of the elastic position map. Inputs: a 17-byte output buffer.
 *             Outputs: exactly sixteen owner characters plus a terminator.
 *             Accessors: main.c diagnostic display.
 */
void fxbuf_devOwnerMap(char out[FXBUF_POSITION_COUNT + 1u])
{
    uint8_t u;

    if (!out)
        return;
    for (u = 0u; u < FXBUF_POSITION_COUNT; u++) {
        uint8_t owner = fxbuf_state.unit_owner[u];

        if (owner == FXBUF_UNIT_FREE)
            out[u] = '.';
        else if (owner < FXBUF_VOICE_SLOT_COUNT)
            out[u] = (char)('0' + owner);
        else if (owner < FXBUF_OWNER_LEASE_BASE + FXBUF_LEASE_UNIT_MAX)
            out[u] = (char)('a' + owner - FXBUF_OWNER_LEASE_BASE);
        else
            out[u] = '?';
    }
    out[FXBUF_POSITION_COUNT] = '\0';
}
#endif

/* Register or clear the one foreground-only share-change callback. */
void fxbuf_setShareChangedCallback(fxbuf_share_changed_fn fn)
{
    fxbuf_state.on_share_changed = fn;
}

/*
 * Begin a handoff snapshot and return its writable record.
 *
 * FxBuffer refreshes share bounds, ownership, free-unit rates and offsets, and
 * the Effect fields. Still-owned voice entries are preserved so their owner
 * can pass positions to the entering owner; the Effect fills entries 12..15.
 */
fxbuf_handoff_t *fxbuf_handoffBeginExit(void)
{
    fxbuf_handoff_t *h = &fxbuf_handoffRecord;
    uint8_t i;

    h->effect_share_offset = 0u;
    h->effect_share_bytes = fxbuf_shareBytes();
    for (i = 0u; i < FXBUF_VOICE_UNIT_COUNT; i++) {
        h->unit_owner_slot[i] = fxbuf_state.unit_owner[i];
        if (fxbuf_state.unit_owner[i] == FXBUF_UNIT_FREE) {
            h->unit_rate_hz[i] = 0u;
            h->read_offset[i] = FXBUF_OFFSET_NONE;
            h->write_offset[i] = FXBUF_OFFSET_NONE;
            h->unit_written_mask &= (uint16_t)~(1u << i);
        }
    }
    h->effect_type = FXBUF_EFFECT_TYPE_NONE;
    h->effect_channels = 0u;
    h->effect_bits = 0u;
    h->effect_rate_hz = 0u;
    for (i = FXBUF_HANDOFF_EFFECT_POINTER_BASE;
         i < FXBUF_HANDOFF_POINTER_COUNT; i++) {
        h->read_offset[i] = FXBUF_OFFSET_NONE;
        h->write_offset[i] = FXBUF_OFFSET_NONE;
    }
    h->state_flags &= (uint8_t)~FXBUF_STATE_EFFECT_WRITTEN;
    if (h->unit_written_mask)
        h->state_flags |= FXBUF_STATE_VOICE_WRITTEN;
    else
        h->state_flags &= (uint8_t)~FXBUF_STATE_VOICE_WRITTEN;
    return h;
}

/*
 * Record one owned voice unit's handoff coordinates and written flag.
 *
 * Invalid or free units are ignored. The unit written mask and aggregate voice
 * state flag are kept in sync with the recorded entry.
 */
void fxbuf_handoffSetVoiceUnit(uint8_t unit, uint32_t read_offset,
                               uint32_t write_offset, uint16_t rate_hz,
                               uint8_t written)
{
    fxbuf_handoff_t *h = &fxbuf_handoffRecord;

    if (unit >= FXBUF_VOICE_UNIT_COUNT ||
        fxbuf_state.unit_owner[unit] == FXBUF_UNIT_FREE)
        return;
    h->read_offset[unit] = read_offset;
    h->write_offset[unit] = write_offset;
    h->unit_rate_hz[unit] = rate_hz;
    if (written)
        h->unit_written_mask |= (uint16_t)(1u << unit);
    else
        h->unit_written_mask &= (uint16_t)~(1u << unit);
    if (h->unit_written_mask)
        h->state_flags |= FXBUF_STATE_VOICE_WRITTEN;
    else
        h->state_flags &= (uint8_t)~FXBUF_STATE_VOICE_WRITTEN;
}

/* Return the current read-only handoff snapshot for the entering owner. */
const fxbuf_handoff_t *fxbuf_handoff(void)
{
    return &fxbuf_handoffRecord;
}

#if DEV_MODE_DIAGNOSTIC
/* Return the self-test result captured before diagnostic unit claims. */
uint8_t fxbuf_devSelfTestResult(void)
{
    return fxbuf_selfTestResult;
}
#endif
