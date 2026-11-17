/*
 * notemap_test.c - host test for Core/MIDI/MidiNoteMap.c (S083 Phase B).
 *
 * Build and run:  make -C tools/midi_test run
 * (host cc only; no new utilities, the tools/dsp_test precedent).
 *
 * What it checks:
 *  1. the four worked examples of MIDI_SPEC 4.2, note by note;
 *  2. N5 (equal roots: the lower track wins, the other owns nothing);
 *  3. G off (unassigned tracks unmapped); `ch off` / `nte off` tracks
 *     (S083 follow-up);
 *  4. for 200,000 pseudo-random settings: every (channel, note) maps to at
 *     most one track whose effective channel is that channel, and every
 *     inverse() result resolves back to the same (track, internal note)
 *     (the S083 Q4 transmit rule).
 * Exit status 0 = pass; failures print the first mismatches.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "MidiNoteMap.h"

static unsigned failures;

#define CHECK(cond, ...) do { if (!(cond)) { \
	if (failures < 20u) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } failures++; } } while (0)

static void expect(const midiNoteMap_settings_t *s, uint8_t ch, uint8_t n,
                   int track, int internal)
{
	uint8_t t = 0xFF, i = 0xFF, sub = 0xFF;
	uint8_t ok = midiNoteMap_resolve(s, ch, n, &t, &i, &sub);

	if (track < 0) {
		CHECK(!ok, "ch %u note %u: expected unmapped, got T%u/%u",
		      ch, n, t + 1u, i);
		return;
	}
	CHECK(ok && t == (uint8_t)track && i == (uint8_t)internal && sub == 0u,
	      "ch %u note %u: expected T%d/%d, got %s T%u/%u", ch, n,
	      track + 1, internal, ok ? "" : "(unmapped)", t + 1u, i);
}

static void base(midiNoteMap_settings_t *s, uint8_t g)
{
	memset(s, 0, sizeof(*s));
	s->global = g;
}

/* S096 P3: a wide-track expectation (sub 0..3). */
static void expect_sub(const midiNoteMap_settings_t *s, uint8_t ch, uint8_t n,
                       int track, int internal, int sub)
{
	uint8_t t = 0xFF, i = 0xFF, su = 0xFF;
	uint8_t ok = midiNoteMap_resolve(s, ch, n, &t, &i, &su);

	CHECK(ok && t == (uint8_t)track && i == (uint8_t)internal &&
	      su == (uint8_t)sub,
	      "ch %u note %u: expected T%d/%d/s%d, got %s T%u/%u/s%u", ch, n,
	      track + 1, internal, sub, ok ? "" : "(unmapped)", t + 1u, i, su);
}

