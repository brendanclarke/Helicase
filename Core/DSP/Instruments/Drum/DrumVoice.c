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
	voice->osc.waveform = TRI+seed_index; //for testing init to tri,saw,rec
	voice->osc.fmMod = 0;
	voice->osc.midiFreq = 70<<8;

	DecayEg_init(&voice->oscPitchEg);
	voice->egPitchModAmount = 0.5f;

	slopeEg2_init(&voice->oscVolEg);
	setDistortionShape(&voice->distortion, 2.f);

#ifdef USE_AMP_FILTER
	initOnePole(&voice->ampFilter);
	setOnePoleCoef(&voice->ampFilter,ampSmoothValue);
#endif

#if ENABLE_MIX_OSC
	voice->modMode = DRUM_MOD_MIX;	/* S085 P1: was mixOscs = true (1 = mix) */
#endif
	/*
	 * snc clock start (S085 P1).
	 * What: the clock phase begins at 0 on every runtime reset (Scene or
	 * type change); triggers do not touch it. Why: deterministic start
	 * after a reset; free-running afterwards like modOsc (plan Q6b).
	 * Accessors: Drum_calcVoiceSyncBlock().
	 */
	voice->syncClock = 0u;
	/* rng depth ramp (S085 P1 follow-up): start dry; the first rng block
	** after a trigger snaps to its own depth (ringSnap). */
	voice->ringDepthQ15 = 0;
	voice->ringSnap = 1u;
	voice->decimationCnt = 0;
	voice->decimationRate = 1;
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

	//only reset phase if envelope is closed
#ifdef USE_AMP_FILTER
	if((voice->volEgValueBlock[15]<=0.01f) || (voice->transGen.waveform==1))
#else
		//if((voice->ampFilterInput<=0.01f) || (voice->transGen.waveform==1))
#endif
	{
		float offset = 1;
		if(voice->transGen.waveform==1) //offset mode
		{
			offset -= voice->transGen.volume;
#ifdef USE_AMP_FILTER
			setOnePoleCoef(&voice->ampFilter,1.0f); //turn off amp filter for super snappy attack

		} else {
			setOnePoleCoef(&voice->ampFilter,ampSmoothValue);
#endif
		}
		if(voice->osc.waveform == SINE)
			voice->osc.phase = 1024 + ( (0x3ff<<20) - 1024)*offset;//voice->osc.startPhase ;
		else if(osc_isTableWave(voice->osc.waveform))
			voice->osc.phase = (0xff<<20)*offset;
		else
			voice->osc.phase = 0;

	}

	osc_setBaseNote(&voice->osc,note);
	osc_setBaseNote(&voice->modOsc,note);


	DecayEg_trigger(&voice->oscPitchEg);
	/*
	 * rng depth snap (S085 P1 follow-up; user choice "snap").
	 * What: the mod envelope restarts at 1 here, so the next rng block starts
	 * at its full depth instead of ramping up from the tail. Why: hits start
	 * at full ring depth, as fm hits start at full FM depth; the carrier phase
	 * resets on this trigger anyway. One store; no effect in fm/mix/snc.
	 * Accessors: Drum_calcVoiceSyncBlock() (rng). Future: free-run run mode
	 * ramps always (user, to try when the run modes return).
	 */
	voice->ringSnap = 1u;
	slopeEg2_trigger(&voice->oscVolEg);
	voice->velo = vol/127.f;

	transient_trigger(&voice->transGen);

	SnapEg_trigger(&voice->snapEg);

	//reset filter coeffs to prevent wrong transient
	SVF_reset(&voice->filter);
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

	/*
	 * Mod envelope: osc2 mod amount x the pitch envelope (S085 P1 follow-up).
	 *
	 * What:       osc.fmMod = amt x env, where env (egPitchVal) is the
	 *             PitchMod decay envelope: 1 at each trigger, decaying to 0.
	 *             The PitchMod amount (egPitchModAmount) scales only the
	 *             pitch sweep, not this.
	 * Why:        fm reads it as its FM depth on a modulator block already
	 *             scaled by amt, so the fm response is amt^2 x env.
	 *             Drum_modEnvDepth() reuses it so that snc and rng follow the
	 *             same amt^2 x env response.
	 * Accessors:  calcNextOscSampleFmBlock() (fm); Drum_modEnvDepth() (snc,
	 *             rng), both in Drum_calcVoiceSyncBlock().
	 */
	voice->osc.fmMod = voice->fmModAmount * egPitchVal;

	//calc the osc + noise vol eg
#if (AMP_EG_SYNC==0)

	//check if in attack phase
	if( (voice->oscVolEg.attack == 1 ) && ((voice->oscVolEg.state == EG_A) || (voice->oscVolEg.state == EG_REPEAT)) )
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
/*
 * snc / rng modulation depth with the fm response (S085 P1 follow-up).
 *
 * What:       amt x osc.fmMod = amt^2 x env, where env is the PitchMod decay
 *             envelope (Drum_calcVoiceAsync()). It is full at each trigger
 *             and decays to 0, so snc fades to a free-running slave and rng
 *             to the dry carrier over the hit.
 * Why:        the user asked for the fm response in all three modes. fm's
 *             FM index is (modulator x amt) x (amt x env), so amt^2 x env is
 *             exactly fm's response to the amount knob and the envelope. fm
 *             itself is not changed. A slightly negative env on the block
 *             where the envelope reaches 0 (DecayEg_calc() subtracts past
 *             zero) gives a slightly negative value; osc_syncDepthQ30() and
 *             osc_ringDepthQ15() clamp it to depth 0 / dry.
 * Inputs:     voice->fmModAmount, voice->osc.fmMod (set this block by
 *             Drum_calcVoiceAsync(), which the mixer runs before the
 *             render).
 * Outputs:    the depth, nominally 0..1.
 * Cost:       one multiply per block.
 * Accessors:  Drum_calcVoiceSyncBlock() (snc and rng cases).
 * Affiliates: Oscillator.h osc_syncDepthQ30(), osc_ringDepthQ15(); DrumVoice.h
 *             DRUM_MOD_*.
 */
