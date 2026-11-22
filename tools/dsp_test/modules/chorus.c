/*
 * modules/chorus.c - host laws for the BBD chorus (S099 P1).
 *
 * What:       builds the real BbdChorus.c and BbdChorusTables.c (extracted)
 *             and checks:
 *               CH-TAP   bbd_tap() against (1 - f) r[i] + f r[i + 1] in
 *                        float, every Q14 fraction at a fixed index: within
 *                        1 LSB of the x 2 scale;
 *               CH-GUARD 1,000 blocks of random input at delays swept over
 *                        65 .. 990 (wraps at every block position): every tap
 *                        equals a reference circular buffer read with exact
 *                        modulo indexing (no guard), for the full-rate loop and
 *                        (S099 P1 review) the half-rate loop bbd_sideH(), whose
 *                        ring holds the mean of each input pair; break: no
 *                        guard refresh;
 *               CH-LAW   the control: every block's rp equals (w << 16) - the
 *                        previous block's end delay exactly (in ring samples:
 *                        halved for the half-rate Colloid); with width 127
 *                        R's delay is 2c - L's; over one LFO cycle the delay's
 *                        extremes are the mode table's (within 0.5 sample)
 *                        and the pitch swing is +-6.5 / +-11.0 / +-13.4 cents
 *                        for I / II / 1+2 (within 5 %, printed);
 *               CH-MIX0  cmx 0: both forms' output equals the input exactly,
 *                        every sample; at cmx 64 the output equals the
 *                        restated x gd + wet gw (break: gn's sign);
 *               CH-SLEW  a dpt step 3 -> 65 (var, depth 0 -> full) moves
 *                        the delay by at most 1/8 of the step plus one LFO
 *                        step per block; dpt -> 127 (dbl) moves the centre
 *                        <= 16 samples a block (S099 P2: dpt replaces cho
 *                        and cdp);
 *               CH-SRC   both csr forms leave both rings holding the selected
 *                        source's last 1,024 pre-pole samples; break: no
 *                        ring-R mirror store in pass A (mid -> L-R fails);
 *               CH-ALK   the Alkaline form: right[] holds the dry before pass
 *                        B; at cdv 0 the ring holds the input exactly (the pre
 *                        pole transparent); at cdv 127 a full-scale input clips
 *                        the ring at int16; the 12 dB wet (S099 P1 review: now
 *                        measured on the loop): a 16 kHz sine at a whole-sample
 *                        delay, wet only, comes out within 0.5 dB of the
 *                        restated pre pole x two post poles (a missing pole is
 *                        about 4 dB off);
 *               CH-HALF  (S099 P1 review, C3) the half-rate loop against the
 *                        full-rate one at a whole-sample delay, wet only: the
 *                        1 kHz level within 0.5 dB; the images (22,054 - f) of
 *                        2 and 5 kHz at most -18 dB re the wet partial; the
 *                        folds of 15 and 18 kHz at most -10 dB re the input
 *                        partial (all printed: regression guards on the C3
 *                        design; the sound is judged by ear);
 *               CH-INIT  bbdChorus_init() clears all 4,416 bytes and sets the
 *                        sides' rings (mono: both on ring L);
 *               CH-DPT   (S099 P2) the Effect Depth row: dpt 0..2 the Juno
 *                        modes at their own rates (rte ignored), 3 var at
 *                        depth 0, 65 var at full depth, 127 dbl at full
 *                        depth, 3..127 at rte's rate, the centre and depth
 *                        never falling and the delay inside its range.
 *               CH-AHALF (S101) the half-rate Alkaline (bbd_alkAH(),
 *                        bbd_alkTH()) against the full-rate one, both outputs:
 *                        the 1 kHz level within 0.5 dB, the images and folds
 *                        under CH-HALF's bounds, and the 6 kHz half / full
 *                        ratio within 0.3 dB of the Colloid form's (break:
 *                        one post pole fewer). S101 also extends CH-GUARD to
 *                        bbd_alkAH() and CH-ALK's ring law to both rates; the
 *                        16 kHz 12 dB-wet law runs the full-rate loops by
 *                        name.
 *             Built twice (Makefile): the default (BBD_COL_HALF_RATE 1, the
 *             Colloid half rate; BBD_ALK_HALF_RATE 1, the Alkaline half rate,
 *             S101) and -DBBD_COL_HALF_RATE=0 -DBBD_ALK_HALF_RATE=0 (both
 *             full rate), so both forms stay proven through
 *             bbdChorus_renderColloid() and bbdChorus_renderAlkaline().
 * Inputs:     generated fragments (Makefile target chorus).
 * Outputs:    failure counts per group; exit 1 on any failure.
 * Accessors:  make -C tools/dsp_test chorus.
 * Affiliates: BbdChorus.c; tools/chorus/gen_chorus_tables.py.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* the unit's constants, as FxBuffer.h has them (its include is dropped) */
#define FXBUF_VOICE_UNIT_SAMPLES 2208u
#define FXBUF_VOICE_UNIT_BYTES   (FXBUF_VOICE_UNIT_SAMPLES * 2u)
#define REAL_FS  44108.07291666667f

#include "BbdChorusTables.host.h"
#include "BbdChorus.host.h"
#include "BbdChorusTables.host.c"
#include "BbdChorus.host.c"

#define BLK OUTPUT_DMA_SIZE

