/*
 * Oscillator.h
 *
 *  Created on: 01.04.2012
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


#ifndef OSCILLATOR_H_
#define OSCILLATOR_H_

#include "stm32f4xx.h"
#include "datatypes.h"
#include "wavetable.h"
#include "config.h"
#include "../SampleRom/SampleMemory.h"
//-----------------------------------------------------------

//the available osc waveforms
#define SINE 				0x00
#define TRI					0x01
#define SAW					0x02
#define REC					0x03
#define NOISE				0x04
#define CRASH				0x05
/*
 * Stock table waves (S086 P2): [11][1024] octave tables like Tri/Saw/Rec,
 * in wavetableStock.c, read by the same table loops. SQ3 = Re3 (303
 * square), SW3 = Sw3 (303 saw). OSC_STOCK_FIRST..OSC_STOCK_LAST index
 * osc_stockTables[] (Oscillator.c); ReA/TrA/SwA follow later.
 */
#define SQ3					0x06
#define SW3					0x07
#define OSC_STOCK_FIRST		SQ3
#define OSC_STOCK_LAST		SW3
#define OSC_SAMPLE_START 	0x08

#define phaseShift 1000

//-----------------------------------------------------------

typedef struct OscStruct
{
	int16_t 	output;
	uint32_t	phaseInc;
	uint32_t	phase;		// the current phase of the osc 8bit indexing 24 interpolation
	float	 	freq;		// frequency in [Hz]
	uint8_t		waveform;	// the selected waveform of the osc

	uint8_t		tableOffset;	//overtone table

	/*
	 * pos runtime state (S086 P3/P4).
	 *
	 * What:       position is the 0..127 osc1_position value. For the phase
	 *             waves (osc_positionIsPhase()) it is position/128 of a cycle;
	 *             for CRASH the cycle is the complete sample. positionApplied
	 *             is the part of position already contained in phase.
	 * Why:        a descriptor value can change once per audio block. Keeping
	 *             the applied value lets the engine spread each change through
	 *             phaseInc instead of jumping phase at a block boundary. These
	 *             bytes occupy the existing padding before pitchMod, so OscInfo
	 *             and every runtime voice keep their size (asserted below).
	 * Inputs:     position: the generic descriptor writer (menu, Morph, LFO,
	 *             velocity, step automation). positionApplied:
	 *             osc_positionTrigger() and osc_positionBendApply().
	 * Outputs:    read by the same helpers only.
	 * Accessors:  osc_positionTrigger(), osc_positionBendApply().
	 * Affiliates: positionBendStep below; the osc1_position rows in the four
	 *             *Parameters.c files; S086_P3_WAV_OSC_POS_PARAMETER.md and
	 *             S086_P4_SAMPLE_CYM_POS.md.
	 */
	uint8_t		position;
	uint8_t		positionApplied;

	float		pitchMod;	// modulation value for frequency
	float 		fmMod;

	float		modNodeValue;
	uint8_t		waveInterpNext;			/* next waveform used for morph blend */
	/*
	 * pos bend of the block being rendered (S086 P3, assessment F1).
	 *
	 * What:       the signed number of 1/128-cycle steps that
	 *             osc_positionBendApply() added to phaseInc for this block
	 *             (-64..+64; 0 outside a render). osc_positionBendRestore()
	 *             subtracts the same amount after the render and clears it.
	 * Why:        the step is kept in the oscillator instead of a local
	 *             variable, so no register stays live across the render that
	 *             LTO inlines into mixer_calcNextSampleBlock(). A live local
	 *             raised register pressure there and added reloads to two
	 *             inlined per-sample loops (assessment F1). It sits in the
	 *             padding after waveInterpNext, so OscInfo keeps its size.
	 * Inputs:     osc_positionBendApply().  Outputs: osc_positionBendRestore().
	 * Accessors:  those two helpers only; never stored in a file.
	 * Affiliates: the four voice render functions; S086_P3_IMPLEMENTATION.md §9.
	 */
	int8_t		positionBendStep;
	float		waveInterpFrac;			/* blend amount [0..1] between waveform and next */
	uint32_t	waveInterpGeneration;	/* audio-block generation stamp */
	float		freqCacheValue;
	uint8_t		freqCacheWaveform;
	uint8_t		freqCacheValid;
	uint8_t		sampleCacheIndex;
	uint8_t		sampleCacheLooped;
	uint32_t	sampleCacheGeneration;
	uint32_t	sampleCacheSize;
	const int16_t* sampleCacheData;

	//for midi
	uint16_t	midiFreq;  //upper 8 bit coarse, lower 8 bit fine -> the sound edit freq offset
	uint8_t baseNote;		// the last played midi note
	uint32_t startPhase;		// the OSC is reset to this phase on retrigger
} OscInfo;

