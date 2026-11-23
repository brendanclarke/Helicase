/*
 * MidiLearn.c - learned CCs and the Learn gesture (S083).
 */

/*
 *  Modified on: 21.10.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#include "MidiLearn.h"
#include "MidiDefs.h"
#include "MidiIn.h"
#include "MidiControl.h"
#include "menu.h"
#include "buttonHandler.h"
#include "filesystem.h"
#include "SceneData.h"
#include "SceneModTargets.h"
#include "InstrumentManager.h"
#include "EffectsManager.h"
#include "LiveRecord.h"        /* S096 P3: the delta recording rule */
#include "sequencer.h"         /* S096 P3: seq_menuOverrideSet() (R12) */
#include "timebase.h"
#include "config.h"
#include <string.h>

/*
 * One learned row (plan 3.6, F36; S096 P3 sources and modes).
 * ccKind: bits 0..6 CC (CC rows only), bit 7 channel kind (0 = G, 1 = the
 * target voice's channel). space: bits 0..2 = 0..5 voice slot, 6 Scene,
 * 7 Effect; S096 P3: bits 3..4 the source (MIDI_LEARN_SRC_*), bits 5..6 the
 * mode (default, absolute, + delta, - delta). type:
 * instrument_type_t (voice), effect_type_id_t (Effect), 0xFF (Scene).
 * index: descriptor 0..63, Scene-local index, or Effect-local 0..62.
 */
typedef struct {
	uint8_t ccKind;
	uint8_t space;
	uint8_t type;
	uint8_t index;
} midiLearn_entry_t;

#define MIDI_LEARN_KIND_VOICE   0x80u
#define MIDI_LEARN_SPACE_SCENE  6u
#define MIDI_LEARN_SPACE_EFFECT 7u
#define MIDI_LEARN_TYPE_NONE    0xFFu

/* S096 P3 C4 (plan §5.2): the space byte's source and mode fields. */
#define MIDI_LEARN_SPACE_MASK   0x07u
#define MIDI_LEARN_SRC_SHIFT    3u
#define MIDI_LEARN_SRC_MASK     0x18u   /* 0 CC, 1 pressure, 2 note number, 3 velocity */
#define MIDI_LEARN_MODE_SHIFT   5u
#define MIDI_LEARN_MODE_MASK    0x60u   /* 0 default, 1 absolute, 2 + delta, 3 - delta */

enum {
    MIDI_LEARN_MODE_DEFAULT = 0,
    MIDI_LEARN_MODE_ABS = 1,
    MIDI_LEARN_MODE_PLUS = 2,
    MIDI_LEARN_MODE_MINUS = 3
};

static uint8_t midiLearn_spaceOf(const midiLearn_entry_t *e)
{
    return (uint8_t)(e->space & MIDI_LEARN_SPACE_MASK);
}

static uint8_t midiLearn_srcOf(const midiLearn_entry_t *e)
{
    return (uint8_t)((e->space & MIDI_LEARN_SRC_MASK) >> MIDI_LEARN_SRC_SHIFT);
}

static uint8_t midiLearn_modeOf(const midiLearn_entry_t *e)
{
    return (uint8_t)((e->space & MIDI_LEARN_MODE_MASK) >> MIDI_LEARN_MODE_SHIFT);
}

enum {
	MIDI_LEARN_IDLE = 0,
	MIDI_LEARN_WAIT_TARGET,	/* armed: touch a parameter */
	MIDI_LEARN_WAIT_CC,	/* target shows LRN: send a CC */
	MIDI_LEARN_FULL		/* refused: FUL shows briefly */
};

static midiLearn_entry_t midiLearn_table[MIDI_LEARN_MAX_ENTRIES];
static uint8_t  midiLearn_used;
static uint8_t  midiLearn_state;
static uint16_t midiLearn_target = MIDI_TARGET_OFF;
static uint16_t midiLearn_fullUntil;

