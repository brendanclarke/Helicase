/*
 * Experimental ladder kernels for S093 P1 (overhead reductions and drive).
 *
 * What:       one per-sample step with compile-time switches, so each
 *             reduction can be costed (ARM, fpseq-style loop counts) and
 *             heard (host, against the golden RK4 model) on its own:
 *               MODEL      0 = A (linear rungs), 1 = B (all rungs), 2 = A1
 *                          (rung 0 nonlinear, its secant one sample later),
 *                          3 = B2 (rungs 0 and 1 nonlinear, 2 and 3 linear;
 *                          per-sample continuants as B)
 *               PADE_RUNG  54 / 32: [5/4] or [3/2] Pade for the rung secants
 *               PADE_IO    54 / 32: the same for the input pair and Q21
 *               RUNG_CLAMP 1 / 0: clamp the rung arguments
 *               XIN_TAIL   1 / 0: input-pair secant ~ 1/|z| beyond the clamp
 *               GAU1       1 / 0: the s-input asymptote term
 *               SPLIT      1 / 0: VCA side (v_vca + OTA) in a second pass
 *               UNROLL     1 / 2: samples per loop iteration
 *               RUNG_HALF  1 / 0: B rung secants refreshed every 2nd sample
 * Why:        S093_P1_ALKALINE_OVERHEAD_REDUCTIONS.md. The firmware kernel
 *             (Core/DSPAudio/DiodeLadder.c) is not changed until a choice is
 *             made.
 * Inputs:     DiodeLadderNet (DiodeLadder.h); the per-block coefficient set.
 * Outputs:    the OTA output in buf, as DiodeLadder_render*.
 * Accessors:  variants/dlv_host.c (host), variants/run_variants.py (both).
 */
#include <stdint.h>
#include <math.h>
#include "DiodeLadder.h"
#include "dlv.h"

#ifndef MODEL
#define MODEL 1
#endif
#ifndef PADE_RUNG
#define PADE_RUNG 54
#endif
#ifndef PADE_IO
#define PADE_IO 54
#endif
#ifndef RUNG_CLAMP
#define RUNG_CLAMP 1
#endif
#ifndef XIN_TAIL
#define XIN_TAIL 0
#endif
#ifndef GAU1
#define GAU1 1
#endif
#ifndef SPLIT
#define SPLIT 0
#endif
#ifndef UNROLL
#define UNROLL 1
#endif
#ifndef RUNG_HALF
#define RUNG_HALF 0
#endif
#ifndef NET1
#define NET1 0          /* 1: no per-sample fast modes; asymptotes 50 / 120 Hz */
#endif
#ifndef Q21_LIN
#define Q21_LIN 0       /* 1: the Q21 stage linear (no tanh, no secant) */
#endif
#ifndef RRAMP
#define RRAMP 1         /* 0: the slow network outputs held per block */
#endif
#ifndef OTA_P32
#define OTA_P32 0       /* 1: [3/2] Pade for the OTA tanh */
#endif

#define K0          6090.59326f
#define K1          3322.14185f
#define KG          0.995162427f
#define TWO_G       0.00972212479f
#define KL          0.142460123f
#define D3X         0.00732955895f
#define KA          (-5.33020973f)
#define KB          103.098831f
#define A_OUT       5.35612011f
#define R_OTA       52525.9531f
#define AS_AL       0.999288023f
#define AS_BE       1.13317765e-05f
#define AS_DECAY    0.977465749f
#if NET1
#include "dlv_n1.h"
#define ALU N1_AL_U
#define BEU N1_BE_U
#define ALV N1_AL_V
#define BEV N1_BE_V
#else
#define ALU AS_AL
#define BEU AS_BE
#define ALV AS_AL
#define BEV AS_BE
#endif
#define TWO_VT      (2.0f * DL_VT)
#define INV_2VT     (1.0f / TWO_VT)
#define INV_BLK     (1.0f / 32.0f)
#define TP_SCALE    (32767.0f / 5.6f)   /* SPLIT: tp_pre carried as int16 */

