/*
 * OscSection.c
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

#include "OscSection.h"
#include "Oscillator.h"
#include "OscillatorSample.h"
#include "OscRenderPool.h"
#include "ResonantFilter.h"
#include "BufferTools.h"
#include "config.h"
#include <string.h>

/*
 * Initialize one voice's oscillator section (S084 P1 P1-7.1).
 *
 * What:       run mode rst, no crossfade, oscCount for this engine; the filter
 *             slew starts from the filter's values at the first recalc
 *             (fSmooth < 0 is the "not yet seeded" marker).
 * Why:        a fresh or reset slot starts in today's behaviour until its
 *             `run` row is applied.
 * Inputs:     section, oscillator count.
 * Outputs:    section fields.
 * Accessors:  each engine's *_initVoice().
 * Affiliates: oscSection_filterRecalc(), IM_SPECIAL_RUN_MODE writer.
 */
void oscSection_init(OscSectionState* s, uint8_t oscCount)
{
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
    s->runMode = OSC_RUN_RST;
    s->itpState = OSC_ITP_IDLE;
    s->itpUnits = 0u;
    s->oscCount = (oscCount > OSC_SECTION_MAX_OSCS)
        ? OSC_SECTION_MAX_OSCS : oscCount;
    s->fSmooth = -1.0f;
    s->qSmooth = 0.0f;
    s->qTarget = 0.0f;
}

/*
 * Apply a trigger to the run mode (S084 P1 P1-7.1).
 *
 * What:       RST returns 1 (the engine performs today's reset); FRE arms the
 *             pitch slew on every oscillator and returns 0; ITP snapshots every
 *             oscillator into itpOld[] with its pitch frozen and returns 1.
 * Why:        plan §4.1.6: the run mode decides whether the reset happens.
 * Inputs:     section, oscillator pointer array.
 * Outputs:    1 = reset the live oscillators, 0 = keep them.
 * Accessors:  the engines' *_triggerVoice().
 * Affiliates: osc_armSlew(), oscSection_claimPending().
 */
uint8_t oscSection_beginTrigger(OscSectionState* s, OscInfo* const oscs[])
{
    uint8_t i;

    if (!s)
        return 1u;
    switch (s->runMode) {
    case OSC_RUN_FRE:
        for (i = 0u; i < s->oscCount; i++) {
            if (oscs[i])
                osc_armSlew(oscs[i]);
        }
        return 0u;
    case OSC_RUN_ITP:
        /*
         * Retrigger during a running crossfade (S084 P1 review F6, accepted).
         *
         * What:       the snapshot below always takes the live (new) section,
         *             so a retrigger inside the ~3 ms crossfade drops the
         *             partly faded previous section.
         * Effect:     a step of at most the old section's remaining weight
         *             times the difference between the two sections, only for
         *             triggers closer than the crossfade length (fast rolls).
         * Why kept:   keeping the old section instead would restart the live
         *             oscillators under a running blend and step by the other
         *             weight; a clean result needs a third section (another
         *             OscInfo set per voice and a third render), which the
         *             runtime-slot reserve (104 B left) and the constant-CPU
         *             budget do not allow. Revisit only if H5 hears it.
         */
        for (i = 0u; i < s->oscCount; i++) {
            if (!oscs[i])
                continue;
            s->itpOld[i] = *oscs[i];
            /* The old section holds its pitch: it is never re-set per block. */
            s->itpOld[i].phaseIncStep = 0;
            s->itpOld[i].slewSamplesLeft = 0u;
        }
        s->itpState = OSC_ITP_PENDING;
        s->itpPos = 0u;
        s->itpUnits = 0u;
        return 1u;
    case OSC_RUN_RST:
    default:
        return 1u;
    }
}

/*
 * Re-claim an ongoing crossfade's units (S084 P1 P1-7.1).
 *
 * What:       ACTIVE states re-claim their oscCount units.
 * Why:        ongoing crossfades are never cut (plan §4.1.6 priority); they are
 *             claimed before any pending crossfade or interpolation.
 * Inputs:     section.  Outputs: pool grant.
 * Accessors:  instrumentManager_claimOscSectionUnits() loop 1.
 * Affiliates: oscRenderPool_claim().
 */
void oscSection_claimOngoing(OscSectionState* s)
{
    if (!s || s->itpState != OSC_ITP_ACTIVE)
        return;
    (void)oscRenderPool_claim(s->oscCount);
}

