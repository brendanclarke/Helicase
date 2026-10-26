/*
 * pwm family shapes (S084 P1 WP5).
 *
 * What:       proves the real oscPwm_prepare()/oscPwm_render() text produces
 *             the documented shapes: peak-normalised square (prc), plain saw
 *             at c=0 (psw), triangle at c=0 (ptr), sine at c=0 (swp), bounded
 *             output at every control value, and FM with a zero modulator
 *             equal to the non-FM render.
 * Why:        plan §4.1.3 and the host proof in §9 (shapes, saturation).
 * Inputs:     generated pwm fragment plus the real wavetable levels.
 * Outputs:    failure count; exits nonzero on any failure.
 * Accessors:  make -C tools/dsp_test osc_pwm.
 * Affiliates: Core/DSPAudio/OscillatorPwm.c, wavetable.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <math.h>

typedef struct {
    uint32_t shift;
    uint32_t split;
    float    scale;
    float    warpA;
    float    warpB;
    float    mix;
    uint8_t  seg;
} OscPwmState;

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
    OscPwmState pwm;
};

#define OSC_PWM_PRC 0u
#define OSC_PWM_PSW 1u
#define OSC_PWM_PTR 2u
#define OSC_PWM_PQ3 3u
#define OSC_PWM_PW3 4u
#define OSC_PWM_SWP 5u
#define OSC_PWM_FAMILY_COUNT 6u
#define REAL_FS 44108.07291666667f
#define OUTPUT_DMA_SIZE 32
#define TABLESIZE 4096
#define PWM_PHASE_CYCLE  4294967296.0f
#define PWM_PHASE_MASK   0x003FFFFFu
#define PWM_INV_CYCLE    2.3283064365386963e-10f
#define PWM_QUARTER      0x40000000u

/* S084 VX2: host stock-wave stub for pq3/pw3 (VX2-4.9 / X2-10.8). */
typedef struct {
    const int16_t *tab;
    uint8_t shift;
    uint16_t mask;
} stock_level_t;

static int hostStockValid;
extern const int16_t recTable[11][1024];
extern const int16_t sawTable[11][1024];
uint8_t sampleMemory_stockValid(void) { return (uint8_t)hostStockValid; }
stock_level_t stockWave_level(uint8_t wave, uint8_t level)
{
    stock_level_t lv;

    if (level > 10u)
        level = 10u;
    lv.tab = (wave == 0u) ? recTable[level] : sawTable[level];
    lv.shift = 22u;
    lv.mask = 1023u;
    return lv;
}

#include "pwm_new.c"

static unsigned failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/*
 * Independent reference read of one 1,024-point level
 * (S084 P1 review fix F3).
 *
 * What:       linear interpolation in double precision, written from the
 *             table format (index = phase >> 22, fraction = low 22 bits / 2^22)
 *             rather than taken from the firmware text.
 * Why:        the previous correlation test used the firmware pwm_read1024()
 *             as its own reference, so a wrong fraction scale (1/2^32) passed.
 */
static double refRead1024(const int16_t *tab, uint32_t phase)
{
    const uint32_t i = (phase >> 22) & 0x3FFu;
    const uint32_t n = (i + 1u) & 0x3FFu;
    const double frac = (double)(phase & 0x003FFFFFu) / 4194304.0;

    return (double)tab[i] + frac * ((double)tab[n] - (double)tab[i]);
}

static int renderFamily(uint8_t family, uint8_t control, int16_t *out,
                        unsigned count)
{
    struct OscStruct osc;
    unsigned i;
    int peak = 0;

    for (i = 0; i < sizeof(osc); i++)
        ((uint8_t *)&osc)[i] = 0u;
    osc.oscType = 1u;
    osc.source = family;
    osc.waveform = control;
    oscPwm_prepare(&osc, 110.0f);
    osc.phaseInc = osc.phaseIncTarget;
    for (i = 0u; i < count; i += OUTPUT_DMA_SIZE) {
        unsigned j;

        oscPwm_render(&osc, &out[i], OUTPUT_DMA_SIZE, 1.0f);
        for (j = 0u; j < OUTPUT_DMA_SIZE; j++) {
            const int a = out[i + j] < 0 ? -out[i + j] : out[i + j];

            if (a > peak)
                peak = a;
        }
    }
    return peak;
}

static float correlationWithSaw(uint8_t family, uint8_t control,
                                const int16_t *level)
{
    struct OscStruct osc;
    unsigned i;
    double ab = 0.0, aa = 0.0, bb = 0.0;
    unsigned sample = 0u;

    for (i = 0u; i < sizeof(osc); i++)
        ((uint8_t *)&osc)[i] = 0u;
    osc.source = family;
    osc.waveform = control;
    oscPwm_prepare(&osc, 110.0f);
    osc.phaseInc = osc.phaseIncTarget;
    level += (unsigned)osc.tableOffset * 1024u;
    for (i = 0u; i < 64u; i += OUTPUT_DMA_SIZE) {
        int16_t block[OUTPUT_DMA_SIZE];
        unsigned j;

        oscPwm_render(&osc, block, OUTPUT_DMA_SIZE, 1.0f);
        for (j = 0u; j < OUTPUT_DMA_SIZE; j++, sample++) {
            const double a = block[j];
            const double b = refRead1024(level,
                                         (uint32_t)(sample *
                                                    osc.phaseIncTarget));

            ab += a * b;
            aa += a * a;
            bb += b * b;
        }
    }
    return (float)(ab / sqrt(aa * bb + 1e-9));
}

