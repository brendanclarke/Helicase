/*
 * VoiceBuffer.c
 *
 *  Created on: 26.10.2026
 * ----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * ----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ----------------------------------------------------------------------------
 */

#include "VoiceBuffer.h"
#include "InsertEffectsManager.h"
#include "BufferTools.h"
#include "config.h"
#include <string.h>

/*
 * One active record per slot/stage and two tail runners (S084 VX3 VX3-4.2).
 * The records are DTCM-resident control/state storage; the scratch block is
 * SRAM1 and is reused for one runner at a time. No boot path claims units.
 */
typedef struct {
    uint8_t owner;
    uint8_t stage;
    uint8_t type_id;
    uint8_t flags;
    uint8_t units;
    uint8_t unit_pos[2];
    uint8_t pending_type;
    uint32_t write_pos;
    uint16_t silent_blocks;
    uint16_t age_blocks;
    float fade;
    float param_now[3];
    uint8_t dsp_state[INSERT_STATE_BYTES];
} voice_buffer_record_t;

_Static_assert(sizeof(voice_buffer_record_t) == 80u,
               "VoiceBuffer records are the approved 80 B");

#if defined(INSERT_HOST_TEST)
/*
 * Host VoiceBuffer case-test boundary (S084 VX3 VX3-11.3).
 *
 * What:       keep the approved record array in ordinary host storage while
 *             compiling the same 14-record layout and state machine.
 * Why:        the C1-C16 lifecycle cases must exercise the real allocator,
 *             Hold/retry path, runner records, and tail bookkeeping without
 *             requiring a linker-owned DTCM section in the host executable.
 * Production: firmware builds retain INDTCMZ, so this branch changes only
 *             test placement and cannot change the production record budget.
 */
#define VOICEBUF_RECORD_STORAGE
#else
#define VOICEBUF_RECORD_STORAGE INDTCMZ
#endif
static VOICEBUF_RECORD_STORAGE voice_buffer_record_t
    voiceBuffer_records[VOICEBUF_RECORDS];
static int16_t voiceBuffer_tailScratch[OUTPUT_DMA_SIZE];

/* Active record index; runners are the two records after the active range. */
static uint8_t vb_activeIndex(uint8_t slot, uint8_t stage)
{
    return (uint8_t)(2u * slot + stage);
}

/*
 * Same-use predicate (S084 VX3 VX3-4.2, C1/C2/C3/C8; remediation R8).
 *
 * What: accepts an identical non-buffer type, or two buffer types in one
 * family where the new type needs at least the old unit count (D15).
 * Why: parameters glide and the ring stays live for same-use changes; a
 * family grow (Ech->Ec2) continues the ring. A shrink (Ec2->Ech) is a
 * different use (C4): the long ring tails on a lease and the short type
 * starts fresh, so the second unit is released instead of being held.
 * Inputs: old and new descriptors. Output: nonzero when the state continues.
 */
static uint8_t vb_sameUse(const insert_effect_type_t *old_type,
                          const insert_effect_type_t *new_type)
{
    if (!old_type || !new_type)
        return 0u;
    if (old_type->units == 0u || new_type->units == 0u)
        return (uint8_t)(old_type == new_type);
    return (uint8_t)(old_type->family != 0u &&
                     old_type->family == new_type->family &&
                     new_type->units >= old_type->units);
}

/* Return the type descriptor represented by one record. */
static const insert_effect_type_t *vb_recordType(
    const voice_buffer_record_t *record)
{
    if (!record || record->owner == FXBUF_UNIT_FREE ||
        record->type_id == VOICEBUF_PENDING_NONE)
        return NULL;
    return (record->stage == VOICEBUF_STAGE_MIX)
        ? insertFx_mixDescriptor(record->type_id)
        : insertFx_transientDescriptor(record->type_id);
}