/*
 * OscInfo layout checks (S086 P3).
 *
 * What:       verify that position and positionApplied fill the padding
 *             between tableOffset (17) and pitchMod (20), that
 *             positionBendStep sits in the padding after waveInterpNext
 *             (before the 4-byte-aligned waveInterpFrac), and the ARM size.
 * Why:        pos is deliberately a zero-RAM feature; a moved field would
 *             silently grow every oscillator in every runtime slot.
 * Inputs:     the OscInfo declaration above.
 * Outputs:    compile-time diagnostics only.
 * Accessors:  the compiler.
 * Affiliates: runtime_slots and the four voice structs; tools/dsp_test's
 *             extracted OscInfo fragment.
 * Note:       <stddef.h> is included here, after the struct, so the line
 *             ranges the test bench extracts from the top of this header do
 *             not move.
 */
#include <stddef.h>
_Static_assert(offsetof(OscInfo, position) == offsetof(OscInfo, tableOffset) + 1u,
               "OscInfo.position must follow tableOffset (padding)");
_Static_assert(offsetof(OscInfo, positionApplied) + 1u == offsetof(OscInfo, pitchMod),
               "OscInfo.positionApplied must end the padding before pitchMod");
_Static_assert(offsetof(OscInfo, positionBendStep) == offsetof(OscInfo, waveInterpNext) + 1u,
               "OscInfo.positionBendStep must follow waveInterpNext (padding)");
_Static_assert(offsetof(OscInfo, waveInterpFrac) == offsetof(OscInfo, waveInterpNext) + 4u,
               "OscInfo.positionBendStep must not move waveInterpFrac");
#if defined(__arm__)
_Static_assert(sizeof(OscInfo) == 72u, "OscInfo must stay 72 bytes (S086 P3)");
#endif
//-----------------------------------------------------------

//extern OscInfo osc1;
//extern OscInfo osc2;

void initOsc();
//-----------------------------------------------------------
void calcSineBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain);
//-----------------------------------------------------------
int16_t calcSine(OscInfo* osc);
//-----------------------------------------------------------
void calcFmSineBlock(OscInfo* osc, int16_t* modBuffer, int16_t* buf, uint8_t size ,const float gain);
//-----------------------------------------------------------
int16_t calcFmSine(OscInfo* osc, OscInfo* modOsc);
//-----------------------------------------------------------
void calcFmBlock(OscInfo* osc, const int16_t table[][1024], int16_t* modBuffer, int16_t* buf, uint8_t size ,const float gain);
//-----------------------------------------------------------
int16_t calcFm(OscInfo* osc, OscInfo* modOsc,  const int16_t table[][1024]);
//-----------------------------------------------------------
void calcWavetableOscBlock(OscInfo* osc, const int16_t table[][1024], int16_t* buf, const uint8_t size ,const float gain);
//-----------------------------------------------------------
int16_t calcWavetableOsc(OscInfo* osc, const int16_t table[][1024]);
//-----------------------------------------------------------
void calcNoiseBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain);
//-----------------------------------------------------------
int16_t calcNoise(OscInfo* osc);
//-----------------------------------------------------------
int16_t calcSampleOscFm(OscInfo* osc, OscInfo* modOsc);
//-----------------------------------------------------------
//used to play back the crash cymbal sample
void calcSampleOscFmBlock(OscInfo* osc,int16_t* modBuffer, int16_t* buf, uint8_t size ,const float gain);
//-----------------------------------------------------------
void calcNextOscSampleBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain);
//-----------------------------------------------------------
// calculate an oscillator
int16_t calcNextOscSample(OscInfo* osc);
 //-----------------------------------------------------------
