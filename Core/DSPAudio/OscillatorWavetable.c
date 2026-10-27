#include "Oscillator.h"
#include "OscillatorWavetable.h"
#include "SampleMemory.h"
#include "config.h"
#include "OscSyncLane.h"

#define WTB_INV_CYCLE 2.3283064365386963e-10f /* 1 / 2^32 */

/*
 * Bind the selected wavetable (S085 C8.2).
 *
 * What:       live index = min(waveform, count - 1), where waveform (the wav
 *             row) is re-expanded from waveRequest when the catalogue
 *             generation changed; reloads geometry and clears primed on an
 *             index/generation change; no tables binds silence. The table
 *             position is source (the mod row), read by the renderers.
 * Accessors:  the renderers, the IM type/source writers.
 * Affiliates: sampleMemory_getWavetableInfo().
 */
void oscWavetable_bind(struct OscStruct *osc)
{
	uint8_t count;
	uint8_t index;
	uint32_t generation;
	WavetableInfo info;

	if (!osc || osc->oscType != OSC_TYPE_WTB)
		return;
	count = sampleMemory_getNumWavetables();
	if (count == 0u) {
		osc->waveform = 0u;
		osc->wtb.base = 0u;
		osc->wtb.frame_count = 0u;
		osc->wtb.primed = 0u;
		return;
	}
	generation = sampleMemory_getGeneration();
	/* A reinstall re-expands the selection from the stored request. */
	if (osc->wtb.generation != generation)
		osc->waveform = osc->waveRequest;
	index = (osc->waveform >= count) ? (uint8_t)(count - 1u) : osc->waveform;
	osc->waveform = index;
	if (osc->wtb.base != 0u && osc->wtb.index == index &&
	    osc->wtb.generation == generation)
		return;
	if (!sampleMemory_getWavetableInfo(index, &info)) {
		osc->wtb.base = 0u;
		return;
	}
	osc->wtb.base = info.offset;
	osc->wtb.frame_len = info.frame_len;
	osc->wtb.frame_count = info.frame_count;
	osc->wtb.bits = info.bits;
	osc->wtb.frame_bytes = (uint32_t)info.frame_len * (info.bits / 8u);
	osc->wtb.generation = generation;
	osc->wtb.index = index;
	osc->wtb.primed = 0u;
}

/*
 * Choose this block's frame pair and blend ramp (S085 C8.2).
 *
 * What:       the frame-pair rule above; returns the two frame indices, the
 *             start blend and the per-sample blend step, and stores pos.
 *             position is source (the mod row, 0..127).
 * Why:        S085 D1: all frame arithmetic is per block.
 */
static inline void oscWavetable_pair(OscWtbState *w, uint8_t position,
                                     uint8_t size, uint32_t *fa, uint32_t *fb,
                                     float *ff, float *ffStep)
{
	const uint32_t n = w->frame_count;
	const float target = (float)position * (float)(n - 1u) / 127.0f;
	float p0;
	float pEnd;

	if (!w->primed) {
		w->pos = target;
		w->primed = 1u;
	}
	p0 = w->pos;
	if (n < 2u) {
		*fa = 0u;
		*fb = 0u;
		pEnd = p0;
	} else if (target >= p0) {
		*fa = (uint32_t)p0;
		if (*fa > n - 2u)
			*fa = n - 2u;
		*fb = *fa + 1u;
		pEnd = (target < (float)*fb) ? target : (float)*fb;
	} else {
		uint32_t up = (uint32_t)p0;

		if ((float)up < p0)
			up++;
		if (up > n - 1u)
			up = n - 1u;
		if (up == 0u)
			up = 1u;
		*fb = up;
		*fa = up - 1u;
		pEnd = (target > (float)*fa) ? target : (float)*fa;
	}
	*ff = p0 - (float)*fa;
	*ffStep = (pEnd - p0) / (float)size;
	w->pos = pEnd;
}

/*
 * One wavetable block, shared by both entry points and both depths
 * (S085 C8.2). modBuffer NULL = no FM; bits selects the 8- or 16-bit read.
 */