enum { G_TAP, G_GUARD, G_LAW, G_MIX0, G_SLEW, G_SRC, G_ALK, G_INIT, G_HALF, G_DPT,
       G_AHALF, G_N };
static const char *g_name[G_N] = { "CH-TAP", "CH-GUARD", "CH-LAW", "CH-MIX0",
                                   "CH-SLEW", "CH-SRC", "CH-ALK", "CH-INIT", "CH-HALF",
                                   "CH-DPT", "CH-AHALF" };
static unsigned g_fail[G_N];

static void fail(int g, const char *what, double a, double b)
{
	if (g_fail[g] < 5u)
		printf("  %s FAIL: %s (%g, %g)\n", g_name[g], what, a, b);
	g_fail[g]++;
}

static int16_t unit[FXBUF_VOICE_UNIT_SAMPLES];

/* the amplitude of the f component of x[0..n-1] (Goertzel; S099 P1 review) */
static double tone_amp(const int16_t *x, uint32_t n, double f)
{
	const double w = 2.0 * M_PI * f / (double)REAL_FS;
	double re = 0.0, im = 0.0;

	for (uint32_t i = 0u; i < n; i++) {
		re += x[i] * cos(w * (double)i);
		im -= x[i] * sin(w * (double)i);
	}
	return 2.0 * sqrt(re * re + im * im) / (double)n;
}

/* ---------------------------------------------------------------- CH-TAP */
static void test_tap(void)
{
	static int16_t r[FXBUF_VOICE_UNIT_SAMPLES];
	const uint32_t i = 500u;

	memset(r, 0, sizeof(r));
	r[i] = 12345;
	r[i + 1u] = -5432;
	for (uint32_t f = 0u; f < 0x4000u; f++) {
		const double fr = (double)f / 16384.0;
		const double ref = 2.0 * ((1.0 - fr) * r[i] + fr * r[i + 1u]);
		const int32_t got = bbd_tap(r, (i << 16) | (f << 2));

		if (fabs((double)got - ref) > 1.5) {
			fail(G_TAP, "interpolated tap", got, ref);
			break;
		}
	}
}

/* -------------------------------------------------------------- CH-GUARD */
static void test_guard(void)
{
	static int16_t r[FXBUF_VOICE_UNIT_SAMPLES];
	static int16_t x[1000u * BLK];
	static int16_t expv[1024];
	BbdSide s;
	uint32_t seed = 0x9E3779B9u;

	memset(r, 0, sizeof(r));
	memset(expv, 0, sizeof(expv));
	memset(&s, 0, sizeof(s));
	s.ring = r;
	/* the first block's read position: the largest delay behind the write
	 * pointer (bbd_sidePlan() needs the previous end delay) */
	s.dEnd = (int32_t)(65.0f * 65536.0f);
	for (uint32_t n = 0u; n < 1000u * BLK; n++)
		x[n] = (int16_t)((int32_t)(golden_rand(&seed) & 0x3FFFu) - 16384);
	for (uint32_t b = 0u; b < 1000u; b++) {
		const uint32_t w = b * BLK;
		const float d = 65.0f + (float)((b * 997u) % 926u);
		uint32_t rp0, inc;

		bbd_sidePlan(&s, w, d, bbdKcTone[BBD_COL_TONE], 0u);
		s.kk = 0u;                 /* a transparent pre pole: ring = x (after
		                            * the plan wrote its coefficients) */
		rp0 = s.rp;
		inc = s.inc;
		bbd_sideD(&s, x + w, w, 0u);
		for (uint32_t n = 0u; n < BLK; n++) {
			const uint32_t rp = rp0 + n * inc;
			const uint32_t i = (rp >> 16) & BBD_RMASK;
			const uint32_t f = (rp >> 2) & 0x3FFFu;
			double ref;

			expv[(w + n) & BBD_RMASK] = x[w + n];
			ref = 2.0 * ((1.0 - (double)f / 16384.0) * expv[i] +
			             ((double)f / 16384.0) * expv[(i + 1u) & BBD_RMASK]);
			{
				const int32_t got = bbd_tap(r, rp);

				if (fabs((double)got - ref) > 1.5) {
					fail(G_GUARD, "tap against the exact ring", got, ref);
					return;
				}
			}
		}
		if (((w + BLK - 1u) & BBD_RMASK) < BLK && r[BBD_RING] != r[0])
			fail(G_GUARD, "guard not refreshed", r[BBD_RING], r[0]);
	}
	/* S099 P1 review: the half-rate loop (C3). The ring holds the mean of
	 * each input pair (the pre pole transparent), w counts ring samples (16
	 * a block), the delay is in output samples (the plan halves it). */
	memset(r, 0, sizeof(r));
	memset(expv, 0, sizeof(expv));
	memset(&s, 0, sizeof(s));
	s.ring = r;
	s.dEnd = (int32_t)(65.0f * 65536.0f);
	for (uint32_t b = 0u; b < 1000u; b++) {
		const uint32_t w = b * BBD_HALF_BLOCK;
		const float d = 65.0f + (float)((b * 997u) % 926u);
		uint32_t rp0, inc;

		bbd_sidePlan(&s, w, d, bbdKcTone[BBD_COL_TONE], 1u);
		s.kk = 0u;
		rp0 = s.rp;
		inc = s.inc;
		bbd_sideH(&s, x + b * BLK, w, 0u);
		for (uint32_t j = 0u; j < BBD_HALF_BLOCK; j++) {
			const uint32_t rp = rp0 + j * inc;
			const uint32_t i = (rp >> 16) & BBD_RMASK;
			const uint32_t f = (rp >> 2) & 0x3FFFu;
			double ref;

			expv[(w + j) & BBD_RMASK] = (int16_t)(((int32_t)x[b * BLK + 2u * j] +
			                                       (int32_t)x[b * BLK + 2u * j + 1u]) >> 1);
			ref = 2.0 * ((1.0 - (double)f / 16384.0) * expv[i] +
			             ((double)f / 16384.0) * expv[(i + 1u) & BBD_RMASK]);
			if (fabs((double)bbd_tap(r, rp) - ref) > 1.5) {
				fail(G_GUARD, "half-rate tap against the exact ring", bbd_tap(r, rp), ref);
				return;
			}
		}
		if (((w + BBD_HALF_BLOCK - 1u) & BBD_RMASK) < BBD_HALF_BLOCK && r[BBD_RING] != r[0])
			fail(G_GUARD, "half-rate guard not refreshed", r[BBD_RING], r[0]);
	}
	/* S101: the half-rate Alkaline loop bbd_alkAH(). At the BBD drive x1
	 * (gDrv 8,192) the ring sample is ((p0 + p1) << 2) x 8,192 >> 16, the
	 * floor of the pair's mean: the same ring as bbd_sideH()'s. */
	{
		static int16_t xr[BLK];

		memset(r, 0, sizeof(r));
		memset(expv, 0, sizeof(expv));
		memset(&s, 0, sizeof(s));
		s.ring = r;
		s.dEnd = (int32_t)(65.0f * 65536.0f);
		for (uint32_t b = 0u; b < 1000u; b++) {
			const uint32_t w = b * BBD_HALF_BLOCK;
			const float d = 65.0f + (float)((b * 997u) % 926u);
			uint32_t rp0, inc;

			bbd_sidePlan(&s, w, d, bbdKcTone[BBD_COL_TONE], 1u);
			s.kk = 0u;
			rp0 = s.rp;
			inc = s.inc;
			bbd_alkAH(&s, x + b * BLK, xr, w, 0u, 8192u);
			for (uint32_t j = 0u; j < BBD_HALF_BLOCK; j++) {
				const uint32_t rp = rp0 + j * inc;
				const uint32_t i = (rp >> 16) & BBD_RMASK;
				const uint32_t f = (rp >> 2) & 0x3FFFu;
				double ref;

				expv[(w + j) & BBD_RMASK] = (int16_t)(((int32_t)x[b * BLK + 2u * j] +
				                                       (int32_t)x[b * BLK + 2u * j + 1u]) >> 1);
				ref = 2.0 * ((1.0 - (double)f / 16384.0) * expv[i] +
				             ((double)f / 16384.0) * expv[(i + 1u) & BBD_RMASK]);
				if (fabs((double)bbd_tap(r, rp) - ref) > 1.5) {
					fail(G_GUARD, "half-rate Alkaline tap against the exact ring",
					     bbd_tap(r, rp), ref);
					return;
				}
			}
			if (((w + BBD_HALF_BLOCK - 1u) & BBD_RMASK) < BBD_HALF_BLOCK && r[BBD_RING] != r[0])
				fail(G_GUARD, "half-rate Alkaline guard not refreshed", r[BBD_RING], r[0]);
		}
	}
}