#define PN54(s) (945.0f + (s) * (105.0f + (s)))
#define PD54(s) (945.0f + (s) * (420.0f + 15.0f * (s)))
#define PN32(s) (27.0f + (s))
#define PD32(s) (27.0f + 9.0f * (s))
#if PADE_RUNG == 54
#define RPN PN54
#define RPD PD54
#define RZM 3.5f
#else
#define RPN PN32
#define RPD PD32
#define RZM 3.0f
#endif
#if PADE_IO == 54
#define IPN PN54
#define IPD PD54
#define IZM 3.5f
#else
#define IPN PN32
#define IPD PD32
#define IZM 3.0f
#endif

static inline float clampf(const float z, const float m)
{
	return fminf(fmaxf(z, -m), m);
}

#ifdef __arm__
#include "stm32f4xx.h"
#endif
#define SSAT16(v) __SSAT((int32_t)(v), 16)

typedef struct {
	float x0, x1, x2, x3, t0, t1, t2, t3, tin, tout, tota, zo;
	float y0, y1, qu, qv, tpL, sL, xinL, vhpL, vcaL, iaL, i, ia, ru, rv;
	float tl0;
	float ip0, ip1, ip2, ip3;         /* A / A1: this sample's pivot reciprocals */
	float accTp, accS;
	/* per block */
	float di, dia, dru, drv;
	float alf0, alf1, bef0, bef1, cfu0, cfu1, cfv0, cfv1, gau0, gau1, gav0;
	float ku0, ku1, dv0, sScale;
} Loc;

#if MODEL != 1
static inline __attribute__((always_inline)) void pivots(const float i, const float t0, float *th)
{
	const float w0 = K0 * i, w1 = K1 * i;
	const float e0 = w0 * t0, e1 = w1 * t0;

	th[0] = 1.0f + e0;
	th[1] = (1.0f + e1 + w1) * th[0] - e1 * e0;
	th[2] = (1.0f + 2.0f * w1) * th[1] - w1 * w1 * th[0];
	th[3] = (1.0f + 2.0f * w1 + D3X) * th[2] - w1 * w1 * th[1];
}
#endif

