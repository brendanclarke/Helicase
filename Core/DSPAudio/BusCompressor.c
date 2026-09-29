/*
 * Core/DSPAudio/BusCompressor.c
 *
 *  Created on: 29.09.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 */

/* BusCompressor.c — Scene-owned master bus compressor (S074). */

#include "BusCompressor.h"
#include "config.h"
#include "SceneData.h"
#include <math.h>

/*
 * Model constants (S074_BUS_COMP.md §4; tuning starting points).
 *
 * What:       block period, bus scale, dB conversions, detector floor,
 *             knee width, makeup reference, fixed RMS/memory charge times,
 *             memory share, and saturator span.
 * Why:        every per-block formula below reads these names, so retuning is
 *             a one-line edit. The cam/ctm curves remain beside their formulas.
 * Inputs:     config.h OUTPUT_DMA_SIZE (32) and REAL_FS (44,108.07 Hz).
 * Affiliates: busComp_processBlock(), busComp_coef().
 */
#define BUS_COMP_BLOCK_MS          (1000.0f * (float)OUTPUT_DMA_SIZE / REAL_FS)
#define BUS_COMP_FULL_SCALE        8388352.0f
#define BUS_COMP_INV_FULL_SCALE    (1.0f / BUS_COMP_FULL_SCALE)
#define BUS_COMP_DB_PER_LOG2_POWER 3.01029996f
#define BUS_COMP_LOG2_PER_DB_GAIN  0.16609640f
#define BUS_COMP_POWER_FLOOR       1.0e-12f
#define BUS_COMP_KNEE_DB           12.0f
#define BUS_COMP_REF_LEVEL_DB      (-12.0f)
#define BUS_COMP_REF_POWER         0.06309573f
#define BUS_COMP_RMS_MS            5.0f
#define BUS_COMP_CHARGE_MS         300.0f
#define BUS_COMP_MEMORY_SHARE      0.5f
#define BUS_COMP_SAT_SPAN          1.5f

/*
 * Compressor runtime state (24 B DTCM; approved up to 32 B, S074).
 *
 * What:       smoothed mean-square power, fast and memory gain reduction in
 *             dB, previous linear gain as the ramp origin, pending sidechain
 *             weight, and the active output pair.
 * Why DTCM:   read and written every block beside mixer state.
 * Lifetime:   static and zeroed at startup; each fade-in reseeds it, so an
 *             off period cannot leak stale cell state into a new target.
 * Owner:      this file. Writers are the foreground block and trigger paths.
 */
typedef struct {
    float power;
    float gr_fast_db;
    float gr_memory_db;
    float gain_prev;
    float sc_pending;
    uint8_t active;
} bus_comp_state_t;

_Static_assert(sizeof(bus_comp_state_t) <= 32u,
               "S074: bus compressor state is approved for at most 32 B DTCM");

static INDTCMZ bus_comp_state_t busComp;

/*
 * One-pole coefficient for one block from a time constant in milliseconds.
 *
 * Inputs:     tau_ms > 0.
 * Outputs:    k = T_b / (tau + T_b/2), the bilinear form of
 *             1 - exp(-T_b/tau), within 0.2% for tau >= 5 ms.
 * Why:        one division instead of an exponential per block; constant
 *             arguments fold at compile time.
 * Caller:     busComp_processBlock().
 */
static inline float busComp_coef(float tau_ms)
{
    return BUS_COMP_BLOCK_MS / (tau_ms + 0.5f * BUS_COMP_BLOCK_MS);
}

/*
 * Soft-knee static gain computer (S074_BUS_COMP.md §4.2).
 *
 * Inputs:     level and threshold in dBFS; slope = 1 - 1/ratio.
 * Outputs:    gain reduction in dB (<= 0): zero below the 12 dB knee,
 *             quadratic inside it, and -slope*over above it.
 * Why:        the same curve supplies the live target and makeup reference.
 * Caller:     busComp_processBlock().
 */
static inline float busComp_staticGainDb(float level_db, float threshold_db,
                                         float slope)
{
    const float over = level_db - threshold_db;
    float x;

    if (over <= -0.5f * BUS_COMP_KNEE_DB)
        return 0.0f;
    if (over >= 0.5f * BUS_COMP_KNEE_DB)
        return -slope * over;
    x = over + 0.5f * BUS_COMP_KNEE_DB;
    return -slope * x * x * (0.5f / BUS_COMP_KNEE_DB);
}

