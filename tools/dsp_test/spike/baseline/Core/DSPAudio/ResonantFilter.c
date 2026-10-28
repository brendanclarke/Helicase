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

		filter->drive = 0.5f;

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
void SVF_setDrive(ResonantFilter* filter,uint8_t drive)
{
#if USE_SHAPER_NONLINEARITY
	filter->drive = drive;
	setDistortionShape(&filter->shaper, filter->drive);
#else
	filter->drive =  0.4f + (drive/127.f)*(drive/127.f)*6;
#endif

}
//------------------------------------------------------------------------------------
void SVF_directSetFilterValue(ResonantFilter* filter, float val)
{
	filter->f = val*(0.5f*0.90f);
	filter->g  = fastTan(M_PI * filter->f );

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
 * Accessors:  SVF_calcBlockZDF() and SVF_calcBlockZDFFloat().
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
 * Affiliates: SVF_calcBlockZDFFloat() retains its own shaper guard.
 */
#if !ENABLE_NONLINEAR_INTEGRATORS || USE_SHAPER_NONLINEARITY
#error "Batched ZDF solver (S073 Step 1) requires nonlinear integrators and no shaper"
#endif
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
		 * Affiliates: the float twin below, svf_padeNum()/svf_padeDen(),
		 *             StereoFilterEffect.c coefficient linking, and the golden
		 *             filter harness.
		 */
		const float drive = filter->drive;
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
//------------------------------------------------------------------------------------
/*
 * Float-I/O counterpart of SVF_calcBlockZDF (Session 072 step 4).
 *
 * The state equations intentionally mirror the int16 path above. Inputs are
 * already normalized, outputs remain float, and the int16 __SSAT conversion is
 * replaced with normalized scaling. The Effect bus owns final saturation after
 * processing; voice callers continue using SVF_calcBlockZDF unchanged.
 */
#if USE_SHAPER_NONLINEARITY
#error "SVF_calcBlockZDFFloat mirrors only the non-shaper configuration"
#endif
INITCM_EFFECT_NOINLINE void SVF_calcBlockZDFFloat(ResonantFilter* filter,
                                                  const uint8_t type,
                                                  float* buf,
                                                  const uint8_t size)
{
    const float out_gain = (float)FILTER_GAIN / 32767.0f;
    const float f = filter->g;
    const float R = filter->f >= 0.4499f ? 1.0f : filter->q;
    const float ff = f * f;
    uint8_t i;

    /* Unlike the legacy int path, an invalid/off type is true pass-through. */
    if (type != FILTER_NAIVE_2_POLE &&
        (type < FILTER_LP || type > FILTER_PEAK))
        return;

    if (type == FILTER_NAIVE_2_POLE) {
        const float f_lp2 = filter->f * 2.21f;

        for (i = 0u; i < size; i++) {
            const float x = softClipTwo(buf[i] * filter->drive);
            const float q = (1.0f - filter->q) * 1.4f +
                            (1.0f - filter->q) / (1.0f - f_lp2);

            filter->a += f_lp2 * ((x - filter->a) +
                                  q * (filter->a - filter->b));
            if (filter->a > 1.0f)
                filter->a = 1.0f;
            else if (filter->a < -1.0f)
                filter->a = -1.0f;
            filter->b += f_lp2 * (filter->a - filter->b);
            if (filter->b > 1.0f)
                filter->b = 1.0f;
            else if (filter->b < -1.0f)
                filter->b = -1.0f;
            buf[i] = filter->b * out_gain;
        }
        return;
    }

    /*
     * Batched ZDF solver, float I/O twin (S073 Step 1).
     *
     * What:       mirrors the int16 two-division solver for normalized float
     *             samples and retains the existing out_gain output scaling.
     * Why:        StereoFilter runs two instances per block; it must receive
     *             the same constant-cost division reduction (class S1).
     * Inputs:     float block, coefficients, drive and filter state.
     * Outputs:    float block and s1/s2/zi written back once after the loop.
     * Accessors:  StereoFilterEffect.c left/right filter instances.
     * Affiliates: SVF_calcBlockZDF(), svf_padeNum()/svf_padeDen(), and the
     *             golden float filter comparison.
     */
    {
        const float drive = filter->drive;
        float s1 = filter->s1;
        float s2 = filter->s2;
        float zi = filter->zi;

        for (i = 0u; i < size; i++) {
            const float u    = buf[i] * drive;
            const float ax   = 0.25f * u * u;
            const float a1   = 0.25f * s1 * s1;
            const float Nx   = svf_padeNum(ax);
            const float Dx   = svf_padeDen(ax);
            const float N1   = svf_padeNum(a1);
            const float D1   = svf_padeDen(a1);
            const float invA = 1.0f / (Dx * D1);
            const float x    = u * Nx * D1 * invA;
            const float t1   = N1 * Dx * invA;
            const float ih   = 0.5f * (x + zi);
            zi = x;
            const float v0   = 0.5f * (ih - 2.0f * R * s1 - s2);
            const float a0   = v0 * v0;
            const float N0   = svf_padeNum(a0);
            const float D0   = svf_padeDen(a0);
            const float E    = D0 + f * 2.0f * R * N0;
            const float P    = ff * N0 * t1;
            const float F    = P + E;
            const float Q    = P * x + s2 * E + f * D0 * t1 * s1;
            const float invB = 1.0f / (D0 * E * F);
            const float t0   = N0 * E * F * invB;
            const float g0   = D0 * D0 * F * invB;
            const float y1   = Q * D0 * E * invB;
            const float s1t1 = s1 * t1;
            const float xx   = t0 * (x - y1);
            const float y0   = (s1t1 + f * xx) * g0;

            s1 = s1t1 + 2.0f * f * (xx - t0 * 2.0f * R * y0);
            s2 += 2.0f * f * t1 * y0;

            switch (type) {
            case FILTER_LP:
                buf[i] = fastTanh(y1);
                break;
            case FILTER_HP:
                buf[i] = (x - 2.0f * R * y0 - y1) * out_gain;
                break;
            case FILTER_BP:
                buf[i] = y0 * out_gain;
                break;
            case FILTER_UNITY_BP:
                buf[i] = 2.0f * R * y0 * out_gain;
                break;
            case FILTER_NOTCH:
                buf[i] = (x - 2.0f * R * y0) * out_gain;
                break;
            case FILTER_PEAK:
                buf[i] = (y1 - (x - 2.0f * R * y0 - y1)) * out_gain;
                break;
            default:
                break;
            }
        }
        filter->s1 = s1;
        filter->s2 = s2;
        filter->zi = zi;
    }
}
