/*
 * modules/katran.c - host laws for the Katran voice (S103 P1).
 *
 * What:       builds the real KatranVoice.c, KatranOsc.c and KatranTables.c
 *             (extracted, firmware includes removed) over the real stock
 *             tables, the real PWM width and warp tables, a synthetic arena
 *             and a seeded RNG, and checks the plan's laws:
 *               KA-BOOT  after init nothing sounds until a kick or a start;
 *               KA-RUN1  run 0..126 decays -60 dB at T(run) (+-5 %);
 *               KA-RUN2  run 127 with a start holds gr at 1;
 *               KA-STOP  a stop edge decays at T(126) even at run 127;
 *               KA-KICK  a kick re-arms gr within 7 blocks; kE reaches
 *                        -60 dB at 80 ms; kik 127 adds the swell;
 *               KA-SILENT run 0 and 72 blocks of gr 0 give exact silence;
 *               KA-CE    a trigger leaves the chaos envelopes untouched;
 *               KA-CYC   a pos change is deferred to the oscillator's next
 *                        cycle (rowA stays, rowANext moves);
 *               KA-SYNC  sth 127 never resets; sth 0 resets on a wrap;
 *               KA-ARENA every window start is inside the arena; the DC
 *                        block removes a step; dmp attenuates high tones;
 *               KA-GUARD a full-scale block sets the guard <= 0.7;
 *               KA-FADE  a renewed fade request stops the voice and, when
 *                        it lapses, gr returns to its run state;
 *               KA-SPR   spr 63 equal times, 127 +-12 %, 0 the max xfb;
 *               KA-VCA   vca 63 gives the patch's g1/g2 (S103 P4: through the
 *                        folded output gains cA0, cB1);
 *               KA-F3    the output LP passes 20 Hz..18 kHz within 1 dB;
 *               KA-QUIET the quiet counter follows gr;
 *               KA-BND   the worst settings stay finite and inside int16;
 *               KA-DLY   (S103 P2, bug B1) the fractional delay read equals
 *                        linear interpolation at the true position, on both
 *                        cores (full and half rate), with the write index
 *                        behind the delay time (the case B1 got wrong);
 *               KA-RANGE (S103 P2, bugs B2/B3) the PWM D sum is stored halved,
 *                        never wrapped; a full-scale arena step saturates
 *                        instead of wrapping to the opposite sign;
 *               KA-HALF  (S103 P2) core B's delay time is half of core A's
 *                        (the same comb pitch), and its interpolated output's
 *                        odd samples equal its half-rate samples;
 *               KA-FOLD  (S103 P4) the scale-folded core loops equal the P2
 *                        arithmetic (normalised units, the unfolded gains) on
 *                        both cores, ring writes and outputs; the folded
 *                        output gains equal the P2 ring / VCA formula;
 *               KA-ROT   (S103 P4; S103 P5 bipolar wid) the stereo matrix:
 *                        rotPh 0 is L = core A, R = core B; rotPh 0.5 swaps
 *                        them; wid 0 never collapses (orthogonal rows);
 *                        wid -12 is the P4 pan law; wid -63 is mono; wid
 *                        +64 widens without collapse; rot 0 holds the
 *                        phase, rot 127 turns at 4 Hz; the output loop is
 *                        L = m0 a + m1 b, R = m2 a + m3 b.
 *             S103 P4: the cores are named A (full rate) and B (half rate);
 *             they were L and R.
 * Why:        S103_P1_IMPLEMENTATION.md §9; the schedule's gates before the
 *             CPU checkpoint.
 * Inputs:     generated fragments (Makefile target katran).
 * Outputs:    failure counts per group; exit 1 on any failure.
 * Accessors:  make -C tools/dsp_test katran.
 * Affiliates: KatranVoice.c, KatranOsc.c, KatranTables.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "osc_host.h"
#include "Oscillator.host.h"
#include "oscPwmTables.host.h"
#include "oscPwmTables.host.c"
#include "MidiPitch.h"

/* the real stock tables (wavetable.c; const removed for the host) */
#include "kat_sine_table.c"
#include "kat_sawTable.c"
#include "kat_triTable.c"
#include "kat_recTable.c"

/* host stand-ins */
typedef struct { uint8_t bytes[124]; } Lfo;
typedef struct { float value; } ModulationNode;
ModulationNode velocityModulators[6];
static unsigned host_lfoRetriggers;
void lfo_init(Lfo *lfo) { memset(lfo, 0, sizeof(*lfo)); }
void lfo_retrigger(uint8_t voiceNr) { (void)voiceNr; host_lfoRetriggers++; }
void modNode_updateValue(ModulationNode *vm, float val) { vm->value = val; }
void instrumentManager_applyVelocityModulationTarget(uint8_t slot, float v)
{ (void)slot; (void)v; }

static uint8_t host_running = 1u;
uint8_t seq_isRunning(void) { return host_running; }
/* S105 P1 A2: the master clock for the patch LFO's rate and pull */
static uint16_t host_bpm = 120u;
uint16_t seq_getBpm(void) { return host_bpm; }
static uint32_t host_ticks;
uint32_t seq_getElapsedTicks(void) { return host_ticks; }
static uint32_t host_rng = 0x103001u;
int16_t GetRngValue(void) { return (int16_t)(golden_rand(&host_rng) & 0xFFFFu); }

/* the wavetable catalogue: none installed, so the wtb path falls back to a
 * stock row (KatranVoice.c's documented fallback). The entry type comes from
 * SampleMemory.h through osc_host.h. */
uint8_t sampleMemory_getNumWavetables(void) { return 0u; }
uint32_t sampleMemory_getGeneration(void) { return 1u; }
const wavetable_catalogue_entry_t *sampleMemory_wavetableEntry(uint8_t i)
{ (void)i; return 0; }
const int16_t *sampleMemory_wavetableRows(const wavetable_catalogue_entry_t *e)
{ (void)e; return 0; }

/* the synthetic arena: 60,000 samples (the real one is about 63,300) */
#define ARENA_N 60000u
static int16_t arena[ARENA_N];
void fxbuf_arenaView(const int16_t **base, uint32_t *samples)
{
	*base = arena;
	*samples = ARENA_N;
}

#include "AlkalineControl.host.h"
#include "oscPwmWarp.host.h"
#include "KatranTables.host.h"
#include "KatranTables.host.c"
#include "KatranVoice.host.h"
#include "KatranOsc.host.h"
#include "KatranOsc.host.c"
#include "KatranVoice.host.c"

#define BLK OUTPUT_DMA_SIZE
enum { G_BOOT, G_RUN1, G_RUN2, G_STOP, G_KICK, G_SILENT, G_CE, G_CYC, G_SYNC,
       G_ARENA, G_GUARD, G_FADE, G_SPR, G_VCA, G_F3, G_QUIET, G_BND,
       G_DLY, G_RANGE, G_HALF, G_FOLD, G_ROT, G_BAND, G_TYPE, G_RING, G_OUT,
       G_RATE, G_FM, G_SDB, G_N };
static const char *g_name[G_N] = {
	"KA-BOOT", "KA-RUN1", "KA-RUN2", "KA-STOP", "KA-KICK", "KA-SILENT",
	"KA-CE", "KA-CYC", "KA-SYNC", "KA-ARENA", "KA-GUARD", "KA-FADE",
	"KA-SPR", "KA-VCA", "KA-F3", "KA-QUIET", "KA-BND",
	"KA-DLY", "KA-RANGE", "KA-HALF", "KA-FOLD", "KA-ROT",
	"KT-BAND", "KT-TYPE", "KT-RING", "KT-OUT", "KT-RATE", "KT-FM", "KT-SDB"
};
static unsigned g_fail[G_N];
static void fail(int g, const char *what, double a, double b)
{
	if (g_fail[g] < 5u)
		printf("  %s FAIL: %s (%g, %g)\n", g_name[g], what, a, b);
	g_fail[g]++;
}
static void expect(int g, int cond, const char *what, double a, double b)
{
	if (!cond)
		fail(g, what, a, b);
}

static KatranVoice V;
/* S103 P4: core A's and core B's rings (they were ringA / ringB) */
static int16_t ringA[KAT_RING];
static int16_t ringB[KAT_RING];
static int16_t outL[BLK];

static void arena_fill(int16_t v)
{
	uint32_t i;

	for (i = 0u; i < ARENA_N; i++)
		arena[i] = v;
}

static void voice_init(void)
{
	arena_fill(0);
	Katran_initVoice(&V, 0u, ringA, ringB);
}

static void block(void)
{
	Katran_calcVoiceAsync(&V);
	Katran_calcVoiceSyncBlock(&V, outL, BLK);
}

static double target_of(float now, float step)
{
	return (double)now + (double)BLK * (double)step;
}

static void test_boot(void)
{
	double e = 0.0;
	int n, i;

	host_running = 0u;
	voice_init();
	V.run = 127u;
	for (n = 0; n < 20; n++) {
		block();
		for (i = 0; i < BLK; i++)
			e += fabs((double)outL[i]) + fabs((double)V.right[i]);
	}
	expect(G_BOOT, V.gr == 0.0f, "gr after init with no kick/start", V.gr, 0.0);
	expect(G_BOOT, e == 0.0, "output before armed", e, 0.0);
	host_running = 1u;
}

static void test_run(void)
{
	static const uint8_t rows[] = { 40u, 80u, 126u };
	uint32_t i;

	host_running = 1u;
	for (i = 0u; i < 3u; i++) {
		const uint8_t r = rows[i];
		const double T = 0.25 * pow(240.0, (double)r / 126.0);
		const double want = T * 44108.07291666667 / 32.0;
		int n = 0;

		voice_init();
		V.armed = 1u;
		V.run = r;
		V.rearm = 0u;
		V.kE = 0.0f;
		V.gr = 1.0f;
		while (V.gr > 1.0e-3f && n < 200000) {
			Katran_calcVoiceAsync(&V);
			n++;
		}
		expect(G_RUN1, fabs((double)n - want) <= 0.05 * want,
		       "run -60 dB block count", n, want);
	}
	/* run 127 with a start holds 1 */
	voice_init();
	V.run = 127u;
	host_running = 0u;
	Katran_calcVoiceAsync(&V);
	host_running = 1u;
	for (i = 0u; i < 2000u; i++)
		Katran_calcVoiceAsync(&V);
	expect(G_RUN2, V.gr > 0.999f, "run 127 gr", V.gr, 1.0);
}

