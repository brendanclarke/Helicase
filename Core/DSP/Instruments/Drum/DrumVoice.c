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
	/*
	 * Modulator role (S087 stage B, A22).
	 *
	 * What:       marks the FM-page oscillator as OSC_TYPE_MODLIST: its wav
	 *             list is the stock waves followed by the user entries (S087
	 *             P0 §12.4); it has no typ or pos row.
	 * Why:        osc_setFreq() and the dispatcher's user branch read the mark
	 *             to bind a list entry; a runtime-slot reset (memset + init)
	 *             restores it.
	 * Affiliates: OSC_TYPE_MODLIST; osc_userListBind() (stage D).
	 */
	voice->modOsc.oscType = OSC_TYPE_MODLIST;

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
	/*
	 * Run mode default (S088 P0 §2.4): 0 rst, today's behaviour.
	 * What: one store so a runtime-slot reset (memset + init) starts in rst.
	 * Why: a file or Scene without the osc1_run key must load exactly the
	 * pre-S088 sound.
	 * Accessors: Drum_triggerVoice(), Drum_calcVoiceSyncBlock().
	 */
	voice->runMode = OSC_RUN_RST;
	/* S088 P0: no armed ramp until a fre/itp retrigger asks for one; the
	** first rendered block starts from a full-scale velocity endpoint. */
	voice->ampRampArm = 0u;
	voice->lastVeloGain = 1.0f;
	/* S088 P0: no itp fade and no FM depth history at reset. */
	voice->snapshot.active = 0u;
	voice->snapshot.counter = 0u;
	voice->lastFmDepth = 0.0f;
	voice->decimationCnt = 0;
	voice->decimationRate = 1;
}
//---------------------------------------------------
/*
 * Fill the itp old-carrier snapshot (S088 P0 §3 Layer 3a).
 *
 * What:       copies the carrier's current phase, phase increment, waveform,
 *             table, type, frozen pos frame and sample bind fields into
 *             voice->snapshot, then activates it for
 *             OSC_RUN_ITP_FADE_BLOCKS whole blocks.
 * Why:        the old carrier must continue from exactly where it was while
 *             the trigger resets the live carrier; the snapshot is the only
 *             state oscSection_renderOldCarrier() needs for a plain render.
 *             It is taken BEFORE the phase reset (see Drum_triggerVoice()).
 * Inputs:     voice->osc (the live carrier, untouched by this call).
 * Outputs:    voice->snapshot (active, counter = 5).
 * Accessors:  Drum_triggerVoice() when runMode is itp, the amp is audible and
 *             instrumentManager_itpFadeSlotAvailable() approves.
 * Affiliates: OscCarrierSnapshot; OSC_RUN_ITP_*; oscSection_renderOldCarrier().
 */