void busComp_sidechainTrigger(uint8_t track, uint8_t velocity)
{
    const uint8_t source = scene_getBusCompSetting(
        scene_getActiveIndex(), SCENE_BUS_COMP_SIDECHAIN);
    /* BC11 working assumption: track 7 (index 6) counts as voice 6. */
    const uint8_t voice = (track >= 5u) ? 6u : (uint8_t)(track + 1u);
    float v;
    float weight;

    /*
     * Record only the largest matching trigger until the next block.
     *
     * Inputs: visible track and velocity. Output: pending (v/127)^3 weight;
     * velocity 0 and csc off never light the cell. The cam-dependent depth is
     * applied later so an edit between trigger and block remains honoured.
     */
    if (source == SCENE_BUS_COMP_SIDECHAIN_OFF || velocity == 0u ||
        voice != source)
        return;
    v = (velocity >= 127u) ? 1.0f : (float)velocity * (1.0f / 127.0f);
    weight = v * v * v;
    if (weight > busComp.sc_pending)
        busComp.sc_pending = weight;
}

void busComp_processBlock(sample_mx_t *st1, sample_mx_t *st2,
                          uint8_t scene_index)
{
    const uint8_t mode = scene_getBusCompSetting(scene_index,
                                                 SCENE_BUS_COMP_MODE);
    sample_mx_t *buf;
    float a;
    float t;
    float threshold;
    float slope;
    float ref_gr_db;
    float makeup_db;
    float drive;
    float target;
    float memory_target;
    float k;
    float gain;
    float gk;
    float gk_step;
    float w;
    float w_step;
    float c_in;
    float c_out;
    float c_out3;
    float sum;
    uint8_t fade_out = 0u;
    uint8_t i;

    /*
     * Off means no DSP work. Pending triggers are discarded every off block,
     * so csc cannot produce a stale duck when the target is enabled later.
     */
    if (busComp.active == SCENE_BUS_COMP_MODE_OFF &&
        mode == SCENE_BUS_COMP_MODE_OFF) {
        busComp.sc_pending = 0.0f;
        return;
    }

    /*
     * cam/ctm macro (S074 §4.2/§4.3): threshold -3..-30 dBFS, ratio 1..8,
     * makeup, and cubic drive. These are recomputed per block so Scene
     * changes take effect smoothly next block.
     *
     * Tuning revision (user, after hardware listening): makeup is the static
     * reduction at the -12 dBFS reference scaled by (1 - 0.3a), so the level
     * rise tapers off progressively across cam (+11.0 dB at cam 127 instead
     * of +15.8; a -18 dBFS bus now leaves at about -17.5 dBFS rather than
     * -12.8 at the top). Drive is 1 + 0.5a^2 (was 0.4a^2): unchanged at low
     * cam, ceiling -3.5 dBFS at cam 127 (was -2.9), i.e. just slightly more
     * saturation at the top. ref_gr_db also seeds the cell on fade-in.
     */
    a = (float)scene_getBusCompSetting(scene_index, SCENE_BUS_COMP_AMOUNT) *
        (1.0f / 127.0f);
    t = (float)scene_getBusCompSetting(scene_index, SCENE_BUS_COMP_TIME) *
        (1.0f / 127.0f);
    threshold = -3.0f - 27.0f * a;
    slope = 1.0f - 1.0f / (1.0f + 3.0f * a + 4.0f * a * a * a);
    ref_gr_db = busComp_staticGainDb(BUS_COMP_REF_LEVEL_DB, threshold, slope);
    makeup_db = -ref_gr_db * (1.0f - 0.3f * a);
    drive = 1.0f + 0.5f * a * a;

    /*
     * Target transitions: one-block dry/wet fades, with St1<->St2 fading the
     * old pair out before the new pair is reseeded and faded in next block.
     */
    if (busComp.active == SCENE_BUS_COMP_MODE_OFF) {
        /* Seed the cell at its steady state for a bus at the reference. */
        busComp.active = mode;
        busComp.power = BUS_COMP_REF_POWER;
        busComp.gr_fast_db = ref_gr_db;
        busComp.gr_memory_db = 0.0f;
        busComp.gain_prev = 1.0f;
        w = 0.0f;
        w_step = 1.0f / (float)OUTPUT_DMA_SIZE;
    } else if (mode != busComp.active) {
        fade_out = 1u;
        w = 1.0f;
        w_step = -1.0f / (float)OUTPUT_DMA_SIZE;
    } else {
        w = 1.0f;
        w_step = 0.0f;
    }
    buf = (busComp.active == SCENE_BUS_COMP_MODE_ST1) ? st1 : st2;

    /*
     * One-block-lagged detector and optical cell, all in the dB/block domain.
     * The pending sidechain weight lights the fast cell below the static target
     * without stacking; the memory cell charges toward half of that reduction.
     */
    target = busComp_staticGainDb(
        BUS_COMP_DB_PER_LOG2_POWER *
            log2f(busComp.power + BUS_COMP_POWER_FLOOR),
        threshold, slope);
    k = (target < busComp.gr_fast_db)
            ? busComp_coef(5.0f + 10.0f * t)
            : busComp_coef(60.0f + 540.0f * t * t);
    busComp.gr_fast_db += k * (target - busComp.gr_fast_db);
    /*
     * Sidechain cut-in depth at velocity 127: 2 + 19a dB (tuning revision;
     * was 9 + 9a). Extremely mild at low cam (2 dB at cam 0), 9.2 dB at the
     * default 48, and fairly extreme at the top (21 dB at cam 127, was 18).
     * Velocity still scales it by (v/127)^3.
     */
    if (busComp.sc_pending > 0.0f) {
        const float duck_db = target - (2.0f + 19.0f * a) * busComp.sc_pending;

        if (duck_db < busComp.gr_fast_db)
            busComp.gr_fast_db = duck_db;
        busComp.sc_pending = 0.0f;
    }
    memory_target = BUS_COMP_MEMORY_SHARE * busComp.gr_fast_db;
    k = (memory_target < busComp.gr_memory_db)
            ? busComp_coef(BUS_COMP_CHARGE_MS)
            : busComp_coef(500.0f + 4500.0f * t * t);
    busComp.gr_memory_db += k * (memory_target - busComp.gr_memory_db);
    gain = exp2f((fminf(busComp.gr_fast_db, busComp.gr_memory_db) +
                  makeup_db) * BUS_COMP_LOG2_PER_DB_GAIN);

    /*
     * Per-sample feed-forward detector, gain ramp, cubic saturator, and fade.
     * c_in/c_out fold the full-scale and drive constants so small-signal gain
     * stays equal to the ramped compressor gain while the cubic remains a soft
     * ceiling at 1/drive. The sum is the selected pair's input power.
     */
    c_in = drive * (1.0f / BUS_COMP_SAT_SPAN) * BUS_COMP_INV_FULL_SCALE;
    c_out = (BUS_COMP_SAT_SPAN * BUS_COMP_FULL_SCALE) / drive;
    c_out3 = c_out * (-1.0f / 3.0f);
    gk = busComp.gain_prev * c_in;
    gk_step = (gain - busComp.gain_prev) * c_in *
              (1.0f / (float)OUTPUT_DMA_SIZE);
    sum = 0.0f;
    for (i = 0u; i < OUTPUT_DMA_SIZE; i++) {
        const float xl = (float)buf[2u * i];
        const float xr = (float)buf[2u * i + 1u];
        float ul;
        float ur;

        gk += gk_step;
        w += w_step;
        sum += xl * xl + xr * xr;
        ul = fminf(fmaxf(xl * gk, -1.0f), 1.0f);
        ur = fminf(fmaxf(xr * gk, -1.0f), 1.0f);
        ul *= c_out + c_out3 * ul * ul;
        ur *= c_out + c_out3 * ur * ur;
        buf[2u * i] = (sample_mx_t)(xl + w * (ul - xl));
        buf[2u * i + 1u] = (sample_mx_t)(xr + w * (ur - xr));
    }
    busComp.gain_prev = gain;

    /* The detector smooths this block's mean (L^2 + R^2)/2 over 5 ms. */
    busComp.power += busComp_coef(BUS_COMP_RMS_MS) *
        (sum * (0.5f / (float)OUTPUT_DMA_SIZE) *
             (BUS_COMP_INV_FULL_SCALE * BUS_COMP_INV_FULL_SCALE) -
         busComp.power);
    if (fade_out)
        busComp.active = SCENE_BUS_COMP_MODE_OFF;
}
