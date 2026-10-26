/*
 * OscillatorPwm.c
 *
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#include "Oscillator.h"
#include "OscillatorPwm.h"
#include "StockWaves.h"
#include "wavetable.h"
#include "config.h"

/* 1,024-point table format: the index is phase >> 22 and a full cycle is
** 2^32 phase units (1,024 points x 2^22). */
#define PWM_PHASE_CYCLE  4294967296.0f
#define PWM_PHASE_MASK   0x003FFFFFu
#define PWM_INV_CYCLE    2.3283064365386963e-10f /* 1 / 2^32 */
#define PWM_QUARTER      0x40000000u             /* a quarter cycle */

/*
 * Interpolated read from one band-limited 1,024-point level (S084 P1 P1-5.2).
 *
 * What:       two reads plus linear interpolation; the index wraps at 1,024.
 * Why:        the same read as calcWavetableOscBlock(), so pwm shapes keep the
 *             established table interpolation.
 * Inputs:     lv (one level), phase (2^22 format).
 * Outputs:    the sample value.
 * Accessors:  the pwm helpers below.
 * Affiliates: wavetable.c, Oscillator.c.
 *
 * Fraction scale (S084 P1 review fix F3):
 * What:       the fraction is the low 22 phase bits times 1 / 2^22
 *             (2.384185791015625e-07f), the same constant as the stock
 *             wavetable reads in Oscillator.c, so frac spans 0..1 between two
 *             table points.
 * Why:        the previous code multiplied by PWM_INV_CYCLE (1 / 2^32), which
 *             is the scale of a whole 2^32 cycle, not of one table step: frac
 *             only reached 1/1,024, so every pwm read was effectively the
 *             lower table point (a stepped read with extra aliasing at low
 *             pitch). PWM_INV_CYCLE stays correct for the warp, split, shift
 *             and sweep positions, which are fractions of the whole cycle.
 * Note:       the constant is written inline (not a #define) so the host test
 *             extract of this function carries the firmware value with it.
 */
static inline int32_t pwm_read1024(const int16_t* tab, uint32_t phase)
{
	const uint32_t i = (phase >> 22) & 0x3FFu;
	const uint32_t n = (i + 1u) & 0x3FFu;
	const float frac =
		(float)(phase & PWM_PHASE_MASK) * 2.384185791015625e-07f; /* 1/2^22 */
	const int32_t a = (int32_t)tab[i];

	return a + (int32_t)(frac * (float)((int32_t)tab[n] - a));
}

/*
 * Interpolated read from the 4,096-point sine table (S084 P1 P1-5.2).
 *
 * What:       the same read as calcSineBlock(), for the pwm sweep's sine
 *             segment.
 * Inputs:     phase (2^22 format).  Outputs: the sine sample.
 * Accessors:  pwm_sweep().
 * Affiliates: sine_table, calcSineBlock().
 */
static inline int32_t pwm_readSine(uint32_t phase)
{
	const uint32_t i = (phase >> 20) & 0xFFFu;
	const uint32_t n = (i + 1u) & 0xFFFu;
	const float frac = (float)(phase & 0x000FFFFFu) * 0.00000095367431640625f;
	const int32_t a = (int32_t)sine_table[i];

	return a + (int32_t)(frac * (float)((int32_t)sine_table[n] - a));
}

/*
 * Interpolated read at a warped fractional phase (S084 P1 P1-5.2).
 *
 * What:       reads one 1,024-point table at p in [0..1), with linear
 *             interpolation.
 * Why:        the warp computes a fractional phase; this is its only read.
 * Inputs:     tab, p.  Outputs: the sample value.
 * Accessors:  pwm_warp().
 */
