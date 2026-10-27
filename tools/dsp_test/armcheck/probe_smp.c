/*
 * ARM cost probe: smp/slp (S085 C17.4, A3).
 *
 * What:       includes OscillatorSample.c; plain, FM and lane wrappers for
 *             the one-shot and the looped type, the type fixed on a local
 *             copy so the per-sample type test folds.
 * Accessors:  make -C tools/dsp_test armcheck-osc (osccost.py).
 */
#include "OscillatorSample.c"

#define PROBE __attribute__((noinline))

#define SMP_CASE(name, type)                                              \
PROBE void p_##name(struct OscStruct *o, int16_t *b, uint8_t n)           \
{                                                                         \
	struct OscStruct l = *o;                                              \
	l.oscType = (type);                                                   \
	oscSample_block(&l, 0, b, n, 1.0f, 0);                                \
	o->phase = l.phase;                                                   \
}                                                                         \
PROBE void p_##name##_fm(struct OscStruct *o, const int16_t *m,           \
                         int16_t *b, uint8_t n)                           \
{                                                                         \
	struct OscStruct l = *o;                                              \
	l.oscType = (type);                                                   \
	oscSample_block(&l, m, b, n, 1.0f, 0);                                \
	o->phase = l.phase;                                                   \
}                                                                         \
OSC_SHARED_LOOP void p_##name##_lane(struct OscStruct *o, int16_t *b,     \
                                     uint8_t n, const OscSyncLane *ln)    \
{                                                                         \
	struct OscStruct l = *o;                                              \
	l.oscType = (type);                                                   \
	oscSample_block(&l, 0, b, n, 1.0f, ln);                               \
	o->phase = l.phase;                                                   \
}

SMP_CASE(smp, OSC_TYPE_SMP)
SMP_CASE(slp, OSC_TYPE_SLP)
