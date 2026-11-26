/*
 * LFO host test (S094 P1 S1).
 *
 * What:       compiles the real lfo_calc(), lfo_calcPhaseInc(), lfo_setFreq()
 *             and lfo_setSync() (lfo.c), the LFO_SR macro (lfo.h) and the real
 *             generated lfoRateHz[] (lfoRateTable.c), and checks:
 *               LF1  order independence: after rate -> sync, sync -> rate and
 *                    sync -> off writes, phaseInc equals
 *                    lfo_calcPhaseInc(freq, sync) for every sync 0..11 and
 *                    frq 0, 1, 63, 127; sync off returns to the free rate
 *                    without a frq write;
 *               LF2  the table: entry 0 is 0 Hz and gives phaseInc 0 (the LFO
 *                    holds its phase over 1,000 blocks); entry 1 is 0.01 Hz;
 *                    entries 63..127 are MIDI notes 0..64 to 1 ppm;
 *               LF3  increasing from entry 1; every step 1..2.8 semitones; the
 *                    step changes by <= 0.03 semitone between neighbours;
 *               LF4  sync wins (L-D2): with sync set, a modNodeValue of 0, 0.5
 *                    and 2 advances the phase by exactly phaseInc per block;
 *                    with sync off it advances by phaseInc x modNodeValue;
 *               LF5  the top rate x 2 (the overlay's maximum) stays below
 *                    2^32 in lfo_calc()'s step.
 *               LF6  (S101 P2 A1) lfo_stepBlock() + lfo_calc() advance the
 *                    phase exactly as two lfo_calc() calls, for free and
 *                    synced rates and rate overlays 0, 0.5, 1, 2, over 1,000
 *                    control runs; a noise LFO whose phase wraps in the
 *                    stepped block draws its next random value.
 *               LF-OS1 (S104 P1) every one-shot 8..15, at two rates with
 *                    offset 0, equals its looping wave over one cycle and
 *                    then holds its end value (sin 0.5, tri 0, sup 1, sdn 0,
 *                    sq1 0, rn1 0, xu1 1, xd1 0);
 *               LF-OS2 after the end, 1,000 more step+calc pairs keep the
 *                    end value and the DONE state;
 *               LF-OS3 with offset 64 the start value holds for 64/127 of a
 *                    cycle (+-1 block), then the cycle runs;
 *               LF-OS4 `sq1` is 0 during the delay, 1 for the first half and
 *                    0 after; `rn1` draws exactly once per retrigger, on
 *                    its first running control (after the delay, if any);
 *               LF-OS5 waves 0..7 stay bit-identical to a reference copy of
 *                    the pre-S104 lfo_calc().
 *             S101 P2 break: an lfo_stepBlock() that does not step must fail
 *             LF6.
 *             Break builds (Makefile): a sync writer that only stores the
 *             byte must fail LF1; lfo_calc() without the sync select must
 *             fail LF4; a one-shot whose RUN wrap does not enter DONE must
 *             fail LF-OS1.
 * Why:        S094 P1 plan §3: the sync bug and the new rate law are host
 *             laws, not only a listening check.
 * Inputs:     generated fragments (Makefile target lfo).
 * Outputs:    failure counts per law; exit 1 on any failure.
 * Accessors:  make -C tools/dsp_test lfo.
 * Affiliates: tools/plots/gen_lfo_rate_table.py (the same C1..C4 checks on
 *             the generator side).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define REAL_FS ((float)44108.07291666667f)
#define OUTPUT_DMA_SIZE 32
#define LFO_RATE_COUNT 128u
#include "lfo_sr.inc"            /* the real LFO_SR macro (lfo.h) */

/* the waveform ids lfo_calc() switches on (lfo.h) */
#include "lfo_waves.inc"
#define SINE 0

/*
 * Lfo stand-in: the members the extracted functions read, by their real
 * names (as modules/alkaline.c stands in for Lfo). The phase law does not
 * depend on the other members.
 */
typedef struct {
	uint32_t phase, phaseInc;
	uint8_t waveform, retrigger, osState;
	uint32_t phaseOffset;
	float rnd;
	uint8_t sync;
	float freq;
	float modNodeValue;
	uint8_t polarity;
} Lfo;

/* host stand-ins for what lfo_calc() reads */
int16_t sine_table[1025];   /* S095 P2: the 1,024-point sine + guard */
static int host_rng_draws;
int16_t GetRngValue(void) { host_rng_draws++; return 0; }
static uint16_t host_bpm = 120u;
uint16_t seq_getBpm(void) { return host_bpm; }

#include "lfo_rate_table.c"      /* the real lfoRateHz[] */
#include "lfo_funcs.c"           /* the real lfo_calc/_calcPhaseInc/_setFreq/_setSync */

