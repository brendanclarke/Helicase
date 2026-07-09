/*
 * sequencer.c
 *
 *  Created on: 11.04.2012
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


#include "stm32f4xx.h"
#include "globals.h"
#include "DrumVoice.h"
#include "Snare.h"
#include "HiHat.h"
#include "random.h"
#include "Uart.h"
#include "MidiMessages.h"
#include "MidiVoiceControl.h"
#include "CymbalVoice.h"
#include "sequencer.h"
#include <string.h>
#include "usb_manager.h"
#include "clockSync.h"
#include "MidiParser.h"
#include "automationNode.h"
#include "SomGenerator.h"
#include "triggerJacks.h"
#include "timebase.h"
#include "ledHandler.h"
#include "menu.h"
#include "SceneData.h"


#define SEQ_INTERNAL_PPQ	96u
#define SEQ_MIDI_PPQ        24u
#define SEQ_DEFAULT_STEPS_PER_BEAT 4u
#define SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP (SEQ_INTERNAL_PPQ / SEQ_DEFAULT_STEPS_PER_BEAT)
#define SEQ_INTERNAL_TICKS_PER_MIDI_CLOCK   (SEQ_INTERNAL_PPQ / SEQ_MIDI_PPQ)
#define SEQ_MASTER_LOOP_STEPS 128u
#define SEQ_AUTO_SYNC_HOLD_US	500000UL

uint8_t seq_masterStepCnt=0;				/** compatibility mirror of the low 8 bits of seq_masterStepClock */
static uint16_t seq_masterStepClock = 0;    /**< 16-bit corrected default-step clock used for track-scale realign */
static uint32_t seq_elapsedPpqTicks = 0;    /**< 96 PPQ ticks elapsed since the current pattern/start reset */
static uint8_t seq_initialSchedulerTick = 1;/**< nonzero until the immediate step at PPQ tick 0 has been processed */
static uint8_t seq_internalMidiClockPhase = 0;
static uint32_t seq_trackEventCount[NUM_TRACKS];
uint8_t seq_rollRate = 0x08;				//start with roll rate = 1/16
uint8_t seq_rollState = 0;					/**< each bit represents a voice. if bit is set, roll is active*/

static int16_t seq_stepIndex[NUM_TRACKS]; /**< bridge step counter, -1 before start and 0..127 while running. Each track has its own counter for independent track lengths. */

static uint16_t seq_tempo = 120;			/**< seq speed in bpm*/

static uint32_t	seq_lastTick = 0;			/**< stores the time the last step change occured*/
static float	seq_deltaT;					/**< time in [ms] until the next step
 	 	 	 	 	 	 	 	 	 	 	 1000ms = 1 sec
 	 	 	 	 	 	 	 	 	 	 	 1 min = 60 sec*/
uint8_t seq_delayedSyncStepFlag = 0;		//normally sync steps will only be advanced by external midi clocks in ext. sync mode
											//if the shuffle needs a delayed sync step, it is indicated here.

uint8_t seq_isSyncExternal = SEQ_EXT_SYNC_OFF;
static uint8_t seq_autoSyncActiveSource = SEQ_EXT_SYNC_OFF;
static uint32_t seq_autoSyncLastUs = 0;
uint8_t seq_lastMasterStep[NUM_TRACKS];		//keeps track of the last triggered master sync step of each track


static uint8_t seq_SomModeActive = 0;

static uint8_t seq_mutedTracks=0;			/**< indicate which tracks are muted */
uint8_t seq_running = 0;					/**< 1 if running, 0 if stopped*/

uint8_t seq_activePattern = 0;				/**< the currently playing pattern*/
uint8_t seq_pendingPattern = 0;				/**< next pattern to play*/

uint8_t seq_recordActive = 0;				/**< set to 1 to activate the reording mode*/

uint8_t seq_eraseActive=0;					/**RECORD will be 1 if live erasing the active voice  */

uint8_t seq_quantisation = QUANT_16;

uint8_t seq_rndValue[NUM_TRACKS];			/**< random value for probability function*/

uint8_t seq_barCounter;						/**< counts the absolute position in bars since the seq was started */

static uint8_t seq_loadPendigFlag = 0;

// --AS Allow it to be configured whether it keeps track of bar position in the song for
// the purpose of pattern changes
uint8_t seq_resetBarOnPatternChange=0;

// --AS keep track of which midi notes are playing
static uint8_t midi_chan_notes[16];		    /**< what note is playing on each channel */
static uint16_t midi_notes_on=0;		    /**< which channels have a note currently playing */

const float seq_shuffleTable[16] =
{
		0.f,
		0.015625f,
		0.0625f,
		0.140625f,
		0.25f,
		0.390625f,
		0.5625f,
		0.765625f,
		1.f,
		0.984375f,
		0.9375f,
		0.859375f,
		0.75f,
		0.609375f,
		0.4375f,
		0.234375f,
};

uint8_t seq_newPatternAvailable = 0; //indicate that a new pattern has loaded in the background and we should switch

//for the automation tracks each track needs 2 modNodes
static AutomationNode seq_automationNodes[NUM_TRACKS][2];

