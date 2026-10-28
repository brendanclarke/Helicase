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
#include "MidiDefs.h"
#include "CymbalVoice.h"
#include "InstrumentManager.h"
#include "presetManager.h"
// #include "ledHandler.h"
// TODO DSP_PORT
// #include "sequencer.h"
// #include "TriggerOut.h"
#include "ledHandler.h"
/* Sequencer owns transient automation restore for the shared trigger funnel. */
#include "sequencer.h"
/* S074: bus compressor sidechain intake in voiceControl_triggerNow(). */
#include "BusCompressor.h"
//#include "LCD_driver.h"

static uint8_t active_voices=0;	// which voices are currently playing a note

/*
 * Per-voice pending-reset mask (S081 §5.8, §19.15).
 *
 * What: bit N (0..6) is set when voice N's next trigger must restore
 * Instrument automation to the Morph base and clear the menu override
 * before the voice fires. Why: voiceControl_noteOff() deletes queued
 * entries for that voice, so a flag stored inside VoiceTriggerEvent
 * would be lost on retrigger. A separate mask survives drops: a reset
 * stays pending until a trigger or end-of-pass consumes it. Inputs:
 * set by voiceControl_noteOn() (every live/seq first trigger),
 * voiceControl_resetOnly() (roll renewal with no trigger). Cleared by
 * voiceControl_processPending() when consumed. Outputs: gates the call
 * to seq_restoreAutomatedParameters() and the menu-override clear.
 * Affiliates: seq_triggerVoice() reset argument, main.c audio loop
 * ordering (voiceControl_processPending -> seq_drainPendingAutomation).
 * RAM: +1 B SRAM1.
 */
static uint8_t voiceControl_pendingReset;

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

/*
 * Atomic reset consumption (S081 §5.8, §19.15).
 *
 * What: voiceControl_popPending() gains an out-parameter `reset`. Inside its
 * existing PRIMASK section it copies the event, then tests and clears
 * voiceControl_pendingReset's bit for ev.voice in the same section. TIM3
 * sets bits concurrently (seq_triggerVoice() runs in the ISR), so every
 * read-modify-write of the mask is under PRIMASK, as the ring cursors
 * already are. Inputs: ring, mask. Outputs: the popped event and its reset
 * decision.
 */
static uint8_t voiceControl_popPending(VoiceTriggerEvent *event, uint8_t *resetOut)
{
	uint32_t primask = voiceControl_irqSave();

	if (voiceTriggerTail == voiceTriggerHead) {
		voiceControl_irqRestore(primask);
		return 0;
	}

	*event = voiceTriggerRing[voiceTriggerTail];
	voiceTriggerTail = (uint8_t)((voiceTriggerTail + 1u) & VOICE_TRIGGER_RING_MASK);
	/* Test-and-clear this voice's pending-reset bit in the same PRIMASK
	** section as the pop so the decision matches the popped event. */
	*resetOut = (uint8_t)((voiceControl_pendingReset >> event->voice) & 1u);
	voiceControl_pendingReset &= (uint8_t)~(1u << event->voice);
	voiceControl_irqRestore(primask);
	return 1;
}

/*
 * Trigger funnel with conditional automation reset (S081 §5.8, §19.15).
 *
 * What: preset_applyDeferredSceneSlotForTrigger() first (unchanged), then
 * seq_restoreAutomatedParameters() only when `reset` is set, then
 * instrumentManager_triggerTrack(), the sidechain and the LED pulse
 * (unchanged). Why: only a step's first trigger, a roll renewal or a live
 * press may restore the Morph base (F-L). Keeping the deferred Scene apply
 * ahead of the restore preserves the pre-S081 order: the restore reads the
 * played Scene's morph_interpolation[] after a pending Scene image is
 * installed. Inputs: voice, note, vel, reset. Outputs: as before.
 * Affiliates: voiceControl_processPending() (the only caller).
 */
