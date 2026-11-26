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
 *               CO-SVF  (S096 P2: also off passes the input exactly, UBP
 *                       is unity at its centre at res 0.2 and 0.9, and a
 *                       full-depth envelope reaches 115.7 / 0 semitones)
 *                       the filters: 20,000 blocks of random cutoff,
 *                       resonance, type, envelope and drive stay finite and
 *                       bounded; every block's ramp starts exactly on the
 *                       previous block's target; LP passes DC at unity
 *                       (within 1 %);
 *               CO-SER  a mono route (rte LP): the left and right blocks
 *                       are equal;
 *               CO-MONO (S103 P6) the mono route's filter mix: spr 63,
 *                       mot 0 is the S102 loop bit for bit (a frozen copy);
 *                       spr 0 is filter A alone, spr 127 filter B alone on
 *                       its own drive (serial and parallel); parallel with
 *                       identical filters at spr 63 is sqrt 2 x one filter
 *                       (equal power); the law's corner values; the ramps
 *                       land on their targets;
 *               CO-RTE  (S095 P3 F9) rte str at zero offsets: both filters'
 *                       coefficients equal and, with no side signal, L == R;
 *                       a cutoff offset moves the right filter only;
 *               CO-POST (S095 P3 F10) the overdrive returns its input exactly
 *                       at wet 0; at drive 0 / tone 127 the output does not
 *                       depend on the tone state (an exact open tone); drive
 *                       127 changes the output and stays bounded;
 *               CO-PWM  the pair loops (sign -1 Rec, +1 Saw) and the warp
 *                       loops (Tri; S099 P1 §15.10 dropped pwm Sin) against
 *                       the laws restated here;
 *                       a Rec voice at pos 0, psp 0: every width
 *                       oscPwmDRec[0] (512); the width moves at most 2
 *                       indices per block; every form's voice bus equals the
 *                       restated form on its rows and widths; psp 1 keeps
 *                       every width in 0..127 (S095 P3 F7 folds);
 *               CO-PWG  (S096 P2 F5) a moving pwm width does not click:
 *                       every form, a slow sweep and the full slew, the
 *                       largest third difference within 3 x the still
 *                       width's + 0.002;
 *               CO-WTB  wtb: psp 0 puts every oscillator on pos's frame;
 *                       frames stay latched when pos moves during a note;
 *                       an empty catalogue plays silence;
 *               MX-ST   (step 2) the stereo mixer functions against the mono
 *                       ones (test_mixer());
 *               CO-RND  (device fix) with the RNG stuck on one value (the
 *                       device between refreshes), the twelve trigger draws
 *                       and the twelve start phases are all distinct;
 *               CO-JOIN (S095 P5, user 2026-11-14) the count follows the pwm
 *                       form (oscLive 6 under Saw/Rec, 6 under Tri, 12
 *                       under wav); an oscillator that starts rendering
 *                       again lands on its target pitch and width in its
 *                       first block while the others still slew: pwm Tri ->
 *                       Saw (oscillators 6, 7) and pwm -> wav (8..11).
 *                       CO-PWM's form bus sums each form's own count
 *                       (COL_PWM_FORM_COUNT()).
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
#include "oscChopTables.host.h"     /* S099 P1: the chop curves, gains, mean and levels */
#include "oscChopTables.host.c"
#include "ColloidTables.host.h"
#include "ColloidOsc.host.h"
#include "ColloidVoice.host.h"
#include "ColloidTables.host.c"
#include "ColloidOsc.host.c"
#include "ColloidVoice.host.c"

#define BLK OUTPUT_DMA_SIZE
enum { G_OSC, G_POS, G_W, G_MOT, G_INT, G_SLEW, G_EG, G_AMP, G_SVF, G_SER, G_RTE, G_POST,
       G_PWM, G_WTB, G_MX, G_RND, G_JOIN, G_PWG, G_GATE, G_RETRIG, G_ROLLGATE,
       G_CHANGE, G_PENV, G_NOISE, G_SCAN, G_CHP, G_E1, G_MONO, G_N };
static const char *g_name[G_N] = { "CO-OSC", "CO-POS", "CO-W", "CO-MOT", "CO-INT", "CO-SLEW",
                                   "CO-EG", "CO-AMP", "CO-SVF", "CO-SER", "CO-RTE", "CO-POST",
                                   "CO-PWM", "CO-WTB", "MX-ST", "CO-RND", "CO-JOIN",
                                   "CO-PWG", "CO-GATE", "CO-RETRIG", "CO-ROLLGATE",
                                   "CO-CHANGE", "CO-PENV", "CO-NOISE", "CO-SCAN",
                                   "CO-CHP", "MX-E1", "CO-MONO" };
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

/* S096 P2 F5: the gliding pulse law: A at ph, B at its own phase q, which
 * starts w0 points earlier and glides to w points earlier over the block */
static int32_t ref_pulseG(const int16_t *row, uint32_t ph0, uint32_t inc, unsigned n,
                          uint32_t w, uint32_t w0, int32_t sign)
{
	const uint32_t j = inc - (uint32_t)(((int32_t)w - (int32_t)w0) * 131072);
	const uint32_t q = ph0 - (w0 << 22) + n * j;

	return ref_read(row, ph0 + n * inc) + sign * ref_read(row, q);
}

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
	/* S096 P2 F5: pass 0 a still width (start = end), pass 1 a glide */
	const uint16_t wid0g[4] = { 504u, 11u, 992u, 72u };
	const uint32_t pq0 = (uint32_t)(uint16_t)(int16_t)-20000 | ((uint32_t)(uint16_t)(int16_t)31000 << 16);
	const uint32_t pq1 = (uint32_t)(uint16_t)(int16_t)32767 | ((uint32_t)(uint16_t)(int16_t)-32768 << 16);
	int32_t M[BLK], S[BLK];
	uint32_t p[4];
	int g = pwm ? G_PWM : G_OSC;
	unsigned n, o, pass;

	for (n = 0u; n < 1024u; n++)
		edge[n] = (n & 1u) ? -32768 : 32767;
	for (pass = 0u; pass < (pwm ? 2u : 1u); pass++) {
		const uint16_t *const wid0 = pass ? wid0g : wid;

		memcpy(p, ph, sizeof(p));
		if (pwm) {
			col_pwmPairStore(&rows[0], &p[0], &inc[0], &wid[0], &wid0[0], pq0, M, S, sign);
			col_pwmPairAdd(&rows[2], &p[2], &inc[2], &wid[2], &wid0[2], pq1, M, S, sign);
		} else {
			col_oscPairStore(&rows[0], &p[0], &inc[0], pq0, M, S);
			col_oscPairAdd(&rows[2], &p[2], &inc[2], pq1, M, S);
		}
		for (n = 0u; n < BLK; n++) {
			int32_t m = 0, s = 0, old = 0;
			for (o = 0u; o < 4u; o++) {
				const uint32_t at = ph[o] + n * inc[o];
				const int32_t v = pwm ? ref_pulseG(rows[o], ph[o], inc[o], n, wid[o], wid0[o], sign)
				                      : ref_read(rows[o], at);
				const uint32_t pq = (o < 2u) ? pq0 : pq1;
				m += v;
				s += ref_side(v, (int16_t)((o & 1u) ? (pq >> 16) : (pq & 0xFFFFu)));
				if (pwm)
					old += ref_pulse(rows[o], at, wid[o], sign);
			}
			if (M[n] != m) fail(g, pass ? "mid bus (glide)" : "mid bus", M[n], m);
			if (S[n] != s) fail(g, pass ? "side bus (glide)" : "side bus", S[n], s);
			/* a still width is the S095 shared-fraction pulse to within the
			 * two reads' separate rounding: 1 per oscillator */
			if (pwm && !pass && abs(M[n] - old) > 4)
				fail(g, "still width = the S095 pulse (+-1 per oscillator)", M[n], old);
		}
		for (o = 0u; o < 4u; o++)
			if (p[o] != ph[o] + BLK * inc[o]) fail(g, "phase advance", p[o], ph[o] + BLK * inc[o]);
	}
}

/* CO-PWM (S095 P3 F2): the warp loops against the restated law, store then add
 * (S099 P1 §15.10: pwm Sin dropped, so all three run on Tri rows and tables) */
static void test_warp(void)
{
	const int16_t *rows[3] = { triTable[5], triTable[2], triTable[9] };
	uint32_t ph[3] = { 0x00000000u, 0x7fff1234u, 0xfffff000u };
	const uint32_t inc[3] = { 0x01234567u, 0x00a00000u, 0x0abcdef1u };
	const uint32_t k[3] = { 0u, 70u, 127u };
	const uint32_t pq[3] = { (uint32_t)(uint16_t)(int16_t)-20000, 32767u,
	                         (uint32_t)(uint16_t)(int16_t)-32768 };
	/* S096 P2 F5: the warp loops take one bus, mid then side */
	int32_t MS[2u * BLK];
	int32_t *const M = MS;
	int32_t *const S = MS + BLK;
	uint32_t p[3];
	unsigned n, o;

	/* S096 P2 F5: pass 0 a still width (steps 0), pass 1 a glide from the
	 * start index k0 to k: W, ra, rb step by (end - start) / 32 per sample */
	const uint32_t k0g[3] = { 2u, 68u, 125u };
	unsigned pass;

	for (pass = 0u; pass < 2u; pass++) {
		uint32_t W0[3], A0[3], B0[3];
		int32_t dW[3], dA[3], dB[3];

		for (o = 0u; o < 3u; o++) {
			const uint32_t ks = pass ? k0g[o] : k[o];
			const uint32_t *const tW = oscPwmTriW;
			const uint32_t *const tA = oscPwmTriRa;
			const uint32_t *const tB = oscPwmTriRb;

			W0[o] = tW[ks]; A0[o] = tA[ks]; B0[o] = tB[ks];
			dW[o] = ((int32_t)(tW[k[o]] - tW[ks])) / 32;
			dA[o] = ((int32_t)(tA[k[o]] - tA[ks])) / 32;
			dB[o] = ((int32_t)(tB[k[o]] - tB[ks])) / 32;
		}
		memcpy(p, ph, sizeof(p));
		col_warpStore(rows[0], &p[0], inc[0], W0[0], A0[0], B0[0], dW[0], dA[0], dB[0], pq[0], MS);
		col_warpAdd(rows[1], &p[1], inc[1], W0[1], A0[1], B0[1], dW[1], dA[1], dB[1], pq[1], MS);
		col_warpAdd(rows[2], &p[2], inc[2], W0[2], A0[2], B0[2], dW[2], dA[2], dB[2], pq[2], MS);
		for (n = 0u; n < BLK; n++) {
			int32_t m = 0, sd = 0;
			for (o = 0u; o < 3u; o++) {
				const uint32_t at = ph[o] + n * inc[o];
				const int32_t v = ref_warp(rows[o], at, W0[o] + n * (uint32_t)dW[o],
				                           A0[o] + n * (uint32_t)dA[o], B0[o] + n * (uint32_t)dB[o]);
				m += v;
				sd += ref_side(v, (int16_t)(pq[o] & 0xFFFFu));
			}
			if (M[n] != m) fail(G_PWM, pass ? "warp mid bus (glide)" : "warp mid bus", M[n], m);
			if (S[n] != sd) fail(G_PWM, pass ? "warp side bus (glide)" : "warp side bus", S[n], sd);
		}
		for (o = 0u; o < 3u; o++)
			if (p[o] != ph[o] + BLK * inc[o]) fail(G_PWM, "warp phase advance", p[o], ph[o]);
	}
}

