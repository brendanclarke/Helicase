/*
 * Run modes and the itp crossfade (S084 P1 WP6/WP7).
 *
 * What:       proves the real OscSection text: rst resets, fre arms the slew on
 *             every oscillator, itp snapshots with a frozen old pitch, claims
 *             units, falls back to fre without units, blends old to new over
 *             OSC_ITP_SAMPLES and frees the state at the end.
 * Why:        plan §4.1.6 and the host proof in §9.
 * Inputs:     generated OscSection fragment plus pool/slew stubs.
 * Outputs:    failure count; exits nonzero on any failure.
 * Accessors:  make -C tools/dsp_test osc_run.
 * Affiliates: Core/DSPAudio/OscSection.c, OscRenderPool.c, Oscillator.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct OscStruct {
    uint32_t phase;
    uint32_t phaseInc;
    int32_t phaseIncStep;
    uint16_t slewSamplesLeft;
} OscInfo;

#define OSC_RUN_RST 0u
#define OSC_RUN_FRE 1u
#define OSC_RUN_ITP 2u
#define OSC_RUN_COUNT 3u
#define OSC_SECTION_MAX_OSCS 3u
#define OSC_ITP_SAMPLES 132u
#define OSC_ITP_IDLE 0u
#define OSC_ITP_PENDING 1u
#define OSC_ITP_ACTIVE 2u

typedef struct {
    uint8_t runMode;
    uint8_t itpState;
    uint8_t itpUnits;
    uint8_t oscCount;
    uint16_t itpPos;
    OscInfo itpOld[OSC_SECTION_MAX_OSCS];
    float fSmooth;
    float qSmooth;
    float qTarget;
} OscSectionState;

static unsigned slewArms;
static unsigned poolGrants;
static int poolAllow;

static void osc_armSlew(OscInfo *osc)
{
    slewArms++;
    if (osc)
        osc->slewSamplesLeft = 132u;
}

static uint8_t oscRenderPool_claim(uint8_t units)
{
    (void)units;
    if (!poolAllow)
        return 0u;
    poolGrants++;
    return 1u;
}

/* Filter stubs for oscSection_filterRecalc() (S084 P1 review fix F4);
 * ResonantFilter comes from prelude.h. */
static void SVF_recalcFreq(ResonantFilter *f) { f->g = f->f; }
static void SVF_recalcFreqFrom(ResonantFilter *f, float fUsed) { f->g = fUsed; }

#include "oscsection_new.c"

static unsigned failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    OscSectionState s;
    OscInfo a, b;
    OscInfo *oscs[2] = { &a, &b };
    int16_t oldBlock[32];
    int16_t newBlock[32];
    unsigned i;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.phase = 111u; a.phaseInc = 222u;
    b.phase = 333u; b.phaseInc = 444u;

    oscSection_init(&s, 2u);
    check(s.runMode == OSC_RUN_RST && s.oscCount == 2u, "init");
    check(oscSection_beginTrigger(&s, oscs) == 1u, "rst returns reset");
    check(slewArms == 0u, "rst does not slew");

    s.runMode = OSC_RUN_FRE;
    check(oscSection_beginTrigger(&s, oscs) == 0u, "fre keeps the phase");
    check(slewArms == 2u, "fre slews every oscillator");
    check(a.slewSamplesLeft == 132u && b.slewSamplesLeft == 132u,
          "slew armed on both");

    s.runMode = OSC_RUN_ITP;
    check(oscSection_beginTrigger(&s, oscs) == 1u, "itp resets the live state");
    check(s.itpState == OSC_ITP_PENDING, "itp is pending");
    check(s.itpOld[0].phase == 111u && s.itpOld[0].phaseIncStep == 0,
          "snapshot keeps phase and freezes pitch");

    poolAllow = 1;
    oscSection_claimOngoing(&s);       /* pending: no ongoing claim */
    oscSection_claimPending(&s, oscs);
    check(s.itpState == OSC_ITP_ACTIVE && s.itpPos == 0u, "itp active");

    for (i = 0u; i < 32u; i++) {
        oldBlock[i] = 1000;
        newBlock[i] = -1000;
    }
    oscSection_blendItp(&s, newBlock, oldBlock, 32u);
    check(newBlock[0] == 1000, "crossfade starts at the old section");
    oscSection_finishBlock(&s, 32u);
    for (i = 0u; i < 32u; i++)
        newBlock[i] = -1000;
    oscSection_blendItp(&s, newBlock, oldBlock, 32u);
    check(newBlock[31] < 1000, "crossfade moves toward the new section");
    while (s.itpState == OSC_ITP_ACTIVE)
        oscSection_finishBlock(&s, 32u);
    check(s.itpState == OSC_ITP_IDLE && s.itpUnits == 0u,
          "crossfade frees at 132 samples");
    for (i = 0u; i < 32u; i++)
        newBlock[i] = -1000;
    oscSection_blendItp(&s, newBlock, oldBlock, 32u);
    check(newBlock[0] == -1000, "idle blend is a no-op");

    /* No units: itp falls back to fre and restores the old phases. */
    a.phase = 999u;
    b.phase = 888u;
    poolAllow = 0;
    (void)oscSection_beginTrigger(&s, oscs);
    slewArms = 0u;
    oscSection_claimPending(&s, oscs);
    check(s.itpState == OSC_ITP_IDLE, "no units falls back");
    check(a.phase == 999u && b.phase == 888u,
          "this trigger's pre-reset phases restored");
    check(slewArms == 2u, "fallback slews every oscillator");

    /*
     * S084 P1 review fix F4: in fre, a q written directly by modulation
     * (velocity node / LFO adapter, through the parameter pointer) becomes
     * the slew target; without a new write q settles on the last target.
     */
    {
        OscSectionState fs;
        ResonantFilter flt;
        unsigned b;

        memset(&flt, 0, sizeof(flt));

        oscSection_init(&fs, 2u);
        fs.runMode = OSC_RUN_FRE;
        flt.f = 0.2f;
        flt.g = 0.0f;
        flt.q = 0.9f;                   /* reso row value */
        oscSection_filterRecalc(&fs, &flt);
        check(flt.q == 0.9f, "fre: first block starts at the written q");
        flt.q = 0.3f;                   /* modulation writes q directly */
        for (b = 0u; b < 40u; b++)
            oscSection_filterRecalc(&fs, &flt);
        check(flt.q > 0.29f && flt.q < 0.31f,
              "fre: modulated q is followed, not overwritten");
        for (b = 0u; b < 40u; b++)
            oscSection_filterRecalc(&fs, &flt);
        check(flt.q > 0.29f && flt.q < 0.31f,
              "fre: q holds its target without new writes");
        fs.runMode = OSC_RUN_RST;
        flt.q = 0.5f;
        oscSection_filterRecalc(&fs, &flt);
        check(flt.q == 0.5f, "rst: q is never touched");
    }

    printf("osc_run %s (%u failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
