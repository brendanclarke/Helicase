/*
 * PatternData.c
 *
 * Scene/Pattern-owned storage and edit API.
 */

#include "PatternData.h"
#include "MidiMessages.h"
#include "ParameterArray.h"
#include "menu.h"
#include "modulationNode.h"
#include "sequencer.h"
#include <string.h>

/* Primary pattern storage.
 *
 * Why it exists here:
 * - This session removes the old local protocol/parser layer.
 * - Pattern/track/step data is Scene/Pattern state, not sequencer transport
 *   state and not front-panel UI state.
 *
 * Who uses it:
 * - PatternData APIs below mutate/query it.
 * - sequencer.c still reads it during playback as a deliberate transition
 *   point until the later Pattern playback refactor.
 * - filesystem.c streams it through PatternData pointer accessors.
 *
 * Risk:
 * - The structure layout is file-format-visible. Do not reorder fields without
 *   auditing `.pat`, `.all`, and performance serializers.
 */
PatternSet pat_patternSet;

/* Temporary pattern load buffer.
 *
 * When a file load targets the pattern that is currently playing, filesystem.c
 * writes into this buffer first. sequencer.c then swaps it into pat_patternSet
 * at a pattern boundary. This preserves the old safe-load behavior while
 * moving the storage owner into PatternData.
 */
TempPattern pat_tmpPattern;

/* Held-step automation edit target.
 *
 * buttonHandler.c sets these when a step button is held long enough to arm
 * automation. Preset/MIDI parameter writes then call pat_recordArmedAutomation()
 * to write the destination/value into that held step. -1 means no armed step.
 */
static int8_t pat_armedAutomationStep = -1;
static int8_t pat_armedAutomationTrack = -1;

/* Which automation lane receives edits/recording: 0 = param1, nonzero = param2.
 * This used to be set via a sequencer opcode; now menu.c writes it directly. */
static uint8_t pat_activeAutomationTrack = 0;

/* Temporary per-pattern shuffle API backing.
 *
 * The value is still forwarded to seq_setShuffle() because playback consumes a
 * global shuffle coefficient today. The API is Pattern-owned now so later work
 * can make shuffle per-pattern or per-track-pattern without changing callers. */
static uint8_t pat_shuffleValue[NUM_PATTERN];

static void pat_resetStep(Step *step)
{
	/* Reset one sub-step to the original default state used by clear-pattern
	 * and clear-track operations. Input/output is a live Step pointer. A null
	 * guard keeps callers simple when future pointer helpers return 0. */
	if (!step)
		return;
	step->note 		= SEQ_DEFAULT_NOTE;
	step->param1Nr 	= NO_AUTOMATION;
	step->param1Val = 0;
	step->param2Nr	= NO_AUTOMATION;
	step->param2Val	= 0;
	step->prob		= 127;
	step->volume	= 100;
}

uint8_t pat_trackValid(uint8_t track)
{
	return (uint8_t)(track < NUM_TRACKS);
}

uint8_t pat_patternValid(uint8_t pattern)
{
	return (uint8_t)(pattern < NUM_PATTERN);
}

uint8_t pat_stepValid(uint8_t step)
{
	return (uint8_t)(step < NUM_STEPS);
}

Step *pat_stepPtr(uint8_t pattern, uint8_t track, uint8_t step)
{
	/* Central bounded access to a Step.
	 *
	 * pattern == PATTERNDATA_STAGING_PATTERN intentionally redirects to
	 * pat_tmpPattern for filesystem staging. All other patterns must be
	 * 0..NUM_PATTERN-1. Returning 0 forces callers to handle invalid indices
	 * without guessing the storage layout. */
	if (!pat_trackValid(track) || !pat_stepValid(step))
		return 0;
	if (pattern == PATTERNDATA_STAGING_PATTERN)
		return &pat_tmpPattern.seq_subStepPattern[track][step];
	if (!pat_patternValid(pattern))
		return 0;
	return &pat_patternSet.seq_subStepPattern[pattern][track][step];
}