static void test_pos_and_w(void)
{
	unsigned b, i;
	/* CO-POS: spr 0, mot 0 */
	voice_init();
	V.spread = 0.0f; V.motion = 0.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
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
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
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
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
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
		Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
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
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
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
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
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
		Colloid_triggerVoice(&V, 0u, 100u, 75u, 5514u, 1u);
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
	Colloid_triggerVoice(&V, 0u, 100u, 51u, 5514u, 1u);
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
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
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
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	if (Colloid_isQuiet(&V)) fail(G_EG, "sounding voice quiet", 0, 0);
	if (run_until_stage(&V.ampEg, COL_EG_IDLE, 40000u) >= 40000u) fail(G_EG, "stopped hit held", V.ampEg.stage, 0);
	/* sus off while running: A-D-R */
	host_running = 1u; V.sustain = 0u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	if (run_until_stage(&V.ampEg, COL_EG_IDLE, 40000u) >= 40000u) fail(G_EG, "sus off held", V.ampEg.stage, 0);
	/* a retrigger while held: the attack restarts from the current level
	 * (S095 P3 F3: no legato) */
	V.sustain = 1u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	(void)run_until_stage(&V.ampEg, COL_EG_SUSTAIN, 5000u);
	lvl = V.ampEg.level;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	if (V.ampEg.stage != COL_EG_ATTACK || V.ampEg.level != lvl) fail(G_EG, "retrigger level", V.ampEg.level, lvl);
}

/* CO-GATE (S096 P3 C1, plan §2.3; S099 P1 §15 the plain ADSR): the note is
 * the gate (the step, or the length special's window), as from a keyboard;
 * every envelope releases at the gate's end from the stage and level it
 * reached, attack included; sustain holds, and a later sus-off write
 * releases on the next block; no amp-attack minimum. */
static void test_gate(void)
{
	unsigned n, b, g;

	/* the full amp attack's length in blocks, at this attack byte */
	voice_init(); host_running = 1u; V.sustain = 0u;
	V.attack = 60u; V.decay = 0u; V.release = 40u; V.sustainLevel = 0.5f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 200000u, 1u);
	n = 0u;
	while (V.ampEg.stage == COL_EG_ATTACK && n < 4000u) {
		block();
		n++;
	}
	if (V.ampEg.stage != COL_EG_DECAY || n < 3u)
		fail(G_GATE, "reference attack did not end", n, V.ampEg.stage);

	/* a step of g blocks, shorter than the attack: attack for g blocks,
	 * then RELEASE on block g + 1 from the level reached (below 1.0) */
	g = n / 2u;
	voice_init(); host_running = 1u; V.sustain = 0u;
	V.attack = 60u; V.decay = 0u; V.release = 40u; V.sustainLevel = 0.5f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, (uint32_t)g * 32u, 1u);
	for (b = 0u; b < g; b++)
		block();
	if (V.ampEg.stage != COL_EG_ATTACK)
		fail(G_GATE, "short step left the attack before its gate ended", V.ampEg.stage, g);
	block();
	if (V.ampEg.stage != COL_EG_RELEASE)
		fail(G_GATE, "short step did not release at its gate's end", V.ampEg.stage, g + 1u);
	if (V.ampEg.level >= 0.99f)
		fail(G_GATE, "short step played the whole attack", V.ampEg.level, 0.99);

	/* a step longer than the attack: decay and sustain hold to the gate's
	 * end, then release (fill 50 is the same law at half the step) */
	voice_init(); host_running = 1u; V.sustain = 0u;
	V.attack = 60u; V.decay = 0u; V.release = 40u; V.sustainLevel = 0.5f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, (uint32_t)(n + 200u) * 32u, 1u);
	for (b = 0u; b < n + 100u; b++)
		block();
	if (V.ampEg.stage != COL_EG_SUSTAIN && V.ampEg.stage != COL_EG_DECAY)
		fail(G_GATE, "long step ended early", V.ampEg.stage, b);
	for (b = 0u; b < 101u; b++)
		block();
	if (V.ampEg.stage != COL_EG_RELEASE && V.ampEg.stage != COL_EG_IDLE)
		fail(G_GATE, "long step did not release at its end", V.ampEg.stage, b);

	/* sustain holds past a short gate while the transport runs */
	voice_init(); host_running = 1u; V.sustain = 1u;
	V.attack = 0u; V.decay = 0u; V.sustainLevel = 0.5f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 64u, 1u);
	for (b = 0u; b < 50u; b++)
		block();
	if (V.ampEg.stage == COL_EG_RELEASE || V.ampEg.stage == COL_EG_IDLE)
		fail(G_GATE, "sustain did not hold past the gate", V.ampEg.stage, b);
	/* a later sus-off write (the drain's automation) ends the held note:
	 * every envelope releases on the next block */
	V.sustain = 0u;
	block();
	if (V.ampEg.stage != COL_EG_RELEASE || V.fltEg.stage != COL_EG_RELEASE ||
	    V.pitchEg.stage != COL_EG_RELEASE)
		fail(G_GATE, "sus off did not release the held note", V.ampEg.stage, V.fltEg.stage);

	/* a one-block gate: all three envelopes hold one block, then release
	 * together on the next, in mid-attack */
	voice_init(); host_running = 1u; V.sustain = 0u;
	V.attack = 60u; V.decay = 0u; V.release = 40u; V.sustainLevel = 0.5f;
	V.fAttack = 60u; V.fRelease = 40u; V.fSustain = 0.5f;
	V.pAttack = 60u; V.pRelease = 40u; V.pSustain = 0.5f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 32u, 1u);
	block();
	if (V.ampEg.stage != COL_EG_ATTACK || V.fltEg.stage != COL_EG_ATTACK ||
	    V.pitchEg.stage != COL_EG_ATTACK)
		fail(G_GATE, "an envelope left the attack inside the gate", V.fltEg.stage, V.ampEg.stage);
	block();
	if (V.ampEg.stage != COL_EG_RELEASE)
		fail(G_GATE, "amp did not release at the gate's end", V.ampEg.stage, 2);
	if (V.fltEg.stage != COL_EG_RELEASE)
		fail(G_GATE, "filter did not release with the gate", V.fltEg.stage, 2);
	if (V.pitchEg.stage != COL_EG_RELEASE)
		fail(G_GATE, "pitch did not release with the gate", V.pitchEg.stage, 2);
}

/* CO-RETRIG (S096 P3 C1): a retrigger in release restarts the attack from
 * the current level, for all three envelopes. */
static void test_retrig(void)
{
	unsigned b;
	float a, f, p;

	voice_init(); host_running = 1u; V.sustain = 0u;
	V.attack = 20u; V.decay = 40u; V.release = 40u; V.sustainLevel = 0.6f;
	V.fAttack = 20u; V.fDecay = 40u; V.fRelease = 40u; V.fSustain = 0.6f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	for (b = 0u; b < 20000u; b++) {
		block();
		if (V.ampEg.stage == COL_EG_RELEASE && V.ampEg.level > 0.1f)
			break;
	}
	a = V.ampEg.level; f = V.fltEg.level; p = V.pitchEg.level;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	if (V.ampEg.stage != COL_EG_ATTACK || V.ampEg.level < a - 1e-6f)
		fail(G_RETRIG, "amp attack did not restart from its level", V.ampEg.level, a);
	if (V.fltEg.stage != COL_EG_ATTACK || V.fltEg.level < f - 1e-6f)
		fail(G_RETRIG, "filter attack did not restart from its level", V.fltEg.level, f);
	if (V.pitchEg.stage != COL_EG_ATTACK || V.pitchEg.level < p - 1e-6f)
		fail(G_RETRIG, "pitch attack did not restart from its level", V.pitchEg.level, p);
}

/* CO-ROLLGATE (S096 P3 C1, plan §2.2): roll hits inside the window keep the
 * gate; the window's end releases; a carry renews the gate. */
static void test_rollgate(void)
{
	int32_t g;
	unsigned b;

	voice_init(); host_running = 0u; V.sustain = 0u; V.attack = 0u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 1000u, 1u);
	if (V.gateLeft != 1000)
		fail(G_ROLLGATE, "first hit did not open the gate", V.gateLeft, 1000);
	for (b = 0u; b < 3u; b++) {
		const int32_t before = V.gateLeft;

		block();
		if (V.gateLeft > before - 32 || V.gateLeft < before - 32)
			fail(G_ROLLGATE, "one block did not consume one block", V.gateLeft, before);
		g = V.gateLeft;
		Colloid_triggerVoice(&V, 0u, 100u, 63u, 9999u, 0u);
		if (V.gateLeft != g)
			fail(G_ROLLGATE, "a roll retrigger reset the gate", V.gateLeft, g);
	}
	Colloid_gateRelease(&V, 0u);
	if (V.gateLeft != 0)
		fail(G_ROLLGATE, "the window end did not release", V.gateLeft, 0);
	Colloid_gateRenew(&V, 2000u);
	if (V.gateLeft != 2000)
		fail(G_ROLLGATE, "a carry did not renew the gate", V.gateLeft, 2000);
}

/* CO-CHANGE (S096 P3 C1, plan §2.1 rule 6): the Scene/track change release
 * decision, and the paraphonic hold. */
