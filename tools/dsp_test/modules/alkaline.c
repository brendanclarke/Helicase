/*
 * Alkaline host test (S093 P0; B2R, N1, drive and make-up since S093 P3;
 * wavetable rows and `typ` since S093 P8).
 *
 * What:       compiles the real DiodeLadder.c, AlkalineControl.c,
 *             AlkalineVoice.c and the generated Alkaline tables, over the
 *             whole real Oscillator.c (as the oscillator module does) and the
 *             real sawTable, and checks:
 *               AL-B  bounded output, no NaN, at every knob extreme (drive
 *                     included) and on minimum-to-maximum knob steps between
 *                     blocks, both waves, the three kernels (B2R, A-R, B-R),
 *                     notes 0..127;
 *               AL-R  on B2R and B-R (A-R printed for information: it is not
 *                     stable at Res full with the T2-2 64-sample network,
 *                     S093 P3), the [SN] p7 calibration (Res full, saw at C2, ladder
 *                     current 7.375 uA, I_abc 50 uA) read on the OTA
 *                     output: ring period 2.0 ms +- 3 % (SPICE 2.034), the
 *                     second ring peak over the first 0.364 +- 0.03 (the
 *                     golden RK4 model, same metric; S093 P0 §5.2); S093 P6
 *                     also the x1.5 feedback point (1.791 ms / 0.823);
 *               AL-S  sequencing (S092 P0 §5.3): accent at velocity 101 not
 *                     100; a note after a slide-flagged note is legato (no
 *                     MEG strike, no VEG trigger, glide), the slid-to note's
 *                     own slide flag decides the next note; gate 7/12 of a
 *                     step, held through the next step's gate after a slide;
 *               AL-N  the N1 Res table: setNet at a grid point reproduces the
 *                     row; interpolation is continuous across a point;
 *               AL-AB B2R against B-R and against A-R within 0.3 dB RMS at
 *                     the 303's drive;
 *               AL-D  drive (S093 P3/P6): at x16 bounded and finite, the
 *                     input-pair argument goes past its clamp (the tail is in
 *                     use); with the output make-up the pattern never reaches
 *                     full scale at x1, x2, x4, x8, x16; drive raises the level;
 *               AL-Q  Alkaline_isQuiet(): not quiet while the gate is open,
 *                     quiet once the gate and ACCENT have closed and the OTA
 *                     current has decayed, at x1 and x8;
 *               AL-F  fast maths (S093 P3 T1): alk_log2f within 1e-5 absolute
 *                     over 1e-12..1e12 (0.012 cent), alk_exp2f within 5e-7
 *                     relative over -100..100, the prewarp ratio within 2e-5
 *                     of tan(x)/x up to its 1.3 clamp;
 *               AL-X  the fused oscillator read (DiodeLadder_renderB2Osc,
 *                     S093 P3) is bit-identical to calcWavetableOscBlock()
 *                     then DiodeLadder_renderB2;
 *               AL-XT (S094 P4) the same for the stock triangle: the fused
 *                     read on the triTable row equals the separate path bit
 *                     for bit, and alk_renderWav() under ALK_WAVE_TRI equals
 *                     the fused call;
 *               AL-O  osc_setTableFreq() (T1-7) gives osc_setFreq()'s
 *                     phaseInc and tableOffset for SAW at every note, with
 *                     pitchMod and modNodeValue at 1 and away from 1.
 *               AL-A  (S093 P6) the VEG attack as the CPU trigger width:
 *                     attack 0 is the former VEG bit for bit; 10->90 % rises
 *                     from 2.0..2.8 ms at 0 to 4.6..5.7 ms at 1;
 *               AL-M  (S093 P6) the MEG decay range: 0.80 x / 1.50 x the
 *                     303's stock times at decay 0 / 1; accent unchanged;
 *               AL-SO (S093 P6) feedback x1.5 sustains after the input stops,
 *                     bounded; the 303's maximum does not;
 *               AL-PS (S093 P6) pos: 0 free-running, 64 half a cycle, legato
 *                     keeps the phase, a sweep leaves no pitch error.
 *               AL-W  (S093 P8, revision 3) typ wtb: the frame position
 *                     glides toward pos at most one frame per block and
 *                     stops on the next frame boundary; a non-legato note
 *                     jumps it to pos (legato does not). Without a slot the
 *                     voice is bit-identical to the table read of the
 *                     nearest frame's row (calcWavetableOscBlock()'s
 *                     arithmetic) then DiodeLadder_renderB2(); with a slot
 *                     it is bit-identical to DiodeLadder_renderB2Osc2() on
 *                     the block's frame pair with the mix ramping from the
 *                     start to the end position; phase too. The two-row loop
 *                     itself plays frame A at mix 0 and frame B at mix 1
 *                     (to the int16 truncation of the one-row read). An
 *                     install rebinds; an empty catalogue plays silence and
 *                     never claims a slot;
 *               AL-TY (S093 P8; S094 P1; S094 P4) Alkaline_setOscType(): 0 ->
 *                     wav, 1 -> wtb, 2 and above -> pwm; wav and pwm clamp
 *                     to Tri, wtb to the last table; the phase runs on; the
 *                     bind is cleared; the same type is a no-op.
 *               AL-PWW (S094 P4) the Alkaline pwm Tri (fused mode 4) against
 *                     the unfused drum WT loop then DiodeLadder_renderB2():
 *                     at least 40 dB below the reference RMS at pos 0, 64
 *                     and 127; pos 0 against Tri under wav within 40 dB;
 *                     bounded and sounding at the three positions.
 *             Modes for tools/alkaline/check_c.py:
 *               --traces IN OUT [DRV]   the three kernels fed with SPICE e2e
 *                                       signals (input x DRV)
 *               --pattern W C R E D A OUT [K [DRV]]   the P4 pattern through
 *                     the voice; K 0 = B2R (the voice), 1 = A-R, 2 = B-R
 * Why:        S093 P0 §5: the C must reproduce the validated S092 model.
 * Inputs:     generated fragments (Makefile target alkaline).
 * Outputs:    failure counts per group; exit 1 on any failure.
 * Accessors:  make -C tools/dsp_test alkaline.
 * Affiliates: tools/alkaline/check_c.py; S093_P0_ALKALINE_C_FUNCTIONS_CODE.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "osc_host.h"
#include "Oscillator.host.c"
/* S094 P1 S3: the real generated PWM tables referenced by Oscillator.c. */
#include "oscPwmTables.host.c"

/* the tables the whole Oscillator.c references: the real triangle since
 * S094 P4 (the Tri laws); sine_table and recTable stay zero stand-ins */
int16_t sine_table[TABLESIZE + 1];
#include "alk_triTable.c"
int16_t recTable[11][1024];
int16_t GetRngValue(void) { return 0; }
/*
 * The interpolation slot claim, host stand-in (S093 P8 revision 3).
 *
 * What:       grants when w_grant is 1 and refuses when it is 0, and counts
 *             its calls in w_claims. The device's modNode_interpClaim()
 *             (two system-wide slots, sticky grants) is not linked here.
 * Why:        AL-W runs the Alkaline wavetable once with a slot (the blend)
 *             and once without (the nearest frame), and checks that a
 *             one-frame table never claims.
 */
static int w_grant = 0;
static unsigned w_claims = 0u;
uint8_t modNode_interpClaim(void* osc) { (void)osc; w_claims++; return (uint8_t)w_grant; }

/* the real sawTable (wavetable.c), writable on the host as osc_host.h says */
#include "alk_sawTable.c"

/* lfo.h / modulationNode.h / InstrumentManager.h: same-shape stand-ins for
 * what the engine references (S093 P4). The engine owns an Lfo and calls
 * lfo_init(); the trigger makes the three platform calls of
 * Drum_triggerVoice(). The bench counts the calls so law AL-P can prove the
 * trigger makes them for its slot. */
typedef struct { uint8_t bytes[124]; } Lfo;
typedef struct { float value; } ModulationNode;
ModulationNode velocityModulators[6];
static unsigned host_lfoRetriggers, host_veloTargets;   /* AL-P call counters */
static uint8_t host_lastSlot;                           /* AL-P: the slot the calls named */
void lfo_init(Lfo *lfo) { memset(lfo, 0, sizeof(*lfo)); }
void lfo_retrigger(uint8_t voiceNr) { host_lfoRetriggers++; host_lastSlot = voiceNr; }
void modNode_updateValue(ModulationNode *vm, float val) { vm->value = val; }
void instrumentManager_applyVelocityModulationTarget(uint8_t source_slot, float velocity_0_1)
{ (void)velocity_0_1; host_veloTargets++; host_lastSlot = source_slot; }

#include "DiodeLadder.host.h"
#include "AlkalineControl.host.h"
#include "AlkalineTables.host.h"
#include "AlkalinePostTables.host.h"   /* S094 P1 S4 */
#include "AlkalineVoice.host.h"
#include "DiodeLadder.host.c"
#include "AlkalineControl.host.c"
#include "AlkalineTables.host.c"
#include "AlkalinePostTables.host.c"   /* S094 P1 S4: the real generated tables */
#include "AlkalineSquareTable.host.c"
#include "AlkalineVoice.host.c"

#define BLK OUTPUT_DMA_SIZE
#define ALK_STEP 5514u   /* a 16th at 120 BPM, samples */

/* law groups (S094 P1: AL-OD0 golden, AL-A3 drive tables, AL-OV overdrive,
 * AL-TN tone, AL-PW pwm appended; S094 P4: AL-XT and AL-PWW appended; the
 * array holds one counter per group) */
static unsigned g_fail[23];
enum { G_B, G_R, G_S, G_N, G_AB, G_D, G_Q, G_F, G_O, G_X, G_A, G_M, G_SO, G_PS, G_W, G_TY,
       G_OD0, G_A3, G_OV, G_TN, G_PW, G_XT, G_PWW };
static const char *g_name[] = { "AL-B", "AL-R", "AL-S", "AL-N", "AL-AB", "AL-D", "AL-Q",
                                "AL-F", "AL-O", "AL-X", "AL-A", "AL-M", "AL-SO", "AL-PS",
                                "AL-W", "AL-TY", "AL-OD0", "AL-A3", "AL-OV", "AL-TN",
                                "AL-PW", "AL-XT", "AL-PWW" };
enum { K_B2R = 0, K_AR = 1, K_BR = 2 };
static const char *k_name[] = { "B2R", "A-R", "B-R" };

/* one kernel by number, the same arguments */
static void render_k(int k, DiodeLadder *dl, int16_t *buf, float sScale, float il, float ia)
{
	if (k == K_AR)
		DiodeLadder_renderA(dl, buf, sScale, il, ia);
	else if (k == K_BR)
		DiodeLadder_renderB(dl, buf, sScale, il, ia);
	else
		DiodeLadder_renderB2(dl, buf, sScale, il, ia);
}

static void fail(int g, const char *what, double a, double b)
{
	if (g_fail[g] < 5u)
		printf("  %s FAIL: %s (%g, %g)\n", g_name[g], what, a, b);
	g_fail[g]++;
}

/* one block of the voice with kernel k: K_B2R is the voice's own sync block;
 * the others repeat its arguments (drive, make-up) with another kernel */
static void block(AlkalineVoice *v, int16_t *buf, int k)
{
	Alkaline_calcVoiceAsync(v);
	if (k != K_B2R) {
		const uint8_t sq = (uint8_t)(v->osc.waveform == ALK_WAVE_SQUARE);
		calcWavetableOscBlock(&v->osc, sq ? alkSqrTable : (const int16_t (*)[1024])sawTable,
		                      buf, BLK, 1.0f);
		render_k(k, &v->ladder, buf, (sq ? ALK_SQR_SCALE_V : ALK_SAW_SCALE_V) * v->driveGain,
		         v->iLadEnd, v->iAbcEnd * v->makeup);
	} else {
		Alkaline_calcVoiceSyncBlock(v, buf, BLK);
	}
}

/* ---------------------------------------------------------------- AL-B */
static void test_bounds(void)
{
	static const float ext[2] = { 0.0f, 1.0f };
	int opt, w, a, b, n;

	for (opt = 0; opt < 3; opt++)
		for (w = 0; w < 2; w++)
			for (a = 0; a < 64; a++) {
				AlkalineVoice v;
				int16_t buf[BLK];

				memset(&v, 0, sizeof(v));
				Alkaline_initVoice(&v);
				v.osc.waveform = (uint8_t)w;
				for (n = 0; n < 600; n++) {
					/* knob pattern a, flipped every 50 blocks (min <-> max steps) */
					const int flip = (n / 50) & 1;

					v.cutoff = ext[((a >> 0) & 1) ^ flip];
					v.reso = ext[((a >> 1) & 1) ^ flip];
					v.envMod = ext[((a >> 2) & 1) ^ flip];
					v.decay = ext[((a >> 3) & 1)];
					v.accent = ext[((a >> 4) & 1) ^ flip];
					v.drive = ext[((a >> 5) & 1) ^ flip];
					v.slide = (uint8_t)(n & 64 ? 1 : 0);
					if (n % 37 == 0)
						Alkaline_triggerVoice(&v, 0u, (uint8_t)((n * 13) & 127),
						                      (uint8_t)((n * 29) & 127), ALK_STEP);
					block(&v, buf, opt);
					for (b = 0; b < BLK; b++)
						if (!isfinite(v.ladder.x3) || !isfinite(v.ladder.zo) ||
						    !isfinite(v.ladder.z[0]))
							fail(G_B, "state not finite", a, n);
					if (fabsf(v.ladder.x0) > 1e3f || fabsf(v.ladder.z[0]) > 1e3f)
						fail(G_B, "state unbounded", v.ladder.x0, v.ladder.z[0]);
				}
			}
}