static unsigned g_fail[11];
static const char *g_name[] = {
	"LF1", "LF2", "LF3", "LF4", "LF5", "LF6",
	"LF-OS1", "LF-OS2", "LF-OS3", "LF-OS4", "LF-OS5"
};

static void fail(int g, const char *what, double a, double b)
{
	if (g_fail[g] < 5u)
		printf("  %s FAIL: %s (%g, %g)\n", g_name[g], what, a, b);
	g_fail[g]++;
}

/* LF1: every write order ends on the law */
static void test_order(void)
{
	static const uint8_t frqs[] = { 0u, 1u, 63u, 127u };

	for (unsigned f = 0u; f < sizeof frqs; f++)
		for (uint8_t s = 0u; s <= 11u; s++) {
			Lfo a, b;

			memset(&a, 0, sizeof a);
			memset(&b, 0, sizeof b);
			/* rate then sync */
			lfo_setFreq(&a, frqs[f]);
			lfo_setSync(&a, s);
			/* sync then rate */
			lfo_setSync(&b, s);
			lfo_setFreq(&b, frqs[f]);
			if (a.phaseInc != lfo_calcPhaseInc(a.freq, s)) fail(0, "rate then sync", f, s);
			if (b.phaseInc != lfo_calcPhaseInc(b.freq, s)) fail(0, "sync then rate", f, s);
			/* back to free without a rate write */
			lfo_setSync(&a, 0u);
			if (a.phaseInc != lfo_calcPhaseInc(lfoRateHz[frqs[f]], 0u))
				fail(0, "sync off returns to the free rate", f, s);
		}
}

/* LF2 / LF3 / LF5: the table */
static void test_table(void)
{
	Lfo l;

	if (lfoRateHz[0] != 0.0f) fail(1, "entry 0 is 0 Hz", lfoRateHz[0], 0);
	if (fabs(lfoRateHz[1] - 0.01) > 1e-7) fail(1, "entry 1 is 0.01 Hz", lfoRateHz[1], 0.01);
	for (unsigned i = 63u; i < 128u; i++) {
		const double want = 440.0 * pow(2.0, ((double)(i - 63u) - 69.0) / 12.0);

		if (fabs(lfoRateHz[i] / want - 1.0) > 1e-6) fail(1, "semitone grid", i, lfoRateHz[i]);
	}
	/* frq 0 stops: phaseInc 0, the phase holds */
	memset(&l, 0, sizeof l);
	l.modNodeValue = 1.0f;
	l.phase = 0x12345678u;
	lfo_setFreq(&l, 0.0f);
	for (int b = 0; b < 1000; b++)
		(void)lfo_calc(&l);
	if (l.phaseInc != 0u || l.phase != 0x12345678u) fail(1, "frq 0 holds the phase", l.phaseInc, 0);
	for (unsigned i = 1u; i < 127u; i++) {
		const double st = 12.0 * log2((double)lfoRateHz[i + 1u] / lfoRateHz[i]);

		if (!(st >= 0.999 && st <= 2.8)) fail(2, "step 1..2.8 semitones", i, st);
		if (i < 126u) {
			const double nx = 12.0 * log2((double)lfoRateHz[i + 2u] / lfoRateHz[i + 1u]);

			if (fabs(nx - st) > 0.03) fail(2, "step changes <= 0.03 semitone", i, nx - st);
		}
	}
	if (2.0 * lfo_calcPhaseInc(lfoRateHz[127], 0u) >= 4294967296.0)
		fail(4, "top x 2 below 2^32", lfo_calcPhaseInc(lfoRateHz[127], 0u), 0);
}

/* LF4: sync wins over the rate overlay */
static void test_syncwins(void)
{
	static const float mods[] = { 0.0f, 0.5f, 2.0f };

	for (unsigned m = 0u; m < 3u; m++)
		for (uint8_t s = 0u; s < 2u; s++) {
			Lfo l;
			uint32_t before;

			memset(&l, 0, sizeof l);
			lfo_setFreq(&l, 90.0f);
			lfo_setSync(&l, s ? 10u : 0u);
			l.modNodeValue = mods[m];
			before = l.phase;
			(void)lfo_calc(&l);
			{
				const uint32_t want = s ? l.phaseInc
				                        : (uint32_t)((float)l.phaseInc * mods[m]);

				if (l.phase - before != want) fail(3, "phase step", m, s);
			}
		}
}

