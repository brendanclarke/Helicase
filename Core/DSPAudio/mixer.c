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
#include "InstrumentManager.h"
#include "EffectsManager.h"
#include "BufferTools.h"
#include "squareRootLut.h"
#include "adcPots.h"
#include "SceneData.h"
#include "presetManager.h"
/* S074 master bus compressor: final stage of mixer_calcNextSampleBlock(). */
#include "BusCompressor.h"
// TODO DSP_PORT
// #include "../Hardware/TriggerOut.h"
//-----------------------------------------------------------------------
INCCMZ uint8_t mixer_audioRouting[6];
/*
 * Last applied per-slot output gain = slider_vol[slot] x voice volume.
 *
 * What: the gain used at the end of the previous 32-frame block, one float
 * per render slot (24 B DTCM, unchanged allocation; renamed from
 * mixer_slider_last_gain in Session 072 step 2 because it now also carries
 * channel volume). Why: mixer_addVoiceInt16ToOutput() ramps linearly from
 * this value to the current combined gain across the block, so slider AND
 * volume changes (knob, LFO, Morph, automation) are click-free. Inputs:
 * mixer_init() seeds it; mixer_calcNextSampleBlock() updates it after each
 * slot. Affiliates: adcPots.c slider_vol[],
 * instrumentManager_runtimeVolume().
 */
INCCMZ float mixer_voice_last_gain[6];
/*
 * Per-block FX bus storage shared by the voice send accumulation and Effect
 * processing stages.
 *
 * What: two 32-frame channels that are int32 sample_mx_t while voices sum and
 * float while the Effect runs (256 B DTCM). Why: the union keeps the fixed
 * bus allocation while making the conversion in-place. Inputs: decimated
 * voice samples and effective send/fader gains. Output: normalized float
 * buffers supplied to effects_process(). Affiliates: fxbuf_effectShare(),
 * EffectsManager, and mixer_addFxReturnToOutput().
 */
typedef union {
	sample_mx_t mx[2][OUTPUT_DMA_SIZE];
	float f[2][OUTPUT_DMA_SIZE];
} mixer_fx_bus_t;
static INDTCMZ mixer_fx_bus_t mixer_fx_bus;

/*
 * Last effective per-slot FX send gain at the end of the previous block.
 *
 * What: send amount x send-side fader, one float per slot (24 B DTCM). Why:
 * mixer_addVoiceInt16ToOutputAndFx() ramps send changes from knobs, automation, and
 * fader-mode edits without a zipper. Inputs: preset_getEffectiveFxSendAmount()
 * (the step override, otherwise Normal/Morph endpoints interpolated by the
 * voice's resolved Morph amount, S075 F2-H)
 * and SceneData fader_setting. Output: the current block's send ramp state.
 * Affiliate: mixer_calcNextSampleBlock().
 */
static INDTCMZ float mixer_send_last_gain[6];

/*
 * Last effective left/right FX return gains at the end of the previous block.
 *
 * What: level/balance gains for the two return channels (8 B DTCM). Why:
 * the return must remain click-free when the active Effect level, pan, or
 * output routing changes. Inputs: effects_commonRuntime(). Output: ramp
 * origins for mixer_addFxReturnToOutput().
 */
static INDTCMZ float mixer_fx_return_last_gain[2];
static volatile uint8_t mixer_out_l1_available = 1u; /* PD6 */
static volatile uint8_t mixer_out_r1_available = 1u; /* PD7 */
static volatile uint8_t mixer_out_l2_available = 1u; /* PB4 */
static volatile uint8_t mixer_out_r2_available = 1u; /* PB6 */
//-----------------------------------------------------------------------
#if USE_DECIMATOR
INCCMZ float mixer_decimation_rate[6];		/**<sets the per-voice sample rate decimation. 0..1 = full rate*/
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
		/* Seed the ramp at the combined gain so the first block does not fade
		** in; requires instrumentManager_runtimeInit() first (dsp_init order). */
		mixer_voice_last_gain[i]    = slider_vol[i] * instrumentManager_runtimeVolume(i);
	}
