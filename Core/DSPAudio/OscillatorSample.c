#include "Oscillator.h"
#include "OscillatorSample.h"
#include "SampleMemory.h"
#include "config.h"
#include "OscSyncLane.h"

#define OSC_SAMPLE_FRAC_MASK  ((1u << OSC_SAMPLE_PHASE_SHIFT) - 1u)
#define OSC_SAMPLE_FRAC_SCALE (1.0f / (float)(1u << OSC_SAMPLE_PHASE_SHIFT))

/*
 * Bind the selected sample (S085 C6.2).
 *
 * What:       live index = min(waveform, count - 1), where waveform (the wav
 *             row) is re-expanded from waveRequest when the catalogue
 *             generation changed; refreshes the cached pointer, frames and
 *             gain code when the index or generation changed; recomputes
 *             start = (source x frames) >> 7 (source = the mod row) and
 *             end = frames.
 * Why:        a smaller reinstall never moves the stored selection and a
 *             reinstall never leaves a dangling pointer; a compare per block
 *             when nothing changed.
 * Inputs:     osc.  Outputs: waveform, sampleCache*, sampleGainCode,
 *             sampleStart/End.
 * Accessors:  oscSample_render*(),
 *             instrumentManager_writeSpecialRuntime().
 * Affiliates: sampleMemory_getSampleInfo()/getGeneration().
 */
void oscSample_bind(struct OscStruct* osc)
{
	uint8_t count;
	uint8_t index;
	uint32_t generation;

	if (!osc || (osc->oscType != OSC_TYPE_SMP && osc->oscType != OSC_TYPE_SLP))
		return;
	count = sampleMemory_getNumSamples();
	if (count == 0u) {
		osc->waveform = 0u;
		osc->sampleCacheData = 0;
		osc->sampleCacheSize = 0u;
		osc->sampleStart = 0u;
		osc->sampleEnd = 0u;
		osc->sampleGainCode = 0u;
		return;
	}
	generation = sampleMemory_getGeneration();
	/* A reinstall re-expands the selection from the stored request. */
	if (osc->sampleCacheGeneration != generation)
		osc->waveform = osc->waveRequest;
	index = (osc->waveform >= count) ? (uint8_t)(count - 1u) : osc->waveform;
	osc->waveform = index;
	if (!osc->sampleCacheData || osc->sampleCacheIndex != index ||
	    osc->sampleCacheGeneration != generation ||
	    osc->sampleCacheSize == 0u) {
		const SampleInfo info = sampleMemory_getSampleInfo(index);

		osc->sampleCacheIndex = index;
		osc->sampleCacheGeneration = generation;
		osc->sampleCacheSize = info.size;
		osc->sampleCacheData = (const int16_t*)SAMPLE_FLASH_PTR(info.offset);
		osc->sampleGainCode = info.gain_code;
	}
	osc->sampleStart = ((uint32_t)osc->source * osc->sampleCacheSize) >> 7;
	if (osc->sampleStart >= osc->sampleCacheSize)
		osc->sampleStart = 0u;
	osc->sampleEnd = osc->sampleCacheSize;
}

/*
 * Restart at the start position (S085 C6.2).
 * Accessors: engine triggers, OscSection snc.
 */
void oscSample_restart(struct OscStruct* osc)
{
	if (osc)
		osc->phase = osc->sampleStart << OSC_SAMPLE_PHASE_SHIFT;
}

/*
 * Whether a one-shot sample has played out (S085 C6.2).
 *
 * What:       1 for smp whose read index reached end - 1; 0 for slp/others.
 * Why:        Q10 snc rule and the 0 output after the end.
 * Accessors:  OscSection snc, oscSample_render*().
 */
uint8_t oscSample_isFinished(const struct OscStruct* osc)
{
	return (uint8_t)(osc && osc->oscType == OSC_TYPE_SMP &&
	                 osc->sampleEnd != 0u &&
	                 (osc->phase >> OSC_SAMPLE_PHASE_SHIFT) + 1u >=
	                     osc->sampleEnd);
}

/*
 * Phase increment for a playback frequency (S085 C6.2).
 * What: f x 2^22 / REAL_FS (Q20.12).  Accessors: osc_setFreq().
 */
uint32_t oscSample_phaseIncForFreq(float f)
{
	return (uint32_t)((f * 4194304.0f) / REAL_FS);
}

/*
 * One block, shared by both entry points (S085 C6.2).
 *
 * What:       the C6.1 render contract; modBuffer NULL = no FM.
 * Why:        one body keeps the two loops identical; the FM test is
 *             hoisted by the compiler (two inlined copies, ITCM).
 */
