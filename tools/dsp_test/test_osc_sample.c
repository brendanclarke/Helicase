/*
 * Sample reader host gate (S087 stage D).
 *
 * What:       compiles the real S087 section of Oscillator.c (bind, typed pos
 *             rules, the eight sample loops and their renderers) and
 *             osc_setType() over the real SampleMemory/SampleInstall code.
 *             Laws:
 *               L1 smp plays the item, then holds 0 from the pad;
 *               L2 slp interpolates across a non-zero seam to loop_start;
 *               L3 a slp start past the loop end clamps to loop_start;
 *               L8 the pos start for 0/64/127;
 *               L9 the bend is linear, capped by frames, at least 1 step;
 *               L10 a finished smp is not revived by pos;
 *               L11 Apply then Restore leaves phaseInc exact;
 *               L12 the modulator list maps 8 / 8+S / 8+2S to s / l / w;
 *               L13 a typ switch clamps wav and re-anchors the phase.
 * Why:        S087 P0 D-45; the readers are the sound of the new types.
 * Inputs:     cat_host.h/catalogue_host.c, gen/osc_sample_*.c.
 * Outputs:    "osc_sample failures: N"; exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test osc_sample.
 * Affiliates: Oscillator.c S087 section, SampleMemory.h, SampleInstall.h.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "cat_host.h"
#include "SampleMemory.h"
#include "SampleInstall.h"

#define REAL_FS ((float)44108.07291666667f)
#define OUTPUT_DMA_SIZE 32
#define OSC_SYNC_NONE 0xFFFFFFFFu
#define OSC_S087_SECTION
#define OSC_S087_LOOP

void catalogue_host_reset(void);
/* This test never blends (stage E has its own test). */
uint8_t modNode_interpClaim(void* osc) { (void)osc; return 0u; }
static inline uint32_t freq2PhaseIncr(float f)
{
    return (uint32_t)((f * 4294967296.0f) / REAL_FS);
}

#include "osc_sample_defs.inc"
#include "osc_sample_info.inc"
static inline uint8_t osc_isTyped(const OscInfo* osc);
void osc_typedPositionApply(OscInfo* osc, uint8_t bend);
void osc_typedPositionRestore(OscInfo* osc);
uint32_t osc_typedTriggerPhase(OscInfo* osc);
#include "osc_sample_helpers.c"
#include "osc_sample_pos.c"
#include "osc_sample_section.c"
#include "osc_sample_settype.c"

static int failures;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        if (failures < 40)
            printf("FAIL: %s\n", msg);
        failures++;
    }
}

static void install_sample(const int16_t *data, uint32_t frames,
                           uint32_t loop_start, uint32_t loop_end,
                           uint8_t has_loop)
{
    const sample_install_fmt_t fmt = { 1u, 1u, 44100u, 2u, 16u, 16u };

    catalogue_host_reset();
    sampleInstall_begin();
    (void)sampleInstall_beginSample("s01     ", frames, &fmt);
    (void)sampleInstall_writeFrames((const uint8_t *)data, frames);
    (void)sampleInstall_finishSample(loop_start, loop_end, has_loop);
    (void)sampleInstall_commit();
    sampleMemory_refresh();
}

static void bind_osc(OscInfo *o, uint8_t type, uint8_t wave, uint32_t inc)
{
    memset(o, 0, sizeof(*o));
    o->oscType = type;
    o->waveform = wave;
    o->freq = 440.0f;
    o->pitchMod = 1.0f;
    o->modNodeValue = 1.0f;
    osc_userBind(o);
    o->phaseInc = inc;
    o->positionApplied = o->position;
}

int main(void)
{
    /* Peak 32767 keeps the installer's gain code at 0, so the reads are exact. */
    static const int16_t data[4] = { 0, 8192, 16384, 32767 };
    int16_t buf[8];
    OscInfo o;
    uint32_t before;

    install_sample(data, 4u, 0u, 4u, 0u);

    /* L1: smp plays the item, then the pad. */
    bind_osc(&o, OSC_TYPE_SMP, 0u, 4096u);
    o.phase = 0u;
    osc_renderTyped(&o, buf, 8u, 1.0f);
    expect(buf[0] == 0 && buf[1] == 8192 && buf[2] == 16384 &&
           buf[3] == 32767, "L1: the four points");
    expect(buf[4] == 0 && buf[5] == 0 && buf[6] == 0 && buf[7] == 0,
           "L1: the pad holds 0");

    /* L2: slp seam to loop_start, never 0. */
    install_sample(data, 4u, 1u, 3u, 1u);
    bind_osc(&o, OSC_TYPE_SLP, 0u, 4096u);
    o.phase = (2u << OSC_SAMPLE_PHASE_SHIFT) + 2048u;
    osc_renderTyped(&o, buf, 1u, 1.0f);
    expect(buf[0] == 12288, "L2: the seam interpolates 16384 -> 8192");

    /* L3: a start past the loop end clamps to loop_start. */
    o.position = 127u;
    o.phase = osc_typedTriggerPhase(&o);
    expect((o.phase >> OSC_SAMPLE_PHASE_SHIFT) == 1u,
           "L3: slp start clamps to loop_start");

    /* L8: the pos start for 0/64/127. */
    bind_osc(&o, OSC_TYPE_SMP, 0u, 4096u);
    o.position = 0u;
    expect(osc_typedTriggerPhase(&o) == 0u, "L8: pos 0 starts at 0");
    o.position = 64u;
    expect((osc_typedTriggerPhase(&o) >> OSC_SAMPLE_PHASE_SHIFT) == 2u,
           "L8: pos 64 starts at point 2");
    o.position = 127u;
    expect((osc_typedTriggerPhase(&o) >> OSC_SAMPLE_PHASE_SHIFT) == 3u,
           "L8: pos 127 starts at point 3");

    /* L9: linear bend of one step = frames. */
    bind_osc(&o, OSC_TYPE_SMP, 0u, 4096u);
    o.position = 0u;
    o.positionApplied = 0u;
    o.position = 1u;
    before = o.phaseInc;
    osc_typedPositionApply(&o, 1u);
    expect(o.positionBendStep == 1, "L9: one step");
    expect(o.phaseInc == before + 4u, "L9: inc moves by step x frames");
    osc_typedPositionRestore(&o);
    expect(o.phaseInc == before, "L11: restore is exact");

    /* L10: a finished smp is not revived. */
    o.phase = 4u << OSC_SAMPLE_PHASE_SHIFT;
    o.position = 10u;
    osc_typedPositionApply(&o, 1u);
    expect(o.positionBendStep == 0, "L10: no bend after the end");

    /* L12: modulator list mapping. */
    bind_osc(&o, OSC_TYPE_MODLIST, 9u, 4096u);
    expect(o.sampleCacheLooped == OSC_BIND_SLP && o.sampleCacheIndex == 0u,
           "L12: 8+S maps to l0");

    /* L13: typ switch clamps wav and re-anchors. */
    memset(&o, 0, sizeof(o));
    o.oscType = OSC_TYPE_WAV;
    o.waveform = 7u;
    o.position = 64u;
    osc_setType(&o, OSC_TYPE_SMP);
    expect(o.oscType == OSC_TYPE_SMP && o.waveform <= 0u,
           "L13: wav clamps to the sample list");
    expect(o.positionApplied == o.position, "L13: pos re-anchored");

    printf("osc_sample failures: %d\n", failures);
    return failures ? 1 : 0;
}
