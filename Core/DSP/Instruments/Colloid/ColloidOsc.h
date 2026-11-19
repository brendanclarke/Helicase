/*
 * ColloidOsc.h - the Colloid's oscillator loops (S095 P2; S095 P3 F2).
 *
 * What:       four per-sample loops, each rendering one pair of free-running
 *             oscillators for one 32-sample block into the voice's two int32
 *             buses:
 *               busM[n] (mid)  = s0 + s1
 *               busS[n] (side) = (s0 x p0 + s1 x p1) / 2, p in Q15
 *             where s is the oscillator's interpolated row read and p its
 *             stereo position (-1..+1) for this block. The "store" forms write
 *             the buses (the first pair of a block, so no clear pass is
 *             needed); the "add" forms accumulate into them.
 *               col_oscPairStore / col_oscPairAdd  a 1,024-point row each
 *                 (a stock octave row, the 1,024-point sine, or a wavetable
 *                 frame), linearly interpolated;
 *               col_pwmPairStore / col_pwmPairAdd  the two-read pulse: the
 *                 saw row read at the phase plus `sign` x the saw row read
 *                 at a second phase `width` points earlier (sign -1 Rec, +1
 *                 Saw; S095 P3 F2). S096 P2 F5: the second read has its own
 *                 phase and interpolation, and its offset glides across the
 *                 block from width0 (the block's start) to width (its end),
 *                 so a moving width never steps (it was a whole-point
 *                 offset sharing the first read's fraction, held per block);
 *               col_warpStore / col_warpAdd  one oscillator per pass: a
 *                 1,024-point row (a triTable octave row) read at the
 *                 warped phase osc_pwmWarp(ph, W, ra, rb) (the shipped WT
 *                 form; S095 P3 F2; S099 P1 §15.10: pwm Sin dropped), its position in pq's low half.
 *                 S096 P2 F5: W, ra and rb glide by dW, dRa, dRb per sample
 *                 from the block's start values toward its end values.
 * Why:        S095 P2 draft §2.1/§2.4: a pair per pass is the cheapest form
 *             measured (13 per oscillator plain, 22 PWM, before the side
 *             term); the side term is one SMLAW per oscillator with both
 *             positions packed in one register (posPair: oscillator 0 in the
 *             low half, oscillator 1 in the high half).
 * Inputs:     row[0..1] (the pair's rows), phase[0..1] (updated in place),
 *             inc[0..1], width[0..1] / width0[0..1] (pwm: the pulse width in
 *             row points, 1..1,023, at the block's end / start), sign (pwm:
 *             -1 Rec, +1 Saw), posPair, busM[32], busS[32]; the warp loops:
 *             one row, phase, inc, W, ra, rb (the block's start), dW, dRa,
 *             dRb (their steps per sample), pq and bus[64]: the mid bus then
 *             the side bus in one array (S096 P2 F5); the scan loops:
 *             row[0..1] (frame A of each), g[0..1] (the Q31 blend at the
 *             block's start), dg[0..1] (its step) and the same buses
 *             (S096 P3).
 *             S099 P1: the chop loops add col_chopDcInit() (the buses start
 *             at minus the stack's mean, flash) and col_chopPairAdd() (three
 *             pairs, each oscillator at its own level, every point
 *             truncated; ITCM .itcm.s099p1).
 * Outputs:    busM, busS; phase[0..1] advanced by 32 x inc.
 * Accessors:  Colloid_calcVoiceSyncBlock() (ColloidVoice.c), once per pair
 *             per block.
 * Affiliates: ColloidOsc.c (placement .itcm.s095p2, the warp loops
 *             .itcm.s095p3); STM32F765VIHx_FLASH.ld;
 *             tools/dsp_test modules/colloid.c laws CO-OSC, CO-POS, CO-PWM.
 */
#ifndef COLLOIDOSC_H_
#define COLLOIDOSC_H_

#include <stdint.h>

void col_oscPairStore(const int16_t *const *row, uint32_t *phase,
                      const uint32_t *inc, uint32_t posPair,
                      int32_t *busM, int32_t *busS);
void col_oscPairAdd(const int16_t *const *row, uint32_t *phase,
                    const uint32_t *inc, uint32_t posPair,
                    int32_t *busM, int32_t *busS);
void col_pwmPairStore(const int16_t *const *row, uint32_t *phase,
                      const uint32_t *inc, const uint16_t *width,
                      const uint16_t *width0, uint32_t posPair,
                      int32_t *busM, int32_t *busS, int32_t sign);
void col_pwmPairAdd(const int16_t *const *row, uint32_t *phase,
                    const uint32_t *inc, const uint16_t *width,
                    const uint16_t *width0, uint32_t posPair,
                    int32_t *busM, int32_t *busS, int32_t sign);
void col_warpStore(const int16_t *row, uint32_t *phase, uint32_t inc,
                   uint32_t wPh, uint32_t ra, uint32_t rb,
                   int32_t dW, int32_t dRa, int32_t dRb, uint32_t pq,
                   int32_t *bus);
void col_warpAdd(const int16_t *row, uint32_t *phase, uint32_t inc,
                 uint32_t wPh, uint32_t ra, uint32_t rb,
                 int32_t dW, int32_t dRa, int32_t dRb, uint32_t pq,
                 int32_t *bus);
void col_scanPairStore(const int16_t *const *row, uint32_t *phase,
                       const uint32_t *inc, const uint32_t *g,
                       const int32_t *dg, uint32_t posPair,
                       int32_t *bus);
void col_scanPairAdd(const int16_t *const *row, uint32_t *phase,
                     const uint32_t *inc, const uint32_t *g,
                     const int32_t *dg, uint32_t posPair,
                     int32_t *bus);
/*
 * S099 P1: the Colloid's chopped pwm wave R-S (S099 P2: REx dropped).
 *
 * What:       col_chopDcInit() writes both buses (mid then side, one 64-word
 *             array) with minus the stack's mean, gliding from the block's
 *             start widths to its end; col_chopPairAdd() adds one pair of
 *             (the 2x gate x the curve at the 1x ramp wrapped at 7/8), each
 *             oscillator at its own level and position. Form T: every point
 *             truncated. Contracts in ColloidOsc.c.
 * Why:        the chop runs COL_PWM_COUNT (6) oscillators per block, in ITCM
 *             (.itcm.s099p1); the mean's start pass stays in flash.
 * Accessors:  col_chopBlock() (ColloidVoice.c), three pairs and one init a
 *             block.
 * Affiliates: oscChopTables.h; tools/dsp_test modules/colloid.c law CO-CHP.
 */
void col_chopDcInit(int32_t *bus, int32_t aM, int32_t dM, int32_t aS, int32_t dS);
void col_chopPairAdd(const int16_t *const *row, uint32_t *phase,
                     const uint32_t *inc, const uint16_t *width,
                     const uint16_t *width0, uint32_t gq, uint32_t lv,
                     uint32_t lp, const int16_t *curve, int32_t *bus);

#endif /* COLLOIDOSC_H_ */
