/*
 * modules/tetrakt.c - host laws for the Tetrakt voice (S096 P1).
 *
 * What:       builds the real TetraktVoice.c and TetraktTables.c (extracted,
 *             firmware includes removed) over the real Oscillator.c read and
 *             bind, the real sample catalogue, the real colEgK[] and
 *             squareRootLut[], and checks:
 *               TK-TRIG  for every lon value 0..15: the set layers restart
 *                        (phase 0 after the next async, env 1 then
 *                        decaying), the clear ones continue bit for bit; a
 *                        layer still sounding fades to exactly 0 over one
 *                        block (FADE) and starts on the next (START); a
 *                        fresh voice (lon A) restarts layer A only;
 *               TK-DEC   the decay is monotone non-increasing per block and
 *                        reaches IDLE (env 0) below 1e-4;
 *               TK-TUNE  tuA 63 at note 63: phaseInc 4,096 exactly; every
 *                        multiple of 5 a whole semitone (|inc - 4096 x
 *                        2^(s / 12)| <= 0.5001); tuA 123..126 all 8,192, 0..3
 *                        all 2,048;
 *               TK-SRR   srA 127: the layer's output equals the srr-free
 *                        restatement exactly; srA 64: the held value
 *                        changes at the tktSrrRate[64] law's sample count
 *                        (+-1);
 *               TK-TONE  tnA 63, teA 63: y == x exactly; a coefficient jump
 *                        across u = 0 under the ramp has no step larger than
 *                        the input's own plus 0.04 (S101 P2: the four-block
 *                        ramp in sub-block steps; was 0.02 for the one-block
 *                        per-sample ramp); tnA 0 and 126 reach the
 *                        LP / HP ends (|H| = 1 / 1.2 at 120 Hz and 3 kHz
 *                        within 2 %, |H| = 1 / sqrt(2) at 1.148 x those
 *                        within 3 %: the -3 dB point of a damping-0.6 SVF);
 *               TK-DRV   drA 0: exact dry; a zero input gives an exactly zero
 *                        output at every drA (the clip(b) subtraction); after
 *                        the DC blocker a driven 100 Hz sine's mean over 1 s
 *                        is below 1e-3 of full scale;
 *               TK-W2    wid 0: busR equals y gR exactly; wid 127 at env 1:
 *                        the same (the (1 - env)^2 mix is 0);
 *               TK-S1    stm 127, sdt 0, sfb 0: the left tap returns the send
 *                        delayed by tapL samples (+-1); sdt 127 puts the
 *                        right tap at tapL x (1 - TKT_SDT_SPAN); sfb 127 with a
 *                        held impulse: the line's peak decays (bounded);
 *               TK-VEL   with veloKind TONE and amount 1: layer A hit at 127
 *                        and layer C at 0 apply 0 and -127 bytes to their own
 *                        tone, each from its own latched velocity; a retrigger
 *                        of A alone leaves C's offset unchanged.
 *               TK-READ  (S096 P1 §19) the read folded into the post loop
 *                        against the platform's osc_smpPlainBlock() on the
 *                        five installed samples (the fifth quiet, so its
 *                        catalogue gain is above 1): for phase steps from 0.37
 *                        to 2.9 frames (fractions included) and for 800
 *                        blocks (past every sample's end), each output
 *                        within one int16 step (the platform read rounds to
 *                        int16; the fused one does not) and the phase equal
 *                        after every block; a finished sample reads exactly
 *                        0 (a block that starts at the end); an unbound layer (OSC_BIND_NONE) is silent and
 *                        keeps its phase.
 *               TK-RING  (S096 P1 §17) tkt_spaceOut()'s segmented ring with
 *                        its front guard equals the first form (per-frame wrapped
 *                        indices over the whole 2,208-sample unit, kept here
 *                        as ref_spaceOut()) bit for bit: both outputs, fbLp
 *                        and the DC / allpass states, over 700 blocks (ten
 *                        laps of the ring) for each tap pair, the shortest
 *                        (2, 2) and the longest (TKT_TAP_MAX) included,
 *                        with feedback, W2 and random buses; tapL and tapR
 *                        from tkt_spaceControl() stay <= TKT_TAP_MAX.
 * Why:        S096_P1_TETRAKT_SAMPLE_PLAYER.md §13 step 1; the schedule's
 *             S10 gate.
 * Inputs:     generated fragments (Makefile target tetrakt).
 * Outputs:    failure counts per group; exit 1 on any failure.
 * Accessors:  make -C tools/dsp_test tetrakt.
 * Affiliates: TetraktVoice.c, TetraktTables.c, modules/colloid.c (the
 *             stand-in pattern).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* S104 P1: the host OscInfo layout differs; skip the firmware size assert. */
#define TKT_HOST 1

#include "osc_host.h"
#include "Oscillator.host.h"
#include "Oscillator.host.c"
#include "oscPwmTables.host.c"

/* host stand-ins for what the voice references (modules/colloid.c pattern) */
typedef struct { uint8_t bytes[124]; } Lfo;
typedef struct { float value; float amount; } ModulationNode;
ModulationNode velocityModulators[6];
static unsigned host_lfoRetriggers;
void lfo_init(Lfo *lfo) { memset(lfo, 0, sizeof(*lfo)); }
void lfo_retrigger(uint8_t voiceNr) { (void)voiceNr; host_lfoRetriggers++; }
void modNode_updateValue(ModulationNode *vm, float val) { vm->value = val; }
void instrumentManager_applyVelocityModulationTarget(uint8_t source_slot, float v)
{ (void)source_slot; (void)v; }

/* the real stock tables Oscillator.c references */
int16_t sine_table[TABLESIZE + 1];
int16_t sawTable[11][1024];
int16_t triTable[11][1024];
int16_t recTable[11][1024];

static uint32_t host_rng = 0x0960001u;
static uint8_t g_claim;
int16_t GetRngValue(void) { return (int16_t)(golden_rand(&host_rng) >> 16); }
uint8_t modNode_interpClaim(void *osc) { (void)osc; return g_claim; }

#include "AlkalineControl.host.h"
#include "ColloidTables.host.h"
#include "ColloidTables.host.c"
#include "tkt_squareRootLut.c"
#include "TetraktTables.host.h"
#include "TetraktTables.host.c"
#include "TetraktVoice.host.h"
#include "TetraktVoice.host.c"

#define BLK OUTPUT_DMA_SIZE
enum { G_TRIG, G_DEC, G_TUNE, G_SRR, G_TONE, G_DRV, G_W2, G_S1, G_VEL, G_RING, G_READ, G_A2Q, G_T7, G_LOOP, G_N };
static const char *g_name[G_N] = { "TK-TRIG", "TK-DEC", "TK-TUNE", "TK-SRR", "TK-TONE",
                                   "TK-DRV", "TK-W2", "TK-S1", "TK-VEL", "TK-RING",
                                   "TK-READ", "TK-A2Q", "TK-T7", "TK-LOOP" };
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

static TetraktVoice V;
static TetraktVoice REF;
static int16_t Lout[BLK];
static int16_t Rout[BLK];
static int16_t line[TKT_LINE_SAMPLES];
static int16_t lineRef[TKT_LINE_SAMPLES];

/*
 * Four samples, 8,192 frames each (modules/oscillator.c install_sample()
 * pattern, one sampleInstall_begin() session). Every peak is 32,767, so the
 * installer's gain code is 0 and the read is exact.
 */
static int16_t smp0[8192];
static int16_t smp1[8192];
static int16_t smp2[8192];
static int16_t smp3[8192];
static int16_t smp4[8192];   /* §19: quiet (peak 3,000), so its gain code is above 0 */
static int16_t smpLoop[1000];   /* S104 P1: a short looped sample [200, 900) */

