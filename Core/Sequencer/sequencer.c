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


#define SEQ_PRESCALER_MASK 	0x03
#define MIDI_PRESCALER_MASK	0x04
#define SEQ_INTERNAL_PPQ	96.f
#define SEQ_AUTO_SYNC_HOLD_US	500000UL
static uint8_t seq_prescaleCounter = 0;

uint8_t seq_masterStepCnt=0;				/** keeps track of the played steps between 0 and 127 independent from the track counters*/
uint8_t seq_rollRate = 0x08;				//start with roll rate = 1/16
uint8_t seq_rollState = 0;					/**< each bit represents a voice. if bit is set, roll is active*/

static int8_t 	seq_stepIndex[NUM_TRACKS];	/**< we have 16 steps consisting of 8 sub steps = 128 steps.
											     each track has its own counter to allow different pattern lengths */

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


float seq_shuffle = 0;

static uint8_t seq_SomModeActive = 0;

static uint8_t seq_mutedTracks=0;			/**< indicate which tracks are muted */
uint8_t seq_running = 0;					/**< 1 if running, 0 if stopped*/

uint8_t seq_activePattern = 0;				/**< the currently playing pattern*/
uint8_t seq_pendingPattern = 0;				/**< next pattern to play*/

uint8_t seq_selectedStep = 0;

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

float seq_lastShuffle = 0;

uint8_t seq_newPatternAvailable = 0; //indicate that a new pattern has loaded in the background and we should switch

//for the automation tracks each track needs 2 modNodes
static AutomationNode seq_automationNodes[NUM_TRACKS][2];

