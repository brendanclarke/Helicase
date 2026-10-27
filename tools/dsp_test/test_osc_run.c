/*
 * Oscillator, section, pool, pwm, sample and wavetable host checks
 * (S085 C17.6; strengthened by the S085 assessment, A4-A6).
 *
 * What:       exercises the real S085 sources through their public entry
 *             points:
 *             - slew: the rendered per-sample increment trajectory (A5);
 *             - itp: carrier-only crossfade against the bound of the same
 *               sound without a trigger, in fm and mix, with the engine's
 *               carrier reset and a new pitch; rst and a modulator-reset
 *               variant must exceed the bound (A4);
 *             - pool: the claim order with waveform interpolation between
 *               ongoing and new crossfades (A6);
 *             - snc: bit-identity of every slave family with no wraps, the
 *               reset index (D13) for table/smp/pwm/wtb slaves, the itp copy's
 *               own ratio, the mix law, a sample master with start > 0 (F5),
 *               a finished one-shot master (D6); rng law (A6);
 *             - smp/slp: request clamp and re-expansion, start, a full
 *               100,000-sample playback, end silence, the loop seam and wrap,
 *               the gain code (A6);
 *             - wtb: frame-pair limits, block continuity, the idx/nx wrap at
 *               frame_len - 1, the 8-bit path (A6);
 *             - pwm: prc polarity (> 0.99), the F3 midpoint read within
 *               1 LSB, monotonic prc width (A6).
 * Why:        G3 needs a host gate for each new layer; each case fails on a
 *             deliberately broken change (the break runs are recorded in
 *             S085_P1_IMPLEMENTATION.md §23).
 * Inputs:     host_osc_support.c catalogue doubles and test hooks.
 * Outputs:    "N failures"; exits nonzero on failure.
 * Accessors:  make -C tools/dsp_test osc_run, osc_pwm, osc_sample, wtb.
 * Affiliates: Oscillator.c, OscSection.c, OscRenderPool.c, OscillatorPwm.c,
 *             OscillatorSample.c, OscillatorWavetable.c, OscSyncLane.h.
 *             - fm depth ramp (S085 P2 F-A): exact wiring, flat = unramped,
 *               forced flat under itp, no boundary step.
 * Not here:   the F11 type-writer hygiene and the A9 LFO base re-derivation
 *             live in InstrumentManager.c, which the host harness does not
 *             compile (hardware rows H6 and H9).
 */
#include "prelude_osc.h"
#include "Oscillator.h"
#include "OscSection.h"
#include "OscRenderPool.h"
#include "SampleMemory.h"
#include "OscillatorSample.h"
#include "OscillatorWavetable.h"
#include "OscillatorPwm.h"
#include "wavetable.h"

void host_catalogue_reset(void);
void host_catalogue_set(uint8_t samples, uint8_t tables, uint8_t stock);
void host_set_gain_code(uint8_t code);
void host_set_table_bits(uint8_t bits);
void host_rng_reset(void);
const int16_t *host_sample_data(uint8_t index);
const int16_t *host_table_data(uint8_t index);

static int failures;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL %s\n", msg); failures++; } \
} while (0)

#define N OUTPUT_DMA_SIZE

static OscInfo osc_init(uint8_t type, uint8_t waveform)
{
    OscInfo o;

    memset(&o, 0, sizeof(o));
    o.oscType = type;
    o.waveform = waveform;
    o.freq = 440.0f;
    o.pitchMod = 1.0f;
    o.modNodeValue = 1.0f;
    o.fmMod = 0.5f;
    o.source = 0u;
    o.sourceRequest = 0u;
    o.waveRequest = waveform;
    return o;
}

static int32_t max_step(const int16_t *buf, uint8_t n, int32_t prev)
{
    int32_t m = 0;
    uint8_t i;

    for (i = 0u; i < n; i++) {
        const int32_t d = (int32_t)buf[i] - prev;
        const int32_t ad = (d < 0) ? -d : d;
        if (ad > m)
            m = ad;
        prev = buf[i];
    }
    return m;
}

static int same_block(const int16_t *a, const int16_t *b, uint8_t n)
{
    return memcmp(a, b, (size_t)n * sizeof(int16_t)) == 0;
}

/* A plain unit-gain render through the type dispatch. */
static void plain(OscInfo *o, int16_t *buf, uint8_t n)
{
    calcNextOscSampleBlock(o, buf, n, 1.0f);
}

/* ---------------------------------------------------------------- slew */

/*
 * A5: the rendered slew trajectory. Before each block osc_setFreq() sets the
 * step; the renderer adds it on every sample. The per-sample increments
 * (phaseInc + k x step) must move monotonically toward the target, never
 * past it, and reach it; the renderer's write-back must match.
 */
