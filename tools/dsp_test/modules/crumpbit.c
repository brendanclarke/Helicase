/*
 * CrumpBit host tests (S091 P1 reductions; S091 P2 expansion).
 *
 * What:       compiles the real Core/DSP/Effects/CrumpBit/CrumpBitEffect.c
 *             and its generated tables (firmware includes stripped by
 *             extract.py) and checks:
 *               CB1  Sync division: crumpBit_divisionFor() and
 *                    crumpBit_syncDivision() against the 128-step S074 walk
 *                    (generalised to a live maximum, S091 P2), for every
 *                    rate at BPM 0..1000 and five share limits, every exact
 *                    division length (ties), and random targets/limits;
 *               CB2  8-bit ADC: crumpBit_adc() against the clamped S074 form
 *                    at every code edge (+-8 ulps), across +-3 and at large
 *                    magnitudes;
 *               CB3  code-difference DAC: crumpBit_codeToFloat(c - p) equals
 *                    the S074 (c - 128)/128 - (p - 128)/128 for every pair;
 *               CB4  whole block: crumpBit_process() against the reference
 *                    below, written from the S091 P2 equations with the
 *                    S074 float AC memory and STORED float ramp origins
 *                    (which the firmware recomputes from raw bytes), over
 *                    60 s of random blocks with parameter jumps (every row,
 *                    every filter type), out-of-range input, mono
 *                    io->r == NULL, short blocks and share changes (tail
 *                    kept or re-seated); outputs, loop bytes and state are
 *                    compared bit for bit, and the seat invariants
 *                    (delay <= length - 2, write_pos < length,
 *                    valid <= length) are checked after every block;
 *               CB5  init: AC memory 128, tone flat, filter `off` (and its
 *                    ramp origin `off`);
 *               CB6  tables and Rate limit: crumpBit_rateTable endpoints
 *                    and strict order, crumpBit_rateSamples() is the table,
 *                    crumpBit_rateLimit() equals a linear scan for every
 *                    share 0..CRUMPBIT_LOOP_MAX_BYTES in 32-byte steps (127
 *                    for the whole arena), the effective_max op clamps only
 *                    the Rate; toneK[63] == 1; filterG[0] == 0, increasing;
 *               CB7  output resonance bound: crumpBit_filterDamping() is in
 *                    [0.0625, 1] for every `res` (Q <= 8), 1 at res 0;
 *               CB8  stability at fbk 127 (gain 1.2): 60 s of
 *                    self-oscillation after a burst, with DC input, silence
 *                    and full-scale input; no NaN/Inf, |output| < 4, and
 *                    no DC lock-up (the loop's mean code stays within
 *                    +-8 LSB of silence);
 *               CB9  row ownership: CRUMPBIT_ROWS_SHARED and
 *                    CRUMPBIT_ROWS_DELAY are disjoint and cover rows
 *                    3..COUNT-1 (the firmware also asserts this at compile
 *                    time; the host law keeps the test honest).
 * Why:        the board cannot report output sample by sample; the
 *             reference proves the code computes exactly the documented
 *             equations (including that the ramp-origin trim is exact and
 *             that a flat tone and a settled `off` filter pass bit-exactly),
 *             and the ARM shape is gated on the linked ELF
 *             (crumpbit_elf_check).
 * Inputs:     build/gen/cb_*.c|h, CrumpBitEffect.host.h/.c (extract.py).
 * Outputs:    one line per failure (first 40); "crumpbit failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test crumpbit (four break builds follow).
 * Affiliates: CrumpBitEffect.c/.h; CrumpBitTables.c; CrumpBitParameters.h;
 *             StepScale.c; S091_P2_Implementation.md §5.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Host substitutes for firmware-only pieces ------------------------------ */

/* __USAT: the ARM instruction clamps a signed value to [0, 2^bits - 1]. */
static inline uint32_t host_usat(int32_t v, unsigned bits)
{
    const int32_t hi = (int32_t)((1u << bits) - 1u);

    return (uint32_t)(v < 0 ? 0 : (v > hi ? hi : v));
}
#define __USAT(v, b) host_usat((int32_t)(v), (b))

#define EFFECT_COMMON_PARAM_AUDIO_OUT 0u
#define EFFECT_COMMON_PARAM_LEVEL     1u
#define EFFECT_COMMON_PARAM_PAN       2u
#define EFFECT_COMMON_PARAM_COUNT     3u

/* seq_getBpm(): the test sets the tempo the DSP reads. */
static uint16_t cb_bpm = 120u;
uint16_t seq_getBpm(void) { return cb_bpm; }

#include "cb_stepscale.h"
#include "cb_stepscale.c"
#include "cb_fxbuf.c"
#include "cb_effects.c"
#include "cb_params.c"
#include "cb_tables.h"
#include "cb_tables.c"
#include "CrumpBitEffect.host.h"
#include "CrumpBitEffect.host.c"