static void install_samples(void)
{
	static const sample_install_fmt_t fmt = { 1u, 1u, 44100u, 2u, 16u, 16u };
	uint32_t i;

	for (i = 0u; i < 8192u; i++) {
		smp0[i] = (int16_t)(32767 - (int32_t)(i * 4u));
		smp1[i] = (int16_t)lrint(32000.0 * sin(2.0 * M_PI * 100.0 * (double)i / 44100.0));
		smp2[i] = 0;
		smp3[i] = (int16_t)lrint(32000.0 * sin(2.0 * M_PI * 300.0 * (double)i / 44100.0));
		smp4[i] = (int16_t)lrint(3000.0 * sin(2.0 * M_PI * 210.0 * (double)i / 44100.0));
	}
	for (i = 0u; i < 1000u; i++)
		smpLoop[i] = (int16_t)lrint(28000.0 * sin(2.0 * M_PI * 37.0 * (double)i / 44100.0));
	smp0[0] = 32767;
	smp1[0] = 32767;    /* the peak, so the gain code stays 0 */
	smp2[0] = 32767;    /* the impulse */
	smp3[0] = 32767;

	catalogue_host_reset();
	sampleInstall_begin();
	(void)sampleInstall_beginSample("r0000000", 8192u, &fmt);
	(void)sampleInstall_writeFrames((const uint8_t *)smp0, 8192u);
	(void)sampleInstall_finishSample(0u, 8192u, 0u);
	(void)sampleInstall_beginSample("s1000000", 8192u, &fmt);
	(void)sampleInstall_writeFrames((const uint8_t *)smp1, 8192u);
	(void)sampleInstall_finishSample(0u, 8192u, 0u);
	(void)sampleInstall_beginSample("i2000000", 8192u, &fmt);
	(void)sampleInstall_writeFrames((const uint8_t *)smp2, 8192u);
	(void)sampleInstall_finishSample(0u, 8192u, 0u);
	(void)sampleInstall_beginSample("s3000000", 8192u, &fmt);
	(void)sampleInstall_writeFrames((const uint8_t *)smp3, 8192u);
	(void)sampleInstall_finishSample(0u, 8192u, 0u);
	(void)sampleInstall_beginSample("q4000000", 8192u, &fmt);
	(void)sampleInstall_writeFrames((const uint8_t *)smp4, 8192u);
	(void)sampleInstall_finishSample(0u, 8192u, 0u);
	/* S104 P1: the looped test sample (index 5) */
	(void)sampleInstall_beginSample("l5000000", 1000u, &fmt);
	(void)sampleInstall_writeFrames((const uint8_t *)smpLoop, 1000u);
	(void)sampleInstall_finishSample(200u, 900u, 1u);
	(void)sampleInstall_commit();
	sampleMemory_refresh();
}

static void voice_init(void)
{
	memset(&V, 0, sizeof(V));
	memset(line, 0, sizeof(line));
	Tetrakt_initVoice(&V, 0u, line);
}
static void block(void) { Tetrakt_calcVoiceAsync(&V); Tetrakt_calcVoiceSyncBlock(&V, Lout, BLK); }
static void block_ref(void) { Tetrakt_calcVoiceAsync(&REF); Tetrakt_calcVoiceSyncBlock(&REF, Rout, BLK); }

/* A layer prepared for the post-chain laws: identity tone, no drive, no SRR. */
static void layer_identity(TktLayer *L)
{
	L->env = L->envLast = 1.0f;
	L->ic1 = L->ic2 = 0.0f;
	L->a1 = L->a2 = L->a3 = 0.0f;
	L->da1 = L->da2 = L->da3 = 0.0f;
	L->mX = 1.0f; L->mB = 0.0f; L->mL = 0.0f;
	L->dmX = L->dmB = L->dmL = 0.0f;
	L->dGain = 1.0f; L->dBias = 0.0f; L->dClipB = 0.0f; L->dWet = 0.0f;
	L->gL = 1.0f; L->gR = 1.0f; L->gS = 0.0f;
	L->srrRate = 1.0f; L->srrCnt = 0.0f; L->srrHold = 0.0f;
	L->bindGain = 1.0f;
}

/*
 * Post-chain block on a given input block (the real tkt_layerPostStore()).
 * S096 P1 §19: the post loop reads its bound sample itself, so the block is
 * bound as a 32-frame sample (two zero frames after it, as installed) read
 * from phase 0 at exactly one frame per sample with gain 1: the read is then
 * the input exactly, as the old scratch was.
 */
static int16_t post_src[BLK + 2u];

static void post_block(TktLayer *L, const int16_t *in)
{
	memcpy(post_src, in, BLK * sizeof(in[0]));
	post_src[BLK] = post_src[BLK + 1u] = 0;
	L->osc.sampleCacheLooped = OSC_BIND_SMP;
	L->osc.sampleCacheData = post_src;
	L->osc.sampleCacheSize = BLK;
	L->osc.phase = 0u;
	L->osc.phaseInc = 4096u;
	tkt_layerPostStore(L, 1.0f);
}

/* ---------------------------------------------------------- TK-TRIG -- */
static void cmp_layer(const char *what, const TktLayer *a, const TktLayer *b)
{
	expect(G_TRIG, a->osc.phase == b->osc.phase, what, (double)a->osc.phase, (double)b->osc.phase);
	expect(G_TRIG, a->env == b->env, "env", a->env, b->env);
	expect(G_TRIG, a->envLast == b->envLast, "envLast", a->envLast, b->envLast);
	expect(G_TRIG, a->ic1 == b->ic1 && a->ic2 == b->ic2, "tone state", a->ic1, b->ic1);
	expect(G_TRIG, a->srrCnt == b->srrCnt && a->srrHold == b->srrHold, "srr state",
	       a->srrCnt, b->srrCnt);
}

static void test_trig(void)
{
	uint32_t mask, i, n;

	/* A fresh voice (lon A) restarts layer A only. */
	voice_init();
	V.layerOn = 1u;
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 0u, NULL);
	expect(G_TRIG, V.layer[0].stage == TKT_STAGE_START, "fresh A starts", 0, 0);
	for (i = 1u; i < TKT_LAYERS; i++)
		expect(G_TRIG, V.layer[i].stage == TKT_STAGE_IDLE, "fresh B..D idle", i, 0);
	block();
	expect(G_TRIG, V.layer[0].stage == TKT_STAGE_PLAY, "A plays", 0, 0);
	expect(G_TRIG, V.layer[0].envLast == 1.0f, "A starts at 1", V.layer[0].envLast, 1);
	for (i = 1u; i < TKT_LAYERS; i++)
		expect(G_TRIG, V.layer[i].stage == TKT_STAGE_IDLE, "B..D stay idle", i, 0);

	/* Every mask: the set layers restart, the clear ones continue bit for bit. */
	for (mask = 0u; mask < 16u; mask++) {
		voice_init();
		V.layerOn = 0x0Fu;
		Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 0u, NULL);
		block();
		block();

		V.layerOn = (uint8_t)mask;
		REF = V;
		REF.line = lineRef;
		(void)lineRef;
		Tetrakt_triggerVoice(&V, 0u, 100u, 63u, 0u, NULL);
		block();

		for (i = 0u; i < TKT_LAYERS; i++) {
			const uint8_t bit = (uint8_t)(1u << i);

			if (mask & bit) {
				if (V.layer[i].stage != TKT_STAGE_START) {
					fail(G_TRIG, "sounding layer did not fade", mask, i);
					continue;
				}
				expect(G_TRIG, V.layer[i].env == 0.0f, "fade target 0",
				       V.layer[i].env, 0);
			}
		}
		/* The reference runs the same block with no trigger. */
		/* (REF is a copy taken before the trigger; its state is the
		 * continuation the clear layers must match.) */
		block_ref();
		for (i = 0u; i < TKT_LAYERS; i++) {
			if (mask & (1u << i))
				continue;
			cmp_layer("clear layer continues", &V.layer[i], &REF.layer[i]);
		}
		/* The restart is complete on the next async: phase 0, env at 1. */
		Tetrakt_calcVoiceAsync(&V);
		for (i = 0u; i < TKT_LAYERS; i++) {
			if (!(mask & (1u << i)))
				continue;
			expect(G_TRIG, V.layer[i].stage == TKT_STAGE_PLAY, "start then play", i, 0);
			expect(G_TRIG, V.layer[i].envLast == 1.0f, "new env starts at 1",
			       V.layer[i].envLast, 1);
			expect(G_TRIG, V.layer[i].osc.phase == 0u, "restart phase 0", i,
			       (double)V.layer[i].osc.phase);
		}
	}

	/* A quiet layer starts at once: lon A then AC, C was idle. */
	voice_init();
	V.layerOn = 1u;
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 0u, NULL);
	block();
	V.layerOn = 0x05u;
	Tetrakt_triggerVoice(&V, 0u, 90u, 63u, 0u, NULL);
	expect(G_TRIG, V.layer[2].stage == TKT_STAGE_START, "C starts at once", 0, 0);
	expect(G_TRIG, V.layer[0].stage == TKT_STAGE_FADE, "A fades", 0, 0);

	/* lon 0 restarts nothing. */
	voice_init();
	V.layerOn = 0x0Fu;
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 0u, NULL);
	block();
	V.layerOn = 0u;
	REF = V;
	REF.line = lineRef;
	Tetrakt_triggerVoice(&V, 0u, 1u, 63u, 0u, NULL);
	for (i = 0u; i < TKT_LAYERS; i++)
		expect(G_TRIG, V.layer[i].stage == TKT_STAGE_PLAY, "lon 0 leaves play", i, 0);
	block();
	block_ref();
	for (i = 0u; i < TKT_LAYERS; i++)
		cmp_layer("lon 0 continues", &V.layer[i], &REF.layer[i]);
	(void)n;
}

