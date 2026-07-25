/*
 * sequencer.h
 *
 * Timing/playback scheduler API.
 */

#ifndef SEQUENCER_H_
#define SEQUENCER_H_

#include "stm32f4xx.h"
#include "globals.h"
#include "PatternData.h"

#define ROLL_VOLUME 100

enum Seq_QuantisationEnum
{
	NO_QUANTISATION,
	QUANT_8,
	QUANT_16,
	QUANT_32,
	QUANT_64,
};

extern uint8_t seq_activePattern;
extern uint8_t seq_newPatternAvailable;
extern uint8_t seq_resetBarOnPatternChange;

void seq_triggerVoice(uint8_t voiceNr, uint8_t vol, uint8_t note);
/*
 * Stopped-transport voice preview.
 *
 * Why: front-panel VOICE re-press audition should use Sequencer's existing
 * voice/MIDI/trigger ownership without advancing pattern state. Input voiceNr
 * is the UI track/voice index. Output is one synth/MIDI preview trigger when
 * valid. Clients: buttonHandler voice button path. Risk: this must not mutate
 * seq_stepIndex[], recording state, automation lanes, or PatternData steps.
 */
void seq_previewVoice(uint8_t voiceNr);
void seq_init(void);
void seq_tick(void);
void seq_resetDeltaAndTick(void);
void seq_resetToPatternStart(void);
void seq_realignActivePatternToMasterClock(void);
void seq_setDeltaT(float delta);
void seq_triggerNextMasterStep(uint8_t stepSize);
void seq_setBpm(uint16_t bpm);
uint16_t seq_getBpm(void);
void seq_sync(void);
uint8_t seq_getExtSync(void);
void seq_setQuantisation(uint8_t value);
void seq_setExtSync(uint8_t isExt);

enum SeqExternalSyncSource {
	SEQ_EXT_SYNC_OFF = 0,
	SEQ_EXT_SYNC_USB,
	SEQ_EXT_SYNC_DIN,
	SEQ_EXT_SYNC_PULSE,
	SEQ_EXT_SYNC_AUTO,
};

void seq_setExtSyncSource(uint8_t source);
uint8_t seq_getExtSyncSource(void);
void seq_noteExtSyncActivity(uint8_t source, uint32_t timestampUs);
void seq_setNextPattern(const uint8_t patNr);
/*
 * Immediately select the active Scene/Pattern for front-panel PERF switching.
 *
 * Inputs: pattern is a resident Scene/Pattern index validated against
 * PatternData. Outputs: seq_activePattern and seq_pendingPattern are aligned,
 * deferred pattern-load flags are cleared, track counters are recalculated from
 * the existing master tick/step position, UI follow/chase state is notified, and
 * any sounding notes are released. Clients: menu_perfModeSceneButtonPressed().
 * Pattern-only queued switching is retired; callers that need a musical switch
 * must use this Scene-aligned path or a future Scene-level scheduler.
 */
void seq_selectActivePattern(uint8_t pattern);
void seq_setRunning(uint8_t isRunning);
uint8_t seq_isRunning(void);
void seq_armActivePatternReload(void);
void seq_setMute(uint8_t trackNr, uint8_t isMuted);
uint8_t seq_isTrackMuted(uint8_t trackNr);
void seq_setRoll(uint8_t voice, uint8_t onOff);
void seq_setRollRate(uint8_t rate);
void seq_addNote(uint8_t trackNr,uint8_t vel, uint8_t note);
void seq_setRecordingMode(uint8_t active);
void seq_setErasingMode(uint8_t active);
void seq_recordAutomation(uint8_t voice, uint8_t dest, uint8_t value);
void seq_midiNoteOff(uint8_t chan);
void seq_sendMidiNoteOn(const uint8_t channel, const uint8_t note, const uint8_t veloc);

/*
 * Runtime hook used by PatternData.
 * Why: PatternData owns rotation storage, but sequencer.c owns seq_stepIndex[].
 * Inputs: track, previous/new rotation, and effective track length. Output:
 * adjusted live step index when the sequencer is running. Risk: this must stay
 * a narrow scheduler hook; UI code should call pat_setTrackRotation() instead.
 */
void seq_offsetTrackStepIndexForRotation(uint8_t trackNr, uint8_t oldRot,
                                         uint8_t newRot, uint8_t len);

#endif /* SEQUENCER_H_ */
