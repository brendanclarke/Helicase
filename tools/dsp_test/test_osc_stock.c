/*
 * Stock table waves Re3/Sw3 (S086 P2 host gate).
 *
 * What:       runs the current Oscillator.c dispatchers (extracted from the
 *             source) with host tables in place of flash:
 *               D1 routing: for SQ3 and SW3, every level 0..10, random phases,
 *                  increments and gains, each dispatcher's output and
 *                  oscillator state are bit-identical to the Tri/Saw/Rec loop
 *                  called directly with that wave's own table:
 *                  calcNextOscSampleBlock -> calcWavetableOscBlock,
 *                  osc_renderPlainWave -> calcWavetableOscBlock(1.f),
 *                  calcNextOscSampleFmBlock -> calcFmBlock,
 *                  osc_renderSyncWave -> calcWavetableOscSyncBlock,
 *                  calcNextOscSampleRingBlock -> calcWavetableOscRingBlock;
 *               D2 the top-level entry points (plain, FM, sync with its
 *                  ratio, ring) treat SQ3/SW3 exactly as REC when the stock
 *                  tables hold recTable's data;
 *               D3 waveform interpolation, all four modes: Tri -> SQ3/SW3
 *                  equals Tri -> Saw when the stock tables hold sawTable's
 *                  data (the sync phase rule included);
 *               D4 osc_syncWaveform(), osc_syncClockInc() (<< 5 only for
 *                  CRASH and user samples) and osc_isTableWave() for
 *                  waveforms 0..15; the user samples start at 8;
 *               D5 osc_setFreq(): SQ3/SW3 get Saw's phaseInc and tableOffset
 *                  at every test frequency.
 *             The stock tables hold different random data per wave in D1,
 *             so a wrong pointer or a swapped osc_stockTables[] order fails.
 * Why:        086_SESSION_HANDOFF_LOG.md §3.5: the new waves must run
 *             the existing loops, unchanged, through every path.
 * Inputs:     generated fragments (Makefile osc_stock).
 * Outputs:    one line per failure (first 40); "osc_stock failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test osc_stock.
 * Affiliates: Oscillator.c/.h (S086 P2 lines), test_osc_mod.c (the pattern).
 */
#include <stdint.h>
#include <string.h>
#include <math.h>

#define REAL_FS ((float)44108.07291666667f)
#define TABLESIZE 4096
#define OSC_S085_LOOP
#define OSC_S085_SECTION
#define OSC_S085_DISPATCH
#define INTERPOLATE_OSC 1
#define INTERPOLATE_FM_OSC 1
#define OSC_SYNC_NONE 0xFFFFFFFFu   /* must equal Oscillator.h (S085 P1) */

#include "om_defs.inc"
#include "om_sampleinfo.inc"
#include "om_oscinfo.inc"

int16_t sine_table[4097];
int16_t sawTable[11][1024];
int16_t triTable[11][1024];
int16_t recTable[11][1024];
int16_t sq3Table[11][1024];
int16_t sw3Table[11][1024];
unsigned char crashSample[32769];
static int16_t host_sample[4097];
static uint32_t host_rng = 0x0860002u;
static uint8_t host_interp_on;

int16_t GetRngValue(void) { return (int16_t)(golden_rand(&host_rng) >> 16); }
uint8_t modNode_getWaveInterpEnabled(void) { return host_interp_on; }
uint32_t modNode_getWaveInterpGeneration(void) { return 7u; }
uint8_t sampleMemory_getNumSamples(void) { return 1u; }
uint32_t sampleMemory_getGeneration(void) { return 1u; }
uint8_t sampleMemory_isLooped(uint8_t index) { (void)index; return 0u; }
SampleInfo sampleMemory_getSampleInfo(uint8_t index)
{
    SampleInfo info;
    (void)index;
    memset(&info, 0, sizeof(info));
    return info;
}

void calcNextOscSampleBlock(OscInfo* osc, int16_t* buf, const uint8_t size,
                            const float gain);
void calcNextOscSampleFmBlock(OscInfo* osc, int16_t* modBuffer, int16_t* buf,
                              uint8_t size, const float gain);

