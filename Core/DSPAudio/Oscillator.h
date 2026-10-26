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
 * Generated stock waves (S084 VX2 VX2-4.5).
 *
 * What:       sq3 sw3 (303 captures), sqa swa (analog models), tra (dirty
 *             triangle), appended after CRASH so stored values 0..5 keep
 *             their meaning.
 * Why:        plan §4.1.2 / D2 names; W4 layout in sample flash.
 * Inputs:     the oscN_wave rows under type wav.
 * Outputs:    Oscillator.c dispatch.
 * Accessors:  Oscillator.c, OscillatorPwm.c (pq3 = SQ3, pw3 = SW3),
 *             transientGenerator.c (bursts), MenuText.h waveformNames[].
 * Affiliates: StockWaves.c.
 */
#define SQ3                 0x06
#define SW3                 0x07
#define SQA                 0x08
#define TRA                 0x09
#define SWA                 0x0A
#define STOCK_WAVE_FIRST    SQ3

/*
 * Oscillator types (S084 P1 P1-3.1; S084 VX2 VX2-5.1).
 *
 * What:       OSC_TYPE_WAV plays the stock list (SINE..SWA); OSC_TYPE_PWM
 *             plays a computed pulse-width
 *             family (OscillatorPwm.c); OSC_TYPE_SMP / OSC_TYPE_SLP play an
 *             installed sample one-shot / looped (OscillatorSample.c).
 *             OSC_TYPE_WTB plays an installed wavetable (OscillatorWavetable.c):
 *             src selects the table and wav 0..127 the frame position.
 *             OSC_STOCK_WAVE_COUNT is the
 *             number of `wav` entries.
 * Why:        plan §4.1.1: each type exposes its continuous control on the
 *             existing `wav` row, so sweeps stay inside the selection.
 * Inputs:     the oscN_type descriptor rows (IM_SPECIAL_OSC_TYPE).
 * Outputs:    OscInfo.oscType; render and frequency dispatch.
 * Accessors:  Oscillator.c, OscillatorPwm.c, OscillatorSample.c,
 *             OscillatorWavetable.c, OscSection.c, InstrumentManager.c,
 *             InstrumentCellFormat.c.
 * Affiliates: MenuText.h oscTypeNames[] (same order, same count).
 */
#define OSC_TYPE_WAV          0u
#define OSC_TYPE_PWM          1u
#define OSC_TYPE_SMP          2u
#define OSC_TYPE_SLP          3u
#define OSC_TYPE_WTB          4u   /* S084 VX2 VX2-5.1, appended */
#define OSC_TYPE_COUNT        5u
#define OSC_STOCK_WAVE_COUNT  (SWA + 1u)

/*
 * Table-wave test for trigger phase offsets (S084 VX2 VX2-4.5).
 *
 * What:       1 for the band-limited table waves Tri, Saw, Rec and the five
 *             generated stock waves; 0 for Sin, Noi, Cym.
 * Why:        the engines start table waves at the (0xff << 20) offset;
 *             the former test waveform > SINE && waveform <= REC would
 *             start the new waves at phase 0.
 * Inputs:     waveform.  Outputs: 0/1.
 * Accessors:  the four engines' trigger paths (VX2-4.12).
 */
static inline uint8_t osc_isTableWave(uint8_t waveform)
{
    return (uint8_t)((waveform >= TRI && waveform <= REC) ||
                     (waveform >= SQ3 && waveform <= SWA));
}

