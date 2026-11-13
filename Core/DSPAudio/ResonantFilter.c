/*
 * ResonantFilter.c
 *
 *  Created on: 05.04.2012
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

#include "ResonantFilter.h"

/* INITCM_EFFECT is a compile-time placement switch from config.h. Session 023
** measured oscillator-only ITCM as the useful A/B state; filter placement stays
** compiled as normal flash code unless ENABLE_EFFECT_INITCM_CODE is explicitly
** enabled for another hardware test. */

// to make the linker happy. __errno seems not defined
// so libm.a won't link without this int.
int __errno;
//------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------
void SVF_setReso(ResonantFilter* filter, float feedback)
{
	filter->q = 1-feedback;
	if(filter->q<0.1f)filter->q = 0.02f;
}
//------------------------------------------------------------------------------------
void SVF_init(ResonantFilter* filter)
{
		filter->s1 = 0;
		filter->s2 = 0;

		filter->a =filter->b = 0;

		filter->f = 0.20f;
		filter->q = 0.9f;

		/* S095 P4 C2: the drv 0 value of SVF_setDrive() (was 0.5). */
		filter->drive = 1.0f;

		SVF_directSetFilterValue(filter,0.25f);

#if USE_SHAPER_NONLINEARITY
		setDistortionShape(&filter->shaper, 0);
#endif
}
//------------------------------------------------------------------------------------
void SVF_reset(ResonantFilter* filter)
{
	filter->s1 = 0;
	filter->s2 = 0;

#if ENABLE_NONLINEAR_INTEGRATORS
	filter->zi = 0;	//input z^(-1)
#endif

	filter->a = filter->b = 0;
}
//------------------------------------------------------------------------------------
static INITCM_EFFECT float fastTanh(float var)
{
   if(var < -1.95f)     return -1.0f;
   else if(var > 1.95f) return  1.0f;
   else					return  4.15f*var/(4.29f+var*var);
}
//------------------------------------------------------------------------------------
INITCM_EFFECT float fastTan(float x)
{
#if 1
	float A = -15*x+x*x*x;
	float B = 3*(-5+2*x*x);
	return A/B;
#else
	const float A = 5*(-21*x+2*x*x*x);
	const float B = 105-45*x*x+x*x*x*x;
	return A/B;
#endif
}
//------------------------------------------------------------------------------------
INITCM_EFFECT void SVF_recalcFreq(ResonantFilter* filter)
{
	filter->g  = fastTan(M_PI * filter->f );
#if USE_SHAPER_NONLINEARITY
	setDistortionShape(&filter->shaper, filter->drive);
#endif
}
//------------------------------------------------------------------------------------
/*
 * SVF_recalcFreqSlew() - filter coefficient slew (S088 P0 §3 Layer 2d; see the
 * header for the contract).
 *
 * What:       computes the same target as SVF_recalcFreq() and moves the
 *             current g a fixed fraction of the way toward it:
 *                 g += OSC_RUN_FILTER_SLEW x (fastTan(pi f) - g)
 * Why:        in fre/itp the filter state is kept, so an instant g change is a
 *             transient at high resonance. 0.28 per block reaches ~90% in
 *             about seven blocks (about 5 ms at 44,108 Hz / 32 samples); 1.0
 *             would be SVF_recalcFreq() and 0.0 would freeze the cutoff.
 * Inputs:     filter->f (already the new cutoff), filter->g.
 * Outputs:    filter->g; the shaper refreshed when enabled.
 * Cost:       one fastTan + one multiply-add per block (same call site as
 *             SVF_recalcFreq()).
 * Accessors:  instrumentManager_recalcSlotFilter() for runMode fre/itp.
 * Affiliates: SVF_recalcFreq(); A2 (hardware tuning constant).
 */
