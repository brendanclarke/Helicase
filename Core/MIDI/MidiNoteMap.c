/*
 * MidiNoteMap.c - note <-> (track, internal note) for one Scene (S083).
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

#include "MidiNoteMap.h"
#ifndef MIDI_NOTEMAP_HOST_TEST
#include "MidiIn.h"
#include "MidiPitch.h"
#include "SceneData.h"
#include "MidiNoteModes.h"   /* S096 P3: the wide (Tetrakt) track mask */
_Static_assert(MIDI_NOTEMAP_ROOT_INTERNAL == MIDI_DEFAULT_TRIGGER_NOTE,
               "note map root must be the default trigger note");
_Static_assert(MIDI_NOTEMAP_CHANNEL_OFF == MIDI_TRACK_CHANNEL_OFF &&
               MIDI_NOTEMAP_NOTE_OFF == MIDI_TRACK_NOTE_OFF,
               "note map off values must match MidiDefs.h");
#endif

/* Contracts in MidiNoteMap.h. */
uint8_t midiNoteMap_effectiveChannel(const midiNoteMap_settings_t *s,
                                     uint8_t track)
{
	uint8_t ch;

	if (!s || track >= MIDI_NOTEMAP_TRACKS)
		return 0u;
	ch = s->channel[track];
	/* S083 follow-up: `off` = no channel (not G); other bytes above 16 are
	** treated as `glb`. */
	if (ch == MIDI_NOTEMAP_CHANNEL_OFF)
		return 0u;
	if (ch > 16u)
		ch = 0u;
	return (ch != 0u) ? ch : ((s->global <= 16u) ? s->global : 0u);
}

/*
 * Tracks that take part in the note map on `channel` (S083 follow-up,
 * user 2026-10-22: track `nte off`).
 *
 * What: midiNoteMap_tracksOnChannel() minus the tracks whose note byte is
 * `off` (MIDI_NOTEMAP_NOTE_OFF; any byte above 127 is treated the same).
 * Why: `nte off` must take a track out of the note map only. It neither
 * receives nor sends notes, and it does not use up a stripe or split
 * position, so the remaining tracks on the channel map as if it were not
 * there. Its channel still counts for CC1/NRPN/Learn/PC, which use
 * midiNoteMap_tracksOnChannel() directly. Inputs: settings, channel 1..16.
 * Output: track bit mask (0 for an invalid channel). Accessors:
 * midiNoteMap_resolve() (RX), midiNoteMap_inverse() (TX). Host test:
 * tools/midi_test/notemap_test.c (`nte off` cases and invariant).
 */
static uint8_t midiNoteMap_noteTracksOnChannel(const midiNoteMap_settings_t *s,
                                               uint8_t channel)
{
	uint8_t set = midiNoteMap_tracksOnChannel(s, channel);
	uint8_t t;

	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
		if (s->note[t] > 127u)
			set &= (uint8_t)~(1u << t);
	}
	return set;
}

uint8_t midiNoteMap_tracksOnChannel(const midiNoteMap_settings_t *s,
                                    uint8_t channel)
{
	uint8_t mask = 0u;
	uint8_t t;

	if (channel < 1u || channel > 16u)
		return 0u;
	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
		if (midiNoteMap_effectiveChannel(s, t) == channel)
			mask |= (uint8_t)(1u << t);
	}
	return mask;
}

/*
 * Lowest assigned note among `set` (128 when none): the top of the
 * unassigned region (rule 2).
 */
static uint8_t midiNoteMap_lowestAssigned(const midiNoteMap_settings_t *s,
                                          uint8_t set)
{
	uint8_t low = 128u;
	uint8_t t;

	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
		if ((set & (1u << t)) && s->note[t] != 0u && s->note[t] < low)
			low = s->note[t];
	}
	return low;
}

/*
 * S096 P3: the weighted split block of one track.
 *
 * What: the region is divided into `weight` near-equal units (the remainder
 * to the lowest units); a track occupies `units` consecutive units (4 for a
 * wide track, 1 otherwise) starting at `unitStart`. Output: the block's
 * note start and size. Why: a wide track's block must span four semitone
 * units, so a lone Tetrakt takes all 128 notes (sub n mod 4, pitch n/4) and
 * a shared channel gives it four times an ordinary track's block.
 */
