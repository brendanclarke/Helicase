/*
 * InsertEffectsManager.h
 *
 *  Created on: 26.10.2026
 * ----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * ----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ----------------------------------------------------------------------------
 */

#ifndef INSERT_EFFECTS_MANAGER_H_
#define INSERT_EFFECTS_MANAGER_H_

#include <stdint.h>
#include "InsertEffect.h"

/*
 * InsertEffectsManager contract (S084 VX3 VX3-5.1).
 *
 * What: owns the immutable Mix/transient registries, per-slot selection and
 * D+S rate state, and the foreground/render entry points. Why: Instrument
 * code hands in only slot values, distortion and stage blocks; the insert
 * layer stays independent of Scene, Menu and InstrumentManager storage.
 * Inputs: option writes and per-block stage contexts. Outputs: processed
 * blocks, hidden-option masks and labels. Affiliates: VoiceBuffer.c,
 * InsertEffect.h, the instrument engines and Menu cell formatting.
 */
#define INSERT_MIX_COUNT        12u
#define INSERT_TRANSIENT_COUNT  7u
#define INSERT_WITNESS_OUTPUT   0xFFFFu

enum {
    INSERT_MIX_DRIVE_SRR = 0u,
    INSERT_MIX_FOLD,
    INSERT_MIX_CRUSH,
    INSERT_MIX_RING,
    INSERT_MIX_PHASER,
    INSERT_MIX_FLANGER,
    INSERT_MIX_CHORUS,
    INSERT_MIX_ECHO,
    INSERT_MIX_ECHO_2,
    INSERT_MIX_COMB,
    INSERT_MIX_ROOM,
    INSERT_MIX_ROOM_2
};

enum {
    INSERT_TRANSIENT_RESONATOR = 0u,
    INSERT_TRANSIENT_PLUCK,
    INSERT_TRANSIENT_TUBE,
    INSERT_TRANSIENT_FLAM,
    INSERT_TRANSIENT_STUTTER,
    INSERT_TRANSIENT_REVERSE,
    INSERT_TRANSIENT_SPRING
};

/* O3 registry symbols; type implementations own their state and ops. */
extern const insert_effect_type_t insert_typeDriveSrr;
extern const insert_effect_type_t insert_typeFold;
extern const insert_effect_type_t insert_typeCrush;
extern const insert_effect_type_t insert_typeRing;
extern const insert_effect_type_t insert_typePhaser;
extern const insert_effect_type_t insert_typeFlanger;
extern const insert_effect_type_t insert_typeChorus;
extern const insert_effect_type_t insert_typeEcho;
extern const insert_effect_type_t insert_typeEcho2;
extern const insert_effect_type_t insert_typeComb;
extern const insert_effect_type_t insert_typeRoom;
extern const insert_effect_type_t insert_typeRoom2;
extern const insert_effect_type_t insert_typeResonator;
extern const insert_effect_type_t insert_typePluck;
extern const insert_effect_type_t insert_typeTube;
extern const insert_effect_type_t insert_typeFlam;
extern const insert_effect_type_t insert_typeStutter;
extern const insert_effect_type_t insert_typeReverse;
extern const insert_effect_type_t insert_typeSpring;

typedef struct {
    const Distortion *drive;
    uint8_t amount;
} insert_stage_in_t;

void insertFx_init(void);
uint8_t insertFx_selectMix(uint8_t slot, uint8_t type);
void insertFx_selectTransient(uint8_t slot, uint8_t option, uint8_t active);
void insertFx_setRate(uint8_t slot, uint8_t value);
uint8_t insertFx_mixType(uint8_t slot);
uint8_t insertFx_mixIsBuffer(uint8_t slot);
uint8_t insertFx_transientOption(uint8_t slot);
uint8_t insertFx_transientActive(uint8_t slot);
uint32_t insertFx_mixHiddenMask(uint8_t slot);
uint32_t insertFx_transientHiddenMask(uint8_t slot);
uint8_t insertFx_transientBufFits(uint8_t slot);
uint32_t insertFx_transientDisabledMask(void);
uint8_t insertFx_mixDescriptorEnabled(uint8_t type);
const char *insertFx_mixName(uint8_t type);
const char *insertFx_transientName(uint8_t option);
const char *insertFx_mixLabel(uint8_t type, uint8_t row, uint8_t long_label);
const char *insertFx_transientLabel(uint8_t option, uint8_t row,
                                    uint8_t long_label);
const insert_effect_type_t *insertFx_mixDescriptor(uint8_t type);
const insert_effect_type_t *insertFx_transientDescriptor(uint8_t option);
uint16_t insertFx_renderMix(uint8_t slot, int16_t *buf, uint8_t size,
                            const insert_stage_in_t *in);
void insertFx_renderTransient(uint8_t slot, uint8_t placement, int16_t *buf,
                              uint8_t size, float vol, float pitch);
void insertFx_transientTrigger(uint8_t slot);
void insertFx_service(void);

#endif /* INSERT_EFFECTS_MANAGER_H_ */