#define OSC_RUN_FILTER_SLEW  0.28f
INITCM_EFFECT void SVF_recalcFreqSlew(ResonantFilter* filter)
{
	const float target = fastTan(M_PI * filter->f);

	filter->g += OSC_RUN_FILTER_SLEW * (target - filter->g);
#if USE_SHAPER_NONLINEARITY
	setDistortionShape(&filter->shaper, filter->drive);
#endif
}
//------------------------------------------------------------------------------------
/*
 * Filter input drive from the `drv` row (Drum, Snare, Cymbal, HiHat;
 * IM_SPECIAL_FILTER_DRIVE).
 *
 * What: the gain in front of the ZDF filter's Pade saturator, 1.0 at drv 0
 * up to 6.4 at drv 127 (1 + 5.4 x^2, x = drv/127). S095 P4 F4: the hard-
 * clipping types lower it per block by svf_driveFloorOffset[] (HP, BP, UBP,
 * Nch and Pek floor 0.4, as before P4); LP and LP2 keep 1.0.
 * Why (S095 P4 C2): the floor was 0.4, so every filtered voice reached the
 * bus 8 dB below the same voice with the filter `off` (which passes the
 * input unchanged), and a clean kit sat well under the bus compressor's
 * threshold. 1.0 keeps a full-scale oscillator at the saturator's knee
 * (gentle compression of its peaks only) and leaves the top (6.4, +16 dB)
 * where it was. The naive LP2 path reads the same drive.
 * Inputs: drive row 0..127. Output: filter->drive. Per row change; no
 * per-sample cost.
 * Callers: InstrumentManager.c IM_SPECIAL_FILTER_DRIVE.
 */
void SVF_setDrive(ResonantFilter* filter,uint8_t drive)
{
#if USE_SHAPER_NONLINEARITY
	filter->drive = drive;
	setDistortionShape(&filter->shaper, filter->drive);
#else
	filter->drive = 1.0f + (drive/127.f)*(drive/127.f)*5.4f;
#endif

}
//------------------------------------------------------------------------------------
void SVF_directSetFilterValue(ResonantFilter* filter, float val)
{
	filter->f = val*(0.5f*0.90f);
	filter->g  = fastTan(M_PI * filter->f );

}
/*
 * SVF_setFrequencyTarget() - cutoff target without the coefficient jump (S088
 * P0 §3 Layer 2d; see the header).
 *
 * What:       the f half of SVF_directSetFilterValue(); g is left alone.
 * Why:        in fre/itp the live filter state must not see a step in g; the
 *             per-block SVF_recalcFreqSlew() moves it from the current value.
 * Inputs:     filter, val.  Outputs: filter->f.
 * Accessors:  IM_SPECIAL_FILTER_FREQ for runMode fre/itp.
 * Affiliates: SVF_directSetFilterValue(); SVF_recalcFreqSlew().
 */
void SVF_setFrequencyTarget(ResonantFilter* filter, float val)
{
	filter->f = val*(0.5f*0.90f);
}
//------------------------------------------------------------------------------------
//#if ENABLE_NONLINEAR_INTEGRATORS
INITCM_EFFECT float tanhXdX(float x)
{
	float a = x*x;
    // IIRC I got this as Pade-approx for tanh(sqrt(x))/sqrt(x)
	x = ((a + 105)*a + 945) / ((15*a + 420)*a + 945);
	return x;
}
//#endif
//------------------------------------------------------------------------------------
INITCM_EFFECT float softClipTwo(float in)
{

	return in * tanhXdX(0.5f*in);

	if(in > 0.76159415595576488811945828260479f) return fastTanh(in-0.76159415595576488811945828260479f)+0.76159415595576488811945828260479f;
	if(in < -0.76159415595576488811945828260479f) return fastTanh(in+0.76159415595576488811945828260479f)-0.76159415595576488811945828260479f;

	return in;
}
//------------------------------------------------------------------------------------
/*
 * Normalised Padé pieces of tanhXdX() for the batched ZDF solver (S073 Step 1).
 *
 * What:       rewrites tanhXdX(v) as N/D with a=v*v, N=(a/945+105/945)*a+1
 *             and D=(15a/945+420/945)*a+1. Both pieces are at least one.
 * Why:        the batched solver multiplies denominators and inverts the
 *             product once; normalisation keeps that algebra range-safe
 *             without a data-dependent path.
 * Inputs:     a, the squared saturator argument.
 * Outputs:    one normalised numerator or denominator.
 * Accessors:  SVF_calcBlockZDF().
 * Affiliates: tanhXdX()/softClipTwo() remain for the naive path and the
 *             tools/dsp_test filter comparison checks the S1 equivalence.
 */
static inline float svf_padeNum(const float a)
{
	return (a * (1.0f / 945.0f) + (105.0f / 945.0f)) * a + 1.0f;
}

static inline float svf_padeDen(const float a)
{
	return (a * (15.0f / 945.0f) + (420.0f / 945.0f)) * a + 1.0f;
}