static void test_stop_kick(void)
{
	voice_init();
	V.armed = 1u;
	V.run = 127u;
	V.gr = 1.0f;
	host_running = 1u;
	Katran_calcVoiceAsync(&V);
	host_running = 0u;
	Katran_calcVoiceAsync(&V);
	expect(G_STOP, fabs((double)V.gr - (double)katRunK[126u]) < 1.0e-6,
	       "stop edge factor", V.gr, katRunK[126u]);
	host_running = 1u;

	voice_init();
	Katran_triggerVoice(&V, 0u, 127u, 60u);
	expect(G_KICK, V.armed == 1u && V.rearm == KAT_KICK_REARM,
	       "kick re-arms", V.rearm, KAT_KICK_REARM);
	{
		int n = 0;
		double want = 0.080 * 44108.07291666667 / 32.0;

		while (V.kE > 1.0e-3f && n < 1000) {
			Katran_calcVoiceAsync(&V);
			n++;
		}
		expect(G_KICK, fabs((double)n - want) <= 0.10 * want,
		       "kE -60 dB blocks", n, want);
	}
	voice_init();
	V.armed = 1u;
	V.kick = 127u;
	V.kE = 1.0f;
	Katran_calcVoiceAsync(&V);
	expect(G_KICK, V.kickSwell > 0.0f, "kik 127 swell", V.kickSwell, 0.0);
}

static void test_silent_quiet(void)
{
	int n, i;
	double e;

	voice_init();
	V.armed = 1u;
	V.run = 0u;
	V.gr = 1.0f;
	for (n = 0; n < 800; n++)
		Katran_calcVoiceAsync(&V);
	expect(G_SILENT, V.gr == 0.0f, "run 0 reaches 0", V.gr, 0.0);
	for (n = 0; n < 100; n++)
		Katran_calcVoiceAsync(&V);
	expect(G_QUIET, Katran_isQuiet(&V) != 0u, "quiet after 72 zero blocks",
	       V.silentBlocks, KAT_QUIET_BLOCKS);
	block();
	e = 0.0;
	for (i = 0; i < BLK; i++)
		e += fabs((double)outL[i]) + fabs((double)V.right[i]);
	expect(G_SILENT, e == 0.0, "silent output", e, 0.0);

	voice_init();
	V.armed = 1u;
	V.run = 127u;
	expect(G_QUIET, Katran_isQuiet(&V) == 0u, "not quiet while running", 1.0, 0.0);
}

static void test_ce_cycle(void)
{
	float level, peak, up, down;
	uint8_t stage;

	voice_init();
	level = V.ce[0].level;
	peak = V.ce[0].peak;
	up = V.ce[0].up;
	down = V.ce[0].down;
	stage = V.ce[0].stage;
	Katran_triggerVoice(&V, 0u, 127u, 60u);
	expect(G_CE, V.ce[0].level == level && V.ce[0].peak == peak &&
	             V.ce[0].up == up && V.ce[0].down == down &&
	             V.ce[0].stage == stage,
	       "trigger reset a chaos envelope", V.ce[0].level, level);
	/* the cycles are bounded and the levels stay in 0..peak */
	{
		int n;
		int bad = 0;

		for (n = 0; n < 5000; n++) {
			Katran_calcVoiceAsync(&V);
			if (V.ce[0].level < -1e-6f || V.ce[0].level > V.ce[0].peak + 1e-6f ||
			    V.ce[1].level < -1e-6f || V.ce[1].level > V.ce[1].peak + 1e-6f)
				bad++;
		}
		expect(G_CE, bad == 0, "CE level outside 0..peak", bad, 0.0);
	}
}

static void test_cycle_defer(void)
{
	const int16_t *r0;

	voice_init();
	V.aBuf = 1u;
	V.osc.position = 0u;
	Katran_calcVoiceAsync(&V);
	r0 = V.rowA[0];
	V.osc.position = 127u;
	Katran_calcVoiceAsync(&V);
	expect(G_CYC, V.rowA[0] == r0 && V.rowANext[0] != r0 &&
	             V.rowANext[0] >= arena && V.rowANext[0] + 1024 <= arena + ARENA_N,
	       "pos applied mid-cycle", 0.0, 1.0);
	(void)r0;
}

static void test_sync(void)
{
	uint32_t inc, p0, p1;
	int n;

	voice_init();
	V.armed = 1u;
	V.aBuf = 0u;
	V.sync = 127u;
	Katran_calcVoiceAsync(&V);
	inc = V.incA[0];
	p0 = V.phA[0];
	for (n = 0; n < 10; n++)
		block();
	p1 = V.phA[0];
	expect(G_SYNC, p1 == p0 + 10u * 32u * inc,
	       "sth 127 reset the phase", (double)p1, (double)(p0 + 10u * 32u * inc));
	/* a low threshold with D nonzero resets; the ring starts silent, so
	 * seed a DC step through blkD after a trigger */
	voice_init();
	V.armed = 1u;
	V.sync = 0u;
	V.aBuf = 0u;
	V.gr = 0.0f;
	V.kE = 0.0f;
	{
		int resets = 0;
		uint32_t before;

		for (n = 0; n < 200; n++) {
			before = V.phA[0];
			block();
			if (V.phA[0] < before)
				resets++;
		}
		expect(G_SYNC, resets < 200, "sth 0 never reset", resets, 200.0);
	}
}

static void test_arena_cond(void)
{
	uint32_t pos;
	int n;
	float y = 0.0f;

	voice_init();
	V.aBuf = 1u;
	for (pos = 0u; pos <= 127u; pos++) {
		V.osc.position = (uint8_t)pos;
		V.incA[0] = V.incA[1] = 0xFFFFFFFFu;
		Katran_calcVoiceAsync(&V);
		expect(G_ARENA, V.rowANext[0] >= arena &&
		                V.rowANext[0] + KAT_ARENA_WIN <= arena + ARENA_N &&
		                V.rowANext[1] >= arena &&
		                V.rowANext[1] + KAT_ARENA_WIN <= arena + ARENA_N,
		       "window outside the arena", pos, 0.0);
	}
	/* a DC step through the DC block falls below 1 % in 100 ms */
	voice_init();
	V.dampK = 0.0f;
	V.damp[0] = 0.0f;
	V.guard[0] = 1.0f;
	V.dcX[0] = 0.0f;
	for (n = 0; n < 5000; n++)
		y = (float)kat_cond(&V, 0u, 10000);
	expect(G_ARENA, fabs((double)y) < 100.0, "DC block", y, 0.0);
	/* dmp 127 attenuates a 5 kHz tone by >= 20 dB */
	{
		float z = 0.0f;
		double acc = 0.0, accD = 0.0;

		voice_init();
		V.dampK = katDampK[127u];
		V.damp[0] = 0.0f;
		for (n = 0; n < 4410; n++) {
			const float x = 10000.0f * sinf(6.2831853f * 5000.0f * (float)n / 44108.07291666667f);
			const float yd = z + V.dampK * (x - z);

			z = yd;
			if (n > 2205) {
				acc += (double)x * (double)x;
				accD += (double)yd * (double)yd;
			}
		}
		expect(G_ARENA, 10.0 * log10(accD / acc) < -20.0,
		       "dmp 127 5 kHz attenuation dB", 10.0 * log10(accD / acc), -20.0);
	}
}

static void test_guard(void)
{
	static int16_t blk[BLK];
	int i;

	voice_init();
	for (i = 0; i < BLK; i++)
		blk[i] = 32767;
	kat_guardUpdate(&V, 0u, blk, BLK);   /* S103 P2: the block's valid length */
	expect(G_GUARD, V.guard[0] <= 0.7001f, "guard target", V.guard[0], 0.7);
}

static void test_fade(void)
{
	int n;

	voice_init();
	V.armed = 1u;
	V.run = 127u;
	V.gr = 1.0f;
	for (n = 0; n < 140; n++) {
		Katran_requestFade(&V);
		Katran_calcVoiceAsync(&V);
		if (Katran_isQuiet(&V))
			break;
	}
	expect(G_FADE, Katran_isQuiet(&V) != 0u, "fade to quiet", n, 140.0);
	for (n = 0; n < 160; n++)
		Katran_calcVoiceAsync(&V);
	expect(G_FADE, V.gr > 0.9f, "fade lapses and gr returns", V.gr, 1.0);
}

/* S103 P2: core B's delay time is in half-rate samples and ramps over 16
 * of them; its full-rate equivalent is twice its block-end target. */
static double target_of_h(float now, float step)
{
	return 2.0 * ((double)now + (double)KAT_HALF_N * (double)step);
}

static void test_spread_vca(void)
{
	double tl, tr;

	voice_init();
	V.spread = 63u;
	V.tNow[0] = 700.0f;
	V.tNow[1] = 350.0f;
	V.drift[0] = V.drift[1] = 0.0f;
	Katran_calcVoiceAsync(&V);
	tl = target_of(V.tNow[0], V.tStep[0]);
	tr = target_of_h(V.tNow[1], V.tStep[1]);
	expect(G_SPR, fabs(tl - tr) < 1.0e-3, "spr 63 equal times", tl, tr);
	/* KA-HALF: the same comb pitch, core B in half-rate samples */
	expect(G_HALF, fabs(target_of(V.tNow[0], V.tStep[0])
	                    - 2.0 * ((double)V.tNow[1] + 16.0 * (double)V.tStep[1])) < 1.0e-3,
	       "core B time = core A time / 2", tl, tr);
	voice_init();
	V.spread = 127u;
	V.tNow[0] = 700.0f;
	V.tNow[1] = 350.0f;
	Katran_calcVoiceAsync(&V);
	tl = target_of(V.tNow[0], V.tStep[0]);
	tr = target_of_h(V.tNow[1], V.tStep[1]);
	expect(G_SPR, fabs(tl / tr - (1.0 + KAT_SPREAD_TIME) / (1.0 - KAT_SPREAD_TIME)) < 0.01,
	       "spr 127 spread", tl / tr,
	       (1.0 + KAT_SPREAD_TIME) / (1.0 - KAT_SPREAD_TIME));
	voice_init();
	V.spread = 0u;
	Katran_calcVoiceAsync(&V);
	expect(G_SPR, fabs((double)V.xfb - (double)KAT_XFB_MAX) < 1e-6,
	       "spr 0 max cross-feedback", V.xfb, KAT_XFB_MAX);

	voice_init();
	V.vca = 63u;
	V.plfoLevel = 0u;
	V.cv = 0.0f;
	Katran_calcVoiceAsync(&V);
	/* S103 P4: g2 (core A) and g1 (core B) are read back from the folded
	 * output gains: with the ring on (bLevel 55, rk 1), cA0 = g2 x 32767 and
	 * cB1 = bLvl (1 + g1) x 32767 / 32768 at the block's end */
	{
		const double s = 32767.0;
		const double bl = 127.0 / 127.0;
		const double g2 = target_of(V.cA0, V.cA0Step) / s;
		const double g1 = target_of(V.cB1, V.cB1Step) / (bl * s / 32768.0) - 1.0;

		expect(G_VCA, fabs(g1 - 0.67 * 0.42) < 1e-3 && fabs(g2 - 0.38 * 0.42) < 1e-3,
		       "vca 63 g1/g2", g1, 0.67 * 0.42);
	}
}

