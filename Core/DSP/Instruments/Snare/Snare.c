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
#include "voiceFold.h"   /* S089 P2: VOICE_CHAIN_*, voiceFold_process() */
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
	/*
	 * Run mode default (S088 P0 §2.4): 0 rst, today's behaviour.
	 * What: one store so a runtime-slot reset starts in rst, which is what a
	 * file without the osc1_run key must load.
	 * Accessors: Snare_triggerVoice(), Snare_calcSyncBlockVoice().
	 */
	voice->runMode = OSC_RUN_RST;
	/* S089 P2: default chain (T+D). The slot reset memset already cleared it;
	 * the explicit store documents the default like runMode above. Under WFd
	 * transGen.midiFreq (the fold's last depth) relies on the same memset
	 * being 0 = "no previous depth" (voiceFold_process()). */
	voice->chainType = VOICE_CHAIN_DEFAULT;
	/* S088 P0: no armed ramp until a fre/itp retrigger asks for one. */
	voice->ampRampArm = 0u;
	voice->lastAmpGain = 0.0f;
	voice->snapshot.active = 0u;
	voice->snapshot.counter = 0u;
}
//---------------------------------------------------
/*
 * Fill the itp old-carrier snapshot (S088 P0 §3 Layer 3a).
 *
 * What:       copies osc's phase/phaseInc/waveform/table/type/frozen-pos and
 *             sample bind fields into voice->snapshot and activates it for
 *             OSC_RUN_ITP_FADE_BLOCKS blocks.
 * Why:        the old carrier keeps playing beside the reset live carrier for
 *             about 3 ms; the snapshot is the only state the plain renderer
 *             needs. Taken before the phase reset (Snare_triggerVoice()).
 * Inputs:     voice->osc.  Outputs: voice->snapshot.
 * Accessors:  Snare_triggerVoice() (itp, audible, slot granted).
 * Affiliates: OscCarrierSnapshot; oscSection_renderOldCarrier().
 */