uint16_t *pat_mainStepsPtr(uint8_t pattern, uint8_t track)
{
	/* Same staging-aware pointer policy as pat_stepPtr(), but for the 16-bit
	 * main-step mask that backs STEP1..16 display and playback gating. */
	if (!pat_trackValid(track))
		return 0;
	if (pattern == PATTERNDATA_STAGING_PATTERN)
		return &pat_tmpPattern.seq_mainSteps[track];
	if (!pat_patternValid(pattern))
		return 0;
	return &pat_patternSet.seq_mainSteps[pattern][track];
}

PatternSetting *pat_patternSettingPtr(uint8_t pattern)
{
	/* Pattern settings are per-pattern, except the temporary filesystem staging
	 * buffer has a single PatternSetting because it only stages one pattern at
	 * a time. */
	if (pattern == PATTERNDATA_STAGING_PATTERN)
		return &pat_tmpPattern.seq_patternSettings;
	if (!pat_patternValid(pattern))
		return 0;
	return &pat_patternSet.seq_patternSettings[pattern];
}

LengthRotate *pat_lengthRotatePtr(uint8_t pattern, uint8_t track)
{
	/* Length/rotation is per pattern+track. Length uses the legacy encoding:
	 * 0 means 16 main steps, 1..15 mean literal shortened lengths. */
	if (!pat_trackValid(track))
		return 0;
	if (pattern == PATTERNDATA_STAGING_PATTERN)
		return &pat_tmpPattern.seq_patternLengthRotate[track];
	if (!pat_patternValid(pattern))
		return 0;
	return &pat_patternSet.seq_patternLengthRotate[pattern][track];
}

void pat_init(void)
{
	uint8_t i;

	/* Shuffle has a PatternData-facing API now. Initialize all pattern slots to
	 * zero even though playback still reads the global sequencer coefficient. */
	memset(pat_shuffleValue, 0, sizeof(pat_shuffleValue));

	for (i = 0; i < NUM_PATTERN; i++) {
		/* Default pattern-change behavior: play once, then stay on itself. */
		pat_patternSet.seq_patternSettings[i].changeBar = 0;
		pat_patternSet.seq_patternSettings[i].nextPattern = i;
		/* pat_clearPattern() initializes steps, main-step mask, and
		 * length/rotation for every track in this pattern. */
		pat_clearPattern(i);
	}
}

uint8_t pat_isStepActive(uint8_t track, uint8_t step, uint8_t pattern)
{
	Step *s = pat_stepPtr(pattern, track, step);
	if (!s)
		return 0;
	return (uint8_t)((s->volume & STEP_ACTIVE_MASK) > 0);
}

uint8_t pat_isMainStepActive(uint8_t track, uint8_t mainStep, uint8_t pattern)
{
	uint16_t *mainSteps;
	if (mainStep >= 16u)
		return 0;
	mainSteps = pat_mainStepsPtr(pattern, track);
	if (!mainSteps)
		return 0;
	return (uint8_t)((*mainSteps & (uint16_t)(1u << mainStep)) > 0);
}

void pat_setMainStep(uint8_t pattern, uint8_t track, uint8_t mainStep, uint8_t onOff)
{
	/* Direct main-step setter used by sequencer live recording/erasing and by
	 * PatternData operations. Inputs are the target pattern/track/main-step and
	 * a boolean onOff. Output is only the main-step bit; sub-step data is not
	 * created or cleared here. */
	uint16_t *mainSteps;
	if (mainStep >= 16u)
		return;
	mainSteps = pat_mainStepsPtr(pattern, track);
	if (!mainSteps)
		return;
	if (onOff)
		*mainSteps |= (uint16_t)(1u << mainStep);
	else
		*mainSteps &= (uint16_t)~(uint16_t)(1u << mainStep);
}

void pat_setMainStepsRaw(uint8_t pattern, uint8_t track, uint16_t bits)
{
	/* Raw writer used by Euklid generation and file loading. It writes the
	 * whole 16-bit main-step mask in one shot because the generator/file already
	 * owns the complete mask. */
	uint16_t *mainSteps = pat_mainStepsPtr(pattern, track);
	if (!mainSteps)
		return;
	*mainSteps = bits;
}

