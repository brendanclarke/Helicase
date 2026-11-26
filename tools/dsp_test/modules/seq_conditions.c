/*
 * seq_conditions.c - host laws SC-1ST .. SC-PRED for the step conditions
 * (S104 P1).
 *
 * What:       compiles the extracted firmware functions (build/gen/
 *             seq_conditions.c: seq_repeatCountNext(),
 *             seq_conditionRepeatPasses(), seq_conditionEval(),
 *             seq_rampConditionPredict()) and checks:
 *               SC-1ST  over 300 passes from a reset, `1:-` (20) passes only
 *                       on pass 0 and `!1-` (21) on every other pass;
 *               SC-XY   every X:Y (11..19) matches c % Y == X - 1 against an
 *                       unbounded counter, over 1,000 passes (the 255 -> 4
 *                       wrap keeps the sequence);
 *               SC-WRAP at 255 -> next, `1:-` does not pass and `1:3` does not
 *                       pass on two consecutive passes;
 *               SC-MAP  22 follows seq_fillActive, 24 seq_loopActive, 26
 *                       seq_trackLastConditionFired, 28 + v / 35 + v the mute
 *                       bits of voice v, and 42 always passes;
 *               SC-PRED for 11..21 the predictor with the next pass's count
 *                       equals the evaluation after that pass; 26/27 match.
 * Why:        the condition table was renumbered (an insertion at 21) and the
 *             one-byte repeat counter was given a wrap rule; both are silent
 *             failures on hardware, so the host laws pin them. The Makefile
 *             break build removes the wrap rule and must fail SC-WRAP (and
 *             SC-1ST), proving the laws see the old 255 -> 0 behaviour.
 * Inputs:     none.  Outputs: a line per failure; exit 1 on any.
 * Accessors:  make -C tools/dsp_test seq_conditions.
 * Affiliates: Core/Sequencer/sequencer.c (the extracted functions),
 *             Core/Menu/MenuText.h conditionNames[].
 */
#include <stdint.h>
#include <stdio.h>

typedef struct {
	uint8_t condition;
} pat_step_specials_t;

#define NUM_TRACKS 7u
static uint8_t seq_trackRepeatCount[NUM_TRACKS];
static uint8_t seq_trackLastConditionFired[NUM_TRACKS];
static uint8_t seq_fillActive;
static uint8_t seq_loopActive;
static uint8_t seq_mutedTracks;

static uint16_t seq_conditionsRng = 0x1234u;

static int16_t GetRngValue(void)
{
	seq_conditionsRng = (uint16_t)(seq_conditionsRng * 1103515245u + 12345u);
	return (int16_t)seq_conditionsRng;
}

#include "seq_conditions_funcs.c"

static int failures;

static void expect(int cond, const char *msg, unsigned a, unsigned b)
{
	if (!cond) {
		printf("FAIL: %s (%u, %u)\n", msg, a, b);
		failures++;
	}
}

static int passes(unsigned cond, unsigned count)
{
	pat_step_specials_t sp = { (uint8_t)cond };

	seq_trackRepeatCount[0] = (uint8_t)count;
	return seq_conditionEval(&sp, 0u, 1u) != 0u;
}