/* S074 reference pieces that still define today's behaviour -------------- */

/* The S074 ADC (CrumpBitEffect.c at 93f35799), for CB2. */
static inline uint32_t ref_adc(float x)
{
    uint32_t code;

    x = (x > 1.0f) ? 1.0f : x;
    x = (x < -1.0f) ? -1.0f : x;
    code = (uint32_t)(x * 128.0f + 128.5f);
    return code - (code >> 8);
}

/*
 * The S074 128-step Sync walk with its constant limit replaced by max (the
 * only S091 P2 change to the rule), for CB1 and the CB4 reference.
 */
static uint8_t ref_divisionFor(float target, float per_tick, float max)
{
    uint8_t low = CRUMPBIT_DIVISION_NONE;
    uint8_t high = CRUMPBIT_DIVISION_NONE;
    float t_low = 0.0f;
    float t_high = 0.0f;
    uint8_t i;

    for (i = 0u; i < STEP_SCALE_COUNT; i++) {
        const float t = ((float)stepScale_ticksQ8(i) / 256.0f) * per_tick;

        if (t > max)
            continue;
        if (t <= target) {
            low = i;
            t_low = t;
        } else if (high == CRUMPBIT_DIVISION_NONE) {
            high = i;
            t_high = t;
        }
    }
    if (low == CRUMPBIT_DIVISION_NONE)
        return (high == CRUMPBIT_DIVISION_NONE) ? 0u : high;
    if (high == CRUMPBIT_DIVISION_NONE)
        return low;
    return (target * target < t_low * t_high) ? low : high;
}

/* Rate limit by linear scan: the largest rate whose length fits. */
static uint8_t ref_rateLimit(uint32_t bytes)
{
    uint32_t len = (bytes > CRUMPBIT_LOOP_MAX_BYTES)
        ? CRUMPBIT_LOOP_MAX_BYTES : bytes;
    const float max = (len < CRUMPBIT_LOOP_MIN) ? 0.0f : (float)(len - 2u);
    uint8_t r = 0u;
    uint8_t i;

    for (i = 0u; i < 128u; i++)
        if (crumpBit_rateTable[i] <= max)
            r = i;
    return r;
}

/*
 * S091 P2 reference process, written from the plan's equations
 * (S091_P2_CrumpBit_Expanded_Plan.md §5) in the firmware's operation order.
 * Deliberate differences from the firmware form, each proving one claim:
 * - AC input memory is the S074 float (ac_x = (code - 128)/128), not a code;
 * - ramp origins (mix, fb, gain_l, gain_r and the output mix cx/cb/cl) are
 *   STORED floats, as S074 stored them, not recomputed from raw bytes;
 * - the tone output is chosen by branch (LP s, flat v, HP v - s), not by the
 *   (a, b) weights;
 * - the filter mix uses its own coefficient table.
 */
typedef struct {
    uint32_t length;
    uint32_t write_pos;
    uint32_t valid;
    float delay;
    float mix;
    float feedback;
    float gain_l;
    float gain_r;
    float ac_x_l;
    float ac_x_r;
    float ac_y_l;
    float ac_y_r;
    float dc_x;
    float dc_y;
    float tone_s;
    float s_l1;
    float s_l2;
    float s_r1;
    float s_r2;
    float cx;
    float cb;
    float cl;
    uint8_t bit_off;
    uint8_t bit_invert;
    uint8_t mix_raw;
    uint8_t feedback_raw;
    uint8_t rate;
    uint8_t sync;
    uint8_t pan_raw;
    uint8_t tone_raw;
    uint8_t filter_type;
    uint8_t filter_freq;
    uint8_t filter_reso;
    uint8_t primed;
} RefP2;

static void ref2_mix(uint8_t type, float r, float c[3])
{
    static const float table[8][3] = {
        { 0.0f, 0.0f, 1.0f },    /* LP   (k entries patched below) */
        { 1.0f, 0.0f, -1.0f },   /* HP:  cb = -k */
        { 0.0f, 1.0f, 0.0f },    /* BP */
        { 0.0f, 0.0f, 0.0f },    /* UBP: cb = k */
        { 1.0f, 0.0f, 0.0f },    /* Nch: cb = -k */
        { -1.0f, 0.0f, 2.0f },   /* Pek: cb = k */
        { 0.0f, 0.0f, 1.0f },    /* LP2 = LP */
        { 1.0f, 0.0f, 0.0f },    /* off */
    };
    const float k = 2.0f * r;
    const uint8_t t = (type > 7u) ? 7u : type;

    c[0] = table[t][0];
    c[1] = table[t][1];
    c[2] = table[t][2];
    if (t == 1u || t == 4u)
        c[1] = -k;
    if (t == 3u || t == 5u)
        c[1] = k;
}

