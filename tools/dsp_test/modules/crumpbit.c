/*
 * CrumpBit host tests (S091 P1 reductions; S091 P2 expansion; S091 P3
 * sub-types).
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
 *               CB4  whole block, `dly` (sub 0): crumpBit_process() against
 *                    the reference
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
 *                    S091 P3: the sub-type rows (aux, st2, st3, spr, den,
 *                    amp, wid) are written too and must not change `dly`;
 *               CB5  init: AC memory 128, tone flat, filter `off` (and its
 *                    ramp origin `off`); S091 P3: sub `dly`, aux/st2/st3 63,
 *                    den 127;
 *               CB6  tables and Rate limit: crumpBit_rateTable endpoints
 *                    and strict order, crumpBit_rateSamples() is the table,
 *                    crumpBit_rateLimit() equals a linear scan for every
 *                    share 0..CRUMPBIT_LOOP_MAX_BYTES in 32-byte steps (127
 *                    for the whole arena), the effective_max op clamps only
 *                    the Rate; toneK[63] == 1; filterG[0] == 0, increasing;
 *                    S091 P3: every sub-type's Rate limit equals a linear
 *                    scan of its own length table under its own bound, the
 *                    effective_max op follows the runtime's sub-type (the
 *                    delay rule without a runtime), and the new tables'
 *                    endpoints and order (comb, pitch window, semitone
 *                    ratio, grain fraction, reverb size and decay);
 *               CB7  output resonance bound: crumpBit_filterDamping() is in
 *                    [0.0625, 1] for every `res` (Q <= 8), 1 at res 0;
 *               CB8  stability at fbk 127 (gain 1.2): 60 s of
 *                    self-oscillation after a burst, with DC input, silence
 *                    and full-scale input; no NaN/Inf, |output| < 4, and
 *                    no DC lock-up (the loop's mean code stays within
 *                    +-8 LSB of silence);
 *               CB9  row ownership: CRUMPBIT_ROWS_SHARED,
 *                    CRUMPBIT_ROWS_SUBTYPE and CRUMPBIT_ROWS_GRN are
 *                    disjoint and cover rows 3..COUNT-1 (the firmware also
 *                    asserts this at compile time); S091 P3 lane maps: the
 *                    nine shared lanes (lane 3 = mix) identical in every
 *                    map, the `dly` map equal to the S091 P2 table, every
 *                    pool lane a row of its own sub-type, no row twice;
 *               CB10 the MIX STAGE in every sub-type (binding): with `mix`
 *                    0 and the output filter `off`, the output equals the
 *                    stage-1 dry signal (S074 form) bit for bit, whatever
 *                    the sub-engine does;
 *               CB11 sub-type switch storm: 30 s of random `sub` changes
 *                    (every 1..50 blocks) with every row, share changes,
 *                    mono and short blocks, extreme input: finite, bounded,
 *                    the active engine follows `sub`, each engine's seat
 *                    invariants hold, no byte beyond the share changes;
 *               CB12 `cmb` tuning: the first echo of an impulse arrives at
 *                    crumpBit_combTable[rte] (+-2 samples); `snc` inverts
 *                    the second echo; combNote() is 112 - rte, held at 96;
 *               CB13 `pit` ratio: +-12 st (snapped) moves a 441 Hz sine's
 *                    energy to 882 / 220.5 Hz (Goertzel, >= 10x the input
 *                    frequency), 0 st keeps it;
 *               CB14 `grn`: the candidate distributions (spr 0 root only,
 *                    40 root/st2 halves, 80 thirds, 127 detune within +-35
 *                    cents), the level/pan bytes, start lags behind the
 *                    write head (headroom rule), freeze keeping the record
 *                    line bit-exact while the delay line runs, stability at
 *                    fbk 127;
 *               CB15 `rvb`: stable and bounded at every extreme of rte,
 *                    fbk, snc, ton and dpn; an RT60 of ~0.49 s (fbk 44)
 *                    decays 9..21 dB over 0.125 s; freeze holds the tail
 *                    within +-6 dB over 1 s.
 * Why:        the board cannot report output sample by sample; the
 *             reference proves the code computes exactly the documented
 *             equations (including that the ramp-origin trim is exact and
 *             that a flat tone and a settled `off` filter pass bit-exactly),
 *             and the ARM shape is gated on the linked ELF
 *             (crumpbit_elf_check).
 * Inputs:     build/gen/cb_*.c|h, CrumpBitEffect.host.h/.c (extract.py).
 * Outputs:    one line per failure (first 40); "crumpbit failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test crumpbit (nine break builds follow).
 * Affiliates: CrumpBitEffect.c/.h; CrumpBitTables.c; CrumpBitParameters.h;
 *             StepScale.c; S091_P2_Implementation.md §5;
 *             S091_P3_Implementation.md (host laws CB4-CB15).
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

/* GetRngValue() (random.h): the hardware word `grn` draws once per block. */
static uint32_t cb_hw_rng = 0xC0FFEEu;
int16_t GetRngValue(void)
{
    cb_hw_rng = cb_hw_rng * 1664525u + 1013904223u;
    return (int16_t)(cb_hw_rng >> 16);
}

