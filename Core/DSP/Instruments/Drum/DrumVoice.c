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
// TODO DSP_PORT
// #include "TriggerOut.h"


INCCM static float ampSmoothValue = 0.1f;
//---------------------------------------------------
INCCMZ DrumVoice voiceArray[NUM_VOICES];
//---------------------------------------------------
void setPan(const uint8_t voiceNr, const uint8_t pan)
{
	Drum_setPanVoice(&voiceArray[voiceNr], pan);
}
//---------------------------------------------------
void Drum_setPanVoice(DrumVoice *voice, const uint8_t pan)
{
	/*
	 * Set pan on an explicit drum runtime instance.
	 *
	 * Inputs: caller-owned DrumVoice and raw 0..127 pan. Output: the instance
	 * pan field changes; mixer owns translating that byte through square-root
	 * gain tables. This cannot be folded into setPan() because dynamic
	 * Instrument Load slots are not always addressable through voiceArray[].
	 */
	if(!voice)
		return;
	voice->pan = pan;
}
//---------------------------------------------------
void drum_setPhase(const uint8_t phase, const uint8_t voiceNr)
{
	Drum_setPhaseVoice(&voiceArray[voiceNr], phase);
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
	 * because InstrumentManager applies descriptor writes to the selected slot
	 * instance directly; the old drum_setPhase() wrapper cannot address extra
	 * dynamic drum slots.
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
	 * original test waveform spread. Output: all DSP subobjects are placed in
	 * the same sane state initDrumVoice() previously gave voiceArray entries.
	 * InstrumentManager calls this for per-slot runtime pools; keeping it here
	 * avoids duplicating oscillator/envelope/filter defaults outside DrumVoice.
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
	voice->mixOscs = true;
#endif
	voice->decimationCnt = 0;
	voice->decimationRate = 1;
}
//---------------------------------------------------
void initDrumVoice()
{
	ampSmoothValue = 0.1f;

	int i;
	for(i=0;i<NUM_VOICES;i++)
	{
		Drum_initVoice(&voiceArray[i], (uint8_t)i);
	}
}
//---------------------------------------------------
void Drum_trigger(const uint8_t voiceNr, const uint8_t vol, const uint8_t note)
{
	Drum_triggerVoice(&voiceArray[voiceNr], voiceNr, vol, note);
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
	 * This separation is required for Instrument Load because a drum can now
	 * live in any of the six storage slots, not only voiceArray[0..2].
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
		else if(voice->osc.waveform > SINE && voice->osc.waveform <= REC)
			voice->osc.phase = (0xff<<20)*offset;
		else
			voice->osc.phase = 0;

	}

	osc_setBaseNote(&voice->osc,note);
	osc_setBaseNote(&voice->modOsc,note);


	DecayEg_trigger(&voice->oscPitchEg);
	slopeEg2_trigger(&voice->oscVolEg);
	voice->velo = vol/127.f;

	transient_trigger(&voice->transGen);

	SnapEg_trigger(&voice->snapEg);

	//reset filter coeffs to prevent wrong transient
	SVF_reset(&voice->filter);
}
//---------------------------------------------------
void calcDrumVoiceAsync(const uint8_t voiceNr)
{
	Drum_calcVoiceAsync(&voiceArray[voiceNr], AMP_EG_SYNC);
}
//---------------------------------------------------
void Drum_calcVoiceAsync(DrumVoice *voice, const uint8_t amp_eg_sync)
{
	/*
	 * Calculate one drum instance's control-rate block.
	 *
	 * Inputs: DrumVoice pointer and the amp-envelope sync mode that belongs to
	 * the hosting slot. Output: pitch, snap, FM amount, amp envelope smoothing,
	 * and oscillator frequencies advance on that instance only. Instrument
	 * Load needs this helper because the hosting slot, not voiceArray index,
	 * now decides whether a dynamic drum is rendered.
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
void calcDrumVoiceSyncBlock(const uint8_t voiceNr, int16_t* buf, const uint8_t size)
{
	Drum_calcVoiceSyncBlock(&voiceArray[voiceNr], buf, size);
}
//---------------------------------------------------
void Drum_calcVoiceSyncBlock(DrumVoice *voice, int16_t* buf, const uint8_t size)
{
	/*
	 * Render one drum runtime instance into a mono block.
	 *
	 * Inputs: DrumVoice pointer, destination buffer, and block size. Output:
	 * buf receives the same synthesized block the legacy wrapper produced for
	 * voiceArray entries. This helper cannot be folded into
	 * calcDrumVoiceSyncBlock() because mixer now selects runtime objects by
	 * current instrument type rather than by hardcoded drum voice index.
	 */
	if(!voice || !buf)
		return;

	static int16_t modBuf[OUTPUT_DMA_SIZE];

	//calc vol EG
#ifdef USE_AMP_FILTER
	calcOnePoleBlockFixedInput(&voice->ampFilter, voice->ampFilterInput,voice->volEgValueBlock, size);
#endif

	//calc next mod osc sampleBlock
	calcNextOscSampleBlock(&voice->modOsc,modBuf,size,voice->fmModAmount);

	if(voice->mixOscs)
	{
		//calc main osc buffer
		calcNextOscSampleBlock(&voice->osc,buf,size, (1.f-voice->fmModAmount));
		//add mod buffer to main osc buffer
		bufferTool_addBuffersSaturating(buf,modBuf,size);
	}
	else
	{
		calcNextOscSampleFmBlock(&voice->osc,modBuf,buf,size,1.0f);
	}

	//calc transient sample
	transient_calcBlock(&voice->transGen,modBuf,size);

	//Mix with transient buffer
	bufferTool_addBuffersSaturating(buf,modBuf,size);

	//calc filter block
	SVF_calcBlockZDF(&voice->filter,voice->filterType,buf,size);

	//attentuate main OSCs by amp EG
#ifdef USE_AMP_FILTER
	bufferTool_multiplyWithFloatBufferDithered(&voice->dither, buf,voice->volEgValueBlock,size);
#else
	bufferTool_addGainInterpolated(buf,voice->ampFilterInput, voice->lastGain, size);
#endif

	//MIDI velocity
	if(voice->volumeMod)
	{
		bufferTool_addGain(buf,voice->velo,size);
	}
	//distortion
#if (USE_FILTER_DRIVE == 0)
	calcDistBlock(&voice->distortion,buf,size);
#endif
	//channel volume
	bufferTool_addGain(buf,voice->vol,size);
}
//---------------------------------------------------
