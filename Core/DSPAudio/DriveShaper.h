/*
 * DriveShaper — the filter input stage per drive type (S084 VX2 VX2-7.1).
 *
 * What:       14 shapers g(u) that replace the ZDF filter's input soft clip,
 *             u = input x drive (drive = 0.4 + 6 (drv/127)^2, unchanged):
 *             sft rct art thf fdc bsw tsw lsw lsq sfd afd asf chb rsb.
 *             Each output is level-trimmed so a sine at drv 64 has sft's RMS,
 *             capped so the drv-127 peak stays within sft's ceiling of 2.0.
 *             The stateful types (art fdc tsw lsw lsq rsb) detect zero
 *             crossings on u with hysteresis 0.03 and keep their state in
 *             DriveShaperState; rct art fdc are DC-blocked (one pole,
 *             R = 0.9986, about 10 Hz). Trackers use 1/period computed once
 *             per block (driveShaper_blockInc()), so no shaper divides per
 *             sample.
 * Why:        plan §4.4, r3 naming; the definitions are those of
 *             tools/plots/plot_waveshapers.py, which is the reference.
 * Inputs:     type, state, u, and s = u N(u^2/4) / D(u^2/4) (the sft output
 *             already computed by the shared Stage A, D7).
 * Outputs:    the shaped filter input.
 * Accessors:  ResonantFilter.c SVF_calcBlockZDFShaped(); the engines
 *             (state reset); InstrumentManager.c (type writer).
 * Affiliates: tools/plots/plot_waveshapers.py (definitions, trims, the
 *             --export-ref arrays for tools/dsp_test/test_shaper.c).
 */
#ifndef DRIVESHAPER_H_
#define DRIVESHAPER_H_
#include <stdint.h>
#include <math.h>

#define DRIVE_TYPE_SFT 0u
#define DRIVE_TYPE_RCT 1u
#define DRIVE_TYPE_ART 2u
#define DRIVE_TYPE_THF 3u
#define DRIVE_TYPE_FDC 4u
#define DRIVE_TYPE_BSW 5u
#define DRIVE_TYPE_TSW 6u
#define DRIVE_TYPE_LSW 7u
#define DRIVE_TYPE_LSQ 8u
#define DRIVE_TYPE_SFD 9u
#define DRIVE_TYPE_AFD 10u
#define DRIVE_TYPE_ASF 11u
#define DRIVE_TYPE_CHB 12u
#define DRIVE_TYPE_RSB 13u
#define DRIVE_TYPE_COUNT 14u

#define DRIVE_SHAPER_HYST     0.03f
#define DRIVE_SHAPER_DC_R     0.9986f
#define DRIVE_SHAPER_ENV_REL  0.998867f   /* exp(-1 / (0.020 * 44108)) */

#define DRIVE_FLAG_ARMED      0x01u
#define DRIVE_FLAG_NEG_ARMED  0x02u
#define DRIVE_FLAG_TOGGLE     0x04u
#define DRIVE_FLAG_LATCHED    0x08u

typedef struct {
    float    ramp;      /* tracker ramp 0..1 (starts 1) */
    float    env;       /* latched cycle amplitude (|s|/2 scale) */
    float    curMax;    /* peak of the running cycle */
    float    dcX, dcY;  /* DC blocker state */
    uint16_t since;     /* samples since the last rising crossing (saturating) */
    uint16_t period;    /* last measured period in samples (starts 200) */
    uint8_t  flags;     /* DRIVE_FLAG_* */
    uint8_t  count;     /* fdc crossing counter, mod 3 */
    uint16_t reserved;
} DriveShaperState;     /* 28 B */

extern const float driveShaper_trim[DRIVE_TYPE_COUNT];

/*
 * Reset one shaper (S084 VX2 VX2-7.2).
 *
 * What:       the script's initial state: ramp 1, period 200, both arm flags,
 *             the rest zero.
 * Why:        a drive-type change must never carry another type's tracker or
 *             DC state.
 * Inputs:     state.  Outputs: reset state.
 * Accessors:  IM_SPECIAL_DRIVE_TYPE writer, the engines' init.
 */
void driveShaper_reset(DriveShaperState *st);

