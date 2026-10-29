/*
 * Run-mode module host tests (S088 P0 Layer 2 gates).
 *
 * What:       compiles the real slopeEg2_retriggerFrom()/slopeEg2_calc()/
 *             slopeEg2_calcSlopeValue()/slopeEg2_isAudibleGain() and the real
 *             voicePost_drum()/voicePost_mixAddGainDist()/
 *             voicePost_addGainDist() from the firmware headers, plus the real
 *             BufferTools gain helpers (extract.py, by name).
 *             Laws:
 *               A1 gain match: the inverted attack curve returns currentGain
 *                  for every slope and every gain in (0, 1);
 *               A2 the retrigger enters EG_A with a cleared repeat counter;
 *               A3 repeat mode falls back to slopeEg2_trigger() (EG_REPEAT,
 *                  value 1.0) - A10;
 *               A4 the audible threshold is exactly 1/32768;
 *               B1 equal post-chain endpoints are bit-identical to the S073
 *                  constant-gain formula (the rst guarantee);
 *               B2 an armed post-chain ramp starts at lastGain and ends at
 *                  gain (the click killer);
 *               B3 an armed Drum velocity ramp starts at lastVeloGain and ends
 *                  at veloGain;
 *               B4 the Snare and Cymbal/HiHat twin chains behave the same way.
 * Why:        these are the L2a/L2b gates: the rst output must not change and
 *             the armed block must actually glide. Break proof: the Makefile's
 *             break build replaces `currentGain / denom` with `currentGain`, so
 *             A1 must fail (and the target fails if it does not).
 * Inputs:     build/gen/rm_*.c from the run_modes rule.
 * Outputs:    one line per failure (first 40); "run_modes failures: N"; exit 1
 *             if N > 0.
 * Accessors:  make -C tools/dsp_test run_modes.
 * Affiliates: SlopeEg2.c/.h, voicePostChain.h, BufferTools.h, DSP_TEST.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>

/*
 * Distortion stand-in. prelude.h supplies the Distortion type; the identity
 * curve below makes the gain ramps observable exactly. Both the new and the
 * reference formulas use it, so B1 still proves the bit-identity of the ramp.
 */
static inline int16_t distortion_curveSample16(const Distortion *dist,
                                               int16_t x)
{
	(void)dist;
	return x;
}

#include "rm_slope_type.c"
#include "rm_slope_consts.c"
#include "rm_slope_eg.c"
#include "rm_audible.c"
#include "rm_buffertools.c"
#include "rm_postchain.c"

static int failures;

static void expect(int cond, const char *msg)
{
	if (!cond) {
		if (failures < 40)
			printf("FAIL: %s\n", msg);
		failures++;
	}
}

static void set_slope(SlopeEg2 *eg, uint8_t data2)
{
	slopeEg2_init(eg);
	slopeEg2_setSlope(eg, data2);
}

/*
 * Gain-match tolerance (ported from the S085 P2 test).
 *
 * What:       1e-5 plus the output change that 8 float steps of the raw value
 *             make on the attack curve: 8 x FLT_EPSILON x v x |df/dv|, with
 *             df/dv = (1 + k) / (1 + k v)^2.
 * Why:        near slope 0 or 127 the curve is extremely steep (k = +3998 or
 *             -0.99975), so a raw-value LSB is a much larger output step at
 *             the other end. This is float resolution, not a gain mismatch.
 */
static double match_tolerance(const SlopeEg2 *eg)
{
	const double k = eg->invSlope;
	const double v = eg->value;
	const double d = 1.0 + k * v;
	const double slope = (1.0 + k) / (d * d);

	return 1e-5 + 8.0 * FLT_EPSILON * v * fabs(slope);
}

