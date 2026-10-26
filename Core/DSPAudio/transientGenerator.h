/*
 * transientGenerator.h
 *
 * The transient generator makes a short attack click or burst at a trigger.
 * S084 VX2 gives it a type row: shaping (Snp/Ofs), the original PCM clicks,
 * installed samples and pitched stock-wave bursts.
 *
 *  Created on: 29.06.2012
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2013 Julian Schmidt
 *  Julian@sonic-potions.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the Sonic Potions LXR drumsynth firmware.
 *
 *  Redistribution and use of the LXR code or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions of source code modified from the original source must include the complete source
 *         code, including the source code for all components used by a binary built from the modified sources.
 *
 *       - Redistributions must reproduce the above copyright notice and this disclaimer.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef TRANSIENTGENERATOR_H_
#define TRANSIENTGENERATOR_H_

#include "stm32f4xx.h"
#include <stdint.h>
#include "transientTables.h"

/*
 * Transient types and the wav list (S084 VX2 VX2-6.1).
 *
 * What:       trs: non-PCM shaping, wav byte 0 = Snp (snap EG on pitch),
 *             1 = Ofs (phase offset). wav: byte 0..11 = the 12 firmware PCM
 *             transients, 12..12+S-1 = installed samples, then the 11 stock
 *             oscillator waves (Sin..swa) as 50 ms pitched bursts with a
 *             fixed decay; at most 256 entries (S <= 233). buf (2) is
 *             reserved for P3 and not selectable in P2.
 * Why:        plan §4.3.1, Q13-Q15: one type row fixes what automation and
 *             modulation of the wav row can reach; sample positions stay
 *             stable because the stock waves come after them.
 * Inputs:     the trt and transient_wave rows.
 * Outputs:    the resolved kind used by the engines and transient_calcBlock().
 * Accessors:  InstrumentManager.c writers, the four engines,
 *             InstrumentCellFormat.c.
 * Affiliates: transientTables.c (PCM), SampleMemory.h (samples, gain),
 *             StockWaves.h and Oscillator.h (bursts).
 */
#define TRANSIENT_TYPE_TRS          0u
#define TRANSIENT_TYPE_WAV          1u
#define TRANSIENT_TYPE_BUF          2u
#define TRANSIENT_TYPE_SELECTABLE   3u
#define TRANSIENT_STOCK_PCM_COUNT   NUM_TRANSIENTS      /* 12 */
#define TRANSIENT_WAV_LIST_MAX      256u
#define TRANSIENT_BURST_SAMPLES     TRANSIENT_SAMPLE_LENGTH   /* 2,205 = 50 ms */

#define TRANSIENT_KIND_NONE   0u
#define TRANSIENT_KIND_SNAP   1u
#define TRANSIENT_KIND_OFFSET 2u
#define TRANSIENT_KIND_PCM    3u
#define TRANSIENT_KIND_SAMPLE 4u
#define TRANSIENT_KIND_TABLE  5u   /* Tri Saw Rec sq3..swa bursts */
#define TRANSIENT_KIND_SINE   6u
#define TRANSIENT_KIND_NOISE  7u
#define TRANSIENT_KIND_CRASH  8u
#define TRANSIENT_KIND_BUFFER 9u

typedef struct TransientGeneratorStruct
{
	int16_t 	output;
	uint8_t		type;		/* TRANSIENT_TYPE_*  (VX2) */
	uint8_t		waveRequest;	/* unclamped wav byte (VX2, as OscInfo F2) */
	uint32_t	phase;		/* PCM/bursts: 2^32 per table; samples: Q20.12 */
	float	 	pitch;		/* 0.25 .. 1.75 from frq, unchanged */
	uint8_t		waveform;	/* live wav byte, clamped to the type */
	uint8_t		kind;		/* resolved TRANSIENT_KIND_* (VX2) */
	uint8_t		source;		/* PCM row, sample index or stock wave id */
	uint8_t		gainCode;	/* sample catalogue gain (VX2) */
	float		volume;
	uint16_t	midiFreq;	/* upper 8 bit coarse, lower 8 bit fine */
	uint16_t	burstLeft;	/* samples left in a stock burst (VX2) */
	uint32_t	dataAddr;	/* sample: absolute flash address (VX2) */
	uint32_t	frames;		/* sample frames (VX2) */
	uint32_t	generation;	/* catalogue generation at resolve (VX2) */
	float		env;		/* stock-burst envelope (VX2) */
} TransientGenerator;		/* 40 B (was 24 B) */

/*
 * Transient state API (S084 VX2 VX2-6.1).
 *
 * What:       init fills the defaults; trigger rewinds the chosen source;
 *             setType/setWave keep the request byte and clamp the live byte
 *             to the type's list; waveMax is the list maximum.
 * Why:        the P1 review fix F2 rule for type-dependent ranges: a type
 *             round trip keeps the chosen entry.
 * Inputs:     a generator, a type or wave byte.  Outputs: state.
 * Accessors:  InstrumentManager.c writers, the four engines' triggers,
 *             InstrumentCellFormat.c.
 * Affiliates: transient_resolve(), SampleMemory.h, StockWaves.h.
 */
void    transient_init(TransientGenerator* transient);
void    transient_trigger(TransientGenerator* transient);
void    transient_calcBlock(TransientGenerator* transient, int16_t* buf,
                            const uint8_t size);
void    transient_setType(TransientGenerator* transient, uint8_t type);
void    transient_setWave(TransientGenerator* transient, uint8_t wave);
uint8_t transient_waveMax(uint8_t type);

static inline uint8_t transient_isSnap(const TransientGenerator* t)
{ return (uint8_t)(t->kind == TRANSIENT_KIND_SNAP); }
static inline uint8_t transient_isOffset(const TransientGenerator* t)
{ return (uint8_t)(t->kind == TRANSIENT_KIND_OFFSET); }

#endif /* TRANSIENTGENERATOR_H_ */
