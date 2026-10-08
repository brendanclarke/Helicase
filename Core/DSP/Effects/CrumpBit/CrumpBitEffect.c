/*
 * Core/DSP/Effects/CrumpBit/CrumpBitEffect.c
 *
 *  Created on: 29.09.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 *
 * CrumpBit DSP (contract in CrumpBitEffect.h; specification
 * S074_CRUMPBIT_EFFECT.md §3). Compiled with -Ofast (Makefile MK4).
 * Every stage runs on every sample whatever its settings (constant CPU).
 */

#include "CrumpBitEffect.h"
#include "CrumpBitParameters.h"
#include "StepScale.h"
#include "sequencer.h"
#include <math.h>

/*
 * Private DSP constants (S074_CRUMPBIT_EFFECT.md §3).
 *
 * CRUMPBIT_SAMPLE_RATE_HZ codec rate used for every time conversion.
 * CRUMPBIT_LSB one 8-bit step, 1/128 of int16 full scale.
 * CRUMPBIT_RATE_LN_SPAN ln(1.60 s / 0.020 s) = ln 80, the Rate curve.
 * CRUMPBIT_SAMPLES_PER_TICK samples per 96-PPQ tick at 1 BPM.
 * CRUMPBIT_GLIDE_K one-pole tape glide, tau 150 ms.
 * CRUMPBIT_AC_R AC coupling pole, about 10 Hz.
 * CRUMPBIT_FEEDBACK_MAX feedback gain at row value 127 (bounded < 1).
 * CRUMPBIT_LOOP_MIN smallest usable loop; below it the block passes through
 * untouched (cannot happen with the guaranteed minimum share).
 */
#define CRUMPBIT_SAMPLE_RATE_HZ    44108u
#define CRUMPBIT_LSB               (1.0f / 128.0f)
#define CRUMPBIT_RATE_LN_SPAN      4.3820266f
#define CRUMPBIT_SAMPLES_PER_TICK  27567.5f
#define CRUMPBIT_GLIDE_K           1.5113e-4f
#define CRUMPBIT_AC_R              0.9985755f
#define CRUMPBIT_FEEDBACK_MAX      0.99f
#define CRUMPBIT_LOOP_MIN          4u
#define CRUMPBIT_DIVISION_NONE     0xFFu

_Static_assert(sizeof(CrumpBitRuntime) == 56u,
               "CrumpBitRuntime size is recorded in the S074 schedule and "
               "must stay inside the 76 B Effect union");
_Static_assert(CRUMPBIT_BUFFER_BYTES >=
               (uint32_t)CRUMPBIT_DELAY_MAX_SAMPLES + 2u,
               "the loop must hold the longest delay plus the interpolation "
               "guard");

/*
 * Simulated bipolar 8-bit ADC (offset binary).
 *
 * What: clamps the normalised sample to +-1.0 full scale and converts it to a
 * code 0..255 with round-half-up. Silence is code 128 (mid-tread); +1.0
 * (256 before the fold) becomes 255. Branch-free: two compare-selects, one
 * multiply-add, one convert, one shift-subtract. Input: float sample. Output:
 * code 0..255. Callers: crumpBit_process(). Affiliates: CRUMPBIT_LSB.
 */
static inline uint32_t crumpBit_adc(float x)
{
    uint32_t code;

    x = (x > 1.0f) ? 1.0f : x;
    x = (x < -1.0f) ? -1.0f : x;
    code = (uint32_t)(x * 128.0f + 128.5f);
    return code - (code >> 8);
}

float crumpBit_rateSamples(uint8_t rate)
{
    const uint8_t r = (rate > 127u) ? 127u : rate;

    return CRUMPBIT_DELAY_MAX_SAMPLES *
           expf(-(float)r * (CRUMPBIT_RATE_LN_SPAN / 127.0f));
}

/*
 * Nearest fitting division for one free-running length.
 *
 * What: walks the 128 ascending StepScale CC positions and keeps the longest
 * one <= target (low) and the shortest one > target (high), skipping any
 * longer than CRUMPBIT_DELAY_MAX_SAMPLES. It picks low when target^2 <
 * low*high, which is nearest in log time. Inputs: target length and samples per
 * tick at the current tempo. Output: a StepScale CC 0..127; 0 if nothing fits.
 * Callers: crumpBit_syncDivision(), crumpBit_targetSamples(). Affiliates:
 * stepScale_ticksQ8() (Q8.8 ticks).
 */
static uint8_t crumpBit_divisionFor(float target, float per_tick)
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

