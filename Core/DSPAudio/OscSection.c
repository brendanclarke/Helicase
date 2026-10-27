#include "OscSection.h"
#include "OscillatorSample.h"
#include "OscRenderPool.h"
#include "BufferTools.h"
#include "config.h"
#include <string.h>

#define OSC_FILTER_SLEW_COEF 0.28f

/*
 * OscSection.c (S085 C10.2).
 *
 * What:       run modes and combination modes, the carrier-only itp
 *             crossfade, snc and the filter slew.
 * Why:        one place for the oscillator-section rules keeps the four
 *             engines identical and makes fm/mix/FM2 S0-provable.
 * Accessors:  the four engines, InstrumentManager.c.
 * Affiliates: Oscillator.c, OscSample, OscRenderPool, ResonantFilter.
 */

/*
 * Initialise a section (S085 C10.2).
 * What: rst, idle, filter slew unseeded.  Accessors: engine init.
 */
void oscSection_init(OscSectionState* s)
{
	if (!s)
		return;
	memset(s, 0, sizeof(*s));
	s->runMode = OSC_RUN_RST;
	s->itpState = OSC_ITP_IDLE;
	s->fSmooth = -1.0f;
}

/*
 * Apply a trigger to the run mode (S085 C10.2).
 *
 * What:       rst -> reset all; fre -> arm the slew, reset nothing; itp ->
 *             snapshot the carrier, arm the modulators, return CARRIER and
 *             enter PENDING (or keep ACTIVE).
 * Why:        plan §4.3.7.
 * Accepted:   a retrigger while a crossfade is ACTIVE re-snapshots only the
 *             live carrier and restarts the fade at 0. The audible signal at
 *             that moment is old x (1 - t) + new x t, so the output steps by
 *             (1 - t) x (old - new). It needs triggers less than 3 ms apart
 *             (fast rolls, ratchets). This is S084 P1 F6, accepted in the S085
 *             plan (R11) and re-confirmed by the assessment (A8): an exact
 *             fix needs a second carrier copy per voice (+124 B RAM each).
 *             Hardware row H7 includes a fast roll in itp.
 * Accessors:  the engines' trigger functions.
 */
uint8_t oscSection_beginTrigger(OscSectionState* s, OscInfo* carrier,
                                OscInfo* const mods[], uint8_t modCount)
{
	uint8_t i;

	if (!s || !carrier)
		return OSC_TRIGGER_RESET_ALL;
	switch (s->runMode) {
	case OSC_RUN_FRE:
		osc_armSlew(carrier);
		for (i = 0u; i < modCount; i++)
			osc_armSlew(mods[i]);
		return OSC_TRIGGER_RESET_NONE;
	case OSC_RUN_ITP:
		s->itpOld = *carrier;
		s->itpOld.phaseIncStep = 0;
		s->itpOld.slewSamplesLeft = 0u;
		s->itpOld.waveInterpGeneration = 0u;  /* the copy never interpolates */
		for (i = 0u; i < modCount; i++)
			osc_armSlew(mods[i]);
		s->itpPos = 0u;
		if (s->itpState != OSC_ITP_ACTIVE)
			s->itpState = OSC_ITP_PENDING;
		return OSC_TRIGGER_RESET_CARRIER;
	case OSC_RUN_RST:
	default:
		return OSC_TRIGGER_RESET_ALL;
	}
}

/*
 * Claim an ongoing crossfade (S085 C10.2).
 * What: an ACTIVE state keeps one unit; a refused claim ends it.
 * Accessors: instrumentManager_claimOscSectionOngoing().
 */
void oscSection_claimOngoing(OscSectionState* s)
{
	if (s && s->itpState == OSC_ITP_ACTIVE && !oscRenderPool_claim(1u))
		s->itpState = OSC_ITP_IDLE;
}

/*
 * Claim a pending crossfade (S085 C10.2).
 * What: PENDING -> ACTIVE on a unit; refused restores the carrier phase and
 * arms the slew (fre fallback).
 * Accessors: instrumentManager_claimOscSectionPending().
 */
