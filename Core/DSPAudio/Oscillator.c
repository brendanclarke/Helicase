/*
 * Oscillator.c
 *
 *  Created on: 02.04.2012
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

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */



#include "Oscillator.h"
#include "OscillatorSample.h"
#include "StockWaves.h"
#include "random.h"
#include "Samples.h"
#include "MidiPitch.h"
#include "modulationNode.h"
#include <math.h>
#include <string.h>
// TODO DSP_PORT
// #include "sequencer.h"


//TODO die phaseInc berechnung kann man doch sicher per LUT machen!
//-----------------------------------------------------------
/*
 * Wavetable octave edges (S073 Step 6).
 *
 * What:       T_k = 440 * 2^(k - 5.75) Hz for k = 1..10 (C0 ... C9), stored
 *             as the nearest floats. The table index is the number of edges
 *             at or below f.
 * Why:        the old index, (int)((69 + 12*log2f(f/440))/12) clamped to
 *             0..10, is floor(5.75 + log2(f/440)): the same count, up to
 *             float and log2f rounding at each edge. Comparing against 10
 *             constants removes a log2f() per wavetable oscillator per block
 *             during pitch sweeps (audit F7).
 * Inputs:     none (flash constants, 40 B).
 * Outputs:    read by freqToTableIndex().
 * Accessors:  freqToTableIndex().
 * Affiliates: tools/dsp_test/test_octave.c reports every input where the
 *             old and new index differ (only within a few ulps of an edge).
 */
static const float osc_octaveEdgeHz[10] = {
	16.3515987f,   32.7031975f,   65.406395f,   130.81279f,   261.62558f,
	523.25116f,  1046.50232f,  2093.00464f,  4186.00928f,  8372.01855f,
};

/*
 * Band-limited wavetable octave for a frequency (S073 Step 6).
 *
 * What:       counts the octave edges <= f with ten branch-free compares.
 *             NaN and f <= 0 give 0 (every compare is false); +inf gives 10.
 *             This matches the old expression's results for those inputs.
 * Why:        see osc_octaveEdgeHz. Constant cost: all ten compares always
 *             run (no early exit), so the cost does not depend on pitch.
 * Inputs:     f, the effective oscillator frequency in Hz.
 * Outputs:    the table index 0..10.
 * Accessors:  osc_calcWavetableFreqValue().
 * Affiliates: osc_setFreq()'s frequency cache (existing, kept); wavetable.c
 *             table layout (11 octaves).
 */
static inline uint8_t freqToTableIndex(const float f)
{
	uint8_t index = 0u;
	uint8_t k;

	for (k = 0u; k < 10u; k++)
		index = (uint8_t)(index + (uint8_t)(f >= osc_octaveEdgeHz[k]));
	return index;
}
//-----------------------------------------------------------
static inline uint32_t freq2PhaseIncr(const float f) //4096
{
	return (((TABLESIZE*f)/REAL_FS))*1048576 ; // 1048576 <-> (<<20)
}
//-----------------------------------------------------------
static inline uint32_t freq2PhaseIncr1024(const float f)
{
	return (((1024*f)/REAL_FS))*4194304 ; //(<<22)
}
//-----------------------------------------------------------
static inline uint32_t freq2PhaseIncr32767(const float f)
{
	return (((1024*f)/REAL_FS))*131072 ; //(<<17)
}
//-----------------------------------------------------------
static INDTCMZ int16_t osc_interp_a[OUTPUT_DMA_SIZE];
static INDTCMZ int16_t osc_interp_b[OUTPUT_DMA_SIZE];

