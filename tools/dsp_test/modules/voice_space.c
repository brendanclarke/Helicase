/*
 * voiceSpace host tests (S101 P3).
 *
 * What:       compiles the real Core/DSPAudio/voiceSpace.c (extract.py --body)
 *             and checks the laws of S101_P3_BASIC_CHAIN_SPATIALIZER.md §8:
 *               SP-RING  the mirrored ring against a plain modulo-ring
 *                        reference of the same frame, bit for bit, over
 *                        hundreds of blocks (every block phase of the 68),
 *                        with the time, spread, feedback and tone swept;
 *               SP-TAP   an impulse returns at the left tap's delay (and the
 *                        right's) within one sample;
 *               SP-FB    feedback +0.97 decays and stays bounded; -0.97
 *                        alternates the sign of successive echoes;
 *               SP-DC    a DC input never wraps (saturation only) and
 *                        negative feedback keeps the line below the input;
 *               SP-DRY   level 0: left = right = the ramped dry input,
 *                        exactly;
 *               SP-GAIN  the ramp starts at g0 and ends within 1 LSB of g1;
 *               SP-MIN   the spread tap's floor (32 samples) holds at time 0,
 *                        spread 127;
 *               SP-NOUNIT no unit: the dry ramp, mono, nothing written;
 *               SP-MIX   the Snare form's level-0 output is sat16(noise x mix
 *                        + tone) x g;
 *               LP1-RESP the Cymbal/HiHat LP against the analytic TPT
 *                        magnitude (pass band and stop band);
 *               LP1-STAB bounded at the lowest damping with full drive and
 *                        full-scale input.
 * Breaks:     make voice_space builds two broken copies (the mirror copy
 *             removed; the tap floor below one block) that must fail SP-RING.
 * Inputs:     build/gen/vs_*.c|h (extracted).
 * Outputs:    one line per failure (first 40); "voice_space failures: N";
 *             exit 1 if N > 0 (the break builds invert this in the Makefile).
 * Accessors:  make -C tools/dsp_test voice_space.
 * Affiliates: voiceSpace.c/.h; ResonantFilter.h; TetraktTables.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define FXBUF_VOICE_UNIT_SAMPLES   2208u
#define TKT_TABLE_POINTS           128u

/* the filter enum and ResonantFilter come from prelude.h (as the firmware's) */
#include "vs_tables.c"
#include "voiceSpace.host.h"
#include "voiceSpace.host.c"

static int failures;

static void expect(int cond, const char *msg)
{
	if (!cond) {
		if (failures < 40)
			printf("FAIL: %s\n", msg);
		failures++;
	}
}

static uint32_t rng_state = 12345u;
static int16_t rnd16(void)
{
	rng_state = rng_state * 1664525u + 1013904223u;
	return (int16_t)(rng_state >> 16);
}

/* ---- the reference: the same frame on a plain modulo ring ---------------- */

typedef struct {
	int16_t ring[VSPACE_RING];
	uint32_t w;
	int32_t lp;
} RefSpace;

static int16_t ref_ring(const RefSpace *r, int32_t i)
{
	i %= (int32_t)VSPACE_RING;
	if (i < 0)
		i += (int32_t)VSPACE_RING;
	return r->ring[i];
}

/* One block of the reference, with the coefficients voiceSpace computes
 * (vs_block(), whose read starts give the delays). */
static void ref_block(RefSpace *r, const VoiceSpace *sp, uint8_t spread, float fb,
                      float g0, float g1, const int16_t *in, const int16_t *add,
                      float mix, int16_t *outL, int16_t *outR)
{
	VsBlock b;
	VoiceSpace tmp = *sp;
	int16_t dummy[FXBUF_VOICE_UNIT_SAMPLES];
	int32_t dL, dR, g, mq;
	uint32_t n;

	tmp.line = dummy;
	vs_block(&tmp, spread, fb, g0, g1, &b);
	dL = ((int32_t)tmp.wi - (int32_t)(b.rl - dummy) + (int32_t)VSPACE_RING) % (int32_t)VSPACE_RING;
	dR = ((int32_t)tmp.wi - (int32_t)(b.rr - dummy) + (int32_t)VSPACE_RING) % (int32_t)VSPACE_RING;
	g = b.g;
	mq = (int32_t)(((mix < 0.0f) ? 0.0f : ((mix > 1.0f) ? 1.0f : mix)) * 32767.0f);
	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t w = (int32_t)r->w;
		const int32_t src = add ? VS_SSAT16(((in[n] * mq) >> 15) + add[n]) : in[n];
		const int32_t x = VS_SMMUL(src << 2, g);
		const int32_t al = ref_ring(r, w - dL), ar = ref_ring(r, w - dR);
		const int32_t tl = al + VS_SMULWB((ref_ring(r, w - dL - 1) - al) << 1, b.fLR);
		const int32_t tr = ar + VS_SMULWT((ref_ring(r, w - dR - 1) - ar) << 1, b.fLR);

		g += b.dg;
		r->lp += VS_SMULWB(((tl + tr) << 14) - r->lp, b.fbK) << 1;
		r->ring[w] = (int16_t)VS_SSAT16(x + (VS_SMULWB(r->lp, b.fbslv) >> 14));
		outL[n] = (int16_t)VS_SSAT16(x + VS_SMULWT(tl << 1, b.fbslv));
		outR[n] = (int16_t)VS_SSAT16(x + VS_SMULWT(tr << 1, b.fbslv));
		r->w = (r->w + 1u) % VSPACE_RING;
	}
}

