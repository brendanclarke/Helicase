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
#include "MidiNoteModes.h"   /* S096 P3: every special note mode */
#include "MidiLearn.h"       /* S096 P3: note / velocity Learn rows */

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
	uint8_t sub;
	uint8_t overlay;

	/* S096 P3: note-number and velocity Learn rows apply before the note map,
	 * so an unmapped note still drives them; the note itself is not consumed. */
	if (on != 0u)
		midiLearn_applyNote(channel, msg->data1, msg->data2);

	/*
	 * Resolve on the active Scene (Scene 17 while it is active), read per
	 * message (S082 R14). An unmapped note is not consumed.
	 */
	midiNoteMap_load(scene_getActiveIndex(), &map);
	if (!midiNoteMap_resolve(&map, channel, msg->data1, &track, &internal, &sub))
		return 0u;
	if (!on) {
		/* Note-off ends a MIDI-held roll; single triggers ignore it.
		 * S096 P3: the note-mode module also sees it (the paraphonic release). */
		buttonHandler_overlayMidiNoteOff(track, msg->data1);
		(void)midiNoteMode_noteOff(track, channel, internal, sub);
		return 1u;
	}
	if (seq_isTrackMuted(track))
		return 1u;	/* resolved, so consumed; a muted track is silent */
	/* The one overlay state, set by BAR `<`/`>` or CC2 (any mode). */
	overlay = buttonHandler_overlayActive();
	if (overlay != 0u) {
		buttonHandler_overlayMidiNoteOn(track, overlay, internal,
		                                msg->data1,
		                                midiNotes_ageUs(msg->stamp), sub);
		return 1u;
	}
	/*
	 * S096 P3: every resolved note-on that the overlay does not take goes to
	 * the note-mode module (MidiNoteModes.c), which plays it by the track's
	 * mode: the S083 single trigger (normal), one Tetrakt layer (layer4) or
	 * the paraphonic Colloid (para). sub is the note's place in a wide
	 * track's group of four (MidiNoteMap). Note-offs reach the module too
	 * (the paraphonic release).
	 */
	return midiNoteMode_noteOn(track, channel, internal, sub, msg->data1,
	                           msg->data2, midiNotes_ageUs(msg->stamp));
}
