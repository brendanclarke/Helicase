/*
 * DrumVoice.c
 *
 *  Created on: 03.04.2012
 *  Modified on 17.05.2026 by Brendan Clarke
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


#include "DrumVoice.h"
#include "Oscillator.h"
#include "random.h"
#include "math.h"
#include "squareRootLut.h"
#include "BufferTools.h"
#include <stdbool.h>
#include "ParameterArray.h"
#include "modulationNode.h"
#include "InstrumentManager.h"
#include "voicePostChain.h"
#include "OscillatorSample.h"
#include "OscRenderPool.h"
// TODO DSP_PORT
// #include "TriggerOut.h"


#ifdef USE_AMP_FILTER
INCCM static float ampSmoothValue = 0.1f;
#endif
//---------------------------------------------------
void Drum_setPanVoice(DrumVoice *voice, const uint8_t pan)
{
	/*
	 * Set pan on an explicit drum runtime instance.
	 *
	 * Inputs: caller-owned DrumVoice and raw 0..127 pan. Output: the instance
 * pan field changes; mixer owns translating that byte through square-root
 * gain tables. InstrumentManager supplies the active tagged-slot member, so
 * this engine module never resolves a slot through hidden global storage.
	 */
	if(!voice)
		return;
	voice->pan = pan;
}
//---------------------------------------------------
void Drum_setPhaseVoice(DrumVoice *voice, const uint8_t phase)
{
	const uint32_t startPhase = (phase/127.f)*0xffffffff;
	/*
	 * Set oscillator start phase on one explicit drum instance.
	 *
	 * Inputs: DrumVoice pointer and storage byte. Output: both drum
	 * oscillators receive the same 32-bit start phase. The helper exists
 * because InstrumentManager applies descriptor writes to the selected tagged
 * slot instance directly and no fixed drum storage exists in this module.
	 */
	if(!voice)
		return;
	voice->osc.startPhase = startPhase;
	voice->modOsc.startPhase = startPhase;
}
//---------------------------------------------------
void Drum_initVoice(DrumVoice *voice, uint8_t seed_index)
{
	/*
	 * Initialize one drum runtime instance.
	 *
 * Inputs: caller-owned DrumVoice and a small seed index used only for the
 * original test waveform spread. Output: all DSP subobjects receive the drum
 * engine's established defaults. InstrumentManager calls this for one tagged
 * slot member, keeping oscillator/envelope/filter defaults in DrumVoice.
	 */
	if(!voice)
		return;

	SnapEg_init(&voice->snapEg);
	Drum_setPanVoice(voice,64);
	voice->vol = 0.8f;
	//voice->panModifier = 1.f;
	voice->fmModAmount = 0.5f;
	transient_init(&voice->transGen);
#if ENABLE_DRUM_SVF
	SVF_init(&voice->filter);
	voice->filterType = 0x01;
#endif
	lfo_init(&voice->lfo);

	voice->modOsc.freq = 440;
	voice->modOsc.waveform = 1;
	voice->modOsc.fmMod = 0;
	voice->modOsc.midiFreq = 70<<8;
	voice->modOsc.pitchMod = 1.0f;
	voice->modOsc.modNodeValue = 1;

	voice->volumeMod = 1;

	voice->osc.freq = 440;
	voice->osc.modNodeValue = 1;
	/* S085 C11.2: seed limited to Tri/Saw/Rec; 6+ are stock waves now. */
	voice->osc.waveform = (uint8_t)(TRI + (seed_index % 3u));
	voice->osc.fmMod = 0;
	voice->osc.midiFreq = 70<<8;

	DecayEg_init(&voice->oscPitchEg);
	voice->egPitchModAmount = 0.5f;

	slopeEg2_init(&voice->oscVolEg);
	/* S085 P2: no retrigger ramp pending; the first block starts flat. */
	voice->lastVeloGain = 1.0f;
	voice->ampRampArm = 0u;
	voice->lastFmMod = 0.0f;   /* S085 P2 F-A */
	setDistortionShape(&voice->distortion, 2.f);

#ifdef USE_AMP_FILTER
	initOnePole(&voice->ampFilter);
	setOnePoleCoef(&voice->ampFilter,ampSmoothValue);
#endif

#if ENABLE_MIX_OSC
	voice->mixOscs = true;
#endif
	voice->decimationCnt = 0;
	voice->decimationRate = 1;
	/* S085 C11.2: rst until the run row is applied. */
	oscSection_init(&voice->section);
}
//---------------------------------------------------
void Drum_triggerVoice(DrumVoice *voice, const uint8_t source_slot,
                       const uint8_t vol, const uint8_t note)
{
	/*
	 * Trigger one explicit drum runtime instance.
	 *
	 * Inputs: DrumVoice pointer, logical source slot, velocity byte, and note.
	 * Output: LFO/velocity modulation uses source_slot while oscillator,
	 * envelope, transient, and filter state mutate only the supplied instance.
 * This separation is required because a drum can occupy any tagged storage
 * slot and no engine-global drum state survives beside that slot.
	 */
	if(!voice)
		return;

	lfo_retrigger(source_slot);

	//update velocity modulation
	modNode_updateValue(&velocityModulators[source_slot],vol/127.f);
	/*
	 * Apply velocity targets that are not direct ModulationNode pointers.
	 *
	 * Inputs: source voice slot and normalized trigger velocity. Output:
	 * InstrumentManager no-ops for ordinary direct descriptor targets, but
	 * applies voice-local slot decimation or retained Scene targets when those
	 * are installed. This call stays beside modNode_updateValue() so trigger
	 * clients do not need to know which backend the current target uses.
	 */
	instrumentManager_applyVelocityModulationTarget(source_slot, vol/127.f);

	{
		OscInfo* const mods[1] = { &voice->modOsc };
		const uint8_t reset = oscSection_beginTrigger(&voice->section,
		                                              &voice->osc, mods, 1u);

		//only reset phase if envelope is closed
#ifdef USE_AMP_FILTER
		if (reset != OSC_TRIGGER_RESET_NONE &&
		    ((voice->volEgValueBlock[15]<=0.01f) || (voice->transGen.waveform==1)))
#else
		if (reset != OSC_TRIGGER_RESET_NONE)
#endif
		{
			float offset = 1;
			if(voice->transGen.waveform==1) //offset mode
			{
				offset -= voice->transGen.volume;
#ifdef USE_AMP_FILTER
				setOnePoleCoef(&voice->ampFilter,1.0f);
			} else {
				setOnePoleCoef(&voice->ampFilter,ampSmoothValue);
#endif
			}
			if (voice->osc.oscType == OSC_TYPE_SMP ||
			    voice->osc.oscType == OSC_TYPE_SLP)
				oscSample_restart(&voice->osc);
			else if (voice->osc.oscType != OSC_TYPE_WAV ||
			         osc_isTableWave(voice->osc.waveform))
				voice->osc.phase = (0xff<<20)*offset;
			else if (voice->osc.waveform == SINE)
				voice->osc.phase = 1024 + ( (0x3ff<<20) - 1024)*offset;
			else
				voice->osc.phase = 0;
		}
		/* S085 C11.2: only rst clears the filter (fre/itp keep it ringing). */
		if (reset == OSC_TRIGGER_RESET_ALL)
			SVF_reset(&voice->filter);
	}

	osc_setBaseNote(&voice->osc,note);
	osc_setBaseNote(&voice->modOsc,note);


	DecayEg_trigger(&voice->oscPitchEg);
	/*
	 * Amp envelope retrigger by run mode (S085 P2).
	 *
	 * What:       rst: slopeEg2_trigger(), exactly as before. fre/itp: the
	 *             attack starts at the gain the last block ended on
	 *             (ampFilterInput; slopeEg2_retriggerFrom()), and when that
	 *             gain is still audible the next block ramps from it
	 *             (ampRampArm). A fully decayed voice gets a fresh attack and
	 *             no ramp.
	 * Why:        slopeEg2_trigger() switches the raw value from the decay to
	 *             the attack curve, a gain step of up to full scale; at
	 *             attack 0 the async also skipped the per-block ramp. That
	 *             was the retrigger click in fre/itp.
	 * Inputs:     section.runMode, ampFilterInput, the envelope.
	 * Outputs:    oscVolEg, ampRampArm.
	 * Affiliates: Drum_calcVoiceAsync() (ramp at attack 0 when armed),
	 *             Drum_calcVoiceSyncBlock() (velocity ramp),
	 *             slopeEg2_retriggerFrom(), slopeEg2_isAudibleGain().
	 */
#if (AMP_EG_SYNC != 0)
#error "S085 P2 amp retrigger assumes AMP_EG_SYNC == 0 (ampFilterInput)"
#endif
	if (voice->section.runMode == OSC_RUN_RST) {
		slopeEg2_trigger(&voice->oscVolEg);
	} else {
		const float gainNow = voice->ampFilterInput;

		slopeEg2_retriggerFrom(&voice->oscVolEg, gainNow);
		voice->ampRampArm = slopeEg2_isAudibleGain(gainNow);
	}
	voice->velo = vol/127.f;

	transient_trigger(&voice->transGen);

	SnapEg_trigger(&voice->snapEg);

}
//---------------------------------------------------
void Drum_calcVoiceAsync(DrumVoice *voice, const uint8_t amp_eg_sync)
{
	/*
	 * Calculate one drum instance's control-rate block.
	 *
	 * Inputs: DrumVoice pointer and the amp-envelope sync mode that belongs to
	 * the hosting slot. Output: pitch, snap, FM amount, amp envelope smoothing,
 * and oscillator frequencies advance on that instance only. The hosting
 * tagged slot, not a fixed drum index, decides whether this engine renders.
	 */
	if(!voice)
		return;


	//add modulation eg to osc freq (1 = no change. a+eg = original freq + modulation
	const float egPitchVal = DecayEg_calc(&voice->oscPitchEg);
	const float pitchEgValue = egPitchVal*voice->egPitchModAmount;
	voice->osc.pitchMod = 1+pitchEgValue;

	//calc snap EG if transient sample 0 is activated
	if(voice->transGen.waveform == 0)
	{
		const float snapVal = SnapEg_calc(&voice->snapEg, voice->transGen.pitch);
		voice->osc.pitchMod += snapVal*voice->transGen.volume;
	}

	// fm amount with pitch eg
	voice->osc.fmMod = voice->fmModAmount * egPitchVal;

	//calc the osc + noise vol eg
#if (AMP_EG_SYNC==0)

	//check if in attack phase
	/*
	 * S085 P2: an armed fre/itp retrigger block always ramps from the last
	 * gain, so attack 0 (and a repeat-mode restart) does not jump to full in
	 * one sample. Unarmed blocks keep the original rule exactly.
	 */
	if( (voice->oscVolEg.attack == 1 ) &&
	    ((voice->oscVolEg.state == EG_A) || (voice->oscVolEg.state == EG_REPEAT)) &&
	    !voice->ampRampArm )
	{
			//if attack is set to 0 -> no interpolation
		voice->ampFilterInput = slopeEg2_calc(&voice->oscVolEg);
		voice->lastGain = voice->ampFilterInput;
	}
	else
	{
		voice->lastGain = voice->ampFilterInput;
		voice->ampFilterInput = slopeEg2_calc(&voice->oscVolEg);
	}

	//turn off trigger signal if trigger gate mode is on and volume == 0
	/* TODO DSP_PORT
	if(trigger_isGateModeOn())
	{
		if(!voice->ampFilterInput) {
			trigger_triggerVoice(TRIGGER_1 + voiceNr, TRIGGER_OFF);
			voiceControl_noteOff(TRIGGER_1 + voiceNr);
		}
	}
	*/
#endif

	//update osc phaseInc
	(void)amp_eg_sync;
	osc_setFreq(&voice->osc);
	osc_setFreq(&voice->modOsc);

}