static void test_change(void)
{
	unsigned b;

	voice_init(); host_running = 1u; V.sustain = 1u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	Colloid_changeArm(&V);
	Colloid_changeDecide(&V);
	if (V.gateLeft != 0 || !V.susBreak || V.changeArm)
		fail(G_CHANGE, "a change without a trigger did not release", V.gateLeft, V.susBreak);
	for (b = 0u; b < 4000u && V.ampEg.stage == COL_EG_ATTACK; b++)
		block();
	if (V.ampEg.stage != COL_EG_RELEASE && V.ampEg.stage != COL_EG_IDLE)
		fail(G_CHANGE, "a forced release did not stop the sustain", V.ampEg.stage, 0);

	/* a drained sus = on keeps the note */
	voice_init(); host_running = 1u; V.sustain = 1u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	Colloid_changeArm(&V);
	Colloid_noteSustainWrite(&V, 1u);
	Colloid_changeDecide(&V);
	if (V.gateLeft == 0 || V.susBreak || V.changeArm)
		fail(G_CHANGE, "sus-on step did not continue the note", V.gateLeft, V.susSeen);

	/* a trigger in the new Scene continues */
	voice_init(); host_running = 1u; V.sustain = 1u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	Colloid_changeArm(&V);
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	Colloid_changeDecide(&V);
	if (V.gateLeft == 0 || V.changeArm)
		fail(G_CHANGE, "a trigger did not continue the note", V.gateLeft, V.changeArm);

	/* held MIDI keys keep the note across the change and hold sus with the
	 * transport stopped */
	voice_init(); host_running = 1u; V.sustain = 1u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	Colloid_setMidiHold(&V, 1u);
	Colloid_changeArm(&V);
	Colloid_changeDecide(&V);
	if (V.gateLeft == 0)
		fail(G_CHANGE, "held MIDI keys did not keep the note", V.gateLeft, 0);
	host_running = 0u;
	for (b = 0u; b < 400u; b++)
		block();
	if (V.ampEg.stage == COL_EG_RELEASE || V.ampEg.stage == COL_EG_IDLE)
		fail(G_CHANGE, "midiHold did not sustain a stopped transport", V.ampEg.stage, 0);
}

/* CO-PENV (S096 P3 C2a, plan §3.3): the offset law, its sign and depth, and
 * the gate; pam 63 (the default) leaves the pitch bit-identical. */
static void test_penv(void)
{
	static const uint8_t pam[4] = { 0u, 63u, 64u, 126u };
	double ref;
	float lvl;
	unsigned b, i;

	/* a reference run: pam 63 = no offset */
	host_running = 1u;
	voice_init(); V.sustain = 0u; V.attack = 0u; V.pAttack = 0u;
	V.pDecay = 0u; V.pSustain = 1.0f; V.detune = 0.0f; V.slew = 0u;
	V.pAmount = 63.0f / 127.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	for (b = 0u; b < 10u; b++)
		block();
	ref = (double)V.inc[0];
	lvl = V.pitchEg.level;
	if (lvl < 0.99f)
		fail(G_PENV, "the pitch envelope did not attack", lvl, 1.0);

	for (i = 0u; i < 4u; i++) {
		const double off = ((double)pam[i] - 63.0) / 63.0 * 2.0 * (double)lvl;
		const double want = ref * exp2(off);

		voice_init(); V.sustain = 0u; V.attack = 0u; V.pAttack = 0u;
		V.pDecay = 0u; V.pSustain = 1.0f; V.detune = 0.0f; V.slew = 0u;
		V.pAmount = (float)pam[i] / 127.0f;
		Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
		for (b = 0u; b < 10u; b++)
			block();
		if (fabs((double)V.inc[0] / want - 1.0) > 2e-5)
			fail(G_PENV, "pam offset law", V.inc[0], want);
	}

	/* the extremes are +-24 semitones (x4 / x4^-1) at level 1 */
	if (lvl > 0.99f) {
		voice_init(); V.sustain = 0u; V.attack = 0u; V.pAttack = 0u;
		V.pDecay = 0u; V.pSustain = 1.0f; V.detune = 0.0f; V.slew = 0u;
		V.pAmount = 126.0f / 127.0f;
		Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
		for (b = 0u; b < 10u; b++)
			block();
		if (fabs((double)V.inc[0] / ref - 4.0) > 1e-4)
			fail(G_PENV, "+24 semitones is not x4", V.inc[0], ref);
	}

	/* the gate: a short step releases the pitch envelope with the amp */
	voice_init(); V.sustain = 0u; V.attack = 0u; V.pAttack = 0u;
	V.pDecay = 0u; V.pRelease = 40u; V.detune = 0.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 32u, 1u);
	for (b = 0u; b < 200u; b++)
		block();
	if (V.pitchEg.stage != COL_EG_RELEASE && V.pitchEg.stage != COL_EG_IDLE)
		fail(G_PENV, "the gate did not release the pitch envelope", V.pitchEg.stage, 0);
}

/* CO-NOISE (S096 P3 C2b, plan §3.4): level 0 is bit-identical, the level and
 * pan laws, the tone/width corners and the mono route. */
static int16_t noise_ref[50][BLK];

static void install_table(unsigned frames);   /* defined with test_wtb() */

