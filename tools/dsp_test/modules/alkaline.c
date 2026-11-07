/*
 * Alkaline host test (S093 P0).
 *
 * What:       compiles the real DiodeLadder.c, AlkalineControl.c,
 *             AlkalineVoice.c and the generated Alkaline tables, over the
 *             whole real Oscillator.c (as the oscillator module does) and the
 *             real sawTable, and checks:
 *               AL-B  bounded output, no NaN, at every knob extreme and on
 *                     minimum-to-maximum knob steps between blocks, both
 *                     waves, both ladder options, notes 0..127;
 *               AL-R  the [SN] p7 calibration (Res full, saw at C2, ladder
 *                     current 7.375 uA, I_abc 50 uA) read on the OTA
 *                     output: ring period 2.0 ms +- 3 % (SPICE 2.034), the
 *                     second ring peak over the first 0.364 +- 0.03 (the
 *                     golden RK4 model, same metric; S093 P0 §5.2);
 *               AL-S  sequencing (S092 P0 §5.3): accent at velocity 101 not
 *                     100; a note after a slide-flagged note is legato (no
 *                     MEG strike, no VEG trigger, glide), the slid-to note's
 *                     own slide flag decides the next note; gate 7/12 of a
 *                     step, held through the next step's gate after a slide;
 *               AL-N  the Res table: setNet at a grid point reproduces the
 *                     row; interpolation is continuous across a point;
 *               AL-AB options A and B within 0.3 dB RMS at the 303's drive.
 *             Modes for tools/alkaline/check_c.py:
 *               --traces IN OUT    ladder fed with SPICE e2e signals
 *               --pattern W C R E D A OUT   the P4 pattern through the voice
 * Why:        S093 P0 §5: the C must reproduce the validated S092 model.
 * Inputs:     generated fragments (Makefile target alkaline).
 * Outputs:    failure counts per group; exit 1 on any failure.
 * Accessors:  make -C tools/dsp_test alkaline.
 * Affiliates: tools/alkaline/check_c.py; S093_P0_ALKALINE_C_FUNCTIONS_CODE.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "osc_host.h"
#include "Oscillator.host.c"

/* the tables the whole Oscillator.c references (unused here, zero) */
int16_t sine_table[TABLESIZE + 1];
int16_t triTable[11][1024];
int16_t recTable[11][1024];
int16_t sq3Table[11][1024];
int16_t sw3Table[11][1024];
unsigned char crashSample[32769];
int16_t GetRngValue(void) { return 0; }
uint8_t modNode_interpClaim(void* osc) { (void)osc; return 0u; }

/* the real sawTable (wavetable.c), writable on the host as osc_host.h says */
#include "alk_sawTable.c"

/* lfo.h: a same-size stand-in (the engine only owns it and calls lfo_init();
 * the LFO itself belongs to the platform integration, S093 P0 §6) */
typedef struct { uint8_t bytes[124]; } Lfo;
void lfo_init(Lfo *lfo) { memset(lfo, 0, sizeof(*lfo)); }

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

static unsigned g_fail[8];
enum { G_B, G_R, G_S, G_N, G_AB };
static const char *g_name[] = { "AL-B", "AL-R", "AL-S", "AL-N", "AL-AB" };

static void fail(int g, const char *what, double a, double b)
{
	if (g_fail[g] < 5u)
		printf("  %s FAIL: %s (%g, %g)\n", g_name[g], what, a, b);
	g_fail[g]++;
}

static void block(AlkalineVoice *v, int16_t *buf, int optionA)
{
	Alkaline_calcVoiceAsync(v);
	if (optionA) {
		const uint8_t sq = (uint8_t)(v->wave == ALK_WAVE_SQUARE);
		calcWavetableOscBlock(&v->osc, sq ? alkSqrTable : (const int16_t (*)[1024])sawTable,
		                      buf, BLK, 1.0f);
		DiodeLadder_renderA(&v->ladder, buf, sq ? ALK_SQR_SCALE_V : ALK_SAW_SCALE_V,
		                    v->iLadEnd, v->iAbcEnd);
	} else {
		Alkaline_calcVoiceSyncBlock(v, buf, BLK);
	}
}