void osc_setFreq(OscInfo* osc);
//-----------------------------------------------------------
void calcNextOscSampleFmBlock(OscInfo* osc, int16_t* modBuffer, int16_t* buf, uint8_t size ,const float gain);
 //-----------------------------------------------------------
// calculate an fm oscillator
int16_t calcNextOscSampleFm(OscInfo* osc, OscInfo* modOsc);
//-----------------------------------------------------------
void calcSampleOscBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain);
//-----------------------------------------------------------
void calcUserSampleOscFmBlock(OscInfo* osc,int16_t* modBuffer, int16_t* buf, uint8_t size ,const float gain);
//-----------------------------------------------------------
void calcUserSampleOscBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain);
//-----------------------------------------------------------
int16_t calcSampleOsc(OscInfo* osc);
//-----------------------------------------------------------
/** set the played midi note.*/
void osc_setBaseNote(OscInfo* osc, uint8_t baseNote);
//-----------------------------------------------------------
/** recalculate the frequency if either the base note or the offset note value changed*/
void osc_recalcFreq(OscInfo* osc);

//-----------------------------------------------------------
/*
 * Drum osc2 mod modes snc (hard sync) and rng (ring): oscillator side
 * (S085 P1).
 *
 * What:       OSC_SYNC_NONE; the sync clock (osc_syncClockInc,
 *             osc_buildSyncList); the sync and ring dispatchers
 *             (calcNextOscSampleSyncBlock, calcNextOscSampleRingBlock); and
 *             the amount mappings (osc_syncDepthQ30, osc_ringDepthQ15). The
 *             dispatchers mirror calcNextOscSampleBlock(): the same
 *             waveform-interpolation entry, the same waveform switch and the
 *             same user-sample range check.
 * Why:        S085_P1_OSC_MOD_SNC_RNG.md §4; S085_P1_OSC_MOD_IMPLEMENTATION.md.
 *             The session rule is no increase in CPU. snc renders only the
 *             slave: the FM-page oscillator is a phase clock and is never
 *             rendered. rng folds the ring product into the carrier's own
 *             store instead of a separate pass. The existing plain and FM
 *             loops are not edited, so fm and mix compile unchanged.
 * Accessors:  Drum_calcVoiceSyncBlock() (DrumVoice.c) only.
 * Affiliates: Oscillator.c S085 P1 section; tools/dsp_test targets osc_mod,
 *             oscmod_identity, armcheck-oscmod, elf_osc_check.
 */

/*
 * "No reset after this sample" marker in a sync reset list (S085 P1).
 *
 * What:       over[i] == OSC_SYNC_NONE: the clock did not wrap after sample
 *             i. Any other value is the clock's overshoot past its wrap.
 *             That overshoot is always < the clock increment, so it can
 *             never equal this marker.
 * Accessors:  osc_buildSyncList() writes it; the sync copies in
 *             Oscillator.c read it.
 */
#define OSC_SYNC_NONE 0xFFFFFFFFu

/*
 * Sync clock increment for the FM-page (master) oscillator (S085 P1).
 *
 * What:       master->phaseInc, in "2^32 per nominal period" units.
 *             - Sine, table (incl. the stock table waves) and noise phases
 *               already are.
 *             - Crash and user samples (a <<17 point index; 1,024 points per
 *               nominal period) are scaled << 5. They are first clamped to
 *               0x07FFFFFF so the shift cannot overflow; a master at or above
 *               fs then resets the slave on every sample.
 * Why:        plan A1/A4: in P1 every master is a clock at its frequency.
 *             Crash and user samples act as noise (one reset per nominal
 *             period), not as their audio.
 * Inputs:     master (read only). Its phaseInc is current:
 *             Drum_calcVoiceAsync() runs osc_setFreq() on both Drum
 *             oscillators every block, in every mode.
 * Outputs:    the clock increment.
 * Accessors:  Drum_calcVoiceSyncBlock().
 * Affiliates: osc_buildSyncList(); calcNextOscSampleSyncBlock() (the ratio).
 */