/* Row key of target `id` in the active Scene; 0 when not addressable. */
static uint8_t midiLearn_keyOf(uint16_t id, uint8_t *space, uint8_t *type,
                               uint8_t *index)
{
	if (instrumentParam_isVoiceParameter(id)) {
		const kit_instrument_slot_t *instr = scene_instrumentSlotConst(
		    scene_getActiveIndex(), instrumentParam_slot(id));

		if (!instr)
			return 0u;
		*space = instrumentParam_slot(id);
		*type = (uint8_t)instr->type;
		*index = instrumentParam_local(id);
		return 1u;
	}
	if (sceneModTarget_isSceneTarget(id)) {
		*space = MIDI_LEARN_SPACE_SCENE;
		*type = MIDI_LEARN_TYPE_NONE;
		return sceneModTarget_indexFromId(id, index);
	}
	if (effectTarget_isEffectId(id)) {
		*space = MIDI_LEARN_SPACE_EFFECT;
		*type = (uint8_t)effects_activeType();
		*index = effectTarget_local(id);
		return (uint8_t)(*index < EFFECT_TARGET_PATTERN_LOCAL_LIMIT);
	}
	return 0u;
}

static uint16_t midiLearn_targetOf(const midiLearn_entry_t *e)
{
	const uint8_t space = midiLearn_spaceOf(e);

	if (space < MIDI_LEARN_SPACE_SCENE)
		return (uint16_t)instrumentParam_make(space, e->index);
	if (space == MIDI_LEARN_SPACE_SCENE)
		return sceneModTarget_idFromIndex(e->index);
	return effectTarget_id(e->index);
}

/* F51: a row applies only while its type is loaded (active Scene). */
static uint8_t midiLearn_typeMatches(const midiLearn_entry_t *e)
{
	const uint8_t space = midiLearn_spaceOf(e);

	if (space < MIDI_LEARN_SPACE_SCENE) {
		const kit_instrument_slot_t *instr = scene_instrumentSlotConst(
		    scene_getActiveIndex(), space);
		return (uint8_t)(instr && (uint8_t)instr->type == e->type);
	}
	if (space == MIDI_LEARN_SPACE_EFFECT)
		return (uint8_t)((uint8_t)effects_activeType() == e->type);
	return 1u;
}

static int16_t midiLearn_find(uint8_t space, uint8_t type, uint8_t index,
                              uint8_t src)
{
	uint8_t i;

	for (i = 0u; i < midiLearn_used; i++) {
		const midiLearn_entry_t *e = &midiLearn_table[i];
		if (midiLearn_spaceOf(e) == space && e->type == type &&
		    e->index == index && midiLearn_srcOf(e) == src)
			return (int16_t)i;
	}
	return -1;
}

/* Leave the gesture: LEDs back to their own state, display refreshed. */
static void midiLearn_end(void)
{
	midiLearn_state = MIDI_LEARN_IDLE;
	midiLearn_target = MIDI_TARGET_OFF;
	buttonHandler_setLearnLeds(0u);
	menu_learnDisplayChanged();
}

/* Contracts in MidiLearn.h. */
void midiLearn_clearAll(void)
{
	midiLearn_used = 0u;
}

uint8_t midiLearn_count(void)
{
	return midiLearn_used;
}

void midiLearn_arm(void)
{
	midiLearn_state = MIDI_LEARN_WAIT_TARGET;
	midiLearn_target = MIDI_TARGET_OFF;
	buttonHandler_setLearnLeds(1u);
	menu_learnDisplayChanged();
}

void midiLearn_cancel(void)
{
	if (midiLearn_state != MIDI_LEARN_IDLE)
		midiLearn_end();
}

uint8_t midiLearn_active(void)
{
	return (uint8_t)(midiLearn_state != MIDI_LEARN_IDLE);
}

uint8_t midiLearn_selectingTarget(void)
{
	return (uint8_t)(midiLearn_state == MIDI_LEARN_WAIT_TARGET ||
	                 midiLearn_state == MIDI_LEARN_WAIT_CC);
}

void midiLearn_selectTarget(uint16_t id)
{
	if (!midiLearn_selectingTarget() || id >= MIDI_TARGET_OFF)
		return;
	midiLearn_target = id;
	midiLearn_state = MIDI_LEARN_WAIT_CC;
	menu_learnDisplayChanged();
}

uint16_t midiLearn_displayTarget(void)
{
	return (midiLearn_state == MIDI_LEARN_WAIT_CC ||
	        midiLearn_state == MIDI_LEARN_FULL) ? midiLearn_target
	                                            : MIDI_TARGET_OFF;
}

uint8_t midiLearn_showingFull(void)
{
	return (uint8_t)(midiLearn_state == MIDI_LEARN_FULL);
}

