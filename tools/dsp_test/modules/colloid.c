/*
 * modules/colloid.c - host laws for the Colloid voice (S095 P2; S095 P3).
 *
 * What:       builds the real ColloidTables.c, ColloidOsc.c and
 *             ColloidVoice.c (extracted, firmware includes and the ITCM
 *             section removed) over the real stock tables, the real PWM width
 *             table and the real sample catalogue code, and checks:
 *               CO-OSC  the pair loops against the read law restated here:
 *                       M = sum of a + ((b - a) f >> 15) (15-bit fraction,
 *                       wrapping next point), S = sum of (s q) >> 16, store
 *                       then add, phases advanced by 32 inc;
 *               CO-POS  spr 0 and mot 0: every position 0, the side bus 0, and
 *                       with equal filters the left and right blocks equal;
 *               CO-W    w(f): a note below 80 Hz centres every oscillator at
 *                       spr 1; above 640 Hz each sits at edge(5 spr home)
 *                       (S095 P3 F6; to the Q15 step);
 *               CO-MOT  mot 1: no oscillator's position moves more than
 *                       0.025 of the field in one block (S095 P3: depth x5);
 *               CO-INT  (S095 P3 F4, F5) with in1..in3 set and dtn 0, every
 *                       oscillator's increment is the root's x 2^(iv[o mod 4]
 *                       / 12) (within 5e-6); at dtn 1 the outer pair is
 *                       +-250 cents (within 0.7 cent);
 *               CO-SLEW (S095 P3 F3) slw 0 lands at once; the first note
 *                       after init lands at once; with slw > 0 every
 *                       oscillator moves at most the slew step per block
 *                       toward its own target and arrives;
 *               CO-EG   the envelopes: sus on holds at the sustain level while
 *                       running; a transport stop releases on the next block;
 *                       sus off runs attack, decay, release to idle; a hit
 *                       while stopped runs attack, decay, release (C-D19);
 *                       a retrigger always starts the attack from the
 *                       current level (S095 P3: no legato);
 *               CO-AMP  the amp ramp: no output step between samples larger
 *                       than the block's level change / 32 (x full scale)
 *                       plus the oscillator's own step;
 *               CO-SVF  the filters: 20,000 blocks of random cutoff,
 *                       resonance, type, envelope and drive stay finite and
 *                       bounded; every block's ramp starts exactly on the
 *                       previous block's target; LP passes DC at unity
 *                       (within 1 %);
 *               CO-SER  a mono route (rte LP): the left and right blocks
 *                       are equal;
 *               CO-RTE  (S095 P3 F9) rte str at zero offsets: both filters'
 *                       coefficients equal and, with no side signal, L == R;
 *                       a cutoff offset moves the right filter only;
 *               CO-POST (S095 P3 F10) the overdrive returns its input exactly
 *                       at wet 0; at drive 0 / tone 127 the output does not
 *                       depend on the tone state (an exact open tone); drive
 *                       127 changes the output and stays bounded;
 *               CO-PWM  the pair loops (sign -1 Rec, +1 Saw) and the warp
 *                       loops (Sin, Tri) against the laws restated here;
 *                       a Rec voice at pos 0, psp 0: every width
 *                       oscPwmDRec[0] (512); the width moves at most 2
 *                       indices per block; every form's voice bus equals the
 *                       restated form on its rows and widths; psp 1 keeps
 *                       every width in 0..127 (S095 P3 F7 folds);
 *               CO-WTB  wtb: psp 0 puts every oscillator on pos's frame;
 *                       frames stay latched when pos moves during a note;
 *                       an empty catalogue plays silence;
 *               MX-ST   (step 2) the stereo mixer functions against the mono
 *                       ones (test_mixer());
 *               CO-RND  (device fix) with the RNG stuck on one value (the
 *                       device between refreshes), the twelve trigger draws
 *                       and the twelve start phases are all distinct.
 * Why:        S095 P2 draft §11 step 3; the schedule's gates for steps 2
 *             and 4.
 * Inputs:     generated fragments (Makefile target colloid).
 * Outputs:    failure counts per group; exit 1 on any failure.
 * Accessors:  make -C tools/dsp_test colloid.
 * Affiliates: ColloidVoice.c, ColloidOsc.c, ColloidTables.c.
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

/* the real stock tables (wavetable.c, const removed for the host) */
#include "col_sine_table.c"
#include "col_sawTable.c"
#include "col_triTable.c"
#include "col_recTable.c"

/* host stand-ins for what the voice references */
typedef struct { uint8_t bytes[124]; } Lfo;
typedef struct { float value; } ModulationNode;
ModulationNode velocityModulators[6];
static unsigned host_lfoRetriggers;
void lfo_init(Lfo *lfo) { memset(lfo, 0, sizeof(*lfo)); }
void lfo_retrigger(uint8_t voiceNr) { (void)voiceNr; host_lfoRetriggers++; }
void modNode_updateValue(ModulationNode *vm, float val) { vm->value = val; }
void instrumentManager_applyVelocityModulationTarget(uint8_t source_slot, float v)
{ (void)source_slot; (void)v; }
static uint8_t host_running = 1u;
uint8_t seq_isRunning(void) { return host_running; }
static uint32_t host_rng = 0x1234567u;
/* host_rng_stuck: return one value on every call, as the device's RNG_DR does
 * between its ~833 ns refreshes (random.c reads it without waiting) */
