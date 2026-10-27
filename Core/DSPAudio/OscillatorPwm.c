#include "Oscillator.h"
#include "OscillatorPwm.h"
#include "StockWaves.h"
#include "SampleMemory.h"
#include "wavetable.h"
#include "config.h"
#include "OscSyncLane.h"

#define PWM_PHASE_CYCLE 4294967296.0f
#define PWM_PHASE_MASK  0x003FFFFFu
#define PWM_INV_CYCLE   2.3283064365386963e-10f   /* 1 / 2^32 */
#define PWM_QUARTER     0x40000000u

/*
 * Interpolated read of a 1,024-point level (S085 C7.2).
 *
 * What:       index = phase >> 22, next wraps at 1,024, fraction = low 22
 *             bits x 1/2^22 (2.384185791015625e-07f, written inline so the
 *             host extract carries it). The same read as
 *             calcWavetableOscBlock().
 * Why:        the 1/2^22 fraction is the fix of S084 P1 F3 (1/2^32 made the
 *             read stepped); PWM_INV_CYCLE stays only for whole-cycle
 *             positions (warp, split, shift, sweep).
 */
static inline int32_t pwm_read1024(const int16_t* tab, uint32_t phase)
{
	const uint32_t i = (phase >> 22) & 0x3FFu;
	const uint32_t n = (i + 1u) & 0x3FFu;
	const float frac = (float)(phase & PWM_PHASE_MASK) * 2.384185791015625e-07f;
	const int32_t a = tab[i];

	return a + (int32_t)(frac * (float)((int32_t)tab[n] - a));
}

/* 4,096-point sine read (the calcSineBlock() read) for the swp segment 0. */
static inline int32_t pwm_readSine(uint32_t phase)
{
	const uint32_t i = (phase >> 20) & 0xFFFu;
	const float frac = (float)(phase & 0x000FFFFFu) * 0.00000095367431640625f;
	const int32_t a = sine_table[i];

	return a + (int32_t)(frac * (float)((int32_t)sine_table[i + 1u] - a));
}

/* Interpolated read at a fractional position p in [0, 1) of one level. */
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
		const int32_t a = lv->tab[i];

		return a + (int32_t)(frac * (float)((int32_t)lv->tab[n] - a));
	}
}

/* prc: saw(phase) - saw(phase + shift), x scale, saturated. */
static inline int32_t pwm_sawPair(const int16_t* tab, uint32_t phase,
                                  uint32_t shift, float scale)
{
	return __SSAT((int32_t)((float)(pwm_read1024(tab, phase) -
	                                pwm_read1024(tab, phase + shift)) * scale), 16);
}

/* psw: saw(phase) + saw(phase + shift), x scale, saturated. */
static inline int32_t pwm_sawSum(const int16_t* tab, uint32_t phase,
                                 uint32_t shift, float scale)
{
	return __SSAT((int32_t)((float)(pwm_read1024(tab, phase) +
	                                pwm_read1024(tab, phase + shift)) * scale), 16);
}

/* ptr/pq3/pw3: two slopes meeting at split, then one read. */
static inline int32_t pwm_warp(const stock_level_t* lv, uint32_t phase,
                               const OscPwmState* s)
{
	const float u = (float)phase * PWM_INV_CYCLE;
	const float w = (float)s->split * PWM_INV_CYCLE;
	const float p = (u < w) ? (u * s->warpA) : (s->mix + (u - w) * s->warpB);

	return pwm_readWarp(lv, p);
}

/* swp: sine->tri, tri->saw, saw->square (two reads + blend), then prc. */
static inline int32_t pwm_sweep(uint8_t level, uint32_t phase,
                                const OscPwmState* s)
{
	int32_t a;
	int32_t b;

	if (s->seg >= 3u)
		return pwm_sawPair(sawTable[level], phase - PWM_QUARTER,
		                   s->shift, s->scale);
	switch (s->seg) {
	case 0u:
		a = pwm_readSine(phase);
		b = pwm_read1024(triTable[level], phase + PWM_QUARTER);
		break;
	case 1u:
		a = pwm_read1024(triTable[level], phase + PWM_QUARTER);
		b = pwm_read1024(sawTable[level], phase - PWM_QUARTER);
		break;
	default:
		a = pwm_read1024(sawTable[level], phase - PWM_QUARTER);
		b = pwm_sawPair(sawTable[level], phase - PWM_QUARTER,
		                s->shift, s->scale);
		break;
	}
	return (int32_t)((float)a + s->mix * ((float)b - (float)a));
}