/* Build the type context from a record and the manager's block controls. */
static void vb_context(voice_buffer_record_t *record,
                       insert_block_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    if (!record || record->units == 0u)
        return;
    ctx->ring = fxbuf_unitPtr(record->unit_pos[0]);
    ctx->ring_len = (uint32_t)record->units * FXBUF_VOICE_UNIT_SAMPLES;
    ctx->wp = &record->write_pos;
}

/*
 * Clear newly owned ring units on first use (S084 VX3 remediation R5).
 *
 * What: zeroes each unit flagged CLEAR0/CLEAR1, then drops the flags.
 * Why: a claim inside an apply pass can take the top of the Effect share.
 * The Effect (CrumpBit) moves its newest audio out of that range only when
 * the pass ends, so clearing at claim time destroyed the audio it was about
 * to keep. Every render happens after the outermost pass has ended, so the
 * first render of the record is the earliest safe point. Cost: one memset of
 * 4,416 or 8,832 B, once per claim. Inputs: record. Outputs: cleared units.
 * Callers: voiceBuffer_render(), voiceBuffer_addTails().
 */
static void vb_clearPending(voice_buffer_record_t *record)
{
    if ((record->flags & VOICEBUF_FLAG_CLEAR0) != 0u && record->units != 0u) {
        int16_t *unit = fxbuf_unitPtr(record->unit_pos[0]);

        if (unit)
            memset(unit, 0, FXBUF_VOICE_UNIT_BYTES);
    }
    if ((record->flags & VOICEBUF_FLAG_CLEAR1) != 0u && record->units > 1u) {
        int16_t *unit = fxbuf_unitPtr(record->unit_pos[1]);

        if (unit)
            memset(unit, 0, FXBUF_VOICE_UNIT_BYTES);
    }
    record->flags &= (uint8_t)~(VOICEBUF_FLAG_CLEAR0 | VOICEBUF_FLAG_CLEAR1);
}

/* Find a free runner; a runner is globally shared by both stages and slots. */
static voice_buffer_record_t *vb_freeRunner(void)
{
    uint8_t i;

    for (i = 0u; i < VOICEBUF_RUNNERS; i++) {
        voice_buffer_record_t *runner =
            &voiceBuffer_records[VOICEBUF_ACTIVE + i];

        if (runner->flags == 0u || runner->owner == FXBUF_UNIT_FREE)
            return runner;
    }
    return NULL;
}

/*
 * Move an active buffer record to a lease runner (S084 VX3 VX3-4.2, C4-C7).
 *
 * What: copies only the 80-byte record and retags its existing physical units
 * as a runner lease; no audio is copied or cleared. Why: the old use must
 * continue audibly while the active record receives the new use. Inputs: an
 * active record and a free runner. Output: the runner is a TAIL record.
 */
static uint8_t vb_moveToRunner(voice_buffer_record_t *active,
                               voice_buffer_record_t *runner)
{
    uint8_t runner_id;

    if (!active || !runner || active->units == 0u)
        return 1u;
    runner_id = (uint8_t)(runner - voiceBuffer_records - VOICEBUF_ACTIVE);
    if (!fxbuf_unitToLease(active->unit_pos, active->units, runner_id))
        return 0u;
    *runner = *active;
    runner->flags = (uint8_t)((runner->flags & VOICEBUF_FLAG_CARRY) |
                              VOICEBUF_FLAG_TAIL);
    runner->fade = 1.0f;
    runner->silent_blocks = 0u;
    runner->age_blocks = 0u;
    runner->pending_type = VOICEBUF_PENDING_NONE;
    return 1u;
}

/*
 * Set up a new active record (S084 VX3 VX3-4.2, C4/C6/C7).
 *
 * What: claims the requested adjacent unit count, flags the newly owned
 * units for clearing at the first render (R5), zeroes the bounded type state
 * and calls init(). Why: ownership changes happen at option apply, and the
 * new use must never hear a previous owner's bytes. Inputs: active record, descriptor and type id. Output:
 * initialized ACTIVE record, or zero on reservation failure.
 */
