/*
 * ParameterArray.h
 *
 *  Created on: 06.01.2013
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



#ifndef PARAMETERARRAY_H_
#define PARAMETERARRAY_H_
#include <stdint.h>


#define TYPE_UINT8 				0	// byte
#define TYPE_FLT 				1	// float
#define TYPE_SPECIAL_F			2	// float value targeting modNodeValue (as opposed to actual parameter)
#define TYPE_UINT32				3	// 32 bit int
#define TYPE_SPECIAL_P			4	// pan
#define TYPE_SPECIAL_FILTER_F	5	// not used apparently

/*
 * Non-sound flat parameter ids.
 *
 * Instrument sound parameters no longer live in this namespace. SceneData
 * allocates generic per-slot storage, and Core/DSP/Instruments descriptors
 * define what each instrument slot cell means. This enum remains only for
 * menu/performance/pattern/global values that still use parameter_values[].
 */
enum ParamEnums
{
	PAR_NONE = 0,
	PAR_MOD_WHEEL = PAR_NONE,

	END_OF_SOUND_PARAMETERS,

	PAR_ROLL = END_OF_SOUND_PARAMETERS,
	PAR_MORPH,

	PAR_ACTIVE_STEP,
	PAR_STEP_VOLUME,
	PAR_STEP_PROB,
	PAR_STEP_NOTE,

	PAR_EUKLID_LENGTH,
	PAR_EUKLID_STEPS,
	PAR_EUKLID_ROTATION,

	PAR_AUTOM_TRACK,

	PAR_P1_DEST,
	PAR_P2_DEST,

	PAR_P1_VAL,
	PAR_P2_VAL,

	PAR_SHUFFLE,

	PAR_PATTERN_BEAT,
	PAR_PATTERN_NEXT,
	PAR_TRACK_LENGTH,

	PAR_POS_X,
	PAR_POS_Y,
	PAR_FLUX,
	PAR_SOM_FREQ,
	PAR_TRACK_ROTATION,
	PAR_TRACK_SCALE,
	PAR_TRACK_MIDI_CHAN,
	PAR_TRACK_MIDI_NOTE,

	PAR_BEGINNING_OF_GLOBALS,
	PAR_BPM = PAR_BEGINNING_OF_GLOBALS,

	PAR_MIDI_CHAN_1,
	PAR_MIDI_CHAN_2,
	PAR_MIDI_CHAN_3,
	PAR_MIDI_CHAN_4,
	PAR_MIDI_CHAN_5,
	PAR_MIDI_CHAN_6,

	PAR_EXT_SYNC,
	PAR_FOLLOW,

	PAR_QUANTISATION,

	PAR_SCREENSAVER_ON_OFF,
	PAR_MIDI_MODE,
	PAR_MIDI_CHAN_7,
	PAR_MIDI_ROUTING,
	PAR_MIDI_FILT_TX,
	PAR_MIDI_FILT_RX,
	PAR_PRESCALER_CLOCK_IN,
	PAR_PRESCALER_CLOCK_OUT1,
	PAR_PRESCALER_CLOCK_OUT2,
	PAR_TRIG_GATE_MODE,

	PAR_BAR_RESET_MODE,
	PAR_MIDI_CHAN_GLOBAL,
	PAR_OSC_WAVE_INTERP,

	NUM_PARAMS = 384,
};

#include "stm32f4xx.h"

typedef union
{
	float 	 flt;
	uint32_t itg;
} ptrValue;

typedef struct ParameterStruct
{
	void* 	ptr;
	uint8_t type;

} Parameter;

extern Parameter parameterArray[];
void paramArray_setParameter(uint16_t idx, ptrValue newValue);
void parameterArray_init();

#endif /* PARAMETERARRAY_H_ */