static void seq_sendMidi(MidiMsg msg);
static void seq_sendRealtime(const uint8_t status);
static void seq_sendProgChg(const uint8_t ptn);
static void seq_processSchedulerTick(void);
static void seq_setStepIndexToStart();
//------------------------------------------------------------------------------
void seq_init()
{
	int i;

	for(i=0;i<NUM_TRACKS;i++) {
		autoNode_init(&seq_automationNodes[i][0]);
		autoNode_init(&seq_automationNodes[i][1]);
	}

	memset(seq_stepIndex,0,sizeof(seq_stepIndex));
	memset(seq_lastMasterStep,0,NUM_TRACKS);
	memset(seq_trackEventCount,0,sizeof(seq_trackEventCount));


	/*
	 * PatternData owns pattern storage after FrontPanelParser removal.
	 *
	 * Sequencer still initializes it because Sequencer startup is where the
	 * playback scheduler, automation nodes, and pattern arrays become usable.
	 * Later Scene/Pattern work can move this init call higher once Scene exists
	 * as a full subsystem.
	 */
	pat_init();

}
void seq_offsetTrackStepIndexForRotation(uint8_t trackNr, uint8_t oldRot,
                                         uint8_t newRot, uint8_t len)
{
	int16_t offset;
	int16_t si;

	/*
	 * Why: PatternData owns the stored rotation value, but seq_stepIndex[] is
	 * scheduler runtime state. Inputs are the old/new step rotations and
	 * effective track length. Output is an adjusted step index preserving
	 * live-rotation behavior. Risk: track/len must be bounded
	 * by PatternData before this hook is called.
	 */
	if (trackNr >= NUM_TRACKS || len == 0)
		return;

	offset = (int16_t)((newRot % len) - (oldRot % len));
	si = seq_stepIndex[trackNr] + offset;
	if (si < 0)
		si += len;
	else if (si >= len)
		si -= len;
	seq_stepIndex[trackNr] = si;
}
//------------------------------------------------------------------------------
static void seq_calcDeltaT(uint16_t bpm)
{
	//--- calc deltaT ----
	// The hardware timer services one 96 PPQ transport tick at a time.
	// Track/default-step scheduling is derived from those ticks separately.
	seq_deltaT 	= (1000*60)/bpm; 	//bei 12 = 500ms = time for one beat
	seq_deltaT /= (float)SEQ_INTERNAL_PPQ;
	seq_deltaT *= SYSTICK_TICKS_PER_MS; //systick_ticks is the canonical 0.25ms LXR tick

	/* Per-track shuffle is applied by seq_trackEventDueTick(). The transport
	 * PPQ tick duration stays uniform so one track's shuffle cannot perturb the
	 * scheduler clock used by every other track. */
}
//------------------------------------------------------------------------------
void seq_setBpm(uint16_t bpm)
{
	if (bpm == 0)
		bpm = 1;
	seq_tempo 	= bpm;
	//seq_calcDeltaT(bpm);
	lfo_recalcSync();
}
//------------------------------------------------------------------------------
uint16_t seq_getBpm()
{
	return seq_tempo;
}
//------------------------------------------------------------------------------
void seq_sync()
{
	sync_tick();
}
//------------------------------------------------------------------------------
void seq_setNextPattern(const uint8_t patNr)
{
	seq_pendingPattern = patNr;
	seq_loadPendigFlag = 1;
}
//------------------------------------------------------------------------------
void seq_armActivePatternReload(void)
{
	seq_loadPendigFlag = 1;
}
//------------------------------------------------------------------------------
static void seq_sendMidi(MidiMsg msg)
{
	//send to usb midi
	usb_sendMidi(msg);

	//send to hardware midi out jack
	uart_sendMidi(msg);

}


//------------------------------------------------------------------------------
static void seq_parseAutomationNodes(uint8_t track, Step* stepData)
{
	//set new destination
	autoNode_setDestination(&seq_automationNodes[track][0], stepData->param1Nr);
	autoNode_setDestination(&seq_automationNodes[track][1], stepData->param2Nr);
	//set new mod value
	autoNode_updateValue(&seq_automationNodes[track][0], stepData->param1Val);
	autoNode_updateValue(&seq_automationNodes[track][1], stepData->param2Val);
}
//------------------------------------------------------------------------------
void seq_triggerVoice(uint8_t voiceNr, uint8_t vol, uint8_t note)
{
	uint8_t midiChan; // which midi channel to send a note on
	uint8_t midiNote; // which midi note to send
	uint8_t midiVelocity;
	Step stepData;

	if(voiceNr > 6) return;

	/*
	 * PatternData owns the current Step; Sequencer only needs a playback-time
	 * snapshot for automation-node parsing and the legacy MIDI echo velocity.
	 * Trigger timing, choke behavior, DSP voice calls, and MIDI output remain
	 * sequencer runtime work.
	 */
	if (pat_readStep(seq_activePattern, voiceNr, (uint8_t)seq_stepIndex[voiceNr],
	                 &stepData)) {
		seq_parseAutomationNodes(voiceNr, &stepData);
		midiVelocity = (uint8_t)(stepData.volume & STEP_VOLUME_MASK);
	} else {
		midiVelocity = vol;
	}

	//turn the trigger off before sending the next one
	if(voiceNr>=5)
	{
		//hihat channels choke each other
		trigger_triggerVoice(5, TRIGGER_OFF);
		trigger_triggerVoice(6, TRIGGER_OFF);
	} else {
		trigger_triggerVoice(voiceNr, TRIGGER_OFF);
	}

	//--AS if a note is on for that channel send note-off first
	voiceControl_noteOff(voiceNr);

	//Trigger internal synth voice
	voiceControl_noteOn(voiceNr, note, vol);

	midiChan = (uint8_t)(scene_getTrackMidiChannel(seq_activePattern, voiceNr) - 1u);

	/*
	 * MIDI output note/channel are PatternData-owned track settings now. A note
	 * value of 0 preserves the old "use the triggered note" behavior; nonzero
	 * values override the outgoing MIDI note for this pattern track.
	 */
	midiNote = scene_getTrackMidiNote(seq_activePattern, voiceNr);
	if(midiNote == 0)
		midiNote = note;

	//send the new note to midi/usb out
	seq_sendMidiNoteOn(midiChan, midiNote, midiVelocity);
}
void seq_previewVoice(uint8_t voiceNr)
{
	uint8_t midiChan;
	uint8_t note;

	/*
	 * Trigger one voice without advancing or reading sequencer step state.
	 *
	 * Why: buttonHandler uses this for stopped-transport VOICE re-press
	 * preview. seq_triggerVoice() intentionally reads the current step for
	 * playback automation and MIDI velocity; preview must skip that so audition
	 * cannot depend on seq_stepIndex[] or write automation side effects.
	 *
	 * Input voiceNr is the 0-based UI track/voice. Outputs: trigger jack, synth
	 * voice, and MIDI note-on follow the same channel/note ownership as normal
	 * playback. Confederates: PatternData supplies the optional track MIDI note
	 * override, MidiVoiceControl owns note-on/off, and triggerJacks owns trigger
	 * pulse state. Invalid voices and running transport are ignored.
	 */
	if (voiceNr > 6u || seq_running)
		return;

	note = scene_getTrackMidiNote(seq_activePattern, voiceNr);
	if (note == 0u)
		note = PAT_DEFAULT_NOTE;

	if (voiceNr >= 5u) {
		trigger_triggerVoice(5, TRIGGER_OFF);
		trigger_triggerVoice(6, TRIGGER_OFF);
	} else {
		trigger_triggerVoice(voiceNr, TRIGGER_OFF);
	}

	voiceControl_noteOff(voiceNr);
	voiceControl_noteOn(voiceNr, note, ROLL_VOLUME);

	midiChan = (uint8_t)(scene_getTrackMidiChannel(seq_activePattern, voiceNr) - 1u);
	seq_sendMidiNoteOn(midiChan, note, ROLL_VOLUME);
}
//------------------------------------------------------------------------------
static uint8_t seq_determineNextPattern()
{
	/*
	 * Resolve the next automatic pattern target for the active pattern.
	 *
	 * PatternData owns the saved change-bar and next-pattern settings; Sequencer
	 * owns bar counting, random-target resolution, and the runtime pending-pattern
	 * state. Input is implicit seq_activePattern/seq_barCounter. Output is the
	 * pattern enum to keep or queue. Caller/confederate: the corrected master
	 * boundary scheduler invokes this before handling PAT_NEXT_RANDOM values.
	 */
	const uint8_t changeBar = pat_getPatternChangeBar(seq_activePattern);
	if(seq_barCounter % (changeBar + 1u) == 0)
		return pat_getPatternNext(seq_activePattern);
	else
		return seq_activePattern;
}

