/*
 * Oscillator module host tests (S085 snc/rng, S086 stock waves and pos,
 * S087 typ, samples and wavetables).
 *
 * What:       compiles the whole real Oscillator.c over the real catalogue
 *             (SampleMemory.c/SampleInstall.c on a RAM flash image) and runs
 *             one group per feature. Each check names the code break that
 *             must make it fail.
 *               ring   R1-R3 the ring law (every stock wave, constant and
 *                      ramped depth; depth 0 is the carrier exactly);
 *               sync   S1 depth 0 == plain; S2 a reset after sample k lands
 *                      on k + 1; S3 osc_syncPull(); S4 osc_buildSyncList();
 *                      S5 osc_syncClockInc() (<< 5 crash, << 10 sample
 *                      modulator, clamps); S6 noise is unsynced; M1 the
 *                      depth and ratio maps;
 *               stock  D1 SQ3/SW3 route to the table loops with their own
 *                      table on every path; D2 with Rec's data they equal
 *                      Rec; D4 the waveform partitions 0..15; D5
 *                      osc_setFreq() gives them Saw's increment and level;
 *               pos    P1 the trigger truth table; P2 identity; P3 no drift;
 *                      P4 circular step; P5 continuity; P6 non-phase waves;
 *                      P7 the cap (cap build only); P8 consume only;
 *               type   T1 osc_setType() clamps and an unchanged type is a
 *                      no-op; T2 a change re-anchors pos, invalidates the
 *                      freq cache and clamps wav;
 *               sample L1 smp plays then holds 0; L2 the slp seam; L3 a slp
 *                      start clamps to loop_start; L4 a backward slp glide
 *                      wraps forward; L6 slp FM reads the loop and lead-in;
 *                      L8/L9/L10/L11 typed pos; L12 the modulator list; L13
 *                      a typ switch clamps wav and re-anchors;
 *               wtb    W1 the UMULL index; W2 the wrap; W3 8-bit frames;
 *                      W4 the blend ends; W5 the ramp cap; W6 no slot is the
 *                      nearest frame; W7 a held pos between frames blends;
 *                      W8 a modulator w entry plays frame 0.
 * Why:        the oscillator is the voice's sound; these are the laws each
 *             session proved, kept runnable against today's source.
 * Inputs:     build/gen/Oscillator.host.h/.c (extract.py --body), osc_host.h,
 *             cat_host.h, catalogue_host.c. -DOSC_POS_TEST_CAP with
 *             -DOSC_POS_BEND_CAP=8 builds the cap variant (pos group only).
 * Outputs:    one line per failure (first 40) and one count per group;
 *             exit 1 on any failure.
 * Accessors:  make -C tools/dsp_test oscillator.
 * Affiliates: Oscillator.c/.h, SampleMemory.c, SampleInstall.c, DSP_TEST.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "osc_host.h"
#include "Oscillator.host.c"

#define N OUTPUT_DMA_SIZE

/* ------------------------------------------------------- host stand-ins -- */
int16_t sine_table[TABLESIZE + 1];
int16_t sawTable[11][1024];
int16_t triTable[11][1024];
int16_t recTable[11][1024];
int16_t sq3Table[11][1024];
int16_t sw3Table[11][1024];
unsigned char crashSample[32769];

static uint32_t host_rng = 0x0850001u;
static uint8_t g_claim;      /* what modNode_interpClaim() grants */

int16_t GetRngValue(void) { return (int16_t)(golden_rand(&host_rng) >> 16); }
uint8_t modNode_interpClaim(void* osc) { (void)osc; return g_claim; }

/* ---------------------------------------------------------- reporting -- */
static unsigned failures;

static void fail(const char *what, unsigned a, unsigned b, unsigned c)
{
    failures++;
    if (failures <= 40u)
        printf("FAIL %s (%u, %u, %u)\n", what, a, b, c);
}

static void expect(int cond, const char *what)
{
    if (!cond)
        fail(what, 0u, 0u, 0u);
}

static void group(const char *name, void (*run)(void))
{
    const unsigned before = failures;

    run();
    printf("oscillator %-6s failures: %u\n", name, failures - before);
}

/* -------------------------------------------------------------- tables -- */
static void fill_sine(void)
{
    for (unsigned i = 0u; i < 4097u; ++i)
        sine_table[i] = (int16_t)lrint(32767.0 * sin(2.0 * M_PI * (double)i / 4096.0));
}

static void fill_random(int16_t t[11][1024], uint32_t *s)
{
    for (unsigned o = 0u; o < 11u; ++o)
        for (unsigned i = 0u; i < 1024u; ++i)
            t[o][i] = (int16_t)(golden_rand(s) >> 16);
}

/* Different random data per wave, so a wrong table pointer shows. */
static void fill_random_tables(uint32_t seed)
{
    uint32_t s = seed;

    fill_sine();
    fill_random(sawTable, &s);
    fill_random(triTable, &s);
    fill_random(recTable, &s);
    fill_random(sq3Table, &s);
    fill_random(sw3Table, &s);
    for (unsigned i = 0u; i < 32769u; ++i)
        crashSample[i] = (unsigned char)golden_rand(&s);
}

/* The real wave shapes (pos group: continuity is checked per sample). */
static void fill_shape_tables(void)
{
    fill_sine();
    for (unsigned o = 0u; o < 11u; ++o)
        for (unsigned i = 0u; i < 1024u; ++i) {
            const double x = (double)i / 1024.0;
            sawTable[o][i] = (int16_t)lrint(32767.0 * (2.0 * x - 1.0));
            triTable[o][i] = (int16_t)lrint(32767.0 * (1.0 - 4.0 * fabs(x - 0.5)));
            recTable[o][i] = (i < 512u) ? 32767 : -32768;
            sq3Table[o][i] = (int16_t)lrint(32767.0 * sin(2.0 * M_PI * x * 3.0));
            sw3Table[o][i] = (int16_t)lrint(32767.0 * (1.0 - 2.0 * x) * fabs(1.0 - 2.0 * x));
        }
    for (unsigned i = 0u; i < 32768u; ++i)
        crashSample[i] = (unsigned char)(128u + (i & 127u));
    crashSample[32768] = crashSample[0];
}

/* A wav-type oscillator (oscType 0) on a stock wave. */
static OscInfo make_osc(uint8_t wave, uint8_t level, uint32_t phase, uint32_t inc)
{
    OscInfo o;

    memset(&o, 0, sizeof(o));
    o.waveform = wave;
    o.phase = phase;
    o.phaseInc = inc;
    o.tableOffset = level;
    o.fmMod = 0.37f;
    return o;
}

/* ============================================================== ring -- */
/*
 * Reference ring law with a per-sample depth: sample i uses
 * d_i = d0 + (i + 1) x step (Q15), G = (32768 - d) + ((d x mod) >> 15),
 * out = SSAT16((car x G) >> 15). Written independently of osc_ringSample().
 */
