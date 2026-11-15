/*
 * Per-block helpers of the S093 P1 experimental kernels: init, the
 * Res-interpolated coefficient set and the slow network (as DiodeLadder.c).
 */
#include <string.h>
#include "dlv.h"

#ifndef GAU1
#define GAU1 1
#endif
#ifndef NET1
#define NET1 0
#endif
#if NET1
#include "dlv_n1.h"
#define AS_BE_U N1_BE_U
#define DECAY_U N1_DECAY_U
#define DECAY_V N1_DECAY_V
#else
#define AS_BE_U 1.13317765e-05f
#define DECAY_U 0.977465749f
#define DECAY_V 0.977465749f
#endif

void dlv_init(DLV *dl)
{
	memset(dl, 0, sizeof(*dl));
	dl->t0 = dl->t1 = dl->t2 = dl->t3 = 1.0f;
	dl->tin = dl->tout = dl->tota = 1.0f;
	dl->tl0 = 1.0f;
}

void dlv_setNet(DLV *dl, const DiodeLadderNet *lo, const DiodeLadderNet *hi, const float f)
{
	const float g = 1.0f - f;

	dl->netLo = lo; dl->netHi = hi; dl->netFrac = f;
	dl->alf0 = g * lo->alf[0] + f * hi->alf[0];
	dl->alf1 = g * lo->alf[1] + f * hi->alf[1];
	dl->bef0 = g * lo->bef[0] + f * hi->bef[0];
	dl->bef1 = g * lo->bef[1] + f * hi->bef[1];
	dl->cfu0 = g * lo->cf[0][0] + f * hi->cf[0][0];
	dl->cfu1 = g * lo->cf[0][1] + f * hi->cf[0][1];
	dl->cfv0 = g * lo->cf[1][0] + f * hi->cf[1][0];
	dl->cfv1 = g * lo->cf[1][1] + f * hi->cf[1][1];
	dl->gau0 = g * lo->ga[0][0] + f * hi->ga[0][0];
	dl->gau1 = GAU1 ? g * lo->ga[0][1] + f * hi->ga[0][1] : 0.0f;
	dl->gav0 = g * lo->ga[1][0] + f * hi->ga[1][0];
	dl->dv0 = g * lo->d[1][0] + f * hi->d[1][0];
	dl->ku0 = (g * lo->d[0][0] + f * hi->d[0][0])
	        + dl->cfu0 * dl->bef0 + dl->cfu1 * dl->bef1 + AS_BE_U * dl->gau0;
	dl->ku1 = (g * lo->d[0][1] + f * hi->d[0][1]) + AS_BE_U * dl->gau1;
}

void dlv_slowUpdate(DLV *dl, const float m0, const float m1)
{
	const DiodeLadderNet *lo = dl->netLo, *hi = dl->netHi;
	const float f = dl->netFrac, g = 1.0f - f;
	float zn[8], ru = 0.0f, rv = 0.0f;
	unsigned r, c;
	/* GAU1 = 0 drops the s input of the u asymptote copy as well */
	const float m1u = GAU1 ? m1 : 0.0f;

	for (r = 0u; r < 8u; r++) {
		float a = lo->gam[r][0] * m0 + lo->gam[r][1] * m1;
		float b = hi->gam[r][0] * m0 + hi->gam[r][1] * m1;

		for (c = 0u; c < 8u; c++) {
			a += lo->phi[r][c] * dl->z[c];
			b += hi->phi[r][c] * dl->z[c];
		}
		zn[r] = g * a + f * b;
	}
	dl->z[8] = DECAY_U * dl->z[8] + g * (lo->gam[8][0] * m0 + lo->gam[8][1] * m1u)
	         + f * (hi->gam[8][0] * m0 + hi->gam[8][1] * m1u);
	dl->z[9] = DECAY_V * dl->z[9] + g * (lo->gam[9][0] * m0 + lo->gam[9][1] * m1)
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