static void seq_resetScaledScheduler(void)
{
	/*
	 * Reset timing counters for the corrected 4-steps-per-beat grid.
	 *
	 * Why: transport start, external reset, and pattern changes need a common
	 * origin for the 16-bit master step clock and every per-track scale ratio.
	 * Outputs: elapsed 96-PPQ tick time returns to zero, each track's processed
	 * event count returns to zero, and the next scheduler pass will emit the
	 * immediate step at PPQ tick 0. PatternData still owns rotation/length/scale;
	 * seq_setStepIndexToStart() must be called alongside this when positions
	 * themselves need resetting.
	 */
	seq_elapsedPpqTicks = 0u;
	seq_masterStepClock = 0u;
	seq_masterStepCnt = 0u;
	seq_initialSchedulerTick = 1u;
	seq_internalMidiClockPhase = 0u;
	memset(seq_trackEventCount, 0, sizeof(seq_trackEventCount));
}

static uint32_t seq_trackEventBaseTick(uint8_t track, uint32_t eventIndex)
{
	TrackScaleRatio ratio = pat_getTrackScaleRatio(seq_activePattern, track);
	uint32_t threshold;

	/*
	 * Convert one per-track event number to its unshuffled absolute PPQ tick.
	 *
	 * Why: scale and shuffle both need an absolute, restartable timing origin.
	 * Inputs are a track and event index since pattern start; event 0 is the
	 * immediate start event at tick 0. Output is the 96-PPQ tick where that
	 * event would occur before shuffle. Confederates: PatternData supplies the
	 * exact scale ratio. Risk: callers use this in scheduler timing, so keep it
	 * bounded and avoid stateful drift corrections here.
	 */
	if (ratio.num == 0u || ratio.den == 0u)
		ratio.num = ratio.den = 1u;
	if (eventIndex == 0u)
		return 0u;
	threshold = (uint32_t)SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP * (uint32_t)ratio.den;
	return (uint32_t)(((eventIndex * threshold) + (uint32_t)ratio.num - 1u) /
	                  (uint32_t)ratio.num);
}

static uint32_t seq_trackEventShuffleOffset(uint8_t track, uint32_t eventIndex)
{
	uint8_t shuffle = pat_getTrackShuffle(seq_activePattern, track);
	uint32_t baseTick;
	uint8_t phase;
	float amount;

	/*
	 * Calculate one event's absolute per-track shuffle offset in PPQ ticks.
	 *
	 * Why: old shuffle altered the global tick delta, which made every track
	 * share one groove amount. Per-track shuffle must be derived from each
	 * track's own event timing so different tracks can swing independently.
	 *
	 * Inputs: track and event index. Output: a non-negative delay in 96-PPQ
	 * ticks, scaled from the existing 16-entry shuffle curve. Clients:
	 * seq_trackEventDueTick() only. Risk: event 0 must remain unshifted so
	 * transport start and pattern realign still have a stable downbeat.
	 */
	if (shuffle == 0u || eventIndex == 0u)
		return 0u;
	baseTick = seq_trackEventBaseTick(track, eventIndex);
	phase = (uint8_t)(baseTick & 0x0fu);
	amount = seq_shuffleTable[phase] * ((float)shuffle / 127.0f) * 16.0f;
	return (uint32_t)(amount + 0.5f);
}