/* Waveform interpolation is intentionally block-based here. The old scalar
** approach evaluated both neighboring waveforms through the normal dispatcher
** for every output sample. Rendering each neighbor once into a 32-frame scratch
** buffer keeps the audible interpolation behavior but turns the extra work into
** two ordinary block renders plus a cheap blend loop. */
static inline void osc_clearWaveInterp(OscInfo* osc)
{
	osc->waveInterpFrac = 0.f;
	osc->waveInterpNext = osc->waveform;
	osc->waveInterpGeneration = 0u;
}
//-----------------------------------------------------------
static inline uint8_t osc_waveInterpActive(const OscInfo* osc)
{
	const uint8_t maxWave = (uint8_t)(OSC_STOCK_WAVE_COUNT - 1u);

	/* S084 P1 P1-3.5: interpolation applies to the `wav` stock list only;
	** pwm/smp/slp controls are continuous by design and never use it. The
	** budget is now granted by OscRenderPool (modulationNode.c), after any
	** `itp` crossfades have claimed their units. */
	if (osc->oscType != OSC_TYPE_WAV) {
		return 0u;
	}
	if (!modNode_getWaveInterpEnabled()) {
		return 0u;
	}
	if (osc->waveInterpGeneration != modNode_getWaveInterpGeneration()) {
		return 0u;
	}
	if (osc->waveInterpFrac <= 0.f || osc->waveInterpFrac >= 1.f) {
		return 0u;
	}
	if (osc->waveInterpNext <= osc->waveform) {
		return 0u;
	}
	if (osc->waveform > maxWave || osc->waveInterpNext > maxWave) {
		return 0u;
	}
	return 1u;
}
//-----------------------------------------------------------
static inline int16_t osc_evalWaveAtPhase(const OscInfo* src, const uint8_t waveform, const uint32_t phase)
{
	OscInfo tmp = *src;
	int16_t out = 0;

	tmp.waveform = waveform;
	tmp.phase = phase;
	tmp.waveInterpFrac = 0.f;
	tmp.waveInterpNext = waveform;
	tmp.waveInterpGeneration = 0u;
	calcNextOscSampleBlock(&tmp, &out, 1u, 1.f);
	return out;
}
//-----------------------------------------------------------
static inline int16_t osc_evalWaveAtPhaseFm(const OscInfo* src, const uint8_t waveform, const uint32_t phase, const int16_t modSample)
{
	OscInfo tmp = *src;
	int16_t out = 0;
	int16_t modBuf[1] = { modSample };

	tmp.waveform = waveform;
	tmp.phase = phase;
	tmp.waveInterpFrac = 0.f;
	tmp.waveInterpNext = waveform;
	tmp.waveInterpGeneration = 0u;
	calcNextOscSampleFmBlock(&tmp, modBuf, &out, 1u, 1.f);
	return out;
}
//-----------------------------------------------------------
static void calcPeriodicInterpBlock(OscInfo* osc, int16_t* buf, const uint8_t size, const float gain)
{
	const uint8_t base = osc->waveform;
	const uint8_t next = osc->waveInterpNext;
	const float frac = osc->waveInterpFrac;
	const uint32_t startPhase = osc->phase;
	const uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	OscInfo oscA = *osc;
	OscInfo oscB = *osc;
	uint8_t i;

	oscA.waveform = base;
	osc_clearWaveInterp(&oscA);
	oscB.waveform = next;
	osc_clearWaveInterp(&oscB);

	calcNextOscSampleBlock(&oscA, osc_interp_a, size, 1.f);
	calcNextOscSampleBlock(&oscB, osc_interp_b, size, 1.f);

	for (i = 0; i < size; i++) {
		const int16_t a = osc_interp_a[i];
		const int16_t b = osc_interp_b[i];
		const int16_t out = (int16_t)(a + frac * (b - a));
		buf[i] = out * gain;
	}
	/* S084 P1 P1-3.7: the block advanced by the same per-sample ramp. */
	osc->phase = startPhase + inc * (uint32_t)size +
		(uint32_t)step * ((uint32_t)size * (uint32_t)(size - 1u)) / 2u;
	osc->phaseInc = inc + (uint32_t)(step * (int32_t)size);
}
//-----------------------------------------------------------
static int16_t calcPeriodicInterp(OscInfo* osc)
{
	const uint8_t base = osc->waveform;
	const uint8_t next = osc->waveInterpNext;
	const float frac = osc->waveInterpFrac;
	const uint32_t phase = osc->phase;

	const int16_t a = osc_evalWaveAtPhase(osc, base, phase);
	const int16_t b = osc_evalWaveAtPhase(osc, next, phase);
	const int16_t out = (int16_t)(a + frac * (b - a));

	osc->phase = phase + osc->phaseInc;
	osc->output = out;
	return out;
}
//-----------------------------------------------------------
static void calcPeriodicInterpFmBlock(OscInfo* osc, int16_t* modBuffer, int16_t* buf, const uint8_t size, const float gain)
{
	const uint8_t base = osc->waveform;
	const uint8_t next = osc->waveInterpNext;
	const float frac = osc->waveInterpFrac;
	const uint32_t startPhase = osc->phase;
	const uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	OscInfo oscA = *osc;
	OscInfo oscB = *osc;
	int16_t lastOut = 0;
	uint8_t i;

	oscA.waveform = base;
	osc_clearWaveInterp(&oscA);
	oscB.waveform = next;
	osc_clearWaveInterp(&oscB);

	calcNextOscSampleFmBlock(&oscA, modBuffer, osc_interp_a, size, 1.f);
	calcNextOscSampleFmBlock(&oscB, modBuffer, osc_interp_b, size, 1.f);

	for (i = 0; i < size; i++) {
		const int16_t a = osc_interp_a[i];
		const int16_t b = osc_interp_b[i];
		lastOut = (int16_t)(a + frac * (b - a));
		buf[i] = lastOut * gain;
	}
	/* S084 P1 P1-3.7: the block advanced by the same per-sample ramp. */
	osc->phase = startPhase + inc * (uint32_t)size +
		(uint32_t)step * ((uint32_t)size * (uint32_t)(size - 1u)) / 2u;
	osc->phaseInc = inc + (uint32_t)(step * (int32_t)size);
	osc->output = lastOut;
}
//-----------------------------------------------------------
static int16_t calcPeriodicInterpFm(OscInfo* osc, OscInfo* modOsc)
{
	const uint8_t base = osc->waveform;
	const uint8_t next = osc->waveInterpNext;
	const float frac = osc->waveInterpFrac;
	const uint32_t phase = osc->phase;

	const int16_t a = osc_evalWaveAtPhaseFm(osc, base, phase, modOsc->output);
	const int16_t b = osc_evalWaveAtPhaseFm(osc, next, phase, modOsc->output);
	const int16_t out = (int16_t)(a + frac * (b - a));

	osc->phase = phase + osc->phaseInc;
	osc->output = out;
	return out;
}
//-----------------------------------------------------------
INITCM void calcSineBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain)
{
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut;

		const uint32_t index =  oscPhase;
		uint32_t  itg	= index>>20;

		#if INTERPOLATE_OSC
			oscOut = sine_table[itg++];
			const float frac = (index & 0xfffff) * 0.00000095367431640625f;
			oscOut += frac*(sine_table[itg] - oscOut);
		#else
			oscOut = sine_table[itg];
		#endif

		/*
		 * Per-sample pitch slew in every block renderer (S084 P1 P1-3.7).
		 *
		 * What:       the loop advances phase by a local increment and then
		 *             adds osc->phaseIncStep to that increment every sample;
		 *             the final increment is written back to osc->phaseInc.
		 * Why:        run mode `fre` glides every oscillator from its
		 *             previous pitch to the new one over OSC_SLEW_SAMPLES
		 *             without zipper steps (plan §4.1.6). The add always
		 *             executes, with step 0 when no slew runs, so the cost
		 *             never depends on the mode (constant CPU).
		 * Inputs:     osc->phaseInc, osc->phaseIncStep (set by osc_setFreq()).
		 * Outputs:    osc->phase, osc->phaseInc.
		 * Accessors:  the block renderers (ITCM).
		 * Affiliates: osc_setFreq() (computes the step), osc_armSlew(),
		 *             OscillatorPwm.c / OscillatorSample.c (same rule).
		 */
		osc->phase = oscPhase + inc;
		inc += (uint32_t)step;

		buf[i] = oscOut * gain;
	}
	osc->phaseInc = inc;
}
//-----------------------------------------------------------
int16_t calcSine(OscInfo* osc)
{
	const uint32_t oscPhase = osc->phase;
	int16_t oscOut;

	const uint32_t index =  oscPhase;
	uint32_t  itg	= index>>20;

#if INTERPOLATE_OSC
	oscOut = sine_table[itg++];
	float frac	= (index & 0xfffff) * 0.00000095367431640625f;
	oscOut += frac*(sine_table[itg] - oscOut);
#else
	oscOut = sine_table[itg];
#endif

	osc->phase = oscPhase + osc->phaseInc;
	osc->output = oscOut;
	return oscOut;
}

