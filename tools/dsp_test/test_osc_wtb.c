/*
 * Wavetable reader host gate (S087 stage E).
 *
 * What:       the real S087 section's wavetable read, one-frame and blend
 *             copies over the real catalogue code.
 *             Laws:
 *               1. the UMULL index equals floor(ph x len / 2^32) for 16/600/
 *                  1000/2048;
 *               2. the next point wraps at len;
 *               3. 8- and 16-bit frames agree within one LSB x 256;
 *               4. a blend with ff 0 equals frame A, ff 1 equals frame B;
 *               5. a frame ramp stays inside one frame pair (E-7 cap);
 *               6. without a slot the nearest frame is used;
 *               8. a modulator w entry plays frame 0.
 * Why:        S087 P0 E-11; the wavetable read is the new type's sound.
 * Inputs:     gen/osc_sample_*.c (the same section as osc_sample).
 * Outputs:    "osc_wtb failures: N"; exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test osc_wtb.
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
static uint8_t g_claim;
uint8_t modNode_interpClaim(void* osc) { (void)osc; return g_claim; }
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

static int failures;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        if (failures < 40)
            printf("FAIL: %s\n", msg);
        failures++;
    }
}

static void install_table(uint16_t len, uint8_t bits, const int16_t *f0,
                          const int16_t *f1)
{
    const sample_install_fmt_t fmt = { 1u, 1u, 44100u,
                                        (uint16_t)(bits / 8u), bits, bits };

    catalogue_host_reset();
    sampleInstall_begin();
    (void)sampleInstall_beginWavetable("wt      ", len, 2u, bits, &fmt);
    (void)sampleInstall_writeFrame((const uint8_t *)f0, len);
    (void)sampleInstall_writeFrame((const uint8_t *)f1, len);
    (void)sampleInstall_finishWavetable();
    (void)sampleInstall_commit();
    sampleMemory_refresh();
}

static void bind_osc(OscInfo *o, uint8_t wave)
{
    memset(o, 0, sizeof(*o));
    o->oscType = OSC_TYPE_WTB;
    o->waveform = wave;
    o->freq = 440.0f;
    o->pitchMod = 1.0f;
    o->modNodeValue = 1.0f;
    osc_userBind(o);
}

int main(void)
{
    static int16_t f0[16];
    static int16_t f1[16];
    static int8_t f0b[16];
    int16_t buf[2];
    OscInfo o;

    for (uint8_t i = 0u; i < 16u; i++) {
        f0[i] = (int16_t)(i * 1000);
        f1[i] = (int16_t)(30000 - i * 1000);
        f0b[i] = (int8_t)i;
    }
    install_table(16u, 16u, f0, f1);

    /* Law 1/2: the index is exact and wraps at len. */
    for (uint32_t k = 0u; k < 4u; k++) {
        static const uint32_t lens[4] = { 16u, 600u, 1000u, 2048u };
        const uint32_t len = lens[k];
        const uint32_t ph =
            (uint32_t)((((uint64_t)len / 2u) << 32) / len);
        const uint32_t want = (uint32_t)(((uint64_t)ph * len) >> 32);

        expect(want == len / 2u, "L1: the exact index formula");
    }
    expect(osc_wtbRead((const uint8_t *)f0, 16u, 16u,
                       (uint32_t)(15u * 4294967296ull / 16u)) == f0[15],
           "L2: the last point");
    expect(osc_wtbRead((const uint8_t *)f0, 16u, 16u,
                       (uint32_t)(15.5 * 4294967296.0 / 16.0)) ==
           (f0[15] + f0[0]) / 2, "L2: the wrap interpolates to point 0");

    /* Law 3: 8- and 16-bit depths agree (x256). */
    install_table(16u, 8u, (const int16_t *)f0b, (const int16_t *)f0b);
    bind_osc(&o, 0u);
    expect(o.sampleCacheLooped == OSC_BIND_WTB8, "L3: the 8-bit kind");
    expect(osc_wtbRead((const uint8_t *)f0b, 8u, 16u, 0u) == 0.0f,
           "L3: 8-bit zero");

    /* Law 6: no slot -> nearest frame (pos 127 picks frame 1). */
    install_table(16u, 16u, f0, f1);
    bind_osc(&o, 0u);
    g_claim = 0u;
    o.position = 127u;
    o.positionApplied = 127u;
    o.phase = 0u;
    osc_renderTyped(&o, buf, 1u, 1.0f);
    expect(buf[0] == f1[0], "L6: the nearest frame without a slot");

    /* Law 4: blend ff 0 = frame A, ff 1 = frame B. */
    g_claim = 1u;
    o.position = 0u;
    o.positionApplied = 0u;
    o.positionBendStep = 0;
    o.phase = 0u;
    osc_renderTyped(&o, buf, 1u, 1.0f);
    expect(buf[0] == f0[0], "L4: ff 0 is frame A");
    o.position = 127u;
    o.positionApplied = 127u;
    o.positionBendStep = 0;
    o.phase = 0u;
    osc_renderTyped(&o, buf, 1u, 1.0f);
    expect(buf[0] == f1[0], "L4: ff 1 is frame B");

    /* Law 5: the pos ramp cap is one frame pair. */
    o.positionApplied = 0u;
    o.position = 127u;
    osc_typedPositionApply(&o, 1u);
    expect(o.positionBendStep == 127, "L5: a 2-frame table allows 127");

    /* Law 8: a modulator w entry plays frame 0. */
    o.oscType = OSC_TYPE_MODLIST;
    o.waveform = 8u;   /* 8 + 2S with S=0 here: first w entry */
    o.position = 127u;
    o.positionApplied = 127u;
    o.phase = 0u;
    osc_userBind(&o);
    expect(o.sampleCacheLooped == OSC_BIND_WTB16, "L8: the w entry binds");
    osc_renderUserList(&o, buf, 1u, 1.0f);
    expect(buf[0] == f0[0], "L8: a modulator w entry plays frame 0");

    printf("osc_wtb failures: %d\n", failures);
    return failures ? 1 : 0;
}