void midiLearn_service(void)
{
	if (midiLearn_state == MIDI_LEARN_FULL &&
	    (uint16_t)(time_sysTick - midiLearn_fullUntil) < 32768u)
		midiLearn_end();
}

uint8_t midiLearn_captureCc(uint8_t channel, uint8_t cc)
{
	uint8_t global = midiIn_globalChannel();
	uint8_t onG = (uint8_t)(global != 0u && channel == global);
	uint8_t space, type, index;
	int16_t row;

	if (midiLearn_state != MIDI_LEARN_WAIT_CC || midiDefs_ccReserved(cc))
		return 0u;
	/* Valid on G, or on the target voice's own channel (voice targets). */
	if (!onG && !midiControl_targetOnChannel(midiLearn_target, channel))
		return 0u;
	if (!midiLearn_keyOf(midiLearn_target, &space, &type, &index))
		return 0u;
	row = midiLearn_find(space, type, index, MIDI_LEARN_SRC_CC);
	if (row < 0) {
		if (midiLearn_used >= MIDI_LEARN_MAX_ENTRIES) {
			/* F29: a full table refuses a new row; FUL shows briefly. */
			midiLearn_state = MIDI_LEARN_FULL;
			midiLearn_fullUntil =
			    (uint16_t)(time_sysTick + MIDI_LEARN_FULL_SHOW_MS);
			menu_learnDisplayChanged();
			return 1u;
		}
		row = (int16_t)midiLearn_used++;
	}
	midiLearn_table[row].ccKind =
	    (uint8_t)((cc & 0x7Fu) | (onG ? 0u : MIDI_LEARN_KIND_VOICE));
	midiLearn_table[row].space = (uint8_t)(space |
	    (MIDI_LEARN_SRC_CC << MIDI_LEARN_SRC_SHIFT) |
	    (MIDI_LEARN_MODE_ABS << MIDI_LEARN_MODE_SHIFT));
	midiLearn_table[row].type = type;
	midiLearn_table[row].index = index;
	/*
	 * S083 §16: every stored or replaced row rewrites midilrn.cfg in full
	 * (no debounce; the writer waits for an idle facade and writes once
	 * more if a row arrives during a write). A FUL refusal above writes
	 * nothing.
	 */
	filesystem_requestMidiLearnWrite();
	midiLearn_end();
	return 1u;
}

/*
 * Capture channel pressure for the waiting target (S096 P3 C4, plan §5.2).
 *
 * What: while a target waits (LRN), the first pressure message on G, or on
 * the target voice's own channel, stores or replaces the target's pressure
 * row with mode + (positive delta on top of the knob, the brief) and ends
 * the gesture; midilrn.cfg is rewritten as for a CC. A note (and its
 * velocity) never captures: notes go to MidiNotes, so "the first MIDI
 * message" rule is broken only for them, as the brief asks. A pressure
 * message for a target that cannot take a delta (an Effect or Scene target,
 * a selector) is not captured and Learn keeps waiting.
 * Inputs: channel 1..16. Output: 1 consumed (stored, or FUL refused).
 * Accessors: midiControl_handlePressure(). Affiliates: midiLearn_captureCc().
 */
uint8_t midiLearn_capturePressure(uint8_t channel)
{
	uint8_t global = midiIn_globalChannel();
	uint8_t onG = (uint8_t)(global != 0u && channel == global);
	uint8_t space, type, index;
	const kit_instrument_slot_t *instr;
	const ParamDescriptor *descriptor;
	int16_t row;

	if (midiLearn_state != MIDI_LEARN_WAIT_CC)
		return 0u;
	if (!onG && !midiControl_targetOnChannel(midiLearn_target, channel))
		return 0u;
	if (!midiLearn_keyOf(midiLearn_target, &space, &type, &index) ||
	    space >= MIDI_LEARN_SPACE_SCENE)
		return 0u;
	instr = scene_instrumentSlotConst(scene_getActiveIndex(), space);
	descriptor = instr ? instrumentManager_descriptor(instr->type, index) : 0;
	if (!descriptor ||
	    (descriptor->flags & INSTRUMENT_PARAM_FLAG_MODULATABLE) == 0u)
		return 0u;   /* a delta needs a modulatable voice row */
	row = midiLearn_find(space, type, index, MIDI_LEARN_SRC_PRESSURE);
	if (row < 0) {
		if (midiLearn_used >= MIDI_LEARN_MAX_ENTRIES) {
			midiLearn_state = MIDI_LEARN_FULL;
			midiLearn_fullUntil =
			    (uint16_t)(time_sysTick + MIDI_LEARN_FULL_SHOW_MS);
			menu_learnDisplayChanged();
			return 1u;
		}
		row = (int16_t)midiLearn_used++;
	}
	midiLearn_table[row].ccKind =
	    (uint8_t)(onG ? 0u : MIDI_LEARN_KIND_VOICE);
	midiLearn_table[row].space = (uint8_t)(space |
	    (MIDI_LEARN_SRC_PRESSURE << MIDI_LEARN_SRC_SHIFT) |
	    (MIDI_LEARN_MODE_PLUS << MIDI_LEARN_MODE_SHIFT));
	midiLearn_table[row].type = type;
	midiLearn_table[row].index = index;
	filesystem_requestMidiLearnWrite();
	midiLearn_end();
	return 1u;
}

