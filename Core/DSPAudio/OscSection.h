/*
 * OscSection.h
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

#ifndef OSCSECTION_H_
#define OSCSECTION_H_

#include "Oscillator.h"
#include "ResonantFilter.h"
#include "OscillatorSample.h"
#include "OscRenderPool.h"

/*
 * OscSection — a voice's oscillator section: combination modes and run
 * modes (S084 P1 P1-7.1).
 *
 * What:       combination of a carrier and modulator(s): OSC_MOD_FM (phase
 *             modulation, today), OSC_MOD_MIX (today), OSC_MOD_SNC (hard sync:
 *             the modulator is the master, the main oscillator the slave,
 *             reset at each master wrap with the overshoot scaled), OSC_MOD_RNG
 *             (ring: carrier x modulator). Run modes: OSC_RUN_RST (today's
 *             trigger resets), OSC_RUN_FRE (no resets; every oscillator slews
 *             its pitch over OSC_SLEW_SAMPLES; the filter keeps its state and
 *             its cutoff/resonance slew), OSC_RUN_ITP (the whole section keeps
 *             rendering its pre-trigger state beside the reset one and the two
 *             crossfade over OSC_ITP_SAMPLES, using OscRenderPool units; with
 *             no free units that trigger behaves as `fre`; the filter keeps its
 *             state and slews).
 * Why:        plan §4.1.6 (run modes, per voice, all oscillators), §4.2
 *             (`snc`, `rng` on Drum).
 * Inputs:     engine oscillators, mode bytes, OscRenderPool grants.
 * Outputs:    the combined section block; slew/itp state.
 * Accessors:  DrumVoice.c, Snare.c, CymbalVoice.c, HiHat.c,
 *             InstrumentManager.c.
 * Affiliates: Oscillator.c (osc_armSlew, block renderers),
 *             OscillatorSample.c (sync/rst restarts), ResonantFilter.c
 *             (SVF_recalcFreqFrom()).
 */
#define OSC_RUN_RST 0u
#define OSC_RUN_FRE 1u
#define OSC_RUN_ITP 2u
#define OSC_RUN_COUNT 3u
#define OSC_MOD_FM  0u
#define OSC_MOD_MIX 1u
#define OSC_MOD_SNC 2u
#define OSC_MOD_RNG 3u
#define OSC_MOD_COUNT 4u
#define OSC_SECTION_MAX_OSCS 3u
#define OSC_ITP_SAMPLES  132u
#define OSC_ITP_IDLE    0u
#define OSC_ITP_PENDING 1u
#define OSC_ITP_ACTIVE  2u

/*
 * Per-voice section state (S084 P1 P1-7.1).
 *
 * What:       runMode (descriptor row `run`), itpState/itpPos/itpUnits, the
 *             snapshot of the voice's oscillators taken at an `itp` trigger,
 *             and the filter slew (fSmooth, qSmooth, qTarget).
 * Why:        the crossfade must keep rendering the old oscillators; the
 *             filter slew needs its own smoothed values (plan §4.1.6, r4 Q4).
 * Accessors:  OscSection.c, the engines, InstrumentManager.c (writer, filter
 *             recalc, claims). Lives inside each engine struct.
 */
typedef struct {
    uint8_t runMode;
    uint8_t itpState;
    uint8_t itpUnits;
    uint8_t oscCount;
    uint16_t itpPos;
    OscInfo itpOld[OSC_SECTION_MAX_OSCS];
    float fSmooth;
    float qSmooth;
    float qTarget;
} OscSectionState;

/*
 * Initialize one voice's oscillator section (S084 P1 P1-7.1).
 *
 * What:       run mode rst, no crossfade, oscCount for this engine; the filter
 *             slew starts from the filter's values at the first recalc.
 * Why:        a fresh or reset slot starts in today's behaviour until its
 *             `run` row is applied.
 * Inputs:     section, oscillator count (Drum/Snare 2, Cymbal/HiHat 3).
 * Outputs:    section fields.
 * Accessors:  each engine's *_initVoice().
 * Affiliates: OscSection.c, IM_SPECIAL_RUN_MODE writer.
 */
void oscSection_init(OscSectionState* s, uint8_t oscCount);

/*
 * Apply a trigger to the run mode (S084 P1 P1-7.1).
 *
 * What:       RST returns 1 (the engine performs today's reset); FRE arms the
 *             pitch slew on every oscillator and returns 0; ITP snapshots every
 *             oscillator into itpOld[] (pitch frozen) and returns 1.
 * Why:        plan §4.1.6: the run mode decides whether the reset happens.
 * Inputs:     section, oscillator pointer array.
 * Outputs:    1 = reset the live oscillators, 0 = keep them.
 * Accessors:  the engines' *_triggerVoice().
 * Affiliates: osc_armSlew(), oscSection_claimPending().
 */
uint8_t oscSection_beginTrigger(OscSectionState* s, OscInfo* const oscs[]);

/*
 * Re-claim an ongoing crossfade's units (S084 P1 P1-7.1).
 *
 * What:       ACTIVE states re-claim their oscCount units.
 * Why:        ongoing crossfades are never cut (plan §4.1.6 priority).
 * Inputs:     section.  Outputs: pool grant.
 * Accessors:  instrumentManager_claimOscSectionUnits() loop 1.
 * Affiliates: oscRenderPool_claim().
 */
void oscSection_claimOngoing(OscSectionState* s);

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
void oscSection_claimPending(OscSectionState* s, OscInfo* const oscs[]);

/*
 * Render the Drum oscillator combination (S084 P1 P1-7.1).
 *
 * What:       fm/mix are byte-identical to the pre-P1 engine code; snc renders
 *             the slave in segments between master wraps; rng multiplies
 *             carrier and modulator with amt as the wet amount.
 * Why:        plan §4.2, S0 proof for fm/mix.
 * Inputs:     car, mod, mode (OSC_MOD_*), amount, buf, modBuf, size.
 * Outputs:    buf; modBuf holds the modulator block.
 * Accessors:  Drum_calcVoiceSyncBlock().
 * Affiliates: calcNextOscSampleBlock(), calcNextOscSampleFmBlock(),
 *             oscSample_restart().
 */
void oscSection_renderDrum(OscInfo* car, OscInfo* mod, uint8_t mode,
                           float amount, int16_t* buf, int16_t* modBuf,
                           uint8_t size);

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
void oscSection_renderFm2(OscInfo* car, OscInfo* m1, OscInfo* m2,
                          float g1, float g2, float carGain,
                          int16_t* buf, int16_t* modBuf, int16_t* modBuf2,
                          uint8_t size);

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
                         const int16_t* oldBuf, uint8_t size);

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
void oscSection_finishBlock(OscSectionState* s, uint8_t size);

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
void oscSection_noteResoTarget(OscSectionState* s, float q);

/*
 * Per-block filter coefficient update through the run mode
 * (S084 P1 P1-7.1).
 *
 * What:       rst: exactly SVF_recalcFreq() (S0). fre/itp: the cutoff and
 *             resonance the loop uses slew over about 2 ms toward the values
 *             the writers set.
 * Why:        plan §4.1.6 (r4 Q4: a fast parameter slew when the filter state
 *             is kept); a retrigger never steps a ringing filter.
 * Inputs:     section, filter.  Outputs: filter->g (and q in fre/itp).
 * Accessors:  instrumentManager_recalcSlotFilter().
 * Affiliates: SVF_recalcFreq(), SVF_recalcFreqFrom().
 */
void oscSection_filterRecalc(OscSectionState* s, ResonantFilter* f);

#endif /* OSCSECTION_H_ */
