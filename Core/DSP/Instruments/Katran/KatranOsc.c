/*
 * KatranOsc.c - Katran's oscillator loops (S103 P1; S103 P2: the full-rate /
 * half-rate core pair, the PWM-sum and arena-store fixes, loop state in
 * registers; S103 P4: the cores are named A (full rate) and B (half rate),
 * and the OSC A sync reset is branch-free).
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
 * A saturating int16 store value (S103 P2, bug B3).
 *
 * What:       x clamped to -32,768..32,767: one SSAT on the device, the
 *             two-sided compare on the host.
 * Why:        the arena form's conditioned value can leave the int16 range
 *             (the DC block's high-pass of a full-scale step overshoots to
 *             about 2x before the guard, a block later, catches it). A bare
 *             (int16_t) cast wrapped it to the opposite sign; this clips it.
 *             Inline asm, so the loop needs no CMSIS header (the host build
 *             has none).
 */
static inline __attribute__((always_inline)) int16_t kat_osat16(const int32_t x)
{
#if defined(__ARM_ARCH_7EM__)
	int32_t r;

	__asm__("ssat %0, #16, %1" : "=r"(r) : "r"(x));
	return (int16_t)r;
#else
	return (int16_t)((x > 32767) ? 32767 : ((x < -32768) ? -32768 : x));
#endif
}

/*
 * The arena read form's conditioning, one step (S103 P1; S103 P2: the state
 * in registers).
 *
 * What:       one arena sample x (an int16 read as it is) -> the DC block
 *             (one-pole high-pass at ~10 Hz: z += KAT_DC_K (x - z),
 *             y = x - z) -> the damping one-pole (d += dampK (y - d)) -> the
 *             guard gain. *z and *d are the caller's register copies of the
 *             voice's dcX / damp for this read block.
 * Why:        always on in this form (constant cost): a full-scale read of
 *             float state or power-up garbage must not walk away with the
 *             loop's DC or pin it at full scale. S103 P2: the loops keep z and
 *             d in locals and write them back once a block. The P1 form read
 *             and wrote four voice members per sample through the voice
 *             pointer (and dcY, which nothing reads).
 * Outputs:    the conditioned value as an int32 (the caller saturates).
 */
static inline __attribute__((always_inline)) int32_t kat_condStep(float *z, float *d,
                                                                  const float dampK,
                                                                  const float guard,
                                                                  const int32_t x)
{
	const float xf = (float)x;
	float y;

	*z += KAT_DC_K * (xf - *z);
	y = xf - *z;
	*d += dampK * (y - *d);
	return (int32_t)(*d * guard);
}

/*
 * The same step on a voice's own state (S103 P1 kat_cond(), kept for the host
 * law KA-ARENA, modules/katran.c).
 */
static inline __attribute__((always_inline)) int32_t kat_cond(KatranVoice *v, uint32_t c,
                                                              const int32_t x)
{
	return kat_condStep(&v->dcX[c], &v->damp[c], v->dampK, v->guard[c], x);
}

/* The sync test's weight: one wrap sample is +P x 32767 (P§3.4/L3). */
#define KAT_SYNC_WRAP ((int32_t)(KAT_SYNC_P * 32767.0f))

/* OSC B plain: the shared wav / wavetable form into blkB. */
void kat_bPlain(KatranVoice *v)
{
	const uint32_t inc = v->incB;
	const int16_t *const rowNext = v->rowBNext;
	const int16_t *row = v->rowB;
	int16_t *const out = v->blkB;
	uint32_t ph = v->phB;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t s = kat_read(row, ph);
		const uint32_t np = ph + inc;
		const uint32_t wrap = (uint32_t)(np < ph);

		out[n] = (int16_t)((s & (int32_t)0xFFFFFFFEu) | (int32_t)wrap);
		ph = np;
		row = wrap ? rowNext : row;
	}
	v->phB = ph;
	v->rowB = row;
}

/*
 * OSC B, the two-read pulse (the Colloid's col_pwmPair form for one copy).
 *
 * S103 P2, bug B2: the two reads' sum is halved before the int16 store.
 * The sum reaches +-65,534 (two full-scale saw reads added for Saw, or one
 * minus the other for Rec), and the P1 form stored it as (int16_t), so it
 * wrapped to the opposite sign whenever the reads added past full scale. The
 * Colloid has no wrap because it keeps the same sum in int32 buses
 * (ColloidOsc.c col_pwmPairStore()). Halving keeps the waveform's shape; the
 * level it costs (-6 dB) is the exciter's and the ring's, both set by rows.
 */