/*
 * Apply every row of one source (S096 P3 C4).
 *
 * What: for each row whose source is `src` (and, for CC, whose CC matches),
 * whose channel kind matches and whose type is loaded (F51), and whose
 * voice owns the channel for a `v` row (F50):
 *   absolute -> the value as a menu edit (midiControl_writeTarget(): recorded
 *               and R12, as a CC);
 *   + / -    -> the voice target's delta overlay (+value or -value, 0 frees
 *               it; instrumentManager_deltaOverlaySet()), and with live
 *               record writing, the effective value recorded like a menu edit
 *               (liveRec_paramValueIn()) with the R12 override
 *               (seq_menuOverrideSet()) (decided Q11).
 * Inputs: channel 1..16, src (MIDI_LEARN_SRC_*), cc (a CC row's number, 0 for
 * the other sources), value7. Output: 1 when any row was applied (consumed).
 * Accessors: midiLearn_applyCc() (CC), midiControl_handlePressure(),
 * midiLearn_applyNote() (note and velocity rows).
 */
uint8_t midiLearn_applySource(uint8_t channel, uint8_t src, uint8_t cc,
                              uint8_t value7)
{
	uint8_t global = midiIn_globalChannel();
	uint8_t onG = (uint8_t)(global != 0u && channel == global);
	uint8_t consumed = 0u;
	uint8_t i;

	for (i = 0u; i < midiLearn_used; i++) {
		const midiLearn_entry_t *e = &midiLearn_table[i];
		uint8_t voiceKind =
		    (uint8_t)((e->ccKind & MIDI_LEARN_KIND_VOICE) != 0u);
		uint16_t id;
		uint8_t mode;

		if (midiLearn_srcOf(e) != src)
			continue;
		if (src == MIDI_LEARN_SRC_CC &&
		    (uint8_t)(e->ccKind & 0x7Fu) != cc)
			continue;
		if (voiceKind ? onG : !onG)
			continue;
		if (!midiLearn_typeMatches(e))
			continue;
		id = midiLearn_targetOf(e);
		/* F50: a `v` row moves only its own slot on a shared channel. */
		if (voiceKind && !midiControl_targetOnChannel(id, channel))
			continue;
		mode = midiLearn_modeOf(e);
		if (mode == MIDI_LEARN_MODE_DEFAULT)
			mode = (src == MIDI_LEARN_SRC_PRESSURE) ? MIDI_LEARN_MODE_PLUS
			                                        : MIDI_LEARN_MODE_ABS;
		if (mode == MIDI_LEARN_MODE_ABS) {
			consumed |= midiControl_writeTarget(id, value7);
		} else if (instrumentParam_isVoiceParameter(id)) {
			const uint8_t slot = instrumentParam_slot(id);
			const uint8_t local = instrumentParam_local(id);
			const int8_t delta = (mode == MIDI_LEARN_MODE_PLUS)
			    ? (int8_t)value7 : (int8_t)(-(int8_t)value7);
			const uint8_t eff = instrumentManager_deltaOverlaySet(slot, local,
			                                                       delta);

			/* Q11: with live record writing, the effective value is
			 * recorded like a menu edit and sets the R12 override. */
			if (liveRec_patternWriting() &&
			    liveRec_paramValueIn(id, eff))
				seq_menuOverrideSet(slot, local);
			consumed = 1u;
		}
	}
	return consumed;
}

