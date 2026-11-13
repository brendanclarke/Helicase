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
 *                 `width` points earlier, interpolated once (the shipped
 *                 osc_pwmReadD() form: sign -1 Rec, +1 Saw; S095 P3 F2);
 *               col_warpStore / col_warpAdd  one oscillator per pass: a
 *                 1,024-point row (sine_table or a triTable octave row) read
 *                 at the warped phase osc_pwmWarp(ph, W, ra, rb) (the shipped
 *                 WS / WT forms; S095 P3 F2), its position in pq's low half.
 * Why:        S095 P2 draft §2.1/§2.4: a pair per pass is the cheapest form
 *             measured (13 per oscillator plain, 22 PWM, before the side
 *             term); the side term is one SMLAW per oscillator with both
 *             positions packed in one register (posPair: oscillator 0 in the
 *             low half, oscillator 1 in the high half).
 * Inputs:     row[0..1] (the pair's rows), phase[0..1] (updated in place),
 *             inc[0..1], width[0..1] (pwm: the pulse width in row points,
 *             1..1,023), sign (pwm: -1 Rec, +1 Saw), posPair, busM[32],
 *             busS[32]; the warp loops: one row, phase, inc, W, ra, rb and pq.
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
                      uint32_t posPair, int32_t *busM, int32_t *busS,
                      int32_t sign);
void col_pwmPairAdd(const int16_t *const *row, uint32_t *phase,
                    const uint32_t *inc, const uint16_t *width,
                    uint32_t posPair, int32_t *busM, int32_t *busS,
                    int32_t sign);
void col_warpStore(const int16_t *row, uint32_t *phase, uint32_t inc,
                   uint32_t wPh, uint32_t ra, uint32_t rb, uint32_t pq,
                   int32_t *busM, int32_t *busS);
void col_warpAdd(const int16_t *row, uint32_t *phase, uint32_t inc,
                 uint32_t wPh, uint32_t ra, uint32_t rb, uint32_t pq,
                 int32_t *busM, int32_t *busS);

#endif /* COLLOIDOSC_H_ */
