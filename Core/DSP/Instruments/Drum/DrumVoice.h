/*
 * DrumVoice.h
 *
 *  Created on: 03.04.2012
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


#ifndef DRUMVOICE_H_
#define DRUMVOICE_H_

#include "Oscillator.h"
#include "config.h"
#include "ResonantFilter.h"
#include "distortion.h"
#include "SlopeEg2.h"
#include "Decay.h"
#include "modulationNode.h"
#include "lfo.h"
#include "1PoleLp.h"
#include "transientGenerator.h"
#include "snapEg.h"
#include "dither.h"
#include "OscSection.h"
//------------------------------------------------------------------------
enum
{
	IS_PLAYING = 0x1,
};
//------------------------------------------------------------------------
typedef struct VoiceStruct
{
	OscInfo 	osc;		// the main fm oscillator
	OscInfo 	modOsc;		// the mod osc

	float		fmModAmount;

	/*
	 * Channel volume 0..1 (descriptor instrument_vol / 127).
	 * Written by descriptor apply, LFO, Morph, and step automation; read only
	 * by instrumentManager_runtimeVolume() for the mixer, which applies it
	 * after decimation. The render function does not apply it (S072 step 2).
	 */
	float	 	vol;
	float 		velo;
	uint8_t 	pan;
	//float 		panModifier;

	int16_t 	oscSample;

	DecayEg		oscPitchEg;
	float		egPitchModAmount;
	float 		offset;				//the start offset for the waveform - used to generate clicks

	TransientGenerator transGen;
	Lfo 		lfo;
	SlopeEg2	oscVolEg;
	/* S085 P2 F-A: the unused float egValueOscVol was removed here (Drum keeps
	** its envelope gain in ampFilterInput); its 4 B pay for lastFmMod, so the
	** P2 growth stays within the approved +8 B. */
	float 		volEgValueBlock[OUTPUT_DMA_SIZE];
	Distortion distortion;

#if ENABLE_DRUM_SVF
	ResonantFilter filter;
	uint8_t		filterType; // bit 0 = lp, bit 1 = hp, bit 3 = bp on/off
#endif

#ifdef USE_AMP_FILTER
	OnePoleFilter ampFilter;
#endif

#if (AMP_EG_SYNC==0)
	float ampFilterInput;
	float lastGain;
	/*
	 * Retrigger ramp state (S085 P2).
	 *
	 * What:       lastVeloGain = the velocity gain the post-chain applied to
	 *             the previous block (velo, or 1.0 with volumeMod off);
	 *             ampRampArm = 1 for the one block after a fre/itp trigger
	 *             that found the voice still audible.
	 * Why:        that block ramps the envelope gain (even at attack 0) and
	 *             the velocity gain from the previous block's values, so the
	 *             retrigger has no step. Every other block uses equal
	 *             endpoints (rst is unchanged).
	 * Writers:    Drum_triggerVoice() (arm), Drum_calcVoiceSyncBlock()
	 *             (lastVeloGain every block; clears the arm),
	 *             Drum_initVoice().
	 * Readers:    Drum_calcVoiceAsync(), Drum_calcVoiceSyncBlock().
	 * Affiliates: voicePost_drum(), slopeEg2_isAudibleGain(); the 1,176 B
	 *             slot _Static_assert in InstrumentManager.c.
	 */
	float lastVeloGain;
	uint8_t ampRampArm;
	/*
	 * FM depth of the previous block (S085 P2 F-A).
	 *
	 * What:       the carrier's fmMod (fmModAmount x pitch envelope) used by
	 *             the last rendered block.
	 * Why:        the armed retrigger block ramps the FM depth from this
	 *             value to the new one (oscSection_renderDrumRamped()), so
	 *             the pitch-envelope restart does not jump the carrier's
	 *             read phase in fre.
	 * Writers:    Drum_calcVoiceSyncBlock() (every block), Drum_initVoice().
	 * Readers:    Drum_calcVoiceSyncBlock().
	 * Affiliates: Drum_calcVoiceAsync() (osc.fmMod), ampRampArm.
	 */
	float lastFmMod;
#endif

#if ENABLE_MIX_OSC
	uint8_t mixOscs;
#endif

	float decimationCnt;
	float decimationRate;
	SnapEg snapEg;

	uint8_t volumeMod;	//modulate volume by midi velocity if 1

	Dither dither;
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
} DrumVoice;
//------------------------------------------------------------------------


/*
 * Pointer-based drum runtime entry points for tagged instrument slots.
 *
 * Inputs: a DrumVoice instance owned by the caller plus the logical source
 * slot for trigger/LFO/velocity affiliation where needed. Outputs: the same
 * DSP state changes on that explicit instance without requiring a permanent
 * fixed drum global. InstrumentManager owns the instance as the active member
 * of one tagged slot; callers must not retain it across a Scene/type reset.
 */
void Drum_initVoice(DrumVoice *voice, uint8_t seed_index);
void Drum_setPanVoice(DrumVoice *voice, const uint8_t pan);
void Drum_setPhaseVoice(DrumVoice *voice, const uint8_t phase);
void Drum_triggerVoice(DrumVoice *voice, const uint8_t source_slot,
                       const uint8_t vol, const uint8_t note);
void Drum_calcVoiceSyncBlock(DrumVoice *voice, int16_t* buf,
                             const uint8_t size);
void Drum_calcVoiceAsync(DrumVoice *voice, const uint8_t amp_eg_sync);

#endif /* DRUMVOICE_H_ */
