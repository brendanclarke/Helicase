/*
 * mixer.c
 *
 *  Created on: 11.04.2012
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


#include "mixer.h"
#include "config.h"
#include "AudioCodecManager.h"
#include "CymbalVoice.h"
#include "DrumVoice.h"
#include "Snare.h"
#include "HiHat.h"
#include "BufferTools.h"
#include "squareRootLut.h"
#include "adcPots.h"
// TODO DSP_PORT
// #include "../Hardware/TriggerOut.h"
//-----------------------------------------------------------------------
INCCMZ uint8_t mixer_audioRouting[6];
INCCMZ float mixer_slider_last_gain[6];
static volatile uint8_t mixer_out_l1_available = 1u; /* PD6 */
static volatile uint8_t mixer_out_r1_available = 1u; /* PD7 */
static volatile uint8_t mixer_out_l2_available = 1u; /* PB4 */
static volatile uint8_t mixer_out_r2_available = 1u; /* PB6 */
//-----------------------------------------------------------------------
#if USE_DECIMATOR
INCCMZ float mixer_decimation_rate[7];		/**<sets the sample rate decimation. 0..1 = full rate*/
INCCMZ float mixer_decimation_cnt[6];		/**<s'n'h counter for decimator*/
INCCMZ int16_t mixer_voice_samples[6];		/**< stores the last outputted sample of the 6 voices*/
#endif
//-----------------------------------------------------------------------
void mixer_init()
{
#if USE_DECIMATOR
	int i;
	for(i=0;i<6;i++)
	{
		mixer_decimation_rate[i] 	= 1;
		mixer_decimation_cnt[i] 	= 0;
		mixer_voice_samples[i] 		= 0;
		mixer_audioRouting[i]		= 0;
		mixer_slider_last_gain[i]   = slider_vol[i];
	}
	mixer_decimation_rate[6] 		= 1;
#endif
}
//-----------------------------------------------------------------------
void mixer_decimateBlock(const uint8_t voiceNr, int16_t* buffer)
{
	uint8_t i;
	for(i=0;i<OUTPUT_DMA_SIZE;i++)
	{
		mixer_decimation_cnt[voiceNr] += mixer_decimation_rate[voiceNr]*mixer_decimation_rate[6];
		if(mixer_decimation_cnt[voiceNr] >= 1.f)
		{
			mixer_decimation_cnt[voiceNr] -= 1.f;
			mixer_voice_samples[voiceNr] = buffer[i];

		}
		buffer[i] = mixer_voice_samples[voiceNr];
	}
}
//-----------------------------------------------------------------------
void mixer_setOutJackDetectPB(uint8_t pb4_high, uint8_t pb6_high)
{
	mixer_out_l2_available = (uint8_t)(pb4_high != 0u);
	mixer_out_r2_available = (uint8_t)(pb6_high != 0u);
}

void mixer_setOutJackDetectPD(uint8_t pd6_high, uint8_t pd7_high)
{
	mixer_out_l1_available = (uint8_t)(pd6_high != 0u);
	mixer_out_r1_available = (uint8_t)(pd7_high != 0u);
}

