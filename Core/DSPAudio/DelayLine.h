/*
 * DelayLine.h
 *
 *  Created on: 26.10.2026
 * ----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * ----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ----------------------------------------------------------------------------
 */

#ifndef DELAY_LINE_H_
#define DELAY_LINE_H_

#if !defined(INSERT_HOST_TEST)
#include "stm32f4xx.h"
#endif
#include <math.h>
#include <stdint.h>

/*
 * DelayLine — static inline building blocks for voice-buffer DSP
 * (S084 VX3 VX3-3.1).
 *
 * What: provides the shared ring write/wrap, fractional tap, damping,
 * allpass and decay-ceiling primitives. Why: every buffer type must use one
 * tested implementation and keep its own positions/filter memories in its
 * fixed state record. Inputs/outputs: ring samples, positions, delays and
 * coefficients in; samples and updated positions/memories out. Context:
 * static inline, engine-agnostic, constant-cost inner loops. Affiliates:
 * InsertEffect.h, VoiceBuffer.c and the insert type files.
 */

/* Compare-and-subtract ring wrap; callers keep i < 2*len. */
static inline uint32_t delay_wrap(uint32_t i, uint32_t len)
{
    return (i >= len) ? i - len : i;
}

/* Write one saturated sample and advance the ring position. */
static inline void delay_write(int16_t *ring, uint32_t len, uint32_t *wp,
                               int32_t x)
{
    ring[*wp] = (int16_t)__SSAT(x, 16);
    *wp = delay_wrap(*wp + 1u, len);
}

/* Read a linearly interpolated tap; d is within the guarded ring. */
static inline float delay_tap(const int16_t *ring, uint32_t len,
                              uint32_t wp, float d)
{
    float rp = (float)wp - d;
    uint32_t i0;
    float frac;

    rp += (rp < 0.0f) ? (float)len : 0.0f;
    i0 = (uint32_t)rp;
    frac = rp - (float)i0;
    return (float)ring[i0] + frac *
           (float)(ring[delay_wrap(i0 + 1u, len)] - ring[i0]);
}

/*
 * Block-constant fractional tap (S084 VX3 remediation §18, R14 cost fix).
 *
 * What: delay_tapStart() splits a delay that is constant for the block into
 * the newer read index r0 = wp - floor(d) (wrapped) and the fraction; the
 * per-sample delay_tapStep() reads ring[r0] and the sample one older and
 * blends them, and delay_advance() moves an index by one with a
 * compare-and-reset wrap. Why: delay_tap() converts the write position to
 * float and back every sample (about 20 instructions); a block-constant
 * delay needs only two loads and one multiply-add per sample.
 * Inputs: ring, length, positions, delay (1 <= d <= len - 2).
 */
static inline uint32_t delay_tapStart(uint32_t len, uint32_t wp, float d,
                                      float *frac)
{
    const uint32_t d0 = (uint32_t)d;

    *frac = d - (float)d0;
    return (wp >= d0) ? wp - d0 : wp + len - d0;
}

static inline float delay_tapStep(const int16_t *ring, uint32_t len,
                                  uint32_t r0, float frac)
{
    const int32_t newer = ring[r0];
    const int32_t older = ring[(r0 == 0u) ? len - 1u : r0 - 1u];

    return (float)newer + frac * (float)(older - newer);
}

static inline uint32_t delay_advance(uint32_t i, uint32_t len)
{
    return (i + 1u == len) ? 0u : i + 1u;
}

/* One-pole low-pass update; a is the per-sample smoothing coefficient. */
static inline float delay_onePole(float *z, float x, float a)
{
    *z += a * (x - *z);
    return *z;
}

/* First-order allpass update; g is bounded by the caller. */
static inline float delay_allpass(float *z, float x, float g)
{
    const float y = *z - g * x;

    *z = x + g * y;
    return y;
}

/* Feedback ceiling for a 60 dB decay within three seconds. */
static inline float delay_feedbackCeiling(float d_seconds)
{
    return expf(-2.302585093f * d_seconds);
}

#endif /* DELAY_LINE_H_ */
