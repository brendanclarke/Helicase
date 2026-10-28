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
#include <math.h>
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
static inline void osc_updateSampleCache(OscInfo* osc, uint8_t sampleIndex)
{
	/* User sample metadata lives in flash-backed SampleMemory tables. The
	** generation counter lets oscillators keep cached pointers/sizes across
	** audio blocks and invalidate them after a sample/loop install refreshes
	** the manifest. */
	const uint32_t generation = sampleMemory_getGeneration();

	if (osc->sampleCacheData &&
		osc->sampleCacheIndex == sampleIndex &&
		osc->sampleCacheGeneration == generation) {
		return;
	}

	SampleInfo info = sampleMemory_getSampleInfo(sampleIndex);
	osc->sampleCacheIndex = sampleIndex;
	osc->sampleCacheGeneration = generation;
	osc->sampleCacheSize = info.size;
	osc->sampleCacheData = (const int16_t*)((const int8_t*)info.offset);
	osc->sampleCacheLooped = sampleMemory_isLooped(sampleIndex);
}
//-----------------------------------------------------------
static inline uint8_t osc_waveInterpActive(const OscInfo* osc)
{
	const uint8_t maxWave = (uint8_t)(OSC_SAMPLE_START + sampleMemory_getNumSamples() - 1u);

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
	osc->phase = startPhase + (osc->phaseInc * (uint32_t)size);
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
	osc->phase = startPhase + (osc->phaseInc * (uint32_t)size);
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

		osc->phase = oscPhase + osc->phaseInc;

		buf[i] = oscOut * gain;
	}
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

		osc->phase = oscPhase + osc->phaseInc;
		buf[i] =  oscOut * gain;
	}
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

		osc->phase = oscPhase + osc->phaseInc;
		osc->output = oscOut;
		buf[i] = oscOut * gain;
	}
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
/*
 * Stock table waves by waveform (S086 P2).
 *
 * What:       the [11][1024] octave table of waveform OSC_STOCK_FIRST + i.
 * Why:        each dispatcher has one case group for the stock waves and
 *             passes osc_stockTables[waveform - OSC_STOCK_FIRST] to the
 *             Tri/Saw/Rec loop. A variable pointer adds one call site per
 *             dispatcher instead of one constant-argument site per wave
 *             (085 log §13; DSP ref §10: constant sites make LTO re-specialise).
 * Accessors:  calcNextOscSampleFmBlock(), calcNextOscSampleBlock(),
 *             osc_renderPlainWave(), osc_renderSyncWave(),
 *             calcNextOscSampleRingBlock().
 * Affiliates: wavetableStock.c (generated), Oscillator.h SQ3/SW3.
 */
static const int16_t (* const osc_stockTables[])[1024] = {
	sq3Table,	/* SQ3 */
	sw3Table,	/* SW3 */
};
_Static_assert(sizeof(osc_stockTables) / sizeof(osc_stockTables[0]) ==
               OSC_STOCK_LAST - OSC_STOCK_FIRST + 1,
               "osc_stockTables must list OSC_STOCK_FIRST..OSC_STOCK_LAST");
//-----------------------------------------------------------
INITCM void calcNextOscSampleFmBlock(OscInfo* osc, int16_t* modBuffer, int16_t* buf, uint8_t size ,const float gain)
{
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

		case SQ3: case SW3:
			calcFmBlock(osc,osc_stockTables[osc->waveform - OSC_STOCK_FIRST],modBuffer,buf,size,gain);
			break;

		case NOISE:
			//TODO FM noise - needed since its tuned digi noise!
			calcNoiseBlock(osc,buf,size,gain);
			break;

		case CRASH:
			calcSampleOscFmBlock(osc,modBuffer,buf,size,gain);
			break;

		default: {
			/* S087 D: unreachable - only osc1 reaches this dispatcher and
			 * osc_setFreq() clamps its wav value (see D-7). */
			uint8_t i;
			for (i = 0u; i < size; i++)
				buf[i] = 0;
			break; }

		}
}
//-----------------------------------------------------------
int16_t calcNextOscSampleFm(OscInfo* osc,OscInfo* modOsc)
{
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

	default:

		return 0;
		break;

	}
	return 0;
}
//-----------------------------------------------------------
INITCM void calcNoiseBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain)
{
	int i;
	for(i=0;i<size;i++)
	{
		const uint32_t lastPhase = osc->phase;
		osc->phase += osc->phaseInc;

		if(lastPhase > osc->phase)
		{
			//overflow happened -> phaseWrapped
			// osc->output = (int16_t)(GetRngValue()& 0x7FFF); //normal pitched white noise
			osc->output = (int16_t)(GetRngValue()); //normal pitched white noise
		}

		buf[i] = osc->output * gain;
	}
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

		case SQ3: case SW3:
			calcWavetableOscBlock(osc,osc_stockTables[osc->waveform - OSC_STOCK_FIRST],buf,size, gain);
			break;

		case NOISE:
			calcNoiseBlock(osc,buf,size, gain);
			break;

		case CRASH:
			calcSampleOscBlock(osc,buf,size, gain);
			break;

		default:
			/* S087 D: modulator list entries (osc_setFreq() clamps osc1). */
			osc_renderUserList(osc, buf, size, gain);
			break;
		}
}
//-----------------------------------------------------------
int16_t calcNextOscSample(OscInfo* osc)
{
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

	default:

		return 0;
		break;

	}
	return 0;
};
//---------------------------------------------------------------
INITCM void calcWavetableOscBlock(OscInfo* osc, const int16_t table[][1024], int16_t* buf, const uint8_t size ,const float gain)
{
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

			osc->phase = oscPhase + osc->phaseInc;
			buf[i] = oscOut * gain;
	}
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
INITCM void calcSampleOscFmBlock(OscInfo* osc,int16_t* modBuffer, int16_t* buf, uint8_t size ,const float gain)
{
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

		osc->phase = oscPhase + osc->phaseInc;
		osc->output = oscOut;
		buf[i] = oscOut * gain;
	}
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
//------------------------------------------------------------------
INITCM void calcUserSampleOscFmBlock(OscInfo* osc,int16_t* modBuffer, int16_t* buf, uint8_t size ,const float gain)
{
	uint8_t sampleIndex = osc->waveform - OSC_SAMPLE_START;
	osc_updateSampleCache(osc, sampleIndex);
	uint32_t sampleSize = osc->sampleCacheSize;
	uint8_t looped = osc->sampleCacheLooped;

	//cast sample data to signed int16_t array
	const int16_t* sampleData = osc->sampleCacheData;
	if(sampleSize == 0) return;

	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase 	= osc->phase;
		int16_t oscOut ;

		const uint32_t index =  oscPhase + (((uint32_t)(modBuffer[i]*osc->fmMod))<<14);
		uint32_t  itg	= index>>17;

		if(looped && itg >= sampleSize)
			itg %= sampleSize;

	#if INTERPOLATE_FM_OSC
		oscOut = sampleData[itg];
		const float frac	= (index & 0x0001FFFF) * 0.00000762939453125f; // 1 / 2^17
		if(looped && (itg + 1u) >= sampleSize)
			oscOut += frac*(sampleData[0] - oscOut);
		else
			oscOut += frac*(sampleData[itg + 1u] - oscOut);
		itg++;
	#else
		oscOut = sampleData[itg];
	#endif

		//one shot
		if(looped)
		{
			uint32_t nextPhase = oscPhase + osc->phaseInc;
			if(sampleSize < 32768u) {
				uint32_t loopPhase = sampleSize << 17;
				if(nextPhase >= loopPhase)
					nextPhase %= loopPhase;
			}
			osc->phase = nextPhase;
		}
		else if(itg < sampleSize)
		{
			osc->phase = oscPhase + osc->phaseInc;
		}
		osc->output = oscOut;
		buf[i] = oscOut * gain;
	}
}
//---------------------------------------------------------------
INITCM void calcUserSampleOscBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain)
{
	uint8_t sampleIndex = osc->waveform - OSC_SAMPLE_START;
	osc_updateSampleCache(osc, sampleIndex);
	uint32_t sampleSize = osc->sampleCacheSize;
	uint8_t looped = osc->sampleCacheLooped;

	//cast sample data to signed int16_t array
	const int16_t* sampleData = osc->sampleCacheData;
	if(sampleSize == 0) return;

	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut ;
		uint32_t  itg	= oscPhase>>17;

		if(looped && itg >= sampleSize)
			itg %= sampleSize;

	#if INTERPOLATE_OSC

		oscOut = sampleData[itg];
		const float frac = (oscPhase & 0x0001FFFF) * 0.00000762939453125f; // 1 / 2^17
		if(looped && (itg + 1u) >= sampleSize)
			oscOut += frac*(sampleData[0] - oscOut);
		else
			oscOut += frac*(sampleData[itg + 1u] - oscOut);
		itg++;
	#else
		oscOut = sampleData[itg];
	#endif
	//	oscOut = (oscOut - 127)*256;

		//one shot
		if(looped)
		{
			uint32_t nextPhase = oscPhase + osc->phaseInc;
			if(sampleSize < 32768u) {
				uint32_t loopPhase = sampleSize << 17;
				if(nextPhase >= loopPhase)
					nextPhase %= loopPhase;
			}
			osc->phase = nextPhase;
		}
		else if(itg < sampleSize)
		{
			osc->phase = oscPhase + osc->phaseInc;
		}
		buf[i] = oscOut * gain;
	}
}
//---------------------------------------------------------------
INITCM void calcSampleOscBlock(OscInfo* osc, int16_t* buf, const uint8_t size ,const float gain)
{
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

		osc->phase = oscPhase + osc->phaseInc;
		buf[i] = oscOut * gain;
	}
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
/* =========================================================================
** S085 P1: Drum osc2 mod modes snc (hard sync) and rng (ring)
** =========================================================================
**
** What:       the sync clock and reset-list builder; sync and ring copies of
**             the plain block loops above; their two dispatchers. Each copy
**             repeats its plain loop's read and interpolation exactly. Only
**             the phase update (sync) or the final store (ring) differs.
** Why:        the existing loops must stay unchanged instruction for
**             instruction (fm and mix may not change cost or sound). So the
**             new behaviour lives in separate functions, not in extra
**             parameters on the shared loops: the S085 rollback's shared
**             plain/sync loops added cost to every plain render.
**             OSC_S085_LOOP keeps each copy out of line and unspecialised.
**             Each one then appears under its own name for fpseq.py and ITCM
**             accounting, and LTO cannot fold it into an existing loop.
** Accessors:  calcNextOscSampleSyncBlock(), calcNextOscSampleRingBlock(),
**             osc_buildSyncList(), osc_syncClockInc() (all called from
**             Drum_calcVoiceSyncBlock()).
** Affiliates: Oscillator.h S085 P1 block; DrumVoice.c; tools/dsp_test
**             (test_osc_mod.c, armcheck-oscmod, elf_osc_check).
*/
#if (INTERPOLATE_OSC != 1)
#error "S085 P1 sync/ring copies implement the interpolated read only (INTERPOLATE_OSC 1)"
#endif

/*
 * S085 P1 ITCM placement.
 *
 * What:       .itcm.s085 for every new oscillator function.
 * Why:        the linker script places *(.itcm) first and *(.itcm.*) after
 *             it, so the new code follows all existing ITCM code. The
 *             existing loops then keep their addresses and their short
 *             (16-bit) tail branches; appending this code inside .itcm
 *             pushed calcUserSampleOscBlock() out of b.n range and changed
 *             that loop's compiled text (which the G-A gate forbids).
 * Accessors:  the S085 P1 functions below.
 */
#define OSC_S085_SECTION __attribute__((section(".itcm.s085")))
#define OSC_S085_LOOP OSC_S085_SECTION __attribute__((noinline, noclone))
/*
 * The two public S085 P1 dispatchers stay out of line in ITCM (S085 P1).
 *
 * What:       INITCM plus noinline/noclone.
 * Why:        they must not be inlined into Drum_calcVoiceSyncBlock() (flash):
 *             their interpolated variants' blend loops are per-sample code
 *             and the cost gate assumes the same ITCM placement that
 *             calcPeriodicInterpBlock() gets through calcNextOscSampleBlock().
 * Accessors:  Drum_calcVoiceSyncBlock().
 */