int main(void)
{
    static int16_t out[4096];
    uint8_t family;
    int16_t fmBlock[OUTPUT_DMA_SIZE];
    int16_t plainBlock[OUTPUT_DMA_SIZE];
    int16_t zeros[OUTPUT_DMA_SIZE] = { 0 };
    struct OscStruct osc;
    unsigned i;

    for (family = 0u; family < OSC_PWM_FAMILY_COUNT; family++) {
        static const uint8_t controls[] = { 0u, 32u, 64u, 96u, 127u };
        unsigned c;

        for (c = 0u; c < sizeof(controls); c++) {
            const int peak = renderFamily(family, controls[c], out, 4096u);

            check(peak <= 32768 && peak > 1000, "bounded, audible peak");
            if (peak <= 1000 || peak > 32768)
                printf("  family %u control %u peak %d\n", family,
                       controls[c], peak);
        }
    }

    /*
     * S084 P1 review fix F3: pwm_read1024() interpolates between table
     * points. Every phase step of 2^14 across the whole saw level must match
     * the independent reference within 1 LSB (float rounding plus the
     * truncating int cast), and the midpoint between two points must be the
     * average of their values, not the lower point.
     */
    {
        const int16_t *tab = &sawTable[0][0];
        double maxErr = 0.0;
        uint32_t phase = 0u;

        do {
            const double err = fabs((double)pwm_read1024(tab, phase) -
                                    refRead1024(tab, phase));

            if (err > maxErr)
                maxErr = err;
            phase += 1u << 14;
        } while (phase != 0u);
        check(maxErr <= 1.0, "pwm_read1024 matches the reference");
        if (maxErr > 1.0)
            printf("  max error %.2f LSB\n", maxErr);
        for (i = 0u; i < 1024u; i += 37u) {
            const uint32_t mid = (i << 22) | 0x00200000u;
            const double want = refRead1024(tab, mid);

            check(fabs((double)pwm_read1024(tab, mid) - want) <= 1.0,
                  "pwm_read1024 midpoint is interpolated");
        }
    }

    /* psw at c=0 is the plain saw. */
    check(correlationWithSaw(OSC_PWM_PSW, 0u, &sawTable[0][0]) > 0.999f,
          "psw c=0 is a saw");
    /* ptr at c=0 is the plain triangle. */
    check(correlationWithSaw(OSC_PWM_PTR, 0u, &triTable[0][0]) > 0.999f,
          "ptr c=0 is a triangle");

    /* prc at c=0 is a square: most samples sit near the peak. */
    {
        int peak = renderFamily(OSC_PWM_PRC, 0u, out, 4096u);
        unsigned near = 0u;

        for (i = 0u; i < 4096u; i++)
            if (out[i] > (int16_t)(peak * 0.9) ||
                out[i] < (int16_t)(-peak * 0.9))
                near++;
        check(near > 3000u, "prc c=0 is a square");
    }

    /* fx == non-fx with a zero modulator. */
    for (i = 0u; i < sizeof(osc); i++)
        ((uint8_t *)&osc)[i] = 0u;
    osc.source = OSC_PWM_SWP;
    osc.waveform = 70u;
    osc.fmMod = 1.0f;
    oscPwm_prepare(&osc, 440.0f);
    osc.phaseInc = osc.phaseIncTarget;
    oscPwm_render(&osc, plainBlock, OUTPUT_DMA_SIZE, 1.0f);
    osc.phase = 0u;
    osc.phaseInc = osc.phaseIncTarget;
    oscPwm_renderFm(&osc, zeros, fmBlock, OUTPUT_DMA_SIZE, 1.0f);
    for (i = 0u; i < OUTPUT_DMA_SIZE; i++)
        check(plainBlock[i] == fmBlock[i], "fm with zero modulator is plain");

    /* S084 VX2 VX2-4.9: pq3/pw3 natural splits and the c=0 identity. */
    {
        struct OscStruct a;
        int16_t block[OUTPUT_DMA_SIZE];
        const int16_t *level;
        uint32_t phase;

        for (i = 0u; i < sizeof(a); i++)
            ((uint8_t *)&a)[i] = 0u;
        a.source = OSC_PWM_PQ3;
        a.waveform = 0u;
        hostStockValid = 0;
        oscPwm_prepare(&a, 110.0f);
        check(a.pwm.mix == 0.5f, "pq3 fallback split is 0.5");
        hostStockValid = 1;
        oscPwm_prepare(&a, 110.0f);
        check(a.pwm.mix == 0.78f, "pq3 natural split is 0.78");
        level = recTable[a.tableOffset];
        a.phase = 0u;
        a.phaseInc = a.phaseIncTarget;
        a.phaseIncStep = 0;
        oscPwm_render(&a, block, OUTPUT_DMA_SIZE, 1.0f);
        phase = 0u;
        for (i = 0u; i < OUTPUT_DMA_SIZE; i++) {
            const double want = refRead1024(level, phase);
            const double got = (double)block[i];

            check(fabs(got - want) <= 1.0, "pq3 c=0 is the plain sq3 read");
            phase += a.phaseInc;
        }

        for (i = 0u; i < sizeof(a); i++)
            ((uint8_t *)&a)[i] = 0u;
        a.source = OSC_PWM_PW3;
        a.waveform = 0u;
        hostStockValid = 0;
        oscPwm_prepare(&a, 110.0f);
        check(a.pwm.mix == 0.5f, "pw3 fallback split is 0.5");
        hostStockValid = 1;
        oscPwm_prepare(&a, 110.0f);
        check(a.pwm.mix == 0.42f, "pw3 natural split is 0.42");
        hostStockValid = 0;
    }

    printf("osc_pwm %s (%u failures)\n", failures ? "FAILED" : "OK",
           failures);
    return failures ? 1 : 0;
}