uint32_t osc_syncClockInc(const OscInfo* master);

/*
 * Build one block's sync reset list (S085 P1).
 *
 * What:       advances clock by clockInc once per sample. When the addition
 *             wraps (the clock passed 2^32 between samples i and i + 1),
 *             over[i] = the new clock value: the overshoot past the wrap.
 *             Otherwise over[i] = OSC_SYNC_NONE.
 * Why:        plan §4.3.1 / Q11: one list per block serves every slave copy.
 *             Later master kinds (looped samples, wavetable frames, the
 *             zero-crossing bitmap; plan §12) add a builder, not new slave
 *             loops.
 * Inputs:     clock (the clock phase at block start), clockInc, over[size],
 *             size (<= OUTPUT_DMA_SIZE).
 * Outputs:    over[0..size-1]; returns the clock phase at block end (the
 *             caller stores it in DrumVoice.syncClock).
 * Cost:       constant per sample (an add, a compare, a select, a store).
 * Accessors:  Drum_calcVoiceSyncBlock().
 * Affiliates: calcNextOscSampleSyncBlock(); OSC_SYNC_NONE.
 */
uint32_t osc_buildSyncList(uint32_t clock, const uint32_t clockInc,
                           uint32_t* over, const uint8_t size);

/*
 * Render one slave block under hard sync (S085 P1).
 *
 * What:       renders osc at gain 1, exactly as calcNextOscSampleBlock()
 *             would. After sample i, when over[i] != OSC_SYNC_NONE, the
 *             advanced phase is pulled toward the reset target:
 *                 target = over[i] x ratio, ratio = osc->phaseInc / clockInc;
 *                 phase += depth x (signed)(target - phase).
 *             The reset therefore lands on sample i + 1 (plan K4).
 *             - Synced: Sin, Tri, Saw, Rec, Crash, the stock table waves
 *               (S086 P2).
 *             - Rendered plain (no sync): Noi, user samples (plan A3/A4).
 *             - Waveform interpolation: both partners use the same list.
 * Why:        plan Q10: the Drum amount is the sync depth (0 free-running,
 *             1 hard sync); the Drum passes amt^2 x the PitchMod decay
 *             envelope, the fm response (DrumVoice.c Drum_modEnvDepth()).
 *             Only the slave is heard.
 * Inputs:     osc (the carrier), over[size] from osc_buildSyncList(),
 *             clockInc (the same value the list was built with), depthQ30
 *             (osc_syncDepthQ30()), buf[size], size.
 * Outputs:    buf; osc->phase (and osc->output for the plain fallbacks).
 * Cost:       one divide per call (the ratio), then the waveform's sync
 *             copy: S085_P1_OSC_MOD_IMPLEMENTATION.md §1.
 * Accessors:  Drum_calcVoiceSyncBlock().
 * Affiliates: the sync copies and osc_renderSyncWave() in Oscillator.c.
 */
void calcNextOscSampleSyncBlock(OscInfo* osc, const uint32_t* over,
                                const uint32_t clockInc, const int32_t depthQ30,
                                int16_t* buf, const uint8_t size);

