/*
 * OscSection — a voice's oscillator section: combination modes and run modes
 * (S085 C10.1).
 *
 * What:       combination of carrier and modulator(s):
 *               OSC_MOD_FM  phase modulation (stored 0, today);
 *               OSC_MOD_MIX carrier x (1-amt) + modulator x amt (1, today);
 *               OSC_MOD_SNC hard sync: the modulator is the master, the
 *                           carrier the slave, reset at each master wrap with
 *                           the overshoot scaled; out = slave x (1-amt) +
 *                           master x amt (Q10);
 *               OSC_MOD_RNG ring: carrier x modulator, amt = wet amount.
 *             Run modes, per voice:
 *               OSC_RUN_RST today's trigger resets, filter cleared;
 *               OSC_RUN_FRE no resets; every oscillator slews its pitch over
 *                           OSC_SLEW_SAMPLES; the filter keeps ringing and its
 *                           cutoff/resonance slew;
 *               OSC_RUN_ITP the carrier is snapshotted and restarted; the copy
 *                           fades out over OSC_ITP_SAMPLES while the
 *                           restarted carrier fades in; the modulators slew as
 *                           in fre and feed both carriers; one pool unit per
 *                           voice; with no free unit the trigger behaves as
 *                           fre; the filter keeps ringing and slews.
 * Why:        plan §4.3.1 (snc cost), §4.3.7 (carrier-only itp, Q14), Q10,
 *             Q15; stored values 0/1 of the mod row keep their meaning.
 * Inputs:     engine oscillators, mode bytes, pool grants.
 * Outputs:    the section block; slew and crossfade state.
 * Accessors:  the four engines, InstrumentManager.c.
 * Affiliates: Oscillator.c (osc_armSlew, renderers), OscillatorSample.c
 *             (restart, end test), OscRenderPool.c, ResonantFilter.c
 *             (SVF_recalcFreqFrom).
 */
#ifndef OSCSECTION_H_
#define OSCSECTION_H_

#include <stdint.h>
#include "Oscillator.h"
#include "ResonantFilter.h"

#define OSC_RUN_RST   0u
#define OSC_RUN_FRE   1u
#define OSC_RUN_ITP   2u
#define OSC_RUN_COUNT 3u

#define OSC_MOD_FM    0u
#define OSC_MOD_MIX   1u
#define OSC_MOD_SNC   2u
#define OSC_MOD_RNG   3u
#define OSC_MOD_COUNT 4u

#define OSC_ITP_SAMPLES 132u
#define OSC_ITP_IDLE    0u
#define OSC_ITP_PENDING 1u
#define OSC_ITP_ACTIVE  2u

/* oscSection_beginTrigger() results: what the engine resets. */
#define OSC_TRIGGER_RESET_NONE    0u  /* fre: nothing */
#define OSC_TRIGGER_RESET_ALL     1u  /* rst: the engine's usual resets */
#define OSC_TRIGGER_RESET_CARRIER 2u  /* itp: the carrier only */

/*
 * Per-voice section state (S085 C10.1).
 *
 * What:       runMode (the run row), the itp state machine and position, the
 *             one carrier snapshot, and the filter slew (fSmooth < 0 = not
 *             yet seeded).
 * Why:        carrier-only itp needs one OscInfo copy (S085 §4.3.2: about
 *             140 B per engine, inside the 1,176 B runtime slot).
 * Writers:    oscSection_*(), IM_SPECIAL_RUN_MODE writer.
 * Readers:    the engines, InstrumentManager.c.
 */
typedef struct OscSectionStateStruct {
    uint8_t  runMode;
    uint8_t  itpState;
    uint16_t itpPos;
    OscInfo  itpOld;
    float    fSmooth;
    float    qSmooth;
    float    qTarget;
} OscSectionState;

/*
 * Initialise (S085 C10.1).
 * What: rst, idle, filter slew unseeded.
 * Accessors: each engine's *_initVoice().
 */
void oscSection_init(OscSectionState* s);

/*
 * Apply a trigger to the run mode (S085 C10.1).
 *
 * What:       rst -> OSC_TRIGGER_RESET_ALL. fre -> arms the slew on the
 *             carrier and every modulator, OSC_TRIGGER_RESET_NONE. itp ->
 *             snapshots the carrier into itpOld (pitch frozen: step 0, no
 *             slew; waveform interpolation off), arms the slew on every
 *             modulator, restarts the crossfade position, and returns
 *             OSC_TRIGGER_RESET_CARRIER; the state becomes PENDING, or stays
 *             ACTIVE when a crossfade was already running (it keeps its unit;
 *             the partly faded copy is replaced, the accepted S084 P1 F6).
 * Why:        plan §4.3.7.
 * Inputs:     section, carrier, modulator array, count (Snare passes its
 *             noise oscillator).
 * Outputs:    the reset instruction; slew and itp state.
 * Accessors:  the engines' trigger functions.
 * Affiliates: osc_armSlew(), oscSection_claimPending().
 */
uint8_t oscSection_beginTrigger(OscSectionState* s, OscInfo* carrier,
                                OscInfo* const mods[], uint8_t modCount);

