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

		case NOISE:
			//TODO FM noise - needed since its tuned digi noise!
			calcNoiseBlock(osc,buf,size,gain);
			break;

		case CRASH:
			calcSampleOscFmBlock(osc,modBuffer,buf,size,gain);
			break;

		default:
			calcUserSampleOscFmBlock(osc,modBuffer,buf,size,gain);
			break;

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

		case NOISE:
			calcNoiseBlock(osc,buf,size, gain);
			break;

		case CRASH:
			calcSampleOscBlock(osc,buf,size, gain);
			break;

		default://sample playback
			if( osc->waveform - OSC_SAMPLE_START > sampleMemory_getNumSamples() ) return; // return if out of range

			calcUserSampleOscBlock(osc,buf,size, gain);
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
 * The ring law for one sample (S085 P1).
 *
 * What:       SSAT16((car x (ringK + mod)) >> 15). With mod = modulator x
 *             amt and ringK = (1 - amt) x 32768 this is car x (1 - amt) +
 *             car x modulator x amt (plan K5). The saturation catches the
 *             single full-scale case (-32768 x -32768 at amt 1).
 * Inputs:     car (the carrier sample), mod (the modulator block sample),
 *             ringK.  Outputs: the stored sample.
 * Accessors:  the ring copies below; tools/dsp_test osc_mod (R1).
 */
static inline __attribute__((always_inline)) int16_t osc_ringSample(
	const int16_t car, const int16_t mod, const int32_t ringK)
{
	return (int16_t)__SSAT(((int32_t)car * (ringK + (int32_t)mod)) >> 15, 16);
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
 * What:       1 for SINE, TRI, SAW, REC and CRASH; 0 for NOISE and user
 *             samples, which render unsynced in P1 (plan A3/A4).
 * Accessors:  calcPeriodicInterpSyncBlock() (the phase rule).
 * Affiliates: osc_renderSyncWave() (the same partition, as a switch).
 */
static inline uint8_t osc_syncWaveform(const uint8_t waveform)
{
	return (uint8_t)((waveform <= CRASH) && (waveform != NOISE));
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
	case NOISE:
		calcNoiseBlock(osc, buf, size, 1.f);
		break;
	case CRASH:
		calcSampleOscBlock(osc, buf, size, 1.f);
		break;
	default://sample playback
		if( osc->waveform - OSC_SAMPLE_START > sampleMemory_getNumSamples() ) return;

		calcUserSampleOscBlock(osc, buf, size, 1.f);
		break;
	}
}

/*
 * Sync clock increment for the FM-page oscillator (S085 P1).
 * Contract: Oscillator.h. Crash and user samples run in <<17 point units
 * (osc_calcSampleFreqValue: f/fs x 2^27), which is 1/32 of the 2^32-per-
 * period clock, hence << 5 after the clamp.
 */
uint32_t osc_syncClockInc(const OscInfo* master)
{
	uint32_t inc = master->phaseInc;

	if (master->waveform >= CRASH) {
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
 *             and user samples render through calcNextOscSampleBlock() at
 *             gain 1, unsynced (plan A3/A4). That call repeats its own
 *             interpolation check, which is false here: the caller already
 *             took the interpolation path, or the partner copy has
 *             interpolation cleared.
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
	case CRASH:
		calcSampleOscSyncBlock(osc, over, ratioQ16, depthQ30, buf, size);
		break;
	default:	/* NOISE and user samples: unsynced in P1 */
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
 * Placement:  no INITCM attribute, like calcPeriodicInterpBlock(); the
 *             compiler inlines it into its ITCM dispatcher, as it does
 *             calcPeriodicInterpBlock() into calcNextOscSampleBlock().
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
 *             osc_ringSample(oscOut, modBuf[i], ringK).
 * Inputs:     osc, modBuf[size], buf[size], size, ringK.
 * Outputs:    buf; osc->phase.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcSineBlock(); tools/dsp_test osc_mod R1.
 */
static OSC_S085_LOOP void calcSineRingBlock(OscInfo* osc, const int16_t* modBuf,
                                            int16_t* buf, const uint8_t size,
                                            const int32_t ringK)
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

		osc->phase = oscPhase + osc->phaseInc;

		buf[i] = osc_ringSample(oscOut, modBuf[i], ringK);
	}
}

/*
 * Tri/Saw/Rec carrier with the ring law (S085 P1).
 *
 * What:       calcWavetableOscBlock() with the store replaced by
 *             osc_ringSample(oscOut, modBuf[i], ringK).
 * Inputs:     osc, table, modBuf[size], buf[size], size, ringK.
 * Outputs:    buf; osc->phase.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcWavetableOscBlock(); tools/dsp_test osc_mod R1.
 */
static OSC_S085_LOOP void calcWavetableOscRingBlock(OscInfo* osc,
                                                    const int16_t table[][1024],
                                                    const int16_t* modBuf,
                                                    int16_t* buf,
                                                    const uint8_t size,
                                                    const int32_t ringK)
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

		osc->phase = oscPhase + osc->phaseInc;
		buf[i] = osc_ringSample(oscOut, modBuf[i], ringK);
	}
}

/*
 * Noise carrier with the ring law (S085 P1).
 *
 * What:       calcNoiseBlock(): a new hardware-RNG value on each phase wrap,
 *             held between wraps; the store is replaced by
 *             osc_ringSample(osc->output, modBuf[i], ringK).
 * Inputs:     osc, modBuf[size], buf[size], size, ringK.
 * Outputs:    buf; osc->phase; osc->output.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcNoiseBlock(); GetRngValue() (random.c).
 */