static void slew_case(float from, float to, const char *label)
{
    OscInfo o = osc_init(OSC_TYPE_WAV, SAW);
    int16_t buf[N];
    uint32_t start, target = 0u, last;
    int64_t worstPast = 0;
    int monotonic = 1, writeBack = 1;
    unsigned samples = 0u, reached = 0u;
    char msg[96];

    o.freq = from;
    osc_setFreq(&o);
    start = o.phaseInc;
    o.freq = to;
    osc_armSlew(&o);
    last = start;
    for (int b = 0; b < 8; b++) {
        uint32_t inc;

        osc_setFreq(&o);
        target = o.phaseIncTarget;
        inc = o.phaseInc;
        for (int k = 0; k < (int)N; k++) {
            const int64_t past = (to > from) ? (int64_t)inc - (int64_t)target
                                             : (int64_t)target - (int64_t)inc;
            if (past > worstPast)
                worstPast = past;
            if ((to > from) ? (inc < last) : (inc > last))
                monotonic = 0;
            if (!reached && inc == target)
                reached = samples + 1u;
            last = inc;
            inc += (uint32_t)o.phaseIncStep;
            samples++;
        }
        plain(&o, buf, N);
        if (o.phaseInc != inc)
            writeBack = 0;
    }
    snprintf(msg, sizeof msg, "%s: renderer write-back matches", label);
    CHECK(writeBack, msg);
    snprintf(msg, sizeof msg, "%s: increment never passes the target "
             "(worst %lld)", label, (long long)worstPast);
    CHECK(worstPast <= 0, msg);
    snprintf(msg, sizeof msg, "%s: increment is monotonic", label);
    CHECK(monotonic, msg);
    snprintf(msg, sizeof msg, "%s: target reached within 6 blocks", label);
    CHECK(o.phaseInc == target && reached > 0u && reached <= 6u * N, msg);
}

static void test_slew(void)
{
    OscInfo o = osc_init(OSC_TYPE_WAV, SAW);

    slew_case(200.0f, 400.0f, "slew up");
    slew_case(400.0f, 150.0f, "slew down");
    o.slewSamplesLeft = 0u;
    osc_setFreq(&o);
    CHECK(o.phaseIncStep == 0 && o.phaseInc == o.phaseIncTarget,
          "no slew is immediate");
}

/* ----------------------------------------------------------------- itp */

#define ITP_WARM_BLOCKS  3
#define ITP_AFTER_BLOCKS 6   /* 132 samples of crossfade fit in 5 blocks */

/*
 * One Drum section run in the engine's order: warm-up blocks, then the
 * trigger (beginTrigger, the engine's carrier reset for a SINE carrier,
 * optionally a modulator reset, the new pitch), then blocks with the mixer's
 * claim order. Returns the largest sample-to-sample step from the last
 * warm-up sample on. With trigger = 0 the pitch changes with no reset: the
 * running sound used as the bound.
 */
static int32_t itp_run(uint8_t mode, uint8_t run, uint8_t trigger,
                       uint8_t resetMods, int *nonVacuous)
{
    OscSectionState s;
    OscInfo car = osc_init(OSC_TYPE_WAV, SINE);
    OscInfo mod = osc_init(OSC_TYPE_WAV, SINE);
    OscInfo *mods[1] = { &mod };
    int16_t buf[N], mb[N], old[N];
    int32_t prev = 0, worst = 0;
    int b;

    car.freq = 220.0f;
    car.fmMod = (mode == OSC_MOD_FM) ? 0.3f : 0.0f;
    mod.freq = 83.0f;
    oscSection_init(&s);
    s.runMode = run;
    for (b = 0; b < ITP_WARM_BLOCKS; b++) {
        osc_setFreq(&car);
        osc_setFreq(&mod);
        oscSection_renderDrum(&car, &mod, mode, 0.5f, buf, mb, N, 0, 0);
    }
    prev = buf[N - 1u];
    if (trigger) {
        const uint8_t reset = oscSection_beginTrigger(&s, &car, mods, 1u);

        if (reset != OSC_TRIGGER_RESET_NONE)
            car.phase = 1024u + ((0x3ffu << 20) - 1024u);  /* engine SINE reset */
        if (resetMods)
            mod.phase = 0u;
    }
    car.freq = 330.0f;
    for (b = 0; b < ITP_AFTER_BLOCKS; b++) {
        OscInfo *oldCar;

        oscRenderPool_beginBlock();
        oscSection_claimOngoing(&s);
        oscSection_claimPending(&s, &car);
        osc_setFreq(&car);
        osc_setFreq(&mod);
        oldCar = oscSection_itpCarrier(&s);
        oscSection_renderDrum(&car, &mod, mode, 0.5f, buf, mb, N, oldCar, old);
        if (oldCar && b == 0 && nonVacuous)
            *nonVacuous = !same_block(buf, old, N);
        if (oldCar)
            oscSection_blendItp(&s, buf, old, N);
        oscSection_finishBlock(&s, N);
        {
            const int32_t m = max_step(buf, N, prev);
            if (m > worst)
                worst = m;
        }
        prev = buf[N - 1u];
    }
    return worst;
}

/*
 * A4: the crossfade may add at most the fade slope (full scale / 132) to the
 * step of the same sound changing pitch without a trigger; rst, and a
 * modulator reset in mix, must exceed that bound (the measure can fail).
 */
static void test_itp_click(void)
{
    const int32_t slope = 65536 / (int32_t)OSC_ITP_SAMPLES + 2;
    const uint8_t modes[2] = { OSC_MOD_FM, OSC_MOD_MIX };
    const char *names[2] = { "fm", "mix" };
    char msg[96];

    for (int m = 0; m < 2; m++) {
        const int32_t plainStep = itp_run(modes[m], OSC_RUN_RST, 0u, 0u, 0);
        const int32_t bound = plainStep + slope;
        int nonVacuous = 0;
        const int32_t itp = itp_run(modes[m], OSC_RUN_ITP, 1u, 0u, &nonVacuous);
        const int32_t rst = itp_run(modes[m], OSC_RUN_RST, 1u, 0u, 0);

        printf("itp %s: crossfade step %d, bound %d, rst step %d\n", names[m],
               (int)itp, (int)bound, (int)rst);
        snprintf(msg, sizeof msg, "itp %s: old and new carrier differ", names[m]);
        CHECK(nonVacuous, msg);
        snprintf(msg, sizeof msg, "itp %s: step %d within bound %d", names[m],
                 (int)itp, (int)bound);
        CHECK(itp <= bound, msg);
        snprintf(msg, sizeof msg, "itp %s: rst step %d exceeds bound %d",
                 names[m], (int)rst, (int)bound);
        CHECK(rst > bound, msg);
        if (modes[m] == OSC_MOD_MIX) {
            const int32_t bad = itp_run(modes[m], OSC_RUN_ITP, 1u, 1u, 0);

            printf("itp mix: with a modulator reset, step %d\n", (int)bad);
            snprintf(msg, sizeof msg,
                     "itp mix: a modulator reset (%d) exceeds bound %d",
                     (int)bad, (int)bound);
            CHECK(bad > bound, msg);
        }
    }
}

