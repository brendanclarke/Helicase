/*
 * lfo.h
 *
 *  Created on: 14.01.2013
 *  Modified on 17.05.2026 by Brendan Clarke
 *
 *      Non bandlimited low freq. oscillator
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


#ifndef LFO_H_
#define LFO_H_
//-------------------------------------------------------------
#include "stm32f4xx.h"
#include "Oscillator.h"
#include "wavetable.h"	//sine
#include "config.h"
// TODO DSP_PORT
// #include "sequencer.h"
#include "modulationNode.h"
//-------------------------------------------------------------
#define LFO_SINE 		0x00
#define LFO_TRI			0x01
#define LFO_SAW_UP		0x02
#define LFO_SAW_DOWN	0x03
#define LFO_REC			0x04
#define LFO_NOISE		0x05
#define LFO_EXP_UP		0x06
#define LFO_EXP_DOWN	0x07

/*
 * One-shot and exponential-triangle LFO waves (S084 P1 P1-1.1).
 *
 * What:       values 8..15 are one-shot versions of waves 0..7 (same order:
 *             si1 tr1 su1 sd1 sq1 rn1 xu1 xd1); 16 is the exponential
 *             triangle (xtr), 17 its one-shot (xt1). Stored values 0..7 keep
 *             their meaning.
 * Why:        plan §4.5 / ENHANCED_FEATURES.md: a one-shot runs one cycle per
 *             retrigger and holds its end value; `ofs` becomes a start delay.
 * Inputs:     the lfo_wave descriptor row (MENU_LFO_WAVES).
 * Outputs:    lfo_calc() wave selection; lfoWaveNames[] count must equal
 *             LFO_WAVE_COUNT.
 * Accessors:  lfo.c, MenuText.h lfoWaveNames[], the four *Parameters.c rows.
 * Affiliates: lfo_retriggerPhase(), lfo_isOneShot(),
 *             instrumentManager_retriggerRuntimeLfos(),
 *             instrumentManager_restoreLfoPhaseIfNeeded().
 */
#define LFO_ONE_SHOT_BASE  0x08
#define LFO_SINE_1         0x08
#define LFO_TRI_1          0x09
#define LFO_SAW_UP_1       0x0A
#define LFO_SAW_DOWN_1     0x0B
#define LFO_REC_1          0x0C
#define LFO_NOISE_1        0x0D
#define LFO_EXP_UP_1       0x0E
#define LFO_EXP_DOWN_1     0x0F
#define LFO_XTRI           0x10
#define LFO_XTRI_1         0x11
#define LFO_WAVE_COUNT     18u

/* One-shot run state (S084 P1 P1-1.1): idle before the first fire, a
** start delay, the single cycle, then holding the end value. */
#define LFO_ONESHOT_IDLE   0u
#define LFO_ONESHOT_DELAY  1u
#define LFO_ONESHOT_RUN    2u
#define LFO_ONESHOT_HOLD   3u


#define LFO_MAX_F 		200 //[Hz]
#define LFO_SR 			(REAL_FS/(float)OUTPUT_DMA_SIZE)
/*
 * LFO retrigger source: Scene change (S076 P2).
 *
 * What:       value 7 in the lfo.retrigger field. When set, a Scene change
 *             resets the LFO phase to phaseOffset using the incoming Scene's
 *             configured offset. When NOT set, the LFO inherits the outgoing
 *             Scene's running phase so the cycle continues uninterrupted.
 * Why:        gives the user per-LFO control over whether a Scene change
 *             interrupts the LFO cycle. This value is outside the voice
 *             trigger range 1..6 and must not match voice retrigger
 *             comparisons.
 * Inputs:     stored as uint8_t in lfo.retrigger via the MENU_RETRIGGER
 *             selector (retriggerNames[] index 7 = "scn").
 * Outputs:    instrumentManager_restoreLfoPhaseIfNeeded() tests this value
 *             to decide reset vs. continue; instrumentManager_retriggerRuntimeLfos()
 *             excludes it from voice trigger matching.
 * Affiliates: retriggerNames[] (MenuText.h),
 *             instrumentManager_retriggerRuntimeLfos() (InstrumentManager.c),
 *             instrumentManager_restoreLfoPhaseIfNeeded() (InstrumentManager.c).
 */