#define OSC_S085_DISPATCH OSC_S085_SECTION __attribute__((noinline, noclone))

/*
 * Apply one sync reset to an advanced slave phase (S085 P1).
 *
 * What:       target = over x ratioQ16 >> 16 is where the slave would be
 *             had it restarted at the clock's wrap instant (the overshoot
 *             correction, plan Q4). The result moves phase toward target by
 *             depthQ30 (0 = no move, 1 << 30 = exactly target), along the
 *             signed (short) way round the 32-bit phase circle.
 *             over == OSC_SYNC_NONE returns phase unchanged.
 * Why:        plan Q10 (amt = sync depth), K6 (the arithmetic runs on every
 *             sample; only the select depends on the list).
 * Inputs:     phase, already advanced by phaseInc (so the reset lands on the
 *             next sample, K4); over (the list entry); ratioQ16; depthQ30.
 * Outputs:    the next phase.
 * Range:      over < the clock increment, so target is below the slave
 *             increment (a clamped ratio only lowers it). |diff x depthQ30|
 *             < 2^61.
 * Accessors:  calcSineSyncBlock(), calcWavetableOscSyncBlock(),
 *             calcSampleOscSyncBlock(); tools/dsp_test osc_mod (S2, S3).
 */
static inline __attribute__((always_inline)) uint32_t osc_syncPull(
	const uint32_t phase, const uint32_t over, const uint32_t ratioQ16,
	const int32_t depthQ30)
{
	const uint32_t target = (uint32_t)(((uint64_t)over * ratioQ16) >> 16);
	const int32_t diff = (int32_t)(target - phase);
	const uint32_t pulled = phase +
		(uint32_t)(int32_t)(((int64_t)diff * depthQ30) >> 30);

	return (over != OSC_SYNC_NONE) ? pulled : phase;
}

/*
 * The ring law for one sample (S085 P1; per-sample depth, S085 P1 follow-up).
 *
 * What:       G = (32768 - d) + ((d x mod) >> 15); out = SSAT16((car x G) >>
 *             15). With mod = the modulator at gain 1 and d = the ring depth
 *             in Q15 (0..32768), this is car x (1 - d) + car x modulator x d
 *             (plan K5). d = 0 gives G = 32768, so the output is the carrier
 *             exactly, whatever mod is. The saturation catches the single
 *             full-scale case (car -32768, G -32768 at d = 1, mod = -32768).
 * Why:        d is ramped per sample by the caller (the ring copies below), so
 *             the ring amount no longer steps once per block. The per-block
 *             step was audible as a 1.4 kHz zipper ("crunchy") once rng
 *             followed the mod envelope.
 * Inputs:     car (the carrier sample), mod (the modulator sample, gain 1),
 *             depthQ15 (this sample's d).
 * Outputs:    the stored sample.
 * Range:      |d x mod| <= 2^30; G lies in [-32768, 32768]; car x G fits
 *             int32.
 * Accessors:  the ring copies below; tools/dsp_test osc_mod (R1, R2).
 */
static inline __attribute__((always_inline)) int16_t osc_ringSample(
	const int16_t car, const int16_t mod, const int32_t depthQ15)
{
	const int32_t gain = (32768 - depthQ15) + ((depthQ15 * (int32_t)mod) >> 15);

	return (int16_t)__SSAT(((int32_t)car * gain) >> 15, 16);
}

/*
 * Slave-to-clock increment ratio in Q16.16, once per block (S085 P1).
 *
 * What:       slaveInc / clockInc x 65536, clamped below 2^32. clockInc 0
 *             gives 0 (the builder then produces no resets anyway).
 * Why:        it turns a clock overshoot into slave phase units (plan
 *             §4.3.2). Float: one VDIV per block (about 0.5 cycles per
 *             sample) instead of a 64-bit integer divide.
 * Inputs:     slaveInc, clockInc.  Outputs: ratioQ16.
 * Accessors:  calcNextOscSampleSyncBlock(); tools/dsp_test osc_mod (M1).
 */
static uint32_t osc_syncRatioQ16(const uint32_t slaveInc, const uint32_t clockInc)
{
	float ratio;

	if (clockInc == 0u)
		return 0u;
	ratio = ((float)slaveInc / (float)clockInc) * 65536.0f;
	if (ratio > 4294967040.0f)
		ratio = 4294967040.0f;
	return (uint32_t)ratio;
}

/*
 * Whether a waveform has a sync copy (S085 P1).
 *
 * What:       1 for SINE, TRI, SAW, REC, CRASH and the stock table waves
 *             (S086 P2); 0 for NOISE and user samples, which render unsynced
 *             in P1 (plan A3/A4).
 * Accessors:  calcPeriodicInterpSyncBlock() (the phase rule).
 * Affiliates: osc_renderSyncWave() (the same partition, as a switch).
 */
static inline uint8_t osc_syncWaveform(const uint8_t waveform)
{
	return (uint8_t)(((waveform <= CRASH) && (waveform != NOISE)) ||
	                 ((waveform >= OSC_STOCK_FIRST) && (waveform <= OSC_STOCK_LAST)));
}

/*
 * Render one waveform at gain 1, without the interpolation check (S085 P1).
 *
 * What:       the waveform switch of calcNextOscSampleBlock() with a constant
 *             gain of 1, calling the leaf loops directly (and repeating its
 *             user-sample range check).
 * Why:        the unsynced fallbacks (noise and user samples) and the ring
 *             interpolation partners must not add constant-gain call sites to
 *             calcNextOscSampleBlock(): LTO then re-specialises that
 *             function's existing constprop clones, which the G-A gate
 *             forbids (implementation §7 I1). The leaves already receive a
 *             constant 1.0f from the baseline interpolation clone, so their
 *             specialisations are unchanged.
 * Inputs:     osc (interpolation already cleared by the caller where
 *             relevant), buf[size], size.
 * Outputs:    buf; osc state, exactly as calcNextOscSampleBlock().
 * Accessors:  osc_renderSyncWave(); calcPeriodicInterpRingBlock().
 * Affiliates: calcNextOscSampleBlock() (the reference dispatch).
 */
static OSC_S085_SECTION void osc_renderPlainWave(OscInfo* osc, int16_t* buf,
                                                 const uint8_t size)
{
	switch(osc->waveform)
	{
	case SINE:
		calcSineBlock(osc, buf, size, 1.f);
		break;
	case SAW:
		calcWavetableOscBlock(osc, sawTable, buf, size, 1.f);
		break;
	case TRI:
		calcWavetableOscBlock(osc, triTable, buf, size, 1.f);
		break;
	case REC:
		calcWavetableOscBlock(osc, recTable, buf, size, 1.f);
		break;
	case SQ3: case SW3:
		calcWavetableOscBlock(osc, osc_stockTables[osc->waveform - OSC_STOCK_FIRST], buf, size, 1.f);
		break;
	case NOISE:
		calcNoiseBlock(osc, buf, size, 1.f);
		break;
	case CRASH:
		calcSampleOscBlock(osc, buf, size, 1.f);
		break;
	default: {
		/* S087 D: unreachable (see D-7). */
		uint8_t i;
		for (i = 0u; i < size; i++)
			buf[i] = 0;
		break; }
	}
}

/*
 * Sync clock increment for the FM-page oscillator (S085 P1).
 * Contract: Oscillator.h. Crash and user samples run in <<17 point units
 * (osc_calcSampleFreqValue: f/fs x 2^27), which is 1/32 of the 2^32-per-
 * period clock, hence << 5 after the clamp. The stock table waves sit
 * between CRASH and OSC_SAMPLE_START and are 2^32-per-period clocks like
 * Tri/Saw/Rec (S086 P2), so the test excludes them.
 */
uint32_t osc_syncClockInc(const OscInfo* master)
{
	uint32_t inc = master->phaseInc;

	if (master->oscType == OSC_TYPE_MODLIST &&
	    (master->sampleCacheLooped == OSC_BIND_SMP ||
	     master->sampleCacheLooped == OSC_BIND_SLP)) {
		/* S087 D: a Q19.12 sample master is scaled <<10 to clock units. */
		if (inc > 0x003FFFFFu)
			inc = 0x003FFFFFu;
		inc <<= 10;
	} else if (master->waveform == CRASH) {
		/* The crash sample keeps its v1 <<17 phase units. */
		if (inc > 0x07FFFFFFu)
			inc = 0x07FFFFFFu;
		inc <<= 5;
	}
	return inc;
}

/*
 * Build one block's sync reset list (S085 P1).
 * Contract: Oscillator.h. `next < clock` is the carry of the 32-bit add,
 * and next is then the overshoot past the wrap (always < clockInc).
 */
OSC_S085_LOOP uint32_t osc_buildSyncList(uint32_t clock, const uint32_t clockInc,
                                         uint32_t* over, const uint8_t size)
{
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const uint32_t next = clock + clockInc;

		over[i] = (next < clock) ? next : OSC_SYNC_NONE;
		clock = next;
	}
	return clock;
}

/*
 * Sine slave under sync (S085 P1).
 *
 * What:       calcSineBlock()'s read and interpolation (4,096-point table,
 *             <<20 phase); the store is oscOut (gain 1, D3); the phase
 *             update goes through osc_syncPull().
 * Inputs:     osc, over[size], ratioQ16, depthQ30, buf[size], size.
 * Outputs:    buf; osc->phase.
 * Accessors:  osc_renderSyncWave().
 * Affiliates: calcSineBlock() (the reference; must stay identical in its
 *             read), tools/dsp_test osc_mod S1/S2.
 */
static OSC_S085_LOOP void calcSineSyncBlock(OscInfo* osc, const uint32_t* over,
                                            const uint32_t ratioQ16,
                                            const int32_t depthQ30,
                                            int16_t* buf, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut;

		const uint32_t index =  oscPhase;
		uint32_t  itg	= index>>20;

		oscOut = sine_table[itg++];
		const float frac = (index & 0xfffff) * 0.00000095367431640625f;
		oscOut += frac*(sine_table[itg] - oscOut);

		osc->phase = osc_syncPull(oscPhase + osc->phaseInc, over[i],
		                          ratioQ16, depthQ30);

		buf[i] = oscOut;
	}
}

/*
 * Tri/Saw/Rec slave under sync (S085 P1).
 *
 * What:       calcWavetableOscBlock()'s read and interpolation (1,024-point
 *             band-limited table at osc->tableOffset, <<22 phase); the
 *             store is oscOut (gain 1, D3); the phase update goes through
 *             osc_syncPull().
 * Inputs:     osc, table (sawTable / triTable / recTable), over[size],
 *             ratioQ16, depthQ30, buf[size], size.
 * Outputs:    buf; osc->phase.
 * Accessors:  osc_renderSyncWave().
 * Affiliates: calcWavetableOscBlock(); tools/dsp_test osc_mod S1/S2.
 */
static OSC_S085_LOOP void calcWavetableOscSyncBlock(OscInfo* osc,
                                                    const int16_t table[][1024],
                                                    const uint32_t* over,
                                                    const uint32_t ratioQ16,
                                                    const int32_t depthQ30,
                                                    int16_t* buf,
                                                    const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut ;
		uint32_t  itg	= oscPhase>>22;
		const uint32_t next = (itg + 1u) & 0x000003FFu;
		oscOut = table[osc->tableOffset][itg];
		const float frac = (oscPhase & 0x003FFFFF) * 2.384185791015625e-07f; // 1 / 2^22
		oscOut += frac*(table[osc->tableOffset][next] - oscOut);

		osc->phase = osc_syncPull(oscPhase + osc->phaseInc, over[i],
		                          ratioQ16, depthQ30);
		buf[i] = oscOut;
	}
}

/*
 * Crash slave under sync (S085 P1).
 *
 * What:       calcSampleOscBlock()'s read (32,768-point 8-bit crashSample,
 *             <<17 phase, (x - 127) x 256); the store is oscOut (gain 1,
 *             D3); the phase update goes through osc_syncPull(). A hard
 *             reset restarts the cymbal read near its start.
 * Note:       it reads crashSample[itg + 1] at itg = 32767, exactly as the
 *             plain loop does (unchanged behaviour, kept for identity).
 * Inputs:     osc, over[size], ratioQ16, depthQ30, buf[size], size.
 * Outputs:    buf; osc->phase.
 * Accessors:  osc_renderSyncWave().
 * Affiliates: calcSampleOscBlock(); plan A2/Q12 (crash slave syncs).
 */
