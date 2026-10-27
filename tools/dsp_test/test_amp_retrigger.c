/*
 * Amp envelope retrigger and post-chain ramp checks (S085 P2).
 *
 * What:       1. gain match: for slope 0..127 (step 1), gains
 *                {0, 1/32768, 0.01, 0.1, 0.25, 0.5, 0.75, 0.99, 1} and
 *                attack settings {1, 20, 30, 64, 127}:
 *                slopeEg2_retriggerFrom(g) then the attack curve at the new
 *                raw value (slopeEg2_calcSlopeValue(value, invSlope)) equals
 *                g within 1e-5 (plus the float-resolution term of
 *                match_tolerance() at steep slopes); the following
 *                slopeEg2_calc() outputs are non-decreasing until the
 *                envelope reaches EG_D (no dip after the retrigger); state
 *                is EG_A.
 *             2. repeat mode: repeat > 0 gives value 1 and EG_REPEAT (the
 *                documented fallback).
 *             3. audible test: slopeEg2_isAudibleGain() is 0 below 1/32768
 *                and 1 at and above it.
 *             4. ramp wiring: on a constant input block, with distortion
 *                shape 0, voicePost_addGainDist() and
 *                voicePost_mixAddGainDist() with last != current produce
 *                sample 0 at the last gain and sample 31 at the current
 *                gain, monotonic in between; voicePost_drum() does the same
 *                for its velocity endpoints. The reference values come from
 *                the same truncation steps, written out in the test.
 *             5. no step: a simulated Snare-style sequence (decay at slope
 *                96 to gain ~0.76, fre retrigger at attack 25, the armed
 *                ramp block, then three attack blocks) has a largest
 *                sample-to-sample gain change no bigger than one block's
 *                normal attack increment, and never dips below the gain
 *                before the trigger (the downward form of the step).
 * Why:        the P2 gate; every check fails on a broken change (C7.5).
 * Outputs:    "N failures"; nonzero exit on failure.
 * Accessors:  make -C tools/dsp_test ampretrig.
 * Affiliates: SlopeEg2.c, voicePostChain.h, test_postchain.c.
 */
#include "prelude_osc.h"
#include <float.h>
#include "SlopeEg2.h"
#include "voicePostChain.h"

/* Non-static in SlopeEg2.c without a declaration (C7.2). */
extern float slopeEg2_calcSlopeValue(float val, float slope);

static unsigned failures;

static void fail(const char *what, int a, int b, double x, double y)
{
	if (failures < 20u)
		printf("FAIL %s (%d, %d): %.9g vs %.9g\n", what, a, b, x, y);
	failures++;
}

/*
 * Gain-match tolerance (S085 P2 implementation note, §11.2 of the
 * schedule).
 *
 * What:       1e-5, plus the gain change that 8 float steps of the raw value
 *             make on the attack curve: 8 * FLT_EPSILON * v * |df/dv|, with
 *             df/dv = (1+k) / (1+k*v)^2, k = invSlope.
 * Why:        near slope 127 the attack curve is so steep near v = 1 that
 *             the nearest float v misses g by up to ~1e-4 (−80 dB): a float
 *             representation limit, not an inversion error. Everywhere else
 *             the term is far below 1e-5, so the plan's 1e-5 still applies.
 *             A wrong inversion (B27, B28) misses by 0.1 or more.
 */
static double match_tolerance(const SlopeEg2 *eg)
{
	const double k = eg->invSlope;
	const double v = eg->value;
	const double d = 1.0 + k * v;
	const double slope = (1.0 + k) / (d * d);

	return 1e-5 + 8.0 * FLT_EPSILON * v * fabs(slope);
}

/*
 * Check 1: the gain match and no dip after it.
 */
static void check_gain_match(void)
{
	static const float gains[] = { 0.0f, 1.0f / 32768.0f, 0.01f, 0.1f,
	                               0.25f, 0.5f, 0.75f, 0.99f, 1.0f };
	static const uint8_t attacks[] = { 1u, 20u, 30u, 64u, 127u };
	unsigned s, gi, ai;

	for (s = 0u; s <= 127u; s++)
		for (gi = 0u; gi < sizeof(gains) / sizeof(gains[0]); gi++)
			for (ai = 0u; ai < sizeof(attacks); ai++) {
				SlopeEg2 eg;
				const float g = gains[gi];
				float prev, match;
				double tol;
				unsigned n;

				slopeEg2_init(&eg);
				slopeEg2_setSlope(&eg, (uint8_t)s);
				slopeEg2_setAttack(&eg, attacks[ai], 0u);
				slopeEg2_setDecay(&eg, 64u, 0u);
				/* A stale raw value, as after part of a decay. */
				eg.value = 0.5f;
				eg.state = EG_D;

				slopeEg2_retriggerFrom(&eg, g);
				if (eg.state != EG_A)
					fail("state", (int)s, (int)gi, eg.state, EG_A);
				match = slopeEg2_calcSlopeValue(eg.value, eg.invSlope);
				tol = match_tolerance(&eg);
				if (fabs((double)match - g) > tol)
					fail("gain match", (int)s, (int)gi, match, g);

				/* No dip: the attack only rises from g. */
				prev = g;
				for (n = 0u; n < 100000u && eg.state == EG_A; n++) {
					const float out = slopeEg2_calc(&eg);

					if (out < prev - tol) {
						fail("dip", (int)s, (int)attacks[ai], out, prev);
						break;
					}
					prev = out;
				}
			}
}

