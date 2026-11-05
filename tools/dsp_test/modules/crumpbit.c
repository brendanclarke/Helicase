/*
 * CrumpBit host tests (S091 P1 reductions; S091 P2 expansion; S091 P3
 * sub-types; S091 P4 `grn` rework and `bal`; S091 P5 efficiencies).
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
 *                    (which the firmware recomputes from raw bytes), and
 *                    (S091 P5, plan Q8) the Q14 tap read (B) and the
 *                    feedback and balance gains stepped per block (S1;
 *                    `mix` still ramps, Q9), over
 *                    60 s of random blocks with parameter jumps (every row,
 *                    every filter type), out-of-range input, mono
 *                    io->r == NULL, short blocks and share changes (tail
 *                    kept or re-seated); outputs, loop bytes and state are
 *                    compared bit for bit, and the seat invariants
 *                    (delay <= length - 2, write_pos < length,
 *                    valid <= length) are checked after every block;
 *                    S091 P3/P4: the sub-type rows (aux, st2, st3, spr,
 *                    den, len, wid) are written too and must not change
 *                    `dly` (`bal`, which does, has its own law CB16);
 *               CB5  init: AC memory 128, tone flat, filter `off` (and its
 *                    ramp origin `off`); S091 P3: sub `dly`, aux/st2/st3 63;
 *                    S091 P4: den 64 (+1), len 127, bal 63 (the copy
 *                    setting and a centred balance);
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
 *                    S091 P5 A: effective_max(dpn) is 255 in every
 *                    sub-type, and the Position fit `grn` caches after a
 *                    block equals crumpBit_grainPosLimit()'s for the share,
 *                    across shares and a tempo change; the one-pole k
 *                    table (F1) is -3 dB at the SVF's res-0 frequency;
 *               CB7  output resonance bound: crumpBit_filterDamping() is in
 *                    [0.0625, 1] for every `res` (Q <= 8), 1 at res 0;
 *               CB8  stability at fbk 127 (gain 1.2; `dly`, the tape
 *                    curve; `grn`'s echo is CB18): 60 s of
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
 *                    S091 P5: the switches cross the SVF and one-pole
 *                    filters (CB19 checks the hand-over itself);
 *               CB12 `cmb` tuning: the first echo of an impulse arrives at
 *                    crumpBit_combTable[rte] (+-2 samples); `snc` inverts
 *                    the second echo; combNote() is 112 - rte, held at 96;
 *               CB13 `pit` ratio: +-12 st (snapped) moves a 441 Hz sine's
 *                    energy to 882 / 220.5 Hz (Goertzel, >= 10x the input
 *                    frequency), 0 st keeps it;
 *               CB14 `grn` (S091 P4 model): the pitch-slot distribution
 *                    over the absolute P1..P3 (spr 0 P1 only, 40 halves,
 *                    80 thirds, 127 detune within +-1 st); the Width
 *                    distributions (0 centre, 64 uniform, 127 50-100 %
 *                    sides); the Position laws (0 two behind the head,
 *                    positive exactly the stop at the tempo, negative inside
 *                    the scatter range); no read ever reaches unrecorded or
 *                    overwritten audio (the overwrite clamp, every row);
 *                    the copy identity (Density +1, Length full, Position
 *                    0: output = the recorded signal two frames late plus
 *                    its delay-line echo, across retriggers); rhythmic
 *                    Density intervals exact, random ones of the right
 *                    mean, off = none; freeze keeps the record line
 *                    bit-exact and anchors Position; stability at fbk 127;
 *                    S091 P5: Position beyond the line plays the last
 *                    fitting stop (A); the candidate takes the fit as an
 *                    argument; every voice's end lands on a block boundary
 *                    (G3: after every 32-frame block, end - age is a whole
 *                    number of blocks), across steals and retriggers;
 *               CB15 `rvb`: stable and bounded at every extreme of rte,
 *                    fbk, snc, ton and dpn; an RT60 of ~0.49 s (fbk 44)
 *                    decays 9..21 dB over 0.125 s; freeze holds the tail
 *                    within +-6 dB over 1 s; S091 P5 R1: every `ton`
 *                    above 63 plays bit-identically to 63;
 *               CB16 `bal` (S091 P4): with balance b, the left channel is
 *                    bit-identical to a run at frq - b and balance 0, the
 *                    right to one at frq + b, for every filter type; S091
 *                    P5: on the SVF (`dly`) and the one-pole (`rvb`) path;
 *               CB17 (S091 P5 X2) the data-line table equals
 *                    (c & keep) ^ invert for all 256 codes after the first
 *                    block and after every `bof`/`biv` change;
 *               CB18 (S091 P5 G4) the `grn` echo: `fbk` 127 is exactly
 *                    0.98; with no input the delay line's peak never grows
 *                    echo to echo and settles inside the 8-bit rounding
 *                    floor, |code| < 0.5 / (1 - gain), at every `fbk`;
 *               CB19 (S091 P5 F1) the one-pole output filter of `grn`/
 *                    `rvb`: -3 dB at the SVF's res-0 frequency (+-0.3 dB,
 *                    sines through the whole Effect, mix 0); the type
 *                    mapping (LP = LP2 = BP = UBP, HP = notch, peak = off,
 *                    bit for bit; off passes the dry signal exactly); `res`
 *                    has no effect; a switch hands the state over (SVF low
 *                    integrator -> one-pole state -> SVF low integrator).
 * Why:        the board cannot report output sample by sample; the
 *             reference proves the code computes exactly the documented
 *             equations (including that the ramp-origin trim is exact and
 *             that a flat tone and a settled `off` filter pass bit-exactly),
 *             and the ARM shape is gated on the linked ELF
 *             (crumpbit_elf_check).
 * Inputs:     build/gen/cb_*.c|h, CrumpBitEffect.host.h/.c (extract.py).
 * Outputs:    one line per failure (first 40); "crumpbit failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test crumpbit (18 break builds follow).
 * Affiliates: CrumpBitEffect.c/.h; CrumpBitTables.c; CrumpBitParameters.h;
 *             StepScale.c; S091_P2_Implementation.md §5;
 *             S091_P3_Implementation.md (host laws CB4-CB15);
 *             S091_P5_IMPLEMENTATION.md (CB17-CB19).
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
 * - ramp origins (mix and the output mix cx/cb/cl) are STORED floats, as
 *   S074 stored them, not recomputed from raw bytes;
 * - S091 P5 (plan Q8): the tap is read in Q14 as the plan states it
 *   (trunc(delay 2^14), a wrap by branch, the integer interpolation, /2^21,
 *   the gate d + 2 <= valid in 64-bit integers), not through the
 *   firmware's mask and fixed-point VCVT forms; the feedback and the balance
 *   gains are this block's targets on every frame (S1, no ramp);
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
    float max, target, per_tick, free_run, synced;
    float mix, fb, gl, gr, mix_to;
    float mix_inc, per_frame;
    float k_tone, g, r, a1, a2, a3, c_to[3], cx_inc, cb_inc, cl_inc;
    float cx, cb, cl;
    uint32_t keep, invert, wp, valid, n;
    uint8_t t;

    if (len < 4u)
        return;
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
    /* S1: the block's feedback and balance gains, every frame. */
    fb = ref2_fbGain(rt->feedback_raw);
    gl = (rt->pan_raw <= 63u) ? 1.0f
        : (float)(127u - rt->pan_raw) * (1.0f / 64.0f);
    gr = (rt->pan_raw >= 63u) ? 1.0f
        : (float)rt->pan_raw * (1.0f / 63.0f);
    mix = rt->mix;
    mix_inc = (mix_to - mix) * per_frame;
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
        float wet, dc, tilt, ret;
        int32_t d14, rp, q;
        uint32_t i0, i1;

        rt->ac_x_l = yl;
        rt->ac_y_l = hl;
        rt->ac_x_r = yr;
        rt->ac_y_r = hr;
        mix += mix_inc;
        rt->delay += (target - rt->delay) * CRUMPBIT_GLIDE_K;
        /* S091 P5 B: position wp - d in Q14, wrapped once, interpolated. */
        d14 = (int32_t)(rt->delay * 16384.0f);
        rp = (int32_t)(wp * 16384u) - d14;
        if (rp < 0)
            rp += (int32_t)(len * 16384u);
        i0 = (uint32_t)rp / 16384u;
        i1 = (i0 + 1u == len) ? 0u : i0 + 1u;
        q = (int32_t)loop[i0] * 16384 +
            (rp % 16384) * ((int32_t)loop[i1] - (int32_t)loop[i0]);
        wet = ((int64_t)d14 + 2 * 16384 <= (int64_t)valid * 16384)
            ? (float)q / 2097152.0f : 0.0f;
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
        CRUMPBIT_PARAM_GRAIN_DENSITY, CRUMPBIT_PARAM_GRAIN_LENGTH,
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
    expect(rt.grain_density == 64u && rt.grain_length == 127u,
           "CB5 grn copy setting (den +1, len full)");
    expect(rt.filter_bal == 63u, "CB5 balance centred");
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
    const float grain_max = (half < 512u) ? 0.0f : (float)(half - 2u);
    /* S091 P4: the grn delay-time curve is in ticks at the test tempo. */
    const float grain_ticks = grain_max * (float)(cb_bpm ? cb_bpm : 1u) /
                              CRUMPBIT_SAMPLES_PER_TICK;

    switch (sub) {
    case CRUMPBIT_SUBTYPE_COMB:   return ref_tableScan(crumpBit_combTable, max);
    case CRUMPBIT_SUBTYPE_PITCH:  return ref_tableScan(crumpBit_pitchWindow,
                                                       max - 2.0f);
    case CRUMPBIT_SUBTYPE_GRAIN:  return ref_tableScan(crumpBit_grainTime,
                                                       grain_ticks);
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
    expect(fabsf(crumpBit_grainFraction[0] - 1.0f / 256.0f) < 1e-9f,
           "CB6 grain length 1/256 of N");
    expect(crumpBit_grainFraction[CRUMPBIT_GRAIN_STEPS] == 1.0f,
           "CB6 grain length N");
    /* S091 P4: the delay-time curve and its stops (Q8, Q10). */
    expect(crumpBit_grainTime[0] == 6.0f && crumpBit_grainTime[127] == 384.0f,
           "CB6 grain time 1/64 .. 1 bar");
    {
        static const float stop_ticks[CRUMPBIT_GRAIN_STOPS] = {
            6, 8, 12, 16, 18, 24, 32, 36, 48, 64, 72, 96, 144, 192, 288, 384 };
        uint32_t k;

        for (k = 0u; k < CRUMPBIT_GRAIN_STOPS; k++) {
            snprintf(msg, sizeof msg, "CB6 grain stop %u", k);
            expect(crumpBit_grainTime[crumpBit_grainStopCc[k]] ==
                   stop_ticks[k], msg);
            if (k)
                expect(crumpBit_grainStopCc[k] > crumpBit_grainStopCc[k - 1u],
                       msg);
            /* The first 14 are StepScale's stops, by tick count (Q8). */
            if (k < 14u) {
                uint32_t cc;
                uint32_t hit = 0u;

                for (cc = 0u; cc < STEP_SCALE_COUNT; cc++)
                    hit |= (stepScale_ticksQ8((uint8_t)cc) ==
                            (uint16_t)(stop_ticks[k] * 256.0f)) ? 1u : 0u;
                expect(hit != 0u, msg);
            }
        }
    }
    expect(crumpBit_grainDensity[1] == 1.0f &&
           fabsf(crumpBit_grainDensity[63] - 882.0f / 63328.0f) < 1e-6f,
           "CB6 grain density N .. 20 ms");
    for (i = 2u; i <= CRUMPBIT_DENSITY_STEPS; i++) {
        snprintf(msg, sizeof msg, "CB6 grain density %u", i);
        expect(crumpBit_grainDensity[i] < crumpBit_grainDensity[i - 1u], msg);
    }
    for (i = 1u; i < 128u; i++) {
        snprintf(msg, sizeof msg, "CB6 grain time %u", i);
        expect(crumpBit_grainTime[i] > crumpBit_grainTime[i - 1u], msg);
    }
    /*
     * Position's limit (the page label): 63 + 4 x (stops that fit). S091
     * P5 A: the manager no longer clamps `dpn` (255 in every sub-type);
     * the engine's own fit, cached by a `grn` block, is the label's.
     */
    for (bytes = 0u; bytes <= CRUMPBIT_LOOP_MAX_BYTES; bytes += 1024u) {
        fx_share_t share = { NULL, 0u, bytes };
        const uint32_t half = bytes / 2u;
        const float gmax = (half < 512u) ? 0.0f : (float)(half - 2u);
        const float tmax = gmax * (float)cb_bpm / CRUMPBIT_SAMPLES_PER_TICK;
        uint32_t fit = 0u;
        uint32_t k;

        for (k = 0u; k < CRUMPBIT_GRAIN_STOPS; k++)
            fit += (crumpBit_grainTime[crumpBit_grainStopCc[k]] <= tmax);
        snprintf(msg, sizeof msg, "CB6 grn position limit bytes %u", bytes);
        expect(crumpBit_grainPosLimit(&share) == 63u + 4u * fit, msg);
        for (sub = 0u; sub < CRUMPBIT_SUBTYPE_COUNT; sub++) {
            crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_SUBTYPE,
                                     (uint8_t)sub);
            expect(crumpBit_ops.effective_max(&rt, CRUMPBIT_PARAM_DLY_PAN,
                                              &share) == 255u, msg);
        }
    }
    {
        /* The engine's cached fit across shares and a tempo change. */
        static const uint16_t bpms[3] = { 120u, 47u, 300u };
        CrumpBitRuntime g;
        float l[32];
        float r[32];
        uint32_t t;

        memset(l, 0, sizeof l);
        memset(r, 0, sizeof r);
        memset(&g, 0, sizeof g);
        crumpBit_ops.init(&g, NULL);
        crumpBit_ops.write_param(&g, CRUMPBIT_PARAM_SUBTYPE,
                                 CRUMPBIT_SUBTYPE_GRAIN);
        for (t = 0u; t < 3u; t++) {
            cb_bpm = bpms[t];
            for (bytes = 1024u; bytes <= CRUMPBIT_LOOP_MAX_BYTES;
                 bytes += 4096u) {
                fx_share_t share = { arena_new, 0u, bytes };
                effect_io_t io = { l, r, 32u, 2u, &share };

                crumpBit_ops.process(&g, &io);
                snprintf(msg, sizeof msg,
                         "CB6 grn engine fit bpm %u bytes %u: %u vs %u",
                         cb_bpm, bytes, g.u.grn.fit,
                         (crumpBit_grainPosLimit(&share) - 63u) / 4u);
                expect(g.u.grn.fit ==
                       (crumpBit_grainPosLimit(&share) - 63u) / 4u, msg);
            }
        }
        cb_bpm = 120u;
    }
    /*
     * S091 P5 F1: the one-pole k is -3 dB where the SVF at res 0 is,
     * tan(w/2) = g sqrt(sqrt 2 - 1), for every index above 0 (0: closed).
     */
    expect(crumpBit_onePoleK[0] == 0.0f, "CB6 one-pole k[0] = 0");
    for (i = 1u; i < 128u; i++) {
        const double w = 2.0 * atan((double)crumpBit_filterG[i] *
                                    sqrt(sqrt(2.0) - 1.0));
        const double k = crumpBit_onePoleK[i];
        const double a = 1.0 - k;
        const double g2 = k * k / (1.0 - 2.0 * a * cos(w) + a * a);

        snprintf(msg, sizeof msg, "CB6 one-pole k %u: |H|^2 %.6f", i, g2);
        expect(fabs(g2 - 0.5) < 1e-5 && k < 1.0 &&
               crumpBit_onePoleK[i] > crumpBit_onePoleK[i - 1u], msg);
    }
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
    expect((CRUMPBIT_ROWS_SHARED &
            CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_BAL)) != 0u,
           "CB9 bal is shared (output filter)");
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
            /* S091 P4: `bal` has no lane (Q11). */
            expect(row != CRUMPBIT_PARAM_FILTER_BAL, msg);
        }
    }
    {
        /* S091 P4 (Q11): the grn pool carries its performance controls. */
        const uint8_t *g = crumpBit_laneMap(CRUMPBIT_SUBTYPE_GRAIN);

        expect(g[4] == CRUMPBIT_PARAM_GRAIN_DENSITY &&
               g[5] == CRUMPBIT_PARAM_RATE && g[6] == CRUMPBIT_PARAM_SYNC &&
               g[9] == CRUMPBIT_PARAM_DLY_PAN &&
               g[10] == CRUMPBIT_PARAM_GRAIN_LENGTH &&
               g[14] == CRUMPBIT_PARAM_AUX &&
               g[15] == CRUMPBIT_PARAM_GRAIN_SPREAD, "CB9 grn lane pool");
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
                    CRUMPBIT_PARAM_GRAIN_DENSITY, CRUMPBIT_PARAM_GRAIN_LENGTH,
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

    /*
     * S091 P4: the seat's tape-delay clamp, deterministically (it was only
     * met by chance before): a full-length delay, then a share shrink, must
     * leave delay <= length - 2 at once (break 4 removes the clamp).
     */
    {
        fx_share_t big = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        fx_share_t small = { arena_p3, 0u, 4096u };
        float bl[32], br[32];

        memset(bl, 0, sizeof bl);
        memset(br, 0, sizeof br);
        p3_init(&rt, CRUMPBIT_SUBTYPE_DELAY);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, 127u, &big);
        p3_block(&rt, &big, bl, br, 32u);
        crumpBit_ops.buffer_changed(&rt, &small);
        expect(rt.u.tap.delay <= (float)(rt.u.tap.length - 2u),
               "CB11 seat: a share shrink clamps the tape delay");
    }
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

            /* S091 P4: voices inside the line, ages within their ends. */
            expect(g->length == 0u ||
                   (g->write_pos < g->length && g->valid <= g->length &&
                    (g->length < CRUMPBIT_GRAIN_LINE_MIN ||
                     ((g->pos_a >> 16) < g->length &&
                      (g->pos_b >> 16) < g->length &&
                      g->age_a <= g->end_a && g->age_b <= g->end_b &&
                      g->end_a <= g->length + CRUMPBIT_GRAIN_FADE &&
                      g->end_b <= g->length + CRUMPBIT_GRAIN_FADE &&
                      (g->anchor == CRUMPBIT_GRAIN_NO_ANCHOR ||
                       g->anchor < g->length) &&
                      g->pending <= 1u + 32u + CRUMPBIT_GRAIN_FADE))), msg);
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

