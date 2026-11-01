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

	Distortion distortion;
	SnapEg snapEg;

	uint8_t volumeMod;	//modulate volume by midi velocity if 1

	/*
	 * Oscillator run mode (S088 P0 §2.4).
	 *
	 * What:       0 rst, 1 fre, 2 itp (OSC_RUN_*); stored by the osc1_run
	 *             descriptor row through IM_SPECIAL_OSC_RUN.
	 * Why:        Snare_triggerVoice() and Snare_calcSyncBlockVoice() branch
	 *             on it for the phase reset, the gain-matched amp retrigger
	 *             and the itp carrier crossfade.
	 * Inputs:     the descriptor writer.  Outputs: read by trigger and render.
	 * Accessors:  Snare_initVoice() (0), the writer, trigger, render.
	 * Affiliates: OSC_RUN_* (Oscillator.h); the osc1_run row
	 *             (SnareParameters.c); InstrumentManager.c runtime-slot reserve.
	 */
	uint8_t runMode;

	/*
	 * Amp-ramp armed flag / previous-block compound amp gain (S088 P0 §3 2a/2b).
	 *
	 * What:       ampRampArm = 1 when the last trigger was a fre/itp retrigger
	 *             into an audible tail; lastAmpGain is the ampGain
	 *             (velo x egValueOscVol, or egValueOscVol) actually used on the
	 *             previous block.
	 * Why:        the armed render ramps the post-chain amp gain from
	 *             lastAmpGain to the new ampGain; every other block passes
	 *             lastAmpGain == ampGain, which keeps the rst output
	 *             bit-identical. velo itself is overwritten by the trigger, so
	 *             the rendered value must be retained.
	 * Inputs:     Snare_triggerVoice() (arm), Snare_calcSyncBlockVoice()
	 *             (update).  Outputs: voicePost_mixAddGainDist() endpoints.
	 * Affiliates: voicePostChain.h; Snare_initVoice() (0u / 0.0f).
	 */
	uint8_t ampRampArm;

	/*
	 * Voice chain selector (S089 P2).
	 *
	 * What:       VOICE_CHAIN_DEFAULT (0, T+D: transient + filter drive +
	 *             distortion, S089 P0 behaviour) or VOICE_CHAIN_WAVEFOLDER
	 *             (1, WFd: a wavefolder in place of the transient). Written by
	 *             IM_SPECIAL_VOICE_CHAIN through the voice_chain row, which
	 *             clamps to VOICE_CHAIN_COUNT - 1.
	 * Why:        the sync render, the trigger and the async calc branch on it
	 *             once per block/trigger; under WFd the transient_* rows and
	 *             transGen/snapEg state belong to the fold (voiceFold.h). A
	 *             selector: never morphed, no LFO/velocity target, one
	 *             step-automation value per step.
	 * Inputs:     instrumentManager_writeSpecialRuntime() via the descriptor.
	 * Outputs:    read by Snare_triggerVoice(), Snare_calcAsyncVoice(),
	 *             Snare_calcSyncBlockVoice(); instrumentManager_slotVoiceChain().
	 * Layout:     after ampRampArm, in existing alignment padding (Drum 571,
	 *             Snare 419, Cymbal 471); HiHat grows 516 -> 520 B, still
	 *             under the 2 x sizeof(DrumVoice) slot reserve, so
	 *             runtime_slots is unchanged (7,584 B).
	 * Affiliates: VOICE_CHAIN_* (voiceFold.h); the voice_chain rows;
	 *             InstrumentManager.c slot-reserve _Static_asserts.
	 */
	uint8_t chainType;
	float lastAmpGain;

	/*
	 * itp old-carrier snapshot (S088 P0 §3 Layer 3a).
	 *
	 * What:       the frozen carrier state filled on an itp retrigger while the
	 *             amp gain is audible and a slot is free; inert when active = 0.
	 * Why:        oscSection_renderOldCarrier() blends it into the new carrier.
	 * Inputs:     Snare_triggerVoice().
	 * Outputs:    Snare_calcSyncBlockVoice(); the mixer's slot count.
	 * Affiliates: OscCarrierSnapshot; OSC_RUN_ITP_SAMPLES;
	 *             instrumentManager_slotHasActiveItpFade().
	 */
	OscCarrierSnapshot snapshot;

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
