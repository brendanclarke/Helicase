/*
 * Core/DSPAudio/voiceSpace.c
 *
 *  Created on: 22.11.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 *
 * The Spc chain's spatializer and the Cymbal/HiHat low-pass (S101 P3).
 * Contracts in voiceSpace.h; the design and its measured costs in
 * S101_P3_BASIC_CHAIN_SPATIALIZER.md (revision 5), the build log in
 * S101_P3_IMPLEMENTATION.md.
 *
 * Build:      -Ofast -fno-lto (Makefile): outside the LTO unit the new loops
 *             cannot move LTO's partition of the other engines' loops (the
 *             S095 P2 / S099 P1 finding); every entry point is noinline, so
 *             nothing here is inlined into mixer_calcNextSampleBlock().
 */
#include <string.h>
#include "voiceSpace.h"
#include "FxBuffer.h"          /* FXBUF_VOICE_UNIT_SAMPLES (the layout assert) */
#include "TetraktTables.h"     /* tktFbLpK[]: the Tetrakt's S1 tone law */

_Static_assert(VSPACE_RING + VSPACE_MIRROR == FXBUF_VOICE_UNIT_SAMPLES,
               "the ring and its mirror are exactly one voice unit");
_Static_assert((VSPACE_RING % OUTPUT_DMA_SIZE) == 0u,
               "the write position wraps only at a block boundary");
_Static_assert(VSPACE_MIRROR == OUTPUT_DMA_SIZE,
               "the mirror covers one block of reads past the ring's end");
_Static_assert(VSPACE_TAP_MIN >= OUTPUT_DMA_SIZE,
               "no tap reads the mirror in the block that writes ring[0..31]");
_Static_assert(VSPACE_TIME_MAX + 1u < VSPACE_RING,
               "a tap's older sample never reaches the write position early");
#if UINTPTR_MAX == 0xFFFFFFFFu   /* the 32-bit target (the host tests run 64-bit) */
_Static_assert(sizeof(VoiceSpace) == 80u, "VoiceSpace is 80 B (S101 P3 §6.8)");
#endif

/*
 * The DSP primitives (S101 P3 §4.2).
 *
 * What:       VS_SMULWB(a, b) = (a x (int16)b) >> 16 and VS_SMULWT(a, b) =
 *             (a x (int16)(b >> 16)) >> 16 (the SMLAWB / SMLAWT products with
 *             a zero accumulator), VS_SSAT16 the 16-bit saturation, VS_SMMUL
 *             the high word of a 32 x 32 product.
 * Why:        packed Q15 coefficient pairs and the DSP multiplies keep the
 *             loop in integer registers with no float conversion (measured:
 *             the float forms cost 51-63 a sample). The portable forms give
 *             the same values on the host (tools/dsp_test), bit for bit.
 */
#if defined(__ARM_FEATURE_DSP)
#include <arm_acle.h>
#define VS_SMULWB(a, b) __smlawb((a), (b), 0)
#define VS_SMULWT(a, b) __smlawt((a), (b), 0)
#define VS_SSAT16(x)    __ssat((x), 16)
#else
static inline int32_t VS_SMULWB(int32_t a, int32_t b)
{
	return (int32_t)(((int64_t)a * (int16_t)(b & 0xFFFF)) >> 16);
}
static inline int32_t VS_SMULWT(int32_t a, int32_t b)
{
	return (int32_t)(((int64_t)a * (int16_t)((uint32_t)b >> 16)) >> 16);
}
static inline int32_t VS_SSAT16(int32_t x)
{
	return (x > 32767) ? 32767 : ((x < -32768) ? -32768 : x);
}
#endif
#define VS_SMMUL(a, b) ((int32_t)(((int64_t)(a) * (int64_t)(b)) >> 32))

/*
 * The time law (S101 P3 Q3, decided: exponential).
 *
 * What:       vspaceTime[v] = 44 x (2174 / 44)^(v / 127) samples: 1.0 ms at 0,
 *             6.9 ms at 63, 49.3 ms at 127 (REAL_FS 44,108 Hz). Equal steps
 *             of the knob are equal steps of comb pitch (2.3 % a step).
 * Why:        on a linear law the useful comb pitches (1-5 ms, 1 kHz-200 Hz)
 *             would be the first ten steps. A table costs no instruction.
 * Source:     python3 -c "print([44*(2174/44)**(v/127) for v in range(128)])"
 *             (values to 4 decimals).
 */