/* CB14: `grn` (S091 P4 model) ---------------------------------------------- */

/* The S074-form dry path and the recorded mono value, for CB14's copy law. */
typedef struct {
    float ax_l, ax_r, ay_l, ay_r;
} cb_dry_t;

static float cb_recorded(cb_dry_t *d, float l, float r)
{
    const float yl = (float)((int32_t)ref_adc(l) - 128) * CRUMPBIT_LSB;
    const float yr = (float)((int32_t)ref_adc(r) - 128) * CRUMPBIT_LSB;
    const float hl = yl - d->ax_l + CRUMPBIT_AC_R * d->ay_l;
    const float hr = yr - d->ax_r + CRUMPBIT_AC_R * d->ay_r;

    d->ax_l = yl;
    d->ay_l = hl;
    d->ax_r = yr;
    d->ay_r = hr;
    /* The record line holds the 8-bit code of the mono dry signal. */
    return (float)((int32_t)crumpBit_adc(0.5f * (hl + hr)) - 128) *
           CRUMPBIT_LSB;
}

static void law_cb14(void)
{
    static const uint8_t spreads[] = { 0u, 40u, 80u, 127u };
    const float per_tick = CRUMPBIT_SAMPLES_PER_TICK / 120.0f;
    CrumpBitRuntime rt;
    uint32_t s;
    uint32_t i;
    char msg[160];

    cb_bpm = 120u;

    /* (a) pitch slots over the absolute P1..P3, and the detune bound. */
    memset(&rt, 0, sizeof rt);
    crumpBit_ops.init(&rt, NULL);
    rt.aux_raw = 63u;                   /* P1 0 st */
    rt.grain_st2 = 68u;                 /* P2 +5 st (absolute) */
    rt.grain_st3 = 72u;                 /* P3 +9 st (absolute) */
    rt.grain_length = 63u;              /* shortest, forward */
    rt.pan_raw = 63u;                   /* Position 0 */
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

            crumpBit_grainPick(&hash, &rt, 60000u, 30000u, 60000u, 0u,
                               per_tick, CRUMPBIT_GRAIN_STOPS, &c);
            for (k = 0u; k < 3u; k++)
                if (c.inc == slot_inc[k])
                    slot = k;
            if (slot == 3u) {
                /* Off-slot: only above spr 80, within +-1 st (+ Q3.12). */
                double best = 1e9;

                for (k = 0u; k < 3u; k++) {
                    const double cents = 1200.0 *
                        log2((double)c.inc / (double)slot_inc[k]);

                    best = (fabs(cents) < fabs(best)) ? cents : best;
                }
                snprintf(msg, sizeof msg, "CB14 spr %u detune %.2f cents",
                         spreads[s], best);
                expect(spreads[s] > 80u && fabs(best) <= 101.0, msg);
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

    /* (b) Width: 0 centre, 64 uniform over the field, 127 sides only. */
    {
        static const uint8_t widths[3] = { 0u, 64u, 127u };
        uint32_t w;

        rt.grain_spread = 0u;
        for (w = 0u; w < 3u; w++) {
            uint32_t hash = 0xBEEFu;
            uint32_t lo = 255u;
            uint32_t hi = 0u;
            uint32_t left = 0u;
            uint32_t right = 0u;
            uint32_t bad = 0u;
            double sum = 0.0;

            rt.grain_width = widths[w];
            for (i = 0u; i < 20000u; i++) {
                crumpbit_grain_t c;

                crumpBit_grainPick(&hash, &rt, 60000u, 30000u, 60000u, 0u,
                                   per_tick, CRUMPBIT_GRAIN_STOPS, &c);
                lo = (c.pan < lo) ? c.pan : lo;
                hi = (c.pan > hi) ? c.pan : hi;
                sum += c.pan;
                left += (c.pan < 127u);
                right += (c.pan > 127u);
                if (widths[w] == 0u)
                    bad += (c.pan != 127u);
                if (widths[w] == 127u)
                    bad += (c.pan > 64u && c.pan < 190u);
            }
            snprintf(msg, sizeof msg,
                     "CB14 wid %u: pans %u..%u mean %.1f L %u R %u bad %u",
                     widths[w], lo, hi, sum / 20000.0, left, right, bad);
            expect(bad == 0u, msg);
            if (widths[w] == 64u)
                expect(lo <= 4u && hi >= 250u &&
                       fabs(sum / 20000.0 - 127.0) < 4.0, msg);
            if (widths[w] == 127u)
                expect(left > 9000u && right > 9000u, msg);
        }
        rt.grain_width = 0u;
    }

    /*
     * (c) Position laws, at unity pitch (no headroom). S091 P5 A: a stop
     * longer than the line plays the last stop that fits it (`fit`, as the
     * engine works it out); a fitting stop not yet recorded still waits.
     */
    for (i = 0u; i < 128u; i++) {
        crumpbit_grain_t c;
        uint32_t hash = 0x777u + i;
        const uint32_t len = 60000u;
        const uint32_t ref = 1000u;
        const uint32_t fit = crumpBit_grainStopFit(
            crumpBit_grainMaxTicks((float)(len - 2u), 120u));
        uint32_t lag;

        rt.pan_raw = (uint8_t)i;
        rt.aux_raw = 63u;
        rt.grain_length = 63u;
        crumpBit_grainPick(&hash, &rt, len, ref, len, 0u, per_tick, fit, &c);
        lag = (ref >= c.start) ? ref - c.start : ref + len - c.start;
        snprintf(msg, sizeof msg, "CB14 position %u lag %u end %u fit %u", i,
                 lag, c.end, fit);
        if (i == 63u) {
            expect(lag == 2u && c.end != 0u, msg);
        } else if (i > 63u) {
            uint32_t stop = crumpBit_grainPosStop((uint8_t)i);
            float want;

            stop = (stop < fit) ? stop : fit - 1u;
            want = crumpBit_grainTime[crumpBit_grainStopCc[stop]] * per_tick;
            expect(fit == 14u && want <= (float)(len - 2u) &&
                   lag == (uint32_t)want && c.end != 0u, msg);
            /* Recorded only half as far back as the lag: it waits. */
            hash = 0x777u + i;
            crumpBit_grainPick(&hash, &rt, len, ref, (uint32_t)want / 2u, 0u,
                               per_tick, fit, &c);
            expect(c.end == 0u, msg);
        } else {
            const float m = (float)(63u - i) / 63.0f;

            expect(lag >= 2u &&
                   (float)lag <= 2.0f + m * (float)(len - 4u) + 1.0f, msg);
        }
    }

    /* (d) No read reaches unrecorded or overwritten audio (Q3). */
    for (i = 0u; i < 300000u; i++) {
        crumpbit_grain_t c;
        uint32_t hash = rnd() | 1u;
        const uint32_t len = 1000u + rnd() % 62000u;
        const uint32_t valid = (rnd() & 1u) ? len : rnd() % (len + 1u);
        const uint32_t ref = rnd() % len;
        const uint32_t frozen = ((rnd() & 3u) == 0u);
        float r;
        float gr;
        float lag0;
        float lag_end;
        float max_lag;

        rt.aux_raw = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_st2 = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_st3 = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_spread = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_length = (uint8_t)(rnd() & 0x7Fu);
        rt.pan_raw = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_width = (uint8_t)(rnd() & 0x7Fu);
        crumpBit_grainPick(&hash, &rt, len, ref, valid, frozen,
                           rndf(30.0f, 1500.0f),
                           rnd() % (CRUMPBIT_GRAIN_STOPS + 1u), &c);
        if (c.end == 0u || (frozen && valid >= len))
            continue;
        r = (float)(c.inc < 0 ? -c.inc : c.inc) / 4096.0f;
        gr = (frozen ? 0.0f : 1.0f) + ((c.inc < 0) ? r : -r);
        lag0 = (float)((ref >= c.start) ? ref - c.start : ref + len - c.start);
        lag_end = lag0 + gr * (float)(c.end - 1u);
        max_lag = (float)((valid < len) ? valid : len) - 2.0f;
        snprintf(msg, sizeof msg,
                 "CB14 safety %u: lag %.1f -> %.1f max %.1f end %u r %.3f%s",
                 i, (double)lag0, (double)lag_end, (double)max_lag, c.end,
                 (double)r, frozen ? " frozen" : "");
        expect(lag0 >= 2.0f && lag0 <= max_lag + 0.01f &&
               lag_end >= 1.0f && lag_end <= max_lag + 1.0f, msg);
    }

    /* (e) The copy identity (A1.9), across two retriggers. */
    {
        fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        const uint32_t len = CRUMPBIT_LOOP_MAX_BYTES / 2u;
        const uint32_t d = (uint32_t)(crumpBit_grainTime[0] * per_tick);
        const uint32_t total = 3u * len + 4096u;
        float *rec = calloc(total + 64u, sizeof(float));
        cb_dry_t dry = { 0.0f, 0.0f, 0.0f, 0.0f };
        uint32_t n = 0u;
        uint32_t bad = 0u;
        float worst = 0.0f;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_GRAIN);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 0u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, 0u, &share);
        while (n < total) {
            float l[32], r[32];
            uint32_t k;

            for (k = 0u; k < 32u; k++) {
                l[k] = rndf(-0.9f, 0.9f);
                r[k] = rndf(-0.9f, 0.9f);
                rec[n + k] = cb_recorded(&dry, l[k], r[k]);
            }
            p3_block(&rt, &share, l, r, 32u);
            for (k = 0u; k < 32u; k++, n++) {
                /* After the first grain's fade-in and its first echo. */
                if (n >= 32u + 128u + d + 64u) {
                    const float want = rec[n - 2u] + rec[n - 2u - d];
                    const float err = fabsf(l[k] - want);

                    worst = (err > worst) ? err : worst;
                    bad += (err > 2e-6f || fabsf(r[k] - want) > 2e-6f);
                }
            }
        }
        snprintf(msg, sizeof msg,
                 "CB14 copy identity: %u frames off (worst %.3g)", bad,
                 (double)worst);
        expect(bad == 0u, msg);
        free(rec);
    }

    /* (f) Density: rhythmic intervals exact, random mean, off = none. */
    {
        static const uint8_t dens[3] = { 100u, 20u, 63u };
        fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        const float flen = (float)(CRUMPBIT_LOOP_MAX_BYTES / 2u);
        uint32_t t;

        for (t = 0u; t < 3u; t++) {
            uint32_t block;
            uint32_t starts = 0u;
            uint32_t last = 0u;
            uint32_t wrong = 0u;
            double sum = 0.0;
            float want_iv;

            memset(arena_p3, 0, sizeof arena_p3);
            p3_init(&rt, CRUMPBIT_SUBTYPE_GRAIN);
            p3_write(&rt, CRUMPBIT_PARAM_GRAIN_LENGTH, 63u, &share);
            p3_write(&rt, CRUMPBIT_PARAM_GRAIN_DENSITY, dens[t], &share);
            want_iv = (dens[t] > 64u)
                ? crumpBit_grainTime[crumpBit_grainStopCc[
                      crumpBit_grainDenStop(dens[t])]] * per_tick
                : flen * crumpBit_grainDensity[63u - dens[t]];
            for (block = 0u; block < 60000u; block++) {
                float l[32], r[32];
                uint32_t k;

                for (k = 0u; k < 32u; k++) {
                    l[k] = rndf(-0.5f, 0.5f);
                    r[k] = l[k];
                }
                p3_block(&rt, &share, l, r, 32u);
                /* A voice started in this block has age <= 32. */
                for (k = 0u; k < 2u; k++) {
                    const uint32_t age = k ? rt.u.grn.age_b : rt.u.grn.age_a;
                    const uint32_t end = k ? rt.u.grn.end_b : rt.u.grn.end_a;

                    if (end != 0u && age <= 32u) {
                        const uint32_t at = block * 32u + 32u - age;

                        if (starts && dens[t] > 64u)
                            wrong += (at - last != (uint32_t)want_iv);
                        if (starts)
                            sum += (double)(at - last);
                        last = at;
                        starts++;
                    }
                }
            }
            snprintf(msg, sizeof msg,
                     "CB14 density %u: %u starts, mean %.1f (want %.1f), %u off",
                     dens[t], starts, starts > 1u ? sum / (starts - 1u) : 0.0,
                     (double)want_iv, wrong);
            if (dens[t] == 63u)
                expect(starts == 0u, msg);
            else if (dens[t] > 64u)
                expect(starts > 100u && wrong == 0u, msg);
            else
                expect(starts > 100u &&
                       fabs(sum / (starts - 1u) - want_iv) < 0.1 * want_iv,
                       msg);
        }
    }

    /* (g) Freeze keeps the record line bit-exact and anchors Position. */
    {
        fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        const uint32_t half = CRUMPBIT_LOOP_MAX_BYTES / 2u;
        uint8_t *snap_rec = malloc(half);
        uint8_t *snap_dly = malloc(half);
        float l[32], r[32];
        uint32_t block;
        uint32_t n;
        uint32_t anchor_ok = 1u;
        uint32_t frozen_at = 0u;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_GRAIN);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 100u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 64u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_DENSITY, 110u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_SPREAD, 100u, &share);
        for (block = 0u; block < 4000u; block++) {
            if (block == 2000u) {
                p3_write(&rt, CRUMPBIT_PARAM_SYNC, 1u, &share);
                memcpy(snap_rec, arena_p3, half);
                frozen_at = rt.u.grn.write_pos;
            }
            if (block == 2001u)
                memcpy(snap_dly, arena_p3 + half, half);
            for (n = 0u; n < 32u; n++) {
                l[n] = rndf(-0.8f, 0.8f);
                r[n] = rndf(-0.8f, 0.8f);
            }
            p3_block(&rt, &share, l, r, 32u);
            if (block >= 2000u)
                anchor_ok &= (rt.u.grn.anchor == frozen_at);
        }
        expect(memcmp(snap_rec, arena_p3, half) == 0,
               "CB14 freeze: record line bit-exact");
        expect(memcmp(snap_dly, arena_p3 + half, half) != 0,
               "CB14 freeze: delay line still running");
        expect(anchor_ok, "CB14 freeze: anchored at the freeze position");
        p3_write(&rt, CRUMPBIT_PARAM_SYNC, 0u, &share);
        p3_block(&rt, &share, l, r, 32u);
        expect(rt.u.grn.anchor == CRUMPBIT_GRAIN_NO_ANCHOR,
               "CB14 unfreeze releases the anchor");
        /* Anchored picks: Position 0 starts two behind the anchor. */
        {
            crumpbit_grain_t c;
            uint32_t hash = 99u;

            rt.pan_raw = 63u;
            rt.aux_raw = 63u;
            crumpBit_grainPick(&hash, &rt, half, frozen_at, half, 1u,
                               per_tick, CRUMPBIT_GRAIN_STOPS, &c);
            expect(c.start == ((frozen_at >= 2u) ? frozen_at - 2u
                                                 : frozen_at + half - 2u),
                   "CB14 frozen Position 0 = anchor - 2");
        }
        free(snap_rec);
        free(snap_dly);
    }

    /* (h) Stability at fbk 127 over sizes, directions and pitches. */
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
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_LENGTH,
                 (uint8_t)((s & 1u) ? 0u : 127u), &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_DENSITY,
                 (uint8_t)((s & 2u) ? 20u : 120u), &share);
        p3_write(&rt, CRUMPBIT_PARAM_AUX, (uint8_t)(s * 40u), &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_SPREAD, 127u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_WIDTH, 127u, &share);
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

    /*
     * (i) S091 P5 G3: every voice's end lands on a block boundary, so after
     * every 32-frame block a sounding voice has a whole number of blocks
     * left (end - age = 0 mod 32), and its window never goes below 0
     * inside a block. Steal-heavy: random Density (short intervals), long
     * grains, every Length and Position, retriggers, freezes.
     */
    {
        fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        float l[32], r[32];
        uint32_t block;
        uint32_t n;
        uint32_t bad = 0u;
        uint32_t steals = 0u;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_GRAIN);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
        for (block = 0u; block < 40000u; block++) {
            if ((block % 97u) == 0u) {
                p3_write(&rt, CRUMPBIT_PARAM_GRAIN_DENSITY,
                         (uint8_t)((rnd() & 1u) ? rnd() % 40u
                                                : 90u + rnd() % 38u), &share);
                p3_write(&rt, CRUMPBIT_PARAM_GRAIN_LENGTH,
                         (uint8_t)(rnd() & 0x7Fu), &share);
                p3_write(&rt, CRUMPBIT_PARAM_DLY_PAN,
                         (uint8_t)(rnd() & 0x7Fu), &share);
                p3_write(&rt, CRUMPBIT_PARAM_AUX, (uint8_t)(rnd() & 0x7Fu),
                         &share);
                p3_write(&rt, CRUMPBIT_PARAM_SYNC,
                         (uint8_t)((rnd() & 7u) == 0u), &share);
            }
            for (n = 0u; n < 32u; n++) {
                l[n] = rndf(-0.8f, 0.8f);
                r[n] = rndf(-0.8f, 0.8f);
            }
            p3_block(&rt, &share, l, r, 32u);
            steals += (rt.u.grn.pending != 0u);
            if (rt.u.grn.age_a < rt.u.grn.end_a)
                bad += ((rt.u.grn.end_a - rt.u.grn.age_a) % 32u) != 0u;
            if (rt.u.grn.age_b < rt.u.grn.end_b)
                bad += ((rt.u.grn.end_b - rt.u.grn.age_b) % 32u) != 0u;
        }
        snprintf(msg, sizeof msg,
                 "CB14 block-aligned ends: %u voices off (%u steal blocks)",
                 bad, steals);
        expect(bad == 0u && steals > 100u, msg);
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

    /*
     * (d) S091 P5 R1: the damping is low-pass only, so every `ton` above
     * 63 plays bit-identically to 63 (flat), whatever the other rows.
     */
    for (pass = 0u; pass < 8u; pass++) {
        static uint8_t arena_r1[2][16384];
        CrumpBitRuntime rr[2];
        fx_share_t sh[2];
        const uint8_t ton = (uint8_t)(64u + rnd() % 64u);
        const uint8_t fbk = (uint8_t)(rnd() & 0x7Fu);
        const uint8_t rte = (uint8_t)(rnd() & 0x7Fu);
        uint32_t bad = 0u;
        uint32_t block;
        uint32_t k;

        for (k = 0u; k < 2u; k++) {
            memset(arena_r1[k], 0, sizeof arena_r1[k]);
            sh[k].base = arena_r1[k];
            sh[k].offset = 0u;
            sh[k].bytes = sizeof arena_r1[k];
            p3_init(&rr[k], CRUMPBIT_SUBTYPE_REVERB);
            p3_write(&rr[k], CRUMPBIT_PARAM_MIX, 127u, &sh[k]);
            p3_write(&rr[k], CRUMPBIT_PARAM_FEEDBACK, fbk, &sh[k]);
            p3_write(&rr[k], CRUMPBIT_PARAM_RATE, rte, &sh[k]);
            p3_write(&rr[k], CRUMPBIT_PARAM_TONE,
                     k ? ton : CRUMPBIT_TONE_FLAT, &sh[k]);
        }
        for (block = 0u; block < 3000u; block++) {
            float l[2][32], r[2][32];
            uint32_t n;

            for (n = 0u; n < 32u; n++) {
                const float a = (block < 300u) ? rndf(-1.0f, 1.0f) : 0.0f;
                const float b = (block < 300u) ? rndf(-1.0f, 1.0f) : 0.0f;

                l[0][n] = l[1][n] = a;
                r[0][n] = r[1][n] = b;
            }
            for (k = 0u; k < 2u; k++)
                p3_block(&rr[k], &sh[k], l[k], r[k], 32u);
            for (n = 0u; n < 32u; n++)
                bad += !same_bits(l[0][n], l[1][n]) +
                       !same_bits(r[0][n], r[1][n]);
        }
        snprintf(msg, sizeof msg, "CB15 R1 ton %u = flat: %u samples differ",
                 ton, bad);
        expect(bad == 0u, msg);
    }
    free(out);
}