OSC_SYNC_INLINE void oscWavetable_block(struct OscStruct *osc,
                                        const int16_t *modBuffer, int16_t *buf,
                                        uint8_t size, float gain,
                                        const OscSyncLane *lane)
{
	OscWtbState *w;
	uint32_t phase = osc->phase;
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	uint32_t fa;
	uint32_t fb;
	float ff;
	float ffStep;
	uint8_t i;

	oscWavetable_bind(osc);
	w = &osc->wtb;
	if (w->base == 0u || w->frame_count == 0u || w->frame_len < 2u) {
		for (i = 0u; i < size; i++) {
			buf[i] = 0;
			phase += inc;
			if (lane)
				phase = oscSync_phase(lane, i, phase, 0u);
			inc += (uint32_t)step;
		}
		osc->phase = phase;
		osc->phaseInc = inc;
		return;
	}
	oscWavetable_pair(w, osc->source, size, &fa, &fb, &ff, &ffStep);
	if (w->bits == 8u) {
		const int8_t *A = (const int8_t *)SAMPLE_FLASH_PTR(w->base + fa * w->frame_bytes);
		const int8_t *B = (const int8_t *)SAMPLE_FLASH_PTR(w->base + fb * w->frame_bytes);

		for (i = 0u; i < size; i++) {
			const uint32_t rp = modBuffer
				? phase + (uint32_t)((int32_t)((float)modBuffer[i] * osc->fmMod) << 19)
				: phase;
			const uint64_t m = (uint64_t)rp * w->frame_len;
			const uint32_t idx = (uint32_t)(m >> 32);
			const uint32_t nx = (idx + 1u == w->frame_len) ? 0u : idx + 1u;
			const float fr = (float)(uint32_t)m * WTB_INV_CYCLE;
			const float a = (float)((int32_t)A[idx] << 8);
			const float b = (float)((int32_t)A[nx] << 8);
			const float a2 = (float)((int32_t)B[idx] << 8);
			const float b2 = (float)((int32_t)B[nx] << 8);
			const float x0 = a + ff * (a2 - a);
			const float x1 = b + ff * (b2 - b);

			buf[i] = (int16_t)((x0 + fr * (x1 - x0)) * gain);
			ff += ffStep;
			phase += inc;            /* S085 C5.6: per-sample slew */
			if (lane)                /* assessment A2: hard-sync reset */
				phase = oscSync_phase(lane, i, phase, 0u);
			inc += (uint32_t)step;
		}
	} else {
		const int16_t *A = (const int16_t *)SAMPLE_FLASH_PTR(w->base + fa * w->frame_bytes);
		const int16_t *B = (const int16_t *)SAMPLE_FLASH_PTR(w->base + fb * w->frame_bytes);

		for (i = 0u; i < size; i++) {
			const uint32_t rp = modBuffer
				? phase + (uint32_t)((int32_t)((float)modBuffer[i] * osc->fmMod) << 19)
				: phase;
			const uint64_t m = (uint64_t)rp * w->frame_len;
			const uint32_t idx = (uint32_t)(m >> 32);
			const uint32_t nx = (idx + 1u == w->frame_len) ? 0u : idx + 1u;
			const float fr = (float)(uint32_t)m * WTB_INV_CYCLE;
			const float a = (float)A[idx];
			const float b = (float)A[nx];
			const float x0 = a + ff * ((float)B[idx] - a);
			const float x1 = b + ff * ((float)B[nx] - b);

			buf[i] = (int16_t)((x0 + fr * (x1 - x0)) * gain);
			ff += ffStep;
			phase += inc;
			if (lane)
				phase = oscSync_phase(lane, i, phase, 0u);
			inc += (uint32_t)step;
		}
	}
	osc->phase = phase;
	osc->phaseInc = inc;
}

/*
 * Render one wavetable block (S085 C8.2).
 *
 * What:       C8.1 frame-pair contract; 8-bit points sign-extended << 8;
 *             per-sample slew; silence still advances phase.
 * Accessors:  Oscillator.c type dispatch.
 */
/*
 * The plain/sync shared wtb loop (S085 assessment A2, ITCM).
 *
 * What:       oscWavetable_block() with a runtime lane: NULL for the ordinary plain
 *             render, an OscSyncLane for a hard-sync slave. One copy of the
 *             loop serves both, kept out of line (noinline, noclone) and with loop unswitching
 *             disabled so the compiler cannot duplicate it per lane value.
 * Why:        a separate sync copy of every renderer cost 5.6 KB of ITCM
 *             (16,272 of 16,384 B) and would have left S086 no room. The wtb
 *             loop is new in S085 (no pre-S085 codegen to preserve), so it
 *             takes the shared form; the wav loops keep separate sync copies
 *             to stay identical to the frozen pre-S085 code.
 * Cost:       the plain render pays one predictable compare-and-branch per
 *             sample (lane NULL); the sync render adds the oscSync_phase()
 *             select. Both figures are in C19.2.
 */
INITCM OSC_SHARED_LOOP
void oscWavetable_blockLane(struct OscStruct *osc, int16_t *buf, uint8_t size,
                            float gain, const OscSyncLane *lane)
{
	oscWavetable_block(osc, 0, buf, size, gain, lane);
}

INITCM void oscWavetable_render(struct OscStruct *osc, int16_t *buf,
                                uint8_t size, float gain)
{
	if (osc && buf)
		oscWavetable_blockLane(osc, buf, size, gain, 0);
}

/*
 * Render one wavetable block with FM (S085 C8.2).
 * Accessors: Oscillator.c type dispatch.
 */
INITCM void oscWavetable_renderFm(struct OscStruct *osc, int16_t *modBuffer,
                                  int16_t *buf, uint8_t size, float gain)
{
	if (osc && buf && modBuffer)
		oscWavetable_block(osc, modBuffer, buf, size, gain, 0);
}

/*
 * Render one wtb block as a hard-sync slave (S085 assessment A2).
 *
 * What:       oscWavetable_block() with the master's resets applied in the
 *             loop. The frame pair stays block-rate (D1); only the cycle
 *             phase resets.
 * Accessors:  osc_renderSyncBlock().
 */
INITCM void oscWavetable_renderSync(struct OscStruct *osc, int16_t *buf,
                                    uint8_t size, float gain,
                                    const struct OscSyncLane *lane)
{
	if (osc && buf && lane)
		oscWavetable_blockLane(osc, buf, size, gain, lane);
}
