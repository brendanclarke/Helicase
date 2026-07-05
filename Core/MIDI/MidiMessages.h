/*
 * MidiMessages.h
 *
 *  Created on: 02.04.2012
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


#ifndef MIDIMESSAGES_H_
#define MIDIMESSAGES_H_

/* LXR-02 PORT NOTES:
 *   - Original mainboard #include "stm32f4xx.h" → <stdint.h>
 *   - Three enum entries renamed to disambiguate from Parameters.h
 *     (which originated from the AVR side and used the unprefixed names
 *     for parameter slots — collision-free across separate compilation
 *     units on the original two-chip LXR; collides on single-chip F765):
 *       NRPN_DATA_ENTRY_COARSE → MIDI_NRPN_DATA_ENTRY_COARSE
 *       NRPN_FINE              → MIDI_NRPN_FINE
 *       NRPN_COARSE            → MIDI_NRPN_COARSE
 *     Slot values (5, 98, 99) unchanged. When MidiParser.c is ported,
 *     update its switch cases to the new names.
 */

#include <stdint.h>

enum MidiSource {
	midiSourceMIDI,
	midiSourceUSB
};

struct MidiBits {
	enum MidiSource source:1; // 0 for midi, 1 for usb
	unsigned sysxbyte:1; // 1 if this message is a sysex payload only
	unsigned length:2; // how many data bytes have been filled
	unsigned :4;
};

//-----------------------------------------------------------
/** a struct defining a standard midi message*/
typedef struct MidiStruct {
	uint8_t status;
	uint8_t data1;
	uint8_t data2;
	struct MidiBits bits;
} MidiMsg;



//-----------------------------------------------------------
//Status bytes
#define NOTE_OFF 			0x80	// 2 data bytes
#define NOTE_ON 			0x90	// 2 data bytes
#define MIDI_CC				0xb0	// 2 data bytes
#define MIDI_CC2			0xF4	// 2 data bytes an unused midi status is used to indicate another cc message for params above 127
#define PROG_CHANGE			0xc0	// 1 data bytes
#define MIDI_PITCH_WHEEL	0xE0	// 2 data bytes
#define MIDI_AT				0xA0	// 2 data bytes
#define CHANNEL_PRESSURE	0xD0	// 2 data bytes


//-----------------------------------------------------------
// CCs (2nd byte controller number)
//#define CC_BANK_CHANGE		0x00 //-> see CC enum
#define CC_MOD_WHEEL		0x01
#define CC_ALL_SOUND_OFF	0x78
#define CC_ALL_NOTES_OFF	0x7B

//-----------------------------------------------------------
// system messages
#define MIDI_CLOCK			0xF8
#define SYSEX_START			0xF0
#define SYSEX_END			0xF7
#define MIDI_START			0xFA
#define MIDI_STOP			0xFC
#define MIDI_CONTINUE		0xFB
#define MIDI_MTC_QFRAME		0xF1	//--AS mtc timecodes
#define MIDI_SONG_SEL		0xF3	//--AS passthru only

//------------------------------------------------------------

#define NO_AUTOMATION 0xff	//used as a dummy message number for the automation tracks.
							// paramNr 0xff means no automation
// Custom CCs

enum
{
	CC_BANK_CHANGE = 0, //	/*0*/
	CC_MODWHEEL,
	OSC_WAVE_DRUM1 = 2,
	OSC_WAVE_DRUM2,
	OSC_WAVE_DRUM3,
	OSC_WAVE_SNARE,
	NRPN_DATA_ENTRY_COARSE, /* renamed from NRPN_DATA_ENTRY_COARSE
	                              * to disambiguate from Parameters.h —
	                              * AVR and STM32 sides used the same
	                              * unprefixed name in separate enums.
	                              * Slot value (5) and meaning unchanged.
	                              * MidiParser.c case label needs the
	                              * matching rename when ported. Nope,
								  * wrong, it's PAR_NRPN_DATA_ENTRY_COARSE
								  * now */
	CYM_WAVE1,
	WAVE1_HH,

	F_OSC1_COARSE,
	F_OSC1_FINE,					/*10*/
	F_OSC2_COARSE,
	F_OSC2_FINE,
	F_OSC3_COARSE,
	F_OSC3_FINE,
	F_OSC4_COARSE,
	F_OSC4_FINE,
	F_OSC5_COARSE,
	F_OSC5_FINE,
	F_OSC6_COARSE,
	F_OSC6_FINE,					/*20*/

	MOD_WAVE_DRUM1,
	MOD_WAVE_DRUM2,
	MOD_WAVE_DRUM3,
	CYM_WAVE2,
	CYM_WAVE3,
	WAVE2_HH,
	WAVE3_HH,

	SNARE_NOISE_F,
	SNARE_MIX,