/* ---------------------------------------------------------------- AL-R
 * The ladder alone at the calibration point, the saw at C2 from the real
 * oscillator; the ring is read on the OTA output with I_abc constant.
 * S093 P6: at a given Res knob (the table pair and fraction as
 * Alkaline_calcVoiceAsync() forms them): knob 1/ALK_RES_KMAX is the 303's
 * maximum, knob 1 the x1.5 feedback. */
static void test_ring(int k, float knob, double *period, double *decay)
{
	static int16_t out[44108];
	DiodeLadder dl;
	OscInfo osc;
	const int nb = (int)(sizeof(out) / sizeof(out[0])) / BLK;
	const float i0 = 7.375e-6f;
	const float kk = knob * (float)(ALK_RES_POINTS - 1u);
	unsigned lo = (unsigned)kk;
	int b, j;

	if (lo > ALK_RES_POINTS - 2u)
		lo = ALK_RES_POINTS - 2u;
	memset(&osc, 0, sizeof(osc));
	osc.waveform = SAW;
	osc.freq = 65.406f;
	osc.pitchMod = 1.0f;
	osc.modNodeValue = 1.0f;
	osc_setFreq(&osc);
	DiodeLadder_init(&dl);
	DiodeLadder_setNet(&dl, &alkResTable[lo], &alkResTable[lo + 1u], kk - (float)lo);
	for (b = 0; b < nb; b++) {
		int16_t *buf = &out[b * BLK];

		calcWavetableOscBlock(&osc, (const int16_t (*)[1024])sawTable, buf, BLK, 1.0f);
		render_k(k, &dl, buf, ALK_SAW_SCALE_V, i0 * alk_prewarp(i0, knob), 50e-6f);
	}
	/* one saw period after 0.5 s: find the reset (largest jump), then the
	 * ring's local maxima after it */
	{
		const int start = 22054, per = (int)(44108.0 / 65.406);
		int jr = start, k, npk = 0;
		int pk[16];
		int big = 0;

		for (j = start; j < start + per; j++) {
			const int d = abs((int)out[j + 1] - (int)out[j]);

			if (d > big) { big = d; jr = j + 1; }
		}
		for (k = jr + 8; k < jr + per - 2 && npk < 16; k++)
			if (out[k] > out[k - 1] && out[k] >= out[k + 1] && out[k] > 200)
				pk[npk++] = k;
		*period = (npk >= 2) ? (pk[1] - pk[0]) * 1000.0 / 44108.0 : 0.0;
		*decay = (npk >= 2) ? (double)out[pk[1]] / (double)out[pk[0]] : 0.0;
	}
}

/* ---------------------------------------------------------------- AL-S */
static void test_sequencing(void)
{
	AlkalineVoice v;
	int16_t buf[BLK];
	int n;

	memset(&v, 0, sizeof(v));
	Alkaline_initVoice(&v);
	Alkaline_triggerVoice(&v, 0u, 100u, 63u, ALK_STEP);
	if (v.accentOn) fail(G_S, "velocity 100 (the default) is not an accent", 100, 1);
	Alkaline_triggerVoice(&v, 0u, 101u, 63u, ALK_STEP);
	if (!v.accentOn) fail(G_S, "velocity 101 is an accent", 101, 0);
	if (v.gateLeft != 3217) fail(G_S, "gate = 7/12 of a step", v.gateLeft, 3217);
	for (n = 0; n < 5; n++) block(&v, buf, 0);

	/* slide-flagged note, then the next note is legato */
	v.slide = 1u;
	Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);
	if (v.gateLeft != 5514 + 3217) fail(G_S, "slide holds the gate a step + 7/12", v.gateLeft, 8731);
	for (n = 0; n < 172; n++) block(&v, buf, 0);
	v.slide = 0u;
	v.meg.strikeLeft = 0.0f;
	v.veg.delayLeft = 0.0f;
	v.veg.attLeft = 0.0f;
	Alkaline_triggerVoice(&v, 0u, 80u, 75u, ALK_STEP);
	if (!v.gliding) fail(G_S, "note after a slide glides", v.gliding, 1);
	if (v.meg.strikeLeft != 0.0f) fail(G_S, "legato: no MEG strike", v.meg.strikeLeft, 0);
	if (v.veg.attLeft != 0.0f) fail(G_S, "legato: no VEG trigger", v.veg.attLeft, 0);
	{
		const float p0 = v.pitchLog2;
		OscInfo ref = v.osc;

		osc_setBaseNote(&ref, 75u);
		block(&v, buf, 0);
		{
			const float tgt = log2f(ref.freq);
			const float want = tgt + (p0 - tgt) * 0.967561f;

			if (fabsf(v.pitchLog2 - want) > 1e-3f)
				fail(G_S, "glide one block (log2 Hz)", v.pitchLog2, want);
		}
	}
	/* the slid-to note had slide 0: the next note is not legato */
	for (n = 0; n < 171; n++) block(&v, buf, 0);
	Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);
	if (v.gliding) fail(G_S, "slide is taken from the flagged note only", v.gliding, 0);
	if (v.meg.strikeLeft <= 0.0f) fail(G_S, "non-legato: MEG strike", v.meg.strikeLeft, 1);
	/* a slide-flagged note followed by a rest: the gate closes at 1.58 steps */
	v.slide = 1u;
	Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);
	for (n = 0; n < 273; n++) block(&v, buf, 0);
	if (v.gateLeft != 0) fail(G_S, "slide into a rest: gate closes", v.gateLeft, 0);
	Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);
	if (v.gliding) fail(G_S, "after the gate closed: not legato", v.gliding, 0);

	/* AL-P (S093 P4): the trigger makes the platform calls of
	 * Drum_triggerVoice() for its own slot, with the velocity as 0..1 */
	memset(&v, 0, sizeof(v));
	Alkaline_initVoice(&v);
	host_lfoRetriggers = host_veloTargets = 0u;
	host_lastSlot = 0u;
	Alkaline_triggerVoice(&v, 3u, 127u, 63u, ALK_STEP);
	if (host_lfoRetriggers != 1u || host_veloTargets != 1u || host_lastSlot != 3u ||
	    velocityModulators[3].value != 1.0f)
		fail(G_S, "AL-P platform calls per trigger", host_lfoRetriggers, host_veloTargets);
}

/* ---------------------------------------------------------------- AL-N */
static void test_net(void)
{
	DiodeLadder a, b;
	unsigned p;

	DiodeLadder_init(&a);
	DiodeLadder_init(&b);
	for (p = 0u; p + 1u < ALK_RES_POINTS; p++) {
		DiodeLadder_setNet(&a, &alkResTable[p], &alkResTable[p + 1u], 0.0f);
		if (a.gau0 != alkResTable[p].ga[0][0] || a.gav0 != alkResTable[p].ga[1][0] ||
		    a.dv0 != alkResTable[p].d[1][0] || a.ku1 != alkResTable[p].d[0][1])
			fail(G_N, "setNet at a grid point reproduces the row", p, 0);
		DiodeLadder_setNet(&a, &alkResTable[p], &alkResTable[p + 1u], 1.0f - 1e-6f);
		if (p + 2u < ALK_RES_POINTS) {
			DiodeLadder_setNet(&b, &alkResTable[p + 1u], &alkResTable[p + 2u], 0.0f);
			if (fabsf(a.ku0 - b.ku0) > 1e-4f * (fabsf(b.ku0) + 1e-3f))
				fail(G_N, "ku0 continuous across a point", a.ku0, b.ku0);
		}
	}
}

/* ---------------------------------------------------------------- AL-AB */
static double pattern_rms(int k, float drive, int *clipped)
{
	AlkalineVoice v;
	int16_t buf[BLK];
	double acc = 0.0;
	int n;

	memset(&v, 0, sizeof(v));
	Alkaline_initVoice(&v);
	v.cutoff = 0.35f; v.reso = 0.85f / ALK_RES_KMAX; v.envMod = 0.6f; v.decay = 0.45f; v.accent = 0.8f;
	v.drive = drive;
	if (clipped)
		*clipped = 0;
	for (n = 0; n < 1400; n++) {
		if (n % 172 == 0)
			Alkaline_triggerVoice(&v, 0u, (n / 172) & 1 ? 127u : 80u, (uint8_t)(63 + (n / 172) % 5), ALK_STEP);
		block(&v, buf, k);
		for (int j = 0; j < BLK; j++) {
			acc += (double)buf[j] * buf[j];
			if (clipped && (buf[j] >= 32767 || buf[j] <= -32767))
				(*clipped)++;
		}
	}
	return sqrt(acc / (1400.0 * BLK));
}