static float ref2_damping(uint8_t reso)
{
    const float q = 1.0f - (float)reso * (1.0f / 127.0f);

    return (q < 0.1f) ? 0.0625f : q;
}

static float ref2_fbGain(uint8_t v)
{
    return (v <= 100u) ? (float)v * (1.0f / 100.0f)
                       : 1.0f + (float)(v - 100u) * (0.2f / 27.0f);
}

static void ref2_init(RefP2 *rt)
{
    float c[3];

    memset(rt, 0, sizeof *rt);
    rt->tone_raw = 63u;
    rt->filter_type = 7u;
    ref2_mix(7u, ref2_damping(0u), c);
    rt->cx = c[0];
    rt->cb = c[1];
    rt->cl = c[2];
}

static void ref2_writeParam(RefP2 *rt, uint8_t index, uint8_t value)
{
    switch (index) {
    case CRUMPBIT_PARAM_BIT_OFF:     rt->bit_off = value;               break;
    case CRUMPBIT_PARAM_BIT_INVERT:  rt->bit_invert = value;            break;
    case CRUMPBIT_PARAM_MIX:         rt->mix_raw = value;               break;
    case CRUMPBIT_PARAM_FEEDBACK:    rt->feedback_raw = value;          break;
    case CRUMPBIT_PARAM_RATE:        rt->rate = value;                  break;
    case CRUMPBIT_PARAM_SYNC:        rt->sync = (uint8_t)(value != 0u); break;
    case CRUMPBIT_PARAM_DLY_PAN:     rt->pan_raw = value;               break;
    case CRUMPBIT_PARAM_TONE:        rt->tone_raw = value;              break;
    case CRUMPBIT_PARAM_FILTER_TYPE: rt->filter_type = value;           break;
    case CRUMPBIT_PARAM_FILTER_FREQ: rt->filter_freq = value;           break;
    case CRUMPBIT_PARAM_FILTER_RESO: rt->filter_reso = value;           break;
    default:                                                            break;
    }
}

static uint32_t ref2_seat(RefP2 *rt, uint32_t bytes)
{
    uint32_t len = (bytes > CRUMPBIT_LOOP_MAX_BYTES)
        ? CRUMPBIT_LOOP_MAX_BYTES : bytes;

    if (len != rt->length) {
        if (rt->length != 0u && rt->write_pos < len) {
            if (rt->valid > rt->write_pos)
                rt->valid = rt->write_pos;
        } else {
            rt->write_pos = 0u;
            rt->valid = 0u;
        }
        rt->length = len;
        if (len >= 4u && rt->delay > (float)(len - 2u))
            rt->delay = (float)(len - 2u);
    }
    return len;
}