/* ---- SP-RING ------------------------------------------------------------- */

static void law_ring(int mixForm)
{
	static int16_t unit[FXBUF_VOICE_UNIT_SAMPLES];
	VoiceSpace sp;
	RefSpace ref;
	int16_t in[OUTPUT_DMA_SIZE], add[OUTPUT_DMA_SIZE], buf[OUTPUT_DMA_SIZE];
	int16_t rl[OUTPUT_DMA_SIZE], rr[OUTPUT_DMA_SIZE];
	uint32_t blk, n, mism = 0u;

	voiceSpace_init(&sp);
	voiceSpace_bind(&sp, unit);
	memset(&ref, 0, sizeof(ref));
	for (blk = 0u; blk < 68u * 7u; blk++) {
		/* sweep every control, the extremes included */
		const uint8_t spread = (uint8_t)((blk * 37u) % 128u);
		const float fb = (float)((blk * 53u) % 128u) / 127.0f;
		const float g0 = (float)(blk % 9u) * 0.22f, g1 = (float)((blk + 3u) % 9u) * 0.22f;

		sp.time = (uint8_t)((blk < 68u) ? 0u : ((blk < 136u) ? 127u : (blk * 29u) % 128u));
		sp.tone = (uint8_t)((blk * 11u) % 128u);
		sp.level = (uint8_t)((blk * 17u) % 128u);
		for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
			in[n] = (blk % 40u < 20u) ? rnd16() : 0;
			add[n] = (int16_t)(rnd16() / 2);
		}
		memcpy(buf, in, sizeof(buf));
		ref_block(&ref, &sp, spread, fb, g0, g1, in, mixForm ? add : 0, 0.6f, rl, rr);
		if (mixForm)
			voiceSpace_processMix(&sp, buf, add, 0.6f, g0, g1, spread, fb);
		else
			voiceSpace_process(&sp, buf, g0, g1, spread, fb);
		for (n = 0u; n < OUTPUT_DMA_SIZE; n++)
			if (buf[n] != rl[n] || sp.right[n] != rr[n])
				mism++;
	}
	if (mism)
		printf("SP-RING%s: %u mismatching samples\n", mixForm ? " (mix)" : "", mism);
	expect(mism == 0u, mixForm ? "SP-RING (Snare form) bit-exact against the modulo ring"
	                           : "SP-RING bit-exact against the modulo ring");
}

/* ---- the impulse laws ---------------------------------------------------- */

static void run_impulse(uint8_t time, uint8_t spread, float fb, uint8_t tone,
                        int16_t *L, int16_t *R, uint32_t blocks, int16_t amp)
{
	static int16_t unit[FXBUF_VOICE_UNIT_SAMPLES];
	VoiceSpace sp;
	int16_t buf[OUTPUT_DMA_SIZE];
	uint32_t blk;

	voiceSpace_init(&sp);
	voiceSpace_bind(&sp, unit);
	sp.time = time;
	sp.tone = tone;
	sp.level = 127u;
	for (blk = 0u; blk < blocks; blk++) {
		memset(buf, 0, sizeof(buf));
		if (blk == 0u)
			buf[0] = amp;
		voiceSpace_process(&sp, buf, 1.0f, 1.0f, spread, fb);
		memcpy(&L[blk * OUTPUT_DMA_SIZE], buf, sizeof(buf));
		memcpy(&R[blk * OUTPUT_DMA_SIZE], sp.right, sizeof(buf));
	}
}

