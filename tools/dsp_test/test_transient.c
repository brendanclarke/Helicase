/*
 * Transient types (S084 VX2 VX2-10.5).
 *
 * What:       proves the real transient_* text: the 12 PCM entries are
 *             bit-identical to the original loop, trs renders silence,
 *             sample transients play one-shot with the catalogue gain, stock
 *             bursts stop at exactly 2,205 samples and decay with tau = 10 ms,
 *             and a type round trip keeps the requested entry.
 * Why:        plan §4.3.1, Q13-Q15 and the host proof.
 * Inputs:     generated transientGenerator fragment, transientTables.c.
 * Outputs:    failure count; exits nonzero on any failure.
 * Accessors:  make -C tools/dsp_test transient.
 * Affiliates: Core/DSPAudio/transientGenerator.c, transientTables.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define OSC_STOCK_WAVE_COUNT 11u
#define REAL_FS 44108.07291666667f
#define TRANSIENT_BURST_DECAY 0.99773f
#define TRANSIENT_INV_CYCLE 2.3283064365386963e-10f
#define TRANSIENT_SAMPLE_SHIFT 12u
#define TRANSIENT_PCM_END_PHASE 2311061504u

typedef struct {
    uint32_t size;
    uint32_t offset;
    uint8_t gain_code;
} SampleInfo;

#define NUM_TRANSIENTS 12u
#define TRANSIENT_SAMPLE_LENGTH 2205u
#define TRANSIENT_TYPE_TRS 0u
#define TRANSIENT_TYPE_WAV 1u
#define TRANSIENT_TYPE_BUF 2u
#define TRANSIENT_TYPE_SELECTABLE 3u
#define INSERT_TRANSIENT_COUNT 7u
#define TRANSIENT_STOCK_PCM_COUNT NUM_TRANSIENTS
#define TRANSIENT_WAV_LIST_MAX 256u
#define TRANSIENT_BURST_SAMPLES TRANSIENT_SAMPLE_LENGTH
#define TRANSIENT_KIND_NONE 0u
#define TRANSIENT_KIND_SNAP 1u
#define TRANSIENT_KIND_OFFSET 2u
#define TRANSIENT_KIND_PCM 3u
#define TRANSIENT_KIND_SAMPLE 4u
#define TRANSIENT_KIND_TABLE 5u
#define TRANSIENT_KIND_SINE 6u
#define TRANSIENT_KIND_NOISE 7u
#define TRANSIENT_KIND_CRASH 8u
#define TRANSIENT_KIND_BUFFER 9u

typedef struct TransientGeneratorStruct {
    int16_t  output;
    uint8_t  type;
    uint8_t  waveRequest;
    uint32_t phase;
    float    pitch;
    uint8_t  waveform;
    uint8_t  kind;
    uint8_t  source;
    uint8_t  gainCode;
    float    volume;
    uint16_t midiFreq;
    uint16_t burstLeft;
    uint32_t dataAddr;
    uint32_t frames;
    uint32_t generation;
    float    env;
} TransientGenerator;

static int8_t transientData[12][2205];

static inline uint8_t transient_isSnap(const TransientGenerator *t)
{ return (uint8_t)(t->kind == TRANSIENT_KIND_SNAP); }
static inline uint8_t transient_isOffset(const TransientGenerator *t)
{ return (uint8_t)(t->kind == TRANSIENT_KIND_OFFSET); }

typedef struct {
    const int16_t *tab;
    uint8_t shift;
    uint16_t mask;
} stock_level_t;

#define TRANSIENT_BURST_BASE_HZ 440.0f

/* Host audio tables (the firmware reads its own, not these). */
static int16_t host_sine[4096];
static int16_t host_tri[11][1024];
static int16_t host_saw[11][1024];
static int16_t host_rec[11][1024];
static uint8_t host_crash[32768];
#define sine_table host_sine
#define triTable host_tri
#define sawTable host_saw
#define recTable host_rec
#define crashSample host_crash

static int16_t host_samples[256];

#define SAMPLE_FLASH_PTR(addr) ((const void *)(host_samples + (addr)))

static unsigned failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static uint8_t sample_count = 2u;
static uint32_t sample_generation = 1u;