/*
 * Render one carrier block with the ring law applied (S085 P1; per-sample
 * depth ramp, S085 P1 follow-up).
 *
 * What:       renders osc exactly as calcNextOscSampleBlock() would at
 *             gain 1, but stores, for sample i,
 *                 d_i    = depthQ15 + (i + 1) x depthStepQ15
 *                 G_i    = (32768 - d_i) + ((d_i x modBuf[i]) >> 15)
 *                 buf[i] = SSAT16((osc_i x G_i) >> 15)
 *             instead of osc_i x gain. With modBuf = the modulator at gain 1
 *             and d in Q15 this is
 *                 carrier x (1 - d) + carrier x modulator x d
 *             (plan K5), with d moving linearly across the block. d = 0 gives
 *             the carrier exactly. Every waveform is supported, including
 *             noise, user samples and waveform interpolation.
 * Why:        D1: a separate ring pass made rng as expensive as fm or more
 *             with user-sample oscillators, so the law is folded into the
 *             carrier's store. The ramp (follow-up): d follows the mod
 *             envelope, and a d that stepped once per block was audible as a
 *             zipper; ramping costs about 3 instructions per sample and keeps
 *             rng under the fm worst case (implementation §14).
 * Inputs:     osc (the carrier); modBuf[size] (the modulator block at gain
 *             1); buf[size]; size; depthQ15 (d at the end of the previous
 *             block, 0..32768); depthStepQ15 (per-sample increment; the
 *             block ends at depthQ15 + size x depthStepQ15). The Drum's d is
 *             amt^2 x the PitchMod decay envelope (Drum_modEnvDepth(),
 *             osc_ringDepthQ15()).
 * Outputs:    buf; osc->phase / osc->output as the plain render.
 * Accessors:  Drum_calcVoiceSyncBlock().
 * Affiliates: the ring copies and osc_ringSample() in Oscillator.c;
 *             DrumVoice.ringDepthQ15 / ringSnap (the ramp state).
 */
void calcNextOscSampleRingBlock(OscInfo* osc, const int16_t* modBuf,
                                int16_t* buf, const uint8_t size,
                                const int32_t depthQ15,
                                const int32_t depthStepQ15);

/*
 * Sync depth from the Drum amount (S085 P1).
 *
 * What:       amount (0..1; the Drum passes amt^2 x the PitchMod decay
 *             envelope, Drum_modEnvDepth()) as Q30: 0 -> 0 (no pull), 1 ->
 *             0x40000000 (exact hard sync). Out-of-range or non-positive
 *             input clamps.
 * Why:        Q30 makes both ends exact. depth 0 is bit-identical to the
 *             free-running slave, and depth 1 lands exactly on the target.
 * Inputs:     amount.  Outputs: depthQ30.
 * Accessors:  Drum_calcVoiceSyncBlock() (once per block).
 * Affiliates: osc_syncPull() in Oscillator.c.
 */
static inline int32_t osc_syncDepthQ30(const float amount)
{
	if (!(amount > 0.0f))
		return 0;
	if (amount >= 1.0f)
		return 0x40000000;
	return (int32_t)(amount * 1073741824.0f);
}

/*
 * Ring depth in Q15 from the Drum amount (S085 P1 follow-up; replaces the
 * dry gain osc_ringK()).
 *
 * What:       d = round(amount x 32768), 0..32768. 0 at amount 0 (the carrier
 *             exactly), 32768 at amount 1 (the pure ring product).
 *             Out-of-range or non-positive input clamps.
 * Why:        the ring copies now take the depth itself and ramp it per
 *             sample (calcNextOscSampleRingBlock()); Q15 keeps every product
 *             in int32.
 * Inputs:     amount (0..1; the Drum passes amt^2 x the PitchMod decay
 *             envelope, Drum_modEnvDepth()).  Outputs: d in Q15.
 * Accessors:  Drum_calcVoiceSyncBlock() (once per block).
 * Affiliates: osc_ringSample() in Oscillator.c.
 */
static inline int32_t osc_ringDepthQ15(const float amount)
{
	if (!(amount > 0.0f))
		return 0;
	if (amount >= 1.0f)
		return 32768;
	return (int32_t)(amount * 32768.0f + 0.5f);
}

/*
 * Whether a waveform is read from a [11][1024] octave table (S086 P2).
 *
 * What:       1 for TRI, SAW, REC and the stock table waves
 *             (OSC_STOCK_FIRST..OSC_STOCK_LAST); 0 for SINE, NOISE, CRASH
 *             and user samples.
 * Why:        the engines' trigger start phase (0xff << 20 x the transient
 *             offset) applies to every table wave alike.
 * Inputs:     waveform.  Outputs: 0 or 1.
 * Accessors:  Drum/Snare/Cymbal/HiHat _triggerVoice().
 * Affiliates: osc_setFreq() (the same set takes the table frequency path).
 */