static void midiNoteMap_splitWeighted(uint8_t region, uint8_t weight,
                                      uint8_t unitStart, uint8_t units,
                                      uint8_t *start, uint8_t *size)
{
	uint8_t base = (uint8_t)(region / weight);
	uint8_t rem = (uint8_t)(region % weight);
	uint8_t i, s = 0u, e = 0u;

	for (i = 0u; i < unitStart; i++)
		s = (uint8_t)(s + base + ((i < rem) ? 1u : 0u));
	e = s;
	for (i = unitStart; i < (uint8_t)(unitStart + units); i++)
		e = (uint8_t)(e + base + ((i < rem) ? 1u : 0u));
	*start = s;
	*size = (uint8_t)(e - s);
}

static uint8_t midiNoteMap_clampInternal(uint16_t internal)
{
	return (uint8_t)((internal > 127u) ? 127u : internal);
}


/*
 * S096 P3 (plan §6.1, decided Q14): wide tracks.
 *   Rule 1 (assigned range): d = n - root; sub = d mod 4; internal = 63 + d / 4.
 *   Rule 2, G stripe: U' lists every unassigned track once, a wide track four
 *     times (sub 0..3, consecutive); n -> U'[n mod |U'|], default pitch.
 *   Rule 2, split: block sizes over the weighted count (wide = 4); inside a
 *     wide block sub = (n - start) mod 4, internal = 63 + (n - start) / 4.
 *   Rule 4: internal clamped to 127.
 *   Results: G = 10, T1 Tetrakt, T2..T7 drums: |U'| = 10; notes 0..3 ->
 *     T1 A..D, 4 -> T2, ... 9 -> T7, 10..13 -> T1 A..D. Ch 2, T1 Tetrakt
 *     alone, no note: 0..127 -> T1, sub n mod 4, +n / 4 semitones (0..+31).
 * The inverse returns the note that resolves back to (track, internal, sub).
 */
uint8_t midiNoteMap_resolve(const midiNoteMap_settings_t *s, uint8_t channel,
                            uint8_t note, uint8_t *track, uint8_t *internal,
                            uint8_t *sub)
{
	uint8_t set;
	uint8_t owner = 0xFFu;
	uint8_t ownerNote = 0u;
	uint8_t unassigned = 0u;
	uint8_t weight = 0u;
	uint8_t region;
	uint8_t t;

	if (!s || !track || !internal || !sub || note > 127u)
		return 0u;
	set = midiNoteMap_noteTracksOnChannel(s, channel);
	if (set == 0u)
		return 0u;
	/* Rule 1: the assigned track with the greatest root <= note; on equal
	** roots the first (lowest) track wins (rule 3 / N5). */
	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
		uint8_t root;

		if ((set & (1u << t)) == 0u)
			continue;
		root = s->note[t];
		if (root == 0u) {
			unassigned |= (uint8_t)(1u << t);
			weight = (uint8_t)(weight + 1u + ((s->wide >> t) & 1u) * 3u);
			continue;
		}
		if (root <= note && (owner == 0xFFu || root > ownerNote)) {
			owner = t;
			ownerNote = root;
		}
	}
	if (owner != 0xFFu) {
		const uint8_t d = (uint8_t)(note - ownerNote);
		const uint8_t wide = (uint8_t)((s->wide >> owner) & 1u);

		*track = owner;
		*sub = (uint8_t)(wide ? (d & 3u) : 0u);
		*internal = midiNoteMap_clampInternal(
		    (uint16_t)(MIDI_NOTEMAP_ROOT_INTERNAL + (wide ? (d / 4u) : d)));
		return 1u;
	}
	/* Rule 2: the note lies below every assigned root. */
	if (weight == 0u)
		return 0u;
	region = midiNoteMap_lowestAssigned(s, set);
	if (channel == s->global) {
		uint8_t want = (uint8_t)(note % weight);
		uint8_t i = 0u;

		for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
			const uint8_t w = (uint8_t)(1u + ((s->wide >> t) & 1u) * 3u);

			if ((unassigned & (1u << t)) == 0u)
				continue;
			if (want >= i && want < (uint8_t)(i + w)) {
				const uint8_t wide = (uint8_t)((s->wide >> t) & 1u);

				*track = t;
				*sub = (uint8_t)(wide ? (uint8_t)(want - i) : 0u);
				*internal = MIDI_NOTEMAP_ROOT_INTERNAL;
				return 1u;
			}
			i = (uint8_t)(i + w);
		}
		return 0u;
	}
	{
		uint8_t i = 0u;

		for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
			const uint8_t w = (uint8_t)(1u + ((s->wide >> t) & 1u) * 3u);
			uint8_t start;
			uint8_t size;

			if ((unassigned & (1u << t)) == 0u)
				continue;
			midiNoteMap_splitWeighted(region, weight, i, w, &start, &size);
			if (size != 0u && note >= start &&
			    note < (uint16_t)start + size) {
				const uint8_t wide = (uint8_t)((s->wide >> t) & 1u);
				const uint8_t d = (uint8_t)(note - start);

				*track = t;
				*sub = (uint8_t)(wide ? (d & 3u) : 0u);
				*internal = midiNoteMap_clampInternal(
				    (uint16_t)(MIDI_NOTEMAP_ROOT_INTERNAL +
				               (wide ? (d / 4u) : d)));
				return 1u;
			}
			i = (uint8_t)(i + w);
		}
	}
	return 0u;
}