/*
 * Pool claims (S085 C10.1).
 *
 * What:       claimOngoing: an ACTIVE crossfade claims its unit (always
 *             granted, at most two can be ACTIVE; if ever refused the
 *             crossfade ends). claimPending: a PENDING crossfade claims one
 *             unit and becomes ACTIVE; refused, the carrier's phase is
 *             restored from the snapshot and the carrier slews (fre).
 * Accessors:  InstrumentManager.c claim loops (C13.7).
 */
void oscSection_claimOngoing(OscSectionState* s);
void oscSection_claimPending(OscSectionState* s, OscInfo* carrier);

/*
 * The fading carrier copy (S085 C10.1).
 * What: &itpOld while ACTIVE, else NULL.
 * Accessors: the engines' render functions.
 */
OscInfo* oscSection_itpCarrier(OscSectionState* s);

/*
 * Render the Drum combination (S085 C10.1).
 *
 * What:       fm and mix are bit-identical to the pre-S085 Drum code when
 *             oldCar is NULL; snc and rng as in the header contract. When
 *             oldCar is not NULL (itp ACTIVE) the old carrier is rendered into
 *             oldBuf with the same, live modulator block (and, in snc, the
 *             same master wraps), so oldBuf differs from buf only by the
 *             carrier.
 * Inputs:     car, mod, mode, amount, buf, modBuf, size, oldCar, oldBuf.
 * Outputs:    buf (and oldBuf); modBuf holds the modulator block.
 * Accessors:  Drum_calcVoiceSyncBlock().
 */
void oscSection_renderDrum(OscInfo* car, OscInfo* mod, uint8_t mode,
                           float amount, int16_t* buf, int16_t* modBuf,
                           uint8_t size, OscInfo* oldCar, int16_t* oldBuf);

/*
 * Render the Drum combination with an FM depth ramp (S085 P2 F-A).
 *
 * What:       as oscSection_renderDrum(); in fm mode the modulator block is
 *             also scaled from fmRampFrom to 1 across the block, so the
 *             carrier's FM depth glides from the previous block's fmMod to
 *             this block's. fmRampFrom = 1 is exactly oscSection_renderDrum().
 *             Forced flat while an itp crossfade is active (oldCar set).
 * Why:        Drum's FM depth follows the pitch envelope, which restarts at
 *             every trigger; the FM is phase modulation, so the depth jump
 *             moved the carrier's read phase at the block boundary (the
 *             sporadic fre click with high FM). One block, matching the
 *             amp ramp.
 * Inputs:     the oscSection_renderDrum() inputs plus fmRampFrom
 *             (fmMod_old / fmMod_new, clamped 0..1, on the armed retrigger
 *             block; 1 otherwise).
 * Outputs:    buf (and oldBuf); modBuf.
 * Accessors:  Drum_calcVoiceSyncBlock().
 * Affiliates: DrumVoice lastFmMod and ampRampArm.
 */
void oscSection_renderDrumRamped(OscInfo* car, OscInfo* mod, uint8_t mode,
                                 float amount, int16_t* buf, int16_t* modBuf,
                                 uint8_t size, OscInfo* oldCar,
                                 int16_t* oldBuf, float fmRampFrom);

/*
 * Render the Cymbal/HiHat two-modulator FM section (S085 C10.1).
 *
 * What:       modulators at g1/g2 into modBuf/modBuf2, saturating sum into
 *             modBuf, carrier FM at carGain into buf (bit-identical to the
 *             pre-S085 engines); oldCar (itp) is rendered with the same
 *             summed modBuf into oldBuf.
 * Accessors:  Cymbal_calcSyncBlockVoice(), HiHat_calcSyncBlockVoice().
 */
void oscSection_renderFm2(OscInfo* car, OscInfo* m1, OscInfo* m2,
                          float g1, float g2, float carGain,
                          int16_t* buf, int16_t* modBuf, int16_t* modBuf2,
                          uint8_t size, OscInfo* oldCar, int16_t* oldBuf);

/*
 * Crossfade and advance (S085 C10.1).
 *
 * What:       blendItp: buf[i] = old[i] + t x (buf[i] - old[i]),
 *             t = (itpPos + i) / OSC_ITP_SAMPLES clamped to 1 (ACTIVE only).
 *             finishBlock: itpPos += size; at OSC_ITP_SAMPLES the state
 *             returns to IDLE and the unit frees next block.
 * Accessors:  the engines' render functions.
 */
void oscSection_blendItp(OscSectionState* s, int16_t* buf,
                         const int16_t* oldBuf, uint8_t size);
void oscSection_finishBlock(OscSectionState* s, uint8_t size);

/*
 * Filter slew (S085 C10.1).
 *
 * What:       noteResoTarget stores the reso writer's q. filterRecalc: rst is
 *             exactly SVF_recalcFreq() (S0); fre/itp slew the cutoff the loop
 *             uses and the resonance with a one-pole of 0.28 per block (about
 *             2 ms), picking up a q written by velocity or LFO since the last
 *             block (S084 P1 F4).
 * Why:        Q15; a ringing filter never steps on a retrigger.
 * Accessors:  the IM_SPECIAL_FILTER_RESO writer; recalcSlotFilter.
 * Affiliates: SVF_recalcFreq(), SVF_recalcFreqFrom().
 */
void oscSection_noteResoTarget(OscSectionState* s, float q);
void oscSection_filterRecalc(OscSectionState* s, ResonantFilter* f);

#endif /* OSCSECTION_H_ */