static inline uint8_t osc_isTableWave(const uint8_t waveform)
{
	return (uint8_t)((waveform >= TRI && waveform <= REC) ||
	                 (waveform >= OSC_STOCK_FIRST && waveform <= OSC_STOCK_LAST));
}

/*
 * Position bend constants (S086 P3/P4).
 *
 * What:       OSC_POS_BEND_CAP limits the number of 1/128-cycle steps moved
 *             in one block; 64 disables the cap because the circular delta
 *             is already in -64..+63. OSC_POS_BEND_PER_STEP is the per-sample
 *             phaseInc for one step held over one OUTPUT_DMA_SIZE block, so
 *             the block moves the phase by exactly 1 << 25 (1/128 cycle).
 * Why:        the exact integer step lands each block on the requested phase
 *             without drift; the asserts pin both constants in the firmware
 *             build, and test_osc_pos.c compiles this same block (assessment
 *             F3), so a wrong value fails the build and the host test. The
 *             cap stays available for listening tests (S086 P3 Q11).
 * Inputs:     OUTPUT_DMA_SIZE from config.h; OSC_POS_BEND_CAP may be
 *             predefined (the host cap build uses 8).
 * Outputs:    the two constants.
 * Accessors:  osc_positionBendApply(), osc_positionBendRestore().
 * Affiliates: tools/dsp_test/Makefile osc_pos (extracts this block from
 *             "#ifndef OSC_POS_BEND_CAP" to the cap-range assert).
 */
#ifndef OSC_POS_BEND_CAP
#define OSC_POS_BEND_CAP 64
#endif
#define OSC_POS_BEND_PER_STEP (0x2000000u / OUTPUT_DMA_SIZE)
_Static_assert((0x2000000u % OUTPUT_DMA_SIZE) == 0u,
               "the pos bend must divide one 1/128-cycle step exactly");
_Static_assert(OSC_POS_BEND_PER_STEP * OUTPUT_DMA_SIZE == (1u << 25),
               "one pos step over one block must move exactly 1/128 cycle");
_Static_assert(OSC_POS_BEND_CAP >= 1 && OSC_POS_BEND_CAP <= 64,
               "OSC_POS_BEND_CAP must be 1..64 (positionBendStep is int8_t)");

/*
 * Return whether position is a phase offset for this waveform (S086 P3/P4).
 *
 * What:       SINE, CRASH and all table waves use the full 32-bit phase range;
 *             NOISE and v1 user samples retain their existing no-position
 *             behaviour until the later sample stage.
 * Why:        one phase unit is position/128 of the wave or complete sample.
 * Inputs:     waveform ID.
 * Outputs:    1 for phase waves, otherwise 0.
 * Accessors:  osc_positionTrigger(), osc_positionBendApply().
 * Affiliates: osc_isTableWave(), S086_P4_SAMPLE_CYM_POS.md.
 */
static inline uint8_t osc_positionIsPhase(const uint8_t waveform)
{
	return (uint8_t)(waveform == SINE || waveform == CRASH ||
	                 osc_isTableWave(waveform));
}

/*
 * Apply position at a trigger (S086 P3/P4).
 *
 * What:       records the current position as applied and returns position
 *             << 25 for phase waves, which is position/128 of 2^32.
 * Why:        the trigger starts at today's phase plus the requested offset;
 *             position 0 returns zero and is bit-for-bit today's start.
 * Inputs:     osc->position and osc->waveform, after velocity modulation and
 *             after the engine's existing transient offset scaling.
 * Outputs:    phase offset for the caller and osc->positionApplied.
 * Accessors:  Drum/Snare/Cymbal/HiHat_triggerVoice().
 * Affiliates: osc_positionBendApply() (the continuous part), the
 *             osc1_position descriptor rows.
 */
