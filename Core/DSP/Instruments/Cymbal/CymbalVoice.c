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
	/*
	 * Modulator roles (S087 stage B, A22).
	 *
	 * What:       marks both Mod Osc oscillators as OSC_TYPE_MODLIST: their wav
	 *             list is the stock waves followed by the user entries (S087
	 *             P0 §12.4); they have no typ or pos row.
	 * Why:        osc_setFreq() and the dispatcher's user branch read the mark
	 *             to bind a list entry; a runtime-slot reset (memset + init)
	 *             restores it.
	 * Affiliates: OSC_TYPE_MODLIST; osc_userListBind() (stage D).
	 */
	voice->modOsc.oscType = OSC_TYPE_MODLIST;
	voice->modOsc2.oscType = OSC_TYPE_MODLIST;

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

	/* Run mode default (S088 P0 §2.4): 0 rst, today's behaviour. */
	voice->runMode = OSC_RUN_RST;
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
 * What:       copies the carrier state into voice->snapshot and activates it
 *             for OSC_RUN_ITP_FADE_BLOCKS blocks. The modulator oscillators
 *             are deliberately NOT snapshotted (A4): they keep running and
 *             serve only the new carrier.
 * Why:        the old carrier plays beside the reset carrier for about 3 ms;
 *             a plain render needs only the copied fields.
 * Inputs:     voice->osc.  Outputs: voice->snapshot.
 * Accessors:  Cymbal_triggerVoice() (itp, audible, slot granted).
 * Affiliates: OscCarrierSnapshot; oscSection_renderOldCarrier().
 */