static OSC_S085_LOOP void calcSampleOscSyncBlock(OscInfo* osc, const uint32_t* over,
                                                 const uint32_t ratioQ16,
                                                 const int32_t depthQ30,
                                                 int16_t* buf, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut ;
		uint32_t  itg	= oscPhase>>17;

		oscOut = crashSample[itg++];
		const float frac = (oscPhase & 0x0001FFFF) * 0.00000762939453125f; // 1 / 2^17
		oscOut += frac*(crashSample[itg] - oscOut);
		oscOut = (oscOut - 127)*256;

		osc->phase = osc_syncPull(oscPhase + osc->phaseInc, over[i],
		                          ratioQ16, depthQ30);
		buf[i] = oscOut;
	}
}

/*
 * One waveform's sync render, without the interpolation check (S085 P1).
 *
 * What:       dispatches Sin/Tri/Saw/Rec/Crash to their sync copies. NOISE
 *             and user samples render unsynced through osc_renderPlainWave()
 *             (the leaf loops at gain 1, plan A3/A4). No interpolation check
 *             is needed here: the caller already took the interpolation
 *             path, or the partner copy has interpolation cleared.
 * Inputs:     osc, over[size], ratioQ16, depthQ30, buf[size], size.
 * Outputs:    buf; osc state.
 * Accessors:  calcNextOscSampleSyncBlock(); calcPeriodicInterpSyncBlock().
 * Affiliates: osc_syncWaveform() (the same partition).
 */
static OSC_S085_SECTION void osc_renderSyncWave(OscInfo* osc, const uint32_t* over,
                                                const uint32_t ratioQ16,
                                                const int32_t depthQ30,
                                                int16_t* buf, const uint8_t size)
{
	switch(osc->waveform)
	{
	case SINE:
		calcSineSyncBlock(osc, over, ratioQ16, depthQ30, buf, size);
		break;
	case SAW:
		calcWavetableOscSyncBlock(osc, sawTable, over, ratioQ16, depthQ30, buf, size);
		break;
	case TRI:
		calcWavetableOscSyncBlock(osc, triTable, over, ratioQ16, depthQ30, buf, size);
		break;
	case REC:
		calcWavetableOscSyncBlock(osc, recTable, over, ratioQ16, depthQ30, buf, size);
		break;
	case SQ3: case SW3:
		calcWavetableOscSyncBlock(osc, osc_stockTables[osc->waveform - OSC_STOCK_FIRST],
		                          over, ratioQ16, depthQ30, buf, size);
		break;
	case CRASH:
		calcSampleOscSyncBlock(osc, over, ratioQ16, depthQ30, buf, size);
		break;
	default:	/* NOISE: unsynced */
		osc_renderPlainWave(osc, buf, size);
		break;
	}
}

/*
 * Waveform-interpolated slave under sync (S085 P1).
 *
 * What:       calcPeriodicInterpBlock() with each partner rendered through
 *             osc_renderSyncWave() against the same reset list, so both
 *             partners reset on the same samples. The blend is the plain
 *             one at gain 1.
 *             Phase rule: when the base partner has a sync copy, osc->phase
 *             takes the base partner's (synced) phase. Otherwise it takes
 *             the plain rule, start + phaseInc x size.
 * Why:        plan Q5: `wav` behaves the same in every mode. Cost is bounded
 *             by the existing two-unit interpolation limit
 *             (OSC_WAVE_INTERP_MAX_ACTIVE).
 * Inputs:     osc (interpolation active), over[size], ratioQ16, depthQ30,
 *             buf[size], size.
 * Outputs:    buf; osc->phase.
 * Placement:  .itcm.s085 (OSC_S085_SECTION); the compiler inlines it into
 *             its noinline ITCM dispatcher (OSC_S085_DISPATCH).
 * Accessors:  calcNextOscSampleSyncBlock().
 * Affiliates: calcPeriodicInterpBlock(); osc_interp_a/osc_interp_b scratch.
 */
static OSC_S085_SECTION void calcPeriodicInterpSyncBlock(OscInfo* osc,
                                                         const uint32_t* over,
                                                         const uint32_t ratioQ16,
                                                         const int32_t depthQ30,
                                                         int16_t* buf,
                                                         const uint8_t size)
{
	const uint8_t base = osc->waveform;
	const uint8_t next = osc->waveInterpNext;
	const float frac = osc->waveInterpFrac;
	const uint32_t startPhase = osc->phase;
	OscInfo oscA = *osc;
	OscInfo oscB = *osc;
	uint8_t i;

	oscA.waveform = base;
	osc_clearWaveInterp(&oscA);
	oscB.waveform = next;
	osc_clearWaveInterp(&oscB);

	osc_renderSyncWave(&oscA, over, ratioQ16, depthQ30, osc_interp_a, size);
	osc_renderSyncWave(&oscB, over, ratioQ16, depthQ30, osc_interp_b, size);

	for (i = 0; i < size; i++) {
		const int16_t a = osc_interp_a[i];
		const int16_t b = osc_interp_b[i];
		buf[i] = (int16_t)(a + frac * (b - a));
	}
	osc->phase = osc_syncWaveform(base) ? oscA.phase
	                                    : startPhase + (osc->phaseInc * (uint32_t)size);
}

/*
 * Render one slave block under hard sync (S085 P1).
 * Contract: Oscillator.h. Order: ratio (one VDIV), then the interpolation
 * entry (as calcNextOscSampleBlock()), then the waveform switch.
 */
OSC_S085_DISPATCH void calcNextOscSampleSyncBlock(OscInfo* osc, const uint32_t* over,
                                                 const uint32_t clockInc,
                                                 const int32_t depthQ30,
                                                 int16_t* buf, const uint8_t size)
{
	const uint32_t ratioQ16 = osc_syncRatioQ16(osc->phaseInc, clockInc);

	if (osc_waveInterpActive(osc)) {
		calcPeriodicInterpSyncBlock(osc, over, ratioQ16, depthQ30, buf, size);
		return;
	}
	osc_renderSyncWave(osc, over, ratioQ16, depthQ30, buf, size);
}

/*
 * Sine carrier with the ring law (S085 P1).
 *
 * What:       calcSineBlock() with the store replaced by
 *             osc_ringSample(oscOut, modBuf[i], d_i).
 * Inputs:     osc, modBuf[size], buf[size], size, depthQ15,
 *             depthStepQ15.
 * Outputs:    buf; osc->phase.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcSineBlock(); tools/dsp_test osc_mod R1.
 */
static OSC_S085_LOOP void calcSineRingBlock(OscInfo* osc, const int16_t* modBuf,
                                            int16_t* buf, const uint8_t size,
                                            const int32_t depthQ15,
                                            const int32_t depthStepQ15)
{
	int32_t ringDepth = depthQ15;
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut;

		const uint32_t index =  oscPhase;
		uint32_t  itg	= index>>20;

		oscOut = sine_table[itg++];
		const float frac = (index & 0xfffff) * 0.00000095367431640625f;
		oscOut += frac*(sine_table[itg] - oscOut);

		osc->phase = oscPhase + osc->phaseInc;

		ringDepth += depthStepQ15;
		buf[i] = osc_ringSample(oscOut, modBuf[i], ringDepth);
	}
}

/*
 * Tri/Saw/Rec carrier with the ring law (S085 P1).
 *
 * What:       calcWavetableOscBlock() with the store replaced by
 *             osc_ringSample(oscOut, modBuf[i], d_i).
 * Inputs:     osc, table, modBuf[size], buf[size], size, depthQ15,
 *             depthStepQ15.
 * Outputs:    buf; osc->phase.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcWavetableOscBlock(); tools/dsp_test osc_mod R1.
 */
static OSC_S085_LOOP void calcWavetableOscRingBlock(OscInfo* osc,
                                                    const int16_t table[][1024],
                                                    const int16_t* modBuf,
                                                    int16_t* buf,
                                                    const uint8_t size,
                                                    const int32_t depthQ15,
                                                    const int32_t depthStepQ15)
{
	int32_t ringDepth = depthQ15;
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut ;
		uint32_t  itg	= oscPhase>>22;
		const uint32_t next = (itg + 1u) & 0x000003FFu;
		oscOut = table[osc->tableOffset][itg];
		const float frac = (oscPhase & 0x003FFFFF) * 2.384185791015625e-07f; // 1 / 2^22
		oscOut += frac*(table[osc->tableOffset][next] - oscOut);

		osc->phase = oscPhase + osc->phaseInc;
		ringDepth += depthStepQ15;
		buf[i] = osc_ringSample(oscOut, modBuf[i], ringDepth);
	}
}

/*
 * Noise carrier with the ring law (S085 P1).
 *
 * What:       calcNoiseBlock(): a new hardware-RNG value on each phase wrap,
 *             held between wraps; the store is replaced by
 *             osc_ringSample(osc->output, modBuf[i], d_i).
 * Inputs:     osc, modBuf[size], buf[size], size, depthQ15,
 *             depthStepQ15.
 * Outputs:    buf; osc->phase; osc->output.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcNoiseBlock(); GetRngValue() (random.c).
 */
static OSC_S085_LOOP void calcNoiseRingBlock(OscInfo* osc, const int16_t* modBuf,
                                             int16_t* buf, const uint8_t size,
                                             const int32_t depthQ15,
                                             const int32_t depthStepQ15)
{
	int32_t ringDepth = depthQ15;
	int i;
	for(i=0;i<size;i++)
	{
		const uint32_t lastPhase = osc->phase;
		osc->phase += osc->phaseInc;

		if(lastPhase > osc->phase)
		{
			//overflow happened -> phaseWrapped
			osc->output = (int16_t)(GetRngValue()); //normal pitched white noise
		}

		ringDepth += depthStepQ15;
		buf[i] = osc_ringSample(osc->output, modBuf[i], ringDepth);
	}
}

/*
 * Crash carrier with the ring law (S085 P1).
 *
 * What:       calcSampleOscBlock() with the store replaced by
 *             osc_ringSample(oscOut, modBuf[i], d_i).
 * Inputs:     osc, modBuf[size], buf[size], size, depthQ15,
 *             depthStepQ15.
 * Outputs:    buf; osc->phase.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcSampleOscBlock().
 */
static OSC_S085_LOOP void calcSampleOscRingBlock(OscInfo* osc, const int16_t* modBuf,
                                                 int16_t* buf, const uint8_t size,
                                                 const int32_t depthQ15,
                                                 const int32_t depthStepQ15)
{
	int32_t ringDepth = depthQ15;
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut ;
		uint32_t  itg	= oscPhase>>17;

		oscOut = crashSample[itg++];
		const float frac = (oscPhase & 0x0001FFFF) * 0.00000762939453125f; // 1 / 2^17
		oscOut += frac*(crashSample[itg] - oscOut);
		oscOut = (oscOut - 127)*256;

		osc->phase = oscPhase + osc->phaseInc;
		ringDepth += depthStepQ15;
		buf[i] = osc_ringSample(oscOut, modBuf[i], ringDepth);
	}
}

/*
 * User-sample carrier with the ring law (S085 P1).
 *
 * What:       calcUserSampleOscBlock() (the sample cache, one-shot hold,
 *             looped wrap, interpolation across the loop seam) with the
 *             store replaced by osc_ringSample(oscOut, modBuf[i], d_i).
 *             Like the plain loop, an empty sample returns without writing
 *             buf (unchanged behaviour, kept for parity).
 * Inputs:     osc (waveform >= OSC_SAMPLE_START), modBuf[size], buf[size],
 *             size, depthQ15,
 *             depthStepQ15.
 * Outputs:    buf; osc->phase; osc sample cache.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcUserSampleOscBlock(); osc_updateSampleCache();
 *             SampleMemory.
 */