static void ref2_process(RefP2 *rt, float *io_l, float *io_r, uint32_t frames,
                         uint8_t *base, uint32_t bytes)
{
    float *left = io_l;
    float *right = io_r ? io_r : io_l;
    int8_t *loop = (int8_t *)base;
    const uint32_t len = ref2_seat(rt, bytes);
    float flen, max, target, per_tick, free_run, synced;
    float mix, fb, gl, gr, mix_to, fb_to, gl_to, gr_to;
    float mix_inc, fb_inc, gl_inc, gr_inc, per_frame;
    float k_tone, g, r, a1, a2, a3, c_to[3], cx_inc, cb_inc, cl_inc;
    float cx, cb, cl;
    uint32_t keep, invert, wp, valid, n;
    uint8_t t;

    if (len < 4u)
        return;
    flen = (float)len;
    max = (float)(len - 2u);
    invert = rt->bit_invert;
    keep = (uint32_t)~((uint32_t)rt->bit_off & ~invert) & 0xFFu;
    per_tick = CRUMPBIT_SAMPLES_PER_TICK / (float)(cb_bpm ? cb_bpm : 1u);
    free_run = crumpBit_rateTable[rt->rate > 127u ? 127u : rt->rate];
    synced = ((float)stepScale_ticksQ8(
        ref_divisionFor(free_run, per_tick, max)) / 256.0f) * per_tick;
    target = rt->sync ? synced : free_run;
    target = (target > max) ? max : target;
    if (!rt->primed) {
        rt->delay = target;
        rt->primed = 1u;
    }
    per_frame = 1.0f / (float)frames;
    mix_to = (float)rt->mix_raw * (1.0f / 127.0f);
    fb_to = ref2_fbGain(rt->feedback_raw);
    gl_to = (rt->pan_raw <= 63u) ? 1.0f
        : (float)(127u - rt->pan_raw) * (1.0f / 64.0f);
    gr_to = (rt->pan_raw >= 63u) ? 1.0f
        : (float)rt->pan_raw * (1.0f / 63.0f);
    mix = rt->mix;
    fb = rt->feedback;
    gl = rt->gain_l;
    gr = rt->gain_r;
    mix_inc = (mix_to - mix) * per_frame;
    fb_inc = (fb_to - fb) * per_frame;
    gl_inc = (gl_to - gl) * per_frame;
    gr_inc = (gr_to - gr) * per_frame;
    t = (rt->tone_raw > 127u) ? 127u : rt->tone_raw;
    k_tone = crumpBit_toneK[t];
    g = crumpBit_filterG[rt->filter_freq > 127u ? 127u : rt->filter_freq];
    r = ref2_damping(rt->filter_reso);
    a1 = 1.0f / (1.0f + g * (g + 2.0f * r));
    a2 = g * a1;
    a3 = g * a2;
    ref2_mix(rt->filter_type, r, c_to);
    cx = rt->cx;
    cb = rt->cb;
    cl = rt->cl;
    cx_inc = (c_to[0] - cx) * per_frame;
    cb_inc = (c_to[1] - cb) * per_frame;
    cl_inc = (c_to[2] - cl) * per_frame;
    wp = rt->write_pos;
    valid = rt->valid;

    for (n = 0u; n < frames; n++) {
        const uint32_t code_l = (ref_adc(left[n]) & keep) ^ invert;
        const uint32_t code_r = (ref_adc(right[n]) & keep) ^ invert;
        const float yl = (float)((int32_t)code_l - 128) * CRUMPBIT_LSB;
        const float yr = (float)((int32_t)code_r - 128) * CRUMPBIT_LSB;
        const float hl = yl - rt->ac_x_l + CRUMPBIT_AC_R * rt->ac_y_l;
        const float hr = yr - rt->ac_x_r + CRUMPBIT_AC_R * rt->ac_y_r;
        float rp, frac, wet, dc, tilt, ret;
        uint32_t i0, i1;

        rt->ac_x_l = yl;
        rt->ac_y_l = hl;
        rt->ac_x_r = yr;
        rt->ac_y_r = hr;
        mix += mix_inc;
        fb += fb_inc;
        gl += gl_inc;
        gr += gr_inc;
        rt->delay += (target - rt->delay) * CRUMPBIT_GLIDE_K;
        rp = (float)wp - rt->delay;
        rp += (rp < 0.0f) ? flen : 0.0f;
        i0 = (uint32_t)rp;
        frac = rp - (float)i0;
        i0 -= (i0 >= len) ? len : 0u;
        i1 = i0 + 1u;
        i1 -= (i1 >= len) ? len : 0u;
        wet = ((float)loop[i0] +
               frac * (float)(loop[i1] - loop[i0])) * CRUMPBIT_LSB;
        wet = ((rt->delay + 2.0f) <= (float)valid) ? wet : 0.0f;
        dc = wet - rt->dc_x + CRUMPBIT_DC_R * rt->dc_y;
        rt->dc_x = wet;
        rt->dc_y = dc;
        rt->tone_s += k_tone * (dc - rt->tone_s);
        if (t < 63u)
            tilt = rt->tone_s;
        else if (t == 63u)
            tilt = dc;
        else
            tilt = dc - rt->tone_s;
        ret = fb * tilt;
        ret = (ret > 1.5f) ? 1.5f : ret;
        ret = (ret < -1.5f) ? -1.5f : ret;
        ret = ret * (1.0f - (4.0f / 27.0f) * ret * ret);
        loop[wp] = (int8_t)((int32_t)ref_adc(0.5f * (hl + hr) + ret) - 128);
        wp++;
        wp -= (wp >= len) ? len : 0u;
        valid += (valid < len) ? 1u : 0u;
        right[n] = hr + mix * (gr * wet - hr);
        left[n] = hl + mix * (gl * wet - hl);
    }
    for (n = 0u; n < frames; n++) {
        const float xl = left[n];
        const float xr = right[n];
        float v1, v2, v3, yl, yr;

        cx += cx_inc;
        cb += cb_inc;
        cl += cl_inc;
        v3 = xl - rt->s_l2;
        v1 = a1 * rt->s_l1 + a2 * v3;
        v2 = rt->s_l2 + a2 * rt->s_l1 + a3 * v3;
        rt->s_l1 = 2.0f * v1 - rt->s_l1;
        rt->s_l2 = 2.0f * v2 - rt->s_l2;
        yl = cx * xl + cb * v1 + cl * v2;
        v3 = xr - rt->s_r2;
        v1 = a1 * rt->s_r1 + a2 * v3;
        v2 = rt->s_r2 + a2 * rt->s_r1 + a3 * v3;
        rt->s_r1 = 2.0f * v1 - rt->s_r1;
        rt->s_r2 = 2.0f * v2 - rt->s_r2;
        yr = cx * xr + cb * v1 + cl * v2;
        right[n] = yr;
        left[n] = yl;
    }
    rt->write_pos = wp;
    rt->valid = valid;
    rt->mix = mix_to;
    rt->feedback = fb_to;
    rt->gain_l = gl_to;
    rt->gain_r = gr_to;
    rt->cx = c_to[0];
    rt->cb = c_to[1];
    rt->cl = c_to[2];
}