#define LFO_RETRIGGER_SCENE  7u
//-------------------------------------------------------------
typedef struct LfoStruct
{
	uint32_t 	phase;		// the current phase of the LFO (upper bits cnt 0-255)
	uint32_t 	phaseInc;	// the phase increment controls the LFO frequency
	uint8_t 	waveform;	// selects the waveform
	uint8_t 	retrigger;	// defines the voice nr that retriggers the LFO (0=no retrigger)
	uint32_t 	phaseOffset;// the phase value to which the LFO is retriggered
	float 		rnd;
	uint8_t 	sync;
	float 		freq;
	ModulationNode modTarget;
	/*
	 * Second destination for the same LFO oscillator.
	 *
	 * modTarget and modTarget2 are not two independent LFOs. They share phase,
	 * waveform, rate, sync, offset, retrigger, and polarity, but each target
	 * owns its own amount and any remaining legacy direct backend state.
	 * Descriptor instrument targets may now be installed in InstrumentManager
	 * adapters rather than these raw ModulationNode slots; the slots remain the
	 * per-pair amount owners and still support any intentionally retained direct
	 * pointer backend.
	 */
	ModulationNode modTarget2;
	/*
	 * Shared application polarity for both destinations.
	 *
	 * Inputs: descriptor/menu writes store a mod_node_polarity_t value here.
	 * Output: lfo_dispatchNextValue() passes it to ModulationNode for legacy
	 * direct backends and to InstrumentManager for descriptor/supplemental
	 * adapters. Polarity belongs to Lfo rather than each destination because
	 * the requested UI shape is one shared LFO with two destinations and
	 * independent amounts.
	 */
	uint8_t		polarity;
	float		modNodeValue;
	/*
	 * One-shot state (S084 P1 P1-1.2).
	 *
	 * What:       oneShotState is LFO_ONESHOT_*; delayBlocks counts the
	 *             remaining start-delay blocks (derived from phaseOffset at
	 *             each fire).
	 * Why:        one-shot waves must stop after one cycle and may wait an
	 *             `ofs`-scaled delay before running (plan §4.5).
	 * Inputs:     lfo_retriggerPhase() arms both; lfo_calc() advances them.
	 * Outputs:    lfo_calc()'s phase handling for waves >= LFO_ONE_SHOT_BASE.
	 * Accessors:  lfo.c only.
	 * Affiliates: instrumentManager_retriggerRuntimeLfos(),
	 *             instrumentManager_restoreLfoPhaseIfNeeded(); lives inside the
	 *             engine struct, so it costs no static RAM (+8 B per LFO).
	 */
	uint8_t		oneShotState;
	uint32_t	delayBlocks;
} Lfo;
//-------------------------------------------------------------
void lfo_init(Lfo *lfo);
/*
 * Dispatch one LFO block with explicit source-slot identity.
 *
 * Inputs: lfo is the oscillator/modulation state for one voice, and source_slot
 * identifies which instrument slot owns it. Output: the shared waveform updates
 * both direct ModulationNode targets and any installed Scene/supplemental
 * target adapters for the same LFO pair slots.
 *
 * The source slot cannot be inferred reliably by Scene target code from the
 * Lfo pointer without hardcoding voice object addresses. Passing it from
 * mixer.c keeps LFO math generic while allowing InstrumentManager to route
 * Scene modulation by the same source slot used when the target was installed.
 */
void lfo_dispatchNextValue(Lfo* lfo, uint8_t source_slot);
uint32_t lfo_calcPhaseInc(float freq, uint8_t sync);
void lfo_setFreq(Lfo *lfo, float f);
void lfo_setSync(Lfo* lfo, uint8_t sync);
void lfo_recalcSync();
void lfo_retrigger(uint8_t voiceNr);

/*
 * Restart an LFO's phase for a retrigger or Scene fire (S084 P1 P1-1.3).
 *
 * What:       looping waves jump to phaseOffset (today's behaviour). One-shot
 *             waves restart at phase 0 in the DELAY state with
 *             delayBlocks = phaseOffset / effective phaseInc (0 if phaseInc is
 *             0), so `ofs` becomes a start delay scaled to the LFO period.
 *             rn1 draws its single random value here.
 * Why:        one place owns the looping/one-shot difference for every caller.
 * Inputs:     lfo (non-NULL).
 * Outputs:    lfo->phase, oneShotState, delayBlocks, rnd.
 * Accessors:  instrumentManager_retriggerRuntimeLfos(),
 *             instrumentManager_restoreLfoPhaseIfNeeded().
 * Affiliates: lfo_calc(), LFO_ONE_SHOT_BASE.
 */
void lfo_retriggerPhase(Lfo *lfo);

/*
 * True for waves that run once per fire (S084 P1 P1-1.3).
 *
 * What:       waveform in 8..15 or LFO_XTRI_1.
 * Inputs:     lfo.  Outputs: 1/0.
 * Accessors:  lfo.c, InstrumentManager.c (Scene-activation fire).
 */
uint8_t lfo_isOneShot(const Lfo *lfo);

#endif /* LFO_H_ */