void kat_bPwmD(KatranVoice *v, const int32_t sign)
{
	const uint32_t inc = v->incB;
	const uint32_t j = inc - (uint32_t)(((int32_t)v->pwD[2] - (int32_t)v->pwD0[2]) * 131072);
	const int16_t *const rowNext = v->rowBNext;
	const int16_t *row = v->rowB;
	int16_t *const out = v->blkB;
	uint32_t ph = v->phB;
	uint32_t q = ph - ((uint32_t)v->pwD0[2] << 22);
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t s = (kat_read(row, ph) + sign * kat_read(row, q)) >> 1;
		const uint32_t np = ph + inc;
		const uint32_t wrap = (uint32_t)(np < ph);

		out[n] = (int16_t)((s & (int32_t)0xFFFFFFFEu) | (int32_t)wrap);
		ph = np;
		q += j;
		row = wrap ? rowNext : row;
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
	const int16_t *const rowNext = v->rowBNext;
	const int16_t *row = v->rowB;
	int16_t *const out = v->blkB;
	uint32_t ph = v->phB;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t s = kat_read(row, osc_pwmWarp(ph, wPh, ra, rb));
		const uint32_t np = ph + inc;
		const uint32_t wrap = (uint32_t)(np < ph);

		out[n] = (int16_t)((s & (int32_t)0xFFFFFFFEu) | (int32_t)wrap);
		ph = np;
		row = wrap ? rowNext : row;
	}
	v->phB = ph;
	v->rowB = row;
}

/*
 * OSC B, the arena window read (no interpolation).
 *
 * S103 P2: bug B3 (the conditioned value is saturated before the store; it
 * wrapped before) and the conditioning state in registers (kat_condStep()).
 */
void kat_bArena(KatranVoice *v)
{
	const uint32_t inc = v->incB;
	const int16_t *const rowNext = v->rowBNext;
	const int16_t *row = v->rowB;
	int16_t *const out = v->blkB;
	const float dampK = v->dampK;
	const float guard = v->guard[2];
	float z = v->dcX[2];
	float d = v->damp[2];
	uint32_t ph = v->phB;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t s = kat_osat16(kat_condStep(&z, &d, dampK, guard, row[ph >> 22]));
		const uint32_t np = ph + inc;
		const uint32_t wrap = (uint32_t)(np < ph);

		out[n] = (int16_t)((s & (int32_t)0xFFFFFFFEu) | (int32_t)wrap);
		ph = np;
		row = wrap ? rowNext : row;
	}
	v->phB = ph;
	v->rowB = row;
	v->dcX[2] = z;
	v->damp[2] = d;
}

/*
 * One copy of OSC A, one block (S103 P2: the shared body of the eight loops;
 * S103 P4: *CoreA / *CoreB, formerly *L / *H).
 *
 * What:       per sample n of `count` (32 full rate, 16 half rate):
 *             1. the hard sync: st = P x wrap + D[n], where wrap is OSC B's
 *                wrap bit at the same time (full rate: blkB[n]; half rate:
 *                the OR over blkB[2n] and blkB[2n + 1], so no wrap of the
 *                full-rate OSC B is missed) and D the core's own delay output
 *                (blkD[c][n]); an upward crossing of th (cur = st > th, the
 *                previous sample's test 0) resets the phase to 0 and takes
 *                rowNext (the cycle start, K-D10);
 *             2. the read for `form`: PLAIN (the interpolated row), PWMD (the
 *                two-read pulse, halved: bug B2), WARP (the warped triangle),
 *                ARENA (the uninterpolated arena read through the
 *                conditioning, saturated: bug B3);
 *             3. the phase step; a natural wrap also takes rowNext.
 *             The half-rate copy steps its phase by 2 x incA[1] (so its pitch
 *             matches the full-rate copy's law), and the PWM D second phase
 *             glides over 16 samples instead of 32 (j = inc - dW x 2^22 /
 *             count, which lands q exactly on the block's end width).
 * Why:        S103 P2: core B runs at half rate. One body for both rates and
 *             all four forms keeps the forms identical between the cores;
 *             `form` and `half` are constants in each wrapper, so the compiler
 *             removes the other forms and rates (constant CPU per wrapper).
 *             The sync state, the conditioning state and the block pointers
 *             live in locals and are written back once (the P1 loops read and
 *             wrote them through the voice pointer every sample).
 *             Also (S103 P2): on a sync reset the PWM D second phase q moves
 *             with the first (q -= ph before ph = 0), so the pulse keeps its
 *             width across the reset; the P1 form left q behind until the next
 *             block.
 * Inputs:     v, c (0 = core A, 1 = core B), th, sign (PWMD), form, half.
 * Outputs:    blkA[0..count-1]; phA[c], rowA[c], syncPrev[c]; under ARENA
 *             dcX[c], damp[c].
 */
