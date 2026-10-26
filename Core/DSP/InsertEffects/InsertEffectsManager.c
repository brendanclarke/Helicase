/*
 * InsertEffectsManager.c
 *
 *  Created on: 26.10.2026
 * ----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * ----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ----------------------------------------------------------------------------
 */

#include "InsertEffectsManager.h"
#include "VoiceBuffer.h"
#include "valueShaper.h"
#include "config.h"
#include <string.h>

static const insert_effect_type_t *const insert_mix_types[INSERT_MIX_COUNT] = {
    &insert_typeDriveSrr, &insert_typeFold, &insert_typeCrush,
    &insert_typeRing, &insert_typePhaser, &insert_typeFlanger,
    &insert_typeChorus, &insert_typeEcho, &insert_typeEcho2,
    &insert_typeComb, &insert_typeRoom, &insert_typeRoom2
};
static const insert_effect_type_t *const insert_transient_types[
    INSERT_TRANSIENT_COUNT] = {
    &insert_typeResonator, &insert_typePluck, &insert_typeTube,
    &insert_typeFlam, &insert_typeStutter, &insert_typeReverse,
    &insert_typeSpring
};

typedef struct {
    uint8_t mix_type;
    uint8_t mix_pending;
    uint8_t mix_pending_type;
    uint8_t transient_option;
    uint8_t transient_active;
    uint8_t transient_pending;
    uint8_t transient_pending_option;
    uint8_t transient_pending_active;
    uint8_t rate;
    float rate_shaped;
} insert_slot_t;

static insert_slot_t insert_slots[FXBUF_VOICE_SLOT_COUNT];
static uint8_t insert_trigger_mask;

/* Non-buffer sentinel used to release an active transient buffer cleanly. */
static const insert_effect_type_t insert_typeTransientNone = {
    "off", { "wav", "vol", "frq" }, { "Wave", "Volume", "Freq" },
    0u, 0u, INSERT_PLACE_NONE, 0u, NULL, NULL, NULL, NULL
};

/* Nonzero when a registered type may be selected (remediation §18.4). */
static uint8_t insert_enabled(const insert_effect_type_t *type)
{
    return (uint8_t)(type && (type->flags & INSERT_FLAG_DISABLED) == 0u);
}

/* Return a registry type or NULL for an invalid public id. */
const insert_effect_type_t *insertFx_mixDescriptor(uint8_t type)
{
    return type < INSERT_MIX_COUNT ? insert_mix_types[type] : NULL;
}

const insert_effect_type_t *insertFx_transientDescriptor(uint8_t option)
{
    return option < INSERT_TRANSIENT_COUNT ? insert_transient_types[option]
                                           : NULL;
}

/*
 * Initialise manager state only; VoiceBuffer claims nothing here (D12).
 * Inputs: none. Outputs: D+S/rate defaults and inactive transient stages.
 * Caller: dsp_init() after fxbuf_init().
 */
void insertFx_init(void)
{
    uint8_t slot;

    voiceBuffer_init();
    memset(insert_slots, 0, sizeof(insert_slots));
    insert_trigger_mask = 0u;
    for (slot = 0u; slot < FXBUF_VOICE_SLOT_COUNT; slot++) {
        insert_slots[slot].mix_type = INSERT_MIX_DRIVE_SRR;
        insert_slots[slot].mix_pending = 0u;
        insert_slots[slot].mix_pending_type = INSERT_MIX_DRIVE_SRR;
        insert_slots[slot].transient_option = INSERT_TRANSIENT_RESONATOR;
        insert_slots[slot].transient_active = 0u;
        insert_slots[slot].transient_pending = 0u;
        insert_slots[slot].transient_pending_option =
            INSERT_TRANSIENT_RESONATOR;
        insert_slots[slot].transient_pending_active = 0u;
        insert_slots[slot].rate = 127u;
        insert_slots[slot].rate_shaped = 1.0f;
    }
}

/*
 * Select a Mix type, downgrading a two-unit family when its pair does not fit
 * (S084 VX3 VX3-5.1, C13).
 * Inputs: slot and persisted type id. Output: 1 when a non-buffer type
 * changed and the caller should arm declick; 0 for buffer/tail changes.
 */