/* LF6 (S101 P2 A1): the staggered control's two-block step is exact */
static void test_stepblock(void)
{
	static const float mods[] = { 0.0f, 0.5f, 1.0f, 2.0f };
	static const uint8_t frqs[] = { 1u, 40u, 100u, 127u };

	for (unsigned m = 0u; m < 4u; m++)
		for (unsigned f = 0u; f < 4u; f++)
			for (uint8_t s = 0u; s < 2u; s++) {
				Lfo a, b;

				memset(&a, 0, sizeof a);
				lfo_setFreq(&a, frqs[f]);
				lfo_setSync(&a, s ? 10u : 0u);
				a.modNodeValue = mods[m];
				a.waveform = LFO_SINE;
				b = a;
				for (int r = 0; r < 1000; r++) {
					lfo_stepBlock(&a);
					(void)lfo_calc(&a);
					(void)lfo_calc(&b);
					(void)lfo_calc(&b);
				}
				if (a.phase != b.phase) fail(5, "two-block step == two calls", f, m);
			}
	/* noise: a step that wraps in lfo_stepBlock() draws */
	{
		Lfo n;

		memset(&n, 0, sizeof n);
		n.waveform = LFO_NOISE;
		n.modNodeValue = 1.0f;
		n.phaseInc = 0xC0000000u;   /* wraps on most steps */
		n.phase = 0x50000000u;      /* + 0xC0000000 wraps */
		host_rng_draws = 0;
		lfo_stepBlock(&n);
		if (host_rng_draws != 1) fail(5, "noise draws on a stepped wrap", host_rng_draws, 1);
	}
}

/* The pre-S104 lfo_calc() for waves 0..7 (LF-OS5's reference). */
static float lfo_calcRef(Lfo *lfo)
{
	const uint32_t oldPhase = lfo->phase;
	const float rateScale = lfo->sync ? 1.0f : lfo->modNodeValue;
	const uint32_t incInt = (uint32_t)(lfo->phaseInc * rateScale);

	lfo->phase += incInt;
	switch (lfo->waveform) {
	case LFO_SINE:
		return ((sine_table[lfo->phase >> 22] / 32767.f) + 1) / 2.f;
	case LFO_TRI:
		return (1.f - fabsf((lfo->phase / (float)0xffffffff) * 2 - 1));
	case LFO_SAW_UP:
		return lfo->phase / (float)0xffffffff;
	case LFO_REC:
		return (lfo->phase > 0x7fffffffu) ? 1.f : 0.f;
	case LFO_NOISE:
		if (oldPhase > lfo->phase)
			lfo->rnd = (float)(GetRngValue()) / 32767.0f;
		return lfo->rnd;
	case LFO_SAW_DOWN:
		return (0xffffffff - lfo->phase) / (float)0xffffffff;
	case LFO_EXP_UP: {
		const float x = lfo->phase / (float)0xffffffff;
		return x * x * x;
	}
	case LFO_EXP_DOWN: {
		const float x = (0xffffffff - lfo->phase) / (float)0xffffffff;
		return x * x * x;
	}
	default:
		return 0.f;
	}
}

static void lfoHostReset(Lfo *l, uint8_t wave, float frq)
{
	memset(l, 0, sizeof *l);
	l->waveform = wave;
	l->osState = LFO_OS_RUN;
	l->modNodeValue = 1.0f;
	lfo_setFreq(l, frq);
}

/* LF-OS1 / LF-OS2: one cycle equals the looping wave, then the end holds. */
static void test_oneshots(void)
{
	static const float frqs[2] = { 90.0f, 127.0f };
	static const float endv[8] = {
		0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f
	};
	unsigned w, f;

	for (w = 0u; w < 8u; w++)
		for (f = 0u; f < 2u; f++) {
			Lfo os, lo;
			int ended = 0;
			int b;

			lfoHostReset(&os, (uint8_t)(LFO_OS_FIRST + w), frqs[f]);
			lfoHostReset(&lo, (uint8_t)w, frqs[f]);
			for (b = 0; b < 4000; b++) {
				const float ov = lfo_calc(&os);
				const float lv = lfo_calc(&lo);

				if (!ended) {
					if (os.osState == LFO_OS_DONE) {
						ended = 1;
						if (fabsf(ov - endv[w]) > 1e-6f)
							fail(6, "one-shot end value", w, ov);
					} else if ((w == 4u) ? (ov != (1.0f - lv)) : (ov != lv)) {
						/* sq1 is the inverted looping REC (S104 P1). */
						fail(6, "one-shot over one cycle", w, b);
					}
				} else if (fabsf(ov - endv[w]) > 1e-6f) {
					fail(6, "end value held", w, ov);
				}
			}
			if (!ended) {
				fail(6, "one-shot reached its end", w, f);
				continue;
			}
			/* LF-OS2: 1,000 more blocks: the value and the pinned phase. */
			for (b = 0; b < 1000; b++) {
				float ov;

				lfo_stepBlock(&os);
				ov = lfo_calc(&os);
				if (fabsf(ov - endv[w]) > 1e-6f)
					fail(7, "end value after more blocks", w, ov);
				if (os.osState != LFO_OS_DONE)
					fail(7, "DONE holds over later wraps", w, os.osState);
			}
		}
}

