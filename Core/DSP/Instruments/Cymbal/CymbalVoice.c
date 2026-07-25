/*
 * CymbalVoice.c
 *
 *  Created on: 16.06.2012
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


#include "CymbalVoice.h"
#include "squareRootLut.h"
#include "modulationNode.h"
#include "InstrumentManager.h"
// TODO DSP_PORT
// #include "TriggerOut.h"
#include "config.h"

INCCMZ CymbalVoice cymbalVoice;
//---------------------------------------------------
void Cymbal_setPan(const uint8_t pan)
{
	Cymbal_setPanVoice(&cymbalVoice, pan);
}
//---------------------------------------------------
void Cymbal_setPanVoice(CymbalVoice *voice, const uint8_t pan)
{
	/*
	 * Set pan on one explicit cymbal runtime instance.
	 *
	 * Inputs: caller-owned CymbalVoice and raw 0..127 pan. Output: the
	 * instance pan byte changes; mixer translates it to gain. Instrument Load
	 * needs this helper because a cymbal may now occupy either Advanced slot
	 * position rather than only the legacy cymbalVoice global.
	 */
	if(!voice)
		return;
	//voice->panL = squareRootLut[127-pan];
	//voice->panR = squareRootLut[pan];
	voice->pan = pan;
}
//---------------------------------------------------

