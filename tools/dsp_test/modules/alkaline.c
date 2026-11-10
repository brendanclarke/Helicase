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
 *               AL-W  (S093 P8) typ wtb: the voice is bit-identical to the
 *                     table read of the bound frame's 1,024-point row
 *                     (calcWavetableOscBlock()'s arithmetic) then
 *                     DiodeLadder_renderB2(), block for block, phase too; the
 *                     frame steps one per block toward pos and a non-legato
 *                     note jumps to it (legato does not); an install rebinds;
 *                     an empty catalogue plays silence;
 *               AL-TY (S093 P8) Alkaline_setOscType(): 0 -> wav, 1 and above
 *                     -> wtb; wav clamps wav to Sqr, wtb to the last table;
 *                     the phase runs on; the bind is cleared; the same type
 *                     is a no-op.
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

/* the tables the whole Oscillator.c references (unused here, zero) */
int16_t sine_table[TABLESIZE + 1];
int16_t triTable[11][1024];
int16_t recTable[11][1024];
unsigned char crashSample[32769];
int16_t GetRngValue(void) { return 0; }
uint8_t modNode_interpClaim(void* osc) { (void)osc; return 0u; }

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
#include "AlkalineVoice.host.h"
#include "DiodeLadder.host.c"
#include "AlkalineControl.host.c"
#include "AlkalineTables.host.c"
#include "AlkalineSquareTable.host.c"
#include "AlkalineVoice.host.c"

#define BLK OUTPUT_DMA_SIZE
#define ALK_STEP 5514u   /* a 16th at 120 BPM, samples */

static unsigned g_fail[16];
enum { G_B, G_R, G_S, G_N, G_AB, G_D, G_Q, G_F, G_O, G_X, G_A, G_M, G_SO, G_PS, G_W, G_TY };
static const char *g_name[] = { "AL-B", "AL-R", "AL-S", "AL-N", "AL-AB", "AL-D", "AL-Q",
                                "AL-F", "AL-O", "AL-X", "AL-A", "AL-M", "AL-SO", "AL-PS",
                                "AL-W", "AL-TY" };
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

/* pos -> nearest frame, the law alk_wtbFrameOf() implements */
static uint32_t w_frameOf(uint32_t pos, uint32_t frames)
{
	return (frames > 1u) ? ((pos * (frames - 1u) + 63u) / 127u) : 0u;
}

/* the reference block: the voice's async, then calcWavetableOscBlock()'s
 * table read (gain 1) of `row` and the plain B2R at the Saw scale */
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

static void test_wavetable(void)
{
	AlkalineVoice a, b;
	const wavetable_catalogue_entry_t *e;
	uint32_t expect = 0u;
	unsigned diff = 0u, phd = 0u, frd = 0u;
	double rms = 0.0;
	int n, j;

	w_install(0);
	e = sampleMemory_wavetableEntry(0u);
	if (!e) { fail(G_W, "no table installed", 0, 0); return; }
	memset(&a, 0, sizeof(a));
	Alkaline_initVoice(&a);
	a.cutoff = 0.55f; a.reso = 0.6f; a.envMod = 0.6f; a.accent = 0.7f; a.drive = 0.3f;
	Alkaline_setOscType(&a, ALK_TYP_WTB);
	a.osc.waveform = 0u;
	b = a;
	/* W1, W2: 3,000 blocks of 16ths with accents and slides; pos jumps
	 * every 23 blocks (the frame steps toward it), a note jumps to it */
	for (n = 0; n < 3000; n++) {
		int16_t ba[BLK], bb[BLK];

		if (n % 23 == 0)
			a.osc.position = b.osc.position = (uint8_t)((n * 37) & 127);
		if (n % 172 == 0) {
			const uint8_t legato = (uint8_t)(a.slideLatch && a.gateLeft > 0);

			a.slide = b.slide = (uint8_t)((n / 172) % 4 == 2);
			Alkaline_triggerVoice(&a, 0u, (n / 172) % 3 ? 80u : 127u,
			                      (uint8_t)(40 + (n * 7) % 40), ALK_STEP);
			Alkaline_triggerVoice(&b, 0u, (n / 172) % 3 ? 80u : 127u,
			                      (uint8_t)(40 + (n * 7) % 40), ALK_STEP);
			if (!legato)    /* a note starts on pos's frame */
				expect = w_frameOf(a.osc.position & 127u, e->frame_count);
		}
		/* the block's frame: one step toward pos's frame */
		{
			const uint32_t target = w_frameOf(a.osc.position & 127u, e->frame_count);

			expect = (target > expect) ? expect + 1u : ((target < expect) ? expect - 1u : expect);
		}
		Alkaline_calcVoiceAsync(&a);
		Alkaline_calcVoiceSyncBlock(&a, ba, BLK);
		w_ref(&b, sampleMemory_wavetableRows(e) + expect * WAVETABLE_ROW_POINTS, bb);
		for (j = 0; j < BLK; j++) {
			diff += (ba[j] != bb[j]);
			rms += (double)ba[j] * ba[j];
		}
		phd += (a.osc.phase != b.osc.phase);
		frd += (a.osc.positionApplied != expect);
	}
	printf("AL-W wtb: %u sample(s), %u phase(s), %u frame(s) differ; RMS %.0f LSB\n",
	       diff, phd, frd, sqrt(rms / (3000.0 * BLK)));
	if (diff || phd) fail(G_W, "fused wtb != table read + B2R", diff, phd);
	if (frd) fail(G_W, "frame stepping law", frd, 0);
	if (rms == 0.0) fail(G_W, "wtb is silent", 0, 0);

	/* W3: an install rebinds at the next block: the table index 0 now names
	 * the 1,024-point table (generation changed) */
	w_install(1);
	{
		int16_t buf[BLK];
		const wavetable_catalogue_entry_t *e2 = sampleMemory_wavetableEntry(0u);

		Alkaline_calcVoiceAsync(&a);
		Alkaline_calcVoiceSyncBlock(&a, buf, BLK);
		if (!e2 || a.osc.sampleCacheGeneration != sampleMemory_getGeneration() ||
		    a.osc.sampleCacheSize != e2->frame_count ||
		    a.osc.sampleCacheData != sampleMemory_wavetableRows(e2) +
		                             (uint32_t)a.osc.positionApplied * WAVETABLE_ROW_POINTS)
			fail(G_W, "an install does not rebind", 0, 0);
	}

	/* W4: an empty catalogue plays silence through the zero row */
	catalogue_host_reset();
	sampleMemory_refresh();
	{
		AlkalineVoice z;
		int16_t buf[BLK];
		int peak = 0;

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
		printf("AL-W empty catalogue: peak %d LSB\n", peak);
		if (peak != 0) fail(G_W, "no table is not silent", peak, 0);
	}
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
	Alkaline_setOscType(&v, 7u);
	if (v.osc.oscType != OSC_TYPE_WTB || v.osc.sampleCacheLooped != ALK_BIND_WTB)
		fail(G_TY, "the same type is a no-op", v.osc.oscType, 0);
	v.osc.waveform = 9u;
	Alkaline_setOscType(&v, ALK_TYP_WAV);
	if (v.osc.oscType != OSC_TYPE_WAV) fail(G_TY, "0 is wav", v.osc.oscType, 0);
	if (v.osc.waveform != ALK_WAVE_SQUARE) fail(G_TY, "wav clamps to Sqr", v.osc.waveform, 0);
	if (v.osc.phase != 0x12345678u) fail(G_TY, "the phase runs on (wtb -> wav)", 0, 0);
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
	test_attack();
	test_meg_range();
	test_pos();
	test_wavetable();
	test_typ();
	/* every group in g_name (S093 P6: fourteen; was a literal 10) */
	for (g = 0u; g < sizeof g_name / sizeof g_name[0]; g++) {
		printf("%s: %u failure(s)\n", g_name[g], g_fail[g]);
		total += g_fail[g];
	}
	return total ? 1 : 0;
}