static void test_noise(void)
{
	unsigned b, i;
	int differ;
	float gL, gR;

	/* level 0: the output does not depend on tone, width or pan */
	host_rng = 0x13579BDFu;
	voice_init(); host_running = 1u; V.nLevel = 0u;
	V.nTone = 0u; V.nWidth = 0u; V.nPan = 0u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	for (b = 0u; b < 50u; b++) {
		block();
		for (i = 0u; i < BLK; i++)
			noise_ref[b][i] = L[i];
	}
	host_rng = 0x13579BDFu;
	voice_init(); host_running = 1u; V.nLevel = 0u;
	V.nTone = 127u; V.nWidth = 0u; V.nPan = 127u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	for (b = 0u; b < 50u; b++) {
		block();
		for (i = 0u; i < BLK; i++)
			if (L[i] != noise_ref[b][i]) {
				fail(G_NOISE, "level 0 is not bit-identical", L[i], noise_ref[b][i]);
				b = 50u;
				break;
			}
	}

	/* the level changes the output */
	host_rng = 0x13579BDFu;
	voice_init(); host_running = 1u; V.nLevel = 127u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	differ = 0;
	for (b = 0u; b < 50u; b++) {
		block();
		for (i = 0u; i < BLK; i++)
			if (L[i] != noise_ref[b][i])
				differ = 1;
	}
	if (!differ)
		fail(G_NOISE, "level 127 changed nothing", 0, 0);

	/*
	 * S096 P3 assessment F7: the noise is audible at full level and its gain
	 * is COL_HEADROOM x the drive gain, whatever the oscillator count, the
	 * table or the pwm stack. The first form scaled it by the stack's
	 * norm x sqrt(N) (a second 1/32768): the difference from level 0 was a
	 * fraction of one output step.
	 */
	{
		double acc = 0.0;
		unsigned cnt = 0u;

		host_rng = 0x13579BDFu;
		voice_init(); host_running = 1u; V.nLevel = 127u;
		Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
		for (b = 0u; b < 50u; b++) {
			block();
			if (b < 10u)
				continue;          /* past the attack */
			for (i = 0u; i < BLK; i++) {
				const double d = (double)L[i] - (double)noise_ref[b][i];

				acc += d * d;
				cnt++;
			}
		}
		if (sqrt(acc / (double)cnt) < 1000.0)
			fail(G_NOISE, "level 127 noise is not audible (RMS difference)",
			     sqrt(acc / (double)cnt), 1000.0);
	}
	voice_init(); host_running = 1u; V.nLevel = 127u; V.nPan = 63u;
	block();
	if (fabsf(V.nGL * 32768.0f - 0.5f) > 1e-4f || fabsf(V.nGR * 32768.0f - 0.5f) > 1e-4f)
		fail(G_NOISE, "noise gain at drv 0 is not COL_HEADROOM", V.nGL * 32768.0f, V.nGR * 32768.0f);
	V.drive = 1.0f;
	block();
	if (fabsf(V.nGL * 32768.0f - 8.0f) > 1e-2f)
		fail(G_NOISE, "noise gain does not follow the drive (x16)", V.nGL * 32768.0f, 8.0f);
	V.drive = 0.0f;
	install_table(0u);
	Colloid_setOscType(&V, COL_TYP_WTB);
	block();
	if (fabsf(V.nGL * 32768.0f - 0.5f) > 1e-4f)
		fail(G_NOISE, "noise silent under wtb with no table", V.nGL * 32768.0f, 0.5f);
	Colloid_setOscType(&V, COL_TYP_WAV);

	/* the tone and width corners, once slewed */
	voice_init(); host_running = 1u; V.nLevel = 127u;
	V.nTone = 63u; V.nWidth = 127u;
	block();
	for (b = 0u; b < 200u; b++)
		block();
	if (fabsf(V.nHSt) > 0.1f || fabsf(V.nLSt - COL_CUT_ST_MAX) > 0.1f)
		fail(G_NOISE, "tone 0 / width 127 corners", V.nHSt, V.nLSt);
	/* form B: width 127 has no make-up (nlv 127, centre pan, drv 0: 0.5) */
	if (fabsf(V.nGL * 32768.0f - 0.5f) > 1e-4f || fabsf(V.nGR * 32768.0f - 0.5f) > 1e-4f)
		fail(G_NOISE, "width 127 make-up is not 1", V.nGL * 32768.0f, V.nGR * 32768.0f);
	V.nTone = 126u; V.nWidth = 127u;
	for (b = 0u; b < 200u; b++)
		block();
	if (fabsf(V.nHSt - COL_CUT_ST_MAX) > 0.1f)
		fail(G_NOISE, "tone +1 HP corner", V.nHSt, COL_CUT_ST_MAX);
	V.nTone = 0u; V.nWidth = 127u;
	for (b = 0u; b < 200u; b++)
		block();
	if (fabsf(V.nLSt) > 0.1f)
		fail(G_NOISE, "tone -1 LP corner", V.nLSt, 0.0f);
	/* width 0 closes both corners on the centre; the width make-up keeps
	 * it audible (user 2026-11-17: width 0 is not "off"): x6.07 */
	V.nTone = 63u; V.nWidth = 0u;
	for (b = 0u; b < 400u; b++)
		block();
	if (fabsf(V.nHSt - COL_NOISE_ST_CENTRE) > 0.5f ||
	    fabsf(V.nLSt - COL_NOISE_ST_CENTRE) > 0.5f)
		fail(G_NOISE, "width 0 centre", V.nHSt, V.nLSt);
	if (fabsf(V.nGL * 32768.0f - 0.5f * (1.0f + COL_NOISE_MAKEUP_W)) > 1e-3f)
		fail(G_NOISE, "width 0 make-up", V.nGL * 32768.0f, 0.5f * (1.0f + COL_NOISE_MAKEUP_W));
	/* width 64: the law 1 + 5.07 (1 - w)^2 */
	V.nWidth = 64u;
	block();
	{
		const float nw = 1.0f - 64.0f / 127.0f;
		const float want = 0.5f * (1.0f + COL_NOISE_MAKEUP_W * nw * nw);

		if (fabsf(V.nGL * 32768.0f - want) > 1e-3f)
			fail(G_NOISE, "width 64 make-up", V.nGL * 32768.0f, want);
	}
	/*
	 * Note tracking (user 2026-11-17): 85 % at width 0, none at width 127.
	 * The test voice plays C3 (coa 48, note 63: the reference). An octave up
	 * moves a width-0 band by 0.85 x 12 = 10.2 semitones; at width 127 the
	 * corners do not move.
	 */
	V.nWidth = 0u;
	V.note = 75u;
	for (b = 0u; b < 400u; b++)
		block();
	if (fabsf(V.nHSt - (COL_NOISE_ST_CENTRE + 10.2f)) > 0.5f ||
	    fabsf(V.nLSt - (COL_NOISE_ST_CENTRE + 10.2f)) > 0.5f)
		fail(G_NOISE, "width 0 tracks the note by 85 %", V.nHSt, V.nLSt);
	V.nWidth = 127u;
	for (b = 0u; b < 400u; b++)
		block();
	if (fabsf(V.nHSt) > 0.1f || fabsf(V.nLSt - COL_CUT_ST_MAX) > 0.1f)
		fail(G_NOISE, "width 127 does not track", V.nHSt, V.nLSt);
	V.note = 63u;

	/* the pan law (one pan rule: stored 63 = centre) */
	voice_init(); host_running = 1u; V.nLevel = 127u; V.nPan = 0u;
	V.nTone = 63u; V.nWidth = 127u;
	block();
	gL = V.nGL; gR = V.nGR;
	if (gR != 0.0f || gL <= 0.0f)
		fail(G_NOISE, "pan 0 is not full left", gL, gR);
	V.nPan = 127u;
	block();
	if (V.nGL != 0.0f || V.nGR <= 0.0f)
		fail(G_NOISE, "pan 127 is not full right", V.nGL, V.nGR);

	/* the mono route: L == R with the noise on */
	voice_init(); host_running = 1u; V.nLevel = 127u;
	V.route = 1u + COL_FT_LP; V.nTone = 40u; V.nWidth = 60u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	for (b = 0u; b < 100u; b++) {
		block();
		for (i = 0u; i < BLK; i++)
			if (L[i] != V.right[i]) {
				fail(G_NOISE, "mono noise L != R", L[i], V.right[i]);
				b = 100u;
				break;
			}
	}
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
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
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
	Colloid_triggerVoice(&V, 0u, 127u, 63u, 5514u, 1u);
	for (b = 0u; b < 20000u; b++) {
		const float t1A = V.svfA.t1, t2A = V.svfA.t2, t3A = V.svfA.t3;
		/* S101 P2 CL5: the ramp spans two blocks; the block between carries
		 * its start on by one block's step (col_svfCarry()) */
		const float a1A = V.svfA.a1, a2A = V.svfA.a2, a3A = V.svfA.a3;
		const float d1A = V.svfA.da1, d2A = V.svfA.da2, d3A = V.svfA.da3;
		if ((b % 7u) == 0u) {
			V.cut = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.cutOfs = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.res = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.resOfs = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.envOfs = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.driveOfs = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.filtType = (uint8_t)(golden_rand(&s) % COL_FT_COUNT);
			V.route = (uint8_t)(golden_rand(&s) % COL_RTE_COUNT);
			V.fAmount = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.drive = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.vcaDrive = (uint8_t)(golden_rand(&s) & 127u);
			V.tone = (uint8_t)(golden_rand(&s) & 127u);
		}
		if ((b % 300u) == 0u) Colloid_triggerVoice(&V, 0u, 127u, (uint8_t)(40u + (golden_rand(&s) % 50u)), 5514u, 1u);
		block();
		/* S101 P2 CL5: the control ran this block when nPhase was 1 before
		 * col_noiseControl() toggled it (0 now); then the ramp starts on the
		 * last target, else it carries on from where the last block ended */
		if (V.nPhase == 0u) {
			if (V.svfA.a1 != t1A || V.svfA.a2 != t2A || V.svfA.a3 != t3A)
				fail(G_SVF, "ramp start != last target", V.svfA.a1, t1A);
		} else if (V.svfA.a1 != a1A + (float)BLK * d1A || V.svfA.a2 != a2A + (float)BLK * d2A ||
		           V.svfA.a3 != a3A + (float)BLK * d3A || V.svfA.t1 != t1A) {
			fail(G_SVF, "carry block: ramp not continued", V.svfA.a1, a1A + (float)BLK * d1A);
		}
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
	/* S096 P2 F3: off passes the input exactly; UBP is unity at its centre */
	{
		ColSvf f;
		float ic1, ic2;
		uint32_t r = 7u;
		col_svfInit(&f, 0.5f, 0.8f, COL_FT_OFF);
		ic1 = f.ic1; ic2 = f.ic2;
		for (i = 0u; i < 2000u; i++) {
			const float x = (float)(int32_t)golden_rand(&r) * (1.0f / 2147483648.0f);
			const float v3 = x - ic2, v1 = f.a1 * ic1 + f.a2 * v3, v2 = ic2 + f.a2 * ic1 + f.a3 * v3;
			const float y = f.mH * x + f.mB * v1 + f.mL * v2;
			ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
			if (y != x) { fail(G_SVF, "off is the input", y, x); break; }
		}
	}
	{
		static const float resList[2] = { 0.2f, 0.9f };
		unsigned q;
		for (q = 0u; q < 2u; q++) {
			ColSvf f;
			float ic1, ic2, peak = 0.0f, hz;
			col_svfInit(&f, 0.5f, resList[q], COL_FT_UBP);
			hz = COL_CUT_BASE_HZ * powf(2.0f, f.cutSt / 12.0f);
			ic1 = f.ic1; ic2 = f.ic2;
			for (i = 0u; i < 40000u; i++) {
				const float x = 0.5f * sinf(2.0f * 3.14159265f * hz * (float)i / REAL_FS);
				const float v3 = x - ic2, v1 = f.a1 * ic1 + f.a2 * v3, v2 = ic2 + f.a2 * ic1 + f.a3 * v3;
				const float y = f.mH * x + f.mB * v1 + f.mL * v2;
				ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
				if (i > 30000u && fabsf(y) > peak) peak = fabsf(y);
			}
			if (fabsf(peak - 0.5f) > 0.01f) fail(G_SVF, "UBP unity at the centre", peak, resList[q]);
		}
	}
	/* S096 P2 F2: a full-depth envelope sweeps the whole range from either end */
	{
		ColSvf f;
		col_svfInit(&f, 0.0f, 0.3f, COL_FT_LP);
		for (i = 0u; i < 200u; i++) col_svfControl(&f, 0.0f, 0.3f, COL_FT_LP, COL_FENV_ST);
		if (fabsf(f.cutSt - COL_CUT_ST_MAX) > 0.01f) fail(G_SVF, "full +fam from cut 0 reaches the top", f.cutSt, COL_CUT_ST_MAX);
		col_svfInit(&f, 1.0f, 0.3f, COL_FT_LP);
		for (i = 0u; i < 200u; i++) col_svfControl(&f, 1.0f, 0.3f, COL_FT_LP, -COL_FENV_ST);
		if (fabsf(f.cutSt) > 0.01f) fail(G_SVF, "full -fam from cut 1 reaches the bottom", f.cutSt, 0);
	}
}

static void test_ser(void)
{
	unsigned b, i;
	voice_init(); host_running = 1u;
	V.route = 1u + COL_FT_LP; V.spread = 1.0f; V.motion = 1.0f;
	V.osc.midiFreq = (uint16_t)((84u << 8) | 63u);
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	for (b = 0u; b < 100u; b++) {
		block();
		for (i = 0u; i < BLK; i++)
			if (L[i] != V.right[i]) { fail(G_SER, "series L != R", L[i], V.right[i]); break; }
	}
}

/*
 * CO-MONO (S103 P6, S103_P6_COLLOID_TWEAKS.md §1.5): the mono route's
 * filter mix.
 *
 * The frozen loop below is col_renderSer() exactly as committed at b79707b3
 * (S103 P5 tree, before S103 P6), renamed. It is the reference for the
 * bit-identity law: spr 63 / mot 0 must render the same int16 samples.
 * Method (every law): a voice is run for many blocks in the configuration,
 * so its states and its bus are a real block's; then the block's bus is
 * rendered again from one snapshot by two loops (or one loop on two
 * snapshots that differ in one place), each from the same RNG state.
 */
static __attribute__((noinline)) void col_renderSerS102(ColloidVoice *voice, int16_t *outL)
{
	ColSvf *const A = &voice->svfA;
	ColSvf *const B = &voice->svfB;
	const int32_t *const busM = voice->busM;
	int16_t *const outR = voice->right;
	const float gin = voice->inGain;
	const float g2 = voice->inGainR;
	const float pg = voice->postGain, pmk = voice->postMakeup, pwet = voice->postWet;
	const float tc = voice->toneC;
	const float ampStep = (voice->ampEg.level - voice->ampLast)
	                    * (COL_OUT_SCALE / (float)OUTPUT_DMA_SIZE);
	const float da1A = A->da1, da2A = A->da2, da3A = A->da3;
	const float da1B = B->da1, da2B = B->da2, da3B = B->da3;
	/* the inter-stage gain folded into A's mix (exact at g2 = 1) */
	const float mHA = A->mH * g2, mBA = A->mB * g2, mLA = A->mL * g2;
	const float mHB = B->mH, mBB = B->mB, mLB = B->mL;
	/* S096 P3 C2b: the noise, mono (the pan has no effect in a mono route,
	 * as spr/mot): one gain into the first drive. */
	const float nG1 = voice->nG1, nG2 = voice->nG2;
	const float nGM = 0.5f * (voice->nGL + voice->nGR);
	float a1A = A->a1, a2A = A->a2, a3A = A->a3, ic1A = A->ic1, ic2A = A->ic2;
	float a1B = B->a1, a2B = B->a2, a3B = B->a3, ic1B = B->ic1, ic2B = B->ic2;
	float nHpZ = voice->nHpZ, nLpZ = voice->nLpZ;
	float amp = voice->ampLast * COL_OUT_SCALE;
	float tz = voice->toneZL;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		/* the RNG in int16 units: COL_NOISE_STACK is folded into nGL / nGR
		 * (the one-poles are linear), one multiply less per sample */
		const float nz = (float)col_rngRead();
		/* form B, as col_renderPar() */
		const float nv = (nz - nHpZ) * nG1;
		const float nlpo = nv + nHpZ;
		const float nhp = nz - nlpo;
		const float nv2 = (nhp - nLpZ) * nG2;
		const float nOut = nv2 + nLpZ;
		float v1, v2, v3, y;
		float x;
		int16_t o;

		nHpZ = nlpo + nv;
		nLpZ = nOut + nv2;
		x = col_drive((float)busM[n] * gin + nOut * nGM);

		v3 = x - ic2A;
		v1 = a1A * ic1A + a2A * v3;
		v2 = ic2A + a2A * ic1A + a3A * v3;
		ic1A = 2.0f * v1 - ic1A;
		ic2A = 2.0f * v2 - ic2A;
		y = col_drive(mHA * x + mBA * v1 + mLA * v2);

		v3 = y - ic2B;
		v1 = a1B * ic1B + a2B * v3;
		v2 = ic2B + a2B * ic1B + a3B * v3;
		ic1B = 2.0f * v1 - ic1B;
		ic2B = 2.0f * v2 - ic2B;
		y = mHB * y + mBB * v1 + mLB * v2;

		/* S095 P3 F10: overdrive, the amp, then the tone (once: mono) */
		y = col_post(y, pg, pmk, pwet) * amp;
		tz = y + tc * (tz - y);
		o = (int16_t)__SSAT((int32_t)tz, 16);
		outL[n] = o;
		outR[n] = o;
		a1A += da1A; a2A += da2A; a3A += da3A;
		a1B += da1B; a2B += da2B; a3B += da3B;
		amp += ampStep;
	}
	A->ic1 = ic1A; A->ic2 = ic2A;
	B->ic1 = ic1B; B->ic2 = ic2B;
	voice->nHpZ = nHpZ;
	voice->nLpZ = nLpZ;
	/* both channels' tone states follow, so a change to str starts smooth */
	voice->toneZL = tz;
	voice->toneZR = tz;
}


