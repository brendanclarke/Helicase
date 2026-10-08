/*
 * Core/Sequencer/StepScale.c
 *
 * Shared 96-PPQ step-scale table for Pattern display/storage and the FX
 * sequencer clock (Session 072 step 8; Effects plan §11.3).
 */

#include "StepScale.h"
#include <stddef.h>

/*
 * 128-entry Q8.8 step-scale tick LUT (S078 §2.4).
 *
 * What: each entry is round(multiplier(cc) * 24 * 256) in Q8.8 format, where
 * multiplier(cc) = 0.25 * 2^(cc / (127/5)), nudged at the 14 musical stops.
 * Why: the DDA accumulator in sequencer.c adds 256 (1.0 Q8.8) per PPQ tick and
 * compares against this interval to decide when each track advances. Inputs:
 * the step_scale_0_127.csv nudged multiplier column. Outputs: read by
 * stepScale_ticksQ8(cc). Range: min 1536 (CC 0, 0.25x), max 49152 (CC 127,
 * 8.0x); all fit uint16_t. Affiliates: sequencer.c per-track accumulator,
 * seq_fxClockTick(), seq_realignActivePatternToMasterClock(),
 * crumpBit_divisionFor().
 */
static const uint16_t stepScale_q8Table[STEP_SCALE_COUNT] = {
    /* Stage 2 true Q8.8 values: round(nudged_multiplier * 24 * 256). */
    1536u, 1564u, 1593u, 1622u, 1652u, 1683u, 1714u, 1745u, /*   0..  7 */
    1777u, 1810u, 1843u, 1877u, 1912u, 1947u, 1983u, 2019u, /*   8.. 15 */
    2048u, 2094u, 2133u, 2172u, 2212u, 2253u, 2294u, 2337u, /*  16.. 23 */
    2380u, 2423u, 2468u, 2514u, 2560u, 2607u, 2655u, 2704u, /*  24.. 31 */
    2754u, 2804u, 2856u, 2908u, 2962u, 3016u, 3072u, 3129u, /*  32.. 39 */
    3186u, 3245u, 3305u, 3365u, 3427u, 3490u, 3555u, 3620u, /*  40.. 47 */
    3687u, 3755u, 3824u, 3894u, 3966u, 4039u, 4096u, 4189u, /*  48.. 55 */
    4266u, 4344u, 4424u, 4506u, 4608u, 4673u, 4759u, 4847u, /*  56.. 63 */
    4936u, 5027u, 5120u, 5214u, 5310u, 5408u, 5507u, 5608u, /*  64.. 71 */
    5712u, 5817u, 5924u, 6033u, 6144u, 6400u, 6666u, 6943u, /*  72.. 79 */
    7232u, 7533u, 7847u, 8192u, 8514u, 8868u, 9216u, 9621u, /*  80.. 87 */
    10022u, 10439u, 10873u, 11326u, 11797u, 12288u, 12799u, 13332u, /*  88.. 95 */
    13887u, 14465u, 15067u, 15694u, 16384u, 17027u, 17736u, 18432u, /*  96..103 */
    19243u, 20043u, 20878u, 21746u, 22651u, 23594u, 24576u, 25599u, /* 104..111 */
    26664u, 27774u, 28930u, 30133u, 31388u, 32694u, 34054u, 35472u, /* 112..119 */
    36864u, 38485u, 40087u, 41755u, 43493u, 45303u, 47188u, 49152u, /* 120..127 */
};

/*
 * Musical stop label table (S078 §2.2, §2.3).
 *
 * What: the 14 CC positions whose log multipliers are nudged to exact musical
 * subdivisions. Each entry carries the CC value, three-character short name,
 * and full label. Why: stepScale_shortName() and stepScale_longName() scan
 * this table to return symbolic labels when the parameter is exactly on a
 * musical stop; non-stop positions return NULL so the caller shows the raw CC
 * integer. Inputs: step_scale_0_127.csv musical value column. Outputs: read by
 * the stepScale_shortName/longName/isMusicalStop APIs and formatShort().
 * Affiliates: menu.c MENU_TRACK_SCALE display, menuEffects.c FX Seq display.
 */
