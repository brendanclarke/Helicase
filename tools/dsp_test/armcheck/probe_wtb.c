/*
 * ARM cost probe: wtb (S085 C17.4, A3).
 *
 * What:       includes OscillatorWavetable.c; plain, FM and lane wrappers.
 *             The stored depth is read after the bind, so each wrapper holds
 *             two innermost loops: the 8-bit and the 16-bit read (osccost.py
 *             reports both; the larger is the worst case).
 * Accessors:  make -C tools/dsp_test armcheck-osc (osccost.py).
 */
#include "OscillatorWavetable.c"

#define PROBE __attribute__((noinline))

PROBE void p_wtb(struct OscStruct *o, int16_t *b, uint8_t n)
{
	oscWavetable_block(o, 0, b, n, 1.0f, 0);
}

PROBE void p_wtb_fm(struct OscStruct *o, const int16_t *m, int16_t *b,
                    uint8_t n)
{
	oscWavetable_block(o, m, b, n, 1.0f, 0);
}

OSC_SHARED_LOOP void p_wtb_lane(struct OscStruct *o, int16_t *b, uint8_t n,
                                const OscSyncLane *ln)
{
	oscWavetable_block(o, 0, b, n, 1.0f, ln);
}
