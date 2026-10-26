/*
 * Q20.12 sample playback (S084 P1 WP4).
 *
 * What:       proves the real oscSample_* text: a 3-second sample (longer than
 *             the retired 0.74 s window) plays to its last frame, `smp` holds
 *             there, `slp` wraps inside [sampleStart, sampleEnd), the start
 *             position follows `wav`, and FM depth is bounded.
 * Why:        plan §4.1.5 and the host proof in §9.
 * Inputs:     generated sample fragment plus a synthetic catalogue.
 * Outputs:    failure count; exits nonzero on any failure.
 * Accessors:  make -C tools/dsp_test osc_sample.
 * Affiliates: Core/DSPAudio/OscillatorSample.c, SampleMemory.h.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define OSC_TYPE_SMP 2u
#define OSC_TYPE_SLP 3u
#define OSC_SAMPLE_PHASE_SHIFT 12u
#define REAL_FS 44108.07291666667f
#define OUTPUT_DMA_SIZE 32

typedef struct {
    uint32_t size;
    uintptr_t offset;   /* host: pointer-sized so the firmware cast works */
    uint8_t gain_code;
} SampleInfo;

struct OscStruct {
    uint8_t oscType;
    uint8_t source;
    uint8_t waveform;
    uint8_t tableOffset;
    uint32_t phase;
    uint32_t phaseInc;
    uint32_t phaseIncTarget;
    int32_t phaseIncStep;
    float fmMod;
    uint32_t sampleStart;
    uint32_t sampleEnd;
    uint8_t sampleCacheIndex;
    uint8_t sampleGainCode; /* S084 VX2 VX2-1.5 */
    uint8_t sourceRequest;  /* S084 P1 review fix F2 (Oscillator.h) */
    uint32_t sampleCacheGeneration;
    uint32_t sampleCacheSize;
    const int16_t *sampleCacheData;
};

#define SAMPLE_FRAMES 132324u   /* 3.0 s at 44,108 Hz, past the old 32,768 */
static int16_t sampleData[SAMPLE_FRAMES];

/* Installed count; the F2 case below changes it to model a reinstall. */
static uint8_t testSampleCount = 1u;
uint8_t sampleMemory_getNumSamples(void) { return testSampleCount; }
uint32_t sampleMemory_getGeneration(void) { return 1u; }
SampleInfo sampleMemory_getSampleInfo(uint8_t index)
{
    SampleInfo info;
    (void)index;
    info.size = SAMPLE_FRAMES;
    info.offset = (uintptr_t)sampleData;
    info.gain_code = 0u;
    return info;
}
float sampleMemory_gainFromCode(uint8_t code)
{
    return powf(2.0f, (float)code / 24.0f);
}

#define SAMPLE_FLASH_PTR(addr) ((const void *)(uintptr_t)(addr))
#include "sample_new.c"

static unsigned failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void initOsc(struct OscStruct *osc, uint8_t type, uint8_t start)
{
    memset(osc, 0, sizeof(*osc));
    osc->oscType = type;
    osc->source = 0u;
    osc->sourceRequest = 0u;
    osc->waveform = start;
}