void oscSection_claimPending(OscSectionState* s, OscInfo* carrier)
{
	if (!s || s->itpState != OSC_ITP_PENDING)
		return;
	if (oscRenderPool_claim(1u)) {
		s->itpState = OSC_ITP_ACTIVE;
		return;
	}
	/* No free unit: behave as fre for this trigger. */
	if (carrier) {
		carrier->phase = s->itpOld.phase;
		osc_armSlew(carrier);
	}
	s->itpState = OSC_ITP_IDLE;
}

/*
 * The fading carrier copy (S085 C10.2).
 * What: &itpOld while ACTIVE, else NULL.  Accessors: the engines.
 */
OscInfo* oscSection_itpCarrier(OscSectionState* s)
{
	return (s && s->itpState == OSC_ITP_ACTIVE) ? &s->itpOld : 0;
}

/*
 * One master cycle in the master's phase units (S085 C10.2).
 *
 * What:       0 = a full 2^32 wrap (every table/sine/noise/pwm/wtb master);
 *             a sample master (smp/slp) spans (end - start) frames in Q20.12.
 */
static inline uint32_t oscSection_masterCycle(const OscInfo* m)
{
	if (m->oscType == OSC_TYPE_SMP || m->oscType == OSC_TYPE_SLP) {
		const uint32_t frames = (m->sampleEnd > m->sampleStart)
			? (m->sampleEnd - m->sampleStart) : 1u;

		return frames << OSC_SAMPLE_PHASE_SHIFT;
	}
	return 0u;
}

/*
 * Build one block's hard-sync lane (S085 C10.2; assessment A2).
 *
 * What:       walks a shadow of the master phase (cycle-relative; a sample
 *             master starts at phase - start, S084 P1 F5) and records, for
 *             each sample i, whether the master wraps between i and i + 1
 *             (wrap[i]) and its overshoot past the cycle (over[i]). A
 *             one-shot master that has played out records no wraps (Q10,
 *             S085 D6). Must run before the master block is rendered, since
 *             it reads the master's phase at block start.
 * Cost:       constant per sample: the add, the wrap compare, the overshoot
 *             (with the modulo for a sample master computed every sample,
 *             not only on a wrap) and two stores.
 * Accessors:  oscSection_renderSync(); armcheck-osc probes it.
 */
static inline __attribute__((always_inline)) void oscSection_buildSyncLane(
	const OscInfo* mod, uint8_t size, uint8_t* wrap, uint32_t* over)
{
	const uint32_t cycle = oscSection_masterCycle(mod);
	const uint8_t fullWrap = (uint8_t)(cycle == 0u);
	const uint32_t base =
		(mod->oscType == OSC_TYPE_SMP || mod->oscType == OSC_TYPE_SLP)
		? (mod->sampleStart << OSC_SAMPLE_PHASE_SHIFT) : 0u;
	const uint8_t live = (uint8_t)!oscSample_isFinished(mod);
	uint32_t phase = (mod->phase > base) ? (mod->phase - base) : 0u;
	uint32_t inc = mod->phaseInc;
	const int32_t step = mod->phaseIncStep;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const uint32_t next = phase + inc;
		/* A full-wrap master (cycle 0) never takes the modulo branch; the
		** test is a block constant. */
		const uint32_t o = fullWrap ? next : ((next - cycle) % cycle);
		const uint8_t w = (uint8_t)((fullWrap ? (next < phase)
		                                      : (next >= cycle)) & live);

		wrap[i] = w;
		over[i] = o;
		phase = w ? o : next;
		inc += (uint32_t)step;
	}
}

/*
 * Hard sync (S085 C10.2; constant-cost form, assessment A2).
 *
 * What:       builds the block's lane, renders the master at unit gain into
 *             modBuf, then renders the slave (and the itp copy, with its own
 *             ratio) in one call each through osc_renderSyncBlock(), which
 *             resets the slave inside its loop. The ratios are computed once
 *             per block (Q5). Output: slave x (1 - amt) + master x amt,
 *             saturated (Q10).
 * Why:        the first S085 form rendered the slave in segments, one
 *             renderer call per master wrap; at one wrap per sample (a sample
 *             master at a high note) the per-call overhead broke the C10.6 cap
 *             for every slave type. C10.6 step 4: one loop per slave family
 *             with the reset inside.
 * Cost:       per sample: the lane loop + the slave family's loop + the
 *             oscSync_phase() select + the mix pass, independent of the wrap
 *             count; the itp copy adds one more slave loop. Figures in C19.2.
 * Stack:      wrap[32] + over[32] = 160 B on the audio stack (no static RAM).
 */
