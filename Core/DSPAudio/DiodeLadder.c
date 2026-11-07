/*
 * DiodeLadder.c - TB-303 diode-ladder VCF + feedback network + OTA, real-time
 * form (S093 P0). See DiodeLadder.h for the contract.
 *
 * What:       DiodeLadder_renderA / _renderB: one 32-sample block of
 *             oscillator (in buf) -> ladder -> network -> OTA (out to buf).
 *             The equations are S092_P6_DSP_EQUATIONS.md §3 (circuit), §5.2
 *             (TPT step, secant linearisation, loop closure with one
 *             division) and §5.3 (SplitNet); the kernel follows the
 *             pseudo-code of §5.4 and tools/alkaline/alk_model.py
 *             LadderTPT._ladder_step / _process_block line for line.
 * Why:        S092 P0 §4: a new function; ResonantFilter.c is not touched.
 *             State is loaded into locals and written back once per block;
 *             no switch in a loop; one int16 saturation at the end.
 * Approximations against the Python reference (S093 P0 §4.3):
 *             - tanh by the [5/4] Pade x(945 + 105x^2 + x^4)/(945 + 420x^2 +
 *               15x^4), argument clamped to +-3.5 (error <= 1e-3 there,
 *               < 2e-5 below 2);
 *             - the secant gains' reciprocals batched: one VDIV per sample
 *               for all of them;
 *             - the tridiagonal pivots from continuants with one shared
 *               reciprocal (option B) or batched with the secants (A);
 *             - the OTA output one sample late (its tanh shares the batch).
 * Constant cost: every sample runs the same instructions; every block runs
 *             the full slow-network update (S092 P0 §1.3).
 * Affiliates: AlkalineTables.c (alkResTable); AlkalineVoice.c; the bench
 *             tools/dsp_test modules/alkaline.c; tools/alkaline/check_c.py.
 */
#include <math.h>
#include "DiodeLadder.h"
#include "config.h"
#include "stm32f4xx.h"

/* ---------------------------------------------------------------- constants
 * Derived in S093 P0 §3.2 from S092 P6 §3 (alk_model.py values):
 *   h/2 = 0.5/fs;  w_k = (h/2) I / (4 Vt C_k)  ->  w0 = K0 I, w1 = K1 I
 *   R_in21 = 100k || (300 Vt / (0.51 mA / 2)) = 23.32 k; tau_out = 0.1u R_in21
 *   g = tan(1/(2 fs tau_out)); KG = 1/(1+g); TWO_G = 2g
 *   term L: wl = (h/2) / (2 R_in21 C26); KL = wl/(2Vt); D3X = wl KG
 *   Q21: A_OUT = 103.6 x 2Vt; KA = -A_OUT KG; KB = A_OUT KG / (2Vt)
 *   OTA load (47k + 22k) || 220k
 *   asymptote poles a = 2 pi 5 Hz: AL = (1 - a h/2)/(1 + a h/2),
 *   BE = (h/2)/(1 + a h/2), DECAY = e^(-a 32/fs) (gen_alk_tables.py checks it) */
#define DL_K0          6090.59326f
#define DL_K1          3322.14185f
#define DL_KG          0.995162427f
#define DL_TWO_G       0.00972212479f
#define DL_KL          0.142460123f
#define DL_D3X         0.00732955895f
#define DL_KA          (-5.33020973f)
#define DL_KB          103.098831f
#define DL_A_OUT       5.35612011f
#define DL_R_OTA       52525.9531f
#define DL_ASYM_AL     0.999288023f
#define DL_ASYM_BE     1.13317765e-05f
#define DL_ASYM_DECAY  0.977465749f
#define DL_INV_2VT     (1.0f / DL_2VT)
#define DL_INV_BLOCK   (1.0f / (float)OUTPUT_DMA_SIZE)

/* [5/4] Pade of tanh: tanh(z) = z N(z^2)/D(z^2); secant t = N/D. */
#define DL_PN0 945.0f
#define DL_PN1 105.0f
#define DL_PD1 420.0f
#define DL_PD2 15.0f
#define DL_ZMAX 3.5f

static inline float dl_clamp(const float z)
{
	return fminf(fmaxf(z, -DL_ZMAX), DL_ZMAX);
}