/*
 * Claim a new crossfade, or fall back to `fre` (S084 P1 P1-7.1).
 *
 * What:       PENDING states claim oscCount units; on success they become
 *             ACTIVE at position 0; on refusal the live phases are restored
 *             from itpOld and the oscillators slew as in `fre`.
 * Why:        plan §4.1.6: no free units behaves as `fre`, smooth and click
 *             free.
 * Inputs:     section, oscillator pointer array.
 * Outputs:    itpState, pool grant, possible phase restore.
 * Accessors:  instrumentManager_claimOscSectionUnits() loop 2.
 * Affiliates: osc_armSlew(), OscRenderPool.c.
 */
void oscSection_claimPending(OscSectionState* s, OscInfo* const oscs[])
{
    uint8_t i;

    if (!s || s->itpState != OSC_ITP_PENDING)
        return;
    if (oscRenderPool_claim(s->oscCount)) {
        s->itpState = OSC_ITP_ACTIVE;
        s->itpPos = 0u;
        s->itpUnits = s->oscCount;
        return;
    }
    /* No free units: behave exactly as fre for this trigger. */
    for (i = 0u; i < s->oscCount; i++) {
        if (!oscs[i])
            continue;
        oscs[i]->phase = s->itpOld[i].phase;
        osc_armSlew(oscs[i]);
    }
    s->itpState = OSC_ITP_IDLE;
    s->itpUnits = 0u;
}

/*
 * One master cycle in the master's own phase units (S084 P1 P1-7.1).
 *
 * What:       wavetable/sine/noise/pwm oscillators wrap their 32-bit phase
 *             (0 = full 2^32 wrap); installed samples use one full
 *             [start,end) span in Q20.12.
 * Why:        snc detects a master wrap in the format the master's renderer
 *             actually uses.
 * Inputs:     master oscillator.  Outputs: cycle length in phase units.
 * Accessors:  oscSection_renderDrum().
 */
static uint32_t oscSection_masterCycle(const OscInfo* m)
{
    if (m->oscType == OSC_TYPE_SMP || m->oscType == OSC_TYPE_SLP) {
        const uint32_t frames = (m->sampleEnd > m->sampleStart)
            ? (m->sampleEnd - m->sampleStart) : 1u;

        return frames << OSC_SAMPLE_PHASE_SHIFT;
    }
    /* Every table oscillator wraps its full 32-bit phase. */
    return 0u;
}

/*
 * Render the Drum oscillator combination (S084 P1 P1-7.1).
 *
 * What:       fm/mix are byte-identical to the pre-P1 Drum render; snc renders
 *             the slave in segments between master wraps; rng multiplies
 *             carrier and modulator with amt as the wet amount.
 * Why:        plan §4.2, S0 proof for fm/mix.
 * Inputs:     car, mod, mode, amount, buf, modBuf, size.
 * Outputs:    buf; modBuf holds the modulator block.
 * Accessors:  Drum_calcVoiceSyncBlock().
 * Affiliates: calcNextOscSampleBlock(), calcNextOscSampleFmBlock().
 */