static uint32_t peak_after(const int16_t *x, uint32_t from, uint32_t to)
{
	uint32_t i, at = from;
	int32_t best = -1;

	for (i = from; i < to; i++)
		if (abs(x[i]) > best) {
			best = abs(x[i]);
			at = i;
		}
	return at;
}

static void law_tap(void)
{
	enum { BL = 100 };
	static int16_t L[BL * 32], R[BL * 32];
	const uint8_t times[] = { 0u, 20u, 63u, 100u, 127u };
	uint32_t t;

	for (t = 0u; t < sizeof(times); t++) {
		const float tL = vspaceTime[times[t]];
		float tR = tL * (1.0f - 0.5f * 64.0f / 127.0f);
		uint32_t pl, pr;
		char msg[96];

		tR = (tR < 32.0f) ? 32.0f : tR;
		run_impulse(times[t], 64u, 63.0f / 127.0f, 127u, L, R, BL, 16000);
		pl = peak_after(L, 1u, BL * 32u);
		pr = peak_after(R, 1u, BL * 32u);
		snprintf(msg, sizeof(msg), "SP-TAP time %u: left echo at %u, tap %.2f", times[t], pl, tL);
		expect(fabsf((float)pl - tL) <= 1.0f, msg);
		snprintf(msg, sizeof(msg), "SP-TAP time %u: right echo at %u, tap %.2f", times[t], pr, tR);
		expect(fabsf((float)pr - tR) <= 1.0f, msg);
	}
}

static void law_feedback(void)
{
	enum { BL = 1500 };
	static int16_t L[BL * 32], R[BL * 32];
	const uint32_t d = (uint32_t)vspaceTime[40];
	uint32_t i;
	double e0 = 0.0, e1 = 0.0;
	int32_t maxabs = 0;

	/* +0.97 (byte 127), tone open: bounded, decaying */
	run_impulse(40u, 0u, 1.0f, 127u, L, R, BL, 16000);
	for (i = 1u; i < BL * 32u; i++)
		maxabs = (abs(L[i]) > maxabs) ? abs(L[i]) : maxabs;
	for (i = 0u; i < 4000u; i++)
		e0 += (double)L[i + 1u] * L[i + 1u];
	for (i = BL * 32u - 4000u; i < BL * 32u; i++)
		e1 += (double)L[i] * L[i];
	expect(maxabs < 32767, "SP-FB +0.97: the echoes stay below full scale");
	expect(e1 < e0 * 0.01, "SP-FB +0.97: the tail decays (last 4,000 samples < 1 % of the first)");
	/* -0.955 (byte 0): successive echoes alternate in sign */
	run_impulse(40u, 0u, 0.0f, 127u, L, R, 40, 16000);
	{
		const int32_t e1s = L[peak_after(L, d - 2u, d + 3u)];
		const int32_t e2s = L[peak_after(L, 2u * d - 2u, 2u * d + 4u)];

		expect(e1s > 0 && e2s < 0, "SP-FB -0.97: the second echo has the opposite sign");
	}
}

static void law_dc(void)
{
	static int16_t unit[FXBUF_VOICE_UNIT_SAMPLES];
	VoiceSpace sp;
	int16_t buf[OUTPUT_DMA_SIZE];
	uint32_t blk, n;
	int ok = 1;
	int32_t ringmax = 0;

	/* +0.97: DC builds and saturates, never wraps */
	voiceSpace_init(&sp);
	voiceSpace_bind(&sp, unit);
	sp.time = 10u; sp.tone = 127u; sp.level = 127u;
	for (blk = 0u; blk < 3000u; blk++) {
		for (n = 0u; n < OUTPUT_DMA_SIZE; n++)
			buf[n] = 8000;
		voiceSpace_process(&sp, buf, 1.0f, 1.0f, 0u, 1.0f);
		for (n = 0u; n < OUTPUT_DMA_SIZE; n++)
			ok &= (buf[n] >= 8000 && sp.right[n] >= 8000);
	}
	expect(ok, "SP-DC +0.97: a positive DC input never wraps negative");
	/* -0.955: the line settles below the input */
	voiceSpace_init(&sp);
	voiceSpace_bind(&sp, unit);
	sp.time = 10u; sp.tone = 127u; sp.level = 127u;
	for (blk = 0u; blk < 3000u; blk++) {
		for (n = 0u; n < OUTPUT_DMA_SIZE; n++)
			buf[n] = 8000;
		voiceSpace_process(&sp, buf, 1.0f, 1.0f, 0u, 0.0f);
	}
	for (n = 0u; n < VSPACE_RING; n++)
		ringmax = (abs(unit[n]) > ringmax) ? abs(unit[n]) : ringmax;
	expect(ringmax <= 8000, "SP-DC -0.97: the line stays at or below the input");
}