/* ----------------------------------------------------------- TK-DEC -- */
static void test_dec(void)
{
	float prev;
	uint32_t n;
	int idle = 0;

	voice_init();
	V.layerOn = 1u;
	V.layer[0].decay = 0u;
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 0u, NULL);
	block();
	prev = V.layer[0].env;
	expect(G_DEC, prev > 0.0f && prev < 1.0f, "starts below 1", prev, 1);
	for (n = 0u; n < 64u; n++) {
		block();
		expect(G_DEC, V.layer[0].env <= prev + 1.0e-7f, "monotone", V.layer[0].env, prev);
		prev = V.layer[0].env;
		if (V.layer[0].stage == TKT_STAGE_IDLE) {
			idle = 1;
			break;
		}
	}
	expect(G_DEC, idle, "decay reaches idle", 0, 0);
	expect(G_DEC, V.layer[0].env == 0.0f, "idle env is 0", V.layer[0].env, 0);
}

/* ----------------------------------------------------------- TK-A2Q -- */
/*
 * TK-A2Q (S101 P2 A2q): the layer control every fourth block.
 * What:  (1) after a hit, every layer's decay follows the exact per-block
 *        exponential, env_n+1 = env_n (1 - k), within 2 ulp a block, on
 *        control and carried blocks alike, to idle; (2) a row change (level)
 *        reaches layer i's gains in the first block where
 *        (ctlPhase + i) % 4 is 0, and not before; each of the four layers
 *        on a different block; (3) the tone ramp is continuous: each block
 *        starts where the last ended (a + 32 da).
 * Break (Makefile): a carry that holds the envelope must fail TK-A2Q.
 */
static void test_a2q(void)
{
	uint32_t i, b;

	voice_init();
	V.layerOn = 15u;
	for (i = 0u; i < TKT_LAYERS; i++)
		V.layer[i].decay = (uint8_t)(5u + 12u * i);
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 15u, NULL);
	block();
	for (b = 0u; b < 3000u; b++) {
		float prev[TKT_LAYERS], a1e[TKT_LAYERS];
		for (i = 0u; i < TKT_LAYERS; i++) {
			prev[i] = V.layer[i].env;
			a1e[i] = V.layer[i].a1 + (float)BLK * V.layer[i].da1;
		}
		block();
		for (i = 0u; i < TKT_LAYERS; i++) {
			const TktLayer *L = &V.layer[i];
			const float k = colEgK[L->decay & 127u];
			const float want = prev[i] - k * prev[i];

			if (L->stage == TKT_STAGE_PLAY &&
			    fabsf(L->env - want) > 2.0f * 1.2e-7f * (prev[i] + 1e-30f))
				fail(G_A2Q, "decay off the per-block exponential", i, L->env - want);
			if (L->a1 != a1e[i] && fabsf(L->a1 - a1e[i]) > 1e-6f)
				fail(G_A2Q, "tone ramp not continuous", i, L->a1 - a1e[i]);
		}
	}
	for (i = 0u; i < TKT_LAYERS; i++)
		expect(G_A2Q, V.layer[i].stage == TKT_STAGE_IDLE && V.layer[i].env == 0.0f,
		       "every layer decays to idle", i, V.layer[i].env);

	/* (2) a level change reaches each layer on its own scheduled block */
	voice_init();
	V.layerOn = 15u;
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 15u, NULL);
	block();
	block();
	{
		uint32_t seen[TKT_LAYERS], blocks;
		float g0[TKT_LAYERS];
		const uint8_t phase0 = V.ctlPhase;

		for (i = 0u; i < TKT_LAYERS; i++) {
			g0[i] = V.layer[i].gL;
			V.layer[i].level = 30u;
			seen[i] = 99u;
		}
		for (blocks = 0u; blocks < 4u; blocks++) {
			block();
			for (i = 0u; i < TKT_LAYERS; i++)
				if (seen[i] == 99u && V.layer[i].gL != g0[i])
					seen[i] = blocks;
		}
		for (i = 0u; i < TKT_LAYERS; i++) {
			const uint32_t want = (4u - ((phase0 + i) & 3u)) & 3u;
			expect(G_A2Q, seen[i] == want, "row change on the layer's own block", i, seen[i]);
			for (b = i + 1u; b < TKT_LAYERS; b++)
				expect(G_A2Q, seen[i] != seen[b], "layers on different blocks", i, b);
		}
	}
}

/* ---------------------------------------------------------- TK-TUNE -- */
static void tune_inc(uint8_t tune, uint32_t note, uint32_t *out)
{
	voice_init();
	V.layerOn = 1u;
	V.layer[0].tune = tune;
	Tetrakt_triggerVoice(&V, 0u, 127u, (uint8_t)note, 0u, NULL);
	Tetrakt_calcVoiceAsync(&V);
	*out = V.layer[0].osc.phaseInc;
}

static void test_tune(void)
{
	uint32_t inc, i;
	static const int32_t ds[9] = { -60, -35, -25, -5, 0, 5, 35, 55, 60 };

	tune_inc(63u, 63u, &inc);
	expect(G_TUNE, inc == 4096u, "unity at 63/63", inc, 4096);

	for (i = 0u; i < 9u; i++) {
		double want = 4096.0 * pow(2.0, (double)ds[i] / 5.0 / 12.0);
		tune_inc((uint8_t)(63 + ds[i]), 63u, &inc);
		expect(G_TUNE, fabs((double)inc - want) <= 0.5001, "semitone grid",
		       (double)inc, want);
	}

	for (i = 123u; i <= 126u; i++) {
		tune_inc((uint8_t)i, 63u, &inc);
		expect(G_TUNE, inc == 8192u, "top holds +12", i, inc);
	}
	for (i = 0u; i <= 3u; i++) {
		tune_inc((uint8_t)i, 63u, &inc);
		expect(G_TUNE, inc == 2048u, "bottom holds -12", i, inc);
	}
	/* The trigger note transposes. */
	tune_inc(63u, 75u, &inc);
	expect(G_TUNE, fabs((double)inc - 8192.0) <= 1.0, "note +12 an octave", inc, 8192);
}

/* ----------------------------------------------------------- TK-SRR -- */
static void test_srr(void)
{
	int16_t in[BLK];
	TktLayer L;
	uint32_t n, i;

	/* The row 127 is exactly the full rate (the break build is 0.9999). */
	voice_init();
	V.layerOn = 1u;
	V.layer[0].srr = 127u;
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 0u, NULL);
	Tetrakt_calcVoiceAsync(&V);
	expect(G_SRR, V.layer[0].srrRate == 1.0f, "127 is exactly 1.0",
	       V.layer[0].srrRate, 1);

	/* The post chain at rate 1 is the input exactly (tone flat, no drive). */
	memset(&L, 0, sizeof(L));
	layer_identity(&L);
	for (n = 0u; n < BLK; n++)
		in[n] = (int16_t)((int32_t)n * 700 - 10000);
	post_block(&L, in);
	for (n = 0u; n < BLK; n++)
		expect(G_SRR, tkt_work.busL[n] == (float)in[n] * (1.0f / 32768.0f),
		       "127 passes the input", tkt_work.busL[n],
		       (double)in[n] / 32768.0);

	/* The 64 entry holds at tktSrrRate[64]'s sample count. */
	memset(&L, 0, sizeof(L));
	layer_identity(&L);
	L.srrRate = tktSrrRate[64];
	post_block(&L, in);
	{
		float cnt = 0.0f, hold = 0.0f;
		uint32_t changes = 0u;

		for (n = 0u; n < BLK; n++) {
			const float x = (float)in[n] * (1.0f / 32768.0f);

			cnt += tktSrrRate[64];
			if (cnt >= 1.0f) {
				cnt -= 1.0f;
				hold = x;
				changes++;
			}
			expect(G_SRR, tkt_work.busL[n] == hold, "held value", tkt_work.busL[n], hold);
		}
		expect(G_SRR, changes >= 1u && changes <= BLK, "hold updates", changes, 0);
	}
	for (i = 0u; i < 1u; i++)
		(void)i;
}