static uint8_t vb_setupFresh(voice_buffer_record_t *record,
                              const insert_effect_type_t *type,
                              uint8_t type_id)
{
    insert_block_ctx_t ctx;
    uint8_t units[2] = { 0u, 0u };

    if (!record || !type)
        return 0u;
    if (type->units != 0u &&
        !fxbuf_unitAcquire(record->owner, type->units, units))
        return 0u;
    record->type_id = type_id;
    /* Remediation R5: the new units are cleared on the first render. */
    record->flags = (uint8_t)(VOICEBUF_FLAG_ACTIVE |
        ((type->units > 0u) ? VOICEBUF_FLAG_CLEAR0 : 0u) |
        ((type->units > 1u) ? VOICEBUF_FLAG_CLEAR1 : 0u));
    record->units = type->units;
    record->unit_pos[0] = units[0];
    record->unit_pos[1] = units[1];
    record->pending_type = VOICEBUF_PENDING_NONE;
    record->write_pos = 0u;
    record->silent_blocks = 0u;
    record->age_blocks = 0u;
    record->fade = 1.0f;
    memset(record->param_now, 0, sizeof(record->param_now));
    memset(record->dsp_state, 0, sizeof(record->dsp_state));
    vb_context(record, &ctx);
    if (type->init)
        type->init(record->dsp_state, &ctx);
    return 1u;
}

/* Release a runner and return its record to the free state. */
static void vb_releaseRunner(voice_buffer_record_t *runner)
{
    if (!runner || runner->flags == 0u)
        return;
    fxbuf_unitRelease(runner->unit_pos, runner->units);
    memset(runner, 0, sizeof(*runner));
    runner->owner = FXBUF_UNIT_FREE;
    runner->pending_type = VOICEBUF_PENDING_NONE;
}

/*
 * Apply a changed use after the fit/runner decision (C4-C7, D1).
 *
 * What: tails the old buffer when necessary, then installs a fresh type and
 * reserves its units. Why: this is the only path that can change a stage's
 * use, so the C cases remain auditable and every allocation is foreground.
 * Inputs: active record, new descriptor/id. Output: selection status.
 */
static uint8_t vb_applyChanged(voice_buffer_record_t *record,
                               const insert_effect_type_t *type,
                               uint8_t type_id)
{
    voice_buffer_record_t *runner;

    if (record->units != 0u &&
        (record->flags & VOICEBUF_FLAG_PRIMED) == 0u) {
        /*
         * Remediation R7a: a ring that has never rendered holds no audio, so
         * it is released instead of becoming a tail. Scene and Instrument
         * applies select a stage more than once (the TYPE writer runs before
         * the WAVE value arrives); without this every such apply spent a
         * runner on silence and forced Hold on the other slots.
         */
        fxbuf_unitRelease(record->unit_pos, record->units);
        record->units = 0u;
    }
    if (type->units != 0u &&
        !((record->units == 0u)
              ? fxbuf_unitFits(record->owner, type->units)
              : fxbuf_unitFitsReplacing(record->owner, type->units,
                                        record->units)))
        return VOICEBUF_SELECT_HELD;

    if (record->units != 0u) {
        runner = vb_freeRunner();
        if (!runner)
            return VOICEBUF_SELECT_HELD;
        if (!vb_moveToRunner(record, runner))
            return VOICEBUF_SELECT_HELD;
    }
    record->flags = VOICEBUF_FLAG_ACTIVE;
    record->units = 0u;
    record->unit_pos[0] = 0u;
    record->unit_pos[1] = 0u;
    record->write_pos = 0u;
    record->pending_type = VOICEBUF_PENDING_NONE;
    if (!vb_setupFresh(record, type, type_id)) {
        record->flags = VOICEBUF_FLAG_ACTIVE;
        record->type_id = type_id;
        return VOICEBUF_SELECT_HELD;
    }
    return VOICEBUF_SELECT_CHANGED;
}