uint8_t insertFx_selectMix(uint8_t slot, uint8_t type)
{
    const insert_effect_type_t *descriptor;
    uint8_t result;

    if (slot >= FXBUF_VOICE_SLOT_COUNT)
        return 0u;
    if (type >= INSERT_MIX_COUNT)
        type = INSERT_MIX_DRIVE_SRR;
    /* Remediation §18.4: a disabled (over-budget) type plays as D+S; the
    ** stored kit value is kept by the caller for a later re-enable. */
    if (!insert_enabled(insert_mix_types[type]))
        type = INSERT_MIX_DRIVE_SRR;
    descriptor = insert_mix_types[type];
    if ((type == INSERT_MIX_ECHO_2 || type == INSERT_MIX_ROOM_2) &&
        !voiceBuffer_fits(slot, VOICEBUF_STAGE_MIX, descriptor)) {
        type = (type == INSERT_MIX_ECHO_2) ? INSERT_MIX_ECHO : INSERT_MIX_ROOM;
        descriptor = insert_mix_types[type];
    }
    result = voiceBuffer_select(slot, VOICEBUF_STAGE_MIX, descriptor, type);
    if (result == VOICEBUF_SELECT_HELD) {
        insert_slots[slot].mix_pending = 1u;
        insert_slots[slot].mix_pending_type = type;
        return 0u;
    }
    insert_slots[slot].mix_type = type;
    insert_slots[slot].mix_pending = 0u;
    return (uint8_t)(result == VOICEBUF_SELECT_CHANGED &&
                      descriptor->units == 0u);
}

/*
 * Select the transient buffer option or release it when `active` is false
 * (S084 VX3 VX3-5.1, C6/C10). Inputs: slot, waveform option and active bit.
 * Output: none; the selection is held in the manager and VoiceBuffer record.
 */
void insertFx_selectTransient(uint8_t slot, uint8_t option, uint8_t active)
{
    const insert_effect_type_t *descriptor;
    uint8_t result;

    if (slot >= FXBUF_VOICE_SLOT_COUNT)
        return;
    if (option >= INSERT_TRANSIENT_COUNT)
        option = INSERT_TRANSIENT_RESONATOR;
    /* Remediation §18.4: a disabled option releases the stage (no buffer
    ** transient) instead of running over its cost cap. */
    if (active && !insert_enabled(insert_transient_types[option]))
        active = 0u;
    descriptor = active ? insert_transient_types[option]
                        : &insert_typeTransientNone;
    if (active && !voiceBuffer_fits(slot, VOICEBUF_STAGE_TRANSIENT,
                                    descriptor)) {
        /* A load-time conflict leaves the generator in BUF but releases the
         * stage; this is safer than retaining an unrelated old buffer use. */
        descriptor = &insert_typeTransientNone;
        active = 0u;
    }
    result = voiceBuffer_select(slot, VOICEBUF_STAGE_TRANSIENT, descriptor,
                                active ? option : VOICEBUF_PENDING_NONE);
    if (result == VOICEBUF_SELECT_HELD) {
        insert_slots[slot].transient_pending = 1u;
        insert_slots[slot].transient_pending_option = option;
        insert_slots[slot].transient_pending_active = (uint8_t)(active != 0u);
        return;
    }
    insert_slots[slot].transient_option = option;
    insert_slots[slot].transient_active = (uint8_t)(active != 0u);
    insert_slots[slot].transient_pending = 0u;
}

/* Store both raw and shaped Rate; D+S preserves the former mixer mapping. */
void insertFx_setRate(uint8_t slot, uint8_t value)
{
    if (slot >= FXBUF_VOICE_SLOT_COUNT)
        return;
    if (value > 127u)
        value = 127u;
    insert_slots[slot].rate = value;
    insert_slots[slot].rate_shaped = valueShaperI2F(value, -0.7f);
}

uint8_t insertFx_mixType(uint8_t slot)
{
    return slot < FXBUF_VOICE_SLOT_COUNT ? insert_slots[slot].mix_type : 0u;
}

uint8_t insertFx_mixIsBuffer(uint8_t slot)
{
    const insert_effect_type_t *type =
        insertFx_mixDescriptor(insertFx_mixType(slot));

    return (uint8_t)(type && type->units != 0u);
}

uint8_t insertFx_transientOption(uint8_t slot)
{
    return slot < FXBUF_VOICE_SLOT_COUNT
        ? insert_slots[slot].transient_option : 0u;
}

uint8_t insertFx_transientActive(uint8_t slot)
{
    return slot < FXBUF_VOICE_SLOT_COUNT
        ? insert_slots[slot].transient_active : 0u;
}