//-----------------------------------------------------------
INITCM void calcFmSineBlock(OscInfo* osc, int16_t* modBuffer, int16_t* buf, uint8_t size,const float gain)
{
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut ;

		const uint32_t index =  oscPhase + (((uint32_t)(modBuffer[i]*osc->fmMod))<<17);
		uint32_t  itg	= index>>20;

	#if INTERPOLATE_FM_OSC
		oscOut = sine_table[itg++];
		const float frac	= (index & 0xfffff) * 0.00000095367431640625f;
		oscOut += frac*(sine_table[itg] - oscOut);
	#else
		oscOut = sine_table[itg];
	#endif

		/* S084 P1 P1-3.7: per-sample slew, see calcSineBlock(). */
		osc->phase = oscPhase + inc;
		inc += (uint32_t)step;
		buf[i] =  oscOut * gain;
	}
	osc->phaseInc = inc;
}
//-----------------------------------------------------------
int16_t calcFmSine(OscInfo* osc, OscInfo* modOsc)
{
	const uint32_t oscPhase 	= osc->phase;
	int16_t oscOut ;

	const uint32_t index =  oscPhase + (((uint32_t)(modOsc->output*osc->fmMod))<<17);
	uint32_t  itg	= index>>20;

#if INTERPOLATE_FM_OSC
	oscOut = sine_table[itg++];
	const float frac	= (index & 0xfffff) * 0.00000095367431640625f;
	oscOut += frac*(sine_table[itg] - oscOut);
#else
	oscOut = sine_table[itg];
#endif

	osc->phase = oscPhase + osc->phaseInc;
	osc->output = oscOut;
	return oscOut;
};
//---------------------------------------------------------------
INITCM void calcFmBlock(OscInfo* osc, const int16_t table[][1024], int16_t* modBuffer, int16_t* buf, uint8_t size ,const float gain)
{
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut ;
		const uint32_t index =  oscPhase + (((uint32_t)(modBuffer[i]*osc->fmMod))<<19);

		uint32_t  itg	= index>>22;

	#if INTERPOLATE_FM_OSC
		const uint32_t next = (itg + 1u) & 0x000003FFu;
		oscOut = table[osc->tableOffset][itg];
		const float frac	= (index&0x003FFFFF)*2.38418579101562e-07f;
		oscOut += frac*(table[osc->tableOffset][next] - oscOut);

	#else
		oscOut = table[overtoneIndex][itg];
	#endif

		/* S084 P1 P1-3.7: per-sample slew, see calcSineBlock(). */
		osc->phase = oscPhase + inc;
		inc += (uint32_t)step;
		osc->output = oscOut;
		buf[i] = oscOut * gain;
	}
	osc->phaseInc = inc;
}
//-----------------------------------------------------------
int16_t calcFm(OscInfo* osc, OscInfo* modOsc, const int16_t table[][1024])
{

	const uint32_t oscPhase = osc->phase;
	int16_t oscOut ;

	const uint32_t index =  oscPhase + (((uint32_t)(modOsc->output*osc->fmMod))<<19);

	uint32_t  itg	= index>>22;

#if INTERPOLATE_FM_OSC
	const uint32_t next = (itg + 1u) & 0x000003FFu;
	oscOut = table[osc->tableOffset][itg];
	const float frac	= (index&0x003FFFFF)*2.38418579101562e-07f;
	oscOut += frac*(table[osc->tableOffset][next] - oscOut);
#else
	oscOut = table[overtoneIndex][itg];
#endif

	osc->phase = oscPhase + osc->phaseInc;
	osc->output = oscOut;
	return oscOut;
}
//-----------------------------------------------------------
INITCM void calcNextOscSampleFmBlock(OscInfo* osc, int16_t* modBuffer, int16_t* buf, uint8_t size ,const float gain)
{
	/*
	 * Render one oscillator block by type (S084 P1 P1-3.6).
	 *
	 * What:       pwm and sample types go to their own renderers; `wav` keeps
	 *             the existing waveform switch and waveform interpolation; an
	 *             out-of-list `wav` value renders silence.
	 * Why:        plan §4.1.1 type dispatch; keeps every type's hot loop
	 *             specialised (no per-sample type test).
	 * Inputs:     osc (type, waveform, phase state), buf, size, gain.
	 * Outputs:    buf (size samples, scaled by gain), osc phase advanced.
	 * Accessors:  the engines through OscSection (P1-7).
	 * Affiliates: oscPwm_renderFm(), oscSample_renderFm(), osc_setFreq().
	 */
	if (osc->oscType == OSC_TYPE_PWM) {
		oscPwm_renderFm(osc, modBuffer, buf, size, gain);
		return;
	}
	if (osc->oscType == OSC_TYPE_SMP || osc->oscType == OSC_TYPE_SLP) {
		oscSample_renderFm(osc, modBuffer, buf, size, gain);
		return;
	}
	/* S084 VX2 VX2-5.5: wavetable type. */
	if (osc->oscType == OSC_TYPE_WTB) {
		oscWavetable_renderFm(osc, modBuffer, buf, size, gain);
		return;
	}
	if (osc_waveInterpActive(osc)) {
		calcPeriodicInterpFmBlock(osc, modBuffer, buf, size, gain);
		return;
	}

	switch(osc->waveform)
		{
		case SINE:
			calcFmSineBlock(osc,modBuffer,buf,size,gain);
			break;

		case SAW:
			calcFmBlock(osc,sawTable,modBuffer,buf,size,gain);
			break;

		case TRI:
			calcFmBlock(osc, triTable, modBuffer,buf,size,gain);
			break;

		case REC:
			calcFmBlock(osc,recTable,modBuffer,buf,size,gain);
			break;

		case NOISE:
			//TODO FM noise - needed since its tuned digi noise!
			calcNoiseBlock(osc,buf,size,gain);
			break;

		case CRASH:
			calcSampleOscFmBlock(osc,modBuffer,buf,size,gain);
			break;

		case SQ3:
		case SW3:
		case SQA:
		case TRA:
		case SWA:
			/* S084 VX2 VX2-4.7: generated stock waves. */
			calcStockWaveFmBlock(osc, modBuffer, buf, size, gain);
			break;

		default:
			memset(buf, 0, (size_t)size * sizeof(int16_t));
			break;

		}
}
//-----------------------------------------------------------
int16_t calcNextOscSampleFm(OscInfo* osc,OscInfo* modOsc)
{
	/* S084 P1 P1-3.6: type dispatch; the scalar path remains for compatibility
	** with any single-sample caller. */
	if (osc->oscType == OSC_TYPE_PWM ||
		osc->oscType == OSC_TYPE_SMP || osc->oscType == OSC_TYPE_SLP ||
		osc->oscType == OSC_TYPE_WTB) {
		int16_t modBuf[1] = { modOsc ? modOsc->output : 0 };
		int16_t out[1] = { 0 };

		calcNextOscSampleFmBlock(osc, modBuf, out, 1u, 1.0f);
		osc->output = out[0];
		return out[0];
	}
	if (osc_waveInterpActive(osc)) {
		return calcPeriodicInterpFm(osc, modOsc);
	}

	switch(osc->waveform)
	{
	case SINE:
		return calcFmSine(osc,modOsc);
		break;

	case SAW:
		return calcFm(osc,modOsc,sawTable);
		break;

	case TRI:
		return calcFm(osc,modOsc, triTable);
		break;

	case REC:
		return calcFm(osc,modOsc,recTable);
		break;

	case NOISE:
		return calcNoise(osc);
		break;

	case CRASH:
		return calcSampleOscFm(osc,modOsc);
		break;

	case SQ3:
	case SW3:
	case SQA:
	case TRA:
	case SWA: {
		/* S084 VX2 VX2-4.7: one-sample block call keeps the scalar path. */
		int16_t modBuf[1] = { modOsc ? modOsc->output : 0 };
		int16_t out[1] = { 0 };

		calcStockWaveFmBlock(osc, modBuf, out, 1u, 1.0f);
		osc->output = out[0];
		return out[0]; }

	default:

		return 0;
		break;

	}
	return 0;
}
//-----------------------------------------------------------
INITCM void calcNoiseBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain)
{
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	int i;
	for(i=0;i<size;i++)
	{
		const uint32_t lastPhase = osc->phase;
		/* S084 P1 P1-3.7: per-sample slew, see calcSineBlock(). */
		osc->phase += inc;
		inc += (uint32_t)step;

		if(lastPhase > osc->phase)
		{
			//overflow happened -> phaseWrapped
			// osc->output = (int16_t)(GetRngValue()& 0x7FFF); //normal pitched white noise
			osc->output = (int16_t)(GetRngValue()); //normal pitched white noise
		}

		buf[i] = osc->output * gain;
	}
	osc->phaseInc = inc;
}
//-----------------------------------------------------------
int16_t calcNoise(OscInfo* osc)
{
	osc->phase += osc->phaseInc;

	//check overflow flag, ( if osc->phase == osc->phaseInc a reset occured ==> retrigger, too
	APSR_Type apsr;
	apsr.w = __get_APSR();
	if(apsr.b.V ||  (osc->phase == osc->phaseInc))
	{
		//overflow happened -> phaseWrapped

		// uint16_t rnd = (int16_t)(GetRngValue()& 0x7FFF);
		uint16_t rnd = (int16_t)(GetRngValue());
		// uint16_t rnd = 0;
		if( rnd > 0x00ff)
		{
			if( rnd > 0x000f)
			{
				osc->output = 32767;
			}
			else
			{
				osc->output = -32768;
			}

		}
	}
	else
	{
			osc->output = 0;
	}
	return osc->output;
}
//-----------------------------------------------------------
INITCM void calcNextOscSampleBlock(OscInfo* osc, int16_t* buf, const uint8_t size, const float gain)
{
	/* S084 P1 P1-3.6: type dispatch; see calcNextOscSampleFmBlock(). */
	if (osc->oscType == OSC_TYPE_PWM) {
		oscPwm_render(osc, buf, size, gain);
		return;
	}
	if (osc->oscType == OSC_TYPE_SMP || osc->oscType == OSC_TYPE_SLP) {
		oscSample_render(osc, buf, size, gain);
		return;
	}
	/* S084 VX2 VX2-5.5: wavetable type. */
	if (osc->oscType == OSC_TYPE_WTB) {
		oscWavetable_render(osc, buf, size, gain);
		return;
	}
	if (osc_waveInterpActive(osc)) {
		calcPeriodicInterpBlock(osc, buf, size, gain);
		return;
	}

	switch(osc->waveform)
		{
		case SINE:
			calcSineBlock(osc,buf,size, gain);
			break;

		case SAW:
			calcWavetableOscBlock(osc,sawTable,buf,size, gain);
			break;

		case TRI:
			calcWavetableOscBlock(osc,triTable,buf,size, gain);
			break;

		case REC:
			calcWavetableOscBlock(osc,recTable,buf,size, gain);
			break;

		case NOISE:
			calcNoiseBlock(osc,buf,size, gain);
			break;

		case CRASH:
			calcSampleOscBlock(osc,buf,size, gain);
			break;

		case SQ3:
		case SW3:
		case SQA:
		case TRA:
		case SWA:
			/* S084 VX2 VX2-4.7: generated stock waves. */
			calcStockWaveBlock(osc, buf, size, gain);
			break;

		default://sample playback
			memset(buf, 0, (size_t)size * sizeof(int16_t));
			break;
		}
}
//-----------------------------------------------------------
int16_t calcNextOscSample(OscInfo* osc)
{
	/* S084 P1 P1-3.6: type dispatch; see calcNextOscSampleFm(). */
	if (osc->oscType == OSC_TYPE_PWM ||
		osc->oscType == OSC_TYPE_SMP || osc->oscType == OSC_TYPE_SLP ||
		osc->oscType == OSC_TYPE_WTB) {
		int16_t out[1] = { 0 };

		calcNextOscSampleBlock(osc, out, 1u, 1.0f);
		osc->output = out[0];
		return out[0];
	}
	if (osc_waveInterpActive(osc)) {
		return calcPeriodicInterp(osc);
	}

	switch(osc->waveform)
	{
	case SINE:
		return calcSine(osc);
		break;

	case SAW:
		return calcWavetableOsc(osc,sawTable);
		break;

	case TRI:
		return calcWavetableOsc(osc,triTable);
		break;

	case REC:
		return calcWavetableOsc(osc,recTable);
		break;

	case NOISE:
		return calcNoise(osc);
		break;

	case CRASH:
		return calcSampleOsc(osc);
		break;

	case SQ3:
	case SW3:
	case SQA:
	case TRA:
	case SWA: {
		/* S084 VX2 VX2-4.7: one-sample block call keeps the scalar path. */
		int16_t out[1] = { 0 };

		calcStockWaveBlock(osc, out, 1u, 1.0f);
		osc->output = out[0];
		return out[0]; }

	default:

		return 0;
		break;

	}
	return 0;
};
//---------------------------------------------------------------
INITCM void calcWavetableOscBlock(OscInfo* osc, const int16_t table[][1024], int16_t* buf, const uint8_t size ,const float gain)
{
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
			int16_t oscOut ;
			uint32_t  itg	= oscPhase>>22;
		#if INTERPOLATE_OSC
			//todo use ldm instead of ldr to laod multiple values from memory
			const uint32_t next = (itg + 1u) & 0x000003FFu;
			oscOut = table[osc->tableOffset][itg];
			const float frac = (oscPhase & 0x003FFFFF) * 2.384185791015625e-07f; // 1 / 2^22
			oscOut += frac*(table[osc->tableOffset][next] - oscOut);
		#else
			oscOut = table[osc->tableOffset][itg];
		#endif

			/* S084 P1 P1-3.7: per-sample slew, see calcSineBlock(). */
			osc->phase = oscPhase + inc;
			inc += (uint32_t)step;
			buf[i] = oscOut * gain;
	}
	osc->phaseInc = inc;
}

