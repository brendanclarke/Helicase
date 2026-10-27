/*
 * ARM cost probe: wav loops and their hard-sync forms (S085 C17.4, A3).
 *
 * What:       includes Oscillator.c and adds one noinline wrapper per case
 *             with every block constant fixed, so each wrapper's innermost
 *             loop is that case's per-sample body. The plain table/sine/noise/
 *             crash/stock loops are the firmware functions themselves; the
 *             p_*_sync wrappers inline the always-inline cores with a lane.
 * Accessors:  make -C tools/dsp_test armcheck-osc (osccost.py).
 */
#include "Oscillator.c"

#define PROBE __attribute__((noinline))

PROBE void p_table_sync(OscInfo *o, int16_t *b, uint8_t n,
                        const uint8_t *w, const uint32_t *ov, float r)
{
	const OscSyncLane l = { w, ov, r };
	calcWavetableOscCore(o, sawTable, b, n, 1.0f, &l);
}

PROBE void p_sine_sync(OscInfo *o, int16_t *b, uint8_t n,
                       const uint8_t *w, const uint32_t *ov, float r)
{
	const OscSyncLane l = { w, ov, r };
	calcSineCore(o, b, n, 1.0f, &l);
}

PROBE void p_noise_sync(OscInfo *o, int16_t *b, uint8_t n,
                        const uint8_t *w, const uint32_t *ov, float r)
{
	const OscSyncLane l = { w, ov, r };
	calcNoiseCore(o, b, n, 1.0f, &l);
}

PROBE void p_crash_sync(OscInfo *o, int16_t *b, uint8_t n,
                        const uint8_t *w, const uint32_t *ov, float r)
{
	const OscSyncLane l = { w, ov, r };
	calcSampleOscCore(o, b, n, 1.0f, &l);
}

PROBE void p_stock_sync(OscInfo *o, int16_t *b, uint8_t n,
                        const uint8_t *w, const uint32_t *ov, float r)
{
	const OscSyncLane l = { w, ov, r };
	calcStockWaveCore(o, b, n, 1.0f, &l);
}