uint8_t crumpBit_syncDivision(uint8_t rate, uint16_t bpm)
{
    const float per_tick =
        CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);

    return crumpBit_divisionFor(crumpBit_rateSamples(rate), per_tick);
}

/*
 * Per-block delay target in samples.
 *
 * What: computes both the free-running length and the snapped division every
 * block (constant control cost, whatever Sync is), selects one by `sync`, and
 * clamps it to CRUMPBIT_DELAY_MAX_SAMPLES. A tempo change therefore moves the
 * target, and the glide bends the pitch like a tape machine. Inputs: row
 * values rate and sync, and bpm (seq_getBpm()). Output: target length in
 * samples. Caller: crumpBit_process(). Affiliates: crumpBit_divisionFor(),
 * stepScale_ticksQ8().
 */
static float crumpBit_targetSamples(uint8_t rate, uint8_t sync, uint16_t bpm)
{
    const float per_tick =
        CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);
    const float free_run = crumpBit_rateSamples(rate);
    const float synced = ((float)stepScale_ticksQ8(
        crumpBit_divisionFor(free_run, per_tick)) / 256.0f) * per_tick;
    const float target = sync ? synced : free_run;

    return (target > CRUMPBIT_DELAY_MAX_SAMPLES)
        ? CRUMPBIT_DELAY_MAX_SAMPLES : target;
}

/*
 * Seat the loop in the current Effect share.
 *
 * What: the loop length is min(share bytes, CRUMPBIT_BUFFER_BYTES). When it
 * differs from the seated length (first block after init, or a share change),
 * the loop is re-seated: write position 0 and valid 0, so old content is
 * muted rather than read with the wrong geometry. Inputs: runtime, share (may
 * be NULL). Output: loop length (0 if no share). Callers: crumpBit_process(),
 * crumpBit_bufferChanged(). Affiliates: FxBuffer share contract.
 */
static uint32_t crumpBit_seat(CrumpBitRuntime *rt, const fx_share_t *share)
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

/*
 * Initialise a newly live CrumpBit runtime ("clear unless you adopt").
 *
 * What: the manager has already zeroed the union. This marks the loop
 * unseated and unprimed, so the first block seats the loop, mutes every
 * unwritten sample through `valid`, and starts the delay at its target. The
 * handoff is not adopted in v1: re-entering CrumpBit never plays a previous
 * owner's tail. Nothing is cleared in bulk. Inputs: runtime and handoff.
 * Output: runtime ready for write_param(). Caller: effects_switchRuntime().
 * Affiliates: crumpBit_seat(), FXBUF_STATE_EFFECT_WRITTEN.
 */
static void crumpBit_init(void *rt_void, const fxbuf_handoff_t *handoff)
{
    CrumpBitRuntime *rt = (CrumpBitRuntime *)rt_void;

    (void)handoff;
    rt->length = 0u;
    rt->write_pos = 0u;
    rt->valid = 0u;
    rt->primed = 0u;
}

/*
 * Describe the loop in the handoff when CrumpBit is exited or refreshed.
 *
 * What: records 1 channel, 8 bits, 44,108 Hz, the write position and integer
 * read position in pointer slot 12 (FXBUF_HANDOFF_EFFECT_POINTER_BASE), and
 * FXBUF_STATE_EFFECT_WRITTEN when any sample was written. Positions are
 * share-relative, which is arena-relative because the share starts at the
 * arena bottom. Inputs: runtime and the fresh handoff record. Output: the
 * Effect fields of the record. Callers: EffectsManager's handoff helper.
 * Affiliates: fxbuf_handoff_t.
 */
static void crumpBit_exportHandoff(const void *rt_void, fxbuf_handoff_t *out)
{
    const CrumpBitRuntime *rt = (const CrumpBitRuntime *)rt_void;
    float rp;

    if (!rt || !out)
        return;
    out->effect_channels = 1u;
    out->effect_bits = 8u;
    out->effect_rate_hz = (uint16_t)CRUMPBIT_SAMPLE_RATE_HZ;
    if (rt->length == 0u)
        return;
    rp = (float)rt->write_pos - rt->delay;
    rp += (rp < 0.0f) ? (float)rt->length : 0.0f;
    out->write_offset[FXBUF_HANDOFF_EFFECT_POINTER_BASE] = rt->write_pos;
    out->read_offset[FXBUF_HANDOFF_EFFECT_POINTER_BASE] =
        (uint32_t)rp % rt->length;
    if (rt->valid != 0u)
        out->state_flags |= FXBUF_STATE_EFFECT_WRITTEN;
}