/* ---------------------------------------------------------- TK-TONE -- */
static void tone_law(float t, float *a1, float *a2, float *a3,
                     float *mX, float *mB, float *mL)
{
	const uint32_t ti = (uint32_t)t;
	const uint32_t tn = (ti < (TKT_TONE_POINTS - 1u)) ? ti + 1u : ti;
	const float g = tktToneG[ti] + (t - (float)ti) * (tktToneG[tn] - tktToneG[ti]);
	const float u = (t - 63.0f) * (1.0f / 63.0f);
	const float w = fminf(fabsf(u) * 8.0f, 1.0f);
	const float wL = (u < 0.0f) ? w : 0.0f;
	const float wH = (u > 0.0f) ? w : 0.0f;
	const float aa1 = 1.0f / (1.0f + g * (g + 1.2f));

	*a1 = aa1;
	*a2 = g * aa1;
	*a3 = g * g * aa1;
	*mX = 1.0f - wL;
	*mB = -wH * 1.2f;
	*mL = wL - wH;
}

/* The filter's response to a sine, measured through the real post chain. */
static float tone_gain(float t, double freq)
{
	TktLayer L;
	int16_t in[BLK];
	uint32_t b, n;
	float peak = 0.0f;

	memset(&L, 0, sizeof(L));
	layer_identity(&L);
	tone_law(t, &L.a1, &L.a2, &L.a3, &L.mX, &L.mB, &L.mL);
	L.da1 = L.da2 = L.da3 = L.dmX = L.dmB = L.dmL = 0.0f;
	for (b = 0u; b < 200u; b++) {
		for (n = 0u; n < BLK; n++) {
			const uint32_t k = b * BLK + n;

			in[n] = (int16_t)lrint(16384.0 * sin(2.0 * M_PI * freq * (double)k / 44108.07291666667));
		}
		post_block(&L, in);
		if (b >= 180u) {
			for (n = 0u; n < BLK; n++) {
				const float v = fabsf(tkt_work.busL[n]);

				if (v > peak)
					peak = v;
			}
		}
	}
	return peak / (16384.0f / 32768.0f);   /* relative to the input's amplitude */
}

static void test_tone(void)
{
	TktLayer L;
	int16_t in[BLK];
	uint32_t n;
	float a1, a2, a3, mX, mB, mL, maxIn = 0.0f, maxOut = 0.0f;

	/* u = 0 is the input exactly. */
	memset(&L, 0, sizeof(L));
	layer_identity(&L);
	for (n = 0u; n < BLK; n++)
		in[n] = (int16_t)((int32_t)n * 511 - 8000);
	post_block(&L, in);
	for (n = 0u; n < BLK; n++)
		expect(G_TONE, tkt_work.busL[n] == (float)in[n] * (1.0f / 32768.0f),
		       "flat tone is the input", tkt_work.busL[n], (double)in[n] / 32768.0);

	/* A jump across u = 0 ramps: no output step larger than the input's. */
	for (n = 0u; n < BLK; n++)
		in[n] = (int16_t)lrint(16000.0 * sin(2.0 * M_PI * 1000.0 * (double)n / 44108.07291666667));
	for (n = 1u; n < BLK; n++) {
		const float d = fabsf((float)(in[n] - in[n - 1u]));

		if (d > maxIn)
			maxIn = d;
	}
	memset(&L, 0, sizeof(L));
	layer_identity(&L);
	tone_law(0.0f, &a1, &a2, &a3, &mX, &mB, &mL);
	L.a1 = a1; L.a2 = a2; L.a3 = a3; L.mX = mX; L.mB = mB; L.mL = mL;
	post_block(&L, in);
	tone_law(126.0f, &a1, &a2, &a3, &mX, &mB, &mL);
	/*
	 * S101 P2 A2q + B1 (sub-block form): the firmware ramps a coefficient
	 * change over TKT_CTL_BLOCKS blocks (tkt_ramp()), stepping it every
	 * TKT_SUB_BLOCK samples, with the start carried between blocks
	 * (tkt_layerCarry()). The law runs that ramp: four blocks, the same
	 * output-step bound. (Before S101 P2 it ran a one-block per-sample ramp,
	 * which the firmware no longer has.)
	 */
	{
		const float span = (float)TKT_CTL_BLOCKS * 32.0f;
		uint32_t b;
		float last = tkt_work.busL[BLK - 1u];

		L.da1 = (a1 - L.a1) / span; L.da2 = (a2 - L.a2) / span; L.da3 = (a3 - L.a3) / span;
		/* the weights stay (the law's form since S096 P1: the post loop
		 * does not ramp them; dmX..dmL are unused) */
		for (b = 0u; b < TKT_CTL_BLOCKS; b++) {
			post_block(&L, in);
			for (n = 0u; n < BLK; n++) {
				const float d = fabsf(tkt_work.busL[n] - (n ? tkt_work.busL[n - 1u] : last)) *
				                32768.0f;

				if (n && d > maxOut)
					maxOut = d;
			}
			last = tkt_work.busL[BLK - 1u];
			L.a1 += 32.0f * L.da1; L.a2 += 32.0f * L.da2; L.a3 += 32.0f * L.da3;
		}
	}
	/*
	 * S101 P2 B1 (sub-block form): the coefficients step by 1/16 of the
	 * ramp at once every 8 samples (the full LP -> HP range over four
	 * blocks), so the bound is the input's step + 0.04 of full scale
	 * (measured +0.029 on this extreme sweep; the per-sample ramp's bound was
	 * + 0.02). A sub-block step four times too large breaks it (Makefile).
	 */
	expect(G_TONE, maxOut <= maxIn + 0.04f * 32768.0f, "sweep is ramped", maxOut, maxIn);

	/* The ends reach their cutoffs (damping 0.6: |H| = 1/1.2 at the corner). */
	expect(G_TONE, fabsf(tone_gain(0.0f, 120.0) - (1.0f / 1.2f)) < 0.02f,
	       "LP corner", tone_gain(0.0f, 120.0), 1.0 / 1.2);
	expect(G_TONE, fabsf(tone_gain(0.0f, 120.0 * 1.148) - 0.7071f) < 0.03f,
	       "LP -3 dB", tone_gain(0.0f, 120.0 * 1.148), 0.7071);
	expect(G_TONE, fabsf(tone_gain(126.0f, 3000.0) - (1.0f / 1.2f)) < 0.02f,
	       "HP corner", tone_gain(126.0f, 3000.0), 1.0 / 1.2);
	expect(G_TONE, fabsf(tone_gain(126.0f, 3000.0 * 0.8707) - 0.7071f) < 0.03f,
	       "HP -3 dB", tone_gain(126.0f, 3000.0 * 0.8707), 0.7071);
}

/* ----------------------------------------------------------- TK-DRV -- */
static void test_drv(void)
{
	TktLayer L;
	int16_t in[BLK];
	uint32_t n, b, d;
	double mean = 0.0;

	/* drA 0 is the dry signal exactly. */
	voice_init();
	V.layerOn = 1u;
	V.layer[0].sample = 3u;
	V.layer[0].drive = 0u;
	V.layer[0].tone = 63u;
	V.layer[0].toneEnv = 63u;
	V.layer[0].srr = 127u;
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 0u, NULL);
	Tetrakt_calcVoiceAsync(&V);
	expect(G_DRV, fabsf(V.layer[0].dGain - 1.0f) < 1.0e-6f &&
	       V.layer[0].dBias == 0.0f &&
	       V.layer[0].dClipB == 0.0f && V.layer[0].dWet == 0.0f,
	       "drive 0 is the bypass", V.layer[0].dGain, 0);

	/* A zero input is exactly zero at every drive. */
	memset(in, 0, sizeof(in));
	for (d = 0u; d < 128u; d += 7u) {
		memset(&L, 0, sizeof(L));
		layer_identity(&L);
		L.dGain = alk_exp2f(5.0f * (float)d / 127.0f);
		L.dBias = 0.25f * ((float)d / 127.0f) * ((float)d / 127.0f);
		L.dClipB = tkt_clip(L.dBias);
		L.dWet = (d >= 8u) ? 1.0f : (float)d / 8.0f;
		post_block(&L, in);
		for (n = 0u; n < BLK; n++)
			expect(G_DRV, tkt_work.busL[n] == 0.0f, "silence stays silent",
			       tkt_work.busL[n], 0);
	}

	/* After the DC blocker a driven sine's mean is 0. */
	voice_init();
	V.layerOn = 1u;
	V.layer[0].sample = 1u;      /* 100 Hz sine */
	V.layer[0].drive = 127u;
	V.layer[0].level = 40u;
	V.layer[0].tone = 63u;
	V.layer[0].toneEnv = 63u;
	V.layer[0].srr = 127u;
	V.layer[0].decay = 127u;
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 0u, NULL);
	for (b = 0u; b < 1378u; b++) {
		block();
		for (n = 0u; n < BLK; n++)
			mean += Lout[n];
	}
	mean /= 1378.0 * BLK;
	expect(G_DRV, fabs(mean) < 10.0, "driven sine mean is 0", mean, 0);
}