/*
 * Per-block tracker reciprocal (S084 VX2 VX2-7.1).
 *
 * What:       1 / max(period, 4): one division per block, none per sample.
 * Inputs:     state.  Outputs: ramp step.
 * Accessors:  driveShaper_apply() callers (SVF_calcBlockZDFShaped()).
 */
static inline float driveShaper_blockInc(const DriveShaperState *st)
{
    const uint16_t p = (st->period < 4u) ? 4u : st->period;
    return 1.0f / (float)p;
}

/* Periodic triangle fold: identity inside +-t, mirrored beyond. */
static inline float driveShaper_triFold(float v, float t)
{
    const float w = (v + t) * (1.0f / (4.0f * t));
    int32_t n = (int32_t)(w * 16777216.0f);

    n &= 0x00ffffff;
    {
        const float frac = (float)n * (1.0f / 16777216.0f);
        const float d = frac - 0.5f;

        return t - 4.0f * t * ((d < 0.0f) ? -d : d);
    }
}

/* sin(pi/2 * t) as the plotted polynomial, t in -1..1 (triangle-folded). */
static inline float driveShaper_sinFold(float v)
{
    const float t = driveShaper_triFold(v, 1.0f);
    const float t2 = t * t;

    return t * (1.5707963f + t2 *
        (-0.6459640f + t2 * (0.0796926f - t2 * 0.0046817f)));
}

static inline float driveShaper_dc(DriveShaperState *st, float x)
{
    const float y = x - st->dcX + DRIVE_SHAPER_DC_R * st->dcY;

    st->dcX = x;
    st->dcY = y;
    return y;
}

static inline void driveShaper_crossings(DriveShaperState *st, float u,
                                         uint8_t *rise, uint8_t *fall)
{
    *rise = 0u;
    *fall = 0u;
    if ((st->flags & DRIVE_FLAG_ARMED) && u > DRIVE_SHAPER_HYST) {
        *rise = 1u;
        st->flags &= (uint8_t)~DRIVE_FLAG_ARMED;
    }
    if (u < -DRIVE_SHAPER_HYST)
        st->flags |= DRIVE_FLAG_ARMED;
    if ((st->flags & DRIVE_FLAG_NEG_ARMED) && u < -DRIVE_SHAPER_HYST) {
        *fall = 1u;
        st->flags &= (uint8_t)~DRIVE_FLAG_NEG_ARMED;
    }
    if (u > DRIVE_SHAPER_HYST)
        st->flags |= DRIVE_FLAG_NEG_ARMED;
}

/*
 * One shaped sample (S084 VX2 VX2-7.1).
 *
 * What:       applies the type's transfer to (u, s), updating the tracker,
 *             latch and DC state, and returns the level-trimmed output.
 * Why:        the ZDF loop calls this once per sample; all state lives in
 *             DriveShaperState, never in ResonantFilter.
 * Inputs:     type 0..13, state, u, s (the shared Stage A soft clip), inc.
 * Outputs:    the shaped input.
 * Accessors:  SVF_calcBlockZDFShaped().
 * Affiliates: plot_waveshapers.py, driveShaper_trim[].
 */
