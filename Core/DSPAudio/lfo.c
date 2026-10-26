/*
 * lfo.c
 *
 *  Created on: 14.01.2013
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

//-------------------------------------------------------------
#include "lfo.h"
#include "random.h"
#include <math.h>

#include "DrumVoice.h"
#include "CymbalVoice.h"
#include "HiHat.h"
#include "Snare.h"
#include "sequencer.h"
#include "valueShaper.h"
#include "InstrumentManager.h"
//-------------------------------------------------------------
void lfo_init(Lfo *lfo)
{
	lfo->phase 			= 0;
	lfo->phaseInc 		= 1;
	lfo->phaseOffset	= 0;
	lfo->retrigger		= 0;
	lfo->waveform		= SINE;
	lfo->sync			= 0;
	lfo->freq			= 1;
	lfo->modNodeValue	= 1;
	lfo->polarity		= MOD_NODE_POLARITY_NEGATIVE;

	/*
	 * One-shot state at init (S084 P1 P1-1.4).
	 *
	 * What:       a new LFO starts IDLE with no pending delay. An idle
	 *             one-shot with a voice retrigger (v1..v6) outputs its start
	 *             value (phase 0) until that voice triggers; with retrigger
	 *             off or scn, lfo_calc() fires it on its first block (S084 P1
	 *             review fix F7), and a Scene activation fires it again.
	 * Why:        engine init and slot resets must leave one-shot waves in a
	 *             defined, silent-start state (plan §4.5).
	 * Inputs:     none.  Outputs: lfo->oneShotState, lfo->delayBlocks.
	 * Accessors:  each engine's *_initVoice() through lfo_init().
	 * Affiliates: lfo_retriggerPhase(), lfo_calc().
	 */
	lfo->oneShotState	= LFO_ONESHOT_IDLE;
	lfo->delayBlocks	= 0u;
	lfo->rnd			= 0.f;

	modNode_init(&lfo->modTarget);
	modNode_init(&lfo->modTarget2);
}
//-------------------------------------------------------------
float lfo_calc(Lfo *lfo)
{
	uint32_t oldPhase 		= lfo->phase;
	float inc = lfo->phaseInc*lfo->modNodeValue;
	uint32_t incInt = inc;
	uint8_t overflow = 0u;
	const uint8_t oneShot = lfo_isOneShot(lfo);
	const uint8_t wave = lfo->waveform;
	uint8_t base;

	/*
	 * One block of LFO output in 0..1 (S084 P1 P1-1.5).
	 *
	 * What:       advances the phase (looping waves always; one-shot waves only
	 *             while running, with a start delay and a held end value) and
	 *             returns the wave value. Noise now spans 0..1 like every wave.
	 * Why:        plan §4.5 one-shots; plan §6 r4 Q17: every wave must behave
	 *             the same under the negative/positive/bipolar polarity
	 *             options, and noise previously spanned -1..1.
	 * Inputs:     lfo (phase, phaseInc, modNodeValue, waveform, one-shot state).
	 * Outputs:    value 0..1; lfo->phase, oneShotState, delayBlocks, rnd.
	 * Accessors:  lfo_dispatchNextValue() once per block per slot.
	 * Affiliates: lfo_retriggerPhase(), LFO_* constants, the LFO adapters in
	 *             InstrumentManager (they clamp the source to 0..1).
	 */
	if (!oneShot) {
		lfo->phase += incInt;
		overflow = (uint8_t)(oldPhase > lfo->phase);
	} else {
		switch (lfo->oneShotState) {
		case LFO_ONESHOT_RUN:
			lfo->phase += incInt;
			overflow = (uint8_t)(oldPhase > lfo->phase);
			if (overflow) {
				lfo->oneShotState = LFO_ONESHOT_HOLD;
				lfo->phase = 0xffffffffu;
			}
			break;
		case LFO_ONESHOT_DELAY:
			lfo->phase = 0u;
			if (lfo->delayBlocks > 0u)
				lfo->delayBlocks--;
			if (lfo->delayBlocks == 0u)
				lfo->oneShotState = LFO_ONESHOT_RUN;
			break;
		case LFO_ONESHOT_IDLE:
			/*
			 * A one-shot that has not been fired yet
			 * (S084 P1 review fix F7).
			 *
			 * What:       retrigger off (0) or scn: fire now through
			 *             lfo_retriggerPhase() (start delay, then run, then
			 *             hold). Retrigger v1..v6: wait for the voice trigger
			 *             at phase 0, so the output is the wave's start value.
			 * Why:        IDLE is the state after lfo_init() and after any
			 *             looping wave (lfo_retriggerPhase() sets IDLE for
			 *             looping waves), so it is exactly "just became a
			 *             one-shot". Before this fix a wave change to
			 *             si1..xt1 in the menu left the LFO frozen at its
			 *             old mid-cycle phase, a static offset on the target,
			 *             until the next Scene activation fired it. An LFO
			 *             with no voice trigger source has nothing else that
			 *             will fire it, so it fires when selected; at Scene
			 *             activation instrumentManager_restoreLfoPhaseIfNeeded()
			 *             fires it again, as before.
			 * Cost:       a compare per block while IDLE; the fire happens
			 *             once, after which the state is DELAY/RUN/HOLD.
			 */
			if (lfo->retrigger == 0u ||
				lfo->retrigger == LFO_RETRIGGER_SCENE)
				lfo_retriggerPhase(lfo);
			else
				lfo->phase = 0u;
			break;
		default:
			/* HOLD keeps phase: the held end value. */
			break;
		}
	}

	/*
	 * Base wave for the value lookup: one-shot values 8..15 reuse waves 0..7,
	 * xt1 reuses xtr. Stored 0..7 and LFO_XTRI are unchanged.
	 */
	if (wave >= LFO_ONE_SHOT_BASE && wave <= LFO_EXP_DOWN_1)
		base = (uint8_t)(wave - LFO_ONE_SHOT_BASE);
	else if (wave == LFO_XTRI_1)
		base = LFO_XTRI;
	else
		base = wave;

	switch(base)
	{
	//---
		case LFO_SINE:
			return ((sine_table[(lfo->phase>>20)]/32767.f) + 1)/2.f;
		break;
	//---
		case LFO_TRI:
			return (1.f-fabsf( (lfo->phase/(float)0xffffffff)*2-1 ) );
			break;
	//---
		case LFO_SAW_UP:
		return lfo->phase/(float)0xffffffff ;
		break;
	//---
	case LFO_REC:
		/* S084 P1: a one-shot square starts high and holds low. */
		if(oneShot)
		{
			if(lfo->phase <= 0x7fffffffu)
			{
				return 1.f;
			}
			else
			{
				return 0.f;
			}
		}
		if(lfo->phase > 0x7fffffff)
		{
			return 1.f;
		}
		else
		{
			return 0.f;
		}
		break;
	//---
	case LFO_NOISE:
		/* S084 P1: 0..1 like every wave; rn1's value is drawn at the fire. */
		if(overflow && !oneShot)
		{
			lfo->rnd = (float)(GetRngValue() & 0x7FFF) / 32767.0f;
		}
		return lfo->rnd;
		break;
	//---
		case LFO_SAW_DOWN:
		return (0xffffffff-(lfo->phase)) / (float)0xffffffff;
		break;
	//---
		case LFO_EXP_UP:
			{
				float x = lfo->phase/(float)0xffffffff;
				return x*x*x;
			}
			break;
	//---
	case LFO_EXP_DOWN:
			{
				float x = (0xffffffff-(lfo->phase)) / (float)0xffffffff;
				return x*x*x;//valueShaperF2F(x,-0.9f);
			}
			break;
	//---
	case LFO_XTRI:
			{
				const float triangle = 1.f -
					fabsf((lfo->phase/(float)0xffffffff)*2.f - 1.f);
				return triangle*triangle*triangle;
			}
			break;
	//---
		default:
		return 0;
		break;
	}
	return 0;
}
//-------------------------------------------------------------
void lfo_dispatchNextValue(Lfo* lfo, uint8_t source_slot)
{
	float val = lfo_calc(lfo);
	/*
	 * Fan one raw oscillator value out to both destination nodes.
	 *
	 * Inputs: lfo_calc() returns the shared 0..1 waveform for this block, and
	 * lfo->polarity stores the shared negative/positive/bipolar mode. The
	 * source slot tells InstrumentManager which descriptor/supplemental/Scene
	 * target adapters belong to this LFO. Outputs: any remaining legacy direct
	 * ModulationNode target is updated first, then InstrumentManager applies
	 * owner-backed adapters for pair 1 and pair 2.
	 *
	 * This stays separate from lfo_calc() because only destination owners know
	 * whether the target is a legacy direct pointer, descriptor-backed runtime
	 * writer, slot decimation, Scene Decimation, or the hidden per-voice Morph
	 * layer.
	 */
	modNode_updateValuePolarity(&lfo->modTarget,val,lfo->polarity);
	modNode_updateValuePolarity(&lfo->modTarget2,val,lfo->polarity);
	instrumentManager_updateLfoAdapters(source_slot, 0u, val,
										lfo->polarity,
										lfo->modTarget.amount);
	instrumentManager_updateLfoAdapters(source_slot, 1u, val,
										lfo->polarity,
										lfo->modTarget2.amount);
}
//-------------------------------------------------------------
uint32_t lfo_calcPhaseInc(float freq, uint8_t sync)
{
	if(sync==0)//no sync
	{
		return freq/(LFO_SR) * 0xffffffff;
	}

	//sync is on

	/* Synced LFOs are the DSP modulation path that follows sequencer tempo.
	** Keep this tied to seq_getBpm() like the reference LXR; a fixed BPM here
	** makes modulation drift from the selected/internal/external clock source. */
	float bpm =  seq_getBpm();

	float tempoAsFrequency = bpm > 0.0 ? bpm / 60.f /4: 0.0f; //bpm/60 = beat duration /4 = bar duration

	float scaler;

	switch(sync)
	{
		default: //no sync

		return freq/(LFO_SR) * 0xffffffff;
		break;

		case 1: // 4/1
		scaler = 0.25f;//NoteTypeScalers[Quadruple] * NoteDivisionScalers[Whole];
		break;

		case 2: // 2/1
		scaler = 0.5f;//NoteTypeScalers[Double] * NoteDivisionScalers[Whole];
		break;

		case 3: // 1/1
		scaler = 1;//NoteTypeScalers[Regular] * NoteDivisionScalers[Whole];
		break;

		case 4: // 1/2
		scaler = 2;// NoteTypeScalers[Regular] * NoteDivisionScalers[Half];
		break;

		case 5: //1/3
		scaler = 3;// NoteTypeScalers[Regular] * NoteDivisionScalers[Third];
		break;

		case 6: // 1/4
		scaler = 4;// NoteTypeScalers[Regular] * NoteDivisionScalers[Quarter];
		break;

		case 7: // 1/6
		scaler = 6;// NoteTypeScalers[Regular] * NoteDivisionScalers[Sixth];
		break;

		case 8: // 1/8
		scaler = 8;// NoteTypeScalers[Regular] * NoteDivisionScalers[Eighth];
		break;

		case 9: // 1/12
		scaler = 12;//NoteTypeScalers[Regular] * NoteDivisionScalers[Twelfth];
		break;

		case 10: // 1/16
		scaler = 16;//NoteTypeScalers[Regular] * NoteDivisionScalers[Sixteenth];
		break;

		case 11: // 1/32
		scaler = 32;//NoteTypeScalers[Regular] * NoteDivisionScalers[Sixteenth];
		break;
	}

	float lfoSyncfreq = tempoAsFrequency * scaler;
	return (lfoSyncfreq / (LFO_SR)) * 0xffffffff;
}
//-------------------------------------------------------------
void lfo_setFreq(Lfo *lfo, float f)
{
	f += 1;
	f = f/128.f;
	f = f*f*f;
	lfo->freq = f*LFO_MAX_F;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);
}
//-------------------------------------------------------------
void lfo_setSync(Lfo* lfo, uint8_t sync)
{
	lfo->sync = sync;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);
}
//-------------------------------------------------------------
void lfo_recalcSync()
{
	/*
	 * Recalculate sync against the active InstrumentManager runtime slots.
	 *
	 * Inputs: none; tempo comes from sequencer through lfo_calcPhaseInc().
	 * Output: every current slot LFO receives a refreshed phase increment.
	 * This delegates instead of walking voiceArray/snareVoice/cymbalVoice/
	 * hatVoice directly because Instrument Load can move those engine types
	 * into different runtime slots.
	 */
	instrumentManager_recalcRuntimeLfoSync();
}
//-------------------------------------------------------------
void lfo_retrigger(uint8_t voice)
{
	/*
	 * Retrigger LFOs through the dynamic runtime dispatcher.
	 *
	 * Input: zero-based visible trigger track. Output: any current slot LFO
	 * whose retrigger selector matches that track resets to phaseOffset. The
	 * old fixed-global scan is no longer correct once instruments can be loaded
	 * into arbitrary slots.
	 */
	instrumentManager_retriggerRuntimeLfos(voice);
}
//-------------------------------------------------------------
uint8_t lfo_isOneShot(const Lfo *lfo)
{
	/*
	 * True for waves that run once per fire (S084 P1 P1-1.3).
	 *
	 * What:       waveform 8..15 (the one-shot list) or LFO_XTRI_1.
	 * Why:        lfo_calc(), lfo_retriggerPhase() and the Scene-activation
	 *             fire need one shared definition of "one-shot".
	 * Inputs:     lfo.  Outputs: 1/0 (0 for NULL).
	 * Accessors:  lfo.c, InstrumentManager.c.
	 */
	if (!lfo)
		return 0u;
	if (lfo->waveform >= LFO_ONE_SHOT_BASE &&
		lfo->waveform <= LFO_EXP_DOWN_1)
		return 1u;
	return (uint8_t)(lfo->waveform == LFO_XTRI_1);
}
//-------------------------------------------------------------
void lfo_retriggerPhase(Lfo *lfo)
{
	/*
	 * Restart an LFO's phase for a retrigger or Scene fire (S084 P1 P1-1.3).
	 *
	 * What:       looping waves jump to phaseOffset (today's behaviour).
	 *             One-shot waves restart at phase 0 in the DELAY state with
	 *             delayBlocks = phaseOffset / effective phaseInc (0 if the
	 *             increment is 0), so `ofs` becomes a start delay scaled to
	 *             the LFO period. rn1 draws its single random value here.
	 * Why:        one place owns the looping/one-shot difference for every
	 *             caller (voice retrigger and Scene activation).
	 * Inputs:     lfo (non-NULL).
	 * Outputs:    lfo->phase, oneShotState, delayBlocks, rnd.
	 * Accessors:  instrumentManager_retriggerRuntimeLfos(),
	 *             instrumentManager_restoreLfoPhaseIfNeeded().
	 * Affiliates: lfo_calc(), lfo_isOneShot(), LFO_ONE_SHOT_BASE.
	 */
	uint32_t inc;

	if (!lfo)
		return;
	if (!lfo_isOneShot(lfo)) {
		lfo->phase = lfo->phaseOffset;
		lfo->oneShotState = LFO_ONESHOT_IDLE;
		lfo->delayBlocks = 0u;
		return;
	}
	inc = (uint32_t)(lfo->phaseInc * lfo->modNodeValue);
	lfo->phase = 0u;
	lfo->delayBlocks = inc ? (lfo->phaseOffset / inc) : 0u;
	lfo->oneShotState = lfo->delayBlocks
		? LFO_ONESHOT_DELAY : LFO_ONESHOT_RUN;
	if (lfo->waveform == LFO_NOISE_1)
		lfo->rnd = (float)(GetRngValue() & 0x7FFF) / 32767.0f;
}
//-------------------------------------------------------------
