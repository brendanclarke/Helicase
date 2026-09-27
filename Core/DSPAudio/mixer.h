/*
 * mixer.h
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



#ifndef MIXER_H_
#define MIXER_H_

#include "stm32f4xx.h"
#include "sample_mix.h"

#define USE_SWITCH_ROUTING 1
#define USE_DECIMATOR 1

#if USE_DECIMATOR
extern float mixer_decimation_rate[7];		/**<sets the sample rate decimation. 0..1 = full rate*/
#endif

extern uint8_t mixer_audioRouting[6];

enum
{
	MIXER_ROUTING_DAC1_STEREO,
	MIXER_ROUTING_DAC2_STEREO,
	MIXER_ROUTING_DAC1_L,
	MIXER_ROUTING_DAC1_R,
	MIXER_ROUTING_DAC2_L,
	MIXER_ROUTING_DAC2_R,
};

/*
 * Per-voice fader modes (Scene setting fader_setting[slot], 0..2; plan §8.3,
 * user rule A24). Values match the stored byte and the Menu labels
 * (menu_sceneSettingFaderName: pre / pst / fx).
 *
 * The voice signal s (decimated, pre-volume) feeds two parallel taps:
 *   mix  = s x vol x F_mix  -> pan -> voice route
 *   send = s x send x F_send -> FX bus
 * PRE : F_mix = fader, F_send = fader  (fader scales both)
 * POST: F_mix = fader, F_send = 1      (send ignores the fader)
 * FX  : F_mix = 1,     F_send = fader  (fader scales only the send; mix level
 *                                       is vol alone)
 * The fader never changes the stored FX Send parameter. Consumer:
 * mixer_calcNextSampleBlock(). Affiliates: SceneData fader_setting,
 * presetManager fader/send setters, Menu VOICE mix cells.
 */
#define MIXER_FADER_PRE   0u
#define MIXER_FADER_POST  1u
#define MIXER_FADER_FX    2u

void mixer_init();
void mixer_calcNextSampleBlock(sample_mx_t* output,sample_mx_t* output2);
void mixer_moveDataToOutput(uint8_t dest, const float panL, const float panR, sample_mx_t* data,sample_mx_t* outL,sample_mx_t* outR,sample_mx_t* outL2, sample_mx_t* outR2);
void mixer_addDataToOutput(uint8_t dest, const float panL, const float panR,  sample_mx_t* data,sample_mx_t* outL,sample_mx_t* outR,sample_mx_t* outL2, sample_mx_t* outR2);
void mixer_setOutJackDetectPB(uint8_t pb4_high, uint8_t pb6_high);
void mixer_setOutJackDetectPD(uint8_t pd6_high, uint8_t pd7_high);

#endif /* MIXER_H_ */
