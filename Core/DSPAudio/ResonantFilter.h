/*
 * ResonantFilter.h
 *
 *  Created on: 05.04.2012
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


#ifndef RESONANTFILTER_H_
#define RESONANTFILTER_H_

/*
 * ResonantFilter.c
 *
 *  Created on: 05.04.2012
 *      Author: Julian
 */
//----------------------------------------------------
#include "globals.h"
#include "config.h"
#include "stm32f4xx.h"
#include "datatypes.h"
#include "math.h"
#include "distortion.h"
//----------------------------------------------------
#define ENABLE_NONLINEAR_INTEGRATORS 	1
#define FILTER_GAIN 					0x70ff

#define USE_SHAPER_NONLINEARITY 0


//----------------------------------------------------
enum filterTypeEnum
{
	FILTER_LP=1,
	FILTER_HP,
	FILTER_BP,
	FILTER_UNITY_BP,
	FILTER_NOTCH,
	FILTER_PEAK,
	FILTER_NAIVE_2_POLE
};
//----------------------------------------------------
typedef struct ResoFilterStruct
{
	float f;	/**< cutoff frequency as integer from 0 to 0xffff where 0xffff = SR*/
	float g;    /**< embedded integrator gain (Fig 3.11), wc == wa*/
	float q;	/**< q value calculated from setReso()*/

	float s1;
	float s2;

	float a,b;
#if ENABLE_NONLINEAR_INTEGRATORS
	float zi;	//input z^(-1)
#endif

	float drive;

#if USE_SHAPER_NONLINEARITY
	Distortion shaper;
#endif
} ResonantFilter;
//------------------------------------------------------------------------------------
void SVF_setReso(ResonantFilter* filter, float feedback);
//------------------------------------------------------------------------------------
void SVF_init();
//------------------------------------------------------------------------------------
void SVF_setDrive(ResonantFilter* filter, uint8_t drive);
//------------------------------------------------------------------------------------
void SVF_directSetFilterValue(ResonantFilter* filter, float val);
/*
 * Set only the cutoff target (S088 P0 §3 Layer 2d).
 *
 * What:       computes filter->f exactly as SVF_directSetFilterValue() does
 *             but leaves filter->g untouched, so the per-block
 *             SVF_recalcFreqSlew() can move the coefficient toward it.
 * Why:        SVF_directSetFilterValue() also jumps g. In rst that is correct
 *             (the trigger has just cleared the filter). In fre/itp the state
 *             is kept, so a writer-time jump would defeat the slew and put a
 *             transient at the control-rate write instead of the block
 *             boundary.
 * Inputs:     filter, val (the same shaped value the direct setter takes).
 * Outputs:    filter->f only.
 * Accessors:  InstrumentManager.c IM_SPECIAL_FILTER_FREQ when the slot's
 *             runMode is not rst.
 * Affiliates: SVF_directSetFilterValue(); SVF_recalcFreqSlew().
 */
void SVF_setFrequencyTarget(ResonantFilter* filter, float val);
//------------------------------------------------------------------------------------
void SVF_calcBlockZDF(ResonantFilter* filter, const uint8_t type, int16_t* buf, const uint8_t size);
//------------------------------------------------------------------------------------
/*
 * Float-I/O ZDF block for Effects only (Session 072 step 4).
 *
 * Inputs: the same ResonantFilter state and type domain as SVF_calcBlockZDF,
 * plus normalized float samples where 1.0 equals int16 full scale. Output:
 * the filter writes normalized floats without __SSAT, allowing a future FX
 * bus to preserve summed headroom until its final return saturation. Types
 * 1..7 filter; other types pass through without advancing state. Keep this
 * arithmetic synchronized with SVF_calcBlockZDF. S073 Step 1 uses the same
 * batched two-division solver in both twins; the golden harness compares them
 * against the frozen pre-S073 code. Client: StereoFilterEffect.
 */
void SVF_calcBlockZDFFloat(ResonantFilter* filter, const uint8_t type,
                           float* buf, const uint8_t size);
//------------------------------------------------------------------------------------
void SVF_recalcFreq(ResonantFilter* filter);
/*
 * Filter coefficient slew (S088 P0 §3 Layer 2d).
 *
 * What:       steps filter->g from its current value toward fastTan(pi x f)
 *             by a fixed per-block fraction (OSC_RUN_FILTER_SLEW, 0.28), so
 *             a cutoff change converges over a few blocks instead of jumping.
 *             filter->f must already hold the new cutoff (the special writer
 *             / SVF_directSetFilterValue() set it on the value change).
 * Why:        in rst the trigger clears the filter and SVF_recalcFreq() jumps
 *             g, which is correct because there is no state to disturb. In
 *             fre/itp the state is kept, so a g jump is a transient whose size
 *             follows the frequency change and the resonance; the slew
 *             (about 2 ms) removes it.
 * Inputs:     filter (f, g, drive).
 * Outputs:    filter->g stepped toward the target; the shaper refreshed when
 *             USE_SHAPER_NONLINEARITY is on (same as SVF_recalcFreq).
 * Cost:       one fastTan plus one multiply-add per block, in place of
 *             SVF_recalcFreq(); called once per block per slot either way.
 * Accessors:  instrumentManager_recalcSlotFilter() when runMode != rst.
 * Affiliates: SVF_recalcFreq(); the engine runMode bytes; A2 (0.28 is the
 *             starting value and may be tuned on hardware).
 */
void SVF_recalcFreqSlew(ResonantFilter* filter);
//------------------------------------------------------------------------------------
void SVF_reset(ResonantFilter* filter);

#endif /* RESONANTFILTER_H_ */