static uint32_t seq_trackEventDueTick(uint8_t track, uint32_t eventIndex)
{
	/*
	 * Return the absolute PPQ tick at which one track event is due.
	 *
	 * Why: deriving due time from event count, scale, and shuffle prevents
	 * fractional scale and per-track shuffle from accumulating drift over
	 * repeated 128-step loops. Inputs are a track and event count since pattern
	 * start. Output is an absolute 96-PPQ tick threshold. Confederates:
	 * seq_dueTrackEvents() compares this threshold to seq_elapsedPpqTicks.
	 */
	return seq_trackEventBaseTick(track, eventIndex) +
	       seq_trackEventShuffleOffset(track, eventIndex);
}

static uint32_t seq_dueTrackEvents(uint8_t track)
{
	uint32_t due = seq_trackEventCount[track];

	/*
	 * Count unprocessed events whose absolute due tick has arrived.
	 *
	 * Input: track index. Output: number of events that should have occurred for
	 * this track by seq_elapsedPpqTicks. Caller seq_processSchedulerTick() then
	 * advances until seq_trackEventCount reaches this number. This loop is
	 * bounded by actual backlog; under normal internal clocking it visits at
	 * most one event for slow/default tracks and a small burst for fast scales.
	 */
	while (seq_trackEventDueTick(track, due) <= seq_elapsedPpqTicks)
		due++;
	return due;
}

static void seq_advanceTrackStep(uint8_t track)
{
	uint8_t seqlen;

	/*
	 * Advance and service exactly one stored step for one track.
	 *
	 * Caller context: seq_processSchedulerTick() may call this more than once
	 * for a fast scale if multiple step events are due. It deliberately triggers
	 * every visited step, rather than jumping to the final landed position.
	 */
	seq_stepIndex[track]++;
	seqlen = pat_getEffectiveTrackLength(seq_activePattern, track);
	if ((seq_stepIndex[track] >= seqlen) || (seq_stepIndex[track] >= NUM_STEPS))
		seq_stepIndex[track] = 0;

	if (seq_SomModeActive) {
		if (track == 0u)
			som_tick(seq_stepIndex[0], seq_mutedTracks);
		return;
	}

	if (!(seq_mutedTracks & (1u << track))) {
		if (pat_isStepActive(track, (uint8_t)seq_stepIndex[track], seq_activePattern)) {
			if (seq_eraseActive && track == menu_getActiveVoice()) {
				pat_eraseStep(seq_activePattern,
				              menu_getActiveVoice(),
				              (uint8_t)seq_stepIndex[track]);
			} else {
				seq_rndValue[track] = GetRngValue() & 0x7f;
				if (seq_rndValue[track] <=
				    pat_getStepProbability(seq_activePattern, track,
				                           (uint8_t)seq_stepIndex[track])) {
					const uint8_t vol = pat_getStepVolume(seq_activePattern, track,
					                                      (uint8_t)seq_stepIndex[track]);
					const uint8_t note = pat_getStepNote(seq_activePattern, track,
					                                      (uint8_t)seq_stepIndex[track]);
					seq_triggerVoice(track, vol, note);
				}
			}
		}
	}

	if (seq_rollRate != 0xffu && (seq_rollState & (1u << track))) {
		if ((seq_stepIndex[track] % seq_rollRate) == 0) {
			const uint8_t vol = ROLL_VOLUME;
			const uint8_t note = pat_getStepNote(seq_activePattern, track,
			                                      (uint8_t)seq_stepIndex[track]);
			seq_triggerVoice(track, vol, note);
			seq_addNote(track, vol, note);
		}
	}
}

static uint8_t seq_handleMasterBoundary(void)
{
	uint8_t len0 = pat_getEffectiveTrackLength(seq_activePattern, 0);
	uint8_t masterStepPos = (uint8_t)(seq_masterStepClock % len0);

	/*
	 * Master boundaries are based on the corrected default grid, not on any one
	 * scaled track. They own pattern-boundary commit, trigger clock output, beat
	 * LED pulse, and 128-step drift correction checkpoints.
	 */
	if (masterStepPos == 0u && seq_masterStepClock != 0u) {
		seq_barCounter++;
		if (seq_activePattern == seq_pendingPattern) {
			seq_pendingPattern = seq_determineNextPattern();
			if (seq_pendingPattern >= PAT_NEXT_RANDOM) {
				uint8_t limit = seq_pendingPattern - PAT_NEXT_RANDOM + 2u;
				uint8_t rnd = GetRngValue() % limit;
				seq_pendingPattern = rnd;
			}
		}

		if ((seq_activePattern != seq_pendingPattern) || seq_loadPendigFlag) {
			if (seq_resetBarOnPatternChange)
				seq_barCounter = 0u;
			seq_loadPendigFlag = 0u;
			seq_newPatternAvailable = 0u;
			seq_activePattern = seq_pendingPattern;
			seq_setStepIndexToStart();
			seq_resetScaledScheduler();
			led_notifyPatternChanged(seq_activePattern);
			seq_sendProgChg(seq_activePattern);
			voiceControl_noteOff(0xFF);
			return 1u;
		}
	}

	if ((seq_masterStepClock % SEQ_DEFAULT_STEPS_PER_BEAT) == 0u) {
		seq_ledState.beatPulse = 1u;
		seq_ledState.dirty |= SEQ_LED_DIRTY_BEAT;
	} else if ((seq_masterStepClock % SEQ_DEFAULT_STEPS_PER_BEAT) == 1u) {
		seq_ledState.beatPulse = 0u;
		seq_ledState.dirty |= SEQ_LED_DIRTY_BEAT;
	}

	trigger_clockTick((uint8_t)((seq_masterStepClock % NUM_STEPS) + 1u));
	return 0u;
}

