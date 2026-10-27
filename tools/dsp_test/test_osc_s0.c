/*
 * Pre-S085 / S085 bit-identity proof (S085 C17.5).
 *
 * What:       renders the same oscillator input through the frozen85 loops and
 *             the current loops with phaseIncStep = 0 (rst) and requires
 *             bit-identical blocks (sine, tri, saw, rec, crash, plain and FM;
 *             noise); then a 20-block pitch sweep through the frozen and the
 *             current osc_setFreq() with rendering (A7); then Drum fm/mix and
 *             Cymbal/HiHat FM2 against the verbatim frozen85 engine lines
 *             (s0_engines.c; HiHat carrier gain 0.5) (A7).
 * Why:        G3 S0 claim for plan §5 item 3.
 * Inputs:     build/gen/s0_old.c (frozen85 renderers, renamed old_*),
 *             s0_old_freq.c (frozen osc_setFreq), s0_*_lines.c (engine
 *             lines).
 * Outputs:    "N failures".
 * Accessors:  make -C tools/dsp_test osc_s0.
 * Affiliates: Oscillator.c, OscSection.c, frozen85 snapshot.
 */
#include "prelude_osc.h"
#include "Oscillator.h"
#include "OscSection.h"
#include "BufferTools.h"
#include "wavetable.h"
#include "s0_engine_shim.h"

void host_rng_reset(void);
void host_catalogue_reset(void);

void old_calcSineBlock(OscInfo*, int16_t*, const uint8_t, const float);
void old_calcFmSineBlock(OscInfo*, int16_t*, int16_t*, uint8_t, const float);
void old_calcWavetableOscBlock(OscInfo*, const int16_t[][1024], int16_t*, const uint8_t, const float);
void old_calcFmBlock(OscInfo*, const int16_t[][1024], int16_t*, int16_t*, uint8_t, const float);
void old_calcSampleOscBlock(OscInfo*, int16_t*, const uint8_t, const float);
void old_calcSampleOscFmBlock(OscInfo*, int16_t*, int16_t*, uint8_t, const float);
void old_calcNoiseBlock(OscInfo*, int16_t*, const uint8_t, const float);
void old_osc_setFreq(OscInfo*);

static int failures;

static OscInfo mk(uint8_t waveform)
{
    OscInfo o;

    memset(&o, 0, sizeof(o));
    o.oscType = OSC_TYPE_WAV;
    o.waveform = waveform;
    o.phase = 0x12345678u;
    o.phaseInc = 0x00123456u;
    o.phaseIncStep = 0;
    o.tableOffset = 5u;
    o.fmMod = 1.234f;
    return o;
}

static void cmp_block(const char *what, const int16_t *a, const int16_t *b,
                      uint8_t n)
{
    for (uint8_t i = 0u; i < n; i++) {
        if (a[i] != b[i]) {
            printf("FAIL %s sample %u: %d != %d\n", what, i, a[i], b[i]);
            failures++;
            return;
        }
    }
}

static void check_plain(uint8_t waveform, const char *name)
{
    OscInfo o = mk(waveform);
    OscInfo p = o;
    int16_t a[OUTPUT_DMA_SIZE], b[OUTPUT_DMA_SIZE];

    if (waveform == SINE) {
        old_calcSineBlock(&o, a, OUTPUT_DMA_SIZE, 1.0f);
        calcSineBlock(&p, b, OUTPUT_DMA_SIZE, 1.0f);
    } else if (waveform <= REC) {
        const int16_t (*tab)[1024] = (waveform == TRI) ? triTable
            : (waveform == SAW) ? sawTable : recTable;
        old_calcWavetableOscBlock(&o, tab, a, OUTPUT_DMA_SIZE, 1.0f);
        calcWavetableOscBlock(&p, tab, b, OUTPUT_DMA_SIZE, 1.0f);
    } else {
        old_calcSampleOscBlock(&o, a, OUTPUT_DMA_SIZE, 1.0f);
        calcSampleOscBlock(&p, b, OUTPUT_DMA_SIZE, 1.0f);
    }
    cmp_block(name, a, b, OUTPUT_DMA_SIZE);
}