/* ---------------------------------------------------------------- CH-LAW */
static void test_law(void)
{
	uint32_t w = 0u;
	int32_t prev = 0;

	bbdChorus_init(unit, 0u, BBD_DPT_I);
	prev = bbd_state(unit)->side[0].dEnd;
	for (uint32_t b = 0u; b < 200u; b++) {
		bbdChorus_control(unit, BBD_DPT_I, 0u, 64u, BBD_COL_TONE, 0u, 127u, 0u);
		/* the plan's rp is exact at the block edge */
		/* in ring samples: the half-rate Colloid halves the delay */
		if (bbd_state(unit)->side[0].rp != (w << 16) - ((uint32_t)prev >> bbd_state(unit)->half))
			fail(G_LAW, "rp not exact", bbd_state(unit)->side[0].rp,
			     (w << 16) - ((uint32_t)prev >> bbd_state(unit)->half));
		prev = bbd_state(unit)->side[0].dEnd;
		w += BLK;
		bbd_state(unit)->w = w;
	}
	/* width 127: R = c - sw, L = c + sw */
	{
		const BbdChorusState *st = bbd_state(unit);
		const double c2 = 2.0 * (double)st->cNow * 65536.0;
		const double sum = (double)st->side[0].dEnd + (double)st->side[1].dEnd;

		if (fabs(sum - c2) > 2.0) fail(G_LAW, "width 127 is not 2c - L", sum, c2);
	}
	/* the delay extremes and the pitch swing over one cycle */
	{
		static const double cents[3] = { 6.5, 11.0, 13.4 };
		static const uint8_t modes[3] = { BBD_DPT_I, BBD_DPT_II, BBD_DPT_I_II };

		for (unsigned m = 0u; m < 3u; m++) {
			double slope = 0.0;
			int32_t prevd;

			bbdChorus_init(unit, 0u, modes[m]);
			bbd_state(unit)->w = 0u;
			prevd = bbd_state(unit)->side[0].dEnd;
			for (uint32_t b = 0u; b < 400u; b++) {
				const int32_t now = bbd_state(unit)->side[0].dEnd;
				const double s = fabs((double)(now - prevd)) / 65536.0 / (double)BLK;

				/* the first 120 blocks are the depth's slew, not the LFO */
				if (b > 160u && s > slope) slope = s;
				prevd = now;
				bbdChorus_control(unit, modes[m], 0u, 64u, BBD_COL_TONE, 0u, 127u, 0u);
				bbd_state(unit)->w += BLK;
			}
			{
				const double sw = fabs(1200.0 * log(1.0 / (1.0 + slope)) / log(2.0));

				printf("CH-LAW: dpt %u max delay slope %.4f a sample, swing %.2f cents (table %.1f)\n",
				       modes[m], slope, sw, cents[m]);
				if (fabs(sw - cents[m]) > 0.05 * cents[m]) fail(G_LAW, "pitch swing", sw, cents[m]);
			}
		}
	}
}