void seq_realignActivePatternToMasterClock(void)
{
	uint8_t track;

	/*
	 * Recalculate runtime track positions from the master clock.
	 *
	 * This is a performance action, not a PatternData edit. It uses the same
	 * due-event math as normal playback, then rewrites only sequencer counters:
	 * seq_stepIndex[] and seq_trackEventCount[]. Fractional scale ratios are
	 * therefore aligned to the position they would have reached from PPQ tick
	 * zero, and repeated calls do not accumulate rounding drift.
	 */
	for (track = 0u; track < NUM_TRACKS; track++) {
		uint8_t len = pat_getEffectiveTrackLength(seq_activePattern, track);
		uint8_t rot = pat_getTrackRotation(seq_activePattern, track);
		uint32_t due = seq_dueTrackEvents(track);
		uint32_t played = (due == 0u) ? 0u : (due - 1u);
		if (len == 0u)
			len = NUM_STEPS;
		rot = (uint8_t)(rot % len);
		seq_trackEventCount[track] = due;
		seq_stepIndex[track] = (int16_t)((rot + (played % len)) % len);
		seq_lastMasterStep[track] = (uint8_t)seq_stepIndex[track];
	}
	seq_ledState.chaseStep = seq_stepIndex[menu_getActiveVoice()];
	seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;
}

static void seq_processSchedulerTick(void)
{
	uint8_t track;
	uint8_t anyAdvanced = 0u;

	if (!seq_running)
		return;

	if (seq_initialSchedulerTick) {
		seq_initialSchedulerTick = 0u;
		seq_masterStepClock = 0u;
		seq_masterStepCnt = 0u;
		if (seq_handleMasterBoundary())
			return;
	} else {
		seq_elapsedPpqTicks++;
		if ((seq_elapsedPpqTicks % SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP) == 0u) {
			seq_masterStepClock =
				(uint16_t)(seq_elapsedPpqTicks / SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP);
			seq_masterStepCnt = (uint8_t)seq_masterStepClock;
			if (seq_handleMasterBoundary())
				return;
		}
	}

	for (track = 0u; track < NUM_TRACKS; track++) {
		uint32_t due = seq_dueTrackEvents(track);
		while (seq_trackEventCount[track] < due) {
			seq_advanceTrackStep(track);
			seq_trackEventCount[track]++;
			anyAdvanced = 1u;
		}
	}

	if (!seq_initialSchedulerTick &&
	    seq_masterStepClock != 0u &&
	    (seq_masterStepClock % SEQ_MASTER_LOOP_STEPS) == 0u &&
	    (seq_elapsedPpqTicks % SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP) == 0u) {
		seq_realignActivePatternToMasterClock();
	}

	if (anyAdvanced) {
		seq_ledState.chaseStep = seq_stepIndex[menu_getActiveVoice()];
		seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;
	}

	midiParser_checkMtc();
}
//------------------------------------------------------------------------------
uint8_t seq_getExtSync()
{
	if (seq_isSyncExternal == SEQ_EXT_SYNC_AUTO) {
		if (seq_autoSyncActiveSource == SEQ_EXT_SYNC_OFF)
			return 0;
		if (timebase_tim2Delta(timebase_tim2Now(), seq_autoSyncLastUs) >
				SEQ_AUTO_SYNC_HOLD_US) {
			/* AUTO falls back to the internal clock when the selected external
			** source disappears. Kick the scheduler out of any long external-sync
			** wait so free-run resumes promptly. */
			seq_autoSyncActiveSource = SEQ_EXT_SYNC_OFF;
			seq_deltaT = 0;
			seq_lastTick = systick_ticks;
			return 0;
		}
		return 1;
	}
	return (uint8_t)(seq_isSyncExternal != SEQ_EXT_SYNC_OFF);
}
//------------------------------------------------------------------------------
void seq_setExtSync(uint8_t isExt)
{
	seq_setExtSyncSource(isExt ? SEQ_EXT_SYNC_DIN : SEQ_EXT_SYNC_OFF);
}
//------------------------------------------------------------------------------
void seq_setExtSyncSource(uint8_t source)
{
	if (source > SEQ_EXT_SYNC_AUTO)
		source = SEQ_EXT_SYNC_OFF;
	seq_isSyncExternal = source;
	seq_autoSyncActiveSource = SEQ_EXT_SYNC_OFF;
	seq_deltaT = 0;
}
//------------------------------------------------------------------------------
uint8_t seq_getExtSyncSource(void)
{
	return seq_isSyncExternal;
}
//------------------------------------------------------------------------------
void seq_noteExtSyncActivity(uint8_t source, uint32_t timestampUs)
{
	if (source == SEQ_EXT_SYNC_OFF || source > SEQ_EXT_SYNC_PULSE)
		return;

	if (seq_isSyncExternal == source) {
		seq_autoSyncActiveSource = source;
		seq_autoSyncLastUs = timestampUs;
		return;
	}

	if (seq_isSyncExternal == SEQ_EXT_SYNC_AUTO) {
		/* AUTO priority, low to high: internal, USB, DIN, jack pulse. A lower
		** priority source cannot steal the clock while a higher source is fresh. */
		if (seq_autoSyncActiveSource != SEQ_EXT_SYNC_OFF &&
				seq_autoSyncActiveSource > source &&
				timebase_tim2Delta(timestampUs, seq_autoSyncLastUs) <=
				SEQ_AUTO_SYNC_HOLD_US)
			return;
		seq_autoSyncActiveSource = source;
		seq_autoSyncLastUs = timestampUs;
	}
}
//------------------------------------------------------------------------------
void seq_setDeltaT(float delta)
{
	seq_deltaT = delta;
}
//------------------------------------------------------------------------------

