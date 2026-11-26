/*
 * MidiControl.c - received control changes (S083).
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

#include "MidiControl.h"
#include "MidiIn.h"
#include "MidiNotes.h"
#include "MidiNoteMap.h"
#include "menu.h"
#include "SceneData.h"
#include "SceneModTargets.h"
#include "InstrumentManager.h"
#include "EffectsManager.h"
#include "PatternData.h"
#include "MidiLearn.h"
#include "config.h"

/*
 * NRPN receive context (plan 3.5, F26).
 *
 * key: bit 7 in use, bit 6 MSB seen, bit 5 LSB seen, bit 4 RPN selected
 * (data entry is then ignored), bits 0..3 channel - 1. msb/lsb: the
 * selected number (CC99/CC98). One table per port (index = MidiSource).
 * A context is allocated by CC98..101 on G or a voice channel: the first
 * free one, else the slot (channel - 1) & 7 is reused. RAM: 48 B SRAM1.
 */
typedef struct {
	uint8_t key;
	uint8_t msb;
	uint8_t lsb;
} midiControl_nrpn_t;

#define MIDI_NRPN_USED     0x80u
#define MIDI_NRPN_MSB      0x40u
#define MIDI_NRPN_LSB      0x20u
#define MIDI_NRPN_RPN      0x10u
#define MIDI_NRPN_CHANNEL  0x0Fu

static midiControl_nrpn_t midiControl_nrpn[2][MIDI_NRPN_CONTEXTS];

static midiControl_nrpn_t *midiControl_context(uint8_t port, uint8_t channel,
                                               uint8_t allocate)
{
	midiControl_nrpn_t *table = midiControl_nrpn[port & 1u];
	uint8_t i;

	for (i = 0u; i < MIDI_NRPN_CONTEXTS; i++) {
		if ((table[i].key & MIDI_NRPN_USED) &&
		    (table[i].key & MIDI_NRPN_CHANNEL) == (uint8_t)(channel - 1u))
			return &table[i];
	}
	if (!allocate)
		return 0;
	for (i = 0u; i < MIDI_NRPN_CONTEXTS; i++) {
		if ((table[i].key & MIDI_NRPN_USED) == 0u)
			break;
	}
	if (i >= MIDI_NRPN_CONTEXTS)
		i = (uint8_t)((channel - 1u) % MIDI_NRPN_CONTEXTS);
	table[i].key = (uint8_t)(MIDI_NRPN_USED | ((channel - 1u) & 0x0Fu));
	table[i].msb = 0u;
	table[i].lsb = 0u;
	return &table[i];
}

/* Contract in MidiControl.h. */
uint8_t midiControl_voiceSlotsOnChannel(uint8_t channel)
{
	midiNoteMap_settings_t map;
	uint8_t tracks;
	uint8_t slots = 0u;
	uint8_t t;

	/*
	 * S104 P1, Omni: a message on O addresses only the selected track's
	 * slot (track 7 shares slot 6), whatever G and the track channels are.
	 */
	{
		const uint8_t omni = midiIn_omniTrack(channel);

		if (omni != 0xFFu)
			return (uint8_t)(1u << ((omni >= 6u) ? 5u : omni));
	}
	if (channel == 0u || channel == midiIn_globalChannel())
		return 0u;
	midiNoteMap_load(scene_getActiveIndex(), &map);
	tracks = midiNoteMap_tracksOnChannel(&map, channel);
	for (t = 0u; t < 7u; t++) {
		if (tracks & (1u << t))
			slots |= (uint8_t)(1u << ((t >= 6u) ? 5u : t));
	}
	return slots;
}

/* Contract in MidiControl.h. */
uint8_t midiControl_targetOnChannel(uint16_t id, uint8_t channel)
{
	uint8_t global = midiIn_globalChannel();
	uint8_t slots;
	/* S104 P1: O is never G (O wins when equal). */
	const uint8_t omni = midiIn_omniTrack(channel);

	if (omni == 0xFFu && global != 0u && channel == global)
		return 1u;
	slots = midiControl_voiceSlotsOnChannel(channel);
	if (slots == 0u)
		return 0u;
	if (instrumentParam_isVoiceParameter(id))
		return (uint8_t)((slots >> instrumentParam_slot(id)) & 1u);
	if (sceneModTarget_isSceneTarget(id)) {
		const scene_mod_target_descriptor_t *d = sceneModTarget_descriptor(id);

		if (!d)
			return 0u;
		switch (d->kind) {
		case SCENE_MOD_TARGET_KIND_VOICE_MORPH:
		case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
		case SCENE_MOD_TARGET_KIND_FX_SEND:
			return (uint8_t)((slots >> d->voice_slot) & 1u);
		case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
			return (uint8_t)((slots >> 5u) & 1u);
		case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
		case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
		case SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE: {
			midiNoteMap_settings_t map;

			/* S104 P1: on O, only the selected track's own timing. */
			if (omni != 0xFFu)
				return (uint8_t)(d->voice_slot == omni);
			midiNoteMap_load(scene_getActiveIndex(), &map);
			return (uint8_t)(midiNoteMap_effectiveChannel(&map,
			                 d->voice_slot) == channel);
		}
		default:
			return 0u;	/* fxm and unused: G only */
		}
	}
	return 0u;	/* Effect rows: G only */
}