/* --------------------------------------------------------------- CH-MIX0 */
static void test_mix0(void)
{
	static int16_t l[BLK], r[BLK], l0[BLK], r0[BLK];

	bbdChorus_init(unit, 0u, BBD_DPT_II);
	for (unsigned src = 0u; src < 2u; src++) {
		for (uint32_t b = 0u; b < 8u; b++) {
			for (uint32_t n = 0u; n < BLK; n++) {
				l[n] = (int16_t)(1000 + 37 * (int)n + 11 * (int)b);
				r[n] = (int16_t)(-500 + 23 * (int)n);
				l0[n] = l[n];
				r0[n] = r[n];
			}
			bbdChorus_control(unit, BBD_DPT_II, 0u, 0u, BBD_COL_TONE, 0u, 127u, 0u);
			bbdChorus_renderColloid(unit, l, r, (uint8_t)src);
			for (uint32_t n = 0u; n < BLK; n++)
				if (l[n] != l0[n] || r[n] != r0[n])
					fail(G_MIX0, "cmx 0 is not the input", l[n], l0[n]);
			bbd_state(unit)->w += BLK;
		}
	}
	/* cmx 64: the output is x + x2 gn / 65536 + wet gw / 65536 */
	{
		static int16_t in[BLK], rin[BLK], dry[BLK], wet[BLK], both[BLK], work[BLK];
		uint32_t gk;

		for (uint32_t n = 0u; n < BLK; n++) {
			in[n] = (int16_t)(3000 + 13 * (int)n);
			rin[n] = (int16_t)(-2000 + 7 * (int)n);
		}
		/* dry only, then wet only, then both: the same ring state is needed, so
		 * each run reseeds the unit with the same block */
		bbdChorus_init(unit, 0u, BBD_DPT_I);
		bbdChorus_control(unit, BBD_DPT_I, 0u, 64u, BBD_COL_TONE, 0u, 127u, 0u);
		gk = bbd_state(unit)->gk;
		memcpy(work, in, sizeof(in));
		memcpy(r, rin, sizeof(rin));
		bbd_state(unit)->gk = (uint32_t)(uint16_t)(gk & 0xFFFFu);
		bbdChorus_renderColloid(unit, work, r, BBD_SRC_LR);
		memcpy(dry, work, sizeof(dry));
		bbdChorus_init(unit, 0u, BBD_DPT_I);
		bbdChorus_control(unit, BBD_DPT_I, 0u, 64u, BBD_COL_TONE, 0u, 127u, 0u);
		memcpy(work, in, sizeof(in));
		memcpy(r, rin, sizeof(rin));
		bbd_state(unit)->gk = gk & 0xFFFF0000u;
		bbdChorus_renderColloid(unit, work, r, BBD_SRC_LR);
		memcpy(wet, work, sizeof(wet));
		bbdChorus_init(unit, 0u, BBD_DPT_I);
		bbdChorus_control(unit, BBD_DPT_I, 0u, 64u, BBD_COL_TONE, 0u, 127u, 0u);
		memcpy(work, in, sizeof(in));
		memcpy(r, rin, sizeof(rin));
		bbdChorus_renderColloid(unit, work, r, BBD_SRC_LR);
		memcpy(both, work, sizeof(both));
		for (uint32_t n = 0u; n < BLK; n++) {
			/* the dry gain is absolute: x + x2 gn / 65536 (gn <= 0) */
			const int32_t gn = (int16_t)(gk & 0xFFFFu);
			const int32_t dryRef = (int32_t)in[n] +
			    ((((int32_t)in[n] << 1) * gn) >> 16);
			const int32_t ref = dryRef + ((int32_t)wet[n] - (int32_t)in[n]);
			const int32_t sat = ref > 32767 ? 32767 : (ref < -32768 ? -32768 : ref);

			if (dry[n] != dryRef) fail(G_MIX0, "cmx 64 dry gain", dry[n], dryRef);
			if (both[n] != sat) fail(G_MIX0, "cmx 64 is not dry + wet", both[n], sat);
		}
	}
}

/* --------------------------------------------------------------- CH-SLEW
 * S099 P2: dpt 3 (var at depth 0) -> 65 (var at full depth): the depth slews
 * by 1/8 a block; -> 127 (dbl at full depth): the centre moves at most
 * BBD_CENTRE_STEP (16 samples) in a block. */