void pat_toggleStep(uint8_t track, uint8_t step, uint8_t pattern)
{
	/* Toggle the active bit of one sub-step while preserving the stored velocity
	 * in the lower seven bits. Used by buttonHandler shift+select. */
	Step *s = pat_stepPtr(pattern, track, step);
	if (!s)
		return;
	if ((s->volume & STEP_ACTIVE_MASK) == 0)
		s->volume |= STEP_ACTIVE_MASK;
	else
		s->volume &= (uint8_t)~STEP_ACTIVE_MASK;
}

void pat_toggleMainStep(uint8_t track, uint8_t mainStep, uint8_t pattern)
{
	/* Toggle one STEP1..16 main-step bit. The LED refresh is done by callers in
	 * ledHandler because PatternData does not own physical presentation. */
	uint16_t *mainSteps;
	if (mainStep >= 16u)
		return;
	mainSteps = pat_mainStepsPtr(pattern, track);
	if (!mainSteps)
		return;
	*mainSteps ^= (uint16_t)(1u << mainStep);
}

void pat_setStepNote(uint8_t pattern, uint8_t track, uint8_t step, uint8_t note)
{
	/* Menu edit path for PAR_STEP_NOTE. Writes storage and mirrors the edited
	 * value into parameter_values so the display remains coherent. */
	Step *s = pat_stepPtr(pattern, track, step);
	if (!s)
		return;
	s->note = note;
	parameter_values[PAR_STEP_NOTE] = note;
}

void pat_setStepVolume(uint8_t pattern, uint8_t track, uint8_t step, uint8_t volume)
{
	/* Menu edit path for PAR_STEP_VOLUME. Only the lower seven velocity bits are
	 * replaced; the high active bit must survive a volume edit. */
	Step *s = pat_stepPtr(pattern, track, step);
	if (!s)
		return;
	s->volume &= (uint8_t)~STEP_VOLUME_MASK;
	s->volume |= (uint8_t)(volume & STEP_VOLUME_MASK);
	parameter_values[PAR_STEP_VOLUME] = (uint8_t)(volume & STEP_VOLUME_MASK);
}

void pat_setStepProbability(uint8_t pattern, uint8_t track, uint8_t step, uint8_t prob)
{
	/* Menu edit path for PAR_STEP_PROB. Probability is stored per sub-step and
	 * read by sequencer playback before triggering a voice. */
	Step *s = pat_stepPtr(pattern, track, step);
	if (!s)
		return;
	s->prob = prob;
	parameter_values[PAR_STEP_PROB] = prob;
}

void pat_setStepAutomationDestination(uint8_t pattern, uint8_t track,
                                      uint8_t step, uint8_t slot,
                                      uint16_t targetParam)
{
	/* Menu edit path for PAR_P1_DEST/PAR_P2_DEST.
	 *
	 * targetParam comes from modTargets[].param. The Step struct stores the old
	 * automation destination encoding: destinations below 128 are stored as
	 * CC-number-style param+1 so midiParser_ccHandler can later interpret them
	 * consistently. Slot 0 writes param1Nr; slot 1 writes param2Nr.
	 */
	Step *s = pat_stepPtr(pattern, track, step);
	uint8_t packed = (uint8_t)(targetParam & 0xFFu);

	if (!s)
		return;
	if ((packed != 0u) && (packed < 128u))
		packed++;

	if (slot == 0)
		s->param1Nr = packed;
	else
		s->param2Nr = packed;
}

void pat_setStepAutomationValue(uint8_t pattern, uint8_t track,
                                uint8_t step, uint8_t slot,
                                uint8_t value)
{
	/* Menu edit path for PAR_P1_VAL/PAR_P2_VAL. The destination is handled by
	 * pat_setStepAutomationDestination(); this only writes the automation value
	 * for the requested slot. */
	Step *s = pat_stepPtr(pattern, track, step);
	if (!s)
		return;
	if (slot == 0)
		s->param1Val = value;
	else
		s->param2Val = value;
}