static ColloidVoice monoS1, monoS2;
static int16_t monoO1[BLK], monoO2[BLK];

/* a sounding mono voice: rte LP, drive, offsets, noise and resonance on */
static void mono_setup(float spr, float mot)
{
	unsigned b;

	voice_init(); host_running = 1u;
	V.route = 1u + COL_FT_LP; V.filtType = COL_FT_BP;
	V.res = 0.5f; V.cut = 0.45f; V.drive = 0.3f;
	V.cutOfs = (63.0f + 15.0f) / 127.0f; V.driveOfs = (63.0f + 10.0f) / 127.0f;
	V.nLevel = 40u;
	V.spread = spr; V.motion = mot;
	V.osc.midiFreq = (uint16_t)((60u << 8) | 63u);
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	for (b = 0u; b < 120u; b++)
		block();
}

/* render the last block's bus again from V's state, with loop fn, into out */
static void mono_render(ColloidVoice *S, void (*fn)(ColloidVoice *, int16_t *),
                        int16_t *out, uint32_t rng0)
{
	host_rng = rng0;
	fn(S, out);
}

static int mono_diff(int tol)
{
	unsigned i;
	int worst = 0;

	for (i = 0u; i < BLK; i++) {
		const int d = abs((int)monoO1[i] - (int)monoO2[i]);

		if (d > worst)
			worst = d;
	}
	return worst > tol ? worst : 0;
}

static void test_mono(void)
{
	uint32_t r0;
	unsigned b;
	int d;

	/* 1. spr 63, mot 0: the S102 loop bit for bit, over 60 blocks */
	mono_setup(63.0f / 127.0f, 0.0f);
	if (V.monoCX != 0.0f || V.monoOA != 0.0f || V.monoOB != 1.0f ||
	    V.monoCXStep != 0.0f || V.monoOAStep != 0.0f || V.monoOBStep != 0.0f)
		fail(G_MONO, "spr 63 / mot 0 is not the plain chain (cx, oa, ob)", V.monoCX, V.monoOB);
	for (b = 0u; b < 60u; b++) {
		block();
		r0 = host_rng;
		monoS1 = V; monoS2 = V;
		mono_render(&monoS1, col_renderSer, monoO1, r0);
		mono_render(&monoS2, col_renderSerS102, monoO2, r0);
		if ((d = mono_diff(0)) != 0) { fail(G_MONO, "spr 63 / mot 0 differs from S102", d, 0); break; }
	}

	/* 2. spr 0 (serial and parallel): A only, B does not matter */
	{
		const float mots[2] = { 0.0f, 1.0f };
		unsigned k;

		for (k = 0u; k < 2u; k++) {
			mono_setup(0.0f, mots[k]);
			r0 = host_rng;
			monoS1 = V; monoS2 = V;
			col_svfInit(&monoS2.svfB, 0.9f, 0.2f, COL_FT_HP);   /* another B */
			monoS2.svfB.ic1 = 0.3f; monoS2.svfB.ic2 = -0.2f;
			mono_render(&monoS1, col_renderSer, monoO1, r0);
			mono_render(&monoS2, col_renderSer, monoO2, r0);
			if ((d = mono_diff(1)) != 0)
				fail(G_MONO, k ? "spr 0, parallel: B is heard" : "spr 0, serial: B is heard", d, 0);
		}
		/* 3. spr 127: B only, A does not matter (B fed its own drive) */
		for (k = 0u; k < 2u; k++) {
			mono_setup(1.0f, mots[k]);
			r0 = host_rng;
			monoS1 = V; monoS2 = V;
			col_svfInit(&monoS2.svfA, 0.9f, 0.2f, COL_FT_HP);   /* another A */
			monoS2.svfA.ic1 = 0.3f; monoS2.svfA.ic2 = -0.2f;
			mono_render(&monoS1, col_renderSer, monoO1, r0);
			mono_render(&monoS2, col_renderSer, monoO2, r0);
			if ((d = mono_diff(1)) != 0)
				fail(G_MONO, k ? "spr 127, parallel: A is heard" : "spr 127, serial: A is heard", d, 0);
		}
	}

	/* 4. parallel, identical filters (zero offsets, drive 0, A's type =
	 *    rte's): spr 63 is sqrt 2 x the spr 0 output (equal power, Q3) */
	{
		unsigned i;
		int bad = 0;

		voice_init(); host_running = 1u;
		V.route = 1u + COL_FT_LP; V.filtType = COL_FT_LP;
		V.res = 0.3f; V.cut = 0.45f; V.drive = 0.0f; V.vcaDrive = 0u; V.tone = 127u;
		V.spread = 63.0f / 127.0f; V.motion = 1.0f;
		V.osc.midiFreq = (uint16_t)((60u << 8) | 63u);
		Colloid_triggerVoice(&V, 0u, 60u, 63u, 5514u, 1u);
		for (b = 0u; b < 200u; b++)
			block();
		r0 = host_rng;
		monoS1 = V; monoS2 = V;
		/* the spr 0 twin: A alone (oa 1, ob 0, cx 1), at rest */
		monoS2.monoOA = 1.0f / monoS2.inGainR; monoS2.monoOB = 0.0f; monoS2.monoCX = 1.0f;
		monoS2.monoOAStep = monoS2.monoOBStep = monoS2.monoCXStep = 0.0f;
		mono_render(&monoS1, col_renderSer, monoO1, r0);
		mono_render(&monoS2, col_renderSer, monoO2, r0);
		for (i = 0u; i < BLK; i++)
			if (abs(monoO2[i]) < 20000 &&
			    fabs((double)monoO1[i] - 1.41421356 * (double)monoO2[i]) > 3.0)
				bad++;
		if (bad) fail(G_MONO, "parallel identical filters at spr 63 != sqrt 2 x one", bad, 0);
		if (V.monoDry != 1.0f) fail(G_MONO, "dro 0: B's dry gain is not A's", V.monoDry, 1.0);
	}

	/* 5. the law's corners (col_monoMixControl()): (cx, oa x g2, ob) */
	{
		static const struct { float spr, mot, cx, oa, ob; } c[] = {
			{ 0.0f,          0.0f, 0.0f, 1.0f,        0.0f },
			{ 63.0f / 127.0f, 0.0f, 0.0f, 0.0f,        1.0f },
			{ 1.0f,          0.0f, 1.0f, 0.0f,        1.0f },
			{ 0.0f,          1.0f, 1.0f, 1.0f,        0.0f },
			{ 63.0f / 127.0f, 1.0f, 1.0f, 0.70710678f, 0.70710678f },
			{ 1.0f,          1.0f, 1.0f, 0.0f,        1.0f },
		};
		unsigned k;

		for (k = 0u; k < sizeof(c) / sizeof(c[0]); k++) {
			voice_init();
			V.route = 1u + COL_FT_LP;
			V.spread = c[k].spr; V.motion = c[k].mot;
			V.inGainR = 1.7f;
			col_monoMixControl(&V, 1.0f, 1.7f);
			if (fabsf(V.monoCX + 32.0f * V.monoCXStep - c[k].cx) > 1e-5f ||
			    fabsf((V.monoOA + 32.0f * V.monoOAStep) * 1.7f - c[k].oa) > 1e-5f ||
			    fabsf(V.monoOB + 32.0f * V.monoOBStep - c[k].ob) > 1e-5f)
				fail(G_MONO, "a corner of the mono law", k, c[k].ob);
			if (fabsf(V.monoDry - 1.7f) > 1e-6f)
				fail(G_MONO, "monoDry = dgB / dgL", V.monoDry, 1.7);
		}
	}

	/* 6. the ramps land: a 0 -> 127 spr step in one block reaches its
	 *    targets at the block's end; a fast sweep stays finite */
	{
		float tcx, toa, tob;

		mono_setup(0.0f, 0.3f);
		V.spread = 1.0f;
		Colloid_calcVoiceAsync(&V);
		tcx = V.monoCX + 32.0f * V.monoCXStep;
		toa = V.monoOA + 32.0f * V.monoOAStep;
		tob = V.monoOB + 32.0f * V.monoOBStep;
		Colloid_calcVoiceSyncBlock(&V, L, BLK);
		if (fabsf(V.monoCX - tcx) > 1e-5f || fabsf(V.monoOA - toa) > 1e-5f ||
		    fabsf(V.monoOB - tob) > 1e-5f || fabsf(tcx - V.monoCX) > 1e-5f ||
		    V.monoCX < 0.5f)
			fail(G_MONO, "a spr step does not land at the block's end", V.monoCX, tcx);
		for (b = 0u; b < 2000u; b++) {
			V.spread = (b & 1u) ? 1.0f : 0.0f;
			V.motion = (float)(b % 128u) / 127.0f;
			block();
			if (!isfinite(V.monoCX) || !isfinite(V.monoOA) || !isfinite(V.monoOB) ||
			    !isfinite(V.svfB.ic1) || !isfinite(V.svfA.ic1)) {
				fail(G_MONO, "a fast spr / mot sweep went non-finite", b, 0);
				break;
			}
		}
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
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
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
		Colloid_triggerVoice(&V, 0u, 127u, 63u, 5514u, 1u);
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
	Colloid_triggerVoice(&V, 0u, 127u, 63u, 5514u, 1u);
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
/* the chop law, restated (S099 P1): one oscillator's y at sample n of the
 * block, from its block-start phase, increment, end / start widths and row */
static int32_t ref_chopY(const int16_t *row, const int16_t *curve, uint32_t ph0,
                         uint32_t inc, unsigned n, uint16_t d, uint16_t d0, int16_t g14)
{
	const uint32_t p = ph0 + n * inc;
	const uint32_t j = (inc << 1) - (uint32_t)(((int32_t)d - (int32_t)d0) * 131072);
	const uint32_t q = (ph0 << 1) - ((uint32_t)d0 << 22) + n * j;
	const int32_t dd = row[(p >> 21) & 1023u] - row[q >> 22];
	const int32_t g = (int32_t)(((int64_t)(dd * 4) * g14) >> 16) + (int32_t)(((p << 1) - q) >> 16);
	const int32_t v = curve[(p + 0x20000000u) >> 22];

	return (int32_t)(((int64_t)g * v) >> 16);
}

static void check_pwm_form(uint8_t form)
{
	uint32_t ph[COL_OSC_MAX];
	unsigned n, i;
	/* S099 P1: a chop form's bus starts at minus the stack's mean
	 * (col_chopBlock()'s start pass), rebuilt here from the widths */
	int32_t dcM0 = 0, dcM1 = 0;

	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_PWM);
	V.osc.waveform = form; V.osc.position = 40u; V.posSpread = 0.6f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	block();
	Colloid_calcVoiceAsync(&V);
	memcpy(ph, V.phase, sizeof(ph));
	Colloid_calcVoiceSyncBlock(&V, L, BLK);
	if (COL_PWM_IS_CHOP(form)) {
		const int16_t *const dcT = oscChopDcRsQ15;    /* S099 P2: no REx */
		const int16_t *const relT = oscChopRelRsQ15;

		for (i = 0u; i < COL_PWM_COUNT; i++) {
			const int32_t rel = relT[V.pwPos[i] & 127u];

			dcM0 += ((int32_t)dcT[V.pwPos0[i] & 127u] * rel) >> 16;
			dcM1 += ((int32_t)dcT[V.pwPos[i] & 127u] * rel) >> 16;
		}
	}
	for (n = 0u; n < BLK; n++) {
		int32_t m = 0;
		/* S095 P5: each form renders its own count (6 Tri, 6 Saw/Rec
		 * since S096 P2 F5) */
		for (i = 0u; i < COL_PWM_FORM_COUNT(form); i++) {
			const uint32_t at = ph[i] + n * V.inc[i];
			const uint32_t k = V.pwPos[i] & 127u;
			int32_t v;
			if (form == COL_PWM_TRI)
				v = ref_warp(V.row[i], at, oscPwmTriW[k], oscPwmTriRa[k], oscPwmTriRb[k]);
			else if (COL_PWM_IS_CHOP(form)) {
				/* S099 P1: the restated chop; the voice's bus also holds the
				 * mean's start pass, so the check is the pairs' sum less
				 * that pass (rebuilt below) */
				const int16_t *const curve = oscChopCurveRs;
				const int16_t *const relT = oscChopRelRsQ15;
				const uint32_t r = (uint32_t)(V.row[i] - sawTable[0]) >> 10;
				const int32_t y = ref_chopY(V.row[i], curve, ph[i], V.inc[i], n,
				                            V.pwD[i], V.pwD0[i], oscChopGainQ14[r]);

				v = (int32_t)(((int64_t)y * relT[k]) >> 16);
			} else   /* S096 P2 F5: the gliding pulse, from pwD0 to pwD */
				v = ref_pulseG(V.row[i], ph[i], V.inc[i], n, V.pwD[i], V.pwD0[i],
				               (form == COL_PWM_SAW) ? 1 : -1);
			m += v;
		}
		if (COL_PWM_IS_CHOP(form))
			m += (-(dcM0 * 32) + (int32_t)n * (dcM0 - dcM1)) >> 5;
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
		Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
		for (i = 0u; i < COL_OSC_MAX; i++)
			if (V.pwPos[i] > 127u) fail(G_PWM, "width outside 0..127", V.pwPos[i], 127);
	}
	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_PWM);
	V.osc.waveform = COL_PWM_REC;
	V.osc.position = 0u; V.posSpread = 0.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
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
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
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
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	for (i = 0u; i < COL_OSC_MAX; i++)
		if (V.frame[i] >= 8u) fail(G_WTB, "frame outside", V.frame[i], 8);
	/* an empty catalogue: silence */
	install_table(0u);
	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_WTB);
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	for (b = 0u; b < 50u; b++) {
		block();
		for (i = 0u; i < BLK; i++)
			if (L[i] != 0 || V.right[i] != 0) { fail(G_WTB, "empty catalogue not silent", L[i], V.right[i]); b = 50u; break; }
	}
}