static void test_slew(void)
{
	int32_t prev;

	bbdChorus_init(unit, 0u, BBD_DPT_VAR);
	bbd_state(unit)->w = 0u;
	bbdChorus_control(unit, BBD_DPT_VAR, 64u, 64u, BBD_COL_TONE, 0u, 127u, 0u);
	prev = bbd_state(unit)->side[0].dEnd;
	for (uint32_t b = 0u; b < 40u; b++) {
		bbdChorus_control(unit, BBD_DPT_VAR_FULL, 64u, 64u, BBD_COL_TONE, 0u, 127u, 0u);
		{
			const int32_t now = bbd_state(unit)->side[0].dEnd;
			const double step = fabs((double)(now - prev)) / 65536.0;

			/* 1/8 of the full sweep (81.4) plus one LFO step (<= 1 sample) */
			if (step > 81.4 / 8.0 + 1.0) fail(G_SLEW, "dpt depth slew", step, 81.4 / 8.0);
			prev = now;
		}
		bbd_state(unit)->w += BLK;
	}
	bbdChorus_control(unit, 127u, 64u, 64u, BBD_COL_TONE, 0u, 127u, 0u);
	{
		const double c = (double)bbd_state(unit)->cNow;

		if (fabs(c - 154.6) > 16.01) fail(G_SLEW, "the centre jumps", c, 154.6);
	}
}

/* ---------------------------------------------------------------- CH-DPT
 * S099 P2 (user, 2026-11-19): the Effect Depth row. After 600 blocks (every
 * slew settled): dpt 0, 1, 2 are the Juno modes (centre, depth and LFO rate
 * fixed: rte 0 and rte 127 give the same phase step); dpt 3 is var at depth
 * 0; 65 var at full depth (154.6 +- 81.4); 127 dbl at full depth (662 +-
 * 110); every dpt 3..127 follows rte, and the centre and depth never fall as
 * dpt rises (one smooth path). */
static void dpt_settle(uint8_t dpt, uint8_t rte, double *c, double *d, uint32_t *step)
{
	uint32_t ph;

	bbdChorus_init(unit, 0u, dpt);
	for (uint32_t b = 0u; b < 600u; b++) {
		bbdChorus_control(unit, dpt, rte, 64u, BBD_COL_TONE, 0u, 127u, 0u);
		bbd_state(unit)->w += BLK;
	}
	ph = bbd_state(unit)->lfoPh;
	bbdChorus_control(unit, dpt, rte, 64u, BBD_COL_TONE, 0u, 127u, 0u);
	*step = bbd_state(unit)->lfoPh - ph;
	*c = bbd_state(unit)->cNow;
	*d = bbd_state(unit)->depthNow;
}

static void test_dpt(void)
{
	static const double juno[3][2] = { { 154.6, 81.4 }, { 154.6, 81.4 }, { 154.4, 8.8 } };
	double c, d, c0, d0, cp = 0.0, dp = -1.0;
	uint32_t s0, s1;

	for (uint8_t m = 0u; m < BBD_DPT_VAR; m++) {
		dpt_settle(m, 0u, &c, &d, &s0);
		dpt_settle(m, 127u, &c0, &d0, &s1);
		if (fabs(c - juno[m][0]) > 0.01 || fabs(d - juno[m][1]) > 0.01)
			fail(G_DPT, "a Juno mode's centre / depth", m, c);
		if (s0 != s1) fail(G_DPT, "a Juno mode follows rte", m, (double)s1);
	}
	dpt_settle(BBD_DPT_VAR, 64u, &c, &d, &s0);
	if (fabs(c - 154.6) > 0.01 || d > 0.01) fail(G_DPT, "dpt 3 is var at depth 0", c, d);
	dpt_settle(BBD_DPT_VAR_FULL, 64u, &c, &d, &s0);
	if (fabs(c - 154.6) > 0.01 || fabs(d - 81.4) > 0.01) fail(G_DPT, "dpt 65 is var at full depth", c, d);
	dpt_settle(127u, 64u, &c, &d, &s0);
	if (fabs(c - 662.0) > 0.05 || fabs(d - 110.0) > 0.05) fail(G_DPT, "dpt 127 is dbl at full depth", c, d);
	dpt_settle(127u, 0u, &c0, &d0, &s1);
	if (s0 == s1) fail(G_DPT, "dpt 127 ignores rte", (double)s0, (double)s1);
	for (uint32_t v = BBD_DPT_VAR; v <= 127u; v += 4u) {
		dpt_settle((uint8_t)v, 64u, &c, &d, &s0);
		if (c < cp - 0.01 || d < dp - 0.01) { fail(G_DPT, "the path falls", v, c); break; }
		if (c - d < BBD_D_MIN || c + d > BBD_D_MAX) { fail(G_DPT, "the delay leaves its range", v, c); break; }
		cp = c; dp = d;
	}
}

/* ---------------------------------------------------------------- CH-SRC */
static void test_src(void)
{
	static int16_t l[BLK], r[BLK];

	for (unsigned src = 0u; src < 2u; src++) {
		/* the ring holds the pre pole's output: the channel for L-R, the mid
		 * (L + R) / 2 for the Juno form (S099 P1 review: the state at the
		 * sample's own scale) */
		const int32_t wantL = (src == BBD_SRC_MID) ? 1500 : 1000;
		const int32_t wantR = (src == BBD_SRC_MID) ? 1500 : 2000;

		bbdChorus_init(unit, 0u, BBD_DPT_I);
		bbd_state(unit)->side[0].kk = 0u;     /* transparent: ring = the input */
		bbd_state(unit)->side[1].kk = 0u;
		bbd_state(unit)->gk = 0u;
		for (uint32_t b = 0u; b < 64u; b++) {
			for (uint32_t n = 0u; n < BLK; n++) {
				l[n] = 1000;
				r[n] = 2000;
			}
			bbdChorus_renderColloid(unit, l, r, (uint8_t)src);
		}
		for (uint32_t i = 0u; i < BBD_RING; i++) {
			if (bbd_state(unit)->side[0].ring[i] != (int16_t)wantL ||
			    bbd_state(unit)->side[1].ring[i] != (int16_t)wantR) {
				fail(G_SRC, "ring content", src, i);
				break;
			}
		}
	}
}