static const float vspaceTime[128] = {
    44.0000f, 45.3722f, 46.7872f, 48.2463f,
    49.7509f, 51.3024f, 52.9024f, 54.5522f,
    56.2535f, 58.0078f, 59.8168f, 61.6823f,
    63.6059f, 65.5895f, 67.6350f, 69.7443f,
    71.9193f, 74.1622f, 76.4751f, 78.8600f,
    81.3194f, 83.8554f, 86.4705f, 89.1672f,
    91.9480f, 94.8155f, 97.7724f, 100.8215f,
    103.9658f, 107.2081f, 110.5515f, 113.9991f,
    117.5543f, 121.2204f, 125.0008f, 128.8991f,
    132.9190f, 137.0642f, 141.3387f, 145.7465f,
    150.2918f, 154.9788f, 159.8120f, 164.7959f,
    169.9352f, 175.2348f, 180.6997f, 186.3351f,
    192.1461f, 198.1384f, 204.3176f, 210.6895f,
    217.2601f, 224.0356f, 231.0224f, 238.2271f,
    245.6564f, 253.3175f, 261.2175f, 269.3639f,
    277.7643f, 286.4267f, 295.3592f, 304.5703f,
    314.0687f, 323.8633f, 333.9633f, 344.3783f,
    355.1182f, 366.1929f, 377.6131f, 389.3894f,
    401.5329f, 414.0552f, 426.9679f, 440.2834f,
    454.0141f, 468.1731f, 482.7736f, 497.8294f,
    513.3548f, 529.3644f, 545.8732f, 562.8968f,
    580.4514f, 598.5535f, 617.2200f, 636.4687f,
    656.3177f, 676.7857f, 697.8920f, 719.6566f,
    742.0999f, 765.2431f, 789.1081f, 813.7173f,
    839.0940f, 865.2621f, 892.2463f, 920.0720f,
    948.7655f, 978.3538f, 1008.8649f, 1040.3274f,
    1072.7712f, 1106.2268f, 1140.7257f, 1176.3006f,
    1212.9848f, 1250.8131f, 1289.8211f, 1330.0457f,
    1371.5246f, 1414.2972f, 1458.4036f, 1503.8856f,
    1550.7860f, 1599.1490f, 1649.0202f, 1700.4468f,
    1753.4771f, 1808.1613f, 1864.5508f, 1922.6990f,
    1982.6605f, 2044.4920f, 2108.2518f, 2174.0000f,
};

/*
 * The block's coefficients (S101 P3 §6.3).
 *
 * What:       tL = vspaceTime[time]; tR = max(tL x (1 - 0.5 spread / 127), 32)
 *             (the Tetrakt's TKT_SDT_SPAN); the integer delays dL, dR and the
 *             Q15 fractions packed fL | fR << 16; fb = (feedback x 127 - 63) x
 *             0.97 / 64, clamped to +-0.97, packed with slv = level / 127 as
 *             fb | slv << 16 (Q15 each); fbK = tktFbLpK[tone] in Q15; the
 *             ramp g (Q30) and its step (g1 - g0) / 31 (Q30, a multiply).
 *             The read starts rl, rr in [1, VSPACE_RING] (voiceSpace.h).
 */
typedef struct {
	const int16_t *rl, *rr;
	int32_t fLR, fbslv, fbK, g, dg;
} VsBlock;

static inline void vs_block(const VoiceSpace *sp, uint8_t spread, float feedback,
                            float g0, float g1, VsBlock *b)
{
	const uint32_t wi = sp->wi;
	const float tL = vspaceTime[sp->time & 127u];
	const float sp127 = (float)((spread > 127u) ? 127u : spread) * (1.0f / 127.0f);
	float tR = tL * (1.0f - 0.5f * sp127);
	float fb = (feedback * 127.0f - 63.0f) * (0.97f / 64.0f);
	uint32_t dL, dR;
	int32_t sL, sR, fbq, slv;

	tR = (tR < (float)VSPACE_TAP_MIN) ? (float)VSPACE_TAP_MIN : tR;
	dL = (uint32_t)tL;
	dR = (uint32_t)tR;
	b->fLR = (int32_t)((tL - (float)dL) * 32767.0f)
#if VSPACE_SPREAD_INTERP
	       | ((int32_t)((tR - (float)dR) * 32767.0f) << 16)
#endif
	       ;
	fb = (fb > 0.97f) ? 0.97f : ((fb < -0.97f) ? -0.97f : fb);
	fbq = (int32_t)(fb * 32767.0f);
	slv = (int32_t)((float)((sp->level > 127u) ? 127u : sp->level) * (32767.0f / 127.0f));
	b->fbslv = (fbq & 0xFFFF) | (slv << 16);
	b->fbK = (int32_t)(tktFbLpK[sp->tone & 127u] * 32767.0f);
	g0 = (g0 < 0.0f) ? 0.0f : ((g0 > 1.999f) ? 1.999f : g0);
	g1 = (g1 < 0.0f) ? 0.0f : ((g1 > 1.999f) ? 1.999f : g1);
	b->g = (int32_t)(g0 * 1073741824.0f);
	b->dg = (int32_t)((g1 - g0) * (1073741824.0f / 31.0f));
	sL = (int32_t)wi - (int32_t)dL;
	sR = (int32_t)wi - (int32_t)dR;
	b->rl = sp->line + ((sL > 0) ? sL : sL + (int32_t)VSPACE_RING);
	b->rr = sp->line + ((sR > 0) ? sR : sR + (int32_t)VSPACE_RING);
}