static void Drum_fillItpSnapshot(DrumVoice *voice)
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
	 * Run-mode decision for this retrigger (S088 P0 §1, §3 Layers 2a/2c/3a).
	 *
	 * What:       reads the amp gain the voice was playing at (ampFilterInput,
	 *             set by the last Drum_calcVoiceAsync()), decides whether the
	 *             amp is audible, whether this is an itp retrigger that may
	 *             crossfade, and derives the two behaviours below:
	 *               phaseReset   the main carrier starts at its trigger
	 *                            phase (rst; itp with a requested fade; itp
	 *                            that is fully decayed); fre and a refused itp
	 *                            fade keep the running phase;
	 *               behavesAsRst the mode is rst, or itp with a decayed amp
	 *                            (D6/A3: bit-identical rst behaviour: filter
	 *                            clear, ring snap, fresh attack).
	 *             The snapshot is taken HERE, before the phase reset below.
	 * Why:        fre must not reset the phase; itp resets the live carrier but
	 *             keeps the old one alive in the snapshot; a refused itp falls
	 *             back to fre (no crossfade, no reset). The amp ramp arm records
	 *             that this block must glide from the previous gains.
	 * Inputs:     voice->runMode, ampFilterInput, and the itp slot query.
	 * Outputs:    voice->ampRampArm, voice->snapshot, and the local decisions.
	 * Accessors:  this trigger only.
	 * Affiliates: slopeEg2_isAudibleGain(), slopeEg2_retriggerFrom(),
	 *             instrumentManager_itpFadeSlotAvailable(),
	 *             Drum_fillItpSnapshot(), Drum_calcVoiceSyncBlock().
	 */
	const float prevGain = voice->ampFilterInput;
	const uint8_t ampAudible = slopeEg2_isAudibleGain(prevGain);
	const uint8_t itpRequested =
		(uint8_t)(voice->runMode == OSC_RUN_ITP && ampAudible);
	const uint8_t behavesAsRst =
		(uint8_t)(voice->runMode == OSC_RUN_RST ||
		          (voice->runMode == OSC_RUN_ITP && !itpRequested));
	uint8_t itpFade = 0u;

	voice->ampRampArm = 0u;
	if (itpRequested && instrumentManager_itpFadeSlotAvailable(source_slot)) {
		Drum_fillItpSnapshot(voice);
		itpFade = 1u;
	}

	/* Phase reset guard (S088 P0 §3 Layer 2c): fre never resets; itp resets
	** unless its fade was refused (then it behaves as fre). */
	const uint8_t phaseReset =
		(uint8_t)(voice->runMode != OSC_RUN_FRE && !(itpRequested && !itpFade));

	/*
	 * Start-phase block (S088 P0 §3 Layer 2c).
	 * What: the S085/S086/S087 phase chain above runs only when the legacy
	 *       "envelope closed" gate allows it (amp-filter builds) AND the run
	 *       mode asks for a reset. Without USE_AMP_FILTER the legacy gate is
	 *       open, exactly as before.
	 * Why:  fre keeps the carrier running; itp with a granted fade starts the
	 *       new carrier from 0 (the snapshot holds the old one); itp that is
	 *       decayed or refused follows the rst/fre rule above.
	 */
#ifdef USE_AMP_FILTER
	const uint8_t ampClosed =
		((voice->volEgValueBlock[15] <= 0.01f) ||
		 (voice->transGen.waveform == 1)) ? 1u : 0u;
#else
	const uint8_t ampClosed = 1u;