static OSC_S085_LOOP void calcUserSampleOscRingBlock(OscInfo* osc,
                                                     const int16_t* modBuf,
                                                     int16_t* buf,
                                                     const uint8_t size,
                                                     const int32_t depthQ15,
                                                     const int32_t depthStepQ15)
{
	uint8_t sampleIndex = osc->waveform - OSC_SAMPLE_START;
	osc_updateSampleCache(osc, sampleIndex);
	uint32_t sampleSize = osc->sampleCacheSize;
	uint8_t looped = osc->sampleCacheLooped;

	//cast sample data to signed int16_t array
	const int16_t* sampleData = osc->sampleCacheData;
	if(sampleSize == 0) return;

	int32_t ringDepth = depthQ15;
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut ;
		uint32_t  itg	= oscPhase>>17;

		if(looped && itg >= sampleSize)
			itg %= sampleSize;

		oscOut = sampleData[itg];
		const float frac = (oscPhase & 0x0001FFFF) * 0.00000762939453125f; // 1 / 2^17
		if(looped && (itg + 1u) >= sampleSize)
			oscOut += frac*(sampleData[0] - oscOut);
		else
			oscOut += frac*(sampleData[itg + 1u] - oscOut);
		itg++;

		//one shot
		if(looped)
		{
			uint32_t nextPhase = oscPhase + osc->phaseInc;
			if(sampleSize < 32768u) {
				uint32_t loopPhase = sampleSize << 17;
				if(nextPhase >= loopPhase)
					nextPhase %= loopPhase;
			}
			osc->phase = nextPhase;
		}
		else if(itg < sampleSize)
		{
			osc->phase = oscPhase + osc->phaseInc;
		}
		ringDepth += depthStepQ15;
		buf[i] = osc_ringSample(oscOut, modBuf[i], ringDepth);
	}
}

/*
 * Waveform-interpolated carrier with the ring law (S085 P1).
 *
 * What:       calcPeriodicInterpBlock(): both partners rendered plain at
 *             gain 1 into the DTCM scratch, then blended. The blend's store
 *             is replaced by osc_ringSample(blend, modBuf[i], d_i). The
 *             phase follows the plain rule (start + phaseInc x size).
 * Placement:  .itcm.s085 (OSC_S085_SECTION); the compiler inlines it into
 *             its noinline ITCM dispatcher (OSC_S085_DISPATCH).
 * Inputs:     osc (interpolation active), modBuf[size], buf[size], size,
 *             depthQ15, depthStepQ15.
 * Outputs:    buf; osc->phase.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcPeriodicInterpBlock(); OSC_WAVE_INTERP_MAX_ACTIVE.
 */
static OSC_S085_SECTION void calcPeriodicInterpRingBlock(OscInfo* osc,
                                                         const int16_t* modBuf,
                                                         int16_t* buf,
                                                         const uint8_t size,
                                                         const int32_t depthQ15,
                                                         const int32_t depthStepQ15)
{
	const uint8_t base = osc->waveform;
	const uint8_t next = osc->waveInterpNext;
	const float frac = osc->waveInterpFrac;
	const uint32_t startPhase = osc->phase;
	OscInfo oscA = *osc;
	OscInfo oscB = *osc;
	int32_t ringDepth = depthQ15;
	uint8_t i;

	oscA.waveform = base;
	osc_clearWaveInterp(&oscA);
	oscB.waveform = next;
	osc_clearWaveInterp(&oscB);

	osc_renderPlainWave(&oscA, osc_interp_a, size);
	osc_renderPlainWave(&oscB, osc_interp_b, size);

	for (i = 0; i < size; i++) {
		const int16_t a = osc_interp_a[i];
		const int16_t b = osc_interp_b[i];
		const int16_t out = (int16_t)(a + frac * (b - a));
		ringDepth += depthStepQ15;
		buf[i] = osc_ringSample(out, modBuf[i], ringDepth);
	}
	osc->phase = startPhase + (osc->phaseInc * (uint32_t)size);
}

/*
 * Render one carrier block with the ring law (S085 P1).
 * Contract: Oscillator.h. It mirrors calcNextOscSampleBlock(), including the
 * user-sample range check that returns without writing buf. Every ring copy
 * uses d_i = depthQ15 + (i + 1) x depthStepQ15 for sample i, so the block
 * ends at depthQ15 + size x depthStepQ15 (the caller's next start value).
 */
OSC_S085_DISPATCH void calcNextOscSampleRingBlock(OscInfo* osc, const int16_t* modBuf,
                                                  int16_t* buf, const uint8_t size,
                                                  const int32_t depthQ15,
                                                  const int32_t depthStepQ15)
{
	if (osc_waveInterpActive(osc)) {
		calcPeriodicInterpRingBlock(osc, modBuf, buf, size, depthQ15, depthStepQ15);
		return;
	}

	switch(osc->waveform)
	{
	case SINE:
		calcSineRingBlock(osc, modBuf, buf, size, depthQ15, depthStepQ15);
		break;
	case SAW:
		calcWavetableOscRingBlock(osc, sawTable, modBuf, buf, size, depthQ15, depthStepQ15);
		break;
	case TRI:
		calcWavetableOscRingBlock(osc, triTable, modBuf, buf, size, depthQ15, depthStepQ15);
		break;
	case REC:
		calcWavetableOscRingBlock(osc, recTable, modBuf, buf, size, depthQ15, depthStepQ15);
		break;
	case SQ3: case SW3:
		calcWavetableOscRingBlock(osc, osc_stockTables[osc->waveform - OSC_STOCK_FIRST],
		                          modBuf, buf, size, depthQ15, depthStepQ15);
		break;
	case NOISE:
		calcNoiseRingBlock(osc, modBuf, buf, size, depthQ15, depthStepQ15);
		break;
	case CRASH:
		calcSampleOscRingBlock(osc, modBuf, buf, size, depthQ15, depthStepQ15);
		break;
	default: {
		/* S087 D: unreachable (see D-7). */
		uint8_t i;
		for (i = 0u; i < size; i++)
			buf[i] = 0;
		break; }
	}
}