static void test_amp_retrigger(void)
{
	static const uint8_t slopes[5] = { 0u, 32u, 64u, 96u, 127u };
	static const float gains[10] = { 0.0f, 1.0f / 32768.0f, 0.001f, 0.01f,
	                                 0.1f, 0.25f, 0.5f, 0.75f, 0.99f, 1.0f };
	unsigned s, g;

	for (s = 0u; s < 5u; s++) {
		for (g = 0u; g < 10u; g++) {
			SlopeEg2 eg;
			float back;
			double tol;

			set_slope(&eg, slopes[s]);
			eg.value = 0.0f;
			eg.state = EG_D;
			slopeEg2_retriggerFrom(&eg, gains[g]);
			back = slopeEg2_calcSlopeValue(eg.value, eg.invSlope);
			tol = match_tolerance(&eg);
			expect(fabs((double)back - gains[g]) <= tol,
			       "A1 the inverse attack curve returns the gain");
			expect(eg.state == EG_A, "A2 the retrigger enters attack");
			expect(eg.repeatCnt == 0u, "A2 the repeat counter is cleared");

			/* A2b: the attack rises from the matched value (no dip). */
			{
				float prev = back;
				unsigned n;

				for (n = 0u; n < 2000u && eg.state == EG_A; n++) {
					const float out = slopeEg2_calc(&eg);

					expect(out >= prev - 1e-6f,
					       "A2b the matched attack never dips");
					prev = out;
				}
			}
		}
	}

	/* A3: repeat mode falls back to trigger(). */
	{
		SlopeEg2 eg;

		set_slope(&eg, 64u);
		eg.repeat = 2u;
		eg.value = 0.3f;
		slopeEg2_retriggerFrom(&eg, 0.5f);
		expect(eg.state == EG_REPEAT, "A3 repeat mode enters repeat");
		expect(eg.value == 1.0f, "A3 repeat mode starts at 1.0");
		expect(eg.repeatCnt == 2u, "A3 the repeat counter reloads");
	}

	/* A4: the audible threshold is one int16 LSB. */
	expect(slopeEg2_isAudibleGain(0.0f) == 0u, "A4 zero is inaudible");
	expect(slopeEg2_isAudibleGain(1.0f / 65536.0f) == 0u,
	       "A4 half an LSB is inaudible");
	expect(slopeEg2_isAudibleGain(1.0f / 32768.0f) == 1u,
	       "A4 one LSB is audible");
	expect(slopeEg2_isAudibleGain(0.5f) == 1u, "A4 a mid gain is audible");
}

/* The pre-S088 post-chain formulas, kept here as the reference. */
static void reference_drum(const int16_t *in, int16_t *out, float gain,
                           float lastGain, float velo, uint8_t size)
{
	uint8_t i;
	const float inv = (size > 1u) ? 1.f / (size - 1.f) : 0.f;

	for (i = 0u; i < size; i++) {
		const int16_t eg = bufferTool_floatToInt16Store(
			in[i] * bufferTool_interpolatedGain(i, inv, gain, lastGain));
		out[i] = bufferTool_floatToInt16Store(eg * velo);
	}
}

static void reference_amp(const int16_t *in, const int16_t *add, int16_t *out,
                          float ampGain, uint8_t size, int mixMode, float mix)
{
	uint8_t i;

	for (i = 0u; i < size; i++) {
		int16_t s = in[i];

		if (mixMode)
			s = bufferTool_satAdd16(
				bufferTool_floatToInt16Store(in[i] * mix), add[i]);
		else
			s = bufferTool_satAdd16(s, add[i]);
		out[i] = bufferTool_floatToInt16Store(s * ampGain);
	}
}