/* The family's table level: saw for prc/psw/swp, tri for ptr, stock for
** pq3 (sq3) and pw3 (sw3) with their Rec/Saw fallback before an install. */
static inline stock_level_t pwm_familyLevel(uint8_t family, uint8_t level)
{
	stock_level_t lv;

	if (family == OSC_PWM_PQ3)
		return stockWave_level(0u, level);
	if (family == OSC_PWM_PW3)
		return stockWave_level(1u, level);
	lv.tab = (family == OSC_PWM_PTR) ? triTable[level] : sawTable[level];
	lv.shift = 22u;
	lv.mask = 1023u;
	return lv;
}

/*
 * Compute one block's width state (S085 C7.2).
 *
 * What:       phaseIncTarget (2^32 per cycle), tableOffset for
 *             frequency x compression, and every OscPwmState field from
 *             c = source / 127 (the `mod` row; S085 P2 row swap).
 * Why:        the divisions run once per block, not per sample. The natural
 *             split of a warped family is c = 0 (w = s0); the slopes map
 *             [0,w) onto [0,s0) and [w,1) onto [s0,1); s0 sits in
 *             OscPwmState.mix for the warped families (swp uses mix for its
 *             segment blend).
 * Inputs:     osc (source, waveform), freqHz.  Outputs: target, level, pwm.
 * Accessors:  osc_setFreq() on a cache miss.
 * Affiliates: osc_freqToTableIndex(), sampleMemory_stockValid().
 */
void oscPwm_prepare(struct OscStruct* osc, float freqHz)
{
	OscPwmState* s;
	uint8_t family;
	float c;
	float w = 0.5f;
	float comp = 1.0f;
	float s0 = 0.5f;
	uint8_t warped = 0u;

	if (!osc)
		return;
	s = &osc->pwm;
	/* S085 P2 row swap: the shape is the `wav` row (waveform), the width the
	** `mod` row (source). */
	family = (osc->waveform < OSC_PWM_FAMILY_COUNT) ? osc->waveform
	                                                : OSC_PWM_PRC;
	c = (float)osc->source * (1.0f / 127.0f);
	s->seg = 0u;
	s->mix = 0.0f;
	s->warpA = 0.5f;
	s->warpB = 0.5f;
	s->scale = 1.0f;
	switch (family) {
	case OSC_PWM_PRC:
		w = 0.5f - 0.46f * c;
		s->scale = 1.0f / (2.0f * ((w > 1.0f - w) ? w : (1.0f - w)));
		break;
	case OSC_PWM_PSW:
		w = 0.5f * c;
		s->scale = 0.5f / (1.0f - w);
		break;
	case OSC_PWM_PTR:
		w = 0.5f + (0.03f - 0.5f) * c;
		warped = 1u;
		break;
	case OSC_PWM_PQ3:
		s0 = sampleMemory_stockValid() ? 0.78f : 0.5f;
		w = s0 + (0.12f - s0) * c;
		warped = 1u;
		break;
	case OSC_PWM_PW3:
		s0 = sampleMemory_stockValid() ? 0.42f : 0.5f;
		w = s0 + (0.04f - s0) * c;
		warped = 1u;
		break;
	default: { /* OSC_PWM_SWP */
		float p = 4.0f * c;

		if (p >= 4.0f)
			p = 3.9999f;
		s->seg = (uint8_t)p;
		s->mix = p - (float)s->seg;
		w = (s->seg >= 3u) ? (0.5f - 0.46f * s->mix) : 0.5f;
		s->scale = 1.0f / (2.0f * ((w > 1.0f - w) ? w : (1.0f - w)));
		break; }
	}
	if (warped || family == OSC_PWM_SWP)
		w = (w < 0.02f) ? 0.02f : ((w > 0.98f) ? 0.98f : w);
	s->split = (uint32_t)(w * PWM_PHASE_CYCLE);
	s->shift = s->split;
	if (warped) {
		s->mix = s0;
		s->warpA = s0 / w;
		s->warpB = (1.0f - s0) / (1.0f - w);
		comp = (s->warpA > s->warpB) ? s->warpA : s->warpB;
	}
	osc->phaseIncTarget =
		(uint32_t)(((1024.0f * freqHz) / REAL_FS) * 4194304.0f);
	{
		const uint8_t level = osc_freqToTableIndex(freqHz * comp);

		osc->tableOffset = (level > 10u) ? 10u : level;
	}
}

