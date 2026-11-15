/*
 * modules/buscomp.c - host law for the bus compressor's sidechain duck
 * (S096 P2 F6).
 *
 * What:       builds the real BusCompressor.c (extracted, firmware includes
 *             removed) over host stand-ins for the Scene settings and checks
 *             BC-SC: after a few silent blocks (the detector at the floor,
 *             so the static target is 0 dB) a csc trigger at velocity 127 on
 *             the source voice ducks the fast cell by 2 + 19 cam dB:
 *               - ctm 0 and 10: the whole duck in the trigger's next block
 *                 (the S074 snap, unchanged up to ctm 12);
 *               - ctm 25, 41, 45 and 127: an eased attack: the cell falls
 *                 monotonically, its first block's step is at most 0.35 of
 *                 its largest, the largest is at most 1.05 x depth x 1.5207
 *                 / n, and it reaches 99 % of the depth between 0.7 n and
 *                 1.4 n blocks, n = 10 ms x smoothstep((ctm - 5) / 40) in
 *                 blocks (the maximum from ctm 45).
 * Why:        S096_P2_INSTRUMENT_FIXES.md §6: the duck landed in one block
 *             (0.73 ms) at every ctm, heard as a pop on the sidechained
 *             voice; the user asked for its attack to round off from ctm 5
 *             to 45.
 * Inputs:     generated fragments (Makefile target buscomp).
 * Outputs:    the failure count; exit 1 on any failure.
 * Accessors:  make -C tools/dsp_test buscomp.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define REAL_FS ((float)44108.07291666667f)
#include "sc_scene.c"

static uint8_t g_bc[4];
uint8_t scene_getBusCompSetting(uint8_t scene_index, uint8_t field)
{
	(void)scene_index;
	return (field < 4u) ? g_bc[field] : 0u;
}
uint8_t scene_getActiveIndex(void) { return 0u; }

#include "BusCompressor.host.c"

static int failures;
static void expect(int cond, const char *what, double a, double b)
{
	if (!cond) {
		if (failures < 20)
			printf("  BC-SC FAIL: %s (%g, %g)\n", what, a, b);
		failures++;
	}
}

static sample_mx_t st1[2u * OUTPUT_DMA_SIZE];
static sample_mx_t st2[2u * OUTPUT_DMA_SIZE];

static float gr_after_block(void)
{
	float p, gr = 0.0f;

	memset(st1, 0, sizeof(st1));
	memset(st2, 0, sizeof(st2));
	busComp_processBlock(st1, st2, 0u);
	(void)busComp_meterRaw(&p, &gr);
	return gr;
}

static void settle(uint8_t cam, uint8_t ctm)
{
	unsigned b;

	memset(&busComp, 0, sizeof(busComp));
	g_bc[SCENE_BUS_COMP_MODE] = SCENE_BUS_COMP_MODE_ST1;
	g_bc[SCENE_BUS_COMP_AMOUNT] = cam;
	g_bc[SCENE_BUS_COMP_TIME] = ctm;
	g_bc[SCENE_BUS_COMP_SIDECHAIN] = 2u;
	for (b = 0u; b < 20000u; b++)
		(void)gr_after_block();
}

int main(void)
{
	static const uint8_t snap[2] = { 0u, 10u };
	static const uint8_t round[4] = { 25u, 41u, 45u, 127u };
	const uint8_t cam = 48u;
	const float depth = 2.0f + 19.0f * (float)cam / 127.0f;
	const float blockMs = 1000.0f * (float)OUTPUT_DMA_SIZE / REAL_FS;
	unsigned c, b;

	for (c = 0u; c < 2u; c++) {
		float g0, g1;

		settle(cam, snap[c]);
		g0 = gr_after_block();
		busComp_sidechainTrigger(1u, 127u);     /* track 1 = voice 2 */
		g1 = gr_after_block();
		expect(fabsf(g0) < 0.01f, "settled at 0 dB", g0, 0);
		expect(fabsf(g1 - (g0 - depth)) < 0.01f, "short ctm: the duck in one block", g1, g0 - depth);
	}
	for (c = 0u; c < 4u; c++) {
		const float u = fminf(fmaxf(((float)round[c] - 5.0f) / 40.0f, 0.0f), 1.0f);
		const float n = 10.0f * u * u * (3.0f - 2.0f * u) / blockMs;
		float prev, first = 0.0f, largest = 0.0f;
		unsigned reached = 0u;
		int mono = 1;

		settle(cam, round[c]);
		prev = gr_after_block();
		busComp_sidechainTrigger(1u, 127u);
		for (b = 1u; b <= 60u; b++) {
			const float g = gr_after_block();
			const float step = prev - g;

			if (!reached) {
				if (step < -1e-4f)
					mono = 0;
				if (b == 1u)
					first = step;
				if (step > largest)
					largest = step;
				if (g <= -0.99f * depth)
					reached = b;
			}
			prev = g;
		}
		expect(mono, "the duck falls monotonically", round[c], 0);
		expect(first <= 0.35f * largest, "an eased first step", first, largest);
		expect(largest <= 1.05f * depth * 1.5207f / n, "the largest step", largest, depth * 1.5207f / n);
		expect(reached >= (unsigned)(0.7f * n) && reached <= (unsigned)(1.4f * n + 1.0f),
		       "arrives in about n blocks", reached, n);
	}
	printf("buscomp BC-SC failures: %d\n", failures);
	return failures ? 1 : 0;
}
