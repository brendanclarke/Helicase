/*
 * CrumpBit host tests (S091 P1 reductions).
 *
 * What:       compiles the real Core/DSP/Effects/CrumpBit/CrumpBitEffect.c
 *             (firmware includes stripped by extract.py) against the S074
 *             reference copied below, and checks that the S091 reductions
 *             R1-R4 are bit-identical (class S0):
 *               CB1  Sync division: crumpBit_divisionFor(),
 *                    crumpBit_syncDivision() and crumpBit_targetSamples()
 *                    against the 128-step S074 walk, for every rate at BPM
 *                    0..1000, for every exact division length (ties), and
 *                    for random targets;
 *               CB2  8-bit ADC: crumpBit_adc() against the clamped S074 form
 *                    at every code edge (+-8 ulps), across +-3 and at large
 *                    magnitudes;
 *               CB3  code-difference DAC: crumpBit_codeToFloat(c - p) equals
 *                    the S074 (c - 128)/128 - (p - 128)/128 for every pair;
 *               CB4  whole block: crumpBit_process() against the S074
 *                    process over 60 s of random blocks with parameter jumps,
 *                    out-of-range input, mono io->r == NULL, short blocks
 *                    and share changes; outputs, loop bytes and runtime
 *                    state compared bit for bit;
 *               CB5  init: the AC memory is seeded with code 128.
 * Why:        the reductions remove instructions from code whose output
 *             the board cannot report sample by sample; S0 is proved here
 *             and the ARM shape by the linked-ELF gate (DSP_TEST.md §6.4).
 * Inputs:     build/gen/cb_*.c|h, CrumpBitEffect.host.h/.c (extract.py).
 * Outputs:    one line per failure (first 40); "crumpbit failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test crumpbit (two break builds follow).
 * Affiliates: CrumpBitEffect.c/.h; StepScale.c; S091_P1_Reductions_
 *             Implementation.md §4.
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

/* seq_getBpm(): the test sets the tempo the DSP reads. */
static uint16_t cb_bpm = 120u;
uint16_t seq_getBpm(void) { return cb_bpm; }

#include "cb_stepscale.h"
#include "cb_stepscale.c"
#include "cb_fxbuf.c"
#include "cb_effects.c"
#include "cb_params.c"
#include "CrumpBitEffect.host.h"
#include "CrumpBitEffect.host.c"

