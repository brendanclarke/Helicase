/*
 * Snare.c
 *
 *  Created on: 17.04.2012
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



#include "Snare.h"
#include "squareRootLut.h"
#include "modulationNode.h"
#include "InstrumentManager.h"
#include "voicePostChain.h"
#include "InsertEffectsManager.h"
// #include "TriggerOut.h"


//---------------------------------------------------
void Snare_setPanVoice(SnareVoice *voice, const uint8_t pan)
{
	/*
	 * Set pan on one explicit snare runtime instance.
	 *
	 * Inputs: caller-owned SnareVoice and raw 0..127 pan. Output: the instance
 * pan byte changes; mixer performs final pan-gain lookup. The manager supplies
 * the tagged slot member, so this module never resolves a permanent snare.
	 */
	if(!voice)
		return;
	//voice->panL = squareRootLut[127-pan];
	//voice->panR = squareRootLut[pan];
	voice->pan = pan;
}
//---------------------------------------------------
void Snare_initVoice(SnareVoice *voice)
{
	/*
	 * Initialize one snare runtime instance.
	 *
 * Inputs: caller-owned SnareVoice. Output: oscillator, envelope, transient,
 * filter, distortion, and LFO members receive snare defaults. The manager
 * calls this for one tagged member so defaults remain in the snare module.
	 */
	if(!voice)
		return;
	SnapEg_init(&voice->snapEg);
	Snare_setPanVoice(voice, 64);
	voice->vol = 0.8f;

	//voice->panModifier = 1.f;

	voice->noiseOsc.freq = 440;
	voice->noiseOsc.waveform = 1;
	voice->noiseOsc.fmMod = 0;
	voice->noiseOsc.midiFreq = 70<<8;
	voice->noiseOsc.pitchMod = 1.0f;
	voice->noiseOsc.modNodeValue = 1;

	voice->osc.freq = 440;
	voice->osc.waveform = 1;
	voice->osc.fmMod = 0;
	voice->osc.midiFreq = 70<<8;
	voice->osc.modNodeValue = 1;

	setDistortionShape(&voice->distortion, 2.f);

	voice->volumeMod = 1;

	transient_init(&voice->transGen);

	DecayEg_init(&voice->oscPitchEg);
	voice->egPitchModAmount = 0.5f;

	slopeEg2_init(&voice->oscVolEg);

	setDistortionShape(&voice->distortion, 2.f);

	SVF_init(&voice->filter);
	driveShaper_reset(&voice->shaper);

	lfo_init(&voice->lfo);
	/*
	 * Oscillator section init (S084 P1 P1-8.2).
	 *
	 * What:       run mode rst, no crossfade, oscCount 2 for Snare.
	 * Why:        a fresh or reset slot starts in today's behaviour until its
	 *             `run` row is applied (plan §4.1.6).
	 * Inputs:     the engine instance.  Outputs: voice->section.
	 * Accessors:  Snare_initVoice() via
	 *             instrumentManager_resetRuntimeSlot*().
	 * Affiliates: OscSection.c, IM_SPECIAL_RUN_MODE writer.
	 */
	oscSection_init(&voice->section, 2u);
}
//---------------------------------------------------
void Snare_triggerVoice(SnareVoice *voice, const uint8_t source_slot,
                        const uint8_t vel, const uint8_t note)
{
	/*
	 * Trigger one explicit snare runtime instance.
	 *
	 * Inputs: SnareVoice pointer, logical source slot, velocity, and note.
	 * Output: modulation affiliation is applied to source_slot while oscillator
	 * and envelope state mutate only the supplied instance. This split is what
	 * allows two snare instruments or a snare outside physical slot 4 to render
	 * correctly after Instrument Load.
	 */
	if(!voice)
		return;
	lfo_retrigger(source_slot);
	//update velocity modulation
	modNode_updateValue(&velocityModulators[source_slot],vel/127.f);
	/*
	 * Apply velocity targets that are not direct ModulationNode pointers.
	 *
	 * Inputs: source voice slot and normalized trigger velocity. Output:
	 * InstrumentManager no-ops for ordinary direct descriptor targets, but
	 * applies voice-local slot decimation or retained Scene targets when those
	 * are installed. This call stays beside modNode_updateValue() so trigger
	 * clients do not need to know which backend the current target uses.
	 */
	instrumentManager_applyVelocityModulationTarget(source_slot, vel/127.f);

	/*
	 * S084 P1 P1-8.3: the run mode decides the trigger behaviour. rst = today
	 * (phase resets); fre = no reset (oscillators slew); itp = snapshot then
	 * reset, crossfaded by the section. Snare never clears its filter.
	 */
	{
		OscInfo* oscs[2] = { &voice->osc, &voice->noiseOsc };

		if (oscSection_beginTrigger(&voice->section, oscs)) {
			float offset = 1;
			/* S084 VX2 VX2-6.5: Snp/Ofs are trs kinds; under wav, bytes 0/1
			** are PCM transients (transient_isSnap()/transient_isOffset()). */
			if(transient_isOffset(&voice->transGen)) //offset mode
			{
				offset -= voice->transGen.volume;
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
				voice->osc.phase = (0x3ff<<20)*offset;
			/* S084 VX2 VX2-4.12: table waves, including sq3..swa, start at the
			** table offset (osc_isTableWave()). */
			else if(osc_isTableWave(voice->osc.waveform))
				voice->osc.phase = (0xff<<20)*offset;
			else
				voice->osc.phase = 0;
		}
	}

	DecayEg_trigger(&voice->oscPitchEg);
	slopeEg2_trigger(&voice->oscVolEg);
	voice->velo = vel/127.f;

	osc_setBaseNote(&voice->osc,note);
	//TODO noise muss mit transponiert werden

	transient_trigger(&voice->transGen);
	/* S084 VX3 VX3-8.9: re-arm the transient buffer stage. */
	insertFx_transientTrigger(voice->stageSlot);

	SnapEg_trigger(&voice->snapEg);
}
//---------------------------------------------------
void Snare_calcAsyncVoice(SnareVoice *voice)
{
	/*
	 * Calculate one snare instance's control-rate block.
	 *
 * Inputs: SnareVoice pointer. Output: pitch envelope, amplitude envelope,
 * snap, and oscillator frequencies advance for that tagged instance only.
 * InstrumentManager renders current slot type without global snare state.
	 */
	if(!voice)
		return;
	//add modulation eg to osc freq (1 = no change. a+eg = original freq + modulation
	const float egPitchVal = DecayEg_calc(&voice->oscPitchEg);
	const float pitchEgValue = egPitchVal*voice->egPitchModAmount;
	voice->osc.pitchMod = 1+pitchEgValue;

	//calc the osc  vol eg
	voice->egValueOscVol = slopeEg2_calc(&voice->oscVolEg);

	//turn off trigger signal if trigger gate mode is on and volume == 0
	/* TODO DSP_PORT
	if(trigger_isGateModeOn())
	{
		if(!voice->egValueOscVol) {
			trigger_triggerVoice(TRIGGER_4, TRIGGER_OFF);
			voiceControl_noteOff(TRIGGER_4);
		}
	}
	*/

	//calc snap EG if transient sample 0 is activated
	if(transient_isSnap(&voice->transGen))
	{
		const float snapVal = SnapEg_calc(&voice->snapEg, voice->transGen.pitch);
		voice->osc.pitchMod += snapVal*voice->transGen.volume;;
	}

	osc_setFreq(&voice->osc);
	osc_setFreq(&voice->noiseOsc);
}
//---------------------------------------------------
void Snare_calcSyncBlockVoice(SnareVoice *voice, int16_t* buf,
                              const uint8_t size)
{
	/*
	 * Render one snare instance into a mono, PRE-VOLUME block.
	 *
	 * Inputs: SnareVoice pointer, destination buffer, and block size. Output:
	 * buf receives noise, oscillator, transient, amp envelope, optional
	 * velocity, and distortion for that tagged instance, but NOT the channel
	 * volume (voice->vol).
	 *
	 * Why (Session 072, Effects Phase 5 step 2; volume-order bug fix D1):
	 * volume used to multiply the signal before calcDistBlock(), so it also
	 * acted as a hidden drive control (a bug; volume must be the last stage).
	 * It is now a pure output level applied by the mixer after decimation,
	 * matching Drum, so the FX send can tap a pre-volume signal that includes
	 * this voice's distortion. Affiliates:
	 * instrumentManager_runtimeVolume(), mixer_calcNextSampleBlock().
	 */
	if(!voice || !buf)
		return;
	static int16_t transBuf[OUTPUT_DMA_SIZE];


	calcNoiseBlock(&voice->noiseOsc,buf,size,0.9f);
	if (voice->section.itpState == OSC_ITP_ACTIVE) {
		int16_t* oldBuf = oscRenderPool_scratchSection();

		calcNoiseBlock(&voice->section.itpOld[1], oldBuf, size, 0.9f);
		oscSection_blendItp(&voice->section, buf, oldBuf, size);
	}
	/* S084 VX3 VX3-8.9: transient buffer PRE-filter stage. */
	insertFx_renderTransient(voice->stageSlot, INSERT_PLACE_PRE, buf, size,
	                        voice->transGen.volume, voice->transGen.pitch);
	/* S084 VX2 VX2-7.6: sft keeps the untouched loop (bit-identical). */
	if (voice->driveType == DRIVE_TYPE_SFT)
		SVF_calcBlockZDF(&voice->filter,voice->filterType,buf,size);
	else
		SVF_calcBlockZDFShaped(&voice->filter, voice->filterType,
		                       voice->driveType, &voice->shaper, buf, size);
	/* S084 VX3 VX3-8.9: transient buffer POST-filter stage. */
	insertFx_renderTransient(voice->stageSlot, INSERT_PLACE_POST, buf, size,
	                        voice->transGen.volume, voice->transGen.pitch);

	//calc transient sample
	transient_calcBlock(&voice->transGen,transBuf,size);
	bufferTool_addBuffersSaturating(buf,transBuf,size);

	//calc next osc sample
	calcNextOscSampleBlock(&voice->osc,transBuf,size,(1.f-voice->mix));
	if (voice->section.itpState == OSC_ITP_ACTIVE) {
		int16_t* oldBuf = oscRenderPool_scratchSection();

		calcNextOscSampleBlock(&voice->section.itpOld[0], oldBuf, size,
		                       (1.f-voice->mix));
		oscSection_blendItp(&voice->section, transBuf, oldBuf, size);
	}
	//--AS apply filter to synthesized sound as well here if desired, or combine code for more efficiency

	/*
	 * Fused Snare post-chain (S073 Step 4).
	 *
	 * What:       mix gain, saturating add, amp gain and distortion in one pass.
	 * Why:        removes one intermediate post-chain pass while preserving
	 *             the old int16 conversion and saturation points (S0).
	 * Inputs:     filtered buf, transBuf, mix, volumeMod/velo/EG and distortion.
	 * Outputs:    pre-volume buf.
	 * Accessors:  this render function.
	 * Affiliates: voicePostChain.h and the Step 4 host/ARM checks.
	 */
	{
		const float ampGain = voice->volumeMod
			? voice->velo * voice->egValueOscVol
			: voice->egValueOscVol;
		voicePost_mixAddGain(buf, transBuf, voice->mix, ampGain, size);
	}
	oscSection_finishBlock(&voice->section, size);
}
//------------------------------------------------------------------------