//---------------------------------------------------
void Drum_calcVoiceSyncBlock(DrumVoice *voice, int16_t* buf, const uint8_t size)
{
	/*
	 * Render one drum runtime instance into a mono, PRE-VOLUME block.
	 *
	 * Inputs: DrumVoice pointer, destination buffer, and block size. Output:
	 * buf receives oscillators, transient, filter, amp EG, velocity, and
	 * distortion, but NOT the channel volume (voice->vol). Mixer selects this
	 * object by current runtime type rather than a hardcoded drum index.
	 *
	 * Why pre-volume (Session 072, Effects Phase 5 step 2): the mixer applies
	 * voice->vol after decimation, combined with the slider gain ramp, so that
	 * the FX send can tap the same decimated signal before volume (user rules
	 * A24/A25). voice->vol remains the retained runtime value written by the
	 * descriptor/LFO/Morph/automation paths; it is read by
	 * instrumentManager_runtimeVolume(). Affiliates: mixer.c
	 * mixer_calcNextSampleBlock(), InstrumentManager.c.
	 */
	if(!voice || !buf)
		return;

	static int16_t modBuf[OUTPUT_DMA_SIZE];

	//calc vol EG
#ifdef USE_AMP_FILTER
	calcOnePoleBlockFixedInput(&voice->ampFilter, voice->ampFilterInput,voice->volEgValueBlock, size);
#endif

	/*
	 * Oscillator section (S085 C11.2).
	 *
	 * What:       fm/mix/snc/rng through OscSection; with itp active the old
	 *             carrier renders into the pool scratch with the same live
	 *             modulator block and the two crossfade.
	 * Why:        plan §4.3.7; fm/mix with no crossfade are bit-identical to
	 *             the pre-S085 code (osc_s0).
	 * Affiliates: oscSection_renderDrum(), oscSection_blendItp(),
	 *             oscRenderPool_scratch().
	 */
	{
		OscInfo* const old = oscSection_itpCarrier(&voice->section);
		int16_t* const oldBuf = old ? oscRenderPool_scratch() : 0;

		/*
		 * FM depth ramp start (S085 P2 F-A).
		 *
		 * What:       on the armed fre/itp retrigger block (ampRampArm, set
		 *             only when the voice is still audible) the fm modulator
		 *             block ramps from lastFmMod / fmMod to 1; every other
		 *             block passes 1 (flat). The ratio is computed every
		 *             block and clamped to 0..1, so the scaled modulator can
		 *             never exceed its rendered value (no int16 overflow);
		 *             a retrigger whose new depth is below the old one
		 *             (rare: lowered FM amount) keeps that part of the step.
		 *             While an itp crossfade is active the section forces it
		 *             flat.
		 * Why:        fmMod = fmModAmount x pitch envelope, and
		 *             DecayEg_trigger() restarts the envelope at 1.0, so the
		 *             FM depth jumped at the retrigger; with phase modulation
		 *             that jumped the carrier's read phase by
		 *             mod x (new - old): the sporadic high-FM fre click.
		 * Inputs:     osc.fmMod (this block), lastFmMod, ampRampArm.
		 * Outputs:    the ramp start passed to the section; lastFmMod.
		 * Affiliates: oscSection_renderDrumRamped(), Drum_calcVoiceAsync().
		 */
		const float fmNow = voice->osc.fmMod;
		float fmRatio = voice->lastFmMod / ((fmNow > 0.0f) ? fmNow : 1.0f);
		float fmRampFrom;

		if (fmRatio > 1.0f)
			fmRatio = 1.0f;
		if (fmRatio < 0.0f)
			fmRatio = 0.0f;
		fmRampFrom = (voice->ampRampArm && fmNow > 0.0f) ? fmRatio : 1.0f;
		oscSection_renderDrumRamped(&voice->osc, &voice->modOsc,
		                            voice->mixOscs, voice->fmModAmount,
		                            buf, modBuf, size, old, oldBuf,
		                            fmRampFrom);
		voice->lastFmMod = fmNow;
		if (old)
			oscSection_blendItp(&voice->section, buf, oldBuf, size);
	}

	//calc transient sample
	transient_calcBlock(&voice->transGen,modBuf,size);

	//Mix with transient buffer
	bufferTool_addBuffersSaturating(buf,modBuf,size);

	//calc filter block
	SVF_calcBlockZDF(&voice->filter,voice->filterType,buf,size);

	#if defined(USE_AMP_FILTER) || (USE_FILTER_DRIVE != 0)
	/* Non-default legacy configurations retain their separate passes. */
	#ifdef USE_AMP_FILTER
	bufferTool_multiplyWithFloatBufferDithered(&voice->dither, buf,voice->volEgValueBlock,size);
	#else
	bufferTool_addGainInterpolated(buf,voice->ampFilterInput, voice->lastGain, size);
	#endif
	if(voice->volumeMod)
		bufferTool_addGain(buf,voice->velo,size);
	#if (USE_FILTER_DRIVE == 0)
	calcDistBlock(&voice->distortion,buf,size);
	#endif
	#else
	/*
	 * Fused Drum post-chain (S073 Step 4).
	 *
	 * What:       amp EG ramp, velocity gain and distortion in one pass.
	 * Why:        removes two intermediate int16 load/store passes while
	 *             preserving all truncation points (S0). The velocity multiply
	 *             always runs; its selected 1.0 gain is not a bypass.
	 * Inputs:     filtered buf, amp gain endpoints, volumeMod/velo and dist.
	 * Outputs:    pre-volume buf; the mixer still owns channel volume.
	 * Accessors:  this render function through InstrumentManager.
	 * Affiliates: voicePostChain.h, Drum gain bookkeeping and the Step 4 tests.
	 */
	/*
	 * Velocity ramp endpoints and the arm (S085 P2).
	 *
	 * What:       the block's velocity gain ramps from lastVeloGain when
	 *             this is the armed retrigger block, else it is flat. After
	 *             the block, lastVeloGain records the gain just applied and
	 *             the arm is cleared (one block only).
	 * Why:        a new velocity at a fre/itp retrigger with level left would
	 *             step the output; rst and ordinary blocks stay bit-identical.
	 * Inputs:     volumeMod, velo, lastVeloGain, ampRampArm.
	 * Outputs:    buf; lastVeloGain; ampRampArm = 0.
	 * Affiliates: voicePost_drum(), Drum_triggerVoice().
	 */
	{
		const float veloGain = voice->volumeMod ? voice->velo : 1.0f;
		const float lastVeloGain = voice->ampRampArm
			? voice->lastVeloGain : veloGain;

		voicePost_drum(buf, voice->ampFilterInput, voice->lastGain,
		               veloGain, lastVeloGain, &voice->distortion, size);
		voice->lastVeloGain = veloGain;
		voice->ampRampArm = 0u;
	}
	#endif
	/*
	 * Channel volume is intentionally not applied here (Session 072 step 2).
	 * The mixer multiplies the decimated block by voice->vol x slider gain;
	 * see instrumentManager_runtimeVolume() and mixer_calcNextSampleBlock().
	 * Re-adding a gain stage here would apply volume twice.
	 */
	/* S085 C11.2: advance or end the itp crossfade. */
	oscSection_finishBlock(&voice->section, size);
}
//---------------------------------------------------