#include "cb_stepscale.h"
#include "cb_stepscale.c"
#include "cb_fxbuf.c"
#include "cb_efftypes.c"
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
        const uint8_t lim = crumpBit_ops.effective_max(rt, row, share);

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
        CRUMPBIT_PARAM_FILTER_RESO,
        /* S091 P3 rows: stored, never read by `dly`. */
        CRUMPBIT_PARAM_AUX, CRUMPBIT_PARAM_GRAIN_ST2,
        CRUMPBIT_PARAM_GRAIN_ST3, CRUMPBIT_PARAM_GRAIN_SPREAD,
        CRUMPBIT_PARAM_GRAIN_DENSITY, CRUMPBIT_PARAM_GRAIN_AMP,
        CRUMPBIT_PARAM_GRAIN_WIDTH };
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
    expect(rt.subtype == CRUMPBIT_SUBTYPE_DELAY &&
           rt.active_subtype == CRUMPBIT_SUBTYPE_DELAY, "CB5 sub dly");
    expect(rt.aux_raw == 63u && rt.grain_st2 == 63u && rt.grain_st3 == 63u,
           "CB5 pitch rows 0 st");
    expect(rt.grain_density == 127u, "CB5 density full");
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
        expect(rt.u.tap.length == ref.length &&
               rt.u.tap.write_pos == ref.write_pos &&
               rt.u.tap.valid == ref.valid &&
               same_bits(rt.u.tap.delay, ref.delay) &&
               same_bits(rt.ac_y_l, ref.ac_y_l) &&
               same_bits(rt.ac_y_r, ref.ac_y_r) &&
               same_bits((float)(rt.ac_c_l - 128) * CRUMPBIT_LSB,
                         ref.ac_x_l) &&
               same_bits((float)(rt.ac_c_r - 128) * CRUMPBIT_LSB,
                         ref.ac_x_r) &&
               same_bits(rt.u.tap.dc_x, ref.dc_x) &&
               same_bits(rt.u.tap.dc_y, ref.dc_y) &&
               same_bits(rt.u.tap.tone_s, ref.tone_s) &&
               same_bits(rt.svf_l1, ref.s_l1) &&
               same_bits(rt.svf_l2, ref.s_l2) &&
               same_bits(rt.svf_r1, ref.s_r1) &&
               same_bits(rt.svf_r2, ref.s_r2), msg);
        snprintf(msg, sizeof msg, "CB4 seat invariant block %u", block);
        expect(rt.u.tap.length < CRUMPBIT_LOOP_MIN ||
               (rt.u.tap.delay <= (float)(rt.u.tap.length - 2u) &&
                rt.u.tap.write_pos < rt.u.tap.length &&
                rt.u.tap.valid <= rt.u.tap.length), msg);
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
        expect(crumpBit_ops.effective_max(NULL, CRUMPBIT_PARAM_RATE,
                                          &share) == ref_rateLimit(bytes),
               msg);
        expect(crumpBit_ops.effective_max(NULL, CRUMPBIT_PARAM_MIX, &share) ==
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
        for (i = 0u; i < rt.u.tap.length; i++)
            sum += (int8_t)arena_new[i];
        snprintf(msg, sizeof msg, "CB8 pass %u finite", pass);
        expect(finite, msg);
        snprintf(msg, sizeof msg, "CB8 pass %u peak %.3f < 4", pass,
                 (double)peak);
        expect(peak < 4.0f, msg);
        snprintf(msg, sizeof msg, "CB8 pass %u mean code %.3f within 8",
                 pass, (double)sum / (double)rt.u.tap.length);
        expect(llabs(sum) <= (int64_t)8 * (int64_t)rt.u.tap.length, msg);
        printf("CB8 pass %u: peak %.3f, mean loop code %.3f LSB\n", pass,
               (double)peak, (double)sum / (double)rt.u.tap.length);
    }
}


/* CB6 (S091 P3): sub-type Rate limits and the new tables ----------------- */

/* The longest index of an ascending table whose entry is <= max. */
static uint8_t ref_tableScan(const float *table, float max)
{
    uint8_t r = 0u;
    uint32_t i;

    for (i = 0u; i < 128u; i++)
        if (table[i] <= max)
            r = (uint8_t)i;
    return r;
}

/*
 * Each sub-type's bound, written from the plan (§3): the delay and comb
 * curves fit the loop (length - 2); the pitch window fits behind the guard;
 * `grn` fits one line (half the share); `rvb` fits its longest line (base
 * 2,053) in the word buffer.
 */
static uint8_t ref_subtypeLimit(uint8_t sub, uint32_t bytes)
{
    const uint32_t len = (bytes > CRUMPBIT_LOOP_MAX_BYTES)
        ? CRUMPBIT_LOOP_MAX_BYTES : bytes;
    const float max = (len < CRUMPBIT_LOOP_MIN) ? 0.0f : (float)(len - 2u);
    const uint32_t half = len / 2u;
    const float grain_max = (half < 66u) ? 0.0f : (float)(half - 2u);

    switch (sub) {
    case CRUMPBIT_SUBTYPE_COMB:   return ref_tableScan(crumpBit_combTable, max);
    case CRUMPBIT_SUBTYPE_PITCH:  return ref_tableScan(crumpBit_pitchWindow,
                                                       max - 2.0f);
    case CRUMPBIT_SUBTYPE_GRAIN:  return ref_tableScan(crumpBit_rateTable,
                                                       grain_max);
    case CRUMPBIT_SUBTYPE_REVERB: return ref_tableScan(crumpBit_reverbSize,
                                      (max * 0.25f - 2.0f) / 2053.0f);
    default:                      return ref_tableScan(crumpBit_rateTable, max);
    }
}