static void Cymbal_fillItpSnapshot(CymbalVoice *voice)
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
	 * Run-mode decision for this retrigger (S088 P0 §1, §3 Layers 2a/2c/3a).
	 *
	 * What:       ampAudible from the last block's egValueOscVol; itpRequested;
	 *             behavesAsRst (rst, or itp with a decayed amp = exactly rst);
	 *             the itp snapshot is taken before the phase reset; phaseReset
	 *             is never set in fre and not set when an itp fade was refused.
	 * Why:        fre keeps the carrier running; itp restarts it while the old
	 *             copy crossfades; the modulators follow behavesAsRst (they keep
	 *             running in fre and in an itp fade).
	 * Inputs:     runMode, egValueOscVol, the itp slot query.
	 * Outputs:    ampRampArm, snapshot and the local decisions.
	 * Accessors:  this trigger only.
	 * Affiliates: slopeEg2_isAudibleGain(), slopeEg2_retriggerFrom(),
	 *             instrumentManager_itpFadeSlotAvailable(); A4.
	 */
	const float prevGain = voice->egValueOscVol;
	const uint8_t ampAudible = slopeEg2_isAudibleGain(prevGain);
	const uint8_t itpRequested =
		(uint8_t)(voice->runMode == OSC_RUN_ITP && ampAudible);
	const uint8_t behavesAsRst =
		(uint8_t)(voice->runMode == OSC_RUN_RST ||
		          (voice->runMode == OSC_RUN_ITP && !itpRequested));
	uint8_t itpFade = 0u;

	voice->ampRampArm = 0u;
	if (itpRequested && instrumentManager_itpFadeSlotAvailable(source_slot)) {
		Cymbal_fillItpSnapshot(voice);
		itpFade = 1u;
	}
	const uint8_t phaseReset =
		(uint8_t)(voice->runMode != OSC_RUN_FRE && !(itpRequested && !itpFade));

	if (phaseReset) {
	float offset = 1;
	if(voice->transGen.waveform==1) //offset mode
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
	 *             The modulators are reset to phase 0 just below, as before.
	 * Inputs:     voice->osc.position (velocity-modulated), osc.waveform.
	 * Outputs:    voice->osc.phase, voice->osc.positionApplied.
	 * Accessors:  this trigger.
	 * Affiliates: osc_positionTrigger() (Oscillator.h); the per-block bend
	 *             in Cymbal_calcSyncBlockVoice(); the osc1_position row.
	 */
	voice->osc.phase += osc_positionTrigger(&voice->osc);
	}
	}

	/*
	 * Modulator phase reset (S088 P0 §3 Layer 2c, A4).
	 * What: rst (and itp with a decayed amp, which is exactly rst) restarts
	 *       both modulators as before; fre and an audible itp keep them
	 *       running, so they slew to the new pitch instead of stepping.
	 * Why:  the crossfade is carrier-only (Q14); restarting the modulators
	 *       would add an unrelated transient under it.
	 */
	if (behavesAsRst) {
		voice->modOsc.phase = 0;
		voice->modOsc2.phase = 0;
	}

	osc_setBaseNote(&voice->osc,note);
	osc_setBaseNote(&voice->modOsc,note);
	osc_setBaseNote(&voice->modOsc2,note);

	/*
	 * Gain-matched amp retrigger (S088 P0 §3 Layer 2a).
	 * What: fre/itp with an audible tail restart the attack at prevGain and
	 *       arm the one-block post-chain ramp; everything else keeps
	 *       slopeEg2_trigger() (rst, decayed tail, repeat fallback).
	 * Inputs: runMode, ampAudible, prevGain. Outputs: oscVolEg, ampRampArm.
	 * Affiliates: slopeEg2_retriggerFrom(); voicePost_addGainDist().
	 */
	if (voice->runMode != OSC_RUN_RST && ampAudible) {
		slopeEg2_retriggerFrom(&voice->oscVolEg, prevGain);
		voice->ampRampArm = 1u;
	} else {
		slopeEg2_trigger(&voice->oscVolEg);
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

	//calc next mod osc sample
	calcNextOscSampleBlock(&voice->modOsc,mod,size,voice->fmModAmount1);
	calcNextOscSampleBlock(&voice->modOsc2,mod2,size,voice->fmModAmount2);

	//combine both mod oscs to 1 modulation signal
	bufferTool_addBuffersSaturating(mod,mod2,size);

	/*
	 * osc1 position bend around the FM render below (S086 P3).
	 *
	 * What:       osc_positionBendApply() adds this block's change of pos to
	 *             osc.phaseInc (circular, capped by OSC_POS_BEND_CAP) and
	 *             stores the step in osc.positionBendStep; the existing FM
	 *             render then glides the carrier's phase to the new offset over
	 *             the block, and osc_positionBendRestore() removes the bend
	 *             exactly.
	 * Why:        changing phase directly would click; bending phaseInc for one
	 *             block is continuous and needs no loop change (086 log §4.3).
	 *             The step lives in the oscillator, not a local, so nothing
	 *             stays live across the render LTO inlines into the mixer
	 *             (assessment F1). modOsc and modOsc2 are separate OscInfo and
	 *             are not bent (their pos rows wait for the typ step, 086 log §4.8).
	 * Inputs:     voice->osc position state; osc.phaseInc from osc_setFreq() in
	 *             Cymbal_calcAsyncVoice() this block.
	 * Outputs:    buf (the carrier block, rendered with the bend); osc.phaseInc
	 *             unchanged afterwards; osc.positionApplied.
	 * Accessors:  this function only.
	 * Affiliates: osc_positionBendApply()/Restore() (Oscillator.h);
	 *             Cymbal_triggerVoice() (osc_positionTrigger()).
	 */
	osc_positionBendApply(&voice->osc, 1u);
	if (osc_isTyped(&voice->osc))
		osc_renderTypedFm(&voice->osc, mod, buf, size, 1.f);
	else
		calcNextOscSampleFmBlock(&voice->osc,mod,buf,size,1.f) ;
	osc_positionBendRestore(&voice->osc);	/* S086 P3: exact restore */
	/*
	 * itp crossfade blend (S088 P0 §3 Layers 3a/3c).
	 *
	 * What:       while snapshot.active, render the frozen carrier plainly and
	 *             crossfade the carrier block (buf) toward it over 132 samples;
	 *             the helper advances the snapshot and releases it.
	 * Why:        removes the click of the reset carrier; the modulators keep
	 *             driving only the live carrier (carrier-only, Q14).
	 * Inputs:     voice->snapshot, buf, size.  Outputs: buf blended.
	 * Accessors:  this render.  Affiliates: oscSection_renderOldCarrier().
	 */
	if (voice->snapshot.active)
		oscSection_renderOldCarrier(&voice->snapshot, buf, size);
	SVF_calcBlockZDF(&voice->filter,voice->filterType,buf,size);

	//calc transient sample
	transient_calcBlock(&voice->transGen,mod,size);

	/*
	 * Fused Cymbal post-chain (S073 Step 4; amp ramp S088 P0 §3 Layer 2b).
	 *
	 * What:       saturating add, amp gain ramp and distortion in one pass. The
	 *             ramp start is the previous block's ampGain only when the
	 *             trigger armed it; otherwise the endpoints are equal and the
	 *             samples are the S073 ones exactly.
	 * Why:        removes an intermediate post-chain pass while preserving the
	 *             old int16 conversion/saturation points and constant cost (S0).
	 * Inputs:     filtered buf, mod, volumeMod/velo/EG and distortion.
	 * Outputs:    pre-volume buf; lastAmpGain updated; ampRampArm cleared.
	 * Accessors:  this render function through InstrumentManager.
	 * Affiliates: voicePostChain.h, voicePost_addGainDist(), the
	 *             lastAmpGain/ampRampArm fields and the Step 4 host/ARM checks.
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
}
//---------------------------------------------------