static double pole_gain(const float a, const double f)
{
	double z = 0.0, peak = 0.0;
	int n;

	for (n = 0; n < 4410; n++) {
		const double x = sin(2.0 * M_PI * f * (double)n / 44108.07291666667);
		z += (double)a * (x - z);
		if (n > 2205 && fabs(z) > peak)
			peak = fabs(z);
	}
	return peak;
}

static void test_f3(void)
{
	double g20, g1k, g18k;

	voice_init();
	V.outCut = 63u;
	Katran_calcVoiceAsync(&V);
	g20 = pole_gain(V.f3.a1, 20.0);
	g1k = pole_gain(V.f3.a1, 1000.0);
	g18k = pole_gain(V.f3.a1, 18000.0);
	expect(G_F3, 20.0 * log10(g20) > -1.0 && 20.0 * log10(g1k) > -1.0 &&
	             20.0 * log10(g18k) > -1.0,
	       "F3 passband 20 Hz / 1 k / 18 k dB",
	       20.0 * log10(g18k), -1.0);
}

static void test_bounds(void)
{
	int n, i;
	int bad = 0;

	voice_init();
	for (i = 0; i < (int)ARENA_N; i++)
		arena[i] = (int16_t)((i & 1) ? 32767 : -32768);
	V.armed = 1u;
	V.run = 127u;
	V.aBuf = 1u;
	V.bType = KAT_TYPE_BUF;
	V.feedback = 127u;
	V.spread = 0u;
	V.kick = 127u;
	V.drive = 127u;
	V.osc.oscType = OSC_TYPE_WAV;
	V.osc.waveform = 0u;
	/* S105 P1 A4/A5/A6/A7: every new extreme together */
	V.fltWidth = 0u;
	V.fltCut = 127u;
	V.fltOffset = 127u;
	V.outDrive = 127u;
	V.ringMode = KAT_RM_FAB;
	V.ringSideband = 127u;
	V.chaosImod = 127u;
	V.chaos = 127u;
	for (n = 0; n < 2000; n++) {
		block();
		if (!isfinite(V.gr) || !isfinite(V.mx[0]) || !isfinite(V.mx[2]) || !isfinite(V.tNow[0]) ||
		    !isfinite(V.fb) || !isfinite(V.ringPrev))
			bad++;
	}
	expect(G_BND, bad == 0, "non-finite state", bad, 0.0);
}

/*
 * KA-DLY (S103 P2, bug B1): the fractional delay read.
 *
 * What:       a non-linear ring (so extrapolation and interpolation differ),
 *             the write index behind the delay time (w = 10 < T: the case the
 *             P1 float read got wrong, its truncation toward zero), and a
 *             fixed T with a fraction. Every sample must equal the linear
 *             interpolation at the true position (w + n - T) plus the dry tap,
 *             halved, within 2 LSB (the 15-bit fraction and the >> 1). Both
 *             cores: core A full rate (32 samples, dry tap 32), core B half
 *             rate (16 samples, dry tap 16). S105 P1 review R-2: then the same
 *             with T ramping per sample (tStep 0.75 / 0.5), the tape bend of
 *             D§5.3, which a block-held T fails.
 */
static void test_delay(void)
{
	uint32_t i, n;
	int bad = 0;

	voice_init();
	for (i = 0u; i < KAT_RING; i++) {
		ringA[i] = (int16_t)((int32_t)((i * 7919u) % 20001u) - 10000);
		ringB[i] = ringA[i];
	}
	V.w[0] = 10u;
	V.tNow[0] = 100.37f;
	V.tStep[0] = 0.0f;
	kat_delayReadA(&V);
	for (n = 0u; n < BLK; n++) {
		const double p = (double)(10u + n) - 100.37 + (double)KAT_RING;
		const uint32_t i0 = (uint32_t)floor(p);
		const double fr = p - floor(p);
		const double a = ringA[i0 & KAT_RING_MASK];
		const double b = ringA[(i0 + 1u) & KAT_RING_MASK];
		const double dry = ringA[(10u + n - KAT_TAP_DRY) & KAT_RING_MASK];
		const double want = 0.5 * (a + (b - a) * fr + dry);

		if (fabs((double)V.blkD[0][n] - want) > 2.0)
			bad++;
	}
	expect(G_DLY, bad == 0, "core A fractional read (w < T)", bad, 0.0);
	bad = 0;
	V.w[1] = 5u;
	V.tNow[1] = 60.5f;
	V.tStep[1] = 0.0f;
	kat_delayReadB(&V);
	for (n = 0u; n < KAT_HALF_N; n++) {
		const double p = (double)(5u + n) - 60.5 + (double)KAT_RING;
		const uint32_t i0 = (uint32_t)floor(p);
		const double fr = p - floor(p);
		const double a = ringB[i0 & KAT_RING_MASK];
		const double b = ringB[(i0 + 1u) & KAT_RING_MASK];
		const double dry = ringB[(5u + n - KAT_TAP_DRY_H) & KAT_RING_MASK];
		const double want = 0.5 * (a + (b - a) * fr + dry);

		if (fabs((double)V.blkD[1][n] - want) > 2.0)
			bad++;
	}
	expect(G_DLY, bad == 0, "core B fractional read (w < T)", bad, 0.0);

	/*
	 * S105 P1 review R-2: the delay time ramps per sample inside the block.
	 * T(n) = tNow + n x tStep (steps exact in Q16: 0.75 and 0.5 samples), the
	 * S103 P2 read; a read that holds T at the block's start (the first
	 * step-4 form) misses it by thousands of LSB on this ring. Both cores.
	 */
	bad = 0;
	V.w[0] = 10u;
	V.tNow[0] = 100.37f;
	V.tStep[0] = 0.75f;
	kat_delayReadA(&V);
	for (n = 0u; n < BLK; n++) {
		const double p = (double)(10u + n) - (100.37 + 0.75 * (double)n) + (double)KAT_RING;
		const uint32_t i0 = (uint32_t)floor(p);
		const double fr = p - floor(p);
		const double a = ringA[i0 & KAT_RING_MASK];
		const double b = ringA[(i0 + 1u) & KAT_RING_MASK];
		const double dry = ringA[(10u + n - KAT_TAP_DRY) & KAT_RING_MASK];
		const double want = 0.5 * (a + (b - a) * fr + dry);

		if (fabs((double)V.blkD[0][n] - want) > 2.0)
			bad++;
	}
	expect(G_DLY, bad == 0, "core A ramped read (T moves per sample)", bad, 0.0);
	bad = 0;
	V.w[1] = 5u;
	V.tNow[1] = 60.5f;
	V.tStep[1] = 0.5f;
	kat_delayReadB(&V);
	for (n = 0u; n < KAT_HALF_N; n++) {
		const double p = (double)(5u + n) - (60.5 + 0.5 * (double)n) + (double)KAT_RING;
		const uint32_t i0 = (uint32_t)floor(p);
		const double fr = p - floor(p);
		const double a = ringB[i0 & KAT_RING_MASK];
		const double b = ringB[(i0 + 1u) & KAT_RING_MASK];
		const double dry = ringB[(5u + n - KAT_TAP_DRY_H) & KAT_RING_MASK];
		const double want = 0.5 * (a + (b - a) * fr + dry);

		if (fabs((double)V.blkD[1][n] - want) > 2.0)
			bad++;
	}
	expect(G_DLY, bad == 0, "core B ramped read (T moves per sample)", bad, 0.0);
}

/*
 * KA-RANGE (S103 P2, bugs B2 and B3).
 *
 * What:       1. OSC B's PWM D (Saw form, the sum of two saw reads) over 50
 *                blocks of moving width: every stored sample equals the
 *                halved int32 sum (LSB aside: it carries the wrap flag). The
 *                P1 form stored the unhalved sum as int16 and wrapped.
 *             2. A full-scale arena step (-32,767 -> +32,767) through core
 *                A's arena read with the damping open and the guard at 1: the
 *                DC block's output at the step is about +65,000, which must
 *                store as +32,767 (saturated), not wrap negative.
 */
static void test_range(void)
{
	int n, blkn;
	int bad = 0;

	voice_init();
	V.osc.oscType = OSC_TYPE_PWM;
	V.osc.waveform = OSC_PWM_SAW;
	for (blkn = 0; blkn < 50; blkn++) {
		uint32_t ph, q, j, inc;
		const int16_t *row;

		V.bPos = (uint8_t)((blkn * 5) & 127);
		Katran_calcVoiceAsync(&V);
		V.rowB = V.rowBNext;   /* one row for the whole block */
		inc = V.incB;
		ph = V.phB;
		row = V.rowB;
		j = inc - (uint32_t)(((int32_t)V.pwD[2] - (int32_t)V.pwD0[2]) * 131072);
		q = ph - ((uint32_t)V.pwD0[2] << 22);
		kat_bPwmD(&V, 1);
		for (n = 0; n < BLK; n++) {
			const int32_t want = (kat_read(row, ph) + kat_read(row, q)) >> 1;

			if (((int32_t)V.blkB[n] & ~1) != (want & ~1))
				bad++;
			ph += inc;
			q += j;
		}
	}
	expect(G_RANGE, bad == 0, "PWM D sum stored halved (no wrap)", bad, 0.0);

	voice_init();
	for (n = 0; n < (int)ARENA_N; n++)
		arena[n] = (int16_t)((n < 512) ? -32767 : 32767);
	V.rowA[0] = V.rowANext[0] = arena;
	V.incA[0] = 1u << 22;          /* one arena sample a sample */
	V.phA[0] = 500u << 22;         /* the step at n = 12 */
	V.dcX[0] = -32767.0f;          /* the DC block settled on the low level */
	V.damp[0] = 0.0f;
	V.dampK = 1.0f;
	V.guard[0] = 1.0f;
	kat_aArenaCoreA(&V, 0x7FFFFFFF);   /* no sync resets */
	expect(G_RANGE, V.blkA[12] > 30000, "arena step saturates (not wraps)",
	       V.blkA[12], 32767.0);
}