static int host_rng_stuck = 0;
int16_t GetRngValue(void)
{
	if (host_rng_stuck)
		return (int16_t)0x1234;
	return (int16_t)(golden_rand(&host_rng) & 0xFFFFu);
}

#include "AlkalineControl.host.h"
#include "AlkalinePostTables.host.h"
#include "AlkalinePostTables.host.c"
#include "oscPwmWarp.host.h"
#include "ColloidTables.host.h"
#include "ColloidOsc.host.h"
#include "ColloidVoice.host.h"
#include "ColloidTables.host.c"
#include "ColloidOsc.host.c"
#include "ColloidVoice.host.c"

#define BLK OUTPUT_DMA_SIZE
enum { G_OSC, G_POS, G_W, G_MOT, G_INT, G_SLEW, G_EG, G_AMP, G_SVF, G_SER, G_RTE, G_POST,
       G_PWM, G_WTB, G_MX, G_RND, G_N };
static const char *g_name[G_N] = { "CO-OSC", "CO-POS", "CO-W", "CO-MOT", "CO-INT", "CO-SLEW",
                                   "CO-EG", "CO-AMP", "CO-SVF", "CO-SER", "CO-RTE", "CO-POST",
                                   "CO-PWM", "CO-WTB", "MX-ST", "CO-RND" };
static unsigned g_fail[G_N];

static void fail(int g, const char *what, double a, double b)
{
	if (g_fail[g] < 5u)
		printf("  %s FAIL: %s (%g, %g)\n", g_name[g], what, a, b);
	g_fail[g]++;
}

static ColloidVoice V;
static int16_t L[BLK];

static void voice_init(void)
{
	memset(&V, 0, sizeof(V));
	Colloid_initVoice(&V);
}

static void block(void)
{
	Colloid_calcVoiceAsync(&V);
	Colloid_calcVoiceSyncBlock(&V, L, BLK);
}

/* the read law, restated */
static int32_t ref_read(const int16_t *row, uint32_t ph)
{
	const uint32_t ia = ph >> 22, na = (ia + 1u) & 1023u;
	const int32_t a = row[ia], b = row[na];
	return a + (int32_t)(((int64_t)(b - a) * (int64_t)((ph >> 7) & 0x7FFFu)) >> 15);
}

static int32_t ref_pulse(const int16_t *row, uint32_t ph, uint32_t d, int32_t sign)
{
	const uint32_t ia = ph >> 22, na = (ia + 1u) & 1023u;
	const uint32_t ib = (ia - d) & 1023u, nb = (ib + 1u) & 1023u;
	const int32_t d0 = row[ia] + sign * row[ib], d1 = row[na] + sign * row[nb];
	return d0 + (int32_t)(((int64_t)(d1 - d0) * (int64_t)((ph >> 7) & 0x7FFFu)) >> 15);
}

static int32_t ref_side(int32_t s, int16_t q) { return (int32_t)(((int64_t)s * q) >> 16); }

/* the warp law (S095 P3 F2): the shared osc_pwmWarp(), then the read law */
static int32_t ref_warp(const int16_t *row, uint32_t ph, uint32_t w, uint32_t ra, uint32_t rb)
{
	return ref_read(row, osc_pwmWarp(ph, w, ra, rb));
}

static int16_t q_of(unsigned i)
{
	return (int16_t)((i & 1u) ? (V.posPair[i >> 1] >> 16) : (V.posPair[i >> 1] & 0xFFFFu));
}

static void test_osc(int pwm, int32_t sign)
{
	/* plain: a full-scale alternating row (steps of 65,535, as a user frame
	 * may have) proves the 15-bit fraction's headroom; pwm: the saw rows its
	 * |d1 - d0| <= 65,534 bound is stated for (C-D21) */
	static int16_t edge[1024];
	const int16_t *plain[4] = { sawTable[3], edge, recTable[0], sine_table };
	const int16_t *pulse[4] = { sawTable[3], sawTable[0], sawTable[10], sawTable[6] };
	const int16_t *const *rows = pwm ? pulse : plain;
	uint32_t ph[4] = { 0x00000000u, 0x7fff1234u, 0xfffff000u, 0x12345678u };
	const uint32_t inc[4] = { 0x01234567u, 0x00a00000u, 0x7ffffff0u, 0x00010001u };
	const uint16_t wid[4] = { 512u, 3u, 1000u, 64u };
	const uint32_t pq0 = (uint32_t)(uint16_t)(int16_t)-20000 | ((uint32_t)(uint16_t)(int16_t)31000 << 16);
	const uint32_t pq1 = (uint32_t)(uint16_t)(int16_t)32767 | ((uint32_t)(uint16_t)(int16_t)-32768 << 16);
	int32_t M[BLK], S[BLK];
	uint32_t p[4];
	int g = pwm ? G_PWM : G_OSC;
	unsigned n, o;

	for (n = 0u; n < 1024u; n++)
		edge[n] = (n & 1u) ? -32768 : 32767;
	memcpy(p, ph, sizeof(p));
	if (pwm) {
		col_pwmPairStore(&rows[0], &p[0], &inc[0], &wid[0], pq0, M, S, sign);
		col_pwmPairAdd(&rows[2], &p[2], &inc[2], &wid[2], pq1, M, S, sign);
	} else {
		col_oscPairStore(&rows[0], &p[0], &inc[0], pq0, M, S);
		col_oscPairAdd(&rows[2], &p[2], &inc[2], pq1, M, S);
	}
	for (n = 0u; n < BLK; n++) {
		int32_t m = 0, s = 0;
		for (o = 0u; o < 4u; o++) {
			const uint32_t at = ph[o] + n * inc[o];
			const int32_t v = pwm ? ref_pulse(rows[o], at, wid[o], sign) : ref_read(rows[o], at);
			const uint32_t pq = (o < 2u) ? pq0 : pq1;
			m += v;
			s += ref_side(v, (int16_t)((o & 1u) ? (pq >> 16) : (pq & 0xFFFFu)));
		}
		if (M[n] != m) fail(g, "mid bus", M[n], m);
		if (S[n] != s) fail(g, "side bus", S[n], s);
	}
	for (o = 0u; o < 4u; o++)
		if (p[o] != ph[o] + BLK * inc[o]) fail(g, "phase advance", p[o], ph[o] + BLK * inc[o]);
}