/* Contract in MidiControl.h. */
uint8_t midiControl_writeTarget(uint16_t id, uint8_t value7)
{
	return menu_commitTargetEdit(id, value7, MIDI_ORIGIN_MIDI);
}

/*
 * Received channel pressure (S096 P3 C4, plan §5.2).
 *
 * What: a waiting Learn takes it first (a pressure row, mode +); otherwise
 * every matching pressure row is applied (absolute: a menu edit as a CC;
 * delta: the non-destructive overlay). Returns 1 when consumed (a capture or
 * any row applied), else 0 (forwarded per MRT).
 * Inputs: msg (status 0xDn, data1 the pressure 0..127).
 * Accessors: midiIn_dispatch(). Affiliates: midiLearn_capturePressure(),
 * midiLearn_applySource().
 */
uint8_t midiControl_handlePressure(const MidiMsg *msg)
{
	const uint8_t channel = (uint8_t)((msg->status & 0x0Fu) + 1u);

	if (midiLearn_capturePressure(channel))
		return 1u;
	return midiLearn_applySource(channel, MIDI_LEARN_SRC_PRESSURE, 0u,
	                             msg->data1);
}

/*
 * Effective channel of a target's voice in the active Scene (S083 Q8).
 * What: voice IDs and the per-voice Scene targets -> the effective channel
 * of the voice's track (slot 5 -> track 6 (T6)); track timing targets ->
 * that track's effective channel; Effect and fxm -> 0. Output: 1..16 or 0.
 * Accessors: MidiOut parameter TX (learned kind `v`; NRPN with G off).
 */
uint8_t midiControl_targetChannel(uint16_t id)
{
	midiNoteMap_settings_t map;
	uint8_t track = 0xFFu;

	if (instrumentParam_isVoiceParameter(id)) {
		track = instrumentParam_slot(id);
	} else if (sceneModTarget_isSceneTarget(id)) {
		const scene_mod_target_descriptor_t *d = sceneModTarget_descriptor(id);

		if (!d || d->kind == SCENE_MOD_TARGET_KIND_EFFECT_MORPH)
			return 0u;
		track = (d->kind == SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY)
		        ? 6u : d->voice_slot;
	}
	if (track >= 7u)
		return 0u;
	midiNoteMap_load(scene_getActiveIndex(), &map);
	return midiNoteMap_effectiveChannel(&map, track);
}

/* Contract in MidiControl.h. */
uint8_t midiControl_targetValue7(uint8_t scene_index, uint16_t id)
{
	if (instrumentParam_isVoiceParameter(id)) {
		uint8_t slot = instrumentParam_slot(id);
		uint8_t local = instrumentParam_local(id);
		const kit_instrument_slot_t *instr =
		    scene_instrumentSlotConst(scene_index, slot);

		if (instr && local < INSTRUMENT_PARAM_COUNT) {
			/*
			 * F-4: target-selection descriptors (LFO/velocity targets)
			 * hold byte tokens up to 0xFF; a MIDI data byte is 7-bit, so
			 * the value is clamped to 127 (a byte >= 0x80 on the wire would
			 * be a status byte). Every other descriptor is already 0..127.
			 */
			uint8_t v = instr->parameter_images.instrument_parameters[local];

			return (v > 127u) ? 127u : v;
		}
		return 0u;
	}
	if (sceneModTarget_isSceneTarget(id)) {
		const scene_mod_target_descriptor_t *d =
		    sceneModTarget_descriptor(id);
		uint8_t slot;

		if (!d)
			return 0u;
		slot = d->voice_slot;
		switch (d->kind) {
		case SCENE_MOD_TARGET_KIND_VOICE_MORPH: {
			uint8_t m = scene_getVoiceMorphAmount(scene_index, slot);

			return (uint8_t)((m == 255u) ? 127u : (m / 2u));
		}
		case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
			return scene_getVoiceAudioOut(scene_index, slot);
		case SCENE_MOD_TARGET_KIND_FX_SEND:
			return scene_getVoiceFxSendAmount(scene_index, slot);
		case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
			/* S104 P1: layer 0 is the generic `7dc` value. */
			return scene_getSlot6Track7Decay(scene_index, 0u);
		case SCENE_MOD_TARGET_KIND_EFFECT_MORPH: {
			uint8_t m = scene_getEffectMorphAmount(scene_index);

			return (uint8_t)((m == 255u) ? 127u : (m / 2u));
		}
		case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
		case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
		case SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE: {
			const pat_scene_region_t *r = pat_sceneRegion(scene_index);

			if (!r || slot >= NUM_TRACKS)
				return 0u;
			switch (d->kind) {
			case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
				return (uint8_t)((r->track_length[slot] > 127u)
				                 ? 127u : r->track_length[slot]);
			case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
				return r->track_scale[slot];
			default:
				return r->track_shuffle[slot];
			}
		}
		default:
			return 0u;
		}
	}
	if (effectTarget_isEffectId(id)) {
		uint8_t local = effectTarget_local(id);

		return effects_storeValue7(scene_index, local,
		    effects_getParameter(scene_index, local, EFFECT_IMAGE_NORMAL));
	}
	return 0u;
}