uint8_t sampleMemory_getNumSamples(void) { return sample_count; }
uint32_t sampleMemory_getGeneration(void) { return sample_generation; }
SampleInfo sampleMemory_getSampleInfo(uint8_t index)
{
    SampleInfo info;

    info.size = 256u;
    info.offset = index * 256u;
    info.gain_code = 0u;
    return info;
}
float sampleMemory_gainFromCode(uint8_t code)
{
    return powf(2.0f, (float)code / 24.0f);
}
uint8_t osc_freqToTableIndex(float f)
{
    (void)f;
    return 0u;
}
stock_level_t stockWave_level(uint8_t wave, uint8_t level)
{
    stock_level_t lv;

    (void)wave;
    (void)level;
    lv.tab = host_sine;
    lv.shift = 20u;
    lv.mask = 4095u;
    return lv;
}

#include "transient_new.c"

int main(void)
{
    static int16_t out[256];
    TransientGenerator t;
    unsigned i;

    for (i = 0u; i < 4096u; i++)
        host_sine[i] = (int16_t)(0.999f * 32767.0f *
                                 sinf(2.0f * 3.14159265f * (float)i / 4096.0f));
    for (i = 0u; i < 11u * 1024u; i++) {
        host_tri[0][i] = (int16_t)((i % 1024u) * 64u - 32768);
        host_saw[0][i] = (int16_t)(32767 - (int)((i % 1024u) * 64u));
        host_rec[0][i] = (int16_t)((i % 1024u) < 512u ? 32767 : -32768);
    }
    for (i = 0u; i < 32768u; i++)
        host_crash[i] = 127u;
    for (i = 0u; i < 256u; i++)
        host_samples[i] = (int16_t)((int)i * 100);
    for (i = 0u; i < 12u; i++) {
        unsigned j;

        for (j = 0u; j < 2205u; j++)
            transientData[i][j] = (int8_t)(((i * 17u + j * 3u) % 127u) - 64);
    }

    /* ---- PCM: bit-identical to the original loop ---------------------- */
    for (i = 0u; i < TRANSIENT_STOCK_PCM_COUNT; i++) {
        uint32_t phase = 0u;
        uint32_t ref_phase = 0u;
        unsigned block;

        transient_init(&t);
        transient_setType(&t, TRANSIENT_TYPE_WAV);
        transient_setWave(&t, (uint8_t)i);
        check(t.kind == TRANSIENT_KIND_PCM, "PCM kind");
        transient_trigger(&t);
        for (block = 0u; block < 20u; block++) {
            unsigned j;

            transient_calcBlock(&t, out, 64u);
            for (j = 0u; j < 64u; j++) {
                uint32_t p = ref_phase >> 20;
                const int16_t want = (int16_t)(t.volume *
                    (transientData[i][p] << 8) *
                    (p < TRANSIENT_SAMPLE_LENGTH));

                check(out[j] == want, "PCM sample matches the old loop");
                ref_phase += (ref_phase < 2311061504u) *
                    (uint32_t)(t.pitch * (1 << 20));
            }
            phase = t.phase;
            (void)phase;
        }
    }

    /* ---- trs: SNAP/OFFSET render silence ------------------------------ */
    transient_init(&t);
    transient_setType(&t, TRANSIENT_TYPE_TRS);
    transient_setWave(&t, 0u);
    check(transient_isSnap(&t), "trs 0 is Snp");
    transient_calcBlock(&t, out, 64u);
    for (i = 0u; i < 64u; i++)
        check(out[i] == 0, "Snp renders silence");
    transient_setWave(&t, 1u);
    check(transient_isOffset(&t), "trs 1 is Ofs");
    transient_calcBlock(&t, out, 64u);
    for (i = 0u; i < 64u; i++)
        check(out[i] == 0, "Ofs renders silence");

    /* ---- sample transients: one-shot and catalogue gain -------------- */
    transient_init(&t);
    transient_setType(&t, TRANSIENT_TYPE_WAV);
    transient_setWave(&t, (uint8_t)(TRANSIENT_STOCK_PCM_COUNT + 0u));
    check(t.kind == TRANSIENT_KIND_SAMPLE, "sample kind");
    transient_trigger(&t);
    transient_calcBlock(&t, out, 64u);
    {
        int nonzero = 0;
        for (i = 0u; i < 64u; i++)
            if (out[i] != 0)
                nonzero++;
        check(nonzero > 0, "sample transient plays");
    }
    t.gainCode = 24u;
    {
        int16_t plain[64];
        int16_t gained[64];

        t.gainCode = 0u;
        transient_trigger(&t);
        transient_calcBlock(&t, plain, 64u);
        t.gainCode = 24u;
        transient_trigger(&t);
        transient_calcBlock(&t, gained, 64u);
        for (i = 0u; i < 64u; i++) {
            const int want = (int)plain[i] * 2;

            check(abs((int)gained[i] - want) <= 1,
                  "catalogue gain doubles the sample");
        }
    }

    /* ---- bursts: 2,205 samples and tau = 10 ms ------------------------ */
    transient_init(&t);
    transient_setType(&t, TRANSIENT_TYPE_WAV);
    transient_setWave(&t, (uint8_t)(TRANSIENT_STOCK_PCM_COUNT + sample_count +
                                    6u));   /* sq3 */
    check(t.kind == TRANSIENT_KIND_TABLE, "stock burst kind");
    transient_trigger(&t);
    {
        unsigned total = 0u;
        double early = 0.0;
        double late = 0.0;
        unsigned early_n = 0u;
        unsigned late_n = 0u;

        for (i = 0u; i < 2400u; i += 64u) {
            unsigned j;
            const unsigned n = (i + 64u > 2400u) ? (2400u - i) : 64u;

            transient_calcBlock(&t, out, (uint8_t)n);
            for (j = 0u; j < n; j++) {
                const unsigned at = i + j;
                const double v = (double)out[j];

                if (at < 100u) {
                    early += v * v;
                    early_n++;
                } else if (at >= 2105u && at < 2205u) {
                    late += v * v;
                    late_n++;
                }
                if (at >= 2205u)
                    check(out[j] == 0, "burst stops after 2,205 samples");
            }
            total += n;
        }
        (void)total;
        if (early_n && late_n) {
            const double db = 10.0 * log10((late / late_n) / (early / early_n));

            check(db > -46.0 && db < -40.0, "burst decays about 43 dB");
            if (!(db > -46.0 && db < -40.0))
                printf("  burst decay %.1f dB\n", db);
        }
    }

    /* ---- type round trip keeps the request --------------------------- */
    transient_init(&t);
    transient_setType(&t, TRANSIENT_TYPE_WAV);
    transient_setWave(&t, 50u);
    check(t.waveform == (uint8_t)(TRANSIENT_STOCK_PCM_COUNT + sample_count +
                                  OSC_STOCK_WAVE_COUNT - 1u),
          "live wave clamps to the list");
    transient_setType(&t, TRANSIENT_TYPE_TRS);
    transient_setType(&t, TRANSIENT_TYPE_WAV);
    check(t.waveRequest == 50u, "type round trip keeps the request");

    /* ---- reinstall re-resolves a sample entry ------------------------ */
    transient_init(&t);
    transient_setType(&t, TRANSIENT_TYPE_WAV);
    transient_setWave(&t, (uint8_t)(TRANSIENT_STOCK_PCM_COUNT + 0u));
    check(t.kind == TRANSIENT_KIND_SAMPLE, "sample before reinstall");
    sample_generation = 2u;
    transient_calcBlock(&t, out, 64u);
    check(t.generation == 2u, "generation change re-resolves");

    /*
     * S084 VX2 review fix F9: a stock-burst entry re-resolves on reinstall.
     * With 2 samples, byte 14 is the first stock wave (Sin burst); after a
     * reinstall with 3 samples the same byte is sample 2.
     */
    sample_count = 2u;
    sample_generation = 3u;
    transient_init(&t);
    transient_setType(&t, TRANSIENT_TYPE_WAV);
    transient_setWave(&t, (uint8_t)(TRANSIENT_STOCK_PCM_COUNT + 2u));
    check(t.kind == TRANSIENT_KIND_SINE, "F9 burst before reinstall");
    sample_count = 3u;
    sample_generation = 4u;
    transient_calcBlock(&t, out, 64u);
    check(t.kind == TRANSIENT_KIND_SAMPLE && t.source == 2u,
          "F9 burst entry re-resolves after reinstall");
    sample_count = 2u;

    /* S084 VX2 review N2: the sine burst starts at the table's zero
    ** crossing (index 1024 of the -cos table). */
    transient_init(&t);
    transient_setType(&t, TRANSIENT_TYPE_WAV);
    transient_setWave(&t, (uint8_t)(TRANSIENT_STOCK_PCM_COUNT + 2u));
    transient_trigger(&t);
    check(t.phase == (1024u << 20), "N2 sine burst starts at the zero crossing");

    printf("transient %s (%u failures)\n", failures ? "FAILED" : "OK",
           failures);
    return failures ? 1 : 0;
}
