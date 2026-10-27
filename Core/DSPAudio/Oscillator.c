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
#include "random.h"
#include "Samples.h"
#include "MidiPitch.h"
#include "modulationNode.h"
#include "OscillatorSample.h"
#include "OscillatorPwm.h"
#include "OscillatorWavetable.h"
#include "StockWaves.h"
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
//-----------------------------------------------------------
/*
 * Waveform interpolation eligibility (S085 C5.5).
 *
 * What:       1 when this block's modulation granted the oscillator a blend
 *             pair (generation match, 0 < frac < 1, next > base, both inside
 *             the wav list) and the oscillator is wav type.
 * Why:        plan §4.3.5: samples left the wav list, and pwm/smp/slp/wtb
 *             controls are continuous by design; only wav blends neighbours.
 *             The budget is granted by OscRenderPool (C9.3).
 * Accessors:  the two block dispatchers and the scalar dispatchers.
 */
static inline uint8_t osc_waveInterpActive(const OscInfo* osc)
{
	const uint8_t maxWave = (uint8_t)(OSC_STOCK_WAVE_COUNT - 1u);

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
	osc->phase = oscA.phase;
	osc->phaseInc = oscA.phaseInc;
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
	osc->phase = oscA.phase;
	osc->phaseInc = oscA.phaseInc;
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
/*
 * Per-sample pitch slew in every block renderer (S085 C5.6).
 *
 * What:       the loop advances phase by a local increment and adds
 *             osc->phaseIncStep to that increment every sample; the final
 *             increment is written back to osc->phaseInc.
 * Why:        run modes fre/itp glide from the old to the new pitch over
 *             OSC_SLEW_SAMPLES without per-block steps (plan §4.3.7). The add
 *             always runs, with step 0 when no slew is active, so the cost
 *             never depends on the run mode (constant CPU). In rst the step is
 *             always 0, so the output is bit-identical to the pre-S085 loop
 *             (proved by osc_s0, C17.5).
 * Inputs:     osc->phaseInc, osc->phaseIncStep (from osc_setFreq()).
 * Outputs:    osc->phase, osc->phaseInc.
 * Accessors:  the dispatchers; ITCM.
 * Affiliates: osc_setFreq(), osc_armSlew(), the type renderers (same rule).
 */
OSC_SYNC_INLINE void calcSineCore(OscInfo* osc, int16_t* buf, const uint8_t size,
                                   const float gain, const OscSyncLane* lane)
{
	uint8_t i;
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
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

		/* S085 C5.6: per-sample slew. */
		osc->phase = oscPhase + inc;
		if (lane)
			osc->phase = oscSync_phase(lane, (uint8_t)i, osc->phase, 0u);
		inc += (uint32_t)step;

		buf[i] = oscOut * gain;
	}
	osc->phaseInc = inc;
}
INITCM void calcSineBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain)
{
	calcSineCore(osc, buf, size, gain, 0);
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
	uint8_t i;
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
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

		/* S085 C5.6: per-sample slew. */
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
	uint8_t i;
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
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

		/* S085 C5.6: per-sample slew. */
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
	 * Render one oscillator block by type (S085 C5.7).
	 *
	 * What:       pwm, smp/slp and wtb go to their own renderers; wav keeps
	 *             the waveform switch (now with the generated stock waves) and
	 *             waveform interpolation; a wav value outside the list
	 *             renders silence.
	 * Why:        plan §5 item 2: each type keeps its own specialised hot
	 *             loop (no per-sample type test); the type test is per block.
	 * Inputs:     osc, (modBuffer), buf, size, gain.
	 * Outputs:    buf; the oscillator's phase state advanced.
	 * Accessors:  OscSection.c (all engine oscillator renders).
	 * Affiliates: oscPwm_render*(), oscSample_render*(),
	 *             oscWavetable_render*(), calcStockWave*Block().
	 */
	switch (osc->oscType) {
	case OSC_TYPE_PWM:
		oscPwm_renderFm(osc, modBuffer, buf, size, gain);
		return;
	case OSC_TYPE_SMP:
	case OSC_TYPE_SLP:
		oscSample_renderFm(osc, modBuffer, buf, size, gain);
		return;
	case OSC_TYPE_WTB:
		oscWavetable_renderFm(osc, modBuffer, buf, size, gain);
		return;
	default:
		break;
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
	/* S085 C5.8: non-wav types render through their block renderer (one
	** sample); the scalar path has no external caller and is kept only for
	** the scalar interpolation helpers. */
	if (osc->oscType != OSC_TYPE_WAV) {
		int16_t mod = modOsc->output;
		int16_t out = 0;

		calcNextOscSampleFmBlock(osc, &mod, &out, 1u, 1.0f);
		osc->output = out;
		return out;
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
		int16_t out = 0;
		int16_t modBuf[1] = { modOsc->output };

		calcStockWaveFmBlock(osc, modBuf, &out, 1u, 1.0f);
		osc->output = out;
		return out;
	}

	default:

		return 0;
		break;

	}
	return 0;
}
//-----------------------------------------------------------
OSC_SYNC_INLINE void calcNoiseCore(OscInfo* osc, int16_t* buf, const uint8_t size,
                                    const float gain, const OscSyncLane* lane)
{
	int i;
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	for(i=0;i<size;i++)
	{
		const uint32_t lastPhase = osc->phase;
		osc->phase += inc;
		inc += (uint32_t)step;

		if(lastPhase > osc->phase)
		{
			//overflow happened -> phaseWrapped
			// osc->output = (int16_t)(GetRngValue()& 0x7FFF); //normal pitched white noise
			osc->output = (int16_t)(GetRngValue()); //normal pitched white noise
		}
		if (lane)
			osc->phase = oscSync_phase(lane, (uint8_t)i, osc->phase, 0u);

		buf[i] = osc->output * gain;
	}
	osc->phaseInc = inc;
}
INITCM void calcNoiseBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain)
{
	calcNoiseCore(osc, buf, size, gain, 0);
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
	switch (osc->oscType) {
	case OSC_TYPE_PWM:
		oscPwm_render(osc, buf, size, gain);
		return;
	case OSC_TYPE_SMP:
	case OSC_TYPE_SLP:
		oscSample_render(osc, buf, size, gain);
		return;
	case OSC_TYPE_WTB:
		oscWavetable_render(osc, buf, size, gain);
		return;
	default:
		break;
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
			calcStockWaveBlock(osc, buf, size, gain);
			break;

		default:
			memset(buf, 0, (size_t)size * sizeof(int16_t));
			break;
		}
}
//-----------------------------------------------------------
int16_t calcNextOscSample(OscInfo* osc)
{
	/* S085 C5.8: non-wav types render through their block renderer (one
	** sample); the scalar path has no external caller and is kept only for
	** the scalar interpolation helpers. */
	if (osc->oscType != OSC_TYPE_WAV) {
		int16_t out = 0;

		calcNextOscSampleBlock(osc, &out, 1u, 1.0f);
		osc->output = out;
		return out;
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
		int16_t out = 0;

		calcStockWaveBlock(osc, &out, 1u, 1.0f);
		osc->output = out;
		return out;
	}

	default:

		return 0;
		break;

	}
	return 0;
};
//---------------------------------------------------------------
OSC_SYNC_INLINE void calcWavetableOscCore(OscInfo* osc, const int16_t table[][1024],
                                           int16_t* buf, const uint8_t size,
                                           const float gain,
                                           const OscSyncLane* lane)
{
	uint8_t i;
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
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

			/* S085 C5.6: per-sample slew. */
			osc->phase = oscPhase + inc;
			if (lane)
				osc->phase = oscSync_phase(lane, i, osc->phase, 0u);
			inc += (uint32_t)step;
			buf[i] = oscOut * gain;
	}
	osc->phaseInc = inc;
}
INITCM void calcWavetableOscBlock(OscInfo* osc, const int16_t table[][1024], int16_t* buf, const uint8_t size ,const float gain)
{
	calcWavetableOscCore(osc, table, buf, size, gain, 0);
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

/*
 * Generated stock-wave renderers (S085 C5.9).
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
OSC_SYNC_INLINE void calcStockWaveCore(OscInfo* osc, int16_t* buf,
                                       const uint8_t size, const float gain,
                                       const OscSyncLane* lane)
{
	const stock_level_t lv = stockWave_level(
		(uint8_t)(osc->waveform - STOCK_WAVE_FIRST), osc->tableOffset);
	const uint8_t lowShift = (uint8_t)(32u - lv.shift);
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const uint32_t oscPhase = osc->phase;
		const uint32_t index = oscPhase >> lv.shift;
		const uint32_t next = (index + 1u) & lv.mask;
		const float frac =
			(float)(oscPhase << lowShift) * 2.3283064365386963e-10f; /* 2^-32 */
		int16_t oscOut = lv.tab[index];

		oscOut = (int16_t)((float)oscOut +
			frac * (float)((int32_t)lv.tab[next] - (int32_t)oscOut));
		buf[i] = oscOut * gain;
		/* S085 C5.6: per-sample slew. */
		osc->phase = oscPhase + inc;
		if (lane)
			osc->phase = oscSync_phase(lane, (uint8_t)i, osc->phase, 0u);
		inc += (uint32_t)step;
	}
	osc->phaseInc = inc;
}
INITCM void calcStockWaveBlock(OscInfo* osc, int16_t* buf, const uint8_t size,
                               const float gain)
{
	calcStockWaveCore(osc, buf, size, gain, 0);
}