//-----------------------------------------------------------------------
uint8_t mixer_checkOutJackAvailable(uint8_t dest)
{
	//read input pins
	// for efficiency, these are split: The PB pins are read each TIM6 cycle
	// alongside the shift-registers. The PD pin reads are hardware interrupt
	// alongside the clk and rst inputs on EXTI. 
	uint8_t l1_Available = mixer_out_l1_available;
	uint8_t r1_Available = mixer_out_r1_available;
	uint8_t l2_Available = mixer_out_l2_available;
	uint8_t r2_Available = mixer_out_r2_available;

	// switch needs some extra logic to deal with the assumption
	// that if NOTHING seems to be connected, we assume the 
	// headphone jack is being used and just throw everything
	// on DAC 1 in whatever L/R/stereo mode it had :p
	switch(dest)
		{

		case MIXER_ROUTING_DAC1_STEREO:
			// at least 1 of MAIN plugged in, route to main
			if(r1_Available || l1_Available) {
				return dest;
			} 
			// neither of MAIN isn't plugged in but something
			// on OUT2 is, route to OUT2
			else if (r2_Available || l2_Available)
			{
				return MIXER_ROUTING_DAC2_STEREO;
			}
			// nothing is plugged in, assume headphones,
			// route to MAIN
			else
			{
				return MIXER_ROUTING_DAC1_STEREO;
			}
			break;

		case MIXER_ROUTING_DAC2_STEREO:
			// at least one of OUT2 plugged in, route to
			// OUT2 
			if(r2_Available || l2_Available) {
				return dest;
			}
			// nothing on OUT2 is plugged in - either something
			// on MAIN or headphones, don't care which.  
			else 
			{
				return MIXER_ROUTING_DAC1_STEREO;
			}
			break;

		case MIXER_ROUTING_DAC1_L:
			// that output is available, send it
			if(l1_Available) {
				return dest;
			} 
			else if (r1_Available) {
				return MIXER_ROUTING_DAC1_R;
			} else if (l2_Available) {
				return MIXER_ROUTING_DAC2_L;
			} else if (r2_Available) {
				return MIXER_ROUTING_DAC2_R;
			}
			// nothing connected: default to MAIN for headphone 
			else return MIXER_ROUTING_DAC1_L;
			break;

		case MIXER_ROUTING_DAC1_R:
			if(r1_Available) {
				return dest;
			} else if (l1_Available) {
				return MIXER_ROUTING_DAC1_L;
			} else if (l2_Available) {
				return MIXER_ROUTING_DAC2_L;
			} else if (r2_Available) {
				return MIXER_ROUTING_DAC2_R;
			}
			// nothing connected: default to MAIN for headphone 
			else return MIXER_ROUTING_DAC1_R;
			break;

		case MIXER_ROUTING_DAC2_L:
			if(l2_Available) {
				return dest;
			} else if (r2_Available) {
				return MIXER_ROUTING_DAC2_R;
			} else if (l1_Available) {
				return MIXER_ROUTING_DAC1_L;
			} else if (r1_Available) {
				return MIXER_ROUTING_DAC1_R;
			}
			// nothing connected: default to MAIN for headphone 
			else return MIXER_ROUTING_DAC1_L;
			break;

		case MIXER_ROUTING_DAC2_R:
			if(r2_Available) {
				return dest;
			} else if (l2_Available) {
				return MIXER_ROUTING_DAC2_L;
			} else if (r1_Available) {
				return MIXER_ROUTING_DAC1_R;
			} else if (l1_Available) {
				return MIXER_ROUTING_DAC1_L;
			}
			// nothing connected: default to MAIN for headphone 
			else return MIXER_ROUTING_DAC1_R;
			break;
		}
	return dest;
}
//-----------------------------------------------------------------------
void mixer_moveDataToOutput(uint8_t dest, const float panL, const float panR, sample_mx_t* data,sample_mx_t* outL,sample_mx_t* outR,sample_mx_t* outL2, sample_mx_t* outR2)
{
	//check if a cable is in the selected out
	dest = mixer_checkOutJackAvailable(dest);

	uint8_t i;
	switch(dest)
	{

	case MIXER_ROUTING_DAC1_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outL2 = (sample_mx_t)((float)data[i] * panL);
			outL2 += 2;

			*outR2 = (sample_mx_t)((float)data[i] * panR);
			outR2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outL = (sample_mx_t)((float)data[i] * panL);
			outL += 2;

			*outR = (sample_mx_t)((float)data[i] * panR);
			outR += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outL2 = data[i];
			outL2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outR2 = data[i];
			outR2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outL = data[i];
			outL += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outR = data[i];
			outR += 2;
		}
		break;
	}
}
//-----------------------------------------------------------------------
void mixer_addDataToOutput(uint8_t dest, const float panL, const float panR,  sample_mx_t* data,sample_mx_t* outL,sample_mx_t* outR,sample_mx_t* outL2, sample_mx_t* outR2)
{
	//check if a cable is in the selected out
	dest = mixer_checkOutJackAvailable(dest);

	//TODO may be possible tooptimize here using both halfwordsof qadd for stereo mixing
	uint8_t i;
	switch(dest)
	{

	case MIXER_ROUTING_DAC1_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outL2 = bufferTool_satAdd32(*outL2, (sample_mx_t)((float)data[i] * panL));
			outL2 += 2;

			*outR2 = bufferTool_satAdd32(*outR2, (sample_mx_t)((float)data[i] * panR));
			outR2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outL = bufferTool_satAdd32(*outL, (sample_mx_t)((float)data[i] * panL));
			outL += 2;

			*outR = bufferTool_satAdd32(*outR, (sample_mx_t)((float)data[i] * panR));
			outR += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outL2 = bufferTool_satAdd32(*outL2, data[i]);
			outL2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outR2 = bufferTool_satAdd32(*outR2, data[i]);
			outR2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outL = bufferTool_satAdd32(*outL, data[i]);
			outL += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			*outR = bufferTool_satAdd32(*outR, data[i]);
			outR += 2;
		}
		break;

	}
}
//-----------------------------------------------------------------------
static void mixer_addVoiceInt16ToOutput(uint8_t dest,
		const float panL,
		const float panR,
		const int16_t* data,
		const float gain,
		const float lastGain,
		sample_mx_t* outL,
		sample_mx_t* outR,
		sample_mx_t* outL2,
		sample_mx_t* outR2)
{
	/* Session 023 fused three formerly separate per-voice passes:
	**   1. interpolate slider_vol from the previous 32-frame block,
	**   2. convert legacy int16 voice output into signed-24 sample_mx_t,
	**   3. pan/route/add to the four output buses.
	**
	** Keeping this as one loop preserves sound quality while reducing memory
	** traffic and repeated routing work in the hot mixer path. */
	uint8_t i;
	const float inv_size = 1.f / (OUTPUT_DMA_SIZE - 1.f);
	const float gain_delta = gain - lastGain;

	switch(dest)
	{
	case MIXER_ROUTING_DAC1_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			const sample_mx_t sm = sampleMix_fromInt16(s16);
			*outL2 = bufferTool_satAdd32(*outL2, (sample_mx_t)((float)sm * panL));
			outL2 += 2;
			*outR2 = bufferTool_satAdd32(*outR2, (sample_mx_t)((float)sm * panR));
			outR2 += 2;
		}
		break;

	case MIXER_ROUTING_DAC2_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			const sample_mx_t sm = sampleMix_fromInt16(s16);
			*outL = bufferTool_satAdd32(*outL, (sample_mx_t)((float)sm * panL));
			outL += 2;
			*outR = bufferTool_satAdd32(*outR, (sample_mx_t)((float)sm * panR));
			outR += 2;
		}
		break;

	case MIXER_ROUTING_DAC1_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			*outL2 = bufferTool_satAdd32(*outL2, sampleMix_fromInt16(s16));
			outL2 += 2;
		}
		break;

	case MIXER_ROUTING_DAC1_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			*outR2 = bufferTool_satAdd32(*outR2, sampleMix_fromInt16(s16));
			outR2 += 2;
		}
		break;

	case MIXER_ROUTING_DAC2_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			*outL = bufferTool_satAdd32(*outL, sampleMix_fromInt16(s16));
			outL += 2;
		}
		break;

	case MIXER_ROUTING_DAC2_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			*outR = bufferTool_satAdd32(*outR, sampleMix_fromInt16(s16));
			outR += 2;
		}
		break;
	}
}
//-----------------------------------------------------------------------
// Test Stub for Audio DMA
//-----------------------------------------------------------------------
// Sine generator state
// ----------------------------------------------------------------------- 
#define SINE_HZ   440.0f
#define SINE_AMP  16000
#define TWO_PI    6.28318530718f