OSC_SYNC_INLINE void oscSample_block(struct OscStruct* osc,
                                     const int16_t* modBuffer, int16_t* buf,
                                     uint8_t size, float gain,
                                     const OscSyncLane* lane)
{
	const int16_t* data;
	uint32_t start;
	uint32_t end;
	uint32_t phase;
	uint32_t inc;
	int32_t step;
	uint8_t looped;
	uint8_t i;

	oscSample_bind(osc);
	data = osc->sampleCacheData;
	if (!data || osc->sampleCacheSize == 0u) {
		for (i = 0u; i < size; i++)
			buf[i] = 0;
		return;
	}
	start = osc->sampleStart;
	end = osc->sampleEnd;
	gain *= sampleMemory_gainFromCode(osc->sampleGainCode);
	looped = (uint8_t)(osc->oscType == OSC_TYPE_SLP);
	phase = osc->phase;
	inc = osc->phaseInc;
	step = osc->phaseIncStep;
	for (i = 0u; i < size; i++) {
		const uint32_t readPhase = modBuffer
			? phase + (uint32_t)((int32_t)((float)modBuffer[i] * osc->fmMod) << 9)
			: phase;
		uint32_t index = readPhase >> OSC_SAMPLE_PHASE_SHIFT;
		uint32_t nextIndex;
		uint8_t done;
		int32_t a;
		int32_t out;

		if (index < start)
			index = start;
		if (index >= end) {
			if (looped) {
				uint32_t rel = index - start;
				const uint32_t len = end - start;

				if (rel >= len)
					rel -= len;
				if (rel >= len)
					rel %= len;          /* only for deep FM jumps */
				index = start + rel;
			} else {
				index = end - 1u;
			}
		}
		done = (uint8_t)(!looped && index + 1u >= end);
		nextIndex = (index + 1u >= end) ? (looped ? start : index) : index + 1u;
		a = data[index];
		out = a + (int32_t)((float)(readPhase & OSC_SAMPLE_FRAC_MASK) *
		                    OSC_SAMPLE_FRAC_SCALE *
		                    (float)((int32_t)data[nextIndex] - a));
		/* Q10: smp outputs 0 once played out (a select, constant cost). */
		buf[i] = (int16_t)((float)out * (done ? 0.0f : gain));
		/* S085 C5.6: per-sample slew. */
		if (looped) {
			uint32_t next = phase + inc;

			if (next >= (end << OSC_SAMPLE_PHASE_SHIFT))
				next -= (end - start) << OSC_SAMPLE_PHASE_SHIFT;
			phase = next;
		} else if (!done && (phase >> OSC_SAMPLE_PHASE_SHIFT) + 1u < end) {
			phase += inc;
		} else {
			phase = (end - 1u) << OSC_SAMPLE_PHASE_SHIFT;
		}
		/* Assessment A2: a hard-sync reset restarts at start + overshoot. */
		if (lane)
			phase = oscSync_phase(lane, i, phase,
			                      start << OSC_SAMPLE_PHASE_SHIFT);
		inc += (uint32_t)step;
	}
	osc->phase = phase;
	osc->phaseInc = inc;
}

/*
 * Render one sample block (S085 C6.2).
 *
 * What:       index clamp, loop wrap or end hold, two reads, interpolation,
 *             gain and the per-sample slew.
 * Why:        constant cost per sample; the wrap/end tests always run.
 * Inputs:     osc, buf, size, gain.  Outputs: buf and phase state.
 * Accessors:  Oscillator.c type dispatch.
 */
/*
 * The plain/sync shared smp/slp loop (S085 assessment A2, ITCM).
 *
 * What:       oscSample_block() with a runtime lane: NULL for the ordinary plain
 *             render, an OscSyncLane for a hard-sync slave. One copy of the
 *             loop serves both, kept out of line (noinline, noclone) and with loop unswitching
 *             disabled so the compiler cannot duplicate it per lane value.
 * Why:        a separate sync copy of every renderer cost 5.6 KB of ITCM
 *             (16,272 of 16,384 B) and would have left S086 no room. The smp/slp
 *             loop is new in S085 (no pre-S085 codegen to preserve), so it
 *             takes the shared form; the wav loops keep separate sync copies
 *             to stay identical to the frozen pre-S085 code.
 * Cost:       the plain render pays one predictable compare-and-branch per
 *             sample (lane NULL); the sync render adds the oscSync_phase()
 *             select. Both figures are in C19.2.
 */
INITCM OSC_SHARED_LOOP
void oscSample_blockLane(struct OscStruct* osc, int16_t* buf, uint8_t size,
                         float gain, const OscSyncLane* lane)
{
	oscSample_block(osc, 0, buf, size, gain, lane);
}

INITCM void oscSample_render(struct OscStruct* osc, int16_t* buf,
                             uint8_t size, float gain)
{
	if (osc && buf)
		oscSample_blockLane(osc, buf, size, gain, 0);
}

/*
 * Render one sample block with FM (S085 C6.2).
 * What: as oscSample_render() with (mod x fmMod) << 9 added to the read
 * phase.  Accessors: Oscillator.c type dispatch.
 */
INITCM void oscSample_renderFm(struct OscStruct* osc, int16_t* modBuffer,
                               int16_t* buf, uint8_t size, float gain)
{
	if (osc && buf && modBuffer)
		oscSample_block(osc, modBuffer, buf, size, gain, 0);
}

/*
 * Render one smp/slp block as a hard-sync slave (S085 assessment A2).
 *
 * What:       oscSample_block() with the master's resets applied in the
 *             loop: a reset restarts at sampleStart + overshoot x ratio, so a
 *             finished one-shot slave plays again from its start on the next
 *             master wrap (that is what hard sync of a one-shot means).
 * Accessors:  osc_renderSyncBlock().
 */
INITCM void oscSample_renderSync(struct OscStruct* osc, int16_t* buf,
                                 uint8_t size, float gain,
                                 const struct OscSyncLane* lane)
{
	if (osc && buf && lane)
		oscSample_blockLane(osc, buf, size, gain, lane);
}
