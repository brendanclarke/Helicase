/*
 * Core/DSP/Effects/Space/SpaceKitDsp.c
 *
 *  Created on: 12.11.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software (licence: see SpaceKit.h).
 * ------------------------------------------------------------------------------------------------------------------------
 */

/*
 * Space kit, the per-frame half (S095 P1; S095_P1_IMPLEMENTATION.md S6, SD8):
 * the feed read and the drain envelope. Built with the DSP fast-math policy
 * (Makefile). Loop rules: no division, libm call or double in a frame loop;
 * the loop state lives in locals for the block (CrumpBit's form).
 */
#include "SpaceKit.h"
#include <math.h>

/*
 * One piece of the head's block, with no wrap inside it (inline, int16 and
 * int8, so neither form tests its format per frame).
 *
 * What: n outputs, y = d[i] x g added to l[m] and r[m], g ramping linearly
 * by dg per output (the end fade and the type's mix ramp in one gain). The
 * Q16 phase advances by `step` per output; its carry c (0 or 1) moves i on
 * by c x stride. A step of exactly 1.0 carries every output (the plain
 * read); a step below 1 repeats samples, the nearest-sample read, so the
 * recorded pitch holds. The caller ends the piece at the wrap, so i never
 * passes the domain's end inside it (only its last carry may reach it).
 * Inputs: the domain, the head state, n, the piece's gains.
 * Outputs: l[], r[] (+=), *pos, *phase; the gain after the piece.
 * Caller: sk_feedRun().
 */
static inline __attribute__((always_inline)) float sk_feedPiece(
    const void *dom, uint32_t *pos, uint32_t *phase, uint32_t st,
    uint32_t step, uint32_t n, float g, float dg, float *l, float *r,
    const int s16)
{
    uint32_t i = *pos;
    uint32_t ph = *phase;
    uint32_t m;

    for (m = 0u; m < n; m++) {
        const float y = (float)(s16 ? (int32_t)((const int16_t *)dom)[i]
                                    : (int32_t)((const int8_t *)dom)[i]) * g;

        l[m] += y;
        r[m] += y;
        g += dg;
        ph += step;
        i += (ph >> 16) * st;
        ph &= 0xFFFFu;
    }
    *pos = i;
    *phase = ph;
    return g;
}

/*
 * The head's block: the piece up to the wrap (n1 outputs), the wrap, then
 * the rest (inline per format; the domain holds at least 64 samples, so a
 * block wraps at most once).
 */
static inline __attribute__((always_inline)) void sk_feedRun(
    sk_ctl_t *c, const void *dom, uint32_t n1, uint32_t run, float g,
    float dg, float *l, float *r, const int s16)
{
    uint32_t i = c->pos;
    uint32_t ph = c->phase;

    g = sk_feedPiece(dom, &i, &ph, c->stride, c->step_q16, n1, g, dg, l, r,
                     s16);
    i -= (i >= c->wrap_elems) ? c->wrap_elems : 0u;
    if (n1 < run)
        sk_feedPiece(dom, &i, &ph, c->stride, c->step_q16, run - n1, g, dg,
                     l + n1, r + n1, s16);
    c->pos = i;
    c->phase = ph;
}

/*
 * The end-fade gain with `left` samples to go: min(1, left x fade_inv),
 * never below 0 (fade_inv is 1 without a fade, so only the last block
 * ramps).
 */
static inline float sk_fadeGain(const sk_ctl_t *c, float left)
{
    const float x = left * c->fade_inv;

    return (x < 0.0f) ? 0.0f : ((x > 1.0f) ? 1.0f : x);
}

/*
 * Out of line (noinline): every caller pays for it only in feed blocks, and
 * the CPU gates count its loop on their own.
 */
__attribute__((noinline)) uint8_t sk_feedMix(const fx_share_t *share,
                                             uint8_t owner, float *l,
                                             float *r, uint32_t frames,
                                             float mix0, float mix1)
{
    sk_ctl_t *const c = sk_ctl(share);
    /* run: outputs the head still has; used: the samples they consume. */
    uint32_t run = frames;
    uint32_t used;
    /* n1: outputs before the head wraps (e: elements left before it). */
    uint32_t n1;
    uint32_t e;
    float g0;
    float g1;

    if (c->magic != (SK_CTL_MAGIC | (uint32_t)owner) || c->feeding == 0u)
        return 0u;
    /* Only near its end: (left x 65536 - phase) / step, rounded up. */
    if (c->left < 2u * frames + 2u) {
        const uint32_t q = ((c->left << 16) - c->phase + c->step_q16 - 1u) /
                           c->step_q16;

        run = (q < frames) ? q : frames;
    }
    used = (c->phase + run * c->step_q16) >> 16;
    g0 = c->gain * mix0 * sk_fadeGain(c, (float)c->left);
    g1 = c->gain * mix1 * sk_fadeGain(c, (float)c->left - (float)used);
    /* The wrap: k carries reach it, after ((k << 16) - phase) / step outputs. */
    n1 = run;
    e = c->wrap_elems - c->pos;
    if (e <= (run + 1u) * c->stride) {
        const uint32_t k = (e + c->stride - 1u) / c->stride;
        const uint32_t n = ((k << 16) - c->phase + c->step_q16 - 1u) /
                           c->step_q16;

        n1 = (n < run) ? n : run;
    }
    if (run != 0u) {
        const float dg = (g1 - g0) / (float)run;
        const void *dom = share->base + c->wrap_off;

        if (c->flags & SK_FEED_S16)
            sk_feedRun(c, dom, n1, run, g0, dg, l, r, 1);
        else
            sk_feedRun(c, dom, n1, run, g0, dg, l, r, 0);
    }
    c->left = (used >= c->left || run < frames) ? 0u : c->left - used;
    if (c->left != 0u)
        return 1u;
    c->feeding = 0u;
    effects_setFeedSlotLock(0u);
    return 0u;
}