//---------------------------------------------------------------
int16_t calcWavetableOsc(OscInfo* osc,  const int16_t table[][1024])
{
	const uint32_t oscPhase = osc->phase;
	int16_t oscOut ;

	uint32_t  itg	= oscPhase>>22;

#if INTERPOLATE_OSC
	//todo use ldm instead of ldr to laod multiple values from memory
	const uint32_t next = (itg + 1u) & 0x000003FFu;
	oscOut = table[osc->tableOffset][itg];
	const float frac = (oscPhase & 0x003FFFFF) * 2.384185791015625e-07f; // 1 / 2^22
	oscOut += frac*(table[osc->tableOffset][next] - oscOut);
#else
	oscOut = table[osc->tableOffset][itg];
#endif

	osc->phase = oscPhase + osc->phaseInc;
	osc->output = oscOut;
	return oscOut;

};

//------------------------------------------------------------------
/*
 * Render a generated stock wave (S084 VX2 VX2-4.6).
 *
 * What:       reads level osc->tableOffset of stock wave (waveform - SQ3)
 *             with linear interpolation: index = phase >> shift, next =
 *             (index + 1) & mask, frac = (phase << (32 - shift)) * 2^-32;
 *             per-sample slew as every table renderer. While the stock block
 *             is invalid it renders the fallback table through
 *             calcWavetableOscBlock() (same cost class).
 * Why:        plan §4.1.2: variable-length levels at the same cost as the
 *             existing 1,024-point tables (about 24 instructions; FM about
 *             35, modulation added as (mod x fmMod) << 19 like calcFmBlock).
 * Inputs:     osc (waveform, tableOffset, phase state), buf, size, gain.
 * Outputs:    buf; osc->phase, osc->phaseInc.
 * Accessors:  calcNextOscSampleBlock(), calcNextOscSampleFmBlock().
 * Affiliates: stockWave_level(), osc_calcWavetableFreqValue(). ITCM.
 */