uint8_t midiLearn_applyCc(uint8_t channel, uint8_t cc, uint8_t value7)
{
	return midiLearn_applySource(channel, MIDI_LEARN_SRC_CC, cc, value7);
}

/*
 * Note number and velocity as sources (S096 P3 C4; rows from midilrn.cfg
 * only, never captured). Deltas hold until the next note-on on the channel
 * (decided Q12: the platform's velocity rule); a note-off does not clear
 * them. Absolute rows write the value as a menu edit. Called for every
 * note-on before the note map; returns nothing (the note still plays).
 */
void midiLearn_applyNote(uint8_t channel, uint8_t note, uint8_t vel)
{
	(void)midiLearn_applySource(channel, MIDI_LEARN_SRC_NOTE, 0u, note);
	(void)midiLearn_applySource(channel, MIDI_LEARN_SRC_VELOCITY, 0u, vel);
}

uint8_t midiLearn_ccForTarget(uint16_t id, uint8_t *cc, uint8_t *voiceKind)
{
	uint8_t i;

	if (!cc || !voiceKind)
		return 0u;
	for (i = 0u; i < midiLearn_used; i++) {
		const midiLearn_entry_t *e = &midiLearn_table[i];

		/* S096 P3 (decided Q13): TX still uses the first CC row. */
		if (midiLearn_srcOf(e) != MIDI_LEARN_SRC_CC ||
		    midiLearn_targetOf(e) != id || !midiLearn_typeMatches(e))
			continue;
		*cc = (uint8_t)(e->ccKind & 0x7Fu);
		*voiceKind = (uint8_t)((e->ccKind & MIDI_LEARN_KIND_VOICE) != 0u);
		return 1u;
	}
	return 0u;
}

/*
 * Append up to maxLen characters of `text` at dst[pos] (no printf: the
 * firmware links no printf family, and S083 does not add it). Returns the
 * new position; stops one byte short of cap for the terminator.
 */
static uint16_t midiLearn_append(char *dst, uint16_t cap, uint16_t pos,
                                 const char *text, uint8_t maxLen)
{
	while (text && *text != '\0' && maxLen-- != 0u &&
	       (uint16_t)(pos + 1u) < cap)
		dst[pos++] = *text++;
	return pos;
}