static void examples(void)
{
	midiNoteMap_settings_t s;
	unsigned n;

	/* 4.2 row 1: G = 10, no track settings: stripe. */
	base(&s, 10u);
	expect(&s, 10u, 0u, 0, 63);
	expect(&s, 10u, 6u, 6, 63);
	expect(&s, 10u, 7u, 0, 63);
	expect(&s, 10u, 36u, 1, 63);
	expect(&s, 10u, 60u, 4, 63);
	expect(&s, 9u, 36u, -1, 0);

	/* Row 2: G = 10; T3 nte 48, T5 nte 60. */
	base(&s, 10u);
	s.note[2] = 48u;
	s.note[4] = 60u;
	{
		static const int stripe[5] = {0, 1, 3, 5, 6};
		for (n = 0u; n < 48u; n++)
			expect(&s, 10u, (uint8_t)n, stripe[n % 5u], 63);
	}
	for (n = 48u; n < 60u; n++)
		expect(&s, 10u, (uint8_t)n, 2, (int)(63u + n - 48u));
	for (n = 60u; n < 128u; n++)
		expect(&s, 10u, (uint8_t)n, 4,
		       (int)((63u + n - 60u > 127u) ? 127u : 63u + n - 60u));

	/* Row 3: T1, T2, T3 chn 2, no notes: 43 / 43 / 42 notes. */
	base(&s, 1u);
	s.channel[0] = s.channel[1] = s.channel[2] = 2u;
	expect(&s, 2u, 0u, 0, 63);
	expect(&s, 2u, 42u, 0, 105);
	expect(&s, 2u, 43u, 1, 63);
	expect(&s, 2u, 50u, 1, 70);
	expect(&s, 2u, 85u, 1, 105);
	expect(&s, 2u, 86u, 2, 63);
	expect(&s, 2u, 127u, 2, 104);

	/* Row 4: ch 2: T1 -, T2 nte 36, T3 -, T4 nte 60. */
	base(&s, 1u);
	s.channel[0] = s.channel[1] = s.channel[2] = s.channel[3] = 2u;
	s.note[1] = 36u;
	s.note[3] = 60u;
	expect(&s, 2u, 0u, 0, 63);
	expect(&s, 2u, 17u, 0, 80);
	expect(&s, 2u, 18u, 2, 63);
	expect(&s, 2u, 35u, 2, 80);
	expect(&s, 2u, 36u, 1, 63);
	expect(&s, 2u, 59u, 1, 86);
	expect(&s, 2u, 60u, 3, 63);

	/* N5: equal roots on ch 3. */
	base(&s, 1u);
	s.channel[1] = s.channel[3] = 3u;
	s.note[1] = s.note[3] = 40u;
	expect(&s, 3u, 40u, 1, 63);
	{
		uint8_t c, nn;
		CHECK(!midiNoteMap_inverse(&s, 3u, 63u, 0u, &c, &nn),
		      "N5 tie loser must have no transmit note");
	}

	/* G off: unassigned tracks unmapped; assigned channel still works. */
	base(&s, 0u);
	s.channel[0] = 5u;
	expect(&s, 1u, 36u, -1, 0);
	expect(&s, 5u, 36u, 0, 99);

	/*
	 * S083 follow-up (user 2026-10-22): `ch off` removes a track from every
	 * channel; `nte off` keeps the channel but removes the track from the
	 * note map (no notes in, none out).
	 */
	base(&s, 10u);
	s.channel[1] = MIDI_NOTEMAP_CHANNEL_OFF;	/* T2 ch off */
	expect(&s, 10u, 0u, 0, 63);
	expect(&s, 10u, 1u, 2, 63);	/* stripe T1 T3 T4 T5 T6 T7 */
	expect(&s, 10u, 6u, 0, 63);
	CHECK(midiNoteMap_effectiveChannel(&s, 1u) == 0u, "ch off: no channel");
	CHECK((midiNoteMap_tracksOnChannel(&s, 10u) & 0x02u) == 0u,
	      "ch off: not on G");
	base(&s, 10u);
	s.note[2] = MIDI_NOTEMAP_NOTE_OFF;		/* T3 nte off */
	expect(&s, 10u, 2u, 3, 63);	/* stripe T1 T2 T4 T5 T6 T7 */
	CHECK((midiNoteMap_tracksOnChannel(&s, 10u) & 0x04u) != 0u,
	      "nte off keeps the channel (CC/NRPN/PC)");
	{
		uint8_t c, nn;
		CHECK(!midiNoteMap_inverse(&s, 2u, 63u, 0u, &c, &nn),
		      "nte off: nothing sent");
	}
	s.channel[2] = 4u;	/* alone on ch 4 with nte off: unmapped */
	expect(&s, 4u, 60u, -1, 0);

	/* Q4 transmit rule on row 2. */
	base(&s, 10u);
	s.note[2] = 48u;
	s.note[4] = 60u;
	{
		uint8_t c = 0, nn = 0;
		CHECK(midiNoteMap_inverse(&s, 4u, 63u, 0u, &c, &nn) && c == 10u &&
		      nn == 60u, "T5 root");
		CHECK(midiNoteMap_inverse(&s, 2u, 70u, 0u, &c, &nn) && nn == 55u,
		      "T3 +7");
		CHECK(!midiNoteMap_inverse(&s, 2u, 75u, 0u, &c, &nn),
		      "T3 +12 lies in T5's range: no TX");
		CHECK(!midiNoteMap_inverse(&s, 0u, 65u, 0u, &c, &nn),
		      "stripe pitch: no TX");
		CHECK(!midiNoteMap_inverse(&s, 0u, 62u, 0u, &c, &nn),
		      "below default pitch: no TX");
	}
}

/* S096 P3 T1 (plan §6.4): wide tracks, their sub, the weighted split and the
 * inverse round trip. */