/* CO-SCAN (S096 P3 C2c, plan §3.6): the two-frame blend law, the fold, the
 * F = 1 frame and the wtb <-> wts landing. */
static int32_t ref_scan(const int16_t *row, uint32_t ph, uint32_t g)
{
	const int32_t a = ref_read(row, ph);
	const int32_t b = ref_read(row + 1024, ph);

	return a + (int32_t)(((int64_t)(b - a) * (int32_t)(g >> 16)) >> 15);
}

static void test_scan(void)
{
	/* each row is a two-frame table: frame A then frame B 2,048 B on */
	static int16_t pairA[2048], pairB[2048];
	int32_t MS[2 * BLK];             /* the 64-word bus: mid then side */
	int32_t *const M = MS, *const S = MS + BLK;
	uint32_t p[2], inc[2], g[2];
	uint32_t p0[2];
	int32_t dg[2];
	const int16_t *rows[2];
	unsigned b, i, n;

	for (i = 0u; i < 1024u; i++) {
		pairA[i] = (int16_t)(24000.0 * sin(6.283185307 * i / 1024.0));
		pairA[i + 1024u] = (int16_t)(-20000.0 * cos(6.283185307 * i / 512.0));
		pairB[i] = (int16_t)(18000.0 * cos(6.283185307 * i / 256.0));
		pairB[i + 1024u] = (int16_t)(-14000.0 * sin(6.283185307 * i / 128.0));
	}
	/* the loop's own law: two adjacent frames, g gliding in Q31 */
	rows[0] = pairA; rows[1] = pairB;
	p[0] = 0x01234567u; p[1] = 0x89ABCDEFu;
	p0[0] = p[0]; p0[1] = p[1];
	inc[0] = 0x01000000u; inc[1] = 0x00A00000u;
	g[0] = 0x10000000u; g[1] = 0x70000000u;
	dg[0] = 0x04000000; dg[1] = -0x02000000;
	col_scanPairStore(rows, p, inc, g, dg, 0x1234ABCDu, MS);
	for (n = 0u; n < BLK; n++) {
		int32_t m = 0, s = 0;
		for (i = 0u; i < 2u; i++) {
			const uint32_t gg = g[i] + (uint32_t)n * (uint32_t)dg[i];
			const int32_t v = ref_scan(rows[i], p0[i] + n * inc[i], gg);

			m += v;
			s += ref_side(v, (int16_t)((i & 1u) ? (0x1234ABCDu >> 16)
			                                    : (0x1234ABCDu & 0xFFFFu)));
		}
		if (M[n] != m) fail(G_SCAN, "scan mid bus", M[n], m);
		if (S[n] != s) fail(G_SCAN, "scan side bus", S[n], s);
	}
	if (p[0] != 0x01234567u + BLK * inc[0] || p[1] != 0x89ABCDEFu + BLK * inc[1])
		fail(G_SCAN, "scan phase advance", p[0], 0);

	/* the voice under wts: the position lands on pos's frame */
	install_table(8u);
	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_WTS);
	V.osc.waveform = 0u; V.osc.position = 0u; V.posSpread = 0.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	block();
	for (i = 0u; i < COL_SCAN_COUNT; i++) {
		if (V.frame[i] != 0u || V.pwD[i] != 0u) fail(G_SCAN, "pos 0 did not land", V.frame[i], V.pwD[i]);
		if (V.row[i] != V.osc.sampleCacheData) fail(G_SCAN, "pos 0 row", i, 0);
	}
	/* pos 127 lands on the top frame, represented as F-2 + full B */
	V.osc.position = 127u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	block();
	for (i = 0u; i < COL_SCAN_COUNT; i++) {
		if (V.frame[i] != 6u || V.pwD[i] != 32768u)
			fail(G_SCAN, "pos 127 top frame", V.frame[i] | (V.pwD[i] << 8), 6u | (32768u << 8));
	}
	/* the fold keeps every frame inside the table at psp 1 */
	V.posSpread = 1.0f;
	for (b = 0u; b < 200u; b++) {
		block();
		for (i = 0u; i < COL_SCAN_COUNT; i++) {
			if (V.frame[i] >= 7u) fail(G_SCAN, "frame outside the table", V.frame[i], 7);
			if (V.pwD[i] > 32768u) fail(G_SCAN, "blend outside Q15", V.pwD[i], 32768);
		}
	}
	/* the position moves at most one frame per block after a landing */
	V.posSpread = 0.0f;
	V.osc.position = 0u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	block();
	for (b = 0u; b < 200u; b++) {
		static int32_t prev[COL_OSC_MAX];
		if (b == 0u)
			for (i = 0u; i < COL_SCAN_COUNT; i++)
				prev[i] = (int32_t)V.frame[i] * 32768 + V.pwD[i];
		V.osc.position = (uint8_t)((b & 1u) ? 127u : 0u);
		block();
		for (i = 0u; i < COL_SCAN_COUNT; i++) {
			const int32_t pos = (int32_t)V.frame[i] * 32768 + V.pwD[i];

			if (pos - prev[i] > 2 * 32768 || prev[i] - pos > 2 * 32768)
				fail(G_SCAN, "position jumped more than two frames", pos, prev[i]);
			prev[i] = pos;
		}
	}
	/*
	 * S096 P3 assessment F2: a scan sweep is continuous and travels both
	 * ways. Each block must start exactly where the last one ended (the
	 * absolute position frame x 32768 + pwD0 equals the previous frame x
	 * 32768 + pwD), move at most one frame, and blend inside its own cell
	 * (0 <= pwD0, pwD <= 32768); an ascent from frame 0 reaches the top
	 * frame and a descent from the top reaches frame 0. The first form
	 * jumped one frame at every upward crossing and stalled on the first
	 * boundary going down.
	 */
	V.posSpread = 0.0f;
	V.osc.position = 0u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	block();
	{
		static int32_t endPos[COL_OSC_MAX];
		unsigned dir;

		for (i = 0u; i < COL_SCAN_COUNT; i++)
			endPos[i] = (int32_t)V.frame[i] * 32768 + V.pwD[i];
		for (dir = 0u; dir < 2u; dir++) {
			V.osc.position = dir ? 0u : 127u;
			for (b = 0u; b < 12u; b++) {
				block();
				for (i = 0u; i < COL_SCAN_COUNT; i++) {
					const int32_t s0 = (int32_t)V.frame[i] * 32768 + V.pwD0[i];
					const int32_t e0 = (int32_t)V.frame[i] * 32768 + V.pwD[i];

					if (s0 != endPos[i])
						fail(G_SCAN, "a block does not start where the last ended", s0, endPos[i]);
					if (V.pwD0[i] > 32768u || V.pwD[i] > 32768u)
						fail(G_SCAN, "a blend outside its cell", V.pwD0[i], V.pwD[i]);
					if (e0 - s0 > 32768 || s0 - e0 > 32768)
						fail(G_SCAN, "more than one frame in a block", e0, s0);
					endPos[i] = e0;
				}
			}
			for (i = 0u; i < COL_SCAN_COUNT; i++) {
				const int32_t want = dir ? 0 : 7 * 32768;

				if (endPos[i] != want)
					fail(G_SCAN, dir ? "the descent did not reach frame 0"
					                 : "the ascent did not reach the top frame",
					     endPos[i], want);
			}
		}
	}

	/*
	 * S096 P3 (user report 2026-11-17): the wts spread is continuous in psp.
	 * On the 8-frame table psp 10 already spreads the oscillators (the
	 * truncated form gave k x F / 2 = 0.58 -> 0: none below psp ~20), and
	 * one psp step moves every landed position by less than a frame (the
	 * truncated form jumped in whole frames).
	 */
	{
		int32_t posA[COL_OSC_MAX], posB[COL_OSC_MAX];
		unsigned spread = 0u, step;

		V.osc.position = 64u;
		V.posSpread = 10.0f / 127.0f;
		Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
		block();
		for (i = 0u; i < COL_SCAN_COUNT; i++) {
			posA[i] = (int32_t)V.frame[i] * 32768 + V.pwD[i];
			if (posA[i] != posA[0])
				spread = 1u;
		}
		if (!spread)
			fail(G_SCAN, "psp 10 does not spread the scan (truncated spread)", posA[0], 0);
		for (step = 30u; step < 34u; step++) {
			V.posSpread = (float)step / 127.0f;
			V.oscLive = 0u;          /* land: compare the targets */
			block();
			for (i = 0u; i < COL_SCAN_COUNT; i++)
				posB[i] = (int32_t)V.frame[i] * 32768 + V.pwD[i];
			V.posSpread = (float)(step + 1u) / 127.0f;
			V.oscLive = 0u;
			block();
			for (i = 0u; i < COL_SCAN_COUNT; i++) {
				const int32_t now = (int32_t)V.frame[i] * 32768 + V.pwD[i];

				if (now - posB[i] >= 32768 || posB[i] - now >= 32768)
					fail(G_SCAN, "one psp step moves a target a whole frame", now, posB[i]);
			}
		}
		V.posSpread = 0.0f;
	}

	/* F = 1 plays frame 0 exactly (g = 0: the second read is weighted out) */
	install_table(2u);
	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_WTS);
	V.osc.waveform = 0u; V.osc.position = 90u; V.posSpread = 1.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	block();                       /* bind the two-frame table first */
	V.osc.sampleCacheSize = 1u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	block();
	for (i = 0u; i < COL_SCAN_COUNT; i++) {
		if (V.frame[i] != 0u || V.pwD[i] != 0u)
			fail(G_SCAN, "F = 1 is not frame 0", V.frame[i], V.pwD[i]);
	}
	/* wtb <-> wts keeps the bound table and lands the positions */
	Colloid_setOscType(&V, COL_TYP_WTB);
	if (V.scan != 0u || V.osc.oscType != OSC_TYPE_WTB)
		fail(G_SCAN, "wts -> wtb did not clear the scan flag", V.scan, 0);
	Colloid_setOscType(&V, COL_TYP_WTS);
	if (V.scan != 1u || V.oscLive != 0u)
		fail(G_SCAN, "wtb -> wts did not land the positions", V.scan, V.oscLive);
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
static float mixer_fx_return_last_gain[2];   /* S101 P2 E1: the return's ramp origin */
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
 * MX-E1 (S101 P2): mixer_addFxReturnToOutput() is within 256 (one int16 LSB)
 * of the pre-S101-P2 law (sample_mx_t)(clamp(x g, +-255) x 8,388,352),
 * restated here, on every routing, stereo and mono return, ramping gains,
 * including values past the +-255 guard. Break (Makefile): 8,388,608 for
 * 8,388,352 in E1's ramp scale must fail MX-E1.
 * (S101 P2: the MX-A5 and MX-E2 laws went with the send-add revert.)
 */
