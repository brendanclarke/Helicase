/*
 * seq_steplen.c - host law AL-T: seq_trackStepSamples() (S093 P4).
 *
 * What:       compiles the extracted firmware function (build/gen/
 *             seq_stepsamples.c) against the real StepScale.c and checks:
 *               - 40/120/300 BPM x the 1/32 (CC 38), 1/16 (CC 76, default)
 *                 and 1/8 (CC 93) stops: an exact table, and equality with a
 *                 double-precision round(ticksQ8 x 44108 x 60 / (256 x 96 x
 *                 bpm));
 *               - a track index >= NUM_TRACKS uses the default scale;
 *               - the longest step (CC 127 at 1 BPM) = 5,292,960;
 *               - BPM 0 is treated as 1.
 * Why:        the 64-bit tick product is the point: in 32 bits it overflows
 *             at every tempo (6144 x 2,646,480 > 2^32); the Makefile break
 *             build proves this law sees it (21 failures).
 * Inputs:     none.  Outputs: a line per failure; exit 1 on any.
 * Accessors:  make -C tools/dsp_test seq_steplen.
 * Affiliates: Core/Sequencer/sequencer.c seq_trackStepSamples(),
 *             StepScale.c (stepScale_q8Table, the musical stops).
 */
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "StepScale.h"

/* the two sequencer statics the extracted function reads, owned here */
#define NUM_TRACKS 7u
static uint8_t seq_effectiveTrackScale[NUM_TRACKS];
static uint16_t seq_tempo = 120u;
#include "seq_stepsamples.c"

static int failures;

static void expect(int cond, const char *msg, unsigned a, unsigned b)
{
	if (!cond) {
		printf("FAIL: %s (%u, %u)\n", msg, a, b);
		failures++;
	}
}

int main(void)
{
	static const uint16_t bpms[3] = { 40u, 120u, 300u };
	/* the 1/32, 1/16 and 1/8 musical stops (StepScale.c stepScale_stops[]) */
	static const uint8_t scales[3] = { 38u, STEP_SCALE_DEFAULT, 93u };
	/* samples per step, [bpm][scale]; 120 BPM 1/16 = 5514 = the alkaline
	 * bench's ALK_STEP */
	static const uint32_t want[3][3] = {
		{ 8270u, 16541u, 33081u },
		{ 2757u,  5514u, 11027u },
		{ 1103u,  2205u,  4411u },
	};
	unsigned b, s;

	/* the table, and the double-precision reference at every point */
	for (b = 0u; b < 3u; b++)
		for (s = 0u; s < 3u; s++) {
			const double ref = floor((double)stepScale_ticksQ8(scales[s]) * 44108.0 * 60.0
			                         / (256.0 * 96.0 * bpms[b]) + 0.5);
			uint32_t got;

			seq_tempo = bpms[b];
			seq_effectiveTrackScale[2] = scales[s];
			got = seq_trackStepSamples(2u);
			expect(got == want[b][s], "AL-T table", got, want[b][s]);
			expect((double)got == ref, "AL-T against double", got, (unsigned)ref);
		}
	/* an out-of-range track index falls back to the 1/16 */
	seq_tempo = 120u;
	expect(seq_trackStepSamples(NUM_TRACKS) == 5514u, "AL-T default scale beyond NUM_TRACKS",
	       seq_trackStepSamples(NUM_TRACKS), 5514u);
	/* the longest step fits 32 bits, and BPM 0 counts as 1 */
	seq_tempo = 1u;
	seq_effectiveTrackScale[0] = 127u;
	expect(seq_trackStepSamples(0u) == 5292960u, "AL-T longest step",
	       seq_trackStepSamples(0u), 5292960u);
	seq_tempo = 0u;
	expect(seq_trackStepSamples(0u) == 5292960u, "AL-T BPM 0 as 1",
	       seq_trackStepSamples(0u), 5292960u);
	printf("seq_steplen: %s (%d failures)\n", failures ? "FAILED" : "OK", failures);
	return failures ? 1 : 0;
}