/*
 * The block's end (S101 P3 §4.1).
 * What:       after the block that wrote ring[0..31], copies them to the
 *             mirror (64 B, one block move); advances wi by one block.
 */
static inline void vs_end(VoiceSpace *sp)
{
	int16_t *const ring = sp->line;

	if (sp->wi == 0u) {
		typedef struct { uint32_t w[VSPACE_MIRROR / 2u]; } VsBlk;
		*(VsBlk *)(void *)(ring + VSPACE_RING) = *(const VsBlk *)(const void *)ring;
	}
	{
		const uint32_t next = (uint32_t)sp->wi + OUTPUT_DMA_SIZE;

		sp->wi = (uint16_t)((next == VSPACE_RING) ? 0u : next);
	}
}

/*
 * No unit (S101 P3 §6.4): the dry ramp only, into the left block (the mixer
 * reads no right block while the slot holds no unit). Only between a chain
 * switch and its bind; never the worst case (the space loop is dearer).
 */
static void vs_dry(int16_t *buf, int32_t g, const int32_t dg)
{
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		buf[n] = (int16_t)VS_SSAT16(VS_SMMUL((int32_t)buf[n] << 2, g));
		g += dg;
	}
}

void voiceSpace_init(VoiceSpace *sp)
{
	if (!sp)
		return;
	memset(sp, 0, sizeof(*sp));
	sp->time = 64u;
	sp->tone = 127u;
	sp->level = 0u;
}

void voiceSpace_bind(VoiceSpace *sp, int16_t *unit)
{
	if (!sp || unit == sp->line)
		return;
	if (unit)
		memset(unit, 0, FXBUF_VOICE_UNIT_SAMPLES * sizeof(int16_t));
	sp->line = unit;
	sp->wi = 0u;
	sp->lp = 0;
	memset(sp->right, 0, sizeof(sp->right));
}

__attribute__((noinline))
void voiceSpace_process(VoiceSpace *sp, int16_t *buf, float g0, float g1,
                        uint8_t spread, float feedback)
{
	/*
	 * The one-input frame (S101 P3 §4.2 form E; prototype loop 46).
	 *
	 * Per sample: x = buf x g (Q30); the taps a + f (b - a), b the older
	 * sample; lp += fbK ((tL + tR)/2 - lp) in Q15 x 2 (the taps' sum << 14);
	 * ring = sat16(x + fb lp); L = sat16(x + slv tL), R = sat16(x + slv
	 * tR). Reads before the write in each frame, so a tap of the longest
	 * delay reads the old sample of the slot this frame overwrites.
	 */
	VsBlock b;

	if (!sp || !buf)
		return;
	vs_block(sp, spread, feedback, g0, g1, &b);
	if (!sp->line) {
		vs_dry(buf, b.g, b.dg);
		return;
	}
	{
		const int16_t *rl = b.rl, *rr = b.rr;
		const int32_t fLR = b.fLR, fbslv = b.fbslv, fbK = b.fbK, dg = b.dg;
		int32_t g = b.g, lp = sp->lp;
		int16_t *ww = sp->line + sp->wi;
		int16_t *p = buf;
		int16_t *q = sp->right;
		int16_t *const end = buf + OUTPUT_DMA_SIZE;

		do {
			const int32_t x = VS_SMMUL((int32_t)*p << 2, g);
			const int32_t al = rl[0], ar = rr[0];
			const int32_t tl = al + VS_SMULWB((rl[-1] - al) << 1, fLR);
#if VSPACE_SPREAD_INTERP
			const int32_t tr = ar + VS_SMULWT((rr[-1] - ar) << 1, fLR);
#else
			const int32_t tr = ar;   /* form H: whole-sample spread tap */
#endif

			rl++;
			rr++;
			g += dg;
			lp += VS_SMULWB(((tl + tr) << 14) - lp, fbK) << 1;
			*ww++ = (int16_t)VS_SSAT16(x + (VS_SMULWB(lp, fbslv) >> 14));
			*p++ = (int16_t)VS_SSAT16(x + VS_SMULWT(tl << 1, fbslv));
			*q++ = (int16_t)VS_SSAT16(x + VS_SMULWT(tr << 1, fbslv));
		} while (p != end);
		sp->lp = lp;
	}
	vs_end(sp);
}