static sample_mx_t ref_floatToMx(float v)
{
	if (v > 255.0f) v = 255.0f; else if (v < -255.0f) v = -255.0f;
	return (sample_mx_t)(v * 8388352.0f);
}
static void test_mixer_s101p2(void)
{
	unsigned r, i, st;

	/* MX-E1: the return, every routing, stereo and mono returns, past the guard */
	{
		float l[BLK], rr[BLK];
		for (i = 0u; i < BLK; i++) {
			l[i] = (float)(0.9 * sin(0.4 * i));
			rr[i] = (float)(0.7 * cos(0.3 * i));
		}
		l[5] = 300.0f; rr[9] = -400.0f;          /* past the +-255 guard */
		for (r = 0u; r < 6u; r++)
			for (st = 0u; st < 2u; st++) {
				sample_mx_t a[4][2 * BLK], b[4][2 * BLK];
				const float *rp = st ? rr : 0;
				const float gl = 0.8f, gr = 0.45f, ll = 0.2f, lr = 0.65f;
				const float inv = 1.f / (OUTPUT_DMA_SIZE - 1.f);
				memset(a, 0, sizeof(a)); memset(b, 0, sizeof(b));
				mixer_fx_return_last_gain[0] = ll; mixer_fx_return_last_gain[1] = lr;
				mixer_addFxReturnToOutput((uint8_t)r, l, rp, gl, gr, a[0], a[1], a[2], a[3]);
				for (i = 0u; i < BLK; i++) {
					const float cl = ll + ((float)i * inv * (gl - ll));
					const float cr = lr + ((float)i * inv * (gr - lr));
					sample_mx_t *outs[4] = { b[0], b[1], b[2], b[3] };
					if (r < 2u) {
						sample_mx_t *sl = outs[r ? 0 : 2], *sr = outs[r ? 1 : 3];
						sl[2*i] = bufferTool_satAdd32(sl[2*i], ref_floatToMx(l[i] * cl));
						sr[2*i] = bufferTool_satAdd32(sr[2*i], ref_floatToMx((rp ? rp[i] : l[i]) * cr));
					} else {
						static const unsigned map[4] = { 2u, 3u, 0u, 1u };
						sample_mx_t *m = outs[map[r - 2u]];
						const float ms = rp ? (0.5f * (l[i] * cl + rp[i] * cr)) : (l[i] * cl);
						m[2*i] = bufferTool_satAdd32(m[2*i], ref_floatToMx(ms));
					}
				}
				for (unsigned k = 0u; k < 4u; k++)
					for (i = 0u; i < 2u * BLK; i++)
						if (llabs((long long)a[k][i] - (long long)b[k][i]) > 256) {
							fail(G_E1, "return off the old law by > 1 int16 LSB", r, a[k][i] - b[k][i]);
							k = 4u; break;
						}
			}
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
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	for (a = 0u; a < COL_OSC_MAX; a++)
		for (b = a + 1u; b < COL_OSC_MAX; b++) {
			if (V.rnd[a] == V.rnd[b]) fail(G_RND, "trigger draws equal", a, b);
			if (V.phase[a] == V.phase[b]) fail(G_RND, "start phases equal", a, b);
		}
	host_rng_stuck = 0;
}

/*
 * CO-JOIN (S095 P5, user 2026-11-14): the count per pwm form, and rejoining
 * oscillators landing.
 *
 * What:       a pwm Saw voice settles at note 63 (slw 0); Tri renders 6, so
 *             oscillators 6 and 7 stop at that pitch. Then, at a slow slw,
 *             a note an octave up and pos 0 -> 127 start the six slewing;
 *             back to Saw, 6 and 7 must sit exactly on the new target pitch
 *             (dtn 0, intervals 0: every target is the base) and on their
 *             width target in that first block, while oscillator 0 is still
 *             on its way. Then typ wav: 8..11 land the same way.
 * Why:        without ColloidVoice.oscLive the rejoining oscillators glided
 *             from where they stopped (the break build colloid_break_join
 *             removes the test and must fail here).
 *             S096 P2 F5: Saw/Rec render 6 again, so Tri -> Saw has no
 *             rejoining oscillator (the loop is empty); pwm -> wav rejoins
 *             6..11, measured against oscillator 0's settled pitch.
 */
static void test_join(void)
{
	unsigned i;
	float want;
	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_PWM);
	V.detune = 0.0f; V.slew = 0u; V.posSpread = 0.6f;
	V.osc.waveform = COL_PWM_SAW; V.osc.position = 0u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	block(); block();
	if (V.oscLive != COL_PWM_COUNT) fail(G_JOIN, "Saw count", V.oscLive, COL_PWM_COUNT);
	V.osc.waveform = COL_PWM_TRI;   /* S099 P1 §15.10: Sin until then */
	block();
	if (V.oscLive != COL_PWM_WARP_COUNT) fail(G_JOIN, "Tri count", V.oscLive, COL_PWM_WARP_COUNT);
	/* an octave up, slowly; the trigger snaps every width, so pos moves after it */
	V.slew = 120u;
	Colloid_triggerVoice(&V, 0u, 100u, 75u, 5514u, 1u);
	V.osc.position = 127u;
	/* S096 P2 F5: oscillator 0's settled pitch (6 was the first idle one
	 * while Saw rendered 8 and the warp forms 6; both render 6 now) */
	want = V.pitchCur[0] + 1.0f;
	block();
	V.osc.waveform = COL_PWM_SAW;
	block();
	for (i = COL_PWM_WARP_COUNT; i < COL_PWM_COUNT; i++) {
		const uint8_t w = col_pwmTarget((int32_t)(V.osc.position & 127u), V.rnd[i], col_pwmScale(&V));
		if (fabsf(V.pitchCur[i] - want) > 1e-5f) fail(G_JOIN, "Tri -> Saw: rejoin pitch", i, V.pitchCur[i] - want);
		if (V.pwPos[i] != w) fail(G_JOIN, "Tri -> Saw: rejoin width", V.pwPos[i], w);
	}
	if (fabsf(V.pitchCur[0] - want) < 1e-3f) fail(G_JOIN, "oscillator 0 not still slewing", V.pitchCur[0], want);
	/* pwm -> wav: 8..11 rejoin */
	Colloid_setOscType(&V, COL_TYP_WAV);
	V.osc.waveform = COL_WAVE_SAW;
	block();
	if (V.oscLive != COL_OSC_COUNT) fail(G_JOIN, "wav count", V.oscLive, COL_OSC_COUNT);
	for (i = COL_PWM_COUNT; i < COL_OSC_COUNT; i++)
		if (fabsf(V.pitchCur[i] - want) > 1e-5f) fail(G_JOIN, "pwm -> wav: rejoin pitch", i, V.pitchCur[i] - want);
}