static float sine_phase = 0.0f;
static const float sine_phase_inc = TWO_PI * SINE_HZ / 44108.0f;
void mixer_calcNextSampleBlockTest(sample_mx_t *output, sample_mx_t *output2)
{
    sample_mx_t sampleData[OUTPUT_DMA_SIZE];
    const uint8_t pos = 0;

    bufferTool_clearBuffer32(output,  OUTPUT_DMA_SIZE * 2);
    bufferTool_clearBuffer32(output2, OUTPUT_DMA_SIZE * 2);

    for (uint32_t i = 0; i < OUTPUT_DMA_SIZE; i++) {
        int16_t s = (int16_t)(sinf(sine_phase) * SINE_AMP);
        sine_phase += sine_phase_inc;
        if (sine_phase >= TWO_PI) sine_phase -= TWO_PI;
        sampleData[i] = sampleMix_fromInt16(s);
    }

    mixer_addDataToOutput(MIXER_ROUTING_DAC1_STEREO, 1.0f, 1.0f,
        sampleData, &output[pos], &output[pos+1], &output2[pos], &output2[pos+1]);
    mixer_addDataToOutput(MIXER_ROUTING_DAC2_STEREO, 1.0f, 1.0f,
        sampleData, &output[pos], &output[pos+1], &output2[pos], &output2[pos+1]);
}

