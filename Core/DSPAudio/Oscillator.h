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
#include "OscSyncLane.h"
#include "OscillatorPwm.h"
#include "OscillatorWavetable.h"
//-----------------------------------------------------------

//the available osc waveforms
#define SINE 				0x00
#define TRI					0x01
#define SAW					0x02
#define REC					0x03
#define NOISE				0x04
#define CRASH				0x05

/*
 * Generated stock waves on the `wav` list (S085 C5.1).
 *
 * What:       wav values 6..10 play the five stock tables generated into
 *             sample flash (StockWaves.c); STOCK_WAVE_FIRST maps a wav value
 *             to its stock index (waveform - STOCK_WAVE_FIRST).
 *             OSC_STOCK_WAVE_COUNT is the length of the wav list.
 * Why:        plan §4.3.4. Values 0..5 keep their meaning; old kits whose wav
 *             was a user sample (6+) now play Re3..SwA or clamp to SwA (no
 *             migration, Q8).
 * Accessors:  Oscillator.c, OscillatorPwm.c, InstrumentManager.c,
 *             InstrumentCellFormat.c, modulationNode.c.
 * Affiliates: MenuText.h waveformNames[] (same order, count 11).
 */
#define SQ3                 0x06
#define SW3                 0x07
#define SQA                 0x08
#define TRA                 0x09
#define SWA                 0x0A
#define STOCK_WAVE_FIRST    SQ3
#define OSC_STOCK_WAVE_COUNT 11u

/*
 * Oscillator types (S085 C5.1).
 *
 * What:       WAV plays the wav list; PWM a computed pulse-width family
 *             (OscillatorPwm.c); SMP/SLP an installed sample one-shot/looped
 *             (OscillatorSample.c); WTB an installed wavetable
 *             (OscillatorWavetable.c). The wav row is each type's continuous
 *             control: stock wave, width, sample start, frame position.
 * Why:        plan §5 items 1-2.
 * Inputs:     the oscN_type rows (IM_SPECIAL_OSC_TYPE).
 * Outputs:    OscInfo.oscType; render and frequency dispatch.
 * Accessors:  Oscillator.c, the type renderers, OscSection.c,
 *             InstrumentManager.c, InstrumentCellFormat.c.
 * Affiliates: MenuText.h oscTypeNames[] (same order, count 5).
 */
#define OSC_TYPE_WAV    0u
#define OSC_TYPE_PWM    1u
#define OSC_TYPE_SMP    2u
#define OSC_TYPE_SLP    3u
#define OSC_TYPE_WTB    4u
#define OSC_TYPE_COUNT  5u

/*
 * Pitch slew length for run modes fre/itp (S085 C5.1).
 *
 * What:       132 samples (3 ms at 44,108 Hz) from the old to the new phase
 *             increment. osc_setFreq() recomputes the step once per
 *             32-sample block as remaining / max(left, 32), so the glide is
 *             linear for four blocks (128 samples) and the last 4/132 of the
 *             interval is spread over a fifth block: 160 samples in all,
 *             monotonic, never past the target (assessment A1).
 * Why:        plan §4.3.7: fre never resets phase; every slewing oscillator
 *             glides to the new pitch without a step.
 * Accessors:  osc_armSlew(), osc_setFreq().
 */
#define OSC_SLEW_SAMPLES 132u

#define phaseShift 1000

//-----------------------------------------------------------