static inline int32_t pwm_readWarp(const stock_level_t* lv, float p)
{
	float x = p * (float)(lv->mask + 1u);
	uint32_t i;
	float frac;

	if (x < 0.0f)
		x = 0.0f;
	i = (uint32_t)x;
	if (i > lv->mask)
		i = lv->mask;
	frac = x - (float)i;
	{
		const uint32_t n = (i + 1u) & lv->mask;
		const int32_t a = (int32_t)lv->tab[i];

		return a + (int32_t)(frac * (float)((int32_t)lv->tab[n] - a));
	}
}

/*
 * Square from two band-limited falling saws (S084 P1 P1-5.2).
 *
 * What:       out = (saw(phase) - saw(phase + shift)) x scale; the difference
 *             removes the saw DC and leaves a pulse of width `shift`.
 * Why:        exact band-limiting for the default square family and the pwm
 *             sweep's square segment (plan §4.1.3).
 * Inputs:     tab (sawTable level), phase, shift, scale.
 * Outputs:    one saturated sample.
 * Accessors:  oscPwm_render(), oscPwm_renderFm(), pwm_sweep().
 */
static inline int32_t pwm_sawPair(const int16_t* tab, uint32_t phase,
                                  uint32_t shift, float scale)
{
	const int32_t a = pwm_read1024(tab, phase);
	const int32_t b = pwm_read1024(tab, phase + shift);

	return __SSAT((int32_t)((float)(a - b) * scale), 16);
}

/*
 * Pwm'ed saw from two shifted saws (S084 P1 P1-5.2).
 *
 * What:       out = (saw(phase) + saw(phase + shift)) x scale.
 * Why:        the psw family's exact band-limited pulse-saw (plan §4.1.3).
 * Inputs:     tab, phase, shift, scale.  Outputs: one saturated sample.
 * Accessors:  oscPwm_render(), oscPwm_renderFm().
 */
static inline int32_t pwm_sawSum(const int16_t* tab, uint32_t phase,
                                 uint32_t shift, float scale)
{
	const int32_t a = pwm_read1024(tab, phase);
	const int32_t b = pwm_read1024(tab, phase + shift);

	return __SSAT((int32_t)((float)(a + b) * scale), 16);
}

/*
 * One warped-phase read (S084 P1 P1-5.2).
 *
 * What:       maps the uniform phase onto two slopes meeting at `split`, then
 *             reads the family's table at the warped phase.
 * Why:        no width tables; the warp slopes come from oscPwm_prepare(), so
 *             the loop has no division (plan §4.1.3).
 * Inputs:     tab, phase, pwm state (split, warpA, warpB).
 * Outputs:    the sample value.
 * Accessors:  oscPwm_render(), oscPwm_renderFm() for ptr/pq3/pw3.
 */
static inline int32_t pwm_warp(const stock_level_t* lv, uint32_t phase,
                               const OscPwmState* s)
{
	const float u = (float)phase * PWM_INV_CYCLE;
	const float w = (float)s->split * PWM_INV_CYCLE;
	const float p = (u < w) ? (u * s->warpA)
	                        : (s->mix + (u - w) * s->warpB);

	return pwm_readWarp(lv, p);
}

/*
 * One sample of the pwm sweep (S084 P1 P1-5.2).
 *
 * What:       sine -> tri -> saw -> square -> pulse, two aligned reads and a
 *             crossfade. The segment-3 pulse narrows from 50 % to 4 %.
 * Why:        plan §4.1.3 swp family: one control sweeps the classic shapes.
 * Inputs:     tab (sawTable level for the square), phase, pwm state.
 * Outputs:    one sample.
 * Accessors:  oscPwm_render(), oscPwm_renderFm().
 */