/* ---------------------------------------------------------------- AL-B */
static void test_bounds(void)
{
	static const float ext[2] = { 0.0f, 1.0f };
	int opt, w, a, b, n;

	for (opt = 0; opt < 2; opt++)
		for (w = 0; w < 2; w++)
			for (a = 0; a < 32; a++) {
				AlkalineVoice v;
				int16_t buf[BLK];

				memset(&v, 0, sizeof(v));
				Alkaline_initVoice(&v);
				v.wave = (uint8_t)w;
				for (n = 0; n < 600; n++) {
					/* knob pattern a, flipped every 50 blocks (min <-> max steps) */
					const int flip = (n / 50) & 1;

					v.cutoff = ext[((a >> 0) & 1) ^ flip];
					v.reso = ext[((a >> 1) & 1) ^ flip];
					v.envMod = ext[((a >> 2) & 1) ^ flip];
					v.decay = ext[((a >> 3) & 1)];
					v.accent = ext[((a >> 4) & 1) ^ flip];
					v.slide = (uint8_t)(n & 64 ? 1 : 0);
					if (n % 37 == 0)
						Alkaline_triggerVoice(&v, (uint8_t)((n * 13) & 127),
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
 * oscillator; the ring is read on the OTA output with I_abc constant. */
static void test_ring(int optionA, double *period, double *decay)
{
	static int16_t out[44108];
	DiodeLadder dl;
	OscInfo osc;
	const int nb = (int)(sizeof(out) / sizeof(out[0])) / BLK;
	const float i0 = 7.375e-6f;
	int b, j;

	memset(&osc, 0, sizeof(osc));
	osc.waveform = SAW;
	osc.freq = 65.406f;
	osc.pitchMod = 1.0f;
	osc.modNodeValue = 1.0f;
	osc_setFreq(&osc);
	DiodeLadder_init(&dl);
	DiodeLadder_setNet(&dl, &alkResTable[ALK_RES_POINTS - 2u], &alkResTable[ALK_RES_POINTS - 1u], 1.0f);
	for (b = 0; b < nb; b++) {
		int16_t *buf = &out[b * BLK];

		calcWavetableOscBlock(&osc, (const int16_t (*)[1024])sawTable, buf, BLK, 1.0f);
		if (optionA)
			DiodeLadder_renderA(&dl, buf, ALK_SAW_SCALE_V, i0 * alk_prewarp(i0, 1.0f), 50e-6f);
		else
			DiodeLadder_renderB(&dl, buf, ALK_SAW_SCALE_V, i0 * alk_prewarp(i0, 1.0f), 50e-6f);
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
	Alkaline_triggerVoice(&v, 100u, 63u, ALK_STEP);
	if (v.accentOn) fail(G_S, "velocity 100 (the default) is not an accent", 100, 1);
	Alkaline_triggerVoice(&v, 101u, 63u, ALK_STEP);
	if (!v.accentOn) fail(G_S, "velocity 101 is an accent", 101, 0);
	if (v.gateLeft != 3217) fail(G_S, "gate = 7/12 of a step", v.gateLeft, 3217);
	for (n = 0; n < 5; n++) block(&v, buf, 0);

	/* slide-flagged note, then the next note is legato */
	v.slide = 1u;
	Alkaline_triggerVoice(&v, 80u, 63u, ALK_STEP);
	if (v.gateLeft != 5514 + 3217) fail(G_S, "slide holds the gate a step + 7/12", v.gateLeft, 8731);
	for (n = 0; n < 172; n++) block(&v, buf, 0);
	v.slide = 0u;
	v.meg.strikeLeft = 0.0f;
	v.veg.delayLeft = 0.0f;
	v.veg.attLeft = 0.0f;
	Alkaline_triggerVoice(&v, 80u, 75u, ALK_STEP);
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
	Alkaline_triggerVoice(&v, 80u, 63u, ALK_STEP);
	if (v.gliding) fail(G_S, "slide is taken from the flagged note only", v.gliding, 0);
	if (v.meg.strikeLeft <= 0.0f) fail(G_S, "non-legato: MEG strike", v.meg.strikeLeft, 1);
	/* a slide-flagged note followed by a rest: the gate closes at 1.58 steps */
	v.slide = 1u;
	Alkaline_triggerVoice(&v, 80u, 63u, ALK_STEP);
	for (n = 0; n < 273; n++) block(&v, buf, 0);
	if (v.gateLeft != 0) fail(G_S, "slide into a rest: gate closes", v.gateLeft, 0);
	Alkaline_triggerVoice(&v, 80u, 63u, ALK_STEP);
	if (v.gliding) fail(G_S, "after the gate closed: not legato", v.gliding, 0);
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
		if (a.alf0 != alkResTable[p].alf[0] || a.cfv1 != alkResTable[p].cf[1][1])
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
static double pattern_rms(int optionA)
{
	AlkalineVoice v;
	int16_t buf[BLK];
	double acc = 0.0;
	int n;

	memset(&v, 0, sizeof(v));
	Alkaline_initVoice(&v);
	v.cutoff = 0.35f; v.reso = 0.85f; v.envMod = 0.6f; v.decay = 0.45f; v.accent = 0.8f;
	for (n = 0; n < 1400; n++) {
		if (n % 172 == 0)
			Alkaline_triggerVoice(&v, (n / 172) & 1 ? 127u : 80u, (uint8_t)(63 + (n / 172) % 5), ALK_STEP);
		block(&v, buf, optionA);
		for (int j = 0; j < BLK; j++)
			acc += (double)buf[j] * buf[j];
	}
	return sqrt(acc / (1400.0 * BLK));
}

/* ------------------------------------------------------------ check modes */
static int mode_traces(const char *in, const char *outp)
{
	/* IN: int32 n, float32 k, then n x (s volts, i_lad A, i_abc A).
	 * OUT: n float32 OTA volts (option B) then n float32 (option A). */
	FILE *f = fopen(in, "rb");
	int32_t n;
	float k;

	if (!f || fread(&n, 4, 1, f) != 1 || fread(&k, 4, 1, f) != 1)
		return 2;
	{
		float *d = malloc(sizeof(float) * 3u * (size_t)n);
		float *o = malloc(sizeof(float) * 2u * (size_t)n);
		const float sScale = 1e-4f;
		int opt;

		if (fread(d, sizeof(float), 3u * (size_t)n, f) != 3u * (size_t)n)
			return 2;
		fclose(f);
		for (opt = 0; opt < 2; opt++) {
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
					buf[j] = (int16_t)lrintf(d[3 * (b + j)] / sScale);
				if (opt)
					DiodeLadder_renderA(&dl, buf, sScale, il * alk_prewarp(il, k), ia);
				else
					DiodeLadder_renderB(&dl, buf, sScale, il * alk_prewarp(il, k), ia);
				for (j = 0; j < BLK; j++)
					o[opt * n + b + j] = (float)buf[j] / DL_OUT_SCALE;
			}
		}
		f = fopen(outp, "wb");
		fwrite(o, sizeof(float), 2u * (size_t)n, f);
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
	opt = (argc > 9) ? atoi(argv[9]) : 0;
	memset(&v, 0, sizeof(v));
	Alkaline_initVoice(&v);
	v.wave = (uint8_t)atoi(argv[2]);
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
				Alkaline_triggerVoice(&v, pat[next][1] ? 127u : 80u,
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

int main(int argc, char **argv)
{
	unsigned g, total = 0u;
	double per, dec;

	if (argc > 3 && !strcmp(argv[1], "--traces"))
		return mode_traces(argv[2], argv[3]);
	if (argc > 1 && !strcmp(argv[1], "--pattern"))
		return mode_pattern(argc, argv);

	test_bounds();
	test_ring(0, &per, &dec);
	printf("AL-R B: ring period %.3f ms, decay per cycle %.3f\n", per, dec);
	if (fabs(per - 2.0) > 0.06) fail(G_R, "B ring period", per, 2.0);
	if (fabs(dec - 0.364) > 0.03) fail(G_R, "B ring decay", dec, 0.364);
	test_ring(1, &per, &dec);
	printf("AL-R A: ring period %.3f ms, decay per cycle %.3f\n", per, dec);
	if (fabs(per - 2.0) > 0.06) fail(G_R, "A ring period", per, 2.0);
	if (fabs(dec - 0.364) > 0.03) fail(G_R, "A ring decay", dec, 0.364);
	test_sequencing();
	test_net();
	{
		const double rb = pattern_rms(0), ra = pattern_rms(1);
		const double db = 20.0 * log10(ra / rb);

		printf("AL-AB: option A re B %+.3f dB RMS\n", db);
		if (fabs(db) > 0.3) fail(G_AB, "A vs B at stock drive", db, 0.3);
	}
	for (g = 0u; g < 5u; g++) {
		printf("%s: %u failure(s)\n", g_name[g], g_fail[g]);
		total += g_fail[g];
	}
	return total ? 1 : 0;
}
