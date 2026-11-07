/*
 * AlkalineControl.c - TB-303 control path at control rate (S093 P0).
 * See AlkalineControl.h. Equations: S092_P6_DSP_EQUATIONS.md §4.1-§4.4,
 * §5.2 (prewarp); reference code: tools/alkaline/alk_model.py.
 */
#include <math.h>
#include "AlkalineControl.h"
#include "AlkalineTables.h"

/* ---- circuit values ([SN] p5 unless stated; S092 P6 §4 tables) ---- */
#define V12        12.0f
#define V561       3.263f          /* Q9's node at rest (P4 §3.1) */
#define VBE11_CAL  0.5279f         /* Q11 Vbe at 7.375 uA */
#define I_CAL      7.375e-6f
#define BETA11     240.0f
#define R47        10e3f
#define R61        10e3f
#define R63        220e3f
#define R71        2.2e3f
#define R72        100e3f
#define R73        100e3f
#define R46        47e3f
#define R119       47e3f
#define R120       22e3f
#define C13F       1e-6f
#define C36F       0.033e-6f
#define TM3        152.74e3f       /* P4 calibration */
#define VR         50e3f           /* VR3, VR4b, VR5, VR7 */
#define VD24       0.48f
#define R_4066     250.0f
#define VT         0.02585f
#define ND_VT      (1.752f * VT)   /* 1N4148 n Vt */
#define IS_D       2.52e-9f
#define I_R117     ((5.333f - 1.02f) / 22e3f)

float alk_taper(const float r)
{
	return (exp2f(4.0f * r) - 1.0f) * (1.0f / 15.0f);
}

/* ---- §4.1 expo converter (Q9/Q10/Q11) ---- */
float alk_expoCurrent(const float cut, const float env, const float n53,
                      const float vW4, float *dv)
{
	const float r3 = VR * alk_taper(cut);
	const float vth3 = V561 + (V12 - V561) * (R47 + r3) / (R47 + VR);
	const float rth3 = (VR - r3) * (R47 + r3) / (R47 + VR);
	const float r5 = VR * alk_taper(env);
	const float vth5 = V561 + (n53 - V561) * (R61 + r5) / (R61 + VR);
	const float rth5 = (R61 + r5) * (VR - r5) / (R61 + VR);
	const float g5 = 1.0f / (R63 + rth5);
	const float g4 = 1.0f / R72;
	const float d = ((vth5 - V561) * g5 + (vW4 - V561) * g4)
	              / (1.0f / R71 + g5 + g4);
	const float e = expf(d / VT);
	float i = I_CAL;
	uint8_t n;

	for (n = 0u; n < 3u; n++) {
		const float vbe = VBE11_CAL + VT * logf(fmaxf(i, 1e-12f) / I_CAL);
		const float i73 = fmaxf((vth3 - (V561 + vbe)) / (rth3 + TM3 + R73), 1e-12f);

		i = i73 * e / (1.0f + e / BETA11);
	}
	*dv = d;
	return i;
}

/* ---- §4.2 MEG ---- */
void alkMeg_init(AlkMeg *m)
{
	m->v = ALK_MEG_V_REST;
	m->strikeLeft = 0.0f;
}

void alkMeg_trigger(AlkMeg *m)
{
	m->strikeLeft = ALK_MEG_T_STRIKE_S;
}

float alkMeg_n53(const AlkMeg *m)
{
	const float x = (m->v - ALK_MEG_DROP) / ALK_MEG_SOFT;
	const float sp = (x > 30.0f) ? x : log1pf(expf(x));

	return ALK_MEG_V53_MIN + ALK_MEG_SOFT * sp;
}

float alkMeg_step(AlkMeg *m, const float decay, const uint8_t accent)
{
	if (m->strikeLeft > 0.0f) {
		m->v = ALK_MEG_V_PEAK;
		m->strikeLeft -= ALK_DT;
	} else {
		/* discharge through R138 + VR6 (or the 4066 on ACCENT) into the
		 * D28 / D27 + D26 stack, whose voltage follows its current */
		const float r = 68e3f + (accent ? R_4066 : 1e6f * alk_taper(decay));
		float i = fmaxf((m->v - 1.3f) / r, 1e-12f);
		float vy = 0.0f;
		uint8_t n;

		for (n = 0u; n < 4u; n++) {
			const float vj = 2.0f * ND_VT * logf((I_R117 + i) / IS_D + 1.0f);

			vy = vj + ND_VT * logf(i / IS_D + 1.0f);
			i = fmaxf((m->v - vy) / r, 0.0f);
		}
		m->v = vy + (m->v - vy) * expf(-ALK_DT / (r * 1e-6f));
	}
	return alkMeg_n53(m);
}

/* ---- §4.3 accent network: 5 x 5 backward-Euler nodal solve ---- */
void alkAcc_init(AlkAccentNet *a)
{
	a->v13 = ALK_ACC_V13_REST;
	a->v36 = 0.0f;
}

/* Gaussian elimination without pivoting (G is a conductance matrix:
 * symmetric, diagonally dominant). */
static void alk_solve5(float G[5][5], float b[5], float x[5])
{
	int8_t r, c, k;

	for (k = 0; k < 5; k++) {
		const float inv = 1.0f / G[k][k];

		for (r = (int8_t)(k + 1); r < 5; r++) {
			const float f = G[r][k] * inv;

			if (f != 0.0f) {
				for (c = k; c < 5; c++)
					G[r][c] -= f * G[k][c];
				b[r] -= f * b[k];
			}
		}
	}
	for (r = 4; r >= 0; r--) {
		float s = b[r];

		for (c = (int8_t)(r + 1); c < 5; c++)
			s -= G[r][c] * x[c];
		x[r] = s / G[r][r];
	}
}