typedef struct {
    uint8_t  cc;
    char     short_name[4];  /* 3 chars + NUL */
    char     long_name[8];   /* up to 7 chars + NUL */
} stepScale_musicalStop_t;

static const stepScale_musicalStop_t stepScale_stops[14] = {
    {  0u, "/64", "1/64"   },
    { 16u, "32t", "1/32T"  },
    { 38u, "/32", "1/32"   },
    { 54u, "16t", "1/16T"  },
    { 60u, "d32", "d 1/32" },
    { 76u, "/16", "1/16"   },
    { 83u, "8Tr", "1/8T"   },
    { 86u, "d16", "d 1/16" },
    { 93u, "/8 ", "1/8"    },
    {100u, "4Tr", "1/4T"   },
    {103u, "d/8", "d 1/8"  },
    {110u, "/4 ", "1/4"    },
    {120u, "d/4", "d 1/4"  },
    {127u, "/2 ", "1/2"    },
};

#define STEP_SCALE_STOP_COUNT \
    ((uint8_t)(sizeof(stepScale_stops) / sizeof(stepScale_stops[0])))

uint16_t stepScale_ticksQ8(uint8_t cc)
{
    /*
     * Q8.8 tick interval for one CC position (S078 §2.4.1).
     *
     * Input: CC 0..127. Output: Q8.8 uint16_t tick interval; out-of-range CCs
     * return the 1/16 default. Caller: sequencer.c DDA accumulator per PPQ
     * tick. This is an ISR-safe flash-only read with no allocation. Affiliates:
     * stepScale_q8Table[], seq_processSchedulerTick(), seq_fxClockTick().
     */
    return (cc < STEP_SCALE_COUNT)
        ? stepScale_q8Table[cc] : stepScale_q8Table[STEP_SCALE_DEFAULT];
}

const char *stepScale_shortName(uint8_t cc)
{
    /*
     * Short name and musical-stop query (S078 §2.3).
     *
     * What: linear scan of the 14-entry musical stop table. Returns the
     * symbolic label when the CC is an exact musical stop, NULL otherwise.
     * The caller (menu.c display) formats the raw CC integer when NULL is
     * returned. Why: only 14 of 128 positions have meaningful names; all
     * others show their numeric CC value. Input: CC 0..127. Output: static
     * string pointer or NULL. The 14-entry scan is negligible (foreground
     * only). Affiliates: menu.c MENU_TRACK_SCALE display handler.
     */
    uint8_t i;

    for (i = 0u; i < STEP_SCALE_STOP_COUNT; i++) {
        if (stepScale_stops[i].cc == cc)
            return stepScale_stops[i].short_name;
    }
    return NULL;
}

const char *stepScale_longName(uint8_t cc)
{
    /* Full-width label for the 14 nudge stops, NULL otherwise (S078 §2.3). */
    uint8_t i;

    for (i = 0u; i < STEP_SCALE_STOP_COUNT; i++) {
        if (stepScale_stops[i].cc == cc)
            return stepScale_stops[i].long_name;
    }
    return NULL;
}

uint8_t stepScale_isMusicalStop(uint8_t cc)
{
    /* Nonzero when the CC lands on one of the 14 nudged positions. */
    uint8_t i;

    for (i = 0u; i < STEP_SCALE_STOP_COUNT; i++) {
        if (stepScale_stops[i].cc == cc)
            return 1u;
    }
    return 0u;
}

void stepScale_formatShort(uint8_t cc, char out[3])
{
    /*
     * Shared three-character formatter (S078 §2.3).
     *
     * What: musical stops copy their symbol; every other CC is written as a
     * right-justified decimal in three columns. Why: the three-character
     * fields in menu.c, menuEffects.c, and CrumpBitParameters.c all need the
     * same NULL fallback, so it lives here once. Inputs: CC 0..127 and a
     * three-byte buffer. Outputs: exactly three characters, no terminator.
     */
    const char *name = stepScale_shortName(cc);

    if (name) {
        out[0] = name[0];
        out[1] = name[1];
        out[2] = name[2];
        return;
    }
    out[0] = (cc >= 100u) ? (char)('0' + cc / 100u) : ' ';
    out[1] = (cc >= 10u)  ? (char)('0' + (cc / 10u) % 10u) : ' ';
    out[2] = (char)('0' + cc % 10u);
}