static void oscSection_renderSync(OscInfo* car, OscInfo* mod, float amount,
                                  int16_t* buf, int16_t* modBuf, uint8_t size,
                                  OscInfo* oldCar, int16_t* oldBuf)
{
	uint8_t wrap[OUTPUT_DMA_SIZE];
	uint32_t over[OUTPUT_DMA_SIZE];
	OscSyncLane lane;
	uint8_t i;

	if (size > OUTPUT_DMA_SIZE)
		size = OUTPUT_DMA_SIZE;
	lane.wrap = wrap;
	lane.over = over;
	lane.ratio = mod->phaseInc
		? (float)car->phaseInc / (float)mod->phaseInc : 0.0f;
	oscSection_buildSyncLane(mod, size, wrap, over);
	calcNextOscSampleBlock(mod, modBuf, size, 1.0f);
	osc_renderSyncBlock(car, buf, size, 1.0f, &lane);
	if (oldCar) {
		lane.ratio = mod->phaseInc
			? (float)oldCar->phaseInc / (float)mod->phaseInc : 0.0f;
		osc_renderSyncBlock(oldCar, oldBuf, size, 1.0f, &lane);
	}
	for (i = 0u; i < size; i++) {
		buf[i] = (int16_t)__SSAT((int32_t)((float)buf[i] * (1.0f - amount) +
		                                   (float)modBuf[i] * amount), 16);
		if (oldCar)
			oldBuf[i] = (int16_t)__SSAT((int32_t)((float)oldBuf[i] *
				(1.0f - amount) + (float)modBuf[i] * amount), 16);
	}
}

/* Ring: wet = (car x mod) >> 15; out = car x (1 - amt) + wet x amt. */
static inline void oscSection_ring(int16_t* out, const int16_t* modBuf,
                                   float amount, uint8_t size)
{
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const int32_t ring = ((int32_t)out[i] * (int32_t)modBuf[i]) >> 15;

		out[i] = (int16_t)__SSAT((int32_t)((float)out[i] * (1.0f - amount)) +
		                         (int32_t)((float)ring * amount), 16);
	}
}

/*
 * FM depth ramp on the modulator block (S085 P2 F-A).
 *
 * What:       modBuf[i] *= from + (i / (size - 1)) x (1 - from), truncated
 *             to int16: the block's modulation scales from `from` to 1.
 * Why:        the carrier reads phase + (mod x fmMod); scaling mod by
 *             fmMod_old / fmMod_new at sample 0 and 1 at the last sample
 *             moves the effective FM depth from the previous block's value
 *             to the new one over one block instead of at the boundary.
 *             With from = 1 every factor is exactly 1.0, so the block is
 *             unchanged (bit-identical; osc_s0 runs this path flat).
 * Inputs:     modBuf, from (0..1; callers clamp), size.
 * Outputs:    modBuf scaled in place.
 * Cost:       constant per sample in fm mode, whatever `from` is; measured
 *             in §13 of S085_P2_OSC_RESET_FOLLOWUP.md.
 * Accessors:  oscSection_renderDrumRamped() (fm branch).
 */
static inline void oscSection_rampModDepth(int16_t* modBuf, float from,
                                           uint8_t size)
{
	const float inv = (size > 1u) ? 1.0f / (float)(size - 1u) : 0.0f;
	const float span = 1.0f - from;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const float g = from + ((float)i * inv) * span;

		modBuf[i] = (int16_t)(int32_t)((float)modBuf[i] * g);
	}
}