/* ---------------------------------------------------------------- pool */

static void test_pool(void)
{
    OscSectionState a, b;

    oscSection_init(&a);
    oscSection_init(&b);
    a.itpState = OSC_ITP_ACTIVE;
    b.itpState = OSC_ITP_ACTIVE;
    oscRenderPool_beginBlock();
    oscSection_claimOngoing(&a);
    oscSection_claimOngoing(&b);
    CHECK(!oscRenderPool_claim(1u), "pool full after two crossfades");
    CHECK(a.itpState == OSC_ITP_ACTIVE && b.itpState == OSC_ITP_ACTIVE,
          "ongoing crossfades are not cut");

    oscRenderPool_beginBlock();
    oscSection_init(&a);
    a.itpState = OSC_ITP_PENDING;
    {
        OscInfo car = osc_init(OSC_TYPE_WAV, SAW);
        const uint32_t saved = 0x12345678u;

        car.phase = saved;
        a.itpOld.phase = saved;
        CHECK(oscRenderPool_claim(2u), "pool drained for the fallback case");
        oscSection_claimPending(&a, &car);
        CHECK(a.itpState == OSC_ITP_IDLE, "refused pending falls back");
        CHECK(car.phase == saved && car.slewSamplesLeft == OSC_SLEW_SAMPLES,
              "refused pending restores phase and arms slew");
    }

    /* A6: ongoing -> waveform interpolation -> new crossfade. */
    {
        OscInfo car = osc_init(OSC_TYPE_WAV, SAW);

        oscSection_init(&a);
        oscSection_init(&b);
        a.itpState = OSC_ITP_ACTIVE;
        b.itpState = OSC_ITP_PENDING;
        oscRenderPool_beginBlock();
        oscSection_claimOngoing(&a);
        CHECK(oscRenderPool_claim(1u), "interpolation takes the unit left");
        oscSection_claimPending(&b, &car);
        CHECK(a.itpState == OSC_ITP_ACTIVE && b.itpState == OSC_ITP_IDLE,
              "a new crossfade claims after interpolation and is refused");

        oscSection_init(&b);
        b.itpState = OSC_ITP_PENDING;
        oscRenderPool_beginBlock();
        CHECK(oscRenderPool_claim(1u), "interpolation claims first");
        oscSection_claimPending(&b, &car);
        CHECK(b.itpState == OSC_ITP_ACTIVE,
              "a new crossfade takes the second unit");
    }
}

/* ---------------------------------------------------------------- snc */

/* A slave prepared with its increment, bound if it needs a catalogue.
** sel = the wav row (waveform), ctl = the mod row (source; P2 §14). */
static OscInfo slave_of(uint8_t type, uint8_t sel, uint8_t ctl)
{
    OscInfo o = osc_init(type, sel);

    o.source = ctl;
    o.sourceRequest = ctl;
    o.freq = 523.0f;
    osc_setFreq(&o);
    if (type == OSC_TYPE_SMP || type == OSC_TYPE_SLP)
        oscSample_bind(&o);
    if (type == OSC_TYPE_WTB)
        oscWavetable_bind(&o);
    return o;
}

/* A2: with no master wrap the sync loop of each family equals its plain loop. */
static void test_snc_identity(void)
{
    const struct { uint8_t type, sel, ctl; const char *name; } fam[] = {
        { OSC_TYPE_WAV, SINE, 0u, "sine" },  { OSC_TYPE_WAV, SAW, 0u, "saw" },
        { OSC_TYPE_WAV, NOISE, 0u, "noise" }, { OSC_TYPE_WAV, CRASH, 0u, "crash" },
        { OSC_TYPE_WAV, SQ3, 0u, "stock" },  { OSC_TYPE_PWM, OSC_PWM_SWP, 40u, "pwm" },
        { OSC_TYPE_SMP, 0u, 10u, "smp" },    { OSC_TYPE_SLP, 1u, 10u, "slp" },
        { OSC_TYPE_WTB, 0u, 50u, "wtb" },
    };
    char msg[96];

    host_catalogue_reset();
    host_catalogue_set(2u, 1u, 0u);
    for (unsigned f = 0u; f < sizeof fam / sizeof fam[0]; f++) {
        OscSectionState s;
        OscInfo car = slave_of(fam[f].type, fam[f].sel, fam[f].ctl);
        OscInfo ref = car;
        OscInfo mod = osc_init(OSC_TYPE_WAV, SINE);
        int16_t a[N], b[N], mb[N];

        mod.phase = 0u;
        mod.phaseInc = 1000u;            /* no wrap in this block */
        oscSection_init(&s);
        host_rng_reset();
        plain(&ref, a, N);
        host_rng_reset();
        oscSection_renderDrum(&car, &mod, OSC_MOD_SNC, 0.0f, b, mb, N, 0, 0);
        snprintf(msg, sizeof msg, "snc %s: no wrap equals the plain loop",
                 fam[f].name);
        CHECK(same_block(a, b, N) && car.phase == ref.phase, msg);
    }
}

