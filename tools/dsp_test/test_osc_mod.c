/*
 * Drum snc / rng oscillator support (S085 P1 host gate).
 *
 * What:       runs the new Oscillator.c S085 P1 functions (extracted from the
 *             current source) against the unchanged plain loops (extracted
 *             from the same file):
 *               R1 ring copies == plain render at gain 1 + the exact integer
 *                  ring law, every carrier family, interpolation, amt 0..1;
 *                  amt 0 == the carrier exactly;
 *               S1 sync at depth 0 == the plain render, resets present;
 *               S2 sync at depth 1: a reset after sample k lands on k + 1 at
 *                  overshoot x ratio (K4, Q4);
 *               S3 osc_syncPull(): the signed (short) pull; NONE passes;
 *               S4 osc_buildSyncList() against a 64-bit reference;
 *               S5 osc_syncClockInc(): << 5 for crash/user samples, the
 *                  clamp, equal reset rates at equal frequency;
 *               S6 unsynced slaves (noise, user sample) == the plain render;
 *               S7 the interpolated slave: partners share the list; the
 *                  phase rule;
 *               M1 osc_syncDepthQ30() / osc_ringK() / osc_syncRatioQ16().
 * Why:        S085 P1 §7.1: the new code must be exact where it should
 *             equal existing code, and its laws must hold where it is new.
 *             Each case names the code break that must make it fail.
 * Inputs:     generated fragments (Makefile osc_mod).
 * Outputs:    one line per failure (first 40); "osc_mod failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test osc_mod.
 * Affiliates: Oscillator.c/.h S085 P1 sections; DSP_TEST.md §5.
 */
#include <stdint.h>
#include <string.h>
#include <math.h>

#define REAL_FS ((float)44108.07291666667f)
#define OSC_S085_LOOP
#define OSC_S085_SECTION
#define OSC_S085_DISPATCH
#define INTERPOLATE_OSC 1
#define INTERPOLATE_FM_OSC 1
#define OSC_SYNC_NONE 0xFFFFFFFFu   /* must equal Oscillator.h (S085 P1) */

#include "om_defs.inc"
#include "om_sampleinfo.inc"
#include "om_oscinfo.inc"

/* Host data in place of the flash tables (old and new read the same data). */
int16_t sine_table[4097];
int16_t sawTable[11][1024];
int16_t triTable[11][1024];
int16_t recTable[11][1024];
unsigned char crashSample[32769];       /* +1: the plain loop reads [32768] */
static int16_t host_sample[4097];       /* 4,096 points + the zero pad word */
static uint32_t host_rng = 0x0850001u;
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
    return info;   /* never reached: tests pre-fill the sample cache */
}

void calcNextOscSampleBlock(OscInfo* osc, int16_t* buf, const uint8_t size,
                            const float gain);

#include "om_scratch.inc"
#include "om_plain.c"
#include "om_map.c"
#include "om_s085.c"

#define N OUTPUT_DMA_SIZE
static unsigned failures;

static void fail(const char *what, unsigned a, unsigned b, unsigned c)
{
    failures++;
    if (failures <= 40u)
        printf("FAIL %s (%u, %u, %u)\n", what, a, b, c);
}

static void fill_tables(void)
{
    uint32_t s = 0x5eed085u;

    for (unsigned i = 0u; i < 4097u; ++i)
        sine_table[i] = (int16_t)lrint(32767.0 * sin(2.0 * M_PI * (double)i / 4096.0));
    for (unsigned o = 0u; o < 11u; ++o)
        for (unsigned i = 0u; i < 1024u; ++i) {
            sawTable[o][i] = (int16_t)(golden_rand(&s) >> 16);
            triTable[o][i] = (int16_t)(golden_rand(&s) >> 16);
            recTable[o][i] = (int16_t)(golden_rand(&s) >> 16);
        }
    for (unsigned i = 0u; i < 32769u; ++i)
        crashSample[i] = (unsigned char)golden_rand(&s);
    for (unsigned i = 0u; i < 4096u; ++i)
        host_sample[i] = (int16_t)(golden_rand(&s) >> 16);
    host_sample[4096] = 0;
}

