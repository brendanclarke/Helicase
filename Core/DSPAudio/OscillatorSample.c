/*
 * OscillatorSample.c
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
#include "OscillatorSample.h"
#include "SampleMemory.h"
#include "config.h"

/*
 * Bind the selected installed sample to an oscillator (S084 P1 P1-4.1).
 *
 * Implementation note: re-homed from Oscillator.c osc_updateSampleCache()
 * (S084 P1); the type selects looping, `wav` selects the start position.
 *
 * What:       refreshes the cached pointer and size for `source` when the
 *             catalogue generation, the source or the type changed; computes
 *             sampleStart = (waveform x size) >> 7 and sampleEnd = size;
 *             derives `source` = min(sourceRequest, installed count - 1)
 *             (0 samples -> silent, source 0); copies the catalogue gain code
 *             for the block-rate normalisation (S084 VX2 VX2-1.4).
 * Why:        the start position is sweepable on `wav` and the v1 loop flag is
 *             no longer read (the type is the loop choice).
 *             S084 P1 review fix F2: the index is taken from sourceRequest
 *             (the unclamped `src` byte), not from the previously clamped
 *             `source`. A reinstall that temporarily has fewer samples
 *             therefore no longer moves the selection permanently: once the
 *             count is large enough again, the stored sample comes back.
 *             While sourceRequest is within the count this is identical to
 *             the previous behaviour (sourceRequest == source).
 * Inputs:     osc (oscType, sourceRequest, waveform); SampleMemory catalogue
 *             v2 (S084 VX2).
 * Outputs:    sampleCache*, sampleGainCode, sampleStart, sampleEnd, source.
 * Accessors:  InstrumentManager writers, renderers' first-block check.
 * Affiliates: SampleMemory.c, oscSample_restart().
 */
void oscSample_bind(struct OscStruct* osc)
{
	uint8_t count;
	uint8_t index;
	uint32_t generation;

	if (!osc)
		return;
	if (osc->oscType != OSC_TYPE_SMP && osc->oscType != OSC_TYPE_SLP)
		return;
	count = sampleMemory_getNumSamples();
	if (count == 0u) {
		osc->source = 0u;
		osc->sampleCacheData = 0;
		osc->sampleCacheSize = 0u;
		osc->sampleStart = 0u;
		osc->sampleEnd = 0u;
		osc->sampleGainCode = 0u;
		return;
	}
	index = osc->sourceRequest;
	if (index >= count)
		index = (uint8_t)(count - 1u);
	osc->source = index;
	generation = sampleMemory_getGeneration();
	if (!osc->sampleCacheData ||
		osc->sampleCacheIndex != index ||
		osc->sampleCacheGeneration != generation ||
		osc->sampleCacheSize == 0u) {
		SampleInfo info = sampleMemory_getSampleInfo(index);
		osc->sampleCacheIndex = index;
		osc->sampleCacheGeneration = generation;
		osc->sampleCacheSize = info.size;
		osc->sampleCacheData =
			(const int16_t*)SAMPLE_FLASH_PTR(info.offset);
		osc->sampleGainCode = info.gain_code;
	}
	osc->sampleStart =
		((uint32_t)osc->waveform * osc->sampleCacheSize) >> 7;
	if (osc->sampleStart >= osc->sampleCacheSize)
		osc->sampleStart = 0u;
	osc->sampleEnd = osc->sampleCacheSize;
}

/*
 * Restart a sample oscillator at its start position (S084 P1 P1-4.1).
 *
 * What:       phase = sampleStart << OSC_SAMPLE_PHASE_SHIFT.
 * Why:        trigger and sync resets start from the configured start.
 * Inputs:     osc.  Outputs: osc->phase.
 * Accessors:  engines' trigger paths, OscSection.c sync.
 * Affiliates: oscSample_bind().
 */
void oscSample_restart(struct OscStruct* osc)
{
	if (!osc)
		return;
	osc->phase = osc->sampleStart << OSC_SAMPLE_PHASE_SHIFT;
}

/*
 * Phase increment for a playback frequency (S084 P1 P1-4.1).
 *
 * What:       f x 2^22 / REAL_FS (Q20.12 index plus 12 fraction bits).
 * Why:        every installable sample plays fully; the ratio equals the old
 *             freq2PhaseIncr32767() (1,024 x f / FS frames per sample).
 * Inputs:     f in Hz.  Outputs: phase increment.
 * Accessors:  osc_setFreq().
 * Affiliates: oscSample_render().
 */
