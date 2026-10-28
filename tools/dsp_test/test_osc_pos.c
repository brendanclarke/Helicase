/*
 * Continuous oscillator position host gate (S086 P3/P4).
 *
 * What:       exercises the exact Oscillator.h position helpers and constants
 *             (extracted, not restated: op_consts.inc is the header's own
 *             OSC_POS_BEND_CAP / OSC_POS_BEND_PER_STEP block with its asserts)
 *             against the real plain Sine, table and crash readers extracted
 *             from the firmware source, called the way the engines call them
 *             (osc_positionBendApply(); render; osc_positionBendRestore()):
 *               P1 osc_positionTrigger() for waveforms 0..15 and pos 0, 1, 64,
 *                  127, 128, 255 against an independent truth table: Sin Tri
 *                  Saw Rec Cym Re3 Sw3 -> (pos & 127) << 25, every other
 *                  waveform -> 0; positionApplied = pos & 127.
 *               P2 identity: a held pos (any value) never bends, and every
 *                  block is bit-identical to rendering without the helpers
 *                  from the same start phase + (pos << 25).
 *               P3 no drift: random pos sequences over 4,096 blocks on all
 *                  seven phase waves: the final phase equals the free-running
 *                  phase + (positionApplied << 25) mod 2^32; phaseInc and
 *                  positionBendStep are restored exactly after every block.
 *               P4 circular: 127->0 = +1 step, 0->127 = -1, 0->64 = -64,
 *                  64->0 = -64, 4->12 = +8 (capped in the cap build); the step
 *                  is stored and phaseInc moves by step x OSC_POS_BEND_PER_STEP.
 *               P5 continuity: every sample of a bent block equals an
 *                  independent per-sample reader at phase0 + k x (inc + bend).
 *               P6 non-phase waves (NOISE, user sample 8): no bend, step 0,
 *                  positionApplied follows pos.
 *               P7 (cap build, OSC_POS_BEND_CAP 8): a step of 40 lands in five
 *                  blocks of 8, exactly.
 *               P8 consume only (Drum snc, bend argument 0): no bend, step 0,
 *                  positionApplied follows pos.
 * Why:        086 log §9.3 and §5, with the S086 P3 assessment fixes
 *             F2 (independent P1, P2 identity, all phase waves) and F3 (the
 *             header's constants are compiled here, so a wrong constant fails
 *             the build or P3/P5). Each check names the break that must fail
 *             it (086_SESSION_HANDOFF_LOG.md §9).
 * Inputs:     generated fragments from the Makefile's osc_mod/osc_pos rules.
 * Outputs:    one line per failure (first 40); "osc_pos failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test osc_pos.
 * Affiliates: Oscillator.h pos helpers and constants; the four voice render
 *             functions; S086 P3/P4 plans.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define REAL_FS ((float)44108.07291666667f)
#define TABLESIZE 4096
#define OSC_S085_LOOP
#define OSC_S085_SECTION
#define OSC_S085_DISPATCH
#define INTERPOLATE_OSC 1
#define INTERPOLATE_FM_OSC 1
#define OSC_SYNC_NONE 0xFFFFFFFFu

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
static uint32_t host_rng = 0x0860002u;

int16_t GetRngValue(void) { return (int16_t)(host_rng = host_rng * 1664525u + 1013904223u); }
uint8_t modNode_getWaveInterpEnabled(void) { return 0u; }
uint32_t modNode_getWaveInterpGeneration(void) { return 1u; }
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

#include "op_consts.inc"      /* Oscillator.h: OSC_POS_BEND_CAP, _PER_STEP, asserts */
#include "op_helpers.c"       /* Oscillator.h: the pos helpers */
#include "om_scratch.inc"
#include "om_stock.inc"
#include "om_plain.c"

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
    for (unsigned i = 0u; i < 4097u; ++i)
        sine_table[i] = (int16_t)lrint(32767.0 *
                                      sin(2.0 * M_PI * (double)i / 4096.0));
    for (unsigned o = 0u; o < 11u; ++o)
        for (unsigned i = 0u; i < 1024u; ++i) {
            const double x = (double)i / 1024.0;
            sawTable[o][i] = (int16_t)lrint(32767.0 * (2.0 * x - 1.0));
            triTable[o][i] = (int16_t)lrint(32767.0 *
                                            (1.0 - 4.0 * fabs(x - 0.5)));
            recTable[o][i] = (i < 512u) ? 32767 : -32768;
            /* distinct content, so a wrong table pointer would show */
            sq3Table[o][i] = (int16_t)lrint(32767.0 * sin(2.0 * M_PI * x * 3.0));
            sw3Table[o][i] = (int16_t)lrint(32767.0 * (1.0 - 2.0 * x) * fabs(1.0 - 2.0 * x));
        }
    for (unsigned i = 0u; i < 32768u; ++i)
        crashSample[i] = (unsigned char)(128u + (i & 127u));
    crashSample[32768] = crashSample[0];
}

static OscInfo make_osc(uint8_t waveform, uint32_t phase, uint32_t inc)
{
    OscInfo osc;
    memset(&osc, 0, sizeof(osc));
    osc.waveform = waveform;
    osc.waveInterpNext = waveform;
    osc.phase = phase;
    osc.phaseInc = inc;
    osc.tableOffset = 3u;
    return osc;
}

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
            OscInfo osc = make_osc(w, 0x12345678u, 0u);
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

            OscInfo bent = make_osc(phase_waves[w], start, inc);
            bent.position = pos;
            bent.phase += osc_positionTrigger(&bent);
            OscInfo plain = make_osc(phase_waves[w], start + ((uint32_t)pos << 25), inc);
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
    OscInfo osc = make_osc(waveform, 0x13572468u, 0x00123456u);
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
        OscInfo osc = make_osc(SINE, 0u, 0x00100000u);
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
        OscInfo osc = make_osc(phase_waves[w], phase, inc);
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

/* P6: non-phase waves consume the delta without bending. */
static void test_nonphase(void)
{
    const uint8_t waves[] = { NOISE, OSC_SAMPLE_START };
    for (unsigned i = 0u; i < sizeof(waves); ++i) {
        OscInfo osc = make_osc(waves[i], 0u, 0x00010000u);
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
    OscInfo osc = make_osc(SINE, 0u, 0u);
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
        OscInfo osc = make_osc(phase_waves[w], 0u, 0x00200000u);
        osc.positionApplied = 3u;
        osc.position = 7u;
        osc_positionBendApply(&osc, 0u);
        if (osc.phaseInc != 0x00200000u || osc.positionBendStep != 0 ||
            osc.positionApplied != 7u)
            fail("P8 consume only", phase_waves[w], osc.positionApplied, 0u);
        osc_positionBendRestore(&osc);
    }
}

int main(void)
{
    fill_tables();
    test_trigger();
    test_identity();
    for (unsigned w = 0u; w < sizeof(phase_waves); ++w)
        test_no_drift(phase_waves[w]);
    test_delta();
    test_continuity();
    test_nonphase();
    test_cap();
    test_consume_only();
    printf("osc_pos failures: %u\n", failures);
    return failures ? 1 : 0;
}