INITCM void calcStockWaveBlock(OscInfo* osc, int16_t* buf, const uint8_t size,
                               const float gain)
{
	uint32_t inc;
	int32_t step;
	const int16_t* tab;
	uint8_t shift;
	uint16_t mask;
	uint8_t i;

	if (!osc || !buf)
		return;
	if (!sampleMemory_stockValid()) {
		const int16_t (*table)[1024];

		switch (osc->waveform) {
		case SW3:
		case SWA:
			table = sawTable;
			break;
		case TRA:
			table = triTable;
			break;
		default:
			table = recTable;
			break;
		}
		calcWavetableOscBlock(osc, table, buf, size, gain);
		return;
	}
	{
		const stock_level_t lv = stockWave_level(
			(uint8_t)(osc->waveform - SQ3), osc->tableOffset);

		tab = lv.tab;
		shift = lv.shift;
		mask = lv.mask;
	}
	inc = osc->phaseInc;
	step = osc->phaseIncStep;
	for (i = 0u; i < size; i++) {
		const uint32_t oscPhase = osc->phase;
		const uint32_t index = oscPhase >> shift;
		const uint32_t next = (index + 1u) & mask;
		const float frac =
			(float)(oscPhase << (32u - shift)) * 2.3283064365386963e-10f;
		int16_t oscOut = tab[index];

		oscOut = (int16_t)((float)oscOut +
			frac * (float)((int32_t)tab[next] - (int32_t)oscOut));
		buf[i] = oscOut * gain;
		/* S084 P1 P1-3.7: per-sample slew, see calcSineBlock(). */
		osc->phase = oscPhase + inc;
		inc += (uint32_t)step;
	}
	osc->phaseInc = inc;
}