void alkAcc_step(AlkAccentNet *a, const uint8_t accent, const float accKnob,
                 const float res, const float n53, const float v134,
                 float *vW4, float *iAcc)
{
	static const uint8_t combos[4][2] = { {1u, 1u}, {1u, 0u}, {0u, 1u}, {0u, 0u} };
	const float ra7 = fmaxf(VR * accKnob, 1.0f);
	const float rb7 = fmaxf(VR * (1.0f - accKnob), 1.0f) + (accent ? R_4066 : 1e12f);
	const float ra = fmaxf(VR * res, 1.0f);
	const float rb = fmaxf(VR * (1.0f - res), 1.0f);
	const float g13 = C13F / ALK_DT, g36 = C36F / ALK_DT;
	const float g119 = 1.0f / R119;
	float best[5] = {0};
	uint8_t found = 0u, n;

	/* all four diode states are solved every block (constant cost); the
	 * first consistent one in this order is kept, as in alk_model.py */
	for (n = 0u; n < 4u; n++) {
		const float g46 = combos[n][0] ? 1.0f / R46 : 0.0f;
		const float g120 = combos[n][1] ? 1.0f / R120 : 0.0f;
		float G[5][5] = {
			{ 1.0f / ra7 + 1.0f / rb7 + g46 + g119, -g46, 0.0f, 0.0f, -g119 },
			{ -g46, g46 + 1.0f / ra, -1.0f / ra, 0.0f, 0.0f },
			{ 0.0f, -1.0f / ra, 1.0f / ra + 1.0f / rb + 1.0f / R72, -1.0f / rb, 0.0f },
			{ 0.0f, 0.0f, -1.0f / rb, 1.0f / rb + g13, 0.0f },
			{ -g119, 0.0f, 0.0f, 0.0f, g119 + g36 + g120 } };
		float b[5] = { n53 / rb7 + g46 * VD24, -g46 * VD24, v134 / R72,
		               g13 * a->v13, g36 * a->v36 + g120 * ALK_ACC_VD27 };
		float x[5];

		alk_solve5(G, b, x);
		{
			const uint8_t ok24 = (uint8_t)((x[0] - VD24 - x[1] >= 0.0f) == (combos[n][0] != 0u));
			const uint8_t ok27 = (uint8_t)((x[4] - ALK_ACC_VD27 >= 0.0f) == (combos[n][1] != 0u));

			if ((!found && ok24 && ok27) || n == 3u) {
				if (!found) {
					uint8_t q;

					for (q = 0u; q < 5u; q++)
						best[q] = x[q];
				}
				found = 1u;
			}
		}
	}
	a->v13 = best[3];
	a->v36 = best[4];
	*vW4 = best[2];
	*iAcc = fmaxf(0.0f, (best[4] - ALK_ACC_VD27) / R120);
}

/* ---- §4.4 VEG ---- */
void alkVeg_init(AlkVeg *g)
{
	g->v = 12.0f - 1e-3f;
	g->delayLeft = 0.0f;
	g->attLeft = 0.0f;
}

void alkVeg_trigger(AlkVeg *g)
{
	g->delayLeft = ALK_VEG_ATT_DELAY_S;
	g->attLeft = 8.0f * ALK_VEG_TAU_ATT_S;
}

float alkVeg_step(AlkVeg *g, const uint8_t gate)
{
	if (g->delayLeft > 0.0f) {
		g->delayLeft -= ALK_DT;
	} else if (g->attLeft > 0.0f) {
		g->v *= expf(-ALK_DT / ALK_VEG_TAU_ATT_S);
		g->attLeft -= ALK_DT;
	}
	g->v = ALK_VEG_V_REC + (g->v - ALK_VEG_V_REC) * expf(-ALK_DT / ALK_VEG_TAU_REC_S);
	{
		float p = (g->v - ALK_VEG_V0) * ALK_VEG_DV_INV;
		int32_t k;
		float i;

		p = fminf(fmaxf(p, 0.0f), (float)(ALK_VEG_POINTS - 1u));
		k = (int32_t)p;
		if (k > (int32_t)ALK_VEG_POINTS - 2)
			k = (int32_t)ALK_VEG_POINTS - 2;
		i = alkVegCurrent[k] + (p - (float)k) * (alkVegCurrent[k + 1] - alkVegCurrent[k]);
		return gate ? i : 0.0f;
	}
}

/* ---- §5.2 TPT prewarp: the factor on the ladder current ---- */
float alk_prewarp(const float iLad, const float res)
{
	float p = fminf(fmaxf(res, 0.0f), 1.0f) * 10.0f;
	int32_t k = (int32_t)p;
	float c, th;

	if (k > 9)
		k = 9;
	c = alkPrewarpC[k] + (p - (float)k) * (alkPrewarpC[k + 1] - alkPrewarpC[k]);
	/* theta = c w1 / (2 fs), w1 = I / (4 Vt C19) */
	th = fminf(c * iLad * (1.0f / (4.0f * VT * 33e-9f)) / (2.0f * 44108.0f), 1.3f);
	return (th > 1e-6f) ? tanf(th) / th : 1.0f;
}