static inline int32_t pwm_sweep(uint8_t tableOffset, uint32_t phase,
                                const OscPwmState* s)
{
	const float t = s->mix;
	int32_t a;
	int32_t b;

	if (s->seg == 3u)
		return pwm_sawPair(sawTable[tableOffset], phase - PWM_QUARTER,
		                   s->shift, s->scale);
	switch (s->seg) {
	case 0u:
		a = pwm_readSine(phase);
		b = pwm_read1024(triTable[tableOffset], phase + PWM_QUARTER);
		break;
	case 1u:
		a = pwm_read1024(triTable[tableOffset], phase + PWM_QUARTER);
		b = pwm_read1024(sawTable[tableOffset], phase - PWM_QUARTER);
		break;
	default:
		a = pwm_read1024(sawTable[tableOffset], phase - PWM_QUARTER);
		b = pwm_sawPair(sawTable[tableOffset], phase - PWM_QUARTER,
		                s->shift, s->scale);
		break;
	}
	return (int32_t)((float)a + t * ((float)b - (float)a));
}

/*
 * The family's table level (S084 VX2 VX2-4.9).
 *
 * What:       prc/psw/swp: sawTable level; ptr: triTable level (shift 22,
 *             mask 1023); pq3: stock sq3 level; pw3: stock sw3 level (their
 *             variable lengths, or the recTable/sawTable fallback before
 *             the first install).
 * Why:        the render loop selects the table once per block.
 * Inputs:     family, level.  Outputs: stock_level_t.
 * Accessors:  oscPwm_render(), oscPwm_renderFm().
 */
static inline stock_level_t pwm_familyLevel(uint8_t family, uint8_t offset)
{
	stock_level_t lv;

	switch (family) {
	case OSC_PWM_PTR:
		lv.tab = triTable[offset];
		lv.shift = 22u;
		lv.mask = 1023u;
		return lv;
	case OSC_PWM_PQ3:
		/* S084 VX2 VX2-4.9: the 303 square, or its Rec fallback. */
		return stockWave_level(0u, offset);
	case OSC_PWM_PW3:
		/* S084 VX2 VX2-4.9: the 303 saw, or its Saw fallback. */
		return stockWave_level(1u, offset);
	case OSC_PWM_PRC:
	case OSC_PWM_PSW:
	case OSC_PWM_SWP:
	default:
		lv.tab = sawTable[offset];
		lv.shift = 22u;
		lv.mask = 1023u;
		return lv;
	}
}

/*
 * Compute one block's pwm width state (S084 P1 P1-5.1).
 *
 * What:       sets phaseIncTarget (1,024-point format), tableOffset for
 *             freqHz x compression (compression = max(s0/w, (1-s0)/(1-w)) for
 *             warped families, 1 otherwise) and every OscPwmState field from
 *             the control c = waveform/127.
 * Why:        all width-dependent divisions happen once per block; the
 *             per-sample loops stay division-free (constant CPU).
 * Inputs:     osc (source, waveform), freqHz.
 * Outputs:    osc->phaseIncTarget, osc->tableOffset, osc->pwm.
 * Accessors:  osc_setFreq().
 * Affiliates: Oscillator.c frequency helpers, osc_freqToTableIndex().
 */
