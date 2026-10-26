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
#include "InsertEffectsManager.h"
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
	driveShaper_reset(&voice->shaper);
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
	/* S084 P1 P1-8.2: test seed limited to Tri/Saw/Rec; values above Cym are
	** no longer legal `wav` values. */
	voice->osc.waveform = TRI + (seed_index % 3u);
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
	voice->mixOscs = true;
#endif
	voice->decimationCnt = 0;
	voice->decimationRate = 1;
	/*
	 * Oscillator section init (S084 P1 P1-8.2).
	 *
	 * What:       run mode rst, no crossfade, oscCount 2 for Drum; the filter
	 *             slew starts from the filter's values at the first recalc.
	 * Why:        a fresh or reset slot must start in today's behaviour until
	 *             its `run` row is applied (plan §4.1.6).
	 * Inputs:     the engine instance.  Outputs: voice->section.
	 * Accessors:  Drum_initVoice() via
	 *             instrumentManager_resetRuntimeSlot*().
	 * Affiliates: OscSection.c, IM_SPECIAL_RUN_MODE writer.
	 */
	oscSection_init(&voice->section, 2u);
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

	/*
	 * S084 P1 P1-8.3: the run mode decides the trigger behaviour. rst = today
	 * (phase resets, filter cleared); fre = no reset (oscillators slew, filter
	 * keeps ringing); itp = snapshot then reset, crossfaded by the section
	 * (filter keeps ringing). Plan §4.1.6, §6 r4 Q3/Q4.
	 */
	{
		OscInfo* oscs[2] = { &voice->osc, &voice->modOsc };
		const uint8_t resetOscillators =
			oscSection_beginTrigger(&voice->section, oscs);

	//only reset phase if envelope is closed
#ifdef USE_AMP_FILTER
	if(resetOscillators &&
	   ((voice->volEgValueBlock[15]<=0.01f) ||
	    transient_isOffset(&voice->transGen)))
#else
		//if((voice->ampFilterInput<=0.01f) || (voice->transGen.waveform==1))
	if(resetOscillators)
#endif
	{
		float offset = 1;
		/* S084 VX2 VX2-6.5: Snp/Ofs are trs kinds; under wav, bytes 0/1
		** are PCM transients (transient_isSnap()/transient_isOffset()). */
		if(transient_isOffset(&voice->transGen)) //offset mode
		{
			offset -= voice->transGen.volume;
#ifdef USE_AMP_FILTER
			setOnePoleCoef(&voice->ampFilter,1.0f); //turn off amp filter for super snappy attack

		} else {
			setOnePoleCoef(&voice->ampFilter,ampSmoothValue);
#endif
		}
		if(voice->osc.oscType == OSC_TYPE_SMP ||
		   voice->osc.oscType == OSC_TYPE_SLP)
			oscSample_restart(&voice->osc);
		else if(voice->osc.oscType == OSC_TYPE_PWM)
			voice->osc.phase = (0xffu<<20)*offset;
		else if(voice->osc.oscType == OSC_TYPE_WTB)
			/* S084 VX2 VX2-5.8: wavetables start at the table offset;
			** waveform is a frame position, not a wave id. */
			voice->osc.phase = (0xffu<<20)*offset;
		else if(voice->osc.waveform == SINE)
			voice->osc.phase = 1024 + ( (0x3ff<<20) - 1024)*offset;//voice->osc.startPhase ;
			/* S084 VX2 VX2-4.12: table waves, including sq3..swa, start at the
			** table offset (osc_isTableWave()). */
			else if(osc_isTableWave(voice->osc.waveform))
			voice->osc.phase = (0xff<<20)*offset;
		else
			voice->osc.phase = 0;

	}
	}

	osc_setBaseNote(&voice->osc,note);
	osc_setBaseNote(&voice->modOsc,note);


	DecayEg_trigger(&voice->oscPitchEg);
	slopeEg2_trigger(&voice->oscVolEg);
	voice->velo = vol/127.f;

	transient_trigger(&voice->transGen);
	/* S084 VX3 VX3-8.9: re-arm the transient buffer stage. */
	insertFx_transientTrigger(voice->stageSlot);

	SnapEg_trigger(&voice->snapEg);

	/* S084 P1 P1-8.3: only rst clears the filter (fre/itp keep it ringing). */
	if (voice->section.runMode == OSC_RUN_RST)
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
	if(transient_isSnap(&voice->transGen))
	{
		const float snapVal = SnapEg_calc(&voice->snapEg, voice->transGen.pitch);
		voice->osc.pitchMod += snapVal*voice->transGen.volume;
	}

	// fm amount with pitch eg
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
	 * S084 P1 P1-8.4: oscillator section (combination + run-mode crossfade).
	 * fm/mix render exactly as before (S0, golden test P1-12.5). While an itp
	 * crossfade is active the pre-trigger section also renders into the pool
	 * scratch and the two are blended over OSC_ITP_SAMPLES.
	 */
	oscSection_renderDrum(&voice->osc, &voice->modOsc, voice->mixOscs,
	                      voice->fmModAmount, buf, modBuf, size);
	if (voice->section.itpState == OSC_ITP_ACTIVE) {
		int16_t* oldBuf = oscRenderPool_scratchSection();
		int16_t* oldMod = oscRenderPool_scratchMod();

		oscSection_renderDrum(&voice->section.itpOld[0],
		                      &voice->section.itpOld[1],
		                      voice->mixOscs, voice->fmModAmount,
		                      oldBuf, oldMod, size);
		oscSection_blendItp(&voice->section, buf, oldBuf, size);
	}
	/* S084 VX3 VX3-8.9: transient buffer PRE-filter stage. */
	insertFx_renderTransient(voice->stageSlot, INSERT_PLACE_PRE, buf, size,
	                        voice->transGen.volume, voice->transGen.pitch);

	//calc transient sample
	transient_calcBlock(&voice->transGen,modBuf,size);

	//Mix with transient buffer
	bufferTool_addBuffersSaturating(buf,modBuf,size);

	//calc filter block
	/* S084 VX2 VX2-7.6: sft keeps the untouched loop (bit-identical). */
	if (voice->driveType == DRIVE_TYPE_SFT)
		SVF_calcBlockZDF(&voice->filter,voice->filterType,buf,size);
	else
		SVF_calcBlockZDFShaped(&voice->filter, voice->filterType,
		                       voice->driveType, &voice->shaper, buf, size);

	#if defined(USE_AMP_FILTER) || (USE_FILTER_DRIVE != 0)
	/* Non-default legacy configurations retain their separate passes. */
	#ifdef USE_AMP_FILTER
	bufferTool_multiplyWithFloatBufferDithered(&voice->dither, buf,voice->volEgValueBlock,size);
	#else
	bufferTool_addGainInterpolated(buf,voice->ampFilterInput, voice->lastGain, size);
	#endif
	if(voice->volumeMod)
		bufferTool_addGain(buf,voice->velo,size);
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
	voicePost_drumGain(buf, voice->ampFilterInput, voice->lastGain,
	                   voice->volumeMod ? voice->velo : 1.0f, size);
	#endif
	/* S084 VX3 VX3-8.9: transient buffer POST-filter stage. */
	insertFx_renderTransient(voice->stageSlot, INSERT_PLACE_POST, buf, size,
	                        voice->transGen.volume, voice->transGen.pitch);
	oscSection_finishBlock(&voice->section, size);
	/*
	 * Channel volume is intentionally not applied here (Session 072 step 2).
	 * The mixer multiplies the decimated block by voice->vol x slider gain;
	 * see instrumentManager_runtimeVolume() and mixer_calcNextSampleBlock().
	 * Re-adding a gain stage here would apply volume twice.
	 */
}
//---------------------------------------------------