/* ---------------------------------------------------------------- CH-ALK */
static void test_alk(void)
{
	static int16_t l[BLK], r[BLK];

	/* gk 0: both sides are the dry input */
	bbdChorus_init(unit, 1u, BBD_DPT_I);
	bbd_state(unit)->gk = 0u;
	for (uint32_t n = 0u; n < BLK; n++)
		l[n] = (int16_t)(1500 - 9 * (int)n);
	bbdChorus_renderAlkaline(unit, l, r);
	for (uint32_t n = 0u; n < BLK; n++)
		if (r[n] != (int16_t)(1500 - 9 * (int)n))
			fail(G_ALK, "right is not the dry", r[n], n);
	/* cdv 0: the ring holds the input (a transparent pre pole); S101: at
	 * half rate (BBD_ALK_HALF_RATE) the floor of each input pair's mean, 16
	 * ring samples a block */
	bbdChorus_init(unit, 1u, BBD_DPT_I);
	bbd_state(unit)->side[0].kk = 0u;
	bbd_state(unit)->gDrv = 8192u;
	for (uint32_t n = 0u; n < BLK; n++)
		l[n] = (int16_t)(500 + 3 * (int)n);
	bbdChorus_renderAlkaline(unit, l, r);
	for (uint32_t n = 0u; n < (BBD_ALK_HALF_RATE ? BBD_HALF_BLOCK : BLK); n++) {
		const int16_t want = BBD_ALK_HALF_RATE
		    ? (int16_t)(((500 + 6 * (int)n) + (503 + 6 * (int)n)) >> 1)
		    : (int16_t)(500 + 3 * (int)n);

		if (bbd_state(unit)->side[0].ring[n] != want)
			fail(G_ALK, "cdv 0 ring", bbd_state(unit)->side[0].ring[n], want);
	}
	if (bbd_state(unit)->w != (BBD_ALK_HALF_RATE ? BBD_HALF_BLOCK : BLK))
		fail(G_ALK, "the write count's step", bbd_state(unit)->w, BLK);
	/* cdv 127: a full-scale input clips */
	bbdChorus_init(unit, 1u, BBD_DPT_I);
	bbd_state(unit)->side[0].kk = 0u;
	bbd_state(unit)->gDrv = (uint32_t)(uint16_t)bbdDrvQ13[127];
	for (uint32_t n = 0u; n < BLK; n++)
		l[n] = 32767;
	bbdChorus_renderAlkaline(unit, l, r);
	if (bbd_state(unit)->side[0].ring[0] != 32767)
		fail(G_ALK, "cdv 127 does not clip", bbd_state(unit)->side[0].ring[0], 32767);
	/* the 12 dB wet, measured (S099 P1 review: the first form printed a
	 * formula with the wrong numerator and never ran the loop). A 16 kHz
	 * sine at 154 samples (a whole sample: the tap's fraction is 0), wet only
	 * (gn -32768: no dry; gw 32767), both poles on the tone coefficient,
	 * against the restated pre pole x post pole x post pole. S101: on the
	 * full-rate loops by name (bbd_alkA(), bbd_alkB()), whatever
	 * BBD_ALK_HALF_RATE selects: 16 kHz is above the half-rate ring's band,
	 * so the half form is held to the full one by CH-AHALF instead. */
	{
		static int16_t out[400u * BLK];
		const int32_t kc = bbdKcTone[BBD_COL_TONE];
		const double a = (double)kc / 65536.0;
		const double fr = 16000.0;
		const double wr = 2.0 * M_PI * fr / (double)REAL_FS;
		const double h = (1.0 - a) / sqrt(1.0 - 2.0 * a * cos(wr) + a * a);
		BbdChorusState *st;
		double got, want;

		bbdChorus_init(unit, 1u, BBD_DPT_I);
		st = bbd_state(unit);
		st->side[0].dEnd = st->side[1].dEnd = (int32_t)(154.0f * 65536.0f);
		for (uint32_t b2 = 0u; b2 < 800u; b2++) {
			for (uint32_t n = 0u; n < BLK; n++)
				l[n] = (int16_t)lround(12000.0 * sin(wr * (double)(b2 * BLK + n)));
			bbd_sidePlan(&st->side[0], st->w, 154.0f, kc, 0u);
			bbd_sidePlan(&st->side[1], st->w, 154.0f, kc, 0u);
			st->side[0].kk = st->side[1].kk = (uint32_t)(uint16_t)kc | ((uint32_t)(uint16_t)kc << 16);
			st->gk = (32767u << 16) | 0x8000u;
			st->gDrv = 8192u;
			bbd_alkA(&st->side[0], l, r, st->w, st->gk, st->gDrv);
			bbd_alkB(&st->side[1], r, st->gk);
			st->w += BLK;
			if (b2 >= 400u)
				memcpy(out + (b2 - 400u) * BLK, l, sizeof(l));
		}
		got = tone_amp(out, 400u * BLK, fr) / 12000.0;
		want = h * h * h;
		printf("CH-ALK: the 12 dB wet at 16 kHz %.2f dB against the restated %.2f dB "
		       "(the second pole adds %.1f dB)\n", 20.0 * log10(got), 20.0 * log10(want),
		       -20.0 * log10(h));
		if (fabs(20.0 * log10(got / want)) > 0.5) fail(G_ALK, "12 dB wet", got, want);
	}
}

