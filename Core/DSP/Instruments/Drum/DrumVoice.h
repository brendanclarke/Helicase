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
//------------------------------------------------------------------------
enum
{
	IS_PLAYING = 0x1,
};
/*
 * Drum osc2 mod modes: the stored values of osc2_mod_type (S085 P1).
 *
 * What:       DRUM_MOD_FM  0  the carrier phase-modulated by the modulator;
 *                             FM index ~ amt^2 x env;
 *             DRUM_MOD_MIX 1  carrier x (1 - amt) + modulator x amt;
 *             DRUM_MOD_SNC 2  the carrier hard-synced to a phase clock at
 *                             the FM-page frequency, sync depth d; only
 *                             the carrier is heard;
 *             DRUM_MOD_RNG 3  carrier x (1 - d) + carrier x modulator x d,
 *                             d ramped per sample (snapped after a trigger).
 *             d = amt^2 x env (Drum_modEnvDepth()), env = the PitchMod
 *             decay envelope: fm, snc and rng share one response to amt
 *             and the envelope (S085 P1 follow-up). mix uses amt alone.
 * Why:        0 and 1 keep their stored meaning, so old kits load unchanged;
 *             2 and 3 are appended (plan K1). Any other byte renders as fm
 *             (plan Q7).
 * Accessors:  DrumVoice.modMode; Drum_calcVoiceSyncBlock(); Drum_initVoice().
 * Affiliates: DrumParameters.c row osc2_mod_type (DTYPE_MIX_FM); MenuText.h
 *             mixFmNames (same order, count = DRUM_MOD_COUNT); menu.c
 *             menu_getMixFmName().
 */
#define DRUM_MOD_FM    0u
#define DRUM_MOD_MIX   1u
#define DRUM_MOD_SNC   2u
#define DRUM_MOD_RNG   3u
#define DRUM_MOD_COUNT 4u
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
	float 		egValueOscVol;
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
#endif

#if ENABLE_MIX_OSC
	/*
	 * osc2 mod mode, DRUM_MOD_FM .. DRUM_MOD_RNG (S085 P1; was the boolean
	 * mixOscs).
	 *
	 * What:       written by the descriptor row osc2_mod_type (TYPE_UINT8)
	 *             from the menu, Morph, step automation and MIDI; never by
	 *             an LFO (MOD_NONE). Read once per block by
	 *             Drum_calcVoiceSyncBlock()'s switch.
	 * Why:        renamed because the byte now holds four modes. A boolean
	 *             test (the old if(voice->mixOscs)) would play snc and rng
	 *             as mix.
	 * Accessors:  Drum_initVoice(), Drum_calcVoiceSyncBlock(), the
	 *             descriptor binding (DrumParameters.c).
	 */
	uint8_t modMode;
