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
 *             their waveform switches select per-sample loops, and the cost
 *             gate assumes ITCM placement for them (S087 P1: the interpolated
 *             variants are retired, A7).
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
 * Accessors:  osc_renderSyncWave() (NOISE: unsynced).
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
 * One waveform's sync render (S085 P1; S087 P1).
 *
 * What:       dispatches Sin/Tri/Saw/Rec/Crash and the stock table waves to
 *             their sync copies. NOISE renders unsynced through
 *             osc_renderPlainWave() (the leaf loops at gain 1, plan A3/A4).
 * Why:        the slave partition is a switch, not a predicate: S087 P1
 *             retires the waveform-interpolation partner path (A7), so no
 *             check or partner copy remains here.
 * Inputs:     osc, over[size], ratioQ16, depthQ30, buf[size], size.
 * Outputs:    buf; osc state.
 * Accessors:  calcNextOscSampleSyncBlock().
 * Affiliates: osc_renderPlainWave() (the NOISE fallback).
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
 * Render one slave block under hard sync (S085 P1).
 * Contract: Oscillator.h. Order: ratio (one VDIV), then the waveform switch
 * (S087 P1: the waveform-interpolation entry is retired, A7).
 */
OSC_S085_DISPATCH void calcNextOscSampleSyncBlock(OscInfo* osc, const uint32_t* over,
                                                 const uint32_t clockInc,
                                                 const int32_t depthQ30,
                                                 int16_t* buf, const uint8_t size)
{
	const uint32_t ratioQ16 = osc_syncRatioQ16(osc->phaseInc, clockInc);

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
 * Render one carrier block with the ring law (S085 P1).
 * Contract: Oscillator.h. It mirrors calcNextOscSampleBlock()'s waveform
 * switch (S087 P1: no interpolation entry, no user samples). Every ring copy
 * uses d_i = depthQ15 + (i + 1) x depthStepQ15 for sample i, so the block
 * ends at depthQ15 + size x depthStepQ15 (the caller's next start value).
 */
OSC_S085_DISPATCH void calcNextOscSampleRingBlock(OscInfo* osc, const int16_t* modBuf,
                                                  int16_t* buf, const uint8_t size,
                                                  const int32_t depthQ15,
                                                  const int32_t depthStepQ15)
{
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
		/*
		 * Modulator list clamp (S087 P1).
		 *
		 * What:       a modulator wav value past the end of its flat list (stock,
		 *             s, l, w; osc_userListCount()) is clamped to the last
		 *             entry.
		 * Why:        S087 P0 r8 / A23: the value is used as it is, clamped to an
		 *             entry that exists; a value past the list bound no item and
		 *             the modulator was silent.
		 * Inputs:     oscType, waveform, the catalogue counts.
		 * Outputs:    waveform.
		 * Affiliates: osc_userListCount(); osc_userBind().
		 */
		if (osc->oscType == OSC_TYPE_MODLIST) {
			const uint8_t last = (uint8_t)(osc_userListCount() - 1u);

			if (osc->waveform > last)
				osc->waveform = last;
		}
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

		/*
		 * S087 P1: user content never reaches this switch (osc_userBind()
		 * above sets its increment); the default only guards an impossible
		 * value with the table law.
		 */
		default:
			osc_calcWavetableFreqValue(osc, currentFreq);
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
	/*
	 * Release a held interpolation slot (S087 P1).
	 *
	 * What:       clears interpStamp, so the oscillator is no longer counted as
	 *             last block's slot holder.
	 * Why:        S087 P0 impl. §11 F17 (the header promised it); a stale stamp
	 *             could take a reservation that belongs to a real holder.
	 * Affiliates: modNode_interpClaim(); OscInfo.interpStamp.
	 */
	osc->interpStamp = 0u;
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
 * Bind the oscillator's catalogue item for this block (S087 P1; S087 stage
 * D/E; see the header).
 *
 * What:       resolves (kind, index) from oscType (osc1: smp/slp/wtb, the index
 *             clamped to the last item) or from the modulator's flat list
 *             (stock, s, l, w). A wavetable's kind is resolved to WTB8 or WTB16
 *             from its entry BEFORE the comparison with the stored bind. When
 *             kind, index or the catalogue generation differ, re-reads the item
 *             (base address, frames or frame length) and re-anchors (D7):
 *             positionApplied = position; a smp phase past the new end is
 *             clamped to the end; a slp phase past the loop end is folded into
 *             the loop once. Every block: phaseInc = the cycle law (2^32 per
 *             cycle, freq2PhaseIncr()) for a wavetable bind or a wtb osc1, else
 *             the sample law f x 2^22 / REAL_FS (Q19.12).
 * Why:        one bind for osc1 and the modulators (S087 P0 §6.3, D7, A23, A24).
 *             impl. §11 F9: an 8-bit table compared its stored WTB8 with a
 *             provisional WTB16 and rebound every block (no frame glide).
 *             impl. §11 F7: a modulator w entry (oscType MODLIST) got the
 *             sample law, 1,024 x too slow; its sync clock was wrong likewise.
 * Inputs:     osc, the catalogue.  Outputs: bind fields, phaseInc,
 *             positionApplied/phase on a change.
 * Accessors:  osc_setFreq(), osc_setType(), osc_typedTriggerPhase().
 * Affiliates: SampleMemory.h; osc_userCount()/osc_userListCount();
 *             osc_syncClockInc() (returns a wavetable master's phaseInc as is).
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

	/* F9: the stored kind of a wavetable is its entry's depth. */
	if (kind == OSC_BIND_WTB16) {
		const wavetable_catalogue_entry_t *w =
			sampleMemory_wavetableEntry(index);

		if (!w) {
			kind = OSC_BIND_NONE;
			index = 0u;
		} else if (w->bits == 8u) {
			kind = OSC_BIND_WTB8;
		}
	}

	if (kind != osc->sampleCacheLooped ||
	    index != osc->sampleCacheIndex ||
	    generation != osc->sampleCacheGeneration) {
		const sample_catalogue_entry_t *entry = (kind == OSC_BIND_SMP ||
		                                        kind == OSC_BIND_SLP)
			? sampleMemory_sampleEntry(index)
			: (const sample_catalogue_entry_t *)0;
		const wavetable_catalogue_entry_t *wentry = (kind == OSC_BIND_WTB8 ||
		                                             kind == OSC_BIND_WTB16)
			? sampleMemory_wavetableEntry(index)
			: (const wavetable_catalogue_entry_t *)0;

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
	/* F7: the increment law follows what is bound, not the oscillator role. */
	osc->phaseInc = (kind == OSC_BIND_WTB8 || kind == OSC_BIND_WTB16 ||
	                 osc->oscType == OSC_TYPE_WTB)
		? freq2PhaseIncr(currentFreq)
		: (uint32_t)((currentFreq * 4194304.0f) / REAL_FS);
}

/*
 * Restart a modulator's one-shot sample on a trigger (S087 P3; see the
 * header).
 *
 * What:       stock-wave modulators return at once (no change to today's
 *             trigger). A user entry is bound by osc_setFreq() first, so an s
 *             entry selected on this hit's own step (automation drains before
 *             the trigger) is the one that restarts; then phase 0 for s.
 * Why:        a one-shot reaches its end pad and holds it; without a reset a
 *             never-reset modulator stayed silent after its first hit.
 * Inputs:     osc (modulator).  Outputs: bind, phaseInc; phase for s.
 * Accessors:  Drum_trigger(), HiHat_trigger().
 * Affiliates: osc_setFreq(), osc_userBind().
 */
void osc_modulatorTrigger(OscInfo* osc)
{
	if (osc->oscType != OSC_TYPE_MODLIST || osc->waveform < OSC_SAMPLE_START)
		return;
	osc_setFreq(osc);
	if (osc->sampleCacheLooped == OSC_BIND_SMP)
		osc->phase = 0u;
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
 * S087 P1: binds first, so a sample changed on the trigger's own step
 *           starts correctly.
 */
uint32_t osc_typedTriggerPhase(OscInfo* osc)
{
	const uint32_t pos = (uint32_t)(osc->position & 127u);
	uint32_t start;

	osc->positionApplied = osc->position;
	if (!osc_isTyped(osc))
		return 0u;
	/*
	 * Bind before the start is computed (S087 P1).
	 *
	 * What:       re-reads the bound item when the wav value, the type or the
	 *             catalogue changed since the last block.
	 * Why:        S087 P0 impl. §11 F10: a step's own automation (a new sample)
	 *             is applied just before its trigger (stage A), but the bind
	 *             otherwise follows only in the next block's osc_setFreq(); the
	 *             pos start and the loop clamp below used the previous item's
	 *             length, and a shorter one-shot started past its end (silence).
	 * Affiliates: osc_userBind(); voiceControl_processPending() (stage A order).
	 */
	osc_userBind(osc);
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
 * Typed pos glide for one block (S087 P1; S087 stage D/E).
 *
 * What:       wtb: d = position - positionApplied, linear, capped so the
 *             block moves at most one frame (|d| <= 127 / (count - 1)); the
 *             frame position moves, phaseInc never bends.
 *             smp/slp: d = position - positionApplied (linear, D3), 0 once a
 *             one-shot has ended (D4, no revival), capped to the D5 speed limit
 *             (|d| x frames <= OSC_POS_SAMPLE_SPEED_CAP x phaseInc, at least 1).
 *             A backward step is further limited while the playhead is outside
 *             a loop (smp, or slp still in its lead-in) so the block's last
 *             phase stays >= 0: phase + size x (phaseInc + d x frames) >= 0.
 *             positionApplied += d; when `bend`, phaseInc += d x frames and
 *             positionBendStep = d, else positionBendStep = 0 (Drum snc).
 * Why:        S087 P0 D1/D3/D4/D5; impl. §11 F16: the old limit (points / 32)
 *             ignored the sample length. Inside a loop a backward glide is a
 *             forward step modulo the loop (osc_slpBlockInc()), so no limit
 *             applies there.
 * Inputs:     osc (bound), bend.
 * Outputs:    phaseInc, positionApplied, positionBendStep.
 * Accessors:  osc_positionBendApply() (typed branch).
 * Affiliates: osc_typedPositionRestore(); OSC_POS_SAMPLE_SPEED_CAP;
 *             osc_wtbPlanBlock() (reads the wtb step); OUTPUT_DMA_SIZE.
 */
void osc_typedPositionApply(OscInfo* osc, uint8_t bend)
{
	const uint32_t frames = osc->sampleCacheSize;
	int32_t d;
	int32_t maxStep;

	if (osc->oscType == OSC_TYPE_WTB) {
		const wavetable_catalogue_entry_t *wt =
			sampleMemory_wavetableEntry(osc->sampleCacheIndex);
		const int32_t count = wt ? (int32_t)wt->frame_count : 0;

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
	if (d < 0) {
		const sample_catalogue_entry_t *entry =
			(osc->oscType == OSC_TYPE_SLP)
			? sampleMemory_sampleEntry(osc->sampleCacheIndex)
			: (const sample_catalogue_entry_t *)0;
		const uint32_t loopStartPh = entry
			? (entry->loop_start << OSC_SAMPLE_PHASE_SHIFT) : 0u;

		if (osc->oscType == OSC_TYPE_SMP || osc->phase < loopStartPh) {
			const int64_t room = (int64_t)osc->phase +
				(int64_t)OUTPUT_DMA_SIZE * (int64_t)osc->phaseInc;
			const int64_t perStep = (int64_t)OUTPUT_DMA_SIZE * (int64_t)frames;
			const int32_t minD = (room > 0) ? (int32_t)(-(room / perStep)) : 0;

			if (d < minD)
				d = minD;
		}
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
 * Per-block step of a looped sample (S087 P1).
 *
 * What:       the block's phase step (phaseInc, possibly bent negative by a pos
 *             glide), returned as the unsigned value the slp copies add each
 *             sample:
 *             - forward: reduced modulo the loop length when it is not shorter
 *               than the loop (A5);
 *             - backward, with the block starting inside the loop: the equal
 *               forward step modulo the loop, loop length - (magnitude modulo the
 *               loop length), so the single upward wrap at loop_end stays exact;
 *             - backward, with the block starting in the lead-in: the signed
 *               step unchanged; osc_typedPositionApply() keeps the block's last
 *               phase >= 0, and a backward step never reaches loop_end.
 * Why:        S087 P0 A2/A5 (pos moves the playhead, which wraps into the fixed
 *             loop); impl. §11 F5: the unsigned modulo of a negative step gave
 *             an arbitrary forward step. Converting once per block keeps the
 *             per-sample loop unchanged (no second wrap).
 * Inputs:     phaseInc (bent for this block), phase (block start),
 *             loopStartPh, lenPh (Q19.12, lenPh > 0).
 * Outputs:    the step to add per sample (two's complement when backward in the
 *             lead-in).
 * Accessors:  osc_slpPlainBlock(), osc_slpFmBlock(), osc_slpRingBlock().
 * Affiliates: osc_typedPositionApply() (the bend and the lead-in limit);
 *             SAMPLE_MAX_FRAMES (every step and phase fit int32).
 */
static inline uint32_t osc_slpBlockInc(uint32_t phaseInc, uint32_t phase,
                                       uint32_t loopStartPh, uint32_t lenPh)
{
	const int32_t inc = (int32_t)phaseInc;
	uint32_t mag;

	if (inc >= 0)
		return (phaseInc >= lenPh) ? (phaseInc % lenPh) : phaseInc;
	if (phase < loopStartPh)
		return phaseInc;
	mag = (uint32_t)(-(int64_t)inc) % lenPh;
	return (mag == 0u) ? 0u : (lenPh - mag);
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
 * slp plain: one looped block (S087 P1; S087 stage D).
 *
 * What:       Q19.12 linear read of the bound item with a fixed loop region:
 *             the seam reads loop_start (A6); the phase takes the per-block
 *             step from osc_slpBlockInc() and wraps once at loop_end.
 * Why:        S087 P0 D-12, A5, A6; impl. §11 F5: a backward pos glide made the
 *             block increment negative and the unsigned modulo turned it into an
 *             arbitrary forward step. osc_slpBlockInc() now returns a backward
 *             step inside the loop as the equal forward step modulo the loop
 *             (and the signed step in the lead-in), so the single upward wrap
 *             stays correct.
 * Inputs:     osc (bound slp), buf, size, gain.
 * Outputs:    buf; osc->phase.
 * Cost:       per sample as the S087 copy before.
 * Accessors:  osc_renderTyped(), osc_renderUserList().
 * Affiliates: osc_slpBlockInc(), osc_userBind(), osc_typedPositionApply().
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
	const uint32_t lenPh = len << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopEndPh = le << OSC_SAMPLE_PHASE_SHIFT;
	const float g = gain * osc_bindGain(osc);
	uint32_t ph = osc->phase;
	uint32_t inc;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u || len == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	inc = osc_slpBlockInc(osc->phaseInc, ph, ls << OSC_SAMPLE_PHASE_SHIFT,
	                      lenPh);
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
 * Affiliates: calcFmBlock(); test_osc_sample.c; osc_smpFmRampBlock() (the
 *             S089 P3 Drum twin with a per-sample depth ramp: a change to
 *             this loop is made to the twin too).
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
 * slp FM carrier: one looped block (S087 P1; S087 stage D).
 *
 * What:       the read position is the phase + (ssat(mod x fmMod) << 9).
 *             The block's read rule is chosen once, from where the block
 *             starts:
 *             - inside the loop (phase >= loop_start): the offset is bounded to
 *               half a loop and the read, relative to loop_start, wraps once
 *               each way; the index is loop_start + that relative point;
 *             - in the lead-in (phase < loop_start): the absolute read is
 *               clamped to [0, loop_end - 1].
 *             The seam reads loop_start. The phase takes the per-block step from
 *             osc_slpBlockInc() and wraps once at loop_end.
 * Why:        S087 P0 D-14, A5, A6; impl. §11 F6: the read was relative to
 *             loop_start but indexed data[] as absolute, so a smpl loop that
 *             does not start at 0 played the wrong region, and the lead-in read
 *             froze at loop_start; impl. §11 F5 (backward glides).
 * Inputs:     osc (bound slp), mod[size], buf, size, gain.
 * Outputs:    buf; osc->phase.
 * Edge:       a block that starts in the lead-in and enters the loop keeps
 *             the lead-in read rule (clamp) for its remaining samples.
 * Accessors:  osc_renderTypedFm().
 * Affiliates: osc_slpBlockInc(), osc_slpPlainBlock(), osc_smpFmBlock() (the
 *             same FM scale, A17 addendum); osc_slpFmRampBlock() (the S089
 *             P3 Drum twin with a per-sample depth ramp: a change to this
 *             loop is made to the twin too).
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
	const uint32_t lenPh = len << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopStartPh = ls << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopEndPh = le << OSC_SAMPLE_PHASE_SHIFT;
	const float g = gain * osc_bindGain(osc);
	uint32_t ph = osc->phase;
	uint32_t inc;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u || len == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	inc = osc_slpBlockInc(osc->phaseInc, ph, loopStartPh, lenPh);
	if (ph >= loopStartPh) {
		const int32_t loopLen = (int32_t)lenPh;
		const int32_t half = loopLen >> 1;

		for (i = 0u; i < size; i++) {
			int32_t off = (int32_t)__SSAT(
				(int32_t)((float)mod[i] * osc->fmMod), 16) << OSC_FM_SAMPLE_SHIFT;
			int32_t rp;
			uint32_t idx;
			uint32_t next;
			int16_t a;
			int16_t b;
			float out;

			off = (off > half) ? half : off;
			off = (off < -half) ? -half : off;
			rp = (int32_t)(ph - loopStartPh) + off;
			rp = (rp < 0) ? rp + loopLen : rp;
			rp = (rp >= loopLen) ? rp - loopLen : rp;
			idx = ls + ((uint32_t)rp >> OSC_SAMPLE_PHASE_SHIFT);
			next = (idx + 1u == le) ? ls : idx + 1u;
			a = data[idx];
			b = data[next];
			out = (float)a + osc_frac12((uint32_t)rp) * (float)(b - a);
			buf[i] = (int16_t)(out * g);
			ph += inc;
			if (ph >= loopEndPh)
				ph -= lenPh;
		}
	} else {
		const int32_t lastPh = (int32_t)loopEndPh - 1;

		for (i = 0u; i < size; i++) {
			const int32_t off = (int32_t)__SSAT(
				(int32_t)((float)mod[i] * osc->fmMod), 16) << OSC_FM_SAMPLE_SHIFT;
			int32_t rp = (int32_t)ph + off;
			uint32_t idx;
			uint32_t next;
			int16_t a;
			int16_t b;
			float out;

			rp = (rp < 0) ? 0 : rp;
			rp = (rp > lastPh) ? lastPh : rp;
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
 * S087 P1: the depth steps before use (S085 ring law).
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

		/* S087 P1: S085 ring law, d_i = depthQ15 + (i + 1) x step. */
		depth += depthStepQ15;
		buf[i] = (int16_t)((float)osc_ringSample((int16_t)out, modBuf[i], depth) * g);
		ph = osc_smpReadPhase(ph + inc, endPh);
	}
	osc->phase = ph;
}

/*
 * slp ring carrier: one looped block (S087 P1; S087 stage D).
 *
 * What:       as osc_slpPlainBlock() (the per-block step from osc_slpBlockInc(),
 *             one upward wrap at loop_end, the seam read at loop_start), with
 *             the ring law d_i = depthQ15 + (i + 1) x depthStepQ15 and the item's
 *             gain code.
 * Why:        S087 P0 D-16, A6; impl. §11 F5 (a backward pos glide is a forward
 *             step modulo the loop, see osc_slpBlockInc()); the S085 ring law
 *             (Oscillator.h calcNextOscSampleRingBlock()).
 * Inputs:     osc (bound slp), modBuf[size], buf, size, depthQ15,
 *             depthStepQ15.
 * Outputs:    buf; osc->phase.
 * Cost:       per sample as the S087 copy before (the depth add moves).
 * Accessors:  osc_renderTypedRing().
 * Affiliates: osc_slpBlockInc(), osc_ringSample(), osc_slpPlainBlock().
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
	const uint32_t lenPh = len << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopEndPh = le << OSC_SAMPLE_PHASE_SHIFT;
	const float g = osc_bindGain(osc);
	uint32_t ph = osc->phase;
	uint32_t inc;
	int32_t depth = depthQ15;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u || len == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	inc = osc_slpBlockInc(osc->phaseInc, ph, ls << OSC_SAMPLE_PHASE_SHIFT,
	                      lenPh);
	for (i = 0u; i < size; i++) {
		const uint32_t idx = ph >> OSC_SAMPLE_PHASE_SHIFT;
		const uint32_t next = (idx + 1u == le) ? ls : idx + 1u;
		const int16_t a = data[idx];
		const int16_t b = data[next];
		const float out = (float)a + osc_frac12(ph) * (float)(b - a);

		depth += depthStepQ15;
		buf[i] = (int16_t)((float)osc_ringSample((int16_t)out, modBuf[i], depth) * g);
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
	/*
	 * Sync reset target (S087 P1; D9, A5).
	 *
	 * What:       the pos start point; a start at or past loop_end (a smpl loop
	 *             that ends before the file) is clamped to loop_start, as
	 *             osc_typedTriggerPhase() does at a trigger.
	 * Why:        S087 P0 impl. §11 F21: the reset and the trigger must start a
	 *             looped sample at the same point, inside its fixed loop.
	 * Affiliates: osc_typedTriggerPhase().
	 */
	const uint32_t posStartPh = (uint32_t)(
		(((uint32_t)(osc->position & 127u) * frames) >> 7u) <<
		OSC_SAMPLE_PHASE_SHIFT);
	const uint32_t startPh = (posStartPh >= loopEndPh)
		? (ls << OSC_SAMPLE_PHASE_SHIFT) : posStartPh;
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

/* S089 P3: osc_renderTypedFmRamp() (end of file) is this dispatcher's Drum
** twin with a per-sample FM depth ramp; keep their cases in step. Cymbal and
** HiHat keep calling this one (no ramp, no extra per-sample cost). */
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
 * itp crossfade scratch (S088 P0 §3 Layer 3c).
 *
 * What:       the old-carrier block, rendered plain and then blended into the
 *             live carrier block. One instance serves all four engines because
 *             the DSP render path is single-context.
 * Why:        the plan's I1 allows a small static buffer and forbids a VLA;
 *             one 64-byte block keeps the cost at +64 B .bss (SRAM1) instead
 *             of 64 B per engine. It is not DTCM and not in a DMA region.
 * Accessors:  oscSection_renderOldCarrier() only.
 * Affiliates: OscCarrierSnapshot; OSC_RUN_ITP_FADE_BLOCKS.
 */
static int16_t osc_itpScratch[OUTPUT_DMA_SIZE];

/*
 * oscSection_renderOldCarrier() - plain old-carrier render + itp blend
 * (S088 P0 §3 Layer 3c; see the header for the contract).
 *
 * What:       builds a temporary OscInfo view from the snapshot, renders it
 *             PLAIN into osc_itpScratch (typed smp/slp/wtb plain copies, or
 *             calcNextOscSampleBlock() for the stock/one-shot waveforms), then
 *             crossfades the live buf toward it:
 *                 t = (elapsed x OUTPUT_DMA_SIZE + i) / (BLOCKS x 32)
 *                 buf[i] = (1 - t) x old[i] + t x buf[i]
 *             then decrements the counter and releases the snapshot at 0.
 * Why:        the old carrier must keep its own phase and pitch while the new
 *             one starts; no modulator may touch it (carrier-only, Q14). A
 *             wtb snapshot plays its frozen pos frame through the plain copy,
 *             so it never claims an interpolation slot (the slot is already
 *             represented by the fade's reserve).
 * Inputs:     snap (active, counter 1..OSC_RUN_ITP_FADE_BLOCKS), buf (live
 *             carrier block), size (<= OUTPUT_DMA_SIZE).
 * Outputs:    buf blended; snap->phase advanced by the old carrier's own
 *             phaseInc x size; snap->counter decremented; active cleared at 0.
 * Cost:       one plain carrier loop (the voice's own loop class) plus about
 *             8 instructions per sample for the blend. No divide per sample
 *             (the progress is accumulated); one convert to float per block.
 * Accessors:  the four engines' calcSyncBlockVoice() when snapshot.active.
 * Affiliates: OscCarrierSnapshot; OSC_RUN_ITP_FADE_BLOCKS; osc_itpScratch;
 *             osc_smpPlainBlock()/osc_slpPlainBlock()/osc_wtbPlainBlock() /
 *             calcNextOscSampleBlock(); the engine triggers that fill it.
 */
void oscSection_renderOldCarrier(OscCarrierSnapshot *snap, int16_t *buf,
                                 uint8_t size)
{
	OscInfo view;
	uint8_t i;
	uint8_t elapsed;
	float total_samples;
	float t;
	const float step = 1.0f / (float)((uint16_t)OSC_RUN_ITP_FADE_BLOCKS *
	                                  OUTPUT_DMA_SIZE);

	if (!snap || !buf || !snap->active || snap->counter == 0u || size == 0u)
		return;
	/* Defensive: a corrupted counter must not underflow `elapsed` below. */
	if (snap->counter > OSC_RUN_ITP_FADE_BLOCKS)
		snap->counter = OSC_RUN_ITP_FADE_BLOCKS;

	/* The plain copies read only these OscInfo fields (and output for noise). */
	view.phase = snap->phase;
	view.phaseInc = snap->phaseInc;
	view.waveform = snap->waveform;
	view.tableOffset = snap->tableOffset;
	view.oscType = snap->oscType;
	view.sampleCacheIndex = snap->sampleCacheIndex;
	view.sampleCacheLooped = snap->sampleCacheLooped;
	view.sampleCacheGeneration = snap->sampleCacheGeneration;
	view.sampleCacheSize = snap->sampleCacheSize;
	view.sampleCacheData = snap->sampleCacheData;
	view.positionApplied = snap->positionApplied;
	view.positionBendStep = 0;
	view.output = 0;

	/* Plain carrier: typed copies keep their frozen frame, never claim. */
	if (osc_isTyped(&view)) {
		switch (snap->sampleCacheLooped) {
		case OSC_BIND_SMP:
			osc_smpPlainBlock(&view, osc_itpScratch, size, 1.0f);
			break;
		case OSC_BIND_SLP:
			osc_slpPlainBlock(&view, osc_itpScratch, size, 1.0f);
			break;
		case OSC_BIND_WTB8:
		case OSC_BIND_WTB16:
			osc_wtbPlainBlock(&view, osc_itpScratch, size, 1.0f);
			break;
		default:
			osc_zeroBlock(osc_itpScratch, size);
			break;
		}
	} else {
		calcNextOscSampleBlock(&view, osc_itpScratch, size, 1.0f);
	}
	snap->phase = view.phase;

	/* Crossfade: t runs from this block's start toward 1 (full-block, D4). */
	elapsed = (uint8_t)(OSC_RUN_ITP_FADE_BLOCKS - snap->counter);
	total_samples = (float)((uint16_t)OSC_RUN_ITP_FADE_BLOCKS *
	                        OUTPUT_DMA_SIZE);
	t = ((float)((uint16_t)elapsed * OUTPUT_DMA_SIZE)) / total_samples;
	for (i = 0u; i < size; i++) {
		const float oldSample = (float)osc_itpScratch[i];
		const float liveSample = (float)buf[i];
		buf[i] = (int16_t)((1.0f - t) * oldSample + t * liveSample);
		t += step;
	}

	if (--snap->counter == 0u)
		snap->active = 0u;
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
 * S087 P1: a stale clock is folded into the loop at entry (F20).
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
	/*
	 * Bring a stale clock into the loop (S087 P1).
	 *
	 * What:       a position at or past loop_end (left in DrumVoice.syncClock by
	 *             a master of another kind, in 2^32-per-cycle units) is folded
	 *             into the loop once, before the per-sample walk.
	 * Why:        S087 P0 impl. §11 F20: without it the walk subtracted one loop
	 *             per sample and published a reset on every sample for several
	 *             blocks after the master changed to an l entry.
	 * Inputs:     pos, the loop region.  Outputs: pos inside [loop_start,
	 *             loop_end) or still in the lead-in.
	 * Affiliates: Drum_calcVoiceSyncBlock() (owner of syncClock).
	 */
	if (pos >= loopEndPh)
		pos = loopStartPh + ((pos - loopStartPh) % lenPh);
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
 * Fractional frame position of one pos value (S087 P1).
 *
 * What:       pos x (count - 1) / 127, in frames (0.0 .. count - 1).
 * Why:        S087 P0 A20: the frame index is the interpolation target; the
 *             blend needs the fraction, not the nearest frame (impl. §11 F8).
 * Inputs:     position (0..127), count (frames, >= 1).
 * Outputs:    frame position.
 * Accessors:  osc_wtbPlanBlock().
 * Affiliates: osc_wtbFrameOf() (the nearest frame, used by the one-frame
 *             fallback).
 */
static float osc_wtbFramePos(uint8_t position, uint32_t count)
{
	if (count <= 1u)
		return 0.0f;
	return (float)(position & 127u) * (float)(count - 1u) * (1.0f / 127.0f);
}

/*
 * One wavetable blend block, split at a frame crossing (S087 P1).
 *
 * What:       segments (1 or 2); for each segment its exclusive end sample,
 *             the lower frame of its pair, and ff at its first sample; step is
 *             the per-sample change of the frame position.
 * Why:        osc_typedPositionApply() limits one block's move to at most one
 *             frame (|d| <= 127 / (count - 1)), so a glide crosses at most one
 *             frame boundary per block; splitting there keeps every sample a
 *             blend of the two frames around its own position.
 * Accessors:  osc_wtbPlanBlock() (writes), the four blend copies (read).
 */
typedef struct {
	uint8_t  segments;
	uint8_t  end[2];
	uint32_t pair[2];
	float    ff[2];
	float    step;
} osc_wtbPlan_t;

/*
 * Plan one wavetable blend block (S087 P1).
 *
 * What:       the block glides from fs (the position at its start,
 *             positionApplied - positionBendStep) to fe (positionApplied), as
 *             fractional frames; sample i sits at fs + i x step, step =
 *             (fe - fs) / size. When the glide crosses frame floor(min) + 1
 *             strictly inside the block, the block splits at the first sample
 *             on or past the crossing. Each segment blends the pair
 *             floor(min of its first and last position), clamped to count - 2,
 *             starting at ff = its first position - that frame.
 * Why:        S087 P0 impl. §11 F8: fractional frames, with no extrapolation
 *             past a pair; a held pos (fs == fe) gives one segment with a
 *             constant ff = the fraction.
 * Inputs:     osc (positionApplied, positionBendStep), count (>= 2), size.
 * Outputs:    *plan.
 * Accessors:  osc_wtbBlendBlock(), osc_wtbFmBlendBlock(),
 *             osc_wtbRingBlendBlock(), osc_wtbSyncBlendBlock().
 * Affiliates: osc_wtbFramePos(); osc_typedPositionApply() (the one-frame cap).
 */
static void osc_wtbPlanBlock(const OscInfo* osc, uint32_t count, uint8_t size,
                             osc_wtbPlan_t* plan)
{
	const float fe = osc_wtbFramePos(osc->positionApplied, count);
	const float fs = osc_wtbFramePos(
		(uint8_t)((osc->positionApplied - (uint8_t)osc->positionBendStep) & 127u),
		count);
	const float step = (size != 0u) ? (fe - fs) / (float)size : 0.0f;
	const float lo = (fs < fe) ? fs : fe;
	const float hi = (fs < fe) ? fe : fs;
	const float cross = (float)((uint32_t)lo + 1u);
	uint8_t k = size;
	uint8_t s;

	plan->step = step;
	if (size == 0u) {
		plan->segments = 0u;
		return;
	}
	if (step != 0.0f && cross > lo && cross < hi) {
		const float t = (cross - fs) / step;
		uint32_t kk = (uint32_t)t;

		if ((float)kk < t)
			kk++;
		if (kk > 0u && kk < size)
			k = (uint8_t)kk;
	}
	plan->segments = (k < size) ? 2u : 1u;
	plan->end[0] = k;
	plan->end[1] = size;
	for (s = 0u; s < plan->segments; s++) {
		const uint8_t i0 = (s == 0u) ? 0u : k;
		const uint8_t i1 = plan->end[s];
		const float ps = fs + (float)i0 * step;
		const float pe = fs + (float)(i1 - 1u) * step;
		const float pmin = (ps < pe) ? ps : pe;
		uint32_t pair = (pmin > 0.0f) ? (uint32_t)pmin : 0u;
		float ff;

		if (pair > count - 2u)
			pair = count - 2u;
		ff = ps - (float)pair;
		if (ff < 0.0f)
			ff = 0.0f;
		else if (ff > 1.0f)
			ff = 1.0f;
		plan->pair[s] = pair;
		plan->ff[s] = ff;
	}
}

/*
 * Wavetable plain copy, one frame (S087 stage E; S087 P1).
 *
 * What:       the frame nearest to positionApplied (osc_wtbFrameOf()).
 * Why:        A20: pos interpolates; wav steps. S087 P1: this is also the
 *             no-slot / one-frame fallback of osc_wtbBlendBlock().
 * Accessors:  osc_renderTyped(), osc_wtbBlendBlock() (fallback).
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

/*
 * Wavetable plain render with frame blending (S087 P1; S087 stage E).
 *
 * What:       with a granted interpolation slot, renders the block in at most
 *             two segments planned by osc_wtbPlanBlock(), each blending one
 *             frame pair with ff ramping through the fractional frame position.
 *             A pos held between two frames plays their mix. Without a slot,
 *             or with one frame, the one-frame copy plays the nearest frame
 *             (the Q9 fallback).
 * Why:        S087 P0 A3/A7/A20: wavetables interpolate the frame index, always
 *             while the global setting is on, until both slots are taken;
 *             impl. §11 F8: the frame position was rounded to whole frames, so
 *             no in-between frame was ever heard.
 * Inputs:     osc (bound to a wavetable; positionApplied/positionBendStep from
 *             osc_typedPositionApply()), buf, size, gain.
 * Outputs:    buf; osc->phase.
 * Accessors:  osc_renderTyped().
 * Affiliates: osc_wtbPlanBlock(), osc_wtbPlainBlock(), modNode_interpClaim().
 */
static OSC_S087_LOOP void osc_wtbBlendBlock(OscInfo* osc, int16_t* buf,
                                            uint8_t size, float gain)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	const uint32_t count = wt ? wt->frame_count : 0u;
	osc_wtbPlan_t plan;
	uint8_t i = 0u;
	uint8_t s;

	if (!wt || !osc->sampleCacheData || count < 2u ||
	    !modNode_interpClaim(osc)) {
		osc_wtbPlainBlock(osc, buf, size, gain);
		return;
	}
	{
		const uint8_t bits = wt->bits;
		const uint32_t len = wt->frame_len;

		osc_wtbPlanBlock(osc, count, size, &plan);
		for (s = 0u; s < plan.segments; s++) {
			const uint8_t *a = osc_wtbFrame(osc, wt, plan.pair[s]);
			const uint8_t *b = osc_wtbFrame(osc, wt, plan.pair[s] + 1u);
			float ff = plan.ff[s];

			for (; i < plan.end[s]; i++) {
				const float x0 = osc_wtbRead(a, bits, len, ph);
				const float x1 = osc_wtbRead(b, bits, len, ph);
				const float out = x0 + ff * (x1 - x0);

				buf[i] = (int16_t)(out * gain);
				ff += plan.step;
				ph += inc;
			}
		}
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
 * Affiliates: osc_wtbFmRampBlock() / osc_wtbFmBlendRampBlock() (the S089 P3
 *             Drum twins with a per-sample depth ramp: a change to these
 *             loops is made to the twins too).
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

/*
 * Wavetable FM carrier with frame blending (S087 P1; S087 stage E).
 *
 * What:       as osc_wtbSyncBlendBlock() for the frame plan; both frames are
 *             read at ph + (ssat(mod x fmMod) << 19), the table-FM fraction of
 *             a cycle. Without a slot, or with one frame, the one-frame FM copy
 *             runs.
 * Why:        S087 P0 A3/A20, A17 addendum; impl. §11 F8.
 * Inputs:     osc (bound to a wavetable), mod[size], buf, size, gain.
 * Outputs:    buf; osc->phase.
 * Accessors:  osc_renderTypedFm().
 * Affiliates: osc_wtbPlanBlock(), osc_wtbFmBlock(), calcFmBlock() (the same
 *             FM scale), modNode_interpClaim(); osc_wtbFmBlendRampBlock()
 *             (the S089 P3 Drum twin with a per-sample depth ramp: a change
 *             to this loop is made to the twin too).
 */
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
	osc_wtbPlan_t plan;
	uint8_t i = 0u;
	uint8_t s;

	if (!wt || !osc->sampleCacheData || count < 2u ||
	    !modNode_interpClaim(osc)) {
		osc_wtbFmBlock(osc, mod, buf, size, gain);
		return;
	}
	{
		const uint8_t bits = wt->bits;
		const uint32_t len = wt->frame_len;

		osc_wtbPlanBlock(osc, count, size, &plan);
		for (s = 0u; s < plan.segments; s++) {
			const uint8_t *a = osc_wtbFrame(osc, wt, plan.pair[s]);
			const uint8_t *b = osc_wtbFrame(osc, wt, plan.pair[s] + 1u);
			float ff = plan.ff[s];

			for (; i < plan.end[s]; i++) {
				const uint32_t rp = ph + ((uint32_t)__SSAT(
					(int32_t)((float)mod[i] * osc->fmMod), 16) << 19);
				const float x0 = osc_wtbRead(a, bits, len, rp);
				const float x1 = osc_wtbRead(b, bits, len, rp);
				const float out = x0 + ff * (x1 - x0);

				buf[i] = (int16_t)(out * gain);
				ff += plan.step;
				ph += inc;
			}
		}
	}
	osc->phase = ph;
}

/*
 * Wavetable ring copies (S087 stage E).
 *
 * What:       the plain read with the ring law and depth ramp.
 * Accessors:  osc_renderTypedRing().
 * S087 P1: the depth steps before use (S085 ring law).
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

			/* S087 P1: S085 ring law, d_i = depthQ15 + (i + 1) x step. */
			depth += depthStepQ15;
			buf[i] = osc_ringSample((int16_t)out, modBuf[i], depth);
			ph += inc;
		}
	}
	osc->phase = ph;
}

/*
 * Wavetable ring carrier with frame blending (S087 P1; S087 stage E).
 *
 * What:       as osc_wtbSyncBlendBlock() for the frame plan; the blended value
 *             takes the ring law with d_i = depthQ15 + (i + 1) x depthStepQ15
 *             (the depth steps before it is used, as in every S085 ring copy).
 *             Without a slot, or with one frame, the one-frame ring copy runs.
 * Why:        S087 P0 A3/A20 and impl. §11 F8 (fractional frames, one pair per
 *             segment); the ring law of Oscillator.h
 *             calcNextOscSampleRingBlock() (the block ends at depthQ15 + size x
 *             depthStepQ15, the caller's next start).
 * Inputs:     osc (bound to a wavetable), modBuf[size], buf, size, depthQ15,
 *             depthStepQ15.
 * Outputs:    buf (gain 1); osc->phase.
 * Accessors:  osc_renderTypedRing().
 * Affiliates: osc_wtbPlanBlock(), osc_wtbRingBlock(), osc_ringSample(),
 *             modNode_interpClaim().
 */
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
	int32_t depth = depthQ15;
	osc_wtbPlan_t plan;
	uint8_t i = 0u;
	uint8_t s;

	if (!wt || !osc->sampleCacheData || count < 2u ||
	    !modNode_interpClaim(osc)) {
		osc_wtbRingBlock(osc, modBuf, buf, size, depthQ15, depthStepQ15);
		return;
	}
	{
		const uint8_t bits = wt->bits;
		const uint32_t len = wt->frame_len;

		osc_wtbPlanBlock(osc, count, size, &plan);
		for (s = 0u; s < plan.segments; s++) {
			const uint8_t *a = osc_wtbFrame(osc, wt, plan.pair[s]);
			const uint8_t *b = osc_wtbFrame(osc, wt, plan.pair[s] + 1u);
			float ff = plan.ff[s];

			for (; i < plan.end[s]; i++) {
				const float x0 = osc_wtbRead(a, bits, len, ph);
				const float x1 = osc_wtbRead(b, bits, len, ph);
				const float out = x0 + ff * (x1 - x0);

				depth += depthStepQ15;
				buf[i] = osc_ringSample((int16_t)out, modBuf[i], depth);
				ff += plan.step;
				ph += inc;
			}
		}
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

/*
 * Wavetable sync slave with frame blending (S087 P1; S087 stage E).
 *
 * What:       with a granted interpolation slot, renders the block in at most
 *             two segments planned by osc_wtbPlanBlock(): each segment blends
 *             one frame pair with ff ramping through the fractional frame
 *             position; the cycle phase takes osc_syncPull() after every sample
 *             (reset target 0 + overshoot). Without a slot, or with one frame,
 *             the one-frame sync copy runs.
 * Why:        S087 P0 A3/A20: the frame index (pos) is the interpolation
 *             target; S087 P0 impl. §11 F8: the frame position must stay
 *             fractional, and a glide crossing a frame boundary inside the
 *             block changes pair at the crossing sample instead of
 *             extrapolating.
 * Inputs:     osc (bound to a wavetable), over[size], ratioQ16, depthQ30,
 *             buf, size.
 * Outputs:    buf (gain 1); osc->phase.
 * Cost:       per sample as before (two frame reads, two interpolations, the
 *             blend, the pull); the segment set-up is per block.
 * Accessors:  osc_renderTypedSync().
 * Affiliates: osc_wtbPlanBlock(), osc_wtbSyncBlock(), modNode_interpClaim().
 */
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
	osc_wtbPlan_t plan;
	uint8_t i = 0u;
	uint8_t s;

	if (!wt || !osc->sampleCacheData || count < 2u ||
	    !modNode_interpClaim(osc)) {
		osc_wtbSyncBlock(osc, over, ratioQ16, depthQ30, buf, size);
		return;
	}
	{
		const uint8_t bits = wt->bits;
		const uint32_t len = wt->frame_len;

		osc_wtbPlanBlock(osc, count, size, &plan);
		for (s = 0u; s < plan.segments; s++) {
			const uint8_t *a = osc_wtbFrame(osc, wt, plan.pair[s]);
			const uint8_t *b = osc_wtbFrame(osc, wt, plan.pair[s] + 1u);
			float ff = plan.ff[s];

			for (; i < plan.end[s]; i++) {
				const float x0 = osc_wtbRead(a, bits, len, ph);
				const float x1 = osc_wtbRead(b, bits, len, ph);
				const float out = x0 + ff * (x1 - x0);

				buf[i] = (int16_t)out;
				ff += plan.step;
				ph = osc_syncPull(ph + inc, over[i], ratioQ16, depthQ30);
			}
		}
	}
	osc->phase = ph;
}
/*
 * ===========================================================================
 * Drum FM depth-ramp carriers (S089 P3).
 * ===========================================================================
 *
 * What:       ramped twins of the four typed FM carrier loops (smp, slp, wtb
 *             blend and its one-frame wtb fallback) and the flash dispatcher
 *             osc_renderTypedFmRamp() that selects among them. Each twin is
 *             its original with exactly one change: the loop-invariant FM
 *             depth osc->fmMod becomes a per-sample depth d that starts at
 *             d0 and moves by dStep after every sample (d_i = d0 + i x dStep,
 *             accumulated). Everything else - the read rule, the clamps, the
 *             phase advance, the gain, the zero-block guards - is copied
 *             unchanged.
 * Why:        Drum's fm branch used to scale the whole modulator block by a
 *             per-sample ramp (S088 P0 Layer 2e) before the carrier read it:
 *             a separate 10-instruction pass on every block, although the
 *             ramp only moves on the one block after a fre/itp retrigger.
 *             Folding the ramp into the carrier's existing depth multiply
 *             costs one vadd per sample instead, so Drum fm with an smp, slp
 *             or wtb carrier drops by about 9 instructions per sample (the
 *             dearest carriers: slp 90 -> 81, wtb blend 86 -> 77, smp 82 ->
 *             73; S089_P3_FM_RAMP_EFFICIENCY.md §5.1). The CPU is at its
 *             ceiling (S089_P2_WAVEFOLDER_CHAIN.md §8), so this is a saving
 *             that lowers the worst case.
 * Rules:      - The originals stay byte-identical: Cymbal and HiHat call them
 *               through osc_renderTypedFm(), and an added per-sample vadd
 *               would cost them +1 for nothing (the ceiling rule). New
 *               per-sample code is a new function, never an edit to an
 *               existing loop (S087 rule).
 *             - Unarmed blocks (every rst block and almost every other):
 *               the caller passes d0 = osc->fmMod and dStep = 0.0f, so d is
 *               exactly fmMod on every sample (x + 0.0f == x) and every
 *               product is the same expression as the original's:
 *               bit-identical output (S0; host law FR1).
 *             - Armed blocks: S2. The modulator is no longer truncated to
 *               int16 after the ramp scale (less quantisation), and a ramp
 *               start above 1 x fmMod now saturates in the existing __SSAT
 *               instead of wrapping in the old (int16_t) cast.
 *             - Keep each twin in step with its original: a fix to one is
 *               a fix to both (the originals name their twin in Affiliates).
 * Placement:  the four loops in .itcm.s089p3, pinned after .itcm.s089 in
 *             STM32F765VIHx_FLASH.ld so no existing ITCM function moves
 *             (S085 rule); the dispatcher runs once per block from flash.
 * Accessors:  Drum_calcVoiceSyncBlock() (fm branch, typed carrier) only.
 * Affiliates: osc_smpFmBlock(), osc_slpFmBlock(), osc_wtbFmBlock(),
 *             osc_wtbFmBlendBlock() (the originals); osc_renderTypedFm()
 *             (their dispatcher, still used by Cymbal/HiHat); DrumVoice.c
 *             lastFmDepth / ampRampArm; tools/dsp_test/modules/oscillator.c
 *             group fmramp (FR1-FR3).
 */

/*
 * S089 P3 ITCM placement.
 *
 * What:       .itcm.s089p3 for the four ramped carrier loops; noinline and
 *             noclone keep each loop its own symbol (elf_loopdump.py measures
 *             it by name) and stop LTO from folding it into a caller or
 *             cloning it per call site (as OSC_S087_LOOP).
 * Why:        S087 P0 §2 rules 2-3 and the S089 P2 linker pin: a new
 *             subsection is appended after every existing one, so the S085,
 *             S087 and S089 P2 loops keep their addresses. The name must not
 *             match the exact *(.itcm.s089) pattern (voiceFold.c).
 * Accessors:  osc_smpFmRampBlock(), osc_slpFmRampBlock(),
 *             osc_wtbFmRampBlock(), osc_wtbFmBlendRampBlock().
 * Affiliates: OSC_S085_SECTION, OSC_S087_SECTION, voiceFold.c
 *             VOICE_FOLD_SECTION; STM32F765VIHx_FLASH.ld ITCM input order;
 *             link_budget.py.
 */
#define OSC_S089P3_SECTION __attribute__((section(".itcm.s089p3")))
#define OSC_S089P3_LOOP    OSC_S089P3_SECTION __attribute__((noinline, noclone))

/*
 * smp FM carrier with a per-sample depth ramp (S089 P3; twin of
 * osc_smpFmBlock()).
 *
 * What:       the one-shot read at ph + (ssat(mod x d) << 9), clamped to
 *             [0, endPh], with d = d0 on the first sample and d += dStep
 *             after each sample. The phase advance is the plain one.
 * Why:        the Drum FM depth ramp folded into the carrier's own depth
 *             multiply (see the block comment above).
 * Inputs:     osc (bound smp: sampleCacheData/Size/Index, phase, phaseInc),
 *             mod[size] (the modulator block at fmModAmount, unscaled by any
 *             ramp), size, gain, d0 (depth on sample 0), dStep (depth change
 *             per sample; 0.0f when not armed).
 * Outputs:    buf[size]; osc->phase.
 * Cost:       the original's 42 instructions per sample + 1 (the vadd).
 * Accessors:  osc_renderTypedFmRamp().
 * Affiliates: osc_smpFmBlock() (keep in step); osc_smpReadPhase(),
 *             osc_frac12(), osc_bindGain().
 */
static OSC_S089P3_LOOP void osc_smpFmRampBlock(OscInfo* osc,
                                               const int16_t *mod,
                                               int16_t *buf, uint8_t size,
                                               float gain, float d0,
                                               float dStep)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	/* The last valid read position: the zero pad after the item. */
	const uint32_t endPh = osc->sampleCacheSize << OSC_SAMPLE_PHASE_SHIFT;
	/* Render gain x the item's stored gain code, once per block. */
	const float g = gain * osc_bindGain(osc);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	/* The ramped FM depth: d0 here, d0 + i x dStep at sample i. */
	float d = d0;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	for (i = 0u; i < size; i++) {
		/*
		 * FM read offset: modulator x this sample's depth, saturated to
		 * int16 and scaled to read-phase units (<< 9, the S087 A17 scale
		 * shared with every typed carrier). Unarmed, d == fmMod exactly,
		 * so this is the original's expression bit for bit.
		 */
		const int32_t off = (int32_t)__SSAT(
			(int32_t)((float)mod[i] * d), 16) << OSC_FM_SAMPLE_SHIFT;
		int32_t rp = (int32_t)ph + off;
		uint32_t idx;
		int16_t a;
		int16_t b;
		float out;

		/* Step the depth after its use: sample 0 reads d0 and sample
		 * size - 1 reads d0 + (size - 1) x dStep = the block's fmMod. */
		d += dStep;
		/* One-shot clamp: never before the start, never past the pad. */
		if (rp < 0)
			rp = 0;
		else if ((uint32_t)rp > endPh)
			rp = (int32_t)endPh;
		idx = (uint32_t)rp >> OSC_SAMPLE_PHASE_SHIFT;
		a = data[idx];
		b = data[idx + 1u];
		/* Linear interpolation on the 12-bit phase fraction. */
		out = (float)a + osc_frac12((uint32_t)rp) * (float)(b - a);
		buf[i] = (int16_t)(out * g);
		ph = osc_smpReadPhase(ph + inc, endPh);
	}
	osc->phase = ph;
}

/*
 * slp FM carrier with a per-sample depth ramp (S089 P3; twin of
 * osc_slpFmBlock()).
 *
 * What:       the looped read at phase + (ssat(mod x d) << 9), with d = d0 on
 *             the first sample and d += dStep after each sample. The read
 *             rule is chosen once per block, from where the block starts:
 *             - inside the loop (phase >= loop_start): the offset is bounded
 *               to half a loop and the read, relative to loop_start, wraps
 *               once each way; the index is loop_start + that point;
 *             - in the lead-in (phase < loop_start): the absolute read is
 *               clamped to [0, loop_end - 1].
 *             The seam reads loop_start. The phase takes the per-block step
 *             from osc_slpBlockInc() and wraps once at loop_end.
 * Why:        the Drum FM depth ramp folded into the carrier's own depth
 *             multiply (see the block comment above). slp is the dearest
 *             Drum fm carrier (50 + 30 modulator + 10 pass = 90 before).
 * Inputs:     osc (bound slp), mod[size], size, gain, d0, dStep (as
 *             osc_smpFmRampBlock()).
 * Outputs:    buf[size]; osc->phase.
 * Edge:       a block that starts in the lead-in and enters the loop keeps
 *             the lead-in read rule (clamp) for its remaining samples (as the
 *             original).
 * Cost:       the original's 50 (in the loop) / 39 (lead-in) instructions per
 *             sample + 1.
 * Accessors:  osc_renderTypedFmRamp().
 * Affiliates: osc_slpFmBlock() (keep in step); osc_slpBlockInc(),
 *             sampleMemory_sampleEntry() (loop points).
 */
static OSC_S089P3_LOOP void osc_slpFmRampBlock(OscInfo* osc,
                                               const int16_t *mod,
                                               int16_t *buf, uint8_t size,
                                               float gain, float d0,
                                               float dStep)
{
	const int16_t *data = (const int16_t *)osc->sampleCacheData;
	const sample_catalogue_entry_t *entry =
		sampleMemory_sampleEntry(osc->sampleCacheIndex);
	/* Loop region in frames; a missing entry loops the whole item. */
	const uint32_t ls = entry ? entry->loop_start : 0u;
	const uint32_t le = entry ? entry->loop_end : osc->sampleCacheSize;
	const uint32_t len = le - ls;
	/* The same region in read-phase units (frames << 12). */
	const uint32_t lenPh = len << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopStartPh = ls << OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t loopEndPh = le << OSC_SAMPLE_PHASE_SHIFT;
	const float g = gain * osc_bindGain(osc);
	uint32_t ph = osc->phase;
	/* The ramped FM depth: d0 here, d0 + i x dStep at sample i. */
	float d = d0;
	uint32_t inc;
	uint8_t i;

	if (!data || osc->sampleCacheSize == 0u || len == 0u) {
		osc_zeroBlock(buf, size);
		return;
	}
	/* Per-block step: forward modulo the loop (a backward pos glide is a
	 * forward wrap), so the per-sample wrap below needs one compare. */
	inc = osc_slpBlockInc(osc->phaseInc, ph, loopStartPh, lenPh);
	if (ph >= loopStartPh) {
		const int32_t loopLen = (int32_t)lenPh;
		/* Half a loop: the largest offset that still wraps only once. */
		const int32_t half = loopLen >> 1;

		for (i = 0u; i < size; i++) {
			/* FM read offset at this sample's depth (S0 when d == fmMod). */
			int32_t off = (int32_t)__SSAT(
				(int32_t)((float)mod[i] * d), 16) << OSC_FM_SAMPLE_SHIFT;
			int32_t rp;
			uint32_t idx;
			uint32_t next;
			int16_t a;
			int16_t b;
			float out;

			/* Depth steps after its use (sample 0 = d0). */
			d += dStep;
			/* Bound the offset to +/- half a loop, then wrap the read
			 * point (relative to loop_start) once in either direction. */
			off = (off > half) ? half : off;
			off = (off < -half) ? -half : off;
			rp = (int32_t)(ph - loopStartPh) + off;
			rp = (rp < 0) ? rp + loopLen : rp;
			rp = (rp >= loopLen) ? rp - loopLen : rp;
			idx = ls + ((uint32_t)rp >> OSC_SAMPLE_PHASE_SHIFT);
			/* The seam interpolates toward loop_start, not loop_end. */
			next = (idx + 1u == le) ? ls : idx + 1u;
			a = data[idx];
			b = data[next];
			out = (float)a + osc_frac12((uint32_t)rp) * (float)(b - a);
			buf[i] = (int16_t)(out * g);
			ph += inc;
			if (ph >= loopEndPh)
				ph -= lenPh;
		}
	} else {
		/* The last readable point before loop_end (lead-in clamp). */
		const int32_t lastPh = (int32_t)loopEndPh - 1;

		for (i = 0u; i < size; i++) {
			/* FM read offset at this sample's depth (S0 when d == fmMod). */
			const int32_t off = (int32_t)__SSAT(
				(int32_t)((float)mod[i] * d), 16) << OSC_FM_SAMPLE_SHIFT;
			int32_t rp = (int32_t)ph + off;
			uint32_t idx;
			uint32_t next;
			int16_t a;
			int16_t b;
			float out;

			/* Depth steps after its use (sample 0 = d0). */
			d += dStep;
			/* Lead-in: absolute read clamped to [0, loop_end - 1]. */
			rp = (rp < 0) ? 0 : rp;
			rp = (rp > lastPh) ? lastPh : rp;
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
	}
	osc->phase = ph;
}

/*
 * One-frame wavetable FM carrier with a per-sample depth ramp (S089 P3;
 * twin of osc_wtbFmBlock()).
 *
 * What:       reads the nearest pos frame at ph + (ssat(mod x d) << 19), the
 *             table-FM fraction of a cycle, with d = d0 on the first sample
 *             and d += dStep after each sample.
 * Why:        osc_wtbFmBlendRampBlock() falls back to this when it has no
 *             interpolation slot or the table has one frame. Without a ramped
 *             fallback those blocks would need the old modulator pass, two
 *             mechanisms on one path (S089 P3 plan §5.1).
 * Inputs:     osc (bound wtb: sampleCacheIndex/Data, positionApplied, phase,
 *             phaseInc), mod[size], size, gain, d0, dStep.
 * Outputs:    buf[size]; osc->phase.
 * Cost:       the original's 32 (8-bit) / 34 (16-bit) instructions per
 *             sample + 1.
 * Accessors:  osc_wtbFmBlendRampBlock() (fallback).
 * Affiliates: osc_wtbFmBlock() (keep in step); osc_wtbFrameOf(),
 *             osc_wtbFrame(), osc_wtbRead().
 */
static OSC_S089P3_LOOP void osc_wtbFmRampBlock(OscInfo* osc,
                                               const int16_t* mod,
                                               int16_t* buf, uint8_t size,
                                               float gain, float d0,
                                               float dStep)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	/* The ramped FM depth: d0 here, d0 + i x dStep at sample i. */
	float d = d0;
	uint8_t i;

	if (!wt || !osc->sampleCacheData) {
		osc_zeroBlock(buf, size);
		return;
	}
	{
		/* The nearest frame to pos (no blend in the fallback). */
		const uint32_t frame = osc_wtbFrameOf(osc->positionApplied,
		                                      wt->frame_count);
		const uint8_t *base = osc_wtbFrame(osc, wt, frame);

		for (i = 0u; i < size; i++) {
			/* Read phase + FM offset: ssat(mod x d) << 19 is the same
			 * fraction of a cycle as the table-wave FM (S087 A17). */
			const uint32_t rp = ph + ((uint32_t)__SSAT(
				(int32_t)((float)mod[i] * d), 16) << 19);

			/* Depth steps after its use (sample 0 = d0). */
			d += dStep;
			buf[i] = (int16_t)(osc_wtbRead(base, wt->bits, wt->frame_len, rp) *
			                   gain);
			ph += inc;
		}
	}
	osc->phase = ph;
}

/*
 * Wavetable FM carrier with frame blending and a per-sample depth ramp
 * (S089 P3; twin of osc_wtbFmBlendBlock()).
 *
 * What:       the S087 P1 frame plan (osc_wtbPlanBlock(): one or two
 *             segments, each blending one frame pair with ff stepping by
 *             plan.step); both frames are read at ph + (ssat(mod x d) << 19),
 *             with d = d0 on the first sample and d += dStep after each
 *             sample. d runs across the segment boundary unchanged: it is a
 *             per-sample ramp over the whole block, not per segment.
 *             Without a slot, or with one frame, the ramped one-frame copy
 *             runs with the same d0/dStep.
 * Why:        the Drum FM depth ramp folded into the carrier's own depth
 *             multiply (see the block comment above).
 * Inputs:     osc (bound wtb), mod[size], size, gain, d0, dStep.
 * Outputs:    buf[size]; osc->phase; one interpolation slot claimed for this
 *             block (modNode_interpClaim()), exactly as the original.
 * Cost:       the original's 46 (16-bit) / 42 (8-bit) instructions per
 *             sample + 1.
 * Accessors:  osc_renderTypedFmRamp().
 * Affiliates: osc_wtbFmBlendBlock() (keep in step); osc_wtbPlanBlock(),
 *             osc_wtbFmRampBlock() (fallback), modNode_interpClaim().
 */
static OSC_S089P3_LOOP void osc_wtbFmBlendRampBlock(OscInfo* osc,
                                                    const int16_t* mod,
                                                    int16_t* buf,
                                                    uint8_t size, float gain,
                                                    float d0, float dStep)
{
	const wavetable_catalogue_entry_t *wt =
		sampleMemory_wavetableEntry(osc->sampleCacheIndex);
	uint32_t ph = osc->phase;
	const uint32_t inc = osc->phaseInc;
	const uint32_t count = wt ? wt->frame_count : 0u;
	osc_wtbPlan_t plan;
	/* The ramped FM depth: d0 here, d0 + i x dStep at sample i. */
	float d = d0;
	uint8_t i = 0u;
	uint8_t s;

	/* The claim is the last test, so a slot is only taken when the blend
	 * really runs (as the original). */
	if (!wt || !osc->sampleCacheData || count < 2u ||
	    !modNode_interpClaim(osc)) {
		osc_wtbFmRampBlock(osc, mod, buf, size, gain, d0, dStep);
		return;
	}
	{
		const uint8_t bits = wt->bits;
		const uint32_t len = wt->frame_len;

		osc_wtbPlanBlock(osc, count, size, &plan);
		/* i runs on across the segments: segment s ends at plan.end[s]. */
		for (s = 0u; s < plan.segments; s++) {
			const uint8_t *a = osc_wtbFrame(osc, wt, plan.pair[s]);
			const uint8_t *b = osc_wtbFrame(osc, wt, plan.pair[s] + 1u);
			float ff = plan.ff[s];

			for (; i < plan.end[s]; i++) {
				/* One FM read point for both frames (S0 when d == fmMod). */
				const uint32_t rp = ph + ((uint32_t)__SSAT(
					(int32_t)((float)mod[i] * d), 16) << 19);
				const float x0 = osc_wtbRead(a, bits, len, rp);
				const float x1 = osc_wtbRead(b, bits, len, rp);
				/* Frame blend at this sample's fractional position. */
				const float out = x0 + ff * (x1 - x0);

				/* Depth steps after its use (sample 0 = d0). */
				d += dStep;
				buf[i] = (int16_t)(out * gain);
				ff += plan.step;
				ph += inc;
			}
		}
	}
	osc->phase = ph;
}

/*
 * Typed FM render with a per-sample depth ramp (S089 P3; see the header).
 *
 * What:       selects the ramped carrier by the oscillator's bind kind, as
 *             osc_renderTypedFm() does for the originals: smp, slp, wtb
 *             (8- and 16-bit, blend with its one-frame fallback); anything
 *             else (no item installed) renders a zero block.
 * Why:        one per-block entry for Drum's fm branch. noinline/noclone and
 *             no section attribute: it runs from flash once per block, and
 *             keeping it out of mixer_calcNextSampleBlock() (where the Drum
 *             render is inlined) means the change adds one call there, not a
 *             switch of three calls that LTO would re-allocate registers
 *             around (the S086 P3 failure). The call costs a few cycles per
 *             block (well under 1 per sample).
 * Inputs:     osc (typed: osc_isTyped()), mod[size], buf, size, gain, d0,
 *             dStep.
 * Outputs:    buf[size]; osc->phase (and a wtb interpolation slot claim).
 * Accessors:  Drum_calcVoiceSyncBlock() (fm branch, typed carrier).
 * Affiliates: osc_renderTypedFm() (the unramped dispatcher; keep the cases
 *             in step); OSC_BIND_*.
 */
void __attribute__((noinline, noclone)) osc_renderTypedFmRamp(
	OscInfo* osc, const int16_t* mod, int16_t* buf, uint8_t size,
	float gain, float d0, float dStep)
{
	switch (osc->sampleCacheLooped) {
	case OSC_BIND_SMP:
		osc_smpFmRampBlock(osc, mod, buf, size, gain, d0, dStep);
		break;
	case OSC_BIND_SLP:
		osc_slpFmRampBlock(osc, mod, buf, size, gain, d0, dStep);
		break;
	case OSC_BIND_WTB8:
	case OSC_BIND_WTB16:
		osc_wtbFmBlendRampBlock(osc, mod, buf, size, gain, d0, dStep);
		break;
	default:
		osc_zeroBlock(buf, size);
		break;
	}
}
