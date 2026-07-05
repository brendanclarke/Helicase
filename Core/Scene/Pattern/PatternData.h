/*
 * PatternData.h
 *
 * Scene/Pattern-owned storage and edit API.
 */

#ifndef PATTERNDATA_H_
#define PATTERNDATA_H_

#include <stdint.h>
#include "globals.h"

 /**<
  * we have 6 voices
  * 3 drums
  * 1 snare/claps
  * 1 cymbal/snare
  * 1 hiHat
  * track 7 is the open hh... it triggers the highhat voice but with longer decay. it chokes the closed hihat*/
#define NUM_TRACKS 7
#define NUM_PATTERN 8
#define NUM_STEPS 128

#define SEQ_DEFAULT_NOTE 63

#define STEP_ACTIVE_MASK 0x80
#define STEP_VOLUME_MASK 0x7f

#define SEQ_NEXT_RANDOM 		0x08
#define SEQ_NEXT_RANDOM_PREV 	0x09

typedef struct StepStruct
{
	uint8_t 	volume;		// 0-127 volume -> 0x7f => lower 7 bit, upper bit => active
	uint8_t  	prob;		//step probability (--AS todo we have one free bit here)
	uint8_t		note;		//midi note value 0-127 -> 0x7f, --AS todo upper bit is now free for other usages

	//parameter automation
	uint8_t 	param1Nr;
	uint8_t 	param1Val;

	uint8_t 	param2Nr;
	uint8_t 	param2Val;

}Step;

typedef struct PatternSettingsStruct
{
	uint8_t 	changeBar;		// change on every Nth bar to the next pattern
	uint8_t  	nextPattern;	// [0:9] (0-7) are the 8 patterns, (8) is random previous, (9) is random all
}PatternSetting;

typedef union {
	uint8_t value;
	struct {
		unsigned length:4;	// length (0 = default 16 steps)
		unsigned rotate:4;	// 0 means not rotated, 15 is max
	};
} LengthRotate;

typedef struct PatternSetStruct
{
	Step seq_subStepPattern[NUM_PATTERN][NUM_TRACKS][NUM_STEPS];
	uint16_t seq_mainSteps[NUM_PATTERN][NUM_TRACKS];
	PatternSetting seq_patternSettings[NUM_PATTERN];
	LengthRotate seq_patternLengthRotate[NUM_PATTERN][NUM_TRACKS];
}PatternSet;

typedef struct TempPatternStruct
{
	Step seq_subStepPattern[NUM_TRACKS][NUM_STEPS];
	uint16_t seq_mainSteps[NUM_TRACKS];
	PatternSetting seq_patternSettings;
	LengthRotate seq_patternLengthRotate[NUM_TRACKS]; // only used for length
}TempPattern;

extern PatternSet pat_patternSet;
extern TempPattern pat_tmpPattern;

/*
 * Transition macros:
 * Why: filesystem/sequencer playback still has deliberate direct layout reads
 * while this session removes the front-panel parser. Inputs/outputs: old global
 * names compile to the new PatternData-owned globals. Risk: hides remaining
 * direct storage users, so new code should call pat_* APIs and the audit grep
 * should continue shrinking these references in later Pattern refactors.
 */
#define seq_patternSet pat_patternSet
#define seq_tmpPattern pat_tmpPattern

/*
 * Validation helpers.
 * Why: direct callers need bounded checks before touching pattern data.
 * Inputs: raw pattern/track/step indices. Output: 1 when valid, 0 otherwise.
 * Risk: callers that ignore a 0 return must not dereference pat_*Ptr results.
 */
uint8_t pat_trackValid(uint8_t track);
uint8_t pat_patternValid(uint8_t pattern);
uint8_t pat_stepValid(uint8_t step);

/*
 * Pointer accessors.
 * Why: playback/filesystem need bounded access to the existing binary layout.
 * Inputs: pattern/track/step indices; PATTERNDATA_STAGING_PATTERN selects the
 * temporary load buffer for filesystem staging. Output: pointer or 0 on invalid.
 * Risk: returned pointers are live mutable storage; use only in owner-level code.
 */
#define PATTERNDATA_STAGING_PATTERN 0xFFu
Step *pat_stepPtr(uint8_t pattern, uint8_t track, uint8_t step);
uint16_t *pat_mainStepsPtr(uint8_t pattern, uint8_t track);
PatternSetting *pat_patternSettingPtr(uint8_t pattern);
LengthRotate *pat_lengthRotatePtr(uint8_t pattern, uint8_t track);

/*
 * Pattern lifecycle and edit API.
 * Why: pattern/track/step mutations must no longer route through opcodes.
 * Inputs are current pattern model indices/values. Outputs are direct storage
 * changes plus menu parameter refresh where named apply helpers say so.
 * Risk: these are foreground/UI-facing calls except playback reads; do not call
 * menu-apply helpers from ISR context.
 */
void pat_init(void);
uint8_t pat_isStepActive(uint8_t track, uint8_t step, uint8_t pattern);
uint8_t pat_isMainStepActive(uint8_t track, uint8_t mainStep, uint8_t pattern);
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
void pat_setTrackRotation(uint8_t pattern, uint8_t track, uint8_t rotation);
uint8_t pat_getTrackRotation(uint8_t pattern, uint8_t track);
void pat_setShuffle(uint8_t pattern, uint8_t value);
uint8_t pat_getShuffle(uint8_t pattern);

void pat_clearTrack(uint8_t pattern, uint8_t track);
void pat_clearPattern(uint8_t pattern);
void pat_clearAutomation(uint8_t pattern, uint8_t track, uint8_t automTrack);
void pat_copyTrack(uint8_t pattern, uint8_t srcTrack, uint8_t dstTrack);
void pat_copyPattern(uint8_t srcPattern, uint8_t dstPattern);

void pat_setSelectedStep(uint8_t step);
void pat_setActiveAutomationTrack(uint8_t track);
uint8_t pat_getActiveAutomationTrack(void);
void pat_armAutomationStep(uint8_t step, uint8_t track, uint8_t armed);
void pat_recordAutomation(uint8_t pattern, uint8_t track, uint8_t step,
                          uint8_t dest, uint8_t value);
void pat_recordArmedAutomation(uint8_t pattern, uint8_t dest, uint8_t value);

void pat_applyStepToMenu(uint8_t pattern, uint8_t track, uint8_t step);
void pat_applyPatternSettingsToMenu(uint8_t pattern);
void pat_applyTrackSettingsToMenu(uint8_t pattern, uint8_t track);

#endif