void seq_triggerNextMasterStep(uint8_t stepSize)
{
	/*
	 * Advance scheduler time from a trigger-jack pulse.
	 *
	 * Trigger input still reports spacing in the legacy native 32 PPQ units
	 * (`PRE_4_PPQ == 8`, etc.). The corrected sequencer scheduler runs at 96
	 * PPQ, so one native unit equals three internal ticks. Processing those
	 * ticks through the same scheduler path keeps track-scale timing, every-step
	 * visitation, and master-clock loop correction identical between internal,
	 * MIDI, and pulse sync.
	 */
	uint16_t ticks = (uint16_t)stepSize * 3u;
	while (ticks--) {
		seq_processSchedulerTick();
	}
	seq_lastTick = systick_ticks;
	seq_calcDeltaT(seq_tempo);
}
//------------------------------------------------------------------------------
void seq_resetDeltaAndTick()
{
	uint8_t i;

	/*
	 * MIDI clock is 24 PPQ while the internal scheduler is 96 PPQ, so every
	 * external MIDI clock pulse advances four internal scheduler ticks. This
	 * replaces the old "4 steps every 3 clocks" bridge, which belonged to the
	 * previous 32-steps-per-beat grid.
	 */
	for (i = 0u; i < SEQ_INTERNAL_TICKS_PER_MIDI_CLOCK; i++)
		seq_processSchedulerTick();
	seq_lastTick = systick_ticks;
	seq_calcDeltaT(seq_tempo);
}
//------------------------------------------------------------------------------
void seq_resetToPatternStart(void)
{
	/* External reset should reposition the sequence without toggling transport
	** state or sending MIDI stop/start. The next clock pulse will play the
	** pattern start according to each track's rotation. */
	seq_barCounter = 0;
	seq_delayedSyncStepFlag = 0;
	seq_setStepIndexToStart();
	seq_resetScaledScheduler();
	seq_deltaT = 0;
	seq_lastTick = systick_ticks;
}
//------------------------------------------------------------------------------
/** call periodically to check if the next step has to be processed */
void seq_tick()
{
	if(systick_ticks-seq_lastTick >= seq_deltaT)
	{

		float rest = systick_ticks-seq_lastTick - seq_deltaT;
		seq_lastTick = systick_ticks;
		seq_calcDeltaT(seq_tempo);
		seq_deltaT = seq_deltaT - rest;

		if (!seq_getExtSync())
			seq_processSchedulerTick();

		if(!seq_getExtSync()) //only send internal MIDI clock to output when external sync is off
		{
			if (seq_internalMidiClockPhase == 0u)
			{
				seq_sendRealtime(MIDI_CLOCK);
			}
			seq_internalMidiClockPhase++;
			if (seq_internalMidiClockPhase >= SEQ_INTERNAL_TICKS_PER_MIDI_CLOCK)
				seq_internalMidiClockPhase = 0u;
		}
	}


}
//------------------------------------------------------------------------------
void seq_setQuantisation(uint8_t value)
{
	seq_quantisation = value;
}
uint8_t seq_isRunning() {
	return seq_running;
}