/* --------------------------------------------------------------- CH-HALF */
/* One side's wet only, at 154 output samples (77 ring samples at half rate:
 * whole samples, the tap's fraction 0 in both forms), both poles on the
 * tone coefficient; returns the amplitude at fm of the last 600 blocks. */
static double half_run(const uint32_t half, const double f, const double fm)
{
	static int16_t r[FXBUF_VOICE_UNIT_SAMPLES];
	static int16_t x[BLK];
	static int16_t out[600u * BLK];
	const int32_t kc = bbdKcTone[BBD_COL_TONE];
	const double wr = 2.0 * M_PI * f / (double)REAL_FS;
	BbdSide s;

	memset(r, 0, sizeof(r));
	memset(&s, 0, sizeof(s));
	s.ring = r;
	s.dEnd = (int32_t)(154.0f * 65536.0f);
	for (uint32_t b = 0u; b < 1000u; b++) {
		const uint32_t w = b * (half ? BBD_HALF_BLOCK : BLK);

		for (uint32_t n = 0u; n < BLK; n++)
			x[n] = (int16_t)lround(12000.0 * sin(wr * (double)(b * BLK + n)));
		bbd_sidePlan(&s, w, 154.0f, kc, half);
		s.kk = (uint32_t)(uint16_t)kc | ((uint32_t)(uint16_t)kc << 16);
		if (half)
			bbd_sideH(&s, x, w, (32767u << 16) | 0x8000u);
		else
			bbd_sideD(&s, x, w, (32767u << 16) | 0x8000u);
		if (b >= 400u)
			memcpy(out + (b - 400u) * BLK, x, sizeof(x));
	}
	return tone_amp(out, 600u * BLK, fm);
}

static void test_half(void)
{
	const double fh = (double)REAL_FS / 2.0;   /* the ring's rate */

	{
		const double db = 20.0 * log10(half_run(1u, 1000.0, 1000.0) / half_run(0u, 1000.0, 1000.0));

		printf("CH-HALF: 1 kHz wet, half against full %+.2f dB\n", db);
		if (fabs(db) > 0.5) fail(G_HALF, "1 kHz level", db, 0.5);
	}
	{
		static const double fi[2] = { 2000.0, 5000.0 };

		for (unsigned i = 0u; i < 2u; i++) {
			const double db = 20.0 * log10(half_run(1u, fi[i], fh - fi[i]) /
			                               half_run(1u, fi[i], fi[i]));

			printf("CH-HALF: image of %.0f Hz at %.0f Hz %.1f dB re the wet partial\n",
			       fi[i], fh - fi[i], db);
			if (db > -18.0) fail(G_HALF, "image", fi[i], db);
		}
	}
	{
		static const double fa[2] = { 15000.0, 18000.0 };

		for (unsigned i = 0u; i < 2u; i++) {
			const double fold = half_run(1u, fa[i], fh - fa[i]);
			const double db = 20.0 * log10(fold / 12000.0);
			const double full = 20.0 * log10(half_run(0u, fa[i], fa[i]) / 12000.0);

			printf("CH-HALF: fold of %.0f Hz at %.0f Hz %.1f dB re the input "
			       "(the full-rate wet at %.0f Hz: %.1f dB)\n", fa[i], fh - fa[i], db, fa[i], full);
			if (db > -10.0) fail(G_HALF, "fold", fa[i], db);
		}
	}
}

/* -------------------------------------------------------------- CH-AHALF
 * S101: the half-rate Alkaline (bbd_alkAH(), bbd_alkTH()) against the
 * full-rate one (bbd_alkA(), bbd_alkB()), CH-HALF's method on the mono form:
 * one ring, both sides at 154 output samples (77 ring samples: whole
 * samples), wet only, every pole on the tone coefficient, the BBD drive x1.
 * Returns the amplitude at fm of the last 600 blocks of the left (pass A) or
 * the right (pass B) output. */
static double alk_half_run(const uint32_t half, const double f, const double fm,
                           const uint32_t right)
{
	static int16_t r[FXBUF_VOICE_UNIT_SAMPLES];
	static int16_t xl[BLK], xr[BLK];
	static int16_t out[600u * BLK];
	const int32_t kc = bbdKcTone[BBD_COL_TONE];
	const uint32_t kk = (uint32_t)(uint16_t)kc | ((uint32_t)(uint16_t)kc << 16);
	const uint32_t gk = (32767u << 16) | 0x8000u;
	const double wr = 2.0 * M_PI * f / (double)REAL_FS;
	BbdSide s[2];

	memset(r, 0, sizeof(r));
	memset(s, 0, sizeof(s));
	for (unsigned i = 0u; i < 2u; i++) {
		s[i].ring = r;
		s[i].dEnd = (int32_t)(154.0f * 65536.0f);
	}
	for (uint32_t b = 0u; b < 1000u; b++) {
		const uint32_t w = b * (half ? BBD_HALF_BLOCK : BLK);

		for (uint32_t n = 0u; n < BLK; n++)
			xl[n] = (int16_t)lround(12000.0 * sin(wr * (double)(b * BLK + n)));
		for (unsigned i = 0u; i < 2u; i++) {
			bbd_sidePlan(&s[i], w, 154.0f, kc, half);
			s[i].kk = kk;
		}
		if (half) {
			bbd_alkAH(&s[0], xl, xr, w, gk, 8192u);
			bbd_alkTH(&s[1], xr, gk);
		} else {
			bbd_alkA(&s[0], xl, xr, w, gk, 8192u);
			bbd_alkB(&s[1], xr, gk);
		}
		if (b >= 400u)
			memcpy(out + (b - 400u) * BLK, right ? xr : xl, sizeof(xl));
	}
	return tone_amp(out, 600u * BLK, fm);
}

