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
#include "InstrumentManager.h"
#include <stddef.h>
#include <string.h>

/* Linker-defined .dtcm_fxbuf bounds; their addresses are never dereferenced
 * as objects. */
extern uint8_t _sfxbuf[];
extern uint8_t _efxbuf[];

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
    uint8_t unit_owner[FXBUF_VOICE_UNIT_COUNT];
    uint8_t units_in_use;
} fxbuf_state_t;

static fxbuf_state_t fxbuf_state;

/* Handoff record (SRAM1 .bss, 180 B); audio contents remain in the arena. */
static fxbuf_handoff_t fxbuf_handoffRecord;

#if DEV_MODE_DIAGNOSTIC
/* Diagnostic-only boot self-test result (1 B). */
static uint8_t fxbuf_selfTestResult;
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

    for (u = (int8_t)(FXBUF_VOICE_UNIT_COUNT - 1u); u >= 0; u--) {
        if (fxbuf_state.unit_owner[(uint8_t)u] != FXBUF_UNIT_FREE)
            return fxbuf_unitOffset((uint8_t)u);
    }
    return fxbuf_state.bytes;
}

/* Notify the active Effect only when the share length really changed. */
static void fxbuf_notifyIfShareChanged(uint32_t before_bytes)
{
    fx_share_t share;

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

done:
    fxbuf_clearOwners();
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
    fxbuf_state.base = _sfxbuf;
    fxbuf_state.bytes = (uint32_t)(_efxbuf - _sfxbuf);
    fxbuf_state.on_share_changed = NULL;
    fxbuf_clearOwners();

#if DEV_MODE_DIAGNOSTIC
    fxbuf_selfTestResult = fxbuf_selfTest();
    {
        uint8_t n;
        for (n = 0u; n < (uint8_t)DEV_FXBUF_FORCE_VOICE_UNITS; n++)
            (void)fxbuf_voiceAcquire((uint8_t)(n / FXBUF_VOICE_UNITS_PER_SLOT), 1u);
    }
#endif
    fxbuf_handoffResetAll();
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
        (uint8_t)(fxbuf_state.units_in_use + count) > FXBUF_VOICE_UNIT_COUNT)
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