/*
 * Configuration guard for the batched ZDF solver (S073 Step 1).
 *
 * What:       rejects filter configurations for which the derived solver is
 *             not valid.
 * Why:        the algebra below is for nonlinear integrators without the
 *             optional output shaper; silently compiling another configuration
 *             would make the sound and CPU contract ambiguous.
 * Inputs:     ResonantFilter.h configuration switches.
 * Outputs:    a compile-time error for unsupported configurations.
 * Accessors:  the preprocessor.
 * Affiliates: SVF_calcBlockZDF(). (The float twin's guard went with the
 *             twin in S095 P4 T3.)
 */
#if !ENABLE_NONLINEAR_INTEGRATORS || USE_SHAPER_NONLINEARITY
#error "Batched ZDF solver (S073 Step 1) requires nonlinear integrators and no shaper"
#endif
//------------------------------------------------------------------------------------
/*
 * Per-type drive floor offset (S095 P4 F4, user: hard clipping is very bad).
 *
 * What:       added once per block to filter->drive (1.0 + 5.4 x^2 from
 *             SVF_setDrive()) in the ZDF branch, so each type's drive floor
 *             at drv 0 is 1.0 + offset: LP 1.0; HP, BP, UBP, Nch and Pek
 *             0.4 (the pre-P4 floor). The top moves by the same offset
 *             (5.8 at drv 127, was 6.4), so at every drv the hard-clipping
 *             types get at most the pre-P4 drive (0.4 + 5.4 x^2 <=
 *             0.4 + 6 x^2).
 * Why:        C2 raised every type's floor from 0.4 to 1.0 to bring clean
 *             voices up to the bus compressor. LP leaves through
 *             fastTanh() (soft, never at the int16 rail), but HP, BP, UBP,
 *             Nch and Pek leave through x FILTER_GAIN and __SSAT, a hard
 *             clip. Measured on the host (full-scale saw, kick and noise
 *             into the real SVF_calcBlockZDF(), drv 0, res 0..127, cutoffs
 *             0.02..0.4): at 1.0, HP clipped 2.1 % of samples (16 % at the
 *             worst setting), BP 0.34 %, UBP 0.03 %, Nch 0.02 %, Pek 7.6 %
 *             (pre-P4 at 0.4: HP 0.014 %, Pek 1.2 %, the rest 0). A floor of
 *             0.8 kept UBP and Nch clean at drv 0 but clipped them more than
 *             before mid-knob (Nch at drv 48: 10.3 % against 0.65 %), so
 *             every hard-clipping type keeps 0.4 and clips no more than
 *             before at any drv (measured drv 0..112). LP and the naive
 *             LP2 (its own path, clamped states) never hard-clip and keep
 *             1.0.
 * Index:      the filter type id (FILTER_LP 1 .. FILTER_NAIVE_2_POLE 7; 0
 *             and 8 `off` unused).
 * Cost:       one load and one add per block; the loop is unchanged.
 * Affiliates: SVF_setDrive(), SVF_calcBlockZDF(), S095_P4_IMPLEMENTATION.md
 *             §16.
 */