/* ---------------------------------------------------------------- AL-D */
static void test_drive(void)
{
	/* S093 P6: 16^drive: 0, 1/4, 1/2, 3/4, 1 are x1 x2 x4 x8 x16 */
	static const float drv[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
	double rms[5];
	int d, clip;

	for (d = 0; d < 5; d++) {
		rms[d] = pattern_rms(K_B2R, drv[d], &clip);
		printf("AL-D: drive x%.0f: %+.2f dBFS RMS, %d full-scale samples\n",
		       exp2(4.0 * drv[d]), 20.0 * log10(rms[d] / 32767.0 + 1e-12), clip);
		if (clip)
			fail(G_D, "the make-up gain leaves headroom (no full-scale sample)", d, clip);
		if (!isfinite(rms[d]) || rms[d] <= 0.0)
			fail(G_D, "finite, non-silent output", d, rms[d]);
		if (d && rms[d] <= rms[d - 1] * 0.9)
			fail(G_D, "drive raises the level (net of the make-up)", rms[d], rms[d - 1]);
	}
	{	/* x16: bounded state, the input-pair argument past its clamp */
		AlkalineVoice v;
		int16_t buf[BLK];
		float xmax = 0.0f;
		int n;

		memset(&v, 0, sizeof(v));
		Alkaline_initVoice(&v);
		v.cutoff = 0.5f; v.reso = 1.0f; v.envMod = 1.0f; v.decay = 1.0f; v.accent = 1.0f;
		v.drive = 1.0f;
		for (n = 0; n < 2000; n++) {
			if (n % 172 == 0)
				Alkaline_triggerVoice(&v, 0u, 127u, (uint8_t)(51 + (n / 172) % 24), ALK_STEP);
			block(&v, buf, K_B2R);
			if (fabsf(v.ladder.xinLast) > xmax)
				xmax = fabsf(v.ladder.xinLast);
			if (!isfinite(v.ladder.x0) || !isfinite(v.ladder.z[0]) || fabsf(v.ladder.x0) > 1e3f)
				fail(G_D, "x16 state bounded and finite", n, v.ladder.x0);
		}
		printf("AL-D: x16 largest input-pair argument %.2f (clamp 3.5)\n", xmax);
		if (xmax <= 3.5f)
			fail(G_D, "x16 drives the input pair past its clamp (tail in use)", xmax, 3.5);
	}
}

/* ---------------------------------------------------------------- AL-Q */
static void test_quiet(void)
{
	static const float drv[2] = { 0.0f, 1.0f };
	int d, n;

	for (d = 0; d < 2; d++) {
		AlkalineVoice v;
		int16_t buf[BLK];

		memset(&v, 0, sizeof(v));
		Alkaline_initVoice(&v);
		v.drive = drv[d];
		block(&v, buf, K_B2R);
		if (!Alkaline_isQuiet(&v))
			fail(G_Q, "quiet before the first note", d, 0);
		Alkaline_triggerVoice(&v, 0u, 127u, 63u, ALK_STEP);
		for (n = 0; n < 10; n++)
			block(&v, buf, K_B2R);
		if (Alkaline_isQuiet(&v))
			fail(G_Q, "not quiet while the gate is open", d, v.iAbcEnd);
		for (n = 0; n < 400; n++)
			block(&v, buf, K_B2R);
		if (!Alkaline_isQuiet(&v))
			fail(G_Q, "quiet after gate, ACCENT and the VEG have closed", d, v.iAbcEnd);
	}
}

/* ---------------------------------------------------------------- AL-F */
static void test_fastmath(void)
{
	double worst = 0.0, wexp = 0.0, wtan = 0.0;
	int k;

	for (k = 0; k <= 200000; k++) {
		const float x = (float)pow(10.0, -12.0 + 24.0 * k / 200000.0);
		const double e = fabs((double)alk_log2f(x) - log2((double)x));

		if (e > worst) worst = e;
	}
	for (k = 0; k <= 200000; k++) {
		const float x = (float)(-100.0 + 200.0 * k / 200000.0);
		const double e = fabs((double)alk_exp2f(x) / exp2((double)x) - 1.0);

		if (e > wexp) wexp = e;
	}
	for (k = 1; k <= 13000; k++) {
		/* the prewarp ratio at theta = k / 1e4, through alk_prewarp's own
		 * expression: invert theta for res 1 (c(1) = 1.47) */
		const double th = k / 1e4;
		const float il = (float)(th / 1.47 * 2.0 * 44108.0 * 4.0 * 0.02585 * 33e-9);
		const double e = fabs((double)alk_prewarp(il, 1.0f) / (tan(th) / th) - 1.0);

		if (e > wtan) wtan = e;
	}
	printf("AL-F: log2 abs %.2e, exp2 rel %.2e, prewarp rel %.2e\n", worst, wexp, wtan);
	if (worst > 1e-5) fail(G_F, "alk_log2f accuracy", worst, 1e-5);
	if (wexp > 5e-7) fail(G_F, "alk_exp2f accuracy", wexp, 5e-7);
	if (wtan > 2e-5) fail(G_F, "prewarp tan ratio accuracy", wtan, 2e-5);
}

/* ---------------------------------------------------------------- AL-O */
static void test_tablefreq(void)
{
	static const float mods[3][2] = { { 1.0f, 1.0f }, { 1.37f, 1.0f }, { 1.0f, 0.61f } };
	unsigned n, m;

	for (m = 0u; m < 3u; m++)
		for (n = 0u; n < 128u; n++) {
			OscInfo a, b;

			memset(&a, 0, sizeof(a));
			a.waveform = SAW;
			a.freq = 440.0f * powf(2.0f, ((float)n - 69.0f) / 12.0f) * 1.0013f;
			a.pitchMod = mods[m][0];
			a.modNodeValue = mods[m][1];
			b = a;
			osc_setFreq(&a);
			osc_setTableFreq(&b);
			if (a.phaseInc != b.phaseInc || a.tableOffset != b.tableOffset)
				fail(G_O, "osc_setTableFreq == osc_setFreq", n, m);
		}
	{	/* a dense sweep 1 Hz..20 kHz, and every octave edge and the float
		 * just below it (the bit form of the octave count, S093 P3) */
		unsigned k;

		for (k = 0u; k < 200000u + 20u; k++) {
			OscInfo a, b;

			memset(&a, 0, sizeof(a));
			a.waveform = SAW;
			a.pitchMod = 1.0f;
			a.modNodeValue = 1.0f;
			if (k < 200000u)
				a.freq = powf(20000.0f, (float)k / 200000.0f);
			else
				a.freq = nextafterf(16.3515987f * (float)(1u << ((k - 200000u) >> 1)),
				                    ((k - 200000u) & 1u) ? 0.0f : 1e9f) ;
			if (k >= 200000u && !(((k - 200000u) & 1u)))
				a.freq = 16.3515987f * (float)(1u << ((k - 200000u) >> 1));
			b = a;
			osc_setFreq(&a);
			osc_setTableFreq(&b);
			if (a.phaseInc != b.phaseInc || a.tableOffset != b.tableOffset)
				fail(G_O, "dense sweep / edges", a.freq, k);
		}
	}
}

/* ---------------------------------------------------------------- AL-X
 * The fused oscillator read (DiodeLadder_renderB2Osc) against the separate
 * path (calcWavetableOscBlock() then DiodeLadder_renderB2): bit-identical
 * output, phase and ladder state over a pattern with glides, both waves,
 * drive x1 and x8. */
static void test_fused(void)
{
	int w, d, n;

	for (w = 0; w < 2; w++)
		for (d = 0; d < 2; d++) {
			AlkalineVoice a, b;

			memset(&a, 0, sizeof(a));
			Alkaline_initVoice(&a);
			a.osc.waveform = (uint8_t)w;
			a.reso = 0.9f; a.cutoff = 0.4f; a.envMod = 0.7f; a.accent = 0.8f;
			a.drive = (float)d;
			b = a;
			for (n = 0; n < 1500; n++) {
				int16_t ba[BLK], bb[BLK];

				if (n % 172 == 0) {
					a.slide = b.slide = (uint8_t)((n / 172) & 1);
					Alkaline_triggerVoice(&a, 0u, (n / 172) % 3 ? 80u : 127u,
					                      (uint8_t)(51 + (n * 7) % 30), ALK_STEP);
					Alkaline_triggerVoice(&b, 0u, (n / 172) % 3 ? 80u : 127u,
					                      (uint8_t)(51 + (n * 7) % 30), ALK_STEP);
				}
				Alkaline_calcVoiceAsync(&a);
				DiodeLadder_renderB2Osc(&a.ladder, ba,
				                        (w ? alkSqrTable : (const int16_t (*)[1024])sawTable)[a.osc.tableOffset],
				                        &a.osc.phase, a.osc.phaseInc,
				                        (w ? ALK_SQR_SCALE_V : ALK_SAW_SCALE_V) * a.driveGain,
				                        a.iLadEnd, a.iAbcEnd * a.makeup);
				Alkaline_calcVoiceAsync(&b);
				calcWavetableOscBlock(&b.osc, w ? alkSqrTable : (const int16_t (*)[1024])sawTable,
				                      bb, BLK, 1.0f);
				DiodeLadder_renderB2(&b.ladder, bb, (w ? ALK_SQR_SCALE_V : ALK_SAW_SCALE_V) * b.driveGain,
				                     b.iLadEnd, b.iAbcEnd * b.makeup);
				if (memcmp(ba, bb, sizeof(ba)) || a.osc.phase != b.osc.phase ||
				    memcmp(&a.ladder, &b.ladder, sizeof(a.ladder)))
					fail(G_X, "fused == separate (bit for bit)", w * 2 + d, n);
			}
		}
}

/* ---------------------------------------------------------------- AL-XT
 * S094 P4: the Tri wav read. The fused loop (DiodeLadder_renderB2Osc on the
 * triTable row) against the separate path (calcWavetableOscBlock() on
 * triTable, then DiodeLadder_renderB2): bit-identical output, phase and
 * ladder state, as AL-X for Saw and Sqr, over the same pattern, drive x1
 * and x8; and alk_renderWav() under ALK_WAVE_TRI equals the fused call. */
static void test_fused_tri(void)
{
	int d, n;

	for (d = 0; d < 2; d++) {
		AlkalineVoice a, b;

		memset(&a, 0, sizeof(a));
		Alkaline_initVoice(&a);
		a.osc.waveform = ALK_WAVE_TRI;
		a.reso = 0.9f; a.cutoff = 0.4f; a.envMod = 0.7f; a.accent = 0.8f;
		a.drive = (float)d;
		b = a;
		for (n = 0; n < 1500; n++) {
			int16_t ba[BLK], bb[BLK], bc[BLK];
			AlkalineVoice c;

			if (n % 172 == 0) {
				a.slide = b.slide = (uint8_t)((n / 172) & 1);
				Alkaline_triggerVoice(&a, 0u, (n / 172) % 3 ? 80u : 127u,
				                      (uint8_t)(51 + (n * 7) % 30), ALK_STEP);
				Alkaline_triggerVoice(&b, 0u, (n / 172) % 3 ? 80u : 127u,
				                      (uint8_t)(51 + (n * 7) % 30), ALK_STEP);
			}
			c = a;
			Alkaline_calcVoiceAsync(&a);
			DiodeLadder_renderB2Osc(&a.ladder, ba,
			                        ((const int16_t (*)[1024])triTable)[a.osc.tableOffset],
			                        &a.osc.phase, a.osc.phaseInc,
			                        ALK_TRI_SCALE_V * a.driveGain,
			                        a.iLadEnd, a.iAbcEnd * a.makeup);
			Alkaline_calcVoiceAsync(&b);
			calcWavetableOscBlock(&b.osc, (const int16_t (*)[1024])triTable,
			                      bb, BLK, 1.0f);
			DiodeLadder_renderB2(&b.ladder, bb, ALK_TRI_SCALE_V * b.driveGain,
			                     b.iLadEnd, b.iAbcEnd * b.makeup);
			Alkaline_calcVoiceAsync(&c);
			Alkaline_calcVoiceSyncBlock(&c, bc, BLK);
			if (memcmp(ba, bb, sizeof(ba)) || a.osc.phase != b.osc.phase ||
			    memcmp(&a.ladder, &b.ladder, sizeof(a.ladder)))
				fail(G_XT, "fused == separate (bit for bit)", d, n);
			if (memcmp(ba, bc, sizeof(ba)) || a.osc.phase != c.osc.phase ||
			    memcmp(&a.ladder, &c.ladder, sizeof(a.ladder)))
				fail(G_XT, "alk_renderWav == fused", d, n);
		}
	}
}

/* ---------------------------------------------------------------- AL-A
 * S093 P6: the VEG attack as the CPU trigger width.
 *   - attack 0 is the former VEG bit for bit: delay, length and factor equal
 *     the old constants (the factor against the compiler-folded expression);
 *   - the 10->90 % OTA-current rise after silence grows monotonically from
 *     knob 0 to 1 and lies in 2.0..2.8 ms at 0 and 4.6..5.7 ms at 1 (SPICE
 *     2.11 / 5.15 ms, one block of resolution plus the fit);
 *   - legato does not restart it (AL-S covers the trigger). */
static void test_attack(void)
{
	AlkVeg g;
	const float folded = expf(-ALK_DT / ALK_VEG_TAU_ATT_S);
	double prev = 0.0;
	int q;

	alkVeg_init(&g);
	alkVeg_trigger(&g, 0.0f);
	if (g.keep != folded || g.delayLeft != ALK_VEG_ATT_DELAY_S ||
	    g.attLeft != 8.0f * ALK_VEG_TAU_ATT_S)
		fail(G_A, "attack 0 is the former VEG exactly", g.keep, folded);
	for (q = 0; q <= 4; q++) {
		const float knob = (float)q / 4.0f;
		float cur[200], pk = 0.0f;
		int n, i10 = -1, i90 = -1;

		alkVeg_init(&g);
		alkVeg_trigger(&g, knob);
		for (n = 0; n < 200; n++) {
			cur[n] = alkVeg_step(&g, 1u);
			if (cur[n] > pk) pk = cur[n];
		}
		for (n = 0; n < 200; n++) {
			if (i10 < 0 && cur[n] > 0.1f * pk) i10 = n;
			if (i90 < 0 && cur[n] > 0.9f * pk) i90 = n;
		}
		{
			/* 10->90 % in whole blocks (the VEG steps once per block) */
			const double ms = (double)(i90 - i10) * ALK_DT * 1e3;

			if (ms < prev)
				fail(G_A, "attack grows with the knob", knob, ms);
			if (q == 0 && (ms < 2.0 || ms > 2.8))
				fail(G_A, "attack 0: 2.0..2.8 ms", knob, ms);
			if (q == 4 && (ms < 4.6 || ms > 5.7))
				fail(G_A, "attack 1: 4.6..5.7 ms", knob, ms);
			prev = ms;
		}
	}
}

/* ---------------------------------------------------------------- AL-M
 * S093 P6: the MEG decay range. Time from N53's peak to 10 % of its swing,
 * the firmware MEG alone (one Newton step, per block): decay 0 is 0.80 x and
 * decay 1 is 1.50 x the model's stock times (174.1 ms / 2.569 s,
 * fit_meg_range.py), within 2 % (one block is 0.5 % at 140 ms). With accent
 * held for the whole decay the 4066 path is the 303's (68 k + 250 R), so
 * the time is the model's stock decay-0 time within 2 %. */
static double meg_t10(float decay, uint8_t accent)
{
	/* steps the MEG from a strike; pk tracks N53's maximum and npk its block,
	 * and the first block below rest + 10 % of the swing after it ends the
	 * count (20,000 blocks = 14.5 s, past the 3.85 s longest decay) */
	AlkMeg m;
	float rest, pk = -1e9f;
	int n, npk = 0;

	alkMeg_init(&m);
	rest = alkMeg_n53(&m);
	alkMeg_trigger(&m);
	for (n = 0; n < 20000; n++) {
		const float v = alkMeg_step(&m, decay, accent);

		if (v > pk) { pk = v; npk = n; }
		else if (v < rest + 0.1f * (pk - rest))
			return (double)(n - npk) * ALK_DT;
	}
	return 1e9;
}

static void test_meg_range(void)
{
	const double t0 = meg_t10(0.0f, 0u), t1 = meg_t10(1.0f, 0u), ta = meg_t10(1.0f, 1u);

	if (fabs(t0 / (0.80 * 0.1741) - 1.0) > 0.02) fail(G_M, "decay 0 = 0.80 x 174.1 ms", t0, 0.80 * 0.1741);
	if (fabs(t1 / (1.50 * 2.569) - 1.0) > 0.02) fail(G_M, "decay 1 = 1.50 x 2.569 s", t1, 1.50 * 2.569);
	if (fabs(ta / 0.1741 - 1.0) > 0.02) fail(G_M, "accent held: the 303's short decay", ta, 0.1741);
	printf("AL-M: t10 %.1f ms / %.3f s (accent %.1f ms)\n", t0 * 1e3, t1, ta * 1e3);
}

/* ---------------------------------------------------------------- AL-SO
 * S093 P6: at knob 1 (feedback x1.5) with a high cutoff the loop sustains
 * after its input stops, bounded below full scale; at the 303's maximum it
 * does not. The ladder alone: a saw at C2 for 0.25 s, then silence for 0.5 s,
 * the cutoff current 4 x the calibration current. (The pitch against the
 * golden model is checked by check_c.py C1 with the acid_k15 case.) */
static double sustain_rms(float knob, int16_t *peak)
{
	static int16_t out[3 * 11027];
	DiodeLadder dl;
	OscInfo osc;
	const int nb = (int)(sizeof(out) / sizeof(out[0])) / BLK;
	const float il = 4.0f * 7.375e-6f;
	const float kk = knob * (float)(ALK_RES_POINTS - 1u);
	unsigned lo = (unsigned)kk;
	double acc = 0.0;
	int b, j, n = 0;

	if (lo > ALK_RES_POINTS - 2u)
		lo = ALK_RES_POINTS - 2u;
	memset(&osc, 0, sizeof(osc));
	osc.waveform = SAW; osc.freq = 65.406f; osc.pitchMod = 1.0f; osc.modNodeValue = 1.0f;
	osc_setFreq(&osc);
	DiodeLadder_init(&dl);
	DiodeLadder_setNet(&dl, &alkResTable[lo], &alkResTable[lo + 1u], kk - (float)lo);
	*peak = 0;
	for (b = 0; b < nb; b++) {
		int16_t *buf = &out[b * BLK];

		if (b * BLK < 11027)   /* 0.25 s of saw, then the input is silent */
			calcWavetableOscBlock(&osc, (const int16_t (*)[1024])sawTable, buf, BLK, 1.0f);
		else
			memset(buf, 0, sizeof(int16_t) * BLK);
		DiodeLadder_renderB2(&dl, buf, ALK_SAW_SCALE_V, il * alk_prewarp(il, knob), 50e-6f);
		for (j = 0; j < BLK; j++) {
			if (abs(buf[j]) > *peak) *peak = (int16_t)abs(buf[j]);
			if (b * BLK + j >= 2 * 11027) { acc += (double)buf[j] * buf[j]; n++; }
		}
	}
	return n ? sqrt(acc / n) : 0.0;
}

static void test_selfosc(void)
{
	int16_t pk15, pk10;
	const double r15 = sustain_rms(1.0f, &pk15);
	const double r10 = sustain_rms(1.0f / ALK_RES_KMAX, &pk10);

	printf("AL-SO: tail RMS x1.5 %.1f LSB (peak %d), 303 max %.1f LSB\n", r15, pk15, r10);
	if (r15 < 100.0) fail(G_SO, "x1.5 sustains after the input stops", r15, 100.0);
	if (pk15 >= 32767) fail(G_SO, "x1.5 bounded below full scale", pk15, 32767);
	if (r10 > 10.0) fail(G_SO, "the 303's maximum does not sustain", r10, 10.0);
}

/* ---------------------------------------------------------------- AL-PS
 * S093 P6: pos on Saw/Sqr.
 *   - pos 0: a note does not touch the phase (free-running, as before);
 *   - pos 64: every non-legato note starts at half a cycle (phase 1 << 31);
 *   - legato keeps the running phase;
 *   - a pos sweep 0 -> 100 -> 0 leaves no lasting pitch change: after it,
 *     one block advances the phase by exactly 32 x phaseInc. */
static void test_pos(void)
{
	AlkalineVoice v;
	int16_t buf[BLK];
	uint32_t ph0;
	int n;

	memset(&v, 0, sizeof(v));
	Alkaline_initVoice(&v);
	v.osc.phase = 0x12345678u;
	Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);
	if (v.osc.phase != 0x12345678u) fail(G_PS, "pos 0 leaves the phase alone", v.osc.phase, 0);
	v.osc.position = 64u;
	for (n = 0; n < 3; n++) block(&v, buf, K_B2R);     /* let the bend settle */
	Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);
	if (v.osc.phase != (64u << 25)) fail(G_PS, "pos 64 starts at half a cycle", v.osc.phase, 64u << 25);
	v.slide = 1u;
	Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);  /* latches the slide */
	block(&v, buf, K_B2R);
	ph0 = v.osc.phase;
	Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);  /* legato */
	if (v.osc.phase != ph0) fail(G_PS, "legato keeps the phase", v.osc.phase, ph0);
	for (n = 0; n <= 100; n += 10) { v.osc.position = (uint8_t)n; block(&v, buf, K_B2R); }
	for (n = 100; n >= 0; n -= 10) { v.osc.position = (uint8_t)n; block(&v, buf, K_B2R); }
	for (n = 0; n < 4; n++) block(&v, buf, K_B2R);
	ph0 = v.osc.phase;
	block(&v, buf, K_B2R);
	if (v.osc.phase - ph0 != 32u * v.osc.phaseInc)
		fail(G_PS, "no lasting pitch change after a pos sweep", v.osc.phase - ph0, 32u * v.osc.phaseInc);
}