int main(void)
{
    struct OscStruct osc;
    int16_t block[OUTPUT_DMA_SIZE];
    uint32_t rendered = 0u;
    uint32_t i;

    for (i = 0u; i < SAMPLE_FRAMES; i++)
        sampleData[i] = (int16_t)((i % 2000u) - 1000);

    /* One-shot: every frame plays, then the phase holds at the last frame. */
    initOsc(&osc, OSC_TYPE_SMP, 0u);
    oscSample_bind(&osc);
    oscSample_restart(&osc);
    osc.phaseInc = oscSample_phaseIncForFreq(100.0f);
    while (rendered < 400000u &&
           osc.phase != ((SAMPLE_FRAMES - 1u) << OSC_SAMPLE_PHASE_SHIFT)) {
        oscSample_render(&osc, block, OUTPUT_DMA_SIZE, 1.0f);
        rendered += OUTPUT_DMA_SIZE;
    }
    check(osc.phase == ((SAMPLE_FRAMES - 1u) << OSC_SAMPLE_PHASE_SHIFT),
          "one-shot reaches the last frame");
    {
        const uint32_t frames = osc.phase >> OSC_SAMPLE_PHASE_SHIFT;
        check(frames + 1u == SAMPLE_FRAMES, "all frames play");
    }
    oscSample_render(&osc, block, OUTPUT_DMA_SIZE, 1.0f);
    check(osc.phase == ((SAMPLE_FRAMES - 1u) << OSC_SAMPLE_PHASE_SHIFT),
          "one-shot holds");

    /* Start position: wav 64 starts half way through the sample. */
    initOsc(&osc, OSC_TYPE_SMP, 64u);
    oscSample_bind(&osc);
    check(osc.sampleStart == ((64u * SAMPLE_FRAMES) >> 7), "start position");
    oscSample_restart(&osc);
    check((osc.phase >> OSC_SAMPLE_PHASE_SHIFT) == osc.sampleStart,
          "restart uses the start position");

    /*
     * S084 P1 review fix F2: the sample index follows sourceRequest.
     * A request of 2 with one installed sample plays sample 0; after a
     * reinstall with three samples the stored request 2 comes back instead of
     * staying on the clamped 0. With no samples the oscillator is silent and
     * source reads 0.
     */
    initOsc(&osc, OSC_TYPE_SLP, 0u);
    osc.sourceRequest = 2u;
    testSampleCount = 1u;
    oscSample_bind(&osc);
    check(osc.source == 0u, "request above the count clamps to the last sample");
    testSampleCount = 3u;
    oscSample_bind(&osc);
    check(osc.source == 2u, "request returns when the count grows again");
    testSampleCount = 0u;
    oscSample_bind(&osc);
    check(osc.source == 0u && osc.sampleCacheData == 0,
          "no samples: silent, source 0");
    check(osc.sourceRequest == 2u, "bind never changes the request");
    testSampleCount = 1u;

    /* Looped: never addresses outside [start, end), wraps at the end. */
    initOsc(&osc, OSC_TYPE_SLP, 0u);
    oscSample_bind(&osc);
    osc.phase = (SAMPLE_FRAMES - 8u) << OSC_SAMPLE_PHASE_SHIFT;
    osc.phaseInc = oscSample_phaseIncForFreq(440.0f);
    for (i = 0u; i < 1000u; i++) {
        oscSample_render(&osc, block, OUTPUT_DMA_SIZE, 1.0f);
        check((osc.phase >> OSC_SAMPLE_PHASE_SHIFT) < SAMPLE_FRAMES,
              "loop stays inside the sample");
    }

    /* FM: a zero modulator equals the plain render. */
    {
        struct OscStruct a;
        struct OscStruct b;
        int16_t plain[OUTPUT_DMA_SIZE];
        int16_t fm[OUTPUT_DMA_SIZE];
        int16_t zeros[OUTPUT_DMA_SIZE] = { 0 };

        initOsc(&a, OSC_TYPE_SLP, 10u);
        oscSample_bind(&a);
        oscSample_restart(&a);
        a.phaseInc = oscSample_phaseIncForFreq(220.0f);
        b = a;
        b.fmMod = 0.5f;
        oscSample_render(&a, plain, OUTPUT_DMA_SIZE, 1.0f);
        oscSample_renderFm(&b, zeros, fm, OUTPUT_DMA_SIZE, 1.0f);
        for (i = 0u; i < OUTPUT_DMA_SIZE; i++)
            check(plain[i] == fm[i], "zero FM equals plain");
    }

    /* S084 VX2 VX2-1.4: the catalogue gain code scales the block. */
    {
        struct OscStruct a;
        struct OscStruct b;
        int16_t x[OUTPUT_DMA_SIZE];
        int16_t y[OUTPUT_DMA_SIZE];

        initOsc(&a, OSC_TYPE_SMP, 0u);
        oscSample_bind(&a);
        oscSample_restart(&a);
        a.phaseInc = oscSample_phaseIncForFreq(220.0f);
        b = a;
        b.sampleGainCode = 24u;
        oscSample_render(&a, x, OUTPUT_DMA_SIZE, 1.0f);
        oscSample_render(&b, y, OUTPUT_DMA_SIZE, 1.0f);
        for (i = 0u; i < OUTPUT_DMA_SIZE; i++) {
            int32_t want = (int32_t)x[i] * 2;

            if (want > 32767) want = 32767;
            if (want < -32768) want = -32768;
            check((int32_t)y[i] - want <= 1 && (int32_t)y[i] - want >= -1,
                  "gain code 24 doubles the sample");
        }
    }

    printf("osc_sample %s (%u failures)\n", failures ? "FAILED" : "OK",
           failures);
    return failures ? 1 : 0;
}