static void wide(void)
{
	midiNoteMap_settings_t s;
	uint8_t c, nn;

	/* G stripe: T1 wide, T2..T7 ordinary: |U'| = 10 places. */
	base(&s, 10u);
	s.wide = 0x01u;
	expect_sub(&s, 10u, 0u, 0, 63, 0);
	expect_sub(&s, 10u, 1u, 0, 63, 1);
	expect_sub(&s, 10u, 2u, 0, 63, 2);
	expect_sub(&s, 10u, 3u, 0, 63, 3);
	expect_sub(&s, 10u, 4u, 1, 63, 0);
	expect_sub(&s, 10u, 9u, 6, 63, 0);
	expect_sub(&s, 10u, 10u, 0, 63, 0);
	expect_sub(&s, 10u, 13u, 0, 63, 3);

	/* wide assigned range (ch 2, T1 alone, root 60). */
	base(&s, 1u);
	s.channel[0] = 2u; s.note[0] = 60u; s.wide = 0x01u;
	expect_sub(&s, 2u, 60u, 0, 63, 0);
	expect_sub(&s, 2u, 61u, 0, 63, 1);
	expect_sub(&s, 2u, 63u, 0, 63, 3);
	expect_sub(&s, 2u, 64u, 0, 64, 0);
	expect_sub(&s, 2u, 127u, 0, 79, 3);   /* clamped at 127 */
	CHECK(midiNoteMap_inverse(&s, 0u, 64u, 1u, &c, &nn) && c == 2u &&
	      nn == 65u, "wide assigned inverse sub 1");
	CHECK(!midiNoteMap_inverse(&s, 0u, 64u, 4u, &c, &nn),
	      "wide inverse sub 4 refused");

	/* wide alone on ch 2, no root: 0..127 -> T1, sub n mod 4, +n/4. */
	base(&s, 1u);
	s.channel[0] = 2u; s.wide = 0x01u;
	expect_sub(&s, 2u, 0u, 0, 63, 0);
	expect_sub(&s, 2u, 1u, 0, 63, 1);
	expect_sub(&s, 2u, 5u, 0, 64, 1);
	expect_sub(&s, 2u, 127u, 0, 94, 3);

	/* weighted split: T1 wide, T2 and T3 ordinary (weight 6). */
	base(&s, 1u);
	s.channel[0] = s.channel[1] = s.channel[2] = 2u;
	s.wide = 0x01u;
	expect_sub(&s, 2u, 0u, 0, 63, 0);
	expect_sub(&s, 2u, 3u, 0, 63, 3);
	expect_sub(&s, 2u, 4u, 0, 64, 0);
	expect_sub(&s, 2u, 85u, 0, 84, 1);
	expect_sub(&s, 2u, 86u, 1, 63, 0);
	expect_sub(&s, 2u, 107u, 2, 63, 0);

	/* inverse round trip for every sub on the stripe. */
	base(&s, 10u);
	s.wide = 0x01u;
	{
		uint8_t sub;

		for (sub = 0u; sub < 4u; sub++) {
			uint8_t tr, in, su;

			CHECK(midiNoteMap_inverse(&s, 0u, 63u, sub, &c, &nn) &&
			      midiNoteMap_resolve(&s, c, nn, &tr, &in, &su) &&
			      tr == 0u && in == 63u && su == sub,
			      "stripe wide inverse round trip sub %u", sub);
		}
	}
}

static uint32_t lcg = 12345u;
static uint32_t rnd(uint32_t m)
{
	lcg = lcg * 1664525u + 1013904223u;
	return (lcg >> 8) % m;
}

static void randomised(void)
{
	unsigned iter;

	for (iter = 0u; iter < 200000u; iter++) {
		midiNoteMap_settings_t s;
		unsigned t, ch, n, i;

		s.global = (uint8_t)rnd(17u);
		s.wide = (uint8_t)(rnd(4u) ? (uint8_t)rnd(128u) : 0u);
		for (t = 0u; t < 7u; t++) {
			/* include the S083 follow-up `off` values */
			s.channel[t] = (uint8_t)(rnd(3u) ? 0u :
			    (rnd(6u) ? 1u + rnd(4u) : MIDI_NOTEMAP_CHANNEL_OFF));
			s.note[t] = (uint8_t)(rnd(3u) ? 0u :
			    (rnd(6u) ? 1u + rnd(127u) : MIDI_NOTEMAP_NOTE_OFF));
		}
		for (ch = 1u; ch <= 16u; ch++) {
			for (n = 0u; n < 128u; n++) {
				uint8_t tr, in, sub;
				if (midiNoteMap_resolve(&s, (uint8_t)ch, (uint8_t)n,
				                        &tr, &in, &sub))
					CHECK(midiNoteMap_effectiveChannel(&s, tr) == ch &&
					      s.note[tr] <= 127u &&	/* nte off never maps */
					      in >= 63u && in <= 127u &&
					      sub <= (((s.wide >> tr) & 1u) ? 3u : 0u),
					      "iter %u ch %u n %u -> T%u ec %u in %u", iter,
					      ch, n, tr + 1u,
					      midiNoteMap_effectiveChannel(&s, tr), in);
			}
		}
		for (t = 0u; t < 7u; t++) {
			for (i = 0u; i < 128u; i++) {
				uint8_t c, nn, tr, in, su, sub;

				for (sub = 0u; sub <= 3u; sub++) {
					if (!midiNoteMap_inverse(&s, (uint8_t)t, (uint8_t)i,
					                         sub, &c, &nn))
						continue;
					CHECK(midiNoteMap_resolve(&s, c, nn, &tr, &in, &su) &&
					      tr == t && in == i && su == sub,
					      "iter %u inverse T%u/%u/s%u -> ch %u n %u", iter,
					      t + 1u, i, sub, c, nn);
				}
			}
		}
	}
}

int main(void)
{
	examples();
	wide();
	randomised();
	printf("%s (%u failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
