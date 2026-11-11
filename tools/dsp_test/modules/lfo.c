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
 *             Break builds (Makefile): a sync writer that only stores the
 *             byte must fail LF1; lfo_calc() without the sync select must
 *             fail LF4.
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
	uint8_t waveform, retrigger;
	uint32_t phaseOffset;
	float rnd;
	uint8_t sync;
	float freq;
	float modNodeValue;
	uint8_t polarity;
} Lfo;

/* host stand-ins for what lfo_calc() reads */
int16_t sine_table[4097];
int16_t GetRngValue(void) { return 0; }
static uint16_t host_bpm = 120u;
uint16_t seq_getBpm(void) { return host_bpm; }

#include "lfo_rate_table.c"      /* the real lfoRateHz[] */
#include "lfo_funcs.c"           /* the real lfo_calc/_calcPhaseInc/_setFreq/_setSync */

static unsigned g_fail[5];
static const char *g_name[] = { "LF1", "LF2", "LF3", "LF4", "LF5" };

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

int main(void)
{
	unsigned total = 0u;

	test_order();
	test_table();
	test_syncwins();
	for (unsigned g = 0u; g < 5u; g++) {
		printf("lfo %s: %u failure(s)\n", g_name[g], g_fail[g]);
		total += g_fail[g];
	}
	return total ? 1 : 0;
}