//-----------------------------------------------------------
static void osc_calcSineFreqValue(OscInfo* osc, const float currentFreq)
{
	osc->phaseInc = freq2PhaseIncr(currentFreq);
}
//-----------------------------------------------------------
static void osc_calcNoiseFreqValue(OscInfo* osc, const float currentFreq)
{
	osc_calcSineFreqValue(osc, currentFreq);
}
//-----------------------------------------------------------
static void osc_calcWavetableFreqValue(OscInfo* osc, const float currentFreq)
{
	osc->phaseInc = freq2PhaseIncr1024(currentFreq);

	const uint8_t overtoneIndex = freqToTableIndex(currentFreq);
	osc->tableOffset = overtoneIndex>10?10:overtoneIndex;
}
//-----------------------------------------------------------
static void osc_calcSampleFreqValue(OscInfo* osc, const float currentFreq)
{
	osc->phaseInc = freq2PhaseIncr32767(currentFreq);
}
//-----------------------------------------------------------
static void osc_calcUserSampleFreqValue(OscInfo* osc, const float currentFreq)
{
	osc_calcSampleFreqValue(osc, currentFreq);
}
//-----------------------------------------------------------
 INITCM void osc_setFreq(OscInfo* osc)
 {
		const float currentFreq = osc->freq*osc->pitchMod*osc->modNodeValue;
		/*
		 * Typed osc1 and modulator list entries (S087 stage D).
		 *
		 * What:       a wav oscillator's wav value past the stock list is
		 *             clamped; a typed osc1, or a modulator whose value is
		 *             past the stock list, binds its catalogue item and
		 *             returns (osc_userBind() also sets phaseInc).
		 * Why:        every writer of wav/typ (Morph, LFO, velocity,
		 *             automation, menu) runs before this call, so the render
		 *             always sees a bound item or a stock wave.
		 * Affiliates: osc_userBind(); OSC_TYPE_*.
		 */
		if (osc->oscType == OSC_TYPE_WAV && osc->waveform > OSC_STOCK_LAST)
			osc->waveform = OSC_STOCK_LAST;
		if (osc->oscType == OSC_TYPE_MODLIST &&
		    osc->waveform < OSC_SAMPLE_START &&
		    osc->sampleCacheLooped != OSC_BIND_NONE) {
			/* S087 D: back on a stock wave, drop the sample bind so the
			 * sync-master rules cannot read a stale kind. */
			osc->sampleCacheLooped = OSC_BIND_NONE;
			osc->sampleCacheIndex = 0u;
			osc->sampleCacheSize = 0u;
			osc->sampleCacheData = 0;
			osc->sampleCacheGeneration = 0u;
		}
		if (osc_isTyped(osc) ||
		    (osc->oscType == OSC_TYPE_MODLIST &&
		     osc->waveform >= OSC_SAMPLE_START)) {
			osc_userBind(osc);
			return;
		}
		/* osc_setFreq() is called frequently by the block dispatcher. Cache the
		** effective frequency+waveform tuple so unchanged blocks skip the
		** phase-increment recalculation and wavetable octave selection
		** (S073 Step 6: octave selection is now an edge count, not log2f()). */
		if (osc->freqCacheValid &&
			osc->freqCacheWaveform == osc->waveform &&
			osc->freqCacheValue == currentFreq) {
			return;
		}

		switch(osc->waveform)
		{
		case SINE:
			osc_calcSineFreqValue(osc, currentFreq);
			break;

		case SAW:
			osc_calcWavetableFreqValue(osc, currentFreq);
			break;

		case TRI:
			osc_calcWavetableFreqValue(osc, currentFreq);
			break;

		case REC:
			osc_calcWavetableFreqValue(osc, currentFreq);
			break;

		case SQ3: case SW3:
			osc_calcWavetableFreqValue(osc, currentFreq);
			break;

		case NOISE:
			osc_calcNoiseFreqValue(osc, currentFreq);
			break;


		case CRASH:
			osc_calcSampleFreqValue(osc, currentFreq);
			break;

		//samples
		default:
			osc_calcUserSampleFreqValue(osc, currentFreq);
			break;

		}
		osc->freqCacheValue = currentFreq;
		osc->freqCacheWaveform = osc->waveform;
		osc->freqCacheValid = 1u;
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

/*
 * S087: oscillator types (samples, loops, wavetables).
 *
 * What:       the type writer plus, from stage D, the catalogue bind, the
 *             typed renderers and the sample/wavetable loops. Everything here
 *             runs at control rate except the loops, which are marked
 *             OSC_S087_LOOP and placed in .itcm.s087.
 * Why:        S087 P0 §2 rules 2-3: new per-sample code is a new function,
 *             never an edit to an existing loop, and it lives in its own ITCM
 *             subsection so the existing loops keep their addresses.
 * Inputs:     osc1's typ row and the modulator role marks.
 * Outputs:    OscInfo state and the typed renderers.
 * Accessors:  osc_setFreq(), the four engines, InstrumentManager.
 * Affiliates: OSC_TYPE_* (Oscillator.h); SampleMemory.h; S087 P0 §12.5.
 */

/*
 * Change osc1's type (S087 stage B; see the header).
 *
 * What:       clamp, then store and re-anchor; an unchanged type is a no-op.
 * Why:        see the header (single writer for menu, Kit apply, restore and
 *             step automation).
 * Inputs:     osc, type.  Outputs: oscType, waveform, positionApplied, phase.
 * Accessors:  instrumentManager_writeSpecialRuntime().
 * Affiliates: osc_isTyped(); S087 P0 §12.5; OSC_TYPE_SELECTABLE.
 */
void osc_setType(OscInfo* osc, uint8_t type)
{
	uint8_t oldType;
	uint8_t last;
	uint8_t count;

	if (!osc)
		return;
	if (type >= OSC_TYPE_SELECTABLE)
		type = (uint8_t)(OSC_TYPE_SELECTABLE - 1u);
	oldType = osc->oscType;
	if (type == oldType)
		return;

	osc->oscType = type;
	/*
	 * The wav byte is used as it is, clamped to an entry that exists for the
	 * new type (S087 P0 r8: nothing is remembered or restored). Stage B has
	 * no user entries, so only wav can hold a selection. Stage D replaces
	 * these two arms with osc_userCount().
	 */
	count = osc_userCount(type);
	last = (count == 0u) ? 0u : (uint8_t)(count - 1u);
	if (type == OSC_TYPE_WAV && last > OSC_STOCK_LAST)
		last = OSC_STOCK_LAST;
	if (osc->waveform > last)
		osc->waveform = last;

	/* D7: a type change re-anchors pos and the phase units. */
	osc->positionApplied = osc->position;
	osc->freqCacheValid = 0u;
	osc_userBind(osc);
	if (type == OSC_TYPE_SMP || type == OSC_TYPE_SLP) {
		osc->phase = osc_typedTriggerPhase(osc);
	} else if (oldType == OSC_TYPE_SMP || oldType == OSC_TYPE_SLP) {
		osc->phase = osc_positionTrigger(osc);
	}
	/* wav <-> wtb keeps the phase (S087 P0 §12.5). */

	osc->freqCacheValid = 0u;
}

/*
 * S087 ITCM placement (stage D/E).
 *
 * What:       .itcm.s087 for every S087 per-sample function; OSC_S087_LOOP adds
 *             noinline/noclone so each loop keeps its own symbol and LTO cannot
 *             fold it into an existing loop or re-clone a dispatcher.
 * Why:        S087 P0 §2 rules 2-3: appending inside .itcm moved existing loops
 *             (S085 D7); the linker places *(.itcm.*) after *(.itcm).
 * Accessors:  the S087 loops and typed renderers below.
 * Affiliates: OSC_S085_SECTION/LOOP; link_budget.py.
 */
#define OSC_S087_SECTION __attribute__((section(".itcm.s087")))
#define OSC_S087_LOOP    OSC_S087_SECTION __attribute__((noinline, noclone))

/*
 * Number of user entries for a type (S087 stage D; see the header).
 *
 * What:       wav -> 8; smp/slp -> sample count; wtb -> wavetable count.
 * Why:        the type writer, bind and menu ranges share one count.
 * Inputs:     type.  Outputs: count (0 for an unknown type).
 * Accessors:  osc_setType(), osc_userBind(), InstrumentManager.
 * Affiliates: SampleMemory.h.
 */
uint8_t osc_userCount(uint8_t type)
{
	switch (type) {
	case OSC_TYPE_WAV:
		return OSC_SAMPLE_START;
	case OSC_TYPE_SMP:
	case OSC_TYPE_SLP:
		return sampleMemory_getNumSamples();
	case OSC_TYPE_WTB:
		return sampleMemory_getNumWavetables();
	default:
		return 0u;
	}
}

/*
 * Length of a modulator's flat wav list (S087 stage D; see the header).
 *
 * What:       min(8 + 2 x samples + tables, 128).
 * Why:        S087 P0 §12.4/A24 order: stock, s, l, w.
 * Inputs:     none.  Outputs: count.
 * Accessors:  osc_userBind(), InstrumentManager.
 * Affiliates: osc_userCount().
 */
uint8_t osc_userListCount(void)
{
	const uint16_t samples = sampleMemory_getNumSamples();
	const uint16_t tables = sampleMemory_getNumWavetables();
	uint16_t count = (uint16_t)(OSC_SAMPLE_START + 2u * samples + tables);

	return (uint8_t)(count > 128u ? 128u : count);
}

/*
 * Bind the oscillator's catalogue item for this block (S087 stage D; see the
 * header).
 *
 * What:       kind/index from oscType or the modulator list; on a change
 *             re-reads the item and re-anchors pos/phase; always sets the
 *             sample phase increment.
 * Why:        one bind for osc1 and the modulators, safe across reinstall/type
 *             changes (S087 P0 §6.3, D7, A23/A24).
 * Inputs:     osc, catalogue.  Outputs: bind fields, phaseInc.
 * Accessors:  osc_setFreq().
 * Affiliates: SampleMemory.h; osc_userCount()/osc_userListCount().
 */
void osc_userBind(OscInfo* osc)
{
	const uint32_t generation = sampleMemory_getGeneration();
	const float currentFreq = osc->freq * osc->pitchMod * osc->modNodeValue;
	uint8_t kind = OSC_BIND_NONE;
	uint8_t index = 0u;
	uint8_t count;

	if (osc->oscType == OSC_TYPE_SMP || osc->oscType == OSC_TYPE_SLP) {
		count = osc_userCount(osc->oscType);
		if (count != 0u) {
			index = (uint8_t)(osc->waveform < count ? osc->waveform
			                                        : count - 1u);
			kind = (osc->oscType == OSC_TYPE_SMP) ? OSC_BIND_SMP
			                                      : OSC_BIND_SLP;
		}
	} else if (osc->oscType == OSC_TYPE_WTB) {
		count = osc_userCount(OSC_TYPE_WTB);
		if (count != 0u) {
			index = (uint8_t)(osc->waveform < count ? osc->waveform
			                                        : count - 1u);
			kind = OSC_BIND_WTB16;
		}
	} else if (osc->oscType == OSC_TYPE_MODLIST &&
	           osc->waveform >= OSC_SAMPLE_START) {
		const uint8_t samples = sampleMemory_getNumSamples();
		const uint8_t entry = (uint8_t)(osc_userListCount() - 1u);
		const uint8_t wanted = (uint8_t)(osc->waveform < entry ? osc->waveform
		                                                       : entry);
		const uint8_t list = (uint8_t)(wanted - OSC_SAMPLE_START);

		if (list < samples) {
			kind = OSC_BIND_SMP;
			index = list;
		} else if (list < (uint8_t)(2u * samples)) {
			kind = OSC_BIND_SLP;
			index = (uint8_t)(list - samples);
		} else if (list < (uint8_t)(2u * samples +
		                            sampleMemory_getNumWavetables())) {
			kind = OSC_BIND_WTB16;
			index = (uint8_t)(list - 2u * samples);
		}
	}

	if (kind != osc->sampleCacheLooped ||
	    index != osc->sampleCacheIndex ||
	    generation != osc->sampleCacheGeneration) {
		const uint8_t isWtb = (uint8_t)(kind == OSC_BIND_WTB8 ||
		                                kind == OSC_BIND_WTB16);
		const sample_catalogue_entry_t *entry = (kind == OSC_BIND_SMP ||
		                                        kind == OSC_BIND_SLP)
			? sampleMemory_sampleEntry(index) : (const sample_catalogue_entry_t *)0;
		const wavetable_catalogue_entry_t *wentry = isWtb
			? sampleMemory_wavetableEntry(index)
			: (const wavetable_catalogue_entry_t *)0;

		if (wentry)
			kind = (wentry->bits == 8u) ? OSC_BIND_WTB8 : OSC_BIND_WTB16;

		osc->sampleCacheIndex = index;
		osc->sampleCacheLooped = kind;
		osc->sampleCacheGeneration = generation;
		osc->sampleCacheSize = entry ? entry->frames
			: (wentry ? wentry->frame_len : 0u);
		osc->sampleCacheData = entry
			? (const int16_t *)SAMPLE_FLASH_PTR(entry->offset)
			: (wentry ? (const int16_t *)SAMPLE_FLASH_PTR(wentry->offset)
			          : (const int16_t *)0);
		osc->positionApplied = osc->position;
		if (entry) {
			const uint32_t endPh = entry->frames << OSC_SAMPLE_PHASE_SHIFT;
			const uint32_t loopStartPh = entry->loop_start << OSC_SAMPLE_PHASE_SHIFT;
			const uint32_t loopEndPh = entry->loop_end << OSC_SAMPLE_PHASE_SHIFT;
			const uint32_t loopLenPh = loopEndPh - loopStartPh;

			if (kind == OSC_BIND_SMP) {
				if (osc->phase > endPh)
					osc->phase = endPh;
			} else if (loopLenPh != 0u && osc->phase >= loopEndPh) {
				osc->phase = loopStartPh +
					((osc->phase - loopStartPh) % loopLenPh);
			}
		}
	}
	osc->phaseInc = (osc->oscType == OSC_TYPE_WTB)
		? freq2PhaseIncr(currentFreq)
		: (uint32_t)((currentFreq * 4194304.0f) / REAL_FS);
}

/*
 * Typed trigger start (S087 stage D; see the header).
 *
 * What:       pos start = ((pos x frames) >> 7) << 12; slp clamps a start at
 *             or past loop_end into loop_start; positionApplied = position.
 * Why:        D1/D2: the start is a fraction of the whole sample and a looped
 *             sample always begins inside its fixed loop.
 * Inputs:     osc (bound).  Outputs: start phase; positionApplied.
 * Accessors:  the engines' typed trigger branch.
 * Affiliates: osc_positionTrigger() guard (D-6).
 */
uint32_t osc_typedTriggerPhase(OscInfo* osc)
{
	const uint32_t pos = (uint32_t)(osc->position & 127u);
	uint32_t start;

	osc->positionApplied = osc->position;
	if (!osc_isTyped(osc))
		return 0u;
	if (osc->oscType == OSC_TYPE_WTB)
		return 0u;   /* stage E: pos is a frame position, not a phase */
	if (osc->sampleCacheSize == 0u)
		return 0u;
	start = (((pos * osc->sampleCacheSize) >> 7u) << OSC_SAMPLE_PHASE_SHIFT);
	if (osc->oscType == OSC_TYPE_SLP) {
		const sample_catalogue_entry_t *entry =
			sampleMemory_sampleEntry(osc->sampleCacheIndex);

		if (entry) {
			const uint32_t loopEndPh = entry->loop_end << OSC_SAMPLE_PHASE_SHIFT;

			if (start >= loopEndPh)
				start = entry->loop_start << OSC_SAMPLE_PHASE_SHIFT;
		}
	}
	return start;
}

/*
 * Typed pos blend for one block (S087 stage D; see the header).
 *
 * What:       linear d = position - positionApplied, capped to the sample
 *             speed limit and to a non-negative block-end phase; smp stops
 *             when finished; phaseInc += d x frames when bending.
 * Why:        D1/D3/D4/D5: samples are not periodic, so the difference is
 *             linear and a finished one-shot stays finished.
 * Inputs:     osc (bound), bend.  Outputs: phaseInc, positionApplied,
 *             positionBendStep.
 * Accessors:  osc_positionBendApply() (typed branch).
 * Affiliates: osc_typedPositionRestore(); OSC_POS_SAMPLE_SPEED_CAP.
 */
void osc_typedPositionApply(OscInfo* osc, uint8_t bend)
{
	const uint32_t frames = osc->sampleCacheSize;
	int32_t d;
	int32_t maxStep;

	if (osc->oscType == OSC_TYPE_WTB) {
		/*
		 * S087 E: pos is the frame position. A block moves at most one
		 * frame pair's worth, and phaseInc is never bent.
		 */
		const wavetable_catalogue_entry_t *wt =
			sampleMemory_wavetableEntry(osc->sampleCacheIndex);
		const int32_t count = wt ? (int32_t)wt->frame_count : 0;
		int32_t maxStep;

		d = (int32_t)osc->position - (int32_t)osc->positionApplied;
		maxStep = (count > 1) ? (int32_t)(127 / (count - 1)) : 1;
		if (maxStep < 1)
			maxStep = 1;
		if (d > maxStep)
			d = maxStep;
		else if (d < -maxStep)
			d = -maxStep;
		osc->positionApplied =
			(uint8_t)(((uint32_t)osc->positionApplied + (uint32_t)d) & 127u);
		osc->positionBendStep = (int8_t)d;
		return;
	}
	d = (int32_t)osc->position - (int32_t)osc->positionApplied;
	if (frames == 0u) {
		osc->positionBendStep = 0;
		return;
	}
	if (osc->oscType == OSC_TYPE_SMP &&
	    osc->phase >= (frames << OSC_SAMPLE_PHASE_SHIFT)) {
		d = 0;   /* D4: a finished one-shot is not revived by pos */
	}
	maxStep = (int32_t)(((int64_t)OSC_POS_SAMPLE_SPEED_CAP *
	                     (int64_t)osc->phaseInc) / (int64_t)frames);
	if (maxStep < 1)
		maxStep = 1;
	if (d > maxStep)
		d = maxStep;
	else if (d < -maxStep)
		d = -maxStep;
	/* Keep the block-end phase non-negative (D4). */
	if (d < 0) {
		const int32_t points = (int32_t)(osc->phase >> OSC_SAMPLE_PHASE_SHIFT);
		const int32_t limit = points / (int32_t)OUTPUT_DMA_SIZE;

		if (d < -limit)
			d = -limit;
	}
	osc->positionApplied =
		(uint8_t)(((uint32_t)osc->positionApplied + (uint32_t)d) & 127u);
	if (bend) {
		osc->phaseInc += (uint32_t)(d * (int32_t)frames);
		osc->positionBendStep = (int8_t)d;
	} else {
		osc->positionBendStep = 0;
	}
}

/*
 * End a typed pos blend (S087 stage D; see the header).
 *
 * What:       subtracts step x frames from phaseInc; clears the step.
 * Why:        D8: bindFrames cannot change inside a block, so the restore is
 *             exact without a live local.
 * Inputs:     osc.  Outputs: phaseInc, positionBendStep.
 * Accessors:  osc_positionBendRestore() (typed branch).
 * Affiliates: osc_typedPositionApply().
 */
void osc_typedPositionRestore(OscInfo* osc)
{
	if (osc->oscType == OSC_TYPE_WTB) {
		osc->positionBendStep = 0;
		return;
	}
	osc->phaseInc -= (uint32_t)((int32_t)osc->positionBendStep *
	                            (int32_t)osc->sampleCacheSize);
	osc->positionBendStep = 0;
}

static void osc_zeroBlock(int16_t *buf, uint8_t size)
{
	uint8_t i;

	for (i = 0u; i < size; i++)
		buf[i] = 0;
}

static uint32_t osc_smpReadPhase(const uint32_t phase, const uint32_t endPh)
{
	return phase > endPh ? endPh : phase;
}

static float osc_frac12(const uint32_t phase)
{
	return (float)(phase & 0xFFFu) * (1.0f / 4096.0f);
}

static float osc_bindGain(const OscInfo* osc)
{
	const sample_catalogue_entry_t *entry =
		sampleMemory_sampleEntry(osc->sampleCacheIndex);

	return entry ? sampleMemory_gainFromCode(entry->gain_code) : 1.0f;
}

/*
 * smp plain: one one-shot block (S087 stage D).
 *
 * What:       Q19.12 linear read of the bound item; the two zero frames at the
 *             end make the played-out samples read 0; the phase clamps at the
 *             end.
 * Why:        D-11, L1: install-time padding removes the played-out select.
 * Inputs:     osc, buf, size, gain.
 * Outputs:    buf; phase.
 * Accessors:  osc_renderTyped()/osc_renderUserList().
 * Affiliates: osc_userBind(); test_osc_sample.c.
 */
static OSC_S087_LOOP void osc_smpPlainBlock(OscInfo* osc, int16_t *buf,
                                            uint8_t size, float gain)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	const uint32_t endPh = osc->sampleCacheSize << OSC_SAMPLE_PHASE_SHIFT;
	const float g = gain * osc_bindGain(osc);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	for (i = 0u; i < size; i++) {
		const uint32_t idx = ph >> OSC_SAMPLE_PHASE_SHIFT;
		const int16_t a = data[idx];
		const int16_t b = data[idx + 1u];
		const float out = (float)a + osc_frac12(ph) * (float)(b - a);

		buf[i] = (int16_t)(out * g);
		ph = osc_smpReadPhase(ph + inc, endPh);
	}
	osc->phase = ph;
}