/* ------------------------------------------------------------ TK-W2 -- */
static void test_w2(void)
{
	TktLayer L;
	int16_t in[BLK];
	uint32_t n;

	memset(&L, 0, sizeof(L));
	layer_identity(&L);
	for (n = 0u; n < BLK; n++)
		in[n] = (int16_t)((int32_t)n * 900 - 14000);
	post_block(&L, in);
	for (n = 0u; n < BLK; n++) {
		expect(G_W2, tkt_work.busR[n] == (float)in[n] * (1.0f / 32768.0f),
		       "wid 0 leaves R as panned", tkt_work.busR[n], (double)in[n] / 32768.0);
	}
	/* The summed-bus mix: 0 at wid 0 and at a full envelope. */
	voice_init();
	V.width = 127u;
	V.layerOn = 0x0Fu;
	for (n = 0u; n < TKT_LAYERS; n++)
		V.layer[n].level = 100u;
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 0u, NULL);
	Tetrakt_calcVoiceAsync(&V);
	expect(G_W2, V.busWMix < 0.001f, "wid at env 1 is panned", V.busWMix, 0);
	V.layer[0].env = 0.0f;
	V.layer[1].env = 0.0f;
	V.layer[2].env = 0.0f;
	V.layer[3].env = 0.0f;
	Tetrakt_calcVoiceAsync(&V);
	expect(G_W2, fabsf(V.busWMix - 1.0f) < 0.001f, "wid at env 0 is full",
	       V.busWMix, 1);
}

/* ------------------------------------------------------------ TK-S1 -- */
static void test_s1(void)
{
	uint32_t n;
	float want;

	/* tapL, tapR and fb from the control. */
	voice_init();
	V.spaceTime = 127u;
	V.spaceDetune = 0u;
	V.spaceFeedback = 0u;
	V.spaceTone = 100u;
	V.spaceLevel = 127u;
	tkt_spaceControl(&V);
	expect(G_S1, fabs(V.tapL - (50.0f * 44.1080729f)) < 0.01, "tapL 50 ms",
	       V.tapL, 50.0 * 44.1080729);
	V.spaceDetune = 127u;
	tkt_spaceControl(&V);
	want = V.tapL * (1.0f - TKT_SDT_SPAN);   /* the wobble is fixed (trim 3); S096 P2 F4: 0.5 */
	expect(G_S1, fabs(V.tapR - want) < 0.001, "tapR detune", V.tapR, want);

	/* The left tap returns the send delayed by tapL, +-1 sample. */
	memset(&V, 0, sizeof(V));
	V.line = line;
	V.tapL = 100.0f;
	V.tapR = 100.0f;
	V.fb = 0.0f;
	V.fbK = 0.0f;
	V.slvGain = 1.0f;
	V.spaceLevel = 127u;
	memset(line, 0, sizeof(line));
	memset(tkt_work.busL, 0, sizeof(tkt_work.busL));
	memset(tkt_work.busR, 0, sizeof(tkt_work.busR));
	memset(tkt_work.send, 0, sizeof(tkt_work.send));
	tkt_work.send[0] = 1.0f;
	{
		int32_t at = -1;
		int32_t val = 0;
		uint32_t b;

		for (b = 0u; b < 5u; b++) {
			tkt_spaceOut(&V, Lout);
			for (n = 0u; n < BLK; n++) {
				if (Lout[n] != 0 && at < 0) {
					at = (int32_t)(b * BLK + n);
					val = Lout[n];
				}
			}
			memset(tkt_work.send, 0, sizeof(tkt_work.send));
		}
		expect(G_S1, at >= 99 && at <= 101, "tap at tapL", at, 100);
		expect(G_S1, val >= 32766, "tap value", val, 32767);
	}

	/* The feedback loop is bounded and decays. */
	memset(&V, 0, sizeof(V));
	V.line = line;
	V.tapL = 50.0f;
	V.tapR = 50.0f;
	V.fb = 0.9f;
	V.fbK = 1.0f;
	V.slvGain = 0.0f;
	memset(line, 0, sizeof(line));
	memset(tkt_work.busL, 0, sizeof(tkt_work.busL));
	memset(tkt_work.busR, 0, sizeof(tkt_work.busR));
	memset(tkt_work.send, 0, sizeof(tkt_work.send));
	tkt_work.send[0] = 1.0f;
	tkt_spaceOut(&V, Lout);
	{
		int32_t peak0 = 0, peak1 = 0;

		for (n = 0u; n < TKT_LINE_SAMPLES; n++) {
			const int32_t v = line[n] < 0 ? -line[n] : line[n];

			if (v > peak0)
				peak0 = v;
		}
		memset(tkt_work.send, 0, sizeof(tkt_work.send));
		for (n = 0u; n < 1000u; n++) {
			uint32_t k;

			tkt_spaceOut(&V, Lout);
			for (k = 0u; k < BLK; k++) {
				const int32_t v = Lout[k] < 0 ? -Lout[k] : Lout[k];

				if (v > peak1)
					peak1 = v;
			}
		}
		expect(G_S1, peak0 > 0, "feedback took the send", peak0, 0);
		expect(G_S1, peak1 < peak0, "feedback is bounded", peak1, peak0);
	}
}

/* ----------------------------------------------------------- TK-VEL -- */
static void test_vel(void)
{
	voice_init();
	V.layerOn = 0x05u;            /* A and C */
	V.veloKind = TKT_KIND_TONE;
	velocityModulators[0].amount = 1.0f;
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 0u, NULL);
	Tetrakt_calcVoiceAsync(&V);
	expect(G_VEL, V.layer[0].velo == 1.0f && V.layer[2].velo == 1.0f,
	       "hard hit latches 1", V.layer[0].velo, V.layer[2].velo);
	expect(G_VEL, V.layer[0].mX > 0.99f && V.layer[2].mX > 0.99f,
	       "no offset at velocity 1", V.layer[0].mX, V.layer[2].mX);

	/* A soft hit restarts A only: its tone falls to 0, C keeps its own. */
	V.layerOn = 0x01u;
	Tetrakt_triggerVoice(&V, 0u, 0u, 63u, 0u, NULL);
	Tetrakt_calcVoiceAsync(&V);
	Tetrakt_calcVoiceAsync(&V);   /* the fade block, then the START latch */
	expect(G_VEL, V.layer[0].velo == 0.0f, "soft hit latches 0", V.layer[0].velo, 0);
	expect(G_VEL, V.layer[2].velo == 1.0f, "C keeps its velocity", V.layer[2].velo, 1);
	/* The ramps hold the block's start; the block's end is start + 31 steps. */
	expect(G_VEL, fabsf(V.layer[0].mX + 31.0f * V.layer[0].dmX) < 0.05f,
	       "A tone offset is 127", V.layer[0].mX + 31.0f * V.layer[0].dmX, 0);
	expect(G_VEL, V.layer[2].mX + 31.0f * V.layer[2].dmX > 0.99f,
	       "C tone offset is 0", V.layer[2].mX + 31.0f * V.layer[2].dmX, 1);
}

/* ---------------------------------------------------------- TK-READ -- */
static void test_read(void)
{
	static const uint32_t incs[] = { 1515u, 4096u, 4097u, 5163u, 8192u, 11873u };
	static int16_t ref[BLK];
	uint32_t k, s, b, n;

	for (s = 0u; s < 5u; s++) {
		for (k = 0u; k < sizeof(incs) / sizeof(incs[0]); k++) {
			TktLayer L;
			OscInfo R;
			uint32_t worst = 0u, phase = 0u, tailZero = 1u;

			memset(&L, 0, sizeof(L));
			layer_identity(&L);
			L.osc.oscType = OSC_TYPE_SMP;
			L.osc.waveform = (uint8_t)s;
			L.osc.sampleCacheLooped = OSC_BIND_NONE;
			tkt_bindLayer(&L, 1u);
			L.osc.phaseInc = incs[k];
			R = L.osc;
			expect(G_READ, L.osc.sampleCacheLooped == OSC_BIND_SMP, "sample bound", s, 0);
			expect(G_READ, (s == 4u) ? (L.bindGain > 1.0f) : (L.bindGain == 1.0f),
			       "bindGain from the catalogue", s, L.bindGain);
			for (b = 0u; b < 800u; b++) {
				const uint32_t done = (R.phase == (R.sampleCacheSize << OSC_SAMPLE_PHASE_SHIFT));

				tkt_layerPostStore(&L, 1.0f);
				osc_smpPlainBlock(&R, ref, BLK, 1.0f);
				for (n = 0u; n < BLK; n++) {
					const float want = (float)ref[n] * (1.0f / 32768.0f);
					const float d = fabsf(tkt_work.busL[n] - want) * 32768.0f;

					if (d > 1.0001f && worst < 5u) {
						fail(G_READ, "within one int16 step", tkt_work.busL[n], want);
						worst++;
					}
					if (done && tkt_work.busL[n] != 0.0f)
						tailZero = 0u;
				}
				phase += (L.osc.phase != R.phase);
			}
			expect(G_READ, phase == 0u, "phase equals the platform read", s, phase);
			expect(G_READ, tailZero, "a finished sample reads 0", s, incs[k]);
			expect(G_READ, L.osc.phase == (L.osc.sampleCacheSize << OSC_SAMPLE_PHASE_SHIFT),
			       "the read stops at the end", L.osc.phase, L.osc.sampleCacheSize);
		}
	}

	/* An unbound layer is silent and keeps its phase. */
	{
		TktLayer L;

		memset(&L, 0, sizeof(L));
		layer_identity(&L);
		L.osc.sampleCacheLooped = OSC_BIND_NONE;
		L.osc.phase = 12345u;
		L.osc.phaseInc = 4096u;
		tkt_layerPostStore(&L, 1.0f);
		for (n = 0u; n < BLK; n++)
			expect(G_READ, tkt_work.busL[n] == 0.0f, "unbound is silent", tkt_work.busL[n], 0);
		expect(G_READ, L.osc.phase == 12345u, "unbound keeps its phase", L.osc.phase, 12345);
	}
}