/* ------------------------------------------------------------ check modes */
static int mode_traces(const char *in, const char *outp, const float drv)
{
	/* IN: int32 n, float32 k, then n x (s volts, i_lad A, i_abc A).
	 * OUT: n float32 OTA volts for each kernel in turn: B2R, A-R, B-R.
	 * The input is s x drv, carried as int16 at 0.8 mV per LSB (+-26 V: room
	 * for x8), as tools/alkaline/variants/dlv_host.c does. */
	FILE *f = fopen(in, "rb");
	int32_t n;
	float k;

	if (!f || fread(&n, 4, 1, f) != 1 || fread(&k, 4, 1, f) != 1)
		return 2;
	{
		float *d = malloc(sizeof(float) * 3u * (size_t)n);
		float *o = malloc(sizeof(float) * 3u * (size_t)n);
		const float sScale = 8e-4f;
		int opt;

		if (fread(d, sizeof(float), 3u * (size_t)n, f) != 3u * (size_t)n)
			return 2;
		fclose(f);
		for (opt = 0; opt < 3; opt++) {
			DiodeLadder dl;
			const float kk = k * (float)(ALK_RES_POINTS - 1u);
			const unsigned lo = (unsigned)fminf(kk, (float)(ALK_RES_POINTS - 2u));
			int b, j;

			DiodeLadder_init(&dl);
			DiodeLadder_setNet(&dl, &alkResTable[lo], &alkResTable[lo + 1u], kk - (float)lo);
			for (b = 0; b + BLK <= n; b += BLK) {
				int16_t buf[BLK];
				const float il = d[3 * (b + BLK - 1) + 1];
				const float ia = d[3 * (b + BLK - 1) + 2];

				for (j = 0; j < BLK; j++)
					buf[j] = (int16_t)lrintf(drv * d[3 * (b + j)] / sScale);
				render_k(opt, &dl, buf, sScale, il * alk_prewarp(il, k), ia);
				for (j = 0; j < BLK; j++)
					o[opt * n + b + j] = (float)buf[j] / DL_OUT_SCALE;
			}
		}
		f = fopen(outp, "wb");
		fwrite(o, sizeof(float), 3u * (size_t)n, f);
		fclose(f);
		free(d);
		free(o);
	}
	return 0;
}

static int mode_pattern(int argc, char **argv)
{
	/* the S092 P4 pattern: (semitones above C2, accent, slide); -1 = rest */
	static const int pat[8][3] = { {0, 0, 0}, {0, 1, 0}, {3, 0, 1}, {7, 0, 0},
	                               {12, 1, 1}, {10, 1, 0}, {-1, 0, 0}, {0, 1, 0} };
	const double step = 0.125, t0 = 0.05, dt = BLK / 44108.0;
	const int nb = (int)((t0 + 8 * step + 0.25) / dt);
	AlkalineVoice v;
	FILE *fo;
	int opt, b, next = 0;

	if (argc < 9)
		return 2;
	opt = (argc > 9) ? atoi(argv[9]) : K_B2R;
	memset(&v, 0, sizeof(v));
	Alkaline_initVoice(&v);
	v.drive = (argc > 10) ? (float)atof(argv[10]) : 0.0f;
	v.osc.waveform = (uint8_t)atoi(argv[2]);
	v.cutoff = (float)atof(argv[3]);
	v.reso = (float)atof(argv[4]);
	v.envMod = (float)atof(argv[5]);
	v.decay = (float)atof(argv[6]);
	v.accent = (float)atof(argv[7]);
	fo = fopen(argv[8], "wb");
	for (b = 0; b < nb; b++) {
		const double t = b * dt - t0;
		int16_t buf[BLK];
		float rec[2];

		while (next < 8 && next * step <= t + 1e-9) {
			if (pat[next][0] >= 0) {
				v.slide = (uint8_t)pat[next][2];
				Alkaline_triggerVoice(&v, 0u, pat[next][1] ? 127u : 80u,
				                      (uint8_t)(63 + pat[next][0]), ALK_STEP);
			}
			next++;
		}
		block(&v, buf, opt);
		rec[0] = v.iLadRaw;
		rec[1] = v.iAbcEnd;
		fwrite(rec, sizeof(float), 2, fo);
		fwrite(buf, sizeof(int16_t), BLK, fo);
	}
	fclose(fo);
	return 0;
}

/* --sweep W DRV OUT: the S093 P3 listening test for the Res table. A 16th
 * bass line at 120 BPM (C2 C2 D#2 C3, the last accented, the third sliding),
 * Res swept 0 -> 1 over 6 s, then 0.6 +- 0.3 at 1.5 Hz for 4 s (an LFO on
 * res); cutoff 0.35, env 0.5, decay 0.4, accent 0.6. OUT: int16 per sample. */
static int mode_sweep(int argc, char **argv)
{
	static const int semi[4] = { 0, 0, 3, 12 };
	const int nb = (int)(10.0 * 44108.0 / BLK);
	AlkalineVoice v;
	FILE *fo;
	int b, step = 0;

	if (argc < 5)
		return 2;
	memset(&v, 0, sizeof(v));
	Alkaline_initVoice(&v);
	v.osc.waveform = (uint8_t)atoi(argv[2]);
	v.drive = (float)atof(argv[3]);
	v.cutoff = 0.35f; v.envMod = 0.5f; v.decay = 0.4f; v.accent = 0.6f;
	fo = fopen(argv[4], "wb");
	for (b = 0; b < nb; b++) {
		const double t = (double)b * BLK / 44108.0;
		int16_t buf[BLK];

		v.reso = (float)(t < 6.0 ? t / 6.0 : 0.6 + 0.3 * sin(2.0 * M_PI * 1.5 * (t - 6.0)));
		while ((double)step * ALK_STEP <= (double)b * BLK) {
			v.slide = (uint8_t)((step & 3) == 2);
			Alkaline_triggerVoice(&v, 0u, (step & 3) == 3 ? 127u : 80u,
			                      (uint8_t)(63 + semi[step & 3]), ALK_STEP);
			step++;
		}
		block(&v, buf, K_B2R);
		fwrite(buf, sizeof(int16_t), BLK, fo);
	}
	fclose(fo);
	return 0;
}

/* ---------------------------------------------------------------- AL-W
 * S093 P8: typ wtb plays the bound frame's 1,024-point row through the same
 * fused table loop as Saw/Sqr. Install: one 3-frame table of 600-point 16-bit
 * frames (the card's AKWF length, so the copy is resampled) with different
 * harmonics per frame, and one 2-frame table of 1,024-point frames (copied as
 * they are). */