/*
 * CO-PWG (S096 P2 F5): a moving pwm width glides, it does not click.
 *
 * What:       for each pwm form (Tri, Saw, Rec, R-S; Sin until S099 P1
 *             §15.10, REx until S099 P2) a low note (31) with the
 *             filter off, 2,400 blocks after 400 to settle: once with the
 *             width still (pos 100), once swept by a slow sine (0.4 Hz over
 *             the whole range) and once at the full slew (pos 0 <-> 127 every
 *             80 blocks, the width moving 2 index steps a block). The largest
 *             third difference of the output must stay within 3 x the still
 *             width's + 0.002 in both moving runs.
 * Why:        before S096 P2 F5 the width changed once per block in whole
 *             table points (Saw/Rec) or moved the warp's half-cycle boundary
 *             in one jump (Sin/Tri): the largest third differences under the
 *             slow sweep were 0.51 / 0.49 (Saw / Rec, still 0.033 / 0.028)
 *             and 0.033 / 0.063 (Sin / Tri, still 0.0001 / 0.0005), heard as
 *             a light popping (S096_P2_INSTRUMENT_FIXES.md §5). The gliding
 *             second read and the gliding warp bring them to 0.039 / 0.037
 *             and 0.0006 / 0.0023.
 */
static float pwg_out[2400u * BLK];

static float pwg_run(uint8_t form, int mode)
{
	unsigned b, i, k = 0u;
	float mx = 0.0f;

	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_PWM);
	V.osc.waveform = form;
	V.osc.position = 100u; V.posSpread = 97.0f / 127.0f;
	V.osc.midiFreq = (uint16_t)((31u << 8) | 63u);
	V.attack = 0u; V.decay = 0u; V.sustainLevel = 1.0f; V.sustain = 1u;
	V.spread = 0.0f; V.motion = 0.0f; V.detune = 58.0f / 127.0f;
	V.filtType = COL_FT_OFF; V.route = COL_RTE_STR; V.cut = 1.0f;
	V.fAmount = 63.0f / 127.0f;
	Colloid_triggerVoice(&V, 0u, 127u, 31u, 5514u, 1u);
	for (b = 0u; b < 2800u; b++) {
		if (mode == 1)
			V.osc.position = (uint8_t)(64.0f + 63.0f * sinf(2.0f * 3.14159265f * 0.4f * (float)b / 1378.4f));
		else if (mode == 2)
			V.osc.position = ((b / 80u) & 1u) ? 127u : 0u;
		block();
		if (b >= 400u)
			for (i = 0u; i < BLK; i++)
				pwg_out[k++] = (float)L[i] * (1.0f / 32768.0f);
	}
	for (i = 3u; i < k; i++) {
		const float d3 = fabsf(pwg_out[i] - 3.0f * pwg_out[i - 1u] + 3.0f * pwg_out[i - 2u] - pwg_out[i - 3u]);
		if (d3 > mx)
			mx = d3;
	}
	return mx;
}

static void test_pwg(void)
{
	uint8_t form;

	for (form = COL_PWM_TRI; form <= COL_PWM_RS; form++) {
		const float still = pwg_run(form, 0);
		const float lim = 3.0f * still + 0.002f;
		const float swept = pwg_run(form, 1);
		const float fast = pwg_run(form, 2);

		if (swept > lim) fail(G_PWG, "swept width clicks", form, swept);
		if (fast > lim) fail(G_PWG, "full-slew width clicks", form, fast);
	}
}

/* CO-CHP (S099 P1): the chop pair loop against the restated law, both
 * buses, on random rows, phases, widths and levels; then a voice-level
 * check: an R-S stack at psp 0, pos 0 and 127, has a mean of the mid bus
 * within 0.3 % of one oscillator's full scale (the start pass removes the
 * mean), every oscillator's gate row is the octave row + 2, and (S099 P2)
 * every width is Rec's, oscPwmDRec[pos] (0.39 % at 127; REx dropped). */
static void test_chop(void)
{
	uint32_t s = 0x5EEDu;
	unsigned t, n;

	for (t = 0u; t < 400u; t++) {
		int32_t bus[2 * BLK], refb[2 * BLK];
		const int16_t *rows[2];
		uint32_t ph[2], inc[2], ph0[2];
		uint16_t w[2], w0[2];
		int16_t g14[2], rel[2], rp[2];
		const int16_t *const curve = oscChopCurveRs;   /* S099 P2: no REx */
		uint32_t gq, lv, lp;

		for (unsigned o = 0u; o < 2u; o++) {
			const uint32_t r = golden_rand(&s) % 11u;
			const uint32_t k = golden_rand(&s) & 127u;
			/* the start width within the slew (+-2 index steps) of the end */
			const uint32_t k0 = (k + 128u + (golden_rand(&s) % 5u) - 2u) & 127u;

			rows[o] = sawTable[r];
			ph0[o] = ph[o] = golden_rand(&s);
			inc[o] = golden_rand(&s) >> 6;
			w[o] = oscPwmDRec[k];      /* S099 P2: Rec's widths */
			w0[o] = oscPwmDRec[k0];
			g14[o] = oscChopGainQ14[r];
			rel[o] = (int16_t)(golden_rand(&s) & 0x7FFFu);
			rp[o] = (int16_t)((int32_t)(golden_rand(&s) & 0xFFFFu) - 32768);
		}
		gq = (uint32_t)(uint16_t)g14[0] | ((uint32_t)(uint16_t)g14[1] << 16);
		lv = (uint32_t)(uint16_t)rel[0] | ((uint32_t)(uint16_t)rel[1] << 16);
		lp = (uint32_t)(uint16_t)rp[0] | ((uint32_t)(uint16_t)rp[1] << 16);
		for (n = 0u; n < 2u * BLK; n++)
			bus[n] = refb[n] = (int32_t)(golden_rand(&s) & 0xFFFFu) - 32768;
		col_chopPairAdd(rows, ph, inc, w, w0, gq, lv, lp, curve, bus);
		for (n = 0u; n < BLK; n++) {
			const int32_t y0 = ref_chopY(rows[0], curve, ph0[0], inc[0], n, w[0], w0[0], g14[0]);
			const int32_t y1 = ref_chopY(rows[1], curve, ph0[1], inc[1], n, w[1], w0[1], g14[1]);
			const int32_t m = refb[n] + (int32_t)(((int64_t)y0 * rel[0]) >> 16)
			                          + (int32_t)(((int64_t)y1 * rel[1]) >> 16);
			const int32_t sd = refb[n + BLK] + (int32_t)(((int64_t)y0 * rp[0]) >> 16)
			                                 + (int32_t)(((int64_t)y1 * rp[1]) >> 16);

			if (bus[n] != m) { fail(G_CHP, "chop mid bus", bus[n], m); break; }
			if (bus[n + BLK] != sd) { fail(G_CHP, "chop side bus", bus[n + BLK], sd); break; }
		}
		if (ph[0] != ph0[0] + BLK * inc[0]) fail(G_CHP, "chop phase advance", ph[0], 0);
	}
	/* the voice: the mean is removed, the rows are lifted */
	for (unsigned form = COL_PWM_RS; form <= COL_PWM_RS; form++) {
		static const uint8_t posv[2] = { 0u, 127u };

		for (unsigned pi = 0u; pi < 2u; pi++) {
			double sum = 0.0;
			unsigned b;

			voice_init(); host_running = 1u;
			Colloid_setOscType(&V, COL_TYP_PWM);
			V.osc.waveform = (uint8_t)form;
			V.osc.position = posv[pi];
			V.posSpread = 0.0f;
			Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
			for (b = 0u; b < 400u; b++) {
				block();
				for (n = 0u; n < BLK; n++)
					sum += V.busM[n];
			}
			if (fabs(sum / (400.0 * BLK)) > 0.003 * 32767.0 * COL_PWM_COUNT)
				fail(G_CHP, "the stack's mean is not removed", form, sum / (400.0 * BLK));
			for (unsigned i = 0u; i < COL_PWM_COUNT; i++) {
				const uint32_t r = (uint32_t)(V.row[i] - sawTable[0]) >> 10;

				if (r < OSC_CHOP_LIFT && r != 10u)
					fail(G_CHP, "gate row not lifted", i, r);
				if (V.pwD[i] != oscPwmDRec[V.pwPos[i] & 127u])
					fail(G_CHP, "R-S width is not Rec's", V.pwD[i], V.pwPos[i]);
			}
		}
	}
}

/* S105 P1 A5 (C2.12): the init.col level, for Katran's KAT_OUT_NORM. */
static void print_level(void)
{
	int n, i;
	double e = 0.0;

	voice_init();
	host_running = 1u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u, 5514u, 1u);
	for (n = 0; n < 2000; n++) {
		block();
		for (i = 0; i < BLK; i++)
			e += (double)L[i] * (double)L[i] +
			     (double)V.right[i] * (double)V.right[i];
	}
	e /= (double)(2000 * BLK * 2);
	printf("colloid init level: RMS %.2f dBFS\n",
	       10.0 * log10(e / (32768.0 * 32768.0)));
}

int main(int argc, char **argv)
{
	unsigned g, total = 0u;
	if (argc > 1 && strcmp(argv[1], "--level") == 0) {
		print_level();
		return 0;
	}
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
	test_gate();
	test_retrig();
	test_rollgate();
	test_change();
	test_penv();
	test_noise();
	test_amp();
	test_svf();
	test_ser();
	test_route();
	test_post();
	test_pwm_voice();
	test_wtb();
	test_scan();
	test_mixer();
	test_mixer_s101p2();
	test_rnd();
	test_join();
	test_pwg();
	/* S099 P1: last, so the existing laws keep the RNG stream they had */
	test_chop();
	/* S103 P6: after the chop, so every earlier law keeps its RNG stream */
	test_mono();
	for (g = 0u; g < G_N; g++) {
		printf("colloid %-7s failures: %u\n", g_name[g], g_fail[g]);
		total += g_fail[g];
	}
	return total ? 1 : 0;
}