/*
 * Check 2: repeat mode keeps the documented fallback.
 */
static void check_repeat(void)
{
	SlopeEg2 eg;

	slopeEg2_init(&eg);
	slopeEg2_setSlope(&eg, 96u);
	slopeEg2_setAttack(&eg, 20u, 0u);
	eg.repeat = 3u;
	eg.value = 0.25f;
	eg.state = EG_D;
	slopeEg2_retriggerFrom(&eg, 0.4f);
	if (eg.state != EG_REPEAT)
		fail("repeat state", 0, 0, eg.state, EG_REPEAT);
	if (eg.value != 1.0f)
		fail("repeat value", 0, 0, eg.value, 1.0);
	if (eg.repeatCnt != 3u)
		fail("repeat count", 0, 0, eg.repeatCnt, 3);
}

/*
 * Check 3: the audible threshold is one output step.
 */
static void check_audible(void)
{
	const float step = 1.0f / 32768.0f;

	if (slopeEg2_isAudibleGain(0.0f) != 0u)
		fail("audible 0", 0, 0, 1, 0);
	if (slopeEg2_isAudibleGain(nextafterf(step, 0.0f)) != 0u)
		fail("audible below", 0, 0, 1, 0);
	if (slopeEg2_isAudibleGain(step) != 1u)
		fail("audible at", 0, 0, 0, 1);
	if (slopeEg2_isAudibleGain(0.01f) != 1u)
		fail("audible 0.01", 0, 0, 0, 1);
	if (slopeEg2_isAudibleGain(1.0f) != 1u)
		fail("audible 1", 0, 0, 0, 1);
}

/*
 * Reference chain steps for check 4, written out (not the helpers under
 * test): the ramp gain for sample j, the int16 truncation, and the
 * distortion curve at the given shape.
 */
static float ref_ramp(unsigned j, float cur, float last)
{
	const float inv_size = 1.f / (OUTPUT_DMA_SIZE - 1.f);
	const float frac = j * inv_size;

	return last + frac * (cur - last);
}

static int16_t ref_trunc(float x)
{
	return (int16_t)(int32_t)x;
}

static int16_t ref_dist(float shape, int16_t in)
{
	float x = in / 32767.f;

	x = (1 + shape) * x / (1 + shape * fabsf(x));
	return (int16_t)(int32_t)(x * 32767);
}

/*
 * Check 4 helper: sample 0 at the last gain, sample 31 at the current gain,
 * every sample equal to the written-out reference, monotonic in between.
 */
static void check_ramp_block(const char *what, const int16_t *out,
                             const int16_t *ref, int rising)
{
	unsigned j;

	for (j = 0u; j < OUTPUT_DMA_SIZE; j++)
		if (out[j] != ref[j]) {
			fail(what, (int)j, 0, out[j], ref[j]);
			return;
		}
	for (j = 1u; j < OUTPUT_DMA_SIZE; j++)
		if (rising ? out[j] < out[j - 1] : out[j] > out[j - 1]) {
			fail(what, (int)j, 1, out[j], out[j - 1]);
			return;
		}
}

/*
 * Check 4: the post-chains ramp from the "last" endpoint to the current one.
 */
static void check_ramp_wiring(void)
{
	static const float pairs[][2] = { { 0.8f, 0.2f }, { 0.2f, 0.8f } };
	const int16_t in = 30000;
	const float mix = 0.5f;
	Distortion dist;
	unsigned p, j;

	setDistortionShape(&dist, 0u);
	for (p = 0u; p < 2u; p++) {
		const float cur = pairs[p][0];
		const float last = pairs[p][1];
		const int rising = cur > last;
		int16_t buf[OUTPUT_DMA_SIZE], add[OUTPUT_DMA_SIZE];
		int16_t ref[OUTPUT_DMA_SIZE];

		/* Cymbal/HiHat: add (zero) -> ramp -> distortion. */
		for (j = 0u; j < OUTPUT_DMA_SIZE; j++) {
			buf[j] = in;
			add[j] = 0;
			ref[j] = ref_dist(dist.shape,
			                  ref_trunc(in * ref_ramp(j, cur, last)));
		}
		voicePost_addGainDist(buf, add, cur, last, &dist, OUTPUT_DMA_SIZE);
		if (ref[0] != ref_dist(dist.shape, ref_trunc(in * last)))
			fail("addGain ref 0", (int)p, 0, ref[0], 0);
		check_ramp_block("addGain ramp", buf, ref, rising);

		/* Snare: mix -> add (zero) -> ramp -> distortion. */
		for (j = 0u; j < OUTPUT_DMA_SIZE; j++) {
			const int16_t m = ref_trunc(in * mix);

			buf[j] = in;
			ref[j] = ref_dist(dist.shape,
			                  ref_trunc(m * ref_ramp(j, cur, last)));
		}
		voicePost_mixAddGainDist(buf, add, mix, cur, last, &dist,
		                         OUTPUT_DMA_SIZE);
		check_ramp_block("mixAddGain ramp", buf, ref, rising);

		/* Drum: flat envelope 1.0, velocity ramp last -> cur. */
		for (j = 0u; j < OUTPUT_DMA_SIZE; j++) {
			const int16_t eg = ref_trunc(in * ref_ramp(j, 1.0f, 1.0f));

			buf[j] = in;
			ref[j] = ref_dist(dist.shape,
			                  ref_trunc(eg * ref_ramp(j, cur, last)));
		}
		voicePost_drum(buf, 1.0f, 1.0f, cur, last, &dist, OUTPUT_DMA_SIZE);
		check_ramp_block("drum velo ramp", buf, ref, rising);

		/* Sample 0 at the last gain and sample 31 at the current gain. */
		if (abs(buf[0] - ref_trunc(in * last)) > 1)
			fail("drum velo first", (int)p, 0, buf[0], in * last);
		if (abs(buf[OUTPUT_DMA_SIZE - 1] - ref_trunc(in * cur)) > 1)
			fail("drum velo last", (int)p, 0,
			     buf[OUTPUT_DMA_SIZE - 1], in * cur);
	}
}