void pat_setPatternChangeBar(uint8_t pattern, uint8_t value)
{
	/*
	 * Sets the viewed pattern's change-bar rule.
	 *
	 * Caller: menu_parseGlobalParam(PAR_PATTERN_BEAT). This replaces the old
	 * pattern-settings parser opcode. PatternData owns the saved value because
	 * it is serialized with pattern files and read by Sequencer when deciding
	 * automatic pattern changes.
	 *
	 * Inputs: pattern selects the pattern slot being edited, value is the menu
	 * value. Output: PatternSetting.changeBar and PAR_PATTERN_BEAT are updated.
	 * Risk: no playback timing is changed immediately; Sequencer observes this
	 * value at the next pattern-boundary decision.
	 */
	PatternSetting *p = pat_patternSettingPtr(pattern);
	if (!p)
		return;
	p->changeBar = value;
	parameter_values[PAR_PATTERN_BEAT] = value;
}

void pat_setPatternNext(uint8_t pattern, uint8_t value)
{
	/*
	 * Sets the viewed pattern's automatic next-pattern target.
	 *
	 * Caller: menu_parseGlobalParam(PAR_PATTERN_NEXT). This is pattern-level
	 * saved data, not the same thing as seq_setNextPattern(), which queues an
	 * immediate performance/playback change.
	 *
	 * Inputs: pattern slot and next-pattern enum value. Output: PatternData
	 * storage plus menu mirror are updated.
	 */
	PatternSetting *p = pat_patternSettingPtr(pattern);
	if (!p)
		return;
	p->nextPattern = value;
	parameter_values[PAR_PATTERN_NEXT] = value;
}

uint8_t pat_getPatternChangeBar(uint8_t pattern)
{
	PatternSetting *p = pat_patternSettingPtr(pattern);
	return p ? p->changeBar : 0;
}

uint8_t pat_getPatternNext(uint8_t pattern)
{
	PatternSetting *p = pat_patternSettingPtr(pattern);
	return p ? p->nextPattern : 0;
}

void pat_setTrackLength(uint8_t pattern, uint8_t track, uint8_t length)
{
	/* Menu edit path for PAR_TRACK_LENGTH.
	 *
	 * UI displays 16 as 16, but storage uses 0 to mean 16 because length and
	 * rotation share one 4-bit/4-bit byte. Output updates both storage and the
	 * currently displayed menu value. */
	LengthRotate *lr = pat_lengthRotatePtr(pattern, track);
	if (!lr)
		return;
	if (length == 16u)
		length = 0;
	lr->length = length;
	parameter_values[PAR_TRACK_LENGTH] = (uint8_t)(length ? length : 16u);
}

uint8_t pat_getTrackLength(uint8_t pattern, uint8_t track)
{
	/* Read storage length in UI form. Invalid indices and encoded 0 both return
	 * 16 so callers never display the internal sentinel value. */
	LengthRotate *lr = pat_lengthRotatePtr(pattern, track);
	uint8_t length;
	if (!lr)
		return 16u;
	length = lr->length;
	return (uint8_t)(length ? length : 16u);
}

void pat_setTrackRotation(uint8_t pattern, uint8_t track, uint8_t rotation)
{
	LengthRotate *lr = pat_lengthRotatePtr(pattern, track);
	uint8_t oldRot;
	uint8_t len;

	/*
	 * Why: track rotation is pattern edit state, but the old sequencer also
	 * compensated the live step index when rotating the currently playing
	 * pattern. Inputs: pattern/track/new rotation. Outputs: stored rotation,
	 * menu value, and possibly a sequencer runtime index adjustment. Risk: the
	 * runtime hook must remain narrow; PatternData must not take over timing.
	 */
	if (!lr)
		return;
	oldRot = lr->rotate;
	if (rotation == oldRot)
		return;
	len = lr->length;
	if (!len)
		len = 16u;
	if (pattern == seq_activePattern && seq_isRunning())
		seq_offsetTrackStepIndexForRotation(track, oldRot, rotation, len);
	lr->rotate = rotation;
	parameter_values[PAR_TRACK_ROTATION] = rotation;
}

uint8_t pat_getTrackRotation(uint8_t pattern, uint8_t track)
{
	LengthRotate *lr = pat_lengthRotatePtr(pattern, track);
	return lr ? lr->rotate : 0;
}