/*
 * CB16: `bal` (S091 P4) ------------------------------------------------------
 * S091 P5: run on `dly` (the SVF) and on `rvb` (the one-pole; `grn` would
 * also be, but it draws a hardware random word per block, so three runs
 * would not see the same engine output).
 */
static void law_cb16_on(uint8_t sub)
{
    static uint8_t arena_bal[3][4096];
    uint32_t pass;
    char msg[128];

    for (pass = 0u; pass < 24u; pass++) {
        /* A: frq f, bal 63 + d; B: frq f - d, bal 63; C: frq f + d. */
        const uint8_t type = (uint8_t)(pass % 7u);
        const int32_t d = (int32_t)(rnd() % 41u) - 20;
        const int32_t f = 30 + (int32_t)(rnd() % 68u);
        const uint8_t res = (uint8_t)(rnd() % 120u);
        CrumpBitRuntime rt[3];
        fx_share_t share[3];
        const uint8_t freq[3] = { (uint8_t)f, (uint8_t)(f - d),
                                  (uint8_t)(f + d) };
        const uint8_t bal[3] = { (uint8_t)(63 + d), 63u, 63u };
        uint32_t bad = 0u;
        uint32_t block;
        uint32_t k;

        for (k = 0u; k < 3u; k++) {
            share[k].base = arena_bal[k];
            share[k].offset = 0u;
            share[k].bytes = sizeof arena_bal[k];
            p3_init(&rt[k], sub);
            p3_write(&rt[k], CRUMPBIT_PARAM_FILTER_TYPE, type, &share[k]);
            p3_write(&rt[k], CRUMPBIT_PARAM_FILTER_FREQ, freq[k], &share[k]);
            p3_write(&rt[k], CRUMPBIT_PARAM_FILTER_RESO, res, &share[k]);
            p3_write(&rt[k], CRUMPBIT_PARAM_FILTER_BAL, bal[k], &share[k]);
        }
        for (block = 0u; block < 400u; block++) {
            float l[3][32], r[3][32];
            uint32_t n;

            for (n = 0u; n < 32u; n++) {
                const float a = rndf(-1.0f, 1.0f);
                const float b = rndf(-1.0f, 1.0f);

                for (k = 0u; k < 3u; k++) {
                    l[k][n] = a;
                    r[k][n] = b;
                }
            }
            for (k = 0u; k < 3u; k++)
                p3_block(&rt[k], &share[k], l[k], r[k], 32u);
            for (n = 0u; n < 32u; n++) {
                bad += !same_bits(l[0][n], l[1][n]);
                bad += !same_bits(r[0][n], r[2][n]);
            }
        }
        snprintf(msg, sizeof msg,
                 "CB16 bal sub %u pass %u type %u frq %d d %d: %u samples "
                 "differ", sub, pass, type, f, d, bad);
        expect(bad == 0u, msg);
    }
}