static inline float driveShaper_apply(uint8_t type, DriveShaperState *st,
                                      float u, float s, float inc)
{
    uint8_t rise;
    uint8_t fall;
    float y;

    if (type >= DRIVE_TYPE_COUNT)
        type = DRIVE_TYPE_SFT;
    if (type == DRIVE_TYPE_SFT)
        return s * driveShaper_trim[type];

    switch (type) {
    case DRIVE_TYPE_THF:
        return driveShaper_triFold(u, 0.6f) * (1.0f / 0.6f) *
               driveShaper_trim[type];
    case DRIVE_TYPE_SFD:
        return driveShaper_sinFold(u) * driveShaper_trim[type];
    case DRIVE_TYPE_AFD:
        return driveShaper_sinFold(u + 0.05f * u * u * u) *
               driveShaper_trim[type];
    case DRIVE_TYPE_ASF:
        /* asf removes triFold(0.35, 1) = 0.35 (the script's offset). */
        return (driveShaper_triFold(u + 0.35f, 1.0f) - 0.35f) *
               driveShaper_trim[type];
    case DRIVE_TYPE_RCT:
        return driveShaper_dc(st, (s < 0.0f) ? -s : s) *
               driveShaper_trim[type];
    case DRIVE_TYPE_BSW: {
        const float v = (0.5f * s + 1.0f) * 127.5f + 0.5f;
        int32_t q = (int32_t)v;
        uint32_t r;

        if (q < 0)
            q = 0;
        if (q > 255)
            q = 255;
        r = (((uint32_t)q & 0x0fu) << 4) | ((uint32_t)q >> 4);
        return ((float)r * (1.0f / 127.5f) - 1.0f) *
               driveShaper_trim[type];
    }
    case DRIVE_TYPE_CHB: {
        const float x = 0.5f * s;

        return (3.0f * x - 4.0f * x * x * x) * driveShaper_trim[type];
    }
    default:
        break;
    }

    /*
     * Stateful types (S084 VX2 review fix F3).
     *
     * What:       the crossings run for all six stateful types; the
     *             amplitude latch (since, curMax, period, env) runs only for
     *             the trackers tsw/lsw/lsq, the only types whose output uses
     *             it; |s| is computed once; toggles are branch-free
     *             (flags ^= rise << 2, sign = ((flags & TOGGLE) >> 1) - 1);
     *             the fdc counter wraps with a compare instead of % 3.
     * Why:        measured cost was sft +43..+55 instructions per sample for
     *             these types against the plan's +6; art, fdc and rsb paid
     *             for a latch they never read. Outputs are unchanged for
     *             every type (the skipped latch state is only ever read by
     *             the trackers, and a type change resets the state).
     * Inputs:     u (crossings), s, inc (trackers).
     * Outputs:    the trimmed sample; state updates.
     */
    {
        const float abs_s = (s < 0.0f) ? -s : s;

        driveShaper_crossings(st, u, &rise, &fall);
        st->flags ^= (uint8_t)(rise << 2);      /* DRIVE_FLAG_TOGGLE */

        switch (type) {
        case DRIVE_TYPE_ART:
            y = ((s > 0.0f) && (st->flags & DRIVE_FLAG_TOGGLE)) ? s : 0.0f;
            return driveShaper_dc(st, y) * driveShaper_trim[type];

        case DRIVE_TYPE_FDC: {
            uint8_t count = (uint8_t)(st->count + (rise | fall));

            if (count >= 3u)
                count = 0u;
            st->count = count;
            y = (count == 0u) ? ((abs_s > 1.3f) ? 1.3f : abs_s) : 0.0f;
            return driveShaper_dc(st, y * (2.0f / 1.3f)) *
                   driveShaper_trim[type];
        }

        case DRIVE_TYPE_RSB: {
            const float sign =
                (float)((int32_t)((st->flags & DRIVE_FLAG_TOGGLE) >> 1) - 1);

            return s * sign * driveShaper_trim[type];
        }

        default:
            break;
        }

        /* Trackers only: the cycle amplitude latch. */
        st->since = (st->since < 0xffffu) ? (uint16_t)(st->since + 1u)
                                          : 0xffffu;
        {
            const float half = 0.5f * abs_s;

            if (half > st->curMax)
                st->curMax = half;
            if (rise) {
                st->period = st->since;
                st->since = 0u;
                st->env = st->curMax;
                st->curMax = 0.0f;
                st->flags |= DRIVE_FLAG_LATCHED;
            }
            if (!(st->flags & DRIVE_FLAG_LATCHED)) {
                const float decayed = st->env * DRIVE_SHAPER_ENV_REL;

                st->env = (half > decayed) ? half : decayed;
            }
        }

        if (type == DRIVE_TYPE_LSQ) {
            const float sign =
                (float)((int32_t)((st->flags & DRIVE_FLAG_TOGGLE) >> 1) - 1);

            return 2.0f * st->env * sign * driveShaper_trim[type];
        }

        /* tsw resets on every rising crossing, lsw on every second one. */
        if (rise && (type == DRIVE_TYPE_TSW ||
                     (st->flags & DRIVE_FLAG_TOGGLE)))
            st->ramp = 0.0f;
        st->ramp += (type == DRIVE_TYPE_TSW) ? inc : (0.5f * inc);
        if (st->ramp > 1.0f)
            st->ramp = 1.0f;
        return 2.0f * st->env * (1.0f - 2.0f * st->ramp) *
               driveShaper_trim[type];
    }
}

#endif /* DRIVESHAPER_H_ */