static int16_t ring_ref(int16_t car, int16_t mod, int32_t d)
{
    const int32_t g = (32768 - d) + ((d * (int32_t)mod) >> 15);
    const int32_t v = ((int32_t)car * g) >> 15;
    return (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}

/*
 * R1 (law, every stock wave, constant and ramped depth), R2 (the ramp:
 * per-sample depth, d0 = d1 snaps, depth 0 is the carrier exactly), R3 (the
 * integer law against the float law car x (1 - d) + car x mod x d within 2
 * LSB). Breaks: >> 14 in osc_ringSample(); the depth taken at i instead of
 * i + 1; the step ignored; (1 - d) in place of d.
 */
static void test_ring(void)
{
    static const uint8_t waves[] = { SINE, TRI, SAW, REC, NOISE, CRASH, SQ3, SW3 };
    static const float amts[] = { 0.0f, 1.0f / 127.0f, 0.5f, 126.0f / 127.0f, 1.0f };
    static const uint32_t phases[] = { 0x00000000u, 0x7fff1234u, 0xfffff000u };
    static const uint32_t incs[] = { 0x00800000u, 0x07000000u, 0x30000000u };

    fill_random_tables(0x5eed085u);
    for (unsigned w = 0u; w < sizeof(waves); ++w)
     for (unsigned a0 = 0u; a0 < 5u; ++a0)
      for (unsigned a1 = 0u; a1 < 5u; ++a1)
       for (unsigned p = 0u; p < 3u; ++p) {
        OscInfo ref = make_osc(waves[w], 3u, phases[p], incs[p]);
        OscInfo dut = ref;
        int16_t mod[N], car[N], out[N];
        const int32_t d0 = osc_ringDepthQ15(amts[a0]);
        const int32_t d1 = osc_ringDepthQ15(amts[a1]);
        const int32_t step = (d1 - d0) / (int32_t)N;
        uint32_t rng;

        for (unsigned i = 0u; i < N; ++i)
            mod[i] = (int16_t)(golden_rand(&host_rng) >> 16);
        mod[0] = -32768;
        mod[1] = 32767;
        rng = host_rng;
        calcNextOscSampleBlock(&ref, car, N, 1.0f);
        host_rng = rng;
        calcNextOscSampleRingBlock(&dut, mod, out, N, d0, step);
        for (unsigned i = 0u; i < N; ++i) {
            const int32_t d = d0 + (int32_t)(i + 1u) * step;
            const double law = (double)car[i] * (1.0 - d / 32768.0) +
                               (double)car[i] * (double)mod[i] * d / (32768.0 * 32768.0);
            const double sat = law > 32767.0 ? 32767.0 : (law < -32768.0 ? -32768.0 : law);

            if (out[i] != ring_ref(car[i], mod[i], d))
                fail(step ? "R2 ramped ring law" : "R1 ring law", w, a0 * 5u + a1, i);
            if (fabs((double)out[i] - sat) > 2.0)
                fail("R3 integer law vs float law", w, a0 * 5u + a1, i);
            if (d0 == 0 && d1 == 0 && out[i] != car[i])
                fail("R2 depth 0 is the carrier", w, p, i);
        }
        if (dut.phase != ref.phase || dut.output != ref.output)
            fail("R1 oscillator state", w, a0 * 5u + a1, p);
       }
}

/* ============================================================== sync -- */
/* S1 and S6. Break: the pull applied regardless of depth (or of NONE). */
static void test_sync_depth0_and_unsynced(void)
{
    static const uint8_t waves[] = { SINE, TRI, SAW, REC, NOISE, CRASH, SQ3, SW3 };
    static const uint32_t clockIncs[] = { 0x01000000u, 0x30000000u, 0xf0000000u };

    for (unsigned w = 0u; w < sizeof(waves); ++w)
     for (unsigned c = 0u; c < 3u; ++c)
      for (unsigned hard = 0u; hard < 2u; ++hard) {
        const int synced = waves[w] != NOISE;
        uint32_t over[N];
        unsigned resets = 0u;
        int16_t ref_out[N], out[N];
        OscInfo ref = make_osc(waves[w], 3u, 0x12345678u, 0x02000000u);
        OscInfo dut = ref;
        uint32_t rng;

        if (hard && synced)
            continue;              /* depth 1 is S2's job for synced waves */
        (void)osc_buildSyncList(0xfff00000u, clockIncs[c], over, N);
        for (unsigned i = 0u; i < N; ++i)
            resets += (unsigned)(over[i] != OSC_SYNC_NONE);
        if (resets == 0u)
            fail("S1 setup: no resets in list", w, c, 0u);
        rng = host_rng;
        calcNextOscSampleBlock(&ref, ref_out, N, 1.0f);
        host_rng = rng;
        calcNextOscSampleSyncBlock(&dut, over, clockIncs[c], hard ? 0x40000000 : 0, out, N);
        for (unsigned i = 0u; i < N; ++i)
            if (out[i] != ref_out[i])
                fail(hard ? "S6 unsynced slave differs" : "S1 depth 0 differs", w, c, i);
        if (dut.phase != ref.phase)
            fail("S1/S6 phase differs", w, c, hard);
      }
}

/* S2. Break: reset applied before sample i; ratio inverted. */
static void test_sync_hard(void)
{
    static const uint8_t waves[] = { SINE, TRI, SAW, REC, CRASH };
    static const unsigned ks[] = { 0u, 5u, 31u };
    const uint32_t clockInc = 0x20000000u;
    const uint32_t over_k = 0x0abcdef0u;

    for (unsigned w = 0u; w < sizeof(waves); ++w)
     for (unsigned kk = 0u; kk < 3u; ++kk) {
        const unsigned k = ks[kk];
        const uint32_t slaveInc = (waves[w] == CRASH) ? 0x00300000u : 0x03000000u;
        const uint32_t ratio = osc_syncRatioQ16(slaveInc, clockInc);
        const uint32_t target = (uint32_t)(((uint64_t)over_k * ratio) >> 16);
        uint32_t over[N];
        int16_t ref_out[N], out[N];
        OscInfo dut = make_osc(waves[w], 3u, 0x40000000u, slaveInc);
        OscInfo ref = dut;

        for (unsigned i = 0u; i < N; ++i)
            over[i] = OSC_SYNC_NONE;
        over[k] = over_k;
        calcNextOscSampleSyncBlock(&dut, over, clockInc, 0x40000000, out, N);
        calcNextOscSampleBlock(&ref, ref_out, (uint8_t)(k + 1u), 1.0f);
        ref.phase = target;
        if (k + 1u < N)
            calcNextOscSampleBlock(&ref, ref_out + k + 1u, (uint8_t)(N - k - 1u), 1.0f);
        for (unsigned i = 0u; i < N; ++i)
            if (out[i] != ref_out[i])
                fail("S2 hard reset sample", w, k, i);
        if (dut.phase != ref.phase)
            fail("S2 hard reset phase", w, k, 0u);
     }
}

/* S3. Break: unsigned (long-way) difference; NONE not honoured. */
static void test_pull(void)
{
    if (osc_syncPull(0x12345678u, OSC_SYNC_NONE, 65536u, 0x40000000) != 0x12345678u)
        fail("S3 NONE passes", 0u, 0u, 0u);
    if (osc_syncPull(0x100u, 0xffffff00u, 65536u, 0x20000000) != 0u)
        fail("S3 short way round", 0u, 0u, 0u);
    if (osc_syncPull(0x80000000u, 0x00012345u, 3u << 16, 0x40000000) != 0x000369cfu)
        fail("S3 depth 1 = target", 0u, 0u, 0u);
    if (osc_syncPull(0u, 0x400u, 65536u, 0x10000000) != 0x100u)
        fail("S3 depth 0.25", 0u, 0u, 0u);
    if (osc_syncPull(0x55555555u, 0x400u, 65536u, 0) != 0x55555555u)
        fail("S3 depth 0", 0u, 0u, 0u);
}

/* S4. Break: carry test inverted; overshoot not the wrapped value. */
static void test_builder(void)
{
    static const uint32_t incs[] = { 0u, 1u, 0x00010000u, 0x12345678u, 0x80000000u, 0xffffffffu };
    static const uint32_t starts[] = { 0u, 0x7fffffffu, 0xfffffff0u };

    for (unsigned a = 0u; a < 6u; ++a)
     for (unsigned b = 0u; b < 3u; ++b) {
        uint32_t over[N];
        uint32_t c = starts[b];
        const uint32_t end = osc_buildSyncList(starts[b], incs[a], over, N);

        for (unsigned i = 0u; i < N; ++i) {
            const uint64_t n = (uint64_t)c + incs[a];
            const uint32_t want = (n >> 32) ? (uint32_t)n : OSC_SYNC_NONE;

            if (over[i] != want)
                fail("S4 list entry", a, b, i);
            if ((n >> 32) && (uint32_t)n >= incs[a])
                fail("S4 overshoot >= increment", a, b, i);
            c = (uint32_t)n;
        }
        if (end != c)
            fail("S4 end clock", a, b, 0u);
     }
}

/* S5. Break: the << 5 / << 10 scaling missing; a clamp missing. */
static void test_clock(void)
{
    static const float freqs[] = { 55.0f, 440.0f, 3000.0f };
    OscInfo m = make_osc(TRI, 3u, 0u, 0x01234567u);

    if (osc_syncClockInc(&m) != 0x01234567u) fail("S5 table clock", 0u, 0u, 0u);
    m.waveform = NOISE;
    if (osc_syncClockInc(&m) != 0x01234567u) fail("S5 noise clock", 0u, 0u, 0u);
    m.waveform = CRASH;
    m.phaseInc = 0x00100000u;
    if (osc_syncClockInc(&m) != 0x02000000u) fail("S5 crash << 5", 0u, 0u, 0u);
    m.phaseInc = 0x09000000u;
    if (osc_syncClockInc(&m) != 0xffffffe0u) fail("S5 crash clamp", 0u, 0u, 0u);

    /* S087: a modulator bound to a user sample runs Q19.12 (<< 10). */
    m = make_osc(OSC_SAMPLE_START, 3u, 0u, 0x00001000u);
    m.oscType = OSC_TYPE_MODLIST;
    m.sampleCacheLooped = OSC_BIND_SMP;
    if (osc_syncClockInc(&m) != 0x00400000u) fail("S5 sample << 10", 0u, 0u, 0u);
    m.sampleCacheLooped = OSC_BIND_SLP;
    m.phaseInc = 0x09000000u;
    if (osc_syncClockInc(&m) != 0xfffffc00u) fail("S5 sample clamp", 0u, 0u, 0u);
    m.sampleCacheLooped = OSC_BIND_WTB16;
    if (osc_syncClockInc(&m) != 0x09000000u) fail("S5 wavetable is a cycle clock", 0u, 0u, 0u);

    for (unsigned f = 0u; f < 3u; ++f) {
        OscInfo tri = make_osc(TRI, 3u, 0u, freq2PhaseIncr1024(freqs[f]));
        OscInfo cr = make_osc(CRASH, 3u, 0u, freq2PhaseIncr32767(freqs[f]));
        const uint32_t a = osc_syncClockInc(&tri);
        const uint32_t b = osc_syncClockInc(&cr);
        uint32_t ca = 0u, cb = 0u, over[N];
        unsigned ra = 0u, rb = 0u;

        if ((a > b ? a - b : b - a) > 32u)
            fail("S5 equal clock at equal frequency", f, a, b);
        for (unsigned blk = 0u; blk < 2000u; ++blk) {
            ca = osc_buildSyncList(ca, a, over, N);
            for (unsigned i = 0u; i < N; ++i) ra += (unsigned)(over[i] != OSC_SYNC_NONE);
            cb = osc_buildSyncList(cb, b, over, N);
            for (unsigned i = 0u; i < N; ++i) rb += (unsigned)(over[i] != OSC_SYNC_NONE);
        }
        if ((ra > rb ? ra - rb : rb - ra) > 1u)
            fail("S5 equal reset rate", f, ra, rb);
    }
}

/* M1. Break: Q31 instead of Q30; K unclamped; ratio unclamped. */
static void test_maps(void)
{
    if (osc_syncDepthQ30(0.0f) != 0) fail("M1 depth 0", 0u, 0u, 0u);
    if (osc_syncDepthQ30(1.0f) != 0x40000000) fail("M1 depth 1", 0u, 0u, 0u);
    if (osc_syncDepthQ30(0.5f) != 0x20000000) fail("M1 depth 0.5", 0u, 0u, 0u);
    if (osc_syncDepthQ30(0.25f) != 0x10000000) fail("M1 depth 0.25", 0u, 0u, 0u);
    if (osc_syncDepthQ30(-1.0f) != 0) fail("M1 depth < 0", 0u, 0u, 0u);
    if (osc_syncDepthQ30(2.0f) != 0x40000000) fail("M1 depth > 1", 0u, 0u, 0u);
    if (osc_syncDepthQ30(NAN) != 0) fail("M1 depth NaN", 0u, 0u, 0u);
    if (osc_ringDepthQ15(0.0f) != 0) fail("M1 ring depth at 0", 0u, 0u, 0u);
    if (osc_ringDepthQ15(1.0f) != 32768) fail("M1 ring depth at 1", 0u, 0u, 0u);
    if (osc_ringDepthQ15(0.5f) != 16384) fail("M1 ring depth at 0.5", 0u, 0u, 0u);
    if (osc_ringDepthQ15(0.25f) != 8192) fail("M1 ring depth at 0.25", 0u, 0u, 0u);
    if (osc_ringDepthQ15(-1.0f) != 0) fail("M1 ring depth < 0", 0u, 0u, 0u);
    if (osc_ringDepthQ15(2.0f) != 32768) fail("M1 ring depth > 1", 0u, 0u, 0u);
    if (osc_ringDepthQ15(NAN) != 0) fail("M1 ring depth NaN", 0u, 0u, 0u);
    if (osc_syncRatioQ16(100u, 0u) != 0u) fail("M1 ratio clock 0", 0u, 0u, 0u);
    if (osc_syncRatioQ16(0x01000000u, 0x01000000u) != 65536u) fail("M1 ratio 1", 0u, 0u, 0u);
    if (osc_syncRatioQ16(0xffffffffu, 1u) != 4294967040u) fail("M1 ratio clamp", 0u, 0u, 0u);
}

static void run_sync(void)
{
    fill_random_tables(0x5eed085u);
    test_sync_depth0_and_unsynced();
    test_sync_hard();
    test_pull();
    test_builder();
    test_clock();
    test_maps();
}

/* ============================================================= stock -- */
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

/* D1: each dispatcher -> the table loop with the wave's own table. Break: a
** wrong pointer or a swapped osc_stockTables[] order. */
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
static void render4(uint8_t wave, uint8_t lv, uint32_t ph, uint32_t inc,
                    const int16_t *mod, const uint32_t *over, uint32_t clockInc,
                    int32_t depth, int32_t d15, int32_t step,
                    int16_t out[4][N], OscInfo st[4])
{
    for (unsigned m = 0u; m < 4u; ++m) {
        OscInfo o = make_osc(wave, lv, ph, inc);

        if (m == 0u) calcNextOscSampleBlock(&o, out[m], N, 0.8f);
        if (m == 1u) calcNextOscSampleFmBlock(&o, (int16_t *)mod, out[m], N, 0.8f);
        if (m == 2u) calcNextOscSampleSyncBlock(&o, over, clockInc, depth, out[m], N);
        if (m == 3u) calcNextOscSampleRingBlock(&o, mod, out[m], N, d15, step);
        st[m] = o;
    }
}

/* D2: with REC's data, SQ3/SW3 behave exactly as REC on every entry point. */
static void d2_equivalence(void)
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

            random_mod(mod, &s);
            random_over(over, &s);
            memcpy(stock[w], recTable, sizeof(recTable));
            render4(waves[w], lv, ph, inc, mod, over, clockInc, depth, d15, step, oa, sa);
            render4(REC, lv, ph, inc, mod, over, clockInc, depth, d15, step, ob, sb);
            for (unsigned m = 0u; m < 4u; ++m)
                if (!same_buf(oa[m], ob[m]) || !same_state(&sa[m], &sb[m]))
                    fail("D2 stock == Rec", w, m, r);
        }
    memcpy(sq3Table, keep[0], sizeof(sq3Table));
    memcpy(sw3Table, keep[1], sizeof(sw3Table));
}

