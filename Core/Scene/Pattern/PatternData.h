/*
 * PatternData.h
 *
 * Scene/Pattern-owned storage and edit API.
 */

#ifndef PATTERNDATA_H_
#define PATTERNDATA_H_

#include <stdint.h>
#include "globals.h"
#include "InstrumentManager.h"

 /**<
  * we have 6 voices
  * 3 drums
  * 1 snare/claps
  * 1 cymbal/snare
  * 1 hiHat
  * track 7 is the open hh... it triggers the highhat voice but with longer decay. it chokes the closed hihat*/
#define NUM_TRACKS 7
#define NUM_STEPS 128
#define NUM_BARS 8u
#define NUM_STEPS_PER_BAR 16u
#define PAT_DEFAULT_TRACK_LENGTH NUM_STEPS_PER_BAR

#define PAT_DEFAULT_NOTE 63

#define STEP_ACTIVE_MASK 0x80
#define STEP_VOLUME_MASK 0x7f

#define PAT_NEXT_RANDOM 		0x08
#define PAT_NEXT_RANDOM_PREV 	0x09

#define TRACK_SCALE_DIV8   0u
#define TRACK_SCALE_DIV7   1u
#define TRACK_SCALE_DIV6   2u
#define TRACK_SCALE_DIV5   3u
#define TRACK_SCALE_DIV4   4u
#define TRACK_SCALE_DIV3   5u
#define TRACK_SCALE_DIV25  6u
#define TRACK_SCALE_DIV2   7u
#define TRACK_SCALE_DIV_D6 8u
#define TRACK_SCALE_DIV_D3 9u
#define TRACK_SCALE_OFF    10u
#define TRACK_SCALE_MUL_D3 11u
#define TRACK_SCALE_MUL_D6 12u
#define TRACK_SCALE_MUL2   13u
#define TRACK_SCALE_MUL25  14u
#define TRACK_SCALE_MUL3   15u
#define TRACK_SCALE_MUL4   16u
#define TRACK_SCALE_MUL5   17u
#define TRACK_SCALE_MUL6   18u
#define TRACK_SCALE_MUL7   19u
#define TRACK_SCALE_MUL8   20u
#define TRACK_SCALE_COUNT  21u

typedef struct {
	uint8_t num;
	uint8_t den;
} TrackScaleRatio;

typedef struct StepStruct
{
	uint8_t 	volume;		// 0-127 volume -> 0x7f => lower 7 bit, upper bit => active
	uint8_t  	prob;		//step probability (--AS todo we have one free bit here)
	uint8_t		note;		//midi note value 0-127 -> 0x7f, --AS todo upper bit is now free for other usages

	//parameter automation
	instrument_param_id_t param1Nr;
	uint8_t 	param1Val;

	instrument_param_id_t param2Nr;
	uint8_t 	param2Val;

}Step;

typedef struct PatternSettingsStruct
{
	uint8_t 	changeBar;		// change on every Nth bar to the next pattern
	uint8_t  	nextPattern;	// [0:9] (0-7) are the 8 patterns, (8) is random previous, (9) is random all
}PatternSetting;

typedef struct {
	/*
	 * Per-track Pattern settings shown by the STEP front page.
	 *
	 * Why: the bridge keeps one PatternData-owned record for track-level
	 * Pattern settings even though the historical type name only mentions
	 * length/rotation. Inputs/outputs are owned through pat_* accessors below.
	 * Clients include Menu for editing/display, Sequencer for timing/playback,
	 * and filesystem for pattern/all/performance serialization. Risk: this type
	 * is file-format-visible; append fields only with matching loader defaults.
	 */
	uint8_t length;	// real track length in steps, 1..128
	uint8_t rotate;	// step rotation, 0 means not rotated
	uint8_t scale;	// track timing scale, TRACK_SCALE_* value
	uint8_t shuffle; // per-track shuffle amount, 0..127
} LengthRotate;

typedef struct PatternSetStruct
{
	Step pat_subStepPattern[NUM_TRACKS][NUM_STEPS];
	uint16_t pat_mainSteps[NUM_TRACKS];
	PatternSetting pat_patternSettings;
	LengthRotate pat_patternLengthRotate[NUM_TRACKS];
}PatternSet;