INITCM void oscSection_renderDrum(OscInfo* car, OscInfo* mod, uint8_t mode,
                                  float amount, int16_t* buf, int16_t* modBuf,
                                  uint8_t size)
{
    uint8_t i;

    if (!car || !mod || !buf || !modBuf)
        return;
    switch (mode) {
    case OSC_MOD_MIX:
        calcNextOscSampleBlock(mod, modBuf, size, amount);
        calcNextOscSampleBlock(car, buf, size, (1.0f - amount));
        bufferTool_addBuffersSaturating(buf, modBuf, size);
        break;
    case OSC_MOD_SNC: {
        const uint32_t cycle = oscSection_masterCycle(mod);
        const uint8_t fullWrap = (uint8_t)(cycle == 0u);
        const uint8_t pow2 = (uint8_t)(!fullWrap &&
                                       ((cycle & (cycle - 1u)) == 0u));
        /*
         * Shadow master phase relative to the cycle start
         * (S084 P1 review fix F5).
         *
         * What:       for a sample master (smp/slp) the shadow phase starts
         *             at mod->phase - (sampleStart << OSC_SAMPLE_PHASE_SHIFT),
         *             clamped at 0, so it runs over [0, cycle) like the span
         *             oscSection_masterCycle() returns. Table masters keep
         *             base 0 (their cycle is the full 32-bit phase).
         * Why:        a sample master's phase is an absolute Q20.12 position
         *             in [start, end), but cycle is the span (end - start).
         *             Comparing the absolute phase with the span placed every
         *             wrap `start` frames early (and wrongly after the first),
         *             so with `wav` (sample start) above 0 the slave resets
         *             drifted against the audible master. Overshoot and the
         *             later shadow phases were already relative, so only the
         *             starting value needed the rebase.
         * Inputs:     mod (oscType, phase, sampleStart) before this block's
         *             master render.
         * Outputs:    startPhase in cycle-relative units.
         */
        const uint32_t cycleBase =
            (mod->oscType == OSC_TYPE_SMP || mod->oscType == OSC_TYPE_SLP)
            ? (mod->sampleStart << OSC_SAMPLE_PHASE_SHIFT) : 0u;
        const uint32_t startPhase = (mod->phase > cycleBase)
            ? (mod->phase - cycleBase) : 0u;
        const uint32_t startInc = mod->phaseInc;
        const int32_t step = mod->phaseIncStep;
        uint32_t phase = startPhase;
        uint32_t inc = startInc;
        uint8_t segmentStart = 0u;

        calcNextOscSampleBlock(mod, modBuf, size, 1.0f);
        for (i = 0u; i < size; i++) {
            const uint32_t next = phase + inc;
            const uint8_t wrapped = fullWrap ? (uint8_t)(next < phase)
                                             : (uint8_t)(next >= cycle);
            const uint32_t overshoot = !wrapped ? 0u
                : (fullWrap ? next
                   : (pow2 ? (next & (cycle - 1u)) : (next % cycle)));

            if (wrapped) {
                const uint8_t segmentLength =
                    (uint8_t)(i - segmentStart);
                const float ratio = (mod->phaseInc != 0u)
                    ? ((float)car->phaseInc / (float)mod->phaseInc) : 0.0f;

                if (segmentLength != 0u)
                    calcNextOscSampleBlock(car, buf + segmentStart,
                                           segmentLength, 1.0f);
                if (car->oscType == OSC_TYPE_SMP ||
                    car->oscType == OSC_TYPE_SLP) {
                    oscSample_restart(car);
                    car->phase += (uint32_t)((float)overshoot * ratio);
                } else {
                    car->phase = (uint32_t)((float)overshoot * ratio);
                }
                phase = overshoot;
                segmentStart = i;
            } else {
                phase = next;
            }
            inc += (uint32_t)step;
        }
        if (segmentStart < size)
            calcNextOscSampleBlock(car, buf + segmentStart,
                                   (uint8_t)(size - segmentStart), 1.0f);
        for (i = 0u; i < size; i++)
            buf[i] = __SSAT((int32_t)buf[i] +
                            (int32_t)((float)modBuf[i] * amount), 16);
        break;
    }
    case OSC_MOD_RNG:
        calcNextOscSampleBlock(mod, modBuf, size, 1.0f);
        calcNextOscSampleBlock(car, buf, size, 1.0f);
        for (i = 0u; i < size; i++) {
            const int32_t ring =
                ((int32_t)buf[i] * (int32_t)modBuf[i]) >> 15;
            buf[i] = __SSAT((int32_t)((float)buf[i] * (1.0f - amount)) +
                            (int32_t)((float)ring * amount), 16);
        }
        break;
    case OSC_MOD_FM:
    default:
        calcNextOscSampleBlock(mod, modBuf, size, amount);
        calcNextOscSampleFmBlock(car, modBuf, buf, size, 1.0f);
        break;
    }
}

/*
 * Render the Cymbal/HiHat two-modulator FM combination (S084 P1 P1-7.1).
 *
 * What:       byte-identical to today's Cymbal/HiHat render: modulators at
 *             g1/g2, saturating sum, carrier FM at carGain.
 * Why:        S0 proof for Cymbal/HiHat.
 * Inputs:     car, m1, m2, gains, buf, modBuf, modBuf2, size.
 * Outputs:    buf; modBuf holds the summed modulator.
 * Accessors:  Cymbal_calcSyncBlockVoice(), HiHat_calcSyncBlockVoice().
 * Affiliates: calcNextOscSampleBlock(), calcNextOscSampleFmBlock().
 */
INITCM void oscSection_renderFm2(OscInfo* car, OscInfo* m1, OscInfo* m2,
                                 float g1, float g2, float carGain,
                                 int16_t* buf, int16_t* modBuf,
                                 int16_t* modBuf2, uint8_t size)
{
    if (!car || !m1 || !m2 || !buf || !modBuf || !modBuf2)
        return;
    calcNextOscSampleBlock(m1, modBuf, size, g1);
    calcNextOscSampleBlock(m2, modBuf2, size, g2);
    bufferTool_addBuffersSaturating(modBuf, modBuf2, size);
    calcNextOscSampleFmBlock(car, modBuf, buf, size, carGain);
}

