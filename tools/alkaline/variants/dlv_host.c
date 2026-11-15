/*
 * Host driver for the S093 P1 experimental kernels.
 *
 * IN:  int32 n, float32 k, float32 drv, then n x (s volts, i_lad A, i_abc A)
 * OUT: n float32, the OTA output in volts.
 * The currents are applied as the firmware does: block-end values, ramped.
 * Build: run_variants.py (with the generated Alkaline host fragments).
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "dlv.h"
#include "AlkalineControl.host.h"
#include "AlkalineTables.host.h"
#include "AlkalineControl.host.c"
#include "AlkalineTables.host.c"
#ifdef NET1
#if NET1
#include "dlv_n1.h"
#endif
#endif

int main(int argc, char **argv)
{
	FILE *f;
	int32_t n;
	float k, drv;

	if (argc < 3 || !(f = fopen(argv[1], "rb")))
		return 2;
	if (fread(&n, 4, 1, f) != 1 || fread(&k, 4, 1, f) != 1 || fread(&drv, 4, 1, f) != 1)
		return 2;
	{
		float *d = malloc(sizeof(float) * 3u * (size_t)n);
		float *o = malloc(sizeof(float) * (size_t)n);
		const float sScale = 8e-4f;          /* int16 = +-26 V: room for x8 drive */
		const float kk = k * (float)(ALK_RES_POINTS - 1u);
		const unsigned lo = (unsigned)fminf(kk, (float)(ALK_RES_POINTS - 2u));
		DLV dl;
		int b, j;

		if (fread(d, sizeof(float), 3u * (size_t)n, f) != 3u * (size_t)n)
			return 2;
		fclose(f);
		dlv_init(&dl);
#if NET1
		dlv_setNet(&dl, &alkResTableN1[lo], &alkResTableN1[lo + 1u], kk - (float)lo);
#else
		dlv_setNet(&dl, &alkResTable[lo], &alkResTable[lo + 1u], kk - (float)lo);
#endif
		for (b = 0; b + 32 <= n; b += 32) {
			int16_t buf[32];
			const float il = d[3 * (b + 31) + 1], ia = d[3 * (b + 31) + 2];

			for (j = 0; j < 32; j++)
				buf[j] = (int16_t)lrintf(drv * d[3 * (b + j)] / sScale);
			dlv_render(&dl, buf, sScale, il * alk_prewarp(il, k), ia);
			for (j = 0; j < 32; j++)
				o[b + j] = (float)buf[j] / VOUT_SCALE;
		}
		f = fopen(argv[2], "wb");
		fwrite(o, sizeof(float), (size_t)n, f);
		fclose(f);
	}
	return 0;
}
