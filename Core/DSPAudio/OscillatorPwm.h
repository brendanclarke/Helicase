/*
 * OscillatorPwm.h
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

#ifndef OSCILLATORPWM_H_
#define OSCILLATORPWM_H_

#include <stdint.h>

/*
 * OscillatorPwm — computed pulse-width families for the `pwm` oscillator
 * type (S084 P1 P1-5.1).
 *
 * What:       family = OscInfo.source: OSC_PWM_PRC (default square: difference
 *             of two band-limited falling saws w apart, DC-free, peak-scaled),
 *             OSC_PWM_PSW (default saw: sum of two saws d apart, scaled
 *             1/(1-d)), OSC_PWM_PTR (triangle through a two-slope phase
 *             warp), OSC_PWM_PQ3 / OSC_PWM_PW3 (303 square / saw through the
 *             warp; in P1 they warp recTable / sawTable, P2 swaps in the sq3 /
 *             sw3 tables), OSC_PWM_SWP (one control: sine -> tri -> saw ->
 *             square -> pulse, two aligned reads + crossfade, then the prc
 *             pulse narrowing 50 % -> 4 %). Control c = waveform/127.
 * Why:        plan §4.1.3: no width tables; prc/psw/swp exactly band-limited;
 *             warped families pick the table level for the warp's worst
 *             compression so narrow widths never alias.
 * Inputs:     OscInfo (source, waveform, phase, phaseInc, phaseIncStep, fmMod,
 *             tableOffset, pwm).
 * Outputs:    rendered blocks; OscPwmState per block.
 * Accessors:  Oscillator.c dispatch and osc_setFreq().
 * Affiliates: wavetable.c tables (sawTable, triTable, recTable, sine_table),
 *             tools/plots/plot_pwm.py (reference shapes), ITCM renderers.
 */
#define OSC_PWM_PRC 0u
#define OSC_PWM_PSW 1u
#define OSC_PWM_PTR 2u
#define OSC_PWM_PQ3 3u
#define OSC_PWM_PW3 4u
#define OSC_PWM_SWP 5u
#define OSC_PWM_FAMILY_COUNT 6u

/*
 * Per-block pwm parameters (S084 P1 P1-5.1).
 *
 * What:       shift = phase offset of the second read (prc/psw, uint32 of a
 *             cycle); scale = output gain (peak 1); warpA/warpB = the two
 *             warp slopes (Q32 multipliers, computed once per block, no
 *             per-sample division); split = warp split in phase units; mix =
 *             swp crossfade fraction; seg = swp segment 0..3.
 *             mix: swp segment blend; warped families (ptr, pq3, pw3):
 *             natural split s0 (S084 VX2 VX2-4.9).
 * Why:        width-dependent constants computed once per block keep the
 *             per-sample loop free of divisions (constant cost).
 * Inputs:     oscPwm_prepare().  Outputs: read by the pwm renderers.
 * Accessors:  OscillatorPwm.c; lives inside OscInfo.
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
 * Compute one block's pwm width state (S084 P1 P1-5.1).
 *
 * What:       sets phaseIncTarget (1,024-point format), tableOffset for
 *             freqHz x compression (compression = max(s0/w, (1-s0)/(1-w)) for
 *             warped families, 1 otherwise) and every OscPwmState field from
 *             the control c = waveform/127.
 * Why:        all width-dependent divisions happen once per block; the
 *             per-sample loops stay division-free (constant CPU).
 * Inputs:     osc (source, waveform), freqHz (effective frequency).
 * Outputs:    osc->phaseIncTarget, osc->tableOffset, osc->pwm.
 * Accessors:  osc_setFreq().
 * Affiliates: Oscillator.c frequency helpers, osc_freqToTableIndex().
 */
void oscPwm_prepare(struct OscStruct* osc, float freqHz);

/*
 * Render one pwm oscillator block (S084 P1 P1-5.1).
 *
 * What:       about 30 instructions per sample (FM about 45): exactly two
 *             band-limited table reads (or four for the square shape of swp),
 *             the per-sample phase slew, and the prepared width state.
 * Why:        plan §4.1.3; a single hot loop per family keeps the cost
 *             predictable.
 * Inputs:     osc (phase state and pwm state), buf, size, gain.
 * Outputs:    buf scaled by gain; osc->phase and phaseInc advanced.
 * Accessors:  Oscillator.c type dispatch.
 * Affiliates: OscillatorPwm.c helpers, oscPwm_renderFm().
 */
void oscPwm_render(struct OscStruct* osc, int16_t* buf, uint8_t size,
                   float gain);

/*
 * Render one pwmed pwm oscillator block (S084 P1 P1-5.1).
 *
 * What:       oscPwm_render with the modulator block added to every table
 *             read as (mod x fmMod) << 19, the existing calcFmBlock depth.
 * Why:        keeps FM through the pwm families equal to the wavetable depth.
 * Inputs:     osc, modBuffer, buf, size, gain.
 * Outputs:    buf; phase state advanced.
 * Accessors:  Oscillator.c type dispatch.
 * Affiliates: oscPwm_render().
 */
void oscPwm_renderFm(struct OscStruct* osc, int16_t* modBuffer, int16_t* buf,
                     uint8_t size, float gain);

#endif /* OSCILLATORPWM_H_ */