/* CO-PWM (S095 P3 F2): the warp loops against the restated law, store then add */
static void test_warp(void)
{
	const int16_t *rows[3] = { sine_table, triTable[2], triTable[9] };
	uint32_t ph[3] = { 0x00000000u, 0x7fff1234u, 0xfffff000u };
	const uint32_t inc[3] = { 0x01234567u, 0x00a00000u, 0x0abcdef1u };
	const uint32_t k[3] = { 0u, 70u, 127u };
	const uint32_t pq[3] = { (uint32_t)(uint16_t)(int16_t)-20000, 32767u,
	                         (uint32_t)(uint16_t)(int16_t)-32768 };
	int32_t M[BLK], S[BLK];
	uint32_t p[3];
	unsigned n, o;

	memcpy(p, ph, sizeof(p));
	col_warpStore(rows[0], &p[0], inc[0], oscPwmSinW[k[0]], oscPwmSinRa[k[0]], oscPwmSinRb[k[0]], pq[0], M, S);
	col_warpAdd(rows[1], &p[1], inc[1], oscPwmTriW[k[1]], oscPwmTriRa[k[1]], oscPwmTriRb[k[1]], pq[1], M, S);
	col_warpAdd(rows[2], &p[2], inc[2], oscPwmTriW[k[2]], oscPwmTriRa[k[2]], oscPwmTriRb[k[2]], pq[2], M, S);
	for (n = 0u; n < BLK; n++) {
		int32_t m = 0, sd = 0;
		for (o = 0u; o < 3u; o++) {
			const uint32_t at = ph[o] + n * inc[o];
			const int32_t v = (o == 0u)
			    ? ref_warp(rows[o], at, oscPwmSinW[k[o]], oscPwmSinRa[k[o]], oscPwmSinRb[k[o]])
			    : ref_warp(rows[o], at, oscPwmTriW[k[o]], oscPwmTriRa[k[o]], oscPwmTriRb[k[o]]);
			m += v;
			sd += ref_side(v, (int16_t)(pq[o] & 0xFFFFu));
		}
		if (M[n] != m) fail(G_PWM, "warp mid bus", M[n], m);
		if (S[n] != sd) fail(G_PWM, "warp side bus", S[n], sd);
	}
	for (o = 0u; o < 3u; o++)
		if (p[o] != ph[o] + BLK * inc[o]) fail(G_PWM, "warp phase advance", p[o], ph[o]);
}

static void test_pos_and_w(void)
{
	unsigned b, i;
	/* CO-POS: spr 0, mot 0 */
	voice_init();
	V.spread = 0.0f; V.motion = 0.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	for (b = 0u; b < 50u; b++) {
		block();
		for (i = 0u; i < COL_PAIR_MAX; i++)
			if (V.posPair[i] != 0u) fail(G_POS, "position not 0", i, V.posPair[i]);
		for (i = 0u; i < BLK; i++) {
			if (V.busS[i] != 0) fail(G_POS, "side bus not 0", i, V.busS[i]);
			if (L[i] != V.right[i]) fail(G_POS, "L != R", L[i], V.right[i]);
		}
	}
	/* CO-W: below 80 Hz (coa 24: C1, 32.7 Hz) every position is 0 at spr 1 */
	voice_init();
	V.spread = 1.0f; V.motion = 0.0f; V.detune = 0.0f;
	V.osc.midiFreq = (uint16_t)((24u << 8) | 63u);
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	block();
	for (i = 0u; i < COL_PAIR_MAX; i++)
		if (V.posPair[i] != 0u) fail(G_W, "low note not centred", i, V.posPair[i]);
	/* above 640 Hz (coa 84: C6, 1,046.5 Hz) each oscillator at its home
	 * through the edge: spr 0.2 x 5 = 1, p = edge(home) (S095 P3 F6) */
	V.spread = 0.2f;
	V.osc.midiFreq = (uint16_t)((84u << 8) | 63u);
	block();
	for (i = 0u; i < COL_OSC_COUNT; i++) {
		const float c = fminf(fmaxf(colHome[i], -1.5f), 1.5f);
		const int32_t want = (int32_t)((c - (4.0f / 27.0f) * c * c * c) * 32767.0f);
		if (abs(q_of(i) - want) > 2) fail(G_W, "high note not at edge(home)", q_of(i), want);
	}
	/* spr 1: every position past 0.8 of the edge (x5: they crowd the edges) */
	V.spread = 1.0f;
	block();
	for (i = 0u; i < COL_OSC_COUNT; i++)
		if (abs(q_of(i)) < (int)(0.8f * 32767.0f)) fail(G_W, "spr 1 not at the edges", q_of(i), 0);
}