/*
 * Render a generated stock wave with FM (S084 VX2 VX2-4.6).
 *
 * What:       calcStockWaveBlock with the modulator offset
 *             (mod x fmMod) << 19, the existing calcFmBlock depth. The
 *             fallback path uses calcFmBlock() on the same table.
 * Why:        keeps FM through the stock waves equal to the other tables.
 * Inputs:     osc, modBuffer, buf, size, gain.
 * Outputs:    buf; phase state advanced.
 * Accessors:  calcNextOscSampleFmBlock().
 * Affiliates: calcStockWaveBlock(), calcFmBlock().
 */
INITCM void calcStockWaveFmBlock(OscInfo* osc, int16_t* modBuffer,
                                 int16_t* buf, uint8_t size, const float gain)
{
	uint32_t inc;
	int32_t step;
	const int16_t* tab;
	uint8_t shift;
	uint16_t mask;
	uint8_t i;

	if (!osc || !buf || !modBuffer)
		return;
	if (!sampleMemory_stockValid()) {
		const int16_t (*table)[1024];

		switch (osc->waveform) {
		case SW3:
		case SWA:
			table = sawTable;
			break;
		case TRA:
			table = triTable;
			break;
		default:
			table = recTable;
			break;
		}
		calcFmBlock(osc, table, modBuffer, buf, size, gain);
		return;
	}
	{
		const stock_level_t lv = stockWave_level(
			(uint8_t)(osc->waveform - SQ3), osc->tableOffset);

		tab = lv.tab;
		shift = lv.shift;
		mask = lv.mask;
	}
	inc = osc->phaseInc;
	step = osc->phaseIncStep;
	for (i = 0u; i < size; i++) {
		const uint32_t fmOffset =
			(uint32_t)((int32_t)((float)modBuffer[i] * osc->fmMod) << 19);
		const uint32_t oscPhase = osc->phase + fmOffset;
		const uint32_t index = oscPhase >> shift;
		const uint32_t next = (index + 1u) & mask;
		const float frac =
			(float)(oscPhase << (32u - shift)) * 2.3283064365386963e-10f;
		int16_t oscOut = tab[index];

		oscOut = (int16_t)((float)oscOut +
			frac * (float)((int32_t)tab[next] - (int32_t)oscOut));
		buf[i] = oscOut * gain;
		/* S084 P1 P1-3.7: per-sample slew, see calcSineBlock(). */
		osc->phase += inc;
		inc += (uint32_t)step;
	}
	osc->phaseInc = inc;
}

