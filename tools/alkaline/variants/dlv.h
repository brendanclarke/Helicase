/*
 * State of the S093 P1 experimental kernels (dlv_kernel.c). The firmware's
 * DiodeLadder plus the fields the variants add (SPLIT's second-pass state,
 * A1's lagged rung-0 secant).
 */
#ifndef DLV_H_
#define DLV_H_

#include <stdint.h>
#include "DiodeLadder.h"

/* output full scale: the OTA can reach R_OTA x I_abc (about 6.7 V) when
 * the filter is driven, so the variants use 7 V (S093 P1 §2) */
#define VOUT_SCALE (32767.0f / 7.0f)

typedef struct {
	float x0, x1, x2, x3;
	float t0, t1, t2, t3, tin, tout, tota;
	float zo;
	float y0, y1, qu, qv;
	float z[10];
	float rU, rV, rUPrev, rVPrev;
	float tpLast, sLast, xinLast, vhpLast, vcaLast, iAbcLast;
	float iLad, iAbc;
	float yv0, yv1, tpLast2;     /* SPLIT */
	float tl0;                   /* A1 */
	const DiodeLadderNet *netLo, *netHi;
	float netFrac;
	float alf0, alf1, bef0, bef1, cfu0, cfu1, cfv0, cfv1;
	float gau0, gau1, gav0, ku0, ku1, dv0;
} DLV;

void dlv_init(DLV *dl);
void dlv_setNet(DLV *dl, const DiodeLadderNet *lo, const DiodeLadderNet *hi, float f);
void dlv_slowUpdate(DLV *dl, float m0, float m1);
void dlv_render(DLV *dl, int16_t *buf, float sScale, float iLadEnd, float iAbcEnd);

#endif