static inline float Drum_modEnvDepth(const DrumVoice *voice)
{
	return voice->fmModAmount * voice->osc.fmMod;
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
	 * Oscillator section by osc2 mod mode (S085 P1).
	 *
	 * What:       fm and mix make exactly the calls they made before, in the
	 *             same order, with the same arguments.
	 *             - rng: the modulator block at gain 1, then the carrier
	 *               through its ring copy: carrier x (1 - d) + carrier x
	 *               modulator x d, with d ramped per sample from the last
	 *               block's value (snapped after a trigger).
	 *             - snc: the FM-page oscillator is not rendered. Its
	 *               frequency drives syncClock, the block's reset list is
	 *               built from that clock, and the carrier renders through
	 *               its sync copy at gain 1 with d as the sync depth.
	 *             Here d = Drum_modEnvDepth() = amt^2 x the PitchMod decay
	 *             envelope: the response fm already has (S085 P1 follow-up).
	 *             modBuf is still fully rewritten by transient_calcBlock()
	 *             below, so its contents in snc do not matter.
	 * Why:        S085_P1_OSC_MOD_SNC_RNG.md §4. The session rule: no mode may
	 *             cost more than today's dearest fm setting (implementation
	 *             §1). The envelope adds one multiply per block.
	 * Inputs:     voice->modMode, fmModAmount, osc (incl. osc.fmMod), modOsc,
	 *             syncClock; size <= OUTPUT_DMA_SIZE (as modBuf already
	 *             assumes).
	 * Outputs:    buf (the oscillator section); modBuf (fm, mix, rng);
	 *             syncClock (snc).
	 * Accessors:  this render function, through InstrumentManager.
	 * Affiliates: Oscillator.h S085 P1 block; DrumVoice.h DRUM_MOD_*;
	 *             DrumParameters.c osc2_mod_type.
	 */
	switch (voice->modMode)
	{
	case DRUM_MOD_MIX:
		//calc next mod osc sampleBlock
		calcNextOscSampleBlock(&voice->modOsc,modBuf,size,voice->fmModAmount);
		//calc main osc buffer
		calcNextOscSampleBlock(&voice->osc,buf,size, (1.f-voice->fmModAmount));
		//add mod buffer to main osc buffer
		bufferTool_addBuffersSaturating(buf,modBuf,size);
		break;

	case DRUM_MOD_SNC:
	{
		uint32_t syncOver[OUTPUT_DMA_SIZE];
		const uint32_t clockInc = osc_syncClockInc(&voice->modOsc);

		voice->syncClock = osc_buildSyncList(voice->syncClock, clockInc,
		                                     syncOver, size);
		/* sync depth = amt^2 x mod envelope (Drum_modEnvDepth()) */
		calcNextOscSampleSyncBlock(&voice->osc, syncOver, clockInc,
		                           osc_syncDepthQ30(Drum_modEnvDepth(voice)),
		                           buf, size);
		break;
	}

	case DRUM_MOD_RNG:
	{
		/*
		 * Ring depth ramp (S085 P1 follow-up).
		 * What: d = amt^2 x mod envelope (Drum_modEnvDepth()) in Q15. The
		 * ring copy moves d linearly from the last block's end value
		 * (ringDepthQ15) to this block's d, one step per sample. On the first
		 * block after a trigger (ringSnap) it starts at this block's d
		 * instead (no ramp). The modulator renders at gain 1; the ring copy
		 * applies d. The value actually reached (start + size x step) is
		 * stored, so the step's integer rounding never accumulates.
		 * Why: a d stepped once per block zippered ("crunchy").
		 * Cost: one divide per block; about 3 instructions per sample in the
		 * ring copy (implementation §14).
		 */
		const int32_t target = osc_ringDepthQ15(Drum_modEnvDepth(voice));
		const int32_t start = voice->ringSnap ? target : voice->ringDepthQ15;
		const int32_t step = size ? (target - start) / (int32_t)size : 0;

		voice->ringSnap = 0u;
		calcNextOscSampleBlock(&voice->modOsc,modBuf,size,1.0f);
		calcNextOscSampleRingBlock(&voice->osc, modBuf, buf, size,
		                           start, step);
		voice->ringDepthQ15 = start + step * (int32_t)size;
		break;
	}

	case DRUM_MOD_FM:
	default:
		//calc next mod osc sampleBlock
		calcNextOscSampleBlock(&voice->modOsc,modBuf,size,voice->fmModAmount);
		calcNextOscSampleFmBlock(&voice->osc,modBuf,buf,size,1.0f);
		break;
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
	voicePost_drum(buf, voice->ampFilterInput, voice->lastGain,
	               voice->volumeMod ? voice->velo : 1.0f,
	               &voice->distortion, size);
	#endif
	/*
	 * Channel volume is intentionally not applied here (Session 072 step 2).
	 * The mixer multiplies the decimated block by voice->vol x slider gain;
	 * see instrumentManager_runtimeVolume() and mixer_calcNextSampleBlock().
	 * Re-adding a gain stage here would apply volume twice.
	 */
}
//---------------------------------------------------
