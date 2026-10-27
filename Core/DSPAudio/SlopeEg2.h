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
float slopeEg2_calc(SlopeEg2* eg);
void slopeEg2_setAttack(SlopeEg2* eg, uint8_t data2, uint8_t isSync);
void slopeEg2_setDecay(SlopeEg2* eg, uint8_t data2, uint8_t isSync);
void slopeEg2_setSlope(SlopeEg2* eg, uint8_t data2);
float slopeEg2_calcDecay(uint8_t data2);

/*
 * Smallest gain that still produces output (S085 P2).
 *
 * What:       one int16 output step, 1/32768. A voice whose last applied
 *             amp gain is below this is silent: its envelope has fully
 *             decayed for every practical purpose.
 * Why:        the fre/itp retrigger ramps only when something is left of the
 *             previous note (user, 2026-10-27). The test must use the applied
 *             gain, not the raw envelope value: at slope 127 a raw value of
 *             0.0001 still gives a gain of about 0.29, and a finished decay
 *             sits at raw 0 in EG_D, never EG_STOPPED.
 * Accessors:  slopeEg2_isAudibleGain().
 * Affiliates: instrumentManager_ampEnvelopeQuiet() (the Scene-swap test,
 *             which keeps its raw-value rule; not used here).
 */
#define SLOPEEG2_AUDIBLE_GAIN (1.0f / 32768.0f)

/*
 * Whether an applied amp gain is still audible (S085 P2).
 *
 * What:       1 when gain >= SLOPEEG2_AUDIBLE_GAIN, else 0.
 * Why:        arms the one-block retrigger ramp only when there is a level
 *             to ramp from.
 * Inputs:     the gain the engine applied to its last block (Drum
 *             ampFilterInput; Snare/Cymbal/HiHat egValueOscVol).
 * Outputs:    0 or 1.
 * Accessors:  the four engines' *_triggerVoice() in fre/itp.
 * Affiliates: slopeEg2_retriggerFrom(), each engine's ampRampArm.
 */
static inline uint8_t slopeEg2_isAudibleGain(const float gain)
{
	return (uint8_t)(gain >= SLOPEEG2_AUDIBLE_GAIN);
}

/*
 * Retrigger the attack from the gain the voice is producing (S085 P2).
 *
 * What:       sets the envelope to attack with its raw value chosen so the
 *             attack curve returns exactly gainNow:
 *             v = g / (1 + invSlope * (1 - g)), clamped to [0, 1].
 *             gainNow is clamped to [0, 1] first. In repeat mode it falls
 *             back to slopeEg2_trigger(), which starts at full by design.
 * Why:        slopeEg2_trigger() keeps the raw value but switches from the
 *             decay curve f(v, slope) to the attack curve f(v, invSlope), so
 *             the gain steps by up to the full range at a retrigger (half a
 *             decay at slope 96: 0.76 -> 0.24). Matching the gain removes
 *             the step at every attack setting above 0; at attack 0 the
 *             first calc reaches 1.0 and the engines' one-block ramp covers
 *             that jump. A fully decayed voice passes gain ~0 and gets a
 *             fresh attack.
 * Inputs:     eg (attack, invSlope, repeat), gainNow (the last applied
 *             gain).
 * Outputs:    eg->value, eg->state = EG_A (or EG_REPEAT), eg->repeatCnt.
 * Cost:       control rate, once per fre/itp trigger: one VDIV and a few
 *             compares.
 * Accessors:  Drum_triggerVoice(), Snare_triggerVoice(),
 *             Cymbal_triggerVoice(), HiHat_triggerVoice() when the run mode
 *             is fre or itp. rst keeps slopeEg2_trigger().
 * Affiliates: slopeEg2_calc() (the curve this inverts),
 *             slopeEg2_setSlope() (invSlope > -1 keeps the denominator
 *             positive), slopeEg2_isAudibleGain().
 */
void slopeEg2_retriggerFrom(SlopeEg2* eg, float gainNow);


#endif /* SLOPEEG2_H_ */