static void law_cb16(void)
{
    law_cb16_on(CRUMPBIT_SUBTYPE_DELAY);
    law_cb16_on(CRUMPBIT_SUBTYPE_REVERB);
}

/* CB17 (S091 P5 X2): the data-line table ---------------------------------- */
static int cb17_table_ok(uint8_t off, uint8_t inv)
{
    const uint32_t keep = (uint32_t)~((uint32_t)off & ~(uint32_t)inv) & 0xFFu;
    uint32_t c;

    for (c = 0u; c < 256u; c++)
        if (crumpBit_dataLines[c] != (uint8_t)((c & keep) ^ inv))
            return 0;
    return 1;
}

static void law_cb17(void)
{
    fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
    CrumpBitRuntime rt;
    float l[32], r[32];
    uint32_t i;
    uint32_t bad = 0u;
    char msg[96];

    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    /* A stale table from another runtime must not survive an init. */
    crumpBit_dataLinesKey = 0u;
    memset(crumpBit_dataLines, 0, sizeof crumpBit_dataLines);
    p3_init(&rt, CRUMPBIT_SUBTYPE_DELAY);
    p3_block(&rt, &share, l, r, 32u);
    expect(cb17_table_ok(0u, 0u), "CB17 first block builds the table");
    for (i = 0u; i < 2000u; i++) {
        const uint8_t sub = (uint8_t)(rnd() % CRUMPBIT_SUBTYPE_COUNT);

        if (rnd() & 1u)
            p3_write(&rt, CRUMPBIT_PARAM_BIT_OFF, (uint8_t)rnd(), &share);
        if (rnd() & 1u)
            p3_write(&rt, CRUMPBIT_PARAM_BIT_INVERT, (uint8_t)rnd(), &share);
        p3_write(&rt, CRUMPBIT_PARAM_SUBTYPE, sub, &share);
        p3_block(&rt, &share, l, r, 32u);
        bad += !cb17_table_ok(rt.bit_off, rt.bit_invert);
    }
    snprintf(msg, sizeof msg, "CB17 table after changes: %u wrong", bad);
    expect(bad == 0u, msg);
}