/*
 * Store one resolved row value.
 *
 * What: keeps the raw 0..255 value of rows 3..10. Every conversion (masks,
 * gains, curve, division) happens once per block in process(), so an LFO that
 * rewrites a row every block costs the same as a static setting. The sub-type
 * row is ignored (only `dly` exists). Inputs: runtime, row index, value
 * (already clamped to the descriptor maximum). Output: runtime fields.
 * Caller: effects_service(). Affiliates: crumpBit_descriptors.
 */
static void crumpBit_writeParam(void *rt_void, uint8_t index, uint8_t value)
{
    CrumpBitRuntime *rt = (CrumpBitRuntime *)rt_void;

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

/*
 * Process one Effect bus block in place (S074_CRUMPBIT_EFFECT.md §3).
 *
 * What, per sample, for both channels: 8-bit ADC -> data lines -> DAC -> AC
 * coupling. Then mono feed 0.5*(hL + hR) enters the tape loop; fractional
 * read uses linear interpolation and is muted beyond valid; the write is
 * re-quantised by the same ADC. Output is a linear dry/wet crossfade with
 * balance-only delay pan. Every operation runs on every sample whatever the
 * settings (constant CPU). Inputs: runtime and normalized stereo io. Output:
 * io->l/io->r in place and loop bytes in the share. Caller: effects_process().
 * Affiliates: crumpBit_adc(), crumpBit_targetSamples(), crumpBit_seat(),
 * seq_getBpm().
 */
static void crumpBit_process(void *rt_void, effect_io_t *io)
{
    CrumpBitRuntime *rt = (CrumpBitRuntime *)rt_void;
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
    len = crumpBit_seat(rt, io->share);
    if (len < CRUMPBIT_LOOP_MIN)
        return;
    left = io->l;
    right = io->r ? io->r : io->l;
    loop = (int8_t *)io->share->base;
    flen = (float)len;

    /* Data lines: invert wins over off (spec §3.2). */
    invert = rt->bit_invert;
    keep = (uint32_t)~((uint32_t)rt->bit_off & ~invert) & 0xFFu;

    /* Tape target for this block; the first block starts on it. */
    target = crumpBit_targetSamples(rt->rate, rt->sync, seq_getBpm());
    if (target > flen - 2.0f)
        target = flen - 2.0f;
    if (!rt->primed) {
        rt->delay = target;
        rt->primed = 1u;
    }

    /* Block ramps (spec §3.5): 0..127 rows to gains. */
    per_frame = 1.0f / (float)io->frames;
    mix_to = (float)rt->mix_raw * (1.0f / 127.0f);
    fb_to = (float)rt->feedback_raw * (CRUMPBIT_FEEDBACK_MAX / 127.0f);
    /*
     * Delay pan balance centred on 63 (S075 F2-E, F2-Q4): 63 is both-side
     * unity and displays 0, 0 is right silent, and 127 is left silent. This
     * matches the Effect return stereo law in mixer.c. Input: pan_raw 0..127;
     * output: block-ramp targets gain_l_to and gain_r_to.
     */
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
        const uint32_t code_l = (crumpBit_adc(left[n]) & keep) ^ invert;
        const uint32_t code_r = (crumpBit_adc(right[n]) & keep) ^ invert;
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

        /* Tape glide and fractional read (wrap by compare, not modulo). */
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

        /* Re-record through the 8-bit converter; feedback stays bounded. */
        loop[wp] = (int8_t)((int32_t)crumpBit_adc(0.5f * (hl + hr) +
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
    /* Land exactly on the targets so ramp rounding never accumulates. */
    rt->mix = mix_to;
    rt->feedback = fb_to;
    rt->gain_l = gain_l_to;
    rt->gain_r = gain_r_to;
}

/*
 * Share-change notification (voice units claimed or released).
 *
 * What: re-seats the loop. If the usable length changed, old content is muted
 * through valid. A forced re-resolution follows from the manager. Inputs:
 * runtime and new share. Output: runtime seat fields. Caller:
 * effects_onShareChanged(). Affiliates: crumpBit_seat().
 */
static void crumpBit_bufferChanged(void *rt_void, const fx_share_t *share)
{
    (void)crumpBit_seat((CrumpBitRuntime *)rt_void, share);
}

/* CrumpBit operations; the fixed delay range has no buffer-dependent clamp. */
const effect_type_ops_t crumpBit_ops = {
    crumpBit_init,
    crumpBit_exportHandoff,
    crumpBit_writeParam,
    crumpBit_process,
    crumpBit_bufferChanged,
    NULL,
};