//------------------------------------------------------------------------------
void seq_setRunning(uint8_t isRunning)
{
	seq_running = isRunning;
	//jump to 1st step if sequencer is stopped
	if(!seq_running)
	{

		// --AS reset all track rotations to 0. We are not saving rotated value. it's a performance tool.
		/*
		 * Stop resets performance track rotations for the active Pattern.
		 *
		 * Rotation storage now belongs to PatternData, so Sequencer calls
		 * pat_setTrackRotation() for each track. The separate
		 * led_notifyTrackRotationReset() call updates only the front-panel menu
		 * display value; PatternData intentionally does not know about LEDs.
		 *
		 * Risk: this preserves current behavior where stop clears rotations
		 * instead of saving them. The scoping notes expect rotation policy to be
		 * revisited when Pattern owns more sequencer behavior.
		 */
		uint8_t i;
		for(i=0;i<NUM_TRACKS;i++) {
			pat_setTrackRotation(seq_activePattern, i, 0);
		}
		led_notifyTrackRotationReset(0);

		//reset song position bar counter
		seq_barCounter = 0;
		seq_resetScaledScheduler();
		//so the next seq_tick call will trigger the next step immediately
		seq_deltaT = 0;
		seq_sendRealtime(MIDI_STOP);

		//--AS send notes off on all channels that have notes playing and reset our bitmap to reflect that
		voiceControl_noteOff(0xFF);

		trigger_reset(0);
		trigger_allOff();


		// --AS if mtc was doing it's thing, tell it to stop it.
		midiParser_checkMtc();
	} else {
		seq_resetScaledScheduler();
		seq_sendRealtime(MIDI_START);
		trigger_reset(1);
	}

	// set start points back to default (happens on start and stop. needs to happen on start
	// in case the user has entered a rotate value while stopped)
	seq_setStepIndexToStart();

}
//------------------------------------------------------------------------------
void seq_setMute(uint8_t trackNr, uint8_t isMuted)
{
	if(trackNr==7)
	{
		//unmute all
		seq_mutedTracks = 0;
	} else {
		//mute/unmute tracks
		if(isMuted) {
			//mute track
			seq_mutedTracks |= (1<<trackNr);
			// --AS turn off the midi note that may be playing on that track
			voiceControl_noteOff(midi_MidiChannels[trackNr]);
		} else {
			//unmute track
			seq_mutedTracks &= ~(1<<trackNr);
		}
	}
};
//------------------------------------------------------------------------------
uint8_t seq_isTrackMuted(uint8_t trackNr)
{
	if(seq_mutedTracks & (1<<trackNr) )
	{
		return 1;
	}
	return 0;
}
void seq_setRoll(uint8_t voice, uint8_t onOff)
{
	if(voice >= 7) return;

	if(onOff) {
		seq_rollState |= (1<<voice);
		if(seq_rollRate == 0xff) {
			//trigger one shot
			seq_triggerVoice(voice,ROLL_VOLUME,PAT_DEFAULT_NOTE);
			//record roll notes
			seq_addNote(voice,ROLL_VOLUME,PAT_DEFAULT_NOTE);
		}
	} else {
		seq_rollState &= ~(1<<voice);
	}
};
//--------------------------------------------------------------------------------
void seq_setRollRate(uint8_t rate)
{
	/*
	0 - one shot immediate trigger
	1 - 1/1
	2 - 1/2
	3 - 1/3
	4 - 1/4
	5 - 1/6
	6 - 1/8
	7 - 1/12
	8 - 1/16
	9 - 1/24
	10 - 1/32
	11 - 1/48
	12 - 1/64
	13 - 1/128
				*/

	switch(rate)
	{
	case 0:
		seq_rollRate = 0xfe;
		break;
	case 1: // 1/1
		seq_rollRate = 0x7f;
		break;

	case 2: // 1/2
		seq_rollRate = 0x3f;
		break;

	case 3:// 1/3
		seq_rollRate = 0x2a;
			break;

	case 4:// 1/4
		seq_rollRate = 0x1f;
			break;

	case 5:// 1/6
		seq_rollRate = 0x31;
			break;

	case 6:// 1/8
			seq_rollRate = 0x0f;
			break;

	case 7:// 1/12
		seq_rollRate = 0x0a;
			break;

	case 8:// 1/16
		seq_rollRate = 0x07;
			break;

	case 9: // 1/24
		seq_rollRate = 0x05;
		break;

	case 10:// 1/32
		seq_rollRate = 0x03;
		break;

	case 11:// 1/48
		seq_rollRate = 0x02;
		break;

	case 12://1/64
		seq_rollRate = 0x01;
		break;

	case 13://1/128
		seq_rollRate = 0x00;
		break;
	}
	seq_rollRate +=1; //is there a reason for this offset here? seems the value could be assigned directly!?!

}
//------------------------------------------------------------------------
/** quantize a step to the seq_quantisation value*/
#define QUANT(x) (NUM_STEPS/x)
static uint8_t seq_quantize(uint8_t step)
{
	uint8_t quantisationMultiplier=1;
	switch(seq_quantisation)
	{
	case QUANT_8:
		quantisationMultiplier = QUANT(8);
		break;

	case QUANT_16:
		quantisationMultiplier = QUANT(16);
		break;

	case QUANT_32:
		quantisationMultiplier = QUANT(32);
		break;

	case QUANT_64:
		quantisationMultiplier = QUANT(64);
		break;

	case NO_QUANTISATION:
	default:
		return step;
		break;
	}

	//now calc the quantisation
	float frac = step/(float)quantisationMultiplier;
	uint8_t itg = (uint8_t)frac;
	frac = frac - itg;

	if(frac>=0.5f)
	{
		return ((itg + 1)*quantisationMultiplier)&0x7f;
	}
	return itg*quantisationMultiplier;
}
//------------------------------------------------------------------------
void seq_recordAutomation(uint8_t voice, uint8_t dest, uint8_t value)
{
	/*
	 * Records automation from live parameter changes into PatternData.
	 *
	 * Callers: Preset sound-parameter apply and MidiParser global-channel CC
	 * handling. Sequencer owns the recording gate and quantization because those
	 * depend on current playback position; PatternData owns the step/lane write.
	 *
	 * Inputs: voice is the target track, dest is the automation parameter id or
	 * legacy CC destination, value is the recorded value. Output: automation is
	 * written to the active pattern when recording/step-active conditions pass.
	 *
	 * Risk: pat_recordArmedAutomation() is called even when seq_recordActive is
	 * off so long-press armed automation can still capture a parameter edit,
	 * matching the old ARM_AUTOMATION_STEP opcode behavior.
	 */
	if(seq_recordActive)
	{
		uint8_t quantizedStep = seq_quantize(seq_stepIndex[voice]);

		//only record to active steps
		if (pat_isStepActive(voice, quantizedStep, seq_activePattern))
		{
			pat_recordAutomation(seq_activePattern, voice, quantizedStep, dest, value);
		}
	}

	pat_recordArmedAutomation(seq_activePattern, dest, value);
}
//------------------------------------------------------------------------
void seq_addNote(uint8_t trackNr,uint8_t vel, uint8_t note)
{
	/*
	 * Records a played note into the active or next Pattern when recording.
	 *
	 * Callers: MIDI note input, roll performance, and internal recording paths.
	 * Sequencer owns quantization and pattern-boundary timing; PatternData owns
	 * the actual Step mutation through pat_recordNote().
	 *
	 * Inputs: trackNr target track, vel recorded velocity, note recorded note.
	 * Outputs: when recording is active, PatternData updates note/velocity,
	 * probability and active bit; visible record LEDs
	 * are marked dirty if the edited track/pattern is currently shown by Menu.
	 *
	 * Risk: quantization can push a late note to step 0 of the next pattern, so
	 * Sequencer must keep the target-pattern decision here even though PatternData
	 * owns the write.
	 */
	uint8_t targetPattern;
	//only record notes when seq is running and recording
	if(seq_running && seq_recordActive)
	{
		const uint8_t quantizedStep = seq_quantize((uint8_t)seq_stepIndex[trackNr]);


		// --AS **RECORD fix for recording across patterns
		if(quantizedStep==0 && seq_stepIndex[trackNr] > (NUM_STEPS/2)) {
			// this means that we hit a note in 2nd half of the bar and quantization pushed
			// the note to position 0 of the next bar.
			// need to see if there is about to be a pattern change so that the note
			// ends up on 0 of the next pattern
			targetPattern=seq_determineNextPattern();

		} else
			targetPattern=seq_activePattern;

		pat_recordNote(targetPattern, trackNr, (uint8_t)quantizedStep, vel, note);

		if( (menu_getViewedPattern() == targetPattern) && ( menu_getActiveVoice() == trackNr) )
		{
			/*
			 * Recording LED updates are queued rather than drawn here.
			 *
			 * Sequencer knows the recorded step, but ledHandler owns the LED
			 * hardware and buttonHandler owns selected-step/shift/mode UI
			 * context. The dirty flags tell led_processSeqLedState() to repaint
			 * only when the recorded track/pattern is visible.
			 */
			seq_ledState.recordSubStep = quantizedStep;
			seq_ledState.dirty |= SEQ_LED_DIRTY_REC_SUB;
			seq_ledState.recordMainStep = quantizedStep;
			seq_ledState.dirty |= SEQ_LED_DIRTY_REC_MAIN;
		}
	}
}