/*
 * Re-adopt a matching runner without a gap (S084 VX3 VX3-4.2, C9).
 *
 * What: swaps the active and tail records after retagging their positions in
 * opposite directions. Why: a returning same-use option resumes its old
 * ring and the displaced active use becomes the tail. Inputs: active record,
 * runner and requested descriptor/id. Output: active/running records swapped.
 */
static uint8_t vb_readoptRunner(voice_buffer_record_t *active,
                                voice_buffer_record_t *runner,
                                const insert_effect_type_t *type,
                                uint8_t type_id)
{
    voice_buffer_record_t old_active;
    uint8_t runner_id;

    /*
     * Remediation R1/R2: only the slot and stage that left the tail may take
     * it back, and a non-buffer active record (or one that never rendered)
     * is never turned into a tail: it has no ring, no tail operation and no
     * expiry, so it used to hold the runner for good.
     */
    if (!active || !runner || active->units > FXBUF_VOICE_UNITS_PER_SLOT ||
        runner->units != type->units || runner->owner != active->owner ||
        runner->stage != active->stage)
        return 0u;
    runner_id = (uint8_t)(runner - voiceBuffer_records - VOICEBUF_ACTIVE);
    if (active->units != 0u &&
        (active->flags & VOICEBUF_FLAG_PRIMED) == 0u) {
        fxbuf_unitRelease(active->unit_pos, active->units);
        active->units = 0u;
    }
    if (active->units == 0u) {
        if (!fxbuf_leaseToSlot(runner->unit_pos, runner->units, active->owner))
            return 0u;
        *active = *runner;
        active->type_id = type_id;
        active->flags = (uint8_t)((active->flags & VOICEBUF_FLAG_CARRY) |
                                  VOICEBUF_FLAG_ACTIVE);
        active->pending_type = VOICEBUF_PENDING_NONE;
        active->fade = 1.0f;
        memset(runner, 0, sizeof(*runner));
        runner->owner = FXBUF_UNIT_FREE;
        runner->pending_type = VOICEBUF_PENDING_NONE;
        return 1u;
    }
    if (!fxbuf_unitToLease(active->unit_pos, active->units, runner_id))
        return 0u;
    if (!fxbuf_leaseToSlot(runner->unit_pos, runner->units, active->owner)) {
        (void)fxbuf_leaseToSlot(active->unit_pos, active->units, active->owner);
        return 0u;
    }
    old_active = *active;
    *active = *runner;
    active->type_id = type_id;
    active->flags = (uint8_t)((active->flags & VOICEBUF_FLAG_CARRY) |
                              VOICEBUF_FLAG_ACTIVE);
    active->pending_type = VOICEBUF_PENDING_NONE;
    active->fade = 1.0f;
    *runner = old_active;
    runner->flags = (uint8_t)((runner->flags & VOICEBUF_FLAG_CARRY) |
                              VOICEBUF_FLAG_TAIL);
    runner->pending_type = VOICEBUF_PENDING_NONE;
    runner->fade = 1.0f;
    runner->silent_blocks = 0u;
    runner->age_blocks = 0u;
    return 1u;
}

/*
 * Select one stage use (S084 VX3 VX3-4.1).
 *
 * What: handles same-use retarget/grow, returning-tail adoption, changed-use
 * tailing and Hold when both runners are occupied. Why: a single selection
 * brackets its allocator mutations, while an outer Scene pass can nest this
 * bracket to coalesce the final share callback. Inputs: slot, stage, type and
 * registry id. Output: SAME, CHANGED or HELD.
 */
