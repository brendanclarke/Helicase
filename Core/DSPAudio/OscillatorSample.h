/*
 * OscillatorSample — installed-sample playback for the smp/slp types
 * (S085 C6.1).
 *
 * What:       Q20.12 phase (index = phase >> 12): every installable sample
 *             (up to 1,048,575 frames) plays fully. smp plays once from the
 *             start position and outputs 0 after its last frame; slp loops
 *             [start, end). The start position is the mod row: start =
 *             mod x frames / 128 (linear, Q11). The wav row selects the
 *             sample. The catalogue gain code normalises the level.
 * Why:        plan §5 item 2, Q10 (smp ends at 0), Q11; the old phase >> 17
 *             index addressed only 32,768 frames (0.74 s).
 * Inputs:     OscInfo (oscType, waveform = sample index, waveRequest,
 *             source = start, phase, phaseInc, phaseIncStep, fmMod).
 * Outputs:    rendered blocks; OscInfo sample cache and bounds.
 * Accessors:  Oscillator.c dispatch and osc_setFreq(), the engines'
 *             triggers (restart), OscSection.c (sync restart, end test),
 *             InstrumentManager.c writers (bind).
 * Affiliates: SampleMemory.h (catalogue v2, gain code).
 */
#ifndef OSCILLATORSAMPLE_H_
#define OSCILLATORSAMPLE_H_

#include <stdint.h>
#include "config.h"

#define OSC_SAMPLE_PHASE_SHIFT 12u

struct OscStruct;

/*
 * Bind the selected sample (S085 C6.1).
 *
 * What:       live index = min(waveform, count - 1) (0 samples: silent,
 *             waveform 0); waveform is first re-expanded from waveRequest
 *             when the catalogue generation changed. Refreshes the cached
 *             pointer, frames and gain code when the index or the generation
 *             changed; recomputes sampleStart = (source x frames) >> 7 and
 *             sampleEnd = frames.
 * Why:        a smaller reinstall never moves the stored selection for good
 *             (S084 P1 F2); a reinstall never leaves a dangling pointer. It is
 *             a compare per block when nothing changed.
 * Inputs:     osc.  Outputs: waveform, sampleCache*, sampleGainCode,
 *             sampleStart, sampleEnd.
 * Accessors:  oscSample_render*() (every block), the IM type/source/wave
 *             writers.
 * Affiliates: sampleMemory_getSampleInfo(), sampleMemory_getGeneration().
 */
void oscSample_bind(struct OscStruct* osc);

/*
 * Restart at the start position (S085 C6.1).
 *
 * What:       phase = sampleStart << OSC_SAMPLE_PHASE_SHIFT.
 * Accessors:  the engines' trigger (rst, itp carrier), OscSection.c (snc
 *             slave reset).
 */
void oscSample_restart(struct OscStruct* osc);

/*
 * Whether a one-shot sample has played out (S085 C6.1).
 *
 * What:       1 for an smp oscillator whose read index has reached
 *             sampleEnd - 1 (it then outputs 0); 0 otherwise, and always 0
 *             for slp and other types.
 * Why:        Q10: an snc slave stops resetting once its one-shot master has
 *             ended (S085 D6); the same test drives the 0 output.
 * Accessors:  OscSection.c snc, oscSample_render*().
 */
uint8_t oscSample_isFinished(const struct OscStruct* osc);

/*
 * Phase increment for a playback frequency (S085 C6.1).
 *
 * What:       f x 2^22 / REAL_FS: 1,024 x f / FS frames per sample in Q20.12,
 *             the same playback ratio as the former freq2PhaseIncr32767().
 * Accessors:  osc_setFreq().
 */
uint32_t oscSample_phaseIncForFreq(float f);

/*
 * Render one sample block (S085 C6.1).
 *
 * What:       per sample: index clamp, loop wrap (slp) or end hold (smp), two
 *             reads, linear interpolation, gain; the per-sample slew. The FM
 *             variant adds (mod x fmMod) << 9 to the read phase (Q20.12; the
 *             same depth as the former << 14 at >> 17).
 * Why:        constant cost per sample: the wrap and end tests always run;
 *             the catalogue gain is folded into the block gain once.
 * Inputs:     osc, (modBuffer), buf, size, gain.
 * Outputs:    buf; osc->phase, osc->phaseInc.
 * Accessors:  Oscillator.c type dispatch.
 */
void oscSample_render(struct OscStruct* osc, int16_t* buf, uint8_t size,
                      float gain);
void oscSample_renderFm(struct OscStruct* osc, int16_t* modBuffer,
                        int16_t* buf, uint8_t size, float gain);

/* Hard-sync slave render (S085 assessment A2); see OscSyncLane.h. */
struct OscSyncLane;
void oscSample_renderSync(struct OscStruct* osc, int16_t* buf, uint8_t size,
                          float gain, const struct OscSyncLane* lane);

#endif /* OSCILLATORSAMPLE_H_ */