/* ------------------------------------------------------------------- init */
void DiodeLadder_init(DiodeLadder *dl)
{
	uint8_t i;

	dl->x0 = dl->x1 = dl->x2 = dl->x3 = 0.0f;
	dl->t0 = dl->t1 = dl->t2 = dl->t3 = 1.0f;
	dl->tin = dl->tout = dl->tota = 1.0f;
	dl->zo = 0.0f;
	dl->y0 = dl->y1 = dl->qu = dl->qv = 0.0f;
	for (i = 0u; i < 10u; i++)
		dl->z[i] = 0.0f;
	dl->rU = dl->rV = dl->rUPrev = dl->rVPrev = 0.0f;
	dl->tpLast = dl->sLast = dl->xinLast = dl->vhpLast = 0.0f;
	dl->vcaLast = dl->iAbcLast = 0.0f;
	dl->iLad = 0.0f;
	dl->iAbc = 0.0f;
	dl->netLo = 0;
	dl->netHi = 0;
	dl->netFrac = 0.0f;
}

/* ------------------------------------------------------------ per block
 * DiodeLadder_setNet: the Res-interpolated per-sample coefficients.
 * Ku0/Ku1 are u's instantaneous gains on tp_pre+ and s+: D plus each
 * one-pole's share of its input this sample (S092 P6 §5.3). */
void DiodeLadder_setNet(DiodeLadder *dl, const DiodeLadderNet *lo,
                        const DiodeLadderNet *hi, const float f)
{
	const float g = 1.0f - f;

	dl->netLo = lo;
	dl->netHi = hi;
	dl->netFrac = f;
	dl->alf0 = g * lo->alf[0] + f * hi->alf[0];
	dl->alf1 = g * lo->alf[1] + f * hi->alf[1];
	dl->bef0 = g * lo->bef[0] + f * hi->bef[0];
	dl->bef1 = g * lo->bef[1] + f * hi->bef[1];
	dl->cfu0 = g * lo->cf[0][0] + f * hi->cf[0][0];
	dl->cfu1 = g * lo->cf[0][1] + f * hi->cf[0][1];
	dl->cfv0 = g * lo->cf[1][0] + f * hi->cf[1][0];
	dl->cfv1 = g * lo->cf[1][1] + f * hi->cf[1][1];
	dl->gau0 = g * lo->ga[0][0] + f * hi->ga[0][0];
	dl->gau1 = g * lo->ga[0][1] + f * hi->ga[0][1];
	dl->gav0 = g * lo->ga[1][0] + f * hi->ga[1][0];
	dl->dv0 = g * lo->d[1][0] + f * hi->d[1][0];
	dl->ku0 = (g * lo->d[0][0] + f * hi->d[0][0])
	        + dl->cfu0 * dl->bef0 + dl->cfu1 * dl->bef1
	        + DL_ASYM_BE * dl->gau0;
	dl->ku1 = (g * lo->d[0][1] + f * hi->d[0][1]) + DL_ASYM_BE * dl->gau1;
}

/*
 * The slow network, once per block (S092 P6 §5.3): zero-order hold on the
 * block means of tp_pre and s, Res-interpolated as a blend of the two
 * neighbouring points' products (exact: the system is linear in phi/gam).
 */
static void dl_slowUpdate(DiodeLadder *dl, const float m0, const float m1)
{
	const DiodeLadderNet *lo = dl->netLo;
	const DiodeLadderNet *hi = dl->netHi;
	const float f = dl->netFrac;
	const float g = 1.0f - f;
	float zn[8];
	float ru = 0.0f, rv = 0.0f;
	uint8_t r, c;

	for (r = 0u; r < 8u; r++) {
		float a = lo->gam[r][0] * m0 + lo->gam[r][1] * m1;
		float b = hi->gam[r][0] * m0 + hi->gam[r][1] * m1;

		for (c = 0u; c < 8u; c++) {
			a += lo->phi[r][c] * dl->z[c];
			b += hi->phi[r][c] * dl->z[c];
		}
		zn[r] = g * a + f * b;
	}
	dl->z[8] = DL_ASYM_DECAY * dl->z[8]
	         + g * (lo->gam[8][0] * m0 + lo->gam[8][1] * m1)
	         + f * (hi->gam[8][0] * m0 + hi->gam[8][1] * m1);
	dl->z[9] = DL_ASYM_DECAY * dl->z[9]
	         + g * (lo->gam[9][0] * m0 + lo->gam[9][1] * m1)
	         + f * (hi->gam[9][0] * m0 + hi->gam[9][1] * m1);
	for (c = 0u; c < 8u; c++) {
		dl->z[c] = zn[c];
		ru += (g * lo->cs[0][c] + f * hi->cs[0][c]) * zn[c];
		rv += (g * lo->cs[1][c] + f * hi->cs[1][c]) * zn[c];
	}
	dl->rUPrev = dl->rU;
	dl->rVPrev = dl->rV;
	dl->rU = ru - dl->z[8];
	dl->rV = rv - dl->z[9];
}

