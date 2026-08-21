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
/*
 * Realign playback to a Scene selection that another owner has already made.
 *
 * Why this exists separately from seq_selectActivePattern(): Pattern is the one
 * Scene-owned payload that playback and the STEP UI address through Sequencer/
 * Menu state (seq_activePattern / menu_shownPattern) rather than through
 * scene_getActiveIndex(). Both are BSS-zero at boot and, before this API
 * existed, were assigned only by menu_perfModeSceneButtonPressed(). Bank Load
 * commits a new active Scene without any front-panel press, so the two indices
 * silently disagreed with SceneData: a subsequent Scene Load wrote its Pattern
 * into the committed active Scene while the sequencer kept reading Scene 0.
 * That produced the "Scene Load never loads the pattern" defect, with every
 * other Scene payload appearing correct because they all resolve through
 * scene_getActiveIndex(). See SCENE_LOAD_PAT_RESTORE.md for the full analysis.
 *
 * Inputs: a resident Scene/Pattern index, validated against PatternData exactly
 * as seq_selectActivePattern() validates its own. Outputs: seq_activePattern
 * and seq_pendingPattern are aligned to that Scene, deferred pattern-load flags
 * are cleared, and fixed-grid track cursors are recalculated from the existing
 * master clock.
 *
 * Deliberately NOT done here, and this is the whole reason the narrower entry
 * point exists: no led_notifyPatternChanged(), no seq_sendProgChg() — which
 * would emit a MIDI program change on the wire — and no voiceControl_noteOff().
 * This is a state restore for a selection somebody else owns, not a performance
 * action, and it must be safe to call during pre-audio boot Bank Load where
 * emitting MIDI or forcing note-offs would be an unwanted side effect.
 *
 * Clients: filesystem.c's Bank Load active-Scene metadata commit. Front-panel
 * PERF switching must keep using seq_selectActivePattern() so that its LED,
 * program-change, and note-off behaviour is unchanged.
 */
void seq_alignActivePatternToScene(uint8_t scene_index);
void seq_setRunning(uint8_t isRunning);
uint8_t seq_isRunning(void);
void seq_armActivePatternReload(void);
void seq_setMute(uint8_t trackNr, uint8_t isMuted);
uint8_t seq_isTrackMuted(uint8_t trackNr);
void seq_setRoll(uint8_t voice, uint8_t onOff);
void seq_setRollRate(uint8_t rate);
/*
 * Record a live MIDI/roll event as one quantized fixed-grid trigger bit.
 * Input is the track; output is an on-bit only when recording is active.
 * Note and velocity are intentionally absent because PatternSet stores neither.
 */
void seq_recordTrigger(uint8_t trackNr);
void seq_setRecordingMode(uint8_t active);
void seq_setErasingMode(uint8_t active);
void seq_midiNoteOff(uint8_t chan);
void seq_sendMidiNoteOn(const uint8_t channel, const uint8_t note, const uint8_t veloc);

#endif /* SEQUENCER_H_ */