/* Contract in MidiLearn.h. */
uint8_t midiLearn_formatFileLine(uint8_t index, char *dst, uint16_t cap)
{
	const midiLearn_entry_t *e;
	const char *space = 0;
	const char *type = 0;
	const char *key = 0;
	char slotText[3] = { 'v', '1', '\0' };
	char srcText[5];
	uint8_t cc;
	uint8_t src, mode;
	uint16_t pos = 0u;

	if (index >= midiLearn_used || !dst || cap < 32u)
		return 0u;
	e = &midiLearn_table[index];
	cc = (uint8_t)(e->ccKind & 0x7Fu);
	src = midiLearn_srcOf(e);
	mode = midiLearn_modeOf(e);
	if (src == MIDI_LEARN_SRC_CC) {
		srcText[0] = (char)('0' + cc / 100u);
		srcText[1] = (char)('0' + (cc / 10u) % 10u);
		srcText[2] = (char)('0' + cc % 10u);
		srcText[3] = '\0';
	} else {
		const char *name = (src == MIDI_LEARN_SRC_PRESSURE) ? "prs"
		                 : ((src == MIDI_LEARN_SRC_NOTE) ? "nte" : "vel");
		const char sign = (mode == MIDI_LEARN_MODE_PLUS) ? '+'
		                : ((mode == MIDI_LEARN_MODE_MINUS) ? '-' : '=');

		srcText[0] = name[0];
		srcText[1] = name[1];
		srcText[2] = name[2];
		srcText[3] = sign;
		srcText[4] = '\0';
	}
	if (midiLearn_spaceOf(e) < MIDI_LEARN_SPACE_SCENE) {
		const instrument_registry_entry_t *reg =
		    instrumentManager_registryEntry((instrument_type_t)e->type);
		const ParamDescriptor *d =
		    instrumentManager_descriptor((instrument_type_t)e->type, e->index);

		slotText[1] = (char)('1' + midiLearn_spaceOf(e));
		space = slotText;
		type = reg ? reg->type_text : 0;
		key = d ? d->file_key : 0;
	} else if (midiLearn_spaceOf(e) == MIDI_LEARN_SPACE_EFFECT) {
		const effect_param_descriptor_t *d =
		    effects_descriptor((effect_type_id_t)e->type, e->index);

		space = "fx";
		type = effects_typeToken((effect_type_id_t)e->type);
		key = d ? d->base.file_key : 0;
	} else {
		const scene_mod_target_descriptor_t *d = sceneModTarget_descriptor(
		    sceneModTarget_idFromIndex(e->index));

		space = "scn";
		key = d ? d->short_name : 0;
	}
	if (key && (type || midiLearn_spaceOf(e) == MIDI_LEARN_SPACE_SCENE)) {
		pos = midiLearn_append(dst, cap, pos, "learn=", 6u);
		pos = midiLearn_append(dst, cap, pos, srcText, 4u);
		pos = midiLearn_append(dst, cap, pos,
		                       (e->ccKind & MIDI_LEARN_KIND_VOICE) ? ",v," : ",g,",
		                       3u);
		pos = midiLearn_append(dst, cap, pos, space, 3u);
		if (type) {
			pos = midiLearn_append(dst, cap, pos, ",", 1u);
			pos = midiLearn_append(dst, cap, pos, type, 3u);
		}
		pos = midiLearn_append(dst, cap, pos, ",", 1u);
		pos = midiLearn_append(dst, cap, pos, key, 40u);
		if ((uint16_t)(pos + 2u) <= cap && key[0] != '\0') {
			dst[pos++] = '\n';
			dst[pos] = '\0';
			/*
			 * The line LENGTH: filesystem_writeTextLine() writes exactly
			 * this many bytes (S083 §16 fix; returning 1 wrote only "l").
			 */
			return (uint8_t)pos;
		}
	}
	/* Unresolvable or too long: a comment line keeps `lines=` exact. */
	pos = midiLearn_append(dst, cap, 0u, "# learn row unresolved\n", 24u);
	dst[pos] = '\0';
	return (uint8_t)pos;
}

/*
 * S096 P3: the source token (plan §5.2).
 *   CCC     a CC 3..119, not reserved: absolute (the S083 form, unchanged);
 *   prs+ prs- prs=   channel pressure: + delta, - delta, absolute;
 *   nte+ nte- nte=   the note number of every note-on on the channel;
 *   vel+ vel- vel=   its velocity.
 * Deltas are voice rows of modulatable parameters only; an fx or scn row with
 * a delta mode, or a selector, is skipped (the file still loads). The writer
 * emits these canonical forms; a CC row's line is byte-identical to S083's.
 * Split "<source>,k,space,type,key" (Scene rows: "<source>,k,scn,key") into
 * fields in a local copy; resolve names to indices once (F34). Duplicate
 * keys: the later line replaces the earlier row (one parameter per source).
 */