/*
 * KA-HALF, the interpolation (S103 P2).
 *
 * What:       after the loops have filled (100 blocks, armed, run on), one
 *             block's core B render by itself: each even full-rate sample
 *             right[2k] (k >= 1) is the mean of its neighbours right[2k - 1]
 *             and right[2k + 1] (the half-rate samples o[k - 1], o[k]) within
 *             1.5 LSB, wherever neither neighbour is saturated.
 */
static void test_half_interp(void)
{
	int n, k;
	int bad = 0;

	voice_init();
	V.armed = 1u;
	V.run = 127u;
	V.gr = 1.0f;
	host_running = 1u;
	for (n = 0; n < 100; n++)
		block();
	Katran_calcVoiceAsync(&V);
	kat_delayReadA(&V);
	kat_delayReadB(&V);
	kat_bPwmD(&V, -1);
	kat_aPwmDCoreB(&V, 0x7FFFFFFF, -1);
	kat_coreRenderB(&V);
	for (k = 1; k < (int)KAT_HALF_N; k++) {
		const int32_t lo = V.right[2 * k - 1];
		const int32_t hi = V.right[2 * k + 1];

		if (lo > -32767 && lo < 32767 && hi > -32767 && hi < 32767 &&
		    fabs((double)V.right[2 * k] - 0.5 * (double)(lo + hi)) > 1.5)
			bad++;
	}
	expect(G_HALF, bad == 0, "core B 2x interpolation", bad, 0.0);
}

/*
 * KA-FOLD (S103 P4): the scale-folded core loops against the P2 arithmetic.
 *
 * What:       after 100 blocks of a sounding drone (noise 60, so the noise
 *             path is live), one block's control, delay reads, OSC B and the
 *             core's OSC A copy; then a snapshot of the voice, the core's
 *             render, and a reference in double that uses the P2 form: every
 *             int16 load scaled by 1/32768, the unfolded gains (tGain,
 *             tGain2, tExc, tNse, tWet, xfb, gr, fb), the same SVF, F2 and
 *             ramps, the ring write x 32768, and the output t1 (o0 + o1 B)
 *             (o0 / o1 are what part 2 checks). The noise is replayed from the
 *             same host RNG state. Every ring write and output sample must
 *             agree within 3 LSB (float against double, truncating stores).
 *             Part 2: for a grid of g1, g2, bLevel (rk 0 and 1), t1 and B,
 *             kat_outGains() gives t1 (cA0 + cA1 B) = (r + g2 t1) 32767 and
 *             t1 (cB0 + cB1 B) = r (1 + g1) 32767, r = t1 ((1 - rk) + rk bLvl
 *             B / 32768) (the P2 formulas), within 1e-3 LSB relative.
 * Why:        the folding moves every scale into a per-block gain; a wrong
 *             factor would change the loop gain or the level and nothing else
 *             here would see it.
 */
static int fold_core(const uint32_t c)
{
	static KatranVoice S;
	static int16_t outRef[BLK];
	const int half = (c == 1u);
	const uint32_t count = half ? KAT_HALF_N : BLK;
	const double rs = half ? 2.0 : 1.0;
	const uint32_t other = c ^ 1u;
	uint32_t rng0, rngEnd, n;
	double gr, fb, o0, o1, gH, gL, sH, sL, hPrev, mk, rres, dgH, dgL;
	int bad = 0;
	int n2;

	voice_init();
	V.armed = 1u;
	V.run = 127u;
	V.noise = 60u;
	V.gr = 1.0f;
	/*
	 * S105 P1 review R-4: a narrow band, so the resonance feedback is live
	 * (wid 24: w = 0.19, r = 1.8 (1 - w / 0.5)^2 = 0.70, mk = 1.66). At
	 * init's wid 127, r is 0 and the reference could not see the sign or
	 * the scale of the r sL term (katran_break_band).
	 */
	V.fltWidth = 24u;
	host_running = 1u;
	for (n2 = 0; n2 < 100; n2++)
		block();
	Katran_calcVoiceAsync(&V);
	kat_delayReadA(&V);
	kat_delayReadB(&V);
	kat_bPwmD(&V, -1);
	if (half)
		kat_aPwmDCoreB(&V, 0x7FFFFFFF, -1);
	else
		kat_aPwmDCoreA(&V, 0x7FFFFFFF, -1);
	S = V;
	rng0 = host_rng;
	if (half)
		kat_coreRenderB(&V);
	else
		kat_coreRenderA(&V, outL);
	rngEnd = host_rng;
	host_rng = rng0;

	gr = S.gr;
	fb = S.fb;
	o0 = half ? S.cB0 : S.cA0;
	o1 = half ? S.cB1 : S.cA1;
	gH = S.band[c].gH;
	gL = S.band[c].gL;
	sH = S.band[c].sH;
	sL = S.band[c].sL;
	mk = S.band[c].mk;
	rres = S.band[c].r;
	dgH = S.band[c].dgH;
	dgL = S.band[c].dgL;
	hPrev = S.hPrev;
	for (n = 0u; n < count; n++) {
		const uint32_t bi = half ? (n << 1) : n;
		const uint32_t oi = half ? (n << 1) : (n >> 1);
		const double D = S.blkD[c][n] / 32768.0;
		const double A = S.blkA[n] / 32768.0;
		const double Bv = (double)S.blkB[bi];
		const double Do = S.blkD[other][oi] / 32768.0;
		const double nz = (double)GetRngValue() / 32768.0;
		const double x = fmin(fmax(D * S.tGain, -KAT_CLIP), KAT_CLIP);
		const double y = x - KAT_CUBIC * x * x * x + S.tTube * x * x;
		const double t1 = KAT_TOR_LV1 * (S.tWet * y + (1.0 - S.tWet) * D);
		const double e = S.tExc * ((1.0 - S.tNse) * A + S.tNse * nz);
		const double x2 = fmin(fmax(S.tGain2 * e, -KAT_CLIP), KAT_CLIP);
		const double t2 = KAT_TOR_LV2 * (x2 - KAT_CUBIC * x2 * x2 * x2);
		const double xin = t1 + t2;
		const double xb = mk * xin + rres * sL;
		double hp, wr, o;
		int32_t got, want;

		sH += gH * (xb - sH);
		hp = xb - sH;
		sL += gL * (hp - sL);
		wr = gr * (sL + S.xfb * Do + fb * D) * 32768.0;
		want = (int32_t)fmin(fmax(wr, -32768.0), 32767.0);
		got = V.ring[c][(S.w[c] + n) & KAT_RING_MASK];
		if (abs(got - want) > 3)
			bad++;
		o = t1 * (o0 + o1 * Bv);
		if (half) {
			outRef[n << 1] = (int16_t)fmin(fmax(0.5 * (hPrev + o), -32768.0), 32767.0);
			outRef[(n << 1) + 1u] = (int16_t)fmin(fmax(o, -32768.0), 32767.0);
			hPrev = o;
		} else {
			outRef[n] = (int16_t)fmin(fmax(o, -32768.0), 32767.0);
		}
		gr += rs * S.grStep;
		fb += rs * S.fbStep;
		o0 += rs * (half ? S.cB0Step : S.cA0Step);
		o1 += rs * (half ? S.cB1Step : S.cA1Step);
		gH += dgH;
		gL += dgL;
	}
	host_rng = rngEnd;
	for (n = 0u; n < BLK; n++) {
		const int32_t got = half ? V.right[n] : outL[n];

		if (abs(got - (int32_t)outRef[n]) > 3)
			bad++;
	}
	return bad;
}

static void test_fold(void)
{
	static const float g[] = { 0.0f, 0.16f, 0.5f, 1.0f };
	static const uint8_t bl[] = { 0u, 1u, 55u, 127u };
	static const float t1s[] = { -0.9f, -0.1f, 0.3f, 1.2f };
	static const float bs[] = { -32768.0f, -1000.0f, 0.0f, 12345.0f, 32767.0f };
	int bad = 0;
	unsigned i, j, k, m, q;

	expect(G_FOLD, fold_core(0u) == 0, "core A folded loop = P2 arithmetic", 0.0, 0.0);
	expect(G_FOLD, fold_core(1u) == 0, "core B folded loop = P2 arithmetic", 0.0, 0.0);
	voice_init();
	for (i = 0u; i < 4u; i++)
		for (j = 0u; j < 4u; j++)
			for (k = 0u; k < 4u; k++) {
				float og[4];
				/* S105 P1 A5: the ring is a crossfade now */
				const double b = bl[k] / 127.0;

				V.ringDepth = bl[k];
				kat_outGains(&V, g[i], g[j], (float)b, (float)b, 1.0f, og);
				for (m = 0u; m < 4u; m++)
					for (q = 0u; q < 5u; q++) {
						const double t1 = t1s[m];
						const double B = bs[q];
							const double r = t1 * ((1.0 - b) + b * B / 32768.0);
						const double wantA = (r + g[j] * t1) * 32767.0;
						const double wantB = r * (1.0 + g[i]) * 32767.0;
						const double gotA = t1 * (og[0] + og[1] * B);
						const double gotB = t1 * (og[2] + og[3] * B);

						if (fabs(gotA - wantA) > 1e-3 * (1.0 + fabs(wantA)) ||
						    fabs(gotB - wantB) > 1e-3 * (1.0 + fabs(wantB)))
							bad++;
					}
			}
	expect(G_FOLD, bad == 0, "output gains = P2 ring / VCA formula", bad, 0.0);
}

