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
#include <string.h>          /* S104 P1: memset() of the Omni held map */

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

/*
 * Omni held-note latch (S104 P1; +17 B SRAM1, approved).
 *
 * What: midiNotes_omniHeld is a 128-bit map of the MIDI notes held on the
 * Omni channel; midiNotes_omniLatch is the track they play (0xFF = none).
 * The first Omni note-on with no Omni note held latches the selected
 * track; every later note-on and every note-off goes to the latched track
 * until the map is empty again. Why: a note-off must reach the track its
 * note-on reached. Without the latch, a track change while a paraphonic
 * Colloid (MNM_PARA) or a gated voice holds a note sends the note-off to
 * the new track and leaves the old one hanging. A bitmap, not a count, so a
 * repeated note-on or a lost note-off cannot desynchronise it.
 * Inputs: Omni note-on/off in midiNotes_handle(). Outputs: the target track.
 * Lifetime: runtime only. Affiliates: midiIn_omniTrack(),
 * midiNotes_omniReleaseAll() (O change).
 */
static uint32_t midiNotes_omniHeld[4];
static uint8_t  midiNotes_omniLatch = 0xFFu;

static uint8_t midiNotes_omniAnyHeld(void)
{
	return (uint8_t)((midiNotes_omniHeld[0] | midiNotes_omniHeld[1] |
	                  midiNotes_omniHeld[2] | midiNotes_omniHeld[3]) != 0u);
}

/*
 * The one-track map of an Omni message (S104 P1).
 *
 * What: keeps the Scene's note roots and wide (Tetrakt) bits, puts `track`
 * alone on `channel` (every other track `off`) and sets G off, so
 * midiNoteMap_resolve() applies the own-channel rules (rule 1 from the
 * track's `nte` root, else rule 2's split, which a lone track owns
 * entirely) and never G's stripe, even when O equals G.
 * Inputs: a loaded map, track 0..6, channel O. Output: *map modified.
 * Accessors: midiNotes_handle(), midiNotes_omniReleaseAll().
 */
static void midiNotes_omniMap(midiNoteMap_settings_t *map, uint8_t track,
                              uint8_t channel)
{
	uint8_t t;

	map->global = 0u;
	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++)
		map->channel[t] = (t == track) ? channel : MIDI_NOTEMAP_CHANNEL_OFF;
}

/* Contract in MidiNotes.h (S104 P1). */
void midiNotes_omniReleaseAll(void)
{
	const uint8_t o = midiIn_omniChannel();
	midiNoteMap_settings_t map;
	uint8_t n;

	if (midiNotes_omniLatch < MIDI_NOTEMAP_TRACKS && o != 0u) {
		midiNoteMap_load(scene_getActiveIndex(), &map);
		midiNotes_omniMap(&map, midiNotes_omniLatch, o);
		for (n = 0u; n < 128u; n++) {
			uint8_t track, internal, sub;

			if ((midiNotes_omniHeld[n >> 5] & (1u << (n & 31u))) == 0u)
				continue;
			if (!midiNoteMap_resolve(&map, o, n, &track, &internal, &sub))
				continue;
			buttonHandler_overlayMidiNoteOff(track, n);
			(void)midiNoteMode_noteOff(track, o, internal, sub);
		}
	}
	memset(midiNotes_omniHeld, 0, sizeof(midiNotes_omniHeld));
	midiNotes_omniLatch = 0xFFu;
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
	uint8_t omni;

	/* S096 P3: note-number and velocity Learn rows apply before the note map,
	 * so an unmapped note still drives them; the note itself is not consumed. */
	if (on != 0u)
		midiLearn_applyNote(channel, msg->data1, msg->data2);

	/*
	 * Resolve on the active Scene (Scene 17 while it is active), read per
	 * message (S082 R14). An unmapped note is not consumed.
	 * S104 P1, Omni: a note on O resolves through the one-track map of the
	 * latched track (the selected track when no Omni note is held), and the
	 * latch map is updated only for a note that resolved.
	 */
	midiNoteMap_load(scene_getActiveIndex(), &map);
	omni = midiIn_omniTrack(channel);
	if (omni != 0xFFu) {
		if (midiNotes_omniLatch < MIDI_NOTEMAP_TRACKS)
			omni = midiNotes_omniLatch;
		midiNotes_omniMap(&map, omni, channel);
	}
	if (!midiNoteMap_resolve(&map, channel, msg->data1, &track, &internal, &sub)) {
		if (omni != 0xFFu && !on)
			midiNotes_omniHeld[msg->data1 >> 5] &= ~(1u << (msg->data1 & 31u));
		return 0u;
	}
	if (omni != 0xFFu) {
		if (on) {
			midiNotes_omniHeld[msg->data1 >> 5] |= (1u << (msg->data1 & 31u));
			midiNotes_omniLatch = omni;
		} else {
			midiNotes_omniHeld[msg->data1 >> 5] &= ~(1u << (msg->data1 & 31u));
			if (!midiNotes_omniAnyHeld())
				midiNotes_omniLatch = 0xFFu;
		}
	}
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