#endif
}
//-----------------------------------------------------------------------
void mixer_decimateBlock(const uint8_t voiceNr, int16_t* buffer)
{
	uint8_t i;
	for(i=0;i<OUTPUT_DMA_SIZE;i++)
	{
		/* S075: the retired global multiplier (former rate[6]) is gone. */
		mixer_decimation_cnt[voiceNr] += mixer_decimation_rate[voiceNr];
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
	// for efficiency, the jack-detect pins are sampled by the 500Hz
	// foreground front-panel service and retained here as state.
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
	**   1. interpolate the output gain from the previous 32-frame block
	**      (slider_vol x channel volume since Session 072 step 2; `gain` and
	**      `lastGain` are that combined value),
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

/*
 * Resolve the parallel dry-mix and FX-send gains for one voice slot.
 *
 * Inputs: zero-based Scene slot, active Scene index, and the retained or
 * step-overridden fader/send values. Output: mix_gain feeds the normal routed
 * output and send_gain feeds the pre-volume FX bus. PRE scales both taps,
 * POST scales only the dry mix, FX scales only the send, and XFD (S074)
 * crossfades: the dry tap follows the fader and the send follows the mirrored
 * fader. The voice volume remains on the dry tap for all modes. Affiliate:
 * mixer_calcNextSampleBlock().
 */
/* The Scene domain limit and the last mixer mode must agree (S074). */
_Static_assert(MIXER_FADER_XFD == SCENE_FADER_SETTING_MAX,
		"fader modes: SceneData domain and mixer modes differ");
static void mixer_faderGains(uint8_t slot,
		uint8_t scene_index,
		float *mix_gain,
		float *send_gain)
{
	const float fader = slider_vol[slot];
	const float volume = instrumentManager_runtimeVolume(slot);
	const float send = (float)preset_getEffectiveFxSendAmount(scene_index, slot)
			/ 127.0f;
	const uint8_t mode = scene_getVoiceFaderSetting(scene_index, slot);

	*mix_gain = volume * fader;
	*send_gain = send * fader;
	if (mode == MIXER_FADER_POST) {
		*send_gain = send;
	} else if (mode == MIXER_FADER_FX) {
		*mix_gain = volume;
	} else if (mode == MIXER_FADER_XFD) {
		/*
		 * xfd: crossfade between the FX bus (fader down) and the voice's
		 * normal output (fader up) (S074).
		 *
		 * The dry tap keeps the default volume x fader: at the bottom nothing
		 * reaches the output whatever the volume, and at the top the voice is
		 * at its normal level. The send uses the slider gain at the mirrored
		 * position: at the bottom it equals FX mode at full fader (send x 1,
		 * no volume, as in FX mode); moving the fader down raises it exactly
		 * as moving it up does in FX mode; at the top it is exactly 0. Both
		 * taps keep their existing per-block ramps, so fader moves and mode
		 * changes are click-free. Cost: one division per xfd slot per block.
		 * Affiliates: adc_sliderGainMirrored(), MIXER_FADER_XFD.
		 */
		*send_gain = send * adc_sliderGainMirrored(fader);
	}
}

/*
 * Dry output and FX send in one pass (S073 Step 5).
 *
 * What:       for one decimated voice block, produces exactly what
 *             mixer_addVoiceInt16ToOutput() and the pre-S073
 *             mixer_addVoiceToFxBus() produced when both ran, reading each
 *             sample once:
 *             - dry: the voice gain ramp, int16 truncation, sample_mx_t
 *               conversion, pan/route/saturating add to the routed DAC
 *               pair;
 *             - send: its own ramp, float x 256 straight to sample_mx_t
 *               without the int16 truncation (on purpose), stereo-input
 *               types panned into bus L/R, mono-input types unpanned into L
 *               only, saturating adds. This is the click-free send ramp
 *               the removed mixer_addVoiceToFxBus() provided.
 *             Each expression keeps its original operand order.
 * Why:        audit F5: the send pass re-read every sample and recomputed
 *             a ramp. The user requires unchanged functionality (S0).
 * Inputs:     dest (jack-resolved routing), panL/panR (squareRootLut),
 *             data (decimated pre-volume block), gain/lastGain (dry ramp:
 *             vol x mix fader), sendGain/sendLastGain (send ramp:
 *             fxSend/127 x send fader), stereo (live Effect stereo-input
 *             flag), and the four interleaved output buses.
 * Outputs:    the output buses and mixer_fx_bus.mx[0..1].
 * Accessors:  mixer_calcNextSampleBlock(), only when the send is active
 *             (fx_active and either send gain > 0: the existing kept path).
 *             Otherwise the dry-only function runs unchanged.
 * Affiliates: mixer_addVoiceInt16ToOutput() (the dry-only path, whose dry
 *             expressions this copies exactly); the former
 *             mixer_addVoiceToFxBus(), removed in S073 Step 5, whose send
 *             expressions this copies exactly (its pre-S073 text is kept in
 *             tools/dsp_test/frozen/Core/DSPAudio/mixer.c as the test
 *             reference); the mixer_send_last_gain / mixer_voice_last_gain
 *             updates in the caller (unchanged); mixer_faderGains();
 *             tools/dsp_test/test_mixer.c.
 */
static void mixer_addVoiceInt16ToOutputAndFx(uint8_t dest,
		const float panL,
		const float panR,
		const int16_t* data,
		const float gain,
		const float lastGain,
		const float sendGain,
		const float sendLastGain,
		const uint8_t stereo,
		sample_mx_t* outL,
		sample_mx_t* outR,
		sample_mx_t* outL2,
		sample_mx_t* outR2)
{
	uint8_t i;
	const float inv_size = 1.f / (OUTPUT_DMA_SIZE - 1.f);
	const float gain_delta = gain - lastGain;
	const float send_delta = sendGain - sendLastGain;

/* The send half of one sample, identical to mixer_addVoiceToFxBus(). The
** stereo test is loop-invariant; GCC -Ofast unswitches it. */
#define MIXER_FX_SEND_SAMPLE()                                                \
	do {                                                                      \
		const float sendCurrentGain = sendLastGain                            \
				+ ((float)i * inv_size * send_delta);                         \
		const float sample = (float)data[i] * sendCurrentGain * 256.0f;       \
		if (stereo) {                                                         \
			mixer_fx_bus.mx[0][i] = bufferTool_satAdd32(                      \
					mixer_fx_bus.mx[0][i], (sample_mx_t)(sample * panL));     \
			mixer_fx_bus.mx[1][i] = bufferTool_satAdd32(                      \
					mixer_fx_bus.mx[1][i], (sample_mx_t)(sample * panR));     \
		} else {                                                              \
			mixer_fx_bus.mx[0][i] = bufferTool_satAdd32(                      \
					mixer_fx_bus.mx[0][i], (sample_mx_t)sample);              \
		}                                                                     \
	} while (0)

	switch(dest)
	{
	case MIXER_ROUTING_DAC1_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			const sample_mx_t sm = sampleMix_fromInt16(s16);
			MIXER_FX_SEND_SAMPLE();
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
			MIXER_FX_SEND_SAMPLE();
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
			MIXER_FX_SEND_SAMPLE();
			*outL2 = bufferTool_satAdd32(*outL2, sampleMix_fromInt16(s16));
			outL2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			MIXER_FX_SEND_SAMPLE();
			*outR2 = bufferTool_satAdd32(*outR2, sampleMix_fromInt16(s16));
			outR2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			MIXER_FX_SEND_SAMPLE();
			*outL = bufferTool_satAdd32(*outL, sampleMix_fromInt16(s16));
			outL += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const int16_t s16 = (int16_t)((float)data[i] * currentGain);
			MIXER_FX_SEND_SAMPLE();
			*outR = bufferTool_satAdd32(*outR, sampleMix_fromInt16(s16));
			outR += 2;
		}
		break;
	default:
		/* An unknown routing adds no dry signal (as the dry-only function's
		** switch falls through), but the send must still accumulate exactly
		** as mixer_addVoiceToFxBus() would. */
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
			MIXER_FX_SEND_SAMPLE();
		break;
	}
#undef MIXER_FX_SEND_SAMPLE
}

/*
 * Convert one normalized Effect return sample to the mixer's signed-24
 * representation.
 *
 * Inputs: normalized float where 1.0 is int16 full scale. Output: saturated
 * sample_mx_t with the same 8-bit fractional shift as sampleMix_fromInt16().
 * The explicit ±255 guard contains Effect overshoot before the float-to-int
 * conversion and leaves the final output accumulation to satAdd32().
 * Affiliate: mixer_addFxReturnToOutput().
 */
static sample_mx_t mixer_floatToMx(float value)
{
	if (value > 255.0f)
		value = 255.0f;
	else if (value < -255.0f)
		value = -255.0f;
	return (sample_mx_t)(value * 8388352.0f);
}

/*
 * Apply, ramp, and route the processed FX return into the selected DAC bus.
 *
 * Inputs: jack-resolved destination, optional stereo return buffers, current
 * left/right return gains, and the four interleaved output buses. Stereo
 * destinations preserve both Effect channels; mono destinations sum stereo
 * returns at 0.5 per side and route mono returns from l. Output: saturated
 * sample_mx_t mix samples and updated two-channel return ramp origins.
 * Affiliate: mixer_calcNextSampleBlock().
 */
static void mixer_addFxReturnToOutput(uint8_t dest,
		const float *l,
		const float *r,
		float gainL,
		float gainR,
		sample_mx_t *outL,
		sample_mx_t *outR,
		sample_mx_t *outL2,
		sample_mx_t *outR2)
{
	sample_mx_t *mono = 0;
	sample_mx_t *stereoL = 0;
	sample_mx_t *stereoR = 0;
	uint8_t stereo_route = 0u;
	uint8_t i;
	const float inv_size = 1.f / (OUTPUT_DMA_SIZE - 1.f);
	const float deltaL = gainL - mixer_fx_return_last_gain[0];
	const float deltaR = gainR - mixer_fx_return_last_gain[1];

	switch (dest) {
	case MIXER_ROUTING_DAC1_STEREO:
		stereoL = outL2;
		stereoR = outR2;
		stereo_route = 1u;
		break;
	case MIXER_ROUTING_DAC2_STEREO:
		stereoL = outL;
		stereoR = outR;
		stereo_route = 1u;
		break;
	case MIXER_ROUTING_DAC1_L:
		mono = outL2;
		break;
	case MIXER_ROUTING_DAC1_R:
		mono = outR2;
		break;
	case MIXER_ROUTING_DAC2_L:
		mono = outL;
		break;
	case MIXER_ROUTING_DAC2_R:
		mono = outR;
		break;
	default:
		return;
	}

	for (i = 0u; i < OUTPUT_DMA_SIZE; i++) {
		const float currentL = mixer_fx_return_last_gain[0]
				+ ((float)i * inv_size * deltaL);
		const float currentR = mixer_fx_return_last_gain[1]
				+ ((float)i * inv_size * deltaR);
		if (stereo_route) {
			*stereoL = bufferTool_satAdd32(*stereoL,
					mixer_floatToMx(l[i] * currentL));
			stereoL += 2;
			*stereoR = bufferTool_satAdd32(*stereoR,
					mixer_floatToMx((r ? r[i] : l[i]) * currentR));
			stereoR += 2;
		} else {
			const float monoSample = r
					? (0.5f * (l[i] * currentL
							+ r[i] * currentR))
					: (l[i] * currentL);
			*mono = bufferTool_satAdd32(*mono, mixer_floatToMx(monoSample));
			mono += 2;
		}
	}
	mixer_fx_return_last_gain[0] = gainL;
	mixer_fx_return_last_gain[1] = gainR;
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

	/*
	 * Dispatch slot-owned DSP runtime work through InstrumentManager.
	 *
	 * Inputs: active Scene instrument assignments for slots 1..6. Output:
	 * LFOs, filters, and async voice state update on whichever engine instance
	 * is currently loaded into each slot. Mixer keeps output routing and gain;
	 * InstrumentManager owns the dynamic instrument-to-runtime selection.
	 */
	instrumentManager_dispatchRuntimeLfos();
	for(uint8_t slot=0u; slot<6u; slot++)
		instrumentManager_recalcSlotFilter(slot);
	for(uint8_t slot=0u; slot<6u; slot++)
		instrumentManager_calcSlotAsync(slot);

	/*
	 * Resolve the active Scene Effect before this block is rendered.
	 *
	 * Inputs: retained normal/Morph images and the active Scene Effect type.
	 * Output: common runtime values and type DSP state are current for the
	 * common runtime values and type DSP state are current for the send/process/
	 * return path below. The active registry row supplies the live I/O shape;
	 * EffectsManager owns type handoff and parameter resolution.
	 */
	effects_service();

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

	/*
	 * Snapshot Effect I/O and return routing once per render block.
	 *
	 * Inputs: EffectsManager's active type flags/common runtime and cached jack
	 * detection. Output: an optional cleared two-channel FX bus and one stable
	 * resolved destination for the processed return. When the type is off, the
	 * bus is left untouched and no Effect work is performed. Affiliates:
	 * mixer_addVoiceInt16ToOutputAndFx(), effects_process(), and
	 * mixer_addFxReturnToOutput().
	 */
	const uint8_t fx_io = effects_activeIoFlags();
	const uint8_t fx_stereo_in = (uint8_t)((fx_io & EFFECT_IO_STEREO_IN) != 0u);
	const uint8_t fx_active = (uint8_t)(fx_io != 0u);
	const uint8_t fx_scene = scene_getActiveIndex();
	const effects_common_runtime_t *fx_common = effects_commonRuntime();
	const uint8_t fx_route = mixer_checkOutJackAvailable(fx_common->route);
	if (fx_active)
		bufferTool_clearBuffer32(&mixer_fx_bus.mx[0][0], OUTPUT_DMA_SIZE * 2);

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


	/*
	 * Render each storage slot with its loaded instrument type.
	 *
	 * Inputs: slot index chooses SceneData/instrument runtime through
	 * InstrumentManager; mixer state chooses routing, decimation, channel
	 * volume, pan tables, and the combined slider x volume gain ramp. Output:
	 * each slot contributes one mono block to
	 * the routed stereo output pair. This replaces the fixed sequence of three
	 * drums, one snare, one cymbal, and one hihat without changing mixer-owned
	 * output behavior.
	 */
	for(uint8_t slot=0u; slot<6u; slot++)
	{
		uint8_t pan;
		float voiceGain;
		float sendGain;
		instrumentManager_calcSlotSyncBlock(slot, sampleData, OUTPUT_DMA_SIZE);
		mixer_decimateBlock(slot,sampleData);
		/*
		 * sampleData is the decimated, pre-volume voice block. The fader mode
		 * creates parallel dry/send gains: PRE scales both, POST scales only the
		 * dry mix, and FX scales only the send. The send tap is accumulated before
		 * channel volume, while the normal output keeps the combined voice ramp.
		 */
		/*
		 * S077 P2 §3.5: the per-voice FX send and fader mode resolve through the
		 * track's played Scene, which may differ from the global active Scene.
		 * The bus compressor remains scene-global and stays on fx_scene.
		 */
		mixer_faderGains(slot, preset_getSlotPlayedScene(slot),
				&voiceGain, &sendGain);
		pan = instrumentManager_runtimePan(slot);
		/*
		 * One-pass dry + send when the send is active (S073 Step 5).
		 *
		 * What:       with an active send (the existing condition, kept by
		 *             user decision), the combined function produces the dry
		 *             output and the FX bus contribution in one read of
		 *             sampleData. Otherwise the dry-only function runs as
		 *             before.
		 * Why:        audit F5; S0 (see the combined function's contract).
		 * Inputs:     the per-slot gains, pan and routing resolved above.
		 * Outputs:    the output buses and mixer_fx_bus; the last-gain
		 *             updates below are unchanged and still run every block.
		 * Accessors:  this loop.
		 * Affiliates: mixer_addVoiceInt16ToOutputAndFx(),
		 *             mixer_addVoiceInt16ToOutput(); the send-only
		 *             mixer_addVoiceToFxBus() was removed in this step.
		 */
		if (fx_active && (sendGain > 0.0f || mixer_send_last_gain[slot] > 0.0f))
			mixer_addVoiceInt16ToOutputAndFx(effectiveRouting[slot],
					squareRootLut[127-pan], squareRootLut[pan],
					sampleData, voiceGain, mixer_voice_last_gain[slot],
					sendGain, mixer_send_last_gain[slot], fx_stereo_in,
					&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
		else
			mixer_addVoiceInt16ToOutput(effectiveRouting[slot],
					squareRootLut[127-pan], squareRootLut[pan],
					sampleData, voiceGain, mixer_voice_last_gain[slot],
					&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
		mixer_voice_last_gain[slot] = voiceGain;
		mixer_send_last_gain[slot] = sendGain;
	}

	if (fx_active) {
		/*
		 * Convert the accumulated signed-24 bus in place, process one normalized
		 * block, then return the Effect through its live Scene route. A mono-input
		 * type has only l input; a mono-input/stereo-output type receives a zeroed
		 * r output channel so its process callback can write the second return.
		 */
		const uint8_t fx_stereo_out =
				(uint8_t)((fx_io & EFFECT_IO_STEREO_OUT) != 0u);
		const uint8_t fx_channels_in = fx_stereo_in ? 2u : 1u;
		const float inv_mx = 1.0f / 8388352.0f;
		const float fx_level = fx_common->level;
		const uint8_t fx_pan = fx_common->pan;
		fx_share_t share;
		effect_io_t io;
		float gainL;
		float gainR;

		for (uint8_t i = 0u; i < OUTPUT_DMA_SIZE; i++) {
			mixer_fx_bus.f[0][i] = (float)mixer_fx_bus.mx[0][i] * inv_mx;
			mixer_fx_bus.f[1][i] = (float)mixer_fx_bus.mx[1][i] * inv_mx;
		}
		if (!fx_stereo_in && fx_stereo_out)
			for (uint8_t i = 0u; i < OUTPUT_DMA_SIZE; i++)
				mixer_fx_bus.f[1][i] = 0.0f;

		fxbuf_effectShare(&share);
		io.l = mixer_fx_bus.f[0];
		io.r = (fx_stereo_in || fx_stereo_out) ? mixer_fx_bus.f[1] : 0;
		io.frames = OUTPUT_DMA_SIZE;
		io.channels = fx_channels_in;
		io.share = &share;
		effects_process(&io);

		/*
		 * Effect return pan (S075 F2-E, user decision F2-Q4).
		 *
		 * What: stereo output uses a balance law centred on stored 63: at 63
		 * both sides are unity, 0 silences the right side, and 127 silences the
		 * left side. Mono output keeps its existing constant-power law. Inputs:
		 * fx_pan 0..127 and fx_level. Outputs: gainL/gainR.
		 */
		if (fx_stereo_out) {
			gainL = fx_level * ((fx_pan <= 63u) ? 1.0f
					: (float)(127u - fx_pan) / 64.0f);
			gainR = fx_level * ((fx_pan >= 63u) ? 1.0f
					: (float)fx_pan / 63.0f);
		} else {
			gainL = fx_level * squareRootLut[127u - fx_pan];
			gainR = fx_level * squareRootLut[fx_pan];
		}
		mixer_addFxReturnToOutput(fx_route, mixer_fx_bus.f[0],
				fx_stereo_out ? mixer_fx_bus.f[1] : 0,
				gainL, gainR, &output[pos], &output[pos+1],
				&output2[pos], &output2[pos+1]);
	} else {
		/*
		 * Reset the FX return ramp origin while no Effect runs (S072 debt 8,
		 * closed in S074).
		 *
		 * What: holds both return gains' previous-block origin at 0 on every
		 * `off` block. Why: mixer_addFxReturnToOutput() ramps from
		 * mixer_fx_return_last_gain[] to the new gains. While `off` it is
		 * not called, so the origin kept the last active Effect's gains, and a
		 * type that outputs on its first block after init (CrumpBit's AC
		 * coupled 8-bit output is never silent) started from those stale gains,
		 * a click. From 0 the return fades in over one block. Cost: two stores
		 * per `off` block, unconditional. Inputs: fx_active == 0. Output:
		 * mixer_fx_return_last_gain[0..1] = 0. Affiliates:
		 * mixer_addFxReturnToOutput(), effects_activeIoFlags(),
		 * EFFECTS_MIXER_DSP_REFERENCE.md §5.3 item 2.
		 */
		mixer_fx_return_last_gain[0] = 0.0f;
		mixer_fx_return_last_gain[1] = 0.0f;
	}

	/*
	 * Master bus compressor (S074), after all voices and the FX return.
	 *
	 * Mapping: St1 = DAC1 = output2 (MAIN/headphones); St2 = DAC2 = output
	 * (OUT2). No jack fallback is applied: the compressor stays on the Scene's
	 * selected physical pair. Cost is zero while cmp is off.
	 * Inputs: both summed output buffers and the active Scene snapshot.
	 * Output: only the selected buffer is compressed in place.
	 * Affiliate: BusCompressor.h and voiceControl_triggerNow().
	 */
	busComp_processBlock(&output2[pos], &output[pos], fx_scene);

}