static void check_fm(uint8_t waveform, const char *name)
{
    OscInfo o = mk(waveform);
    OscInfo p = o;
    int16_t mod[OUTPUT_DMA_SIZE], a[OUTPUT_DMA_SIZE], b[OUTPUT_DMA_SIZE];

    for (uint8_t i = 0u; i < OUTPUT_DMA_SIZE; i++)
        mod[i] = (int16_t)(i * 1200 - 16000);
    if (waveform == SINE) {
        old_calcFmSineBlock(&o, mod, a, OUTPUT_DMA_SIZE, 1.0f);
        calcFmSineBlock(&p, mod, b, OUTPUT_DMA_SIZE, 1.0f);
    } else if (waveform <= REC) {
        const int16_t (*tab)[1024] = (waveform == TRI) ? triTable
            : (waveform == SAW) ? sawTable : recTable;
        old_calcFmBlock(&o, tab, mod, a, OUTPUT_DMA_SIZE, 1.0f);
        calcFmBlock(&p, tab, mod, b, OUTPUT_DMA_SIZE, 1.0f);
    } else {
        old_calcSampleOscFmBlock(&o, mod, a, OUTPUT_DMA_SIZE, 1.0f);
        calcSampleOscFmBlock(&p, mod, b, OUTPUT_DMA_SIZE, 1.0f);
    }
    cmp_block(name, a, b, OUTPUT_DMA_SIZE);
}

/* The pre-S085 dispatchers for the wav list 0..5 (frozen85 routing). */
static const int16_t (*old_table(uint8_t waveform))[1024]
{
    return (waveform == TRI) ? triTable : (waveform == SAW) ? sawTable
                                                            : recTable;
}

void old_dispatch(OscInfo *osc, int16_t *buf, uint8_t size, float gain)
{
    switch (osc->waveform) {
    case SINE:  old_calcSineBlock(osc, buf, size, gain); break;
    case NOISE: old_calcNoiseBlock(osc, buf, size, gain); break;
    case CRASH: old_calcSampleOscBlock(osc, buf, size, gain); break;
    default:
        old_calcWavetableOscBlock(osc, old_table(osc->waveform), buf, size,
                                  gain);
        break;
    }
}

void old_dispatchFm(OscInfo *osc, int16_t *mod, int16_t *buf, uint8_t size,
                    float gain)
{
    switch (osc->waveform) {
    case SINE:  old_calcFmSineBlock(osc, mod, buf, size, gain); break;
    case NOISE: old_calcNoiseBlock(osc, buf, size, gain); break;
    case CRASH: old_calcSampleOscFmBlock(osc, mod, buf, size, gain); break;
    default:
        old_calcFmBlock(osc, old_table(osc->waveform), mod, buf, size, gain);
        break;
    }
}

/* A7: the noise loop, same random sequence on both sides. */
static void check_noise(void)
{
    OscInfo o = mk(NOISE), p;
    int16_t a[OUTPUT_DMA_SIZE], b[OUTPUT_DMA_SIZE];

    o.phaseInc = 0x30000000u;    /* several wraps per block */
    p = o;
    host_rng_reset();
    old_calcNoiseBlock(&o, a, OUTPUT_DMA_SIZE, 0.9f);
    host_rng_reset();
    calcNoiseBlock(&p, b, OUTPUT_DMA_SIZE, 0.9f);
    cmp_block("noise", a, b, OUTPUT_DMA_SIZE);
}

/*
 * A7: a pitch sweep. Each block the frozen osc_setFreq() and the current one
 * (no slew armed) set the increment and table level for a new frequency,
 * then both render; increments, levels and samples must match every block.
 */
static void check_sweep(void)
{
    const uint8_t waves[] = { SINE, TRI, SAW, REC, NOISE, CRASH };
    const char *names[] = { "sweep sine", "sweep tri", "sweep saw",
                            "sweep rec", "sweep noise", "sweep crash" };

    for (uint8_t w = 0u; w < 6u; w++) {
        OscInfo o = mk(waves[w]), p;
        float f = 27.5f;

        o.freq = f;
        o.pitchMod = 1.0f;
        o.modNodeValue = 1.0f;
        o.freqCacheValid = 0u;
        p = o;
        for (int b = 0; b < 20; b++) {
            int16_t a[OUTPUT_DMA_SIZE], c[OUTPUT_DMA_SIZE];

            o.freq = f;
            p.freq = f;
            old_osc_setFreq(&o);
            osc_setFreq(&p);
            if (o.phaseInc != p.phaseInc || o.tableOffset != p.tableOffset) {
                printf("FAIL %s block %d: inc %u/%u level %u/%u\n", names[w], b,
                       o.phaseInc, p.phaseInc, o.tableOffset, p.tableOffset);
                failures++;
                break;
            }
            /* Noise draws from one generator: render old and new in turn
            ** from the same generator state. */
            host_rng_reset();
            old_dispatch(&o, a, OUTPUT_DMA_SIZE, 1.0f);
            host_rng_reset();
            calcNextOscSampleBlock(&p, c, OUTPUT_DMA_SIZE, 1.0f);
            cmp_block(names[w], a, c, OUTPUT_DMA_SIZE);
            f *= 1.37f;
        }
    }
}