/* ---------------------------------------------------------- TK-RING -- */
/*
 * The first form of tkt_spaceOut() (S096 P1 S3, before §17), kept as the
 * reference: every index wrapped per frame over the whole unit.
 */
static void ref_spaceOut(TetraktVoice *voice, int16_t *ln, int16_t *outL, int16_t *outR)
{
	const float *const bL = tkt_work.busL;
	const float *const bR = tkt_work.busR;
	const float *const bS = tkt_work.send;
	const uint32_t iL = (uint32_t)voice->tapL;
	const uint32_t iR = (uint32_t)voice->tapR;
	const float fL = voice->tapL - (float)iL;
	const float fR = voice->tapR - (float)iR;
	const float fb = voice->fb, fbK = voice->fbK, slv = voice->slvGain;
	const float wm = voice->busWMix;
	float lp = voice->fbLp;
	float az = voice->busApZ;
	float xl1 = voice->dcXL, yl1 = voice->dcYL, xr1 = voice->dcXR, yr1 = voice->dcYR;
	uint32_t wi = voice->wi;
	uint32_t n;

	for (n = 0u; n < BLK; n++) {
		float tl, tr, l, r, ap, yl, yr;
		const uint32_t al = (wi >= iL) ? wi - iL : wi + TKT_LINE_SAMPLES - iL;
		const uint32_t bl = al ? al - 1u : TKT_LINE_SAMPLES - 1u;
		const uint32_t ar = (wi >= iR) ? wi - iR : wi + TKT_LINE_SAMPLES - iR;
		const uint32_t br = ar ? ar - 1u : TKT_LINE_SAMPLES - 1u;

		tl = ((float)ln[al] + fL * (float)(ln[bl] - ln[al])) * TKT_IN_SCALE;
		tr = ((float)ln[ar] + fR * (float)(ln[br] - ln[ar])) * TKT_IN_SCALE;
		lp += fbK * (0.5f * (tl + tr) - lp);
		ln[wi] = tkt_sat16((bS[n] + fb * lp) * TKT_OUT_SCALE);
		wi = (wi + 1u < TKT_LINE_SAMPLES) ? wi + 1u : 0u;
		l = bL[n] + tl * slv;
		r = bR[n] + tr * slv;
		ap = TKT_BUS_AP * r + az;
		az = r - TKT_BUS_AP * ap;
		r += wm * (ap - r);
		yl = l - xl1 + TKT_DC_R * yl1;
		yr = r - xr1 + TKT_DC_R * yr1;
		xl1 = l; yl1 = yl;
		xr1 = r; yr1 = yr;
		outL[n] = tkt_sat16(yl * TKT_OUT_SCALE);
		outR[n] = tkt_sat16(yr * TKT_OUT_SCALE);
	}
	voice->wi = (uint16_t)wi;
	voice->fbLp = lp;
	voice->busApZ = az;
	voice->dcXL = xl1; voice->dcYL = yl1;
	voice->dcXR = xr1; voice->dcYR = yr1;
}

static float ring_rand(void)
{
	return (float)(int32_t)golden_rand(&host_rng) * (1.0f / 2147483648.0f);
}

static void test_ring(void)
{
	static const float taps[][2] = {
		{ 2.0f, 2.0f }, { 2.3f, 2.7f }, { 31.5f, 2.0f }, { 32.0f, 33.0f },
		{ 100.25f, 85.2f }, { 1103.6f, 937.9f }, { 2205.4f, 1874.6f },
		{ TKT_TAP_MAX, TKT_TAP_MAX }, { TKT_TAP_MAX, 2.0f },
	};
	static int16_t refR[BLK];
	uint32_t t, b, n;

	for (t = 0u; t < sizeof(taps) / sizeof(taps[0]); t++) {
		uint32_t diff = 0u, state = 0u;

		memset(&V, 0, sizeof(V));
		memset(&REF, 0, sizeof(REF));
		memset(line, 0, sizeof(line));
		memset(lineRef, 0, sizeof(lineRef));
		V.line = line;
		V.tapL = REF.tapL = taps[t][0];
		V.tapR = REF.tapR = taps[t][1];
		V.fb = REF.fb = 0.9f;
		V.fbK = REF.fbK = 0.37f;
		V.slvGain = REF.slvGain = 0.8f;
		V.busWMix = REF.busWMix = 0.5f;
		for (b = 0u; b < 700u; b++) {
			for (n = 0u; n < BLK; n++) {
				tkt_work.busL[n] = 0.5f * ring_rand();
				tkt_work.busR[n] = 0.5f * ring_rand();
				tkt_work.send[n] = (b % 70u < 20u) ? 0.6f * ring_rand() : 0.0f;
			}
			tkt_spaceOut(&V, Lout);
			ref_spaceOut(&REF, lineRef, Rout, refR);
			for (n = 0u; n < BLK; n++)
				diff += (Lout[n] != Rout[n]) + (V.right[n] != refR[n]);
			state += (V.fbLp != REF.fbLp) + (V.busApZ != REF.busApZ) +
			         (V.dcYL != REF.dcYL) + (V.dcYR != REF.dcYR);
		}
		expect(G_RING, diff == 0u, "output equals the first form", t, diff);
		expect(G_RING, state == 0u, "states equal the first form", t, state);
		expect(G_RING, V.wi < TKT_RING, "wi stays in the ring", t, V.wi);
	}

	/* The control keeps both taps inside the ring's bound. */
	voice_init();
	V.spaceTime = 127u;
	V.spaceDetune = 0u;
	tkt_spaceControl(&V);
	expect(G_RING, V.tapL <= TKT_TAP_MAX && V.tapR <= TKT_TAP_MAX,
	       "taps <= TKT_TAP_MAX", V.tapL, TKT_TAP_MAX);
	expect(G_RING, (uint32_t)V.tapL + 1u < TKT_RING, "a read never reaches the write",
	       V.tapL, TKT_RING);
}

/* S096 P3 T1: a trigger's layer mask overrides lon for that trigger only. */
static void test_layers_override(void)
{
	uint32_t i;

	voice_init();
	V.layerOn = 0x05u;                 /* A and C in the patch */
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 0x02u, NULL);   /* the note's layer B */
	for (i = 0u; i < TKT_LAYERS; i++)
		expect(G_TRIG, V.layer[i].stage == (i == 1u ? TKT_STAGE_START : TKT_STAGE_IDLE),
		       "mask override layer state", i, V.layer[i].stage);
	expect(G_TRIG, V.layerOn == 0x05u, "layerOn is not written", V.layerOn, 0x05u);

	/* mask 0 falls back to lon */
	voice_init();
	V.layerOn = 0x05u;
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 0u, NULL);
	for (i = 0u; i < TKT_LAYERS; i++) {
		const int on = (i == 0u || i == 2u);

		expect(G_TRIG, V.layer[i].stage == (on ? TKT_STAGE_START : TKT_STAGE_IDLE),
		       "lon fallback layer state", i, V.layer[i].stage);
	}
}

