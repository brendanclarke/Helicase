/*
 * transientGenerator.h
 *
 *  Created on: 29.06.2012
 *
 *      the transient generator generates a kind of 'click' oneshot sound when triggered
 *      a variety of different, very short [50 ms] transient samples is used
 *
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2013 Julian Schmidt
 *  Julian@sonic-potions.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the Sonic Potions LXR drumsynth firmware.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR code or any derivative works are permitted
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


#ifndef TRANSIENTGENERATOR_H_
#define TRANSIENTGENERATOR_H_


#include "stm32f4xx.h"
#include <stdint.h>
#include "transientTables.h"

typedef struct TransientGeneratorStruct
{
	int16_t 	output;
	uint32_t	phase;		// the current phase of the osc
	float	 	pitch;		// we need no accurate tuning for a transient, so we use a pitch factor [0.25f:1.75f]
	uint8_t		waveform;	// the selected transient table of the osc
	float		volume;

	//for midi
	uint16_t	midiFreq;  //upper 8 bit coarse, lower 8 bit fine
} TransientGenerator;


void transient_init(TransientGenerator* transient);

void transient_trigger(TransientGenerator* transient);

void transient_calc(TransientGenerator* transient);
void transient_calcBlock(TransientGenerator* transient, int16_t* buf, const uint8_t size);

void transient_setWaveform(TransientGenerator* transient, const uint8_t waveform);

/*
 * Transient list layout with user one-shot samples (S087 P3).
 *
 * What:       the transient wav list is 0 Snp (snap EG), 1 Ofs (offset),
 *             2..TRANSIENT_USER_FIRST-1 the built-in PCM clicks
 *             (transientData rows 0..NUM_TRANSIENTS-1), then one entry per
 *             installed sample, shown s01, s02, ... (one-shot only: no loop
 *             or wavetable entries). TRANSIENT_USER_PHASE_SHIFT is the Q19.12
 *             phase of a sample entry (the built-in rows use Q20).
 * Why:        S087 P3: the transient lookup gains the user one-shots the way
 *             the modulator wav list does (built-in entries first, then sNN).
 * Accessors:  transient_calcBlock(), transient_entryCount(), menu.c (count,
 *             names), transient_setWaveform().
 * Affiliates: MenuText.h transientNames (its count must equal
 *             TRANSIENT_USER_FIRST); SampleMemory.h.
 */
#define TRANSIENT_USER_FIRST        (NUM_TRANSIENTS + 2u)
#define TRANSIENT_USER_PHASE_SHIFT  12u

/*
 * Number of transient wav entries (S087 P3).
 *
 * What:       TRANSIENT_USER_FIRST + the installed sample count, at most 128
 *             (the row stores 0..127).
 * Why:        the menu clamp, the automation range and the display must agree
 *             with what the generator can play.
 * Inputs:     the catalogue.  Outputs: the entry count (at least 14).
 * Accessors:  menu.c (clamp, automation max, names).
 * Affiliates: sampleMemory_getNumSamples().
 */
uint8_t transient_entryCount(void);
#endif /* TRANSIENTGENERATOR_H_ */