int main(void)
{
	unsigned i, v;
	uint8_t c;

	/* SC-1ST: 1:- on pass 0 only, !1- on every other pass. */
	c = 0u;
	for (i = 0u; i < 300u; i++) {
		expect(passes(20u, c) == (c == 0u), "SC-1ST 1:-", i, c);
		expect(passes(21u, c) == (c != 0u), "SC-1ST !1-", i, c);
		c = seq_repeatCountNext(c);
	}
	expect(c == 48u, "SC-1ST count after 300 passes", 300u, c);

	/* SC-XY: each X:Y against the unbounded count, through the wrap. */
	static const uint8_t xy[9][2] = {
		{1u,2u},{2u,2u},{1u,3u},{2u,3u},{3u,3u},
		{1u,4u},{2u,4u},{3u,4u},{4u,4u}
	};
	c = 0u;
	for (i = 0u; i < 1000u; i++) {
		unsigned cond;

		for (cond = 11u; cond <= 19u; cond++) {
			const unsigned y = xy[cond - 11u][1];
			const unsigned x = xy[cond - 11u][0];

			expect(passes(cond, c) == (i % y == x - 1u), "SC-XY", i, cond);
		}
		c = seq_repeatCountNext(c);
	}

	/* SC-WRAP: 255 -> next keeps 1:- off and 1:3 off the second time. */
	c = 255u;
	expect(passes(20u, c) == 0u, "SC-WRAP 1:- at 255", c, 0u);
	expect(passes(13u, c) != 0u, "SC-WRAP 1:3 at 255", c, 0u);
	expect(seq_repeatCountNext(255u) == 4u, "SC-WRAP next", 255u,
	       seq_repeatCountNext(255u));
	expect(passes(20u, 4u) == 0u, "SC-WRAP 1:- after wrap", 4u, 0u);
	expect(passes(13u, 4u) == 0u, "SC-WRAP 1:3 after wrap", 4u, 0u);

	/* SC-MAP: one value per renumbered group, and an undefined value. */
	seq_fillActive = 1u;
	expect(passes(22u, 0u) == 1u, "SC-MAP fil", 22u, 0u);
	seq_fillActive = 0u;
	expect(passes(23u, 0u) == 1u, "SC-MAP !fl", 23u, 0u);
	seq_loopActive = 1u;
	expect(passes(24u, 0u) == 1u, "SC-MAP lop", 24u, 0u);
	seq_loopActive = 0u;
	expect(passes(25u, 0u) == 1u, "SC-MAP !lp", 25u, 0u);
	seq_trackLastConditionFired[0] = 1u;
	expect(passes(26u, 0u) == 1u, "SC-MAP lst", 26u, 0u);
	expect(passes(27u, 0u) == 0u, "SC-MAP !ls", 27u, 0u);
	seq_trackLastConditionFired[0] = 0u;
	expect(passes(27u, 0u) == 1u, "SC-MAP !ls fire", 27u, 0u);
	for (v = 0u; v < NUM_TRACKS; v++) {
		seq_mutedTracks = (uint8_t)(1u << v);
		expect(passes(28u + v, 0u) == 1u, "SC-MAP muted", v, 28u + v);
		expect(passes(35u + v, 0u) == 0u, "SC-MAP not-muted", v, 35u + v);
		seq_mutedTracks = (uint8_t)~seq_mutedTracks;
		expect(passes(28u + v, 0u) == 0u, "SC-MAP muted other", v, 28u + v);
		expect(passes(35u + v, 0u) == 1u, "SC-MAP not-muted other", v, 35u + v);
	}
	expect(passes(42u, 0u) == 1u, "SC-MAP 42 passes", 42u, 0u);

	/* SC-PRED: the predictor with the next pass's count. */
	c = 1u;
	for (i = 0u; i < 400u; i++) {
		unsigned cond;

		for (cond = 11u; cond <= 21u; cond++) {
			pat_step_specials_t sp = { (uint8_t)cond };
			const uint8_t next = seq_repeatCountNext(c);
			uint8_t pred;
			const uint8_t actual = (uint8_t)passes(cond, next);

			seq_trackRepeatCount[0] = c;
			pred = seq_rampConditionPredict(&sp, 0u, 1u);
			expect(pred == actual, "SC-PRED", cond, pred);
		}
		seq_trackLastConditionFired[0] = (uint8_t)(i & 1u);
		{
			pat_step_specials_t s26 = { 26u };
			pat_step_specials_t s27 = { 27u };

			expect(seq_rampConditionPredict(&s26, 0u, 1u) ==
			       seq_trackLastConditionFired[0], "SC-PRED lst", 26u, 0u);
			expect(seq_rampConditionPredict(&s27, 0u, 1u) ==
			       (uint8_t)!seq_trackLastConditionFired[0], "SC-PRED !ls",
			       27u, 0u);
		}
		c = seq_repeatCountNext(c);
	}

	if (failures)
		printf("seq_conditions: %d failure(s)\n", failures);
	else
		printf("seq_conditions: all laws pass\n");
	return failures ? 1 : 0;
}