#include "om_scratch.inc"
#include "om_stock.inc"
#include "om_plain.c"
#include "os_fm.c"
#include "om_map.c"
#include "om_s085.c"
#include "os_inc4096.inc"   /* freq2PhaseIncr() (a trailing comment defeats --func) */
#include "os_freq.c"
#include "os_tablewave.c"

#define N OUTPUT_DMA_SIZE
static unsigned failures;

static void fail(const char *what, unsigned a, unsigned b, unsigned c)
{
    failures++;
    if (failures <= 40u)
        printf("FAIL %s (%u, %u, %u)\n", what, a, b, c);
}

static void fill_random(int16_t t[11][1024], uint32_t *s)
{
    for (unsigned o = 0u; o < 11u; ++o)
        for (unsigned i = 0u; i < 1024u; ++i)
            t[o][i] = (int16_t)(golden_rand(s) >> 16);
}

static void fill_tables(void)
{
    uint32_t s = 0x5eed086u;

    for (unsigned i = 0u; i < 4097u; ++i)
        sine_table[i] = (int16_t)lrint(32767.0 * sin(2.0 * M_PI * (double)i / 4096.0));
    fill_random(sawTable, &s);
    fill_random(triTable, &s);
    fill_random(recTable, &s);
    fill_random(sq3Table, &s);
    fill_random(sw3Table, &s);
    for (unsigned i = 0u; i < 32769u; ++i)
        crashSample[i] = (unsigned char)golden_rand(&s);
    for (unsigned i = 0u; i < 4096u; ++i)
        host_sample[i] = (int16_t)(golden_rand(&s) >> 16);
    host_sample[4096] = 0;
}

static OscInfo make_osc(uint8_t wave, uint8_t level, uint32_t phase, uint32_t inc)
{
    OscInfo o;

    memset(&o, 0, sizeof(o));
    o.waveform = wave;
    o.waveInterpNext = wave;
    o.phase = phase;
    o.phaseInc = inc;
    o.tableOffset = level;
    o.fmMod = 0.37f;
    return o;
}

static int same_state(const OscInfo *a, const OscInfo *b)
{
    return a->phase == b->phase && a->output == b->output;
}

static int same_buf(const int16_t *a, const int16_t *b)
{
    return memcmp(a, b, N * sizeof(int16_t)) == 0;
}

static void random_mod(int16_t *m, uint32_t *s)
{
    for (unsigned i = 0u; i < N; ++i)
        m[i] = (int16_t)(golden_rand(s) >> 16);
}

/* A reset list with resets on about a quarter of the samples. */
static void random_over(uint32_t *over, uint32_t *s)
{
    for (unsigned i = 0u; i < N; ++i)
        over[i] = (golden_rand(s) & 3u) ? OSC_SYNC_NONE : golden_rand(s);
}

typedef const int16_t (*TableRef)[1024];