/* Test plumbing ----------------------------------------------------------- */

static int failures;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        if (failures < 40)
            printf("FAIL: %s\n", msg);
        failures++;
    }
}

static uint32_t rng_state = 0x5091u;

static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static float rndf(float lo, float hi)
{
    return lo + (hi - lo) * ((float)(rnd() & 0xFFFFFFu) / 16777215.0f);
}

static int same_bits(float a, float b)
{
    uint32_t x;
    uint32_t y;

    memcpy(&x, &a, 4u);
    memcpy(&y, &b, 4u);
    return x == y;
}

/* Shares used by CB1 and CB4: whole arena, 1, 2, 6 and 12 units, odd. */
static const uint32_t cb_shares[] = {
    CRUMPBIT_LOOP_MAX_BYTES, 122272u, 117856u, 100192u,
    CRUMPBIT_LOOP_MIN_SHARE_BYTES, 40000u, 64u
};
#define CB_SHARE_COUNT (sizeof cb_shares / sizeof cb_shares[0])

/* CB1: the Sync division ------------------------------------------------- */
static void law_cb1(void)
{
    uint32_t bpm;
    uint32_t i;
    uint32_t s;
    char msg[96];

    for (s = 0u; s < 5u; s++) {
        fx_share_t share = { NULL, 0u, cb_shares[s] };
        const float max = crumpBit_shareMaxSamples(&share);

        for (bpm = 0u; bpm <= 1000u; bpm++) {
            const float per_tick =
                CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);
            uint32_t r;

            for (r = 0u; r < 128u; r++) {
                const float t = crumpBit_rateSamples((uint8_t)r);

                snprintf(msg, sizeof msg, "CB1 share %u rate %u bpm %u",
                         s, r, bpm);
                expect(crumpBit_divisionFor(t, per_tick, max) ==
                       ref_divisionFor(t, per_tick, max), msg);
                expect(crumpBit_syncDivision((uint8_t)r, (uint16_t)bpm,
                                             max) ==
                       ref_divisionFor(t, per_tick, max), msg);
            }
            for (i = 0u; i < STEP_SCALE_COUNT; i++) {
                const float t = ((float)stepScale_ticksQ8((uint8_t)i) /
                                 256.0f) * per_tick;
                const float probe[3] = { t, nextafterf(t, 0.0f),
                                         nextafterf(t, INFINITY) };
                uint32_t k;

                for (k = 0u; k < 3u; k++) {
                    snprintf(msg, sizeof msg,
                             "CB1 tie share %u cc %u bpm %u k %u",
                             s, i, bpm, k);
                    expect(crumpBit_divisionFor(probe[k], per_tick, max) ==
                           ref_divisionFor(probe[k], per_tick, max), msg);
                }
            }
        }
    }
    for (i = 0u; i < 2000000u; i++) {
        const float per_tick = rndf(0.4f, 30000.0f);
        const float max = rndf(0.0f, CRUMPBIT_DELAY_CURVE_MAX);
        const float t = (i & 1u) ? rndf(0.0f, 2.0f * max)
                                 : max * rndf(0.999f, 1.001f);

        snprintf(msg, sizeof msg, "CB1 random %u", i);
        expect(crumpBit_divisionFor(t, per_tick, max) ==
               ref_divisionFor(t, per_tick, max), msg);
    }
}

/* CB2: the 8-bit ADC ----------------------------------------------------- */
static void law_cb2(void)
{
    uint32_t i;
    int32_t k;
    char msg[96];

    for (k = -2; k <= 258; k++) {
        float x = ((float)k - 128.5f) / 128.0f;
        int32_t u;

        for (u = 0; u < 8; u++)
            x = nextafterf(x, -INFINITY);
        for (u = 0; u < 17; u++) {
            snprintf(msg, sizeof msg, "CB2 edge %d step %d", k, u);
            expect(crumpBit_adc(x) == ref_adc(x), msg);
            x = nextafterf(x, INFINITY);
        }
    }
    for (i = 0u; i < 4000000u; i++) {
        const float x = rndf(-3.0f, 3.0f);

        snprintf(msg, sizeof msg, "CB2 sweep %u", i);
        expect(crumpBit_adc(x) == ref_adc(x), msg);
    }
    {
        static const float big[] = { 1.0f, -1.0f, 0.0f, -0.0f, 6.0f, -6.0f,
                                     1.0e6f, -1.0e6f, 1.6e7f, -1.6e7f };
        for (i = 0u; i < sizeof big / sizeof big[0]; i++) {
            snprintf(msg, sizeof msg, "CB2 big %g", (double)big[i]);
            expect(crumpBit_adc(big[i]) == ref_adc(big[i]), msg);
        }
    }
}