static inline __attribute__((always_inline)) void step(Loc *L, int16_t *bj, const int odd)
{
	(void)odd;
	L->i += L->di;
	L->ia += L->dia;
#if RRAMP
	L->ru += L->dru;
	L->rv += L->drv;
#endif
	const float s = (float)(*bj) * L->sScale;
	const float i = L->i;
	const float w0 = K0 * i, w1 = K1 * i;
	const float x0 = L->x0, x1 = L->x1, x2 = L->x2, x3 = L->x3;
	const float d01 = x0 - x1, d12 = x1 - x2, d23 = x2 - x3;
#if MODEL == 1
	const float t0 = L->t0, t1 = L->t1, t2 = L->t2, t3 = L->t3;
#elif MODEL == 3
	const float t0 = L->t0, t1 = L->t1, t2 = 1.0f, t3 = 1.0f;
#elif MODEL == 2
	const float t0 = L->tl0, t1 = 1.0f, t2 = 1.0f, t3 = 1.0f;
#else
	const float t0 = 1.0f, t1 = 1.0f, t2 = 1.0f, t3 = 1.0f;
#endif
	const float a0 = t0 * d01, a1 = t1 * d12, a2 = t2 * d23, a3 = t3 * x3;
	const float r0 = x0 - w0 * (a0 + L->tin * L->xinL);
	const float r1 = x1 + w1 * (a0 - a1);
	const float r2 = x2 + w1 * (a1 - a2);
	const float r3 = x3 + w1 * (a2 - a3) + KL * (KG * L->zo - L->vhpL);
	const float e0 = w0 * t0, e1 = w1 * t0, e2 = w1 * t1, e3 = w1 * t2;
#if MODEL == 1 || MODEL == 3
	/* B / B2: this sample's continuants, own reciprocal */
	const float th1 = 1.0f + e0;
	const float th2 = (1.0f + e1 + e2) * th1 - e1 * e0;
	const float th3 = (1.0f + e2 + e3) * th2 - e2 * e2 * th1;
	const float th4 = (1.0f + e3 + w1 * t3 + D3X) * th3 - e3 * e3 * th2;
	const float p12 = th1 * th2, p123 = p12 * th3;
	const float inv = 1.0f / (p123 * th4);
	const float i123 = inv * th4, i12 = i123 * th3;
	const float ip0 = i12 * th2, ip1 = i12 * th1 * th1;
	const float ip2 = i123 * p12 * th2, ip3 = inv * p123 * th3;
#else
	const float ip0 = L->ip0, ip1 = L->ip1, ip2 = L->ip2, ip3 = L->ip3;
#endif
	const float m1 = e1 * ip0, m2 = e2 * ip1, m3 = e3 * ip2;
	const float y1 = r1 + m1 * r0, y2 = r2 + m2 * y1, y3 = r3 + m3 * y2;
	const float q0 = -w0 * L->tin, q1 = m1 * q0, q2 = m2 * q1, q3 = m3 * q2;
	const float pr3 = y3 * ip3, pq3 = q3 * ip3;

#if GAU1
	const float quk = ALU * L->qu + BEU * (L->gau0 * L->tpL + L->gau1 * L->sL);
#else
	const float quk = ALU * L->qu + BEU * L->gau0 * L->tpL;
#endif
#if NET1
	const float base = L->ru + quk + L->ku1 * s;
#else
	const float yk0 = L->alf0 * L->y0 + L->bef0 * L->tpL;
	const float yk1 = L->alf1 * L->y1 + L->bef1 * L->tpL;
	const float base = L->ru + L->cfu0 * yk0 + L->cfu1 * yk1 + quk + L->ku1 * s;
#endif
#if Q21_LIN
	const float toutv = 1.0f;
#else
	const float toutv = L->tout;
#endif
	const float at = KA * toutv, bt = KB * toutv * L->zo;
	const float xin = (base + L->ku0 * (at * pr3 + bt)) / (TWO_VT - L->ku0 * at * pq3);

	const float n3 = pr3 + pq3 * xin;
	const float n2 = (y2 + q2 * xin + e3 * n3) * ip2;
	const float n1 = (y1 + q1 * xin + e2 * n2) * ip1;
	const float n0 = (r0 + q0 * xin + e0 * n1) * ip0;
	const float vhp = (TWO_VT * n3 - L->zo) * KG;
	L->zo += TWO_G * vhp;

	/* ---- secants, one shared reciprocal ---- */
	const float zE = clampf(xin, IZM);
#if Q21_LIN
	const float zF = vhp * INV_2VT;
	const float nF = 1.0f;
#else
	const float zF = clampf(vhp * INV_2VT, IZM);
	const float sF = zF * zF;
	const float nF = IPN(sF);
#endif
	const float sE = zE * zE;
	const float nE = IPN(sE);
#if XIN_TAIL
	const float dE = IPD(sE) * (fmaxf(fabsf(xin), IZM) * (1.0f / IZM));
#else
	const float dE = IPD(sE);
#endif
#if Q21_LIN
	const float dF = 1.0f;
#else
	const float dF = IPD(sF);
#endif
	float prod = dE * dF;
#if !SPLIT
#if OTA_P32
	const float zG = clampf(L->vcaL * INV_2VT, 3.0f);
	const float sG = zG * zG;
	const float nG = PN32(sG), dG = PD32(sG);
#else
	const float zG = clampf(L->vcaL * INV_2VT, 3.5f);
	const float sG = zG * zG;
	const float nG = PN54(sG), dG = PD54(sG);
#endif
	const float pEFG = prod * dG;
	prod = pEFG;
#endif
#if MODEL == 1
	const int doR = RUNG_HALF ? !odd : 1;
	float zA = n0 - n1, zB = n1 - n2, zC = n2 - n3, zD = n3;
#if RUNG_CLAMP
	zA = clampf(zA, RZM); zB = clampf(zB, RZM); zC = clampf(zC, RZM); zD = clampf(zD, RZM);
#endif
	const float sA = zA * zA, sB = zB * zB, sC = zC * zC, sD = zD * zD;
	const float nA = RPN(sA), dA = RPD(sA), nB = RPN(sB), dB = RPD(sB);
	const float nC = RPN(sC), dC = RPD(sC), nD = RPN(sD), dD = RPD(sD);
	if (doR) {
		const float pAB = dA * dB, pABC = pAB * dC, pABCD = pABC * dD;
		const float pAll = pABCD * prod;
		float r = 1.0f / pAll;
		const float iP = r * pABCD;    r *= prod;    /* r = 1/(dA dB dC dD) */
		L->t3 = nD * r * pABC;         r *= dD;
		L->t2 = nC * r * pAB;          r *= dC;
		L->t1 = nB * r * dA;
		L->t0 = nA * r * dB;
		/* iP = 1/prod: the E, F (G) reciprocals from it */
#if !SPLIT
		const float iG = iP * (dE * dF);
		const float iEF = iP * dG;
#else
		const float iEF = iP;
#endif
		L->tin = nE * iEF * dF;
#if !Q21_LIN
		L->tout = nF * iEF * dE;
#endif
#if !SPLIT
		L->tota = nG * iG;
#endif
	} else {
		float r = 1.0f / prod;
#if !SPLIT
		const float iG = r * (dE * dF);
		r *= dG;
		L->tota = nG * iG;
#endif
		L->tin = nE * r * dF;
#if !Q21_LIN
		L->tout = nF * r * dE;
#endif
	}
#elif MODEL == 3
	const int doR = RUNG_HALF ? !odd : 1;
	const float zA = n0 - n1, zB = n1 - n2;
	const float sA = zA * zA, sB = zB * zB;
	const float nA = RPN(sA), dA = RPD(sA), nB = RPN(sB), dB = RPD(sB);
	if (doR) {
		const float pAB = dA * dB;
		float r = 1.0f / (pAB * prod);
		const float iP = r * pAB;      r *= prod;    /* r = 1/(dA dB) */
		L->t1 = nB * r * dA;
		L->t0 = nA * r * dB;
#if !SPLIT
		const float iG = iP * (dE * dF);
		const float iEF = iP * dG;
		L->tota = nG * iG;
#else
		const float iEF = iP;
#endif
		L->tin = nE * iEF * dF;
#if !Q21_LIN
		L->tout = nF * iEF * dE;
#endif
	} else {
		float r = 1.0f / prod;
#if !SPLIT
		const float iG = r * (dE * dF);
		r *= dG;
		L->tota = nG * iG;
#endif
		L->tin = nE * r * dF;
#if !Q21_LIN
		L->tout = nF * r * dE;
#endif
	}
#else
	/* A / A1: the next sample's continuants share the batch */
	float th[4];
#if MODEL == 2
	const float zA = clampf(n0 - n1, RZM);
	const float sA = zA * zA;
	const float nA = RPN(sA), dA = RPD(sA);
	pivots(L->i + L->di, L->t0, th);       /* next sample uses the last t0 */
	const float pRest = prod;
	prod *= dA;
#else
	pivots(L->i + L->di, 1.0f, th);
#endif
	{
		const float p1 = th[0] * th[1], p2 = p1 * th[2], p3 = p2 * th[3];
		float r = 1.0f / (p3 * prod);
		const float iP = r * p3;        r *= prod;    /* r = 1/(th0..th3) */
		const float i4 = r * p2;        r *= th[3];
		const float i3 = r * p1;        r *= th[2];
		L->ip0 = r * th[1];
		L->ip1 = th[0] * (r * th[0]);
		L->ip2 = th[1] * i3;
		L->ip3 = th[2] * i4;
#if MODEL == 2
		L->tl0 = L->t0;
		L->t0 = nA * iP * pRest;
		const float iEFG = iP * dA;
#else
		const float iEFG = iP;
#endif
#if !SPLIT
		L->tota = nG * iEFG * (dE * dF);
		L->tin = nE * iEFG * dF * dG;
#if !Q21_LIN
		L->tout = nF * iEFG * dE * dG;
#endif
#else
		L->tin = nE * iEFG * dF;
#if !Q21_LIN
		L->tout = nF * iEFG * dE;
#endif
#endif
	}
#endif

#if Q21_LIN
	const float tp = -A_OUT * zF;
#else
	const float tp = -A_OUT * zF * L->tout;
#endif
#if !NET1
	L->y0 = yk0 + L->bef0 * tp;
	L->y1 = yk1 + L->bef1 * tp;
#endif
#if GAU1
	L->qu = quk + BEU * (L->gau0 * tp + L->gau1 * s);
#else
	L->qu = quk + BEU * L->gau0 * tp;
#endif
#if !SPLIT
	L->qv = ALV * L->qv + BEV * L->gav0 * (L->tpL + tp);
#if NET1
	const float vca = L->rv + L->qv + L->dv0 * tp;
#else
	const float vca = L->rv + L->cfv0 * L->y0 + L->cfv1 * L->y1 + L->qv + L->dv0 * tp;
#endif
	const float out = R_OTA * L->iaL * zG * L->tota;
	*bj = (int16_t)SSAT16(out * VOUT_SCALE);
	L->vcaL = vca;
	L->iaL = L->ia;
#else
	*bj = (int16_t)SSAT16(tp * TP_SCALE);
#endif
	L->accTp += tp;
	L->accS += s;
	L->x0 = n0; L->x1 = n1; L->x2 = n2; L->x3 = n3;
	L->tpL = tp; L->sL = s; L->xinL = xin; L->vhpL = vhp;
}