static void law_cb6_subtypes(void)
{
    CrumpBitRuntime rt;
    uint32_t bytes;
    uint32_t sub;
    uint32_t i;
    char msg[96];

    memset(&rt, 0, sizeof rt);
    crumpBit_ops.init(&rt, NULL);
    for (sub = 0u; sub < CRUMPBIT_SUBTYPE_COUNT; sub++) {
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_SUBTYPE, (uint8_t)sub);
        for (bytes = 0u; bytes <= CRUMPBIT_LOOP_MAX_BYTES + 64u;
             bytes += 32u) {
            fx_share_t share = { NULL, 0u, bytes };

            snprintf(msg, sizeof msg, "CB6 sub %u limit bytes %u", sub,
                     bytes);
            expect(crumpBit_subtypeRateLimit((uint8_t)sub, &share) ==
                   ref_subtypeLimit((uint8_t)sub, bytes), msg);
            expect(crumpBit_ops.effective_max(&rt, CRUMPBIT_PARAM_RATE,
                                              &share) ==
                   ref_subtypeLimit((uint8_t)sub, bytes), msg);
        }
    }
    /* sub above the last engine clamps to `rvb`. */
    crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_SUBTYPE, 200u);
    expect(rt.subtype == CRUMPBIT_SUBTYPE_COUNT - 1u, "CB6 sub clamps");

    /* Comb: L = fs / f(note), note = 112 - rte held at 96 (note 16). */
    for (i = 0u; i < 128u; i++) {
        const uint32_t note = 112u - ((i > 96u) ? 96u : i);
        const double f = 440.0 * pow(2.0, ((double)note - 69.0) / 12.0);

        snprintf(msg, sizeof msg, "CB6 comb %u", i);
        expect(crumpBit_combNote((uint8_t)i) == note, msg);
        expect(fabs(crumpBit_combTable[i] - 44108.0 / f) <=
               1e-4 * (44108.0 / f), msg);
        if (i)
            expect(crumpBit_combTable[i] >= crumpBit_combTable[i - 1u], msg);
    }
    /* Pitch window 10..200 ms; the other new tables' endpoints/order. */
    expect(fabsf(crumpBit_pitchWindow[0] - 441.08f) < 0.5f, "CB6 pit 10 ms");
    expect(fabsf(crumpBit_pitchWindow[127] - 8821.6f) < 0.5f,
           "CB6 pit 200 ms");
    expect(crumpBit_semitoneRatio[CRUMPBIT_SEMITONE_SPAN] == 1.0f,
           "CB6 ratio 0 st = 1");
    expect(fabsf(crumpBit_semitoneRatio[CRUMPBIT_SEMITONE_SPAN + 12u] -
                 2.0f) < 1e-6f, "CB6 ratio +12 = 2");
    expect(fabsf(crumpBit_semitoneRatio[0] - 0.25f) < 1e-7f,
           "CB6 ratio -24 = 1/4");
    expect(fabsf(crumpBit_semitoneRatio[2u * CRUMPBIT_SEMITONE_SPAN] -
                 4.0f) < 1e-6f, "CB6 ratio +24 = 4");
    expect(fabsf(crumpBit_grainFraction[0] - 1.0f / 32.0f) < 1e-8f,
           "CB6 grain 1/32");
    expect(crumpBit_grainFraction[CRUMPBIT_GRAIN_STEPS] == 1.0f,
           "CB6 grain 1");
    expect(fabsf(crumpBit_reverbSize[0] - 0.25f) < 1e-7f, "CB6 size 0.25");
    expect(fabsf(crumpBit_reverbSize[127] - 2.5f) < 1e-6f, "CB6 size 2.5");
    for (i = 0u; i < 128u; i++) {
        const double t = 0.1 * pow(100.0, (double)i / 127.0);

        snprintf(msg, sizeof msg, "CB6 new tables %u", i);
        expect(fabs(crumpBit_reverbDecay[i] -
                    (-3.0 * log2(10.0) / (t * 44108.0))) <=
               1e-5 * fabs(crumpBit_reverbDecay[i]), msg);
        if (i) {
            expect(crumpBit_pitchWindow[i] > crumpBit_pitchWindow[i - 1u],
                   msg);
            expect(crumpBit_reverbSize[i] > crumpBit_reverbSize[i - 1u], msg);
            expect(crumpBit_reverbDecay[i] > crumpBit_reverbDecay[i - 1u],
                   msg);
        }
        if (i && i <= 2u * CRUMPBIT_SEMITONE_SPAN)
            expect(crumpBit_semitoneRatio[i] >
                   crumpBit_semitoneRatio[i - 1u], msg);
        if (i && i <= CRUMPBIT_GRAIN_STEPS)
            expect(crumpBit_grainFraction[i] >
                   crumpBit_grainFraction[i - 1u], msg);
    }
}

/* CB9: row ownership and lane maps ---------------------------------------- */
static void law_cb9(void)
{
    /* The S091 P2 CrumpBit lane table (EffectsManager.c at 080b1507). */
    static const uint8_t p2_lanes[EFFECT_SEQ_LANE_COUNT] = {
        EFFECT_LANE_MORPH_SOURCE, CRUMPBIT_PARAM_BIT_OFF,
        CRUMPBIT_PARAM_BIT_INVERT, CRUMPBIT_PARAM_MIX,
        CRUMPBIT_PARAM_FEEDBACK, CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SYNC,
        CRUMPBIT_PARAM_LEVEL, CRUMPBIT_PARAM_PAN, CRUMPBIT_PARAM_DLY_PAN,
        CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_FILTER_TYPE,
        CRUMPBIT_PARAM_FILTER_FREQ, CRUMPBIT_PARAM_FILTER_RESO,
        EFFECT_LANE_NONE, EFFECT_LANE_NONE };
    /* The nine shared lanes (plan §2.9): fixed in every map. */
    static const uint8_t shared_lanes[] = { 0u, 1u, 2u, 3u, 7u, 8u, 11u, 12u,
                                            13u };
    const uint32_t rows = CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_COUNT) -
                          CRUMPBIT_ROW_BIT(EFFECT_COMMON_PARAM_COUNT);
    uint32_t v;
    uint32_t lane;
    char msg[96];

    expect((CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROWS_SUBTYPE) == 0u &&
           (CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROWS_GRN) == 0u &&
           (CRUMPBIT_ROWS_SUBTYPE & CRUMPBIT_ROWS_GRN) == 0u,
           "CB9 stage masks disjoint");
    expect((CRUMPBIT_ROWS_SHARED | CRUMPBIT_ROWS_SUBTYPE |
            CRUMPBIT_ROWS_GRN) == rows, "CB9 every type row has one owner");
    expect((CRUMPBIT_ROWS_SUBTYPE &
            CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_TONE)) != 0u,
           "CB9 ton is a sub-type row");
    expect((CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_MIX)) != 0u,
           "CB9 mix is shared");
    expect(crumpBit_ops.lane_variant_row == CRUMPBIT_PARAM_SUBTYPE &&
           crumpBit_ops.lane_variant_count == CRUMPBIT_SUBTYPE_COUNT,
           "CB9 lane variants are the sub-types");
    expect(memcmp(crumpBit_laneMap(CRUMPBIT_SUBTYPE_DELAY), p2_lanes,
                  sizeof p2_lanes) == 0, "CB9 dly map = S091 P2 table");
    for (v = 0u; v < CRUMPBIT_SUBTYPE_COUNT; v++) {
        const uint8_t *map = crumpBit_ops.lane_map((uint8_t)v);
        uint32_t seen = 0u;
        uint32_t k;

        for (k = 0u; k < sizeof shared_lanes; k++) {
            snprintf(msg, sizeof msg, "CB9 sub %u shared lane %u", v,
                     shared_lanes[k]);
            expect(map[shared_lanes[k]] == p2_lanes[shared_lanes[k]], msg);
        }
        for (lane = 0u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
            const uint8_t row = map[lane];

            if (row == EFFECT_LANE_NONE || row == EFFECT_LANE_MORPH_SOURCE)
                continue;
            snprintf(msg, sizeof msg, "CB9 sub %u lane %u row %u", v, lane,
                     row);
            expect(row < CRUMPBIT_PARAM_COUNT, msg);
            expect(row != CRUMPBIT_PARAM_SUBTYPE, msg);  /* never a lane */
            expect((seen & CRUMPBIT_ROW_BIT(row)) == 0u, msg);
            seen |= CRUMPBIT_ROW_BIT(row);
            /* Granular rows are lanes of `grn` only. */
            if ((CRUMPBIT_ROWS_GRN & CRUMPBIT_ROW_BIT(row)) != 0u)
                expect(v == CRUMPBIT_SUBTYPE_GRAIN, msg);
            /* `aux` is a lane of the sub-types that read it. */
            if (row == CRUMPBIT_PARAM_AUX)
                expect(v == CRUMPBIT_SUBTYPE_PITCH ||
                       v == CRUMPBIT_SUBTYPE_GRAIN, msg);
        }
    }
}