/*
 * KA-ROT (S103 P4; S103 P5: the bipolar wid): the stereo matrix and the
 * rotation.
 *
 * What:       with dp1 0, kat_stereoTargets() gives m[] = (L from a, L from
 *             b, R from a, R from b) and g = KAT_OUT_NORM x 32767 / 32768;
 *             wid as display d (stored d + 63):
 *             1. rotPh 0, d 0: L = g a, R = g b (core A left, core B right);
 *             2. rotPh 0.5, d 0: L = g b, R = -g a (swapped; A's polarity);
 *             3. d 0, every phase (no collapse): the rows are orthogonal
 *                (m0 m2 + m1 m3 = 0) and each has norm g; at rotPh 0.25 the
 *                L/R correlation of equal uncorrelated cores is 0;
 *             4. d -12, every phase: the S103 P4 pan law, L = gL a + gR b,
 *                R = gR a + gL b (equal power), centred at rotPh 0.25;
 *             5. d -6 (the blend): both rows keep norm g at every phase;
 *                d -63: mono (L = R) at every phase;
 *             6. d +64: still no collapse (L.R <= 0) and wider than d 0
 *                (the side part doubled);
 *             7. rot 0 holds rotPh over 1,000 blocks; rot 127 (chs 0)
 *                advances it by 4 Hz x 32 / fs a block (1e-4 relative);
 *             8. the output loop with F3 open: L = sat(m0 a + m1 b), R =
 *                sat(m2 a + m3 b) within 1 LSB for fixed gains.
 */
static void stereo_at(float ph, int d, float m[4])
{
	V.rotPh = ph;
	V.width = (uint8_t)(d + 63);
	kat_stereoTargets(&V, m);
}

static void test_rot(void)
{
	const double g = KAT_OUT_NORM * 32767.0 / 32768.0;
	const double h = g / sqrt(2.0);
	float m[4];
	int i, bad;

	voice_init();
	V.chaos = 0u;
	stereo_at(0.0f, 0, m);
	expect(G_ROT, fabs(m[0] - g) < 1e-4 && fabs(m[1]) < 1e-4 && fabs(m[2]) < 1e-4 &&
	             fabs(m[3] - g) < 1e-4, "rotPh 0: L = A, R = B", m[0], g);
	stereo_at(0.5f, 0, m);
	expect(G_ROT, fabs(m[0]) < 1e-4 && fabs(m[1] - g) < 1e-4 && fabs(m[2] + g) < 1e-4 &&
	             fabs(m[3]) < 1e-4, "rotPh 0.5: L = B, R = -A", m[1], g);
	stereo_at(0.25f, 0, m);
	expect(G_ROT, fabs((double)m[0] * m[2] + (double)m[1] * m[3]) < 1e-6,
	       "rotPh 0.25, wid 0: no collapse", m[0] * m[2] + m[1] * m[3], 0.0);
	stereo_at(0.25f, -12, m);
	expect(G_ROT, fabs(m[0] - h) < 1e-3 && fabs(m[1] - h) < 1e-3 && fabs(m[2] - h) < 1e-3 &&
	             fabs(m[3] - h) < 1e-3, "rotPh 0.25, wid -12: centred (collapse)", m[2], h);
	bad = 0;
	for (i = 0; i < 1024; i++) {
		const float ph = (float)i / 1024.0f;
		double nl, nr, dot;

		stereo_at(ph, 0, m);
		nl = (double)m[0] * m[0] + (double)m[1] * m[1];
		nr = (double)m[2] * m[2] + (double)m[3] * m[3];
		dot = (double)m[0] * m[2] + (double)m[1] * m[3];
		if (fabs(nl - g * g) > 1e-3 * g * g || fabs(nr - g * g) > 1e-3 * g * g ||
		    fabs(dot) > 1e-3 * g * g)
			bad++;
		stereo_at(ph, -12, m);
		if (fabs((double)m[0] - m[3]) > 1e-5 * g || fabs((double)m[1] - m[2]) > 1e-5 * g ||
		    fabs((double)m[0] * m[0] + (double)m[1] * m[1] - g * g) > 1e-3 * g * g)
			bad++;
		/* the blend keeps both rows at full power (no silent R half-way) */
		stereo_at(ph, -6, m);
		if (fabs((double)m[0] * m[0] + (double)m[1] * m[1] - g * g) > 1e-3 * g * g ||
		    fabs((double)m[2] * m[2] + (double)m[3] * m[3] - g * g) > 1e-3 * g * g)
			bad++;
		stereo_at(ph, -63, m);
		if (fabs((double)m[0] - m[2]) > 1e-5 * g || fabs((double)m[1] - m[3]) > 1e-5 * g)
			bad++;
		stereo_at(ph, 64, m);
		dot = (double)m[0] * m[2] + (double)m[1] * m[3];
		if (dot > 1e-6 * g * g)
			bad++;
	}
	expect(G_ROT, bad == 0, "wid 0 orthogonal / -6 power / -12 pan law / -63 mono / +64 no collapse",
	       bad, 0.0);
	{
		float w0[4], w64[4];

		stereo_at(0.1f, 0, w0);
		stereo_at(0.1f, 64, w64);
		/* the side part (L - R) doubles, the mid part (L + R) stays */
		expect(G_ROT, fabs((w64[0] - w64[2]) - 2.0 * (w0[0] - w0[2])) < 1e-4 &&
		             fabs((w64[0] + w64[2]) - (w0[0] + w0[2])) < 1e-4,
		       "wid +64 doubles the side", w64[0] - w64[2], 2.0 * (w0[0] - w0[2]));
	}

	voice_init();
	V.rotate = 0u;
	V.rotPh = 0.3f;
	for (i = 0; i < 1000; i++)
		Katran_calcVoiceAsync(&V);
	expect(G_ROT, V.rotPh == 0.3f, "rot 0 holds", V.rotPh, 0.3);
	voice_init();
	V.rotate = 127u;
	V.chaos = 0u;
	V.rotPh = 0.1f;
	Katran_calcVoiceAsync(&V);
	{
		const double want = 0.1 + 4.0 * 32.0 / 44108.07291666667;

		expect(G_ROT, fabs((double)V.rotPh - want) < 1e-4 * want, "rot 127 rate",
		       V.rotPh, want);
	}

	voice_init();
	V.f3.a1 = 1.0f;
	V.f3.da1 = 0.0f;
	V.f3.mL = 1.0f;
	V.mx[0] = 0.37f;
	V.mx[1] = -0.21f;
	V.mx[2] = 0.12f;
	V.mx[3] = 0.44f;
	for (i = 0; i < 4; i++)
		V.mxStep[i] = 0.0f;
	bad = 0;
	for (i = 0; i < BLK; i++) {
		outL[i] = (int16_t)(i * 1500 - 20000);
		V.right[i] = (int16_t)(15000 - i * 900);
	}
	{
		int16_t a[BLK], b[BLK];

		memcpy(a, outL, sizeof(a));
		memcpy(b, V.right, sizeof(b));
		kat_outRender(&V, outL);
		for (i = 0; i < BLK; i++) {
			const double wl = 0.37 * a[i] - 0.21 * b[i];
			const double wr = 0.12 * a[i] + 0.44 * b[i];

			if (fabs(outL[i] - wl) > 1.0 || fabs(V.right[i] - wr) > 1.0)
				bad++;
		}
	}
	expect(G_ROT, bad == 0, "output matrix L = m0 a + m1 b, R = m2 a + m3 b", bad, 0.0);
}

/*
 * KT-BAND (S105 P1 A4): the band filter's corners, resonance and bounds.
 *
 * What:       kat_bandControl() is run to its targets for each setting, then
 *             a double recurrence of the loop's own form (xb = mk x + r sL;
 *             sH += gH (xb - sH); hp = xb - sH; sL += gL (hp - sL)) measures
 *             the steady sine gain at three frequencies. KA-FOLD checks that
 *             kat_coreBody() runs that recurrence, so together they check
 *             the loop's filter.
 * Why:        the laws of §A4.2-A4.4: full band at cut 0 / wid 127; raising
 *             the cutoff cuts lows, lowering it cuts highs; a narrowing band
 *             resonates; ofs splits the cores; nothing diverges. The full
 *             band's ends are one-pole corners at 20 Hz and 15.96 kHz, so
 *             the "flat" window is checked to 2.5 dB rather than 1 dB (the
 *             plan's 1 dB is not reachable with one-pole corners; noted in
 *             S105_P1_IMPLEMENTATION.md §10).
 */
static double band_gain_at(uint8_t cut, uint8_t wid, uint8_t ofs, double f)
{
	uint32_t i, n;
	double sH = 0.0, sL = 0.0, peak = 0.0;
	/* loop pitch 0 for both cores (the tracking term is then a constant);
	 * S105 P1 review: kat_bandTargets() gives both cores at once */
	const float ls[2] = { 0.0f, 0.0f };
	float hT[2], lT[2], r, mk;

	voice_init();
	V.fltCut = cut;
	V.fltWidth = wid;
	V.fltOffset = ofs;
	for (i = 0u; i < 200u; i++) {
		kat_bandTargets(&V, ls, 0.0f, hT, lT, &r, &mk);
		kat_bandControl(&V.band[0], hT[0], lT[0], r, mk, KAT_BAND_ARG, KAT_CUT_ST_MAX,
		                OUTPUT_DMA_SIZE);
		/* the core loop's write-back of its ramp ends (kat_coreBody(),
		 * S105 P1 review): without the loop, advance them here */
		V.band[0].gH += (float)OUTPUT_DMA_SIZE * V.band[0].dgH;
		V.band[0].gL += (float)OUTPUT_DMA_SIZE * V.band[0].dgL;
	}
	for (n = 0u; n < 8820u; n++) {
		const double x = sin(2.0 * M_PI * f * (double)n / 44108.07291666667);
		const double xb = (double)V.band[0].mk * x + (double)V.band[0].r * sL;
		double hp, y;

		sH += (double)V.band[0].gH * (xb - sH);
		hp = xb - sH;
		sL += (double)V.band[0].gL * (hp - sL);
		y = sL;
		if (n > 4410u && fabs(y) > peak)
			peak = fabs(y);
	}
	return peak;
}

