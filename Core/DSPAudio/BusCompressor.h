/*
 * Core/DSPAudio/BusCompressor.h
 *
 *  Created on: 29.09.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef BUS_COMPRESSOR_H_
#define BUS_COMPRESSOR_H_

#include <stdint.h>
#include "sample_mix.h"

/*
 * Master bus compressor (S074): one Scene-owned, soft-knee, RMS, optical
 * compressor on the St1 or St2 output pair, with a trigger sidechain.
 *
 * What:       a master-bus stage, not an Effect slot. The active Scene's
 *             four settings (cmp target, cam amount, ctm time, csc sidechain)
 *             are read every block; nothing is pushed into this module.
 * Why:        levelling on one output pair, at a CPU cost counted against the
 *             worst-case Scene only while it is on.
 * Ownership:  BusCompressor.c owns its 24 B DTCM state; SceneData owns the
 *             settings; the mixer and trigger funnel are the only callers,
 *             both in the foreground.
 * Affiliates: mixer_calcNextSampleBlock(), voiceControl_triggerNow(),
 *             S074_BUS_COMP.md, EFFECTS_MIXER_DSP_REFERENCE.md.
 */

/*
 * Process one 32-frame block of the selected pair in place.
 *
 * Inputs:     st1 = DAC1 interleaved buffer (the mixer's `output2`: MAIN
 *             jacks and headphones); st2 = DAC2 buffer (`output`: OUT2).
 *             Both hold signed 24-bit scale in int32 (1.0 = 8,388,352)
 *             after every voice and the FX return are summed.
 *             scene_index = the active Scene.
 * Outputs:    the selected pair is compressed, made up, saturated and
 *             soft-limited in place: the cubic saturates the low band plus a
 *             quarter of the high band (crossover ~2 kHz), the rest of the
 *             highs bypass it, and a smooth knee from -2.5 dBFS holds the sum
 *             at or below full scale (S074_COMP_SAT_UPDATE.md). The other
 *             pair is untouched. With cmp off and no fade
 *             pending the call returns after one settings read and clears
 *             any pending sidechain weight.
 * Transitions: off/on and target changes fade over one block; St1<->St2
 *              fades the old pair out, then the new pair in.
 * Caller:     mixer_calcNextSampleBlock(), once per block.
 */
void busComp_processBlock(sample_mx_t *st1, sample_mx_t *st2,
                          uint8_t scene_index);

/*
 * Sidechain intake: one trigger from the shared trigger funnel.
 *
 * Inputs:     track = zero-based visible track 0..6; track 7 (index 6)
 *             counts as voice 6 (BC11 working assumption); velocity 0..127.
 * Outputs:    when track matches the active Scene's csc and velocity > 0,
 *             records the largest weight (velocity/127)^3 since the last
 *             block. busComp_processBlock() applies it once and clears it.
 * Caller:     voiceControl_triggerNow(), drained before each mixer block, so
 *             the duck lands in the voice's own block.
 */
void busComp_sidechainTrigger(uint8_t track, uint8_t velocity);

#endif /* BUS_COMPRESSOR_H_ */