/*
 * Render the Drum combination (S085 C10.2; FM depth ramp S085 P2 F-A).
 *
 * What:       fm/mix/snc/rng, with the old carrier rendered against the same
 *             live modulator block when oldCar is set. In fm mode the
 *             modulator block is scaled by oscSection_rampModDepth() from
 *             fmRampFrom to 1 before the carrier reads it; fmRampFrom = 1
 *             leaves it unchanged. With oldCar set the ramp is forced flat:
 *             the old carrier keeps its own (old) fmMod and must see the
 *             unscaled block, and the itp crossfade already hides the new
 *             carrier's depth jump.
 * Why:        fm and mix with no crossfade and a flat ramp call the pre-S085
 *             loops in the same order on the same data, so the output is
 *             bit-identical (osc_s0). The ramp removes the Drum fre
 *             retrigger click: Drum's FM depth follows the pitch envelope,
 *             which DecayEg_trigger() restarts at 1.0, so fmMod jumps at
 *             the retrigger and, because the FM is phase modulation, the
 *             carrier's read phase jumps by mod x (fmMod_new - fmMod_old).
 * Inputs:     car, mod, mode, amount, buf, modBuf, size, oldCar, oldBuf,
 *             fmRampFrom (fmMod_old / fmMod_new clamped to 0..1 on the armed
 *             retrigger block, else 1).
 * Outputs:    buf (and oldBuf); modBuf holds the (ramped) modulator block.
 * Accessors:  Drum_calcVoiceSyncBlock(); oscSection_renderDrum() (flat).
 * Affiliates: DrumVoice lastFmMod/ampRampArm, Drum_calcVoiceAsync() (sets
 *             fmMod from the pitch envelope).
 */
INITCM void oscSection_renderDrumRamped(OscInfo* car, OscInfo* mod,
                                        uint8_t mode, float amount,
                                        int16_t* buf, int16_t* modBuf,
                                        uint8_t size, OscInfo* oldCar,
                                        int16_t* oldBuf, float fmRampFrom)
{
	if (!car || !mod || !buf || !modBuf)
		return;
	if (!oldBuf)
		oldCar = 0;
	if (oldCar)
		fmRampFrom = 1.0f;
	switch (mode) {
	case OSC_MOD_MIX:
		calcNextOscSampleBlock(mod, modBuf, size, amount);
		calcNextOscSampleBlock(car, buf, size, (1.0f - amount));
		if (oldCar) {
			calcNextOscSampleBlock(oldCar, oldBuf, size, (1.0f - amount));
			bufferTool_addBuffersSaturating(oldBuf, modBuf, size);
		}
		bufferTool_addBuffersSaturating(buf, modBuf, size);
		break;
	case OSC_MOD_SNC:
		oscSection_renderSync(car, mod, amount, buf, modBuf, size,
		                      oldCar, oldBuf);
		break;
	case OSC_MOD_RNG:
		calcNextOscSampleBlock(mod, modBuf, size, 1.0f);
		calcNextOscSampleBlock(car, buf, size, 1.0f);
		oscSection_ring(buf, modBuf, amount, size);
		if (oldCar) {
			calcNextOscSampleBlock(oldCar, oldBuf, size, 1.0f);
			oscSection_ring(oldBuf, modBuf, amount, size);
		}
		break;
	case OSC_MOD_FM:
	default:
		calcNextOscSampleBlock(mod, modBuf, size, amount);
		/* S085 P2 F-A: runs every fm block (flat when fmRampFrom = 1). */
		oscSection_rampModDepth(modBuf, fmRampFrom, size);
		calcNextOscSampleFmBlock(car, modBuf, buf, size, 1.0f);
		if (oldCar)
			calcNextOscSampleFmBlock(oldCar, modBuf, oldBuf, size, 1.0f);
		break;
	}
}

/*
 * Render the Drum combination with no FM depth ramp (S085 C10.2).
 *
 * What:       oscSection_renderDrumRamped() with fmRampFrom = 1.
 * Why:        the flat form for callers with no retrigger state (the host
 *             tests and the S0 proof).
 * Accessors:  tools/dsp_test osc_run, osc_s0.
 */
void oscSection_renderDrum(OscInfo* car, OscInfo* mod, uint8_t mode,
                           float amount, int16_t* buf, int16_t* modBuf,
                           uint8_t size, OscInfo* oldCar, int16_t* oldBuf)
{
	oscSection_renderDrumRamped(car, mod, mode, amount, buf, modBuf, size,
	                            oldCar, oldBuf, 1.0f);
}