/* ------------------------------------------------------------- option B
 * All six nonlinearities (rungs, input pair, Q21) as secant gains; the OTA
 * is the seventh secant in the batch. 3 VDIV per sample. */
void DiodeLadder_renderB(DiodeLadder *dl, int16_t *buf, const float sScale,
                         const float iLadEnd, const float iAbcEnd)
{
	float x0 = dl->x0, x1 = dl->x1, x2 = dl->x2, x3 = dl->x3;
	float t0 = dl->t0, t1 = dl->t1, t2 = dl->t2, t3 = dl->t3;
	float tin = dl->tin, tout = dl->tout, tota = dl->tota;
	float zo = dl->zo;
	float y0 = dl->y0, y1 = dl->y1, qu = dl->qu, qv = dl->qv;
	float tpL = dl->tpLast, sL = dl->sLast, xinL = dl->xinLast;
	float vhpL = dl->vhpLast, vcaL = dl->vcaLast, iaL = dl->iAbcLast;
	float i = dl->iLad, ia = dl->iAbc;
	float ru = dl->rUPrev, rv = dl->rVPrev;
	const float di = (iLadEnd - i) * DL_INV_BLOCK;
	const float dia = (iAbcEnd - ia) * DL_INV_BLOCK;
	const float dru = (dl->rU - ru) * DL_INV_BLOCK;
	const float drv = (dl->rV - rv) * DL_INV_BLOCK;
	const float alf0 = dl->alf0, alf1 = dl->alf1, bef0 = dl->bef0, bef1 = dl->bef1;
	const float cfu0 = dl->cfu0, cfu1 = dl->cfu1, cfv0 = dl->cfv0, cfv1 = dl->cfv1;
	const float gau0 = dl->gau0, gau1 = dl->gau1, gav0 = dl->gav0;
	const float ku0 = dl->ku0, ku1 = dl->ku1, dv0 = dl->dv0;
	float accTp = 0.0f, accS = 0.0f;
	uint8_t j;

	for (j = 0u; j < OUTPUT_DMA_SIZE; j++) {
		i += di;
		ia += dia;
		ru += dru;
		rv += drv;
		const float s = (float)buf[j] * sScale;
		const float w0 = DL_K0 * i, w1 = DL_K1 * i;

		/* right-hand side: the trapezoidal half step from the last sample */
		const float d01 = x0 - x1, d12 = x1 - x2, d23 = x2 - x3;
		const float a0 = t0 * d01, a1 = t1 * d12, a2 = t2 * d23, a3 = t3 * x3;
		const float r0 = x0 - w0 * (a0 + tin * xinL);
		const float r1 = x1 + w1 * (a0 - a1);
		const float r2 = x2 + w1 * (a1 - a2);
		const float r3 = x3 + w1 * (a2 - a3) + DL_KL * (DL_KG * zo - vhpL);

		/* tridiagonal: continuants, one reciprocal (VDIV 1) */
		const float e0 = w0 * t0, e1 = w1 * t0, e2 = w1 * t1, e3 = w1 * t2;
		const float th1 = 1.0f + e0;
		const float th2 = (1.0f + e1 + e2) * th1 - e1 * e0;
		const float th3 = (1.0f + e2 + e3) * th2 - e2 * e2 * th1;
		const float th4 = (1.0f + e3 + w1 * t3 + DL_D3X) * th3 - e3 * e3 * th2;
		const float p12 = th1 * th2, p123 = p12 * th3;
		const float inv = 1.0f / (p123 * th4);
		const float i123 = inv * th4;
		const float i12 = i123 * th3;
		const float ip0 = i12 * th2;              /* 1/th1 */
		const float ip1 = i12 * th1 * th1;        /* th1/th2 */
		const float ip2 = i123 * p12 * th2;       /* th2/th3 */
		const float ip3 = inv * p123 * th3;       /* th3/th4 */

		/* forward pass for the right-hand side and the x_in column */
		const float m1 = e1 * ip0, m2 = e2 * ip1, m3 = e3 * ip2;
		const float yy1 = r1 + m1 * r0, yy2 = r2 + m2 * yy1, yy3 = r3 + m3 * yy2;
		const float q0 = -w0 * tin, q1 = m1 * q0, q2 = m2 * q1, q3 = m3 * q2;
		const float pr3 = yy3 * ip3, pq3 = q3 * ip3;

		/* close the loop through the network (VDIV 2) */
		const float yk0 = alf0 * y0 + bef0 * tpL;
		const float yk1 = alf1 * y1 + bef1 * tpL;
		const float quk = DL_ASYM_AL * qu + DL_ASYM_BE * (gau0 * tpL + gau1 * sL);
		const float base = ru + cfu0 * yk0 + cfu1 * yk1 + quk + ku1 * s;
		const float at = DL_KA * tout, bt = DL_KB * tout * zo;
		const float xin = (base + ku0 * (at * pr3 + bt)) / (DL_2VT - ku0 * at * pq3);

		/* back-substitution */
		const float n3 = pr3 + pq3 * xin;
		const float n2 = (yy2 + q2 * xin + e3 * n3) * ip2;
		const float n1 = (yy1 + q1 * xin + e2 * n2) * ip1;
		const float n0 = (r0 + q0 * xin + e0 * n1) * ip0;
		const float vhp = (DL_2VT * n3 - zo) * DL_KG;
		zo += DL_TWO_G * vhp;

		/* seven secants, one shared reciprocal (VDIV 3) */
		const float zA = dl_clamp(n0 - n1), zB = dl_clamp(n1 - n2);
		const float zC = dl_clamp(n2 - n3), zD = dl_clamp(n3);
		const float zE = dl_clamp(xin), zF = dl_clamp(vhp * DL_INV_2VT);
		const float zG = dl_clamp(vcaL * DL_INV_2VT);
		const float sA = zA * zA, sB = zB * zB, sC = zC * zC, sD = zD * zD;
		const float sE = zE * zE, sF = zF * zF, sG = zG * zG;
		const float nA = DL_PN0 + sA * (DL_PN1 + sA), dA = DL_PN0 + sA * (DL_PD1 + DL_PD2 * sA);
		const float nB = DL_PN0 + sB * (DL_PN1 + sB), dB = DL_PN0 + sB * (DL_PD1 + DL_PD2 * sB);
		const float nC = DL_PN0 + sC * (DL_PN1 + sC), dC = DL_PN0 + sC * (DL_PD1 + DL_PD2 * sC);
		const float nD = DL_PN0 + sD * (DL_PN1 + sD), dD = DL_PN0 + sD * (DL_PD1 + DL_PD2 * sD);
		const float nE = DL_PN0 + sE * (DL_PN1 + sE), dE = DL_PN0 + sE * (DL_PD1 + DL_PD2 * sE);
		const float nF = DL_PN0 + sF * (DL_PN1 + sF), dF = DL_PN0 + sF * (DL_PD1 + DL_PD2 * sF);
		const float nG = DL_PN0 + sG * (DL_PN1 + sG), dG = DL_PN0 + sG * (DL_PD1 + DL_PD2 * sG);
		const float pAB = dA * dB, pABC = pAB * dC, pABCD = pABC * dD;
		const float pE = pABCD * dE, pF = pE * dF;
		float r = 1.0f / (pF * dG);
		const float iG = r * pF;       r *= dG;
		const float iF = r * pE;       r *= dF;
		const float iE = r * pABCD;    r *= dE;
		const float iD = r * pABC;     r *= dD;
		const float iC = r * pAB;      r *= dC;
		const float iB = r * dA;
		const float iA = r * dB;
		t0 = nA * iA; t1 = nB * iB; t2 = nC * iC; t3 = nD * iD;
		tin = nE * iE; tout = nF * iF;
		const float tG = nG * iG;

		/* Q21 output, network per-sample parts, OTA (one sample late) */
		const float tp = -DL_A_OUT * zF * tout;
		y0 = yk0 + bef0 * tp;
		y1 = yk1 + bef1 * tp;
		qu = quk + DL_ASYM_BE * (gau0 * tp + gau1 * s);
		qv = DL_ASYM_AL * qv + DL_ASYM_BE * gav0 * (tpL + tp);
		const float vca = rv + cfv0 * y0 + cfv1 * y1 + qv + dv0 * tp;
		const float out = DL_R_OTA * iaL * zG * tG;
		buf[j] = (int16_t)__SSAT((int32_t)(out * DL_OUT_SCALE), 16);

		accTp += tp;
		accS += s;
		x0 = n0; x1 = n1; x2 = n2; x3 = n3;
		tpL = tp; sL = s; xinL = xin; vhpL = vhp; vcaL = vca; iaL = ia;
		tota = tG;
	}
	dl->x0 = x0; dl->x1 = x1; dl->x2 = x2; dl->x3 = x3;
	dl->t0 = t0; dl->t1 = t1; dl->t2 = t2; dl->t3 = t3;
	dl->tin = tin; dl->tout = tout; dl->tota = tota;
	dl->zo = zo;
	dl->y0 = y0; dl->y1 = y1; dl->qu = qu; dl->qv = qv;
	dl->tpLast = tpL; dl->sLast = sL; dl->xinLast = xinL;
	dl->vhpLast = vhpL; dl->vcaLast = vcaL; dl->iAbcLast = iaL;
	dl->iLad = i; dl->iAbc = ia;
	dl_slowUpdate(dl, accTp * DL_INV_BLOCK, accS * DL_INV_BLOCK);
}