/* CB18 (S091 P5 G4): the `grn` echo always decays -------------------------- */
static void law_cb18(void)
{
    static const uint8_t fbks[4] = { 0u, 64u, 100u, 127u };
    fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
    const float per_tick = CRUMPBIT_SAMPLES_PER_TICK / 120.0f;
    /* d: the echo period at rte 0 (1/64 at 120 BPM), whole frames. */
    const uint32_t d = (uint32_t)(crumpBit_grainTime[0] * per_tick);
    uint32_t t;
    char msg[160];

    expect(crumpBit_grainFeedbackGain(127u) == CRUMPBIT_GRAIN_FB_MAX &&
           crumpBit_grainFeedbackGain(0u) == 0.0f,
           "CB18 grn fbk 127 = 0.98, 0 = 0");
    cb_bpm = 120u;
    for (t = 0u; t < 4u; t++) {
        CrumpBitRuntime rt;
        const float gain = crumpBit_grainFeedbackGain(fbks[t]);
        /* floor: the largest code the 8-bit re-record can hold forever. */
        const float floor_codes = 0.5f / (1.0f - gain);
        float window = 0.0f;
        float last = 1e9f;
        uint32_t grew = 0u;
        uint32_t frame = 0u;
        uint32_t block;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_GRAIN);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, 0u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, fbks[t], &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_LENGTH, 63u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_DENSITY, 100u, &share);
        for (block = 0u; block < 10000u; block++) {     /* 7.3 s */
            float l[32], r[32];
            uint32_t n;

            if (block == 300u)      /* 0.2 s of grains, then none */
                p3_write(&rt, CRUMPBIT_PARAM_GRAIN_DENSITY, 63u, &share);
            for (n = 0u; n < 32u; n++) {
                l[n] = (block < 300u) ? rndf(-1.0f, 1.0f) : 0.0f;
                r[n] = (block < 300u) ? rndf(-1.0f, 1.0f) : 0.0f;
            }
            p3_block(&rt, &share, l, r, 32u);
            if (block < 800u)       /* the last grains end, the echo rings */
                continue;
            for (n = 0u; n < 32u; n++) {
                window = fmaxf(window, fabsf(l[n]) * 128.0f);
                if (++frame == d) {
                    grew += (window > last + 1e-3f);
                    last = window;
                    window = 0.0f;
                    frame = 0u;
                }
            }
        }
        snprintf(msg, sizeof msg,
                 "CB18 grn fbk %u (gain %.3f): echo peak %.2f codes (floor "
                 "%.2f), grew %u times", fbks[t], (double)gain, (double)last,
                 (double)floor_codes, grew);
        expect(grew == 0u && last < floor_codes + 0.01f, msg);
        printf("%s\n", msg);
    }
}

