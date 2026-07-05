/*
 * MidiVoiceControl.c
 *
 *  Created on: 03.04.2012
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



#include "MidiVoiceControl.h"
#include "DrumVoice.h"
#include "Snare.h"
#include "HiHat.h"
#include "MidiMessages.h"
#include "CymbalVoice.h"
// #include "ledHandler.h"
// TODO DSP_PORT
// #include "sequencer.h"
// #include "TriggerOut.h"
#include "Uart.h"
#include "ledHandler.h"
//#include "LCD_driver.h"

static uint8_t active_voices=0;	// which voices are currently playing a note

typedef struct {
	uint8_t voice;
	uint8_t note;
	uint8_t vel;
} VoiceTriggerEvent;

#define VOICE_TRIGGER_RING_SIZE 32u
#define VOICE_TRIGGER_RING_MASK (VOICE_TRIGGER_RING_SIZE - 1u)

static VoiceTriggerEvent voiceTriggerRing[VOICE_TRIGGER_RING_SIZE];
static uint8_t voiceTriggerHead = 0;
static uint8_t voiceTriggerTail = 0;

static uint32_t voiceControl_irqSave(void)
{
	uint32_t primask;
	__asm volatile ("mrs %0, primask\ncpsid i" : "=r" (primask) :: "memory");
	return primask;
}

static void voiceControl_irqRestore(uint32_t primask)
{
	__asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}

static void voiceControl_enqueueTriggerLocked(uint8_t voice, uint8_t note, uint8_t vel)
{
	uint8_t next = (uint8_t)((voiceTriggerHead + 1u) & VOICE_TRIGGER_RING_MASK);

	if (next == voiceTriggerTail) {
		/* Session 019 Phase 7: MIDI, BAR, and sequencer events are produced
		** from different contexts but consumed only at the audio render
		** boundary. If the ring fills, keep the newest gesture rather than
		** blocking or touching DSP voice state from the wrong context. */
		voiceTriggerTail = (uint8_t)((voiceTriggerTail + 1u) & VOICE_TRIGGER_RING_MASK);
	}

	voiceTriggerRing[voiceTriggerHead].voice = voice;
	voiceTriggerRing[voiceTriggerHead].note = note;
	voiceTriggerRing[voiceTriggerHead].vel = vel;
	voiceTriggerHead = next;
}

static void voiceControl_dropPendingLocked(uint8_t voice)
{
	uint8_t read;
	uint8_t write;

	if (voice == 0xffu) {
		voiceTriggerTail = voiceTriggerHead;
		return;
	}

	read = voiceTriggerTail;
	write = voiceTriggerTail;
	while (read != voiceTriggerHead) {
		VoiceTriggerEvent ev = voiceTriggerRing[read];
		read = (uint8_t)((read + 1u) & VOICE_TRIGGER_RING_MASK);
		if (ev.voice != voice) {
			voiceTriggerRing[write] = ev;
			write = (uint8_t)((write + 1u) & VOICE_TRIGGER_RING_MASK);
		}
	}
	voiceTriggerHead = write;
}

static uint8_t voiceControl_popPending(VoiceTriggerEvent *event)
{
	uint32_t primask = voiceControl_irqSave();

	if (voiceTriggerTail == voiceTriggerHead) {
		voiceControl_irqRestore(primask);
		return 0;
	}

	*event = voiceTriggerRing[voiceTriggerTail];
	voiceTriggerTail = (uint8_t)((voiceTriggerTail + 1u) & VOICE_TRIGGER_RING_MASK);
	voiceControl_irqRestore(primask);
	return 1;
}

static void voiceControl_triggerNow(uint8_t voice, uint8_t note, uint8_t vel)
{
	if(voice < 3)
		Drum_trigger(voice, vel, note);
	else if(voice < 4)
		Snare_trigger(vel, note);
	else if(voice < 5)
		Cymbal_trigger(vel, note);
	else
		HiHat_trigger(vel,voice-5,note);

	led_pulseLed((uint8_t)(LED_VOICE1 + voice));
}

//----------------------------------------------------------------
// this fn assumes a valid voice is sent
void voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel)
{
	uint32_t primask;

	if (voice >= 7u)
		return;

	primask = voiceControl_irqSave();
	active_voices |= (1<<voice);

	voiceControl_enqueueTriggerLocked(voice, note, vel);
	voiceControl_irqRestore(primask);
	
	//Send trigger out signal	
	/* TODO DSP_PORT
	if(trigger_isGateModeOn())
	{
		if(vel)
			trigger_triggerVoice(voice, TRIGGER_ON);
	} else {
		trigger_triggerVoice(voice, TRIGGER_PULSE);
	}
	*/

}
//----------------------------------------------------------------
void voiceControl_noteOff(uint8_t voice)
{
	uint32_t primask;

	if(voice==0xff)
	{
		primask = voiceControl_irqSave();
		voiceControl_dropPendingLocked(0xffu);
		active_voices = 0;
		voiceControl_irqRestore(primask);
		// TODO DSP_PORT
		// seq_midiNoteOff(0xff);
		return;
	}

	if (voice >= 7u)
		return;

	primask = voiceControl_irqSave();
	voiceControl_dropPendingLocked(voice);

	//only set voice inactive and send MIDI off when voice is currently playing
	if(active_voices & (1<<voice))
	{
		active_voices &= (~(1<<voice));

		//send midi note off
		// TODO DSP_PORT
		// midiChan = midi_MidiChannels[voice];
		// TODO DSP_PORT
			// seq_midiNoteOff(midiChan);
	}
	voiceControl_irqRestore(primask);
}
//----------------------------------------------------------------
uint8_t voiceControl_isVoicePlaying(uint8_t voice)
{
	uint8_t result;
	uint32_t primask = voiceControl_irqSave();
	result = (uint8_t)(active_voices & (1<<voice));
	voiceControl_irqRestore(primask);
	return result;
}

//----------------------------------------------------------------
void voiceControl_processPending(void)
{
	VoiceTriggerEvent ev;

	while (voiceControl_popPending(&ev)) {
		voiceControl_triggerNow(ev.voice, ev.note, ev.vel);
	}
}