//------------------------------------------------------------------------
void seq_setRecordingMode(uint8_t active)
{
	seq_recordActive = active;
}

void seq_setErasingMode(uint8_t active)
{
	seq_eraseActive = active;
}

void seq_midiNoteOff(uint8_t chan)
{
	uint8_t i;
	MidiMsg msg;

	// we are not filtering according to tx filter because they might have turned that
	// setting on while a note was sustaining

	msg.bits.length=2;
	msg.data2=0;

	if(chan==0xff) { // all notes off
		for(i=0; i<16; i++)
			if((1<<i) & midi_notes_on) {
				msg.status=	NOTE_OFF | i;
				msg.data1=midi_chan_notes[i];
				seq_sendMidi(msg);
			}
		// reset all
		midi_notes_on=0;
		return;
	}
	// The proper way to do a note off is with 0x80. 0x90 with velocity 0 is also used, however I think there is still
	// synth gear out there that doesn't recognize that properly.
	if((1<<chan) & midi_notes_on) {
		msg.status=	NOTE_OFF | chan;
		msg.data1=midi_chan_notes[chan];
		seq_sendMidi(msg);
		// turn off our knowledge of that note playing
		midi_notes_on &= (~(1<<chan));
	}
}

static void seq_sendRealtime(const uint8_t status)
{
	MidiMsg msg = {0,0,0, {0,0,0}};
	// --AS FILT filter out realtime msgs if appropriate
	if((midiParser_txRxFilter & 0x20)==0)
		return;
	msg.status=status;
	seq_sendMidi(msg);
}

/* Send a note on message. This will filter out these messages if appropriate
 */
void seq_sendMidiNoteOn(const uint8_t channel, const uint8_t note, const uint8_t veloc)
{
	MidiMsg msg = {0,0,0, {0,0,2}};
	// --AS FILT filter out note msgs if appropriate
	if((midiParser_txRxFilter & 0x10)==0)
		return;

	msg.status=NOTE_ON | channel;
	msg.data1=note;
	msg.data2=veloc;
	seq_sendMidi(msg);

	// keep track of which notes are on so we can turn them off later
	midi_chan_notes[channel]=note;
	midi_notes_on |= (1 << channel);

}

/* This will send a prog change on the global channel and will filter
 * out the message if appropriate
 */
static void seq_sendProgChg(const uint8_t ptn)
{
	MidiMsg msg = {0,0,0, {0,0,1}};

	// --AS FILT filter out PC msgs if appropriate
	if((midiParser_txRxFilter & 0x80)==0)
		return;

	msg.status = PROG_CHANGE | midi_MidiChannels[7];
	msg.data1=ptn;
	msg.bits.length=1;
	seq_sendMidi(msg);
}

/* **PATROT set the step starting index to the position where the pattern rotation would have it start.
 *  A pattern rotation of 0 means start at the beginning of the track. During the
 *  bridge, rotation is a step offset in the effective 1..128 step length.
 *
 *  This is called when the sequencer starts/stops running, also when a pattern change takes place.
 */
static void seq_setStepIndexToStart()
{
	/*
	 * Reset runtime track counters to each track's PatternData rotation.
	 *
	 * PatternData owns stored rotation and effective length; Sequencer owns
	 * `seq_stepIndex[]`, `seq_lastMasterStep[]`, and seq_trackEventCount[], which
	 * are scheduler runtime state. Input is implicit seq_activePattern. Output:
	 * every track starts one step before its rotated start so the immediate PPQ-0
	 * scheduler event lands on the correct position. Callers: transport
	 * start/stop, pattern changes, and external reset. Risk: length and rotation
	 * must stay normalized by PatternData.
	 */
	uint8_t len, rot, i;
	for(i=0;i<NUM_TRACKS;i++) {
		// adjust rot in case the pattern length is less than the rotated amount
		// len is 0-15 where a value of 0 means 16
		rot = pat_getTrackRotation(seq_activePattern, i);
		len = pat_getEffectiveTrackLength(seq_activePattern, i);
		if((len != 16u) && (rot > len))
			rot = rot % len;

		// this is for external clock sync via trigger expansion kit (the ext tick will adjust this -1)
		seq_lastMasterStep[i] = rot;
		seq_trackEventCount[i] = 0u;

		// -1 here because we increment it first thing when we start
		seq_stepIndex[i] = (int16_t)rot - 1;

	}

}