/* Shared helpers for CB10-CB15 -------------------------------------------- */

/* A guard band past the largest share: CB11 checks it is never written. */
#define CB_GUARD 256u
static uint8_t arena_p3[CB_ARENA + CB_GUARD];

static void p3_init(CrumpBitRuntime *rt, uint8_t sub)
{
    memset(rt, 0, sizeof *rt);
    crumpBit_ops.init(rt, NULL);
    crumpBit_ops.write_param(rt, CRUMPBIT_PARAM_SUBTYPE, sub);
    crumpBit_ops.write_param(rt, CRUMPBIT_PARAM_DLY_PAN, 63u);
}

/* Write a row, Rate through the sub-type-aware clamp (as the manager). */
static void p3_write(CrumpBitRuntime *rt, uint8_t row, uint8_t v,
                     const fx_share_t *share)
{
    if (row == CRUMPBIT_PARAM_RATE) {
        const uint8_t lim = crumpBit_ops.effective_max(rt, row, share);

        v = (v > lim) ? lim : v;
    }
    crumpBit_ops.write_param(rt, row, v);
}

static void p3_block(CrumpBitRuntime *rt, const fx_share_t *share, float *l,
                     float *r, uint32_t frames)
{
    effect_io_t io;

    io.l = l;
    io.r = r;
    io.frames = (uint8_t)frames;
    io.channels = r ? 2u : 1u;
    io.share = share;
    crumpBit_ops.process(rt, &io);
}