uint32_t oscSample_phaseIncForFreq(float f)
{
	return (uint32_t)(((f * 4194304.0f) / REAL_FS));
}

/*
 * Render one installed-sample block, one-shot or looped (S084 P1 P1-4.1).
 *
 * Implementation note: re-homed from Oscillator.c
 * calcUserSampleOscBlock() (S084 P1); Q20.12 and type-selected looping.
 *
 * What:       two reads plus interpolation per sample; the loop wrap is always
 *             evaluated; `slp` loops [sampleStart, sampleEnd), `smp` holds the
 *             last frame. The per-sample pitch slew (P1-3.7) runs always.
 * Why:        plan §4.1.5: long samples play fully; loop is per oscillator.
 * Inputs:     osc, buf, size, gain.
 * Outputs:    buf; osc->phase and phaseInc advanced.
 * Accessors:  Oscillator.c type dispatch.
 * Affiliates: oscSample_bind(), oscSample_restart().
 */
INITCM void oscSample_render(struct OscStruct* osc, int16_t* buf,
                             uint8_t size, float gain)
{
	const int16_t* data;
	uint32_t frames;
	uint32_t start;
	uint32_t end;
	uint32_t phase;
	uint32_t inc;
	int32_t step;
	uint8_t looped;
	uint8_t i;

	if (!osc || !buf)
		return;
	/* Refresh on a catalogue generation change as well as a first block. */
	oscSample_bind(osc);
	data = osc->sampleCacheData;
	frames = osc->sampleCacheSize;
	if (!data || frames == 0u) {
		for (i = 0u; i < size; i++)
			buf[i] = 0;
		return;
	}
	start = osc->sampleStart;
	end = (osc->sampleEnd > start && osc->sampleEnd <= frames)
		? osc->sampleEnd : frames;
	/*
	 * Catalogue gain on sample playback (S084 VX2 VX2-1.4).
	 *
	 * What:       the block gain is multiplied once by 2^(code/24), the
	 *             catalogue's peak-normalisation gain for this sample.
	 * Why:        Q15: matched sample levels everywhere a sample plays. One
	 *             multiply per block; the per-sample loop is unchanged.
	 * Inputs:     osc->sampleGainCode (set by oscSample_bind()).
	 * Outputs:    the gain applied in the existing per-sample multiply.
	 * Accessors:  Oscillator.c dispatch.
	 * Affiliates: sampleMemory_gainFromCode(), SampleInstall.c peak tracking.
	 */
	gain *= sampleMemory_gainFromCode(osc->sampleGainCode);
	looped = (uint8_t)(osc->oscType == OSC_TYPE_SLP);
	phase = osc->phase;
	inc = osc->phaseInc;
	step = osc->phaseIncStep;

	for (i = 0u; i < size; i++) {
		uint32_t index = phase >> OSC_SAMPLE_PHASE_SHIFT;
		const uint32_t fracBits =
			phase & ((1u << OSC_SAMPLE_PHASE_SHIFT) - 1u);
		uint32_t nextIndex;
		int32_t a;
		int32_t b;
		int32_t out;

		if (index < start)
			index = start;
		if (index >= end) {
			if (looped) {
				uint32_t len = end - start;
				uint32_t rel = index - start;

				if (rel >= len)
					rel -= len;
				if (rel >= len)
					rel %= len;
				index = start + rel;
			} else {
				index = end - 1u;
			}
		}
		a = (int32_t)data[index];
		nextIndex = index + 1u;
		if (nextIndex >= end)
			nextIndex = looped ? start : (end - 1u);
		b = (int32_t)data[nextIndex];
		out = a + (int32_t)(
			(float)fracBits * (1.0f / (float)(1u << OSC_SAMPLE_PHASE_SHIFT)) *
			(float)(b - a));
		buf[i] = (int16_t)(out * gain);
		/* S084 P1 P1-3.7: per-sample slew, see calcSineBlock(). */
		if (looped) {
			const uint32_t loopEnd = end << OSC_SAMPLE_PHASE_SHIFT;
			const uint32_t loopLen =
				(end - start) << OSC_SAMPLE_PHASE_SHIFT;
			uint32_t nextPhase = phase + inc;

			if (nextPhase >= loopEnd)
				nextPhase -= loopLen;
			phase = nextPhase;
		} else if (index + 1u < end) {
			phase += inc;
		} else {
			phase = (end - 1u) << OSC_SAMPLE_PHASE_SHIFT;
		}
		inc += (uint32_t)step;
	}
	osc->phase = phase;
	osc->phaseInc = inc;
}