/*
 * D13 reset index for one slave: the master wraps between samples 10 and 11
 * with an overshoot of half its increment; the slave must render samples
 * 0..10 as the free-running slave, then restart at base + overshoot x ratio
 * from sample 11. amount 0 makes the output the slave alone (exact through
 * the mix law).
 */
static void snc_reset_case(OscInfo car, uint32_t base, const char *name)
{
    OscInfo mod = osc_init(OSC_TYPE_WAV, SINE);
    OscInfo ref = car;
    int16_t out[N], mb[N], expect[N];
    const uint32_t minc = 0x08000000u;
    char msg[96];

    mod.phaseInc = minc;
    mod.phase = 0u - (minc * 10u + minc / 2u);   /* wraps after sample 10 */
    oscSection_renderDrum(&car, &mod, OSC_MOD_SNC, 0.0f, out, mb, N, 0, 0);

    plain(&ref, expect, 11u);
    ref.phase = base + (uint32_t)((float)(minc / 2u) *
                ((float)ref.phaseInc / (float)minc));
    plain(&ref, expect + 11, (uint8_t)(N - 11u));
    snprintf(msg, sizeof msg, "snc %s: slave renders through the wrap sample, "
             "resets on the next (D13)", name);
    CHECK(same_block(out, expect, N), msg);
}

static void test_snc_reset_index(void)
{
    host_catalogue_reset();
    host_catalogue_set(1u, 1u, 0u);
    {
        OscInfo car = slave_of(OSC_TYPE_WAV, SAW, 0u);

        car.phase = 0x10000000u;
        car.phaseInc = 0x01000000u;
        snc_reset_case(car, 0u, "saw");
    }
    {
        OscInfo car = slave_of(OSC_TYPE_SMP, 0u, 20u);

        car.phase = (car.sampleStart + 5000u) << OSC_SAMPLE_PHASE_SHIFT;
        car.phaseInc = 3u << OSC_SAMPLE_PHASE_SHIFT;
        snc_reset_case(car, car.sampleStart << OSC_SAMPLE_PHASE_SHIFT, "smp");
    }
    {
        OscInfo car = slave_of(OSC_TYPE_PWM, OSC_PWM_PRC, 64u);

        car.phase = 0x30000000u;
        car.phaseInc = 0x00800000u;
        snc_reset_case(car, 0u, "pwm");
    }
    {
        OscInfo car = slave_of(OSC_TYPE_WTB, 0u, 0u);

        car.phase = 0x20000000u;
        car.phaseInc = 0x00400000u;
        snc_reset_case(car, 0u, "wtb");
    }
}

/* The itp copy follows the same wraps with its own ratio. */
static void test_snc_itp_copy(void)
{
    OscInfo car = osc_init(OSC_TYPE_WAV, SAW), old;
    OscInfo mod = osc_init(OSC_TYPE_WAV, SINE), refOld;
    int16_t out[N], oldOut[N], mb[N], expect[N];
    const uint32_t minc = 0x08000000u;

    car.phase = 0x10000000u;
    car.phaseInc = 0x01000000u;
    old = car;
    old.phase = 0x50000000u;
    old.phaseInc = 0x00600000u;
    refOld = old;
    mod.phaseInc = minc;
    mod.phase = 0u - (minc * 10u + minc / 2u);
    oscSection_renderDrum(&car, &mod, OSC_MOD_SNC, 0.0f, out, mb, N, &old,
                          oldOut);
    plain(&refOld, expect, 11u);
    refOld.phase = (uint32_t)((float)(minc / 2u) *
                   ((float)refOld.phaseInc / (float)minc));
    plain(&refOld, expect + 11, (uint8_t)(N - 11u));
    CHECK(same_block(oldOut, expect, N),
          "snc itp copy resets at the same wrap with its own ratio");
}

