/*
 * Frozen engine oscillator lines for osc_s0 (S085 assessment A7).
 *
 * What:       OldVoice carries the engine fields the pre-S085 oscillator
 *             lines read (osc, modOsc, modOsc2, mixOscs, fmModAmount*), and
 *             old_dispatch()/old_dispatchFm() stand in for the pre-S085
 *             calcNextOscSample(Fm)Block() dispatchers, routing by waveform to
 *             the frozen85 loops (old_calc*Block). s0_engines.c wraps the
 *             verbatim frozen85 lines of DrumVoice.c, CymbalVoice.c and
 *             HiHat.c (extracted with extract.py --lines, dispatcher names
 *             renamed) in old_drumOsc(), old_cymOsc() and old_hatOsc().
 * Why:        the S0 claim for Drum fm/mix and Cymbal/HiHat FM2 must compare
 *             against the frozen engine code itself (composition order,
 *             gains, saturating sums, the HiHat 0.5 carrier gain), not a
 *             transcription written in the test.
 * Accessors:  test_osc_s0.c, s0_engines.c.
 */
#ifndef S0_ENGINE_SHIM_H_
#define S0_ENGINE_SHIM_H_

#include <stdint.h>
#include "Oscillator.h"

typedef struct {
    OscInfo osc;
    OscInfo modOsc;
    OscInfo modOsc2;
    uint8_t mixOscs;
    float   fmModAmount;
    float   fmModAmount1;
    float   fmModAmount2;
} OldVoice;

void old_dispatch(OscInfo *osc, int16_t *buf, uint8_t size, float gain);
void old_dispatchFm(OscInfo *osc, int16_t *mod, int16_t *buf, uint8_t size,
                    float gain);

void old_drumOsc(OldVoice *voice, int16_t *buf, int16_t *modBuf, uint8_t size);
void old_cymOsc(OldVoice *voice, int16_t *buf, int16_t *mod, int16_t *mod2,
                uint8_t size);
void old_hatOsc(OldVoice *voice, int16_t *buf, int16_t *mod1, int16_t *mod2,
                uint8_t size);

#endif