static void test_motion(void)
{
	unsigned b, i;
	int16_t last[COL_OSC_MAX];
	voice_init();
	V.spread = 0.5f; V.motion = 1.0f;
	V.osc.midiFreq = (uint16_t)((84u << 8) | 63u);   /* w = 1: the full movement */
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	block();
	for (i = 0u; i < COL_OSC_COUNT; i++)
		last[i] = (int16_t)((i & 1u) ? (V.posPair[i >> 1] >> 16) : (V.posPair[i >> 1] & 0xFFFFu));
	for (b = 0u; b < 3000u; b++) {
		block();
		for (i = 0u; i < COL_OSC_COUNT; i++) {
			const int16_t q = (int16_t)((i & 1u) ? (V.posPair[i >> 1] >> 16) : (V.posPair[i >> 1] & 0xFFFFu));
			/* 0.025 of the field (-1..+1, so 2 x 32,767) per block: depth 3
			 * x the parabola's slope 4 x 0.004 of a cycle, through the edge
			 * (slope <= 1) */
			if (abs(q - last[i]) > (int)(0.025 * 2.0 * 32767.0)) fail(G_MOT, "position step", q, last[i]);
			last[i] = q;
		}
	}
}

static void test_intervals(void)
{
	static const uint8_t ivb[3][3] = { { 63u + 7u, 63u - 12u, 63u + 24u },
	                                   { 63u + 3u, 63u + 5u, 63u - 7u },
	                                   { 63u, 63u, 63u } };
	unsigned s, i;
	for (s = 0u; s < 3u; s++) {
		voice_init();
		V.detune = 0.0f;
		memcpy(V.interval, ivb[s], 3u);
		Colloid_triggerVoice(&V, 0u, 100u, 63u);
		block();
		for (i = 0u; i < COL_OSC_COUNT; i++) {
			const unsigned g = i & 3u;
			const int st = g ? (int)ivb[s][g - 1u] - 63 : 0;
			const double want = (double)V.inc[0] * exp2(st / 12.0);
			if (fabs(V.inc[i] / want - 1.0) > 5e-6) fail(G_INT, "interval ratio", V.inc[i], want);
		}
	}
	voice_init();
	V.detune = 1.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	block();
	{
		/* oscillators 6 and 7: colDetC +1 and -1 */
		const double cents = 1200.0 * log2((double)V.inc[6] / (double)V.inc[7]) / 2.0;
		if (fabs(cents - 250.0) > 0.7) fail(G_INT, "outer pair cents", cents, 250.0);
	}
}

/* the slew step restated (S095 P3 F3, Q1) */
static float ref_slewStep(uint8_t slw)
{
	return (slw == 0u) ? 1.0e9f
	    : (float)((32.0 / REAL_FS) / 0.001 * exp2(-13.2877124 * (double)(slw - 1u) / 126.0));
}

static void test_slew(void)
{
	unsigned b, i;
	/* the first note after init lands at once, at any slw */
	voice_init();
	V.slew = 100u; V.detune = 0.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	block();
	for (i = 0u; i < COL_OSC_COUNT; i++)
		if (fabsf(V.pitchCur[i] - V.pitchCur[0]) > 1e-6f) fail(G_SLEW, "first note not landed", i, V.pitchCur[i]);
	if (V.pitchJump != 0u) fail(G_SLEW, "jump flag not cleared", V.pitchJump, 0);
	/* slw 60: an octave up moves at most one step per block and arrives */
	{
		const float start = V.pitchCur[0];
		const float step = ref_slewStep(60u);
		unsigned arrived = 0u;
		V.slew = 60u;
		Colloid_triggerVoice(&V, 0u, 100u, 75u);
		for (b = 0u; b < 20000u && !arrived; b++) {
			float before[COL_OSC_MAX];
			memcpy(before, V.pitchCur, sizeof(before));
			block();
			for (i = 0u; i < COL_OSC_COUNT; i++)
				if (fabsf(V.pitchCur[i] - before[i]) > step * 1.0001f + 1e-6f) {
					fail(G_SLEW, "slew step", V.pitchCur[i] - before[i], step);
					b = 20000u;
					break;
				}
			if (fabsf(V.pitchCur[0] - (start + 1.0f)) < 1e-5f) arrived = 1u;
		}
		if (!arrived) fail(G_SLEW, "slew never arrived", V.pitchCur[0], start + 1.0f);
		if (b < 2u) fail(G_SLEW, "slw 60 jumped", b, 0);
	}
	/* slw 0: the next note lands in one block */
	V.slew = 0u;
	Colloid_triggerVoice(&V, 0u, 100u, 51u);
	{
		const float want = V.pitchCur[0] - 2.0f;
		block();
		if (fabsf(V.pitchCur[0] - want) > 1e-5f) fail(G_SLEW, "slw 0 not instant", V.pitchCur[0], want);
	}
}

static unsigned run_until_stage(ColEg *e, uint8_t stage, unsigned max)
{
	unsigned b;
	for (b = 0u; b < max; b++) {
		block();
		if (e->stage == stage) return b;
	}
	return max;
}