static void test_snc_and_rng(void)
{
    OscSectionState s;
    int16_t buf[N], modBuf[N];

    /* D6: a finished one-shot master does not reset the slave. */
    {
        OscInfo car = osc_init(OSC_TYPE_WAV, SAW);
        OscInfo mod = osc_init(OSC_TYPE_SMP, 0u);
        const uint32_t carStart = 0x00200000u;

        host_catalogue_reset();
        host_catalogue_set(1u, 0u, 0u);
        oscSection_init(&s);
        car.phase = carStart;
        car.phaseInc = 0x00100000u;
        car.phaseIncStep = 0;
        oscSample_bind(&mod);
        mod.phase = (mod.sampleEnd - 1u) << OSC_SAMPLE_PHASE_SHIFT;
        mod.phaseInc = 0x00010000u;
        mod.phaseIncStep = 0;
        oscSection_renderDrum(&car, &mod, OSC_MOD_SNC, 0.5f, buf, modBuf,
                              N, 0, 0);
        CHECK(car.phase == carStart + car.phaseInc * N,
              "finished one-shot master does not reset the slave");
    }

    /* F5: a sample master with start > 0 wraps after (end - start) frames. */
    {
        OscInfo car = osc_init(OSC_TYPE_WAV, SAW), ref;
        OscInfo mod = osc_init(OSC_TYPE_SMP, 0u);
        int16_t expect[N];
        uint32_t cycle, over;

        host_catalogue_reset();
        host_catalogue_set(1u, 0u, 0u);
        mod.source = 64u;                /* mod row: start (P2 §14) */
        oscSample_bind(&mod);
        cycle = (mod.sampleEnd - mod.sampleStart) << OSC_SAMPLE_PHASE_SHIFT;
        mod.phase = mod.sampleStart << OSC_SAMPLE_PHASE_SHIFT;
        /* 20.5 samples per span: one wrap, after sample 20. */
        mod.phaseInc = (uint32_t)((uint64_t)cycle * 2u / 41u) + 1u;
        over = mod.phaseInc * 21u - cycle;
        car.phase = 0x10000000u;
        car.phaseInc = 0x01000000u;
        ref = car;
        oscSection_renderDrum(&car, &mod, OSC_MOD_SNC, 0.0f, buf, modBuf,
                              N, 0, 0);
        plain(&ref, expect, 21u);
        ref.phase = (uint32_t)((float)over *
                    ((float)ref.phaseInc / (float)mod.phaseInc));
        plain(&ref, expect + 21, (uint8_t)(N - 21u));
        CHECK(mod.sampleStart > 0u && same_block(buf, expect, N),
              "snc sample master with start > 0 wraps after its span (F5)");
    }

    /* Q10 mix law: slave x (1 - amt) + master x amt, saturated. */
    {
        OscInfo car = osc_init(OSC_TYPE_WAV, SAW), mod = osc_init(OSC_TYPE_WAV, TRI);
        OscInfo c2, m2, mref;
        int16_t slave[N], master[N];
        const float amt = 0.25f;
        int ok = 1;

        car.phase = 0x10000000u;
        car.phaseInc = 0x01000000u;
        car.tableOffset = 3u;
        mod.phaseInc = 0x08000000u;
        mod.phase = 0u - (0x08000000u * 10u + 0x04000000u);
        c2 = car;
        m2 = mod;
        mref = mod;
        oscSection_renderDrum(&c2, &m2, OSC_MOD_SNC, 0.0f, slave, modBuf, N,
                              0, 0);
        oscSection_renderDrum(&car, &mod, OSC_MOD_SNC, amt, buf, modBuf, N,
                              0, 0);
        plain(&mref, master, N);
        for (uint8_t i = 0u; i < N; i++) {
            const int16_t e = (int16_t)__SSAT((int32_t)((float)slave[i] *
                (1.0f - amt) + (float)master[i] * amt), 16);
            if (buf[i] != e)
                ok = 0;
        }
        CHECK(ok, "snc mix law slave x (1 - amt) + master x amt (Q10)");
    }

    /* rng: carrier x (1 - amt) + ((carrier x modulator) >> 15) x amt. */
    {
        OscInfo car = osc_init(OSC_TYPE_WAV, SAW), mod = osc_init(OSC_TYPE_WAV, SINE);
        OscInfo cref, mref;
        int16_t c[N], m[N];
        const float amt = 0.6f;
        int ok = 1;

        car.phase = 0x10000000u;
        car.phaseInc = 0x01000000u;
        mod.phaseInc = 0x00300000u;
        cref = car;
        mref = mod;
        oscSection_renderDrum(&car, &mod, OSC_MOD_RNG, amt, buf, modBuf, N,
                              0, 0);
        plain(&cref, c, N);
        plain(&mref, m, N);
        for (uint8_t i = 0u; i < N; i++) {
            const int32_t ring = ((int32_t)c[i] * (int32_t)m[i]) >> 15;
            const int16_t e = (int16_t)__SSAT(
                (int32_t)((float)c[i] * (1.0f - amt)) +
                (int32_t)((float)ring * amt), 16);
            if (buf[i] != e)
                ok = 0;
        }
        CHECK(ok, "rng law carrier x (1 - amt) + ring x amt");
    }
}

/* --------------------------------------------------------------- sample */

