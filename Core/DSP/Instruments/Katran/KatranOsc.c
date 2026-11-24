/*
 * KatranOsc.c - Katran's oscillator loops (S103 P1).
 * Contract: KatranOsc.h. Placement: flash (no ITCM section; P§2.3).
 */
#include "KatranOsc.h"
#include "oscPwmTables.h"  /* oscPwmTriW/Ra/Rb: the tri warp */
#include "oscPwmWarp.h"   /* osc_pwmWarp(): the platform's tri warp */

/*
 * The zero row (S103 P1).
 *
 * What:       a 1,024-sample row of zeros, used when fxbuf_arenaView() is
 *             shorter than one window (a defensive case; the arena is about
 *             63,000 samples).
 * Why:        the arena loops index ph >> 22 (0..1,023) unconditionally; a
 *             NULL or short arena must still read valid memory, and silence
 *             is the only safe choice.
 */
const int16_t katZeroRow[KAT_ARENA_WIN] = { 0 };

/*
 * One interpolated row read (S103 P1; the Colloid's col_read()).
 *
 * What:       ia = ph >> 22, na = (ia + 1) & 1,023, f = (ph >> 7) & 0x7FFF;
 *             returns a + ((b - a) x f >> 15).
 * Why:        the table forms share the Colloid's arithmetic (15-bit fraction:
 *             a row step may be 65,535, and 65,535 x 32,767 fits int32).
 */
static inline __attribute__((always_inline)) int32_t kat_read(const int16_t *row, uint32_t ph)
{
	const uint32_t ia = ph >> 22;
	const uint32_t na = (ia + 1u) & 0x3FFu;
	const int32_t a = row[ia];

	return a + (((row[na] - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}

/*
 * The arena read form's conditioning (S103 P1, P§3.4).
 *
 * What:       one arena sample x (an int16 read as it is) -> the DC block
 *             (one-pole high-pass at ~10 Hz: z += KAT_DC_K (x - z),
 *             y = x - z) -> the damping one-pole (damp += dampK (y - damp))
 *             -> the guard gain. The states live in the voice (dcX/dcY,
 *             damp, guard), one set per read block (A L, A R, B).
 * Why:        always on in this form (constant cost): a full-scale read of
 *             float state or power-up garbage must not walk away with the
 *             loop's DC or pin it at full scale.
 * Inputs:     v, c (0 A L, 1 A R, 2 B), x.
 * Outputs:    the conditioned value, as an int32.
 */
static inline __attribute__((always_inline)) int32_t kat_cond(KatranVoice *v, uint32_t c,
                                                              const int32_t x)
{
	const float xf = (float)x;
	float z = v->dcX[c];
	float y;
	float d;

	z += KAT_DC_K * (xf - z);
	v->dcX[c] = z;
	y = xf - z;
	v->dcY[c] = y;
	d = v->damp[c] + v->dampK * (y - v->damp[c]);
	v->damp[c] = d;
	d *= v->guard[c];
	return (int32_t)d;
}

/* The sync test's weight: one wrap sample is +P x 32767 (P§3.4/L3). */
#define KAT_SYNC_WRAP ((int32_t)(KAT_SYNC_P * 32767.0f))

/* A is reset when the sum crosses th upward. cur is this sample's test; the
 * previous sample's test is carried in v->syncPrev[c]. */
static inline __attribute__((always_inline)) uint32_t kat_aReset(KatranVoice *v, const uint32_t c,
                                                                 const uint32_t n,
                                                                 const int32_t th,
                                                                 uint32_t *ph,
                                                                 const int16_t **row)
{
	const int32_t s = (int32_t)(v->blkB[n] & 1) * KAT_SYNC_WRAP + (int32_t)v->blkD[c][n];
	const int32_t cur = (s > th);
	const uint32_t reset = (uint32_t)(cur && v->syncPrev[c] == 0);

	v->syncPrev[c] = cur;
	if (reset) {
		*ph = 0u;
		*row = v->rowANext[c];
	}
	return reset;
}

/* OSC B plain: the shared wav / wavetable form into blkB. */
void kat_bPlain(KatranVoice *v)
{
	const uint32_t inc = v->incB;
	const int16_t *row = v->rowB;
	uint32_t ph = v->phB;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t s = kat_read(row, ph);
		const uint32_t np = ph + inc;
		const uint32_t wrap = (uint32_t)(np < ph);

		v->blkB[n] = (int16_t)((s & (int32_t)0xFFFFFFFEu) | (int32_t)wrap);
		ph = np;
		if (wrap)
			row = v->rowBNext;
	}
	v->phB = ph;
	v->rowB = row;
}

/* OSC B, the two-read pulse (the Colloid's col_pwmPair form for one copy). */
void kat_bPwmD(KatranVoice *v, const int32_t sign)
{
	const uint32_t inc = v->incB;
	const uint32_t j = inc - (uint32_t)(((int32_t)v->pwD[2] - (int32_t)v->pwD0[2]) * 131072);
	const int16_t *row = v->rowB;
	uint32_t ph = v->phB;
	uint32_t q = ph - ((uint32_t)v->pwD0[2] << 22);
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t s = kat_read(row, ph) + sign * kat_read(row, q);
		const uint32_t np = ph + inc;
		const uint32_t wrap = (uint32_t)(np < ph);

		v->blkB[n] = (int16_t)((s & (int32_t)0xFFFFFFFEu) | (int32_t)wrap);
		ph = np;
		q += j;
		if (wrap)
			row = v->rowBNext;
	}
	v->phB = ph;
	v->rowB = row;
}

/* OSC B, the warped triangle. The warp's W, ra, rb come from the slewed
 * width index at the block's start (the Katran form; no intra-block glide). */
void kat_bWarp(KatranVoice *v)
{
	const uint32_t k = v->pwPos[2] & 127u;
	const uint32_t wPh = oscPwmTriW[k];
	const uint32_t ra = oscPwmTriRa[k];
	const uint32_t rb = oscPwmTriRb[k];
	const uint32_t inc = v->incB;
	const int16_t *row = v->rowB;
	uint32_t ph = v->phB;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t s = kat_read(row, osc_pwmWarp(ph, wPh, ra, rb));
		const uint32_t np = ph + inc;
		const uint32_t wrap = (uint32_t)(np < ph);

		v->blkB[n] = (int16_t)((s & (int32_t)0xFFFFFFFEu) | (int32_t)wrap);
		ph = np;
		if (wrap)
			row = v->rowBNext;
	}
	v->phB = ph;
	v->rowB = row;
}