/*
 * One pwm block, shared by both entry points (S085 C7.2).
 *
 * What:       the C7.1 render contract; modBuffer NULL = no FM. The family
 *             switch is per sample on a block constant (branch-predicted);
 *             C17.4 measures it, and per-family loops are the option if the
 *             probe shows a material saving (plan §6 item 2).
 */
OSC_SYNC_INLINE void oscPwm_block(struct OscStruct* osc,
                                  const int16_t* modBuffer, int16_t* buf,
                                  uint8_t size, float gain,
                                  const OscSyncLane* lane)
{
	const OscPwmState* s = &osc->pwm;
	const uint8_t family = osc->waveform;   /* S085 P2 row swap: `wav` row */
	const stock_level_t lv = pwm_familyLevel(family, osc->tableOffset);
	uint32_t phase = osc->phase;
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const uint32_t readPhase = modBuffer
			? phase + (uint32_t)((int32_t)((float)modBuffer[i] * osc->fmMod) << 19)
			: phase;
		int32_t out;

		switch (family) {
		case OSC_PWM_PRC:
			out = pwm_sawPair(lv.tab, readPhase, s->shift, s->scale);
			break;
		case OSC_PWM_PSW:
			out = pwm_sawSum(lv.tab, readPhase, s->shift, s->scale);
			break;
		case OSC_PWM_SWP:
			out = pwm_sweep(osc->tableOffset, readPhase, s);
			break;
		default:
			out = pwm_warp(&lv, readPhase, s);
			break;
		}
		buf[i] = (int16_t)((float)out * gain);
		phase += inc;            /* S085 C5.6: per-sample slew */
		if (lane)                /* assessment A2: hard-sync reset */
			phase = oscSync_phase(lane, i, phase, 0u);
		inc += (uint32_t)step;
	}
	osc->phase = phase;
	osc->phaseInc = inc;
}

/*
 * Render one pwm block (S085 C7.2).
 *
 * What:       prc/psw two reads; ptr/pq3/pw3 one warped read; swp up to
 *             three reads and a blend; per-sample slew. The FM variant adds
 *             (mod x fmMod) << 19 to the read phase.
 * Accessors:  Oscillator.c type dispatch.
 */
/*
 * The plain/sync shared pwm loop (S085 assessment A2, ITCM).
 *
 * What:       oscPwm_block() with a runtime lane: NULL for the ordinary plain
 *             render, an OscSyncLane for a hard-sync slave. One copy of the
 *             loop serves both, kept out of line (noinline, noclone) and with loop unswitching
 *             disabled so the compiler cannot duplicate it per lane value.
 * Why:        a separate sync copy of every renderer cost 5.6 KB of ITCM
 *             (16,272 of 16,384 B) and would have left S086 no room. The pwm
 *             loop is new in S085 (no pre-S085 codegen to preserve), so it
 *             takes the shared form; the wav loops keep separate sync copies
 *             to stay identical to the frozen pre-S085 code.
 * Cost:       the plain render pays one predictable compare-and-branch per
 *             sample (lane NULL); the sync render adds the oscSync_phase()
 *             select. Both figures are in C19.2.
 */
INITCM OSC_SHARED_LOOP
void oscPwm_blockLane(struct OscStruct* osc, int16_t* buf, uint8_t size,
                      float gain, const OscSyncLane* lane)
{
	oscPwm_block(osc, 0, buf, size, gain, lane);
}

INITCM void oscPwm_render(struct OscStruct* osc, int16_t* buf, uint8_t size,
                          float gain)
{
	if (osc && buf)
		oscPwm_blockLane(osc, buf, size, gain, 0);
}

/*
 * Render one pwm block with FM (S085 C7.2).
 * Accessors: Oscillator.c type dispatch.
 */
INITCM void oscPwm_renderFm(struct OscStruct* osc, int16_t* modBuffer,
                            int16_t* buf, uint8_t size, float gain)
{
	if (osc && buf && modBuffer)
		oscPwm_block(osc, modBuffer, buf, size, gain, 0);
}

/*
 * Render one pwm block as a hard-sync slave (S085 assessment A2).
 *
 * What:       oscPwm_block() with the master's resets applied in the loop
 *             (OscSyncLane.h); no FM (snc renders the slave plain).
 * Accessors:  osc_renderSyncBlock().
 */
INITCM void oscPwm_renderSync(struct OscStruct* osc, int16_t* buf,
                              uint8_t size, float gain,
                              const struct OscSyncLane* lane)
{
	if (osc && buf && lane)
		oscPwm_blockLane(osc, buf, size, gain, lane);
}