/*
 * The bounds: CH-HALF's for the level (0.5 dB at 1 kHz), the images (2 and
 * 5 kHz at 22,054 - f: at most -18 dB re the wet partial) and the folds (15
 * and 18 kHz at 22,054 - f: at most -10 dB re the input partial), on both
 * outputs; and at 6 kHz the half / full ratio within 0.3 dB of the
 * Colloid form's (the pair mean and the tap interpolation are the same
 * structure, the post poles full rate in both: one post pole fewer in a half
 * loop moves the ratio by about 2 dB; break chorus_break_ahalf).
 */
static void test_ahalf(void)
{
	const double fh = (double)REAL_FS / 2.0;

	for (uint32_t side = 0u; side < 2u; side++) {
		const char *nm = side ? "right" : "left";

		{
			const double db = 20.0 * log10(alk_half_run(1u, 1000.0, 1000.0, side) /
			                               alk_half_run(0u, 1000.0, 1000.0, side));

			printf("CH-AHALF: %s, 1 kHz wet, half against full %+.2f dB\n", nm, db);
			if (fabs(db) > 0.5) fail(G_AHALF, "1 kHz level", db, 0.5);
		}
		{
			/* the half form's own loss at 6 kHz (the pair mean and the tap
			 * interpolation) is the Colloid form's: the post poles run at the
			 * full rate in both forms, so the half / full ratio must match
			 * the Colloid's (CH-HALF's loops) */
			const double db = 20.0 * log10(alk_half_run(1u, 6000.0, 6000.0, side) /
			                               alk_half_run(0u, 6000.0, 6000.0, side));
			const double col = 20.0 * log10(half_run(1u, 6000.0, 6000.0) /
			                                half_run(0u, 6000.0, 6000.0));

			printf("CH-AHALF: %s, 6 kHz wet (the two post poles), half against full %+.2f dB "
			       "(the Colloid's form %+.2f dB)\n", nm, db, col);
			if (fabs(db - col) > 0.3) fail(G_AHALF, "6 kHz half / full against the Colloid's", db, col);
		}
		{
			static const double fi[2] = { 2000.0, 5000.0 };

			for (unsigned i = 0u; i < 2u; i++) {
				const double db = 20.0 * log10(alk_half_run(1u, fi[i], fh - fi[i], side) /
				                               alk_half_run(1u, fi[i], fi[i], side));

				printf("CH-AHALF: %s, image of %.0f Hz at %.0f Hz %.1f dB re the wet partial\n",
				       nm, fi[i], fh - fi[i], db);
				if (db > -18.0) fail(G_AHALF, "image", fi[i], db);
			}
		}
		{
			static const double fa[2] = { 15000.0, 18000.0 };

			for (unsigned i = 0u; i < 2u; i++) {
				const double db = 20.0 * log10(alk_half_run(1u, fa[i], fh - fa[i], side) / 12000.0);

				printf("CH-AHALF: %s, fold of %.0f Hz at %.0f Hz %.1f dB re the input\n",
				       nm, fa[i], fh - fa[i], db);
				if (db > -10.0) fail(G_AHALF, "fold", fa[i], db);
			}
		}
	}
}

/* --------------------------------------------------------------- CH-INIT */
static void test_init(void)
{
	for (uint32_t n = 0u; n < FXBUF_VOICE_UNIT_SAMPLES; n++)
		unit[n] = (int16_t)0x5A5A;
	bbdChorus_init(unit, 0u, BBD_DPT_II);
	for (uint32_t n = 0u; n < BBD_STATE_OFS; n++)
		if (unit[n] != 0) { fail(G_INIT, "the unit is not cleared", n, unit[n]); break; }
	if (bbd_state(unit)->side[0].ring != unit ||
	    bbd_state(unit)->side[1].ring != unit + BBD_RING_R_OFS)
		fail(G_INIT, "the stereo rings", 0, 0);
	bbdChorus_init(unit, 1u, BBD_DPT_I);
	if (bbd_state(unit)->side[1].ring != unit)
		fail(G_INIT, "the mono ring", 0, 0);
}

int main(void)
{
	unsigned g, total = 0u;

	test_tap();
	test_guard();
	test_law();
	test_mix0();
	test_slew();
	test_src();
	test_alk();
	test_init();
	test_half();
	test_dpt();
	test_ahalf();
	for (g = 0u; g < G_N; g++) {
		printf("chorus %-8s failures: %u\n", g_name[g], g_fail[g]);
		total += g_fail[g];
	}
	return total ? 1 : 0;
}