void pat_setShuffle(uint8_t pattern, uint8_t value)
{
	/* Menu edit path for PAR_SHUFFLE.
	 *
	 * The pattern argument is kept even while playback is global because the
	 * future owner is Pattern. Callers should not call seq_setShuffle() directly
	 * for user-facing shuffle edits anymore. */
	if (pat_patternValid(pattern))
		pat_shuffleValue[pattern] = value;
	/*
	 * Why: shuffle should become Pattern-owned, but playback still consumes
	 * seq_shuffle. Inputs: viewed pattern/value 0..127. Outputs: PatternData's
	 * remembered value and current sequencer shuffle coefficient. Risk: until
	 * playback is Scene-aware this remains a global audible setting.
	 */
	seq_setShuffle((float)value / 127.0f);
}

uint8_t pat_getShuffle(uint8_t pattern)
{
	if (!pat_patternValid(pattern))
		return 0;
	return pat_shuffleValue[pattern];
}

void pat_clearTrack(uint8_t pattern, uint8_t track)
{
	/* Clear all pattern data for one track.
	 *
	 * Outputs:
	 * - every sub-step reset to defaults
	 * - first sub-step of each main-step block active, matching legacy record
	 *   behavior
	 * - main-step mask off
	 * - length/rotation reset to 16/no rotation
	 */
	uint8_t k;
	if (!pat_patternValid(pattern) || !pat_trackValid(track))
		return;
	for (k = 0; k < NUM_STEPS; k++) {
		pat_resetStep(&pat_patternSet.seq_subStepPattern[pattern][track][k]);
		if ((k % 8u) == 0)
			pat_patternSet.seq_subStepPattern[pattern][track][k].volume |= STEP_ACTIVE_MASK;
	}
	pat_patternSet.seq_mainSteps[pattern][track] = 0;
	pat_patternSet.seq_patternLengthRotate[pattern][track].value = 0;
}

void pat_clearPattern(uint8_t pattern)
{
	/* Clear every track in one pattern. Used by initialization and copy/clear
	 * menu actions. Does not touch other patterns. */
	uint8_t i;
	if (!pat_patternValid(pattern))
		return;
	for (i = 0; i < NUM_TRACKS; i++)
		pat_clearTrack(pattern, i);
}

void pat_clearAutomation(uint8_t pattern, uint8_t track, uint8_t automTrack)
{
	/* Clear one automation lane across all sub-steps for a track. automTrack 0
	 * clears param1Nr/param1Val; any other value clears param2Nr/param2Val. */
	uint8_t k;
	if (!pat_patternValid(pattern) || !pat_trackValid(track))
		return;
	for (k = 0; k < NUM_STEPS; k++) {
		if (automTrack == 0) {
			pat_patternSet.seq_subStepPattern[pattern][track][k].param1Nr = NO_AUTOMATION;
			pat_patternSet.seq_subStepPattern[pattern][track][k].param1Val = 0;
		} else {
			pat_patternSet.seq_subStepPattern[pattern][track][k].param2Nr = NO_AUTOMATION;
			pat_patternSet.seq_subStepPattern[pattern][track][k].param2Val = 0;
		}
	}
}

void pat_copyTrack(uint8_t pattern, uint8_t srcTrack, uint8_t dstTrack)
{
	/* Copy one track inside one pattern. Copies sub-step data, main-step mask,
	 * and length/rotation. Does not copy pattern-level next/change settings. */
	if (!pat_patternValid(pattern) || !pat_trackValid(srcTrack) || !pat_trackValid(dstTrack))
		return;
	memcpy(&pat_patternSet.seq_subStepPattern[pattern][dstTrack],
	       &pat_patternSet.seq_subStepPattern[pattern][srcTrack],
	       sizeof(Step) * NUM_STEPS);
	pat_patternSet.seq_mainSteps[pattern][dstTrack] =
		pat_patternSet.seq_mainSteps[pattern][srcTrack];
	pat_patternSet.seq_patternLengthRotate[pattern][dstTrack].value =
		pat_patternSet.seq_patternLengthRotate[pattern][srcTrack].value;
}