static void Snare_fillItpSnapshot(SnareVoice *voice)
{
	voice->snapshot.phase = voice->osc.phase;
	voice->snapshot.phaseInc = voice->osc.phaseInc;
	voice->snapshot.waveform = voice->osc.waveform;
	voice->snapshot.tableOffset = voice->osc.tableOffset;
	voice->snapshot.oscType = voice->osc.oscType;
	voice->snapshot.sampleCacheIndex = voice->osc.sampleCacheIndex;
	voice->snapshot.sampleCacheLooped = voice->osc.sampleCacheLooped;
	voice->snapshot.sampleCacheGeneration = voice->osc.sampleCacheGeneration;
	voice->snapshot.sampleCacheSize = voice->osc.sampleCacheSize;
	voice->snapshot.sampleCacheData = voice->osc.sampleCacheData;
	voice->snapshot.positionApplied = voice->osc.positionApplied;
	voice->snapshot.active = 1u;
	voice->snapshot.counter = OSC_RUN_ITP_FADE_BLOCKS;
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
	 * Run-mode decision for this retrigger (S088 P0 §1, §3 Layers 2a/2c/3a).
	 *
	 * What:       reads the amp gain of the last block (egValueOscVol), derives
	 *             ampAudible, itpRequested, takes the itp snapshot BEFORE the
	 *             phase reset when a slot is free, and derives phaseReset
	 *             (never in fre; an itp fade that was refused behaves as fre).
	 * Why:        fre must keep the carrier running; itp restarts the live
	 *             carrier while the snapshot crossfades the old one out.
	 * Inputs:     runMode, egValueOscVol, the itp slot query.
	 * Outputs:    ampRampArm, snapshot and the local decisions.
	 * Accessors:  this trigger only.
	 * Affiliates: slopeEg2_isAudibleGain(), slopeEg2_retriggerFrom(),
	 *             instrumentManager_itpFadeSlotAvailable().
	 */
	const float prevGain = voice->egValueOscVol;
	const uint8_t ampAudible = slopeEg2_isAudibleGain(prevGain);
	const uint8_t itpRequested =
		(uint8_t)(voice->runMode == OSC_RUN_ITP && ampAudible);
	uint8_t itpFade = 0u;

	voice->ampRampArm = 0u;
	if (itpRequested && instrumentManager_itpFadeSlotAvailable(source_slot)) {
		Snare_fillItpSnapshot(voice);
		itpFade = 1u;
	}
	const uint8_t phaseReset =
		(uint8_t)(voice->runMode != OSC_RUN_FRE && !(itpRequested && !itpFade));

	if (phaseReset) {
	float offset = 1;
	/* Ofs belongs to T+D only (S089 P2; see DrumVoice.c offset guard). */
	if(voice->chainType == VOICE_CHAIN_DEFAULT &&
	   voice->transGen.waveform==1) //offset mode
	{
		offset -= voice->transGen.volume;
	}
	/* S087 D: a typed osc1 starts at its pos start point. */
	if (osc_isTyped(&voice->osc))
		voice->osc.phase = osc_typedTriggerPhase(&voice->osc);
	else {
	if(voice->osc.waveform == SINE)
		voice->osc.phase = (0x3ff<<20)*offset;
	else if(osc_isTableWave(voice->osc.waveform))
		voice->osc.phase = (0xff<<20)*offset;
	else
		voice->osc.phase = 0;

	/*
	 * Apply osc1 position at the hit (S086 P3/P4).
	 *
	 * What:       adds osc_positionTrigger() to the start phase set above:
	 *             position << 25 (position/128 of the phase range) for Sin,
	 *             Tri, Saw, Rec, Re3, Sw3 and Cym (the whole crash sample),
	 *             0 for noise and v1 user samples. The helper also sets
	 *             positionApplied = position, so the first block does not bend.
	 * Why:        the hit starts exactly at today's start phase + pos;
	 *             position 0 adds 0, so today's start is kept bit for bit.
	 *             It is added after the transient offset-mode scaling
	 *             (086 log §4.2). Velocity modulation has already run, so a
	 *             velocity target on pos is exact for this hit.
	 * Inputs:     voice->osc.position (velocity-modulated), osc.waveform.
	 * Outputs:    voice->osc.phase, voice->osc.positionApplied.
	 * Accessors:  this trigger.
	 * Affiliates: osc_positionTrigger() (Oscillator.h); the per-block bend
	 *             in Snare_calcSyncBlockVoice(); the osc1_position row.
	 */
	voice->osc.phase += osc_positionTrigger(&voice->osc);
	}
	}

	DecayEg_trigger(&voice->oscPitchEg);
	/*
	 * Gain-matched amp retrigger (S088 P0 §3 Layer 2a).
	 * What: in fre/itp with an audible tail, restart the attack at prevGain
	 *       and arm the one-block post-chain ramp; otherwise (rst, decayed
	 *       tail, repeat fallback) use slopeEg2_trigger() as before.
	 * Why:  the attack enters where the decay left off, so the retrigger does
	 *       not cut the tail to silence for a block.
	 * Inputs: runMode, ampAudible, prevGain. Outputs: oscVolEg, ampRampArm.
	 * Affiliates: slopeEg2_retriggerFrom(); voicePost_mixAddGainDist().
	 */
	if (voice->runMode != OSC_RUN_RST && ampAudible) {
		slopeEg2_retriggerFrom(&voice->oscVolEg, prevGain);
		voice->ampRampArm = 1u;
	} else {
		slopeEg2_trigger(&voice->oscVolEg);
	}
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

	/* Snp belongs to T+D only; under WFd snapEg is the fold's decay (S089 P2). */
	if(voice->chainType == VOICE_CHAIN_DEFAULT && voice->transGen.waveform == 0)
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
	SVF_calcBlockZDF(&voice->filter,voice->filterType,buf,size);

	/*
	 * Transient (T+D only) (S089 P2).
	 * What: T+D adds the click to the filtered noise as before (S0). WFd has
	 *       no transient: its fold runs on the tone oscillator below (D1).
	 * Why:  Snare's only pre-filter signal is noise; folding it does little.
	 */
	if (voice->chainType != VOICE_CHAIN_WAVEFOLDER) {
		//calc transient sample
		transient_calcBlock(&voice->transGen,transBuf,size);
		bufferTool_addBuffersSaturating(buf,transBuf,size);
	}

	/*
	 * osc1 position bend around the render below (S086 P3).
	 *
	 * What:       osc_positionBendApply() adds this block's change of pos to
	 *             osc.phaseInc (circular, capped by OSC_POS_BEND_CAP) and
	 *             stores the step in osc.positionBendStep; the existing render
	 *             then glides the phase to the new offset over the block, and
	 *             osc_positionBendRestore() removes the bend exactly.
	 * Why:        changing phase directly would click; bending phaseInc for one
	 *             block is continuous and needs no loop change (086 log §4.3).
	 *             The step lives in the oscillator, not a local, so nothing
	 *             stays live across the render LTO inlines into the mixer
	 *             (assessment F1). noiseOsc is a separate OscInfo, not bent.
	 * Inputs:     voice->osc position state; osc.phaseInc from osc_setFreq() in
	 *             Snare_calcAsyncVoice() this block.
	 * Outputs:    transBuf (the oscillator block, rendered with the bend);
	 *             osc.phaseInc unchanged afterwards; osc.positionApplied.
	 * Accessors:  this function only.
	 * Affiliates: osc_positionBendApply()/Restore() (Oscillator.h);
	 *             Snare_triggerVoice() (osc_positionTrigger()).
	 */
	osc_positionBendApply(&voice->osc, 1u);
	//calc next osc sample
	if (osc_isTyped(&voice->osc))
		osc_renderTyped(&voice->osc, transBuf, size, 1.f - voice->mix);
	else
		calcNextOscSampleBlock(&voice->osc,transBuf,size,(1.f-voice->mix));
	osc_positionBendRestore(&voice->osc);	/* S086 P3: exact restore */
	//--AS apply filter to synthesized sound as well here if desired, or combine code for more efficiency
	/*
	 * itp crossfade blend (S088 P0 §3 Layers 3a/3c).
	 *
	 * What:       while snapshot.active, render the frozen carrier plainly and
	 *             crossfade the carrier block (transBuf) toward it over 132
	 *             samples; the helper advances the snapshot and releases it.
	 * Why:        removes the click of the reset carrier; carrier-only (Q14).
	 *             The noise oscillator keeps running (A7: slew, no snapshot).
	 * Inputs:     voice->snapshot, transBuf, size.
	 * Outputs:    transBuf blended; snapshot advanced.
	 * Accessors:  this render.  Affiliates: oscSection_renderOldCarrier().
	 */
	if (voice->snapshot.active)
		oscSection_renderOldCarrier(&voice->snapshot, transBuf, size);

	/*
	 * WFd: fold the tone oscillator (S089 P2, D1).
	 *
	 * What:       folds transBuf (the tone oscillator, already blended by
	 *             itp) in place before the post-chain mixes it with the noise.
	 * Why:        the tone oscillator is Snare's tonal, unfiltered body. It is
	 *             rendered at gain (1 - mix), which would make the fold depth
	 *             follow the noise mix; inGain = 1/(1 - mix) restores full
	 *             scale into the fold (capped at VOICE_FOLD_IN_GAIN_MAX = 16,
	 *             i.e. mix > 0.9375, where the tone is near silence) and
	 *             outGain = (1 - mix) restores the mix level after it. Both
	 *             are per-block constants inside voiceFold_process(), so they
	 *             cost nothing per sample.
	 * Inputs:     transBuf, voice->mix (0..1), transGen/snapEg (fold state).
	 * Outputs:    transBuf folded; read by voicePost_mixAddGainDist() below.
	 * Affiliates: voiceFold_process(); the osc render above (1 - mix gain).
	 */
	if (voice->chainType == VOICE_CHAIN_WAVEFOLDER) {
		const float tone = 1.0f - voice->mix;

		voiceFold_process(&voice->transGen, &voice->snapEg, transBuf, size,
		                  (tone > (1.0f / VOICE_FOLD_IN_GAIN_MAX))
		                      ? (1.0f / tone) : VOICE_FOLD_IN_GAIN_MAX,
		                  tone);
	}

	/*
	 * Fused Snare post-chain (S073 Step 4; amp ramp S088 P0 §3 Layer 2b).
	 *
	 * What:       mix gain, saturating add, amp gain ramp and distortion in one
	 *             pass. The ramp start is the previous block's ampGain only
	 *             when the trigger armed it; otherwise the endpoints are equal
	 *             and the samples are the S073 ones exactly.
	 * Why:        removes one intermediate post-chain pass while preserving
	 *             the old int16 conversion and saturation points (S0).
	 * Inputs:     filtered buf, transBuf, mix, volumeMod/velo/EG and distortion.
	 * Outputs:    pre-volume buf; lastAmpGain updated; ampRampArm cleared.
	 * Accessors:  this render function through InstrumentManager.
	 * Affiliates: voicePostChain.h, voicePost_mixAddGainDist(), Snare's
	 *             lastAmpGain/ampRampArm and the Step 4 host/ARM checks.
	 */
	{
		const float ampGain = voice->volumeMod
			? voice->velo * voice->egValueOscVol
			: voice->egValueOscVol;
		const float lastAmpGain = voice->ampRampArm
			? voice->lastAmpGain : ampGain;

		voicePost_mixAddGainDist(buf, transBuf, voice->mix, ampGain,
		                         lastAmpGain,
		                         &voice->distortion, size);
		voice->lastAmpGain = ampGain;
		voice->ampRampArm = 0u;
	}
}
//------------------------------------------------------------------------