uint8_t voiceBuffer_select(uint8_t slot, uint8_t stage,
                           const insert_effect_type_t *type, uint8_t type_id)
{
    voice_buffer_record_t *record;
    const insert_effect_type_t *old_type;
    uint8_t i;
    uint8_t result;

    if (slot >= FXBUF_VOICE_SLOT_COUNT || stage > VOICEBUF_STAGE_TRANSIENT ||
        !type)
        return VOICEBUF_SELECT_HELD;
    record = &voiceBuffer_records[vb_activeIndex(slot, stage)];
    record->owner = slot;
    record->stage = stage;
    fxbuf_beginPass();

    if (record->flags == 0u) {
        result = vb_setupFresh(record, type, type_id)
            ? VOICEBUF_SELECT_CHANGED : VOICEBUF_SELECT_HELD;
        fxbuf_endPass();
        return result;
    }
    old_type = vb_recordType(record);
    if (vb_sameUse(old_type, type)) {
        if (type->units > record->units) {
            if (!fxbuf_unitAcquireAbove(record->owner, record->unit_pos[0],
                                        record->unit_pos)) {
                result = vb_applyChanged(record, type, type_id);
                fxbuf_endPass();
                return result;
            }
            record->units = type->units;
            /* Remediation R3/R5: unit_pos[0] (the base) is unchanged; the
            ** new higher-address unit is cleared on the next render. */
            record->flags |= VOICEBUF_FLAG_CLEAR1;
            if (type->retarget) {
                insert_block_ctx_t ctx;

                vb_context(record, &ctx);
                type->retarget(record->dsp_state, &ctx,
                               (uint8_t)(type->units - 1u));
            }
        } else if (type->retarget) {
            insert_block_ctx_t ctx;

            vb_context(record, &ctx);
            type->retarget(record->dsp_state, &ctx, record->units);
        }
        record->type_id = type_id;
        record->flags |= VOICEBUF_FLAG_ACTIVE;
        /* Remediation R4: a later same-use choice cancels a held change. */
        record->pending_type = VOICEBUF_PENDING_NONE;
        fxbuf_endPass();
        return VOICEBUF_SELECT_SAME;
    }
    for (i = 0u; i < VOICEBUF_RUNNERS; i++) {
        voice_buffer_record_t *runner =
            &voiceBuffer_records[VOICEBUF_ACTIVE + i];
        const insert_effect_type_t *runner_type = vb_recordType(runner);

        if ((runner->flags & VOICEBUF_FLAG_TAIL) != 0u &&
            vb_sameUse(runner_type, type) &&
            vb_readoptRunner(record, runner, type, type_id)) {
            fxbuf_endPass();
            return VOICEBUF_SELECT_CHANGED;
        }
    }
    result = vb_applyChanged(record, type, type_id);
    /* Remediation R4: InsertEffectsManager is the single retry owner; the
    ** record keeps no pending selection of its own. */
    record->pending_type = VOICEBUF_PENDING_NONE;
    fxbuf_endPass();
    return result;
}

/*
 * Test whether a type is selectable now (S084 VX3 VX3-4.1, C13/D15).
 *
 * What: counts both stages' active units against the slot cap and treats the
 * current record's units as reusable for a replacement. Why: menu hiding and
 * Ec2/Rv2 downgrade decisions must agree with the allocator. Inputs: slot,
 * stage and candidate type. Output: nonzero when selection can proceed.
 */
uint8_t voiceBuffer_fits(uint8_t slot, uint8_t stage,
                         const insert_effect_type_t *type)
{
    voice_buffer_record_t *record;
    uint8_t released;

    if (slot >= FXBUF_VOICE_SLOT_COUNT || stage > VOICEBUF_STAGE_TRANSIENT ||
        !type)
        return 0u;
    record = &voiceBuffer_records[vb_activeIndex(slot, stage)];
    if (record->flags == 0u)
        return type->units == 0u || fxbuf_unitFits(slot, type->units);
    if (vb_sameUse(vb_recordType(record), type) && type->units <= record->units)
        return 1u;
    released = record->units;
    return type->units == 0u ||
           fxbuf_unitFitsReplacing(slot, type->units, released);
}