/* CB19 (S091 P5 F1): the one-pole output filter --------------------------- */

/* Goertzel power at f of `sub`'s output for a sine at f (mix 0). */
static double cb19_power(uint8_t sub, uint8_t type, uint8_t frq, uint8_t res,
                         double f)
{
    static uint8_t arena_f[16384];
    fx_share_t share = { arena_f, 0u, sizeof arena_f };
    const uint32_t total = 44108u;
    float *out = calloc(total, sizeof(float));
    CrumpBitRuntime rt;
    uint32_t got = 0u;
    uint32_t t = 0u;
    double p;

    memset(arena_f, 0, sizeof arena_f);
    p3_init(&rt, sub);
    p3_write(&rt, CRUMPBIT_PARAM_MIX, 0u, &share);
    p3_write(&rt, CRUMPBIT_PARAM_FILTER_TYPE, type, &share);
    p3_write(&rt, CRUMPBIT_PARAM_FILTER_FREQ, frq, &share);
    p3_write(&rt, CRUMPBIT_PARAM_FILTER_RESO, res, &share);
    while (got < total) {
        float l[32], r[32];
        uint32_t n;

        for (n = 0u; n < 32u; n++, t++) {
            l[n] = 0.5f * sinf(2.0f * (float)M_PI * (float)f * (float)t /
                               44108.0f);
            r[n] = l[n];
        }
        p3_block(&rt, &share, l, r, 32u);
        memcpy(out + got, l, sizeof l);
        got += 32u;
    }
    p = goertzel(out + total / 2u, total / 2u, f);
    free(out);
    return p;
}