/* Goertzel power of x[0..n) at frequency f (Hz, fs 44,108). */
static double goertzel(const float *x, uint32_t n, double f)
{
    const double w = 2.0 * M_PI * f / 44108.0;
    const double c = 2.0 * cos(w);
    double s1 = 0.0;
    double s2 = 0.0;
    uint32_t i;

    for (i = 0u; i < n; i++) {
        const double s0 = (double)x[i] + c * s1 - s2;

        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

/* CB10: the mix stage, every sub-type ------------------------------------- */
static void law_cb10(void)
{
    fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
    uint32_t sub;
    char msg[96];

    for (sub = 0u; sub < CRUMPBIT_SUBTYPE_COUNT; sub++) {
        CrumpBitRuntime rt;
        /* The S074 stage 1: float AC input memory (CB4's reference). */
        float ax_l = 0.0f, ax_r = 0.0f, ay_l = 0.0f, ay_r = 0.0f;
        float l[32], r[32], hl[32], hr[32];
        uint32_t block;

        memset(arena_p3, 0x33, sizeof arena_p3);
        p3_init(&rt, (uint8_t)sub);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 0u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 120u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, 50u, &share);
        for (block = 0u; block < 8000u; block++) {
            const uint32_t mono = (block % 700u) >= 650u;
            uint32_t n;

            if ((block % 37u) == 0u) {
                /* Every row except mix (and the filter, kept `off`). */
                static const uint8_t rows[] = {
                    CRUMPBIT_PARAM_BIT_OFF, CRUMPBIT_PARAM_BIT_INVERT,
                    CRUMPBIT_PARAM_FEEDBACK, CRUMPBIT_PARAM_RATE,
                    CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_DLY_PAN,
                    CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_AUX,
                    CRUMPBIT_PARAM_GRAIN_ST2, CRUMPBIT_PARAM_GRAIN_ST3,
                    CRUMPBIT_PARAM_GRAIN_SPREAD,
                    CRUMPBIT_PARAM_GRAIN_DENSITY, CRUMPBIT_PARAM_GRAIN_AMP,
                    CRUMPBIT_PARAM_GRAIN_WIDTH };
                const uint8_t row = rows[rnd() % sizeof rows];
                uint8_t v = (uint8_t)(rnd() & 0x7Fu);

                if (row == CRUMPBIT_PARAM_BIT_OFF ||
                    row == CRUMPBIT_PARAM_BIT_INVERT)
                    v = (uint8_t)rnd();
                p3_write(&rt, row, v, &share);
            }
            for (n = 0u; n < 32u; n++) {
                l[n] = rndf(-1.5f, 1.5f);
                r[n] = rndf(-1.5f, 1.5f);
            }
            /* The dry reference, from the same inputs and masks. */
            for (n = 0u; n < 32u; n++) {
                const uint32_t invert = rt.bit_invert;
                const uint32_t keep =
                    (uint32_t)~((uint32_t)rt.bit_off & ~invert) & 0xFFu;
                const float yl = (float)((int32_t)((ref_adc(l[n]) & keep) ^
                                                   invert) - 128) *
                                 CRUMPBIT_LSB;
                const float yr = (float)((int32_t)((ref_adc(mono ? l[n]
                                                                 : r[n]) &
                                                    keep) ^ invert) - 128) *
                                 CRUMPBIT_LSB;

                hl[n] = yl - ax_l + CRUMPBIT_AC_R * ay_l;
                hr[n] = yr - ax_r + CRUMPBIT_AC_R * ay_r;
                ax_l = yl;
                ay_l = hl[n];
                ax_r = yr;
                ay_r = hr[n];
            }
            p3_block(&rt, &share, l, mono ? NULL : r, 32u);
            for (n = 0u; n < 32u; n++) {
                snprintf(msg, sizeof msg, "CB10 sub %u block %u frame %u",
                         sub, block, n);
                expect(same_bits(l[n], hl[n]), msg);
                if (!mono)
                    expect(same_bits(r[n], hr[n]), msg);
            }
        }
    }
}

/* CB11: sub-type switch storm --------------------------------------------- */
static void law_cb11(void)
{
    static const uint32_t shares[] = {
        CRUMPBIT_LOOP_MAX_BYTES, 122272u, 100192u,
        CRUMPBIT_LOOP_MIN_SHARE_BYTES, 40000u, 4096u, 300u, 64u };
    CrumpBitRuntime rt;
    fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
    float l[32], r[32];
    uint32_t block;
    uint32_t next_switch = 1u;
    uint32_t i;
    int finite = 1;
    char msg[128];

    memset(arena_p3, 0, sizeof arena_p3);
    memset(arena_p3 + CB_ARENA, 0xA5, CB_GUARD);
    p3_init(&rt, 0u);
    for (block = 0u; block < 41400u; block++) {          /* 30 s */
        const uint32_t mono = ((block / 613u) % 5u) == 2u;
        const uint32_t frames = ((block % 97u) == 7u) ? 1u + rnd() % 32u
                                                      : 32u;
        const uint32_t kind = (block / 300u) % 4u;
        uint32_t n;

        if (block == next_switch) {
            p3_write(&rt, CRUMPBIT_PARAM_SUBTYPE,
                     (uint8_t)(rnd() % CRUMPBIT_SUBTYPE_COUNT), &share);
            next_switch = block + 1u + rnd() % 50u;
        }
        if ((rnd() & 3u) == 0u) {
            const uint8_t row = (uint8_t)(EFFECT_COMMON_PARAM_COUNT +
                                          rnd() % (CRUMPBIT_PARAM_COUNT -
                                                   EFFECT_COMMON_PARAM_COUNT));
            uint8_t v = (uint8_t)(rnd() & 0x7Fu);

            if (row == CRUMPBIT_PARAM_FILTER_TYPE)
                v = (uint8_t)(rnd() & 7u);
            if (row == CRUMPBIT_PARAM_FILTER_RESO)
                v = (uint8_t)(rnd() % 100u);
            if (row != CRUMPBIT_PARAM_SUBTYPE)
                p3_write(&rt, row, v, &share);
        }
        if ((block % 1501u) == 1500u) {
            share.bytes = shares[rnd() % (sizeof shares / sizeof shares[0])];
            crumpBit_ops.buffer_changed(&rt, &share);
        }
        /* The manager re-applies the BUFFER_DEPENDENT Rate every block. */
        p3_write(&rt, CRUMPBIT_PARAM_RATE, rt.rate, &share);
        for (n = 0u; n < frames; n++) {
            switch (kind) {
            case 0: l[n] = rndf(-1.0f, 1.0f); r[n] = rndf(-1.0f, 1.0f); break;
            case 1: l[n] = rndf(-4.0f, 4.0f); r[n] = -l[n];             break;
            case 2: l[n] = 0.0f; r[n] = 0.0f;                           break;
            default: l[n] = 1.0f; r[n] = 1.0f;                          break;
            }
        }
        p3_block(&rt, &share, l, mono ? NULL : r, frames);
        for (n = 0u; n < frames; n++) {
            finite &= isfinite(l[n]) && fabsf(l[n]) < 64.0f;
            if (!mono)
                finite &= isfinite(r[n]) && fabsf(r[n]) < 64.0f;
        }
        snprintf(msg, sizeof msg, "CB11 block %u sub %u: invariants", block,
                 rt.subtype);
        if (share.bytes >= CRUMPBIT_LOOP_MIN)
            expect(rt.active_subtype == rt.subtype, msg);
        switch (rt.active_subtype) {
        case CRUMPBIT_SUBTYPE_GRAIN: {
            const CrumpBitGrainState *g = &rt.u.grn;

            expect(g->length == 0u ||
                   (g->write_pos < g->length && g->valid <= g->length &&
                    (g->length < 4u * CRUMPBIT_GRAIN_MIN ||
                     ((g->pos_a >> 16) < g->length &&
                      (g->pos_b >> 16) < g->length &&
                      g->age < g->length))), msg);
            break;
        }
        case CRUMPBIT_SUBTYPE_REVERB:
            expect(rt.u.rvb.length == 0u ||
                   (rt.u.rvb.write_pos < rt.u.rvb.length &&
                    rt.u.rvb.valid <= rt.u.rvb.length), msg);
            break;
        default:
            expect(rt.u.tap.length < CRUMPBIT_LOOP_MIN ||
                   (rt.u.tap.write_pos < rt.u.tap.length &&
                    rt.u.tap.valid <= rt.u.tap.length &&
                    (rt.active_subtype == CRUMPBIT_SUBTYPE_PITCH ||
                     rt.u.tap.delay <= (float)(rt.u.tap.length - 2u))), msg);
            break;
        }
    }
    expect(finite, "CB11 finite and bounded");
    for (i = 0u; i < CB_GUARD; i++)
        if (arena_p3[CB_ARENA + i] != 0xA5u) {
            expect(0, "CB11 guard band untouched");
            break;
        }
}

/* CB12: `cmb` tuning ------------------------------------------------------- */
static void law_cb12(void)
{
    static const uint8_t rates[] = { 0u, 20u, 40u, 60u, 80u, 96u };
    fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
    uint32_t k;
    uint32_t polarity;
    char msg[96];

    for (polarity = 0u; polarity < 2u; polarity++)
        for (k = 0u; k < sizeof rates; k++) {
            CrumpBitRuntime rt;
            const float len_f = crumpBit_combTable[rates[k]];
            const uint32_t span = 3u * (uint32_t)len_f + 64u;
            float *out = calloc(span + 32u, sizeof(float));
            float l[32], r[32];
            uint32_t block;
            uint32_t got = 0u;
            uint32_t peak1 = 0u;
            float best = 0.0f;
            uint32_t i;

            memset(arena_p3, 0, sizeof arena_p3);
            p3_init(&rt, CRUMPBIT_SUBTYPE_COMB);
            p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 90u, &share);
            p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
            p3_write(&rt, CRUMPBIT_PARAM_RATE, rates[k], &share);
            p3_write(&rt, CRUMPBIT_PARAM_SYNC, (uint8_t)polarity, &share);
            /* 0.2 s of silence: the 5 ms glide settles on the length. */
            for (block = 0u; block < 276u; block++) {
                memset(l, 0, sizeof l);
                memset(r, 0, sizeof r);
                p3_block(&rt, &share, l, r, 32u);
            }
            while (got < span) {
                memset(l, 0, sizeof l);
                memset(r, 0, sizeof r);
                if (got == 0u) {
                    l[0] = 0.9f;
                    r[0] = 0.9f;
                }
                p3_block(&rt, &share, l, r, 32u);
                memcpy(out + got, l, sizeof l);
                got += 32u;
            }
            for (i = (uint32_t)(0.5f * len_f); i < (uint32_t)(1.5f * len_f);
                 i++)
                if (fabsf(out[i]) > best) {
                    best = fabsf(out[i]);
                    peak1 = i;
                }
            snprintf(msg, sizeof msg, "CB12 rte %u: echo at %u, L %.1f",
                     rates[k], peak1, (double)len_f);
            expect(fabsf((float)peak1 - len_f) <= 2.0f, msg);
            {
                /* The second echo, one length later: its sign. */
                const uint32_t p2 = peak1 + (uint32_t)(len_f + 0.5f);
                float v2 = 0.0f;
                int32_t d;

                for (d = -2; d <= 2; d++)
                    if (fabsf(out[p2 + d]) > fabsf(v2))
                        v2 = out[p2 + d];
                snprintf(msg, sizeof msg, "CB12 rte %u snc %u polarity",
                         rates[k], polarity);
                expect(polarity ? (v2 * out[peak1] < 0.0f)
                                : (v2 * out[peak1] > 0.0f), msg);
            }
            free(out);
        }
}

