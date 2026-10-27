/*
 * OscSyncLane.h — per-sample hard-sync reset lane (S085 assessment A2).
 *
 * What:       the per-block description of a hard-sync master's wraps, as
 *             two 32-entry arrays built once per block by
 *             oscSection_renderSync(): wrap[i] = 1 when the master wrapped
 *             between samples i and i + 1, over[i] = the master's overshoot
 *             past its cycle at that point (master phase units). ratio =
 *             slave phaseInc / master phaseInc, computed once per block, turns
 *             an overshoot into slave phase units.
 * Why:        the S085 C10.2 segment render made one full renderer call per
 *             master wrap. A sample master wraps on every sample at high
 *             notes, so the per-call overhead (dispatch, bind, frame pair)
 *             landed on every sample and the snc worst case exceeded the
 *             C10.6 cap (FM carrier + 10 instructions per sample) for every
 *             slave type. C10.6 step 4 replaces the segments with one loop per
 *             slave family that applies the reset inside the loop at constant
 *             cost; this header is the contract those loops share.
 * Contract:   a renderer core takes `const OscSyncLane* lane`. NULL means no
 *             sync. Two placements:
 *             - wav cores (sine, Tri/Saw/Rec, noise, crash, stock) are
 *               always-inline and every plain entry point passes a literal
 *               NULL, so the sync code is removed at compile time from the
 *               ordinary loops (fpseq counts and osc_s0 prove those loops
 *               unchanged); osc_renderSyncBlock() holds their sync copies.
 *             - pwm, smp/slp and wtb run one shared loop (OSC_SHARED_LOOP)
 *               for both, testing the lane every sample; a third copy of
 *               each would have cost 3.4 KB of ITCM.
 *             With a lane, after the
 *             sample at index i is produced and the phase advanced, the core
 *             replaces the phase with oscSync_phase(): the reset value is
 *             computed every sample and selected by wrap[i], so the cost does
 *             not depend on how often the master wraps (constant CPU). The
 *             reset lands on sample i + 1, i.e. the slave renders through the
 *             wrap sample first (S085 D13).
 * Accessors:  OscSection.c (builds lanes); Oscillator.c
 *             osc_renderSyncBlock() and the wav cores; OscillatorPwm.c,
 *             OscillatorSample.c, OscillatorWavetable.c (*_renderSync()).
 * Affiliates: OscSection.h OSC_MOD_SNC; C10.6 of S085_P1_IMPLEMENTATION.md.
 */
#ifndef OSCSYNCLANE_H_
#define OSCSYNCLANE_H_

#include <stdint.h>

typedef struct OscSyncLane {
	const uint8_t*  wrap;   /* 1 = master wrapped after sample i */
	const uint32_t* over;   /* master overshoot at i, master phase units */
	float           ratio;  /* slave inc / master inc, once per block */
} OscSyncLane;

/* Force inlining so a literal NULL lane removes the sync code. */
#define OSC_SYNC_INLINE static inline __attribute__((always_inline))

/*
 * Attributes of a plain/sync shared loop (pwm, smp/slp, wtb).
 *
 * What:       out of line, never cloned per argument value, and no loop
 *             unswitching, so one copy of the loop serves both lane values.
 *             GCC only: clang (the host harness) has no noclone/optimize, and
 *             the host build needs only correct values, not this layout.
 */
#if defined(__GNUC__) && !defined(__clang__)
#define OSC_SHARED_LOOP __attribute__((noinline, noclone, optimize("no-unswitch-loops")))
#else
#define OSC_SHARED_LOOP __attribute__((noinline))
#endif

/*
 * The slave phase after sample i (S085 assessment A2).
 *
 * What:       base + overshoot x ratio when the master wrapped after sample
 *             i, else the already-advanced phase. base is 0 for periodic
 *             slaves and sampleStart << 12 for smp/slp slaves.
 * Cost:       one load of each lane entry, a convert-multiply-convert and a
 *             select, every sample, whether or not the master wrapped.
 * Range:      over < master inc, so over x ratio < slave inc < 2^32; the
 *             float-to-uint32 conversion cannot overflow.
 */
OSC_SYNC_INLINE uint32_t oscSync_phase(const OscSyncLane* lane, uint8_t i,
                                       uint32_t phase, uint32_t base)
{
	const uint32_t reset =
		base + (uint32_t)((float)lane->over[i] * lane->ratio);

	return lane->wrap[i] ? reset : phase;
}

#endif /* OSCSYNCLANE_H_ */
