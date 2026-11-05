/*
 * Core/DSP/Effects/CrumpBit/CrumpBitTables.h
 *
 *  Created on: 04.11.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */

#ifndef CRUMP_BIT_TABLES_H_
#define CRUMP_BIT_TABLES_H_

#include <stdint.h>   /* uint8_t: crumpBit_grainStopCc (S091 P4) */

/*
 * CrumpBit control tables (S091 P2; generated, CrumpBitTables.c).
 *
 * What: three 128-entry float tables indexed by a 0..127 row value.
 * - crumpBit_rateTable[rte]: delay length in samples, 882 (20 ms) at 0 to
 *   126,654 (2.871 s: the arena less one 32 B granule and the 2-sample
 *   guard, S091 P3 N1) at 127, exponential and
 *   strictly increasing. Higher `rte` = longer delay.
 * - crumpBit_toneK[ton]: the one-pole coefficient of the feedback-return
 *   tilt. 0..62 low-pass (150 Hz .. 18 kHz), 63 exactly 1.0 (flat),
 *   64..127 high-pass (20 Hz .. 2 kHz).
 * - crumpBit_filterG[frq]: the TPT pre-warped cutoff g of the output filter,
 *   the voice cutoff law (fastTan(pi * 0.45 * valueShaperF2F(frq/127,
 *   FILTER_SHAPER))). 0 at frq 0 (closed), strictly increasing.
 * S091 P3 sub-type tables:
 * - crumpBit_combTable[rte]: `cmb` comb length in samples. rte n plays MIDI
 *   note 112 - n (n clamped at 96), L = fs / f, 8.36 .. 2,141 samples: a
 *   higher rte is a longer comb and a lower note (plan A1).
 * - crumpBit_pitchWindow[rte]: `pit` window in samples, 441 .. 8,822
 *   (10 .. 200 ms), exponential.
 * - crumpBit_semitoneRatio[s]: 2^((s - 24)/12), s = 0..48 (-24..+24 st).
 * - crumpBit_grainFraction[m]: S091 P4: `grn` grain Length / N (the record
 *   line), 2^(-8 (1 - m/64)) = 1/256 .. 1 for m = |len - 63| = 0..64; the
 *   DSP holds a grain at 220 samples (~5 ms) or more (plan Q1).
 * - crumpBit_reverbSize[rte]: `rvb` delay scale 0.25 .. 2.5.
 * - crumpBit_reverbDecay[fbk]: `rvb` log2 of the per-sample line gain for an
 *   RT60 of 0.1 .. 10 s; a line of d samples has gain 2^(entry * d).
 * S091 P4 `grn` tables:
 * - crumpBit_grainTime[rte]: the delay time in 96-PPQ ticks (samples = ticks
 *   x CRUMPBIT_SAMPLES_PER_TICK / BPM), 6 (1/64) .. 384 (one bar): a
 *   continuous log curve whose 16 rhythmic stops sit exactly on their tick
 *   counts (plan Q8, Q10).
 * - crumpBit_grainStopCc[k]: the `rte` position of stop k (k = 0 /64 ..
 *   15 1br); Position and Density pick stops by k, the page labels them.
 * - crumpBit_grainDensity[d]: negative Density d = 1..63 -> the mean
 *   trigger interval as a fraction of N (1 at d = 1, ~20 ms at d = 63 with
 *   the whole arena; plan Q6). Entry 0 is unused.
 * S091 P5 output filter table (plan F1, Q6):
 * - crumpBit_onePoleK[frq]: the one-pole coefficient k (s += k (x - s)) of
 *   the `grn`/`rvb` output filter, matched so its -3 dB point is the TPT
 *   SVF's at `res` 0 for the same index (crumpBit_filterG[frq]); 0 at frq 0
 *   (closed), strictly increasing, below 1. A lookup replaces two divides
 *   per block.
 * Why tables: CrumpBit's per-block work runs inside the render
 * (mixer_calcNextSampleBlock()); a lookup replaces an expf, a fastTan
 * division and a valueShaper division per block, and the Rate table makes the
 * share clamp and the DSP use bit-identical lengths.
 * Regenerate with `python3 tools/plots/gen_crumpbit_tables.py` from the
 * repository root; `make -C tools/dsp_test crumpbit_tables` checks that the
 * committed file matches the generator byte for byte.
 * Accessors: CrumpBitEffect.c only.
 * Affiliates: tools/plots/gen_crumpbit_tables.py.
 */
#define CRUMPBIT_TABLE_SIZE      128u
#define CRUMPBIT_SEMITONE_SPAN   24u   /* semitoneRatio covers +-24 */
#define CRUMPBIT_GRAIN_STEPS     64u   /* grainFraction covers 0..64 */
#define CRUMPBIT_GRAIN_STOPS     16u   /* grainStopCc: /64 .. 1br (P4) */
#define CRUMPBIT_DENSITY_STEPS   63u   /* grainDensity covers 1..63 (P4) */

extern const float crumpBit_rateTable[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_toneK[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_filterG[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_combTable[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_pitchWindow[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_semitoneRatio[2u * CRUMPBIT_SEMITONE_SPAN + 1u];
extern const float crumpBit_grainFraction[CRUMPBIT_GRAIN_STEPS + 1u];
extern const float crumpBit_reverbSize[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_reverbDecay[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_grainTime[CRUMPBIT_TABLE_SIZE];
extern const uint8_t crumpBit_grainStopCc[CRUMPBIT_GRAIN_STOPS];
extern const float crumpBit_grainDensity[CRUMPBIT_DENSITY_STEPS + 1u];
extern const float crumpBit_onePoleK[CRUMPBIT_TABLE_SIZE];

#endif /* CRUMP_BIT_TABLES_H_ */