/*
 * Check 5: a Snare-style fre retrigger has no step.
 *
 * The engine flow is written out (Snare.c is not compiled on the host):
 * per block, the async calc gives egValueOscVol; the render ramps from
 * lastAmpGain when armed, else flat; then records and clears. The level is
 * read back from the real voicePost_mixAddGainDist() on a constant block.
 */
static void check_no_step(void)
{
	const int16_t in = 30000;
	const double quantum = 3.0 / in;
	SlopeEg2 eg;
	Distortion dist;
	float egValue = 0.0f, lastAmpGain = 0.0f, gainBefore;
	float levels[5 * OUTPUT_DMA_SIZE];
	float blockGain[5];
	unsigned n = 0u, b, j, guard;
	uint8_t arm = 0u;
	double maxStep = 0.0, maxInc = 0.0;

	setDistortionShape(&dist, 0u);
	slopeEg2_init(&eg);
	slopeEg2_setSlope(&eg, 96u);
	slopeEg2_setAttack(&eg, 25u, 0u);
	slopeEg2_setDecay(&eg, 90u, 0u);

	/* First note: attack, then decay until the gain is about 0.76. */
	slopeEg2_trigger(&eg);
	for (guard = 0u; guard < 100000u; guard++) {
		egValue = slopeEg2_calc(&eg);
		if (eg.state == EG_D && egValue <= 0.76f)
			break;
	}
	lastAmpGain = egValue;
	gainBefore = egValue;

	/* fre retrigger: gain-matched attack, armed when audible. */
	slopeEg2_retriggerFrom(&eg, egValue);
	arm = slopeEg2_isAudibleGain(egValue);
	if (!arm)
		fail("no-step arm", 0, 0, egValue, 0);

	/* The armed ramp block, then three attack blocks. */
	for (b = 0u; b < 4u; b++) {
		int16_t buf[OUTPUT_DMA_SIZE], add[OUTPUT_DMA_SIZE];
		float ampGain, last;

		egValue = slopeEg2_calc(&eg);
		ampGain = egValue;
		last = arm ? lastAmpGain : ampGain;
		for (j = 0u; j < OUTPUT_DMA_SIZE; j++) {
			buf[j] = in;
			add[j] = 0;
		}
		voicePost_mixAddGainDist(buf, add, 1.0f, ampGain, last, &dist,
		                         OUTPUT_DMA_SIZE);
		lastAmpGain = ampGain;
		arm = 0u;
		blockGain[b] = ampGain;
		for (j = 0u; j < OUTPUT_DMA_SIZE; j++)
			levels[n++] = buf[j] / (float)in;
	}

	/* One block's normal attack increment, from the attack blocks. */
	for (b = 1u; b < 4u; b++) {
		const double inc = fabs((double)blockGain[b] - blockGain[b - 1]);

		if (inc > maxInc)
			maxInc = inc;
	}
	for (j = 0u; j < n; j++) {
		const double prev = j ? levels[j - 1] : gainBefore;
		const double step = fabs((double)levels[j] - prev);

		if (step > maxStep)
			maxStep = step;
		if (levels[j] < gainBefore - quantum)
			fail("no-step dip", (int)j, 0, levels[j], gainBefore);
	}
	if (maxStep > maxInc + quantum)
		fail("no-step step", 0, 0, maxStep, maxInc);
	printf("no-step: gain before %.4f, largest sample step %.5f, "
	       "attack increment per block %.5f\n", gainBefore, maxStep, maxInc);
}

int main(void)
{
	check_gain_match();
	check_repeat();
	check_audible();
	check_ramp_wiring();
	check_no_step();
	printf("ampretrig: %u failures\n", failures);
	return failures ? 1 : 0;
}