/* Contract in MidiControl.h. */
uint8_t midiControl_handleCc(const MidiMsg *msg)
{
	const uint8_t channel = (uint8_t)((msg->status & 0x0Fu) + 1u);
	const uint8_t cc = msg->data1;
	const uint8_t value = msg->data2;
	const uint8_t global = midiIn_globalChannel();
	/* S104 P1: a message on the Omni channel is never a G message. */
	const uint8_t onG = (uint8_t)(global != 0u && channel == global &&
	                              midiIn_omniTrack(channel) == 0xFFu);
	const uint8_t voiceSlots = midiControl_voiceSlotsOnChannel(channel);
	const uint8_t port = (uint8_t)msg->bits.source;
	midiControl_nrpn_t *ctx;

	switch (cc) {
	case CC_NOTE_ACTION:
		/*
		 * CC2 on G only (user 2026-10-22): sets the PERF BAR overlay
		 * itself (0 off, 1 `<`, 2 `>`), like a BAR press, through
		 * midiNotes_setNoteAction() -> buttonHandler_setOverlay(). On any
		 * other channel it is not consumed (forwarded).
		 */
		return onG ? midiNotes_setNoteAction(value) : 0u;
	case CC_MOD_WHEEL:
		/* C7: G = global Morph (never recorded, G-1); voice channel =
		** that channel's voices' Morph, recorded as individual edits. */
		if (onG) {
			menu_commitGlobalMorphFromMidi(value);
			return 1u;
		}
		if (voiceSlots != 0u) {
			uint8_t slot;

			for (slot = 0u; slot < 6u; slot++) {
				if (voiceSlots & (1u << slot))
					(void)midiControl_writeTarget(
					    sceneModTarget_voiceMorphId(slot), value);
			}
			return 1u;
		}
		return 0u;
	case CC_NRPN_MSB:
	case CC_NRPN_LSB:
	case CC_RPN_MSB:
	case CC_RPN_LSB:
	case CC_DATA_ENTRY_LSB:
		if (!onG && voiceSlots == 0u)
			return 0u;
		if (cc == CC_DATA_ENTRY_LSB)
			return 1u;	/* F26: ignored, consumed */
		ctx = midiControl_context(port, channel, 1u);
		if (cc == CC_NRPN_MSB) {
			ctx->msb = value;
			ctx->key = (uint8_t)((ctx->key | MIDI_NRPN_MSB) & ~MIDI_NRPN_RPN);
		} else if (cc == CC_NRPN_LSB) {
			ctx->lsb = value;
			ctx->key = (uint8_t)((ctx->key | MIDI_NRPN_LSB) & ~MIDI_NRPN_RPN);
		} else {
			/* Any RPN selection, incl. the RPN null (127/127), deselects
			** the NRPN: data entry is ignored until a new NRPN number. */
			ctx->key = (uint8_t)((ctx->key | MIDI_NRPN_RPN) &
			                     ~(MIDI_NRPN_MSB | MIDI_NRPN_LSB));
		}
		return 1u;
	case CC_DATA_ENTRY_MSB:
	case CC_DATA_INCREMENT:
	case CC_DATA_DECREMENT: {
		uint16_t id;
		uint8_t v = value;

		ctx = midiControl_context(port, channel, 0u);
		if (!ctx || (ctx->key & MIDI_NRPN_RPN) ||
		    (ctx->key & (MIDI_NRPN_MSB | MIDI_NRPN_LSB)) !=
		        (MIDI_NRPN_MSB | MIDI_NRPN_LSB))
			return 0u;
		id = (uint16_t)(((uint16_t)ctx->msb << 7) | ctx->lsb);
		if (id >= MIDI_TARGET_OFF || !midiControl_targetOnChannel(id, channel))
			return 0u;	/* C1/C-7: unused or not here -> forwarded */
		if (cc != CC_DATA_ENTRY_MSB) {
			v = midiControl_targetValue7(scene_getActiveIndex(), id);
			if (cc == CC_DATA_INCREMENT && v < 127u) v++;
			if (cc == CC_DATA_DECREMENT && v > 0u) v--;
		}
		return midiControl_writeTarget(id, v);
	}
	default:
		break;
	}
	/* Learn (plan 3.6): a waiting capture takes the CC first; otherwise
	** every matching row is written (macros). Non-reserved CCs only. */
	if (midiDefs_ccReserved(cc))
		return 0u;
	if (midiLearn_captureCc(channel, cc))
		return 1u;
	return midiLearn_applyCc(channel, cc, value);
}