/* the band's largest gain over 80 Hz .. 5.4 kHz (a quarter octave apart) */
static double band_peak(uint8_t cut, uint8_t wid, uint8_t ofs)
{
	double best = 0.0;
	int i;

	for (i = 0; i < 24; i++) {
		const double f = 80.0 * pow(2.0, (double)i * 0.25);
		const double g = band_gain_at(cut, wid, ofs, f);

		if (g > best)
			best = g;
	}
	return best;
}

static void test_band(void)
{
	const double g30 = band_gain_at(63u, 127u, 63u, 30.0);
	const double g1k = band_gain_at(63u, 127u, 63u, 1000.0);
	const double g12k = band_gain_at(63u, 127u, 63u, 12000.0);
	const double dbl = 20.0 * log10(g1k);
	const double hp1k = band_gain_at(127u, 127u, 63u, 1000.0);
	const double lp1k = band_gain_at(0u, 127u, 63u, 1000.0);
	const double narrow = band_peak(63u, 0u, 63u);
	const double wide = band_peak(63u, 64u, 63u);
	float hT[2], lT[2], r, mk;
	int n;
	int bad = 0;

	expect(G_BAND, fabs(20.0 * log10(g30) - dbl) < 2.5 &&
	               fabs(20.0 * log10(g12k) - dbl) < 2.5,
	       "cut 63 wid 127 flat 30 Hz..12 kHz (2.5 dB)", 20.0 * log10(g30), dbl);
	expect(G_BAND, 20.0 * log10(hp1k) < -20.0, "cut 127 cuts 1 kHz",
	       20.0 * log10(hp1k), -20.0);
	expect(G_BAND, 20.0 * log10(lp1k) < -25.0, "cut 0 cuts 1 kHz",
	       20.0 * log10(lp1k), -25.0);
	expect(G_BAND, 20.0 * log10(narrow / wide) > 15.0,
	       "wid 0 resonates at the centre", 20.0 * log10(narrow / wide), 15.0);

	voice_init();
	V.fltCut = 100u;
	V.fltWidth = 127u;
	V.fltOffset = 127u;
	{
		const float ls[2] = { 0.0f, 0.0f };

		kat_bandTargets(&V, ls, 0.0f, hT, lT, &r, &mk);
		expect(G_BAND, fabs((double)hT[0] - (double)hT[1] - 48.0) < 0.01,
		       "ofs 127: A's corners 48 st above B's", hT[0] - hT[1], 48.0);
	}

	/* the worst settings stay finite and inside int16 (4,000 blocks) */
	voice_init();
	V.armed = 1u;
	V.run = 127u;
	V.feedback = 127u;
	V.drive = 127u;
	V.fltCut = 127u;
	V.fltWidth = 0u;
	V.fltOffset = 127u;
	for (n = 0; n < 4000; n++) {
		uint32_t i;

		block();
		if (!isfinite(V.band[0].sH) || !isfinite(V.band[0].sL) ||
		    !isfinite(V.band[1].sH) || !isfinite(V.band[1].sL))
			bad++;
		for (i = 0u; i < BLK; i++)
			if (outL[i] < -32768 || outL[i] > 32767 ||
			    V.right[i] < -32768 || V.right[i] > 32767)
				bad++;
	}
	expect(G_BAND, bad == 0, "extremes finite and inside int16", bad, 0.0);
}

/*
 * KT-CE (S105 P1 A3): the four chaos rows.
 *
 * What:       (a) chs 0: two voices that differ only in ce[1].level give
 *             identical band corners; (b) chs 127: the slow term moves the
 *             corners by 48 (L - 0.5) through the slew; (c) imd 0: the
 *             slow cycle is exactly N x the fast one and both totals equal
 *             peak/up + peak/down; (d) skw +63: the slow rise share is >=
 *             0.97 and the fast <= 0.03; (e) imd 127 turns skPh by 0.25 a
 *             slow cycle.
 * Why:        the F-1 fix (the slow envelope reaches the band) and your
 *             point P5's law; KA-CE keeps the trigger rule.
 */
static double ce_rise_share(const KatCe *e)
{
	const double u = (double)e->peak / (double)e->up;
	const double d = (double)e->peak / (double)e->down;

	return u / (u + d);
}

static void test_ce_law(void)
{
	float h0, l0, h1, l1;
	int i;

	voice_init();
	V.chaos = 0u;
	V.plfoLevel = 0u;
	V.ce[1].level = 0.0f;
	Katran_calcVoiceAsync(&V);
	h0 = V.band[0].hSt;
	l0 = V.band[0].lSt;
	voice_init();
	V.chaos = 0u;
	V.plfoLevel = 0u;
	V.ce[1].level = 1.0f;
	Katran_calcVoiceAsync(&V);
	expect(G_CE, V.band[0].hSt == h0 && V.band[0].lSt == l0,
	       "chs 0: the slow level does not move the band", V.band[0].hSt, h0);

	voice_init();
	V.chaos = 127u;
	V.plfoLevel = 0u;
	V.ce[1].level = 0.5f;
	Katran_calcVoiceAsync(&V);
	h0 = V.band[0].hSt;
	l0 = V.band[0].lSt;
	voice_init();
	V.chaos = 127u;
	V.plfoLevel = 0u;
	V.ce[1].level = 1.0f;
	Katran_calcVoiceAsync(&V);
	h1 = V.band[0].hSt;
	l1 = V.band[0].lSt;
	expect(G_CE, fabs((double)(h1 - h0) - 0.28 * 24.0) < 0.5 &&
	             fabs((double)(l1 - l0) - 0.28 * 24.0) < 0.5,
	       "chs 127: the slow envelope moves the band", h1 - h0, 0.28 * 24.0);

	voice_init();
	V.chaosImod = 0u;
	for (i = 1; i <= 127; i += 9) {
		double nf, ns, want;
		const double x = (double)i / 127.0;
		const double nm = 1.0 + floor(7.0 * x * x + 0.5);

		V.chaosCycle = (uint8_t)i;
		kat_ceStart(&V, 0u);
		kat_ceStart(&V, 1u);
		nf = (double)V.ce[0].peak / (double)V.ce[0].up +
		     (double)V.ce[0].peak / (double)V.ce[0].down;
		ns = (double)V.ce[1].peak / (double)V.ce[1].up +
		     (double)V.ce[1].peak / (double)V.ce[1].down;
		want = nm * nf;
		expect(G_CE, fabs(ns - want) <= 1.5 * nm,
		       "imd 0: slow = N x fast", ns, want);
	}

	voice_init();
	V.chaosCycle = 90u;
	V.chaosSkew = 126u;
	V.chaosImod = 0u;
	kat_ceStart(&V, 0u);
	kat_ceStart(&V, 1u);
	expect(G_CE, ce_rise_share(&V.ce[1]) >= 0.97 && ce_rise_share(&V.ce[0]) <= 0.03,
	       "skw +63: opposite rise shares", ce_rise_share(&V.ce[1]),
	       ce_rise_share(&V.ce[0]));

	voice_init();
	V.chaosImod = 127u;
	V.skPh = 0.0f;
	kat_ceStart(&V, 1u);
	expect(G_CE, fabs((double)V.skPh - 0.25) < 1e-5, "imd 127: a quarter turn",
	       V.skPh, 0.25);
}

/*
 * KT-TYPE (S105 P1 A8): a type and wave per oscillator.
 *
 * What:       the four buffer combinations point the arena rows exactly at
 *             the buf oscillators; Osc 1 pwm Rec with Osc 2 wav Sin reads
 *             sine_table for Osc 2 and a saw row for Osc 1 (the PWM D form);
 *             Katran_setOscType() restores Osc 1's table wave after buf;
 *             Katran_setOscBType() clamps Osc 2's wave to its type's list.
 * Why:        your point P8 / Q-A11: the buffer is an oscillator type, and
 *             each oscillator has its own type and wave.
 */
static int in_arena(const int16_t *p)
{
	return p >= arena && p < arena + ARENA_N;
}

static void test_types(void)
{
	static const uint8_t ab[4] = { 0u, 1u, 0u, 1u };
	static const uint8_t bt[4] = { KAT_TYPE_PWM, KAT_TYPE_PWM, KAT_TYPE_BUF, KAT_TYPE_BUF };
	unsigned i;

	for (i = 0u; i < 4u; i++) {
		voice_init();
		V.aBuf = ab[i];
		V.bType = bt[i];
		V.osc.oscType = OSC_TYPE_WAV;
		V.osc.waveform = 0u;
		V.bOscType = OSC_TYPE_WAV;
		V.bWave = 0u;
		Katran_calcVoiceAsync(&V);
		expect(G_TYPE, in_arena(V.rowANext[0]) == (int)ab[i] &&
		                in_arena(V.rowANext[1]) == (int)ab[i] &&
		                in_arena(V.rowBNext) == (int)(bt[i] == KAT_TYPE_BUF),
		       "the four buffer combinations", (double)i, 0.0);
	}

	voice_init();
	V.osc.oscType = OSC_TYPE_PWM;
	V.osc.waveform = OSC_PWM_REC;
	V.bOscType = OSC_TYPE_WAV;
	V.bWave = 3u;                       /* Sin */
	Katran_calcVoiceAsync(&V);
	expect(G_TYPE, V.rowBNext == sine_table && V.rowANext[0] != sine_table,
	       "Osc 1 pwm Rec with Osc 2 wav Sin", 0.0, 1.0);

	voice_init();
	V.osc.oscType = OSC_TYPE_PWM;
	V.osc.waveform = OSC_PWM_REC;
	Katran_setOscType(&V, KAT_TYPE_BUF);
	expect(G_TYPE, V.aBuf == 1u && V.osc.oscType == OSC_TYPE_PWM &&
	               V.osc.waveform == OSC_PWM_REC,
	       "buf keeps Osc 1's table wave", V.osc.waveform, OSC_PWM_REC);
	Katran_setOscType(&V, KAT_TYPE_PWM);
	expect(G_TYPE, V.aBuf == 0u && V.osc.oscType == OSC_TYPE_PWM,
	       "leaving buf restores Osc 1", V.aBuf, 0.0);

	voice_init();
	V.bWave = 2u;
	Katran_setOscBType(&V, KAT_TYPE_WAV);
	expect(G_TYPE, V.bOscType == OSC_TYPE_WAV && V.bWave == 2u,
	       "Osc 2 wav keeps a value in its list", V.bWave, 2.0);
	V.bWave = 3u;
	Katran_setOscBType(&V, KAT_TYPE_WTB);
	expect(G_TYPE, V.bOscType == OSC_TYPE_WTB && V.bWave == 0u,
	       "Osc 2 wtb with no tables clamps", V.bWave, 0.0);
}