static void test_eg(void)
{
	unsigned b;
	float lvl;
	/* sus on, running: holds */
	voice_init(); host_running = 1u; V.sustain = 1u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	if (run_until_stage(&V.ampEg, COL_EG_SUSTAIN, 5000u) >= 5000u) fail(G_EG, "no sustain", 0, 0);
	for (b = 0u; b < 1000u; b++) {
		block();
		if (V.ampEg.stage != COL_EG_SUSTAIN) { fail(G_EG, "sustain did not hold", b, V.ampEg.stage); break; }
	}
	if (fabsf(V.ampEg.level - V.sustainLevel) > 0.01f) fail(G_EG, "sustain level", V.ampEg.level, V.sustainLevel);
	/* a transport stop releases on the next block */
	host_running = 0u;
	block();
	if (V.ampEg.stage != COL_EG_RELEASE) fail(G_EG, "stop did not release", V.ampEg.stage, COL_EG_RELEASE);
	if (V.fltEg.stage != COL_EG_RELEASE) fail(G_EG, "stop did not release fenv", V.fltEg.stage, COL_EG_RELEASE);
	if (run_until_stage(&V.ampEg, COL_EG_IDLE, 20000u) >= 20000u) fail(G_EG, "release never ended", 0, 0);
	if (!Colloid_isQuiet(&V)) fail(G_EG, "idle voice not quiet", 0, 0);
	/* a hit while stopped: attack, decay, release, idle */
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	if (Colloid_isQuiet(&V)) fail(G_EG, "sounding voice quiet", 0, 0);
	if (run_until_stage(&V.ampEg, COL_EG_IDLE, 40000u) >= 40000u) fail(G_EG, "stopped hit held", V.ampEg.stage, 0);
	/* sus off while running: A-D-R */
	host_running = 1u; V.sustain = 0u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	if (run_until_stage(&V.ampEg, COL_EG_IDLE, 40000u) >= 40000u) fail(G_EG, "sus off held", V.ampEg.stage, 0);
	/* a retrigger while held: the attack restarts from the current level
	 * (S095 P3 F3: no legato) */
	V.sustain = 1u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	(void)run_until_stage(&V.ampEg, COL_EG_SUSTAIN, 5000u);
	lvl = V.ampEg.level;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	if (V.ampEg.stage != COL_EG_ATTACK || V.ampEg.level != lvl) fail(G_EG, "retrigger level", V.ampEg.level, lvl);
}

static void test_amp(void)
{
	unsigned b, i;
	int16_t prev = 0;
	voice_init(); host_running = 1u;
	V.sustain = 0u; V.attack = 0u; V.release = 0u; V.decay = 0u;
	V.cut = 1.0f; V.res = 0.0f; V.detune = 0.0f;
	/* a smooth signal (Sin at C3: under 2 % of full scale per sample for the
	 * whole stack), so the bound tests the ramp whatever the start phases
	 * (they are random: CO-RND) */
	V.osc.waveform = COL_WAVE_SIN;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	for (b = 0u; b < 400u; b++) {
		const float before = V.ampEg.level;
		block();
		{
			const float bound = fabsf(V.ampEg.level - before) * 32767.0f / (float)BLK;
			for (i = 0u; i < BLK; i++) {
				/* the step also carries the signal's own change: the Sin
				 * stack moves at most a few hundred LSB per sample */
				if (abs(L[i] - prev) > (int)(bound + 1500.0f)) fail(G_AMP, "output step", L[i] - prev, bound);
				prev = L[i];
			}
		}
	}
}

static void test_svf(void)
{
	unsigned b, i;
	uint32_t s = 99u;
	voice_init(); host_running = 1u;
	Colloid_triggerVoice(&V, 0u, 127u, 63u);
	for (b = 0u; b < 20000u; b++) {
		const float t1A = V.svfA.t1, t2A = V.svfA.t2, t3A = V.svfA.t3;
		if ((b % 7u) == 0u) {
			V.cut = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.cutOfs = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.res = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.resOfs = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.envOfs = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.driveOfs = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.filtType = (uint8_t)(golden_rand(&s) % 5u);
			V.route = (uint8_t)(golden_rand(&s) % COL_RTE_COUNT);
			V.fAmount = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.drive = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.vcaDrive = (uint8_t)(golden_rand(&s) & 127u);
			V.tone = (uint8_t)(golden_rand(&s) & 127u);
		}
		if ((b % 300u) == 0u) Colloid_triggerVoice(&V, 0u, 127u, (uint8_t)(40u + (golden_rand(&s) % 50u)));
		block();
		if (V.svfA.a1 != t1A || V.svfA.a2 != t2A || V.svfA.a3 != t3A) fail(G_SVF, "ramp start != last target", V.svfA.a1, t1A);
		if (!isfinite(V.svfA.ic1) || !isfinite(V.svfA.ic2) || !isfinite(V.svfB.ic1) || !isfinite(V.svfB.ic2) ||
		    fabsf(V.svfA.ic1) > 100.0f || fabsf(V.svfA.ic2) > 100.0f ||
		    fabsf(V.svfB.ic1) > 100.0f || fabsf(V.svfB.ic2) > 100.0f) {
			fail(G_SVF, "state unbounded", V.svfA.ic1, V.svfB.ic2);
			break;
		}
	}
	/* LP DC gain: a constant input through the parallel A filter */
	{
		ColSvf f;
		float y = 0.0f, ic1, ic2;
		col_svfInit(&f, 0.5f, 0.3f, COL_FT_LP);
		ic1 = f.ic1; ic2 = f.ic2;
		for (i = 0u; i < 20000u; i++) {
			const float x = 0.5f;
			const float v3 = x - ic2, v1 = f.a1 * ic1 + f.a2 * v3, v2 = ic2 + f.a2 * ic1 + f.a3 * v3;
			ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
			y = f.mH * x + f.mB * v1 + f.mL * v2;
		}
		if (fabsf(y - 0.5f) > 0.005f) fail(G_SVF, "LP DC gain", y, 0.5);
	}
}