void pat_copyPattern(uint8_t srcPattern, uint8_t dstPattern)
{
	/* Copy a full pattern. This includes all track data and pattern-level
	 * settings, preserving the old copy-pattern behavior from sequencer.c. */
	if (!pat_patternValid(srcPattern) || !pat_patternValid(dstPattern))
		return;
	memcpy(&pat_patternSet.seq_subStepPattern[dstPattern],
	       &pat_patternSet.seq_subStepPattern[srcPattern],
	       sizeof(Step) * NUM_TRACKS * NUM_STEPS);
	memcpy(&pat_patternSet.seq_mainSteps[dstPattern],
	       &pat_patternSet.seq_mainSteps[srcPattern],
	       sizeof(uint16_t) * NUM_TRACKS);
	memcpy(&pat_patternSet.seq_patternLengthRotate[dstPattern],
	       &pat_patternSet.seq_patternLengthRotate[srcPattern],
	       sizeof(LengthRotate) * NUM_TRACKS);
	pat_patternSet.seq_patternSettings[dstPattern] =
		pat_patternSet.seq_patternSettings[srcPattern];
}

void pat_setSelectedStep(uint8_t step)
{
	/* Store the current edit step in both legacy seq_selectedStep and the menu
	 * parameter array. seq_selectedStep remains because some existing code still
	 * reads it; PatternData is now the public edit API. */
	if (!pat_stepValid(step))
		return;
	seq_selectedStep = step;
	parameter_values[PAR_ACTIVE_STEP] = step;
}

void pat_setActiveAutomationTrack(uint8_t track)
{
	/* Menu path for PAR_AUTOM_TRACK. Values are treated as lane selectors:
	 * 0 records/edits param1, nonzero records/edits param2. */
	pat_activeAutomationTrack = track;
}

uint8_t pat_getActiveAutomationTrack(void)
{
	return pat_activeAutomationTrack;
}

void pat_armAutomationStep(uint8_t step, uint8_t track, uint8_t armed)
{
	/*
	 * Why: long-press automation arming is step-edit state and no longer needs
	 * a parser status byte. Inputs: step, track, armed flag. Outputs: remembered
	 * armed target for later CC writes. Risk: invalid disarm values must clear
	 * both fields so stale held-step recording cannot continue.
	 */
	if (armed && pat_stepValid(step) && pat_trackValid(track)) {
		pat_armedAutomationStep = (int8_t)step;
		pat_armedAutomationTrack = (int8_t)track;
	} else {
		pat_armedAutomationStep = -1;
		pat_armedAutomationTrack = -1;
	}
}

void pat_recordAutomation(uint8_t pattern, uint8_t track, uint8_t step,
                          uint8_t dest, uint8_t value)
{
	/*
	 * Writes one automation value into a quantized Pattern step.
	 *
	 * Callers: seq_recordAutomation() for live recording and
	 * pat_recordArmedAutomation() for held-step recording. Sequencer still
	 * decides whether recording is active and which step is quantized; PatternData
	 * owns the stored Step mutation.
	 *
	 * Inputs: pattern/track/step identify the destination, dest is the
	 * automation parameter id in the same encoded form used by playback, and
	 * value is the recorded 0..127 value.
	 *
	 * Output: active automation lane param/value fields are written. Risk:
	 * pat_activeAutomationTrack treats any nonzero value as lane 2 to preserve
	 * the old menu behavior.
	 */
	if (!pat_patternValid(pattern) || !pat_trackValid(track) || !pat_stepValid(step))
		return;
	if (pat_activeAutomationTrack == 0) {
		pat_patternSet.seq_subStepPattern[pattern][track][step].param1Nr = dest;
		pat_patternSet.seq_subStepPattern[pattern][track][step].param1Val = value;
	} else {
		pat_patternSet.seq_subStepPattern[pattern][track][step].param2Nr = dest;
		pat_patternSet.seq_subStepPattern[pattern][track][step].param2Val = value;
	}
}