static int32_t ref_x(int16_t in, int32_t g) { return VS_SSAT16(VS_SMMUL((int32_t)in << 2, g)); }

static void law_dry_gain_min_nounit(void)
{
	static int16_t unit[FXBUF_VOICE_UNIT_SAMPLES];
	VoiceSpace sp;
	VsBlock b;
	int16_t in[OUTPUT_DMA_SIZE], buf[OUTPUT_DMA_SIZE], add[OUTPUT_DMA_SIZE];
	uint32_t blk, n;
	int dry = 1, mixok = 1, nounit = 1;

	/* SP-DRY, SP-GAIN: level 0, a ramp 0.3 -> 1.7 */
	voiceSpace_init(&sp);
	voiceSpace_bind(&sp, unit);
	sp.time = 50u; sp.tone = 90u; sp.level = 0u;
	for (blk = 0u; blk < 300u; blk++) {
		int32_t g;

		for (n = 0u; n < OUTPUT_DMA_SIZE; n++)
			in[n] = buf[n] = rnd16();
		vs_block(&sp, 40u, 1.0f, 0.3f, 1.7f, &b);
		g = b.g;
		voiceSpace_process(&sp, buf, 0.3f, 1.7f, 40u, 1.0f);
		for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
			const int32_t x = ref_x(in[n], g);

			dry &= (buf[n] == x && sp.right[n] == x);
			g += b.dg;
		}
		if (blk == 0u) {
			expect(b.g == (int32_t)(0.3f * 1073741824.0f), "SP-GAIN: the ramp starts at g0");
			expect(labs((long)(b.g + 31 * b.dg) - (long)(int32_t)(1.7f * 1073741824.0f)) < 64,
			       "SP-GAIN: the ramp ends at g1");
		}
	}
	expect(dry, "SP-DRY: level 0 gives the ramped dry input on both sides, exactly");
	/* SP-MIN: time 0, spread 127 -> the right tap at its 32-sample floor */
	{
		int16_t dummy[FXBUF_VOICE_UNIT_SAMPLES];
		VoiceSpace t = sp;
		int32_t dR;

		t.line = dummy; t.wi = 640u; t.time = 0u;
		vs_block(&t, 127u, 0.5f, 1.0f, 1.0f, &b);
		dR = (int32_t)t.wi - (int32_t)(b.rr - dummy);
		expect(dR == (int32_t)VSPACE_TAP_MIN && (b.fLR >> 16) == 0,
		       "SP-MIN: the spread tap is held at 32 samples");
	}
	/* SP-MIX: the Snare form, level 0 */
	voiceSpace_init(&sp);
	voiceSpace_bind(&sp, unit);
	sp.level = 0u;
	for (blk = 0u; blk < 50u; blk++) {
		int32_t g;
		const int32_t mq = (int32_t)(0.35f * 32767.0f);

		for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
			in[n] = buf[n] = rnd16();
			add[n] = rnd16();
		}
		vs_block(&sp, 0u, 0.5f, 1.0f, 0.8f, &b);
		g = b.g;
		voiceSpace_processMix(&sp, buf, add, 0.35f, 1.0f, 0.8f, 0u, 0.5f);
		for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
			const int32_t s = VS_SSAT16(((in[n] * mq) >> 15) + add[n]);
			const int32_t x = VS_SSAT16(VS_SMMUL(s << 2, g));

			mixok &= (buf[n] == x);
			g += b.dg;
		}
	}
	expect(mixok, "SP-MIX: the Snare form's level-0 output is sat16(noise x mix + tone) x g");
	/* SP-NOUNIT */
	voiceSpace_init(&sp);
	sp.level = 127u;
	for (n = 0u; n < OUTPUT_DMA_SIZE; n++)
		in[n] = buf[n] = rnd16();
	vs_block(&sp, 0u, 1.0f, 0.5f, 0.5f, &b);
	voiceSpace_process(&sp, buf, 0.5f, 0.5f, 0u, 1.0f);
	for (n = 0u; n < OUTPUT_DMA_SIZE; n++)
		nounit &= (buf[n] == ref_x(in[n], b.g) && sp.right[n] == 0);
	expect(nounit && sp.wi == 0u, "SP-NOUNIT: no unit gives the dry ramp, mono, nothing written");
}

/* ---- LP1 ------------------------------------------------------------------ */

