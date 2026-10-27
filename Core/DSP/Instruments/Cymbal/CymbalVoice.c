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
#include "voicePostChain.h"
#include "OscillatorSample.h"
#include "OscRenderPool.h"
// TODO DSP_PORT
// #include "TriggerOut.h"
#include "config.h"

//---------------------------------------------------
void Cymbal_setPanVoice(CymbalVoice *voice, const uint8_t pan)
{
	/*
	 * Set pan on one explicit cymbal runtime instance.
	 *
	 * Inputs: caller-owned CymbalVoice and raw 0..127 pan. Output: the
 * instance pan byte changes; mixer translates it to gain. InstrumentManager
 * supplies the current tagged member instead of a fixed cymbal global.
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
 * Inputs: caller-owned CymbalVoice. Output: oscillator, transient, envelope,
 * filter, distortion, and LFO members receive cymbal defaults. The manager
 * calls this for one tagged member, keeping defaults local to the module.
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
	/* S085 P2: no retrigger ramp pending. */
	voice->lastAmpGain = 0.0f;
	voice->ampRampArm = 0u;

	SVF_init(&voice->filter);

	lfo_init(&voice->lfo);
	oscSection_init(&voice->section);

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

	/*
	 * Run mode decides the trigger resets (S085 C11.4).
	 *
	 * What:       rst: today's resets (carrier at its start phase, both
	 *             modulators to 0 / sample start); fre: no reset, all three
	 *             slew; itp: the carrier is snapshotted and reset, the
	 *             modulators slew (so an itp hit's FM attack is not identical
	 *             from hit to hit; rst is, plan §4.3.7).
	 */
	{
		OscInfo* const mods[2] = { &voice->modOsc, &voice->modOsc2 };
		const uint8_t reset = oscSection_beginTrigger(&voice->section,
		                                              &voice->osc, mods, 2u);
		uint8_t m;

		if (reset != OSC_TRIGGER_RESET_NONE) {
			float offset = 1;
			if(voice->transGen.waveform==1) //offset mode
				offset -= voice->transGen.volume;
			if (voice->osc.oscType == OSC_TYPE_SMP ||
			    voice->osc.oscType == OSC_TYPE_SLP)
				oscSample_restart(&voice->osc);
			else if (voice->osc.oscType != OSC_TYPE_WAV ||
			         osc_isTableWave(voice->osc.waveform))
				voice->osc.phase = (0xff<<20)*offset;
			else if (voice->osc.waveform == SINE)
				voice->osc.phase = (0x3ff<<20)*offset;
			else
				voice->osc.phase = 0;
		}
		if (reset == OSC_TRIGGER_RESET_ALL) {
			for (m = 0u; m < 2u; m++) {
				if (mods[m]->oscType == OSC_TYPE_SMP ||
				    mods[m]->oscType == OSC_TYPE_SLP)
					oscSample_restart(mods[m]);
				else
					mods[m]->phase = 0;
			}
		}
	}

	osc_setBaseNote(&voice->osc,note);
	osc_setBaseNote(&voice->modOsc,note);
	osc_setBaseNote(&voice->modOsc2,note);

	/*
	 * Amp envelope retrigger by run mode (S085 P2).
	 *
	 * What:       rst: slopeEg2_trigger(), exactly as before. fre/itp: the
	 *             attack starts at the last block's envelope gain
	 *             (egValueOscVol) and, when audible, the next block ramps
	 *             from the previous amp gain (ampRampArm).
	 * Why:        the decay -> attack curve switch and the per-block constant
	 *             gain stepped the output on the retrigger sample.
	 * Inputs:     section.runMode, egValueOscVol, the envelope.
	 * Outputs:    oscVolEg, ampRampArm.
	 * Affiliates: Cymbal_calcSyncBlockVoice(), slopeEg2_retriggerFrom(),
	 *             slopeEg2_isAudibleGain().
	 */
	if (voice->section.runMode == OSC_RUN_RST) {
		slopeEg2_trigger(&voice->oscVolEg);
	} else {
		const float gainNow = voice->egValueOscVol;

		slopeEg2_retriggerFrom(&voice->oscVolEg, gainNow);
		voice->ampRampArm = slopeEg2_isAudibleGain(gainNow);
	}
	voice->velo = vel/127.f;

	transient_trigger(&voice->transGen);

	SnapEg_trigger(&voice->snapEg);
}
//---------------------------------------------------
void Cymbal_calcAsyncVoice(CymbalVoice *voice)
{
	/*
	 * Calculate one cymbal instance's control-rate block.
	 *
 * Inputs: CymbalVoice pointer. Output: envelope, snap, and oscillator
 * frequencies advance on that tagged instance, independent of any historical
 * physical cymbal slot.
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
void Cymbal_calcSyncBlockVoice(CymbalVoice *voice, int16_t* buf,
                               const uint8_t size)
{
	/*
	 * Render one cymbal instance into a mono, PRE-VOLUME block.
	 *
	 * Inputs: CymbalVoice pointer, destination buffer, and block size. Output:
	 * buf receives the FM cymbal, transient, amp envelope, optional velocity,
	 * and distortion for that tagged instance, but NOT the channel volume
	 * (voice->vol). InstrumentManager selects it through the runtime tag.
	 *
	 * Why (Session 072, Effects Phase 5 step 2; volume-order bug fix D1):
	 * volume is the last stage, a pure output level applied by the mixer
	 * after decimation. It previously multiplied the signal before
	 * calcDistBlock() and so also set the drive (a bug). The FX send taps the
	 * pre-volume signal including distortion. Affiliates:
	 * instrumentManager_runtimeVolume(), mixer_calcNextSampleBlock().
	 */
	if(!voice || !buf)
		return;
	// VLA problem
	// int16_t mod[size];
	// int16_t mod2[size];

	static int16_t mod[OUTPUT_DMA_SIZE];
	static int16_t mod2[OUTPUT_DMA_SIZE];

	/* S085 C11.4: two-modulator FM through OscSection (bit-identical with no
	** crossfade); itp renders the old carrier with the same summed modulator
	** and crossfades. */
	{
		OscInfo* const old = oscSection_itpCarrier(&voice->section);
		int16_t* const oldBuf = old ? oscRenderPool_scratch() : 0;

		oscSection_renderFm2(&voice->osc, &voice->modOsc, &voice->modOsc2,
		                     voice->fmModAmount1, voice->fmModAmount2, 1.f,
		                     buf, mod, mod2, size, old, oldBuf);
		if (old)
			oscSection_blendItp(&voice->section, buf, oldBuf, size);
	}
	SVF_calcBlockZDF(&voice->filter,voice->filterType,buf,size);

	//calc transient sample
	transient_calcBlock(&voice->transGen,mod,size);

	/*
	 * Fused Cymbal post-chain (S073 Step 4).
	 *
	 * What:       saturating add, amp gain and distortion in one pass.
	 * Why:        removes an intermediate post-chain pass while preserving the
	 *             old int16 conversion/saturation points and constant cost (S0).
	 * Inputs:     filtered buf, mod, volumeMod/velo/EG and distortion.
	 * Outputs:    pre-volume buf.
	 * Accessors:  this render function.
	 * Affiliates: voicePostChain.h and the Step 4 host/ARM checks.
	 */
	/*
	 * Amp ramp endpoints and the arm (S085 P2).
	 *
	 * What:       as Snare (C4.4): the armed retrigger block ramps from
	 *             lastAmpGain; other blocks are flat; then record and clear.
	 * Why:        removes the retrigger step in fre/itp; rst unchanged.
	 * Inputs:     volumeMod, velo, egValueOscVol, lastAmpGain, ampRampArm.
	 * Outputs:    buf; lastAmpGain; ampRampArm = 0.
	 * Affiliates: voicePost_addGainDist(), Cymbal_triggerVoice().
	 */
	{
		const float ampGain = voice->volumeMod
			? voice->velo * voice->egValueOscVol
			: voice->egValueOscVol;
		const float lastAmpGain = voice->ampRampArm
			? voice->lastAmpGain : ampGain;

		voicePost_addGainDist(buf, mod, ampGain, lastAmpGain,
		                      &voice->distortion, size);
		voice->lastAmpGain = ampGain;
		voice->ampRampArm = 0u;
	}
	oscSection_finishBlock(&voice->section, size);
}
//---------------------------------------------------