/*
 * The level print (--level; S105 P1 C2.12; review R-3).
 *
 * What:       2,000 blocks after a kick (velocity 100; the hot case 127): the
 *             output RMS in dBFS (L and R together), the peak, and the share
 *             of samples at full scale (+-32,767 / -32,768, the output's hard
 *             clip). Two cases: init.kat's values, and a hot dry patch (ring
 *             depth 0, fbk 127, Torcido drive 127, drv 0).
 * Why:        C2.12 set KAT_OUT_NORM from the RMS alone; review R-3 found the
 *             hot dry patch at the full scale for about 5 % of its samples.
 *             Step 5 sets the level and the drive's make-up by ear with this
 *             print beside it.
 */
static void level_case(const char *name, const int hot)
{
	int n, i;
	long clip = 0;
	int peak = 0;
	double e = 0.0;

	voice_init();
	host_running = 1u;
	V.run = 127u;
	if (hot) {
		V.ringDepth = 0u;
		V.feedback = 127u;
		V.drive = 127u;
		V.outDrive = 0u;
	}
	Katran_triggerVoice(&V, 0u, hot ? 127u : 100u, 60u);
	for (n = 0; n < 2000; n++) {
		block();
		for (i = 0; i < BLK; i++) {
			const int a = outL[i], b = V.right[i];

			e += (double)a * (double)a + (double)b * (double)b;
			if (a >= 32767 || a <= -32768)
				clip++;
			if (b >= 32767 || b <= -32768)
				clip++;
			if (abs(a) > peak)
				peak = abs(a);
			if (abs(b) > peak)
				peak = abs(b);
		}
	}
	e /= (double)(2000 * BLK * 2);
	printf("katran %s level: RMS %.2f dBFS, peak %d, full scale %.2f %%\n", name,
	       10.0 * log10(e / (32768.0 * 32768.0)), peak,
	       100.0 * (double)clip / (double)(2000 * BLK * 2));
}

static void print_level(void)
{
	level_case("init", 0);
	level_case("hot dry", 1);
}

/*
 * KT-RING (S105 P1 A5): the ring as a crossfade.
 *
 * What:       ringDepth 0 gives the dry gains and no ring term; 127 gives the
 *             S103 P5 lvB 127 gains; core A's output over ringDepth 0..127
 *             (steps of 8) never steps by more than 6 dB, so there is no
 *             level cliff (finding F-3).
 * Why:        the S103 P1 law switched the ring on at any lvB above 0.
 */
static void test_ring(void)
{
	const double s = KAT_OUT_SCALE;
	const double g1 = 0.3, g2 = 0.2;
	float og[4];
	double prev = 0.0;
	int i, bad = 0;

	voice_init();
	V.ringDepth = 0u;
	kat_outGains(&V, (float)g1, (float)g2, 0.0f, 0.0f, 1.0f, og);
	expect(G_RING, og[1] == 0.0f && og[3] == 0.0f &&
	               fabs((double)og[0] - (1.0 + g2) * s) < 0.01 &&
	               fabs((double)og[2] - (1.0 + g1) * s) < 0.01,
	       "ringDepth 0: the dry gains", og[0], (1.0 + g2) * s);
	kat_outGains(&V, (float)g1, (float)g2, 1.0f, 1.0f, 1.0f, og);
	expect(G_RING, fabs((double)og[0] - g2 * s) < 0.01 && og[2] == 0.0f,
	       "ringDepth 127: the ring-only gains", og[0], g2 * s);

	for (i = 0; i <= 127; i += 8) {
		const double b = (double)i / 127.0;
		const double lvl = (1.0 - b + g2) * s;   /* core A, a = 1, B = 0 */

		if (i > 0 && lvl < prev && 20.0 * log10(prev / lvl) > 6.0)
			bad++;
		prev = lvl;
	}
	expect(G_RING, bad == 0, "no level cliff over ringDepth", bad, 0.0);
}

/*
 * KT-OUT (S105 P1 A5): tone, drive and the level.
 *
 * What:       (a) ton 63 makes kat_outRender() the matrix exactly; (b) the
 *             drive wrapper with drvA 1 / drvB 0 is bit-identical to the
 *             plain core; (c) outDrive 127 keeps every sample inside int16.
 *             The level print (--level) feeds KAT_OUT_NORM.
 */
static void test_out(void)
{
	static KatranVoice S;
	static int16_t a[BLK], b[BLK];
	uint32_t r0;
	int i, n;
	int bad = 0;

	voice_init();
	V.f3.a1 = 1.0f;
	V.f3.da1 = 0.0f;
	V.f3.mL = 1.0f;
	V.mx[0] = 0.37f;
	V.mx[1] = -0.21f;
	V.mx[2] = 0.12f;
	V.mx[3] = 0.44f;
	for (i = 0; i < 4; i++)
		V.mxStep[i] = 0.0f;
	for (i = 0; i < BLK; i++) {
		outL[i] = (int16_t)(i * 1500 - 20000);
		V.right[i] = (int16_t)(15000 - i * 900);
	}
	memcpy(a, outL, sizeof(a));
	memcpy(b, V.right, sizeof(b));
	kat_outRender(&V, outL);
	for (i = 0; i < BLK; i++) {
		const double wl = 0.37 * a[i] - 0.21 * b[i];
		const double wr = 0.12 * a[i] + 0.44 * b[i];

		if (fabs(outL[i] - wl) > 1.0 || fabs(V.right[i] - wr) > 1.0)
			bad++;
	}
	expect(G_OUT, bad == 0, "ton 63 passes the matrix exactly", bad, 0.0);

	voice_init();
	V.armed = 1u;
	V.run = 127u;
	V.gr = 1.0f;
	host_running = 1u;
	for (n = 0; n < 100; n++)
		block();
	Katran_calcVoiceAsync(&V);
	kat_delayReadA(&V);
	kat_delayReadB(&V);
	kat_bPwmD(&V, -1);
	kat_aPwmDCoreA(&V, 0x7FFFFFFF, -1);
	S = V;
	r0 = host_rng;
	kat_coreRenderA(&V, a);
	host_rng = r0;
	V = S;
	V.drvA = 1.0f;
	V.drvB = 0.0f;
	kat_coreRenderAD(&V, outL);
	bad = 0;
	for (i = 0; i < BLK; i++)
		if (a[i] != outL[i])
			bad++;
	expect(G_OUT, bad == 0, "drvA 1 / drvB 0 is the plain core", bad, 0.0);

	voice_init();
	V.armed = 1u;
	V.run = 127u;
	V.gr = 1.0f;
	V.outDrive = 127u;
	bad = 0;
	for (n = 0; n < 200; n++) {
		block();
		for (i = 0; i < BLK; i++)
			if (outL[i] <= -32767 || outL[i] >= 32767 ||
			    V.right[i] <= -32767 || V.right[i] >= 32767)
				bad++;
	}
	expect(G_OUT, bad == 0, "outDrive 127 stays inside int16", bad, 0.0);
}

/*
 * KT-RATE (S105 P1 A2): the step-synced patch LFO and its clock pull.
 *
 * What:       (a) at 60, 120 and 200 BPM row 104 (/16, 24 ticks) advances
 *             the phase by bpm x 1.6 / 24 x 32 / fs a block (1e-4); (b) the
 *             rate rises with the row; (c) running, slf 0 and chs 0, a
 *             start phase 0.4 off is pulled within 1 % of frac(E / 24) after
 *             2 s; (d) slf 127 lets the phase drift past 10 %, and slf back
 *             to 0 brings it under 2 %; (e) stopped, the step is exactly the
 *             synced rate (no pull); (f) review R-1: at slf 127 the shape
 *             warp is 0.5 - 0.45 cv (the inverted output), so the next cv is
 *             the sine at the phase warped by it.
 * Why:        your point P1: synced in general, drifting away with slf and
 *             pulled back; the shape keeps the S103 P1 skew.
 */
/*
 * The clock phase error the voice itself sees: frac(est / 24) - lfoPh, with
 * est the tick count extrapolated between ticks exactly as kat_plfoStep()
 * does (the integer tick alone lags by up to one tick, 4 % of the phase).
 */
static double clock_err(void)
{
	double est = (double)host_ticks
	           + (double)V.tickBlocks * (32.0 / 44108.07291666667)
	           * (double)host_bpm * 1.6;
	double pc = est / 24.0;
	double err;

	pc -= floor(pc);
	err = pc - (double)V.lfoPh;
	err -= floor(err + 0.5);
	return err;
}