/* A7: Drum fm/mix and Cymbal/HiHat FM2 against the frozen engine lines. */
static void check_engines(void)
{
    const struct { uint8_t car, mod, mix; float amt; const char *name; } drum[] = {
        { SAW, TRI, 0u, 0.5f, "drum fm saw/tri" },
        { SINE, REC, 0u, 0.8f, "drum fm sine/rec" },
        { SINE, SAW, 1u, 0.3f, "drum mix sine/saw" },
        { CRASH, SINE, 1u, 0.6f, "drum mix crash/sine" },
    };

    host_catalogue_reset();
    for (unsigned k = 0u; k < sizeof drum / sizeof drum[0]; k++) {
        OldVoice v;
        OscInfo car, mod;
        int16_t a[OUTPUT_DMA_SIZE], b[OUTPUT_DMA_SIZE];
        int16_t ma[OUTPUT_DMA_SIZE], mb[OUTPUT_DMA_SIZE];

        memset(&v, 0, sizeof v);
        v.osc = mk(drum[k].car);
        v.modOsc = mk(drum[k].mod);
        v.modOsc.phase = 0x0ABCDEF0u;
        v.mixOscs = drum[k].mix;
        v.fmModAmount = drum[k].amt;
        car = v.osc;
        mod = v.modOsc;
        old_drumOsc(&v, a, ma, OUTPUT_DMA_SIZE);
        oscSection_renderDrum(&car, &mod, drum[k].mix ? OSC_MOD_MIX : OSC_MOD_FM,
                              drum[k].amt, b, mb, OUTPUT_DMA_SIZE, 0, 0);
        cmp_block(drum[k].name, a, b, OUTPUT_DMA_SIZE);
        if (car.phase != v.osc.phase || mod.phase != v.modOsc.phase) {
            printf("FAIL %s: phases differ\n", drum[k].name);
            failures++;
        }
    }
    for (int hat = 0; hat < 2; hat++) {
        OldVoice v;
        OscInfo car, m1, m2;
        int16_t a[OUTPUT_DMA_SIZE], b[OUTPUT_DMA_SIZE];
        int16_t x1[OUTPUT_DMA_SIZE], x2[OUTPUT_DMA_SIZE];
        int16_t y1[OUTPUT_DMA_SIZE], y2[OUTPUT_DMA_SIZE];

        memset(&v, 0, sizeof v);
        v.osc = mk(SAW);
        v.modOsc = mk(SINE);
        v.modOsc2 = mk(TRI);
        v.modOsc2.phase = 0x3456789Au;
        v.fmModAmount1 = 0.7f;
        v.fmModAmount2 = 0.3f;
        car = v.osc;
        m1 = v.modOsc;
        m2 = v.modOsc2;
        if (hat)
            old_hatOsc(&v, a, x1, x2, OUTPUT_DMA_SIZE);
        else
            old_cymOsc(&v, a, x1, x2, OUTPUT_DMA_SIZE);
        oscSection_renderFm2(&car, &m1, &m2, 0.7f, 0.3f, hat ? 0.5f : 1.0f,
                             b, y1, y2, OUTPUT_DMA_SIZE, 0, 0);
        cmp_block(hat ? "hihat fm2 (0.5)" : "cymbal fm2", a, b,
                  OUTPUT_DMA_SIZE);
    }
}

int main(void)
{
    const uint8_t waves[] = { SINE, TRI, SAW, REC, CRASH };
    const char *names[] = { "sine", "tri", "saw", "rec", "crash" };

    for (uint8_t i = 0u; i < 5u; i++) {
        check_plain(waves[i], names[i]);
        check_fm(waves[i], names[i]);
    }

    check_noise();
    check_sweep();
    check_engines();

    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