INITCM void calcStockWaveFmBlock(OscInfo* osc, int16_t* modBuffer,
                                 int16_t* buf, uint8_t size, const float gain)
{
	const stock_level_t lv = stockWave_level(
		(uint8_t)(osc->waveform - STOCK_WAVE_FIRST), osc->tableOffset);
	const uint8_t lowShift = (uint8_t)(32u - lv.shift);
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const uint32_t index32 = osc->phase +
			(((uint32_t)(modBuffer[i] * osc->fmMod)) << 19);
		const uint32_t index = index32 >> lv.shift;
		const uint32_t next = (index + 1u) & lv.mask;
		const float frac =
			(float)(index32 << lowShift) * 2.3283064365386963e-10f;
		int16_t oscOut = lv.tab[index];

		oscOut = (int16_t)((float)oscOut +
			frac * (float)((int32_t)lv.tab[next] - (int32_t)oscOut));
		osc->output = oscOut;
		buf[i] = oscOut * gain;
		/* S085 C5.6: per-sample slew. */
		osc->phase += inc;
		inc += (uint32_t)step;
	}
	osc->phaseInc = inc;
}

//------------------------------------------------------------------
INITCM void calcSampleOscFmBlock(OscInfo* osc,int16_t* modBuffer, int16_t* buf, uint8_t size ,const float gain)
{
	uint8_t i;
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
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

		/* S085 C5.6: per-sample slew. */
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
OSC_SYNC_INLINE void calcSampleOscCore(OscInfo* osc, int16_t* buf, const uint8_t size,
                                        const float gain,
                                        const OscSyncLane* lane)
{
	uint8_t i;
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
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

		/* S085 C5.6: per-sample slew. */
		osc->phase = oscPhase + inc;
		if (lane)
			osc->phase = oscSync_phase(lane, (uint8_t)i, osc->phase, 0u);
		inc += (uint32_t)step;
		buf[i] = oscOut * gain;
	}
	osc->phaseInc = inc;
}
INITCM void calcSampleOscBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain)
{
	calcSampleOscCore(osc, buf, size, gain, 0);
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
/*
 * Update an oscillator's frequency state for the next block (S085 C5.12).
 *
 * What:       computes the phase-increment target for the oscillator's type
 *             (wav tables/sine/noise/crash, pwm via oscPwm_prepare(), Q20.12
 *             samples, wtb at 2^32 per cycle), caches it by (frequency,
 *             waveform, source, type key) — source is the pwm width, which
 *             velocity and the LFO write directly — then advances the slew: with no slew armed
 *             phaseInc = target and step = 0; with a slew armed the per-sample
 *             step reaches the target after the remaining slew samples.
 *             The pwm cache key carries sampleMemory_stockValid() in bit 7,
 *             so an install that makes the stock block valid re-derives the
 *             pq3/pw3 natural split once (S084 P2 F10).
 * Why:        plan §5 items 1-3. The cache must never hide the slew
 *             bookkeeping, so the slew part runs every call. The step
 *             division always runs (divisor clamped to 1), so the block cost
 *             does not depend on the run mode.
 * Inputs:     freq, pitchMod, modNodeValue, oscType, waveform, source, slew.
 * Outputs:    phaseIncTarget, phaseInc, phaseIncStep, slewSamplesLeft,
 *             tableOffset, pwm state.
 * Accessors:  each engine's async update, once per block per oscillator.
 * Affiliates: osc_armSlew(), oscPwm_prepare(), oscSample_phaseIncForFreq().
 *             ITCM.
 */
INITCM void osc_setFreq(OscInfo* osc)
{
	const float currentFreq = osc->freq * osc->pitchMod * osc->modNodeValue;
	const uint8_t cacheType = (uint8_t)(osc->oscType |
		((osc->oscType == OSC_TYPE_PWM && sampleMemory_stockValid())
			? 0x80u : 0u));

	if (!(osc->freqCacheValid &&
	      osc->freqCacheType == cacheType &&
	      osc->freqCacheWaveform == osc->waveform &&
	      osc->freqCacheSource == osc->source &&
	      osc->freqCacheValue == currentFreq)) {
		switch (osc->oscType) {
		case OSC_TYPE_PWM:
			oscPwm_prepare(osc, currentFreq);
			break;
		case OSC_TYPE_SMP:
		case OSC_TYPE_SLP:
			osc->phaseIncTarget = oscSample_phaseIncForFreq(currentFreq);
			break;
		case OSC_TYPE_WTB:
			osc->phaseIncTarget = freq2PhaseIncr1024(currentFreq);
			break;
		case OSC_TYPE_WAV:
		default:
			switch (osc->waveform) {
			case SINE:
				osc_calcSineFreqValue(osc, currentFreq);
				break;
			case NOISE:
				osc_calcNoiseFreqValue(osc, currentFreq);
				break;
			case CRASH:
				osc_calcSampleFreqValue(osc, currentFreq);
				break;
			default: /* TRI SAW REC and SQ3..SWA: table octave */
				osc_calcWavetableFreqValue(osc, currentFreq);
				break;
			}
			break;
		}
		osc->freqCacheValue = currentFreq;
		osc->freqCacheWaveform = osc->waveform;
		osc->freqCacheSource = osc->source;
		osc->freqCacheType = cacheType;
		osc->freqCacheValid = 1u;
	}
	{
		/*
		 * Slew step for this block (S085 C5.12, assessment A1 fix).
		 *
		 * What:       step = (target - phaseInc) / max(left, block). Every
		 *             renderer adds the step on all OUTPUT_DMA_SIZE samples of
		 *             the block, so the divisor must never be smaller than the
		 *             block: with OSC_SLEW_SAMPLES 132 the last slew block
		 *             starts with left = 4, and dividing by 4 drove the
		 *             increment 7 steps past the target (a 20.5 % pitch
		 *             overshoot for 28 samples, then a snap back). With the
		 *             floor the last block lands on the target at its final
		 *             sample; the glide takes 160 samples (5 blocks) and is
		 *             monotonic. The difference is taken in uint32 and cast
		 *             once, so increments above 2^31 cannot overflow int32.
		 * Cost:       unchanged: one compare-select and one SDIV every call.
		 * Proof:      osc_run test_slew renders the trajectory (A5).
		 */
		const uint16_t left = osc->slewSamplesLeft;
		const uint16_t divisor = (left > OUTPUT_DMA_SIZE)
			? left : (uint16_t)OUTPUT_DMA_SIZE;
		const int32_t step = (int32_t)(osc->phaseIncTarget - osc->phaseInc) /
		                     (int32_t)divisor;

		if (left == 0u) {
			osc->phaseInc = osc->phaseIncTarget;
			osc->phaseIncStep = 0;
		} else {
			osc->phaseIncStep = step;
			osc->slewSamplesLeft = (left > OUTPUT_DMA_SIZE)
				? (uint16_t)(left - OUTPUT_DMA_SIZE) : 0u;
		}
	}
}

/*
 * Band-limited octave index for a frequency (S085 C5.12).
 *
 * What:       the S073 Step 6 edge count (0..10), exposed.
 * Why:        the pwm families select the same band-limited level as the wav
 *             tables, at frequency x warp compression.
 * Accessors:  oscPwm_prepare().  Affiliates: freqToTableIndex().
 */
uint8_t osc_freqToTableIndex(float f)
{
	return freqToTableIndex(f);
}

/*
 * Arm the pitch slew (S085 C5.12).
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
void osc_armSlew(OscInfo* osc)
{
	if (osc)
		osc->slewSamplesLeft = OSC_SLEW_SAMPLES;
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
//-----------------------------------------------------------
/*
 * Waveform-interpolation pair under hard sync (S085 assessment A2).
 *
 * What:       calcPeriodicInterpBlock() with both neighbour renders synced
 *             by the same lane, so the blend partners reset together.
 * Why:        a wav slave that holds a pool unit for interpolation must keep
 *             its blend in snc; the unit already budgets the second render.
 * Accessors:  osc_renderSyncBlock().
 */
static void calcPeriodicInterpSyncBlock(OscInfo* osc, int16_t* buf,
                                        const uint8_t size, const float gain,
                                        const OscSyncLane* lane)
{
	const float frac = osc->waveInterpFrac;
	OscInfo oscA = *osc;
	OscInfo oscB = *osc;
	uint8_t i;

	oscA.waveform = osc->waveform;
	osc_clearWaveInterp(&oscA);
	oscB.waveform = osc->waveInterpNext;
	osc_clearWaveInterp(&oscB);

	osc_renderSyncBlock(&oscA, osc_interp_a, size, 1.f, lane);
	osc_renderSyncBlock(&oscB, osc_interp_b, size, 1.f, lane);

	for (i = 0; i < size; i++) {
		const int16_t a = osc_interp_a[i];
		const int16_t b = osc_interp_b[i];
		const int16_t out = (int16_t)(a + frac * (b - a));
		buf[i] = out * gain;
	}
	osc->phase = oscA.phase;
	osc->phaseInc = oscA.phaseInc;
}
//-----------------------------------------------------------
/*
 * Hard-sync slave dispatch (S085 assessment A2; declared in Oscillator.h).
 *
 * What:       the calcNextOscSampleBlock() type and waveform dispatch, once
 *             per block, into the sync form of each family's loop.
 * Cost:       per sample, the family's plain loop plus the oscSync_phase()
 *             select (about 6 instructions); measured by armcheck-osc
 *             (C19.2).
 */
INITCM void osc_renderSyncBlock(OscInfo* osc, int16_t* buf, uint8_t size,
                                float gain, const OscSyncLane* lane)
{
	switch (osc->oscType) {
	case OSC_TYPE_PWM:
		oscPwm_renderSync(osc, buf, size, gain, lane);
		return;
	case OSC_TYPE_SMP:
	case OSC_TYPE_SLP:
		oscSample_renderSync(osc, buf, size, gain, lane);
		return;
	case OSC_TYPE_WTB:
		oscWavetable_renderSync(osc, buf, size, gain, lane);
		return;
	default:
		break;
	}
	if (osc_waveInterpActive(osc)) {
		calcPeriodicInterpSyncBlock(osc, buf, size, gain, lane);
		return;
	}
	switch (osc->waveform) {
	case SINE:
		calcSineCore(osc, buf, size, gain, lane);
		break;
	case SAW:
	case TRI:
	case REC:
		/* One inlined copy for the three tables (ITCM). */
		calcWavetableOscCore(osc, (osc->waveform == SAW) ? sawTable
		                     : (osc->waveform == TRI) ? triTable : recTable,
		                     buf, size, gain, lane);
		break;
	case NOISE:
		calcNoiseCore(osc, buf, size, gain, lane);
		break;
	case CRASH:
		calcSampleOscCore(osc, buf, size, gain, lane);
		break;
	case SQ3:
	case SW3:
	case SQA:
	case TRA:
	case SWA:
		calcStockWaveCore(osc, buf, size, gain, lane);
		break;
	default:
		memset(buf, 0, (size_t)size * sizeof(int16_t));
		break;
	}
}
