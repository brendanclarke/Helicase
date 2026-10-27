/*
 * Snare.h
 *
 *  Created on: 16.04.2012
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


#ifndef SNARE_H_
#define SNARE_H_

#include "stm32f4xx.h"
#include "Oscillator.h"
#include "config.h"
#include "ResonantFilter.h"
#include "distortion.h"
#include "Decay.h"
#include "SlopeEg2.h"
#include "transientGenerator.h"
#include "BufferTools.h"
#include "lfo.h"
#include "snapEg.h"
#include "OscSection.h"

#define USE_PEAK 0
//------------------------------------------------------------------------
typedef struct SnareStruct
{
	OscInfo 	osc;		// the tonal oscillator
	OscInfo 	noiseOsc;	// the noise osc


	uint8_t		filterType; // bit 0 = lp, bit 1 = hp, bit 3 = bp on/off
	/*
	 * Channel volume 0..1 (descriptor instrument_vol / 127).
	 * Written by descriptor apply, LFO, Morph, and step automation; read only
	 * by instrumentManager_runtimeVolume() for the mixer, which applies it
	 * after decimation. Snare_calcSyncBlockVoice() leaves it unapplied so
	 * volume is the final shared mixer stage (S072 step 2).
	 */
	float	 	vol;
	//float		panL;		// [0:1]
	//float		panR;		// [0:1]
	uint8_t pan;
	//float 		panModifier;

	float   	mix;		// defines the mix between osc and noise [0 = 100% osc: 1 = 100% noise]
	float 		velo;

	int16_t		oscSample;
	int32_t		noiseSample;

#if USE_PEAK
	PeakFilter  filter;
#else
	ResonantFilter filter;
#endif

	DecayEg		oscPitchEg;
	float		egPitchModAmount;

	Lfo 		lfo;
	TransientGenerator transGen;

	SlopeEg2		oscVolEg;
	float 		egValueOscVol;
	/*
	 * Retrigger ramp state (S085 P2).
	 *
	 * What:       lastAmpGain = the amp gain (envelope x velocity when
	 *             volumeMod is on) the post-chain applied to the previous
	 *             block; ampRampArm = 1 for the one block after a fre/itp
	 *             trigger that found the voice still audible.
	 * Why:        that block ramps from the previous gain, so neither the
	 *             envelope restart nor a new velocity steps the output; all
	 *             other blocks use equal endpoints (rst unchanged).
	 * Writers:    Snare_triggerVoice() (arm), Snare_calcSyncBlockVoice()
	 *             (lastAmpGain every block; clears the arm),
	 *             Snare_initVoice().
	 * Readers:    Snare_calcSyncBlockVoice().
	 * Affiliates: voicePost_mixAddGainDist(), slopeEg2_isAudibleGain(); the
	 *             1,176 B slot _Static_assert in InstrumentManager.c.
	 */
	float lastAmpGain;
	uint8_t ampRampArm;

	Distortion distortion;
	SnapEg snapEg;

	uint8_t volumeMod;	//modulate volume by midi velocity if 1

	/*
	 * Oscillator section state (S085 C11.1).
	 *
	 * What:       run mode, the itp carrier snapshot and crossfade, and the
	 *             filter slew for this voice (OscSection.h).
	 * Why:        plan §4.3.7: run modes apply per voice; the snapshot holds
	 *             one carrier (about 140 B), inside the runtime-slot reserve.
	 * Inputs:     the run row (IM_SPECIAL_RUN_MODE); triggers; pool grants.
	 * Outputs:    used by this engine's trigger and render, and by
	 *             InstrumentManager (filter recalc, claims).
	 * Accessors:  this engine's .c, InstrumentManager.c.
	 * Affiliates: OscSection.c, OscRenderPool.c.
	 */
	OscSectionState section;
} SnareVoice;

/*
 * Pointer-based snare runtime entry points for tagged instrument slots.
 *
 * Inputs: a caller-owned SnareVoice instance plus the logical source slot for
 * trigger/LFO/velocity affiliation where needed. Outputs: the same DSP state
 * mutations on one explicit instance without coupling snare rendering to a
 * physical slot. InstrumentManager owns this instance as a tagged slot member;
 * callers must not retain it across a Scene/type reset.
 */
void Snare_initVoice(SnareVoice *voice);
void Snare_setPanVoice(SnareVoice *voice, const uint8_t pan);
void Snare_triggerVoice(SnareVoice *voice, const uint8_t source_slot,
                        const uint8_t vel, const uint8_t note);
void Snare_calcSyncBlockVoice(SnareVoice *voice, int16_t* buf,
                              const uint8_t size);
void Snare_calcAsyncVoice(SnareVoice *voice);
#endif /* SNARE_H_ */