static void test_sample(void)
{
    OscInfo o = osc_init(OSC_TYPE_SMP, 0u);
    int16_t buf[N];
    uint8_t nonzero = 0u;

    host_catalogue_reset();
    host_catalogue_set(2u, 0u, 0u);
    o.waveRequest = 9u;
    o.waveform = 9u;
    oscSample_bind(&o);
    CHECK(o.waveform == 1u, "the wav-row selection clamps to count-1");
    o.source = 64u;
    oscSample_bind(&o);
    CHECK(o.sampleStart == (100000u - 4u) / 2u, "sample start is mod x frames / 128");
    o.phase = (o.sampleEnd - 1u) << OSC_SAMPLE_PHASE_SHIFT;
    oscSample_render(&o, buf, N, 1.0f);
    for (uint8_t i = 0u; i < N; i++)
        nonzero |= (uint8_t)(buf[i] != 0);
    CHECK(nonzero == 0u, "smp outputs 0 after the last frame");
    CHECK(oscSample_isFinished(&o) != 0u, "finished test matches");

    /* A6: re-expansion keeps the request. */
    {
        OscInfo r = osc_init(OSC_TYPE_SMP, 0u);

        host_catalogue_set(5u, 0u, 0u);
        r.waveRequest = 7u;
        r.waveform = 7u;
        oscSample_bind(&r);
        CHECK(r.waveform == 4u, "7 requested of 5 installed binds 4");
        host_catalogue_set(8u, 0u, 0u);
        oscSample_bind(&r);
        CHECK(r.waveform == 7u, "after a reinstall of 8 the request binds 7");
    }

    /* A6: a full one-shot at one frame per sample. */
    {
        OscInfo p = osc_init(OSC_TYPE_SMP, 0u);
        const int16_t *data;
        uint32_t k = 0u, bad = 0u, end;

        host_catalogue_reset();
        host_catalogue_set(1u, 0u, 0u);
        oscSample_bind(&p);
        data = host_sample_data(0u);
        end = p.sampleEnd;
        p.phase = 0u;
        p.phaseInc = 1u << OSC_SAMPLE_PHASE_SHIFT;
        for (uint32_t blk = 0u; blk < 100000u / N; blk++) {
            oscSample_render(&p, buf, N, 1.0f);
            for (uint8_t i = 0u; i < N; i++, k++) {
                const int16_t e = (k + 1u < end) ? data[k] : 0;
                if (buf[i] != e)
                    bad++;
            }
        }
        CHECK(end == 99996u && bad == 0u,
              "smp plays all frames once, then 0 (100,000-sample run)");
    }

    /* A6: the slp seam interpolates last -> start, and the phase wraps. */
    {
        OscInfo lp = osc_init(OSC_TYPE_SLP, 0u);
        const int16_t *data;
        int32_t a, bv, e;

        host_catalogue_reset();
        host_catalogue_set(1u, 0u, 0u);
        oscSample_bind(&lp);
        data = host_sample_data(0u);
        a = data[lp.sampleEnd - 1u];
        bv = data[lp.sampleStart];
        lp.phase = ((lp.sampleEnd - 1u) << OSC_SAMPLE_PHASE_SHIFT) + 2048u;
        lp.phaseInc = 1u << OSC_SAMPLE_PHASE_SHIFT;
        oscSample_render(&lp, buf, 1u, 1.0f);
        e = a + (int32_t)((float)2048u * (1.0f / 4096.0f) * (float)(bv - a));
        CHECK(a != bv && buf[0] == (int16_t)(float)e,
              "slp seam interpolates from the last frame to the start");
        CHECK(lp.phase == 2048u, "slp phase wraps to start + remainder");
    }

    /* A6: the gain code scales the output by 2^(code/24). */
    {
        OscInfo g0 = osc_init(OSC_TYPE_SMP, 0u), g24;
        int16_t b0[N], b24[N];
        int ok = 1;

        host_catalogue_reset();
        host_catalogue_set(1u, 0u, 0u);
        oscSample_bind(&g0);
        g0.phase = 0u;
        g0.phaseInc = 1u << OSC_SAMPLE_PHASE_SHIFT;
        g24 = g0;
        oscSample_render(&g0, b0, N, 1.0f);
        host_set_gain_code(24u);
        oscSample_render(&g24, b24, N, 1.0f);
        for (uint8_t i = 0u; i < N; i++)
            if (b24[i] != (int16_t)((float)b0[i] * 2.0f))
                ok = 0;
        CHECK(ok && b0[5] != 0, "gain code 24 doubles the output");
        host_set_gain_code(0u);
    }
}

/* ------------------------------------------------------------------ wtb */

static void test_wtb(void)
{
    OscInfo o = osc_init(OSC_TYPE_WTB, 0u);
    int16_t buf[N];

    host_catalogue_reset();
    host_catalogue_set(0u, 2u, 0u);
    o.waveRequest = 0u;
    o.source = 0u;
    oscWavetable_bind(&o);
    CHECK(o.wtb.base != 0u, "wtb binds a table");
    o.source = 127u;                     /* mod row: position (P2 §14) */
    oscWavetable_render(&o, buf, N, 1.0f);
    CHECK(o.wtb.pos <= 3.0f, "one block moves at most one frame");
    CHECK(o.wtb.pos >= 1.0f, "a jump moves a frame");
    o.source = 127u;
    oscWavetable_render(&o, buf, N, 1.0f);
    CHECK(o.wtb.pos <= 3.0f, "second block stays within the table");

    /* A6: two blocks equal one 64-sample render at a fixed position. */
    {
        OscInfo a = osc_init(OSC_TYPE_WTB, 0u), b;
        int16_t one[2u * N], two[2u * N];

        a.phase = 0x01234567u;
        a.phaseInc = 0x00F00000u;
        b = a;
        oscWavetable_render(&a, one, (uint8_t)(2u * N), 1.0f);
        oscWavetable_render(&b, two, N, 1.0f);
        oscWavetable_render(&b, two + N, N, 1.0f);
        CHECK(same_block(one, two, (uint8_t)(2u * N)) && a.phase == b.phase,
              "wtb block boundary carries phase and frame state");
    }

    /* A6: idx = frame_len - 1 interpolates to point 0 of the same frame. */
    {
        OscInfo w = osc_init(OSC_TYPE_WTB, 0u);
        const int16_t *t = host_table_data(0u);
        const float fa = (float)t[31], fb = (float)t[0];
        const int16_t e = (int16_t)((fa + 0.5f * (fb - fa)) * 1.0f);

        w.phase = 31u * 0x08000000u + 0x04000000u;
        w.phaseInc = 0u;
        oscWavetable_render(&w, buf, 1u, 1.0f);
        CHECK(buf[0] == e, "wtb wraps the last point to point 0 (nx bound)");
    }

    /* A6: the 8-bit path reads int8 points << 8. */
    {
        OscInfo w = osc_init(OSC_TYPE_WTB, 0u);
        const int8_t *bytes = (const int8_t *)host_table_data(0u);
        int ok = 1;

        host_set_table_bits(8u);
        w.phase = 0u;
        w.phaseInc = 0x08000000u;          /* one point per sample, frac 0 */
        oscWavetable_render(&w, buf, N, 1.0f);
        for (uint8_t i = 0u; i < N; i++)
            if (buf[i] != (int16_t)((int32_t)bytes[i] << 8))
                ok = 0;
        CHECK(ok && w.wtb.bits == 8u, "wtb 8-bit frames read as int8 << 8");
        host_set_table_bits(16u);
    }
}