/*
 * Release expired tails (S084 VX3 VX3-4.1, D1; remediation R4).
 *
 * What: frees the lease positions of every expired runner inside one nested
 * allocator pass. Why: all changes happen between render blocks and the
 * Effect sees one final share. Held selections are retried by
 * insertFx_service() right after this call; keeping one retry owner stops a
 * stale held type from being applied after the user has chosen another.
 * Inputs: runner flags. Output: free runners, one coalesced share callback.
 */
void voiceBuffer_service(void)
{
    uint8_t i;

    fxbuf_beginPass();
    for (i = 0u; i < VOICEBUF_RUNNERS; i++) {
        voice_buffer_record_t *runner =
            &voiceBuffer_records[VOICEBUF_ACTIVE + i];

        if ((runner->flags & VOICEBUF_FLAG_EXPIRED) != 0u)
            vb_releaseRunner(runner);
    }
    fxbuf_endPass();
}

/* Report whether a held selection could be retried now (a runner is free). */
uint8_t voiceBuffer_runnerFree(void)
{
    return (uint8_t)(vb_freeRunner() != NULL);
}

/*
 * Render one active stage and ramp its three controls (S084 VX3 VX3-4.1).
 *
 * What: supplies the active ring and state to the type, applies the common
 * input gain policy, and advances parameter state toward the block targets.
 * Why: types see resolved/smoothed controls and never know the slot. Inputs:
 * slot/stage, int16 block and manager context. Output: block in place.
 */
void voiceBuffer_render(uint8_t slot, uint8_t stage, int16_t *buf,
                        uint8_t size, insert_block_ctx_t *ctx)
{
    voice_buffer_record_t *record;
    const insert_effect_type_t *type;
    insert_block_ctx_t local;
    float target[3];
    uint8_t i;

    if (!buf || size == 0u || slot >= FXBUF_VOICE_SLOT_COUNT ||
        stage > VOICEBUF_STAGE_TRANSIENT || !ctx)
        return;
    record = &voiceBuffer_records[vb_activeIndex(slot, stage)];
    if ((record->flags & VOICEBUF_FLAG_ACTIVE) == 0u)
        return;
    type = vb_recordType(record);
    if (!type || !type->process)
        return;
    vb_clearPending(record);
    target[0] = ctx->c0;
    target[1] = ctx->c1;
    target[2] = ctx->c2;
    local = *ctx;
    vb_context(record, &local);
    local.c0 = record->param_now[0];
    local.c1 = record->param_now[1];
    local.c2 = record->param_now[2];
    local.rate_shaped = ctx->rate_shaped;
    local.drive = ctx->drive;
    local.trigger = ctx->trigger;
    if ((type->flags & INSERT_FLAG_INPUT_GAIN) != 0u)
        for (i = 0u; i < size; i++)
            buf[i] = (int16_t)__SSAT((int32_t)buf[i] * 5 / 4, 16);
    type->process(record->dsp_state, buf, size, &local);
    for (i = 0u; i < 3u; i++)
        record->param_now[i] = target[i];
    record->flags |= VOICEBUF_FLAG_PRIMED;
}

/*
 * Add every slot tail after its active Mix stage (S084 VX3 VX3-4.1).
 *
 * What: renders wet-only tail audio, applies the residue fade, measures peak
 * silence/age expiry and saturating-adds it to the slot block. Why: tails
 * follow the slot's later volume/pan path and survive a type change. Inputs:
 * slot, output block and size. Output: block with all live runner tails.
 */