static void test_postchain(void)
{
	static int16_t in[OUTPUT_DMA_SIZE];
	static int16_t live[OUTPUT_DMA_SIZE];
	static int16_t ref[OUTPUT_DMA_SIZE];
	static int16_t add[OUTPUT_DMA_SIZE];
	Distortion dist = { 0 };
	unsigned i;

	for (i = 0u; i < OUTPUT_DMA_SIZE; i++) {
		in[i] = (int16_t)(((int)i - 16) * 1300);
		add[i] = (int16_t)(i * 97u - 1500u);
	}

	/* B1: equal endpoints are exactly the S073 output (the rst guarantee). */
	{
		static const float gains[3] = { 0.37f, 1.0f, 0.0f };
		unsigned k;

		for (k = 0u; k < 3u; k++) {
			memcpy(live, in, sizeof(live));
			voicePost_drum(live, gains[k], gains[k], 0.8f, 0.8f, &dist,
			               OUTPUT_DMA_SIZE);
			reference_drum(in, ref, gains[k], gains[k], 0.8f, OUTPUT_DMA_SIZE);
			for (i = 0u; i < OUTPUT_DMA_SIZE; i++)
				expect(live[i] == ref[i],
				       "B1 equal Drum endpoints are bit-identical");

			memcpy(live, in, sizeof(live));
			voicePost_mixAddGainDist(live, add, 0.6f, gains[k], gains[k],
			                         &dist, OUTPUT_DMA_SIZE);
			reference_amp(in, add, ref, gains[k], OUTPUT_DMA_SIZE, 1, 0.6f);
			for (i = 0u; i < OUTPUT_DMA_SIZE; i++)
				expect(live[i] == ref[i],
				       "B4 equal Snare endpoints are bit-identical");

			memcpy(live, in, sizeof(live));
			voicePost_addGainDist(live, add, gains[k], gains[k], &dist,
			                      OUTPUT_DMA_SIZE);
			reference_amp(in, add, ref, gains[k], OUTPUT_DMA_SIZE, 0, 0.0f);
			for (i = 0u; i < OUTPUT_DMA_SIZE; i++)
				expect(live[i] == ref[i],
				       "B4 equal Cymbal/HiHat endpoints are bit-identical");
		}
	}

	/* B2: an armed amp ramp starts at lastGain and ends at gain. */
	{
		memcpy(live, in, sizeof(live));
		voicePost_addGainDist(live, add, 1.0f, 0.25f, &dist,
		                      OUTPUT_DMA_SIZE);
		reference_amp(in, add, ref, 0.25f, OUTPUT_DMA_SIZE, 0, 0.0f);
		expect(live[0] == ref[0], "B2 an armed ramp starts at lastAmpGain");
		reference_amp(in, add, ref, 1.0f, OUTPUT_DMA_SIZE, 0, 0.0f);
		expect(live[OUTPUT_DMA_SIZE - 1u] == ref[OUTPUT_DMA_SIZE - 1u],
		       "B2 an armed ramp ends at ampGain");
	}

	/* B3: an armed Drum velocity ramp starts at lastVeloGain. */
	{
		memcpy(live, in, sizeof(live));
		voicePost_drum(live, 0.5f, 0.5f, 0.5f, 1.0f, &dist,
		               OUTPUT_DMA_SIZE);
		reference_drum(in, ref, 0.5f, 0.5f, 1.0f, OUTPUT_DMA_SIZE);
		expect(live[0] == ref[0], "B3 an armed velocity ramp starts at last");
		reference_drum(in, ref, 0.5f, 0.5f, 0.5f, OUTPUT_DMA_SIZE);
		expect(live[OUTPUT_DMA_SIZE - 1u] == ref[OUTPUT_DMA_SIZE - 1u],
		       "B3 an armed velocity ramp ends at veloGain");
	}

	/* B4: the Snare ramp starts at lastAmpGain too. */
	{
		memcpy(live, in, sizeof(live));
		voicePost_mixAddGainDist(live, add, 0.0f, 1.0f, 0.25f, &dist,
		                         OUTPUT_DMA_SIZE);
		reference_amp(in, add, ref, 0.25f, OUTPUT_DMA_SIZE, 1, 0.0f);
		expect(live[0] == ref[0], "B4 an armed Snare ramp starts at last");
		reference_amp(in, add, ref, 1.0f, OUTPUT_DMA_SIZE, 1, 0.0f);
		expect(live[OUTPUT_DMA_SIZE - 1u] == ref[OUTPUT_DMA_SIZE - 1u],
		       "B4 an armed Snare ramp ends at ampGain");
	}
}

int main(void)
{
	test_amp_retrigger();
	test_postchain();
	printf("run_modes failures: %d\n", failures);
	return failures ? 1 : 0;
}
