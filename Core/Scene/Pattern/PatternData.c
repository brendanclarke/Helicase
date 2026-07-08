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

static uint8_t pat_defaultTrackMidiChannel(uint8_t track)
{
	/*
	 * Default new PatternData-owned track MIDI channel in menu/display form.
	 *
	 * The old kit/global MIDI channel parameters remain for compatibility, but
	 * the STEP track-settings page now loads from PatternData. A 1..7 default
	 * keeps freshly cleared patterns valid before any globals or pattern file
	 * have been loaded.
	 */
	return (uint8_t)((track < 15u) ? (track + 1u) : 1u);
}

/*
 * Track-scale menu values as exact rational timing ratios.
 *
 * Why this table lives in PatternData: track scale is Pattern-owned per-track
 * metadata, while Sequencer only needs a normalized numerator/denominator when
 * it schedules playback. Inputs are TRACK_SCALE_* menu/storage values. Output
 * is a small ratio where 1/1 means the corrected default 4-steps-per-beat grid,
 * 5/2 is x25, and 2/5 is /25. Risk: MenuText.h must keep the display order in
 * sync with this table because the menu value is the table index.
 */
static const TrackScaleRatio pat_trackScaleRatios[TRACK_SCALE_COUNT] = {
	{1u, 8u}, {1u, 7u}, {1u, 6u}, {1u, 5u}, {1u, 4u}, {1u, 3u},
	{2u, 5u}, {1u, 2u}, {3u, 5u}, {3u, 4u}, {1u, 1u},
	{4u, 3u}, {5u, 3u}, {2u, 1u}, {5u, 2u}, {3u, 1u},
	{4u, 1u}, {5u, 1u}, {6u, 1u}, {7u, 1u}, {8u, 1u},
};

static void pat_resetStep(Step *step)
{
	/* Reset one bridge step to the default inactive edit state used by clear-pattern
	 * and clear-track operations. Input/output is a live Step pointer. A null
	 * guard keeps callers simple when pointer helpers return 0. */
	if (!step)
		return;
	step->note 		= PAT_DEFAULT_NOTE;
	step->param1Nr 	= NO_AUTOMATION;
	step->param1Val = 0;
	step->param2Nr	= NO_AUTOMATION;
	step->param2Val	= 0;
	step->prob		= 127;
	step->volume	= 100;
}

void pat_commitStagedPattern(uint8_t pattern)
{
	/*
	 * Commits the temporary filesystem load buffer into one live pattern slot.
	 *
	 * Why this moved here: filesystem owns async file streaming and sequencer owns
	 * the safe pattern-boundary timing, but the copy itself is PatternData storage
	 * mutation. Keeping the four field copies here removes the last bulk PatternSet
	 * write from sequencer.c and keeps the legacy binary layout local to the owner.
	 *
	 * Input: pattern is the destination pattern slot, usually seq_activePattern
	 * supplied by sequencer.c after filesystem has filled pat_tmpPattern. Output:
	 * destination steps, compatibility masks, pattern settings, and length/rotation
	 * bytes are overwritten from pat_tmpPattern.
	 *
	 * Callers/clients/confederates: sequencer.c calls this from its
	 * pattern-boundary swap path after filesystem sets seq_newPatternAvailable and
	 * arms seq_armActivePatternReload(). filesystem.c writes the staging buffer
	 * through pat_*Ptr accessors. Risk: this is file-format-visible storage; do not
	 * change field order or sizes here as part of a move-only refactor.
	 */
	if (!pat_patternValid(pattern))
		return;
	memcpy(&pat_patternSet.pat_subStepPattern[pattern],
	       &pat_tmpPattern.pat_subStepPattern,
	       sizeof(Step) * NUM_TRACKS * NUM_STEPS);
	memcpy(&pat_patternSet.pat_mainSteps[pattern],
	       &pat_tmpPattern.pat_mainSteps,
	       sizeof(uint16_t) * NUM_TRACKS);
	memcpy(&pat_patternSet.pat_patternSettings[pattern],
	       &pat_tmpPattern.pat_patternSettings,
	       sizeof(PatternSetting));
	memcpy(&pat_patternSet.pat_patternLengthRotate[pattern],
	       &pat_tmpPattern.pat_patternLengthRotate,
	       sizeof(LengthRotate) * NUM_TRACKS);
}