/*
 * TK-LOOP (S104 P1): the looped-sample seam forms.
 *
 * What: with the looped test sample (index 5, loop [200, 900)) bound, form A
 * (an interpolation slot) equals a double-precision read of the
 * osc_slpPlainBlock() rule across at least five wraps, at 1.0, 0.73 and 2.6
 * frames a sample; form B wraps at frame 898 (phase < 899 << 12) and never
 * reads frame 899; a no-seam block in the plain form is bit-identical to
 * the one-shot loop at the same phase; a row value samples .. 2 x samples -
 * 1 binds SLP with index value - samples, and a value below samples binds
 * SMP.
 * S104 P1 assessment: the trigger splits the row value (pendSample is the
 * catalogue index, TKT_PF_PEND_SLP set); the seam threshold seamPh gives the
 * same decision as phase + 32 x phaseInc >= loopEnd - 4,096 for every phase
 * at three tunes; in a block where another layer restarts, a seam layer
 * takes form B even with a slot free (no claim), and form A the next block.
 * Break (Makefile): dropping the seam select, never predicting a seam, or
 * claiming in a restart block must fail this law.
 */
static double ref_loop_sample(const int16_t *d, uint32_t ph, uint32_t ls,
                              uint32_t le)
{
	const uint32_t idx = ph >> 12;
	const uint32_t nx = (idx + 1u == le) ? ls : idx + 1u;
	const int32_t a = d[idx];

	return ((double)((int32_t)a << 12) +
	        (double)(ph & 0xFFFu) * (double)((int32_t)d[nx] - a)) /
	       4096.0 / 32768.0;
}

static void test_loop(void)
{
	static const double rates[3] = { 1.0, 0.73, 2.6 };
	const uint32_t ls = 200u, le = 900u;
	uint32_t r, b, n;

	for (r = 0u; r < 3u; r++) {
		TktLayer *L;
		double ph = (double)(ls << 12);
		double inc;
		unsigned wraps = 0u;

		voice_init();
		L = &V.layer[0];
		L->osc.oscType = OSC_TYPE_SLP;
		L->osc.waveform = 5u;
		L->osc.sampleCacheLooped = OSC_BIND_NONE;
		tkt_bindLayer(L, 1u);
		expect(G_LOOP, L->osc.sampleCacheLooped == OSC_BIND_SLP,
		       "a looped row binds SLP", r, L->osc.sampleCacheLooped);
		expect(G_LOOP, L->loopStartPh == (ls << 12) && L->loopEndPh == (le << 12),
		       "the loop points are cached", r, L->loopEndPh);
		layer_identity(L);
		L->bindGain = 1.0f;
		L->osc.phaseInc = (uint32_t)lrint(rates[r] * 4096.0);
		inc = (double)L->osc.phaseInc;
		L->osc.phase = ls << 12;
		L->pflags = (uint8_t)(TKT_FORM_A << TKT_PF_FORM_SHIFT);
		for (b = 0u; b < 400u; b++) {
			tkt_layerPostStoreSeamA(L, 1.0f);
			for (n = 0u; n < BLK; n++) {
				const double want = ref_loop_sample(
				    (const int16_t *)L->osc.sampleCacheData,
				    (uint32_t)ph, ls, le);

				if (fabs((double)tkt_work.busL[n] - want) > 3.0 / 32768.0)
					fail(G_LOOP, "form A exact across a seam",
					     (double)(b * BLK + n),
					     (double)tkt_work.busL[n] - want);
				ph += inc;
				if (ph >= (double)(le << 12)) {
					ph -= (double)((le - ls) << 12);
					wraps++;
				}
			}
		}
		expect(G_LOOP, wraps >= 5u, "five wraps in the run", r, wraps);
	}

	/* form B: early wrap, never reads the last loop frame */
	{
		TktLayer *L;
		double ph = (double)(ls << 12);
		const double inc = 4096.0;
		uint32_t seenMax = 0u;
		unsigned wraps = 0u;

		voice_init();
		L = &V.layer[0];
		L->osc.oscType = OSC_TYPE_SLP;
		L->osc.waveform = 5u;
		L->osc.sampleCacheLooped = OSC_BIND_NONE;
		tkt_bindLayer(L, 1u);
		layer_identity(L);
		L->bindGain = 1.0f;
		L->osc.phaseInc = 4096u;
		L->osc.phase = ls << 12;
		L->pflags = (uint8_t)(TKT_FORM_B << TKT_PF_FORM_SHIFT);
		for (b = 0u; b < 80u; b++) {
			const uint32_t before = L->osc.phase;

			tkt_layerPostStoreSeamB(L, 1.0f);
			if (L->osc.phase < before)
				wraps++;
			if (L->osc.phase < L->loopStartPh || L->osc.phase >= L->loopEndPh)
				fail(G_LOOP, "form B stays inside the loop", b, L->osc.phase);
			ph += inc;
			if (ph >= (double)((le - 1u) << 12)) {
				ph -= (double)((le - 1u - ls) << 12);
			}
			if ((uint32_t)(ph / 4096.0) > seenMax)
				seenMax = (uint32_t)(ph / 4096.0);
		}
		expect(G_LOOP, wraps >= 1u, "form B wraps", 0, wraps);
		expect(G_LOOP, seenMax <= le - 2u, "form B never reads the last frame",
		       seenMax, le - 2u);
	}

	/* a no-seam block is the one-shot loop, bit for bit */
	{
		TktLayer A, B2;

		voice_init();
		memset(&A, 0, sizeof(A));
		layer_identity(&A);
		A.osc.oscType = OSC_TYPE_SLP;
		A.osc.waveform = 5u;
		A.osc.sampleCacheLooped = OSC_BIND_NONE;
		tkt_bindLayer(&A, 1u);
		layer_identity(&A);
		A.bindGain = 1.0f;
		A.osc.phaseInc = 4096u;
		A.osc.phase = 300u << 12;
		A.pflags = (uint8_t)(TKT_FORM_PLAIN << TKT_PF_FORM_SHIFT);
		B2 = A;
		B2.loopStartPh = 0u;
		B2.loopEndPh = 0u;
		tkt_layerPostStore(&A, 1.0f);
		{
			float ab[BLK];
			memcpy(ab, tkt_work.busL, sizeof(ab));

			tkt_layerPostStore(&B2, 1.0f);
			for (n = 0u; n < BLK; n++)
				expect(G_LOOP, ab[n] == tkt_work.busL[n],
				       "a no-seam block is the one-shot loop", n, 0);
		}
	}

	/* row values: samples .. 2s-1 are looped, below are one-shots */
	{
		const uint8_t n = sampleMemory_getNumSamples();

		voice_init();
		V.layerOn = 1u;
		V.layer[0].sample = (uint8_t)(n + 1u);
		Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 1u, NULL);
		tkt_layerControl(&V, 0u, 0u, 1u);
		expect(G_LOOP, V.layer[0].osc.oscType == OSC_TYPE_SLP &&
		       V.layer[0].osc.waveform == 1u, "a looped row binds SLP",
		       V.layer[0].osc.oscType, V.layer[0].osc.waveform);
		voice_init();
		V.layerOn = 1u;
		V.layer[0].sample = 2u;
		Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 1u, NULL);
		tkt_layerControl(&V, 0u, 0u, 1u);
		expect(G_LOOP, V.layer[0].osc.oscType == OSC_TYPE_SMP &&
		       V.layer[0].osc.waveform == 2u, "a one-shot row binds SMP",
		       V.layer[0].osc.oscType, V.layer[0].osc.waveform);
	}

	/* the async's seam prediction chooses A with a slot and B without */
	{
		TktLayer *L;
		const uint32_t formShift = TKT_PF_FORM_SHIFT;

		voice_init();
		V.layerOn = 1u;
		V.layer[0].sample = (uint8_t)(sampleMemory_getNumSamples() + 5u);
		Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 1u, NULL);
		tkt_layerControl(&V, 0u, 0u, 1u);   /* latch + bind SLP */
		L = &V.layer[0];
		L->stage = TKT_STAGE_PLAY;
		L->env = L->envLast = 1.0f;
		g_claim = 1u;
		L->osc.phase = (le - 1u) << 12;   /* this block reaches the seam */
		Tetrakt_calcVoiceAsync(&V);
		expect(G_LOOP, ((L->pflags & TKT_PF_FORM_MASK) >> formShift) == TKT_FORM_A,
		       "a seam with a slot takes form A", 0, L->pflags);
		g_claim = 0u;
		L->osc.phase = (le - 1u) << 12;
		Tetrakt_calcVoiceAsync(&V);
		expect(G_LOOP, ((L->pflags & TKT_PF_FORM_MASK) >> formShift) == TKT_FORM_B,
		       "a seam without a slot takes form B", 0, L->pflags);
		g_claim = 1u;
	}

	/* S104 P1 assessment: the trigger splits the row value */
	{
		const uint8_t n = sampleMemory_getNumSamples();

		voice_init();
		V.layerOn = 1u;
		V.layer[0].sample = (uint8_t)(n + 3u);
		Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 1u, NULL);
		expect(G_LOOP, V.layer[0].pendSample == 3u &&
		       (V.layer[0].pflags & TKT_PF_PEND_SLP) != 0u,
		       "the trigger maps a looped row", V.layer[0].pendSample,
		       V.layer[0].pflags);
		V.layer[0].sample = 3u;
		Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 1u, NULL);
		expect(G_LOOP, V.layer[0].pendSample == 3u &&
		       (V.layer[0].pflags & TKT_PF_PEND_SLP) == 0u,
		       "the trigger maps a one-shot row", V.layer[0].pendSample,
		       V.layer[0].pflags);
	}

	/* S104 P1 assessment: seamPh equals the multiply-add test */
	{
		static const uint8_t tunes[3] = { 0u, 63u, 127u };
		uint32_t t;

		for (t = 0u; t < 3u; t++) {
			TktLayer *L;
			uint32_t ph;

			voice_init();
			V.layerOn = 1u;
			V.layer[0].sample = (uint8_t)(sampleMemory_getNumSamples() + 5u);
			V.layer[0].tune = tunes[t];
			Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 1u, NULL);
			tkt_layerControl(&V, 0u, 0u, 1u);
			L = &V.layer[0];
			for (ph = 0u; ph < (le << 12); ph += 997u) {
				const uint32_t oldRule =
				    (uint32_t)(ph + (uint32_t)OUTPUT_DMA_SIZE * L->osc.phaseInc >=
				               L->loopEndPh - (1u << 12));
				const uint32_t newRule = (uint32_t)(ph >= L->seamPh);

				if (oldRule != newRule) {
					expect(G_LOOP, 0, "seamPh matches the seam test", ph, t);
					break;
				}
			}
		}
	}

	/* S104 P1 assessment: no slot claim in a restart block */
	{
		TktLayer *L;
		const uint32_t formShift = TKT_PF_FORM_SHIFT;

		voice_init();
		V.layerOn = 1u;
		V.layer[0].sample = (uint8_t)(sampleMemory_getNumSamples() + 5u);
		Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 1u, NULL);
		tkt_layerControl(&V, 0u, 0u, 1u);   /* latch + bind SLP */
		L = &V.layer[0];
		L->stage = TKT_STAGE_PLAY;
		L->env = L->envLast = 1.0f;
		g_claim = 1u;
		V.layer[1].sample = 2u;
		Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 2u, NULL);   /* B restarts */
		L->osc.phase = (le - 1u) << 12;
		Tetrakt_calcVoiceAsync(&V);
		expect(G_LOOP, ((L->pflags & TKT_PF_FORM_MASK) >> formShift) == TKT_FORM_B,
		       "a seam in a restart block takes form B", 0, L->pflags);
		L->osc.phase = (le - 1u) << 12;
		Tetrakt_calcVoiceAsync(&V);
		expect(G_LOOP, ((L->pflags & TKT_PF_FORM_MASK) >> formShift) == TKT_FORM_A,
		       "the next block claims (form A)", 0, L->pflags);
	}
}