#endif

	/*
	 * rng depth snap request (S085 P1 follow-up).
	 *
	 * What:       set to 1 by Drum_triggerVoice(); the next rng block starts
	 *             its depth ramp at that block's own depth (no ramp up from
	 *             the previous hit's tail) and clears it.
	 * Why:        user choice "snap": a hit starts at full ring depth, as fm
	 *             starts at full FM depth. The carrier phase is reset on the
	 *             same trigger, so the jump adds no new discontinuity. When
	 *             the oscillator run modes return, the free-run mode is to
	 *             ramp always instead (user, for trial then).
	 * Accessors:  Drum_triggerVoice() (sets), Drum_calcVoiceSyncBlock() (rng:
	 *             reads and clears), Drum_initVoice().
	 * Layout:     sits in the padding after modMode (no size change).
	 */
	uint8_t ringSnap;

	/*
	 * snc phase clock (S085 P1, deviation D2).
	 *
	 * What:       the clock that resets the carrier in snc. It is advanced by
	 *             osc_buildSyncList() once per snc block, at
	 *             osc_syncClockInc(&modOsc) per sample. Zeroed by
	 *             Drum_initVoice(). It is not reset on a trigger: like
	 *             modOsc, it runs free (user, plan Q6b).
	 * Why:        modOsc is not rendered in snc. Advancing modOsc.phase
	 *             instead would push a one-shot user-sample phase past the
	 *             sample's end, and a later fm/mix render would read beyond
	 *             the sample (and, near the top of sample flash, beyond
	 *             flash).
	 * Inputs/Outputs: the 32-bit clock phase (2^32 per nominal period).
	 * Accessors:  Drum_calcVoiceSyncBlock(), Drum_initVoice().
	 * Affiliates: Oscillator.h osc_syncClockInc() / osc_buildSyncList();
	 *             InstrumentManager.c runtime-slot reserve (2 x
	 *             sizeof(DrumVoice), so +8 B per slot).
	 */
	uint32_t syncClock;

	/*
	 * rng depth ramp state (S085 P1 follow-up).
	 *
	 * What:       the ring depth d (Q15, 0..32768) reached at the end of the
	 *             last rng block. The next rng block ramps from here to its
	 *             own d = osc_ringDepthQ15(Drum_modEnvDepth()) across its
	 *             samples, then stores the value it reached.
	 * Why:        a d stepped once per block was audible as a zipper once rng
	 *             followed the mod envelope ("crunchy"). Storing the reached
	 *             value (not the target) means integer rounding of the step
	 *             never accumulates.
	 * Inputs/Outputs: Q15 depth.
	 * Accessors:  Drum_calcVoiceSyncBlock() (rng), Drum_initVoice().
	 * Affiliates: Oscillator.h calcNextOscSampleRingBlock() (depthQ15,
	 *             depthStepQ15); ringSnap; InstrumentManager.c runtime-slot
	 *             reserve (2 x sizeof(DrumVoice): +8 B per slot).
	 */
	int32_t ringDepthQ15;

	float decimationCnt;
	float decimationRate;
	SnapEg snapEg;

	uint8_t volumeMod;	//modulate volume by midi velocity if 1

	/*
	 * Oscillator run mode (S088 P0 §2.4).
	 *
	 * What:       0 rst (today), 1 fre (free-run), 2 itp (interpolate);
	 *             OSC_RUN_* in Oscillator.h. Written by IM_SPECIAL_OSC_RUN
	 *             through the osc1_run descriptor row.
	 * Why:        the trigger and render functions branch on this byte to
	 *             select phase behaviour, the gain-matched retrigger and the
	 *             carrier crossfade. It is a selector: never morphed, no LFO
	 *             or velocity target, one step-automation value per step.
	 * Inputs:     instrumentManager_writeSpecialRuntime() via the descriptor.
	 * Outputs:    read by Drum_triggerVoice() and Drum_calcVoiceSyncBlock().
	 * Accessors:  Drum_initVoice() (0), the descriptor writer, trigger, render.
	 * Affiliates: OSC_RUN_*; the osc1_run row (DrumParameters.c);
	 *             InstrumentManager.c runtime-slot reserve (2 x sizeof(DrumVoice)).
	 * Layout:     after volumeMod, before the Dither tail: grows the struct by
	 *             1 B plus alignment.
	 */
	uint8_t runMode;

	/*
	 * Amp-ramp armed flag (S088 P0 §3 Layer 2a).
	 *
	 * What:       1 when the last trigger was a fre/itp retrigger into an
	 *             audible tail; the next render ramps the post-chain endpoints
	 *             and the FM depth from their previous values, then clears it.
	 *             Cleared by every trigger before the mode branch, so a rst
	 *             hit can never inherit a stale arm.
	 * Why:        the render functions need one bit to know that the previous
	 *             block's gain/velocity/depth are the correct ramp starts.
	 * Inputs:     Drum_triggerVoice().
	 * Outputs:    read/cleared by Drum_calcVoiceSyncBlock().
	 * Affiliates: voicePost_drum(); lastVeloGain; lastFmDepth.
	 */
	uint8_t ampRampArm;

	/*
	 * Previous-block velocity gain (S088 P0 §3 Layer 2b).
	 *
	 * What:       the velocity gain the post-chain actually used on the last
	 *             block (volumeMod ? velo : 1.0f). Updated after each
	 *             voicePost_drum() call.
	 * Why:        voice->velo is overwritten by the next trigger; keeping the
	 *             value that was rendered lets the armed block ramp from it.
	 * Inputs:     Drum_calcVoiceSyncBlock() after voicePost_drum().
	 * Outputs:    passed as voicePost_drum()'s lastVeloGain when armed.
	 * Affiliates: voicePost_drum(); ampRampArm; Drum_initVoice() (1.0f).
	 */
	float lastVeloGain;

	/*
	 * itp old-carrier snapshot (S088 P0 §3 Layer 3a).
	 *
	 * What:       the frozen carrier state filled on an itp retrigger while the
	 *             amp gain is audible and an interpolation slot is free.
	 *             active = 0 leaves it inert (no cost, no slot).
	 * Why:        oscSection_renderOldCarrier() plays this copy beside the new
	 *             carrier for 132 samples (OSC_RUN_ITP_SAMPLES) and blends.
	 * Inputs:     Drum_triggerVoice().
	 * Outputs:    Drum_calcVoiceSyncBlock() (blend); the mixer's slot count.
	 * Layout:     OscCarrierSnapshot is 28 B on ARM; the four engine structs
	 *             each carry one whether or not they use itp.
	 * Affiliates: OscCarrierSnapshot (Oscillator.h); OSC_RUN_ITP_*;
	 *             instrumentManager_slotHasActiveItpFade().
	 */
	OscCarrierSnapshot snapshot;

	/*
	 * Previous-block FM depth (S088 P0 §3 Layer 2e, D1).
	 *
	 * What:       voice->osc.fmMod (fmModAmount x the PitchMod decay envelope)
	 *             from the block that just rendered.
	 * Why:        the FM depth ramp starts the modulator block at
	 *             lastFmDepth / fmMod and moves to 1 across the armed block, so
	 *             the depth change of a retrigger glides instead of stepping.
	 *             On every other block the start ratio is exactly 1, so the
	 *             multiply is the identity and rst stays bit-identical.
	 * Inputs:     Drum_calcVoiceSyncBlock() (fm branch), after the ramp.
	 * Outputs:    the next fm block's ramp start.
	 * Affiliates: the FM depth ramp; ampRampArm; Drum_calcVoiceAsync().
	 */
	float lastFmDepth;

	Dither dither;
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
