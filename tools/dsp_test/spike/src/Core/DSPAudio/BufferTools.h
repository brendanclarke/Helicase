/*
 * BufferTools.h
 *
 *  Created on: 04.01.2013
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


 // TODO DSP_PORT
 // 'inline' removed from all function definitions

#ifndef BUFFERTOOLS_H_
#define BUFFERTOOLS_H_

#include "stm32f4xx.h"
#include "dither.h"
#include "sample_mix.h"

static inline int16_t bufferTool_satAdd16(int16_t a, int16_t b)
{
	return (int16_t)__SSAT((int32_t)a + (int32_t)b, 16);
}

static inline int16_t bufferTool_satSub16(int16_t a, int16_t b)
{
	return (int16_t)__SSAT((int32_t)a - (int32_t)b, 16);
}

/*
 * Register form of an int16 float store (S073 Step 4).
 *
 * What:       performs the existing float-to-int32 conversion and narrowing.
 * Why:        fused post-chains keep every old int16 truncation point without
 *             an intermediate memory round trip.
 * Inputs:     one float expression.
 * Outputs:    the int16 value that the old store would hold.
 * Accessors:  voicePostChain.h and the gain helpers.
 * Affiliates: distortion_curveSample16() and the Step 4 golden/ARM checks.
 */
static inline int16_t bufferTool_floatToInt16Store(const float x)
{
	return (int16_t)(int32_t)x;
}

/*
 * Shared per-sample gain ramp (S073 Step 4).
 *
 * What:       computes the existing lastGain + frac*(gain-lastGain) ramp.
 * Why:        keeps the standalone helper and fused Drum loop's operand order
 *             identical for the S0 comparison.
 * Inputs:     sample index, 1/(size-1), current gain and previous gain.
 * Outputs:    gain for that sample.
 * Accessors:  bufferTool_addGainInterpolated() and voicePost_drum().
 * Affiliates: DrumVoice.c gain bookkeeping and the Step 4 harness.
 */
static inline float bufferTool_interpolatedGain(const uint8_t i,
		const float inv_size, const float gain, const float lastGain)
{
	const float frac = i * inv_size;
	return lastGain + frac*(gain - lastGain);
}

static inline sample_mx_t bufferTool_satAdd32(sample_mx_t a, sample_mx_t b)
{
	int64_t acc = (int64_t)a + (int64_t)b;
	if (acc > INT32_MAX) return INT32_MAX;
	if (acc < INT32_MIN) return INT32_MIN;
	return (sample_mx_t)acc;
}

static inline sample_mx_t bufferTool_satSub32(sample_mx_t a, sample_mx_t b)
{
	int64_t acc = (int64_t)a - (int64_t)b;
	if (acc > INT32_MAX) return INT32_MAX;
	if (acc < INT32_MIN) return INT32_MIN;
	return (sample_mx_t)acc;
}
//---------------------------------------------------
void bufferTool_addBuffers(int16_t* buf1, int16_t* buf2, const uint8_t size);
//---------------------------------------------------
void bufferTool_addBuffersSaturating(int16_t* buf1, int16_t* buf2, const uint8_t size);
//---------------------------------------------------
void bufferTool_addBuffersSaturatingWithGain(int16_t* buf1, int16_t* buf2, const float gain, const uint8_t size);
//---------------------------------------------------
void bufferTool_subBuffersSaturating(int16_t* buf1, int16_t* buf2, const uint8_t size);
//---------------------------------------------------
void bufferTool_copyWithGain(int16_t* buf1, int16_t* buf2, float gain, const uint8_t size);
//---------------------------------------------------
void bufferTool_clearBuffer(int16_t* buf, const uint8_t size);
//---------------------------------------------------
void bufferTool_addGain(int16_t* buf, const float gain, const uint8_t size);
//---------------------------------------------------
void bufferTool_addGainDithered(Dither* dither, int16_t* buf, const float gain, const uint8_t size);
//---------------------------------------------------
void bufferTool_addGainInterpolated(int16_t* buf, const float gain, const float lastGain, const uint8_t size);
//---------------------------------------------------
void bufferTool_mulInt(int16_t* buf, const int16_t gain, const uint8_t size);
//---------------------------------------------------
void bufferTool_multiplyWithFloatBuffer(int16_t* buf, float* fltBuf, const uint8_t size);
//---------------------------------------------------
void bufferTool_multiplyWithFloatBufferDithered(Dither* dither, int16_t* buf, float* fltBuf, const uint8_t size);
//---------------------------------------------------
void bufferTool_moveBuffer(int16_t* dst, int16_t* src, const uint8_t size);
//---------------------------------------------------
void bufferTool_clearBuffer32(sample_mx_t* buf, const uint8_t size);
//---------------------------------------------------
void bufferTool_addGain32(sample_mx_t* buf, const float gain, const uint8_t size);
//---------------------------------------------------
void bufferTool_addGainInterpolated32(sample_mx_t* buf, const float gain, const float lastGain, const uint8_t size);
//---------------------------------------------------
void bufferTool_convertInt16ToSampleMix(sample_mx_t* dst, const int16_t* src, const uint8_t size);
//---------------------------------------------------
#endif /* BUFFERTOOLS_H_ */