/* OSC B, the arena window read (no interpolation). */
void kat_bArena(KatranVoice *v)
{
	const uint32_t inc = v->incB;
	const int16_t *row = v->rowB;
	uint32_t ph = v->phB;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t s = kat_cond(v, 2u, row[ph >> 22]);
		const uint32_t np = ph + inc;
		const uint32_t wrap = (uint32_t)(np < ph);

		v->blkB[n] = (int16_t)((s & (int32_t)0xFFFFFFFEu) | (int32_t)wrap);
		ph = np;
		if (wrap)
			row = v->rowBNext;
	}
	v->phB = ph;
	v->rowB = row;
}

/* OSC A plain, one copy (c 0 L, 1 R): the shared form into blkA, with the
 * hard sync from the core's delay output and OSC B's wrap. */
void kat_aPlain(KatranVoice *v, const uint32_t c, const int32_t th)
{
	const uint32_t inc = v->incA[c];
	const int16_t *row = v->rowA[c];
	uint32_t ph = v->phA[c];
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		int32_t s;
		uint32_t np;

		(void)kat_aReset(v, c, n, th, &ph, &row);
		s = kat_read(row, ph);
		v->blkA[n] = (int16_t)s;
		np = ph + inc;
		if (np < ph)
			row = v->rowANext[c];
		ph = np;
	}
	v->phA[c] = ph;
	v->rowA[c] = row;
}

/* OSC A, the two-read pulse, one copy. */
void kat_aPwmD(KatranVoice *v, const uint32_t c, const int32_t th, const int32_t sign)
{
	const uint32_t inc = v->incA[c];
	const uint32_t j = inc - (uint32_t)(((int32_t)v->pwD[c] - (int32_t)v->pwD0[c]) * 131072);
	const int16_t *row = v->rowA[c];
	uint32_t ph = v->phA[c];
	uint32_t q = ph - ((uint32_t)v->pwD0[c] << 22);
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		uint32_t np;

		(void)kat_aReset(v, c, n, th, &ph, &row);
		v->blkA[n] = (int16_t)(kat_read(row, ph) + sign * kat_read(row, q));
		np = ph + inc;
		q += j;
		if (np < ph)
			row = v->rowANext[c];
		ph = np;
	}
	v->phA[c] = ph;
	v->rowA[c] = row;
}

/* OSC A, the warped triangle, one copy. */
void kat_aWarp(KatranVoice *v, const uint32_t c, const int32_t th)
{
	const uint32_t k = v->pwPos[c] & 127u;
	const uint32_t wPh = oscPwmTriW[k];
	const uint32_t ra = oscPwmTriRa[k];
	const uint32_t rb = oscPwmTriRb[k];
	const uint32_t inc = v->incA[c];
	const int16_t *row = v->rowA[c];
	uint32_t ph = v->phA[c];
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		uint32_t np;

		(void)kat_aReset(v, c, n, th, &ph, &row);
		v->blkA[n] = (int16_t)kat_read(row, osc_pwmWarp(ph, wPh, ra, rb));
		np = ph + inc;
		if (np < ph)
			row = v->rowANext[c];
		ph = np;
	}
	v->phA[c] = ph;
	v->rowA[c] = row;
}

/* OSC A, the arena window read, one copy. */
void kat_aArena(KatranVoice *v, const uint32_t c, const int32_t th)
{
	const uint32_t inc = v->incA[c];
	const int16_t *row = v->rowA[c];
	uint32_t ph = v->phA[c];
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		int32_t s0;
		uint32_t np;

		/* the sync test reads this block's D and B, then the read (at the
		 * reset phase if the test fired) goes through the conditioning */
		{
			const int32_t st = (int32_t)(v->blkB[n] & 1) * KAT_SYNC_WRAP
			                 + (int32_t)v->blkD[c][n];
			const int32_t cur = (st > th);

			if (cur && v->syncPrev[c] == 0) {
				ph = 0u;
				row = v->rowANext[c];
			}
			v->syncPrev[c] = cur;
		}
		s0 = kat_cond(v, c, row[ph >> 22]);
		v->blkA[n] = (int16_t)s0;
		np = ph + inc;
		if (np < ph)
			row = v->rowANext[c];
		ph = np;
	}
	v->phA[c] = ph;
	v->rowA[c] = row;
}
