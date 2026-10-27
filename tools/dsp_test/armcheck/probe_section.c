/*
 * ARM cost probe: oscillator-section loops (S085 C17.4, A3).
 *
 * What:       includes OscSection.c; wrappers for the snc lane builder with
 *             a full-wrap master and with a sample master, the snc render
 *             (lane loop and mix pass),
 *             the rng law and the itp blend.
 * Accessors:  make -C tools/dsp_test armcheck-osc (osccost.py).
 */
#include "OscSection.c"

#define PROBE __attribute__((noinline))

PROBE void p_lane_table(OscInfo *m, uint8_t n, uint8_t *w, uint32_t *ov)
{
	OscInfo l = *m;
	l.oscType = OSC_TYPE_WAV;
	oscSection_buildSyncLane(&l, n, w, ov);
}

PROBE void p_lane_sample(OscInfo *m, uint8_t n, uint8_t *w, uint32_t *ov)
{
	OscInfo l = *m;
	l.oscType = OSC_TYPE_SLP;
	oscSection_buildSyncLane(&l, n, w, ov);
}

/* The real snc render with no itp copy: its innermost loops are the lane
** builder and the mix pass (the master and slave renders are calls). */
PROBE void p_snc(OscInfo *car, OscInfo *mod, float amount, int16_t *buf,
                 int16_t *modBuf, uint8_t n)
{
	oscSection_renderSync(car, mod, amount, buf, modBuf, n, 0, 0);
}

PROBE void p_rng(int16_t *out, const int16_t *mod, float amount, uint8_t n)
{
	oscSection_ring(out, mod, amount, n);
}

PROBE void p_blend(OscSectionState *s, int16_t *buf, const int16_t *old,
                   uint8_t n)
{
	oscSection_blendItp(s, buf, old, n);
}

/* S085 P2 F-A: the Drum fm modulator depth ramp (runs every fm block). */
PROBE void p_fm_ramp(int16_t *m, float from, uint8_t n)
{
	oscSection_rampModDepth(m, from, n);
}
