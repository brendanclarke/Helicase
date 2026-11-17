/*
 * notemode_test.c - host laws for Core/MIDI/MidiNoteModes.c (S096 P3).
 *
 * What: mode resolution, sub -> layer mask, the NORMAL and LAYER4 note-on
 * paths, six layer tracks, and repeated layer notes in one pass. The module
 * is linked alone with SceneData, the sequencer, LiveRecord and the overlay
 * stubbed (notemode_host.h).
 * Accessors: make -C tools/midi_test run.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "notemode_host.h"
#include "MidiNoteModes.h"

uint8_t host_slot_type[16][6];
uint8_t host_slot_param[16][6][64];
uint8_t host_track_channel[16][7];
uint8_t host_active_scene;
uint8_t host_global_channel;
uint8_t host_overlay;
uint8_t host_epoch;

static struct { uint8_t track; uint16_t target; uint8_t value; } qlog[128];
static unsigned qn;
static uint8_t holdSlot, holdOn;

static unsigned failures;
static unsigned normal_calls, layer_calls;
static uint8_t last_mask, last_track, last_internal, last_vel;
static liveRec_trigger_t last_lr;

#define CHECK(cond, ...) do { if (!(cond)) { \
	if (failures < 30u) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } failures++; } } while (0)

void host_reset_calls(void)
{
	normal_calls = layer_calls = 0u;
	last_mask = last_track = last_internal = last_vel = 0u;
	memset(&last_lr, 0, sizeof(last_lr));
}
unsigned host_normal_calls(void) { return normal_calls; }
unsigned host_layer_calls(void) { return layer_calls; }
uint8_t host_last_mask(void) { return last_mask; }
uint8_t host_last_track(void) { return last_track; }
uint8_t host_last_internal(void) { return last_internal; }
uint8_t host_last_vel(void) { return last_vel; }
const liveRec_trigger_t *host_last_lr(void) { return &last_lr; }

void seq_triggerVoice(uint8_t track, uint8_t vol, uint8_t note, uint8_t reset)
{
	(void)reset;
	normal_calls++;
	last_track = track;
	last_internal = note;
	last_vel = vol;
}

void seq_triggerVoiceLayers(uint8_t track, uint8_t vol, uint8_t note,
                            uint8_t layers)
{
	layer_calls++;
	last_track = track;
	last_internal = note;
	last_vel = vol;
	last_mask = layers;
}

void liveRec_triggerIn(const liveRec_trigger_t *t)
{
	last_lr = *t;
}

uint8_t seq_queueForegroundAutomation(uint8_t track, uint16_t target,
                                      uint8_t value)
{
	if (qn < 128u) {
		qlog[qn].track = track;
		qlog[qn].target = target;
		qlog[qn].value = value;
		qn++;
	}
	return 1u;
}

void instrumentManager_setMidiHold(uint8_t slot, uint8_t on)
{
	holdSlot = slot;
	holdOn = on;
}

uint8_t midiIn_globalChannel(void) { return host_global_channel; }
uint8_t buttonHandler_overlayActive(void) { return host_overlay; }

static void reset_scene(void)
{
	memset(host_slot_type, 0, sizeof(host_slot_type));
	memset(host_slot_param, 0, sizeof(host_slot_param));
	memset(host_track_channel, 0, sizeof(host_track_channel));
	host_active_scene = 0u;
	host_global_channel = 10u;
	host_overlay = 0u;
	host_epoch = 0u;
	qn = 0u;
	holdSlot = 0xFFu;
	holdOn = 0xFFu;
	midiNoteMode_init();
	host_reset_calls();
}

int main(void)
{
	uint8_t t;

	/* Mode resolution. */
	reset_scene();
	host_slot_type[0][0] = INSTRUMENT_TYPE_COL;
	host_slot_type[0][1] = INSTRUMENT_TYPE_TKT;
	CHECK(midiNoteMode_trackMode(0u, 0u, 10u) == MNM_NORMAL,
	      "Colloid on G is NORMAL");
	CHECK(midiNoteMode_trackMode(0u, 1u, 10u) == MNM_LAYER4,
	      "Tetrakt on G is LAYER4");
	CHECK(midiNoteMode_trackMode(0u, 6u, 2u) == MNM_NORMAL,
	      "a non-Tetrakt slot 6 is NORMAL");
	host_slot_type[0][5] = INSTRUMENT_TYPE_TKT;
	CHECK(midiNoteMode_trackMode(0u, 6u, 10u) == MNM_LAYER4,
	      "track 7 plays slot 6 (Tetrakt)");
	CHECK(midiNoteMode_wideTracks(0u) == 0x62u,
	      "wide mask for T2, T6 and T7 (slot 6 plays both)");
	CHECK(midiNoteMode_wideTracks(1u) == 0u, "another Scene has none");

	/* sub -> mask, only on a layer track. */
	CHECK(midiNoteMode_layerMask(1u, 0u) == 1u &&
	      midiNoteMode_layerMask(1u, 2u) == 4u &&
	      midiNoteMode_layerMask(1u, 3u) == 8u, "sub -> layer mask");
	CHECK(midiNoteMode_layerMask(0u, 2u) == 0u, "no mask on a Colloid");
	CHECK(midiNoteMode_layerMask(1u, 4u) == 0u, "sub 4 refused");

	/* A layer note: one masked trigger and a LiveRecord hit with layers. */
	host_reset_calls();
	CHECK(midiNoteMode_noteOn(1u, 10u, 65u, 2u, 62u, 90u, 123u) == 1u,
	      "layer note consumed");
	CHECK(layer_calls == 1u && normal_calls == 0u, "one layer trigger");
	CHECK(last_mask == 4u && last_track == 1u && last_internal == 65u &&
	      last_vel == 90u, "layer trigger payload");
	CHECK(last_lr.layers == 4u && last_lr.kind == LIVEREC_HIT_SINGLE &&
	      last_lr.source == LIVEREC_SRC_MIDI && last_lr.age_us == 123u &&
	      last_lr.note == 65u && last_lr.velocity == 90u,
	      "layer hit reported");

	/* Two layer notes on one track in one pass both fire. */
	host_reset_calls();
	(void)midiNoteMode_noteOn(1u, 10u, 63u, 0u, 60u, 100u, 0u);
	(void)midiNoteMode_noteOn(1u, 10u, 63u, 1u, 61u, 100u, 0u);
	CHECK(layer_calls == 2u && last_mask == 2u, "two layers in one pass");

	/* A NORMAL note (Colloid on G): the S083 single trigger. */
	host_reset_calls();
	(void)midiNoteMode_noteOn(0u, 10u, 64u, 0u, 64u, 77u, 0u);
	CHECK(normal_calls == 1u && layer_calls == 0u, "normal trigger");
	CHECK(last_lr.layers == 0u && last_lr.note_is_default == 0u,
	      "normal hit has no layer mask");
	host_reset_calls();
	(void)midiNoteMode_noteOn(0u, 10u, 63u, 0u, 36u, 77u, 0u);
	CHECK(last_lr.note_is_default == 1u, "default note flagged");

	/* Six layer tracks, one chord each. */
	for (t = 0u; t < 6u; t++)
		host_slot_type[0][t] = INSTRUMENT_TYPE_TKT;
	host_reset_calls();
	for (t = 0u; t < 6u; t++)
		(void)midiNoteMode_noteOn(t, 10u, 63u, (uint8_t)(t & 3u), 60u, 100u, 0u);
	CHECK(layer_calls == 6u, "six layer tracks fire");

	/* Note-offs are consumed (the overlay rule already ran). */
	CHECK(midiNoteMode_noteOff(1u, 10u, 65u, 0u) == 1u, "note-off consumed");

	/* S096 P3 C3: the paraphonic Colloid. */
	reset_scene();
	host_slot_type[0][0] = INSTRUMENT_TYPE_COL;
	host_track_channel[0][0] = 2u;
	host_slot_param[0][0][COL_IDX_COARSE] = 48u;
	host_slot_param[0][0][COL_IDX_INTERVAL_1] = 63u;
	host_slot_param[0][0][COL_IDX_INTERVAL_1 + 1u] = 63u;
	host_slot_param[0][0][COL_IDX_INTERVAL_1 + 2u] = 63u;
	CHECK(midiNoteMode_trackMode(0u, 0u, 2u) == MNM_PARA, "Colloid ch 2 is PARA");
	CHECK(midiNoteMode_trackMode(0u, 0u, 10u) == MNM_NORMAL, "Colloid on G is NORMAL");
	host_overlay = 1u;
	CHECK(midiNoteMode_trackMode(0u, 0u, 2u) == MNM_NORMAL,
	      "the overlay falls back to NORMAL");
	host_overlay = 0u;

	/* first key: trigger + sus on */
	host_reset_calls();
	qn = 0u;
	(void)midiNoteMode_noteOn(0u, 2u, 60u, 0u, 48u, 100u, 7u);
	CHECK(normal_calls == 1u && layer_calls == 0u, "PARA first key triggers");
	CHECK(qn == 1u && qlog[0].target ==
	      instrumentParam_make(0u, COL_IDX_SUSTAIN) && qlog[0].value == 1u,
	      "PARA first key automates sus on");
	CHECK(holdSlot == 0u && holdOn == 1u, "PARA holds the voice");
	CHECK(last_lr.kind == LIVEREC_HIT_SINGLE && last_lr.autoCount == 1u &&
	      last_lr.autoTarget[0] == instrumentParam_make(0u, COL_IDX_SUSTAIN) &&
	      last_lr.autoValue[0] == 1u && last_lr.age_us == 7u,
	      "PARA first hit records sus");

	/* later keys take in1, in2, in3, then in1 again (round robin) */
	qn = 0u;
	(void)midiNoteMode_noteOn(0u, 2u, 64u, 0u, 52u, 90u, 0u);
	CHECK(qn == 1u && qlog[0].target == instrumentParam_make(0u, COL_IDX_INTERVAL_1) &&
	      qlog[0].value == 67u, "key 2 -> in1 +4");
	CHECK(last_lr.kind == LIVEREC_HIT_AUTO && last_lr.autoCount == 1u &&
	      last_lr.autoValue[0] == 67u, "key 2 records the interval");
	qn = 0u;
	(void)midiNoteMode_noteOn(0u, 2u, 67u, 0u, 55u, 90u, 0u);
	CHECK(qn == 1u && qlog[0].target == instrumentParam_make(0u, COL_IDX_INTERVAL_1 + 1u) &&
	      qlog[0].value == 70u, "key 3 -> in2 +7");
	qn = 0u;
	(void)midiNoteMode_noteOn(0u, 2u, 71u, 0u, 59u, 90u, 0u);
	CHECK(qn == 1u && qlog[0].target == instrumentParam_make(0u, COL_IDX_INTERVAL_1 + 2u) &&
	      qlog[0].value == 74u, "key 4 -> in3 +11");
	qn = 0u;
	(void)midiNoteMode_noteOn(0u, 2u, 74u, 0u, 62u, 90u, 0u);
	CHECK(qn == 1u && qlog[0].target == instrumentParam_make(0u, COL_IDX_INTERVAL_1) &&
	      qlog[0].value == 77u, "key 5 takes in1 (+14)");
	/* a repeated note-on updates the velocity and does nothing else */
	qn = 0u;
	(void)midiNoteMode_noteOn(0u, 2u, 64u, 0u, 52u, 70u, 0u);
	CHECK(qn == 0u, "a held key does not re-queue");

	/* release the fundamental: coa and every interval move to F' = 64 */
	qn = 0u;
	(void)midiNoteMode_noteOff(0u, 2u, 60u, 0u);
	CHECK(qn == 4u, "fundamental release queues coa and 3 intervals");
	{
		unsigned hasCoa = 0u, has66 = 0u, has70 = 0u, has73 = 0u;
		unsigned i;

		for (i = 0u; i < qn; i++) {
			if (qlog[i].target == instrumentParam_make(0u, COL_IDX_COARSE) &&
			    qlog[i].value == 52u) hasCoa = 1u;
			if (qlog[i].target == instrumentParam_make(0u, COL_IDX_INTERVAL_1 + 1u) &&
			    qlog[i].value == 66u) has66 = 1u;
			if (qlog[i].target == instrumentParam_make(0u, COL_IDX_INTERVAL_1 + 2u) &&
			    qlog[i].value == 70u) has70 = 1u;
			if (qlog[i].target == instrumentParam_make(0u, COL_IDX_INTERVAL_1) &&
			    qlog[i].value == 73u) has73 = 1u;
		}
		CHECK(hasCoa && has66 && has70 && has73,
		      "fundamental hand-over keeps every held pitch");
	}
	CHECK(last_lr.kind == LIVEREC_HIT_AUTO && last_lr.autoCount == 4u,
	      "hand-over records up to four entries");

	/* a non-fundamental release returns its group to the patch value */
	qn = 0u;
	(void)midiNoteMode_noteOff(0u, 2u, 71u, 0u);
	CHECK(qn == 1u && qlog[0].target == instrumentParam_make(0u, COL_IDX_INTERVAL_1 + 2u) &&
	      qlog[0].value == 63u, "non-fundamental release restores in3");

	/* all keys up: sus off, the hold cleared, the group freed */
	qn = 0u;
	(void)midiNoteMode_noteOff(0u, 2u, 64u, 0u);
	(void)midiNoteMode_noteOff(0u, 2u, 67u, 0u);
	(void)midiNoteMode_noteOff(0u, 2u, 74u, 0u);
	CHECK(qlog[qn - 1u].target == instrumentParam_make(0u, COL_IDX_SUSTAIN) &&
	      qlog[qn - 1u].value == 0u && holdOn == 0u, "all keys up: sus off");
	host_reset_calls();
	(void)midiNoteMode_noteOn(0u, 2u, 60u, 0u, 48u, 100u, 0u);
	CHECK(normal_calls == 1u, "the freed group starts again");

	/* F' previously owned an interval: that group returns to the patch */
	(void)midiNoteMode_noteOn(0u, 2u, 64u, 0u, 52u, 90u, 0u);   /* in1 */
	(void)midiNoteMode_noteOn(0u, 2u, 67u, 0u, 55u, 90u, 0u);   /* in2 */
	qn = 0u;
	(void)midiNoteMode_noteOff(0u, 2u, 60u, 0u);
	{
		unsigned hasRestore = 0u, has66 = 0u, i;

		for (i = 0u; i < qn; i++) {
			if (qlog[i].target == instrumentParam_make(0u, COL_IDX_INTERVAL_1) &&
			    qlog[i].value == 63u) hasRestore = 1u;
			if (qlog[i].target == instrumentParam_make(0u, COL_IDX_INTERVAL_1 + 1u) &&
			    qlog[i].value == 66u) has66 = 1u;
		}
		CHECK(hasRestore && has66,
		      "F''s old interval group returns to the patch");
	}
	/* leave this group */
	(void)midiNoteMode_noteOff(0u, 2u, 64u, 0u);
	(void)midiNoteMode_noteOff(0u, 2u, 67u, 0u);

	/* revalidation: a type change drops the group and clears the hold */
	host_reset_calls();
	(void)midiNoteMode_noteOn(0u, 2u, 60u, 0u, 48u, 100u, 0u);
	qn = 0u;
	host_slot_type[0][0] = 0u;
	midiNoteMode_service();
	/* S096 P3 assessment F4: after a type change nothing is queued (the
	 * Colloid's sus index would name another type's row); the hold clears */
	CHECK(qn == 0u && holdOn == 0u,
	      "a type change drops the group without writing the new instrument");
	host_slot_type[0][0] = INSTRUMENT_TYPE_COL;
	host_reset_calls();
	(void)midiNoteMode_noteOn(0u, 2u, 60u, 0u, 48u, 100u, 0u);
	CHECK(normal_calls == 1u, "the dropped group starts again");
	/* the overlay coming on (still a Colloid): sus = off is queued */
	qn = 0u;
	host_overlay = 1u;
	midiNoteMode_service();
	host_overlay = 0u;
	CHECK(qn == 1u && qlog[0].target == instrumentParam_make(0u, COL_IDX_SUSTAIN) &&
	      qlog[0].value == 0u && holdOn == 0u,
	      "the overlay drops a Colloid group with sus off");
	(void)midiNoteMode_noteOn(0u, 2u, 60u, 0u, 48u, 100u, 0u);

	/* a Scene change keeps the keys and re-asserts sus once */
	qn = 0u;
	host_epoch++;
	midiNoteMode_service();
	{
		unsigned hasSus = 0u, hasCoa2 = 0u, i;

		for (i = 0u; i < qn; i++) {
			if (qlog[i].target == instrumentParam_make(0u, COL_IDX_SUSTAIN) &&
			    qlog[i].value == 1u) hasSus = 1u;
			if (qlog[i].target == instrumentParam_make(0u, COL_IDX_COARSE)) hasCoa2 = 1u;
		}
		CHECK(hasSus && hasCoa2, "a Scene change re-asserts the live state");
	}
	qn = 0u;
	midiNoteMode_service();
	CHECK(qn == 0u, "the re-assert happens once per Scene change");

	/* six paraphonic Colloids at once */
	reset_scene();
	host_reset_calls();
	for (t = 0u; t < 6u; t++) {
		host_slot_type[0][t] = INSTRUMENT_TYPE_COL;
		host_track_channel[0][t] = (uint8_t)(2u + t);
	}
	for (t = 0u; t < 6u; t++)
		(void)midiNoteMode_noteOn(t, (uint8_t)(2u + t), 60u, 0u, 48u, 100u, 0u);
	CHECK(normal_calls == 6u, "six paraphonic Colloids trigger");

	/*
	 * S096 P3 assessment F10: every key, full detail. All 128 notes held on
	 * one group (one trigger, the hold on), then released in a shuffled
	 * order: no sus = off and the hold kept until the very last key, then
	 * exactly one sus = off and the hold cleared; the freed group starts
	 * again with a new trigger.
	 */
	reset_scene();
	host_slot_type[0][0] = INSTRUMENT_TYPE_COL;
	host_track_channel[0][0] = 2u;
	{
		uint8_t order[128];
		uint32_t rs = 0x2545F491u;
		unsigned k, early = 0u, lastOff = 0u, i;

		for (k = 0u; k < 128u; k++)
			(void)midiNoteMode_noteOn(0u, 2u, (uint8_t)k, 0u, (uint8_t)k, 100u, 0u);
		CHECK(normal_calls == 1u && holdOn == 1u,
		      "128 keys: one trigger, the hold on");
		for (k = 0u; k < 128u; k++)
			order[k] = (uint8_t)k;
		for (k = 127u; k > 0u; k--) {
			uint8_t tmp;
			unsigned j;

			rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
			j = rs % (k + 1u);
			tmp = order[k]; order[k] = order[j]; order[j] = tmp;
		}
		for (k = 0u; k < 128u; k++) {
			unsigned susOff = 0u;

			qn = 0u;
			(void)midiNoteMode_noteOff(0u, 2u, order[k], 0u);
			for (i = 0u; i < qn; i++)
				if (qlog[i].target == instrumentParam_make(0u, COL_IDX_SUSTAIN) &&
				    qlog[i].value == 0u)
					susOff++;
			if (k < 127u && (susOff != 0u || holdOn != 1u))
				early++;
			if (k == 127u)
				lastOff = susOff;
		}
		CHECK(early == 0u, "128 keys: no release before the last key");
		CHECK(lastOff == 1u && holdOn == 0u,
		      "128 keys: the last key releases once and clears the hold");
		(void)midiNoteMode_noteOn(0u, 2u, 60u, 0u, 60u, 100u, 0u);
		CHECK(normal_calls == 2u, "128 keys: the freed group triggers again");
	}

	printf("%s (%u failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