/* CB3: code-difference DAC is exact -------------------------------------- */
static void law_cb3(void)
{
    int32_t c;
    int32_t p;
    char msg[96];

    for (c = 0; c < 256; c++)
        for (p = 0; p < 256; p++) {
            const float want = (float)(c - 128) * CRUMPBIT_LSB -
                               (float)(p - 128) * CRUMPBIT_LSB;

            snprintf(msg, sizeof msg, "CB3 c %d p %d", c, p);
            expect(same_bits(crumpBit_codeToFloat(c - p), want), msg);
        }
}

/* CB4 + CB5: the whole block against the P2 reference -------------------- */
#define CB_ARENA CRUMPBIT_LOOP_MAX_BYTES

static uint8_t arena_new[CB_ARENA];
static uint8_t arena_ref[CB_ARENA];

/* Write one row to both runtimes; Rate goes through the share clamp. */
static void cb4_write(CrumpBitRuntime *rt, RefP2 *ref, uint8_t row,
                      uint8_t v, const fx_share_t *share)
{
    if (row == CRUMPBIT_PARAM_RATE) {
        const uint8_t lim = crumpBit_ops.effective_max(row, share);

        expect(lim == ref_rateLimit(share->bytes), "CB4 rate limit");
        v = (v > lim) ? lim : v;
    }
    crumpBit_ops.write_param(rt, row, v);
    ref2_writeParam(ref, row, v);
}

