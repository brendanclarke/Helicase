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
#define OSC_SAMPLE_START 	0x06

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


	float		pitchMod;	// modulation value for frequency
	float 		fmMod;

	float		modNodeValue;
	uint8_t		waveInterpNext;			/* next waveform used for morph blend */
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
 *             the amount mappings (osc_syncDepthQ30, osc_ringK). The
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
 *             - Sine, table and noise phases already are.
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
 *             - Synced: Sin, Tri, Saw, Rec, Crash.
 *             - Rendered plain (no sync): Noi, user samples (plan A3/A4).
 *             - Waveform interpolation: both partners use the same list.
 * Why:        plan Q10: amt is the sync depth (0 free-running, 1 hard sync).
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
 * Render one carrier block with the ring law applied (S085 P1).
 *
 * What:       renders osc exactly as calcNextOscSampleBlock() would at
 *             gain 1, but stores
 *                 buf[i] = SSAT16(osc_i x (ringK + modBuf[i]) >> 15)
 *             instead of osc_i x gain. With modBuf = modulator x amt and
 *             ringK = (1 - amt) x 32768, this is
 *                 carrier x (1 - amt) + carrier x modulator x amt
 *             (plan K5). Every waveform is supported, including noise, user
 *             samples and waveform interpolation.
 * Why:        D1: a separate ring pass made rng as expensive as fm or more
 *             with user-sample oscillators. Folded into the store, it costs
 *             the plain loop + 0..4 instructions per sample.
 * Inputs:     osc (the carrier), modBuf[size] (the modulator block at gain
 *             amt), buf[size], size, ringK (osc_ringK()).
 * Outputs:    buf; osc->phase / osc->output as the plain render.
 * Accessors:  Drum_calcVoiceSyncBlock().
 * Affiliates: the ring copies in Oscillator.c; the mix path (the same
 *             modBuf).
 */
void calcNextOscSampleRingBlock(OscInfo* osc, const int16_t* modBuf,
                                int16_t* buf, const uint8_t size,
                                const int32_t ringK);

/*
 * Sync depth from the Drum amount (S085 P1).
 *
 * What:       amount (fmModAmount, 0..1) as Q30: 0 -> 0 (no pull), 1 ->
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
 * Ring dry gain from the Drum amount (S085 P1).
 *
 * What:       K = round((1 - amount) x 32768), 0..32768. K = 32768 at
 *             amount 0, so rng at amount 0 is the carrier exactly (the
 *             modulator block is then all zeros). K = 0 at amount 1 gives
 *             the pure ring product.
 * Inputs:     amount (fmModAmount, 0..1).  Outputs: K.
 * Range:      K + modBuf lies in [-32768, 65535]; times an int16 carrier it
 *             stays below 2^31.
 * Accessors:  Drum_calcVoiceSyncBlock() (once per block).
 * Affiliates: osc_ringSample() in Oscillator.c.
 */
static inline int32_t osc_ringK(const float amount)
{
	if (!(amount > 0.0f))
		return 32768;
	if (amount >= 1.0f)
		return 0;
	return (int32_t)((1.0f - amount) * 32768.0f + 0.5f);
}

#endif /* OSCILLATOR_H_ */