/*
 * TK-T7 (S104 P1): the track-7 decays of a Tetrakt on slot 6.
 *
 * What: a T7 trigger (altDecay {10,20,30,40}) with lon 15 gives each layer
 * that alt decay (its first START's envelope factor is colEgK[alt]) while
 * L->decay stays at track 6's 90 and pflags records ALT; a T6 trigger keeps
 * track 6's decay. With shared lon masks, T6 pre-empts T7 on a shared layer
 * in either pop order (T6's decay and velocity), while layers only one
 * track restarts keep that track's decay. A T7 trigger alone after the START
 * latch restarts normally with its decay.
 * Break (Makefile): removing the T6-wins `continue;` must fail this law.
 */
static void test_t7(void)
{
	static const uint8_t alt[4] = { 10u, 20u, 30u, 40u };
	uint32_t i;

	/* every layer, T7 alone */
	voice_init();
	V.layerOn = 15u;
	for (i = 0u; i < TKT_LAYERS; i++)
		V.layer[i].decay = 90u;
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 15u, alt);
	for (i = 0u; i < TKT_LAYERS; i++) {
		TktLayer *L = &V.layer[i];

		tkt_layerControl(&V, i, 0u, 1u);
		expect(G_T7, L->stage == TKT_STAGE_PLAY, "T7 starts the layer", i, L->stage);
		expect(G_T7, fabsf(L->env - (1.0f - colEgK[alt[i]])) < 1e-6f,
		       "T7 decay factor", i, L->env);
		expect(G_T7, L->decay == 90u && L->decayAlt == alt[i],
		       "T7 keeps track 6's row", i, L->decay);
		expect(G_T7, (L->pflags & TKT_PF_ALT) != 0u, "T7 flag", i, L->pflags);
	}

	/* shared mask, T6 then T7: A/B T6, C T7 */
	voice_init();
	V.layerOn = 15u;
	for (i = 0u; i < TKT_LAYERS; i++)
		V.layer[i].decay = 90u;
	Tetrakt_triggerVoice(&V, 0u, 120u, 63u, 0x03u, NULL);
	Tetrakt_triggerVoice(&V, 0u, 90u, 63u, 0x06u, alt);
	for (i = 0u; i < TKT_LAYERS; i++) {
		TktLayer *L = &V.layer[i];
		const uint8_t want = (i < 2u) ? 90u : ((i == 2u) ? alt[2] : 0u);

		if (i >= 3u)
			continue;
		tkt_layerControl(&V, i, 0u, 1u);
		expect(G_T7, L->stage == TKT_STAGE_PLAY, "shared mask starts", i, L->stage);
		expect(G_T7, fabsf(L->env - (1.0f - colEgK[want])) < 1e-6f,
		       "T6 wins a shared layer", i, L->env);
		expect(G_T7, L->velo == ((i < 2u) ? (120.0f / 127.0f)
		                                 : (90.0f / 127.0f)),
		       "T6 velocity on the shared layer", i, L->velo);
	}

	/* reverse pop order: A T6, B T6 (T6 overwrites the pending T7), C T7 */
	voice_init();
	V.layerOn = 15u;
	for (i = 0u; i < TKT_LAYERS; i++)
		V.layer[i].decay = 90u;
	Tetrakt_triggerVoice(&V, 0u, 90u, 63u, 0x06u, alt);
	Tetrakt_triggerVoice(&V, 0u, 120u, 63u, 0x03u, NULL);
	for (i = 0u; i < TKT_LAYERS; i++) {
		TktLayer *L = &V.layer[i];
		const uint8_t want = (i < 2u) ? 90u : ((i == 2u) ? alt[2] : 0u);

		if (i >= 3u)
			continue;
		tkt_layerControl(&V, i, 0u, 1u);
		expect(G_T7, fabsf(L->env - (1.0f - colEgK[want])) < 1e-6f,
		       "reverse order keeps each track's decay", i, L->env);
	}

	/* a later T7 trigger after the latch restarts with its own decay */
	voice_init();
	V.layerOn = 1u;
	V.layer[0].decay = 90u;
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 1u, NULL);
	tkt_layerControl(&V, 0u, 0u, 1u);
	Tetrakt_triggerVoice(&V, 0u, 127u, 63u, 1u, alt);
	tkt_layerControl(&V, 0u, 0u, 1u);
	tkt_layerControl(&V, 0u, 0u, 1u);   /* FADE -> START, then the latch */
	expect(G_T7, fabsf(V.layer[0].env - (1.0f - colEgK[alt[0]])) < 1e-6f,
	       "a later T7 restart uses its decay", 0, V.layer[0].env);
}

int main(void)
{
	test_a2q();
	uint32_t i;
	int rc = 0;

	for (i = 0u; i < 1025u; i++)
		sine_table[i] = (int16_t)lrint(32767.0 * sin(2.0 * M_PI * (double)i / 1024.0));
	catalogue_host_reset();
	sampleMemory_refresh();
	install_samples();
	if (sampleMemory_getNumSamples() < 5u) {
		printf("tetrakt: sample install failed (%u)\n", sampleMemory_getNumSamples());
		return 1;
	}

	test_trig();
	test_layers_override();
	test_dec();
	test_tune();
	test_srr();
	test_tone();
	test_drv();
	test_w2();
	test_s1();
	test_vel();
	test_ring();
	test_read();
	test_t7();
	test_loop();

	for (i = 0u; i < G_N; i++) {
		printf("tetrakt %-7s failures: %u\n", g_name[i], g_fail[i]);
		if (g_fail[i])
			rc = 1;
	}
	(void)host_lfoRetriggers;
	return rc;
}
