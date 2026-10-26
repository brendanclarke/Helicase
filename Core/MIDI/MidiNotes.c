/*
 * MidiNotes.c - what a received note does (S083).
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

#include "MidiNotes.h"
#include "MidiNoteMap.h"
#include "MidiIn.h"
#include "buttonHandler.h"
#include "sequencer.h"
#include "LiveRecord.h"
#include "SceneData.h"
#include "timebase.h"
#include "config.h"

/*
 * Arrival age of a message in us (S083 Q6).
 * What: now - stamp in the 16-bit us domain; ages above
 * MIDI_ARRIVAL_MAX_AGE_US (or a stale stamp after a long stall) return 0,
 * which means "use processing time". Inputs: 16-bit stamp. Output: age.
 */
static uint16_t midiNotes_ageUs(uint16_t stamp)
{
	uint16_t age = (uint16_t)((uint16_t)timebase_tim2Now() - stamp);

	return (age > MIDI_ARRIVAL_MAX_AGE_US) ? 0u : age;
}

/* Contract in MidiNotes.h. */
uint8_t midiNotes_setNoteAction(uint8_t value)
{
	if (value > 2u)
		return 0u;
	/* The PERF BAR overlay itself (state, LEDs, held entries). */
	buttonHandler_setOverlay(value);
	return 1u;
}

/* Contract in MidiNotes.h. */
uint8_t midiNotes_handle(const MidiMsg *msg)
{
	midiNoteMap_settings_t map;
	uint8_t channel = (uint8_t)((msg->status & 0x0Fu) + 1u);
	uint8_t on = (uint8_t)((msg->status & 0xF0u) == NOTE_ON &&
	                       msg->data2 != 0u);
	uint8_t track;
	uint8_t internal;
	uint8_t overlay;

	/*
	 * Resolve on the active Scene (Scene 17 while it is active), read per
	 * message (S082 R14). An unmapped note is not consumed.
	 */
	midiNoteMap_load(scene_getActiveIndex(), &map);
	if (!midiNoteMap_resolve(&map, channel, msg->data1, &track, &internal))
		return 0u;
	if (!on) {
		/* Note-off ends a MIDI-held roll; single triggers ignore it. */
		buttonHandler_overlayMidiNoteOff(track, msg->data1);
		return 1u;
	}
	if (seq_isTrackMuted(track))
		return 1u;	/* resolved, so consumed; a muted track is silent */
	/* The one overlay state, set by BAR `<`/`>` or CC2 (any mode). */
	overlay = buttonHandler_overlayActive();
	if (overlay != 0u) {
		buttonHandler_overlayMidiNoteOn(track, overlay, internal,
		                                msg->data1,
		                                midiNotes_ageUs(msg->stamp));
		return 1u;
	}
	/*
	 * Single trigger (N6): one resetting trigger at the note's velocity and
	 * resolved pitch, through the sequencer's trigger path (trigger jack and
	 * TX N as for any LXR trigger, C-3), reported to LiveRecord with the
	 * arrival age. note_is_default: a default-pitch hit records no note
	 * special (S081 Q12 rule, as for the overlay `def`).
	 */
	seq_triggerVoice(track, msg->data2, internal, 1u);
	{
		liveRec_trigger_t lr;

		lr.track = track;
		lr.note = internal;
		lr.velocity = msg->data2;
		lr.kind = LIVEREC_HIT_SINGLE;
		lr.roll = 0u;
		lr.rhythm = 0u;
		lr.source = LIVEREC_SRC_MIDI;
		lr.note_is_default =
		    (uint8_t)(internal == MIDI_NOTEMAP_ROOT_INTERNAL);
		lr.age_us = midiNotes_ageUs(msg->stamp);
		liveRec_triggerIn(&lr);
	}
	return 1u;
}