/*
 * slp plain: one looped block (S087 stage D).
 *
 * What:       fixed loop region; the next-point read at the seam goes to
 *             loop_start (A6); the phase wraps by one loop length.
 * Why:        D-12, L2/L3/L4.
 * Inputs:     osc, buf, size, gain.  Outputs: buf; phase.
 * Accessors:  osc_renderTyped()/osc_renderUserList().
 * Affiliates: osc_userBind(); test_osc_sample.c.
 */
static OSC_S087_LOOP void osc_slpPlainBlock(OscInfo* osc, int16_t *buf,
                                            uint8_t size, float gain)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	const sample_catalogue_entry_t *entry =
		sampleMemory_sampleEntry(osc->sampleCacheIndex);
	const uint32_t ls = entry ? entry->loop_start : 0u;
	const uint32_t le = entry ? entry->loop_end : osc->sampleCacheSize;
	const uint32_t len = le - ls;
	uint32_t inc = osc->phaseInc;
	const uint32_t lenPh = len << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopEndPh = le << OSC_SAMPLE_PHASE_SHIFT;
	const float g = gain * osc_bindGain(osc);
	uint32_t ph = osc->phase;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u || len == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	if (inc >= lenPh)
		inc %= lenPh;
	for (i = 0u; i < size; i++) {
		const uint32_t idx = ph >> OSC_SAMPLE_PHASE_SHIFT;
		const uint32_t next = (idx + 1u == le) ? ls : idx + 1u;
		const int16_t a = data[idx];
		const int16_t b = data[next];
		const float out = (float)a + osc_frac12(ph) * (float)(b - a);

		buf[i] = (int16_t)(out * g);
		ph += inc;
		if (ph >= loopEndPh)
			ph -= lenPh;
	}
	osc->phase = ph;
}

/*
 * smp FM carrier: one one-shot block (S087 stage D).
 *
 * What:       the plain read at ph + (ssat(mod x fmMod) << 9), clamped to
 *             [0, endPh]; the phase advance is the plain one.
 * Why:        D-13, L5/L6: amt means the same as on a table wave.
 * Inputs:     osc, mod, buf, size, gain.  Outputs: buf; phase.
 * Accessors:  osc_renderTypedFm().
 * Affiliates: calcFmBlock(); test_osc_sample.c.
 */
static OSC_S087_LOOP void osc_smpFmBlock(OscInfo* osc, const int16_t *mod,
                                         int16_t *buf, uint8_t size, float gain)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	const uint32_t endPh = osc->sampleCacheSize << OSC_SAMPLE_PHASE_SHIFT;
	const float g = gain * osc_bindGain(osc);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	for (i = 0u; i < size; i++) {
		const int32_t off = (int32_t)__SSAT(
			(int32_t)((float)mod[i] * osc->fmMod), 16) << OSC_FM_SAMPLE_SHIFT;
		int32_t rp = (int32_t)ph + off;
		uint32_t idx;
		int16_t a;
		int16_t b;
		float out;

		if (rp < 0)
			rp = 0;
		else if ((uint32_t)rp > endPh)
			rp = (int32_t)endPh;
		idx = (uint32_t)rp >> OSC_SAMPLE_PHASE_SHIFT;
		a = data[idx];
		b = data[idx + 1u];
		out = (float)a + osc_frac12((uint32_t)rp) * (float)(b - a);
		buf[i] = (int16_t)(out * g);
		ph = osc_smpReadPhase(ph + inc, endPh);
	}
	osc->phase = ph;
}

/*
 * slp FM carrier: one looped block (S087 stage D).
 *
 * What:       the FM offset is bounded to half a loop, wrapped once each way
 *             into the loop, then clamped to the last point; the plain seam
 *             and phase wrap stay.
 * Why:        D-14, A5: constant cost, no per-sample modulo.
 * Inputs:     osc, mod, buf, size, gain.  Outputs: buf; phase.
 * Accessors:  osc_renderTypedFm().
 * Affiliates: test_osc_sample.c.
 */
static OSC_S087_LOOP void osc_slpFmBlock(OscInfo* osc, const int16_t *mod,
                                         int16_t *buf, uint8_t size, float gain)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	const sample_catalogue_entry_t *entry =
		sampleMemory_sampleEntry(osc->sampleCacheIndex);
	const uint32_t ls = entry ? entry->loop_start : 0u;
	const uint32_t le = entry ? entry->loop_end : osc->sampleCacheSize;
	const uint32_t len = le - ls;
	uint32_t inc = osc->phaseInc;
	const uint32_t lenPh = len << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopEndPh = le << OSC_SAMPLE_PHASE_SHIFT;
	const int32_t half = (int32_t)(lenPh >> 1);
	const float g = gain * osc_bindGain(osc);
	uint32_t ph = osc->phase;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u || len == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	if (inc >= lenPh)
		inc %= lenPh;
	for (i = 0u; i < size; i++) {
		int32_t off = (int32_t)__SSAT(
			(int32_t)((float)mod[i] * osc->fmMod), 16) << OSC_FM_SAMPLE_SHIFT;
		int32_t rel;
		int32_t rp;
		uint32_t idx;
		uint32_t next;
		int16_t a;
		int16_t b;
		float out;

		if (off > half)
			off = half;
		else if (off < -half)
			off = -half;
		rel = (int32_t)(ph - (ls << OSC_SAMPLE_PHASE_SHIFT));
		if (rel < 0)
			rel = 0;
		else if ((uint32_t)rel >= lenPh)
			rel = (int32_t)(lenPh - 1u);
		rp = rel + off;
		if (rp < 0)
			rp += (int32_t)lenPh;
		else if ((uint32_t)rp >= lenPh)
			rp -= (int32_t)lenPh;
		if (rp < 0)
			rp = 0;
		else if ((uint32_t)rp >= lenPh)
			rp = (int32_t)(lenPh - 1u);
		idx = (uint32_t)rp >> OSC_SAMPLE_PHASE_SHIFT;
		next = (idx + 1u == le) ? ls : idx + 1u;
		a = data[idx];
		b = data[next];
		out = (float)a + osc_frac12((uint32_t)rp) * (float)(b - a);
		buf[i] = (int16_t)(out * g);
		ph += inc;
		if (ph >= loopEndPh)
			ph -= lenPh;
	}
	osc->phase = ph;
}

/*
 * smp ring carrier: one one-shot block (S087 stage D).
 *
 * What:       the plain read with the ring law and the per-sample depth ramp.
 * Why:        D-15, L7; gain 1 x the item's code.
 * Inputs:     osc, modBuf, buf, size, depthQ15, depthStepQ15.
 * Outputs:    buf; phase.
 * Accessors:  osc_renderTypedRing().
 * Affiliates: osc_ringSample().
 */
static OSC_S087_LOOP void osc_smpRingBlock(OscInfo* osc, const int16_t *modBuf,
                                           int16_t *buf, uint8_t size,
                                           int32_t depthQ15,
                                           int32_t depthStepQ15)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	const uint32_t endPh = osc->sampleCacheSize << OSC_SAMPLE_PHASE_SHIFT;
	const float g = osc_bindGain(osc);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	int32_t depth = depthQ15;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	for (i = 0u; i < size; i++) {
		const uint32_t idx = ph >> OSC_SAMPLE_PHASE_SHIFT;
		const int16_t a = data[idx];
		const int16_t b = data[idx + 1u];
		const float out = (float)a + osc_frac12(ph) * (float)(b - a);

		buf[i] = (int16_t)((float)osc_ringSample((int16_t)out, modBuf[i], depth) * g);
		depth += depthStepQ15;
		ph = osc_smpReadPhase(ph + inc, endPh);
	}
	osc->phase = ph;
}

/*
 * slp ring carrier: one looped block (S087 stage D).
 *
 * What:       the looped seam read with the ring law and depth ramp.
 * Why:        D-16, L2/L7.
 * Inputs:     osc, modBuf, buf, size, depthQ15, depthStepQ15.
 * Outputs:    buf; phase.
 * Accessors:  osc_renderTypedRing().
 * Affiliates: osc_ringSample().
 */
static OSC_S087_LOOP void osc_slpRingBlock(OscInfo* osc, const int16_t *modBuf,
                                           int16_t *buf, uint8_t size,
                                           int32_t depthQ15,
                                           int32_t depthStepQ15)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	const sample_catalogue_entry_t *entry =
		sampleMemory_sampleEntry(osc->sampleCacheIndex);
	const uint32_t ls = entry ? entry->loop_start : 0u;
	const uint32_t le = entry ? entry->loop_end : osc->sampleCacheSize;
	const uint32_t len = le - ls;
	uint32_t inc = osc->phaseInc;
	const uint32_t lenPh = len << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopEndPh = le << OSC_SAMPLE_PHASE_SHIFT;
	const float g = osc_bindGain(osc);
	uint32_t ph = osc->phase;
	int32_t depth = depthQ15;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u || len == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	if (inc >= lenPh)
		inc %= lenPh;
	for (i = 0u; i < size; i++) {
		const uint32_t idx = ph >> OSC_SAMPLE_PHASE_SHIFT;
		const uint32_t next = (idx + 1u == le) ? ls : idx + 1u;
		const int16_t a = data[idx];
		const int16_t b = data[next];
		const float out = (float)a + osc_frac12(ph) * (float)(b - a);

		buf[i] = (int16_t)((float)osc_ringSample((int16_t)out, modBuf[i], depth) * g);
		depth += depthStepQ15;
		ph += inc;
		if (ph >= loopEndPh)
			ph -= lenPh;
	}
	osc->phase = ph;
}