void Cymbal_initVoice(CymbalVoice *voice)
{
	/*
	 * Initialize one cymbal runtime instance.
	 *
	 * Inputs: caller-owned CymbalVoice. Output: oscillator, transient,
	 * envelope, filter, distortion, and LFO defaults match the legacy
	 * cymbalVoice initialization. InstrumentManager calls this for dynamic
	 * per-slot pools, keeping cymbal defaults local to the cymbal module.
	 */
	if(!voice)
		return;

	SnapEg_init(&voice->snapEg);
	Cymbal_setPanVoice(voice, 64);
	voice->vol = 0.8f;

	//voice->panModifier = 1.f;

	transient_init(&voice->transGen);

	voice->fmModAmount1 = 0.5f;
	voice->fmModAmount2 = 0.5f;

	setDistortionShape(&voice->distortion, 2.f);

	voice->modOsc.freq = 440;
	voice->modOsc.waveform = SINE;
	voice->modOsc.fmMod = 0;
	voice->modOsc.midiFreq = 70<<8;
	voice->modOsc.pitchMod = 1.0f;
	voice->modOsc.modNodeValue = 1;

	voice->modOsc2.freq = 440;
	voice->modOsc2.waveform = NOISE;//SINE;
	voice->modOsc2.fmMod = 0;
	voice->modOsc2.midiFreq = 70<<8;
	voice->modOsc2.pitchMod = 1.0f;
	voice->modOsc2.modNodeValue = 1;

	voice->osc.freq = 440;
	voice->osc.waveform = 1;
	voice->osc.fmMod = 1;
	voice->osc.midiFreq = 70<<8;
	voice->osc.pitchMod = 1.0f;
	voice->osc.modNodeValue = 1;

	voice->volumeMod = 1;

	slopeEg2_init(&voice->oscVolEg);

	SVF_init(&voice->filter);

	lfo_init(&voice->lfo);

}
//---------------------------------------------------
void Cymbal_init()
{
	Cymbal_initVoice(&cymbalVoice);
}
//---------------------------------------------------
void Cymbal_trigger( const uint8_t vel, const uint8_t note)
{
	Cymbal_triggerVoice(&cymbalVoice, 4u, vel, note);
}
//---------------------------------------------------
void Cymbal_triggerVoice(CymbalVoice *voice, const uint8_t source_slot,
                         const uint8_t vel, const uint8_t note)
{
	/*
	 * Trigger one explicit cymbal runtime instance.
	 *
	 * Inputs: CymbalVoice pointer, logical source slot, velocity, and note.
	 * Output: LFO/velocity modulation uses source_slot while the supplied
	 * instance receives oscillator/envelope/transient changes. This lets
	 * InstrumentManager host cymbals in any Advanced-eligible voice slot.
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
		voice->osc.phase = (0x3ff<<20)*offset;//voiceArray[voiceNr].osc.startPhase ;
	else if(voice->osc.waveform > SINE && voice->osc.waveform <= REC)
		voice->osc.phase = (0xff<<20)*offset;
	else
		voice->osc.phase = 0;

	voice->modOsc.phase = 0;
	voice->modOsc2.phase = 0;

	osc_setBaseNote(&voice->osc,note);
	osc_setBaseNote(&voice->modOsc,note);
	osc_setBaseNote(&voice->modOsc2,note);

	slopeEg2_trigger(&voice->oscVolEg);
	voice->velo = vel/127.f;

	transient_trigger(&voice->transGen);

	SnapEg_trigger(&voice->snapEg);
}
//---------------------------------------------------
void Cymbal_calcAsync()
{
	Cymbal_calcAsyncVoice(&cymbalVoice);
}
//---------------------------------------------------
void Cymbal_calcAsyncVoice(CymbalVoice *voice)
{
	/*
	 * Calculate one cymbal instance's control-rate block.
	 *
	 * Inputs: CymbalVoice pointer. Output: envelope, snap, and oscillator
	 * frequencies advance on that instance only. The helper cannot be folded
	 * into Cymbal_calcAsync() because dynamic slots may use a different
	 * CymbalVoice than the legacy global.
	 */
	if(!voice)
		return;
	//calc the osc  vol eg
	voice->egValueOscVol = slopeEg2_calc(&voice->oscVolEg);

	//turn off trigger signal if trigger gate mode is on and volume == 0
	/* TODO DSP_PORT
	if(trigger_isGateModeOn())
	{
		if(!voice->egValueOscVol) {
			trigger_triggerVoice(TRIGGER_5, TRIGGER_OFF);
			voiceControl_noteOff(TRIGGER_5);
		}
	}
	*/

	//calc snap EG if transient sample 0 is activated
	if(voice->transGen.waveform == 0)
	{
		const float snapVal = SnapEg_calc(&voice->snapEg,voice->transGen.pitch);
		voice->osc.pitchMod = 1 + snapVal*voice->transGen.volume;
	}

	//update osc phaseInc
	osc_setFreq(&voice->osc);
	osc_setFreq(&voice->modOsc);
	osc_setFreq(&voice->modOsc2);
}
//---------------------------------------------------
void Cymbal_calcSyncBlock(int16_t* buf, const uint8_t size)
{
	Cymbal_calcSyncBlockVoice(&cymbalVoice, buf, size);
}
//---------------------------------------------------
void Cymbal_calcSyncBlockVoice(CymbalVoice *voice, int16_t* buf,
                               const uint8_t size)
{
	/*
	 * Render one cymbal instance into a mono block.
	 *
	 * Inputs: CymbalVoice pointer, destination buffer, and block size. Output:
	 * buf receives the FM cymbal, transient, envelope, and distortion output
	 * for that instance. InstrumentManager uses this for current slot type
	 * dispatch; the legacy wrapper remains only for old fixed-slot callers.
	 */
	if(!voice || !buf)
		return;
	// VLA problem
	// int16_t mod[size];
	// int16_t mod2[size];

	static int16_t mod[OUTPUT_DMA_SIZE];
	static int16_t mod2[OUTPUT_DMA_SIZE];

	//calc next mod osc sample
	calcNextOscSampleBlock(&voice->modOsc,mod,size,voice->fmModAmount1);
	calcNextOscSampleBlock(&voice->modOsc2,mod2,size,voice->fmModAmount2);

	//combine both mod oscs to 1 modulation signal
	bufferTool_addBuffersSaturating(mod,mod2,size);

	calcNextOscSampleFmBlock(&voice->osc,mod,buf,size,1.f) ;
	SVF_calcBlockZDF(&voice->filter,voice->filterType,buf,size);

	//calc transient sample
	transient_calcBlock(&voice->transGen,mod,size);

	uint8_t j;
	if(voice->volumeMod)
	{
		for(j=0;j<size;j++)
		{
			//add filter to buffer
			buf[j] = bufferTool_satAdd16(buf[j], mod[j]);
			buf[j] *=  voice->velo * voice->vol * voice->egValueOscVol;
		}
	}
	else
	{
		for(j=0;j<size;j++)
		{
			//add filter to buffer
			buf[j] = bufferTool_satAdd16(buf[j], mod[j]);
			buf[j] *=  voice->vol * voice->egValueOscVol;
		}
	}
	calcDistBlock(&voice->distortion,buf,size);
}
//---------------------------------------------------