/* CB13: `pit` ratio --------------------------------------------------------- */
static void law_cb13(void)
{
    /* aux 95 -> +12.19 st, 31 -> -12.19 st, snapped by snc to +-12. */
    static const uint8_t aux[] = { 95u, 31u, 63u };
    static const double ratio[] = { 2.0, 0.5, 1.0 };
    fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
    const uint32_t total = 44108u;
    float *out = calloc(total + 32u, sizeof(float));
    uint32_t k;
    char msg[96];

    for (k = 0u; k < 3u; k++) {
        CrumpBitRuntime rt;
        float l[32], r[32];
        uint32_t got = 0u;
        uint32_t t = 0u;
        double e_in;
        double e_out;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_PITCH);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 0u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, 80u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_SYNC, 1u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_AUX, aux[k], &share);
        while (got < total) {
            uint32_t n;

            for (n = 0u; n < 32u; n++, t++) {
                l[n] = 0.8f * sinf(2.0f * (float)M_PI * 441.0f *
                                   (float)t / 44108.0f);
                r[n] = l[n];
            }
            p3_block(&rt, &share, l, r, 32u);
            memcpy(out + got, l, sizeof l);
            got += 32u;
        }
        /* The last half second. */
        e_in = goertzel(out + total / 2u, total / 2u, 441.0);
        e_out = goertzel(out + total / 2u, total / 2u, 441.0 * ratio[k]);
        snprintf(msg, sizeof msg, "CB13 aux %u: E(%.1f) %.3g vs E(441) %.3g",
                 aux[k], 441.0 * ratio[k], e_out, e_in);
        if (ratio[k] == 1.0)
            expect(e_in > 10.0 * goertzel(out + total / 2u, total / 2u,
                                          882.0), msg);
        else
            expect(e_out > 10.0 * e_in, msg);
    }
    free(out);
}