static void test_ser(void)
{
	unsigned b, i;
	voice_init(); host_running = 1u;
	V.route = 1u + COL_FT_LP; V.spread = 1.0f; V.motion = 1.0f;
	V.osc.midiFreq = (uint16_t)((84u << 8) | 63u);
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	for (b = 0u; b < 100u; b++) {
		block();
		for (i = 0u; i < BLK; i++)
			if (L[i] != V.right[i]) { fail(G_SER, "series L != R", L[i], V.right[i]); break; }
	}
}

/* CO-RTE (S095 P3 F9) */
static void test_route(void)
{
	unsigned b, i;
	voice_init(); host_running = 1u;
	V.spread = 0.0f; V.motion = 0.0f;   /* no side signal */
	V.route = COL_RTE_STR; V.res = 0.6f; V.fAmount = 100.0f / 127.0f; V.drive = 0.5f;
	V.cut = 0.4f;   /* below the envelope's reach of the ceiling (115.7 st) */
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	for (b = 0u; b < 50u; b++) {
		block();
		if (V.svfA.t1 != V.svfB.t1 || V.svfA.t2 != V.svfB.t2 || V.svfA.k != V.svfB.k)
			fail(G_RTE, "str zero offsets: filters differ", V.svfA.t1, V.svfB.t1);
		for (i = 0u; i < BLK; i++)
			if (L[i] != V.right[i]) { fail(G_RTE, "str zero offsets: L != R", L[i], V.right[i]); break; }
	}
	/* a cutoff offset moves the right filter only */
	{
		float cutA;
		V.fAmount = 63.0f / 127.0f;   /* no envelope: both cutoffs on their knobs */
		for (b = 0u; b < 40u; b++)
			block();
		cutA = V.svfA.cutSt;
		V.cutOfs = (63.0f + 20.0f) / 127.0f;
		for (b = 0u; b < 40u; b++)
			block();
		if (fabsf(V.svfA.cutSt - cutA) > 0.01f) fail(G_RTE, "fro moved the left filter", V.svfA.cutSt, cutA);
		if (V.svfB.cutSt - V.svfA.cutSt < 10.0f) fail(G_RTE, "fro did not move the right filter", V.svfB.cutSt, V.svfA.cutSt);
	}
}

/* CO-POST (S095 P3 F10) */
static void test_post(void)
{
	unsigned b, i;
	uint32_t s = 7u;
	int differ = 0;
	int16_t ref[50][BLK];
	/* the overdrive at wet 0 returns its input to the bit */
	for (i = 0u; i < 10000u; i++) {
		const float y = ((float)(golden_rand(&s) & 0xFFFFu) - 32768.0f) / 9000.0f;
		const uint32_t k = golden_rand(&s) & 127u;
		if (col_post(y, alkOvdGain[k], alkOvdMakeup[k], 0.0f) != y) fail(G_POST, "wet 0 not exact", y, k);
	}
	/* drive 0, tone 127: the output does not depend on the tone state */
	for (b = 0u; b < 2u; b++) {
		unsigned blk;
		host_rng = 0x2468ACEu;   /* the same start phases and draws each run */
		voice_init(); host_running = 1u;
		V.vcaDrive = 0u; V.tone = 127u; V.res = 0.8f;
		Colloid_triggerVoice(&V, 0u, 127u, 63u);
		for (blk = 0u; blk < 50u; blk++) {
			/* a large state (float step about 2 near 3e7): only an exact
			 * open tone (o + 0 x (tz - o)) leaves the output unchanged */
			if (b) { V.toneZL = 3.0e7f; V.toneZR = -3.0e7f; }
			block();
			for (i = 0u; i < BLK; i++) {
				if (!b) ref[blk][i] = L[i];
				else if (L[i] != ref[blk][i]) { fail(G_POST, "open tone depends on its state", L[i], ref[blk][i]); blk = 50u; break; }
			}
		}
	}
	/* drive 127 changes the output and keeps it finite */
	host_rng = 0x2468ACEu;
	voice_init(); host_running = 1u;
	V.vcaDrive = 127u; V.tone = 127u; V.res = 0.8f;
	Colloid_triggerVoice(&V, 0u, 127u, 63u);
	for (b = 0u; b < 50u; b++) {
		block();
		if (!isfinite(V.toneZL) || !isfinite(V.toneZR)) { fail(G_POST, "state not finite", V.toneZL, V.toneZR); break; }
		for (i = 0u; i < BLK; i++)
			if (L[i] != ref[b][i]) differ = 1;
	}
	if (!differ) fail(G_POST, "drive 127 changed nothing", 0, 0);
	(void)s;
}