static void seq_sendMidi(MidiMsg msg);
static void seq_sendRealtime(const uint8_t status);
static void seq_sendProgChg(const uint8_t ptn);
static void seq_eraseStepAndSubSteps(const uint8_t voice, const uint8_t mainStep);
static void seq_activateTmpPattern();
static void seq_nextStep();
static uint8_t seq_isNextStepSyncStep();
static uint8_t seq_intIsStepActive(uint8_t voice, uint8_t stepNr, uint8_t patternNr);
static uint8_t seq_intIsMainStepActive(uint8_t voice, uint8_t mainStepNr, uint8_t pattern);
static void seq_resetNote(Step *step);
static void seq_setStepIndexToStart();
//------------------------------------------------------------------------------
void seq_init()
{
	int i;

	for(i=0;i<NUM_TRACKS;i++) {
		autoNode_init(&seq_automationNodes[i][0]);
		autoNode_init(&seq_automationNodes[i][1]);
	}

	memset(seq_stepIndex,0,NUM_TRACKS);
	memset(seq_lastMasterStep,0,NUM_TRACKS);


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
//------------------------------------------------------------------------------
static void seq_activateTmpPattern()
{
	/*
	 * Commits an asynchronously loaded pattern into the active pattern slot.
	 *
	 * Caller: seq_nextStep() when filesystem has loaded into pat_tmpPattern and
	 * the pattern boundary permits activation. PatternData owns both buffers,
	 * but Sequencer currently owns the timing of when a loaded pattern becomes
	 * audible.
	 *
	 * Inputs: seq_activePattern selects the destination pattern slot; the source
	 * is pat_tmpPattern. Outputs: active pattern step, main-step, settings, and
	 * length/rotation data are overwritten.
	 *
	 * Risk: this still uses seq_patternSet/seq_tmpPattern compatibility macros
	 * from PatternData.h so the playback refactor can happen later without
	 * reintroducing front-panel parser behavior.
	 */
	memcpy(&seq_patternSet.seq_subStepPattern[seq_activePattern],&seq_tmpPattern.seq_subStepPattern,sizeof(Step)*NUM_TRACKS*NUM_STEPS);
	memcpy(&seq_patternSet.seq_mainSteps[seq_activePattern],&seq_tmpPattern.seq_mainSteps,sizeof(uint16_t)*NUM_TRACKS);
	memcpy(&seq_patternSet.seq_patternSettings[seq_activePattern],&seq_tmpPattern.seq_patternSettings,sizeof(PatternSetting));
	memcpy(&seq_patternSet.seq_patternLengthRotate[seq_activePattern],&seq_tmpPattern.seq_patternLengthRotate,sizeof(LengthRotate)*NUM_TRACKS);
}
//------------------------------------------------------------------------------
void seq_setShuffle(float shuffle)
{
	seq_shuffle = shuffle;
}
void seq_offsetTrackStepIndexForRotation(uint8_t trackNr, uint8_t oldRot,
                                         uint8_t newRot, uint8_t len)
{
	int8_t offset;
	int16_t si;

	/*
	 * Why: PatternData owns the stored rotation value, but seq_stepIndex[] is
	 * scheduler runtime state. Inputs are the old/new main-step rotations and
	 * effective track length. Output is an adjusted sub-step index preserving
	 * the pre-refactor live-rotation behavior. Risk: track/len must be bounded
	 * by PatternData before this hook is called.
	 */
	if (trackNr >= NUM_TRACKS || len == 0)
		return;

	offset = (int8_t)(((int8_t)newRot % (int8_t)len) -
	                  ((int8_t)oldRot % (int8_t)len));
	si = seq_stepIndex[trackNr] + (offset * 8);
	if (si < 0)
		si += (len * 8);
	else if (si >= (len * 8))
		si -= (len * 8);
	seq_stepIndex[trackNr] = (int8_t)si;
}
//------------------------------------------------------------------------------
static void seq_calcDeltaT(uint16_t bpm)
{
	//--- calc deltaT ----
	// f�r 4/4tel takt -> 1 beat = 4 main steps = 4*8 = 32 sub steps
	// 120 bpm 4/4tel = 120 * 1 beat / 60sec = 120 * 32 in 60 sec;
	seq_deltaT 	= (1000*60)/bpm; 	//bei 12 = 500ms = time for one beat
	seq_deltaT /= SEQ_INTERNAL_PPQ; //we run the internal clock at 96ppq -> seq clock 32 ppq == prescaler 3, midi clock 24 ppq == prescale 4
	seq_deltaT *= SYSTICK_TICKS_PER_MS; //systick_ticks is the canonical 0.25ms LXR tick

	//--- calc shuffle ---
	if(seq_shuffle != 0)
	{
		//every 2nd and 4th 16th note in a beat is shifted full
		//=> step 8 and step 24
		//every 2nd 16th note in half a beat
		//every beat has 32 steps => half = 16
		uint8_t stepInHalfBeat = seq_masterStepCnt&0xf;
		const float shuffleFactor = seq_shuffleTable[stepInHalfBeat] * seq_shuffle;
		const float originalDeltaT = seq_deltaT;

		seq_deltaT += shuffleFactor * originalDeltaT * 16.f;
		seq_deltaT -= seq_lastShuffle * originalDeltaT * 16.f;

		seq_lastShuffle = shuffleFactor;
	}
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

	if(voiceNr > 6) return;

	seq_parseAutomationNodes(voiceNr, &seq_patternSet.seq_subStepPattern[seq_activePattern][voiceNr][seq_stepIndex[voiceNr]]);

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

	midiChan = midi_MidiChannels[voiceNr];

	//--AS the note that is played will be whatever is received unless we have a note override set
	// A note override is any non-zero value for this parameter
	if(midi_NoteOverride[voiceNr] == 0)
		midiNote = note;
	else
		midiNote = midi_NoteOverride[voiceNr];

	//send the new note to midi/usb out
	seq_sendMidiNoteOn(midiChan, midiNote,
			seq_patternSet.seq_subStepPattern[seq_activePattern][voiceNr][seq_stepIndex[voiceNr]].volume&STEP_VOLUME_MASK);
}
//------------------------------------------------------------------------------
static uint8_t seq_determineNextPattern()
{
	const PatternSetting * const p=&seq_patternSet.seq_patternSettings[seq_activePattern];
	if(seq_barCounter % (p->changeBar+1) == 0)
		return p->nextPattern;
	else
		return seq_activePattern;
}

static void seq_nextStep()
{

	if(!seq_running)
		return;

	seq_masterStepCnt++;

	//---- calc master step position. max value is 127. also take in regard the pattern length -----
	// track 0 determines the master step position
	uint8_t masterStepPos;
	uint8_t seqlen;
	//if( (((seq_stepIndex[0]+1) &0x7f) == 0) ||
	//    (((seq_patternSet.seq_subStepPattern[seq_activePattern][0][seq_stepIndex[0]+1]).note & PATTERN_END_MASK)>=PATTERN_END_MASK) )

	seqlen=seq_patternSet.seq_patternLengthRotate[seq_activePattern][0].length;
	if(!seqlen)
		seqlen=16;

	if( (((seq_stepIndex[0]+1) & 0x7f) == 0) || ((seq_stepIndex[0]+1) / 8 == seqlen))
			//(((seq_patternSet.seq_subStepPattern[seq_activePattern][0][seq_stepIndex[0]+1]).note & PATTERN_END)) )
	{
		masterStepPos = 0;
		//a bar has passed
		seq_barCounter++;
	}
	else
	{
		masterStepPos = seq_stepIndex[0]+1;
	}

	//-------- check if the master track has ended and check if a pattern switch is necessary --------
	if(masterStepPos == 0)
	{
		if(seq_activePattern == seq_pendingPattern)
		{
			//check pattern settings if we have to auto change patterns
			seq_pendingPattern = seq_determineNextPattern();
			if(seq_pendingPattern >= SEQ_NEXT_RANDOM)
			{
				uint8_t limit = seq_pendingPattern - SEQ_NEXT_RANDOM +2;
				uint8_t rnd = GetRngValue() % limit;
				seq_pendingPattern = rnd;
			}
		}

		// a new pattern is about to start
		// set pendingPattern active
		if((seq_activePattern != seq_pendingPattern) || seq_loadPendigFlag)
		{
			//--AS if this setting is active and the user has manually changed patterns,
			// reset the bar counter. uncommenting the below will cause it to only reset
			// when a manual pattern change is invoked. to me the whole auto pattern change modulo stuff
			// above is a bit broken.
			if(/*seq_loadPendigFlag &&*/ seq_resetBarOnPatternChange)
				seq_barCounter=0;
			// --AS TODO we need to also reset barCounter to 0 when the end of a repetition set of a pattern plays
			// EVEN IF the pattern is set to play itself again, this will facilitate having the bits that play
			// certain steps only on certain intervals of bar counter

			seq_loadPendigFlag = 0;
			//first check if 2 new pattern is available
			if(seq_newPatternAvailable)
			{
				seq_newPatternAvailable = 0;
				seq_activateTmpPattern();
			}

			seq_activePattern = seq_pendingPattern;

			//reset pattern position to pattern rotate starting position for the active pattern --AS **PATROT
			seq_setStepIndexToStart();

			/*
			 * Pattern changes are Sequencer timing events, but LED repaint is
			 * front-panel ownership. led_notifyPatternChanged() is a direct
			 * notification, not an opcode bridge: Sequencer tells ledHandler
			 * the new active pattern so the visible pattern LEDs can follow.
			 */
			led_notifyPatternChanged(seq_activePattern);

			// --AS send a pattern change message to midi/usb out
			seq_sendProgChg(seq_activePattern);


			// --AS all notes off here since we are switching patterns
			voiceControl_noteOff(0xFF);
		}
	}

	//---------- now check if the master track is at a full beat position to flash the start/stop button --------
	if((masterStepPos&31) == 0)
	{
		//&32 <=> %32
		//a quarter beat occured (multiple of 32 steps in the 128 step pattern)
		seq_ledState.beatPulse = 1;
		seq_ledState.dirty |= SEQ_LED_DIRTY_BEAT;
	}
	else if ((masterStepPos&31) == 1)
	{
		seq_ledState.beatPulse = 0;
		seq_ledState.dirty |= SEQ_LED_DIRTY_BEAT;
	}

	//--------- Time to process the single tracks -------------------------
	trigger_clockTick(seq_stepIndex[0]+1);

	int i;
	for(i=0;i<NUM_TRACKS;i++)
	{
		//increment the step index
		seq_stepIndex[i]++;
		//check if track end is reached

		// --AS **PATROT we now use this for length
		seqlen=seq_patternSet.seq_patternLengthRotate[seq_activePattern][i].length;
		if(!seqlen)
			seqlen=16;

		if((seq_stepIndex[i] / 8) == seqlen || (seq_stepIndex[i] & 0x7f) == 0)
		{
			//if end is reached reset track to step 0
			seq_stepIndex[i] = 0;
		}


		if(seq_SomModeActive)
		{
			som_tick(seq_stepIndex[0],seq_mutedTracks);

		} else {
			//if track is not muted
			if(!(seq_mutedTracks & (1<<i) ) )
			{
				//if main step (associated with current substep) is active
				if(seq_intIsMainStepActive(i,seq_stepIndex[i]/8,seq_activePattern)) {

					// --AS **RECORD if we are in erase mode (shift clear while record and playing)
					// and this is the active track on the front, we erase the note value
					// only do so if we are on a main step while erase is active. in this case, the main step and
					// all it's substeps are erased.
					if(seq_eraseActive && i==menu_getActiveVoice() && seq_stepIndex[i]%8==0) {
						/*
						 * Live erase targets the active front-panel voice only.
						 *
						 * Menu owns active voice selection; Sequencer owns the
						 * playback-time erase condition; PatternData receives
						 * the actual step mutation inside
						 * seq_eraseStepAndSubSteps().
						 */
						// erase the main step and all substeps
						seq_eraseStepAndSubSteps(menu_getActiveVoice(),seq_stepIndex[i]/8);
					} else
					// if sub-step is active
					if(seq_intIsStepActive(i,seq_stepIndex[i],seq_activePattern))
					{
						//PROBABILITY
						//every 8th step a new random value is generated
						//thus every sub step block has only one random value to compare against
						//allows randomisation of rolls by chance

						if((seq_stepIndex[i] & 0x07) == 0x00) //every 8th step
						{
							seq_rndValue[i] = GetRngValue()&0x7f;
						}

						if( (seq_rndValue[i]) <= seq_patternSet.seq_subStepPattern[seq_activePattern][i][seq_stepIndex[i]].prob )
						{
							const uint8_t vol = seq_patternSet.seq_subStepPattern[seq_activePattern][i][seq_stepIndex[i]].volume&STEP_VOLUME_MASK;
							const uint8_t note = seq_patternSet.seq_subStepPattern[seq_activePattern][i][seq_stepIndex[i]].note;
							seq_triggerVoice(i,vol,note);
						}
					} // if sub step is active
				} // if main step is active
			} // if this track is not muted
		}

		//---- check if the roll mode has to trigger the voice
		if(seq_rollRate!=0xff) //not in oneshot mode
		{
			if(seq_rollState & (1<<i))
			{
				//check if roll is active
				{
					if((seq_stepIndex[i]%seq_rollRate)==0)
					{
						const uint8_t vol = ROLL_VOLUME;

						const uint8_t note = seq_patternSet.seq_subStepPattern[seq_activePattern][i][seq_stepIndex[i]].note;
						seq_triggerVoice(i,vol,note);

						seq_addNote(i,vol, note); // --AS todo should this be note or should it be SEQ_DEFAULT_NOTE (before my change it would have been SEQ_DEFAULT_NOTE)
					}
				}
			}
		}//end oneshot

	}

	/*
	 * Chase LED state is produced by Sequencer timing and consumed later by
	 * led_processSeqLedState() in the foreground loop. This avoids doing LED
	 * work inside the playback step walk while preserving the old live chase
	 * display for the active Menu voice.
	 */
	seq_ledState.chaseStep = seq_stepIndex[menu_getActiveVoice()];
	seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;

	// --AS check mtc, which might stop the sequencer if we haven't seen one in a while
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

/*This is called from IRQ handler when an external clock tick is received
 * master steps are used to keep the sync with the external clocks
 * a master step is a step that is directly triggered by the external clock signal.
 * non master steps are derived from the internaly calculated phase accumulator.
 * spacing is defined by the prescaler value
 * - with 32ppq every step is a master step
 * - with 4ppq only every 8th step is a master step
 * We set the next step index to a value - 1 because seq_nextStep() will
 * increment the value itself
 */
void seq_triggerNextMasterStep(uint8_t stepSize)
{
	uint8_t i, sn, len;
	for(i=0;i<NUM_TRACKS;i++) {
		len = seq_patternSet.seq_patternLengthRotate[seq_activePattern][i].length;
		if(!len) // length of 0 means length of 16 (since we are using 4 bits)
			len=16;
		len *= 8; // need length in steps

		if(seq_lastMasterStep[i] == 0) // need to set it so next step will inc it to 0
			sn=len-1; // set to last step before wrap around, effectively 0
		else
			sn=seq_lastMasterStep[i]-1; // adjust for seq_nextStep

		// establish the next step for this track for the sequencer
		seq_stepIndex[i] = sn;

		// save the position where we will trigger on the next external clock tick.
		// wrap around if we would exceed our track length
		seq_lastMasterStep[i] += stepSize;
		if(seq_lastMasterStep[i] >= len)
			seq_lastMasterStep[i] -= len;

		//set time to next step to zero, forcing the sequencer to process the next step now
		seq_setDeltaT(-1);
	}
}
//------------------------------------------------------------------------------
void seq_resetDeltaAndTick()
{
	//if there are unplayed steps jump over them
	while(!seq_isNextStepSyncStep())
	{
		seq_nextStep();
	}

	//is shuffle delay necessary?

	if(seq_shuffle != 0)
	{
		//seq_deltaT = 0;^

			seq_deltaT = (1000.f * 60.f) / ((float)seq_tempo * SEQ_INTERNAL_PPQ);
			seq_deltaT *= SYSTICK_TICKS_PER_MS;

		seq_lastTick = systick_ticks;

		uint8_t stepInHalfBeat = seq_masterStepCnt&0xf;
		const float shuffleFactor = seq_shuffleTable[stepInHalfBeat] * seq_shuffle;
		const float originalDeltaT = seq_deltaT;

		seq_deltaT = shuffleFactor * originalDeltaT * 16.f;
		seq_lastShuffle = shuffleFactor;

		if(seq_deltaT <= 0)
		{
			seq_nextStep();
			seq_lastTick = systick_ticks;
			seq_calcDeltaT(seq_tempo);
		} else
		{
			seq_delayedSyncStepFlag =1;
		}

		seq_prescaleCounter = 0;

	}
	else
	{
		//play next sync step
		seq_nextStep();

		seq_lastTick = systick_ticks;
		seq_calcDeltaT(seq_tempo);

		seq_prescaleCounter = 0;
	}


}
//------------------------------------------------------------------------------
void seq_resetToPatternStart(void)
{
	/* External reset should reposition the sequence without toggling transport
	** state or sending MIDI stop/start. The next clock pulse will play the
	** pattern start according to each track's rotation. */
	seq_lastShuffle = 0;
	seq_barCounter = 0;
	seq_masterStepCnt = 0;
	seq_prescaleCounter = 0;
	seq_delayedSyncStepFlag = 0;
	seq_setStepIndexToStart();
	seq_deltaT = 0;
	seq_lastTick = systick_ticks;
}
//------------------------------------------------------------------------------
/** call periodically to check if the next step has to be processed */
void seq_tick()
{
	if(seq_deltaT == -1)
	{
		seq_deltaT = 32000;
		seq_nextStep();

		return;
	}
	if(systick_ticks-seq_lastTick >= seq_deltaT)
	{

		float rest = systick_ticks-seq_lastTick - seq_deltaT;
		seq_lastTick = systick_ticks;
		seq_calcDeltaT(seq_tempo);
		seq_deltaT = seq_deltaT - rest;

		if((seq_prescaleCounter%SEQ_PRESCALER_MASK) == 0)
		{
			//for external sync we have a ratio of 3/4 ppq/steps
			//so when the 3rd ppq is received we have to activate the 4th step etc
			//advance only 2 steps automatically, then wait for sync message

			if(seq_getExtSync()) {
				if(seq_isNextStepSyncStep()==0) {
					seq_delayedSyncStepFlag = 0;

					seq_nextStep();
				}
			} else {
				seq_nextStep();
			}
		}

		if(!seq_getExtSync()) //only send internal MIDI clock to output when external sync is off
		{
			if((seq_prescaleCounter%MIDI_PRESCALER_MASK) == 0)
			{
				seq_sendRealtime(MIDI_CLOCK);
			}
		}
		seq_prescaleCounter++;
		if(seq_prescaleCounter>=12)seq_prescaleCounter=0;
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
		seq_lastShuffle = 0;
		seq_barCounter = 0;
		seq_masterStepCnt = 0;
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
		seq_prescaleCounter = 0;
		seq_sendRealtime(MIDI_START);
		trigger_reset(1);
	}

	// set start points back to default (happens on start and stop. needs to happen on start
	// in case the user has entered a rotate value while stopped)
	seq_setStepIndexToStart();

}
//------------------------------------------------------------------------------
static uint8_t seq_intIsStepActive(uint8_t voice, uint8_t stepNr, uint8_t patternNr)
{
	/*
	 * Playback read wrapper around PatternData.
	 *
	 * Sequencer still asks whether a step is active while walking playback
	 * timing. PatternData owns the storage and validity checks. Keeping this
	 * wrapper local limits churn until the later Pattern refactor can move more
	 * playback-facing helpers behind Pattern APIs.
	 */
	return pat_isStepActive(voice, stepNr, patternNr);
}

//------------------------------------------------------------------------------
static uint8_t seq_intIsMainStepActive(uint8_t voice, uint8_t mainStepNr, uint8_t pattern)
{
	/*
	 * Playback read wrapper around PatternData main-step state.
	 *
	 * Inputs are Sequencer playback coordinates. Output is a boolean active
	 * flag from PatternData. This replaces direct parser queries with direct
	 * Pattern storage reads.
	 */
	return pat_isMainStepActive(voice, mainStepNr, pattern);
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
			seq_triggerVoice(voice,ROLL_VOLUME,SEQ_DEFAULT_NOTE);
			//record roll notes
			seq_addNote(voice,ROLL_VOLUME,SEQ_DEFAULT_NOTE);
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
static int8_t seq_quantize(int8_t step)
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
	int8_t itg = (int8_t)frac;
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
		if( seq_intIsMainStepActive(voice,quantizedStep/8,seq_activePattern) &&
				seq_intIsStepActive(voice,quantizedStep,seq_activePattern))
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
	 * main-step activation, while this function still writes Step fields through
	 * compatibility arrays until the later PatternData refactor moves the rest
	 * of step mutation behind pat_ APIs.
	 *
	 * Inputs: trackNr target track, vel recorded velocity, note recorded note.
	 * Outputs: step note/volume/probability/active bit are updated, the
	 * corresponding main step is activated, and visible record LEDs are marked
	 * dirty if the edited track/pattern is currently shown by Menu.
	 *
	 * Risk: the direct Step writes are intentional temporary compatibility with
	 * existing playback code. New UI code should prefer PatternData APIs.
	 */
	uint8_t targetPattern;
	Step *stepPtr;
	//only record notes when seq is running and recording
	if(seq_running && seq_recordActive)
	{
		const int8_t quantizedStep = seq_quantize(seq_stepIndex[trackNr]);


		// --AS **RECORD fix for recording across patterns
		if(quantizedStep==0 && seq_stepIndex[trackNr] > (NUM_STEPS/2)) {
			// this means that we hit a note in 2nd half of the bar and quantization pushed
			// the note to position 0 of the next bar.
			// need to see if there is about to be a pattern change so that the note
			// ends up on 0 of the next pattern
			targetPattern=seq_determineNextPattern();

		} else
			targetPattern=seq_activePattern;

		//special care must be taken when recording midi notes!
		//since per default the 1st substep of a mainstep cluster is always active
		//we will get double notes when a substep other than ss1 is recorded
		if(!seq_intIsMainStepActive(trackNr, quantizedStep/8, targetPattern))
		{
			//if the mainstep is not active, we clear the 1st substep
			//to prevent double notes while recording
			seq_patternSet.seq_subStepPattern[targetPattern][trackNr][(quantizedStep/8)*8].volume 	&= ~STEP_ACTIVE_MASK;
		}

		//set the current step in the requested track active
		stepPtr=&seq_patternSet.seq_subStepPattern[targetPattern][trackNr][quantizedStep];
		stepPtr->note 		= note;				// note (--AS was SEQ_DEFAULT_NOTE)
		stepPtr->volume		= vel;				// new velocity
		stepPtr->prob		= 127;				// 100% probability
		stepPtr->volume 	|= STEP_ACTIVE_MASK;

		//activate corresponding main step
		pat_setMainStep(targetPattern, trackNr, quantizedStep/8,1);

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
// --AS **RECORD erase a main step and all it's sub steps on the active pattern
// for the specified voice
static void seq_eraseStepAndSubSteps(const uint8_t voice, const uint8_t mainStep)
{
	/*
	 * Erases one main step and its eight sub-steps during live erase.
	 *
	 * Caller: seq_nextStep() when erase mode is active, playback reaches a main
	 * step, and the playing track is the active Menu voice. PatternData owns the
	 * main-step bit via pat_setMainStep(); this function still resets sub-step
	 * Step structs directly through PatternData compatibility arrays until the
	 * broader Pattern mutation refactor.
	 *
	 * Inputs: voice is the track to erase, mainStep is 0..15. Output: main step
	 * disabled, all eight sub-steps reset, first sub-step re-enabled to preserve
	 * the old invariant that each main-step cluster has an active first substep.
	 *
	 * Risk: direct Step writes must stay in sync with pat_resetStep() semantics.
	 * Moving this fully into PatternData is a good later cleanup target.
	 */
	uint8_t i;
	// turn off the main step
	pat_setMainStep(seq_activePattern, voice, mainStep,0);

	// turn off all substeps
	for(i=(uint8_t)(mainStep*8);i<(uint8_t)((mainStep+1)*8);i++) {
		seq_resetNote(&seq_patternSet.seq_subStepPattern[seq_activePattern][voice][i]);
	}

	// first substep needs to be made active
	seq_patternSet.seq_subStepPattern[seq_activePattern][voice][(uint8_t)(mainStep*8)].volume |= STEP_ACTIVE_MASK;

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

//------------------------------------------------------------------------------
// --AS reset a step to it's default state
static void seq_resetNote(Step *step)
{
	step->note 		= SEQ_DEFAULT_NOTE;
	step->param1Nr 	= NO_AUTOMATION;
	step->param1Val = 0;
	step->param2Nr	= NO_AUTOMATION;
	step->param2Val	= 0;
	step->prob		= 127;
	step->volume	= 100; // clears active bit as well
}
static uint8_t seq_isNextStepSyncStep()
{
	if(seq_delayedSyncStepFlag)
	{
		seq_delayedSyncStepFlag = 0;
		seq_prescaleCounter = 0;
		return 0;
	}
	if( ((seq_stepIndex[0] & 0x3) % 4) == 3) {
		return 1;
	}
	return 0;
}
//------------------------------------------------------------------------------

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
 *  A pattern rotation of 0 means start at the beginning of the pattern. max value is 15.
 *  Each value represents a main step interval (which contains 8 substeps)
 *
 *  This is called when the sequencer starts/stops running, also when a pattern change takes place
 */
static void seq_setStepIndexToStart()
{
	uint8_t len, rot, i;
	for(i=0;i<NUM_TRACKS;i++) {
		// adjust rot in case the pattern length is less than the rotated amount
		// len is 0-15 where a value of 0 means 16
		rot=seq_patternSet.seq_patternLengthRotate[seq_activePattern][i].rotate;
		len=seq_patternSet.seq_patternLengthRotate[seq_activePattern][i].length;
		if(len && (rot > len))
			rot = rot % len;

		// this is for external clock sync via trigger expansion kit (the ext tick will adjust this -1)
		seq_lastMasterStep[i] = (8 * rot);

		// -1 here because we increment it first thing when we start
		seq_stepIndex[i] = ( 8 * rot) - 1;

	}

}