uint8_t midiNoteMap_inverse(const midiNoteMap_settings_t *s, uint8_t track,
                            uint8_t internal, uint8_t sub, uint8_t *channel,
                            uint8_t *note)
{
	uint8_t ch;
	uint8_t set;
	uint16_t candidate;
	uint8_t gotTrack;
	uint8_t gotInternal;
	uint8_t gotSub;
	uint8_t wide;

	if (!s || !channel || !note || track >= MIDI_NOTEMAP_TRACKS ||
	    internal < MIDI_NOTEMAP_ROOT_INTERNAL || internal > 127u)
		return 0u;
	wide = (uint8_t)((s->wide >> track) & 1u);
	if (sub > (wide ? 3u : 0u))
		return 0u;
	ch = midiNoteMap_effectiveChannel(s, track);
	if (ch == 0u || s->note[track] > 127u)
		return 0u;	/* no channel, or note `off`: never sent */
	set = midiNoteMap_noteTracksOnChannel(s, ch);
	if (s->note[track] != 0u) {
		candidate = (uint16_t)(s->note[track] +
		                       (wide ? 4u * (internal - MIDI_NOTEMAP_ROOT_INTERNAL) + sub
		                             : (internal - MIDI_NOTEMAP_ROOT_INTERNAL)));
	} else {
		uint8_t region = midiNoteMap_lowestAssigned(s, set);
		uint8_t weight = 0u;
		uint8_t index = 0xFFu;
		uint8_t t;

		for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
			const uint8_t w = (uint8_t)(1u + ((s->wide >> t) & 1u) * 3u);

			if ((set & (1u << t)) == 0u || s->note[t] != 0u)
				continue;
			if (t == track)
				index = weight;
			weight = (uint8_t)(weight + w);
		}
		if (index == 0xFFu)
			return 0u;
		if (ch == s->global) {
			if (internal != MIDI_NOTEMAP_ROOT_INTERNAL)
				return 0u;	/* stripe notes play default pitch only */
			candidate = (uint16_t)(index + sub);
		} else {
			uint8_t start;
			uint8_t size;

			midiNoteMap_splitWeighted(region, weight, index,
			                          (uint8_t)(wide ? 4u : 1u), &start, &size);
			candidate = (uint16_t)(start +
			                       (wide ? 4u * (internal - MIDI_NOTEMAP_ROOT_INTERNAL) + sub
			                             : (internal - MIDI_NOTEMAP_ROOT_INTERNAL)));
		}
	}
	if (candidate > 127u)
		return 0u;
	/* Q4: send only what plays back as the same track, pitch and place. */
	if (!midiNoteMap_resolve(s, ch, (uint8_t)candidate, &gotTrack,
	                         &gotInternal, &gotSub) ||
	    gotTrack != track || gotInternal != internal || gotSub != sub)
		return 0u;
	*channel = ch;
	*note = (uint8_t)candidate;
	return 1u;
}

#ifndef MIDI_NOTEMAP_HOST_TEST
/*
 * Snapshot one Scene's map inputs (S083).
 * What: G from MidiIn, channel and note per track from SceneData. Scene
 * index 16 (Scene 17) resolves through the SceneData accessors (S082 5).
 * Read per message, never cached (S082 R14). Accessors: MidiNotes, MidiOut,
 * MidiControl, MidiProgram, MidiLearn.
 */
void midiNoteMap_load(uint8_t scene_index, midiNoteMap_settings_t *out)
{
	uint8_t t;

	if (!out)
		return;
	out->global = midiIn_globalChannel();
	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
		out->channel[t] = scene_getTrackMidiChannel(scene_index, t);
		out->note[t] = scene_getTrackMidiNote(scene_index, t);
	}
	/* S096 P3: the map stays type-blind; the note-mode module says which
	 * tracks take four notes per pitch (a Tetrakt). */
	out->wide = midiNoteMode_wideTracks(scene_index);
}
#endif