/*
 * Build a hidden mask from the exact VoiceBuffer fit query (D16/C13).
 * Inputs: slot. Output: bit t set when Mix type t cannot be applied now;
 * the current type is always visible.
 */
uint32_t insertFx_mixHiddenMask(uint8_t slot)
{
    uint32_t mask = 0u;
    uint8_t type;

    if (slot >= FXBUF_VOICE_SLOT_COUNT)
        return 0xFFFFFFFFu;
    for (type = 0u; type < INSERT_MIX_COUNT; type++)
        if (!insert_enabled(insert_mix_types[type]) ||
            (type != insert_slots[slot].mix_type &&
             !voiceBuffer_fits(slot, VOICEBUF_STAGE_MIX,
                               insert_mix_types[type])))
            mask |= 1u << type;
    return mask;
}

/*
 * Can a transient buffer option be applied now (D16; remediation §18.4)?
 * All options use one unit, so any enabled option answers for all; with no
 * enabled option `buf` is never offered.
 */
uint8_t insertFx_transientBufFits(uint8_t slot)
{
    uint8_t option;

    if (slot >= FXBUF_VOICE_SLOT_COUNT)
        return 0u;
    for (option = 0u; option < INSERT_TRANSIENT_COUNT; option++)
        if (insert_enabled(insert_transient_types[option]))
            return voiceBuffer_fits(slot, VOICEBUF_STAGE_TRANSIENT,
                                    insert_transient_types[option]);
    return 0u;
}

/* Nonzero when Mix type t may be selected (§18.4). */
uint8_t insertFx_mixDescriptorEnabled(uint8_t type)
{
    return (uint8_t)(type < INSERT_MIX_COUNT &&
                     insert_enabled(insert_mix_types[type]));
}

/* Bit o set when transient buffer option o is disabled (§18.4). */
uint32_t insertFx_transientDisabledMask(void)
{
    uint32_t mask = 0u;
    uint8_t option;

    for (option = 0u; option < INSERT_TRANSIENT_COUNT; option++)
        if (!insert_enabled(insert_transient_types[option]))
            mask |= 1u << option;
    return mask;
}

uint32_t insertFx_transientHiddenMask(uint8_t slot)
{
    uint32_t mask = 0u;

    if (slot >= FXBUF_VOICE_SLOT_COUNT)
        return 0xFFFFFFFFu;
    if (!insertFx_transientBufFits(slot) &&
        !(insert_slots[slot].transient_active &&
          insert_slots[slot].transient_option < INSERT_TRANSIENT_COUNT))
        mask |= 1u << 2;
    return mask;
}

const char *insertFx_mixName(uint8_t type)
{
    return type < INSERT_MIX_COUNT ? insert_mix_types[type]->name3 : "---";
}

const char *insertFx_transientName(uint8_t option)
{
    return option < INSERT_TRANSIENT_COUNT ? insert_transient_types[option]->name3
                                           : "---";
}

const char *insertFx_mixLabel(uint8_t type, uint8_t row, uint8_t long_label)
{
    const insert_effect_type_t *descriptor = insertFx_mixDescriptor(type);

    if (!descriptor || row >= 3u)
        return NULL;
    return long_label ? descriptor->label8[row] : descriptor->label3[row];
}

const char *insertFx_transientLabel(uint8_t option, uint8_t row,
                                    uint8_t long_label)
{
    const insert_effect_type_t *descriptor =
        insertFx_transientDescriptor(option);

    if (!descriptor || row >= 3u)
        return NULL;
    return long_label ? descriptor->label8[row] : descriptor->label3[row];
}

/*
 * Render the Mix stage and its tails (S084 VX3 VX3-5.1).
 * Inputs: slot, int16 block, stage distortion/amount. Outputs: block and the
 * pre-stage peak for buffer witness logic, or INSERT_WITNESS_OUTPUT.
 */