#if SPLIT
/* the VCA side: v_vca from tp_pre (its own fast one-poles and q_v), OTA */
static void __attribute__((noinline)) vcaPass(DLV *dl, int16_t *buf, Loc *L, const float ia0,
                                               const float rv0)
{
	float yv0 = dl->yv0, yv1 = dl->yv1, qv = dl->qv, tpL = dl->tpLast2;
	float ia = ia0, rv = rv0;
	const float alf0 = L->alf0, alf1 = L->alf1, bef0 = L->bef0, bef1 = L->bef1;
	const float cfv0 = L->cfv0, cfv1 = L->cfv1, gav0 = L->gav0, dv0 = L->dv0;
	const float dia = L->dia, drv = L->drv;
	uint8_t j;

	for (j = 0u; j < 32u; j++) {
		ia += dia;
		rv += drv;
		const float tp = (float)buf[j] * (1.0f / TP_SCALE);
		qv = ALV * qv + BEV * gav0 * (tpL + tp);
#if NET1
		const float v = rv + qv + dv0 * tp;
		(void)alf0; (void)alf1; (void)bef0; (void)bef1; (void)cfv0; (void)cfv1;
#else
		yv0 = alf0 * yv0 + bef0 * (tpL + tp);
		yv1 = alf1 * yv1 + bef1 * (tpL + tp);
		const float v = rv + cfv0 * yv0 + cfv1 * yv1 + qv + dv0 * tp;
#endif
		const float z = clampf(v * INV_2VT, 3.5f);
		const float sz = z * z;
		const float out = R_OTA * ia * z * PN54(sz) / PD54(sz);
		buf[j] = (int16_t)SSAT16(out * VOUT_SCALE);
		tpL = tp;
	}
	dl->yv0 = yv0; dl->yv1 = yv1; dl->qv = qv; dl->tpLast2 = tpL;
}
#endif