/*
 * Render one installed-sample block with FM (S084 P1 P1-4.1).
 *
 * Implementation note: re-homed from Oscillator.c
 * calcUserSampleOscFmBlock() (S084 P1); Q20.12 and type-selected looping.
 *
 * What:       oscSample_render with the modulator offset (mod x fmMod) << 9 in
 *             Q20.12, equal depth to the old << 14 at >> 17.
 * Why:        FM through an installed sample keeps its previous depth.
 * Inputs:     osc, modBuffer, buf, size, gain.
 * Outputs:    buf; phase/phaseInc advanced.
 * Accessors:  Oscillator.c type dispatch.
 * Affiliates: oscSample_render().
 */
INITCM void oscSample_renderFm(struct OscStruct* osc, int16_t* modBuffer,
                               int16_t* buf, uint8_t size, float gain)
{
	const int16_t* data;
	uint32_t frames;
	uint32_t start;
	uint32_t end;
	uint32_t phase;
	uint32_t inc;
	int32_t step;
	uint8_t looped;
	uint8_t i;

	if (!osc || !buf || !modBuffer)
		return;
	/* Refresh on a catalogue generation change as well as a first block. */
	oscSample_bind(osc);
	data = osc->sampleCacheData;
	frames = osc->sampleCacheSize;
	if (!data || frames == 0u) {
		for (i = 0u; i < size; i++)
			buf[i] = 0;
		return;
	}
	start = osc->sampleStart;
	end = (osc->sampleEnd > start && osc->sampleEnd <= frames)
		? osc->sampleEnd : frames;
	/* S084 VX2 VX2-1.4: catalogue gain, see oscSample_render(). */
	gain *= sampleMemory_gainFromCode(osc->sampleGainCode);
	looped = (uint8_t)(osc->oscType == OSC_TYPE_SLP);
	phase = osc->phase;
	inc = osc->phaseInc;
	step = osc->phaseIncStep;

	for (i = 0u; i < size; i++) {
		const uint32_t fmOffset =
			(uint32_t)((int32_t)((float)modBuffer[i] * osc->fmMod) << 9);
		const uint32_t modPhase = phase + fmOffset;
		uint32_t index = modPhase >> OSC_SAMPLE_PHASE_SHIFT;
		const uint32_t fracBits =
			modPhase & ((1u << OSC_SAMPLE_PHASE_SHIFT) - 1u);
		uint32_t nextIndex;
		int32_t a;
		int32_t b;
		int32_t out;

		if (index < start)
			index = start;
		if (index >= end) {
			if (looped) {
				uint32_t len = end - start;
				uint32_t rel = index - start;

				if (rel >= len)
					rel -= len;
				if (rel >= len)
					rel %= len;
				index = start + rel;
			} else {
				index = end - 1u;
			}
		}
		a = (int32_t)data[index];
		nextIndex = index + 1u;
		if (nextIndex >= end)
			nextIndex = looped ? start : (end - 1u);
		b = (int32_t)data[nextIndex];
		out = a + (int32_t)(
			(float)fracBits * (1.0f / (float)(1u << OSC_SAMPLE_PHASE_SHIFT)) *
			(float)(b - a));
		buf[i] = (int16_t)(out * gain);
		/* S084 P1 P1-3.7: per-sample slew, see calcSineBlock(). */
		if (looped) {
			const uint32_t loopEnd = end << OSC_SAMPLE_PHASE_SHIFT;
			const uint32_t loopLen =
				(end - start) << OSC_SAMPLE_PHASE_SHIFT;
			uint32_t nextPhase = phase + inc;

			if (nextPhase >= loopEnd)
				nextPhase -= loopLen;
			phase = nextPhase;
		} else if ((phase >> OSC_SAMPLE_PHASE_SHIFT) + 1u < end) {
			phase += inc;
		} else {
			phase = (end - 1u) << OSC_SAMPLE_PHASE_SHIFT;
		}
		inc += (uint32_t)step;
	}
	osc->phase = phase;
	osc->phaseInc = inc;
}