uint16_t insertFx_renderMix(uint8_t slot, int16_t *buf, uint8_t size,
                            const insert_stage_in_t *in)
{
    const insert_effect_type_t *type;
    insert_block_ctx_t ctx;
    uint32_t peak = 0u;
    uint8_t i;

    if (slot >= FXBUF_VOICE_SLOT_COUNT || !buf || !in)
        return INSERT_WITNESS_OUTPUT;
    type = insert_mix_types[insert_slots[slot].mix_type];
    ctx.ring = NULL;
    ctx.ring_len = 0u;
    ctx.wp = NULL;
    ctx.c0 = in->drive ? in->drive->shape / (2.0f + in->drive->shape) : 0.0f;
    ctx.c1 = insert_slots[slot].rate / 127.0f;
    ctx.c2 = in->amount / 127.0f;
    ctx.rate_shaped = insert_slots[slot].rate_shaped;
    ctx.drive = in->drive;
    ctx.trigger = 0u;
    if (type->units != 0u)
        for (i = 0u; i < size; i++) {
            uint32_t magnitude = (uint32_t)(buf[i] < 0 ? -buf[i] : buf[i]);

            if (magnitude > peak)
                peak = magnitude;
        }
    voiceBuffer_render(slot, VOICEBUF_STAGE_MIX, buf, size, &ctx);
    voiceBuffer_addTails(slot, buf, size);
    return type->units != 0u ? (uint16_t)peak : INSERT_WITNESS_OUTPUT;
}

/* Render a PRE or POST transient buffer stage at the engine call site. */
void insertFx_renderTransient(uint8_t slot, uint8_t placement, int16_t *buf,
                              uint8_t size, float vol, float pitch)
{
    const insert_effect_type_t *type;
    insert_block_ctx_t ctx;

    if (slot >= FXBUF_VOICE_SLOT_COUNT || !buf)
        return;
    if (!insert_slots[slot].transient_active) {
        /* Remediation R18: a trigger seen while the stage is off is dropped,
        ** so switching to buf later does not fire a stale trigger. */
        insert_trigger_mask &= (uint8_t)~(1u << slot);
        return;
    }
    type = insert_transient_types[insert_slots[slot].transient_option];
    if (type->placement != placement)
        return;
    ctx.ring = NULL; ctx.ring_len = 0u; ctx.wp = NULL;
    ctx.c0 = vol;
    /* Remediation R15: the P2 pitch factor spans 0.25..4.0 (byte 0..127),
    ** so c1 = (pitch - 0.25) / 3.75 covers the whole frq knob. */
    ctx.c1 = (pitch - 0.25f) * (1.0f / 3.75f);
    if (ctx.c1 < 0.0f) ctx.c1 = 0.0f;
    if (ctx.c1 > 1.0f) ctx.c1 = 1.0f;
    ctx.c2 = 0.0f;
    ctx.rate_shaped = 1.0f;
    ctx.drive = NULL;
    ctx.trigger = (uint8_t)((insert_trigger_mask >> slot) & 1u);
    voiceBuffer_render(slot, VOICEBUF_STAGE_TRANSIENT, buf, size, &ctx);
    insert_trigger_mask &= (uint8_t)~(1u << slot);
}

void insertFx_transientTrigger(uint8_t slot)
{
    if (slot < FXBUF_VOICE_SLOT_COUNT)
        insert_trigger_mask |= (uint8_t)(1u << slot);
}

/* Forward one foreground/render-block service tick to VoiceBuffer. */
void insertFx_service(void)
{
    uint8_t slot;

    voiceBuffer_service();
    /* Retry manager-held selections so its render state follows the record
     * immediately after a runner expires (S084 VX3 VX3-4.1, D1). */
    for (slot = 0u; slot < FXBUF_VOICE_SLOT_COUNT; slot++) {
        if (insert_slots[slot].mix_pending) {
            const uint8_t type = insert_slots[slot].mix_pending_type;
            const insert_effect_type_t *descriptor = insert_mix_types[type];
            const uint8_t result = voiceBuffer_select(
                slot, VOICEBUF_STAGE_MIX, descriptor, type);

            if (result != VOICEBUF_SELECT_HELD) {
                insert_slots[slot].mix_type = type;
                insert_slots[slot].mix_pending = 0u;
            }
        }
        if (insert_slots[slot].transient_pending) {
            const uint8_t option =
                insert_slots[slot].transient_pending_option;
            const uint8_t active =
                insert_slots[slot].transient_pending_active;
            const insert_effect_type_t *descriptor = active
                ? insert_transient_types[option] : &insert_typeTransientNone;
            const uint8_t result = voiceBuffer_select(
                slot, VOICEBUF_STAGE_TRANSIENT, descriptor,
                active ? option : VOICEBUF_PENDING_NONE);

            if (result != VOICEBUF_SELECT_HELD) {
                insert_slots[slot].transient_option = option;
                insert_slots[slot].transient_active = active;
                insert_slots[slot].transient_pending = 0u;
            }
        }
    }
}