static void law_cb19(void)
{
    static const uint8_t frqs[3] = { 50u, 70u, 90u };
    uint32_t i;
    char msg[160];

    /* (a) -3 dB at the SVF's res-0 frequency, both filters, vs `off`. */
    for (i = 0u; i < 3u; i++) {
        const double w3 = 2.0 * atan((double)crumpBit_filterG[frqs[i]] *
                                     sqrt(sqrt(2.0) - 1.0));
        const double f3 = w3 * 44108.0 / (2.0 * M_PI);
        const double off = cb19_power(CRUMPBIT_SUBTYPE_REVERB,
                                      CRUMPBIT_FILTER_OFF, frqs[i], 0u, f3);
        const double one = cb19_power(CRUMPBIT_SUBTYPE_REVERB,
                                      CRUMPBIT_FILTER_LP, frqs[i], 0u, f3);
        const double svf = cb19_power(CRUMPBIT_SUBTYPE_DELAY,
                                      CRUMPBIT_FILTER_LP, frqs[i], 0u, f3);
        const double db1 = 10.0 * log10(one / off);
        const double db2 = 10.0 * log10(svf / off);

        snprintf(msg, sizeof msg,
                 "CB19 frq %u at %.0f Hz: one-pole %.2f dB, SVF %.2f dB",
                 frqs[i], f3, db1, db2);
        expect(fabs(db1 + 3.01) < 0.3 && fabs(db2 + 3.01) < 0.3, msg);
        printf("%s\n", msg);
    }

    /*
     * (b) the type mapping and (c) `res` inert, bit for bit, on `rvb`
     * (LP = LP2 = BP = UBP; HP = notch; peak = off; res 0 = res 120).
     */
    {
        /* cls: each type's one-pole class, 0 low-pass, 1 high-pass, 2 flat. */
        static const uint8_t cls[8] = { 0u, 1u, 0u, 0u, 1u, 2u, 0u, 2u };
        static uint8_t arena_m[9][4096];
        CrumpBitRuntime rm[9];
        fx_share_t sh[9];
        uint32_t bad = 0u;
        uint32_t block;
        uint32_t k;

        for (k = 0u; k < 9u; k++) {
            /* 0..7: `rvb` with type k; 8: `rvb` LP at res 120. */
            sh[k].base = arena_m[k];
            sh[k].offset = 0u;
            sh[k].bytes = sizeof arena_m[k];
            memset(arena_m[k], 0, sizeof arena_m[k]);
            p3_init(&rm[k], CRUMPBIT_SUBTYPE_REVERB);
            p3_write(&rm[k], CRUMPBIT_PARAM_MIX, 64u, &sh[k]);
            p3_write(&rm[k], CRUMPBIT_PARAM_FILTER_FREQ, 60u, &sh[k]);
            p3_write(&rm[k], CRUMPBIT_PARAM_FILTER_TYPE,
                     (k < 8u) ? (uint8_t)k : CRUMPBIT_FILTER_LP, &sh[k]);
            p3_write(&rm[k], CRUMPBIT_PARAM_FILTER_RESO,
                     (k == 8u) ? 120u : 0u, &sh[k]);
        }
        for (block = 0u; block < 2000u; block++) {
            float l[9][32], r[9][32];
            uint32_t n;

            for (n = 0u; n < 32u; n++) {
                const float a = rndf(-1.0f, 1.0f);
                const float b = rndf(-1.0f, 1.0f);

                for (k = 0u; k < 9u; k++) {
                    l[k][n] = a;
                    r[k][n] = b;
                }
            }
            for (k = 0u; k < 9u; k++)
                p3_block(&rm[k], &sh[k], l[k], r[k], 32u);
            for (n = 0u; n < 32u; n++) {
                for (k = 1u; k < 8u; k++) {
                    /* the first type of each class: LP 0, HP 1, flat 5. */
                    const uint32_t first = (cls[k] == 0u) ? 0u
                                         : ((cls[k] == 1u) ? 1u : 5u);

                    bad += !same_bits(l[k][n], l[first][n]) +
                           !same_bits(r[k][n], r[first][n]);
                }
                bad += !same_bits(l[8][n], l[0][n]) +
                       !same_bits(r[8][n], r[0][n]);
            }
        }
        snprintf(msg, sizeof msg,
                 "CB19 type mapping and res inert: %u samples differ", bad);
        expect(bad == 0u, msg);
    }
    {
        /* `off` on the one-pole path is the dry signal, as `dly` `off`. */
        static uint8_t arena_o[2][4096];
        CrumpBitRuntime ro[2];
        fx_share_t sh[2];
        uint32_t bad = 0u;
        uint32_t block;
        uint32_t k;

        for (k = 0u; k < 2u; k++) {
            sh[k].base = arena_o[k];
            sh[k].offset = 0u;
            sh[k].bytes = sizeof arena_o[k];
            p3_init(&ro[k], k ? CRUMPBIT_SUBTYPE_REVERB
                              : CRUMPBIT_SUBTYPE_DELAY);
            p3_write(&ro[k], CRUMPBIT_PARAM_MIX, 0u, &sh[k]);
        }
        for (block = 0u; block < 1000u; block++) {
            float l[2][32], r[2][32];
            uint32_t n;

            for (n = 0u; n < 32u; n++) {
                l[0][n] = l[1][n] = rndf(-1.5f, 1.5f);
                r[0][n] = r[1][n] = rndf(-1.5f, 1.5f);
            }
            for (k = 0u; k < 2u; k++)
                p3_block(&ro[k], &sh[k], l[k], r[k], 32u);
            for (n = 0u; n < 32u; n++)
                bad += !same_bits(l[0][n], l[1][n]) +
                       !same_bits(r[0][n], r[1][n]);
        }
        snprintf(msg, sizeof msg, "CB19 one-pole off = dry: %u differ", bad);
        expect(bad == 0u, msg);
    }

    /*
     * (d) the hand-over on a switch: the state moves exactly, and the
     * output does not jump. A 100 Hz sine through `LP` (frq 90, -3 dB
     * near 2.6 kHz), mix 0; the sub-type flips dly <-> rvb (and dly ->
     * grn) every 37 blocks.
     */
    {
        static uint8_t arena_s[4096];
        static const uint8_t subs[4] = { CRUMPBIT_SUBTYPE_DELAY,
                                         CRUMPBIT_SUBTYPE_REVERB,
                                         CRUMPBIT_SUBTYPE_DELAY,
                                         CRUMPBIT_SUBTYPE_GRAIN };
        fx_share_t share = { arena_s, 0u, sizeof arena_s };
        CrumpBitRuntime rt;
        float prev = 0.0f;
        float worst_switch = 0.0f;
        float worst_run = 0.0f;
        uint32_t moved_bad = 0u;
        uint32_t t = 0u;
        uint32_t block;

        memset(arena_s, 0, sizeof arena_s);
        p3_init(&rt, CRUMPBIT_SUBTYPE_DELAY);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 0u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_FILTER_TYPE, CRUMPBIT_FILTER_LP, &share);
        p3_write(&rt, CRUMPBIT_PARAM_FILTER_FREQ, 90u, &share);
        for (block = 0u; block < 3000u; block++) {
            float l[32], r[32];
            const uint32_t sw = (block >= 100u && (block % 37u) == 0u);
            uint32_t n;

            if (sw) {
                /* The hand-over itself, checked on the bare enter. */
                const uint8_t next = subs[(block / 37u) % 4u];
                CrumpBitRuntime probe = rt;

                if (next != probe.active_subtype) {
                    const uint32_t from1 =
                        crumpBit_filterIsOnePole(probe.active_subtype);
                    const float l1 = probe.svf_l1;
                    const float l2 = probe.svf_l2;

                    crumpBit_enter(&probe, next, share.bytes);
                    if (crumpBit_filterIsOnePole(next) && !from1)
                        moved_bad += !same_bits(probe.svf_l1, l2);
                    if (!crumpBit_filterIsOnePole(next) && from1)
                        moved_bad += !same_bits(probe.svf_l2, l1) ||
                                     probe.svf_l1 != 0.0f;
                }
                p3_write(&rt, CRUMPBIT_PARAM_SUBTYPE, next, &share);
            }
            for (n = 0u; n < 32u; n++, t++) {
                l[n] = 0.5f * sinf(2.0f * (float)M_PI * 100.0f * (float)t /
                                   44108.0f);
                r[n] = l[n];
            }
            p3_block(&rt, &share, l, r, 32u);
            if (block >= 50u) {
                const float jump = fabsf(l[0] - prev);

                if (sw)
                    worst_switch = fmaxf(worst_switch, jump);
                else
                    worst_run = fmaxf(worst_run, jump);
            }
            prev = l[31];
        }
        snprintf(msg, sizeof msg,
                 "CB19 hand-over: %u states wrong; jump at a switch %.4f "
                 "(steady %.4f)", moved_bad, (double)worst_switch,
                 (double)worst_run);
        expect(moved_bad == 0u && worst_switch < 0.05f, msg);
        printf("%s\n", msg);
    }
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
    law_cb16();
    law_cb17();
    law_cb18();
    law_cb19();
    printf("crumpbit failures: %d\n", failures);
    return failures ? 1 : 0;
}
