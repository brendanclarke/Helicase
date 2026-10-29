/*
 * SlopeEg2.h
 *
 *  Created on: 16.06.2012
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


#ifndef SLOPEEG2_H_
#define SLOPEEG2_H_



#include "stm32f4xx.h"


#define EG_STOPPED 	0
#define EG_A		1
#define EG_D		2
#define EG_REPEAT	3

typedef struct SLOPE_EG2_Struct
{
	float 	attack;
	float 	decay;
	float 	slope;
	float 	invSlope;
	uint8_t repeat; 				/**<number of repetitions of the attack phase*/
	uint8_t repeatCnt;				/**<a counter for the already played repeats*/
	float 	value;
	uint8_t state;

} SlopeEg2;


void slopeEg2_init(SlopeEg2* eg);
void slopeEg2_trigger(SlopeEg2* eg);
/*
 * Gain-matched retrigger (S088 P0 §3 Layer 2a).
 *
 * What:       restarts the attack from a given output gain instead of from
 *             the envelope's current raw value. The raw value v is the
 *             inverse of the attack curve g = (1 + k) v / (1 + k v), i.e.
 *             v = g / (1 + k (1 - g)) with k = invSlope. The state becomes
 *             EG_A and the repeat counter is cleared. k is always > -1
 *             (k = 2a / (1 - a) for the net slope amount a in (-1, 1)), so the
 *             denominator stays positive for every g in [0, 1] and the
 *             inversion covers the whole slope knob.
 *             When repeat mode is
 *             active (eg->repeat != 0) the call falls back to
 *             slopeEg2_trigger(), because the repeat leg enters at 1.0 and has
 *             no meaningful attack entry point (A10).
 * Why:        in fre/itp a retrigger into an audible tail must not click: the
 *             attack starts where the old decay left off, and the one-block
 *             post-chain ramp covers the remaining rounding step.
 * Inputs:     eg pointer, currentGain = the shaped output gain (0..1) from the
 *             last block (Drum: ampFilterInput; Snare/Cymbal/HiHat:
 *             egValueOscVol).
 * Outputs:    eg->value, eg->state, eg->repeatCnt.
 * Accessors:  Drum/Snare/Cymbal/HiHat _triggerVoice() when runMode is not rst
 *             and slopeEg2_isAudibleGain() accepts the gain.
 * Affiliates: slopeEg2_calcSlopeValue() (the forward curve);
 *             voicePostChain.h (the armed-block ramp).
 */
void slopeEg2_retriggerFrom(SlopeEg2* eg, float currentGain);
/*
 * Audible-gain test (S088 P0 §3 Layer 2a).
 *
 * What:       1 when gain >= 1/32768 (one int16 LSB), else 0.
 * Why:        below that threshold a fre/itp retrigger behaves exactly as rst
 *             (D6/A3): full phase reset, filter clear, slopeEg2_trigger().
 *             It also keeps the curve inversion away from g = 0.
 * Inputs:     gain (the shaped output gain, 0..1).
 * Outputs:    0 or 1.
 * Accessors:  the four engine trigger functions.
 * Affiliates: slopeEg2_retriggerFrom(); the engine ampRampArm flags.
 */
static inline uint8_t slopeEg2_isAudibleGain(float gain)
{
	return (gain >= (1.f / 32768.f)) ? 1u : 0u;
}
float slopeEg2_calc(SlopeEg2* eg);
void slopeEg2_setAttack(SlopeEg2* eg, uint8_t data2, uint8_t isSync);
void slopeEg2_setDecay(SlopeEg2* eg, uint8_t data2, uint8_t isSync);
void slopeEg2_setSlope(SlopeEg2* eg, uint8_t data2);
float slopeEg2_calcDecay(uint8_t data2);


#endif /* SLOPEEG2_H_ */