typedef struct OscStruct
{
	int16_t 	output;
	uint32_t	phaseInc;
	uint32_t	phase;		// the current phase of the osc 8bit indexing 24 interpolation
	float	 	freq;		// frequency in [Hz]
	/*
	 * The `wav` row's live value (S085 P2 row swap).
	 *
	 * What:       the selection for every type: the wave (wav, 0..10), the
	 *             pwm shape (OSC_PWM_*), the bound sample (smp/slp) or the
	 *             bound wavetable (wtb), clamped to the type's maximum.
	 * Why:        the `wav` row picks what plays; `mod` (osc->source) is the
	 *             continuous control. One field per row keeps the velocity
	 *             direct pointer, the LFO adapters and Morph on the right
	 *             value for every type.
	 * Writers:    IM_SPECIAL_OSC_WAVE / _OSC_TYPE writers, the velocity
	 *             direct pointer, waveform interpolation (wav type only),
	 *             oscSample_bind() and oscWavetable_bind() (clamp; re-expand
	 *             from waveRequest after a reinstall).
	 */
	uint8_t		waveform;

	uint8_t		tableOffset;	//overtone table

	/*
	 * Type, source, slew and per-type state (S085 C5.2, block A).
	 *
	 * What:       oscType = OSC_TYPE_*; source = the `mod` row's live value,
	 *             the continuous control (pwm width, smp/slp start, wtb frame
	 *             position, 0..127; unused by wav; S085 P2 row swap);
	 *             freqCacheType and freqCacheSource extend the frequency cache
	 *             key; phaseIncTarget/phaseIncStep/
	 *             slewSamplesLeft implement the per-sample pitch slew (every
	 *             renderer adds phaseIncStep to its increment every sample,
	 *             always); sampleStart/sampleEnd bound smp/slp playback in
	 *             frames; the union holds the active type's block state
	 *             (pwm: width parameters; wtb: bound table and frame ramp).
	 * Why:        plan §5 items 1-2; one union member is live at a time and
	 *             each type re-initialises its member (oscPwm_prepare(),
	 *             oscWavetable_bind()), so a type change never reads the other
	 *             type's bytes.
	 * Writers:    the IM_SPECIAL_OSC_* writers, osc_setFreq(), osc_armSlew(),
	 *             oscSample_bind(), oscPwm_prepare(), oscWavetable_bind() and
	 *             the wtb renderers.
	 * Readers:    every block renderer and osc_setFreq().
	 * Affiliates: the engine structs (runtime-slot union); the slot
	 *             _Static_asserts in InstrumentManager.c bound the growth.
	 */
	uint8_t		oscType;
	uint8_t		source;
	uint8_t		freqCacheType;
	/* S085 P2 row swap: the pwm width lives in source, which the velocity
	** direct pointer can write without a writer, so it is part of the
	** osc_setFreq() cache key. Fills the padding before slewSamplesLeft. */
	uint8_t		freqCacheSource;
	uint16_t	slewSamplesLeft;
	uint32_t	phaseIncTarget;
	int32_t		phaseIncStep;
	uint32_t	sampleStart;
	uint32_t	sampleEnd;
	union {
		OscPwmState	pwm;
		OscWtbState	wtb;
	};


	float		pitchMod;	// modulation value for frequency
	float 		fmMod;

	float		modNodeValue;
	uint8_t		waveInterpNext;			/* next waveform used for morph blend */
	float		waveInterpFrac;			/* blend amount [0..1] between waveform and next */
	uint32_t	waveInterpGeneration;	/* audio-block generation stamp */

	/*
	 * Requested wave/source bytes (S085 C5.2, block B).
	 *
	 * What:       the last retained byte written to the `wav` row
	 *             (waveRequest: the selection) and the `mod` row
	 *             (sourceRequest: the continuous control), before the clamp to
	 *             the current type's maximum. waveform/source hold the clamped
	 *             live values the renderers read.
	 * Why:        a type change re-derives waveform/source from these, so a
	 *             pwm shape or a sample selection survives pwm -> wav -> pwm,
	 *             a smaller reinstall re-expands to the stored selection, and
	 *             the load order cannot clamp a value away (S084 P1 F2). Only retained writes record them
	 *             (menu, MIDI, load, Morph, automation); LFO overlays do not
	 *             (S085 D2).
	 * Writers:    the IM_SPECIAL_OSC_WAVE/_OSC_SOURCE writers (retained only).
	 * Readers:    the IM_SPECIAL_OSC_TYPE writer, oscSample_bind(),
	 *             oscWavetable_bind().
	 */
	uint8_t		waveRequest;
	uint8_t		sourceRequest;
	float		freqCacheValue;
	uint8_t		freqCacheWaveform;
	uint8_t		freqCacheValid;
	uint8_t		sampleCacheIndex;
	/*
	 * Catalogue gain code of the bound sample (S085 C5.2, block C).
	 *
	 * What:       the gain byte of the bound sample's catalogue entry, 0 when
	 *             nothing is bound; replaces the retired loop flag.
	 * Writers:    oscSample_bind().  Readers: oscSample_render(_Fm).
	 */
	uint8_t		sampleGainCode;
	uint32_t	sampleCacheGeneration;
	uint32_t	sampleCacheSize;
	const int16_t* sampleCacheData;

	//for midi
	uint16_t	midiFreq;  //upper 8 bit coarse, lower 8 bit fine -> the sound edit freq offset
	uint8_t baseNote;		// the last played midi note
	uint32_t startPhase;		// the OSC is reset to this phase on retrigger
} OscInfo;