void oscPwm_prepare(struct OscStruct* osc, float freqHz)
{
	OscPwmState* s;
	uint8_t family;
	float c;
	float w;
	float comp = 1.0f;
	float s0 = 0.5f;
	uint8_t warped = 0u;

	if (!osc)
		return;
	s = &osc->pwm;
	family = osc->source;
	if (family >= OSC_PWM_FAMILY_COUNT) {
		family = OSC_PWM_PRC;
		osc->source = family;
	}
	c = (float)osc->waveform * (1.0f / 127.0f);
	s->seg = 0u;
	s->mix = 0.0f;
	s->warpA = 0.5f;
	s->warpB = 0.5f;
	s->split = (uint32_t)(0.5f * PWM_PHASE_CYCLE);
	s->shift = s->split;
	s->scale = 1.0f;
	w = 0.5f;

	switch (family) {
	case OSC_PWM_PRC:
		w = 0.5f - 0.46f * c;
		s->scale = 1.0f / (2.0f * ((w > (1.0f - w)) ? w : (1.0f - w)));
		break;
	case OSC_PWM_PSW: {
		const float d = 0.5f * c;

		w = d;
		s->scale = 0.5f / (1.0f - d);
		break;
	}
	case OSC_PWM_PTR:
		w = 0.5f + (0.03f - 0.5f) * c;
		warped = 1u;
		break;
	case OSC_PWM_PQ3:
		/* S084 VX2 VX2-4.9: sq3's edge sits at 0.78 of the cycle; the
		** recTable fallback is symmetric at 0.5. */
		s0 = sampleMemory_stockValid() ? 0.78f : 0.5f;
		w = s0 + (0.12f - s0) * c;
		warped = 1u;
		break;
	case OSC_PWM_PW3:
		/* S084 VX2 VX2-4.9: sw3's reset sits at 0.42; the sawTable
		** fallback is symmetric at 0.5. */
		s0 = sampleMemory_stockValid() ? 0.42f : 0.5f;
		w = s0 + (0.04f - s0) * c;
		warped = 1u;
		break;
	case OSC_PWM_SWP:
	default: {
		float p = 4.0f * c;

		if (p >= 4.0f)
			p = 3.9999f;
		s->seg = (uint8_t)p;
		s->mix = p - (float)s->seg;
		if (s->seg >= 3u)
			w = 0.5f - 0.46f * s->mix;
		else
			w = 0.5f;
		s->scale = 1.0f / (2.0f * ((w > (1.0f - w)) ? w : (1.0f - w)));
		break;
	}
	}

	/* The warp slopes divide by w and 1-w; psw's d = 0 is a plain saw. */
	if (warped || family == OSC_PWM_SWP) {
		if (w < 0.02f)
			w = 0.02f;
		if (w > 0.98f)
			w = 0.98f;
	}
	s->split = (uint32_t)(w * PWM_PHASE_CYCLE);
	if (family == OSC_PWM_PRC || family == OSC_PWM_PSW ||
	    family == OSC_PWM_SWP)
		s->shift = s->split;
	if (warped) {
		/*
		 * Natural split of warped families (S084 VX2 VX2-4.9).
		 *
		 * What:       c = 0 plays the wave unwarped (w = s0); c = 1
		 *             narrows to the family's minimum. The two slopes map
		 *             [0, w) onto [0, s0) and [w, 1) onto [s0, 1). s0 is
		 *             stored in OscPwmState.mix, which only swp otherwise
		 *             uses, so OscInfo does not grow.
		 * Why:        P2-B: sq3's edge sits at 0.78 of the cycle, sw3's
		 *             reset at 0.42; warping around 0.5 would shift and
		 *             distort them at c = 0.
		 * Inputs:     family, c, stock validity.  Outputs: w, warpA,
		 *             warpB, mix.
		 * Accessors:  oscPwm_prepare().
		 * Affiliates: pwm_warp(), tools/plots/plot_pwm.py.
		 */
		s->mix = s0;
		s->warpA = s0 / w;
		s->warpB = (1.0f - s0) / (1.0f - w);
		comp = (s->warpA > s->warpB) ? s->warpA : s->warpB;
	}
	osc->phaseIncTarget =
		(uint32_t)((((1024.0f * freqHz) / REAL_FS)) * 4194304.0f);
	{
		const uint8_t level = osc_freqToTableIndex(freqHz * comp);

		osc->tableOffset = (level > 10u) ? 10u : level;
	}
}

/*
 * Render one pwm oscillator block (S084 P1 P1-5.1).
 *
 * What:       about 30 instructions per sample (FM about 45): exactly two
 *             band-limited table reads (four for the sweep's square), the
 *             per-sample phase slew, and the prepared width state.
 * Why:        plan §4.1.3; one hot loop keeps the cost predictable.
 * Inputs:     osc, buf, size, gain.
 * Outputs:    buf scaled by gain; osc->phase and phaseInc advanced.
 * Accessors:  Oscillator.c type dispatch.
 * Affiliates: oscPwm_prepare(), oscPwm_renderFm().
 */
