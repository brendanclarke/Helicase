#include "OscRenderPool.h"
#include "config.h"

/*
 * OscRenderPool.c (S085 C9.2).
 *
 * What:       the 2-unit block budget and the one old-carrier scratch block.
 * Why:        the pool replaces the two-slot interpolation limit and is
 *             budgeted full (constant-CPU rule).
 * Accessors:  mixer.c (begin), modulationNode.c (interpolation),
 *             InstrumentManager.c (itp), the engines (scratch).
 * Affiliates: OscSection.c, config.h OSC_RENDER_POOL_UNITS.
 */

/*
 * Pool state (S085 C9.2).
 *
 * What:       units granted this block, and the old-carrier scratch.
 * Why:        approved SRAM1 (S085 §0.5: 64 B + 1 B).
 * Writers:    beginBlock/claim; the engines write the scratch.
 */
static uint8_t oscRenderPool_unitsUsed;
static int16_t oscRenderPool_scratchBuf[OUTPUT_DMA_SIZE];

/*
 * Reset the pool for a new block (S085 C9.2).
 * Accessors: mixer_calcNextSampleBlock().
 */
void oscRenderPool_beginBlock(void)
{
	oscRenderPool_unitsUsed = 0u;
}

/*
 * Claim units (S085 C9.2).
 * What: grants units when they fit; refused claims count nothing.
 * Accessors: OscSection.c, modulationNode.c.
 */
uint8_t oscRenderPool_claim(uint8_t units)
{
	if ((uint16_t)oscRenderPool_unitsUsed + units > OSC_RENDER_POOL_UNITS)
		return 0u;
	oscRenderPool_unitsUsed = (uint8_t)(oscRenderPool_unitsUsed + units);
	return 1u;
}

/*
 * Old-carrier scratch (S085 C9.2).
 * What: one OUTPUT_DMA_SIZE int16 block; voices render one at a time.
 * Accessors: the engines' render functions.
 */
int16_t *oscRenderPool_scratch(void)
{
	return oscRenderPool_scratchBuf;
}