static void w_install(int second)
{
	static int16_t f600[3][600];
	static int16_t f1024[2][1024];
	const sample_install_fmt_t fw = { 1u, 1u, 44100u, 2u, 16u, 16u };
	int i, f;

	for (f = 0; f < 3; f++)
		for (i = 0; i < 600; i++)
			f600[f][i] = (int16_t)(28000.0 * sin(6.283185307 * i * (2 * f + 1) / 600.0));
	for (f = 0; f < 2; f++)
		for (i = 0; i < 1024; i++)
			f1024[f][i] = (int16_t)(20000.0 * sin(6.283185307 * i * (f + 3) / 1024.0));
	/* the install's frame buffer (the firmware borrows the name cache) */
	static int16_t fbuf[WAVETABLE_FRAME_MAX];

	catalogue_host_reset();
	sampleInstall_begin();
	if (!second) {
		(void)sampleInstall_beginWavetable("w600    ", 600u, 3u, &fw);
		for (f = 0; f < 3; f++)
			(void)sampleInstall_writeFrame((const uint8_t *)f600[f], 600u, fbuf);
		(void)sampleInstall_finishWavetable();
	}
	(void)sampleInstall_beginWavetable("w1024   ", 1024u, 2u, &fw);
	for (f = 0; f < 2; f++)
		(void)sampleInstall_writeFrame((const uint8_t *)f1024[f], 1024u, fbuf);
	(void)sampleInstall_finishWavetable();
	(void)sampleInstall_commit();
	sampleMemory_refresh();
}

/* pos -> the frame position: the law alk_wtbFramePos() implements, restated */
static float w_framePos(uint32_t pos, uint32_t frames)
{
	return (float)pos * (float)(frames - 1u) * (1.0f / 127.0f);
}

/* one block's glide, restated (revision 3): toward the target by at most one
 * frame, then never past the next frame boundary in the direction of travel
 * (the same float operations as alk_renderWtb(), so the result is exact) */
static float w_glide(float start, float target)
{
	float d = target - start;
	float end;

	if (d > 1.0f) d = 1.0f;
	if (d < -1.0f) d = -1.0f;
	end = start + d;
	if (end > start && end > floorf(start) + 1.0f)
		end = floorf(start) + 1.0f;
	if (end < start && end < ceilf(start) - 1.0f)
		end = ceilf(start) - 1.0f;
	return end;
}

/* the reference block without a slot: the voice's async, then
 * calcWavetableOscBlock()'s table read (gain 1) of `row` and the plain B2R
 * at the Saw scale */
static void w_ref(AlkalineVoice *v, const int16_t *row, int16_t *buf)
{
	uint32_t ph;
	int j;

	Alkaline_calcVoiceAsync(v);
	ph = v->osc.phase;
	for (j = 0; j < BLK; j++) {
		const uint32_t itg = ph >> 22;
		int16_t o = row[itg];

		o += (ph & 0x003FFFFFu) * 2.384185791015625e-07f
		   * (row[(itg + 1u) & 0x000003FFu] - o);
		ph += v->osc.phaseInc;
		buf[j] = o;
	}
	v->osc.phase = ph;
	DiodeLadder_renderB2(&v->ladder, buf, ALK_SAW_SCALE_V * v->driveGain,
	                     v->iLadEnd, v->iAbcEnd * v->makeup);
}

/* the reference block with a slot: the voice's async, then the two-row loop
 * on the frame pair holding [start, end] (its lower frame, capped at
 * frames - 2), the mix ramping from start - p to end - p across the block */
static void w_refBlend(AlkalineVoice *v, const int16_t *rows, uint32_t frames,
                       float start, float end, int16_t *buf)
{
	uint32_t p = (uint32_t)(start < end ? start : end);

	Alkaline_calcVoiceAsync(v);
	if (p > frames - 2u)
		p = frames - 2u;
	DiodeLadder_renderB2Osc2(&v->ladder, buf, rows + p * WAVETABLE_ROW_POINTS,
	                         rows + (p + 1u) * WAVETABLE_ROW_POINTS,
	                         &v->osc.phase, v->osc.phaseInc,
	                         start - (float)p, (end - start) * (1.0f / (float)BLK),
	                         ALK_SAW_SCALE_V * v->driveGain,
	                         v->iLadEnd, v->iAbcEnd * v->makeup);
}

/* W1/W2: the voice against the references, 3,000 blocks of 16ths with
 * accents and slides; pos jumps every 23 blocks (the position glides toward
 * it), a non-legato note jumps the position to it. grant: 0 never a slot (the
 * nearest frame), 1 always (the blend), 2 a slot that comes and goes every 50
 * blocks (the switch between the two paths keeps the phase and position) */
static void w_run(const wavetable_catalogue_entry_t *e, int grant, const char *tag)
{
	AlkalineVoice a, b;
	const int16_t *const rows = sampleMemory_wavetableRows(e);
	const uint32_t frames = e->frame_count;
	float expect = 0.0f;
	unsigned diff = 0u, phd = 0u, pod = 0u, between = 0u, blends = 0u;
	double rms = 0.0;
	int n, j;

	memset(&a, 0, sizeof(a));
	Alkaline_initVoice(&a);
	a.cutoff = 0.55f; a.reso = 0.6f; a.envMod = 0.6f; a.accent = 0.7f; a.drive = 0.3f;
	Alkaline_setOscType(&a, ALK_TYP_WTB);
	a.osc.waveform = 0u;
	b = a;
	for (n = 0; n < 3000; n++) {
		int16_t ba[BLK], bb[BLK];
		float start;

		w_grant = (grant == 2) ? ((n / 50) & 1) : grant;
		if (n % 23 == 0)
			a.osc.position = b.osc.position = (uint8_t)((n * 37) & 127);
		if (n % 172 == 0) {
			const uint8_t legato = (uint8_t)(a.slideLatch && a.gateLeft > 0);

			a.slide = b.slide = (uint8_t)((n / 172) % 4 == 2);
			Alkaline_triggerVoice(&a, 0u, (n / 172) % 3 ? 80u : 127u,
			                      (uint8_t)(40 + (n * 7) % 40), ALK_STEP);
			Alkaline_triggerVoice(&b, 0u, (n / 172) % 3 ? 80u : 127u,
			                      (uint8_t)(40 + (n * 7) % 40), ALK_STEP);
			if (!legato)    /* a note starts on pos's position */
				expect = w_framePos(a.osc.position & 127u, frames);
		}
		/* the block: from where the last ended, one glide step toward pos */
		start = expect;
		expect = w_glide(start, w_framePos(a.osc.position & 127u, frames));
		Alkaline_calcVoiceAsync(&a);
		Alkaline_calcVoiceSyncBlock(&a, ba, BLK);
		if (w_grant) {
			w_refBlend(&b, rows, frames, start, expect, bb);
			blends++;
		} else {
			/* the nearest frame to the block's end position */
			w_ref(&b, rows + (uint32_t)(expect + 0.5f) * WAVETABLE_ROW_POINTS, bb);
		}
		between += (expect != floorf(expect));
		for (j = 0; j < BLK; j++) {
			diff += (ba[j] != bb[j]);
			rms += (double)ba[j] * ba[j];
		}
		phd += (a.osc.phase != b.osc.phase);
		pod += (a.osc.waveInterpFrac != expect);
	}
	printf("AL-W %s: %u sample(s), %u phase(s), %u position(s) differ; "
	       "%u blended block(s), %u between frames; RMS %.0f LSB\n",
	       tag, diff, phd, pod, blends, between, sqrt(rms / (3000.0 * BLK)));
	if (diff || phd) fail(G_W, "wtb voice != its reference", diff, phd);
	if (pod) fail(G_W, "frame position glide law", pod, 0);
	if (!between) fail(G_W, "the position never fell between frames", 0, 0);
	if (rms == 0.0) fail(G_W, "wtb is silent", 0, 0);
}

/* W5: the two-row loop itself. Reference: the frames mixed in float with the
 * same ramp, truncated to int16 and fed to the unfused B2R; the two differ
 * only by that truncation (the one-row loop truncates the same way). Each
 * block ramps the mix up or down across part of 0..1. The other check: the
 * output is far from frame A alone when the mix is near 1. */
static void w_loop(const int16_t *rowA, const int16_t *rowB)
{
	AlkalineVoice v;
	DiodeLadder l1, l2, l3;
	uint32_t p1 = 0x12345678u, p2 = 0x12345678u, p3 = 0x12345678u;
	int maxd = 0, peak = 0, far = 0;
	int n, j;

	memset(&v, 0, sizeof(v));
	Alkaline_initVoice(&v);
	v.cutoff = 0.5f; v.reso = 0.5f; v.envMod = 0.3f; v.drive = 0.2f;
	Alkaline_triggerVoice(&v, 0u, 100u, 48u, ALK_STEP);
	/* a few blocks of the voice so the VEG is open, then its last block's
	 * currents and network are held for the comparison */
	for (n = 0; n < 12; n++) {
		int16_t w[BLK];

		block(&v, w, K_B2R);
	}
	l1 = l2 = l3 = v.ladder;
	for (n = 0; n < 600; n++) {
		const float ff0 = (float)((n * 7) % 9) / 8.0f;
		const float ffStep = (n & 1) ? (1.0f - ff0) / (float)BLK : -ff0 / (float)BLK;
		const float sc = ALK_SAW_SCALE_V * v.driveGain;
		int16_t b1[BLK], b2[BLK], b3[BLK];
		uint32_t ph = p1;
		float ff = ff0;

		for (j = 0; j < BLK; j++) {
			const uint32_t itg = ph >> 22, nx = (itg + 1u) & 0x3FFu;
			const float fr = (float)(ph & 0x003FFFFFu) * 2.384185791015625e-07f;
			const float x = (float)rowA[itg] + fr * (float)(rowA[nx] - rowA[itg]);
			const float y = (float)rowB[itg] + fr * (float)(rowB[nx] - rowB[itg]);

			b1[j] = (int16_t)(x + ff * (y - x));
			ff += ffStep;
			ph += v.osc.phaseInc;
		}
		p1 = ph;
		DiodeLadder_renderB2(&l1, b1, sc, v.iLadEnd, v.iAbcEnd * v.makeup);
		DiodeLadder_renderB2Osc2(&l2, b2, rowA, rowB, &p2, v.osc.phaseInc, ff0, ffStep,
		                         sc, v.iLadEnd, v.iAbcEnd * v.makeup);
		DiodeLadder_renderB2Osc(&l3, b3, rowA, &p3, v.osc.phaseInc, sc,
		                        v.iLadEnd, v.iAbcEnd * v.makeup);
		for (j = 0; j < BLK; j++) {
			const int d = abs(b1[j] - b2[j]);

			if (d > maxd) maxd = d;
			if (abs(b2[j]) > peak) peak = abs(b2[j]);
			if (ff0 >= 0.875f && !(n & 1) && abs(b2[j] - b3[j]) > far)
				far = abs(b2[j] - b3[j]);
		}
	}
	printf("AL-W two-row loop: max %d LSB from the float-mix reference (peak %d); "
	       "%d LSB from frame A alone at mix ~1\n", maxd, peak, far);
	if (peak == 0) fail(G_W, "two-row loop test is silent", 0, 0);
	if (p1 != p2) fail(G_W, "two-row loop phase", (double)p1, (double)p2);
	if (maxd > peak / 256 + 2) fail(G_W, "two-row loop != mixed frames", maxd, peak);
	if (far < peak / 8) fail(G_W, "two-row loop ignores frame B", far, peak);
}

static void test_wavetable(void)
{
	const wavetable_catalogue_entry_t *e;
	int n, j;

	w_install(0);
	e = sampleMemory_wavetableEntry(0u);
	if (!e) { fail(G_W, "no table installed", 0, 0); return; }
	w_run(e, 0, "no slot");
	w_run(e, 1, "slot");
	w_run(e, 2, "slot on/off");
	w_loop(sampleMemory_wavetableRows(e),
	       sampleMemory_wavetableRows(e) + 2u * WAVETABLE_ROW_POINTS);

	/* W3: an install rebinds at the next block: the table index 0 now names
	 * the 1,024-point table (generation changed); the bind keeps the first
	 * row and jumps the position to pos */
	{
		AlkalineVoice a;
		int16_t buf[BLK];
		const wavetable_catalogue_entry_t *e2;

		memset(&a, 0, sizeof(a));
		Alkaline_initVoice(&a);
		Alkaline_setOscType(&a, ALK_TYP_WTB);
		a.osc.waveform = 0u;
		a.osc.position = 127u;
		block(&a, buf, K_B2R);
		w_install(1);
		e2 = sampleMemory_wavetableEntry(0u);
		Alkaline_calcVoiceAsync(&a);
		if (!e2 || a.osc.sampleCacheGeneration != sampleMemory_getGeneration() ||
		    a.osc.sampleCacheSize != e2->frame_count ||
		    a.osc.sampleCacheData != sampleMemory_wavetableRows(e2) ||
		    a.osc.waveInterpFrac != w_framePos(127u, e2->frame_count))
			fail(G_W, "an install does not rebind", 0, 0);
		Alkaline_calcVoiceSyncBlock(&a, buf, BLK);
	}

	/* W4: an empty catalogue plays silence through the zero row (one
	 * frame), and never claims a slot even when one would be granted */
	catalogue_host_reset();
	sampleMemory_refresh();
	{
		AlkalineVoice z;
		int16_t buf[BLK];
		int peak = 0;

		w_grant = 1;
		w_claims = 0u;
		memset(&z, 0, sizeof(z));
		Alkaline_initVoice(&z);
		z.reso = 1.0f; z.drive = 1.0f;
		Alkaline_setOscType(&z, ALK_TYP_WTB);
		for (n = 0; n < 400; n++) {
			if (n % 100 == 0)
				Alkaline_triggerVoice(&z, 0u, 127u, 60u, ALK_STEP);
			block(&z, buf, K_B2R);
			for (j = 0; j < BLK; j++)
				if (abs(buf[j]) > peak) peak = abs(buf[j]);
		}
		printf("AL-W empty catalogue: peak %d LSB, %u claim(s)\n", peak, w_claims);
		if (peak != 0) fail(G_W, "no table is not silent", peak, 0);
		if (w_claims != 0u) fail(G_W, "a one-frame table claims a slot", w_claims, 0);
		w_grant = 0;
	}
}