/* the voice's buses against the restated pwm forms, one block (S095 P3 F2) */
static void check_pwm_form(uint8_t form)
{
	uint32_t ph[COL_OSC_MAX];
	unsigned n, i;
	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_PWM);
	V.osc.waveform = form; V.osc.position = 40u; V.posSpread = 0.6f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	block();
	Colloid_calcVoiceAsync(&V);
	memcpy(ph, V.phase, sizeof(ph));
	Colloid_calcVoiceSyncBlock(&V, L, BLK);
	for (n = 0u; n < BLK; n++) {
		int32_t m = 0;
		for (i = 0u; i < COL_PWM_COUNT; i++) {
			const uint32_t at = ph[i] + n * V.inc[i];
			const uint32_t k = V.pwPos[i] & 127u;
			int32_t v;
			if (form == COL_PWM_SIN)
				v = ref_warp(sine_table, at, oscPwmSinW[k], oscPwmSinRa[k], oscPwmSinRb[k]);
			else if (form == COL_PWM_TRI)
				v = ref_warp(V.row[i], at, oscPwmTriW[k], oscPwmTriRa[k], oscPwmTriRb[k]);
			else
				v = ref_pulse(V.row[i], at, (form == COL_PWM_SAW) ? oscPwmDSaw[k] : oscPwmDRec[k],
				              (form == COL_PWM_SAW) ? 1 : -1);
			m += v;
		}
		if (V.busM[n] != m) { fail(G_PWM, "form bus", form, n); break; }
	}
}

static void test_pwm_voice(void)
{
	unsigned b, i;
	uint8_t f;
	for (f = 0u; f < COL_PWM_WAVE_COUNT; f++)
		check_pwm_form(f);
	/* psp 1 (k 5): every width target inside 0..127 (the folds) */
	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_PWM);
	V.osc.position = 0u; V.posSpread = 1.0f;
	for (b = 0u; b < 20u; b++) {
		Colloid_triggerVoice(&V, 0u, 100u, 63u);
		for (i = 0u; i < COL_OSC_MAX; i++)
			if (V.pwPos[i] > 127u) fail(G_PWM, "width outside 0..127", V.pwPos[i], 127);
	}
	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_PWM);
	V.osc.waveform = COL_PWM_REC;
	V.osc.position = 0u; V.posSpread = 0.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	block();
	for (i = 0u; i < COL_PWM_COUNT; i++)
		if (V.pwD[i] != oscPwmDRec[0]) fail(G_PWM, "pos 0 width", V.pwD[i], oscPwmDRec[0]);
	V.osc.position = 127u;
	for (b = 0u; b < 80u; b++) {
		uint8_t before[COL_OSC_MAX];
		memcpy(before, V.pwPos, sizeof(before));
		block();
		for (i = 0u; i < COL_PWM_COUNT; i++)
			if (abs((int)V.pwPos[i] - (int)before[i]) > OSC_PWM_SLEW_STEPS) fail(G_PWM, "width slew", V.pwPos[i], before[i]);
	}
	for (i = 0u; i < COL_PWM_COUNT; i++)
		if (V.pwPos[i] != 127u) fail(G_PWM, "width did not arrive", V.pwPos[i], 127);
}

static void install_table(unsigned frames)
{
	static int16_t fr[16][1024];
	static int16_t fbuf[WAVETABLE_FRAME_MAX];
	const sample_install_fmt_t fw = { 1u, 1u, 44100u, 2u, 16u, 16u };
	unsigned f, i;
	for (f = 0u; f < frames; f++)
		for (i = 0u; i < 1024u; i++)
			fr[f][i] = (int16_t)(20000.0 * sin(6.283185307 * i * (f + 1) / 1024.0));
	catalogue_host_reset();
	sampleInstall_begin();
	if (frames) {
		(void)sampleInstall_beginWavetable("wcol    ", 1024u, (uint8_t)frames, &fw);
		for (f = 0u; f < frames; f++)
			(void)sampleInstall_writeFrame((const uint8_t *)fr[f], 1024u, fbuf);
		(void)sampleInstall_finishWavetable();
	}
	(void)sampleInstall_commit();
	sampleMemory_refresh();
}

static void test_wtb(void)
{
	unsigned b, i;
	/* psp 0: every oscillator on pos's frame; latched while pos moves */
	install_table(8u);
	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_WTB);
	V.osc.waveform = 0u; V.osc.position = 127u; V.posSpread = 0.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	block();
	if (V.osc.sampleCacheSize != 8u) fail(G_WTB, "frames bound", V.osc.sampleCacheSize, 8);
	for (i = 0u; i < COL_OSC_MAX; i++)
		if (V.frame[i] != 7u) fail(G_WTB, "psp 0 frame", V.frame[i], 7);
	V.osc.position = 0u;
	block();
	for (i = 0u; i < COL_OSC_COUNT; i++)
		if (V.row[i] != V.osc.sampleCacheData + 7u * WAVETABLE_ROW_POINTS) fail(G_WTB, "frame not latched", i, 0);
	/* psp 1: frames spread, all inside the table */
	V.posSpread = 1.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	for (i = 0u; i < COL_OSC_MAX; i++)
		if (V.frame[i] >= 8u) fail(G_WTB, "frame outside", V.frame[i], 8);
	/* an empty catalogue: silence */
	install_table(0u);
	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_WTB);
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	for (b = 0u; b < 50u; b++) {
		block();
		for (i = 0u; i < BLK; i++)
			if (L[i] != 0 || V.right[i] != 0) { fail(G_WTB, "empty catalogue not silent", L[i], V.right[i]); b = 50u; break; }
	}
}