static void lp1_setup(ResonantFilter *f, float fc_norm, float reso, float drive)
{
	memset(f, 0, sizeof(*f));
	f->f = fc_norm;                       /* the ZDF's f (cutoff / fs) */
	f->g = tanf((float)M_PI * fc_norm);   /* SVF_recalcFreq() (fastTan there) */
	f->q = 1.0f - reso;
	if (f->q < 0.1f)
		f->q = 0.02f;                     /* SVF_setReso() */
	f->drive = drive;
}

static double lp1_gain(float fc_norm, float reso, float f_norm, float amp)
{
	ResonantFilter f;
	int16_t buf[OUTPUT_DMA_SIZE];
	uint32_t blk, n, k = 0u;
	double peak = 0.0;

	lp1_setup(&f, fc_norm, reso, 1.0f);
	for (blk = 0u; blk < 600u; blk++) {
		for (n = 0u; n < OUTPUT_DMA_SIZE; n++, k++)
			buf[n] = (int16_t)(amp * 32767.0f * sinf(2.0f * (float)M_PI * f_norm * (float)k));
		voiceSpace_lp1(&f, buf);
		if (blk >= 400u)
			for (n = 0u; n < OUTPUT_DMA_SIZE; n++)
				peak = (fabs((double)buf[n]) > peak) ? fabs((double)buf[n]) : peak;
	}
	return peak / (amp * 32767.0);
}

static double tpt_lp_mag(float fc_norm, float reso, float f_norm)
{
	/* the bilinear-prewarped SVF low-pass: 1 / |1 - r^2 + j k r|, r = tan(pi f)/g */
	const double g = tan(M_PI * fc_norm);
	double q = 1.0 - reso;
	double r, k;

	q = (q < 0.1) ? 0.02 : q;
	k = 2.0 * ((fc_norm >= 0.4499) ? 1.0 : q);
	r = tan(M_PI * f_norm) / g;
	return 1.0 / sqrt((1.0 - r * r) * (1.0 - r * r) + (k * r) * (k * r));
}

static void law_lp1(void)
{
	const float fcs[] = { 0.01f, 0.05f, 0.15f };
	uint32_t i;
	char msg[120];

	for (i = 0u; i < 3u; i++) {
		const float fc = fcs[i];
		/* small signal: the cubics' slopes 1 (input) and 0.967 (output) */
		const double pass = lp1_gain(fc, 0.1f, fc * 0.25f, 0.05f);
		const double pass_ref = tpt_lp_mag(fc, 0.1f, fc * 0.25f) * 0.967;
		const double stop = lp1_gain(fc, 0.1f, fc * 3.0f, 0.05f);
		const double stop_ref = tpt_lp_mag(fc, 0.1f, fc * 3.0f) * 0.967;

		snprintf(msg, sizeof(msg), "LP1-RESP fc %.3f pass %.4f ref %.4f", fc, pass, pass_ref);
		expect(fabs(pass - pass_ref) < 0.03 * pass_ref + 0.002, msg);
		snprintf(msg, sizeof(msg), "LP1-RESP fc %.3f stop %.4f ref %.4f", fc, stop, stop_ref);
		expect(fabs(stop - stop_ref) < 0.05 * stop_ref + 0.004, msg);
	}
	/* LP1-STAB: lowest damping, full drive, full-scale noise and a square */
	for (i = 0u; i < 3u; i++) {
		ResonantFilter f;
		int16_t buf[OUTPUT_DMA_SIZE];
		uint32_t blk, n;
		int finite = 1;
		float smax = 0.0f;

		lp1_setup(&f, fcs[i], 1.0f, 6.4f);
		for (blk = 0u; blk < 3000u; blk++) {
			for (n = 0u; n < OUTPUT_DMA_SIZE; n++)
				buf[n] = (blk & 1u) ? rnd16() : (int16_t)((n < 16u) ? 32767 : -32768);
			voiceSpace_lp1(&f, buf);
			finite &= isfinite(f.s1) && isfinite(f.s2);
			smax = fmaxf(smax, fmaxf(fabsf(f.s1), fabsf(f.s2)));
		}
		snprintf(msg, sizeof(msg), "LP1-STAB fc %.3f: states finite and bounded (max %.2f)", fcs[i], smax);
		expect(finite && smax < 100.0f, msg);
	}
}

int main(void)
{
	law_ring(0);
	law_ring(1);
	law_tap();
	law_feedback();
	law_dc();
	law_dry_gain_min_nounit();
	law_lp1();
	printf("voice_space failures: %d\n", failures);
	return failures ? 1 : 0;
}