/* ------------------------------------------------------------------ pwm */

static void test_pwm(void)
{
    OscInfo o = osc_init(OSC_TYPE_PWM, 0u);
    int16_t buf[N];
    double ab = 0.0, aa = 0.0, bb = 0.0;
    uint32_t phase;

    host_catalogue_reset();
    host_catalogue_set(0u, 0u, 1u);
    o.waveform = OSC_PWM_PRC;            /* wav row: family (P2 §14) */
    o.source = 0u;                       /* mod row: width */
    o.phase = 0x00200000u;
    oscPwm_prepare(&o, 110.0f);
    o.phaseInc = o.phaseIncTarget;
    phase = o.phase;
    for (int b = 0; b < 16; b++) {
        oscPwm_render(&o, buf, N, 1.0f);
        for (uint8_t i = 0u; i < N; i++) {
            const double a = buf[i];
            const double r = recTable[o.tableOffset][(phase >> 22) & 0x3ffu];

            ab += a * r;
            aa += a * a;
            bb += r * r;
            phase += o.phaseInc;
        }
    }
    CHECK(aa > 0.0 && bb > 0.0, "prc renders signal");
    CHECK(ab / sqrt(aa * bb) > 0.99, "prc polarity matches Rec (> 0.99)");
    CHECK(o.pwm.split <= 0x80000000u, "prc width is at most half a cycle");

    /* A6 (F3): psw at width 0 is the interpolated saw; midpoints within
    ** 1 LSB of the exact table interpolation. */
    {
        OscInfo p = osc_init(OSC_TYPE_PWM, 0u);
        double worst = 0.0;

        p.waveform = OSC_PWM_PSW;
        p.source = 0u;
        oscPwm_prepare(&p, 110.0f);
        p.phaseInc = 0u;
        for (uint32_t k = 0u; k < 1024u; k += 7u) {
            const int16_t *t = sawTable[p.tableOffset];
            const double ref = (double)t[k] +
                0.5 * ((double)t[(k + 1u) & 1023u] - (double)t[k]);
            double err;

            p.phase = (k << 22) + (1u << 21);
            oscPwm_render(&p, buf, 1u, 1.0f);
            err = fabs((double)buf[0] - ref);
            if (err > worst)
                worst = err;
        }
        CHECK(worst <= 1.0, "pwm read hits table midpoints within 1 LSB (F3)");
    }

    /* A6: prc high-time changes monotonically as mod rises (it rises from
    ** 50 % toward 96 %: the pulse narrows on the negative side). */
    {
        const uint8_t widths[5] = { 0u, 32u, 64u, 96u, 127u };
        double duty[5];
        int mono = 1;

        for (int w = 0; w < 5; w++) {
            OscInfo p = osc_init(OSC_TYPE_PWM, OSC_PWM_PRC);
            unsigned pos = 0u;

            p.source = widths[w];
            oscPwm_prepare(&p, 21.5f);
            p.phase = 0u;
            p.phaseInc = 0x00200000u;      /* 2,048 samples per cycle */
            for (int b = 0; b < 64; b++) {
                oscPwm_render(&p, buf, N, 1.0f);
                for (uint8_t i = 0u; i < N; i++)
                    pos += (buf[i] > 0) ? 1u : 0u;
            }
            duty[w] = (double)pos / 2048.0;
            if (w > 0 && !(duty[w] > duty[w - 1]))
                mono = 0;
        }
        if (!(mono && duty[4] - duty[0] > 0.3))
            printf("duty %.3f %.3f %.3f %.3f %.3f\n", duty[0], duty[1],
                   duty[2], duty[3], duty[4]);
        CHECK(mono && duty[4] - duty[0] > 0.3,
              "prc width changes monotonically with mod");
    }
}


/* ------------------------------------------------------ FM depth ramp */

/*
 * S085 P2 F-A: the Drum fm modulator block is scaled from fmRampFrom to 1
 * across the block, so the carrier's FM depth glides from the previous
 * block's fmMod to the new one instead of jumping its read phase.
 */
static void ref_ramp_mod(int16_t *m, float from, uint8_t n)
{
    const float inv = 1.0f / (float)(n - 1u);
    const float span = 1.0f - from;

    for (uint8_t i = 0u; i < n; i++) {
        const float g = from + ((float)i * inv) * span;
        m[i] = (int16_t)(int32_t)((float)m[i] * g);
    }
}