/*
 * S074 reference (CrumpBitEffect.c at 93f35799, renamed ref_*).
 * Copied verbatim except for the names; the constants come from the real
 * file, which R1-R4 did not change.
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
    uint8_t bit_off;
    uint8_t bit_invert;
    uint8_t mix_raw;
    uint8_t feedback_raw;
    uint8_t rate;
    uint8_t sync;
    uint8_t pan_raw;
    uint8_t primed;
} RefRuntime;

static inline uint32_t ref_adc(float x)
{
    uint32_t code;

    x = (x > 1.0f) ? 1.0f : x;
    x = (x < -1.0f) ? -1.0f : x;
    code = (uint32_t)(x * 128.0f + 128.5f);
    return code - (code >> 8);
}

static uint8_t ref_divisionFor(float target, float per_tick)
{
    uint8_t low = CRUMPBIT_DIVISION_NONE;
    uint8_t high = CRUMPBIT_DIVISION_NONE;
    float t_low = 0.0f;
    float t_high = 0.0f;
    uint8_t i;

    for (i = 0u; i < STEP_SCALE_COUNT; i++) {
        const float t = ((float)stepScale_ticksQ8(i) / 256.0f) * per_tick;

        if (t > CRUMPBIT_DELAY_MAX_SAMPLES)
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

static uint8_t ref_syncDivision(uint8_t rate, uint16_t bpm)
{
    const float per_tick =
        CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);

    return ref_divisionFor(crumpBit_rateSamples(rate), per_tick);
}

static float ref_targetSamples(uint8_t rate, uint8_t sync, uint16_t bpm)
{
    const float per_tick =
        CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);
    const float free_run = crumpBit_rateSamples(rate);
    const float synced = ((float)stepScale_ticksQ8(
        ref_divisionFor(free_run, per_tick)) / 256.0f) * per_tick;
    const float target = sync ? synced : free_run;

    return (target > CRUMPBIT_DELAY_MAX_SAMPLES)
        ? CRUMPBIT_DELAY_MAX_SAMPLES : target;
}

static uint32_t ref_seat(RefRuntime *rt, const fx_share_t *share)
{
    uint32_t len = share ? share->bytes : 0u;

    if (len > CRUMPBIT_BUFFER_BYTES)
        len = CRUMPBIT_BUFFER_BYTES;
    if (len != rt->length) {
        rt->length = len;
        rt->write_pos = 0u;
        rt->valid = 0u;
    }
    return len;
}

static void ref_init(RefRuntime *rt)
{
    rt->length = 0u;
    rt->write_pos = 0u;
    rt->valid = 0u;
    rt->primed = 0u;
}

static void ref_writeParam(RefRuntime *rt, uint8_t index, uint8_t value)
{
    switch (index) {
    case CRUMPBIT_PARAM_BIT_OFF:    rt->bit_off = value;               break;
    case CRUMPBIT_PARAM_BIT_INVERT: rt->bit_invert = value;            break;
    case CRUMPBIT_PARAM_MIX:        rt->mix_raw = value;               break;
    case CRUMPBIT_PARAM_FEEDBACK:  rt->feedback_raw = value;          break;
    case CRUMPBIT_PARAM_RATE:       rt->rate = value;                  break;
    case CRUMPBIT_PARAM_SYNC:       rt->sync = (uint8_t)(value != 0u); break;
    case CRUMPBIT_PARAM_DLY_PAN:    rt->pan_raw = value;               break;
    default:                                                           break;
    }
}

static void ref_process(RefRuntime *rt, effect_io_t *io)
{
    float *left;
    float *right;
    int8_t *loop;
    uint32_t len;
    uint32_t keep;
    uint32_t invert;
    uint32_t wp;
    uint32_t valid;
    float flen;
    float target;
    float delay;
    float mix;
    float fb;
    float gain_l;
    float gain_r;
    float mix_to;
    float fb_to;
    float gain_l_to;
    float gain_r_to;
    float mix_inc;
    float fb_inc;
    float gain_l_inc;
    float gain_r_inc;
    float ac_xl;
    float ac_xr;
    float ac_yl;
    float ac_yr;
    float per_frame;
    uint8_t n;

    if (!rt || !io || !io->l || !io->share || !io->share->base ||
        io->frames == 0u)
        return;
    len = ref_seat(rt, io->share);
    if (len < CRUMPBIT_LOOP_MIN)
        return;
    left = io->l;
    right = io->r ? io->r : io->l;
    loop = (int8_t *)io->share->base;
    flen = (float)len;
    invert = rt->bit_invert;
    keep = (uint32_t)~((uint32_t)rt->bit_off & ~invert) & 0xFFu;
    target = ref_targetSamples(rt->rate, rt->sync, seq_getBpm());
    if (target > flen - 2.0f)
        target = flen - 2.0f;
    if (!rt->primed) {
        rt->delay = target;
        rt->primed = 1u;
    }
    per_frame = 1.0f / (float)io->frames;
    mix_to = (float)rt->mix_raw * (1.0f / 127.0f);
    fb_to = (float)rt->feedback_raw * (CRUMPBIT_FEEDBACK_MAX / 127.0f);
    gain_l_to = (rt->pan_raw <= 63u) ? 1.0f
        : (float)(127u - rt->pan_raw) * (1.0f / 64.0f);
    gain_r_to = (rt->pan_raw >= 63u) ? 1.0f
        : (float)rt->pan_raw * (1.0f / 63.0f);
    mix = rt->mix;
    fb = rt->feedback;
    gain_l = rt->gain_l;
    gain_r = rt->gain_r;
    mix_inc = (mix_to - mix) * per_frame;
    fb_inc = (fb_to - fb) * per_frame;
    gain_l_inc = (gain_l_to - gain_l) * per_frame;
    gain_r_inc = (gain_r_to - gain_r) * per_frame;
    delay = rt->delay;
    wp = rt->write_pos;
    valid = rt->valid;
    ac_xl = rt->ac_x_l;
    ac_xr = rt->ac_x_r;
    ac_yl = rt->ac_y_l;
    ac_yr = rt->ac_y_r;
    for (n = 0u; n < io->frames; n++) {
        const uint32_t code_l = (ref_adc(left[n]) & keep) ^ invert;
        const uint32_t code_r = (ref_adc(right[n]) & keep) ^ invert;
        const float yl = (float)((int32_t)code_l - 128) * CRUMPBIT_LSB;
        const float yr = (float)((int32_t)code_r - 128) * CRUMPBIT_LSB;
        const float hl = yl - ac_xl + CRUMPBIT_AC_R * ac_yl;
        const float hr = yr - ac_xr + CRUMPBIT_AC_R * ac_yr;
        float rp;
        float frac;
        float wet;
        uint32_t i0;
        uint32_t i1;

        ac_xl = yl;
        ac_yl = hl;
        ac_xr = yr;
        ac_yr = hr;
        mix += mix_inc;
        fb += fb_inc;
        gain_l += gain_l_inc;
        gain_r += gain_r_inc;
        delay += (target - delay) * CRUMPBIT_GLIDE_K;
        rp = (float)wp - delay;
        rp += (rp < 0.0f) ? flen : 0.0f;
        i0 = (uint32_t)rp;
        frac = rp - (float)i0;
        i0 -= (i0 >= len) ? len : 0u;
        i1 = i0 + 1u;
        i1 -= (i1 >= len) ? len : 0u;
        wet = ((float)loop[i0] +
               frac * (float)(loop[i1] - loop[i0])) * CRUMPBIT_LSB;
        wet = ((delay + 2.0f) <= (float)valid) ? wet : 0.0f;
        loop[wp] = (int8_t)((int32_t)ref_adc(0.5f * (hl + hr) +
                                             fb * wet) - 128);
        wp++;
        wp -= (wp >= len) ? len : 0u;
        valid += (valid < len) ? 1u : 0u;
        right[n] = hr + mix * (gain_r * wet - hr);
        left[n] = hl + mix * (gain_l * wet - hl);
    }
    rt->delay = delay;
    rt->write_pos = wp;
    rt->valid = valid;
    rt->ac_x_l = ac_xl;
    rt->ac_x_r = ac_xr;
    rt->ac_y_l = ac_yl;
    rt->ac_y_r = ac_yr;
    rt->mix = mix_to;
    rt->feedback = fb_to;
    rt->gain_l = gain_l_to;
    rt->gain_r = gain_r_to;
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

/* CB1: the Sync division ------------------------------------------------- */
static void law_cb1(void)
{
    uint32_t bpm;
    uint32_t i;
    char msg[96];

    for (bpm = 0u; bpm <= 1000u; bpm++) {
        const float per_tick =
            CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);
        uint32_t r;

        for (r = 0u; r < 128u; r++) {
            const float t = crumpBit_rateSamples((uint8_t)r);

            snprintf(msg, sizeof msg, "CB1 rate %u bpm %u", r, bpm);
            expect(crumpBit_divisionFor(t, per_tick) ==
                   ref_divisionFor(t, per_tick), msg);
            expect(crumpBit_syncDivision((uint8_t)r, (uint16_t)bpm) ==
                   ref_syncDivision((uint8_t)r, (uint16_t)bpm), msg);
            expect(same_bits(crumpBit_targetSamples((uint8_t)r, 1u,
                                                    (uint16_t)bpm),
                             ref_targetSamples((uint8_t)r, 1u,
                                               (uint16_t)bpm)), msg);
            expect(same_bits(crumpBit_targetSamples((uint8_t)r, 0u,
                                                    (uint16_t)bpm),
                             ref_targetSamples((uint8_t)r, 0u,
                                               (uint16_t)bpm)), msg);
        }
        /* Ties: a target exactly equal to each division length, and the
         * floats either side of it. */
        for (i = 0u; i < STEP_SCALE_COUNT; i++) {
            const float t = ((float)stepScale_ticksQ8((uint8_t)i) / 256.0f)
                            * per_tick;
            const float probe[3] = { t, nextafterf(t, 0.0f),
                                     nextafterf(t, INFINITY) };
            uint32_t k;

            for (k = 0u; k < 3u; k++) {
                snprintf(msg, sizeof msg, "CB1 tie cc %u bpm %u k %u",
                         i, bpm, k);
                expect(crumpBit_divisionFor(probe[k], per_tick) ==
                       ref_divisionFor(probe[k], per_tick), msg);
            }
        }
    }
    /* MAX itself and around it, random targets and tempos. */
    for (i = 0u; i < 2000000u; i++) {
        const float per_tick = rndf(0.4f, 30000.0f);
        const float t = (i & 1u)
            ? rndf(0.0f, 2.0f * CRUMPBIT_DELAY_MAX_SAMPLES)
            : CRUMPBIT_DELAY_MAX_SAMPLES * rndf(0.999f, 1.001f);

        snprintf(msg, sizeof msg, "CB1 random %u", i);
        expect(crumpBit_divisionFor(t, per_tick) ==
               ref_divisionFor(t, per_tick), msg);
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

/* CB4 + CB5: the whole block --------------------------------------------- */
#define CB_ARENA 126688u

static uint8_t arena_new[CB_ARENA];
static uint8_t arena_ref[CB_ARENA];

static void law_cb4(void)
{
    static const uint32_t shares[] = { CB_ARENA, 73696u, 40000u, 70592u };
    CrumpBitRuntime rt;
    RefRuntime ref;
    fx_share_t share_new;
    fx_share_t share_ref;
    float l_new[32], r_new[32], l_ref[32], r_ref[32];
    uint32_t block;
    uint32_t share_i = 0u;
    char msg[128];

    memset(arena_new, 0x55, sizeof arena_new);
    memcpy(arena_ref, arena_new, sizeof arena_ref);
    memset(&rt, 0, sizeof rt);
    memset(&ref, 0, sizeof ref);
    crumpBit_ops.init(&rt, NULL);
    ref_init(&ref);
    expect(rt.ac_c_l == 128 && rt.ac_c_r == 128, "CB5 AC memory seeded 128");
    share_new.base = arena_new;
    share_new.offset = 0u;
    share_new.bytes = shares[0];
    share_ref = share_new;
    share_ref.base = arena_ref;

    for (block = 0u; block < 82800u; block++) {    /* 60 s at 32 frames */
        effect_io_t io_new;
        effect_io_t io_ref;
        const uint32_t mono = ((block / 997u) % 7u) == 3u;
        const uint32_t frames = ((block % 211u) == 5u)
            ? 1u + (rnd() % 32u) : 32u;
        const uint32_t kind = (block / 400u) % 5u;
        uint32_t n;

        /* Parameter jumps and LFO-like rewrites. */
        if ((block % 13u) == 0u || (block % 1000u) < 40u) {
            static const uint8_t rows[] = {
                CRUMPBIT_PARAM_BIT_OFF, CRUMPBIT_PARAM_BIT_INVERT,
                CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK,
                CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SYNC,
                CRUMPBIT_PARAM_DLY_PAN };
            const uint8_t row = rows[rnd() % 7u];
            uint8_t v = (uint8_t)(rnd() & 0x7Fu);

            if (row == CRUMPBIT_PARAM_BIT_OFF ||
                row == CRUMPBIT_PARAM_BIT_INVERT)
                v = ((rnd() & 3u) == 0u) ? (uint8_t)rnd() : 0u;
            crumpBit_ops.write_param(&rt, row, v);
            ref_writeParam(&ref, row, v);
        }
        if ((block % 5000u) == 2500u)
            cb_bpm = (uint16_t)(20u + rnd() % 280u);
        /* Share changes (voice units claimed or released). */
        if ((block % 9000u) == 8999u) {
            share_i = (share_i + 1u) % 4u;
            share_new.bytes = shares[share_i];
            share_ref.bytes = shares[share_i];
            crumpBit_ops.buffer_changed(&rt, &share_new);
            (void)ref_seat(&ref, &share_ref);
        }
        for (n = 0u; n < frames; n++) {
            float a;
            float b;

            switch (kind) {
            case 0: a = rndf(-1.0f, 1.0f); b = rndf(-1.0f, 1.0f); break;
            case 1: a = 0.0f; b = 0.0f; break;                    /* silence */
            case 2: a = rndf(-3.0f, 3.0f); b = rndf(-3.0f, 3.0f); break;
            case 3: a = 1.0f; b = -1.0f; break;                   /* full DC */
            default: a = 0.25f * sinf((float)(block * 32u + n) * 0.01f);
                     b = a * 0.5f; break;
            }
            l_new[n] = a; r_new[n] = b; l_ref[n] = a; r_ref[n] = b;
        }
        io_new.l = l_new; io_new.r = mono ? NULL : r_new;
        io_new.frames = (uint8_t)frames; io_new.channels = mono ? 1u : 2u;
        io_new.share = &share_new;
        io_ref = io_new;
        io_ref.l = l_ref; io_ref.r = mono ? NULL : r_ref;
        io_ref.share = &share_ref;
        crumpBit_ops.process(&rt, &io_new);
        ref_process(&ref, &io_ref);

        for (n = 0u; n < frames; n++) {
            snprintf(msg, sizeof msg, "CB4 block %u frame %u", block, n);
            expect(same_bits(l_new[n], l_ref[n]), msg);
            if (!mono)
                expect(same_bits(r_new[n], r_ref[n]), msg);
        }
        snprintf(msg, sizeof msg, "CB4 state block %u", block);
        expect(rt.length == ref.length && rt.write_pos == ref.write_pos &&
               rt.valid == ref.valid && same_bits(rt.delay, ref.delay) &&
               same_bits(rt.mix, ref.mix) &&
               same_bits(rt.feedback, ref.feedback) &&
               same_bits(rt.gain_l, ref.gain_l) &&
               same_bits(rt.gain_r, ref.gain_r) &&
               same_bits(rt.ac_y_l, ref.ac_y_l) &&
               same_bits(rt.ac_y_r, ref.ac_y_r) &&
               same_bits((float)(rt.ac_c_l - 128) * CRUMPBIT_LSB,
                         ref.ac_x_l) &&
               same_bits((float)(rt.ac_c_r - 128) * CRUMPBIT_LSB,
                         ref.ac_x_r), msg);
    }
    expect(memcmp(arena_new, arena_ref, sizeof arena_new) == 0,
           "CB4 loop bytes identical after 60 s");
}

int main(void)
{
    law_cb1();
    law_cb2();
    law_cb3();
    law_cb4();
    printf("crumpbit failures: %d\n", failures);
    return failures ? 1 : 0;
}
