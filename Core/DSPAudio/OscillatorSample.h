/*
 * OscillatorSample.h
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

#ifndef OSCILLATORSAMPLE_H_
#define OSCILLATORSAMPLE_H_

#include <stdint.h>
#include "config.h"

/*
 * OscillatorSample — installed-sample playback for the smp/slp oscillator
 * types (S084 P1 P1-4.1).
 *
 * What:       Q20.12 phase (index = phase >> OSC_SAMPLE_PHASE_SHIFT) so every
 *             installable sample (<= 654,160 frames < 2^20) plays fully;
 *             one-shot (smp) holds at the end; looped (slp) wraps
 *             [sampleStart, sampleEnd) by compare-and-subtract; the start
 *             position is the oscillator's `wav` value 0..127 mapped onto the
 *             sample length; FM phase modulation as before at equal depth.
 * Why:        plan §4.1.5: the old `phase >> 17` index addressed only 32,768
 *             frames (0.74 s) and wrapped longer samples; loop becomes a
 *             per-oscillator choice; start position is sweepable.
 * Inputs:     OscInfo (oscType, source, waveform, phase, phaseInc,
 *             phaseIncStep, fmMod); the v1 sample catalogue (SampleMemory).
 * Outputs:    rendered blocks; OscInfo sample cache and bounds.
 * Accessors:  Oscillator.c dispatch, osc_setFreq(), OscSection.c (restart,
 *             sync reset), InstrumentManager.c writers.
 * Affiliates: SampleMemory.c (catalogue v1 in P1, v2 in P2); ITCM renderers.
 */
#define OSC_SAMPLE_PHASE_SHIFT 12u

/*
 * Render one installed-sample block, one-shot or looped (S084 P1 P1-4.1).
 *
 * What:       two reads plus interpolation per sample; the loop wrap is always
 *             evaluated (constant cost). `slp` wraps [sampleStart,
 *             sampleEnd); `smp` holds the last frame.
 * Why:        plan §4.1.5 Q20.12 sample phase; the type selects looping, not a
 *             per-sample install flag.
 * Inputs:     osc (phase, phaseInc, phaseIncStep, bounds, catalogue cache).
 * Outputs:    buf scaled by gain; phase/phaseInc advanced.
 * Accessors:  Oscillator.c type dispatch.
 * Affiliates: oscSample_bind(), oscSample_restart(), oscSample_renderFm().
 */
void oscSample_render(struct OscStruct* osc, int16_t* buf, uint8_t size,
                      float gain);

/*
 * Render one installed-sample block with FM (S084 P1 P1-4.1).
 *
 * What:       oscSample_render with the modulator offset
 *             (mod x fmMod) << 9 in Q20.12, equal depth to the old << 14 at
 *             >> 17.
 * Inputs:     osc, modBuffer, buf, size, gain.
 * Outputs:    buf; phase/phaseInc advanced.
 * Accessors:  Oscillator.c type dispatch.
 * Affiliates: oscSample_render().
 */
void oscSample_renderFm(struct OscStruct* osc, int16_t* modBuffer,
                        int16_t* buf, uint8_t size, float gain);

/*
 * Phase increment for a playback frequency (S084 P1 P1-4.1).
 *
 * What:       returns f x 2^22 / REAL_FS: the same playback ratio as the old
 *             freq2PhaseIncr32767() (1,024 x f / FS frames per sample), now
 *             with 12 fraction bits.
 * Why:        Q20.12 needs the frame index and its fraction in one integer.
 * Inputs:     f (effective frequency Hz).  Outputs: phase increment.
 * Accessors:  osc_setFreq().
 * Affiliates: oscSample_render().
 */
uint32_t oscSample_phaseIncForFreq(float f);

/*
 * Bind the selected installed sample to an oscillator (S084 P1 P1-4.1).
 *
 * What:       refreshes the cached pointer and size for `source` when the
 *             catalogue generation, the source or the type changed; computes
 *             sampleStart = (waveform x size) >> 7 and sampleEnd = size;
 *             clamps `source` to the installed count (0 samples -> silent
 *             binding with size 0).
 * Why:        replaces osc_updateSampleCache(); the type now decides looping
 *             and `wav` decides the start position.
 * Inputs:     osc.  Outputs: cache fields, sampleStart, sampleEnd, source.
 * Accessors:  InstrumentManager writers, the renderers' first-block check.
 * Affiliates: SampleMemory.c.
 */
void oscSample_bind(struct OscStruct* osc);

/*
 * Restart a sample oscillator at its start position (S084 P1 P1-4.1).
 *
 * What:       phase = sampleStart << OSC_SAMPLE_PHASE_SHIFT.
 * Why:        the trigger (`rst`, `itp`) and the `snc` slave reset start the
 *             sample from its configured start, not frame 0.
 * Inputs:     osc.  Outputs: osc->phase.
 * Accessors:  engines' trigger paths, OscSection.c sync.
 * Affiliates: oscSample_bind().
 */
void oscSample_restart(struct OscStruct* osc);

#endif /* OSCILLATORSAMPLE_H_ */