static void test_fm_depth_ramp(void)
{
    const float amt = 0.8f, depthOld = 0.15f, depthNew = 0.8f;
    const float from = depthOld / depthNew;

    /* 1. exact wiring: modulator scaled by the written-out ramp, carrier
    **    FM'd by it. */
    {
        OscInfo car = osc_init(OSC_TYPE_WAV, SINE), mod = osc_init(OSC_TYPE_WAV, SINE);
        OscInfo cref, mref;
        int16_t buf[N], mb[N], rb[N], rmb[N];

        car.phaseInc = 0x00280000u;     /* low carrier */
        mod.phaseInc = 0x00500000u;
        mod.phase = 0x40000000u;        /* modulator near its peak */
        car.fmMod = depthNew;
        cref = car;
        mref = mod;
        oscSection_renderDrumRamped(&car, &mod, OSC_MOD_FM, amt, buf, mb, N,
                                    0, 0, from);
        calcNextOscSampleBlock(&mref, rmb, N, amt);
        ref_ramp_mod(rmb, from, N);
        calcNextOscSampleFmBlock(&cref, rmb, rb, N, 1.0f);
        CHECK(same_block(mb, rmb, N) && same_block(buf, rb, N),
              "fm depth ramp scales the modulator block from old/new to 1");
    }

    /* 2. a flat ramp leaves the block unchanged. */
    {
        OscInfo car = osc_init(OSC_TYPE_WAV, SINE), mod = osc_init(OSC_TYPE_WAV, SINE);
        OscInfo c2, m2;
        int16_t a[N], b[N], ma[N], mbb[N];

        car.phaseInc = 0x00280000u;
        mod.phaseInc = 0x00500000u;
        car.fmMod = depthNew;
        c2 = car;
        m2 = mod;
        oscSection_renderDrumRamped(&car, &mod, OSC_MOD_FM, amt, a, ma, N,
                                    0, 0, 1.0f);
        oscSection_renderDrum(&c2, &m2, OSC_MOD_FM, amt, b, mbb, N, 0, 0);
        CHECK(same_block(a, b, N) && same_block(ma, mbb, N),
              "fm depth ramp from 1 is the unramped render");
    }

    /* 3. with an itp copy the ramp is forced flat (the copy keeps its own
    **    depth and must see the unscaled modulator). */
    {
        OscInfo car = osc_init(OSC_TYPE_WAV, SINE), mod = osc_init(OSC_TYPE_WAV, SINE);
        OscInfo old, mref;
        int16_t buf[N], mb[N], ob[N], rmb[N];

        car.phaseInc = 0x00280000u;
        mod.phaseInc = 0x00500000u;
        mod.phase = 0x40000000u;
        car.fmMod = depthNew;
        old = car;
        old.fmMod = depthOld;
        mref = mod;
        oscSection_renderDrumRamped(&car, &mod, OSC_MOD_FM, amt, buf, mb, N,
                                    &old, ob, from);
        calcNextOscSampleBlock(&mref, rmb, N, amt);
        CHECK(same_block(mb, rmb, N),
              "fm depth ramp is flat while an itp crossfade is active");
    }

    /* 4. no step: block 1 at the old depth, block 2 at the new depth, over
    **    a sweep of modulator and carrier phases. With the ramp the boundary
    **    step is never larger than the ramped block's own largest step;
    **    without it the largest boundary step is several times larger. */
    {
        int32_t worstFlat = 0, worstRamp = 0, worstExcess = 0;

        for (uint32_t mp = 0u; mp < 16u; mp++) {
            for (uint32_t cp = 0u; cp < 8u; cp++) {
                int32_t bound[2] = { 0, 0 }, inside = 0;

                for (int ramp = 0; ramp < 2; ramp++) {
                    OscInfo car = osc_init(OSC_TYPE_WAV, SINE);
                    OscInfo mod = osc_init(OSC_TYPE_WAV, SINE);
                    int16_t b1[N], b2[N], mb[N];
                    int32_t d;

                    car.phaseInc = 0x00280000u;
                    car.phase = cp << 29;
                    mod.phaseInc = 0x00500000u;
                    mod.phase = mp << 28;
                    car.fmMod = depthOld;
                    oscSection_renderDrum(&car, &mod, OSC_MOD_FM, amt, b1, mb,
                                          N, 0, 0);
                    car.fmMod = depthNew;
                    oscSection_renderDrumRamped(&car, &mod, OSC_MOD_FM, amt, b2,
                                                mb, N, 0, 0,
                                                ramp ? from : 1.0f);
                    d = (int32_t)b2[0] - (int32_t)b1[N - 1u];
                    bound[ramp] = (d < 0) ? -d : d;
                    if (ramp)
                        inside = max_step(b2 + 1, (uint8_t)(N - 1u), b2[0]);
                }
                if (bound[0] > worstFlat)
                    worstFlat = bound[0];
                if (bound[1] > worstRamp)
                    worstRamp = bound[1];
                if (bound[1] - inside > worstExcess)
                    worstExcess = bound[1] - inside;
            }
        }
        printf("fm depth ramp: worst boundary step %d (unramped %d); "
               "boundary above the block's own largest step by %d\n",
               (int)worstRamp, (int)worstFlat, (int)worstExcess);
        CHECK(worstFlat > 8000, "fm depth jump test is sensitive (unramped step)");
        CHECK(worstExcess <= 3 && worstRamp * 4 < worstFlat,   /* 3 LSB: int16 truncation */
              "fm depth ramp removes the boundary step");
    }
}

int main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "all";
    const int all = (strcmp(mode, "all") == 0);

    if (all || strcmp(mode, "run") == 0) {
        test_slew();
        test_itp_click();
        test_pool();
        test_snc_identity();
        test_snc_reset_index();
        test_snc_itp_copy();
        test_snc_and_rng();
        test_fm_depth_ramp();
    }
    if (all || strcmp(mode, "sample") == 0)
        test_sample();
    if (all || strcmp(mode, "wtb") == 0)
        test_wtb();
    if (all || strcmp(mode, "pwm") == 0)
        test_pwm();
    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