	CYM_MOD_OSC_F1,					/*30*/
	CYM_MOD_OSC_F2,
	CYM_MOD_OSC_GAIN1,
	CYM_MOD_OSC_GAIN2,
	MOD_OSC_F1,
	MOD_OSC_F2,
	MOD_OSC_GAIN1,
	MOD_OSC_GAIN2,

	FILTER_FREQ_DRUM1,
	FILTER_FREQ_DRUM2,
	FILTER_FREQ_DRUM3,				/*40*/
	SNARE_FILTER_F,
	CYM_FIL_FREQ,
	HAT_FILTER_F,

	RESO_DRUM1,
	RESO_DRUM2,
	RESO_DRUM3,
	SNARE_RESO,
	CYM_RESO,
	HAT_RESO,

	VELOA1,							/*50*/
	VELOD1,
	VELOA2,
	VELOD2,
	VELOA3,
	VELOD3,
	VELOA4,
	VELOD4,
	VELOA5,
	VELOD5,
	VELOA6,							/*60*/
	VELOD6,
	VELOD6_OPEN,

	VOL_SLOPE1,
	VOL_SLOPE2,
	VOL_SLOPE3,
	EG_SNARE1_SLOPE,
	CYM_SLOPE,
	VOL_SLOPE6,

	REPEAT1,
	CYM_REPEAT,						/*70*/

	PITCHD1, //mod eg decay
	PITCHD2,
	PITCHD3,
	PITCHD4,

	MODAMNT1,
	MODAMNT2,
	MODAMNT3,
	MODAMNT4,

	PITCH_SLOPE1,
	PITCH_SLOPE2,					/*80*/
	PITCH_SLOPE3,
	PITCH_SLOPE4,

	FMAMNT1,	//TODO rename!
	FMDTN1,
	FMAMNT2,
	FMDTN2,
	FMAMNT3,
	FMDTN3,

	VOL1,
	VOL2,							/*90*/
	VOL3,
	VOL4,
	VOL5,
	VOL6,

	PAN1,
	PAN2,
	PAN3,
	NRPN_FINE,    /* renamed from NRPN_FINE — see note above NIXED, no Parameters.h any more*/
	NRPN_COARSE,  /* renamed from NRPN_COARSE — see note above NIXED, no Parameters.h any more*/
	PAN4,							/*100*/
	PAN5,
	PAN6,

	OSC1_DIST,
	OSC2_DIST,
	OSC3_DIST,
	SNARE_DISTORTION,
	CYMBAL_DISTORTION,
	HAT_DISTORTION,

	VOICE_DECIMATION1,
	VOICE_DECIMATION2,				/*110*/
	VOICE_DECIMATION3,
	VOICE_DECIMATION4,
	VOICE_DECIMATION5,
	VOICE_DECIMATION6,
	VOICE_DECIMATION_ALL,

	FREQ_LFO1,	//todo rename cc and cc2 according to their new position
	FREQ_LFO2,
	FREQ_LFO3,
	FREQ_LFO4,
	FREQ_LFO5,						/*120*/
	FREQ_LFO6,

	AMOUNT_LFO1,
	AMOUNT_LFO2,
	AMOUNT_LFO3,
	AMOUNT_LFO4,
	AMOUNT_LFO5,
	AMOUNT_LFO6,

	RESERVED4,						/*128*/

};  /* anonymous enum — trailing 'ParamEnums' variable name removed.
    ** Original mainboard used `} ParamEnums;` which declares an unused
    ** variable in every TU that includes this header — multiple-
    ** definition link error under GCC 10+ with default -fno-common.
    ** Constants above retain global scope; nothing referenced the var. */

//for all parameters above 127
enum
{

	CC2_FILTER_DRIVE_1,
	CC2_FILTER_DRIVE_2,
	CC2_FILTER_DRIVE_3,
	CC2_FILTER_DRIVE_4,
	CC2_FILTER_DRIVE_5,
	CC2_FILTER_DRIVE_6,

	CC2_MIX_MOD_1,
	CC2_MIX_MOD_2,
	CC2_MIX_MOD_3,

	CC2_VOLUME_MOD_ON_OFF1,
	CC2_VOLUME_MOD_ON_OFF2,
	CC2_VOLUME_MOD_ON_OFF3,
	CC2_VOLUME_MOD_ON_OFF4,
	CC2_VOLUME_MOD_ON_OFF5,
	CC2_VOLUME_MOD_ON_OFF6,

	CC2_VELO_MOD_AMT_1,
	CC2_VELO_MOD_AMT_2,
	CC2_VELO_MOD_AMT_3,
	CC2_VELO_MOD_AMT_4,
	CC2_VELO_MOD_AMT_5,
	CC2_VELO_MOD_AMT_6,

	CC2_VEL_DEST_1,
	CC2_VEL_DEST_2,
	CC2_VEL_DEST_3,
	CC2_VEL_DEST_4,
	CC2_VEL_DEST_5,
	CC2_VEL_DEST_6,