uint8_t midiLearn_parseFileValue(const char *value)
{
	char buf[64];
	char *field[5];
	uint8_t count = 0u;
	uint16_t cc = 0u;
	uint8_t voiceKind;
	uint8_t space, type = MIDI_LEARN_TYPE_NONE, index = 0u;
	uint8_t src = MIDI_LEARN_SRC_CC;
	uint8_t mode = MIDI_LEARN_MODE_ABS;
	uint8_t i;
	int16_t row;
	char *p;

	if (!value || strlen(value) >= sizeof(buf))
		return 0u;
	strcpy(buf, value);
	for (p = buf; count < 5u; ) {
		field[count++] = p;
		p = strchr(p, ',');
		if (!p)
			break;
		*p++ = '\0';
	}
	if (count < 4u)
		return 0u;
	if (field[0][0] >= '0' && field[0][0] <= '9') {
		for (i = 0u; field[0][i] != '\0'; i++) {
			if (field[0][i] < '0' || field[0][i] > '9' || i > 2u)
				return 0u;
			cc = (uint16_t)(cc * 10u + (uint16_t)(field[0][i] - '0'));
		}
		if (i == 0u || cc > 127u || midiDefs_ccReserved((uint8_t)cc))
			return 0u;
	} else {
		const char sign = (field[0][3] == '+') ? '+'
		                : ((field[0][3] == '-') ? '-' : '=');

		if (field[0][4] != '\0' ||
		    !((field[0][0] == 'p' && field[0][1] == 'r' && field[0][2] == 's') ||
		      (field[0][0] == 'n' && field[0][1] == 't' && field[0][2] == 'e') ||
		      (field[0][0] == 'v' && field[0][1] == 'e' && field[0][2] == 'l')) ||
		    (field[0][3] != '+' && field[0][3] != '-' && field[0][3] != '='))
			return 0u;
		src = (field[0][0] == 'p') ? MIDI_LEARN_SRC_PRESSURE
		    : ((field[0][0] == 'n') ? MIDI_LEARN_SRC_NOTE
		                             : MIDI_LEARN_SRC_VELOCITY);
		mode = (sign == '+') ? MIDI_LEARN_MODE_PLUS
		     : ((sign == '-') ? MIDI_LEARN_MODE_MINUS : MIDI_LEARN_MODE_ABS);
	}
	if (strcmp(field[1], "g") == 0) voiceKind = 0u;
	else if (strcmp(field[1], "v") == 0) voiceKind = 1u;
	else return 0u;
	if (field[2][0] == 'v' && field[2][1] >= '1' && field[2][1] <= '6' &&
	    field[2][2] == '\0' && count == 5u) {
		instrument_type_t t = instrumentManager_typeFromText(field[3]);

		if (t == INSTRUMENT_TYPE_UNKNOWN ||
		    !instrumentManager_descriptorIndexByKey(t, field[4], &index))
			return 0u;
		space = (uint8_t)(field[2][1] - '1');
		type = (uint8_t)t;
	} else if (strcmp(field[2], "fx") == 0 && count == 5u) {
		effect_type_id_t t;

		if (strlen(field[3]) != 3u || voiceKind ||
		    !effects_typeFromToken(field[3], &t) ||
		    !effects_descriptorByKey(t, field[4], &index) ||
		    index >= EFFECT_TARGET_PATTERN_LOCAL_LIMIT)
			return 0u;
		space = MIDI_LEARN_SPACE_EFFECT;
		type = (uint8_t)t;
	} else if (strcmp(field[2], "scn") == 0 && count == 4u) {
		uint8_t n = sceneModTarget_count();

		for (i = 0u; i < n; i++) {
			const scene_mod_target_descriptor_t *d =
			    sceneModTarget_descriptor(sceneModTarget_idFromIndex(i));
			if (d && d->kind != SCENE_MOD_TARGET_KIND_RETIRED &&
			    d->short_name && strcmp(d->short_name, field[3]) == 0)
				break;
		}
		if (i >= n)
			return 0u;
		/* kind v only for per-voice Scene targets (not fxm). */
		if (voiceKind && sceneModTarget_descriptor(
		        sceneModTarget_idFromIndex(i))->kind ==
		        SCENE_MOD_TARGET_KIND_EFFECT_MORPH)
			return 0u;
		space = MIDI_LEARN_SPACE_SCENE;
		index = i;
	} else {
		return 0u;
	}
	/* S096 P3: a delta mode is a voice row of a modulatable parameter only
	 * (decided Q11: no Effect or Scene deltas). */
	if (mode != MIDI_LEARN_MODE_ABS) {
		const ParamDescriptor *d = instrumentManager_descriptor(
		    (instrument_type_t)type, index);

		if (space >= MIDI_LEARN_SPACE_SCENE || !d ||
		    (d->flags & INSTRUMENT_PARAM_FLAG_MODULATABLE) == 0u)
			return 0u;
	}
	row = midiLearn_find(space, type, index, src);
	if (row < 0) {
		if (midiLearn_used >= MIDI_LEARN_MAX_ENTRIES)
			return 0u;
		row = (int16_t)midiLearn_used++;
	}
	midiLearn_table[row].ccKind =
	    (uint8_t)((src == MIDI_LEARN_SRC_CC ? cc : 0u) |
	              (voiceKind ? MIDI_LEARN_KIND_VOICE : 0u));
	midiLearn_table[row].space = (uint8_t)(space |
	    (src << MIDI_LEARN_SRC_SHIFT) | (mode << MIDI_LEARN_MODE_SHIFT));
	midiLearn_table[row].type = type;
	midiLearn_table[row].index = index;
	return 1u;
}
