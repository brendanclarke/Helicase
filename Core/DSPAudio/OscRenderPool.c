/*
 * OscRenderPool.c
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

#include "OscRenderPool.h"
#include "config.h"

/*
 * Shared oscillator render pool state (S084 P1 P1-6.3).
 *
 * What:       unitsUsed is the number of extra oscillator renders granted this
 *             block; the two scratch blocks hold an `itp` voice's old section
 *             output and its old modulator block.
 * Why:        one fixed, always-budgeted pool replaces the two-unit waveform
 *             interpolation counter (plan §4.1.6, r5 Q-N1).
 * Inputs:     mixer_calcNextSampleBlock() (beginBlock and itp claims),
 *             modulationNode.c (interpolation claims).
 * Outputs:    claim results and scratch pointers.
 * Accessors:  oscillators and engines; foreground render context only.
 * Affiliates: OSC_RENDER_POOL_UNITS, InstrumentManager.c.
 */
static uint8_t oscRenderPool_unitsUsed;
static int16_t oscRenderPool_scratchSectionBuf[OUTPUT_DMA_SIZE];
static int16_t oscRenderPool_scratchModBuf[OUTPUT_DMA_SIZE];

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
void oscRenderPool_beginBlock(void)
{
	oscRenderPool_unitsUsed = 0u;
}

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
uint8_t oscRenderPool_claim(uint8_t units)
{
	if ((uint16_t)oscRenderPool_unitsUsed + (uint16_t)units >
		OSC_RENDER_POOL_UNITS)
		return 0u;
	oscRenderPool_unitsUsed =
		(uint8_t)(oscRenderPool_unitsUsed + units);
	return 1u;
}

/*
 * Scratch block for an `itp` old-section render (S084 P1 P1-6.2).
 *
 * What:       returns the OUTPUT_DMA_SIZE block the active crossfade renders
 *             into.
 * Why:        the old section must keep rendering beside the new one without a
 *             second persistent buffer per voice.
 * Inputs:     none.  Outputs: pointer valid for one engine render call.
 * Accessors:  DrumVoice.c, Snare.c, CymbalVoice.c, HiHat.c,
 *             OscSection.c.
 * Affiliates: oscRenderPool_scratchMod().
 */
int16_t *oscRenderPool_scratchSection(void)
{
	return oscRenderPool_scratchSectionBuf;
}

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
int16_t *oscRenderPool_scratchMod(void)
{
	return oscRenderPool_scratchModBuf;
}