void voiceBuffer_addTails(uint8_t slot, int16_t *buf, uint8_t size)
{
    uint8_t i;

    if (!buf || slot >= FXBUF_VOICE_SLOT_COUNT || size == 0u)
        return;
    for (i = 0u; i < VOICEBUF_RUNNERS; i++) {
        voice_buffer_record_t *runner =
            &voiceBuffer_records[VOICEBUF_ACTIVE + i];
        const insert_effect_type_t *type;
        insert_block_ctx_t ctx;
        uint32_t peak = 0u;
        uint8_t n;

        if ((runner->flags & VOICEBUF_FLAG_TAIL) == 0u ||
            runner->owner != slot)
            continue;
        type = vb_recordType(runner);
        if (!type || !type->tail) {
            /* Remediation R1: a tail with nothing to render expires now. */
            runner->flags = VOICEBUF_FLAG_EXPIRED;
            continue;
        }
        vb_clearPending(runner);
        memset(voiceBuffer_tailScratch, 0,
               sizeof(voiceBuffer_tailScratch));
        vb_context(runner, &ctx);
        ctx.c0 = runner->param_now[0];
        ctx.c1 = runner->param_now[1];
        ctx.c2 = runner->param_now[2];
        type->tail(runner->dsp_state, voiceBuffer_tailScratch, size, &ctx);
        if ((runner->flags & VOICEBUF_FLAG_FADING) != 0u) {
            runner->fade -= 1.0f / (float)VOICEBUF_TAIL_FADE_BLOCKS;
            if (runner->fade <= 0.0f) {
                runner->flags = VOICEBUF_FLAG_EXPIRED;
                continue;
            }
        }
        for (n = 0u; n < size; n++) {
            int32_t sample = (int32_t)((float)voiceBuffer_tailScratch[n] *
                                       runner->fade);
            uint32_t magnitude = (uint32_t)(sample < 0 ? -sample : sample);

            if (magnitude > peak)
                peak = magnitude;
            buf[n] = (int16_t)__SSAT((int32_t)buf[n] + sample, 16);
        }
        runner->silent_blocks = (peak < VOICEBUF_SILENCE_PEAK)
            ? (uint16_t)(runner->silent_blocks + 1u) : 0u;
        if (runner->age_blocks != 0xFFFFu)
            runner->age_blocks++;
        if (runner->silent_blocks >= VOICEBUF_TAIL_SILENT_BLOCKS ||
            runner->age_blocks >= VOICEBUF_TAIL_AGE_BLOCKS)
            runner->flags |= VOICEBUF_FLAG_FADING;
    }
}

/*
 * Initialise records without reserving any units (S084 VX3 VX3-4.3, D12).
 *
 * What: marks active and runner records free and pending none. Why: the boot
 * Scene selector owns the first reservations, so boot never claims a blanket
 * voice allocation. Caller: insertFx_init() after fxbuf_init().
 */
void voiceBuffer_init(void)
{
    uint8_t i;

    memset(voiceBuffer_records, 0, sizeof(voiceBuffer_records));
    for (i = 0u; i < VOICEBUF_RECORDS; i++) {
        voiceBuffer_records[i].owner = FXBUF_UNIT_FREE;
        voiceBuffer_records[i].pending_type = VOICEBUF_PENDING_NONE;
    }
}

/* Nested pass wrapper used by preset/apply callers. */
void voiceBuffer_beginPass(void)
{
    fxbuf_beginPass();
}

/* End the outer voice-apply pass and publish one final share. */
void voiceBuffer_endPass(void)
{
    fxbuf_endPass();
}

#if DEV_MODE_DIAGNOSTIC
/*
 * Compact owner/record witness for the DEV diagnostics page (S084 VX3-4.9).
 * Inputs: two 17-byte caller buffers. Outputs: one code per active/runner
 * record; no audio or retained state is touched.
 */
void voiceBuffer_devDump(char row1[17], char row2[17])
{
    uint8_t i;

    if (!row1 || !row2)
        return;
    /* Remediation R19: row 2 holds no records (14 < 16); keep it blank. */
    memset(row1, ' ', 16u);
    memset(row2, ' ', 16u);
    for (i = 0u; i < VOICEBUF_RECORDS; i++) {
        char code = (voiceBuffer_records[i].flags == 0u) ? '.' :
                    (voiceBuffer_records[i].flags & VOICEBUF_FLAG_TAIL) ?
                    't' : 'a';
        if (i < 16u)
            row1[i] = code;
        else
            row2[i - 16u] = code;
    }
    row1[16] = '\0';
    row2[16] = '\0';
}
#endif