__attribute__((noinline))
void voiceSpace_processMix(VoiceSpace *sp, int16_t *buf, const int16_t *add,
                           float mix, float g0, float g1,
                           uint8_t spread, float feedback)
{
	/*
	 * The Snare's frame (S101 P3 §4.2 form E, Snare; prototype loop 54):
	 * x = sat16(noise x mix + tone) x g, the rest as voiceSpace_process().
	 * mix (0..1) in Q15.
	 */
	VsBlock b;
	const int32_t mq = (int32_t)(((mix < 0.0f) ? 0.0f : ((mix > 1.0f) ? 1.0f : mix))
	                             * 32767.0f);

	if (!sp || !buf || !add)
		return;
	vs_block(sp, spread, feedback, g0, g1, &b);
	if (!sp->line) {
		uint32_t n;

		for (n = 0u; n < OUTPUT_DMA_SIZE; n++)
			buf[n] = (int16_t)VS_SSAT16(((buf[n] * mq) >> 15) + add[n]);
		vs_dry(buf, b.g, b.dg);
		return;
	}
	{
		const int16_t *rl = b.rl, *rr = b.rr;
		const int16_t *tp = add;
		const int32_t fLR = b.fLR, fbslv = b.fbslv, fbK = b.fbK, dg = b.dg;
		int32_t g = b.g, lp = sp->lp;
		int16_t *ww = sp->line + sp->wi;
		int16_t *p = buf;
		int16_t *q = sp->right;
		int16_t *const end = buf + OUTPUT_DMA_SIZE;

		do {
			const int32_t x = VS_SMMUL(VS_SSAT16(((*p * mq) >> 15) + *tp++) << 2, g);
			const int32_t al = rl[0], ar = rr[0];
			const int32_t tl = al + VS_SMULWB((rl[-1] - al) << 1, fLR);
#if VSPACE_SPREAD_INTERP
			const int32_t tr = ar + VS_SMULWT((rr[-1] - ar) << 1, fLR);
#else
			const int32_t tr = ar;   /* form H: whole-sample spread tap */
#endif

			rl++;
			rr++;
			g += dg;
			lp += VS_SMULWB(((tl + tr) << 14) - lp, fbK) << 1;
			*ww++ = (int16_t)VS_SSAT16(x + (VS_SMULWB(lp, fbslv) >> 14));
			*p++ = (int16_t)VS_SSAT16(x + VS_SMULWT(tl << 1, fbslv));
			*q++ = (int16_t)VS_SSAT16(x + VS_SMULWT(tr << 1, fbslv));
		} while (p != end);
		sp->lp = lp;
	}
	vs_end(sp);
}

__attribute__((noinline))
void voiceSpace_lp1(ResonantFilter *filter, int16_t *buf)
{
	/*
	 * The Cymbal/HiHat low-pass (S101 P3 §6.2; contract in voiceSpace.h).
	 *
	 * Per block: g and the damping k = 2R from the ZDF's own coefficients
	 * (SVF_calcBlockZDF(): R = 1 at f >= 0.4499, else q, q >= 0.02 from
	 * SVF_setReso()); a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2 (the
	 * one division); d = drive / 32767 (the LP's drive floor offset is 0).
	 * Per sample: u = in x d, |u| <= 3, x = u - u^3 / 27 (the ZDF input
	 * saturator 2 tanh(u/2): slope 1, limit 2); the TPT step; c = v2,
	 * |c| <= 1.551, out = c (0.967 - 0.1340 c^2) x 32767 (fastTanh()'s
	 * slope 4.15 / 4.29 and its limit 1).
	 */
	const float g = filter->g;
	const float R = (filter->f >= 0.4499f) ? 1.0f : filter->q;
	const float k = 2.0f * R;
	const float a1 = 1.0f / (1.0f + g * (g + k));
	const float a2 = g * a1;
	const float a3 = g * a2;
	const float d = filter->drive * (1.0f / 32767.0f);
	float ic1 = filter->s1, ic2 = filter->s2;
	uint32_t i;

	for (i = 0u; i < OUTPUT_DMA_SIZE; i++) {
		float u = (float)buf[i] * d;
		float c, v1, v2, v3, x;

		u = fminf(fmaxf(u, -3.0f), 3.0f);
		x = u - (1.0f / 27.0f) * u * u * u;
		v3 = x - ic2;
		v1 = a1 * ic1 + a2 * v3;
		v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.0f * v1 - ic1;
		ic2 = 2.0f * v2 - ic2;
		c = fminf(fmaxf(v2, -1.551f), 1.551f);
		buf[i] = (int16_t)(int32_t)(c * (0.967f - 0.1340f * c * c) * 32767.0f);
	}
	filter->s1 = ic1;
	filter->s2 = ic2;
}