static const float svf_driveFloorOffset[9] = {
    0.0f,   /* unused */
    0.0f,   /* LP   floor 1.0 */
    -0.6f,  /* HP   floor 0.4 */
    -0.6f,  /* BP   floor 0.4 */
    -0.6f,  /* UBP  floor 0.4 */
    -0.6f,  /* Nch  floor 0.4 */
    -0.6f,  /* Pek  floor 0.4 */
    0.0f,   /* LP2  (its own path reads filter->drive) */
    0.0f,   /* off */
};
//------------------------------------------------------------------------------------
INITCM_EFFECT_NOINLINE void SVF_calcBlockZDF(ResonantFilter* filter, const uint8_t type, int16_t* buf, const uint8_t size)
{
	uint8_t i;
	const float f 	= filter->g;
	//fix unstable filter for high f and r settings
	const float R 	= filter->f >= 0.4499f ? 1 : filter->q;
	const float ff 	= f*f;

	if(type == FILTER_NAIVE_2_POLE)
	{

		float f_lp2 = filter->f * 2.21f;
		for(i=0;i<size;i++)
		{
			/* alternative 2Pole LP filter to fix the kick transient problems with the nonlinear ZDF LP */

			float x = softClipTwo((buf[i]/((float)0x7fff))*filter->drive);
			//float q = (1-filter->q) *2.5 ;/// (1.0 - filter->f);
			float q = (1-filter->q) *1.4 + (1-filter->q) / (1.0f - f_lp2);

			filter->a += f_lp2 * ((x - filter->a)  + q * (filter->a - filter->b ));
			if(filter->a > 1) filter->a = 1;
			else if(filter->a < -1) filter->a = -1;

			filter->b  += f_lp2 * (filter->a - filter->b );
			if(filter->b > 1) filter->b = 1;
			else if(filter->b < -1) filter->b = -1;

			int32_t tmp;
			tmp = (filter->b  * FILTER_GAIN);
			buf[i] = __SSAT(tmp,16);
		}

	} else {
		/*
		 * Batched ZDF solver: two divisions per sample (S073 Step 1).
		 *
		 * What:       computes the nonlinear trapezoidal SVF with two reciprocal
		 *             divisions: Stage A shares the input and t1 denominator;
		 *             Stage B shares t0, g0 and y1. The output switch and all
		 *             int16 saturation points remain unchanged.
		 * Why:        audit F1. Exact algebra lowers the dependent VDIV chain;
		 *             only float rounding changes (class S1).
		 * Inputs:     input block, filter coefficients, drive and s1/s2/zi state.
		 * Outputs:    buf plus s1/s2/zi, written back once after the loop.
		 * Accessors:  DrumVoice.c, Snare.c, CymbalVoice.c and HiHat.c.
		 * Affiliates: svf_padeNum()/svf_padeDen() and the golden filter
		 *             harness. (The float twin and its Effect client were
		 *             removed in S095 P4 T3.)
		 * S095 P4 F4: the type's floor offset (svf_driveFloorOffset[]) is
		 *             added here, once per block.
		 */
		const float drive = filter->drive +
		                    svf_driveFloorOffset[(type < 9u) ? type : 0u];
		float s1 = filter->s1;
		float s2 = filter->s2;
		float zi = filter->zi;

		for(i=0;i<size;i++)
		{
			const float u    = (buf[i]/((float)0x7fff))*drive;
			const float ax   = 0.25f*u*u;
			const float a1   = 0.25f*s1*s1;
			const float Nx   = svf_padeNum(ax);
			const float Dx   = svf_padeDen(ax);
			const float N1   = svf_padeNum(a1);
			const float D1   = svf_padeDen(a1);
			const float invA = 1.f / (Dx*D1);
			const float x    = u*Nx*D1*invA;
			const float t1   = N1*Dx*invA;

			/* input with half sample delay, for non-linearities */
			const float ih = 0.5f * (x + zi);
			zi = x;

			const float v0   = 0.5f * (ih - 2*R*s1 - s2);
			const float a0   = v0*v0;
			const float N0   = svf_padeNum(a0);
			const float D0   = svf_padeDen(a0);
			const float E    = D0 + f*2*R*N0;
			const float P    = ff*N0*t1;
			const float F    = P + E;
			const float Q    = P*x + s2*E + f*D0*t1*s1;
			const float invB = 1.f / (D0*E*F);
			const float t0   = N0*E*F*invB;
			const float g0   = D0*D0*F*invB;
			const float y1   = Q*D0*E*invB;

			const float s1t1 = s1*t1;
			const float xx   = t0*(x - y1);
			const float y0   = (s1t1 + f*xx)*g0;
			s1 = s1t1 + 2*f*(xx - t0*2*R*y0);
			s2 = s2 + 2*f*t1*y0;

			int32_t tmp;
			switch(type)
			{
			default:
				filter->s1 = s1;
				filter->s2 = s2;
				filter->zi = zi;
				return;
			case FILTER_LP:
				tmp = fastTanh(y1) * 0x7fff;
				buf[i] = __SSAT(tmp,16);
				break;
			case FILTER_HP:
			{
				const float ugb = 2*R*y0;
				const float h = x - ugb - y1;
				tmp = h * FILTER_GAIN;
				buf[i] = __SSAT(tmp,16);
			}
				break;
			case FILTER_BP:
				tmp = y0 * FILTER_GAIN;
				buf[i] = __SSAT(tmp,16);
				break;
			case FILTER_UNITY_BP:
			{
				const float ugb = 2*R*y0;
				tmp = ugb * FILTER_GAIN;
				buf[i] = __SSAT(tmp,16);
			}
				break;
			case FILTER_NOTCH:
			{
				const float ugb = 2*R*y0;
				tmp = (x-ugb) * FILTER_GAIN;
				buf[i] = __SSAT(tmp,16);
			}
				break;
			case FILTER_PEAK:
			{
				const float ugb = 2*R*y0;
				const float h = x - ugb - y1;
				tmp = (y1-h) * FILTER_GAIN;
				buf[i] = __SSAT(tmp,16);
			}
				break;
			}
		}
		filter->s1 = s1;
		filter->s2 = s2;
		filter->zi = zi;
	}
}