/*
 * Render the Cymbal/HiHat two-modulator FM section (S085 C10.2).
 * What: two modulator blocks, saturating sum, carrier FM, optional old
 * carrier with the same sum.  Accessors: Cymbal/HiHat render.
 */
INITCM void oscSection_renderFm2(OscInfo* car, OscInfo* m1, OscInfo* m2,
                                 float g1, float g2, float carGain,
                                 int16_t* buf, int16_t* modBuf,
                                 int16_t* modBuf2, uint8_t size,
                                 OscInfo* oldCar, int16_t* oldBuf)
{
	if (!car || !m1 || !m2 || !buf || !modBuf || !modBuf2)
		return;
	calcNextOscSampleBlock(m1, modBuf, size, g1);
	calcNextOscSampleBlock(m2, modBuf2, size, g2);
	bufferTool_addBuffersSaturating(modBuf, modBuf2, size);
	calcNextOscSampleFmBlock(car, modBuf, buf, size, carGain);
	if (oldCar && oldBuf)
		calcNextOscSampleFmBlock(oldCar, modBuf, oldBuf, size, carGain);
}

/*
 * Crossfade the old carrier into the new block (S085 C10.2).
 * What: linear blend over OSC_ITP_SAMPLES while ACTIVE.
 * Accessors: the engines after the section render.
 */
void oscSection_blendItp(OscSectionState* s, int16_t* buf,
                         const int16_t* oldBuf, uint8_t size)
{
	uint8_t i;

	if (!s || !buf || !oldBuf || s->itpState != OSC_ITP_ACTIVE)
		return;
	for (i = 0u; i < size; i++) {
		float t = (float)(s->itpPos + i) * (1.0f / (float)OSC_ITP_SAMPLES);

		if (t > 1.0f)
			t = 1.0f;
		buf[i] = (int16_t)((float)oldBuf[i] +
		                   t * ((float)buf[i] - (float)oldBuf[i]));
	}
}

/*
 * Advance or end the itp crossfade (S085 C10.2).
 * Accessors: the engines' render functions.
 */
void oscSection_finishBlock(OscSectionState* s, uint8_t size)
{
	if (!s || s->itpState != OSC_ITP_ACTIVE)
		return;
	s->itpPos = (uint16_t)(s->itpPos + size);
	if (s->itpPos >= OSC_ITP_SAMPLES)
		s->itpState = OSC_ITP_IDLE;
}

/*
 * Record the resonance target for fre/itp (S085 C10.2).
 * Accessors: the IM_SPECIAL_FILTER_RESO writer.
 */
void oscSection_noteResoTarget(OscSectionState* s, float q)
{
	if (s)
		s->qTarget = q;
}

/*
 * Filter recalc through the section (S085 C10.2).
 *
 * What:       rst is exactly SVF_recalcFreq(); fre/itp slew cutoff and
 *             resonance. The float q compare is valid because the write-back
 *             is a plain copy; one compare per slot per block.
 * Accessors:  instrumentManager_recalcSlotFilter().
 */
void oscSection_filterRecalc(OscSectionState* s, ResonantFilter* f)
{
	if (!s || !f)
		return;
	if (s->runMode == OSC_RUN_RST) {
		SVF_recalcFreq(f);
		s->fSmooth = f->f;
		s->qSmooth = f->q;
		s->qTarget = f->q;
		return;
	}
	if (s->fSmooth < 0.0f) {
		s->fSmooth = f->f;
		s->qSmooth = f->q;
		s->qTarget = f->q;
	}
	if (f->q != s->qSmooth)       /* modulation wrote q since the last block */
		s->qTarget = f->q;
	s->fSmooth += (f->f - s->fSmooth) * OSC_FILTER_SLEW_COEF;
	s->qSmooth += (s->qTarget - s->qSmooth) * OSC_FILTER_SLEW_COEF;
	f->q = s->qSmooth;
	SVF_recalcFreqFrom(f, s->fSmooth);
}
