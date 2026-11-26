/*
 * seq_tempo.c - host laws TP-PERIOD .. TP-ROUND for the 1/100-BPM tempo
 * (S104 P1).
 *
 * What:       compiles the extracted firmware functions (build/gen/
 *             seq_tempo_funcs.c) and checks:
 *               TP-PERIOD the tempo-timer period at 120.00, 133.00, 128.37 and
 *                         255.99 BPM equals 60,000 / bpm / 96 x 4 within 1e-5
 *                         relative (the integer form truncated 133.00 to
 *                         133.04);
 *               TP-NUDGE  +0.05 gives 1.05 x base; the external-sync flag
 *                         leaves the base; values beyond +-0.05 clamp;
 *               TP-ROUND  seq_getBpm() rounds 12049 -> 120 and 12050 -> 121,
 *                         and lfo_recalcSync() runs only when the rounded
 *                         value changes.
 * Why:        DJ mode writes 1/100 BPM and a nudge; both feed the TIM3 timer
 *             and the step-length gate. The Makefile break build restores the
 *             integer `(60000 / bpm)` term, and TP-PERIOD must fail at 133.00.
 * Inputs:     none.  Outputs: a line per failure; exit 1 on any.
 * Accessors:  make -C tools/dsp_test seq_tempo.
 * Affiliates: Core/Sequencer/sequencer.c (the extracted functions),
 *             Core/DSPAudio/lfo.c lfo_recalcSync().
 */
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#define SEQ_INTERNAL_PPQ      96
#define SYSTICK_TICKS_PER_MS  4

/* the tempo statics the extracted functions read (owned here) */
static volatile uint16_t seq_tempoCenti = 12000u;
static volatile float    seq_tempoNudge = 0.0f;
static volatile float    seq_tempoEff   = 120.0f;
static float             seq_deltaT;

static uint8_t  hostExtSync;
static unsigned hostLfoRecalc;

static uint8_t seq_getExtSync(void)
{
	return hostExtSync;
}

static void lfo_recalcSync(void)
{
	hostLfoRecalc++;
}

/* the extracted functions' own entry points (source order) */
void seq_setTempoCenti(uint16_t centi);
uint16_t seq_getTempoCenti(void);
void seq_setTempoNudge(float nudge);
float seq_getTempoNudge(void);
uint16_t seq_getBpm(void);

#include "seq_tempo_funcs.c"

static int failures;

static void expect(int cond, const char *msg, double a, double b)
{
	if (!cond) {
		printf("FAIL: %s (%.9f, %.9f)\n", msg, a, b);
		failures++;
	}
}

int main(void)
{
	static const uint16_t centi[4] = { 12000u, 13300u, 12837u, 25599u };
	unsigned i;

	/* TP-PERIOD: the float period against the exact 60,000 / bpm. */
	for (i = 0u; i < 4u; i++) {
		const double bpm = (double)centi[i] / 100.0;
		const double want = 60000.0 / bpm / 96.0 * 4.0;
		double got;

		seq_setTempoCenti(centi[i]);
		seq_calcDeltaT();
		got = (double)seq_deltaT;
		expect(fabs(got - want) <= 1e-5 * want, "TP-PERIOD", got, want);
	}

	/* TP-NUDGE: +5 %, external sync, clamping. */
	seq_setTempoCenti(12000u);
	seq_setTempoNudge(0.05f);
	expect(fabs((double)seq_tempoEff - 126.0) <= 1e-4, "TP-NUDGE +5%",
	       seq_tempoEff, 126.0);
	hostExtSync = 1u;
	seq_setTempoNudge(0.05f);
	expect(fabs((double)seq_tempoEff - 120.0) <= 1e-4, "TP-NUDGE external",
	       seq_tempoEff, 120.0);
	hostExtSync = 0u;
	seq_setTempoNudge(0.5f);
	expect(fabs((double)seq_getTempoNudge() - 0.05) <= 1e-6, "TP-NUDGE clamp up",
	       seq_getTempoNudge(), 0.05);
	seq_setTempoNudge(-0.5f);
	expect(fabs((double)seq_getTempoNudge() + 0.05) <= 1e-6, "TP-NUDGE clamp down",
	       seq_getTempoNudge(), -0.05);
	seq_setTempoNudge(0.0f);

	/* TP-ROUND: rounding and the LFO recompute trigger. */
	hostLfoRecalc = 0u;
	seq_setTempoCenti(12049u);
	expect(seq_getBpm() == 120u, "TP-ROUND 12049", seq_getBpm(), 120.0);
	seq_setTempoCenti(12050u);
	expect(seq_getBpm() == 121u, "TP-ROUND 12050", seq_getBpm(), 121.0);
	expect(hostLfoRecalc == 1u, "TP-ROUND one LFO recalc", hostLfoRecalc, 1.0);
	seq_setTempoCenti(12051u);
	expect(hostLfoRecalc == 1u, "TP-ROUND no recalc within a whole BPM",
	       hostLfoRecalc, 1.0);

	if (failures)
		printf("seq_tempo: %d failure(s)\n", failures);
	else
		printf("seq_tempo: all laws pass\n");
	return failures ? 1 : 0;
}