	CC2_WAVE_LFO1,
	CC2_WAVE_LFO2,
	CC2_WAVE_LFO3,
	CC2_WAVE_LFO4,
	CC2_WAVE_LFO5,
	CC2_WAVE_LFO6,

	//the target and voice CC2ameters must be after one another!
	CC2_VOICE_LFO1,
	CC2_VOICE_LFO2,
	CC2_VOICE_LFO3,
	CC2_VOICE_LFO4,
	CC2_VOICE_LFO5,
	CC2_VOICE_LFO6,

	CC2_TARGET_LFO1,
	CC2_TARGET_LFO2,
	CC2_TARGET_LFO3,
	CC2_TARGET_LFO4,
	CC2_TARGET_LFO5,
	CC2_TARGET_LFO6,

	CC2_RETRIGGER_LFO1,
	CC2_RETRIGGER_LFO2,
	CC2_RETRIGGER_LFO3,
	CC2_RETRIGGER_LFO4,
	CC2_RETRIGGER_LFO5,
	CC2_RETRIGGER_LFO6,

	CC2_SYNC_LFO1,
	CC2_SYNC_LFO2,
	CC2_SYNC_LFO3,
	CC2_SYNC_LFO4,
	CC2_SYNC_LFO5,
	CC2_SYNC_LFO6,

	CC2_OFFSET_LFO1,
	CC2_OFFSET_LFO2,
	CC2_OFFSET_LFO3,
	CC2_OFFSET_LFO4,
	CC2_OFFSET_LFO5,
	CC2_OFFSET_LFO6,

	CC2_FILTER_TYPE_1,
	CC2_FILTER_TYPE_2,
	CC2_FILTER_TYPE_3,
	CC2_FILTER_TYPE_4,
	CC2_FILTER_TYPE_5,
	CC2_FILTER_TYPE_6,

	CC2_TRANS1_VOL,
	CC2_TRANS2_VOL,
	CC2_TRANS3_VOL,
	CC2_TRANS4_VOL,
	CC2_TRANS5_VOL,
	CC2_TRANS6_VOL,

	CC2_TRANS1_WAVE,
	CC2_TRANS2_WAVE,
	CC2_TRANS3_WAVE,
	CC2_TRANS4_WAVE,
	CC2_TRANS5_WAVE,
	CC2_TRANS6_WAVE,

	CC2_TRANS1_FREQ,
	CC2_TRANS2_FREQ,
	CC2_TRANS3_FREQ,
	CC2_TRANS4_FREQ,
	CC2_TRANS5_FREQ,
	CC2_TRANS6_FREQ,

	CC2_AUDIO_OUT1,
	CC2_AUDIO_OUT2,
	CC2_AUDIO_OUT3,
	CC2_AUDIO_OUT4,
	CC2_AUDIO_OUT5,
	CC2_AUDIO_OUT6,

	// --AS
	CC2_MIDI_NOTE1,
	CC2_MIDI_NOTE2,
	CC2_MIDI_NOTE3,
	CC2_MIDI_NOTE4,
	CC2_MIDI_NOTE5,
	CC2_MIDI_NOTE6,
	CC2_MIDI_NOTE7, // s/b 111 i think
	
	//<<insert new parameters here>>
	
	
	

	//Mute Button NRPN messages
	//these have to stay at the end of the CC2 list.
	//They are a special case to enable control of the channel muting via external NRPN messages
	CC2_MUTE_1 = 200,
	CC2_MUTE_2,
	CC2_MUTE_3,
	CC2_MUTE_4,
	CC2_MUTE_5,
	CC2_MUTE_6,
	CC2_MUTE_7,

};  /* trailing 'Param2Enums' variable name removed — see note on first enum */

//codec control messages
#define EQ_ON_OFF						0x01
#define EQ_BASS_F						0x02
#define EQ_TREB_F						0x03
#define EQ_TREB_GAIN					0x04
#define EQ_BASS_GAIN					0x05

#define LIMIT_ENABLE					0x06
#define LIMIT_ATT						0x07
#define LIMIT_REL						0x08
#define LIMIT_MAX						0x09
#define LIMIT_MIN						0x0A

//preset messages

//SysEx
#define SYSEX_INACTIVE					0x00	/**< SysEx mode is deactivated*/
#define SYSEX_REQUEST_STEP_DATA		 	0x01
#define SYSEX_RECEIVE_STEP_DATA			0x02
#define SYSEX_REQUEST_MAIN_STEP_DATA	0x03
#define SYSEX_RECEIVE_MAIN_STEP_DATA	0x04
#define SYSEX_REQUEST_PATTERN_DATA		0x05
#define SYSEX_RECEIVE_PAT_LEN_DATA		0x06
#define SYSEX_ACTIVE_MODE_NONE			0x7f	/**< a placeholder message indicating that sysex is active but no mode is selected yet*/
#endif /* MIDIMESSAGES_H_ */