static void law_cb4(void)
{
    static const uint8_t rows[] = {
        CRUMPBIT_PARAM_BIT_OFF, CRUMPBIT_PARAM_BIT_INVERT,
        CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK, CRUMPBIT_PARAM_RATE,
        CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_DLY_PAN, CRUMPBIT_PARAM_TONE,
        CRUMPBIT_PARAM_FILTER_TYPE, CRUMPBIT_PARAM_FILTER_FREQ,
        CRUMPBIT_PARAM_FILTER_RESO };
    CrumpBitRuntime rt;
    RefP2 ref;
    fx_share_t share;
    float l_new[32], r_new[32], l_ref[32], r_ref[32];
    uint8_t rate_intended = 64u;
    uint32_t block;
    uint32_t share_i = 0u;
    char msg[128];

    memset(arena_new, 0x55, sizeof arena_new);
    memcpy(arena_ref, arena_new, sizeof arena_ref);
    memset(&rt, 0, sizeof rt);
    crumpBit_ops.init(&rt, NULL);
    ref2_init(&ref);
    expect(rt.ac_c_l == 128 && rt.ac_c_r == 128, "CB5 AC memory seeded 128");
    expect(rt.tone_raw == CRUMPBIT_TONE_FLAT, "CB5 tone flat");
    expect(rt.filter_type == CRUMPBIT_FILTER_OFF &&
           rt.filter_type_prev == CRUMPBIT_FILTER_OFF, "CB5 filter off");
    share.base = arena_new;
    share.offset = 0u;
    share.bytes = cb_shares[0];
    cb4_write(&rt, &ref, CRUMPBIT_PARAM_FEEDBACK, 64u, &share);
    cb4_write(&rt, &ref, CRUMPBIT_PARAM_RATE, rate_intended, &share);
    cb4_write(&rt, &ref, CRUMPBIT_PARAM_DLY_PAN, 63u, &share);

    for (block = 0u; block < 82800u; block++) {    /* 60 s at 32 frames */
        effect_io_t io;
        const uint32_t mono = ((block / 997u) % 7u) == 3u;
        const uint32_t frames = ((block % 211u) == 5u)
            ? 1u + (rnd() % 32u) : 32u;
        const uint32_t kind = (block / 400u) % 5u;
        uint32_t n;

        if ((block % 13u) == 0u || (block % 1000u) < 40u) {
            const uint8_t row = rows[rnd() % (sizeof rows)];
            uint8_t v = (uint8_t)(rnd() & 0x7Fu);

            if (row == CRUMPBIT_PARAM_BIT_OFF ||
                row == CRUMPBIT_PARAM_BIT_INVERT)
                v = ((rnd() & 3u) == 0u) ? (uint8_t)rnd() : 0u;
            if (row == CRUMPBIT_PARAM_FILTER_TYPE)
                v = (uint8_t)(rnd() & 7u);
            if (row == CRUMPBIT_PARAM_SYNC)
                v = (uint8_t)(rnd() & 1u);
            if (row == CRUMPBIT_PARAM_TONE && (rnd() & 3u) == 0u)
                v = CRUMPBIT_TONE_FLAT;
            if (row == CRUMPBIT_PARAM_RATE)
                rate_intended = v;
            cb4_write(&rt, &ref, row, v, &share);
        }
        if ((block % 5000u) == 2500u)
            cb_bpm = (uint16_t)(20u + rnd() % 280u);
        /* Share changes (voice units claimed or released), then the
         * manager's forced re-resolution re-clamps the retained Rate. */
        if ((block % 3001u) == 3000u) {
            share_i = (share_i + 1u + rnd() % 3u) % CB_SHARE_COUNT;
            share.bytes = cb_shares[share_i];
            crumpBit_ops.buffer_changed(&rt, &share);
            (void)ref2_seat(&ref, share.bytes);
            cb4_write(&rt, &ref, CRUMPBIT_PARAM_RATE, rate_intended, &share);
        }
        for (n = 0u; n < frames; n++) {
            float a;
            float b;

            switch (kind) {
            case 0: a = rndf(-1.0f, 1.0f); b = rndf(-1.0f, 1.0f); break;
            case 1: a = 0.0f; b = 0.0f; break;
            case 2: a = rndf(-3.0f, 3.0f); b = rndf(-3.0f, 3.0f); break;
            case 3: a = 1.0f; b = -1.0f; break;
            default: a = 0.25f * sinf((float)(block * 32u + n) * 0.01f);
                     b = a * 0.5f; break;
            }
            l_new[n] = a; r_new[n] = b; l_ref[n] = a; r_ref[n] = b;
        }
        io.l = l_new; io.r = mono ? NULL : r_new;
        io.frames = (uint8_t)frames; io.channels = mono ? 1u : 2u;
        io.share = &share;
        crumpBit_ops.process(&rt, &io);
        ref2_process(&ref, l_ref, mono ? NULL : r_ref, frames, arena_ref,
                     share.bytes);

        for (n = 0u; n < frames; n++) {
            snprintf(msg, sizeof msg, "CB4 block %u frame %u", block, n);
            expect(same_bits(l_new[n], l_ref[n]), msg);
            if (!mono)
                expect(same_bits(r_new[n], r_ref[n]), msg);
        }
        snprintf(msg, sizeof msg, "CB4 state block %u", block);
        expect(rt.length == ref.length && rt.write_pos == ref.write_pos &&
               rt.valid == ref.valid && same_bits(rt.delay, ref.delay) &&
               same_bits(rt.ac_y_l, ref.ac_y_l) &&
               same_bits(rt.ac_y_r, ref.ac_y_r) &&
               same_bits((float)(rt.ac_c_l - 128) * CRUMPBIT_LSB,
                         ref.ac_x_l) &&
               same_bits((float)(rt.ac_c_r - 128) * CRUMPBIT_LSB,
                         ref.ac_x_r) &&
               same_bits(rt.dc_x, ref.dc_x) && same_bits(rt.dc_y, ref.dc_y) &&
               same_bits(rt.tone_s, ref.tone_s) &&
               same_bits(rt.svf_l1, ref.s_l1) &&
               same_bits(rt.svf_l2, ref.s_l2) &&
               same_bits(rt.svf_r1, ref.s_r1) &&
               same_bits(rt.svf_r2, ref.s_r2), msg);
        snprintf(msg, sizeof msg, "CB4 seat invariant block %u", block);
        expect(rt.length < CRUMPBIT_LOOP_MIN ||
               (rt.delay <= (float)(rt.length - 2u) &&
                rt.write_pos < rt.length && rt.valid <= rt.length), msg);
    }
    expect(memcmp(arena_new, arena_ref, sizeof arena_new) == 0,
           "CB4 loop bytes identical after 60 s");
}

/* CB6: tables and the Rate limit ----------------------------------------- */
static void law_cb6(void)
{
    uint32_t i;
    uint32_t bytes;
    char msg[96];

    expect(crumpBit_rateTable[0] == 882.0f, "CB6 rate[0] = 882");
    expect(crumpBit_rateTable[127] == CRUMPBIT_DELAY_CURVE_MAX,
           "CB6 rate[127] = curve max");
    for (i = 0u; i < 128u; i++) {
        snprintf(msg, sizeof msg, "CB6 rate %u", i);
        expect(same_bits(crumpBit_rateSamples((uint8_t)i),
                         crumpBit_rateTable[i]), msg);
        if (i)
            expect(crumpBit_rateTable[i] > crumpBit_rateTable[i - 1u], msg);
    }
    expect(crumpBit_rateSamples(200u) == crumpBit_rateTable[127],
           "CB6 rate > 127 reads the top");
    for (bytes = 0u; bytes <= CRUMPBIT_LOOP_MAX_BYTES + 64u; bytes += 32u) {
        fx_share_t share = { NULL, 0u, bytes };

        snprintf(msg, sizeof msg, "CB6 limit bytes %u", bytes);
        expect(crumpBit_rateLimit(&share) == ref_rateLimit(bytes), msg);
        expect(crumpBit_ops.effective_max(CRUMPBIT_PARAM_RATE, &share) ==
               ref_rateLimit(bytes), msg);
        expect(crumpBit_ops.effective_max(CRUMPBIT_PARAM_MIX, &share) ==
               255u, msg);
    }
    {
        fx_share_t full = { NULL, 0u, CRUMPBIT_LOOP_MAX_BYTES };

        expect(crumpBit_rateLimit(&full) == 127u, "CB6 whole arena = 127");
    }
    expect(crumpBit_toneK[CRUMPBIT_TONE_FLAT] == 1.0f, "CB6 toneK flat");
    expect(crumpBit_filterG[0] == 0.0f, "CB6 filterG[0] = 0");
    for (i = 1u; i < 128u; i++) {
        snprintf(msg, sizeof msg, "CB6 tables %u", i);
        expect(crumpBit_filterG[i] > crumpBit_filterG[i - 1u], msg);
        expect(crumpBit_toneK[i] > 0.0f && crumpBit_toneK[i] <= 1.0f, msg);
    }
}

