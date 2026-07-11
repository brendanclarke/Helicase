/*
 * lfo.h
 *
 *  Created on: 14.01.2013
 *  Modified on 17.05.2026 by Brendan Clarke
 *
 *      Non bandlimited low freq. oscillator
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


#ifndef LFO_H_
#define LFO_H_
//-------------------------------------------------------------
#include "stm32f4xx.h"
#include "Oscillator.h"
#include "wavetable.h"	//sine
#include "config.h"
// TODO DSP_PORT
// #include "sequencer.h"
#include "modulationNode.h"
//-------------------------------------------------------------
#define LFO_SINE 		0x00
#define LFO_TRI			0x01
#define LFO_SAW_UP		0x02
#define LFO_SAW_DOWN	0x03
#define LFO_REC			0x04
#define LFO_NOISE		0x05
#define LFO_EXP_UP		0x06
#define LFO_EXP_DOWN	0x07


#define LFO_MAX_F 		200 //[Hz]
#define LFO_SR 			(REAL_FS/(float)OUTPUT_DMA_SIZE)
//-------------------------------------------------------------
typedef struct LfoStruct
{
	uint32_t 	phase;		// the current phase of the LFO (upper bits cnt 0-255)
	uint32_t 	phaseInc;	// the phase increment controls the LFO frequency
	uint8_t 	waveform;	// selects the waveform
	uint8_t 	retrigger;	// defines the voice nr that retriggers the LFO (0=no retrigger)
	uint32_t 	phaseOffset;// the phase value to which the LFO is retriggered
	float 		rnd;
	uint8_t 	sync;
	float 		freq;
	ModulationNode modTarget;
	/*
	 * Second destination for the same LFO oscillator.
	 *
	 * modTarget and modTarget2 are not two independent LFOs. They share phase,
	 * waveform, rate, sync, offset, retrigger, and polarity, but each target
	 * owns its own amount, resolved runtime pointer, restore baseline, cached
	 * min/max range, and waveform-interpolation affiliation. Keeping these as
	 * two ModulationNode instances prevents one target selection from
	 * overwriting the other target's DSP pointer or baseline.
	 */
	ModulationNode modTarget2;
	/*
	 * Shared application polarity for both destinations.
	 *
	 * Inputs: descriptor/menu writes store a mod_node_polarity_t value here.
	 * Output: lfo_dispatchNextValue() passes it to ModulationNode so target
	 * shaping can use cached descriptor ranges. Polarity belongs to Lfo rather
	 * than each destination because the requested UI shape is one shared LFO
	 * with two destinations and independent amounts.
	 */
	uint8_t		polarity;
	float		modNodeValue;
} Lfo;
//-------------------------------------------------------------
void lfo_init(Lfo *lfo);
void lfo_dispatchNextValue(Lfo* lfo);
void lfo_setFreq(Lfo *lfo, float f);
void lfo_setSync(Lfo* lfo, uint8_t sync);
void lfo_recalcSync();
void lfo_retrigger(uint8_t voiceNr);

#endif /* LFO_H_ */