/* ---------------------------------------------------------------- golden
 * The Alkaline golden capture and law AL-OD0 (S094 P1 S0/S4).
 *
 * What:       gold_render() plays a fixed set of voice configurations into
 *             one int16 stream:
 *               1. typ wav, Saw then Sqr, x drive bytes 0, 64, 127: the S092
 *                  P4 pattern (mode_pattern()'s steps) at fixed knobs;
 *               2. typ wav, Saw, drive 0: the mode_sweep() Res sweep, 4 s;
 *               3. typ wtb (w_install(1): two tables) at drive 0, the P4
 *                  pattern, once without a slot (w_grant 0) and once with
 *                  (w_grant 1).
 *             --golden OUT writes that stream to OUT (S0, before any firmware
 *             change). test_golden() renders it again and compares it with
 *             the committed file:
 *               - every sample of every drive-0 render must be byte-identical;
 *               - drive bytes 64 and 127 may differ by at most 2 LSB (A3:
 *                 their table entries may sit 1 ulp from the runtime
 *                 exp2 polynomial, plan finding 5); the largest difference is
 *                 printed either way.
 * Why:        P9-D7 (user): Overdrive 0 and Tone 127 must leave the output
 *             bit-identical to today. The capture is made from the S0 tree, so
 *             every later step (S1..S5) is checked against today's voice, not
 *             against itself. The voice's new fields (S4: vcaDrive 0, tone 127)
 *             are set by Alkaline_initVoice(), so this code never names them
 *             and compiles unchanged before and after S4.
 * Inputs:     none (fixed configurations); OUT path (--golden); the committed
 *             file baselines/s094p1/alk_golden.bin (test_golden()).
 * Outputs:    the file (--golden); law AL-OD0 (test_golden()).
 * Accessors:  main() (--golden mode; test_golden() in the law run).
 * Affiliates: mode_pattern(), mode_sweep(), w_install(), w_grant;
 *             S094_P0_ALKALINE_POST_FX.md §4.
 */
#define GOLD_PATH   "baselines/s094p1/alk_golden.bin"
/* blocks per pattern render: the pattern's 8 steps at 0.125 s plus 0.30 s
 * of tail, 1.3 s x 44,108 / 32 = 1,791.8 -> 1,792 blocks (an integer
 * literal, so the buffer sizes below are constant expressions) */
#define GOLD_PAT_BLOCKS   1792
/* blocks of the Res sweep: 4 s x 44,108 / 32 = 5,513.5 -> 5,513 */
#define GOLD_SWEEP_BLOCKS 5513
/* configurations: 2 waves x 3 drives + 1 sweep + 2 wtb */
#define GOLD_RUNS   9
/* samples in the whole capture: 8 pattern renders and one sweep */
#define GOLD_TOTAL  ((size_t)(8 * GOLD_PAT_BLOCKS + GOLD_SWEEP_BLOCKS) * BLK)

/* one pattern render into out; returns the number of samples written */
static size_t gold_pattern(AlkalineVoice *v, int16_t *out)
{
	/* the S092 P4 pattern: (semitones above C2, accent, slide); -1 = rest */
	static const int pat[8][3] = { {0, 0, 0}, {0, 1, 0}, {3, 0, 1}, {7, 0, 0},
	                               {12, 1, 1}, {10, 1, 0}, {-1, 0, 0}, {0, 1, 0} };
	/* samples per pattern step: 0.125 s */
	const int stepBlocks = (int)(0.125 * 44108.0 / BLK);
	int b, next = 0;

	for (b = 0; b < GOLD_PAT_BLOCKS; b++) {
		/* trigger each step on the first block at or after its start */
		if (next < 8 && b >= next * stepBlocks) {
			if (pat[next][0] >= 0) {
				v->slide = (uint8_t)pat[next][2];
				Alkaline_triggerVoice(v, 0u, pat[next][1] ? 127u : 80u,
				                      (uint8_t)(63 + pat[next][0]), ALK_STEP);
			}
			next++;
		}
		block(v, out + (size_t)b * BLK, K_B2R);
	}
	return (size_t)GOLD_PAT_BLOCKS * BLK;
}

/* a voice at the golden knobs: cutoff, Res, env, decay, accent fixed;
 * drive = byte / 127 (the descriptor's TYPE_FLT store) */
static void gold_voice(AlkalineVoice *v, uint8_t wave, uint8_t driveByte)
{
	memset(v, 0, sizeof(*v));
	Alkaline_initVoice(v);
	v->osc.waveform = wave;
	v->drive = (float)driveByte / 127.0f;
	v->cutoff = 0.45f; v->reso = 0.75f; v->envMod = 0.6f;
	v->decay = 0.4f; v->accent = 0.7f;
}

/*
 * Render every golden configuration.
 *
 * Inputs:  out (GOLD_TOTAL samples), drv0 (per-run flag array, GOLD_RUNS:
 *          1 when the run is at drive byte 0), len (per-run sample counts).
 * Outputs: out; drv0; len; returns the number of samples (GOLD_TOTAL).
 */
static size_t gold_render(int16_t *out, uint8_t *drv0, size_t *len)
{
	static const uint8_t drives[3] = { 0u, 64u, 127u };
	AlkalineVoice v;
	size_t n = 0u;
	int r = 0;

	/* 1. wav: Saw, Sqr x the three drive bytes */
	for (int w = 0; w < 2; w++)
		for (int d = 0; d < 3; d++, r++) {
			gold_voice(&v, (uint8_t)w, drives[d]);
			len[r] = gold_pattern(&v, out + n);
			drv0[r] = (uint8_t)(drives[d] == 0u);
			n += len[r];
		}
	/* 2. the Res sweep (mode_sweep()'s line, 4 s), Saw, drive 0 */
	{
		static const int semi[4] = { 0, 0, 3, 12 };
		int step = 0;

		gold_voice(&v, ALK_WAVE_SAW, 0u);
		v.cutoff = 0.35f; v.envMod = 0.5f; v.decay = 0.4f; v.accent = 0.6f;
		for (int b = 0; b < GOLD_SWEEP_BLOCKS; b++) {
			const double t = (double)b * BLK / 44108.0;

			/* Res 0 -> 1 over the 4 s */
			v.reso = (float)(t / 4.0);
			while ((double)step * ALK_STEP <= (double)b * BLK) {
				v.slide = (uint8_t)((step & 3) == 2);
				Alkaline_triggerVoice(&v, 0u, (step & 3) == 3 ? 127u : 80u,
				                      (uint8_t)(63 + semi[step & 3]), ALK_STEP);
				step++;
			}
			block(&v, out + n + (size_t)b * BLK, K_B2R);
		}
		len[r] = (size_t)GOLD_SWEEP_BLOCKS * BLK;
		drv0[r] = 1u;
		n += len[r];
		r++;
	}
	/* 3. wtb: two installed tables, without and with an interpolation slot */
	w_install(1);
	for (int g = 0; g < 2; g++, r++) {
		gold_voice(&v, 1u, 0u);
		Alkaline_setOscType(&v, ALK_TYP_WTB);
		v.osc.position = 40u;             /* between frames: the blend is heard */
		w_grant = g;
		len[r] = gold_pattern(&v, out + n);
		drv0[r] = 1u;
		n += len[r];
	}
	w_grant = 0;
	return n;
}

/* --golden OUT: write the capture (run once, at S0) */
static int mode_golden(const char *path)
{
	uint8_t drv0[GOLD_RUNS];
	size_t len[GOLD_RUNS];
	int16_t *buf = malloc(GOLD_TOTAL * sizeof(int16_t));
	const size_t n = gold_render(buf, drv0, len);
	FILE *fo;

	fo = fopen(path, "wb");
	if (!fo || fwrite(buf, sizeof(int16_t), n, fo) != n)
		return 1;
	fclose(fo);
	free(buf);
	printf("golden: %zu samples written to %s\n", n, path);
	return 0;
}

/* law AL-OD0: today's renders against the committed capture */
static void test_golden(void)
{
	uint8_t drv0[GOLD_RUNS];
	size_t len[GOLD_RUNS];
	const size_t n = GOLD_TOTAL;
	int16_t *now = malloc(n * sizeof(int16_t)), *ref = malloc(n * sizeof(int16_t));
	FILE *fi = fopen(GOLD_PATH, "rb");
	size_t off = 0u;

	if (!fi || fread(ref, sizeof(int16_t), n, fi) != n) {
		fail(G_OD0, "golden file missing or short", 0, 0);
		if (fi) fclose(fi);
		free(now); free(ref);
		return;
	}
	fclose(fi);
	gold_render(now, drv0, len);
	for (int r = 0; r < GOLD_RUNS; r++) {
		int worst = 0;

		/* the largest per-sample difference of this run */
		for (size_t i = 0; i < len[r]; i++) {
			const int d = abs((int)now[off + i] - (int)ref[off + i]);

			if (d > worst) worst = d;
		}
		printf("AL-OD0 run %d (drive-0 %u): largest difference %d LSB\n", r, drv0[r], worst);
		/* drive 0: byte-identical; drive bytes 64/127: A3's 1-ulp entries */
		if (drv0[r] ? (worst != 0) : (worst > 2))
			fail(G_OD0, "golden render differs", r, worst);
		off += len[r];
	}
	free(now); free(ref);
}

/* ---------------------------------------------------------------- AL-TY
 * S093 P8: the typ writer. */
static void test_typ(void)
{
	AlkalineVoice v;

	w_install(0);   /* two tables (w600, w1024): the wtb list's last entry is 1 */
	memset(&v, 0, sizeof(v));
	Alkaline_initVoice(&v);
	v.osc.phase = 0x12345678u;
	v.osc.waveform = 5u;
	Alkaline_setOscType(&v, ALK_TYP_WTB);
	if (v.osc.oscType != OSC_TYPE_WTB) fail(G_TY, "1 is wtb", v.osc.oscType, 0);
	if (v.osc.waveform != 1u) fail(G_TY, "wtb clamps to the last table", v.osc.waveform, 0);
	if (v.osc.phase != 0x12345678u) fail(G_TY, "the phase runs on", 0, 0);
	if (v.osc.sampleCacheLooped != ALK_BIND_NONE) fail(G_TY, "the bind is cleared", 0, 0);
	v.osc.sampleCacheLooped = ALK_BIND_WTB;
	/* S094 P1 S5: 2 and above are pwm now, so the no-op check uses wtb itself */
	Alkaline_setOscType(&v, ALK_TYP_WTB);
	if (v.osc.oscType != OSC_TYPE_WTB || v.osc.sampleCacheLooped != ALK_BIND_WTB)
		fail(G_TY, "the same type is a no-op", v.osc.oscType, 0);
	v.osc.waveform = 9u;
	Alkaline_setOscType(&v, ALK_TYP_WAV);
	if (v.osc.oscType != OSC_TYPE_WAV) fail(G_TY, "0 is wav", v.osc.oscType, 0);
	if (v.osc.waveform != ALK_WAVE_TRI) fail(G_TY, "wav clamps to Tri", v.osc.waveform, 0);
	if (v.osc.phase != 0x12345678u) fail(G_TY, "the phase runs on (wtb -> wav)", 0, 0);
	/* S093 P8 revision 3: a type change drops the interpolation slot (the
	 * stamp and the grant), as Drum's osc_setType() does (S087 A18) */
	v.osc.interpStamp = 7u;
	v.wtbSlot = 1u;
	Alkaline_setOscType(&v, ALK_TYP_WTB);
	if (v.osc.interpStamp != 0u || v.wtbSlot != 0u)
		fail(G_TY, "a type change keeps the slot", v.osc.interpStamp, v.wtbSlot);
	/* S094 P1 S5: 2 and above are pwm; S094 P4: pwm clamps wav to Tri; the
	 * phase runs on */
	v.osc.waveform = 9u;
	v.osc.phase = 0x0badf00du;
	Alkaline_setOscType(&v, 7u);
	if (v.osc.oscType != OSC_TYPE_PWM) fail(G_TY, "7 is pwm", v.osc.oscType, 0);
	if (v.osc.waveform != ALK_WAVE_TRI) fail(G_TY, "pwm clamps to Tri", v.osc.waveform, 0);
	if (v.osc.phase != 0x0badf00du) fail(G_TY, "the phase runs on (-> pwm)", 0, 0);
	/* S094 P4: Tri (2) is in both lists: pwm Tri -> wav stays Tri */
	v.osc.waveform = ALK_WAVE_TRI;
	Alkaline_setOscType(&v, ALK_TYP_WAV);
	if (v.osc.waveform != ALK_WAVE_TRI) fail(G_TY, "pwm Tri -> wav Tri", v.osc.waveform, 0);
}