/* CB14: `grn` --------------------------------------------------------------- */
static void law_cb14(void)
{
    static const uint8_t spreads[] = { 0u, 40u, 80u, 127u };
    CrumpBitRuntime rt;
    uint32_t s;
    uint32_t i;
    char msg[128];

    /* (a) candidate pitch-slot distribution and detune bound. */
    memset(&rt, 0, sizeof rt);
    crumpBit_ops.init(&rt, NULL);
    rt.aux_raw = 63u;                   /* root 0 st */
    rt.grain_st2 = 68u;                 /* +5 st */
    rt.grain_st3 = 72u;                 /* +9 st */
    rt.tone_raw = 100u;                 /* forward */
    rt.pan_raw = 63u;                   /* follow */
    for (s = 0u; s < sizeof spreads; s++) {
        const int32_t slot_inc[3] = {
            (int32_t)(crumpBit_semitoneRatio[24] * 4096.0f + 0.5f),
            (int32_t)(crumpBit_semitoneRatio[29] * 4096.0f + 0.5f),
            (int32_t)(crumpBit_semitoneRatio[33] * 4096.0f + 0.5f) };
        uint32_t count[4] = { 0u, 0u, 0u, 0u };
        uint32_t hash = 0x12345u;
        const uint32_t trials = 30000u;

        rt.grain_spread = spreads[s];
        for (i = 0u; i < trials; i++) {
            crumpbit_grain_t c;
            uint32_t slot = 3u;
            uint32_t k;

            crumpBit_grainPick(&hash, i & 1u, &rt, 60000u, 30000u, 20000u,
                               2000u, 60000u, &c);
            for (k = 0u; k < 3u; k++)
                if (c.inc == slot_inc[k])
                    slot = k;
            if (slot == 3u) {
                /* Off-slot: only above spr 80, within +-35 cents. */
                double best = 1e9;

                for (k = 0u; k < 3u; k++) {
                    const double cents = 1200.0 *
                        log2((double)c.inc / (double)slot_inc[k]);

                    best = (fabs(cents) < fabs(best)) ? cents : best;
                }
                /* +-35 cents by the linear cent law (< 0.5 cent error at
                 * the extreme), then the Q3.12 step (<= 0.2 cent at 1). */
                snprintf(msg, sizeof msg, "CB14 spr %u detune %.2f cents",
                         spreads[s], best);
                expect(spreads[s] > 80u && fabs(best) <= 36.0, msg);
            }
            count[slot]++;
        }
        snprintf(msg, sizeof msg, "CB14 spr %u slots %u %u %u off %u",
                 spreads[s], count[0], count[1], count[2], count[3]);
        switch (spreads[s]) {
        case 0u:
            expect(count[0] == trials, msg);
            break;
        case 40u:
            expect(fabs((double)count[0] / trials - 0.5) < 0.02 &&
                   fabs((double)count[1] / trials - 0.5) < 0.02 &&
                   count[2] == 0u, msg);
            break;
        case 80u:
            expect(fabs((double)count[0] / trials - 1.0 / 3.0) < 0.02 &&
                   fabs((double)count[1] / trials - 1.0 / 3.0) < 0.02 &&
                   fabs((double)count[2] / trials - 1.0 / 3.0) < 0.02, msg);
            break;
        default:
            expect(count[3] > trials / 2u, msg);
            break;
        }
    }

    /* (b) level/pan bytes and the start lag behind the write head. */
    for (i = 0u; i < 200000u; i++) {
        crumpbit_grain_t c;
        uint32_t hash = rnd() | 1u;
        const uint32_t len = 1000u + rnd() % 62000u;
        const uint32_t window = 128u + rnd() % (len - 130u);
        const uint32_t grain_len = 64u + rnd() % (window - 63u);
        const uint32_t valid = (rnd() & 1u) ? len : rnd() % len;
        const uint32_t wp = rnd() % len;
        const uint32_t grain = i & 1u;
        uint32_t lag;
        float ratio;

        rt.aux_raw = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_st2 = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_st3 = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_spread = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_amp = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_width = (uint8_t)(rnd() & 0x7Fu);
        rt.tone_raw = (uint8_t)(rnd() & 0x7Fu);
        rt.pan_raw = (uint8_t)(rnd() & 0x7Fu);
        if ((i % 7u) == 0u) {
            rt.grain_amp = 0u;
            rt.grain_width = 0u;
        }
        crumpBit_grainPick(&hash, grain, &rt, len, wp, window, grain_len,
                           valid, &c);
        lag = (wp >= c.start) ? wp - c.start : wp + len - c.start;
        ratio = (float)(c.inc < 0 ? -c.inc : c.inc) / 4096.0f;
        snprintf(msg, sizeof msg,
                 "CB14 pick %u: lag %u len %u G %u r %.3f inc %d", i, lag,
                 len, grain_len, (double)ratio, c.inc);
        expect(c.start < len, msg);
        expect(lag >= 2u, msg);
        /*
         * Headroom: a forward grain above unity starts at least
         * 2 + G (r - 1) behind the head (truncated to whole samples), with r
         * the played Q3.12 ratio, whenever that lag fits the line at all.
         */
        if (c.inc > 0 && ratio > 1.0f &&
            2.0f + (float)grain_len * (ratio - 1.0f) < (float)(len - 1u))
            expect((float)lag + 1.0f >
                   2.0f + (float)grain_len * (ratio - 1.0f), msg);
        expect((c.inc < 0) == (rt.tone_raw < CRUMPBIT_TONE_FLAT), msg);
        if (rt.grain_amp == 0u)
            expect(c.amp == 255u, msg);
        if (rt.grain_width == 0u)
            expect(c.pan == (grain ? 191u : 64u), msg);
    }

    /* (c) freeze keeps the record line bit-exact; the delay line runs. */
    {
        fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        const uint32_t half = CRUMPBIT_LOOP_MAX_BYTES / 2u;
        uint8_t *snap_rec = malloc(half);
        uint8_t *snap_dly = malloc(half);
        float l[32], r[32];
        uint32_t block;
        uint32_t n;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_GRAIN);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 100u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 64u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, 40u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_SPREAD, 100u, &share);
        for (block = 0u; block < 4000u; block++) {
            if (block == 2000u) {
                p3_write(&rt, CRUMPBIT_PARAM_SYNC, 1u, &share);
                memcpy(snap_rec, arena_p3, half);
            }
            if (block == 2001u)
                memcpy(snap_dly, arena_p3 + half, half);
            for (n = 0u; n < 32u; n++) {
                l[n] = rndf(-0.8f, 0.8f);
                r[n] = rndf(-0.8f, 0.8f);
            }
            p3_block(&rt, &share, l, r, 32u);
        }
        expect(memcmp(snap_rec, arena_p3, half) == 0,
               "CB14 freeze: record line bit-exact");
        expect(memcmp(snap_dly, arena_p3 + half, half) != 0,
               "CB14 freeze: delay line still running");
        free(snap_rec);
        free(snap_dly);
    }

    /* (d) stability at fbk 127, every size/direction extreme. */
    for (s = 0u; s < 4u; s++) {
        fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        float l[32], r[32];
        float peak = 0.0f;
        int finite = 1;
        uint32_t block;
        uint32_t n;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_GRAIN);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 127u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, (uint8_t)(s * 40u), &share);
        p3_write(&rt, CRUMPBIT_PARAM_TONE, (uint8_t)((s & 1u) ? 0u : 127u),
                 &share);
        p3_write(&rt, CRUMPBIT_PARAM_AUX, (uint8_t)(s * 40u), &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_SPREAD, 127u, &share);
        for (block = 0u; block < 27600u; block++) {     /* 20 s */
            for (n = 0u; n < 32u; n++) {
                const float burst = (block < 138u) ? rndf(-1.0f, 1.0f)
                                                   : 0.0f;

                l[n] = burst;
                r[n] = -burst;
            }
            p3_block(&rt, &share, l, r, 32u);
            for (n = 0u; n < 32u; n++) {
                finite &= isfinite(l[n]) && isfinite(r[n]);
                peak = fmaxf(peak, fmaxf(fabsf(l[n]), fabsf(r[n])));
            }
        }
        snprintf(msg, sizeof msg, "CB14 stability pass %u peak %.3f", s,
                 (double)peak);
        expect(finite && peak < 8.0f, msg);
    }
}