#endif
	if(ampClosed && phaseReset)
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
		/*
		 * Typed osc1 start (S087 stage D).
		 *
		 * What:       a typed osc1 starts at osc_typedTriggerPhase(): the pos
		 *             start point (D1), no transient offset; wav keeps the
		 *             chain below unchanged.
		 * Why:        the chain below tests waveform, which is a sample index
		 *             under smp/slp.
		 * Affiliates: osc_typedTriggerPhase(); D-6.
		 */
		if (osc_isTyped(&voice->osc))
			voice->osc.phase = osc_typedTriggerPhase(&voice->osc);
		else {
		if(voice->osc.waveform == SINE)
			voice->osc.phase = 1024 + ( (0x3ff<<20) - 1024)*offset;//voice->osc.startPhase ;
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
		 *             positionApplied = position, so the first block after the
		 *             hit does not bend.
		 * Why:        the hit starts exactly at today's start phase + pos;
		 *             position 0 adds 0, so today's start is kept bit for bit.
		 *             It is added after the transient offset-mode scaling
		 *             (086 log §4.2). Velocity modulation has already run at the
		 *             top of this function, so a velocity target on pos is exact
		 *             for this hit.
		 * Inputs:     voice->osc.position (velocity-modulated), osc.waveform.
		 * Outputs:    voice->osc.phase, voice->osc.positionApplied.
		 * Accessors:  this trigger.
		 * Affiliates: osc_positionTrigger() (Oscillator.h); the per-block bend
		 *             in Drum_calcVoiceSyncBlock(); the osc1_position row.
		 */
		voice->osc.phase += osc_positionTrigger(&voice->osc);
		}

	}

	osc_setBaseNote(&voice->osc,note);
	osc_setBaseNote(&voice->modOsc,note);
	/*
	 * Restart a one-shot modulator (S087 P3).
	 *
	 * What:       when osc2's wav selects an s entry, its phase returns to 0
	 *             so the sample starts again on this hit; any other osc2
	 *             selection keeps today's free-running phase.
	 * Why:        osc2 never resets, so a one-shot played once and then stayed
	 *             at its silent end pad (S087 P3 hardware report).
	 * Affiliates: osc_modulatorTrigger() (Oscillator.c).
	 */
	osc_modulatorTrigger(&voice->modOsc);


	DecayEg_trigger(&voice->oscPitchEg);
	/*
	 * rng depth snap vs ramp (S085 P1 follow-up; S088 P0 A8/D7).
	 * What: rst (and itp with a decayed amp, which is exactly rst) snaps the
	 *       ring depth to the new target; fre and an audible itp ramp from the
	 *       last block's depth ("ramp always in free-run").
	 * Why:  snapping a decayed tail to full ring is a click in fre; the user
	 *       asked for the ramp in free-run mode (S085 P1 follow-up).
	 * Inputs: behavesAsRst. Outputs: voice->ringSnap.
	 * Accessors: Drum_calcVoiceSyncBlock() (rng branch).
	 */
	voice->ringSnap = behavesAsRst;
	/*
	 * Gain-matched amp retrigger (S088 P0 §3 Layer 2a).
	 *
	 * What:       in fre/itp with an audible tail, restart the attack at the
	 *             gain the voice was playing (slopeEg2_retriggerFrom()) and arm
	 *             the one-block post-chain/FM ramps. Every other case (rst, a
	 *             decayed tail, repeat mode inside the helper) uses the original
	 *             slopeEg2_trigger() from silence.
	 * Why:        a retrigger into a tail must not click; the attack curve
	 *             enters where the decay left off and the armed ramps absorb the
	 *             rounding step.
	 * Inputs:     voice->runMode, ampAudible, prevGain.
	 * Outputs:    voice->oscVolEg state, voice->ampRampArm.
	 * Math:       v = g / (1 + invSlope (1 - g)) - the inverse attack curve.
	 * Affiliates: slopeEg2_retriggerFrom(); slopeEg2_isAudibleGain();
	 *             voicePost_drum(); the FM depth ramp.
	 */
	if (voice->runMode != OSC_RUN_RST && ampAudible) {
		slopeEg2_retriggerFrom(&voice->oscVolEg, prevGain);
		voice->ampRampArm = 1u;
	} else {
		slopeEg2_trigger(&voice->oscVolEg);
	}
	voice->velo = vol/127.f;

	transient_trigger(&voice->transGen);

	SnapEg_trigger(&voice->snapEg);

	/*
	 * Filter reset guard (S088 P0 §3 Layer 2c).
	 * What: rst (and itp with a decayed amp) clears the SVF state so the new
	 *       hit cannot inherit the old tail; fre and an audible itp keep the
	 *       filter ringing and slew its coefficient instead (Layer 2d).
	 * Why:  clearing the filter in fre/itp would be an audible cut of the tail
	 *       the crossfade/ramp is meant to preserve.
	 * Inputs: behavesAsRst. Outputs: voice->filter state.
	 */
	if (behavesAsRst)
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
	/*
	 * Start the osc1 position bend for this block (S086 P3).
	 *
	 * What:       osc_positionBendApply() adds this block's change of pos to
	 *             osc.phaseInc (circular, capped by OSC_POS_BEND_CAP) and
	 *             stores the step in osc.positionBendStep; the switch below
	 *             renders the carrier with it, and osc_positionBendRestore()
	 *             after the switch removes it. In snc the bend argument is 0:
	 *             the delta is consumed (positionApplied follows pos) but
	 *             phaseInc is not changed, because every master reset sets the
	 *             slave's phase to a target without pos, and osc_syncRatioQ16()
	 *             reads the slave's phaseInc (086 log §4.6).
	 * Why:        pos is continuous (086 log §4.3). The step lives in the
	 *             oscillator, not in a local, so nothing stays live across the
	 *             renders that LTO inlines into mixer_calcNextSampleBlock()
	 *             (assessment F1). modOsc is a separate OscInfo and is never
	 *             bent, so in fm, mix and rng only the carrier moves.
	 * Inputs:     voice->osc position state; voice->modMode; osc.phaseInc as
	 *             set by osc_setFreq() in Drum_calcVoiceAsync() this block.
	 * Outputs:    osc.phaseInc bent for the render; osc.positionBendStep;
	 *             osc.positionApplied.
	 * Accessors:  this function only.
	 * Affiliates: osc_positionBendApply()/Restore() (Oscillator.h);
	 *             Drum_triggerVoice() (osc_positionTrigger()).
	 */
	osc_positionBendApply(&voice->osc, (uint8_t)(voice->modMode != DRUM_MOD_SNC));

	switch (voice->modMode)
	{
	case DRUM_MOD_MIX:
		//calc next mod osc sampleBlock
		calcNextOscSampleBlock(&voice->modOsc,modBuf,size,voice->fmModAmount);
		//calc main osc buffer
		/* S087 D (A2): a typed osc1 renders through the S087 renderers. */
		if (osc_isTyped(&voice->osc))
			osc_renderTyped(&voice->osc, buf, size, 1.f - voice->fmModAmount);
		else
			calcNextOscSampleBlock(&voice->osc,buf,size, (1.f-voice->fmModAmount));
		//add mod buffer to main osc buffer
		bufferTool_addBuffersSaturating(buf,modBuf,size);
		break;

	case DRUM_MOD_SNC:
	{
		uint32_t syncOver[OUTPUT_DMA_SIZE];
		const uint8_t loopMaster = osc_syncMasterIsLoop(&voice->modOsc);
		const uint32_t clockInc = loopMaster
			? voice->modOsc.phaseInc : osc_syncClockInc(&voice->modOsc);

		/*
		 * snc master builder by kind (S087 stage D, A12).
		 *
		 * What:       an l-entry master resets the slave each time its loop
		 *             wraps (osc_buildLoopSyncList()); every other master uses
		 *             the periodic clock as before (s entries act as noise).
		 * Affiliates: osc_buildLoopSyncList(), osc_syncClockInc().
		 */
		if (loopMaster)
			voice->syncClock = osc_buildLoopSyncList(&voice->modOsc,
			                                         voice->syncClock,
			                                         syncOver, size);
		else
			voice->syncClock = osc_buildSyncList(voice->syncClock, clockInc,
			                                     syncOver, size);
		/* sync depth = amt^2 x mod envelope (Drum_modEnvDepth()) */
		if (osc_isTyped(&voice->osc))
			osc_renderTypedSync(&voice->osc, syncOver, clockInc,
			                    osc_syncDepthQ30(Drum_modEnvDepth(voice)),
			                    buf, size);
		else
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
		if (osc_isTyped(&voice->osc))
			osc_renderTypedRing(&voice->osc, modBuf, buf, size, start, step);
		else
			calcNextOscSampleRingBlock(&voice->osc, modBuf, buf, size,
			                           start, step);
		voice->ringDepthQ15 = start + step * (int32_t)size;
		break;
	}

	case DRUM_MOD_FM:
	default:
		//calc next mod osc sampleBlock
		calcNextOscSampleBlock(&voice->modOsc,modBuf,size,voice->fmModAmount);
		/*
		 * FM depth ramp (S088 P0 §3 Layer 2e, D1: constant cost).
		 *
		 * What:       scales the modulator block by a linear ramp that starts at
		 *             lastFmDepth / osc.fmMod and ends at 1, so the FM index
		 *             glides from the previous block's depth to this block's
		 *             across the armed block. The FM renderer below still uses
		 *             osc.fmMod as the block depth.
		 * Why:        on a retrigger the mod envelope restarts, so osc.fmMod
		 *             jumps; without the ramp that step is a click whose size
		 *             follows the modulator amplitude. The loop runs in every
		 *             mode and on every block (constant CPU): when the voice is
		 *             not armed the start ratio is exactly 1.0f, so the multiply
		 *             is the identity and rst stays bit-identical.
		 * Inputs:     modBuf (modulator at fmModAmount), osc.fmMod (new depth),
		 *             lastFmDepth, ampRampArm.
		 * Outputs:    modBuf scaled; lastFmDepth = osc.fmMod afterwards.
		 * Cost:       one float multiply per sample (plus the ramp add).
		 * Accessors:  this fm branch only.
		 * Affiliates: DrumVoice.lastFmDepth; ampRampArm;
		 *             Drum_calcVoiceAsync() (sets osc.fmMod).
		 */
		{
			const float newDepth = voice->osc.fmMod;
			float start = 1.0f;

			if (voice->ampRampArm && newDepth > 0.0001f) {
				start = voice->lastFmDepth / newDepth;
				if (start < 0.0f) start = 0.0f;
				else if (start > 4.0f) start = 4.0f;
			}
			/* Always runs (constant cost, D1): with start == 1 the multiply
			** is the identity and the output matches the pre-S088 block. */
			{
				const float dstep = (size > 1u)
					? (1.0f - start) / (float)(size - 1u) : 0.0f;
				uint8_t k;
				float s = start;

				for (k = 0u; k < size; k++) {
					modBuf[k] = (int16_t)((float)modBuf[k] * s);
					s += dstep;
				}
			}
			voice->lastFmDepth = newDepth;
		}
		if (osc_isTyped(&voice->osc))
			osc_renderTypedFm(&voice->osc, modBuf, buf, size, 1.0f);
		else
			calcNextOscSampleFmBlock(&voice->osc,modBuf,buf,size,1.0f);
		break;
	}

	/* S086 P3: end the pos bend started before the switch (exact restore). */
	osc_positionBendRestore(&voice->osc);
	/*
	 * itp crossfade blend (S088 P0 §3 Layers 3a/3c).
	 *
	 * What:       while the snapshot is active, render its frozen carrier
	 *             plainly and crossfade buf toward it over 132 samples
	 *             (OSC_RUN_ITP_SAMPLES); the helper advances the snapshot
	 *             phase, decrements its block counter and clears active at 0.
	 * Why:        the retrigger reset the live carrier; blending the old copy
	 *             out removes the click. The old copy is carrier-only (no FM,
	 *             sync or ring) per S085 P1 Q14, so the cost is one plain
	 *             carrier loop plus the blend.
	 * Inputs:     voice->snapshot, buf (carrier section), size.
	 * Outputs:    buf blended; snapshot advanced/reset.
	 * Accessors:  this render only.
	 * Affiliates: oscSection_renderOldCarrier(); OSC_RUN_ITP_FADE_BLOCKS;
	 *             Drum_fillItpSnapshot(); the mixer's slot reserve.
	 */
	if (voice->snapshot.active)
		oscSection_renderOldCarrier(&voice->snapshot, buf, size);

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
	 * Fused Drum post-chain (S073 Step 4; velocity ramp S088 P0 §3 Layer 2b).
	 *
	 * What:       amp EG ramp, velocity ramp and distortion in one pass.
	 *             The velocity endpoint is the previous block's gain only when
	 *             the trigger armed the ramp; every other block passes equal
	 *             endpoints so the sample values are the S073 ones exactly.
	 * Why:        removes two intermediate int16 load/store passes while
	 *             preserving all truncation points (S0). A velocity change on a
	 *             fre/itp retrigger glides over the armed block instead of
	 *             clicking.
	 * Inputs:     filtered buf, amp gain endpoints, veloGain/lastVeloGain, dist.
	 * Outputs:    pre-volume buf; lastVeloGain updated; ampRampArm cleared by
	 *             the shared tail below.
	 * Accessors:  this render function through InstrumentManager.
	 * Affiliates: voicePostChain.h, voicePost_drum(), Drum.lastVeloGain,
	 *             Drum.ampRampArm and the Step 4 tests.
	 */
	{
		const float veloGain = voice->volumeMod ? voice->velo : 1.0f;
		const float lastVeloGain = voice->ampRampArm
			? voice->lastVeloGain : veloGain;

		voicePost_drum(buf, voice->ampFilterInput, voice->lastGain,
		               veloGain, lastVeloGain, &voice->distortion, size);
		voice->lastVeloGain = veloGain;
	}
	#endif
	/* S088 P0: the armed block is done; later blocks use flat endpoints. */
	voice->ampRampArm = 0u;
	/*
	 * Channel volume is intentionally not applied here (Session 072 step 2).
	 * The mixer multiplies the decimated block by voice->vol x slider gain;
	 * see instrumentManager_runtimeVolume() and mixer_calcNextSampleBlock().
	 * Re-adding a gain stage here would apply volume twice.
	 */
}
//---------------------------------------------------