static void voiceControl_triggerNow(uint8_t voice, uint8_t note, uint8_t vel, uint8_t reset)
{
	/*
	 * Trigger through the dynamic instrument runtime dispatcher.
	 *
	 * Inputs: queued visible voice/track number, note, and velocity. Output:
	 * Preset first has a chance to force-apply a pending deferred Scene slot for
	 * this track, then InstrumentManager maps tracks 1..6 to storage slots 1..6
	 * and track 7 to slot 6's alternate/choke trigger. This keeps Scene changes
	 * smooth while guaranteeing the new Scene pattern's first trigger uses the
	 * new Scene's instrument parameters.
	 */
	preset_applyDeferredSceneSlotForTrigger(voice);
	/*
	 * Restore any foreground automation overlays before the new voice reads
	 * them, but only for a trigger that requests a reset (first trigger of a
	 * step, a roll renewal, or a live press). Roll retriggers 2+ pass reset=0
	 * so a step's own automation is not undone by every roll hit (F-L).
	 */
	if (reset)
		seq_restoreAutomatedParameters(voice);
	instrumentManager_triggerTrack(voice, note, vel);

	/*
	 * Offer every trigger-funnel event to the S074 sidechain.
	 *
	 * Inputs: visible track and trigger velocity. Output: at most one pending
	 * velocity-cubed weight; the compressor itself runs only in the mixer block
	 * and ignores nonmatching/off sources. Sequencer, MIDI, rolls and previews
	 * all share this funnel, preserving same-block duck timing.
	 */
	busComp_sidechainTrigger(voice, vel);

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
	/*
	 * Live and sequencer first triggers always request a reset (F12, §5.8).
	 * The bit is set under the queue's existing PRIMASK so that the enqueue
	 * and the reset intent are published atomically. Roll retriggers 2+ use
	 * voiceControl_noteOnNoReset() instead.
	 */
	voiceControl_pendingReset |= (uint8_t)(1u << voice);
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
/*
 * Enqueue a voice trigger without requesting an automation reset (S081 §5.8).
 *
 * What: identical to voiceControl_noteOn() but does not set the
 * pending-reset bit. Why: roll retriggers 2+ must not restore the
 * Morph base or clear the menu override - only the roll's first hit
 * resets (F-L). Inputs: voice 0..6, note, vel. Outputs: ring entry
 * enqueued. Affiliates: seq_triggerVoice(..., reset=0) for roll hits,
 * voiceControl_noteOn() for first triggers.
 */
void voiceControl_noteOnNoReset(uint8_t voice, uint8_t note, uint8_t vel)
{
	uint32_t primask;

	if (voice >= 7u)
		return;

	primask = voiceControl_irqSave();
	active_voices |= (1<<voice);
	voiceControl_enqueueTriggerLocked(voice, note, vel);
	voiceControl_irqRestore(primask);
}
//----------------------------------------------------------------
/*
 * Request an automation reset for a voice with no trigger (S081 §5.8).
 *
 * What: sets the pending-reset bit under PRIMASK without enqueuing a
 * ring entry. Why: a roll carried across steps renews the roll at the
 * new step boundary. The renewal resets automation (so the new step's
 * entries apply cleanly) but fires no new first hit. Inputs: voice
 * 0..6. Outputs: pending-reset bit set; consumed by the next
 * voiceControl_processPending() pass. Affiliates: seq_rollBegin()
 * carry path, seq_restoreAutomatedParameters().
 */
void voiceControl_resetOnly(uint8_t voice)
{
	uint32_t primask;

	if (voice >= 7u)
		return;

	primask = voiceControl_irqSave();
	voiceControl_pendingReset |= (uint8_t)(1u << voice);
	voiceControl_irqRestore(primask);
}
//----------------------------------------------------------------
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
	uint8_t reset;
	uint8_t leftover;

	while (voiceControl_popPending(&ev, &reset)) {
		voiceControl_triggerNow(ev.voice, ev.note, ev.vel, reset);
	}

	/*
	 * After the ring is empty, take the remaining reset bits under PRIMASK
	 * (copy, then store 0) and run the restore for each bit taken: reset-only
	 * renewals and resets whose trigger entry was dropped by
	 * voiceControl_noteOff(). All of this precedes
	 * seq_drainPendingAutomation() (main.c:212-213), so a step's own
	 * automation still lands after its reset.
	 */
	{
		uint32_t primask = voiceControl_irqSave();
		leftover = voiceControl_pendingReset;
		voiceControl_pendingReset = 0u;
		voiceControl_irqRestore(primask);
	}
	while (leftover) {
		uint8_t voice = 0u;
		while ((leftover & (uint8_t)(1u << voice)) == 0u)
			voice++;
		leftover &= (uint8_t)~(1u << voice);
		seq_restoreAutomatedParameters(voice);
	}
}