/* CB15: `rvb` --------------------------------------------------------------- */
static double cb15_rms(const float *x, uint32_t n)
{
    double e = 0.0;
    uint32_t i;

    for (i = 0u; i < n; i++)
        e += (double)x[i] * (double)x[i];
    return sqrt(e / (double)n);
}

/* Run `rvb`: 0.5 s settle, a 50 ms burst, then `tail` samples recorded. */
static void cb15_run(CrumpBitRuntime *rt, const fx_share_t *share,
                     uint8_t freeze_after, float *out, uint32_t tail)
{
    float l[32], r[32];
    uint32_t block;
    uint32_t got = 0u;
    uint32_t n;

    for (block = 0u; block < 690u; block++) {
        memset(l, 0, sizeof l);
        memset(r, 0, sizeof r);
        p3_block(rt, share, l, r, 32u);
    }
    for (block = 0u; block < 69u; block++) {
        for (n = 0u; n < 32u; n++) {
            l[n] = rndf(-0.5f, 0.5f);
            r[n] = rndf(-0.5f, 0.5f);
        }
        p3_block(rt, share, l, r, 32u);
    }
    if (freeze_after)
        p3_write(rt, CRUMPBIT_PARAM_SYNC, 1u, share);
    while (got < tail) {
        memset(l, 0, sizeof l);
        memset(r, 0, sizeof r);
        p3_block(rt, share, l, r, 32u);
        memcpy(out + got, l, sizeof l);
        got += 32u;
    }
}

static void law_cb15(void)
{
    fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
    const uint32_t tail = 48000u;
    float *out = calloc(tail + 32u, sizeof(float));
    CrumpBitRuntime rt;
    uint32_t pass;
    char msg[128];

    /* (a) every extreme: finite and bounded over 5 s with input. */
    for (pass = 0u; pass < 32u; pass++) {
        float l[32], r[32];
        float peak = 0.0f;
        int finite = 1;
        uint32_t block;
        uint32_t n;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_REVERB);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, (pass & 1u) ? 127u : 0u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, (pass & 2u) ? 127u : 0u,
                 &share);
        p3_write(&rt, CRUMPBIT_PARAM_SYNC, (uint8_t)((pass >> 2) & 1u),
                 &share);
        p3_write(&rt, CRUMPBIT_PARAM_TONE, (pass & 8u) ? 127u : 0u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_DLY_PAN, (pass & 16u) ? 127u : 0u,
                 &share);
        for (block = 0u; block < 6900u; block++) {
            for (n = 0u; n < 32u; n++) {
                l[n] = rndf(-1.0f, 1.0f);
                r[n] = rndf(-1.0f, 1.0f);
            }
            p3_block(&rt, &share, l, r, 32u);
            for (n = 0u; n < 32u; n++) {
                finite &= isfinite(l[n]) && isfinite(r[n]);
                peak = fmaxf(peak, fmaxf(fabsf(l[n]), fabsf(r[n])));
            }
        }
        snprintf(msg, sizeof msg, "CB15 extreme %u peak %.3f", pass,
                 (double)peak);
        expect(finite && peak < 8.0f, msg);
    }

    /* (b) decay: fbk 44 is RT60 0.493 s; 0.125 s later -15.2 dB. */
    memset(arena_p3, 0, sizeof arena_p3);
    p3_init(&rt, CRUMPBIT_SUBTYPE_REVERB);
    p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
    p3_write(&rt, CRUMPBIT_PARAM_RATE, 64u, &share);
    p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 44u, &share);
    cb15_run(&rt, &share, 0u, out, tail);
    {
        const double a = cb15_rms(out + 2205u, 2205u);    /* 50..100 ms */
        const double b = cb15_rms(out + 7718u, 2205u);    /* 175..225 ms */
        const double db = 20.0 * log10(a / (b > 1e-12 ? b : 1e-12));

        snprintf(msg, sizeof msg, "CB15 decay %.2f dB over 0.125 s", db);
        expect(db >= 9.0 && db <= 21.0, msg);
        printf("CB15 decay: %.2f dB over 0.125 s (law 15.2)\n", db);
    }

    /* (c) freeze holds the tail within +-6 dB over 1 s. */
    memset(arena_p3, 0, sizeof arena_p3);
    p3_init(&rt, CRUMPBIT_SUBTYPE_REVERB);
    p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
    p3_write(&rt, CRUMPBIT_PARAM_RATE, 64u, &share);
    p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 100u, &share);
    cb15_run(&rt, &share, 1u, out, tail);
    {
        const double a = cb15_rms(out + 2205u, 2205u);
        const double b = cb15_rms(out + 44108u, 2205u);
        const double db = 20.0 * log10(a / (b > 1e-12 ? b : 1e-12));

        snprintf(msg, sizeof msg, "CB15 freeze %.2f dB over 1 s", db);
        expect(fabs(db) <= 6.0, msg);
        printf("CB15 freeze: %.2f dB over 1 s\n", db);
    }
    free(out);
}

int main(void)
{
    law_cb1();
    law_cb2();
    law_cb3();
    law_cb4();
    law_cb6();
    law_cb6_subtypes();
    law_cb7();
    law_cb8();
    law_cb9();
    law_cb10();
    law_cb11();
    law_cb12();
    law_cb13();
    law_cb14();
    law_cb15();
    printf("crumpbit failures: %d\n", failures);
    return failures ? 1 : 0;
}