_Static_assert(sizeof(OscWtbState) <= sizeof(OscPwmState),
               "wtb state must fit the pwm union slot");
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
int16_t calcSampleOsc(OscInfo* osc);
//-----------------------------------------------------------
/** set the played midi note.*/
void osc_setBaseNote(OscInfo* osc, uint8_t baseNote);
//-----------------------------------------------------------
/** recalculate the frequency if either the base note or the offset note value changed*/
void osc_recalcFreq(OscInfo* osc);

/*
 * Generated stock-wave renderers (S085 C5.3).
 *
 * What:       read level tableOffset of stock wave (waveform - SQ3) with
 *             linear interpolation and the per-sample slew; the FM variant
 *             adds (mod x fmMod) << 19 to the read phase, the calcFmBlock
 *             depth. Before the first install they render the fallback table
 *             through the same loop.
 * Why:        plan §4.3.4; same cost class as calcWavetableOscBlock().
 * Inputs:     osc, (modBuffer), buf, size, gain.
 * Outputs:    buf; osc->phase, osc->phaseInc.
 * Accessors:  calcNextOscSampleBlock(), calcNextOscSampleFmBlock().
 * Affiliates: stockWave_level(), osc_calcWavetableFreqValue(). ITCM.
 */
void calcStockWaveBlock(OscInfo* osc, int16_t* buf, const uint8_t size,
                        const float gain);

/*
 * Render one hard-sync slave block (S085 assessment A2, C10.6 step 4).
 *
 * What:       the slave oscillator's ordinary plain renderer for its type
 *             (wav: sine, Tri/Saw/Rec tables, noise, crash, stock waves and
 *             the waveform-interpolation pair; pwm; smp/slp; wtb) with the
 *             master's resets applied inside the per-sample loop through
 *             lane (OscSyncLane.h). One call per block, whatever the number
 *             of master wraps.
 * Why:        the segment render it replaces paid one renderer call per wrap;
 *             at one wrap per sample that broke the snc cost cap. Each type's
 *             loop is the same always-inline core its plain renderer uses, so
 *             a synced slave sounds exactly like the free-running one between
 *             resets (osc_run proves bit-identity with an all-zero lane).
 * Inputs:     osc (slave), buf, size, gain, lane (never NULL here).
 * Outputs:    buf; the slave's phase and phaseInc advanced and reset.
 * Accessors:  oscSection_renderSync() only.
 * Affiliates: calc*Core() in Oscillator.c, oscPwm_renderSync(),
 *             oscSample_renderSync(), oscWavetable_renderSync(). ITCM.
 */
void osc_renderSyncBlock(OscInfo* osc, int16_t* buf, uint8_t size,
                         float gain, const OscSyncLane* lane);
void calcStockWaveFmBlock(OscInfo* osc, int16_t* modBuffer, int16_t* buf,
                          uint8_t size, const float gain);

/*
 * Arm the pitch slew (S085 C5.3).
 *
 * What:       sets slewSamplesLeft = OSC_SLEW_SAMPLES; the next osc_setFreq()
 *             computes the per-sample step from the current phaseInc toward
 *             the new target.
 * Why:        plan §4.3.7: fre glides every oscillator; itp glides the
 *             modulators.
 * Inputs:     osc.  Outputs: osc->slewSamplesLeft.
 * Accessors:  oscSection_beginTrigger(), oscSection_claimPending().
 * Affiliates: osc_setFreq(), every renderer's phaseIncStep.
 */
void osc_armSlew(OscInfo* osc);

/*
 * Band-limited octave index for a frequency (S085 C5.3).
 *
 * What:       the S073 Step 6 edge count (0..10), exposed.
 * Why:        the pwm families select the same band-limited level as the wav
 *             tables, at frequency x warp compression.
 * Accessors:  oscPwm_prepare().  Affiliates: freqToTableIndex().
 */
uint8_t osc_freqToTableIndex(float f);

/*
 * Whether a wav value is a band-limited table wave (S085 C5.3).
 *
 * What:       1 for TRI, SAW, REC and SQ3..SWA; 0 for SINE, NOISE, CRASH.
 * Why:        the engines' trigger phase offset ((0xff << 20) x offset)
 *             applies to every table wave, including the new ones.
 * Accessors:  the four engines' trigger functions.
 */
static inline uint8_t osc_isTableWave(uint8_t waveform)
{
    return (uint8_t)((waveform >= TRI && waveform <= REC) ||
                     (waveform >= SQ3 && waveform <= SWA));
}

#endif /* OSCILLATOR_H_ */