/*
 * Crossfade an `itp` old section into the new one (S084 P1 P1-7.1).
 *
 * What:       buf[i] = old[i] + t x (new[i] - old[i]), t = (pos+i)/132 clamped.
 * Why:        the pre-trigger section fades out over about 3 ms.
 * Inputs:     section, buf (new), oldBuf, size.
 * Outputs:    buf blended.
 * Accessors:  the engines' render functions.
 * Affiliates: oscSection_finishBlock().
 */
void oscSection_blendItp(OscSectionState* s, int16_t* buf,
                         const int16_t* oldBuf, uint8_t size)
{
    uint8_t i;

    if (!s || !buf || !oldBuf || s->itpState != OSC_ITP_ACTIVE)
        return;
    for (i = 0u; i < size; i++) {
        float t = (float)(s->itpPos + i) / (float)OSC_ITP_SAMPLES;

        if (t > 1.0f)
            t = 1.0f;
        buf[i] = (int16_t)((float)oldBuf[i] +
                           t * ((float)buf[i] - (float)oldBuf[i]));
    }
}

/*
 * Advance or finish the crossfade position (S084 P1 P1-7.1).
 *
 * What:       itpPos += size; at OSC_ITP_SAMPLES the state returns IDLE so the
 *             units free next block.
 * Why:        bounded, budgeted crossfade lifetime.
 * Inputs:     section, size.  Outputs: itpState, itpPos.
 * Accessors:  the engines' render functions.
 * Affiliates: oscSection_blendItp().
 */
void oscSection_finishBlock(OscSectionState* s, uint8_t size)
{
    if (!s || s->itpState != OSC_ITP_ACTIVE)
        return;
    s->itpPos = (uint16_t)(s->itpPos + size);
    if (s->itpPos >= OSC_ITP_SAMPLES) {
        s->itpState = OSC_ITP_IDLE;
        s->itpUnits = 0u;
    }
}

/*
 * Note the resonance target written by the reso row (S084 P1 P1-7.1).
 *
 * What:       stores the writer's q value so the `fre`/`itp` slew has a
 *             target independent of filter->q.
 * Why:        filter->q is overwritten with the smoothed value.
 * Inputs:     section, q.  Outputs: qTarget.
 * Accessors:  the IM_SPECIAL_FILTER_RESO writer.
 * Affiliates: oscSection_filterRecalc().
 */
void oscSection_noteResoTarget(OscSectionState* s, float q)
{
    if (!s)
        return;
    s->qTarget = q;
}

/*
 * Per-block filter coefficient update through the run mode
 * (S084 P1 P1-7.1).
 *
 * What:       rst: exactly SVF_recalcFreq() (S0). fre/itp: the cutoff and
 *             resonance the loop uses slew over about 2 ms toward the values
 *             the writers set.
 * Why:        plan §4.1.6 (r4 Q4); a retrigger never steps a ringing filter.
 * Inputs:     section, filter.  Outputs: filter->g (and q in fre/itp).
 * Accessors:  instrumentManager_recalcSlotFilter().
 * Affiliates: SVF_recalcFreq(), SVF_recalcFreqFrom().
 *
 * Resonance target pick-up (S084 P1 review fix F4):
 * What:       in fre/itp, if filter->q differs from the value this function
 *             wrote back on the previous block (qSmooth), something else
 *             wrote it since, and that value becomes the new qTarget before
 *             the slew step. The slewed value is then written back as before.
 * Why:        the voice ZDF loop reads only filter->q, and the velocity node
 *             and LFO adapters write filter->q directly through their
 *             parameter pointer (modulationNode.c, TYPE_FLT), not through the
 *             reso writer. Previously qTarget came only from the reso writer
 *             (oscSection_noteResoTarget()), so this function overwrote every
 *             modulated q with the slew toward the unmodulated target, and
 *             velocity or LFO on resonance had no effect in fre/itp. The exact
 *             float compare is valid because the write-back is a plain copy of
 *             qSmooth; it costs one compare per slot per block and leaves the
 *             ZDF loop untouched.
 * Unchanged:  the reso writer still calls oscSection_noteResoTarget(); its
 *             value is also picked up by this compare, so the two paths agree.
 *             rst keeps the exact SVF_recalcFreq() behaviour (S0).
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
    /* F4: a q written by modulation since the last block is the new target. */
    if (f->q != s->qSmooth)
        s->qTarget = f->q;
    s->fSmooth += (f->f - s->fSmooth) * 0.28f;
    s->qSmooth += (s->qTarget - s->qSmooth) * 0.28f;
    f->q = s->qSmooth;
    SVF_recalcFreqFrom(f, s->fSmooth);
}