/*
 * PatternData storage API contract:
 * Why: sequencer.c no longer owns pattern arrays, but playback, filesystem, UI,
 * and generator code still need narrow access to the legacy 8-pattern layout.
 * The first coordinate used by the functions below is now a Scene index, even
 * where its C type remains uint8_t. Inputs/outputs use Scene/track/step indices
 * and either return bounded values, live owner-level pointers, or mutate stored
 * PatternData records. Callers/clients include sequencer playback and recording,
 * filesystem serialization, button/menu edit paths, ledHandler display refresh,
 * and Pattern generator modules. Risk: these types are file-format-visible, so
 * field order and sizes must not change without auditing pattern/all/performance
 * serializers.
 */

/*
 * Validation helpers.
 * Why: direct callers need bounded checks before touching pattern data.
 * Inputs: raw pattern/track/step indices. Output: 1 when valid, 0 otherwise.
 * Risk: callers that ignore a 0 return must not dereference pat_*Ptr results.
 */
uint8_t pat_trackValid(uint8_t track);
uint8_t pat_patternValid(uint8_t scene_index);
uint8_t pat_stepValid(uint8_t step);

/*
 * Pointer accessors.
 * Why: playback/filesystem need bounded access to the existing binary layout.
 * Inputs: Scene/track/step indices. Output: pointer or 0 on invalid.
 * Risk: returned pointers are live mutable storage; use only in owner-level code.
 */
Step *pat_stepPtr(uint8_t scene_index, uint8_t track, uint8_t step);
uint16_t *pat_mainStepsPtr(uint8_t scene_index, uint8_t track);
PatternSetting *pat_patternSettingPtr(uint8_t scene_index);
LengthRotate *pat_lengthRotatePtr(uint8_t scene_index, uint8_t track);

/*
 * Pattern lifecycle and edit API.
 * Why: pattern/track/step mutations must no longer route through opcodes.
 * Inputs are current pattern model indices/values. Outputs are direct storage
 * changes plus menu parameter refresh where named apply helpers say so.
 * Risk: these are foreground/UI-facing calls except playback reads; do not call
 * menu-apply helpers from ISR context.
 */
void pat_init(void);
void pat_initScene(uint8_t scene_index);

uint8_t pat_isStepActive(uint8_t track, uint8_t step, uint8_t pattern);
uint8_t pat_isMainStepActive(uint8_t track, uint8_t mainStep, uint8_t pattern);
/*
 * Playback-safe step readers.
 * Why: sequencer timing code needs probability, note, velocity, and automation
 * data without reaching into PatternData arrays directly. Inputs are
 * pattern/track/step coordinates. Outputs are bounded scalar values or a copied
 * Step record; invalid coordinates return safe defaults and pat_readStep()
 * returns 0. Callers/clients: sequencer playback, rolls, MIDI note echo, and
 * automation-node parsing. Risk: pat_readStep() copies the current Step
 * snapshot; callers must not expect later PatternData edits to update that copy.
 */
uint8_t pat_readStep(uint8_t pattern, uint8_t track, uint8_t step, Step *out);
uint8_t pat_getStepProbability(uint8_t pattern, uint8_t track, uint8_t step);
uint8_t pat_getStepNote(uint8_t pattern, uint8_t track, uint8_t step);
uint8_t pat_getStepVolume(uint8_t pattern, uint8_t track, uint8_t step);
void pat_setMainStep(uint8_t pattern, uint8_t track, uint8_t mainStep, uint8_t onOff);
void pat_setMainStepsRaw(uint8_t pattern, uint8_t track, uint16_t bits);
void pat_toggleStep(uint8_t track, uint8_t step, uint8_t pattern);
void pat_toggleMainStep(uint8_t track, uint8_t mainStep, uint8_t pattern);

void pat_setStepNote(uint8_t pattern, uint8_t track, uint8_t step, uint8_t note);
void pat_setStepVolume(uint8_t pattern, uint8_t track, uint8_t step, uint8_t volume);
void pat_setStepProbability(uint8_t pattern, uint8_t track, uint8_t step, uint8_t prob);
void pat_setStepAutomationDestination(uint8_t pattern, uint8_t track,
                                      uint8_t step, uint8_t slot,
                                      uint16_t targetParam);
void pat_setStepAutomationValue(uint8_t pattern, uint8_t track,
                                uint8_t step, uint8_t slot,
                                uint8_t value);