//------------------------------------------------------------------
INITCM void calcSampleOscFmBlock(OscInfo* osc,int16_t* modBuffer, int16_t* buf, uint8_t size ,const float gain)
{
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase 	= osc->phase;
		int16_t oscOut ;

		const uint32_t index =  oscPhase + (((uint32_t)(modBuffer[i]*osc->fmMod))<<14);
		uint32_t  itg	= index>>17;

	#if INTERPOLATE_FM_OSC
		oscOut = crashSample[itg++];
		const float frac	= (index & 0x0001FFFF) * 0.00000762939453125f; // 1 / 2^17
		oscOut += frac*(crashSample[itg] - oscOut);
	#else
		oscOut = crashSample[itg];
	#endif

		oscOut = (oscOut - 127)*256;

		/* S084 P1 P1-3.7: per-sample slew, see calcSineBlock(). */
		osc->phase = oscPhase + inc;
		inc += (uint32_t)step;
		osc->output = oscOut;
		buf[i] = oscOut * gain;
	}
	osc->phaseInc = inc;
}
//------------------------------------------------------------------
int16_t calcSampleOscFm(OscInfo* osc, OscInfo* modOsc)
{

	const uint32_t oscPhase 	= osc->phase;
	int16_t oscOut ;

	const uint32_t index =  oscPhase + (((uint32_t)(modOsc->output*osc->fmMod))<<14);
	uint32_t  itg	= index>>17;

#if INTERPOLATE_FM_OSC
	oscOut = crashSample[itg++];
	const float frac	= (index & 0x0001FFFF) * 0.00000762939453125f; // 1 / 2^17
	oscOut += frac*(crashSample[itg] - oscOut);
#else
	oscOut = crashSample[itg];
#endif

	oscOut = (oscOut - 127)*256;

	osc->phase = oscPhase + osc->phaseInc;
	osc->output = oscOut;
	return oscOut;
};
//---------------------------------------------------------------
INITCM void calcSampleOscBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain)
{
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut ;
		uint32_t  itg	= oscPhase>>17;


	#if INTERPOLATE_OSC
		//todo use ldm instead of ldr to laod multiple values from memory
		oscOut = crashSample[itg++];
		const float frac = (oscPhase & 0x0001FFFF) * 0.00000762939453125f; // 1 / 2^17
		oscOut += frac*(crashSample[itg] - oscOut);
	#else
		oscOut = crashSample[itg];
	#endif
		oscOut = (oscOut - 127)*256;

		/* S084 P1 P1-3.7: per-sample slew, see calcSineBlock(). */
		osc->phase = oscPhase + inc;
		inc += (uint32_t)step;
		buf[i] = oscOut * gain;
	}
	osc->phaseInc = inc;
}
//---------------------------------------------------------------
int16_t calcSampleOsc(OscInfo* osc)
{
	const uint32_t oscPhase = osc->phase;
	int16_t oscOut ;

	uint32_t  itg	= oscPhase>>17;

#if INTERPOLATE_OSC
	//todo use ldm instead of ldr to laod multiple values from memory
	oscOut = crashSample[itg++];
	const float frac = (oscPhase & 0x0001FFFF) * 0.00000762939453125f; // 1 / 2^17
	oscOut += frac*(crashSample[itg] - oscOut);
#else
	oscOut = crashSample[itg];
#endif
	oscOut = (oscOut - 127)*256;

	osc->phase = oscPhase + osc->phaseInc;
	osc->output = oscOut;
	return oscOut;

};
 //-----------------------------------------------------------