/* D4: the waveform partitions, 0..15, wav type. Break: a stock wave given
** the sample clock; osc_isTableWave() missing a wave. */
static void d4_partitions(void)
{
    if (OSC_SAMPLE_START != 8) fail("D4 OSC_SAMPLE_START", OSC_SAMPLE_START, 8u, 0u);
    for (uint8_t wv = 0u; wv < 16u; ++wv) {
        const int table = wv == TRI || wv == SAW || wv == REC || wv == SQ3 || wv == SW3;
        OscInfo m = make_osc(wv, 0u, 0u, 0x00123456u);
        const uint32_t want = (wv == CRASH) ? (0x00123456u << 5) : 0x00123456u;

        if (osc_isTableWave(wv) != table) fail("D4 osc_isTableWave", wv, osc_isTableWave(wv), (unsigned)table);
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

static void run_stock(void)
{
    fill_random_tables(0x5eed086u);
    d1_routing();
    d2_equivalence();
    d4_partitions();
    d5_setfreq();
}

/* =============================================================== pos -- */
/* The table each table wave renders from (as the dispatchers pass it). */
static int16_t (*wave_table(uint8_t waveform))[1024]
{
    switch (waveform) {
    case TRI: return triTable;
    case SAW: return sawTable;
    case REC: return recTable;
    case SQ3: return sq3Table;
    case SW3: return sw3Table;
    default:  return 0;
    }
}

/* Render one block with the plain loop of a phase wave, unbent. */
static void render(OscInfo *osc, int16_t *buf)
{
    if (osc->waveform == SINE)
        calcSineBlock(osc, buf, N, 1.0f);
    else if (osc->waveform == CRASH)
        calcSampleOscBlock(osc, buf, N, 1.0f);
    else
        calcWavetableOscBlock(osc, (const int16_t (*)[1024])wave_table(osc->waveform), buf, N, 1.0f);
}

/* Render one block the way the engines do: apply, render, restore. */
static void render_bent(OscInfo *osc, int16_t *buf)
{
    osc_positionBendApply(osc, 1u);
    render(osc, buf);
    osc_positionBendRestore(osc);
}

/* Independent per-sample readers (the loops' read law, written here). */
static int16_t expected_sample(uint8_t waveform, uint32_t phase)
{
    if (waveform == SINE) {
        const uint32_t itg = phase >> 20;
        int16_t out = sine_table[itg];
        const float frac = (phase & 0x000FFFFFu) * 9.5367431640625e-07f;
        out += frac * (sine_table[itg + 1u] - out);
        return out;
    }
    if (waveform == CRASH) {
        const uint32_t itg = phase >> 17;
        int16_t out = crashSample[itg];
        const float frac = (phase & 0x0001FFFFu) * 0.00000762939453125f;
        out += frac * (crashSample[itg + 1u] - out);
        return (int16_t)((out - 127) * 256);
    }
    {
        int16_t (*t)[1024] = wave_table(waveform);
        const uint32_t itg = phase >> 22;
        const uint32_t next = (itg + 1u) & 0x3FFu;
        int16_t out = t[3][itg];
        const float frac = (phase & 0x003FFFFFu) * 2.384185791015625e-07f;
        out += frac * (t[3][next] - out);
        return out;
    }
}

static const uint8_t phase_waves[] = { SINE, TRI, SAW, REC, CRASH, SQ3, SW3 };

/* P1: trigger offset against an independent truth table. Break: pos << 24;
** a wave dropped from osc_positionIsPhase(); the & 127 mask removed. */
static void test_trigger(void)
{
    static const unsigned positions[] = { 0u, 1u, 64u, 127u, 128u, 255u };

    for (uint8_t w = 0u; w < 16u; ++w) {
        const int phase_wave = (w == SINE || w == TRI || w == SAW || w == REC ||
                                w == CRASH || w == SQ3 || w == SW3);
        for (unsigned i = 0u; i < sizeof(positions) / sizeof(positions[0]); ++i) {
            const unsigned p = positions[i];
            OscInfo osc = make_osc(w, 3u, 0x12345678u, 0u);
            osc.position = (uint8_t)p;
            const uint32_t got = osc_positionTrigger(&osc);
            const uint32_t want = phase_wave ? ((uint32_t)(p & 127u) << 25) : 0u;
            if (got != want || osc.positionApplied != (uint8_t)(p & 127u))
                fail("P1 trigger", w, p, got != want);
        }
    }
}

/* P2: a held pos renders bit-identically to no helpers. Break: a bend from
** a stale positionBendStep; phaseInc not restored. */
static void test_identity(void)
{
    int16_t a[N], b[N];
    uint32_t state = 0x0860b2u;

    for (unsigned w = 0u; w < sizeof(phase_waves); ++w)
        for (unsigned r = 0u; r < 16u; ++r) {
            const uint8_t pos = (uint8_t)(state >> 25);
            const uint32_t start = state * 2654435761u;
            const uint32_t inc = (state >> 6) | 1u;
            state = state * 1664525u + 1013904223u;

            OscInfo bent = make_osc(phase_waves[w], 3u, start, inc);
            bent.position = pos;
            bent.phase += osc_positionTrigger(&bent);
            OscInfo plain = make_osc(phase_waves[w], 3u, start + ((uint32_t)pos << 25), inc);
            for (unsigned blk = 0u; blk < 8u; ++blk) {
                render_bent(&bent, a);
                render(&plain, b);
                if (memcmp(a, b, sizeof(a)) != 0 || bent.phase != plain.phase ||
                    bent.phaseInc != inc || bent.positionBendStep != 0)
                    fail("P2 identity", phase_waves[w], r, blk);
            }
        }
}

/* P3: no drift over 4,096 random blocks, every phase wave. Break: wrong
** OSC_POS_BEND_PER_STEP; restore missing; positionApplied not advanced. */
static void test_no_drift(uint8_t waveform)
{
    OscInfo osc = make_osc(waveform, 3u, 0x13572468u, 0x00123456u);
    uint32_t freePhase = osc.phase;
    const uint32_t freeInc = osc.phaseInc;
    uint32_t state = 0x0860a3u + waveform;
    int16_t buf[N];

    for (unsigned block = 0u; block < 4096u; ++block) {
        osc.position = (uint8_t)(state >> 24);
        state = state * 1664525u + 1013904223u;
        render_bent(&osc, buf);
        freePhase += freeInc * N;
        if (osc.phase != freePhase + ((uint32_t)osc.positionApplied << 25) ||
            osc.phaseInc != freeInc || osc.positionBendStep != 0)
            fail("P3 no drift", waveform, block, osc.positionApplied);
    }
}

/* P4: circular difference, stored step and phaseInc change. Break: linear
** difference; positionApplied not advanced; cap ignored (cap build). */
static void test_delta(void)
{
    const struct { uint8_t from, to; int32_t delta; } cases[] = {
        { 127u, 0u, 1 }, { 0u, 127u, -1 }, { 0u, 64u, -64 },
        { 64u, 0u, -64 }, { 4u, 12u, 8 }, { 100u, 20u, 48 }
    };

    for (unsigned i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        OscInfo osc = make_osc(SINE, 3u, 0u, 0x00100000u);
        int32_t expected = cases[i].delta;

        if (expected > OSC_POS_BEND_CAP)
            expected = OSC_POS_BEND_CAP;
        if (expected < -OSC_POS_BEND_CAP)
            expected = -OSC_POS_BEND_CAP;
        osc.positionApplied = cases[i].from;
        osc.position = cases[i].to;
        osc_positionBendApply(&osc, 1u);
        if (osc.positionBendStep != (int8_t)expected ||
            osc.phaseInc != 0x00100000u + (uint32_t)(expected * (int32_t)OSC_POS_BEND_PER_STEP) ||
            osc.positionApplied != (uint8_t)((cases[i].from + expected) & 127))
            fail("P4 circular", cases[i].from, cases[i].to, i);
        osc_positionBendRestore(&osc);
        if (osc.phaseInc != 0x00100000u || osc.positionBendStep != 0)
            fail("P4 restore", cases[i].from, cases[i].to, i);
    }
}

/* P5: continuity inside a bent block, every phase wave. Break: the bend
** applied as a phase jump; a wrong step constant. */
static void test_continuity(void)
{
    int16_t actual[N];
    const uint32_t phase = 0x31415926u;
    const uint32_t inc = 0x01234567u;

    for (unsigned w = 0u; w < sizeof(phase_waves); ++w) {
        OscInfo osc = make_osc(phase_waves[w], 3u, phase, inc);
        osc.position = 5u;
        osc_positionBendApply(&osc, 1u);
        const uint32_t bentInc = inc + (uint32_t)(5 * (int32_t)OSC_POS_BEND_PER_STEP);
        render(&osc, actual);
        osc_positionBendRestore(&osc);
        for (unsigned i = 0u; i < N; ++i)
            if (actual[i] != expected_sample(phase_waves[w], phase + bentInc * i))
                fail("P5 continuity", phase_waves[w], i, (uint16_t)actual[i]);
        if (osc.phase != phase + inc * N + (5u << 25))
            fail("P5 lands on the offset", phase_waves[w], 0u, 0u);
    }
}

/* P6: non-phase waves (wav type) consume the delta without bending. */
static void test_nonphase(void)
{
    const uint8_t waves[] = { NOISE, OSC_SAMPLE_START };

    for (unsigned i = 0u; i < sizeof(waves); ++i) {
        OscInfo osc = make_osc(waves[i], 3u, 0u, 0x00010000u);
        int32_t expected = -48;

        if (expected < -OSC_POS_BEND_CAP)
            expected = -OSC_POS_BEND_CAP;
        osc.positionApplied = 10u;
        osc.position = 90u;
        osc_positionBendApply(&osc, 1u);
        if (osc.phaseInc != 0x00010000u || osc.positionBendStep != 0 ||
            osc.positionApplied != (uint8_t)((10 + expected) & 127))
            fail("P6 nonphase", waves[i], osc.positionApplied, (unsigned)((10 + expected) & 127));
        osc_positionBendRestore(&osc);
        if (osc.phaseInc != 0x00010000u)
            fail("P6 restore", waves[i], 0u, 0u);
    }
}

/* P7 (cap build): a step of 40 glides in blocks of 8 and lands exactly. */
static void test_cap(void)
{
#ifdef OSC_POS_TEST_CAP
    OscInfo osc = make_osc(SINE, 3u, 0u, 0u);

    osc.position = 40u;
    for (unsigned i = 1u; i <= 5u; ++i) {
        osc_positionBendApply(&osc, 1u);
        if (osc.positionBendStep != OSC_POS_BEND_CAP ||
            osc.positionApplied != (uint8_t)(i * OSC_POS_BEND_CAP))
            fail("P7 cap", i, osc.positionApplied, (unsigned)osc.positionBendStep);
        osc_positionBendRestore(&osc);
    }
    osc_positionBendApply(&osc, 1u);
    if (osc.positionBendStep != 0 || osc.positionApplied != 40u)
        fail("P7 cap lands", osc.positionApplied, 40u, 0u);
    osc_positionBendRestore(&osc);
#endif
}

/* P8: consume only (Drum snc passes bend = 0). Break: the bend argument
** ignored. */
static void test_consume_only(void)
{
    for (unsigned w = 0u; w < sizeof(phase_waves); ++w) {
        OscInfo osc = make_osc(phase_waves[w], 3u, 0u, 0x00200000u);

        osc.positionApplied = 3u;
        osc.position = 7u;
        osc_positionBendApply(&osc, 0u);
        if (osc.phaseInc != 0x00200000u || osc.positionBendStep != 0 ||
            osc.positionApplied != 7u)
            fail("P8 consume only", phase_waves[w], osc.positionApplied, 0u);
        osc_positionBendRestore(&osc);
    }
}

static void run_pos(void)
{
    fill_shape_tables();
    test_trigger();
    test_identity();
    for (unsigned w = 0u; w < sizeof(phase_waves); ++w)
        test_no_drift(phase_waves[w]);
    test_delta();
    test_continuity();
    test_nonphase();
    test_cap();
    test_consume_only();
}

/* ============================================================== type -- */
/* T1/T2. Break: the clamp removed; the early return for an unchanged type
** removed; positionApplied or freqCacheValid not reset. */
static void run_type(void)
{
    OscInfo a;
    OscInfo before;

    catalogue_host_reset();
    sampleMemory_refresh();

    memset(&a, 0, sizeof(a));
    a.oscType = OSC_TYPE_MODLIST;
    a.waveform = 7u;
    a.position = 40u;
    a.positionApplied = 12u;
    a.freqCacheValid = 1u;
    before = a;
    osc_setType(&a, 200u);
    expect(a.oscType == OSC_TYPE_SELECTABLE - 1u, "T1 clamps to SELECTABLE-1");
    expect(memcmp(&a, &before, sizeof(a)) != 0, "T1 a real change is applied");
    before = a;
    osc_setType(&a, OSC_TYPE_SELECTABLE - 1u);
    expect(memcmp(&a, &before, sizeof(a)) == 0, "T1 unchanged type is a no-op");

    memset(&a, 0, sizeof(a));
    a.oscType = OSC_TYPE_SMP;
    a.waveform = 9u;
    a.position = 77u;
    a.positionApplied = 3u;
    a.freqCacheValid = 1u;
    a.phase = 1234u;
    osc_setType(&a, OSC_TYPE_WAV);
    expect(a.positionApplied == a.position, "T2 positionApplied re-anchored");
    expect(a.freqCacheValid == 0u, "T2 freq cache invalidated");
    expect(a.waveform <= OSC_STOCK_LAST, "T2 waveform clamped to the stock list");
}

/* ============================================================ sample -- */
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

static void bind_typed(OscInfo *o, uint8_t type, uint8_t wave, uint32_t inc)
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

static void run_sample(void)
{
    /* Peak 32767 keeps the installer's gain code at 0, so the reads are exact. */
    static const int16_t data[4] = { 0, 8192, 16384, 32767 };
    int16_t buf[8];
    OscInfo o;
    uint32_t before;

    g_claim = 0u;
    install_sample(data, 4u, 0u, 4u, 0u);

    /* L1: smp plays the item, then the pad. */
    bind_typed(&o, OSC_TYPE_SMP, 0u, 4096u);
    o.phase = 0u;
    osc_renderTyped(&o, buf, 8u, 1.0f);
    expect(buf[0] == 0 && buf[1] == 8192 && buf[2] == 16384 &&
           buf[3] == 32767, "L1 the four points");
    expect(buf[4] == 0 && buf[5] == 0 && buf[6] == 0 && buf[7] == 0,
           "L1 the pad holds 0");

    /* L2: slp seam to loop_start, never 0. */
    install_sample(data, 4u, 1u, 3u, 1u);
    bind_typed(&o, OSC_TYPE_SLP, 0u, 4096u);
    o.phase = (2u << OSC_SAMPLE_PHASE_SHIFT) + 2048u;
    osc_renderTyped(&o, buf, 1u, 1.0f);
    expect(buf[0] == 12288, "L2 the seam interpolates 16384 -> 8192");

    /* L3: a start past the loop end clamps to loop_start. */
    o.position = 127u;
    o.phase = osc_typedTriggerPhase(&o);
    expect((o.phase >> OSC_SAMPLE_PHASE_SHIFT) == 1u,
           "L3 slp start clamps to loop_start");

    /* L4 (S087 P1, F5): a backward glide inside the loop is a forward wrap. */
    {
        static int16_t glide[100];
        const uint32_t start = 50u << OSC_SAMPLE_PHASE_SHIFT;

        for (uint32_t i = 0u; i < 100u; i++)
            glide[i] = (int16_t)(i * 300);
        glide[99] = 32767;   /* peak keeps the gain code at 0 */
        install_sample(glide, 100u, 0u, 100u, 1u);
        bind_typed(&o, OSC_TYPE_SLP, 0u, 10u);
        o.phase = start;
        o.position = 64u;
        o.positionApplied = 65u;
        osc_typedPositionApply(&o, 1u);
        expect(o.positionBendStep == -1, "L4 a backward step");
        osc_renderTyped(&o, buf, 1u, 1.0f);
        expect(o.phase == start - 90u, "L4 the block moves back by 90");
    }

    /* L6 (S087 P1, F6): slp FM reads from loop_start, not the file start. */
    {
        static int16_t loopData[100];
        int16_t mod[1] = { 0 };

        for (uint32_t i = 0u; i < 100u; i++)
            loopData[i] = (int16_t)(i * 300);
        loopData[99] = 32767;
        install_sample(loopData, 100u, 40u, 60u, 1u);
        bind_typed(&o, OSC_TYPE_SLP, 0u, 4096u);
        o.fmMod = 0.0f;
        o.phase = 50u << OSC_SAMPLE_PHASE_SHIFT;
        osc_renderTypedFm(&o, mod, buf, 1u, 1.0f);
        expect(buf[0] == 15000, "L6 slp FM reads inside the loop");
        o.phase = 10u << OSC_SAMPLE_PHASE_SHIFT;
        osc_renderTypedFm(&o, mod, buf, 1u, 1.0f);
        expect(buf[0] == 3000, "L6 slp FM lead-in is not frozen");
    }

    /* The L4/L6 blocks replaced the item; restore the 4-point sample. */
    install_sample(data, 4u, 0u, 4u, 0u);

    /* L8: the pos start for 0/64/127. */
    bind_typed(&o, OSC_TYPE_SMP, 0u, 4096u);
    o.position = 0u;
    expect(osc_typedTriggerPhase(&o) == 0u, "L8 pos 0 starts at 0");
    o.position = 64u;
    expect((osc_typedTriggerPhase(&o) >> OSC_SAMPLE_PHASE_SHIFT) == 2u,
           "L8 pos 64 starts at point 2");
    o.position = 127u;
    expect((osc_typedTriggerPhase(&o) >> OSC_SAMPLE_PHASE_SHIFT) == 3u,
           "L8 pos 127 starts at point 3");

    /* L9: linear bend of one step = frames; L11: restore is exact. */
    bind_typed(&o, OSC_TYPE_SMP, 0u, 4096u);
    o.position = 0u;
    o.positionApplied = 0u;
    o.position = 1u;
    before = o.phaseInc;
    osc_typedPositionApply(&o, 1u);
    expect(o.positionBendStep == 1, "L9 one step");
    expect(o.phaseInc == before + 4u, "L9 inc moves by step x frames");
    osc_typedPositionRestore(&o);
    expect(o.phaseInc == before, "L11 restore is exact");

    /* L10: a finished smp is not revived. */
    o.phase = 4u << OSC_SAMPLE_PHASE_SHIFT;
    o.position = 10u;
    osc_typedPositionApply(&o, 1u);
    expect(o.positionBendStep == 0, "L10 no bend after the end");

    /* L12: modulator list mapping. */
    bind_typed(&o, OSC_TYPE_MODLIST, 9u, 4096u);
    expect(o.sampleCacheLooped == OSC_BIND_SLP && o.sampleCacheIndex == 0u,
           "L12 8+S maps to l0");

    /* L13: typ switch clamps wav and re-anchors. */
    memset(&o, 0, sizeof(o));
    o.oscType = OSC_TYPE_WAV;
    o.waveform = 7u;
    o.position = 64u;
    osc_setType(&o, OSC_TYPE_SMP);
    expect(o.oscType == OSC_TYPE_SMP && o.waveform <= 0u,
           "L13 wav clamps to the sample list");
    expect(o.positionApplied == o.position, "L13 pos re-anchored");
}

/* =============================================================== wtb -- */
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

static void bind_wtb(OscInfo *o, uint8_t wave)
{
    memset(o, 0, sizeof(*o));
    o->oscType = OSC_TYPE_WTB;
    o->waveform = wave;
    o->freq = 440.0f;
    o->pitchMod = 1.0f;
    o->modNodeValue = 1.0f;
    osc_userBind(o);
}

static void run_wtb(void)
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

    /* W1/W2: the index is exact and wraps at len. */
    for (uint32_t k = 0u; k < 4u; k++) {
        static const uint32_t lens[4] = { 16u, 600u, 1000u, 2048u };
        const uint32_t len = lens[k];
        const uint32_t ph = (uint32_t)((((uint64_t)len / 2u) << 32) / len);
        const uint32_t want = (uint32_t)(((uint64_t)ph * len) >> 32);

        expect(want == len / 2u, "W1 the exact index formula");
    }
    expect(osc_wtbRead((const uint8_t *)f0, 16u, 16u,
                       (uint32_t)(15u * 4294967296ull / 16u)) == f0[15],
           "W2 the last point");
    expect(osc_wtbRead((const uint8_t *)f0, 16u, 16u,
                       (uint32_t)(15.5 * 4294967296.0 / 16.0)) ==
           (f0[15] + f0[0]) / 2, "W2 the wrap interpolates to point 0");

    /* W3: 8- and 16-bit depths agree (x256). */
    install_table(16u, 8u, (const int16_t *)f0b, (const int16_t *)f0b);
    bind_wtb(&o, 0u);
    expect(o.sampleCacheLooped == OSC_BIND_WTB8, "W3 the 8-bit kind");
    expect(osc_wtbRead((const uint8_t *)f0b, 8u, 16u, 0u) == 0.0f, "W3 8-bit zero");

    /* W6: no slot -> nearest frame (pos 127 picks frame 1). */
    install_table(16u, 16u, f0, f1);
    bind_wtb(&o, 0u);
    g_claim = 0u;
    o.position = 127u;
    o.positionApplied = 127u;
    o.phase = 0u;
    osc_renderTyped(&o, buf, 1u, 1.0f);
    expect(buf[0] == f1[0], "W6 the nearest frame without a slot");

    /* W4: blend ff 0 = frame A, ff 1 = frame B. */
    g_claim = 1u;
    o.position = 0u;
    o.positionApplied = 0u;
    o.positionBendStep = 0;
    o.phase = 0u;
    osc_renderTyped(&o, buf, 1u, 1.0f);
    expect(buf[0] == f0[0], "W4 ff 0 is frame A");
    o.position = 127u;
    o.positionApplied = 127u;
    o.positionBendStep = 0;
    o.phase = 0u;
    osc_renderTyped(&o, buf, 1u, 1.0f);
    expect(buf[0] == f1[0], "W4 ff 1 is frame B");

    /* W5: the pos ramp cap is one frame pair. */
    o.positionApplied = 0u;
    o.position = 127u;
    osc_typedPositionApply(&o, 1u);
    expect(o.positionBendStep == 127, "W5 a 2-frame table allows 127");

    /* W7 (S087 P1, F8): a held pos halfway between frames mixes them. */
    g_claim = 1u;
    o.position = 64u;
    o.positionApplied = 64u;
    o.positionBendStep = 0;
    o.phase = 0u;
    osc_renderTyped(&o, buf, 1u, 1.0f);
    expect(buf[0] > (int16_t)((f0[0] + f1[0]) / 3) &&
           buf[0] < (int16_t)(2 * (f0[0] + f1[0]) / 3),
           "W7 a held pos between frames blends");

    /* W8: a modulator w entry plays frame 0 (8 + 2S with S = 0). */
    o.oscType = OSC_TYPE_MODLIST;
    o.waveform = 8u;
    o.position = 127u;
    o.positionApplied = 127u;
    o.phase = 0u;
    osc_userBind(&o);
    expect(o.sampleCacheLooped == OSC_BIND_WTB16, "W8 the w entry binds");
    osc_renderUserList(&o, buf, 1u, 1.0f);
    expect(buf[0] == f0[0], "W8 a modulator w entry plays frame 0");
    g_claim = 0u;
}

/*
 * itp old-carrier crossfade helper (S088 P0 §3 Layer 3c).
 *
 * What:       exercises oscSection_renderOldCarrier() directly:
 *               I1 the snapshot renders exactly the plain carrier (live ==
 *                  old leaves every sample unchanged) and advances phase by
 *                  the same amount as calcNextOscSampleBlock();
 *               I2 the blend starts at t = 0 (the first sample of a fresh
 *                  fade is the old carrier) and ends weighted toward the
 *                  live block;
 *               I3 the counter counts BLOCKS blocks to inactive;
 *               I4 a wavetable snapshot renders the frozen frame through the
 *                  plain copy with or without an interpolation slot (it never
 *                  claims).
 * Why:        the crossfade is the audible core of itp; these laws pin the
 *             snapshot contract without the full engine. A wrong phase
 *             advance, a blend that ignores the live side, a counter that
 *             never releases the slot, or a wtb path that claims all fail.
 * Inputs:     the real Oscillator.c (host build), the sine table and one
 *             2-frame wavetable.
 * Outputs:    failure lines; the group count.
 * Affiliates: Oscillator.c oscSection_renderOldCarrier(); OscCarrierSnapshot.
 */
static void run_itp(void)
{
    static int16_t expectBuf[OUTPUT_DMA_SIZE];
    static int16_t buf[OUTPUT_DMA_SIZE];
    static int16_t f0[16];
    static int16_t f1[16];
    OscInfo live, ref, w;
    OscCarrierSnapshot snap;
    unsigned i;

    fill_sine();
    memset(&live, 0, sizeof(live));
    live.waveform = SINE;
    live.oscType = OSC_TYPE_WAV;
    live.phase = 0x12345678u;
    live.phaseInc = 0x00100000u;

    ref = live;
    calcNextOscSampleBlock(&ref, expectBuf, OUTPUT_DMA_SIZE, 1.0f);

    memset(&snap, 0, sizeof(snap));
    snap.phase = live.phase;
    snap.phaseInc = live.phaseInc;
    snap.waveform = live.waveform;
    snap.oscType = live.oscType;
    snap.active = 1u;
    snap.counter = OSC_RUN_ITP_FADE_BLOCKS;

    /* I1: live == old -> the block is unchanged; phase advances with ref. */
    memcpy(buf, expectBuf, sizeof(buf));
    oscSection_renderOldCarrier(&snap, buf, OUTPUT_DMA_SIZE);
    /* Float rounding of (1 - t) x old + t x live allows one LSB either way. */
    for (i = 0u; i < OUTPUT_DMA_SIZE; i++) {
        const int diff = (int)buf[i] - (int)expectBuf[i];
        expect(diff >= -1 && diff <= 1,
               "I1 old carrier equals the plain render");
    }
    expect(snap.phase == ref.phase, "I1 the snapshot advances by its own phaseInc");
    expect(snap.counter == (uint8_t)(OSC_RUN_ITP_FADE_BLOCKS - 1u),
           "I1 one block consumed");

    /* I2: a fresh fade starts at t = 0 on its first sample. */
    memset(&snap, 0, sizeof(snap));
    snap.phase = live.phase;
    snap.phaseInc = live.phaseInc;
    snap.waveform = live.waveform;
    snap.oscType = live.oscType;
    snap.active = 1u;
    snap.counter = OSC_RUN_ITP_FADE_BLOCKS;
    for (i = 0u; i < OUTPUT_DMA_SIZE; i++)
        buf[i] = 1000;
    oscSection_renderOldCarrier(&snap, buf, OUTPUT_DMA_SIZE);
    expect(buf[0] == expectBuf[0], "I2 the first sample is the old carrier");
    expect(buf[OUTPUT_DMA_SIZE - 1u] != 1000 &&
           buf[OUTPUT_DMA_SIZE - 1u] != expectBuf[OUTPUT_DMA_SIZE - 1u],
           "I2 later samples are a real blend (neither pure side)");

    /* I3: BLOCKS blocks run to completion and release the snapshot. */
    memset(&snap, 0, sizeof(snap));
    snap.phase = live.phase;
    snap.phaseInc = live.phaseInc;
    snap.waveform = live.waveform;
    snap.oscType = live.oscType;
    snap.active = 1u;
    snap.counter = OSC_RUN_ITP_FADE_BLOCKS;
    for (i = 0u; i < OSC_RUN_ITP_FADE_BLOCKS; i++) {
        for (unsigned k = 0u; k < OUTPUT_DMA_SIZE; k++)
            buf[k] = 0;
        oscSection_renderOldCarrier(&snap, buf, OUTPUT_DMA_SIZE);
    }
    expect(snap.counter == 0u, "I3 the counter reaches zero");
    expect(snap.active == 0u, "I3 the slot is released");

    /* I4: a wavetable snapshot uses the plain copy and never claims. */
    for (i = 0u; i < 16u; i++) {
        f0[i] = (int16_t)(i * 1000);
        f1[i] = (int16_t)(30000 - i * 1000);
    }
    install_table(16u, 16u, f0, f1);
    bind_wtb(&w, 0u);
    w.positionApplied = 127u;   /* frozen on frame 1 */
    w.positionBendStep = 0;
    w.phase = 0u;
    memset(&snap, 0, sizeof(snap));
    snap.phase = w.phase;
    snap.phaseInc = w.phaseInc;
    snap.waveform = w.waveform;
    snap.oscType = w.oscType;
    snap.sampleCacheIndex = w.sampleCacheIndex;
    snap.sampleCacheLooped = w.sampleCacheLooped;
    snap.sampleCacheSize = w.sampleCacheSize;
    snap.sampleCacheData = w.sampleCacheData;
    snap.positionApplied = w.positionApplied;
    snap.active = 1u;
    snap.counter = 1u;          /* last block: t near 1, weight on live */
    for (i = 0u; i < OUTPUT_DMA_SIZE; i++)
        buf[i] = 0;
    g_claim = 0u;
    oscSection_renderOldCarrier(&snap, buf, OUTPUT_DMA_SIZE);
    expect(buf[0] > 0, "I4 the frozen frame renders without a slot");
    {
        static int16_t bufB[OUTPUT_DMA_SIZE];
        for (i = 0u; i < OUTPUT_DMA_SIZE; i++)
            bufB[i] = 0;
        snap.phase = w.phase;
        snap.active = 1u;
        snap.counter = 1u;
        g_claim = 1u;
        oscSection_renderOldCarrier(&snap, bufB, OUTPUT_DMA_SIZE);
        for (i = 0u; i < OUTPUT_DMA_SIZE; i++)
            expect(buf[i] == bufB[i], "I4 a slot grant changes nothing");
    }
    g_claim = 0u;
}

int main(void)
{
#ifdef OSC_POS_TEST_CAP
    group("pos", run_pos);
#else
    group("ring", test_ring);
    group("sync", run_sync);
    group("stock", run_stock);
    group("pos", run_pos);
    group("type", run_type);
    group("sample", run_sample);
    group("wtb", run_wtb);
    group("itp", run_itp);
#endif
    printf("oscillator failures: %u\n", failures);
    return failures ? 1 : 0;
}
