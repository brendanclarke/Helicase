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
 * ordering (voiceControl_processPending -> seq_drainPendingAutomationTo()).
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
 * Pop every queued trigger in one masked section (S087 stage A).
 *
 * What:       copies up to VOICE_TRIGGER_RING_SIZE events into `events`, with
 *             each voice's test-and-clear of voiceControl_pendingReset in the
 *             same PRIMASK section as its pop; then copies and clears the
 *             remaining reset bits into `leftover` and takes the automation
 *             mark into `markOut`. The pop logic replaces
 *             voiceControl_popPending(), which S087 deleted: this is its only
 *             caller and the batch must be taken atomically with the
 *             automation mark (voiceControl_processPending()).
 * Why:        D6 (S087 P0 §6.5): a step's trigger and its automation entries
 *             are published by TIM3 in one interrupt run; taking all pops, the
 *             leftover reset bits and seq_pendingAutomationMark() under one
 *             mask guarantees the drained entries belong to the popped
 *             triggers.
 * Inputs:     the ring cursors and voiceControl_pendingReset. Returns the
 *             number of events copied; *leftoverOut receives the old reset
 *             mask and *markOut the automation producer count.
 * Outputs:    the batch, *leftoverOut, *markOut, the advanced tail, a cleared
 *             mask.
 * Accessors:  voiceControl_processPending() only.
 * Affiliates: voiceControl_prepareTrigger(), voiceControl_fireTrigger(),
 *             seq_pendingAutomationMark(). RAM: the batch lives in
 *             voiceControl_processPending()'s frame (R-A2).
 */
static uint8_t voiceControl_popPendingBatch(VoiceTriggerEvent *events,
											 uint8_t *resetFlags,
											 uint8_t *leftoverOut,
											 uint8_t *markOut)
{
	uint8_t n = 0u;
	uint32_t primask = voiceControl_irqSave();

	while (voiceTriggerTail != voiceTriggerHead) {
		VoiceTriggerEvent ev = voiceTriggerRing[voiceTriggerTail];

		voiceTriggerTail = (uint8_t)((voiceTriggerTail + 1u) &
									  VOICE_TRIGGER_RING_MASK);
		events[n] = ev;
		/* Test-and-clear this voice's pending-reset bit in the same PRIMASK
		** section as the pop so the decision matches the popped event. */
		resetFlags[n] = (uint8_t)((voiceControl_pendingReset >> ev.voice) & 1u);
		voiceControl_pendingReset &= (uint8_t)~(1u << ev.voice);
		n++;
	}
	*leftoverOut = voiceControl_pendingReset;
	voiceControl_pendingReset = 0u;
	*markOut = seq_pendingAutomationMark();
	voiceControl_irqRestore(primask);
	return n;
}

/*
 * Prepare one popped trigger (S087 stage A; first half of the old
 * voiceControl_triggerNow()).
 *
 * What:       preset_applyDeferredSceneSlotForTrigger(voice), then
 *             seq_restoreAutomatedParameters(voice) when `reset` is set.
 * Why:        the restore must run before the step's own automation is
 *             drained (D6). The deferred Scene apply keeps its place ahead
 *             of the restore, so the restore reads the played Scene's
 *             morph_interpolation[] (S081 §5.8 order, unchanged).
 * Inputs:     voice 0..6, reset flag from the trigger batch.
 * Outputs:    the installed Scene slot image; restored runtime values;
 *             cleared menu override for the voice.
 * Accessors:  voiceControl_processPending().
 * Affiliates: voiceControl_fireTrigger(), seq_drainPendingAutomationTo().
 */
static void voiceControl_prepareTrigger(uint8_t voice, uint8_t reset)
{
	/*
	 * Preset first has a chance to force-apply a pending deferred Scene slot
	 * for this track, then InstrumentManager maps tracks 1..6 to storage slots
	 * 1..6 and track 7 to slot 6's alternate/choke trigger. This keeps Scene
	 * changes smooth while guaranteeing the new Scene pattern's first trigger
	 * uses the new Scene's instrument parameters.
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
}

/*
 * Fire one popped trigger (S087 stage A; second half of the old
 * voiceControl_triggerNow()).
 *
 * What:       instrumentManager_triggerTrack(), the S074 sidechain offer and
 *             the LED pulse, exactly as before.
 * Why:        runs after the drain, so the voice starts with its step's
 *             automation already applied (D6).
 * Inputs:     voice, note, vel.
 * Outputs:    the triggered voice; sidechain weight; LED pulse.
 * Accessors:  voiceControl_processPending().
 * Affiliates: voiceControl_prepareTrigger().
 */
static void voiceControl_fireTrigger(uint8_t voice, uint8_t note, uint8_t vel)
{
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

/*
 * Process all pending triggers for one render pass (S087 stage A order).
 *
 * What:       1. under one PRIMASK section: pop every ring entry with its
 *                reset bit into a local batch (at most the ring size, 32),
 *                take the leftover reset bits, and mark the automation queue
 *                (seq_pendingAutomationMark());
 *             2. prepare each popped trigger (deferred Scene apply, restore
 *                when reset), then restore each leftover reset voice;
 *             3. seq_drainPendingAutomationTo(mark);
 *             4. fire each popped trigger in pop order.
 * Why:        D6 (S087 P0 §6.5): a step's automation must be applied before
 *             its trigger fires; before S087 the drain ran after the trigger
 *             and every hit's first block used the previous values. Taking
 *             the pops and the mark together keeps a step's trigger and its
 *             entries in the same pass (TIM3 publishes them in one
 *             interrupt run).
 * Inputs:     the trigger ring, voiceControl_pendingReset, the automation
 *             queue.
 * Outputs:    restored, then automated, then triggered voices.
 * Cost:       one PRIMASK section of at most 32 event copies; the batch is
 *             128 B of stack (R-A2).
 * Edge:       two triggers of one voice in one pass: both prepares run
 *             before the drain, so the drain's values serve both hits
 *             (last writer wins, as in step order).
 * Accessors:  main.c render loop (once per OUTPUT_DMA_SIZE block).
 * Affiliates: seq_pendingAutomationMark(), seq_drainPendingAutomationTo(),
 *             seq_restoreAutomatedParameters(), voiceControl_popPendingBatch().
 */
void voiceControl_processPending(void)
{
	VoiceTriggerEvent batch[VOICE_TRIGGER_RING_SIZE];
	uint8_t resetFlags[VOICE_TRIGGER_RING_SIZE];
	uint8_t leftover;
	uint8_t mark;
	uint8_t n;
	uint8_t i;

	/* Pop the ring and take the automation mark in one masked section. */
	n = voiceControl_popPendingBatch(batch, resetFlags, &leftover, &mark);

	/* 2. Restore state first: popped triggers, then reset-only voices. */
	for (i = 0u; i < n; i++)
		voiceControl_prepareTrigger(batch[i].voice, resetFlags[i]);
	while (leftover) {
		uint8_t voice = 0u;
		while ((leftover & (uint8_t)(1u << voice)) == 0u)
			voice++;
		leftover &= (uint8_t)~(1u << voice);
		seq_restoreAutomatedParameters(voice);
	}

	/* 3. Apply this step's own automation before any of its triggers. */
	seq_drainPendingAutomationTo(mark);

	/* 4. Fire. */
	for (i = 0u; i < n; i++)
		voiceControl_fireTrigger(batch[i].voice, batch[i].note, batch[i].vel);
}