static void osc_calcSineFreqValue(OscInfo* osc, const float currentFreq)
{
	osc->phaseIncTarget = freq2PhaseIncr(currentFreq);
}
//-----------------------------------------------------------
static void osc_calcNoiseFreqValue(OscInfo* osc, const float currentFreq)
{
	osc_calcSineFreqValue(osc, currentFreq);
}
//-----------------------------------------------------------
static void osc_calcWavetableFreqValue(OscInfo* osc, const float currentFreq)
{
	osc->phaseIncTarget = freq2PhaseIncr1024(currentFreq);

	const uint8_t overtoneIndex = freqToTableIndex(currentFreq);
	osc->tableOffset = overtoneIndex>10?10:overtoneIndex;
}
//-----------------------------------------------------------
static void osc_calcSampleFreqValue(OscInfo* osc, const float currentFreq)
{
	osc->phaseIncTarget = freq2PhaseIncr32767(currentFreq);
}
//-----------------------------------------------------------
uint8_t osc_freqToTableIndex(float f)
{
	/*
	 * Band-limited wavetable octave index for a frequency (S084 P1 P1-3.1).
	 *
	 * What:       exposes the S073 Step 6 edge count so other render code
	 *             (OscillatorPwm.c, and P2's wavetable reader) selects the
	 *             same band-limited level as the `wav` tables.
	 * Why:        avoids a second copy of the octave-edge table.
	 * Inputs:     f in Hz.  Outputs: 0..10.
	 * Accessors:  oscPwm_prepare().
	 * Affiliates: osc_octaveEdgeHz[], freqToTableIndex().
	 */
	return freqToTableIndex(f);
}
//-----------------------------------------------------------
void osc_armSlew(OscInfo* osc)
{
	/*
	 * Arm the `fre` pitch slew for one oscillator (S084 P1 P1-3.3).
	 *
	 * What:       records the current phaseInc as the slew origin and
	 *             OSC_SLEW_SAMPLES as its length; the next osc_setFreq()
	 *             computes the per-sample step toward the new target.
	 * Why:        plan §4.1.6: `fre` never resets phase; every oscillator
	 *             glides from its previous pitch to the new one over about
	 *             3 ms.
	 * Inputs:     osc.  Outputs: osc->slewSamplesLeft.
	 * Accessors:  oscSection_beginTrigger(), oscSection_claimPending().
	 * Affiliates: osc_setFreq(), the renderers' per-sample phaseIncStep.
	 */
	if (!osc)
		return;
	osc->slewSamplesLeft = OSC_SLEW_SAMPLES;
}
//-----------------------------------------------------------
 INITCM void osc_setFreq(OscInfo* osc)
 {
		const float currentFreq = osc->freq*osc->pitchMod*osc->modNodeValue;
		/*
		 * Update an oscillator's frequency state for the next block
		 * (S084 P1 P1-3.8).
		 *
		 * What:       computes the phase-increment target for the
		 *             oscillator's type (wav tables, pwm with
		 *             warp-compensated table level, Q20.12 samples), caches it
		 *             by (frequency, waveform, type), and every block advances
		 *             the `fre` slew: a per-sample step that reaches the
		 *             target after OSC_SLEW_SAMPLES, or an immediate snap when
		 *             no slew is armed.
		 * Why:        plan §4.1.1 types, §4.1.6 `fre` (every oscillator
		 *             glides); the cache must not skip the slew bookkeeping.
		 * Inputs:     osc->freq, pitchMod, modNodeValue, oscType, waveform,
		 *             source, slew state.
		 * Outputs:    osc->phaseIncTarget, phaseInc, phaseIncStep,
		 *             tableOffset, pwm parameters.
		 * Accessors:  each engine's async update, once per block per
		 *             oscillator.
		 * Affiliates: osc_armSlew(), oscPwm_prepare(),
		 *             oscSample_phaseIncForFreq(). ITCM (INITCM) as today.
		 */
		/*
		 * Cache key with the stock-block state (S084 VX2 review fix F10).
		 *
		 * What:       for pwm oscillators bit 7 of the key carries
		 *             sampleMemory_stockValid(), so an install that makes the
		 *             stock block valid (or invalid) misses the cache once and
		 *             oscPwm_prepare() re-derives pq3/pw3's natural split
		 *             (0.78 / 0.42 on sq3/sw3, 0.5 on the fallback tables).
		 * Why:        pwm_familyLevel() switches tables every block, but the
		 *             split was only recomputed on a pitch or control change,
		 *             so a held note after Load:[Samples] warped the new table
		 *             around the old split. Types are < 0x80, so the bit never
		 *             collides; other types keep the plain type as key.
		 */
		const uint8_t cacheType = (uint8_t)(osc->oscType |
			((osc->oscType == OSC_TYPE_PWM && sampleMemory_stockValid())
				? 0x80u : 0u));

		if (osc->freqCacheValid &&
			osc->freqCacheType == cacheType &&
			osc->freqCacheWaveform == osc->waveform &&
			osc->freqCacheValue == currentFreq) {
			/* Cache hit: skip only the target calculation, not the slew. */
		} else {
			switch(osc->oscType)
			{
			case OSC_TYPE_PWM:
				oscPwm_prepare(osc, currentFreq);
				break;

			case OSC_TYPE_SMP:
			case OSC_TYPE_SLP:
				osc->phaseIncTarget =
					oscSample_phaseIncForFreq(currentFreq);
				break;

			case OSC_TYPE_WTB:
				/* S084 VX2 VX2-5.6: 2^32 phase units per cycle, as the
				** table waves; no octave level (W3). */
				osc->phaseIncTarget = freq2PhaseIncr1024(currentFreq);
				break;

			case OSC_TYPE_WAV:
			default:
				switch(osc->waveform)
				{
				case SINE:
					osc_calcSineFreqValue(osc, currentFreq);
					break;

				case SAW:
				case TRI:
				case REC:
				case SQ3:
				case SW3:
				case SQA:
				case TRA:
				case SWA:
					/* S084 VX2 VX2-4.8: the generated waves use the table
					** octave level. */
					osc_calcWavetableFreqValue(osc, currentFreq);
					break;

				case NOISE:
					osc_calcNoiseFreqValue(osc, currentFreq);
					break;

				case CRASH:
					osc_calcSampleFreqValue(osc, currentFreq);
					break;

				default:
					/* Out-of-list `wav` value: the writer clamps it. */
					osc->phaseIncTarget =
						freq2PhaseIncr1024(currentFreq);
					break;
				}
				break;
			}
			osc->freqCacheValue = currentFreq;
			osc->freqCacheWaveform = osc->waveform;
			osc->freqCacheType = cacheType;
			osc->freqCacheValid = 1u;
		}

		/* Slew bookkeeping runs every call, so the cache never hides it. */
		if (osc->slewSamplesLeft == 0u) {
			osc->phaseInc = osc->phaseIncTarget;
			osc->phaseIncStep = 0;
		} else {
			osc->phaseIncStep =
				((int32_t)osc->phaseIncTarget - (int32_t)osc->phaseInc) /
				(int32_t)osc->slewSamplesLeft;
			if (osc->slewSamplesLeft > OUTPUT_DMA_SIZE)
				osc->slewSamplesLeft -= OUTPUT_DMA_SIZE;
			else
				osc->slewSamplesLeft = 0u;
		}
 }
 //-----------------------------------------------------------
void osc_setBaseNote(OscInfo* osc, uint8_t baseNote)
{

	 //get fine tune
	 const float cent = midiPitch_detune(osc->midiFreq&0xff);
	 /*
	  * PatternData owns the default sequencer note value after the 1.3 pattern
	  * move. Oscillator uses PAT_DEFAULT_NOTE only as the neutral pitch reference:
	  * input is the caller-supplied base note plus oscillator coarse/fine tuning,
	  * output is osc->freq and osc->baseNote. Common callers are voice parameter
	  * apply paths; PatternData itself is not involved in DSP state mutation here.
	  */
	 int16_t note =  (osc->midiFreq>>8) + (baseNote-MIDI_DEFAULT_TRIGGER_NOTE);
	 if(note>127)note=127;
	 if(note<0)note=0;

	 osc->freq = MidiNoteFrequencies[note]*cent;
	 osc->baseNote = baseNote;
 };

 //-----------------------------------------------------------
void osc_recalcFreq(OscInfo* osc)
{
	 //get fine tune
	 const float cent = midiPitch_detune(osc->midiFreq&0xff);
	 /*
	  * Recompute frequency from the stored base note using PatternData's neutral
	  * default-note constant. Input/output mirror osc_setBaseNote(), but this path
	  * uses osc->baseNote already stored in the oscillator. This keeps the renamed
	  * PAT_DEFAULT_NOTE dependency explicit after the sequencer storage move.
	  */
	 int16_t note =  (osc->midiFreq>>8) + (osc->baseNote-MIDI_DEFAULT_TRIGGER_NOTE);

	 if(note>127)note=127;
 	 if(note<0)note=0;

	 osc->freq = MidiNoteFrequencies[note]*cent;
 }
 //-----------------------------------------------------------