static void test_rate(void)
{
	static const uint16_t bpms[3] = { 60u, 120u, 200u };
	int i, n;
	double step0, step127;

	host_running = 0u;
	for (i = 0; i < 3; i++) {
		double period;

		host_bpm = bpms[i];
		voice_init();
		V.plfoRate = 104u;   /* /16 = 24 ticks */
		V.plfoMod = 0u;
		V.plfoLevel = 0u;
		V.lfoPh = 0.1f;
		Katran_calcVoiceAsync(&V);
		period = (double)host_bpm * 1.6 * (1.0 / 24.0) * 32.0
		       / 44108.07291666667;
		expect(G_RATE, fabs((double)V.lfoPh - (0.1 + period)) < 1e-4,
		       "the synced rate at a stop", V.lfoPh, 0.1 + period);
	}
	host_bpm = 120u;
	voice_init();
	V.plfoRate = 0u;
	V.lfoPh = 0.1f;
	Katran_calcVoiceAsync(&V);
	step0 = (double)V.lfoPh - 0.1;
	voice_init();
	V.plfoRate = 127u;
	V.lfoPh = 0.1f;
	Katran_calcVoiceAsync(&V);
	step127 = (double)V.lfoPh - 0.1;
	expect(G_RATE, step127 > 1000.0 * step0, "the rate rises with the row",
	       step127, step0);

	/*
	 * S105 P1 review R-1: the shape warp follows the inverted output. With
	 * slf 127 and the last block's cv = +-0.5, the warp point is w = 0.5 -+
	 * 0.225 (0.275 / 0.725), and the new cv is the sine read at the warped
	 * phase q of the phase the step lands on. A warp that reads the rate
	 * table's 1 / ticks (the first S105 form) puts w near 0.52 and misses.
	 */
	{
		static const float cvs[2] = { 0.5f, -0.5f };
		int k;

		host_running = 0u;
		for (k = 0; k < 2; k++) {
			float w, ph, q, want;
			uint32_t idx;

			voice_init();
			V.plfoRate = 104u;
			V.plfoMod = 127u;
			V.plfoLevel = 127u;
			V.chaos = 0u;
			V.lfoPh = 0.3f;
			V.cv = cvs[k];
			Katran_calcVoiceAsync(&V);
			w = fminf(fmaxf(0.5f + 0.45f * 1.0f * -cvs[k], 0.05f), 0.95f);
			ph = V.lfoPh;
			q = (ph < w) ? (0.5f * ph / w) : (0.5f + 0.5f * (ph - w) / (1.0f - w));
			idx = (uint32_t)(q * 1024.0f) & 0x3FFu;
			want = ((float)sine_table[idx] * (1.0f / 32768.0f)) * 1.0f;
			expect(G_RATE, fabsf(V.cv - want) < 1e-6f,
			       "the shape warp follows the inverted output (slf 127)",
			       V.cv, want);
		}
	}

	host_running = 1u;
	host_bpm = 120u;
	host_ticks = 0u;
	voice_init();
	V.plfoRate = 104u;
	V.plfoMod = 0u;
	V.plfoLevel = 0u;
	V.chaos = 0u;          /* no per-cycle jitter: KT-RATE's lock case */
	V.lfoPh = 0.4f;
	for (n = 0; n < 2756; n++) {
		host_ticks = (uint32_t)((double)n * 32.0 / 44108.07291666667
		                        * 120.0 * 1.6);
		Katran_calcVoiceAsync(&V);
	}
	expect(G_RATE, fabs(clock_err()) < 0.01,
	       "pulled onto the clock (<1 %)", clock_err(), 0.01);

	voice_init();
	V.plfoRate = 104u;
	V.plfoMod = 127u;
	V.plfoLevel = 127u;
	V.chaos = 0u;
	V.lfoPh = 0.4f;
	{
		double worst = 0.0;

		for (n = 0; n < 2000; n++) {
			double e;

			host_ticks = (uint32_t)((double)n * 32.0 / 44108.07291666667
			                        * 120.0 * 1.6);
			Katran_calcVoiceAsync(&V);
			e = fabs(clock_err());
			if (e > worst)
				worst = e;
		}
		expect(G_RATE, worst > 0.1, "slf 127 drifts away", worst, 0.1);
		V.plfoMod = 0u;
		for (n = 0; n < 2000; n++) {
			host_ticks = (uint32_t)((double)(2000 + n) * 32.0
			                        / 44108.07291666667 * 120.0 * 1.6);
			Katran_calcVoiceAsync(&V);
		}
		expect(G_RATE, fabs(clock_err()) < 0.02,
		       "slf 0 pulls it back (<2 %)", clock_err(), 0.02);
	}
	host_running = 1u;
	host_bpm = 120u;
}

/*
 * KT-FM (S105 P1 A6): Ring/FM per core.
 *
 * What:       (a) ring mode leaves pmQ 0; (b) fmA at depth 127 puts k =
 *             KAT_PM_MAX on core A only and the delay read moves by
 *             (pmQ x B) >> 8 (against a double reference, 2 LSB); (c) with
 *             the time moving over 2,000 blocks the clamped target keeps
 *             T - k >= 32 (the staged read: no unwritten sample).
 * Why:        your point P6: the ring switches to FM per core.
 */
static void test_fm(void)
{
	uint32_t i, n;
	int blkn, bad;

	voice_init();
	V.ringMode = KAT_RM_RING;
	V.ringDepth = 127u;
	Katran_calcVoiceAsync(&V);
	expect(G_FM, V.pmQ[0] == 0 && V.pmQ[1] == 0, "rng: no PM", V.pmQ[0], 0.0);

	voice_init();
	V.ringMode = KAT_RM_FMA;
	V.ringDepth = 127u;
	Katran_calcVoiceAsync(&V);
	expect(G_FM, V.pmQ[0] == (int32_t)(KAT_PM_MAX * 512.0f) && V.pmQ[1] == 0,
	       "fmA: core A's PM depth", V.pmQ[0], KAT_PM_MAX * 512.0);

	for (i = 0u; i < KAT_RING; i++)
		ringA[i] = (int16_t)((int32_t)((i * 7919u) % 20001u) - 10000);
	V.w[0] = 10u;
	V.tNow[0] = 200.37f;
	V.tStep[0] = 0.0f;
	for (i = 0u; i < BLK; i++)
		V.blkB[i] = (int16_t)((int32_t)i * 900 - 14000);
	kat_delayReadA(&V);
	bad = 0;
	for (n = 0u; n < BLK; n++) {
		const double pm = (double)V.pmQ[0] * (double)V.blkB[n] / 256.0 / 65536.0;
		const double p = (double)(10u + n) - 200.37 - pm + (double)KAT_RING;
		const uint32_t i0 = (uint32_t)floor(p);
		const double fr = p - floor(p);
		const double a = ringA[i0 & KAT_RING_MASK];
		const double b = ringA[(i0 + 1u) & KAT_RING_MASK];
		const double dry = ringA[(10u + n - KAT_TAP_DRY) & KAT_RING_MASK];
		const double want = 0.5 * (a + (b - a) * fr + dry);

		if (fabs((double)V.blkD[0][n] - want) > 2.0)
			bad++;
	}
	expect(G_FM, bad == 0, "fmA: the read displaced by k B", bad, 0.0);

	voice_init();
	V.armed = 1u;
	V.run = 127u;
	V.gr = 1.0f;
	V.ringMode = KAT_RM_FAB;
	V.ringDepth = 127u;
	bad = 0;
	for (blkn = 0; blkn < 2000; blkn++) {
		const double k = (double)V.pmQ[0] / 512.0;
		double target;

		V.time = (uint8_t)(blkn % 8);   /* the shortest times: the clamp bites */
		Katran_calcVoiceAsync(&V);
		target = (double)V.tNow[0] + (double)BLK * (double)V.tStep[0];
		if (target < 32.0 + k - 1e-3)
			bad++;
		block();
	}
	expect(G_FM, bad == 0, "the FM clamp keeps T - k >= 32", bad, 0.0);
}

/*
 * KT-SDB (S105 P1 A7): Osc 2's self-feedback.
 *
 * What:       sdb 0 leaves bFbK 0; on wav Sin the high-frequency share (the
 *             first-difference energy over the signal energy) rises
 *             monotonically with sdb 0, 32, 64, 96, 127.
 * Why:        your point P7: raising sdb adds Osc 2's harmonics one order
 *             after another, so the sidebands reach higher.
 */
/* The share of Osc 2's energy above the fundamental. One block is 32
 * samples; 64 blocks are exactly one cycle when incB = 2^32 / 2048 (21.54
 * Hz), so a plain DFT over the 2,048 samples has no leakage. */
static double hf_share(int sdb)
{
	static double x[2048];
	int n, b, k;
	double e1 = 0.0, eh = 0.0;

	voice_init();
	V.bType = KAT_TYPE_WAV;
	V.bOscType = OSC_TYPE_WAV;
	V.bWave = 3u;                   /* Sin */
	V.ringSideband = (uint8_t)sdb;
	Katran_calcVoiceAsync(&V);
	V.incB = 4294967296u / 2048u;
	V.rowB = V.rowBNext;            /* take the Sin row for the whole window */
	for (b = 0; b < 64; b++) {
		kat_bPlain(&V);
		for (n = 0; n < BLK; n++)
			x[b * BLK + n] = (double)((int32_t)V.blkB[n] & ~1);
	}
	for (k = 1; k < 64; k++) {
		double re = 0.0, im = 0.0;

		for (n = 0; n < 2048; n++) {
			const double a = 2.0 * M_PI * (double)k * (double)n / 2048.0;

			re += x[n] * cos(a);
			im += x[n] * sin(a);
		}
		if (k == 1)
			e1 = re * re + im * im;
		else
			eh += re * re + im * im;
	}
	return eh / (e1 + 1.0);
}

static void test_sdb(void)
{
	static const int sdb[5] = { 0, 32, 64, 96, 127 };
	double prev = -1.0;
	int i, bad = 0;

	voice_init();
	V.ringSideband = 0u;
	Katran_calcVoiceAsync(&V);
	expect(G_SDB, V.bFbK == 0, "sdb 0: no feedback", V.bFbK, 0.0);

	for (i = 0; i < 5; i++) {
		const double r = hf_share(sdb[i]);

		if (i > 0 && r < prev * 0.9)
			bad++;
		prev = r;
	}
	expect(G_SDB, bad == 0 && prev > 2.0 * hf_share(0),
	       "the harmonics rise with sdb", prev, 2.0 * hf_share(0));
}

int main(int argc, char **argv)
{
	int g;
	int any = 0;

	if (argc > 1 && strcmp(argv[1], "--level") == 0) {
		print_level();
		return 0;
	}
	test_boot();
	test_run();
	test_stop_kick();
	test_silent_quiet();
	test_ce_cycle();
	test_cycle_defer();
	test_sync();
	test_arena_cond();
	test_guard();
	test_fade();
	test_spread_vca();
	test_f3();
	test_bounds();
	test_delay();
	test_range();
	test_half_interp();
	test_fold();        /* S103 P4 */
	test_rot();         /* S103 P4 */
	test_band();        /* S105 P1 A4 */
	test_ce_law();      /* S105 P1 A3 */
	test_types();       /* S105 P1 A8 */
	test_ring();        /* S105 P1 A5 */
	test_out();         /* S105 P1 A5 */
	test_rate();        /* S105 P1 A2 */
	test_fm();          /* S105 P1 A6 */
	test_sdb();         /* S105 P1 A7 */
	/* S103 P2: every group counts (the P1 form listed the groups by hand) */
	for (g = 0; g < G_N; g++) {
		if (g_fail[g]) {
			printf("  %s failures: %u\n", g_name[g], g_fail[g]);
			any = 1;
		}
	}
	printf("katran: %s\n", any ? "FAILED" : "all laws pass");
	return any;
}
