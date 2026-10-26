/*
 * VoiceBuffer.h
 *
 *  Created on: 26.10.2026
 * ----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * ----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ----------------------------------------------------------------------------
 */

#ifndef VOICE_BUFFER_H_
#define VOICE_BUFFER_H_

#include <stdint.h>
#include "FxBuffer.h"
#include "InsertEffect.h"

/*
 * VoiceBuffer — per-slot, per-stage insert records (S084 VX3 VX3-4.1).
 *
 * What: owns 12 active records and two lease-backed tail runners, all unit
 * reservations, same-use continuation, different-use tailing, Hold overflow,
 * parameter state, and the foreground apply pass. Why: one owner controls
 * audio lifetime and FxBuffer notifications, so an option change never cuts a
 * ringing buffer. Inputs: manager selections and render blocks. Outputs:
 * processed stage blocks and released/acquired units. Affiliates: FxBuffer,
 * InsertEffect.h and InsertEffectsManager.c.
 */
#define VOICEBUF_ACTIVE          12u
#define VOICEBUF_RUNNERS         2u
#define VOICEBUF_RECORDS         (VOICEBUF_ACTIVE + VOICEBUF_RUNNERS)
#define VOICEBUF_STAGE_MIX       0u
#define VOICEBUF_STAGE_TRANSIENT 1u
#define VOICEBUF_SELECT_SAME     0u
#define VOICEBUF_SELECT_CHANGED  1u
#define VOICEBUF_SELECT_HELD     2u

#define VOICEBUF_FLAG_ACTIVE     0x01u
#define VOICEBUF_FLAG_TAIL       0x02u
#define VOICEBUF_FLAG_FADING     0x04u
#define VOICEBUF_FLAG_EXPIRED    0x08u
#define VOICEBUF_FLAG_PRIMED     0x10u
/* Remediation R5: unit 0/1 of the ring still holds a previous owner's bytes;
** cleared on the record's first render, after the pass's share callback. */
#define VOICEBUF_FLAG_CLEAR0     0x20u
#define VOICEBUF_FLAG_CLEAR1     0x40u
#define VOICEBUF_FLAG_CARRY      (VOICEBUF_FLAG_PRIMED | VOICEBUF_FLAG_CLEAR0 | \
                                  VOICEBUF_FLAG_CLEAR1)
#define VOICEBUF_PENDING_NONE    0xFFu
#define VOICEBUF_SILENCE_PEAK    64u
#define VOICEBUF_TAIL_FADE_BLOCKS 28u
#define VOICEBUF_TAIL_SILENT_BLOCKS 69u
#define VOICEBUF_TAIL_AGE_BLOCKS 4824u

_Static_assert(VOICEBUF_RUNNERS * 2u == FXBUF_LEASE_UNIT_MAX,
               "lease cap = 2 units per runner (D2)");

void voiceBuffer_init(void);
void voiceBuffer_beginPass(void);
void voiceBuffer_endPass(void);
uint8_t voiceBuffer_select(uint8_t slot, uint8_t stage,
                           const insert_effect_type_t *type, uint8_t type_id);
uint8_t voiceBuffer_fits(uint8_t slot, uint8_t stage,
                         const insert_effect_type_t *type);
void voiceBuffer_service(void);
uint8_t voiceBuffer_runnerFree(void);
void voiceBuffer_render(uint8_t slot, uint8_t stage, int16_t *buf,
                        uint8_t size, insert_block_ctx_t *ctx);
void voiceBuffer_addTails(uint8_t slot, int16_t *buf, uint8_t size);

#if DEV_MODE_DIAGNOSTIC
void voiceBuffer_devDump(char row1[17], char row2[17]);
#endif

#endif /* VOICE_BUFFER_H_ */