void dlv_render(DLV *dl, int16_t *buf, const float sScale, const float iLadEnd,
                const float iAbcEnd)
{
	Loc L;
	uint8_t j;

	L.x0 = dl->x0; L.x1 = dl->x1; L.x2 = dl->x2; L.x3 = dl->x3;
	L.t0 = dl->t0; L.t1 = dl->t1; L.t2 = dl->t2; L.t3 = dl->t3;
	L.tin = dl->tin; L.tout = dl->tout; L.tota = dl->tota; L.zo = dl->zo;
	L.y0 = dl->y0; L.y1 = dl->y1; L.qu = dl->qu; L.qv = dl->qv;
	L.tpL = dl->tpLast; L.sL = dl->sLast; L.xinL = dl->xinLast; L.vhpL = dl->vhpLast;
	L.vcaL = dl->vcaLast; L.iaL = dl->iAbcLast; L.i = dl->iLad; L.ia = dl->iAbc;
#if RRAMP
	L.ru = dl->rUPrev; L.rv = dl->rVPrev;
#else
	L.ru = dl->rU; L.rv = dl->rV;
#endif
	L.tl0 = dl->tl0;
	L.di = (iLadEnd - L.i) * INV_BLK;
	L.dia = (iAbcEnd - L.ia) * INV_BLK;
	L.dru = (dl->rU - L.ru) * INV_BLK;
	L.drv = (dl->rV - L.rv) * INV_BLK;
	L.alf0 = dl->alf0; L.alf1 = dl->alf1; L.bef0 = dl->bef0; L.bef1 = dl->bef1;
	L.cfu0 = dl->cfu0; L.cfu1 = dl->cfu1; L.cfv0 = dl->cfv0; L.cfv1 = dl->cfv1;
	L.gau0 = dl->gau0; L.gau1 = dl->gau1; L.gav0 = dl->gav0;
	L.ku0 = dl->ku0; L.ku1 = dl->ku1; L.dv0 = dl->dv0; L.sScale = sScale;
	L.accTp = 0.0f; L.accS = 0.0f;
#if MODEL != 1
	{	/* the first sample's pivots (once per block) */
		float th[4];

		pivots(L.i + L.di, MODEL == 2 ? L.tl0 : 1.0f, th);
		L.ip0 = 1.0f / th[0];
		L.ip1 = th[0] / th[1];
		L.ip2 = th[1] / th[2];
		L.ip3 = th[2] / th[3];
	}
#endif
#if SPLIT
	const float ia0 = L.ia, rv0 = L.rv;
#endif
#if UNROLL == 2
	for (j = 0u; j < 32u; j += 2u) {
		step(&L, &buf[j], 0);
		step(&L, &buf[j + 1u], 1);
	}
#else
	for (j = 0u; j < 32u; j++)
		step(&L, &buf[j], 0);
#endif
#if SPLIT
	vcaPass(dl, buf, &L, ia0, rv0);
#endif
	dl->x0 = L.x0; dl->x1 = L.x1; dl->x2 = L.x2; dl->x3 = L.x3;
	dl->t0 = L.t0; dl->t1 = L.t1; dl->t2 = L.t2; dl->t3 = L.t3;
	dl->tin = L.tin; dl->tout = L.tout; dl->tota = L.tota; dl->zo = L.zo;
	dl->y0 = L.y0; dl->y1 = L.y1; dl->qu = L.qu;
#if !SPLIT
	dl->qv = L.qv;
#endif
	dl->tpLast = L.tpL; dl->sLast = L.sL; dl->xinLast = L.xinL; dl->vhpLast = L.vhpL;
	dl->vcaLast = L.vcaL; dl->iAbcLast = L.iaL; dl->iLad = L.i; dl->iAbc = L.ia;
	dl->tl0 = L.tl0;
	dlv_slowUpdate(dl, L.accTp * INV_BLK, L.accS * INV_BLK);
}