/*
 * Pitch-slew length for run mode `fre` (S084 P1 P1-3.3).
 *
 * What:       132 samples, about 3 ms at 44,108 Hz. osc_armSlew() sets it and
 *             every renderer adds phaseIncStep to phaseInc once per sample.
 * Why:        plan §4.1.6: `fre` glides every oscillator from its old pitch
 *             to the new one without a zipper step. It lives beside the
 *             renderers because osc_setFreq() owns the slew bookkeeping;
 *             OscSection.h uses the same value for its trigger arm.
 * Inputs:     osc_armSlew(), oscSection_claimPending() (fre fallback).
 * Outputs:    slewSamplesLeft.
 * Accessors:  Oscillator.c, OscSection.c.
 * Affiliates: osc_setFreq(), OscillatorPwm.c, OscillatorSample.c.
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
	uint8_t		waveform;	// the selected waveform of the osc

	uint8_t		tableOffset;	//overtone table

	/*
	 * Type, source and slew state (S084 P1 P1-3.2).
	 *
	 * What:       oscType = OSC_TYPE_*; source = pwm family or sample index
	 *             (unused for `wav`); freqCacheType extends the frequency
	 *             cache key; phaseIncTarget/phaseIncStep/slewSamplesLeft
	 *             implement the per-sample `fre` pitch slew (renderers add
	 *             phaseIncStep to phaseInc every sample, always);
	 *             sampleStart/sampleEnd bound smp/slp playback (frames);
	 *             pwm holds the per-block width parameters.
	 * Why:        plan §4.1.1 (types), §4.1.5 (start/loop), §4.1.6 (slew).
	 * Inputs:     IM_SPECIAL_OSC_TYPE / _OSC_SOURCE / _OSC_WAVE writers;
	 *             osc_setFreq() (target, step); oscSection_beginTrigger()
	 *             (slew arm).
	 * Outputs:    read by every block renderer and osc_setFreq().
	 * Accessors:  Oscillator.c, OscillatorPwm.c, OscillatorSample.c,
	 *             OscSection.c, InstrumentManager.c.
	 * Affiliates: inside the engine structs (runtime-slot union); the
	 *             _Static_asserts in InstrumentManager.c bound the growth.
	 */
	uint8_t		oscType;
	uint8_t		source;
	uint8_t		freqCacheType;
	uint16_t	slewSamplesLeft;
	uint32_t	phaseIncTarget;
	int32_t		phaseIncStep;
	uint32_t	sampleStart;
	uint32_t	sampleEnd;
	/*
	 * Per-type block state (S084 VX2 VX2-5.2).
	 *
	 * What:       pwm (OscPwmState) for type pwm; wtb (OscWtbState) for type
	 *             wtb. An anonymous union: only the active type's member is
	 *             live, and each type re-initialises its member on bind or
	 *             prepare, so a type change never reads the other's bytes.
	 * Why:        keeps sizeof(OscInfo) at 120 B (runtime-slot margin); the
	 *             two states are never needed together.
	 * Writers:    oscPwm_prepare() (pwm); oscWavetable_bind() and the wtb
	 *             renderers (wtb).
	 * Readers:    the matching renderers.
	 */
	union {
		OscPwmState	pwm;
		OscWtbState	wtb;
	};


	float		pitchMod;	// modulation value for frequency
	float 		fmMod;

	float		modNodeValue;
	uint8_t		waveInterpNext;			/* next waveform used for morph blend */
	/*
	 * Requested wave/source bytes (S084 P1 review fix F2).
	 *
	 * What:       the last byte written to the `wav` row (waveRequest) and to
	 *             the `src` row (sourceRequest), before the clamp to the
	 *             current type's maximum. waveform/source hold the clamped
	 *             live values the renderers read.
	 * Why:        a type change re-derives waveform/source from these bytes,
	 *             so a pwm width or sample start survives a pass through
	 *             `wav` (maximum 5) and the Kit apply order. oscSample_bind()
	 *             re-derives the sample index from sourceRequest when the
	 *             installed sample count changes.
	 * Size:       both bytes sit in the existing padding between
	 *             waveInterpNext and the 4-byte-aligned waveInterpFrac, so
	 *             sizeof(OscInfo) is unchanged (120 B, measured).
	 * Writers:    the IM_SPECIAL_OSC_WAVE / _OSC_SOURCE writers only; a slot
	 *             reset zeroes them.
	 * Readers:    the IM_SPECIAL_OSC_TYPE writer, oscSample_bind().
	 */
	uint8_t		waveRequest;
	uint8_t		sourceRequest;
	float		waveInterpFrac;			/* blend amount [0..1] between waveform and next */
	uint32_t	waveInterpGeneration;	/* audio-block generation stamp */
	float		freqCacheValue;
	uint8_t		freqCacheWaveform;
	uint8_t		freqCacheValid;
	uint8_t		sampleCacheIndex;
	/*
	 * Catalogue gain code of the bound sample (S084 VX2 VX2-1.5).
	 *
	 * What:       the gain byte of the bound sample's catalogue entry;
	 *             0 when nothing is bound.
	 * Why:        Q15 matched levels; stored as the byte so OscInfo does
	 *             not grow (it fills the padding before
	 *             sampleCacheGeneration).
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

/* S084 VX2 VX2-5.2: the wtb state must fit the pwm union slot. */
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
/*
 * Render a generated stock wave (S084 VX2 VX2-4.6).
 *
 * What:       reads level osc->tableOffset of stock wave (waveform - SQ3)
 *             from sample flash with linear interpolation; while the stock
 *             block is invalid it renders the fallback table through
 *             calcWavetableOscBlock().
 * Why:        variable-length levels at the same cost as the 1,024-point
 *             tables; FM adds (mod x fmMod) << 19 like calcFmBlock.
 * Inputs:     osc, buf, size, gain.  Outputs: buf; phase state advanced.
 * Accessors:  calcNextOscSampleBlock(), calcNextOscSampleFmBlock().
 * Affiliates: StockWaves.h, calcWavetableOscBlock(), calcFmBlock().
 */
void calcStockWaveBlock(OscInfo* osc, int16_t* buf, const uint8_t size,
                        const float gain);
void calcStockWaveFmBlock(OscInfo* osc, int16_t* modBuffer, int16_t* buf,
                          uint8_t size, const float gain);
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
 * Band-limited wavetable octave index for a frequency (S084 P1 P1-3.1).
 *
 * What:       exposes the S073 Step 6 edge count so other render code
 *             (OscillatorPwm.c, and P2's wavetable reader) selects the same
 *             band-limited level as the `wav` tables.
 * Why:        avoids a second copy of the octave-edge table; the compare-based
 *             count keeps the cost pitch-independent.
 * Inputs:     f in Hz.  Outputs: 0..10 (NaN and f <= 0 give 0).
 * Accessors:  oscPwm_prepare().
 * Affiliates: osc_octaveEdgeHz[], freqToTableIndex().
 */
uint8_t osc_freqToTableIndex(float f);

/*
 * Arm the `fre` pitch slew for one oscillator (S084 P1 P1-3.3).
 *
 * What:       records the current phaseInc as the slew origin and
 *             OSC_SLEW_SAMPLES as its length; the next osc_setFreq() computes
 *             the per-sample step toward the new target.
 * Why:        plan §4.1.6: `fre` never resets phase; every oscillator glides
 *             from its previous pitch to the new one over about 3 ms.
 * Inputs:     osc.  Outputs: osc->slewSamplesLeft.
 * Accessors:  oscSection_beginTrigger(), oscSection_claimPending() (fallback).
 * Affiliates: osc_setFreq(), the renderers' per-sample phaseIncStep.
 */
void osc_armSlew(OscInfo* osc);

#endif /* OSCILLATOR_H_ */
