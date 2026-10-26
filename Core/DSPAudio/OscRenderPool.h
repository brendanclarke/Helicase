/*
 * OscRenderPool.h
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

#ifndef OSCRENDEPOOL_H_
#define OSCRENDEPOOL_H_

#include <stdint.h>

/*
 * OscRenderPool — per-block budget for extra oscillator renders
 * (S084 P1 P1-6.2).
 *
 * What:       oscRenderPool_beginBlock() resets the budget at the top of each
 *             mixer block; oscRenderPool_claim(n) grants n units if they fit;
 *             the two scratch blocks hold an `itp` voice's old-section output
 *             and its old modulator block while it renders.
 * Why:        `itp` must outrank waveform interpolation and stay within a fixed
 *             worst case (plan §4.1.6).
 * Inputs:     claims from instrumentManager_claimOscSectionUnits() (itp) and
 *             modNode_updateValue() (waveform interpolation).
 * Outputs:    grants; scratch pointers valid for one engine render call.
 * Accessors:  mixer.c, InstrumentManager.c, modulationNode.c, OscSection.c.
 * Affiliates: OSC_RENDER_POOL_UNITS (config.h). 129 B SRAM1 (§0.4).
 */

/*
 * Reset the per-block budget (S084 P1 P1-6.2).
 *
 * What:       clears the used-unit count.
 * Why:        every mixer block starts with the full pool.
 * Inputs:     none.  Outputs: oscRenderPool_unitsUsed = 0.
 * Accessors:  mixer_calcNextSampleBlock() (before the itp claims).
 * Affiliates: instrumentManager_claimOscSectionUnits(),
 *             modNode_resetTargets().
 */
void oscRenderPool_beginBlock(void);

/*
 * Claim n render units (S084 P1 P1-6.2).
 *
 * What:       returns 1 and consumes n units when they fit, else 0.
 * Why:        `itp` and waveform interpolation share one fixed worst case.
 * Inputs:     units (0..OSC_RENDER_POOL_UNITS).
 * Outputs:    1 granted / 0 refused.
 * Accessors:  oscSection_claimPending(), modNode_updateValue().
 * Affiliates: OSC_RENDER_POOL_UNITS.
 */
uint8_t oscRenderPool_claim(uint8_t units);

/*
 * Scratch block for an `itp` old-section render (S084 P1 P1-6.2).
 *
 * What:       returns the OUTPUT_DMA_SIZE block the active crossfade renders
 *             into; the scratch is shared sequentially because engines render
 *             one slot at a time.
 * Why:        the old section must keep rendering beside the new one without a
 *             second persistent buffer per voice.
 * Inputs:     none.  Outputs: pointer valid for one engine render call.
 * Accessors:  DrumVoice.c, Snare.c, CymbalVoice.c, HiHat.c,
 *             OscSection.c.
 * Affiliates: oscRenderPool_scratchMod().
 */
int16_t *oscRenderPool_scratchSection(void);

/*
 * Scratch block for an `itp` old-modulator render (S084 P1 P1-6.2).
 *
 * What:       returns the OUTPUT_DMA_SIZE modulator block paired with
 *             oscRenderPool_scratchSection().
 * Why:        the old section's combination modes need a modulator block.
 * Inputs:     none.  Outputs: pointer valid for one engine render call.
 * Accessors:  the engine render functions, OscSection.c.
 * Affiliates: oscRenderPool_scratchSection().
 */
int16_t *oscRenderPool_scratchMod(void);

#endif /* OSCRENDEPOOL_H_ */