static OscInfo make_osc(uint8_t wave, uint32_t phase, uint32_t inc, uint8_t looped)
{
    OscInfo o;

    memset(&o, 0, sizeof(o));
    o.waveform = wave;
    o.waveInterpNext = wave;
    o.phase = phase;
    o.phaseInc = inc;
    o.tableOffset = 3u;
    if (wave >= OSC_SAMPLE_START) {   /* cache hit: no SampleMemory lookup */
        o.sampleCacheData = host_sample;
        o.sampleCacheSize = 4096u;
        o.sampleCacheIndex = (uint8_t)(wave - OSC_SAMPLE_START);
        o.sampleCacheGeneration = 1u;
        o.sampleCacheLooped = looped;
    }
    return o;
}

static void set_interp(OscInfo *o, uint8_t next, float frac)
{
    o->waveInterpNext = next;
    o->waveInterpFrac = frac;
    o->waveInterpGeneration = 7u;
}

static int16_t ring_ref(int16_t car, int16_t mod, int32_t k)
{
    const int32_t v = ((int32_t)car * (k + (int32_t)mod)) >> 15;
    return (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}

/* R1. Break: >> 14 in osc_ringSample(); K taken from amt instead of 1 - amt. */
static void test_ring(void)
{
    static const uint8_t waves[] = { SINE, TRI, SAW, REC, NOISE, CRASH,
                                     OSC_SAMPLE_START, OSC_SAMPLE_START };
    static const float amts[] = { 0.0f, 1.0f / 127.0f, 0.5f, 126.0f / 127.0f, 1.0f };
    static const uint32_t phases[] = { 0x00000000u, 0x7fff1234u, 0xfffff000u };
    static const uint32_t incs[] = { 0x00800000u, 0x07000000u, 0x30000000u };

    for (unsigned w = 0u; w < 8u; ++w)
     for (unsigned a = 0u; a < 5u; ++a)
      for (unsigned p = 0u; p < 3u; ++p)
       for (unsigned interp = 0u; interp < 2u; ++interp) {
        const uint8_t isSample = (uint8_t)(waves[w] >= OSC_SAMPLE_START);
        const uint32_t ph = isSample ? (p == 2u ? (4090u << 17) : ((100u + 997u * p) << 17))
                                     : phases[p];
        const uint32_t inc = isSample ? ((p + 1u) << 17) : incs[p];
        OscInfo ref = make_osc(waves[w], ph, inc, (uint8_t)(w == 7u));
        OscInfo dut;
        int16_t mod[N], car[N], out[N];
        const int32_t k = osc_ringK(amts[a]);
        uint32_t rng;

        if (interp && waves[w] >= CRASH)
            continue;
        if (interp)
            set_interp(&ref, (uint8_t)(waves[w] + 1u), 0.3f);
        host_interp_on = (uint8_t)interp;
        dut = ref;
        for (unsigned i = 0u; i < N; ++i)
            mod[i] = (int16_t)(golden_rand(&host_rng) >> 16);
        mod[0] = -32768;
        mod[1] = 32767;
        if (amts[a] == 0.0f)       /* amt 0: the modulator block (gain amt) is 0 */
            memset(mod, 0, sizeof(mod));
        rng = host_rng;
        calcNextOscSampleBlock(&ref, car, N, 1.0f);
        host_rng = rng;
        calcNextOscSampleRingBlock(&dut, mod, out, N, k);
        for (unsigned i = 0u; i < N; ++i) {
            if (out[i] != ring_ref(car[i], mod[i], k))
                fail("R1 ring law", w, a, i);
            if (amts[a] == 0.0f && out[i] != car[i])
                fail("R1 amt 0 is the carrier", w, p, i);
        }
        if (dut.phase != ref.phase || dut.output != ref.output)
            fail("R1 oscillator state", w, a, p);
       }
    host_interp_on = 0u;
}

/* S1 and S6. Break: the pull applied regardless of depth (or of NONE). */
static void test_sync_depth0_and_unsynced(void)
{
    static const uint8_t waves[] = { SINE, TRI, SAW, REC, NOISE, CRASH, OSC_SAMPLE_START };
    static const uint32_t clockIncs[] = { 0x01000000u, 0x30000000u, 0xf0000000u };

    for (unsigned w = 0u; w < 7u; ++w)
     for (unsigned c = 0u; c < 3u; ++c)
      for (unsigned interp = 0u; interp < 2u; ++interp)
       for (unsigned hard = 0u; hard < 2u; ++hard) {
        const uint8_t synced = osc_syncWaveform(waves[w]);
        uint32_t over[N];
        unsigned resets = 0u;
        int16_t ref_out[N], out[N];
        OscInfo ref = make_osc(waves[w], waves[w] >= OSC_SAMPLE_START ? (200u << 17) : 0x12345678u,
                               waves[w] >= OSC_SAMPLE_START ? (2u << 17) : 0x02000000u, 1u);
        OscInfo dut;
        uint32_t rng;

        if (hard && synced)
            continue;              /* depth 1 is S2's job for synced waves */
        if (interp && (hard || waves[w] >= CRASH))
            continue;              /* an interpolation partner may sync (S7) */
        if (interp)
            set_interp(&ref, (uint8_t)(waves[w] + 1u), 0.6f);
        host_interp_on = (uint8_t)interp;
        dut = ref;
        (void)osc_buildSyncList(0xfff00000u, clockIncs[c], over, N);
        for (unsigned i = 0u; i < N; ++i)
            resets += (unsigned)(over[i] != OSC_SYNC_NONE);
        if (resets == 0u)
            fail("S1 setup: no resets in list", w, c, 0u);
        rng = host_rng;
        calcNextOscSampleBlock(&ref, ref_out, N, 1.0f);
        host_rng = rng;
        calcNextOscSampleSyncBlock(&dut, over, clockIncs[c], hard ? 0x40000000 : 0, out, N);
        /* depth 0 (any wave, plain or interpolated) or an unsynced wave at
        ** depth 1: identical to the plain render, samples and phase. */
        for (unsigned i = 0u; i < N; ++i)
            if (out[i] != ref_out[i])
                fail(hard ? "S6 unsynced slave differs" : "S1 depth 0 differs", w, c, i);
        if (dut.phase != ref.phase)
            fail("S1/S6 phase differs", w, c, interp);
       }
    host_interp_on = 0u;
}

/* S2. Break: reset applied before sample i; ratio inverted. */
static void test_sync_hard(void)
{
    static const uint8_t waves[] = { SINE, TRI, SAW, REC, CRASH };
    static const unsigned ks[] = { 0u, 5u, 31u };
    const uint32_t clockInc = 0x20000000u;
    const uint32_t over_k = 0x0abcdef0u;

    for (unsigned w = 0u; w < 5u; ++w)
     for (unsigned kk = 0u; kk < 3u; ++kk) {
        const unsigned k = ks[kk];
        const uint32_t slaveInc = (waves[w] == CRASH) ? 0x00300000u : 0x03000000u;
        const uint32_t ratio = osc_syncRatioQ16(slaveInc, clockInc);
        const uint32_t target = (uint32_t)(((uint64_t)over_k * ratio) >> 16);
        uint32_t over[N];
        int16_t ref_out[N], out[N];
        OscInfo dut = make_osc(waves[w], 0x40000000u, slaveInc, 0u);
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
            const uint32_t expect = (n >> 32) ? (uint32_t)n : OSC_SYNC_NONE;

            if (over[i] != expect)
                fail("S4 list entry", a, b, i);
            if ((n >> 32) && (uint32_t)n >= incs[a])
                fail("S4 overshoot >= increment", a, b, i);
            c = (uint32_t)n;
        }
        if (end != c)
            fail("S4 end clock", a, b, 0u);
     }
}

