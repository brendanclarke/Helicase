/*
 * Core/DSP/Effects/CrumpBit/CrumpBitEffect.h
 *
 *  Created on: 29.09.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */

#ifndef CRUMP_BIT_EFFECT_H_
#define CRUMP_BIT_EFFECT_H_

#include "EffectsManager.h"

/*
 * CrumpBit loop geometry (S074_CRUMPBIT_EFFECT.md §3.4).
 *
 * CRUMPBIT_DELAY_MAX_SAMPLES: the longest delay, 1.60 s at 44,108 Hz. Rate 0
 * and every Sync division are clamped to it. It is fixed (not
 * share-dependent) so a Scene sounds the same whatever voice units future
 * instruments claim.
 * CRUMPBIT_BUFFER_BYTES: the loop length actually used, 1.60 s plus a
 * 2-sample interpolation guard, rounded up to 32 B. One byte per 8-bit mono
 * sample. It must stay <= the minimum Effect share (73,632 B with all twelve
 * voice units claimed). It is declared as buffer_min_bytes and
 * buffer_pref_bytes in the registry row.
 * Accessors: CrumpBitEffect.c, EffectsManager.c (registry row).
 */
#define CRUMPBIT_DELAY_MAX_SAMPLES   70573.0f
#define CRUMPBIT_BUFFER_BYTES        70592u

/*
 * CrumpBit runtime (56 B, a member of EffectsManager's DTCM runtime union).
 *
 * What: the per-sample DSP state and the last resolved row values. Audio (the
 * 8-bit loop) lives in the FxBuffer Effect share, never here.
 * - length: the loop length in samples/bytes (0 = not seated yet);
 * - write_pos: the next loop index to write (0..length-1);
 * - valid: samples written since the loop was last treated as empty,
 *   saturating at length. Reads further back than this are muted, which
 *   gives "clear unless you adopt" without a bulk clear;
 * - delay: the current delay in samples, gliding towards the per-block
 *   target (tape style);
 * - mix, feedback, gain_l, gain_r: block-ramp values (end of the previous
 *   block);
 * - ac_x_*, ac_y_*: the AC-coupling (one-pole high-pass) state per channel;
 * - bit_off .. pan_raw: the last row values from write_param();
 * - primed: 0 until the first block sets delay directly to its target (no
 *   glide on entry).
 * Why here: EffectsManager zeroes the union and calls init() when the type
 * becomes active; the union is 76 B (StereoFilter), so 56 B needs no RAM.
 * Accessors: CrumpBitEffect.c only; no other module may keep a pointer.
 * Affiliates: effects_runtime_t, M4 (size guard),
 * EFFECTS_MIXER_DSP_REFERENCE.md §4.2.
 */
typedef struct {
    uint32_t length;
    uint32_t write_pos;
    uint32_t valid;
    float delay;
    float mix;
    float feedback;
    float gain_l;
    float gain_r;
    float ac_x_l;
    float ac_x_r;
    float ac_y_l;
    float ac_y_r;
    uint8_t bit_off;
    uint8_t bit_invert;
    uint8_t mix_raw;
    uint8_t feedback_raw;
    uint8_t rate;
    uint8_t sync;
    uint8_t pan_raw;
    uint8_t primed;
} CrumpBitRuntime;

/* DSP operations for the registry row (init, export, write, process, share). */
extern const effect_type_ops_t crumpBit_ops;

/*
 * Free-running delay for one Rate value.
 *
 * What: t = 1.60 s * 80^(-rate/127) in samples: rate 0 = 1.60 s (70,573),
 * rate 127 = 20 ms (882), about 20 steps per octave of time. Higher rate
 * means faster tape and a shorter delay (Q5). Why shared: the DSP and the
 * Effect-page Sync label must use one curve. Input: rate 0..127 (larger
 * values are treated as 127). Output: delay in samples.
 * Accessors: crumpBit_syncDivision(), CrumpBitEffect.c per-block target.
 * Affiliates: CRUMPBIT_DELAY_MAX_SAMPLES.
 */
float crumpBit_rateSamples(uint8_t rate);

/*
 * Step-scale division that Sync snaps a Rate to (Q7).
 *
 * What: among the shared StepScale divisions (/64 .. 2br, 96 PPQ ticks)
 * whose length at `bpm` fits CRUMPBIT_DELAY_MAX_SAMPLES, returns the index
 * nearest in log time to crumpBit_rateSamples(rate). It uses the bracketing
 * pair and the geometric-mean rule: no logarithms, and a constant 14-step
 * loop. If no division fits (below about 4 BPM) it returns 0, and the DSP
 * clamps that length to the maximum. Why shared: the Effect-page label must
 * show exactly the division the DSP plays. Inputs: rate 0..127, bpm from
 * seq_getBpm() (0 is treated as 1). Output: a StepScale index 0..13.
 * Accessors: crumpBit_uiFormatValue3() (label), CrumpBitEffect.c (target).
 * Affiliates: stepScale_ticks(), stepScale_shortName().
 */
uint8_t crumpBit_syncDivision(uint8_t rate, uint16_t bpm);

#endif /* CRUMP_BIT_EFFECT_H_ */