/* ------------------------------------------------------------- option A
 * Linear rungs (t = 1): the tridiagonal depends only on the ladder current,
 * so the next sample's pivot reciprocals share the secant batch. 2 VDIV per
 * sample. */
static inline void dl_pivotsA(const float i, float *th)
{
	const float w0 = DL_K0 * i, w1 = DL_K1 * i;

	th[0] = 1.0f + w0;
	th[1] = (1.0f + 2.0f * w1) * th[0] - w1 * w0;
	th[2] = (1.0f + 2.0f * w1) * th[1] - w1 * w1 * th[0];
	th[3] = (1.0f + 2.0f * w1 + DL_D3X) * th[2] - w1 * w1 * th[1];
}

void DiodeLadder_renderA(DiodeLadder *dl, int16_t *buf, const float sScale,
                         const float iLadEnd, const float iAbcEnd)
{
	float x0 = dl->x0, x1 = dl->x1, x2 = dl->x2, x3 = dl->x3;
	float tin = dl->tin, tout = dl->tout, tota = dl->tota;
	float zo = dl->zo;
	float y0 = dl->y0, y1 = dl->y1, qu = dl->qu, qv = dl->qv;
	float tpL = dl->tpLast, sL = dl->sLast, xinL = dl->xinLast;
	float vhpL = dl->vhpLast, vcaL = dl->vcaLast, iaL = dl->iAbcLast;
	float i = dl->iLad, ia = dl->iAbc;
	float ru = dl->rUPrev, rv = dl->rVPrev;
	const float di = (iLadEnd - i) * DL_INV_BLOCK;
	const float dia = (iAbcEnd - ia) * DL_INV_BLOCK;
	const float dru = (dl->rU - ru) * DL_INV_BLOCK;
	const float drv = (dl->rV - rv) * DL_INV_BLOCK;
	const float alf0 = dl->alf0, alf1 = dl->alf1, bef0 = dl->bef0, bef1 = dl->bef1;
	const float cfu0 = dl->cfu0, cfu1 = dl->cfu1, cfv0 = dl->cfv0, cfv1 = dl->cfv1;
	const float gau0 = dl->gau0, gau1 = dl->gau1, gav0 = dl->gav0;
	const float ku0 = dl->ku0, ku1 = dl->ku1, dv0 = dl->dv0;
	float accTp = 0.0f, accS = 0.0f;
	float ip0, ip1, ip2, ip3;
	uint8_t j;

	{	/* the first sample's pivots (control rate: once per block) */
		float th[4];

		dl_pivotsA(i + di, th);
		ip0 = 1.0f / th[0];
		ip1 = th[0] / th[1];
		ip2 = th[1] / th[2];
		ip3 = th[2] / th[3];
	}
	for (j = 0u; j < OUTPUT_DMA_SIZE; j++) {
		i += di;
		ia += dia;
		ru += dru;
		rv += drv;
		const float s = (float)buf[j] * sScale;
		const float w0 = DL_K0 * i, w1 = DL_K1 * i;

		const float d01 = x0 - x1, d12 = x1 - x2, d23 = x2 - x3;
		const float r0 = x0 - w0 * (d01 + tin * xinL);
		const float r1 = x1 + w1 * (d01 - d12);
		const float r2 = x2 + w1 * (d12 - d23);
		const float r3 = x3 + w1 * (d23 - x3) + DL_KL * (DL_KG * zo - vhpL);

		const float m1 = w1 * ip0, m2 = w1 * ip1, m3 = w1 * ip2;
		const float yy1 = r1 + m1 * r0, yy2 = r2 + m2 * yy1, yy3 = r3 + m3 * yy2;
		const float q0 = -w0 * tin, q1 = m1 * q0, q2 = m2 * q1, q3 = m3 * q2;
		const float pr3 = yy3 * ip3, pq3 = q3 * ip3;

		const float yk0 = alf0 * y0 + bef0 * tpL;
		const float yk1 = alf1 * y1 + bef1 * tpL;
		const float quk = DL_ASYM_AL * qu + DL_ASYM_BE * (gau0 * tpL + gau1 * sL);
		const float base = ru + cfu0 * yk0 + cfu1 * yk1 + quk + ku1 * s;
		const float at = DL_KA * tout, bt = DL_KB * tout * zo;
		const float xin = (base + ku0 * (at * pr3 + bt)) / (DL_2VT - ku0 * at * pq3);

		const float n3 = pr3 + pq3 * xin;
		const float n2 = (yy2 + q2 * xin + w1 * n3) * ip2;
		const float n1 = (yy1 + q1 * xin + w1 * n2) * ip1;
		const float n0 = (r0 + q0 * xin + w0 * n1) * ip0;
		const float vhp = (DL_2VT * n3 - zo) * DL_KG;
		zo += DL_TWO_G * vhp;

		/* three secants + the next sample's four continuants: one VDIV */
		float th[4];
		dl_pivotsA(i + di, th);
		const float zE = dl_clamp(xin), zF = dl_clamp(vhp * DL_INV_2VT);
		const float zG = dl_clamp(vcaL * DL_INV_2VT);
		const float sE = zE * zE, sF = zF * zF, sG = zG * zG;
		const float nE = DL_PN0 + sE * (DL_PN1 + sE), dE = DL_PN0 + sE * (DL_PD1 + DL_PD2 * sE);
		const float nF = DL_PN0 + sF * (DL_PN1 + sF), dF = DL_PN0 + sF * (DL_PD1 + DL_PD2 * sF);
		const float nG = DL_PN0 + sG * (DL_PN1 + sG), dG = DL_PN0 + sG * (DL_PD1 + DL_PD2 * sG);
		const float p1 = th[0] * th[1], p2 = p1 * th[2], p3 = p2 * th[3];
		const float p4 = p3 * dE, p5 = p4 * dF;
		float r = 1.0f / (p5 * dG);
		const float iG = r * p5;       r *= dG;
		const float iF = r * p4;       r *= dF;
		const float iE = r * p3;       r *= dE;   /* r = 1/(th0 th1 th2 th3) */
		const float i4 = r * p2;       r *= th[3];
		const float i3 = r * p1;       r *= th[2];
		const float i2 = r * th[0];
		const float i1 = r * th[1];
		ip0 = i1;
		ip1 = th[0] * i2;
		ip2 = th[1] * i3;
		ip3 = th[2] * i4;
		tin = nE * iE;
		tout = nF * iF;
		const float tG = nG * iG;

		const float tp = -DL_A_OUT * zF * tout;
		y0 = yk0 + bef0 * tp;
		y1 = yk1 + bef1 * tp;
		qu = quk + DL_ASYM_BE * (gau0 * tp + gau1 * s);
		qv = DL_ASYM_AL * qv + DL_ASYM_BE * gav0 * (tpL + tp);
		const float vca = rv + cfv0 * y0 + cfv1 * y1 + qv + dv0 * tp;
		const float out = DL_R_OTA * iaL * zG * tG;
		buf[j] = (int16_t)__SSAT((int32_t)(out * DL_OUT_SCALE), 16);

		accTp += tp;
		accS += s;
		x0 = n0; x1 = n1; x2 = n2; x3 = n3;
		tpL = tp; sL = s; xinL = xin; vhpL = vhp; vcaL = vca; iaL = ia;
		tota = tG;
	}
	dl->x0 = x0; dl->x1 = x1; dl->x2 = x2; dl->x3 = x3;
	dl->t0 = dl->t1 = dl->t2 = dl->t3 = 1.0f;
	dl->tin = tin; dl->tout = tout; dl->tota = tota;
	dl->zo = zo;
	dl->y0 = y0; dl->y1 = y1; dl->qu = qu; dl->qv = qv;
	dl->tpLast = tpL; dl->sLast = sL; dl->xinLast = xinL;
	dl->vhpLast = vhpL; dl->vcaLast = vcaL; dl->iAbcLast = iaL;
	dl->iLad = i; dl->iAbc = ia;
	dl_slowUpdate(dl, accTp * DL_INV_BLOCK, accS * DL_INV_BLOCK);
}