/* D1: each dispatcher -> the table loop with the wave's own table. */
static void d1_routing(void)
{
    static const uint8_t waves[2] = { SQ3, SW3 };
    const TableRef tables[2] = { sq3Table, sw3Table };
    uint32_t s = 0xd1d1d1u;
    int16_t mod[N], ba[N], bb[N];
    uint32_t over[N];

    for (unsigned w = 0u; w < 2u; ++w)
        for (uint8_t lv = 0u; lv < 11u; ++lv)
            for (unsigned r = 0u; r < 24u; ++r) {
                const uint32_t ph = golden_rand(&s);
                const uint32_t inc = golden_rand(&s) >> (r % 12u);
                const float gain = (float)(golden_rand(&s) >> 8) / 16777216.0f * 1.5f;
                const uint32_t ratio = golden_rand(&s) >> 8;
                const int32_t depth = (int32_t)(golden_rand(&s) >> 2);
                const int32_t d15 = (int32_t)(golden_rand(&s) % 32769u);
                const int32_t step = (int32_t)(golden_rand(&s) % 129u) - 64;
                OscInfo a, b;

                random_mod(mod, &s);
                random_over(over, &s);

                a = make_osc(waves[w], lv, ph, inc); b = a;
                calcNextOscSampleBlock(&a, ba, N, gain);
                calcWavetableOscBlock(&b, tables[w], bb, N, gain);
                if (!same_buf(ba, bb) || !same_state(&a, &b)) fail("D1 plain", w, lv, r);

                a = make_osc(waves[w], lv, ph, inc); b = a;
                osc_renderPlainWave(&a, ba, N);
                calcWavetableOscBlock(&b, tables[w], bb, N, 1.f);
                if (!same_buf(ba, bb) || !same_state(&a, &b)) fail("D1 renderPlain", w, lv, r);

                a = make_osc(waves[w], lv, ph, inc); b = a;
                calcNextOscSampleFmBlock(&a, mod, ba, N, gain);
                calcFmBlock(&b, tables[w], mod, bb, N, gain);
                if (!same_buf(ba, bb) || !same_state(&a, &b)) fail("D1 fm", w, lv, r);

                a = make_osc(waves[w], lv, ph, inc); b = a;
                osc_renderSyncWave(&a, over, ratio, depth, ba, N);
                calcWavetableOscSyncBlock(&b, tables[w], over, ratio, depth, bb, N);
                if (!same_buf(ba, bb) || !same_state(&a, &b)) fail("D1 sync", w, lv, r);

                a = make_osc(waves[w], lv, ph, inc); b = a;
                calcNextOscSampleRingBlock(&a, mod, ba, N, d15, step);
                calcWavetableOscRingBlock(&b, tables[w], mod, bb, N, d15, step);
                if (!same_buf(ba, bb) || !same_state(&a, &b)) fail("D1 ring", w, lv, r);
            }
}

/* Render one block of wave through all four top-level entry points. */
static void render4(uint8_t wave, uint8_t next, float frac, uint8_t lv,
                    uint32_t ph, uint32_t inc, const int16_t *mod,
                    const uint32_t *over, uint32_t clockInc, int32_t depth,
                    int32_t d15, int32_t step, int16_t out[4][N], OscInfo st[4])
{
    for (unsigned m = 0u; m < 4u; ++m) {
        OscInfo o = make_osc(wave, lv, ph, inc);

        o.waveInterpNext = next;
        o.waveInterpFrac = frac;
        o.waveInterpGeneration = 7u;
        if (m == 0u) calcNextOscSampleBlock(&o, out[m], N, 0.8f);
        if (m == 1u) calcNextOscSampleFmBlock(&o, (int16_t *)mod, out[m], N, 0.8f);
        if (m == 2u) calcNextOscSampleSyncBlock(&o, over, clockInc, depth, out[m], N);
        if (m == 3u) calcNextOscSampleRingBlock(&o, mod, out[m], N, d15, step);
        st[m] = o;
    }
}

static void compare4(const char *what, int16_t a[4][N], OscInfo sa[4],
                     int16_t b[4][N], OscInfo sb[4], unsigned w, unsigned r)
{
    for (unsigned m = 0u; m < 4u; ++m)
        if (!same_buf(a[m], b[m]) || !same_state(&sa[m], &sb[m]))
            fail(what, w, m, r);
}

