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

	lfo_init(&voice->lfo);
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

	float offset = 1;
	if(voice->transGen.waveform==1) //offset mode
	{
		offset -= voice->transGen.volume;
	}
	if(voice->osc.waveform == SINE)
		voice->osc.phase = (0x3ff<<20)*offset;
	else if(voice->osc.waveform > SINE && voice->osc.waveform <= REC)
		voice->osc.phase = (0xff<<20)*offset;
	else
		voice->osc.phase = 0;

	DecayEg_trigger(&voice->oscPitchEg);
	slopeEg2_trigger(&voice->oscVolEg);
	voice->velo = vel/127.f;

	osc_setBaseNote(&voice->osc,note);
	//TODO noise muss mit transponiert werden

	transient_trigger(&voice->transGen);

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
	if(voice->transGen.waveform == 0)
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
	 * Render one snare instance into a mono block.
	 *
	 * Inputs: SnareVoice pointer, destination buffer, and block size. Output:
 * buf receives noise, oscillator, transient, envelope, and distortion for
 * that tagged instance without relying on a fixed snare wrapper.
	 */
	if(!voice || !buf)
		return;
	static int16_t transBuf[OUTPUT_DMA_SIZE];


	calcNoiseBlock(&voice->noiseOsc,buf,size,0.9f);
	SVF_calcBlockZDF(&voice->filter,voice->filterType,buf,size);

	//calc transient sample
	transient_calcBlock(&voice->transGen,transBuf,size);
	bufferTool_addBuffersSaturating(buf,transBuf,size);

	//calc next osc sample
	calcNextOscSampleBlock(&voice->osc,transBuf,size,(1.f-voice->mix));
	//--AS apply filter to synthesized sound as well here if desired, or combine code for more efficiency

	uint8_t j;
	if(voice->volumeMod)
	{
		for(j=0;j<size;j++)
		{
			//add filter to buffer
			buf[j] *= voice->mix;
			buf[j] = bufferTool_satAdd16(buf[j], transBuf[j]);
			buf[j] *=  voice->velo * voice->vol * voice->egValueOscVol;
		}
	}
	else
	{
		for(j=0;j<size;j++)
		{
			//add filter to buffer
			buf[j] *= voice->mix;
			buf[j] = bufferTool_satAdd16(buf[j], transBuf[j]);
			buf[j] *=  voice->vol * voice->egValueOscVol;
		}
	}

	calcDistBlock(&voice->distortion,buf,size);
}
//------------------------------------------------------------------------