uint8_t pat_trackValid(uint8_t track)
{
	return (uint8_t)(track < NUM_TRACKS);
}

uint8_t pat_patternValid(uint8_t pattern)
{
	return (uint8_t)(pattern == 0u);
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
		return &pat_tmpPattern.pat_subStepPattern[track][step];
	if (!pat_patternValid(pattern))
		return 0;
	return &pat_patternSet.pat_subStepPattern[pattern][track][step];
}

uint16_t *pat_mainStepsPtr(uint8_t pattern, uint8_t track)
{
	/* Same staging-aware pointer policy as pat_stepPtr(), but for the 16-bit
	 * compatibility main-step mask used only by legacy file/menu plumbing. */
	if (!pat_trackValid(track))
		return 0;
	if (pattern == PATTERNDATA_STAGING_PATTERN)
		return &pat_tmpPattern.pat_mainSteps[track];
	if (!pat_patternValid(pattern))
		return 0;
	return &pat_patternSet.pat_mainSteps[pattern][track];
}

PatternSetting *pat_patternSettingPtr(uint8_t pattern)
{
	/* Pattern settings are per-pattern, except the temporary filesystem staging
	 * buffer has a single PatternSetting because it only stages one pattern at
	 * a time. */
	if (pattern == PATTERNDATA_STAGING_PATTERN)
		return &pat_tmpPattern.pat_patternSettings;
	if (!pat_patternValid(pattern))
		return 0;
	return &pat_patternSet.pat_patternSettings[pattern];
}

LengthRotate *pat_lengthRotatePtr(uint8_t pattern, uint8_t track)
{
	/* Per-track Pattern settings live here during the bridge.
	 *
	 * LengthRotate keeps its historical name for now, but the record now owns
	 * the STEP front-page track settings: length, rotation, scale, MIDI channel,
	 * and MIDI note. The legacy pattern length stream may still supply only the
	 * length byte; loaders must default the newer fields before reading the
	 * optional settings extension block.
	 */
	if (!pat_trackValid(track))
		return 0;
	if (pattern == PATTERNDATA_STAGING_PATTERN)
		return &pat_tmpPattern.pat_patternLengthRotate[track];
	if (!pat_patternValid(pattern))
		return 0;
	return &pat_patternSet.pat_patternLengthRotate[pattern][track];
}