void pat_recordArmedAutomation(uint8_t pattern, uint8_t dest, uint8_t value)
{
	/*
	 * Writes automation to the long-press armed step, if one exists.
	 *
	 * Caller: seq_recordAutomation() after the normal record-gated path. This
	 * preserves the old ARM_AUTOMATION_STEP behavior where holding a step and
	 * moving a control records that control to the held step even when the
	 * sequencer is not currently writing a quantized live step.
	 *
	 * Inputs: pattern is the active pattern supplied by Sequencer, dest/value
	 * describe the parameter edit. Output: no-op when nothing is armed, otherwise
	 * pat_recordAutomation() writes the armed track/step.
	 */
	if (pat_armedAutomationStep == -1 || pat_armedAutomationTrack == -1)
		return;
	pat_recordAutomation(pattern, (uint8_t)pat_armedAutomationTrack,
	                     (uint8_t)pat_armedAutomationStep, dest, value);
}

void pat_applyStepToMenu(uint8_t pattern, uint8_t track, uint8_t step)
{
	/* Replaces SEQ_REQUEST_STEP_PARAMS.
	 *
	 * Reads one Step from PatternData and mirrors its editable fields into
	 * parameter_values[] for the menu display. Automation destinations are
	 * converted from stored param-number encoding back to modTargets[] indices.
	 * This function mutates menu edit state but does not change pattern data. */
	Step *s = pat_stepPtr(pattern, track, step);
	uint8_t dest;
	if (!s)
		return;
	parameter_values[PAR_STEP_VOLUME] = (uint8_t)(s->volume & STEP_VOLUME_MASK);
	parameter_values[PAR_STEP_NOTE] = s->note;
	parameter_values[PAR_STEP_PROB] = s->prob;

	dest = s->param1Nr;
	if ((dest < 128u) && (dest != 0u))
		dest--;
	if (dest < END_OF_SOUND_PARAMETERS)
		parameter_values[PAR_P1_DEST] = paramToModTarget[dest];

	dest = s->param2Nr;
	if ((dest < 128u) && (dest != 0u))
		dest--;
	if (dest < END_OF_SOUND_PARAMETERS)
		parameter_values[PAR_P2_DEST] = paramToModTarget[dest];

	parameter_values[PAR_P1_VAL] = s->param1Val;
	parameter_values[PAR_P2_VAL] = s->param2Val;
	pat_setSelectedStep(step);
}

void pat_applyPatternSettingsToMenu(uint8_t pattern)
{
	/*
	 * Copies PatternData pattern-level settings into menu parameter_values.
	 *
	 * Replaces SEQ_REQUEST_PATTERN_PARAMS. Callers are Menu/button/load paths
	 * that need the Pattern Settings page to reflect the currently viewed
	 * pattern.
	 *
	 * Input: pattern slot to display. Output: PAR_PATTERN_BEAT and
	 * PAR_PATTERN_NEXT mirror PatternData. Risk: this is a menu/UI sync helper,
	 * not a pattern mutation; do not call it from interrupt context.
	 */
	PatternSetting *p = pat_patternSettingPtr(pattern);
	if (!p)
		return;
	parameter_values[PAR_PATTERN_BEAT] = p->changeBar;
	parameter_values[PAR_PATTERN_NEXT] = p->nextPattern;
}

void pat_applyTrackSettingsToMenu(uint8_t pattern, uint8_t track)
{
	/*
	 * Copies PatternData track-level settings into menu parameter_values.
	 *
	 * Completes the non-LED side effect formerly hidden inside
	 * LED_QUERY_SEQ_TRACK. Callers pair this with led_updatePatternTrack() when
	 * changing viewed pattern/track so both physical LEDs and editable menu
	 * params refresh from the same PatternData source.
	 *
	 * Inputs: pattern/track to display. Output: PAR_TRACK_LENGTH,
	 * PAR_TRACK_ROTATION, and PAR_SHUFFLE are refreshed. Risk: shuffle still has
	 * global playback backing, but the UI-facing API is Pattern-owned so callers
	 * are ready for per-pattern/per-track shuffle later.
	 */
	if (!pat_patternValid(pattern) || !pat_trackValid(track))
		return;
	parameter_values[PAR_TRACK_LENGTH] = pat_getTrackLength(pattern, track);
	parameter_values[PAR_TRACK_ROTATION] = pat_getTrackRotation(pattern, track);
	parameter_values[PAR_SHUFFLE] = pat_getShuffle(pattern);
}