void pat_setPatternChangeBar(uint8_t pattern, uint8_t value);
void pat_setPatternNext(uint8_t pattern, uint8_t value);
uint8_t pat_getPatternChangeBar(uint8_t pattern);
uint8_t pat_getPatternNext(uint8_t pattern);

void pat_setTrackLength(uint8_t pattern, uint8_t track, uint8_t length);
uint8_t pat_getTrackLength(uint8_t pattern, uint8_t track);
/*
 * Effective track length reader.
 * Why: stored length may still receive 0 from older files, but sequencer
 * runtime code needs a nonzero length for wrap and external-clock math. Inputs
 * are pattern/track coordinates. Output is 1..128, with invalid coordinates
 * falling back to 128. Callers/clients: sequencer step walk, external-clock
 * master-step alignment, and start-position reset.
 */
uint8_t pat_getEffectiveTrackLength(uint8_t pattern, uint8_t track);
void pat_setTrackRotation(uint8_t pattern, uint8_t track, uint8_t rotation);
uint8_t pat_getTrackRotation(uint8_t pattern, uint8_t track);
void pat_setTrackScale(uint8_t pattern, uint8_t track, uint8_t scale);
uint8_t pat_getTrackScale(uint8_t pattern, uint8_t track);
TrackScaleRatio pat_getTrackScaleRatio(uint8_t pattern, uint8_t track);
/*
 * Per-track shuffle accessors.
 *
 * Why: shuffle is now Pattern timing data per track, not a global sequencer
 * coefficient. Inputs are pattern/track coordinates and a 0..127 menu value.
 * Outputs update/read PatternData storage; setters also mirror PAR_SHUFFLE for
 * the currently visible menu value. Clients: Menu edits, filesystem legacy
 * import/extension load, and Sequencer due-event timing.
 */
void pat_setTrackShuffle(uint8_t pattern, uint8_t track, uint8_t shuffle);
uint8_t pat_getTrackShuffle(uint8_t pattern, uint8_t track);
void pat_clearTrack(uint8_t pattern, uint8_t track);
void pat_clearPattern(uint8_t pattern);
void pat_clearAutomation(uint8_t pattern, uint8_t track, uint8_t automTrack);
/*
 * Recording and live-erase mutation helpers.
 * Why: sequencer.c owns recording/erase timing and quantization, but Step
 * writes belong in PatternData. Inputs identify the destination pattern/track
 * and bridge step plus note/velocity where needed. Outputs mutate Step
 * defaults, active bits, probability, note, velocity, and compatibility
 * main-step masks. Callers/clients: seq_addNote() and sequencer live erase.
 * Confederates: pat_setMainStep() for bridge mask mirroring, pat_resetStep(),
 * and LED dirty-state handling in sequencer.
 * Risk: all 128 Step records are live sequencer steps; old main-step helpers
 * exist only for compatibility until Scene storage replaces them.
 */
void pat_recordNote(uint8_t pattern, uint8_t track, uint8_t step,
                    uint8_t velocity, uint8_t note);
void pat_eraseMainStepSubSteps(uint8_t pattern, uint8_t track, uint8_t mainStep);
void pat_eraseStep(uint8_t pattern, uint8_t track, uint8_t step);
void pat_copyTrack(uint8_t pattern, uint8_t srcTrack, uint8_t dstTrack);
void pat_copyPattern(uint8_t srcPattern, uint8_t dstPattern);
void pat_copyBar(uint8_t pattern, uint8_t track, uint8_t srcBar, uint8_t dstBar);

void pat_setSelectedStep(uint8_t step);
void pat_setActiveAutomationTrack(uint8_t track);
uint8_t pat_getActiveAutomationTrack(void);
void pat_armAutomationStep(uint8_t step, uint8_t track, uint8_t armed);
void pat_recordAutomation(uint8_t pattern, uint8_t track, uint8_t step,
                          instrument_param_id_t dest, uint8_t value);
void pat_recordArmedAutomation(uint8_t pattern, instrument_param_id_t dest,
                               uint8_t value);

void pat_applyStepToMenu(uint8_t pattern, uint8_t track, uint8_t step);
void pat_applyPatternSettingsToMenu(uint8_t pattern);
void pat_applyTrackSettingsToMenu(uint8_t pattern, uint8_t track);

#endif
