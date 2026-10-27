/*
 * OscRenderPool — the shared budget of extra oscillator renders
 * (S085 C9.1).
 *
 * What:       OSC_RENDER_POOL_UNITS (2) units per 32-frame block. One unit is
 *             one extra oscillator render: an itp carrier crossfade (one per
 *             voice) or one waveform interpolation. The pool resets at block
 *             start. Claim order per block (C9.4): ongoing itp crossfades,
 *             then waveform interpolation (as modulation is applied), then new
 *             itp crossfades, lowest slot first. One scratch block holds the
 *             old carrier of the voice being rendered (voices render one at a
 *             time).
 * Why:        plan §4.3.7 / Q14: the worst case is fixed at two extra renders
 *             per block, budgeted as always used.
 * Inputs:     claims.  Outputs: grants; the scratch block.
 * Accessors:  mixer.c (beginBlock), InstrumentManager.c (itp claims),
 *             modulationNode.c (interpolation claims), the engines (scratch).
 * Affiliates: config.h OSC_RENDER_POOL_UNITS, OscSection.c.
 */
#ifndef OSCRENDERPOOL_H_
#define OSCRENDERPOOL_H_

#include <stdint.h>

/* Reset the pool for a new block. Accessor: mixer_calcNextSampleBlock(). */
void     oscRenderPool_beginBlock(void);

/*
 * Claim units (S085 C9.1).
 * What: grants `units` when they fit in what is left this block.
 * Outputs: 1 granted (units counted), 0 refused (nothing counted).
 */
uint8_t  oscRenderPool_claim(uint8_t units);

/*
 * Old-carrier scratch (S085 C9.1).
 * What: one OUTPUT_DMA_SIZE int16 block for the itp copy of the voice being
 * rendered. Accessors: the engines' render functions, OscSection.c.
 */
int16_t *oscRenderPool_scratch(void);

#endif /* OSCRENDERPOOL_H_ */