/*
 * smp sync slave: one one-shot block (S087 stage D).
 *
 * What:       the plain read; the phase is pulled relative to the pos start
 *             point (D9), so each reset restarts the item at its start.
 * Why:        D-17.
 * Inputs:     osc, over, ratioQ16, depthQ30, buf, size.
 * Outputs:    buf; phase.
 * Accessors:  osc_renderTypedSync().
 * Affiliates: osc_syncPull().
 */
static OSC_S087_LOOP void osc_smpSyncBlock(OscInfo* osc, const uint32_t *over,
                                           uint32_t ratioQ16, int32_t depthQ30,
                                           int16_t *buf, uint8_t size)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	const uint32_t frames = osc->sampleCacheSize;
	const uint32_t endPh = frames << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t startPh = (uint32_t)(
		(((uint32_t)(osc->position & 127u) * frames) >> 7u) <<
		OSC_SAMPLE_PHASE_SHIFT);
	const float g = osc_bindGain(osc);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	uint8_t i;

	if (!data || frames == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	for (i = 0u; i < size; i++) {
		const uint32_t idx = ph >> OSC_SAMPLE_PHASE_SHIFT;
		const int16_t a = data[idx];
		const int16_t b = data[idx + 1u];
		const float out = (float)a + osc_frac12(ph) * (float)(b - a);
		const uint32_t rel = osc_syncPull(ph - startPh + inc, over[i],
		                                  ratioQ16, depthQ30);

		buf[i] = (int16_t)(out * g);
		ph = osc_smpReadPhase(startPh + rel, endPh);
	}
	osc->phase = ph;
}

/*
 * slp sync slave: one looped block (S087 stage D).
 *
 * What:       the looped seam read; the phase is pulled relative to the pos
 *             start and then wrapped into the loop.
 * Why:        D-18, D9.
 * Inputs:     osc, over, ratioQ16, depthQ30, buf, size.
 * Outputs:    buf; phase.
 * Accessors:  osc_renderTypedSync().
 * Affiliates: osc_syncPull().
 */
static OSC_S087_LOOP void osc_slpSyncBlock(OscInfo* osc, const uint32_t *over,
                                           uint32_t ratioQ16, int32_t depthQ30,
                                           int16_t *buf, uint8_t size)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	const sample_catalogue_entry_t *entry =
		sampleMemory_sampleEntry(osc->sampleCacheIndex);
	const uint32_t ls = entry ? entry->loop_start : 0u;
	const uint32_t le = entry ? entry->loop_end : osc->sampleCacheSize;
	const uint32_t len = le - ls;
	uint32_t inc = osc->phaseInc;
	const uint32_t frames = osc->sampleCacheSize;
	const uint32_t lenPh = len << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopEndPh = le << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t startPh = (uint32_t)(
		(((uint32_t)(osc->position & 127u) * frames) >> 7u) <<
		OSC_SAMPLE_PHASE_SHIFT);
	const float g = osc_bindGain(osc);
	uint32_t ph = osc->phase;
	uint8_t i;

	if (!data || frames == 0u || len == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	if (inc >= lenPh)
		inc %= lenPh;
	for (i = 0u; i < size; i++) {
		const uint32_t idx = ph >> OSC_SAMPLE_PHASE_SHIFT;
		const uint32_t next = (idx + 1u == le) ? ls : idx + 1u;
		const int16_t a = data[idx];
		const int16_t b = data[next];
		const float out = (float)a + osc_frac12(ph) * (float)(b - a);
		const uint32_t rel = osc_syncPull(ph - startPh + inc, over[i],
		                                  ratioQ16, depthQ30);

		buf[i] = (int16_t)(out * g);
		ph = startPh + rel;
		if (ph >= loopEndPh)
			ph -= lenPh;
	}
	osc->phase = ph;
}

/* S087 E: the wavetable copies are defined after the renderers. */
static OSC_S087_LOOP void osc_wtbBlendBlock(OscInfo* osc, int16_t* buf,
                                            uint8_t size, float gain);
static OSC_S087_LOOP void osc_wtbPlainBlock(OscInfo* osc, int16_t* buf,
                                            uint8_t size, float gain);
static OSC_S087_LOOP void osc_wtbFmBlendBlock(OscInfo* osc,
                                              const int16_t* mod, int16_t* buf,
                                              uint8_t size, float gain);
static OSC_S087_LOOP void osc_wtbRingBlendBlock(OscInfo* osc,
                                                const int16_t* modBuf,
                                                int16_t* buf, uint8_t size,
                                                int32_t depthQ15,
                                                int32_t depthStepQ15);
static OSC_S087_LOOP void osc_wtbSyncBlendBlock(OscInfo* osc,
                                                const uint32_t* over,
                                                uint32_t ratioQ16,
                                                int32_t depthQ30,
                                                int16_t* buf, uint8_t size);

/* S087 D/E: typed render entries (see the header). */
void OSC_S087_SECTION osc_renderTyped(OscInfo* osc, int16_t* buf, uint8_t size,
                                      float gain)
{
	switch (osc->sampleCacheLooped) {
	case OSC_BIND_SMP:
		osc_smpPlainBlock(osc, buf, size, gain);
		break;
	case OSC_BIND_SLP:
		osc_slpPlainBlock(osc, buf, size, gain);
		break;
	case OSC_BIND_WTB8:
	case OSC_BIND_WTB16:
		osc_wtbBlendBlock(osc, buf, size, gain);
		break;
	default:
		osc_zeroBlock(buf, size);
		break;
	}
}

void OSC_S087_SECTION osc_renderTypedFm(OscInfo* osc, const int16_t* mod,
                                        int16_t* buf, uint8_t size, float gain)
{
	switch (osc->sampleCacheLooped) {
	case OSC_BIND_SMP:
		osc_smpFmBlock(osc, mod, buf, size, gain);
		break;
	case OSC_BIND_SLP:
		osc_slpFmBlock(osc, mod, buf, size, gain);
		break;
	case OSC_BIND_WTB8:
	case OSC_BIND_WTB16:
		osc_wtbFmBlendBlock(osc, mod, buf, size, gain);
		break;
	default:
		osc_zeroBlock(buf, size);
		break;
	}
}

void OSC_S087_SECTION osc_renderTypedSync(OscInfo* osc, const uint32_t* over,
                                          uint32_t clockInc, int32_t depthQ30,
                                          int16_t* buf, uint8_t size)
{
	const uint32_t ratioQ16 = osc_syncRatioQ16(osc->phaseInc, clockInc);

	switch (osc->sampleCacheLooped) {
	case OSC_BIND_SMP:
		osc_smpSyncBlock(osc, over, ratioQ16, depthQ30, buf, size);
		break;
	case OSC_BIND_SLP:
		osc_slpSyncBlock(osc, over, ratioQ16, depthQ30, buf, size);
		break;
	case OSC_BIND_WTB8:
	case OSC_BIND_WTB16:
		osc_wtbSyncBlendBlock(osc, over, ratioQ16, depthQ30, buf, size);
		break;
	default:
		osc_zeroBlock(buf, size);
		break;
	}
}

void OSC_S087_SECTION osc_renderTypedRing(OscInfo* osc, const int16_t* modBuf,
                                          int16_t* buf, uint8_t size,
                                          int32_t depthQ15,
                                          int32_t depthStepQ15)
{
	switch (osc->sampleCacheLooped) {
	case OSC_BIND_SMP:
		osc_smpRingBlock(osc, modBuf, buf, size, depthQ15, depthStepQ15);
		break;
	case OSC_BIND_SLP:
		osc_slpRingBlock(osc, modBuf, buf, size, depthQ15, depthStepQ15);
		break;
	case OSC_BIND_WTB8:
	case OSC_BIND_WTB16:
		osc_wtbRingBlendBlock(osc, modBuf, buf, size, depthQ15, depthStepQ15);
		break;
	default:
		osc_zeroBlock(buf, size);
		break;
	}
}

/*
 * Modulator list render (S087 stage D; see the header).
 *
 * What:       plain sample loops for an s/l entry; silence for NONE.
 * Why:        a modulator is never an FM carrier, ring carrier or sync slave.
 * Inputs:     osc (MODLIST), buf, size, gain.
 * Outputs:    buf; phase.
 * Accessors:  calcNextOscSampleBlock() default branch (D-23).
 * Affiliates: osc_userBind().
 */
void OSC_S087_SECTION osc_renderUserList(OscInfo* osc, int16_t* buf,
                                         uint8_t size, float gain)
{
	if (osc->sampleCacheLooped == OSC_BIND_WTB8 ||
	    osc->sampleCacheLooped == OSC_BIND_WTB16) {
		/* S087 E/A24: a modulator w entry plays frame 0 and never claims. */
		osc->positionApplied = 0u;
		osc_wtbPlainBlock(osc, buf, size, gain);
		return;
	}
	osc_renderTyped(osc, buf, size, gain);
}

/*
 * Whether a modulator master resets the slave on its loop wrap (S087 D).
 *
 * What:       1 for an l-entry master (SLP); 0 for stock/s entries.
 * Why:        A12: the master's list entry picks the reset rule.
 * Inputs:     master.  Outputs: 0/1.
 * Accessors:  Drum snc.
 * Affiliates: osc_buildLoopSyncList().
 */
uint8_t osc_syncMasterIsLoop(const OscInfo* master)
{
	return (uint8_t)(master->oscType == OSC_TYPE_MODLIST &&
	                 master->sampleCacheLooped == OSC_BIND_SLP);
}

/*
 * Build a reset list from a looped sample master (S087 D).
 *
 * What:       per sample pos += inc; when pos passes loop_end, subtract the
 *             loop length and publish the overshoot from loop_start.
 * Why:        A12: an l master resets the slave each time its loop wraps.
 * Inputs:     master, pos (Q19.12), over[size], size.
 * Outputs:    over[]; the next position.
 * Accessors:  Drum snc master branch (D-38).
 * Affiliates: osc_syncMasterIsLoop().
 */
OSC_S087_LOOP uint32_t osc_buildLoopSyncList(OscInfo* master, uint32_t pos,
                                             uint32_t* over, uint8_t size)
{
	const sample_catalogue_entry_t *entry =
		sampleMemory_sampleEntry(master->sampleCacheIndex);
	const uint32_t ls = entry ? entry->loop_start : 0u;
	const uint32_t le = entry ? entry->loop_end : 0u;
	const uint32_t len = le - ls;
	uint32_t inc = master->phaseInc;
	const uint32_t lenPh = len << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopEndPh = le << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopStartPh = ls << OSC_SAMPLE_PHASE_SHIFT;
	uint8_t i;

	if (len == 0u) {
		for (i = 0u; i < size; i++)
			over[i] = OSC_SYNC_NONE;
		return pos;
	}
	if (inc >= lenPh)
		inc %= lenPh;
	for (i = 0u; i < size; i++) {
		pos += inc;
		if (pos >= loopEndPh) {
			pos -= lenPh;
			over[i] = pos - loopStartPh;
		} else {
			over[i] = OSC_SYNC_NONE;
		}
	}
	return pos;
}

/*
 * Wavetable frame helpers (S087 stage E).
 *
 * What:       one frame's base pointer and one interpolated point at a cycle
 *             phase (2^32 per frame, any length, A16).
 * Why:        the one-frame and blend copies share the exact read; 8-bit data
 *             is scaled by 256 so both depths land in int16 units.
 * Inputs:     data, bits, frame_len, phase.
 * Outputs:    the interpolated value.
 * Accessors:  the wtb loops below.
 * Affiliates: SampleMemory.h wavetable entry.
 */
static inline __attribute__((always_inline)) float osc_wtbRead(
	const uint8_t *data, uint8_t bits, uint32_t len, uint32_t ph)
{
	const uint64_t m = (uint64_t)ph * len;
	const uint32_t idx = (uint32_t)(m >> 32);
	const uint32_t n = (idx + 1u == len) ? 0u : idx + 1u;
	const float fr = (float)(uint32_t)m * (1.0f / 4294967296.0f);
	int32_t a;
	int32_t b;

	if (bits == 8u) {
		a = ((const int8_t *)data)[idx] << 8;
		b = ((const int8_t *)data)[n] << 8;
	} else {
		a = ((const int16_t *)data)[idx];
		b = ((const int16_t *)data)[n];
	}
	return (float)a + fr * (float)(b - a);
}

