/*
 * BufferTools.c
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



#include "BufferTools.h"
#include <string.h>

//TODO DSP_PORT
// all static inline void declarations to void declarations

//---------------------------------------------------
void bufferTool_addBuffers(int16_t* buf1, int16_t* buf2, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		buf1[i] += buf2[i];
	}
}
//---------------------------------------------------
void bufferTool_addBuffersSaturating(int16_t* buf1, int16_t* buf2, const uint8_t size)
{
	uint8_t i;
	/* Two signed 16-bit samples are processed per word with ARM's packed
	** saturating add. memcpy keeps this safe for unaligned int16_t buffers
	** and strict-aliasing/LTO while still allowing the compiler to emit the
	** intended single-word load, QADD16, store sequence. */
	for(i=0;i + 1u < size;i = (uint8_t)(i + 2u))
	{
		uint32_t a;
		uint32_t b;
		uint32_t r;
		__builtin_memcpy(&a, &buf1[i], sizeof(a));
		__builtin_memcpy(&b, &buf2[i], sizeof(b));
		r = __QADD16(a, b);
		__builtin_memcpy(&buf1[i], &r, sizeof(r));
	}
	for(;i<size;i++)
	{
		buf1[i] = bufferTool_satAdd16(buf1[i], buf2[i]);
	}
}
//---------------------------------------------------
void bufferTool_addBuffersSaturatingWithGain(int16_t* buf1, int16_t* buf2, const float gain, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		buf1[i] = bufferTool_satAdd16(buf1[i], buf2[i]) * gain;
	}
}
//---------------------------------------------------
void bufferTool_subBuffersSaturating(int16_t* buf1, int16_t* buf2, const uint8_t size)
{
	uint8_t i;
	/* Packed saturating subtract, matching the add path above. */
	for(i=0;i + 1u < size;i = (uint8_t)(i + 2u))
	{
		uint32_t a;
		uint32_t b;
		uint32_t r;
		__builtin_memcpy(&a, &buf1[i], sizeof(a));
		__builtin_memcpy(&b, &buf2[i], sizeof(b));
		r = __QSUB16(a, b);
		__builtin_memcpy(&buf1[i], &r, sizeof(r));
	}
	for(;i<size;i++)
	{
		buf1[i] = bufferTool_satSub16(buf1[i], buf2[i]);
	}
}
//---------------------------------------------------
void bufferTool_copyWithGain(int16_t* buf1, int16_t* buf2, float gain, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		buf1[i] = buf2[i]*gain;
	}
}
//---------------------------------------------------
void bufferTool_clearBuffer(int16_t* buf, const uint8_t size)
{
	memset(buf, 0, (size_t)size * sizeof(*buf));
}
//---------------------------------------------------
void bufferTool_addGain(int16_t* buf, const float gain, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		buf[i] *= gain;
	}
}
//---------------------------------------------------
void bufferTool_addGainDithered(Dither* dither, int16_t* buf, const float gain, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		buf[i] = dither_process(dither,(buf[i]/32768.0f) * gain);
	}
}
//---------------------------------------------------
void bufferTool_addGainInterpolated(int16_t* buf, const float gain, const float lastGain, const uint8_t size)
{
	uint8_t i;
	const float inv_size = 1.f/(size-1.f);
	for(i=0;i<size;i++)
	{
		const float frac = i * inv_size;
		const float currentGain = lastGain + frac*(gain - lastGain);
		buf[i] = buf[i] * currentGain;
	}
}
//---------------------------------------------------
void bufferTool_mulInt(int16_t* buf, const int16_t gain, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		buf[i] *= gain;
	}
}
//---------------------------------------------------
void bufferTool_multiplyWithFloatBuffer(int16_t* buf, float* fltBuf, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		buf[i] *= fltBuf[i];
	}
}
//---------------------------------------------------
void bufferTool_multiplyWithFloatBufferDithered(Dither* dither, int16_t* buf, float* fltBuf, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		buf[i] = dither_process(dither,(buf[i]/32768.0f) * fltBuf[i]);

	}
}
//---------------------------------------------------
void bufferTool_moveBuffer(int16_t* dst, int16_t* src, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		dst[i] = src[i];
	}
}
//---------------------------------------------------
void bufferTool_clearBuffer32(sample_mx_t* buf, const uint8_t size)
{
	memset(buf, 0, (size_t)size * sizeof(*buf));
}
//---------------------------------------------------
void bufferTool_addGain32(sample_mx_t* buf, const float gain, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		buf[i] = (sample_mx_t)((float)buf[i] * gain);
	}
}
//---------------------------------------------------
void bufferTool_addGainInterpolated32(sample_mx_t* buf, const float gain, const float lastGain, const uint8_t size)
{
	uint8_t i;
	const float inv_size = 1.f/(size-1.f);
	for(i=0;i<size;i++)
	{
		const float frac = i * inv_size;
		const float currentGain = lastGain + frac*(gain - lastGain);
		buf[i] = (sample_mx_t)((float)buf[i] * currentGain);
	}
}
//---------------------------------------------------
void bufferTool_convertInt16ToSampleMix(sample_mx_t* dst, const int16_t* src, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		dst[i] = sampleMix_fromInt16(src[i]);
	}
}
//---------------------------------------------------