INITCM void oscPwm_render(struct OscStruct* osc, int16_t* buf, uint8_t size,
                          float gain)
{
	const OscPwmState* s;
	const int16_t* tab;
	stock_level_t lv;
	uint32_t phase;
	uint32_t inc;
	int32_t step;
	uint8_t family;
	uint8_t i;

	if (!osc || !buf)
		return;
	s = &osc->pwm;
	family = osc->source;
	lv = pwm_familyLevel(family, osc->tableOffset);
	tab = lv.tab;
	phase = osc->phase;
	inc = osc->phaseInc;
	step = osc->phaseIncStep;
	for (i = 0u; i < size; i++) {
		int32_t out;

		switch (family) {
		case OSC_PWM_PRC:
			out = pwm_sawPair(tab, phase, s->shift, s->scale);
			break;
		case OSC_PWM_PSW:
			out = pwm_sawSum(tab, phase, s->shift, s->scale);
			break;
		case OSC_PWM_SWP:
			out = pwm_sweep(osc->tableOffset, phase, s);
			break;
		case OSC_PWM_PTR:
		case OSC_PWM_PQ3:
		case OSC_PWM_PW3:
		default:
			out = pwm_warp(&lv, phase, s);
			break;
		}
		buf[i] = (int16_t)((float)out * gain);
		/* S084 P1 P1-3.7: per-sample slew, see calcSineBlock(). */
		phase += inc;
		inc += (uint32_t)step;
	}
	osc->phase = phase;
	osc->phaseInc = inc;
}

/*
 * Render one pwmed pwm oscillator block (S084 P1 P1-5.1).
 *
 * What:       oscPwm_render with the modulator block added to every table
 *             read as (mod x fmMod) << 19, the existing calcFmBlock depth.
 * Why:        keeps FM through the pwm families equal to the wavetable depth.
 * Inputs:     osc, modBuffer, buf, size, gain.
 * Outputs:    buf; phase state advanced.
 * Accessors:  Oscillator.c type dispatch.
 * Affiliates: oscPwm_render().
 */
INITCM void oscPwm_renderFm(struct OscStruct* osc, int16_t* modBuffer,
                            int16_t* buf, uint8_t size, float gain)
{
	const OscPwmState* s;
	const int16_t* tab;
	stock_level_t lv;
	uint32_t phase;
	uint32_t inc;
	int32_t step;
	uint8_t family;
	uint8_t i;

	if (!osc || !buf || !modBuffer)
		return;
	s = &osc->pwm;
	family = osc->source;
	lv = pwm_familyLevel(family, osc->tableOffset);
	tab = lv.tab;
	phase = osc->phase;
	inc = osc->phaseInc;
	step = osc->phaseIncStep;
	for (i = 0u; i < size; i++) {
		const uint32_t fmOffset =
			(uint32_t)((int32_t)((float)modBuffer[i] * osc->fmMod) << 19);
		const uint32_t readPhase = phase + fmOffset;
		int32_t out;

		switch (family) {
		case OSC_PWM_PRC:
			out = pwm_sawPair(tab, readPhase, s->shift, s->scale);
			break;
		case OSC_PWM_PSW:
			out = pwm_sawSum(tab, readPhase, s->shift, s->scale);
			break;
		case OSC_PWM_SWP:
			out = pwm_sweep(osc->tableOffset, readPhase, s);
			break;
		case OSC_PWM_PTR:
		case OSC_PWM_PQ3:
		case OSC_PWM_PW3:
		default:
			out = pwm_warp(&lv, readPhase, s);
			break;
		}
		buf[i] = (int16_t)((float)out * gain);
		/* S084 P1 P1-3.7: per-sample slew, see calcSineBlock(). */
		phase += inc;
		inc += (uint32_t)step;
	}
	osc->phase = phase;
	osc->phaseInc = inc;
}