/* S5. Break: the << 5 scaling missing; the clamp missing. */
static void test_clock(void)
{
    static const float freqs[] = { 55.0f, 440.0f, 3000.0f };
    OscInfo m = make_osc(TRI, 0u, 0x01234567u, 0u);

    if (osc_syncClockInc(&m) != 0x01234567u) fail("S5 table clock", 0u, 0u, 0u);
    m.waveform = NOISE;
    if (osc_syncClockInc(&m) != 0x01234567u) fail("S5 noise clock", 0u, 0u, 0u);
    m.waveform = CRASH;
    m.phaseInc = 0x00100000u;
    if (osc_syncClockInc(&m) != 0x02000000u) fail("S5 crash << 5", 0u, 0u, 0u);
    m.waveform = OSC_SAMPLE_START;
    m.phaseInc = 0x09000000u;
    if (osc_syncClockInc(&m) != 0xffffffe0u) fail("S5 clamp", 0u, 0u, 0u);

    for (unsigned f = 0u; f < 3u; ++f) {
        OscInfo tri = make_osc(TRI, 0u, freq2PhaseIncr1024(freqs[f]), 0u);
        OscInfo cr = make_osc(CRASH, 0u, freq2PhaseIncr32767(freqs[f]), 0u);
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

/* S7. Break: partners given different lists; the phase rule reversed. */
static void test_interp_sync(void)
{
    const uint32_t clockInc = 0x09000000u;
    uint32_t over[N];
    int16_t out[N], ra[N], rb[N];
    OscInfo dut = make_osc(SAW, 0x40000000u, 0x05000000u, 0u);
    OscInfo a, b;

    (void)osc_buildSyncList(0u, clockInc, over, N);
    set_interp(&dut, REC, 0.4f);
    host_interp_on = 1u;
    a = dut;
    a.waveInterpNext = SAW;
    a.waveInterpFrac = 0.0f;
    a.waveInterpGeneration = 0u;
    b = a;
    b.waveform = REC;
    b.waveInterpNext = REC;
    calcNextOscSampleSyncBlock(&dut, over, clockInc, 0x40000000, out, N);
    calcNextOscSampleSyncBlock(&a, over, clockInc, 0x40000000, ra, N);
    calcNextOscSampleSyncBlock(&b, over, clockInc, 0x40000000, rb, N);
    for (unsigned i = 0u; i < N; ++i)
        if (out[i] != (int16_t)(ra[i] + 0.4f * (rb[i] - ra[i])))
            fail("S7 interpolated blend", i, 0u, 0u);
    if (dut.phase != a.phase)
        fail("S7 phase follows the synced base", 0u, 0u, 0u);

    dut = make_osc(NOISE, 0x10000000u, 0x05000000u, 0u);
    set_interp(&dut, CRASH, 0.5f);
    calcNextOscSampleSyncBlock(&dut, over, clockInc, 0x40000000, out, N);
    if (dut.phase != 0x10000000u + 0x05000000u * (uint32_t)N)
        fail("S7 unsynced base keeps the plain phase rule", 0u, 0u, 0u);
    host_interp_on = 0u;
}

/* M1. Break: Q31 instead of Q30; K unclamped; ratio unclamped. */
static void test_maps(void)
{
    if (osc_syncDepthQ30(0.0f) != 0) fail("M1 depth 0", 0u, 0u, 0u);
    if (osc_syncDepthQ30(1.0f) != 0x40000000) fail("M1 depth 1", 0u, 0u, 0u);
    if (osc_syncDepthQ30(0.5f) != 0x20000000) fail("M1 depth 0.5", 0u, 0u, 0u);
    if (osc_syncDepthQ30(-1.0f) != 0) fail("M1 depth < 0", 0u, 0u, 0u);
    if (osc_syncDepthQ30(2.0f) != 0x40000000) fail("M1 depth > 1", 0u, 0u, 0u);
    if (osc_syncDepthQ30(NAN) != 0) fail("M1 depth NaN", 0u, 0u, 0u);
    if (osc_ringK(0.0f) != 32768) fail("M1 K at 0", 0u, 0u, 0u);
    if (osc_ringK(1.0f) != 0) fail("M1 K at 1", 0u, 0u, 0u);
    if (osc_ringK(0.5f) != 16384) fail("M1 K at 0.5", 0u, 0u, 0u);
    if (osc_ringK(0.25f) != 24576) fail("M1 K at 0.25", 0u, 0u, 0u);
    if (osc_syncDepthQ30(0.25f) != 0x10000000) fail("M1 depth 0.25", 0u, 0u, 0u);
    if (osc_ringK(NAN) != 32768) fail("M1 K NaN", 0u, 0u, 0u);
    if (osc_syncRatioQ16(100u, 0u) != 0u) fail("M1 ratio clock 0", 0u, 0u, 0u);
    if (osc_syncRatioQ16(0x01000000u, 0x01000000u) != 65536u) fail("M1 ratio 1", 0u, 0u, 0u);
    if (osc_syncRatioQ16(0xffffffffu, 1u) != 4294967040u) fail("M1 ratio clamp", 0u, 0u, 0u);
}

int main(void)
{
    fill_tables();
    test_ring();
    test_sync_depth0_and_unsynced();
    test_sync_hard();
    test_pull();
    test_builder();
    test_clock();
    test_interp_sync();
    test_maps();
    printf("osc_mod failures: %u\n", failures);
    return failures ? 1 : 0;
}