// void mixer_calcNextSampleBlockTest(int16_t *output, int16_t *output2)
// {
//     for (uint32_t i = 0; i < OUTPUT_DMA_SIZE; i++) {
//         int16_t s = (int16_t)(sinf(sine_phase) * SINE_AMP);
//         sine_phase += sine_phase_inc;
//         if (sine_phase >= TWO_PI) sine_phase -= TWO_PI;
//         output [2*i + 0] = s;
//         output [2*i + 1] = s;
//         output2[2*i + 0] = s;
//         output2[2*i + 1] = s;
//     }
// }
//-----------------------------------------------------------------------
void mixer_calcNextSampleBlock(sample_mx_t* output,sample_mx_t* output2)
{

	modNode_resetTargets();
	//re assign velocity modulation
	modNode_reassignVeloMod();

	//calc and dispatch LFO
	lfo_dispatchNextValue(&voiceArray[0].lfo);
	lfo_dispatchNextValue(&voiceArray[1].lfo);
	lfo_dispatchNextValue(&voiceArray[2].lfo);
	lfo_dispatchNextValue(&snareVoice.lfo);
	lfo_dispatchNextValue(&cymbalVoice.lfo);
	lfo_dispatchNextValue(&hatVoice.lfo);

	//update filter frequencies
	SVF_recalcFreq(&voiceArray[0].filter);
	SVF_recalcFreq(&voiceArray[1].filter);
	SVF_recalcFreq(&voiceArray[2].filter);
	SVF_recalcFreq(&snareVoice.filter);
	SVF_recalcFreq(&cymbalVoice.filter);
	SVF_recalcFreq(&hatVoice.filter);

	//--- Calc async -----
	calcDrumVoiceAsync(0);
	calcDrumVoiceAsync(1);
	calcDrumVoiceAsync(2);

	Snare_calcAsync();
	Cymbal_calcAsync();
	HiHat_calcAsync();

	//calculate trigger io phase
	// TODO DSP_PORT
	// trigger_tickPhaseCounter();

	//an array to store intermediate voice samples
	//befor output distribution
	int16_t sampleData[OUTPUT_DMA_SIZE];
	uint8_t effectiveRouting[6];

	const uint8_t pos = 0;
	/* Snapshot jack-dependent routing once per 32-frame block. The cached
	** PD/PB detect inputs can change asynchronously, but per-sample routing
	** checks are unnecessary and costlier than one block-level decision. */
	for(uint8_t i=0;i<6;i++)
		effectiveRouting[i] = mixer_checkOutJackAvailable(mixer_audioRouting[i]);

	bufferTool_clearBuffer32(output,OUTPUT_DMA_SIZE*2);
	bufferTool_clearBuffer32(output2,OUTPUT_DMA_SIZE*2);

	// //---------------------------------------
	// // TEST BLOCK - Calc and add test sine tone
	// for (uint32_t i = 0; i < OUTPUT_DMA_SIZE; i++) {
    //     int16_t s = (int16_t)(sinf(sine_phase) * SINE_AMP);
    //     sine_phase += sine_phase_inc;
    //     if (sine_phase >= TWO_PI) sine_phase -= TWO_PI;
    //     sampleData[i] = s;
    // }
    // mixer_addDataToOutput(MIXER_ROUTING_DAC1_STEREO, 1.0f, 1.0f,
    //     sampleData, &output[pos], &output[pos+1], &output2[pos], &output2[pos+1]);
    // mixer_addDataToOutput(MIXER_ROUTING_DAC2_STEREO, 1.0f, 1.0f,
    //     sampleData, &output[pos], &output[pos+1], &output2[pos], &output2[pos+1]);
	// // END TEST BLOCK
	// //----------------------------------------


	//calc voice 1
	calcDrumVoiceSyncBlock(0, sampleData,OUTPUT_DMA_SIZE);
	//decimate voice
	
	mixer_decimateBlock(0,sampleData);
	mixer_addVoiceInt16ToOutput(effectiveRouting[0], squareRootLut[127-voiceArray[0].pan],
			squareRootLut[voiceArray[0].pan], sampleData, slider_vol[0], mixer_slider_last_gain[0],
			&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
	mixer_slider_last_gain[0] = slider_vol[0];

	//calc voice 2
	calcDrumVoiceSyncBlock(1, sampleData,OUTPUT_DMA_SIZE);
	//decimate voice
	mixer_decimateBlock(1,sampleData);
	mixer_addVoiceInt16ToOutput(effectiveRouting[1], squareRootLut[127-voiceArray[1].pan],
			squareRootLut[voiceArray[1].pan], sampleData, slider_vol[1], mixer_slider_last_gain[1],
			&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
	mixer_slider_last_gain[1] = slider_vol[1];

	//calc voice 3
	calcDrumVoiceSyncBlock(2, sampleData,OUTPUT_DMA_SIZE);
	//decimate voice
	mixer_decimateBlock(2,sampleData);
	mixer_addVoiceInt16ToOutput(effectiveRouting[2], squareRootLut[127-voiceArray[2].pan],
			squareRootLut[voiceArray[2].pan], sampleData, slider_vol[2], mixer_slider_last_gain[2],
			&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
	mixer_slider_last_gain[2] = slider_vol[2];

	//calc snare
	Snare_calcSyncBlock(sampleData,OUTPUT_DMA_SIZE);
	//decimate voice
	mixer_decimateBlock(3,sampleData);
	mixer_addVoiceInt16ToOutput(effectiveRouting[3], squareRootLut[127-snareVoice.pan],
			squareRootLut[snareVoice.pan], sampleData, slider_vol[3], mixer_slider_last_gain[3],
			&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
	mixer_slider_last_gain[3] = slider_vol[3];

	//calc cymbal
	Cymbal_calcSyncBlock(sampleData,OUTPUT_DMA_SIZE);
	//decimate voice
	mixer_decimateBlock(4,sampleData);
	mixer_addVoiceInt16ToOutput(effectiveRouting[4], squareRootLut[127-cymbalVoice.pan],
			squareRootLut[cymbalVoice.pan], sampleData, slider_vol[4], mixer_slider_last_gain[4],
			&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
	mixer_slider_last_gain[4] = slider_vol[4];

	//calc HiHat
	HiHat_calcSyncBlock(sampleData,OUTPUT_DMA_SIZE);
	//decimate voice
	mixer_decimateBlock(5,sampleData);
	mixer_addVoiceInt16ToOutput(effectiveRouting[5], squareRootLut[127-hatVoice.pan],
			squareRootLut[hatVoice.pan], sampleData, slider_vol[5], mixer_slider_last_gain[5],
			&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
	mixer_slider_last_gain[5] = slider_vol[5];

}