static const uint8_t *osc_wtbFrame(const OscInfo* osc,
                                   const wavetable_catalogue_entry_t *wt,
                                   uint32_t frame)
{
	const uint32_t bytes = (uint32_t)wt->frame_len * (wt->bits / 8u);

	return (const uint8_t *)osc->sampleCacheData + frame * bytes;
}

/* Nearest frame for one pos value (Q9 fallback). */
static uint32_t osc_wtbFrameOf(uint8_t position, uint32_t count)
{
	if (count <= 1u)
		return 0u;
	return ((uint32_t)position * (count - 1u) + 63u) / 127u;
}

/*
 * Wavetable plain copies (S087 stage E).
 *
 * What:       one frame, or two frames blended with ff ramping from the
 *             block's start to its end frame fraction.
 * Why:        A20: pos interpolates; wav steps.
 * Accessors:  osc_renderTyped().
 */
static OSC_S087_LOOP void osc_wtbPlainBlock(OscInfo* osc, int16_t* buf,
                                            uint8_t size, float gain)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	uint8_t i;

	if (!wt || !osc->sampleCacheData) {
		osc_zeroBlock(buf, size);
		return;
	}
	{
		const uint32_t count = wt->frame_count;
		const uint32_t frame = osc_wtbFrameOf(osc->positionApplied, count);
		const uint8_t *base = osc_wtbFrame(osc, wt, frame);

		for (i = 0u; i < size; i++) {
			buf[i] = (int16_t)(osc_wtbRead(base, wt->bits, wt->frame_len, ph) *
			                   gain);
			ph += inc;
		}
	}
	osc->phase = ph;
}

static OSC_S087_LOOP void osc_wtbBlendBlock(OscInfo* osc, int16_t* buf,
                                            uint8_t size, float gain)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	const uint32_t count = wt ? wt->frame_count : 0u;
	uint32_t fe;
	uint32_t fs;
	uint32_t frame;
	const uint8_t *a;
	const uint8_t *b;
	float ff;
	float ffStep;
	uint8_t i;

	if (!wt || !osc->sampleCacheData || count < 2u ||
	    !modNode_interpClaim(osc)) {
		osc_wtbPlainBlock(osc, buf, size, gain);
		return;
	}
	fe = osc_wtbFrameOf(osc->positionApplied, count);
	fs = osc_wtbFrameOf((uint8_t)((osc->positionApplied -
	                               (uint8_t)osc->positionBendStep) & 127u), count);
	frame = fs < fe ? fs : fe;
	if (frame + 1u >= count)
		frame = count - 2u;
	a = osc_wtbFrame(osc, wt, frame);
	b = osc_wtbFrame(osc, wt, frame + 1u);
	ff = (float)(fs - frame);
	ffStep = size ? ((float)((int32_t)fe - (int32_t)fs) / (float)size) : 0.0f;
	for (i = 0u; i < size; i++) {
		const float x0 = osc_wtbRead(a, wt->bits, wt->frame_len, ph);
		const float x1 = osc_wtbRead(b, wt->bits, wt->frame_len, ph);
		const float out = x0 + ff * (x1 - x0);

		buf[i] = (int16_t)(out * gain);
		ff += ffStep;
		ph += inc;
	}
	osc->phase = ph;
}

/*
 * Wavetable FM copies (S087 stage E).
 *
 * What:       the read phase gets (ssat(mod x fmMod) << 19), the same fraction
 *             of a cycle as the table FM.
 * Why:        A17 addendum: amt means the same on every type.
 * Accessors:  osc_renderTypedFm().
 */
static OSC_S087_LOOP void osc_wtbFmBlock(OscInfo* osc, const int16_t* mod,
                                         int16_t* buf, uint8_t size, float gain)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	uint8_t i;

	if (!wt || !osc->sampleCacheData) {
		osc_zeroBlock(buf, size);
		return;
	}
	{
		const uint32_t frame = osc_wtbFrameOf(osc->positionApplied,
		                                      wt->frame_count);
		const uint8_t *base = osc_wtbFrame(osc, wt, frame);

		for (i = 0u; i < size; i++) {
			const uint32_t rp = ph + ((uint32_t)__SSAT(
				(int32_t)((float)mod[i] * osc->fmMod), 16) << 19);

			buf[i] = (int16_t)(osc_wtbRead(base, wt->bits, wt->frame_len, rp) *
			                   gain);
			ph += inc;
		}
	}
	osc->phase = ph;
}

static OSC_S087_LOOP void osc_wtbFmBlendBlock(OscInfo* osc,
                                              const int16_t* mod,
                                              int16_t* buf, uint8_t size,
                                              float gain)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	const uint32_t count = wt ? wt->frame_count : 0u;
	uint32_t fe;
	uint32_t fs;
	uint32_t frame;
	const uint8_t *a;
	const uint8_t *b;
	float ff;
	float ffStep;
	uint8_t i;

	if (!wt || !osc->sampleCacheData || count < 2u ||
	    !modNode_interpClaim(osc)) {
		osc_wtbFmBlock(osc, mod, buf, size, gain);
		return;
	}
	fe = osc_wtbFrameOf(osc->positionApplied, count);
	fs = osc_wtbFrameOf((uint8_t)((osc->positionApplied -
	                               (uint8_t)osc->positionBendStep) & 127u), count);
	frame = fs < fe ? fs : fe;
	if (frame + 1u >= count)
		frame = count - 2u;
	a = osc_wtbFrame(osc, wt, frame);
	b = osc_wtbFrame(osc, wt, frame + 1u);
	ff = (float)(fs - frame);
	ffStep = size ? ((float)((int32_t)fe - (int32_t)fs) / (float)size) : 0.0f;
	for (i = 0u; i < size; i++) {
		const uint32_t rp = ph + ((uint32_t)__SSAT(
			(int32_t)((float)mod[i] * osc->fmMod), 16) << 19);
		const float x0 = osc_wtbRead(a, wt->bits, wt->frame_len, rp);
		const float x1 = osc_wtbRead(b, wt->bits, wt->frame_len, rp);
		const float out = x0 + ff * (x1 - x0);

		buf[i] = (int16_t)(out * gain);
		ff += ffStep;
		ph += inc;
	}
	osc->phase = ph;
}

/*
 * Wavetable ring copies (S087 stage E).
 *
 * What:       the plain read with the ring law and depth ramp.
 * Accessors:  osc_renderTypedRing().
 */
static OSC_S087_LOOP void osc_wtbRingBlock(OscInfo* osc, const int16_t* modBuf,
                                           int16_t* buf, uint8_t size,
                                           int32_t depthQ15,
                                           int32_t depthStepQ15)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	int32_t depth = depthQ15;
	uint8_t i;

	if (!wt || !osc->sampleCacheData) {
		osc_zeroBlock(buf, size);
		return;
	}
	{
		const uint32_t frame = osc_wtbFrameOf(osc->positionApplied,
		                                      wt->frame_count);
		const uint8_t *base = osc_wtbFrame(osc, wt, frame);

		for (i = 0u; i < size; i++) {
			const float out = osc_wtbRead(base, wt->bits, wt->frame_len, ph);

			buf[i] = osc_ringSample((int16_t)out, modBuf[i], depth);
			depth += depthStepQ15;
			ph += inc;
		}
	}
	osc->phase = ph;
}

static OSC_S087_LOOP void osc_wtbRingBlendBlock(OscInfo* osc,
                                                const int16_t* modBuf,
                                                int16_t* buf, uint8_t size,
                                                int32_t depthQ15,
                                                int32_t depthStepQ15)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	const uint32_t count = wt ? wt->frame_count : 0u;
	uint32_t fe;
	uint32_t fs;
	uint32_t frame;
	const uint8_t *a;
	const uint8_t *b;
	float ff;
	float ffStep;
	int32_t depth = depthQ15;
	uint8_t i;

	if (!wt || !osc->sampleCacheData || count < 2u ||
	    !modNode_interpClaim(osc)) {
		osc_wtbRingBlock(osc, modBuf, buf, size, depthQ15, depthStepQ15);
		return;
	}
	fe = osc_wtbFrameOf(osc->positionApplied, count);
	fs = osc_wtbFrameOf((uint8_t)((osc->positionApplied -
	                               (uint8_t)osc->positionBendStep) & 127u), count);
	frame = fs < fe ? fs : fe;
	if (frame + 1u >= count)
		frame = count - 2u;
	a = osc_wtbFrame(osc, wt, frame);
	b = osc_wtbFrame(osc, wt, frame + 1u);
	ff = (float)(fs - frame);
	ffStep = size ? ((float)((int32_t)fe - (int32_t)fs) / (float)size) : 0.0f;
	for (i = 0u; i < size; i++) {
		const float x0 = osc_wtbRead(a, wt->bits, wt->frame_len, ph);
		const float x1 = osc_wtbRead(b, wt->bits, wt->frame_len, ph);
		const float out = x0 + ff * (x1 - x0);

		buf[i] = osc_ringSample((int16_t)out, modBuf[i], depth);
		depth += depthStepQ15;
		ff += ffStep;
		ph += inc;
	}
	osc->phase = ph;
}

/*
 * Wavetable sync copies (S087 stage E).
 *
 * What:       the plain read with osc_syncPull() on the cycle phase.
 * Accessors:  osc_renderTypedSync().
 */
static OSC_S087_LOOP void osc_wtbSyncBlock(OscInfo* osc, const uint32_t* over,
                                           uint32_t ratioQ16, int32_t depthQ30,
                                           int16_t* buf, uint8_t size)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	uint8_t i;

	if (!wt || !osc->sampleCacheData) {
		osc_zeroBlock(buf, size);
		return;
	}
	{
		const uint32_t frame = osc_wtbFrameOf(osc->positionApplied,
		                                      wt->frame_count);
		const uint8_t *base = osc_wtbFrame(osc, wt, frame);

		for (i = 0u; i < size; i++) {
			buf[i] = (int16_t)osc_wtbRead(base, wt->bits, wt->frame_len, ph);
			ph = osc_syncPull(ph + inc, over[i], ratioQ16, depthQ30);
		}
	}
	osc->phase = ph;
}

static OSC_S087_LOOP void osc_wtbSyncBlendBlock(OscInfo* osc,
                                                const uint32_t* over,
                                                uint32_t ratioQ16,
                                                int32_t depthQ30,
                                                int16_t* buf, uint8_t size)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	const uint32_t count = wt ? wt->frame_count : 0u;
	uint32_t fe;
	uint32_t fs;
	uint32_t frame;
	const uint8_t *a;
	const uint8_t *b;
	float ff;
	float ffStep;
	uint8_t i;

	if (!wt || !osc->sampleCacheData || count < 2u ||
	    !modNode_interpClaim(osc)) {
		osc_wtbSyncBlock(osc, over, ratioQ16, depthQ30, buf, size);
		return;
	}
	fe = osc_wtbFrameOf(osc->positionApplied, count);
	fs = osc_wtbFrameOf((uint8_t)((osc->positionApplied -
	                               (uint8_t)osc->positionBendStep) & 127u), count);
	frame = fs < fe ? fs : fe;
	if (frame + 1u >= count)
		frame = count - 2u;
	a = osc_wtbFrame(osc, wt, frame);
	b = osc_wtbFrame(osc, wt, frame + 1u);
	ff = (float)(fs - frame);
	ffStep = size ? ((float)((int32_t)fe - (int32_t)fs) / (float)size) : 0.0f;
	for (i = 0u; i < size; i++) {
		const float x0 = osc_wtbRead(a, wt->bits, wt->frame_len, ph);
		const float x1 = osc_wtbRead(b, wt->bits, wt->frame_len, ph);
		const float out = x0 + ff * (x1 - x0);

		buf[i] = (int16_t)out;
		ff += ffStep;
		ph = osc_syncPull(ph + inc, over[i], ratioQ16, depthQ30);
	}
	osc->phase = ph;
}