static OSC_S085_LOOP void calcNoiseRingBlock(OscInfo* osc, const int16_t* modBuf,
                                             int16_t* buf, const uint8_t size,
                                             const int32_t ringK)
{
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

		buf[i] = osc_ringSample(osc->output, modBuf[i], ringK);
	}
}

/*
 * Crash carrier with the ring law (S085 P1).
 *
 * What:       calcSampleOscBlock() with the store replaced by
 *             osc_ringSample(oscOut, modBuf[i], ringK).
 * Inputs:     osc, modBuf[size], buf[size], size, ringK.
 * Outputs:    buf; osc->phase.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcSampleOscBlock().
 */
static OSC_S085_LOOP void calcSampleOscRingBlock(OscInfo* osc, const int16_t* modBuf,
                                                 int16_t* buf, const uint8_t size,
                                                 const int32_t ringK)
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

		osc->phase = oscPhase + osc->phaseInc;
		buf[i] = osc_ringSample(oscOut, modBuf[i], ringK);
	}
}

/*
 * User-sample carrier with the ring law (S085 P1).
 *
 * What:       calcUserSampleOscBlock() (the sample cache, one-shot hold,
 *             looped wrap, interpolation across the loop seam) with the
 *             store replaced by osc_ringSample(oscOut, modBuf[i], ringK).
 *             Like the plain loop, an empty sample returns without writing
 *             buf (unchanged behaviour, kept for parity).
 * Inputs:     osc (waveform >= OSC_SAMPLE_START), modBuf[size], buf[size],
 *             size, ringK.
 * Outputs:    buf; osc->phase; osc sample cache.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcUserSampleOscBlock(); osc_updateSampleCache();
 *             SampleMemory.
 */
static OSC_S085_LOOP void calcUserSampleOscRingBlock(OscInfo* osc,
                                                     const int16_t* modBuf,
                                                     int16_t* buf,
                                                     const uint8_t size,
                                                     const int32_t ringK)
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
		buf[i] = osc_ringSample(oscOut, modBuf[i], ringK);
	}
}

/*
 * Waveform-interpolated carrier with the ring law (S085 P1).
 *
 * What:       calcPeriodicInterpBlock(): both partners rendered plain at
 *             gain 1 into the DTCM scratch, then blended. The blend's store
 *             is replaced by osc_ringSample(blend, modBuf[i], ringK). The
 *             phase follows the plain rule (start + phaseInc x size).
 * Placement:  no INITCM attribute, like calcPeriodicInterpBlock(); the
 *             compiler inlines it into its ITCM dispatcher, as it does
 *             calcPeriodicInterpBlock() into calcNextOscSampleBlock().
 * Inputs:     osc (interpolation active), modBuf[size], buf[size], size,
 *             ringK.
 * Outputs:    buf; osc->phase.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcPeriodicInterpBlock(); OSC_WAVE_INTERP_MAX_ACTIVE.
 */
static OSC_S085_SECTION void calcPeriodicInterpRingBlock(OscInfo* osc,
                                                         const int16_t* modBuf,
                                                         int16_t* buf,
                                                         const uint8_t size,
                                                         const int32_t ringK)
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

	osc_renderPlainWave(&oscA, osc_interp_a, size);
	osc_renderPlainWave(&oscB, osc_interp_b, size);

	for (i = 0; i < size; i++) {
		const int16_t a = osc_interp_a[i];
		const int16_t b = osc_interp_b[i];
		const int16_t out = (int16_t)(a + frac * (b - a));
		buf[i] = osc_ringSample(out, modBuf[i], ringK);
	}
	osc->phase = startPhase + (osc->phaseInc * (uint32_t)size);
}

/*
 * Render one carrier block with the ring law (S085 P1).
 * Contract: Oscillator.h. It mirrors calcNextOscSampleBlock(), including the
 * user-sample range check that returns without writing buf.
 */
OSC_S085_DISPATCH void calcNextOscSampleRingBlock(OscInfo* osc, const int16_t* modBuf,
                                                  int16_t* buf, const uint8_t size,
                                                  const int32_t ringK)
{
	if (osc_waveInterpActive(osc)) {
		calcPeriodicInterpRingBlock(osc, modBuf, buf, size, ringK);
		return;
	}

	switch(osc->waveform)
	{
	case SINE:
		calcSineRingBlock(osc, modBuf, buf, size, ringK);
		break;
	case SAW:
		calcWavetableOscRingBlock(osc, sawTable, modBuf, buf, size, ringK);
		break;
	case TRI:
		calcWavetableOscRingBlock(osc, triTable, modBuf, buf, size, ringK);
		break;
	case REC:
		calcWavetableOscRingBlock(osc, recTable, modBuf, buf, size, ringK);
		break;
	case NOISE:
		calcNoiseRingBlock(osc, modBuf, buf, size, ringK);
		break;
	case CRASH:
		calcSampleOscRingBlock(osc, modBuf, buf, size, ringK);
		break;
	default://sample playback
		if( osc->waveform - OSC_SAMPLE_START > sampleMemory_getNumSamples() ) return; // return if out of range

		calcUserSampleOscRingBlock(osc, modBuf, buf, size, ringK);
		break;
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
