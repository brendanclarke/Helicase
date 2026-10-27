/*
 * The frozen85 engine oscillator lines, verbatim (S085 assessment A7).
 *
 * What:       each function body is a generated fragment of the frozen85
 *             engine source (see the osc_s0 Makefile target for the line
 *             ranges); only the dispatcher names were renamed to the
 *             s0_engine_shim.h stand-ins.
 * Accessors:  test_osc_s0.c.
 */
#include "prelude_osc.h"
#include "BufferTools.h"
#include "s0_engine_shim.h"

void old_drumOsc(OldVoice *voice, int16_t *buf, int16_t *modBuf, uint8_t size)
{
#include "s0_drum_lines.c"
}

void old_cymOsc(OldVoice *voice, int16_t *buf, int16_t *mod, int16_t *mod2,
                uint8_t size)
{
#include "s0_cym_lines.c"
}

void old_hatOsc(OldVoice *voice, int16_t *buf, int16_t *mod1, int16_t *mod2,
                uint8_t size)
{
#include "s0_hat_lines.c"
}
