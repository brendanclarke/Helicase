/*
 * OscillatorPwm — computed pulse-width families for the pwm type (S085 C7.1).
 *
 * What:       six families selected by src; the wav row is the width control
 *             c = wav / 127:
 *             prc  pulse from two band-limited saws, width 50 % -> 4 %;
 *             psw  sum of two saws (pulse-saw), d = c / 2;
 *             ptr  triangle with a warped phase (peak 50 % -> 3 %);
 *             pq3  the sq3 stock wave, phase warped around its edge (0.78);
 *             pw3  the sw3 stock wave, warped around its reset (0.42);
 *             swp  sweep sine -> tri -> saw -> square -> narrowing pulse.
 *             Nothing is stored; every family reads the band-limited tables
 *             (sawTable, triTable, sine_table) or the stock block.
 * Why:        plan §5 item 2; width-dependent constants are computed once per
 *             block (oscPwm_prepare()), so the per-sample loops have no
 *             division. prc at c = 0 equals Rec (S085 D4, measured).
 * Inputs:     OscInfo (waveform = the family, the `wav` row; source = the
 *             width 0..127, the `mod` row (S085 P2 row swap); phase,
 *             phaseInc, phaseIncStep, fmMod, tableOffset, pwm).
 * Outputs:    rendered blocks; OscPwmState per block.
 * Accessors:  Oscillator.c dispatch and osc_setFreq().
 * Affiliates: wavetable.c, StockWaves.c, tools/plots/plot_pwm.py
 *             (reference shapes).
 */
#ifndef OSCILLATORPWM_H_
#define OSCILLATORPWM_H_

#include <stdint.h>

#define OSC_PWM_PRC 0u
#define OSC_PWM_PSW 1u
#define OSC_PWM_PTR 2u
#define OSC_PWM_PQ3 3u
#define OSC_PWM_PW3 4u
#define OSC_PWM_SWP 5u
#define OSC_PWM_FAMILY_COUNT 6u

/*
 * Per-block pwm state (S085 C7.1).
 *
 * What:       shift = second-read offset (prc/psw/swp square, fraction of a
 *             2^32 cycle); split = warp split in phase units; scale = output
 *             gain (peak 1); warpA/warpB = the two warp slopes; mix = swp
 *             segment blend, or the natural split s0 for the warped families;
 *             seg = swp segment 0..3.
 * Why:        computed once per block, read every sample.
 * Writers:    oscPwm_prepare().  Readers: the pwm renderers.
 * Accessors:  OscillatorPwm.c; lives in the OscInfo union.
 */
typedef struct {
    uint32_t shift;
    uint32_t split;
    float    scale;
    float    warpA;
    float    warpB;
    float    mix;
    uint8_t  seg;
} OscPwmState;

struct OscStruct;

/*
 * Compute one block's width state (S085 C7.1).
 *
 * What:       sets phaseIncTarget (1,024-point format, 2^32 per cycle),
 *             tableOffset for freqHz x compression (compression =
 *             max(s0/w, (1-s0)/(1-w)) for the warped families, else 1), and
 *             every OscPwmState field from c = source / 127 (the `mod`
 *             row; S085 P2 row swap).
 * Why:        the divisions happen here, at most once per block.
 * Inputs:     osc (waveform = family, source = width), freqHz.
 * Outputs:    phaseIncTarget, tableOffset, pwm.
 * Accessors:  osc_setFreq() (on a cache miss).
 * Affiliates: osc_freqToTableIndex(), sampleMemory_stockValid().
 */
void oscPwm_prepare(struct OscStruct* osc, float freqHz);

/*
 * Render one pwm block (S085 C7.1).
 *
 * What:       prc/psw: two interpolated reads; ptr/pq3/pw3: one warped read;
 *             swp: two or three reads and a blend (segment 2 is the worst
 *             case and is the one probed); the per-sample slew. The FM
 *             variant adds (mod x fmMod) << 19 to the read phase.
 * Inputs:     osc, (modBuffer), buf, size, gain.
 * Outputs:    buf; phase, phaseInc.
 * Accessors:  Oscillator.c type dispatch.
 */
void oscPwm_render(struct OscStruct* osc, int16_t* buf, uint8_t size,
                   float gain);
void oscPwm_renderFm(struct OscStruct* osc, int16_t* modBuffer, int16_t* buf,
                     uint8_t size, float gain);

/* Hard-sync slave render (S085 assessment A2); see OscSyncLane.h. */
struct OscSyncLane;
void oscPwm_renderSync(struct OscStruct* osc, int16_t* buf, uint8_t size,
                       float gain, const struct OscSyncLane* lane);

#endif /* OSCILLATORPWM_H_ */