/* ---------------------------------------------------------------- AL-A3
 * S094 P1 S4: the A3 tables against the runtime formula.
 *   alkDriveGain[k] / alkDriveMakeup[k] against (alk_exp2f(k/127)^2)^2 and
 *   alk_exp2f(1 - k/127) evaluated here (the host compiler's contraction):
 *   entry 0 exactly; every entry within 1 ulp. */
static int ulps(float a, float b)
{
	int32_t ia, ib;

	memcpy(&ia, &a, 4);
	memcpy(&ib, &b, 4);
	return abs(ia - ib);
}

static void test_a3(void)
{
	int worst = 0;

	for (unsigned k = 0u; k < ALK_POST_POINTS; k++) {
		const float drv = (float)k / 127.0f;
		const float t = alk_exp2f(drv), t2 = t * t;
		const float g = t2 * t2, m = alk_exp2f(ALK_MAKEUP_OCT * (1.0f - drv));
		const int e = ulps(g, alkDriveGain[k]) > ulps(m, alkDriveMakeup[k])
		            ? ulps(g, alkDriveGain[k]) : ulps(m, alkDriveMakeup[k]);

		if (e > worst) worst = e;
		if (k == 0u && e != 0) fail(G_A3, "drive 0 exact", e, 0);
		if (e > 1) fail(G_A3, "entry within 1 ulp", k, e);
	}
	printf("AL-A3: largest difference %d ulp\n", worst);
}

/* ---------------------------------------------------------------- AL-OV
 * S094 P1 S4: the VCA overdrive. The pattern at overdrive bytes 0, 64, 127
 * (drive 0, tone open): every sample finite and inside int16 (the store
 * saturates); the crest factor (peak / RMS) falls as the overdrive rises (the
 * OTA's tanh flattens the peaks). */
static double ov_crest(uint8_t ovd, double *rms)
{
	static int16_t buf[GOLD_PAT_BLOCKS * BLK];
	AlkalineVoice v;
	double s = 0.0;
	int pk = 0;

	gold_voice(&v, ALK_WAVE_SAW, 0u);
	v.vcaDrive = ovd;
	gold_pattern(&v, buf);
	for (size_t i = 0; i < (size_t)GOLD_PAT_BLOCKS * BLK; i++) {
		s += (double)buf[i] * buf[i];
		if (abs(buf[i]) > pk) pk = abs(buf[i]);
	}
	*rms = sqrt(s / ((double)GOLD_PAT_BLOCKS * BLK));
	return (*rms > 0.0) ? pk / *rms : 0.0;
}

static void test_overdrive(void)
{
	double r0, r64, r127;
	const double c0 = ov_crest(0u, &r0), c64 = ov_crest(64u, &r64), c127 = ov_crest(127u, &r127);

	printf("AL-OV: crest %.2f / %.2f / %.2f, RMS %.0f / %.0f / %.0f LSB at overdrive 0 / 64 / 127\n",
	       c0, c64, c127, r0, r64, r127);
	if (!(c127 < c0)) fail(G_OV, "the overdrive flattens the peaks", c0, c127);
	if (!(r0 > 0.0 && r64 > 0.0 && r127 > 0.0)) fail(G_OV, "silent", r64, r127);
}

/* ---------------------------------------------------------------- AL-TN
 * S094 P1 S4: the tone.
 *   table: alkToneC[127] == 0; decreasing; for k <= 63 (design fc up to
 *   4 kHz) the one-pole's -3 dB point, fc = acos(1 - (1 - c)^2 / (2c)) x
 *   fs / 2 pi (exact for y = (1 - c) x + c y), within 10 % of the design
 *   800 x 25^(k/127) Hz (computed: at most 2.7 %). Above 4 kHz
 *   c = exp(-2 pi fc / fs) drifts above the design toward Nyquist (+25 % at
 *   k = 100), which is inaudible as a tone setting and not checked;
 *   render (measured S094 P1 S4): tone 0 lowers the pattern's high band
 *   (the RMS of the second difference, a 12 dB/octave weighting) by at least
 *   3 dB at overdrive 0, 63 and 127 (measured 5.4 / 5.4 / 7.5 dB), and by more
 *   at 127 than at 0 (the tone rolls off the overdrive's added highs). The
 *   pattern's high band sits at the ladder's resonance peak (about 1-2 kHz,
 *   where an 800 Hz one-pole takes 5-8 dB); above it the 24 dB/octave ladder
 *   has already removed most of the energy. */
static double tn_hiRms(uint8_t tone, uint8_t ovd)
{
	static int16_t buf[GOLD_PAT_BLOCKS * BLK];
	AlkalineVoice v;
	double s = 0.0;

	gold_voice(&v, ALK_WAVE_SAW, 0u);
	v.tone = tone;
	v.vcaDrive = ovd;
	gold_pattern(&v, buf);
	for (size_t i = 2; i < (size_t)GOLD_PAT_BLOCKS * BLK; i++) {
		const double d = (double)buf[i] - 2.0 * buf[i - 1] + buf[i - 2];

		s += d * d;
	}
	return sqrt(s / ((double)GOLD_PAT_BLOCKS * BLK));
}

static void test_tone(void)
{
	if (alkToneC[127] != 0.0f) fail(G_TN, "open at 127", alkToneC[127], 0);
	for (unsigned k = 0u; k + 1u < ALK_POST_POINTS; k++) {
		if (!(alkToneC[k + 1u] < alkToneC[k])) fail(G_TN, "decreasing", k, alkToneC[k]);
		if (k <= 63u) {
			const double c = alkToneC[k];
			const double fc = acos(1.0 - (1.0 - c) * (1.0 - c) / (2.0 * c)) * 44108.0 / (2.0 * M_PI);
			const double want = 800.0 * pow(25.0, k / 127.0);

			if (fabs(fc / want - 1.0) > 0.10) fail(G_TN, "-3 dB point", k, fc);
		}
	}
	{
		double cut[3];

		for (unsigned o = 0u; o < 3u; o++) {
			/* overdrive bytes 0, 63, 127 */
			const uint8_t ovd = (uint8_t)(o * 63u + (o == 2u));
			const double open = tn_hiRms(127u, ovd), dark = tn_hiRms(0u, ovd);

			cut[o] = 20.0 * log10(dark / open);
			printf("AL-TN: overdrive %u, high band at tone 0 %+.1f dB against tone 127\n",
			       ovd, cut[o]);
			if (cut[o] > -3.0) fail(G_TN, "tone 0 rolls off the highs", ovd, cut[o]);
		}
		/* the tone's purpose: it takes more off the overdriven voice */
		if (!(cut[2] < cut[0])) fail(G_TN, "the cut grows with overdrive", cut[0], cut[2]);
	}
}

/* ---------------------------------------------------------------- AL-PW
 * S094 P1 S5: the Alkaline pwm (S094 P4 law and Tri).
 *   - pos 0, 64, 127 under Saw, Sqr and Tri: finite, bounded (the pattern);
 *   - the width slew: positionApplied moves <= OSC_PWM_SLEW_STEPS per block
 *     toward pos and lands; a non-legato note snaps it; the phase runs on
 *     through the note (free-running VCO);
 *   - level: Sqr at pos 0 under pwm within 3 dB RMS of Sqr under wav (the
 *     1.40 scale; printed); S094 P4: Tri's ladder input within 0.5 dB of
 *     Saw's (the row RMS match that sets ALK_TRI_SCALE_V, S9), with the
 *     voice's own ratio printed for the record (the ladder colours the two
 *     spectra differently, so it is not a scale measure);
 *   - P-D6 error: this build's render (ALK_PWM_READ_FORM, default 2 after
 *     the S5 measurement) against the form-0 reference written by the second
 *     bench build (build/alk_pwm_ref.bin, Makefile): printed; it must stay
 *     at least 15 dB below the reference RMS (a gross-error guard: about
 *     -29 dB measured for forms 1 and 2). */
#define PW_PATH "build/alk_pwm_ref.bin"

static void pw_render(int16_t *out)
{
	AlkalineVoice v;

	/* Sqr at pos 32, then Saw at pos 96: one pattern each */
	gold_voice(&v, ALK_WAVE_SQUARE, 0u);
	Alkaline_setOscType(&v, ALK_TYP_PWM);
	v.osc.position = 32u;
	gold_pattern(&v, out);
	gold_voice(&v, ALK_WAVE_SAW, 0u);
	Alkaline_setOscType(&v, ALK_TYP_PWM);
	v.osc.position = 96u;
	gold_pattern(&v, out + (size_t)GOLD_PAT_BLOCKS * BLK);
}

static int mode_pwmRender(const char *path)
{
	int16_t *buf = malloc((size_t)2 * GOLD_PAT_BLOCKS * BLK * sizeof(int16_t));
	FILE *fo = fopen(path, "wb");

	pw_render(buf);
	if (!fo || fwrite(buf, sizeof(int16_t), (size_t)2 * GOLD_PAT_BLOCKS * BLK, fo)
	           != (size_t)2 * GOLD_PAT_BLOCKS * BLK)
		return 1;
	fclose(fo);
	free(buf);
	return 0;
}

/* the pattern's RMS for one wave (Saw/Sqr), typ and pos */
static double pw_rms(uint8_t wave, uint8_t typ, uint8_t pos)
{
	static int16_t buf[GOLD_PAT_BLOCKS * BLK];
	AlkalineVoice v;
	double s = 0.0;

	gold_voice(&v, wave, 0u);
	Alkaline_setOscType(&v, typ);
	v.osc.position = pos;
	gold_pattern(&v, buf);
	for (size_t i = 0; i < (size_t)GOLD_PAT_BLOCKS * BLK; i++)
		s += (double)buf[i] * buf[i];
	return sqrt(s / ((double)GOLD_PAT_BLOCKS * BLK));
}