/*
 * MX-ST (S095 P2 step 2): the stereo mixer functions against the mono ones.
 *
 * What:       with dataL == dataR, mixer_addVoiceStereoToOutput() and
 *             mixer_addVoiceStereoToOutputAndFx() (stereo-input and
 *             mono-input send) produce the same output buses and FX bus as
 *             mixer_addVoiceInt16ToOutput() / mixer_addVoiceInt16ToOutputAndFx()
 *             for every routing, pan 0, 63 and 127, a ramping gain and send;
 *             with dataR = 0 a stereo route adds nothing to the right bus.
 * Why:        C-D11: a stereo voice is the mono voice's law per side.
 */
static mixer_fx_bus_t mixer_fx_bus;
static inline sample_mx_t bufferTool_satAdd32(sample_mx_t a, sample_mx_t b)
{
	const int64_t s = (int64_t)a + b;
	return (sample_mx_t)(s > INT32_MAX ? INT32_MAX : (s < INT32_MIN ? INT32_MIN : s));
}
#include "col_mixer_funcs.c"
static float sqrtLut(uint8_t p) { return sqrtf((float)p / 127.0f); }

static void test_mixer(void)
{
	static const uint8_t pans[3] = { 0u, 63u, 127u };
	int16_t d[BLK], z[BLK];
	unsigned r, pi, st, i;
	for (i = 0u; i < BLK; i++) { d[i] = (int16_t)(12000 * sin(0.3 * i)); z[i] = 0; }
	for (r = 0u; r < 6u; r++)
		for (pi = 0u; pi < 3u; pi++)
			for (st = 0u; st < 3u; st++) {   /* 0 dry, 1 send stereo-in, 2 send mono-in */
				sample_mx_t a[4][2 * BLK], b[4][2 * BLK];
				mixer_fx_bus_t fa, fb;
				const float pl = sqrtLut((uint8_t)(127u - pans[pi])), pr = sqrtLut(pans[pi]);
				memset(a, 0, sizeof(a)); memset(b, 0, sizeof(b));
				memset(&mixer_fx_bus, 0, sizeof(mixer_fx_bus));
				if (st == 0u)
					mixer_addVoiceInt16ToOutput((uint8_t)r, pl, pr, d, 0.8f, 0.5f, a[0], a[1], a[2], a[3]);
				else
					mixer_addVoiceInt16ToOutputAndFx((uint8_t)r, pl, pr, d, 0.8f, 0.5f, 0.6f, 0.2f,
					                                 (uint8_t)(st == 1u), a[0], a[1], a[2], a[3]);
				fa = mixer_fx_bus;
				memset(&mixer_fx_bus, 0, sizeof(mixer_fx_bus));
				if (st == 0u)
					mixer_addVoiceStereoToOutput((uint8_t)r, pl, pr, d, d, 0.8f, 0.5f, b[0], b[1], b[2], b[3]);
				else
					mixer_addVoiceStereoToOutputAndFx((uint8_t)r, pl, pr, d, d, 0.8f, 0.5f, 0.6f, 0.2f,
					                                  (uint8_t)(st == 1u), b[0], b[1], b[2], b[3]);
				fb = mixer_fx_bus;
				if (memcmp(a, b, sizeof(a)) != 0) fail(G_MX, "dry differs", r, pans[pi]);
				if (memcmp(&fa, &fb, sizeof(fa)) != 0) fail(G_MX, "send differs", r, st);
			}
	/* dataR = 0 on a stereo route: nothing reaches the right bus */
	{
		sample_mx_t b[4][2 * BLK];
		memset(b, 0, sizeof(b));
		mixer_addVoiceStereoToOutput(0u, 1.0f, 1.0f, d, z, 1.0f, 1.0f, b[0], b[1], b[2], b[3]);
		for (i = 0u; i < BLK; i++)
			if (b[3][2 * i] != 0) { fail(G_MX, "right bus fed from L", i, b[3][2 * i]); break; }
	}
}

/*
 * CO-RND (S095 P2, device fix): the draws do not depend on fresh RNG values.
 *
 * What:       with GetRngValue() stuck on one value, Colloid_initVoice()'s
 *             twelve oscillator phases and a trigger's twelve draws are each
 *             pairwise distinct.
 * Why:        the device's GetRngValue() returns RNG_DR without waiting, so
 *             back-to-back reads repeat; the voice must seed once and generate.
 */
static void test_rnd(void)
{
	unsigned a, b;
	host_rng_stuck = 1;
	voice_init();
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	for (a = 0u; a < COL_OSC_MAX; a++)
		for (b = a + 1u; b < COL_OSC_MAX; b++) {
			if (V.rnd[a] == V.rnd[b]) fail(G_RND, "trigger draws equal", a, b);
			if (V.phase[a] == V.phase[b]) fail(G_RND, "start phases equal", a, b);
		}
	host_rng_stuck = 0;
}

int main(void)
{
	unsigned g, total = 0u;
	install_table(0u);
	test_osc(0, -1);
	test_osc(1, -1);   /* Rec */
	test_osc(1, 1);    /* Saw (S095 P3 F2) */
	test_warp();
	test_pos_and_w();
	test_motion();
	test_intervals();
	test_slew();
	test_eg();
	test_amp();
	test_svf();
	test_ser();
	test_route();
	test_post();
	test_pwm_voice();
	test_wtb();
	test_mixer();
	test_rnd();
	for (g = 0u; g < G_N; g++) {
		printf("colloid %-7s failures: %u\n", g_name[g], g_fail[g]);
		total += g_fail[g];
	}
	return total ? 1 : 0;
}