uint8_t sk_drainStep(effect_io_t *io)
{
    sk_ctl_t *c;
    /* g, dg: the envelope at the block start and its per-frame step. */
    float g;
    float dg;
    float peak = 0.0f;
    uint32_t b;
    uint32_t n;

    if (!io || !io->share || !io->share->base ||
        io->share->bytes < SK_PREFIX_BYTES)
        return 1u;
    c = sk_ctl(io->share);
    b = c->drain_blocks;
    /* 1.0 until the last 500 ms of the 6 s ceiling, then linear to 0. */
    if (b + SK_DRAIN_FADE_BLOCKS < SK_DRAIN_BLOCKS) {
        g = 1.0f;
        dg = 0.0f;
    } else {
        const float left = (float)(SK_DRAIN_BLOCKS - b);

        g = left * (1.0f / (float)SK_DRAIN_FADE_BLOCKS);
        dg = -(1.0f / (float)SK_DRAIN_FADE_BLOCKS) / (float)io->frames;
    }
    for (n = 0u; n < io->frames; n++) {
        const float yl = io->l[n] * g;
        const float al = (yl < 0.0f) ? -yl : yl;

        io->l[n] = yl;
        peak = (al > peak) ? al : peak;
        if (io->r) {
            const float yr = io->r[n] * g;
            const float ar = (yr < 0.0f) ? -yr : yr;

            io->r[n] = yr;
            peak = (ar > peak) ? ar : peak;
        }
        g += dg;
    }
    c->drain_blocks = (uint16_t)(b + 1u);
    c->drain_quiet = (peak < SK_DRAIN_QUIET)
        ? (uint8_t)(c->drain_quiet + 1u) : 0u;
    return (uint8_t)(c->drain_quiet >= SK_DRAIN_QUIET_BLOCKS ||
                     c->drain_blocks >= SK_DRAIN_BLOCKS);
}

/*
 * The idle's meter: four samples a block (frames 0 and half the block, both
 * channels). A sound above -90 dBFS is not below it at all four points of
 * 14 blocks in a row.
 */
static inline float sk_peak4i(const effect_io_t *io)
{
    const uint32_t h = io->frames >> 1;
    float peak = __builtin_fmaxf(fabsf(io->l[0]), fabsf(io->l[h]));

    if (io->r)
        peak = __builtin_fmaxf(peak, __builtin_fmaxf(fabsf(io->r[0]),
                                                     fabsf(io->r[h])));
    return peak;
}

float sk_peak4(const effect_io_t *io)
{
    return sk_peak4i(io);
}

/* The idle block: wake on audio, else zero the outputs (out of line). */
static __attribute__((noinline, cold)) uint8_t sk_idleBlock(effect_io_t *io,
                                                           sk_ctl_t *c)
{
    uint32_t n;

    if (sk_peak4i(io) > SK_IDLE_QUIET) {
        /* Audio again: wake at full return. */
        c->idle = 0u;
        c->idle_q = 0u;
        c->idle_n = 0u;
        c->idle_g = 1.0f;
        return 0u;
    }
    for (n = 0u; n < io->frames; n++) {
        io->l[n] = 0.0f;
        if (io->r)
            io->r[n] = 0.0f;
    }
    return 1u;
}

__attribute__((noinline)) uint8_t sk_idleBefore(effect_io_t *io,
                                                uint8_t running)
{
    sk_ctl_t *c;

    if (!io->share || io->share->bytes < SK_PREFIX_BYTES)
        return 0u;
    c = sk_ctl(io->share);
    if (running) {
        c->idle = 0u;
        c->idle_q = 0u;
        c->idle_n = 0u;
        c->idle_g = 1.0f;
        return 0u;
    }
    return c->idle ? sk_idleBlock(io, c) : 0u;
}

__attribute__((noinline)) void sk_idleAfter(const effect_io_t *io)
{
    sk_ctl_t *c;

    if (!io->share || io->share->bytes < SK_PREFIX_BYTES)
        return;
    c = sk_ctl(io->share);
    if (sk_peak4i(io) > SK_IDLE_QUIET) {
        c->idle_q = 0u;
        c->idle_n = 0u;
        c->idle_g = 1.0f;
        return;
    }
    if (c->idle_q < SK_IDLE_QUIET_BLOCKS) {
        c->idle_q++;
        return;
    }
    c->idle_n++;
    c->idle_g = 1.0f - (float)c->idle_n * (1.0f / (float)SK_IDLE_RAMP_BLOCKS);
    if (c->idle_n >= SK_IDLE_RAMP_BLOCKS) {
        c->idle = 1u;
        c->idle_g = 0.0f;
    }
}

float sk_idleGain(const fx_share_t *share)
{
    if (!share || !share->base || share->bytes < SK_PREFIX_BYTES)
        return 1.0f;
    return sk_ctl(share)->idle_g;
}

void sk_idleReset(const fx_share_t *share)
{
    sk_ctl_t *c;

    if (!share || !share->base || share->bytes < SK_PREFIX_BYTES)
        return;
    c = sk_ctl(share);
    c->idle = 0u;
    c->idle_q = 0u;
    c->idle_n = 0u;
    c->idle_g = 1.0f;
}