static inline __attribute__((always_inline)) void kat_aBody(KatranVoice *v, const uint32_t c,
                                                            const int32_t th, const int32_t sign,
                                                            const uint32_t form, const uint32_t half)
{
	const uint32_t count = half ? (OUTPUT_DMA_SIZE / 2u) : OUTPUT_DMA_SIZE;
	const uint32_t inc = half ? (v->incA[c] << 1) : v->incA[c];
	const int16_t *const dSelf = v->blkD[c];
	const int16_t *const blkB = v->blkB;
	int16_t *const out = v->blkA;
	const int16_t *const rowNext = v->rowANext[c];
	const int16_t *row = v->rowA[c];
	uint32_t ph = v->phA[c];
	int32_t prev = v->syncPrev[c];
	/* the PWM D second read: width glide over this block (see What) */
	const int32_t dW = (int32_t)v->pwD[c] - (int32_t)v->pwD0[c];
	const uint32_t j = inc - (uint32_t)(dW * (int32_t)((1u << 22) / count));
	uint32_t q = ph - ((uint32_t)v->pwD0[c] << 22);
	/* the warp's parameters (from the slewed width index) */
	const uint32_t k = v->pwPos[c] & 127u;
	const uint32_t wPh = oscPwmTriW[k];
	const uint32_t ra = oscPwmTriRa[k];
	const uint32_t rb = oscPwmTriRb[k];
	/* the arena conditioning, in registers */
	const float dampK = v->dampK;
	const float guard = v->guard[c];
	float z = v->dcX[c];
	float d = v->damp[c];
	uint32_t n;

	for (n = 0u; n < count; n++) {
		const int32_t wrapB = half ? (int32_t)((blkB[n << 1] | blkB[(n << 1) + 1u]) & 1)
		                           : (int32_t)(blkB[n] & 1);
		const int32_t st = wrapB * KAT_SYNC_WRAP + (int32_t)dSelf[n];
		const uint32_t cur = (uint32_t)(st > th);
		/*
		 * S103 P4: the reset without a branch. rst is 1 on an upward
		 * crossing (cur set, the previous test clear); keep is then 0, else
		 * all ones, so `ph & keep` is the reset phase and `ph & ~keep` the
		 * amount the PWM D second phase moves with it (q -= ph, as P2's B5).
		 * prev may hold the init value -1 ("unknown"): ~prev is then 0 and
		 * the first sample never resets, as the P2 `prev == 0` test.
		 * Why: the P2 `if (cur && prev == 0) { ... }` compiled into a
		 * branchy loop whose longest path measured 56 instructions a sample
		 * (OSC A, PWM D, core A), against 30 for OSC B's same form with no
		 * sync. The select form keeps one straight path.
		 */
		const uint32_t rst = cur & ~(uint32_t)prev;
		const uint32_t keep = rst - 1u;
		int32_t s;
		uint32_t np;

		q -= ph & ~keep;
		ph &= keep;
		row = rst ? rowNext : row;
		prev = (int32_t)cur;
		if (form == KAT_FORM_PWMD)
			s = (kat_read(row, ph) + sign * kat_read(row, q)) >> 1;
		else if (form == KAT_FORM_WARP)
			s = kat_read(row, osc_pwmWarp(ph, wPh, ra, rb));
		else if (form == KAT_FORM_ARENA)
			s = kat_osat16(kat_condStep(&z, &d, dampK, guard, row[ph >> 22]));
		else
			s = kat_read(row, ph);
		out[n] = (int16_t)s;
		np = ph + inc;
		row = (np < ph) ? rowNext : row;
		ph = np;
		q += j;
	}
	v->phA[c] = ph;
	v->rowA[c] = row;
	v->syncPrev[c] = prev;
	if (form == KAT_FORM_ARENA) {
		v->dcX[c] = z;
		v->damp[c] = d;
	}
}

/*
 * OSC A's eight wrappers (S103 P4: core A / core B, formerly the *L / *H
 * sets): core A's copy at the full rate, core B's at half rate. Contract:
 * KatranOsc.h.
 */
void kat_aPlainCoreA(KatranVoice *v, const int32_t th)
{
	kat_aBody(v, 0u, th, 1, KAT_FORM_PLAIN, 0u);
}

void kat_aPwmDCoreA(KatranVoice *v, const int32_t th, const int32_t sign)
{
	kat_aBody(v, 0u, th, sign, KAT_FORM_PWMD, 0u);
}

void kat_aWarpCoreA(KatranVoice *v, const int32_t th)
{
	kat_aBody(v, 0u, th, 1, KAT_FORM_WARP, 0u);
}

void kat_aArenaCoreA(KatranVoice *v, const int32_t th)
{
	kat_aBody(v, 0u, th, 1, KAT_FORM_ARENA, 0u);
}

void kat_aPlainCoreB(KatranVoice *v, const int32_t th)
{
	kat_aBody(v, 1u, th, 1, KAT_FORM_PLAIN, 1u);
}

void kat_aPwmDCoreB(KatranVoice *v, const int32_t th, const int32_t sign)
{
	kat_aBody(v, 1u, th, sign, KAT_FORM_PWMD, 1u);
}

void kat_aWarpCoreB(KatranVoice *v, const int32_t th)
{
	kat_aBody(v, 1u, th, 1, KAT_FORM_WARP, 1u);
}

void kat_aArenaCoreB(KatranVoice *v, const int32_t th)
{
	kat_aBody(v, 1u, th, 1, KAT_FORM_ARENA, 1u);
}