/* CB7: output resonance bound -------------------------------------------- */
static void law_cb7(void)
{
    uint32_t v;
    char msg[64];

    expect(crumpBit_filterDamping(0u) == 1.0f, "CB7 res 0 = R 1");
    for (v = 0u; v < 128u; v++) {
        const float r = crumpBit_filterDamping((uint8_t)v);

        snprintf(msg, sizeof msg, "CB7 res %u", v);
        expect(r >= CRUMPBIT_FILTER_R_MIN && r <= 1.0f, msg);
    }
}

/* CB8: stability at the maximum feedback ---------------------------------- */
static void law_cb8(void)
{
    uint32_t pass;
    char msg[96];

    for (pass = 0u; pass < 3u; pass++) {
        CrumpBitRuntime rt;
        fx_share_t share = { arena_new, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        float l[32], r[32];
        float peak = 0.0f;
        int finite = 1;
        int64_t sum = 0;
        uint32_t block;
        uint32_t i;

        memset(arena_new, 0, sizeof arena_new);
        memset(&rt, 0, sizeof rt);
        crumpBit_ops.init(&rt, NULL);
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_FEEDBACK, 127u);
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_MIX, 127u);
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_DLY_PAN, 63u);
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_RATE, 20u);
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_TONE,
                                 (uint8_t)(40u + 40u * pass));
        for (block = 0u; block < 82800u; block++) {
            effect_io_t io = { l, r, 32u, 2u, &share };
            uint32_t n;

            for (n = 0u; n < 32u; n++) {
                /* A 0.1 s burst, then the pass's steady input. */
                const float burst = (block < 138u) ? rndf(-1.0f, 1.0f) : 0.0f;
                const float steady = (pass == 0u) ? 0.0f
                                   : (pass == 1u) ? 0.5f : rndf(-1.0f, 1.0f);

                l[n] = burst + steady;
                r[n] = burst - steady;
            }
            crumpBit_ops.process(&rt, &io);
            for (n = 0u; n < 32u; n++) {
                finite &= isfinite(l[n]) && isfinite(r[n]);
                peak = fmaxf(peak, fmaxf(fabsf(l[n]), fabsf(r[n])));
            }
        }
        for (i = 0u; i < rt.length; i++)
            sum += (int8_t)arena_new[i];
        snprintf(msg, sizeof msg, "CB8 pass %u finite", pass);
        expect(finite, msg);
        snprintf(msg, sizeof msg, "CB8 pass %u peak %.3f < 4", pass,
                 (double)peak);
        expect(peak < 4.0f, msg);
        snprintf(msg, sizeof msg, "CB8 pass %u mean code %.3f within 8",
                 pass, (double)sum / (double)rt.length);
        expect(llabs(sum) <= (int64_t)8 * (int64_t)rt.length, msg);
        printf("CB8 pass %u: peak %.3f, mean loop code %.3f LSB\n", pass,
               (double)peak, (double)sum / (double)rt.length);
    }
}

/* CB9: row ownership ------------------------------------------------------ */
static void law_cb9(void)
{
    const uint32_t rows = CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_COUNT) -
                          CRUMPBIT_ROW_BIT(EFFECT_COMMON_PARAM_COUNT);

    expect((CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROWS_DELAY) == 0u,
           "CB9 stage masks disjoint");
    expect((CRUMPBIT_ROWS_SHARED | CRUMPBIT_ROWS_DELAY) == rows,
           "CB9 every type row has one owner");
    expect((CRUMPBIT_ROWS_DELAY & CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_TONE)) != 0u,
           "CB9 ton is a delay sub-type row");
}

int main(void)
{
    law_cb1();
    law_cb2();
    law_cb3();
    law_cb4();
    law_cb6();
    law_cb7();
    law_cb8();
    law_cb9();
    printf("crumpbit failures: %d\n", failures);
    return failures ? 1 : 0;
}