static inline uint32_t osc_positionTrigger(OscInfo* osc)
{
	const uint32_t pos = (uint32_t)(osc->position & 127u);

	osc->positionApplied = (uint8_t)pos;
	if (!osc_positionIsPhase(osc->waveform))
		return 0u;
	return pos << 25;
}

/*
 * Start the continuous position bend for one block (S086 P3/P4).
 *
 * What:       computes the circular delta d from positionApplied to position
 *             (-64..+63; 127 -> 0 is +1), limits it to +-OSC_POS_BEND_CAP,
 *             advances positionApplied by d, and, for a phase wave with
 *             bend != 0, adds d x OSC_POS_BEND_PER_STEP to phaseInc and
 *             stores d in positionBendStep. Otherwise it stores 0 and leaves
 *             phaseInc alone, while still consuming the delta.
 * Why:        LFO, knob, velocity, Morph and automation values arrive once
 *             per block. Bending phaseInc for one block makes the phase
 *             glide and land exactly on the new offset after OUTPUT_DMA_SIZE
 *             samples, with no loop change. Storing the step in the
 *             oscillator (not returning it) means the caller keeps nothing
 *             live across the render (assessment F1). Consuming the delta
 *             for non-phase waves, and for Drum snc (bend = 0), means a later
 *             waveform or mode switch cannot release a stale bend.
 * Inputs:     osc->position, osc->positionApplied, osc->waveform; bend: 1 to
 *             bend, 0 to consume the delta only (Drum snc, S086 P3 Q12).
 * Outputs:    osc->phaseInc (bent), osc->positionBendStep,
 *             osc->positionApplied.
 * Accessors:  Drum_calcVoiceSyncBlock(), Snare_calcSyncBlockVoice(),
 *             Cymbal_calcSyncBlockVoice(), HiHat_calcSyncBlockVoice(),
 *             immediately before their osc1 render.
 * Affiliates: osc_positionBendRestore() (must follow the render);
 *             osc_setFreq() (its cache would keep a bend left in phaseInc);
 *             calcPeriodicInterp*() (end the block at start + phaseInc x size,
 *             which includes the bend); S086 P3 §2.3.
 */
static inline void osc_positionBendApply(OscInfo* osc, const uint8_t bend)
{
	const uint32_t u = ((uint32_t)(osc->position & 127u) -
	                    (uint32_t)osc->positionApplied + 64u) & 127u;
	int32_t d = (int32_t)u - 64;

	if (d > (int32_t)OSC_POS_BEND_CAP)
		d = (int32_t)OSC_POS_BEND_CAP;
	else if (d < -(int32_t)OSC_POS_BEND_CAP)
		d = -(int32_t)OSC_POS_BEND_CAP;
	osc->positionApplied =
		(uint8_t)(((uint32_t)osc->positionApplied + (uint32_t)d) & 127u);
	if (!bend || !osc_positionIsPhase(osc->waveform))
		d = 0;
	osc->positionBendStep = (int8_t)d;
	osc->phaseInc += (uint32_t)(d * (int32_t)OSC_POS_BEND_PER_STEP);
}

/*
 * End the continuous position bend after the block's render (S086 P3/P4).
 *
 * What:       subtracts positionBendStep x OSC_POS_BEND_PER_STEP from
 *             phaseInc, restoring it exactly, and clears positionBendStep.
 * Why:        phaseInc is cached by osc_setFreq(); a bend left in it would
 *             become a lasting pitch error. Reading the step from the
 *             oscillator keeps the caller free of live state (assessment F1).
 * Inputs:     osc->positionBendStep, osc->phaseInc.
 * Outputs:    osc->phaseInc (unbent), osc->positionBendStep = 0.
 * Accessors:  the same four render functions, immediately after the osc1
 *             render that osc_positionBendApply() preceded.
 * Affiliates: osc_positionBendApply().
 */
static inline void osc_positionBendRestore(OscInfo* osc)
{
	osc->phaseInc -= (uint32_t)((int32_t)osc->positionBendStep *
	                            (int32_t)OSC_POS_BEND_PER_STEP);
	osc->positionBendStep = 0;
}

#endif /* OSCILLATOR_H_ */