/* D2: with REC's data, SQ3/SW3 behave exactly as REC (no interpolation).
** D3: with SAW's data, Tri -> SQ3/SW3 equals Tri -> Saw (interpolation). */
static void d2_d3_equivalence(void)
{
    static const uint8_t waves[2] = { SQ3, SW3 };
    int16_t (*const stock[2])[1024] = { sq3Table, sw3Table };
    static int16_t keep[2][11][1024];
    uint32_t s = 0xd2d3u;
    int16_t mod[N], oa[4][N], ob[4][N];
    OscInfo sa[4], sb[4];
    uint32_t over[N];

    memcpy(keep[0], sq3Table, sizeof(sq3Table));
    memcpy(keep[1], sw3Table, sizeof(sw3Table));

    for (unsigned w = 0u; w < 2u; ++w)
        for (unsigned r = 0u; r < 64u; ++r) {
            const uint8_t lv = (uint8_t)(r % 11u);
            const uint32_t ph = golden_rand(&s);
            const uint32_t inc = golden_rand(&s) >> (2u + r % 10u);
            const uint32_t clockInc = (golden_rand(&s) >> (2u + r % 8u)) | 1u;
            const int32_t depth = (int32_t)(golden_rand(&s) >> 2);
            const int32_t d15 = (int32_t)(golden_rand(&s) % 32769u);
            const int32_t step = (int32_t)(golden_rand(&s) % 129u) - 64;
            const float frac = 0.05f + 0.9f * (float)(golden_rand(&s) >> 8) / 16777216.0f;

            random_mod(mod, &s);
            random_over(over, &s);

            host_interp_on = 0u;
            memcpy(stock[w], recTable, sizeof(recTable));
            render4(waves[w], waves[w], 0.f, lv, ph, inc, mod, over, clockInc,
                    depth, d15, step, oa, sa);
            render4(REC, REC, 0.f, lv, ph, inc, mod, over, clockInc,
                    depth, d15, step, ob, sb);
            compare4("D2 stock == Rec", oa, sa, ob, sb, w, r);

            host_interp_on = 1u;
            memcpy(stock[w], sawTable, sizeof(sawTable));
            render4(TRI, waves[w], frac, lv, ph, inc, mod, over, clockInc,
                    depth, d15, step, oa, sa);
            render4(TRI, SAW, frac, lv, ph, inc, mod, over, clockInc,
                    depth, d15, step, ob, sb);
            compare4("D3 Tri->stock == Tri->Saw", oa, sa, ob, sb, w, r);
        }
    host_interp_on = 0u;
    memcpy(sq3Table, keep[0], sizeof(sq3Table));
    memcpy(sw3Table, keep[1], sizeof(sw3Table));
}

/* D4: the waveform partitions, 0..15. */
static void d4_partitions(void)
{
    if (OSC_SAMPLE_START != 8) fail("D4 OSC_SAMPLE_START", OSC_SAMPLE_START, 8u, 0u);
    for (uint8_t wv = 0u; wv < 16u; ++wv) {
        const int table = wv == TRI || wv == SAW || wv == REC || wv == SQ3 || wv == SW3;
        const int synced = wv != NOISE && wv < OSC_SAMPLE_START;
        const int sampleClock = wv == CRASH || wv >= OSC_SAMPLE_START;
        OscInfo m = make_osc(wv, 0u, 0u, 0x00123456u);
        const uint32_t want = sampleClock ? (0x00123456u << 5) : 0x00123456u;

        if (osc_isTableWave(wv) != table) fail("D4 osc_isTableWave", wv, osc_isTableWave(wv), (unsigned)table);
        if (osc_syncWaveform(wv) != synced) fail("D4 osc_syncWaveform", wv, osc_syncWaveform(wv), (unsigned)synced);
        if (osc_syncClockInc(&m) != want) fail("D4 osc_syncClockInc", wv, osc_syncClockInc(&m), want);
    }
}

/* D5: osc_setFreq() gives the stock waves Saw's increment and level. */
static void d5_setfreq(void)
{
    for (unsigned i = 0u; i < 400u; ++i) {
        const float f = 5.0f * powf(1.03f, (float)i);
        OscInfo saw = make_osc(SAW, 0u, 0u, 0u);

        saw.freq = f; saw.pitchMod = 1.f; saw.modNodeValue = 1.f;
        osc_setFreq(&saw);
        for (uint8_t wv = SQ3; wv <= SW3; ++wv) {
            OscInfo o = saw;

            o.waveform = wv; o.phaseInc = 0u; o.tableOffset = 0xFFu; o.freqCacheValid = 0u;
            osc_setFreq(&o);
            if (o.phaseInc != saw.phaseInc || o.tableOffset != saw.tableOffset)
                fail("D5 osc_setFreq", i, wv, o.tableOffset);
        }
    }
}

int main(void)
{
    fill_tables();
    d1_routing();
    d2_d3_equivalence();
    d4_partitions();
    d5_setfreq();
    printf("osc_stock failures: %u\n", failures);
    return failures ? 1 : 0;
}