/* LF-OS3: offset = start delay, PRE holds the start value. */
static void test_delay(void)
{
	Lfo l;
	const uint32_t offset = (uint32_t)(((uint64_t)64u * 0xFFFFFFFFu) / 127u);
	int b;
	uint32_t seenPre = 0u;

	lfoHostReset(&l, LFO_OS_TRI, 90.0f);
	l.phaseOffset = offset;
	if (l.phaseInc == 0u)
		return;
	{
		const uint32_t pre = (uint32_t)((uint64_t)offset / l.phaseInc);

		lfo_retriggerPhase(&l);
		if (l.osState != LFO_OS_PRE)
			fail(8, "offset starts in PRE", l.osState, LFO_OS_PRE);
		for (b = 0; b < 4000; b++) {
			const float v = lfo_calc(&l);

			if (l.osState == LFO_OS_PRE) {
				seenPre++;
				if (fabsf(v) > 1e-6f)
					fail(8, "start value during the delay", b, v);
			} else {
				break;
			}
		}
		if (seenPre + 1u < pre || seenPre > pre + 1u)
			fail(8, "delay length", seenPre, pre);
	}
}

/* LF-OS4: sq1 inverted with a 0 delay; rn1 draws once per retrigger. */
static void test_rec_and_noise(void)
{
	Lfo l;
	int draws;
	float v;
	int b;
	int sawHigh = 0, sawLow = 0, sawLate = 0;

	lfoHostReset(&l, LFO_OS_REC, 100.0f);
	lfo_retriggerPhase(&l);
	for (b = 0; b < 200; b++) {
		v = lfo_calc(&l);
		if (l.osState == LFO_OS_RUN) {
			const float want = (l.phase <= 0x7fffffffu) ? 1.0f : 0.0f;

			if (v != want)
				fail(9, "sq1 high first half", b, v);
			if (want == 1.0f)
				sawHigh = 1;
			else
				sawLow = 1;
		} else {
			sawLate = 1;
		}
	}
	if (!sawHigh || !sawLow || !sawLate)
		fail(9, "sq1 halves", sawHigh, sawLow | (sawLate << 1));

	/* a delay on sq1 outputs 0 */
	lfoHostReset(&l, LFO_OS_REC, 90.0f);
	l.phaseOffset = 0x40000000u;
	lfo_retriggerPhase(&l);
	if (lfo_calc(&l) != 0.0f)
		fail(9, "sq1 zero during the delay", 0, 0);

	/* rn1: one draw on the first running control, none after */
	lfoHostReset(&l, LFO_OS_NOISE, 90.0f);
	host_rng_draws = 0;
	lfo_retriggerPhase(&l);
	(void)lfo_calc(&l);
	if (host_rng_draws != 1)
		fail(9, "rn1 draws once", host_rng_draws, 1);
	draws = host_rng_draws;
	for (b = 0; b < 100; b++) {
		lfo_stepBlock(&l);
		(void)lfo_calc(&l);
	}
	if (host_rng_draws != draws)
		fail(9, "rn1 holds one value", host_rng_draws, draws);

	/* rn1 with a delay: no draw until the delay ends, then exactly one */
	lfoHostReset(&l, LFO_OS_NOISE, 90.0f);
	l.phaseOffset = 0x40000000u;
	host_rng_draws = 0;
	lfo_retriggerPhase(&l);
	while (l.osState == LFO_OS_PRE && host_rng_draws == 0)
		(void)lfo_calc(&l);
	if (host_rng_draws != 0)
		fail(9, "rn1 holds through the delay", host_rng_draws, 0);
	for (b = 0; b < 100; b++)
		(void)lfo_calc(&l);
	if (host_rng_draws != 1)
		fail(9, "rn1 draws once after the delay", host_rng_draws, 1);
}

/* LF-OS5: the looping waves are unchanged. */
static void test_loop_identical(void)
{
	unsigned w, i;

	for (w = 0u; w < 8u; w++)
		for (i = 0u; i < 200u; i++) {
			Lfo a, b;
			const uint32_t inc = (uint32_t)(0x01000000u + i * 0x00100000u);
			const uint32_t ph = (uint32_t)(i * 0x00ABCDEFu);

			lfoHostReset(&a, (uint8_t)w, 90.0f);
			a.phase = ph;
			a.phaseInc = inc;
			b = a;
			if (lfo_calc(&a) != lfo_calcRef(&b))
				fail(10, "looping wave identical", w, i);
		}
}

int main(void)
{
	unsigned total = 0u;

	test_order();
	test_table();
	test_syncwins();
	test_stepblock();
	test_oneshots();
	test_delay();
	test_rec_and_noise();
	test_loop_identical();
	for (unsigned g = 0u; g < 11u; g++) {
		printf("lfo %s: %u failure(s)\n", g_name[g], g_fail[g]);
		total += g_fail[g];
	}
	return total ? 1 : 0;
}