void pat_init(void)
{
	uint8_t i;

	/* Shuffle has a PatternData-facing API now. Initialize all pattern slots to
	 * zero even though playback still reads the global sequencer coefficient. */
	memset(pat_shuffleValue, 0, sizeof(pat_shuffleValue));

	for (i = 0; i < NUM_PATTERN; i++) {
		/* Default pattern-change behavior: play once, then stay on itself. */
		pat_patternSet.pat_patternSettings[i].changeBar = 0;
		pat_patternSet.pat_patternSettings[i].nextPattern = i;
		/* pat_clearPattern() initializes steps, compatibility mask, and
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

uint8_t pat_readStep(uint8_t pattern, uint8_t track, uint8_t step, Step *out)
{
	/*
	 * Copies one Step out of PatternData for playback-side inspection.
	 *
	 * Why: sequencer.c still needs to parse automation nodes and trigger note/vol
	 * data at playback time, but should no longer index PatternData arrays
	 * directly. Inputs are pattern/track/step and a destination Step pointer.
	 * Output is 1 plus a copied Step on success, 0 on invalid coordinates or null
	 * output. Common callers are sequencer playback and roll/MIDI trigger paths.
	 * Risk: the returned Step is a snapshot, not live storage; callers that need to
	 * mutate pattern data must use pat_* mutation helpers instead.
	 */
	Step *s;
	if (!out)
		return 0;
	s = pat_stepPtr(pattern, track, step);
	if (!s)
		return 0;
	*out = *s;
	return 1;
}

uint8_t pat_getStepProbability(uint8_t pattern, uint8_t track, uint8_t step)
{
	/*
	 * Returns one step probability for sequencer playback.
	 *
	 * Inputs: pattern/track/step. Output: stored probability, or 0 for invalid
	 * coordinates so invalid reads cannot trigger a voice by accident. Caller:
	 * seq_nextStep() before comparing against its per-track random value.
	 */
	Step *s = pat_stepPtr(pattern, track, step);
	return s ? s->prob : 0u;
}

uint8_t pat_getStepNote(uint8_t pattern, uint8_t track, uint8_t step)
{
	/*
	 * Returns one stored note value for playback/roll paths.
	 *
	 * Inputs: pattern/track/step. Output: stored MIDI note, or PAT_DEFAULT_NOTE
	 * on invalid coordinates. Callers/clients: seq_nextStep(), seq_triggerVoice(),
	 * and roll recording. Risk: this is a read-only helper; note edits must go
	 * through pat_setStepNote() or pat_recordNote().
	 */
	Step *s = pat_stepPtr(pattern, track, step);
	return s ? s->note : PAT_DEFAULT_NOTE;
}

uint8_t pat_getStepVolume(uint8_t pattern, uint8_t track, uint8_t step)
{
	/*
	 * Returns the stored 0..127 velocity for one step.
	 *
	 * Inputs: pattern/track/step. Output: lower seven bits of volume, or 0 on
	 * invalid coordinates. Caller/confederates: sequencer voice trigger and MIDI
	 * note echo paths use this to preserve the legacy behavior where MIDI output
	 * velocity follows stored step velocity even if an internal roll trigger used a
	 * fixed roll volume.
	 */
	Step *s = pat_stepPtr(pattern, track, step);
	return s ? (uint8_t)(s->volume & STEP_VOLUME_MASK) : 0u;
}

void pat_setMainStep(uint8_t pattern, uint8_t track, uint8_t mainStep, uint8_t onOff)
{
	/* Direct compatibility-mask setter used while old pattern file/menu fields
	 * still exist. Inputs are target pattern/track, a 0..15 mask bit, and a
	 * boolean onOff. Output is only the mask bit; Step data is not created or
	 * cleared here, and playback does not consult this mask. */
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
	/* Raw compatibility-mask writer used by legacy file/staging code. It writes
	 * the whole 16-bit mask in one shot; bridge playback and the Euklid generator
	 * use Step active bits instead. */
	uint16_t *mainSteps = pat_mainStepsPtr(pattern, track);
	if (!mainSteps)
		return;
	*mainSteps = bits;
}

void pat_toggleStep(uint8_t track, uint8_t step, uint8_t pattern)
{
	/* Toggle the active bit of one bridge step while preserving the stored velocity
	 * in the lower seven bits. Used by buttonHandler step toggles. */
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
	/* Toggle one legacy 16-bit compatibility-mask bit. The LED refresh is done by
	 * callers in ledHandler because PatternData does not own presentation. */
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
	/* Menu edit path for PAR_STEP_PROB. Probability is stored per bridge step and
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
	 * The Phase 2 bridge stores a real 1..128 step count. A zero value can arrive from older files or defensive callers and is normalized to the full 128-step track default. Output updates both PatternData and the currently displayed menu value. */
	LengthRotate *lr = pat_lengthRotatePtr(pattern, track);
	if (!lr)
		return;
	if (length == 0u)
		length = NUM_STEPS;
	else if (length > NUM_STEPS)
		length = NUM_STEPS;
	lr->length = length;
	parameter_values[PAR_TRACK_LENGTH] = length;
}

uint8_t pat_getTrackLength(uint8_t pattern, uint8_t track)
{
	/* Read storage length in UI form. Invalid indices and missing/zero legacy values return the bridge default of 128 steps. */
	LengthRotate *lr = pat_lengthRotatePtr(pattern, track);
	uint8_t length;
	if (!lr)
		return NUM_STEPS;
	length = lr->length;
	if (length == 0u)
		return NUM_STEPS;
	if (length > NUM_STEPS)
		return NUM_STEPS;
	return length;
}

uint8_t pat_getEffectiveTrackLength(uint8_t pattern, uint8_t track)
{
	/*
	 * Returns a nonzero playback length for one pattern track.
	 *
	 * Why: sequencer wrap and external-clock math require a concrete nonzero length. Inputs: pattern/track. Output: 1..128 steps, with invalid coordinates falling back to 128 so playback callers never divide/modulo by zero.
	 * Callers/clients: seq_nextStep(), seq_triggerNextMasterStep(), and
	 * seq_setStepIndexToStart().
	 */
	return pat_getTrackLength(pattern, track);
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
	len = pat_getEffectiveTrackLength(pattern, track);
	if (rotation >= len)
		rotation = (uint8_t)(rotation % len);
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

void pat_setTrackScale(uint8_t pattern, uint8_t track, uint8_t scale)
{
	LengthRotate *lr = pat_lengthRotatePtr(pattern, track);

	/*
	 * Stores the per-track timing scale selected from the Pattern STEP front
	 * page.
	 *
	 * Inputs: pattern/track select the PatternData owner and scale is a
	 * TRACK_SCALE_* menu index. Outputs: storage is clamped to a known scale and
	 * PAR_TRACK_SCALE mirrors it for the current UI. The sequencer reads this
	 * through pat_getTrackScaleRatio() when scheduling. If the edited pattern is
	 * currently running, Sequencer is asked to realign immediately so a mid-run
	 * change from a slow divide to a fast multiply cannot dump a long backlog of
	 * "missed" scaled steps into one tick.
	 */
	if (!lr)
		return;
	if (scale >= TRACK_SCALE_COUNT)
		scale = TRACK_SCALE_OFF;
	lr->scale = scale;
	parameter_values[PAR_TRACK_SCALE] = scale;
	if (pattern == seq_activePattern && seq_isRunning())
		seq_realignActivePatternToMasterClock();
}

void pat_setTrackMidiChannel(uint8_t pattern, uint8_t track, uint8_t channel)
{
	LengthRotate *lr = pat_lengthRotatePtr(pattern, track);

	/*
	 * Store the Pattern-owned MIDI channel shown on the STEP track-settings
	 * page. The menu value is 1..16; zero or out-of-range values are normalized
	 * so old/partial files cannot leave the MIDI parser with an underflowing
	 * channel.
	 */
	if (!lr)
		return;
	if (channel < 1u)
		channel = 1u;
	else if (channel > 16u)
		channel = 16u;
	lr->midiChannel = channel;
	parameter_values[PAR_TRACK_MIDI_CHAN] = channel;
}

uint8_t pat_getTrackMidiChannel(uint8_t pattern, uint8_t track)
{
	LengthRotate *lr = pat_lengthRotatePtr(pattern, track);
	if (!lr || lr->midiChannel < 1u || lr->midiChannel > 16u)
		return pat_defaultTrackMidiChannel(track);
	return lr->midiChannel;
}

void pat_setTrackMidiNote(uint8_t pattern, uint8_t track, uint8_t note)
{
	LengthRotate *lr = pat_lengthRotatePtr(pattern, track);

	/*
	 * Store the Pattern-owned MIDI note override shown on the STEP front page.
	 * A value of 0 keeps the existing "any/default" behavior; nonzero values
	 * are concrete MIDI notes.
	 */
	if (!lr)
		return;
	if (note > 127u)
		note = 127u;
	lr->midiNote = note;
	parameter_values[PAR_TRACK_MIDI_NOTE] = note;
}

uint8_t pat_getTrackMidiNote(uint8_t pattern, uint8_t track)
{
	LengthRotate *lr = pat_lengthRotatePtr(pattern, track);
	(void)track;
	if (!lr || lr->midiNote > 127u)
		return 0u;
	return lr->midiNote;
}

uint8_t pat_getTrackScale(uint8_t pattern, uint8_t track)
{
	LengthRotate *lr = pat_lengthRotatePtr(pattern, track);
	if (!lr || lr->scale >= TRACK_SCALE_COUNT)
		return TRACK_SCALE_OFF;
	return lr->scale;
}

TrackScaleRatio pat_getTrackScaleRatio(uint8_t pattern, uint8_t track)
{
	uint8_t scale = pat_getTrackScale(pattern, track);

	/*
	 * Converts PatternData track scale to the exact rational ratio consumed by
	 * Sequencer timing. Invalid storage falls back to 1/1 so corrupt or legacy
	 * data cannot create a zero denominator in the timing path.
	 */
	if (scale >= TRACK_SCALE_COUNT)
		scale = TRACK_SCALE_OFF;
	return pat_trackScaleRatios[scale];
}

void pat_setShuffle(uint8_t pattern, uint8_t value)
{
	/* Menu edit path for PAR_SHUFFLE.
	 *
	 * The pattern argument is kept even while playback is global because the
	 * stored/user-facing owner is PatternData. Callers should not call
	 * seq_setShuffle() directly for user-facing shuffle edits anymore. */
	if (pat_patternValid(pattern))
		pat_shuffleValue[pattern] = value;
	/*
	 * Why: shuffle is Pattern-owned, but playback still consumes seq_shuffle.
	 * Inputs: viewed pattern/value 0..127. Outputs: PatternData's remembered
	 * value and current sequencer shuffle coefficient. Risk: until playback is
	 * Scene-aware this remains a global audible setting.
	 */
	seq_setShuffle((float)value / 127.0f);
}

void pat_setAllShuffle(uint8_t value)
{
	/*
	 * Imports the legacy one-byte pattern-set shuffle value into PatternData.
	 *
	 * Why: filesystem pattern and container files currently serialize one shuffle
	 * byte, not one value per pattern slot. Shuffle ownership is PatternData, so
	 * file loads should no longer call seq_setShuffle() directly; they call this
	 * helper to update Pattern-owned backing and let PatternData bridge the current
	 * audible runtime coefficient.
	 *
	 * Input: value is the loaded 0..127 shuffle byte. Outputs: all current pattern
	 * slots receive the value, PAR_SHUFFLE mirrors it for menu display, and the
	 * same PatternData-to-sequencer runtime bridge used by pat_setShuffle()
	 * refreshes playback timing.
	 *
	 * Callers/clients/confederates: filesystem_loadPattern_tick() and
	 * filesystem_loadContainer_tick() call this after reading the shuffle block.
	 * pat_applyTrackSettingsToMenu() later exposes the per-pattern backing to UI.
	 * Risk: this deliberately preserves the existing single-byte file format; do
	 * not interpret it as a Phase 3 per-pattern shuffle serialization design.
	 */
	uint8_t pattern;
	for (pattern = 0; pattern < NUM_PATTERN; pattern++)
		pat_shuffleValue[pattern] = value;
	parameter_values[PAR_SHUFFLE] = value;
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
	 * - every bridge step resets to default note, velocity, probability, and no automation
	 * - every step is inactive
	 * - the legacy main-step mask is cleared for file/UI compatibility
	 * - length/rotation reset to 128 steps/no rotation
	 */
	uint8_t k;
	if (!pat_patternValid(pattern) || !pat_trackValid(track))
		return;
	for (k = 0; k < NUM_STEPS; k++)
		pat_resetStep(&pat_patternSet.pat_subStepPattern[pattern][track][k]);
	pat_patternSet.pat_mainSteps[pattern][track] = 0;
	pat_patternSet.pat_patternLengthRotate[pattern][track].length = NUM_STEPS;
	pat_patternSet.pat_patternLengthRotate[pattern][track].rotate = 0;
	pat_patternSet.pat_patternLengthRotate[pattern][track].scale = TRACK_SCALE_OFF;
	pat_patternSet.pat_patternLengthRotate[pattern][track].midiChannel =
		pat_defaultTrackMidiChannel(track);
	pat_patternSet.pat_patternLengthRotate[pattern][track].midiNote = 0u;
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
	/* Clear one automation lane across all bridge steps for a track. automTrack 0
	 * clears param1Nr/param1Val; any other value clears param2Nr/param2Val. */
	uint8_t k;
	if (!pat_patternValid(pattern) || !pat_trackValid(track))
		return;
	for (k = 0; k < NUM_STEPS; k++) {
		if (automTrack == 0) {
			pat_patternSet.pat_subStepPattern[pattern][track][k].param1Nr = NO_AUTOMATION;
			pat_patternSet.pat_subStepPattern[pattern][track][k].param1Val = 0;
		} else {
			pat_patternSet.pat_subStepPattern[pattern][track][k].param2Nr = NO_AUTOMATION;
			pat_patternSet.pat_subStepPattern[pattern][track][k].param2Val = 0;
		}
	}
}

void pat_recordNote(uint8_t pattern, uint8_t track, uint8_t step,
                    uint8_t velocity, uint8_t note)
{
	/*
	 * Records a quantized note into PatternData.
	 *
	 * Why this moved here: seq_addNote() owns recording state, quantization, and
	 * target-pattern timing, but the Step mutation itself is pattern storage.
	 * Inputs: pattern/track/step identify the destination, velocity is stored
	 * in the lower seven volume bits, and note is the MIDI note to store. Outputs:
	 * the target Step gets note, velocity, 100% probability, and active bit.
	 * The legacy 16-bit mask is mirrored from step % 16 so old save/load fields
	 * stay deterministic during the bridge.
	 *
	 * Callers/clients/confederates: seq_addNote() calls this after choosing the
	 * quantized destination. ledHandler still receives dirty-step notifications
	 * from sequencer because LED presentation is not PatternData ownership.
	 * Risk: do not clear automation lanes here because recording a note did not
	 * previously wipe step automation.
	 */
	Step *stepPtr;
	uint8_t mainStep;
	if (!pat_patternValid(pattern) || !pat_trackValid(track) || !pat_stepValid(step))
		return;
	mainStep = (uint8_t)(step & 0x0fu);
	stepPtr = &pat_patternSet.pat_subStepPattern[pattern][track][step];
	stepPtr->note = note;
	stepPtr->volume = (uint8_t)(velocity & STEP_VOLUME_MASK);
	stepPtr->prob = 127;
	stepPtr->volume |= STEP_ACTIVE_MASK;
	pat_setMainStep(pattern, track, mainStep, 1);
}

void pat_eraseMainStepSubSteps(uint8_t pattern, uint8_t track, uint8_t mainStep)
{
	/*
	 * Legacy helper that erases one old main-step group.
	 *
	 * Why this moved here: live erase is triggered by sequencer timing, but clearing
	 * Step records and main-step bits is PatternData mutation. Inputs:
	 * pattern/track/mainStep select the historical eight-step cluster to reset.
	 * Outputs: parent main-step bit is cleared, all eight Steps return to default
	 * note, automation, probability, and velocity, then the first old-group step
	 * is activated to preserve compatibility for any remaining legacy caller.
	 *
	 * Callers/clients/confederates: seq_nextStep() calls this when erase mode is
	 * active on the visible voice. pat_resetStep() supplies the per-step defaults;
	 * ledHandler repaint remains sequencer/UI responsibility. Risk: mainStep must
	 * be 0..15; invalid indices are ignored so live playback cannot write outside
	 * PatternData storage.
	 */
	uint8_t i;
	uint8_t firstStep;
	if (!pat_patternValid(pattern) || !pat_trackValid(track) || mainStep >= 16u)
		return;
	pat_setMainStep(pattern, track, mainStep, 0);
	firstStep = (uint8_t)(mainStep * 8u);
	for (i = firstStep; i < (uint8_t)(firstStep + 8u); i++)
		pat_resetStep(&pat_patternSet.pat_subStepPattern[pattern][track][i]);
	pat_patternSet.pat_subStepPattern[pattern][track][firstStep].volume |=
		STEP_ACTIVE_MASK;
}


void pat_eraseStep(uint8_t pattern, uint8_t track, uint8_t step)
{
	/* Clear one bridge step without touching neighbouring steps.
	 *
	 * Why: the 8-bar bridge treats Step[0..127] as real sequencer steps rather
	 * than old sub-steps grouped under a main-step mask. Live erase now needs to
	 * remove only the current step. Inputs identify the PatternData destination;
	 * output resets that Step to defaults and leaves it inactive. */
	Step *s = pat_stepPtr(pattern, track, step);
	if (!s)
		return;
	pat_resetStep(s);
}
void pat_copyTrack(uint8_t pattern, uint8_t srcTrack, uint8_t dstTrack)
{
	/* Copy one track inside one pattern. Copies all 128 bridge steps, the legacy
	 * main-step mask, and length/rotation. Does not copy pattern-level next/change
	 * settings. */
	if (!pat_patternValid(pattern) || !pat_trackValid(srcTrack) || !pat_trackValid(dstTrack))
		return;
	memcpy(&pat_patternSet.pat_subStepPattern[pattern][dstTrack],
	       &pat_patternSet.pat_subStepPattern[pattern][srcTrack],
	       sizeof(Step) * NUM_STEPS);
	pat_patternSet.pat_mainSteps[pattern][dstTrack] =
		pat_patternSet.pat_mainSteps[pattern][srcTrack];
	pat_patternSet.pat_patternLengthRotate[pattern][dstTrack] =
		pat_patternSet.pat_patternLengthRotate[pattern][srcTrack];
}

void pat_copyPattern(uint8_t srcPattern, uint8_t dstPattern)
{
	/* Copy a full pattern. This includes all track data and pattern-level
	 * settings, preserving the old copy-pattern behavior from sequencer.c. */
	if (!pat_patternValid(srcPattern) || !pat_patternValid(dstPattern))
		return;
	memcpy(&pat_patternSet.pat_subStepPattern[dstPattern],
	       &pat_patternSet.pat_subStepPattern[srcPattern],
	       sizeof(Step) * NUM_TRACKS * NUM_STEPS);
	memcpy(&pat_patternSet.pat_mainSteps[dstPattern],
	       &pat_patternSet.pat_mainSteps[srcPattern],
	       sizeof(uint16_t) * NUM_TRACKS);
	memcpy(&pat_patternSet.pat_patternLengthRotate[dstPattern],
	       &pat_patternSet.pat_patternLengthRotate[srcPattern],
	       sizeof(LengthRotate) * NUM_TRACKS);
	pat_patternSet.pat_patternSettings[dstPattern] =
		pat_patternSet.pat_patternSettings[srcPattern];
}


void pat_copyBar(uint8_t pattern, uint8_t track, uint8_t srcBar, uint8_t dstBar)
{
	/* Copy one 16-step bar inside one track.
	 *
	 * Caller/client: COPY + SELECT bridge gesture in buttonHandler/copyClearTools.
	 * Inputs are zero-based bars 0..7 for the current track/pattern. Output: the
	 * destination 16 Step records are overwritten, and track length is extended to
	 * include the destination bar when needed. */
	uint8_t srcStep;
	uint8_t dstStep;
	uint8_t neededLength;

	if (!pat_patternValid(pattern) || !pat_trackValid(track) ||
	    srcBar >= NUM_BARS || dstBar >= NUM_BARS)
		return;

	srcStep = (uint8_t)(srcBar * NUM_STEPS_PER_BAR);
	dstStep = (uint8_t)(dstBar * NUM_STEPS_PER_BAR);
	memcpy(&pat_patternSet.pat_subStepPattern[pattern][track][dstStep],
	       &pat_patternSet.pat_subStepPattern[pattern][track][srcStep],
	       sizeof(Step) * NUM_STEPS_PER_BAR);

	neededLength = (uint8_t)((dstBar + 1u) * NUM_STEPS_PER_BAR);
	if (pat_getTrackLength(pattern, track) < neededLength)
		pat_setTrackLength(pattern, track, neededLength);
}
void pat_setSelectedStep(uint8_t step)
{
	/* Store the current edit step in the menu parameter array.
	 *
	 * Why: selected-step state is Pattern/Menu edit context, not sequencer
	 * transport state. Inputs: absolute bridge step index. Output:
	 * PAR_ACTIVE_STEP mirrors the selected step for the menu and later
	 * PatternData edits. Callers/clients: buttonHandler step selection and menu
	 * active-step changes. Risk: invalid steps are ignored so stale UI state does
	 * not point later edit calls outside PatternData storage.
	 */
	if (!pat_stepValid(step))
		return;
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
		pat_patternSet.pat_subStepPattern[pattern][track][step].param1Nr = dest;
		pat_patternSet.pat_subStepPattern[pattern][track][step].param1Val = value;
	} else {
		pat_patternSet.pat_subStepPattern[pattern][track][step].param2Nr = dest;
		pat_patternSet.pat_subStepPattern[pattern][track][step].param2Val = value;
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
	parameter_values[PAR_TRACK_SCALE] = pat_getTrackScale(pattern, track);
	/*
	 * STEP front-page aliases mirror PatternData-owned track settings. Legacy
	 * PAR_MIDI_* parameters may still be updated as a compatibility output when
	 * these aliases are edited, but they are no longer the storage owner for
	 * this page.
	 */
	parameter_values[PAR_TRACK_MIDI_CHAN] = pat_getTrackMidiChannel(pattern, track);
	parameter_values[PAR_TRACK_MIDI_NOTE] = pat_getTrackMidiNote(pattern, track);
	parameter_values[PAR_SHUFFLE] = pat_getShuffle(pattern);
}