static void test_pwm(void)
{
	/* bounds at the width extremes, every wave */
	{
		static const uint8_t pos[3] = { 0u, 64u, 127u };
		static const uint8_t wave[3] = { ALK_WAVE_SAW, ALK_WAVE_SQUARE, ALK_WAVE_TRI };

		for (int w = 0; w < 3; w++)
			for (int p = 0; p < 3; p++) {
				const double r = pw_rms(wave[w], ALK_TYP_PWM, pos[p]);

				if (!isfinite(r) || r <= 0.0) fail(G_PW, "bounded and sounding", w, pos[p]);
			}
	}
	/* the slew and the snap */
	{
		AlkalineVoice v;
		int16_t buf[BLK];

		gold_voice(&v, ALK_WAVE_SAW, 0u);
		Alkaline_setOscType(&v, ALK_TYP_PWM);
		v.osc.position = 10u;
		Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);
		if (v.osc.positionApplied != 10u) fail(G_PW, "a note snaps the width", v.osc.positionApplied, 10);
		v.osc.position = 100u;
		for (int b = 0; b < 60; b++) {
			const uint8_t before = v.osc.positionApplied;

			block(&v, buf, K_B2R);
			if (abs((int)v.osc.positionApplied - (int)before) > OSC_PWM_SLEW_STEPS)
				fail(G_PW, "slew step", b, v.osc.positionApplied);
		}
		if (v.osc.positionApplied != 100u) fail(G_PW, "the slew lands", v.osc.positionApplied, 100);
		{
			const uint32_t ph = v.osc.phase;

			Alkaline_triggerVoice(&v, 0u, 80u, 63u, ALK_STEP);
			if (v.osc.phase != ph) fail(G_PW, "the phase runs free through a note", 0, 0);
		}
	}
	/* level against Sqr under wav (pos 0 is the S094 P1 width-1/2 point) */
	{
		const double db = 20.0 * log10(pw_rms(ALK_WAVE_SQUARE, ALK_TYP_PWM, 0u) /
		                               pw_rms(ALK_WAVE_SQUARE, ALK_TYP_WAV, 0u));

		printf("AL-PW: Sqr pwm at 0 against Sqr wav %+.2f dB RMS\n", db);
		if (fabs(db) > 3.0) fail(G_PW, "pulse level", db, 3.0);
	}
	/* S094 P4 / S9: the Tri scale, measured where it is defined: the ladder
	 * input (the oscillator row x ALK_TRI_SCALE_V x the drive) at the same
	 * octave row, against the Saw's. */
	{
		AlkalineVoice a, b;
		double sa = 0.0, sb = 0.0;

		gold_voice(&a, ALK_WAVE_TRI, 0u);
		gold_voice(&b, ALK_WAVE_SAW, 0u);
		for (int n = 0; n < 64; n++) {
			int16_t ba[BLK], bb[BLK];

			Alkaline_calcVoiceAsync(&a);
			Alkaline_calcVoiceAsync(&b);
			calcWavetableOscBlock(&a.osc, (const int16_t (*)[1024])triTable, ba, BLK, 1.0f);
			calcWavetableOscBlock(&b.osc, (const int16_t (*)[1024])sawTable, bb, BLK, 1.0f);
			for (int i = 0; i < BLK; i++) {
				const double xa = (double)ba[i] * (ALK_TRI_SCALE_V * a.driveGain);
				const double xb = (double)bb[i] * (ALK_SAW_SCALE_V * b.driveGain);

				sa += xa * xa;
				sb += xb * xb;
			}
		}
		{
			const double db = 10.0 * log10((sa + 1e-30) / (sb + 1e-30));

			printf("AL-PW: Tri ladder input against Saw %+.2f dB\n", db);
			if (fabs(db) > 0.5) fail(G_PW, "tri scale", db, 0.5);
		}
	}
	{
		const double db = 20.0 * log10(pw_rms(ALK_WAVE_TRI, ALK_TYP_WAV, 0u) /
		                               pw_rms(ALK_WAVE_SAW, ALK_TYP_WAV, 0u));

		printf("AL-PW: Tri voice against Saw voice (golden knobs) %+.2f dB RMS\n", db);
	}
	/* P-D6: against the both-interpolated reference */
	{
		const size_t n = (size_t)2 * GOLD_PAT_BLOCKS * BLK;
		int16_t *now = malloc(n * sizeof(int16_t)), *ref = malloc(n * sizeof(int16_t));
		FILE *fi = fopen(PW_PATH, "rb");

		if (fi && fread(ref, sizeof(int16_t), n, fi) == n) {
			double se = 0.0, sr = 0.0;

			pw_render(now);
			for (size_t i = 0; i < n; i++) {
				const double e = (double)now[i] - ref[i];

				se += e * e;
				sr += (double)ref[i] * ref[i];
			}
			{
				const double db = 10.0 * log10((se + 1e-30) / (sr + 1e-30));

				printf("AL-PW: read form %d against form 0: %.1f dB\n", ALK_PWM_READ_FORM, db);
				/*
				 * S094 P1 S5: a gross-error guard, not a quality bound. The
				 * cheaper forms measure about -29 dB sample for sample
				 * against form 0 (a steep band-limited edge read at a whole
				 * table point: a sub-sample time shift, which an RMS of the
				 * sample difference overstates); the quality call is the
				 * device listening (plan §10). A read past the row, a lost
				 * level or a wrong sign comes out near 0 dB.
				 */
				if (db > -15.0) fail(G_PW, "read-form error", db, -15.0);
			}
		} else {
			fail(G_PW, "reference render missing", 0, 0);
		}
		if (fi) fclose(fi);
		free(now); free(ref);
	}
}

/* ---------------------------------------------------------------- AL-PWW
 * S094 P4: the Alkaline pwm Tri (fused mode 4).
 *   - against an unfused reference: osc_pwmRenderPlain() (the drum
 *     instruments' WT loop at gain 1 on the same lifted row, W, ra, rb) into
 *     a buffer, then DiodeLadder_renderB2() at the same scale: the
 *     difference is at least 40 dB below the reference RMS at pos 0, 64 and
 *     127 (the reference interpolates in integers and the fused read in
 *     float, so they are not bit-identical);
 *   - pos 0 against the unwarped wave: Tri under pwm at pos 0 against Tri
 *     under wav (the fused one-row read), within 40 dB, the same way;
 *   - bounded and sounding at pos 0, 64 and 127, and the slew (as AL-PW).
 * A swapped ra / rb, a wrong lift or a wrong half-cycle offset moves the
 * peak and fails the first check at pos 64 and 127. */
static void test_pwm_wt(void)
{
	static const uint8_t pos[3] = { 0u, 64u, 127u };
	int p;

	/* bounded and sounding at the three positions */
	for (p = 0; p < 3; p++) {
		const double r = pw_rms(ALK_WAVE_TRI, ALK_TYP_PWM, pos[p]);

		if (!isfinite(r) || r <= 0.0) fail(G_PWW, "bounded and sounding", p, pos[p]);
	}
	/* the fused warp against the drum WT loop then the unfused ladder */
	for (p = 0; p < 3; p++) {
		AlkalineVoice a, b;
		double se = 0.0, sr = 0.0;
		int n;

		gold_voice(&a, ALK_WAVE_TRI, 0u);
		Alkaline_setOscType(&a, ALK_TYP_PWM);
		a.osc.position = pos[p];
		a.osc.positionApplied = pos[p];
		b = a;
		for (n = 0; n < 600; n++) {
			int16_t ba[BLK], bb[BLK];
			int i;

			if (n % 150 == 0) {
				Alkaline_triggerVoice(&a, 0u, 80u, 63u, ALK_STEP);
				Alkaline_triggerVoice(&b, 0u, 80u, 63u, ALK_STEP);
			}
			Alkaline_calcVoiceAsync(&a);
			Alkaline_calcVoiceAsync(&b);
			{
				const uint32_t r = (uint32_t)a.osc.tableOffset + oscPwmTriLift[pos[p]];

				DiodeLadder_renderB2OscPwmWt(&a.ladder, ba, triTable[r > 10u ? 10u : r],
				                             &a.osc.phase, a.osc.phaseInc,
				                             oscPwmTriW[pos[p]], oscPwmTriRa[pos[p]],
				                             oscPwmTriRb[pos[p]],
				                             ALK_TRI_SCALE_V * a.driveGain,
				                             a.iLadEnd, a.iAbcEnd * a.makeup);
			}
			/* the drum WT role reads the drum wave enum: Tri is 1 there */
			b.osc.waveform = TRI;
			osc_pwmRenderPlain(&b.osc, bb, BLK, 1.0f);
			DiodeLadder_renderB2(&b.ladder, bb, ALK_TRI_SCALE_V * b.driveGain,
			                     b.iLadEnd, b.iAbcEnd * b.makeup);
			for (i = 0; i < BLK; i++) {
				const double e = (double)ba[i] - bb[i];

				se += e * e;
				sr += (double)bb[i] * bb[i];
			}
		}
		{
			const double db = 10.0 * log10((se + 1e-30) / (sr + 1e-30));

			printf("AL-PWW: Tri pwm at %u against the unfused WT loop: %.1f dB\n",
			       pos[p], db);
			if (db > -40.0) fail(G_PWW, "fused vs unfused", pos[p], db);
		}
	}
	/* pos 0 against Tri under wav (the unwarped one-row read) */
	{
		AlkalineVoice a, b;
		double se = 0.0, sr = 0.0;
		int n;

		gold_voice(&a, ALK_WAVE_TRI, 0u);
		Alkaline_setOscType(&a, ALK_TYP_PWM);
		a.osc.position = 0u;
		a.osc.positionApplied = 0u;
		gold_voice(&b, ALK_WAVE_TRI, 0u);
		for (n = 0; n < 300; n++) {
			int16_t ba[BLK], bb[BLK];
			int i;

			if (n % 150 == 0) {
				Alkaline_triggerVoice(&a, 0u, 80u, 63u, ALK_STEP);
				Alkaline_triggerVoice(&b, 0u, 80u, 63u, ALK_STEP);
			}
			Alkaline_calcVoiceAsync(&a);
			Alkaline_calcVoiceAsync(&b);
			DiodeLadder_renderB2OscPwmWt(&a.ladder, ba, triTable[a.osc.tableOffset],
			                             &a.osc.phase, a.osc.phaseInc,
			                             oscPwmTriW[0], oscPwmTriRa[0], oscPwmTriRb[0],
			                             ALK_TRI_SCALE_V * a.driveGain,
			                             a.iLadEnd, a.iAbcEnd * a.makeup);
			Alkaline_calcVoiceSyncBlock(&b, bb, BLK);
			for (i = 0; i < BLK; i++) {
				const double e = (double)ba[i] - bb[i];

				se += e * e;
				sr += (double)bb[i] * bb[i];
			}
		}
		{
			const double db = 10.0 * log10((se + 1e-30) / (sr + 1e-30));

			printf("AL-PWW: Tri pwm at 0 against Tri wav: %.1f dB\n", db);
			if (db > -40.0) fail(G_PWW, "Tri pwm 0 vs wav", 0, db);
		}
	}
}

int main(int argc, char **argv)
{
	unsigned g, total = 0u;

	if (argc > 3 && !strcmp(argv[1], "--traces"))
		return mode_traces(argv[2], argv[3], argc > 4 ? (float)atof(argv[4]) : 1.0f);
	if (argc > 1 && !strcmp(argv[1], "--pattern"))
		return mode_pattern(argc, argv);
	if (argc > 1 && !strcmp(argv[1], "--sweep"))
		return mode_sweep(argc, argv);
	/* S094 P1 S0: write the golden capture (once, before any firmware edit) */
	if (argc > 2 && !strcmp(argv[1], "--golden"))
		return mode_golden(argv[2]);
	/* S094 P1 S5: write the pwm reference render (the form-0 build) */
	if (argc > 2 && !strcmp(argv[1], "--pwm-render"))
		return mode_pwmRender(argv[2]);

	test_bounds();
	for (int k = 0; k < 3; k++) {
		double per, dec, per15, dec15;

		/* the 303's maximum: the stock ring, as before */
		test_ring(k, 1.0f / ALK_RES_KMAX, &per, &dec);
		/* S093 P6: x1.5 feedback at the calibration point: a long ring that
		 * still decays (measured on the 13-point tables: B2R 1.791 ms / 0.823,
		 * B-R 1.791 / 0.822, A-R 1.791 / 0.831 per cycle) */
		test_ring(k, 1.0f, &per15, &dec15);
		printf("AL-R %s: ring period %.3f ms, decay per cycle %.3f; x1.5: %.3f ms, %.3f%s\n",
		       k_name[k], per, dec, per15, dec15,
		       (k == K_AR) ? " (information: A-R is not stable at Res full with the T2-2 64-sample network, S093 P3)" : "");
		if (k == K_AR)
			continue;
		if (fabs(per - 2.0) > 0.06) fail(G_R, "ring period", k, per);
		if (fabs(dec - 0.364) > 0.03) fail(G_R, "ring decay", k, dec);
		if (fabs(per15 - 1.78) > 0.06) fail(G_R, "x1.5 ring period", k, per15);
		if (fabs(dec15 - 0.823) > 0.03) fail(G_R, "x1.5 ring decay", k, dec15);
	}
	test_selfosc();
	test_sequencing();
	test_net();
	{
		const double r2 = pattern_rms(K_B2R, 0.0f, 0);
		const double rb = pattern_rms(K_BR, 0.0f, 0), ra = pattern_rms(K_AR, 0.0f, 0);
		const double db = 20.0 * log10(rb / r2), da = 20.0 * log10(ra / r2);

		printf("AL-AB: B-R re B2R %+.3f dB RMS, A-R re B2R %+.3f dB RMS\n", db, da);
		if (fabs(db) > 0.3) fail(G_AB, "B-R vs B2R at the 303's drive", db, 0.3);
		if (fabs(da) > 0.3) fail(G_AB, "A-R vs B2R at the 303's drive", da, 0.3);
	}
	test_drive();
	test_quiet();
	test_fastmath();
	test_tablefreq();
	test_fused();
	test_fused_tri();
	test_attack();
	test_meg_range();
	test_pos();
	test_wavetable();
	test_typ();
	/* S094 P1: law AL-OD0 against the S0 capture (every step re-runs it) */
	test_golden();
	/* S094 P1 S4: the drive tables, the overdrive and the tone */
	test_a3();
	test_overdrive();
	test_tone();
	/* S094 P1 S5: the Alkaline pwm */
	test_pwm();
	/* S094 P4: the Alkaline pwm Tri (fused mode 4) */
	test_pwm_wt();
	/* every group in g_name (S093 P6: fourteen; was a literal 10) */
	for (g = 0u; g < sizeof g_name / sizeof g_name[0]; g++) {
		printf("%s: %u failure(s)\n", g_name[g], g_fail[g]);
		total += g_fail[g];
	}
	return total ? 1 : 0;
}
