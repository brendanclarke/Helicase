/*
 * Core/DSP/Effects/Space/SpaceParameters.c
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
 * Space Effect rows, tokens and page text (S095 P1 plan §4, §6 item 8).
 * The descriptor tables, the three persisted tokens, and one value-text
 * formatter for all three types, driven by a row-kind table per type
 * (§S18: one function instead of six hooks). Storage and the DSP read the
 * raw byte; only the page text follows each row's law.
 */
#include "SpaceParameters.h"
#include "SpaceEffect.h"
#include "EffectParamRows.h"
#include "menu.h"
#include "MenuText.h"
#include "menuEffects.h"
#include "FxBuffer.h"
#include "CrumpBitParameters.h"
#include <string.h>

/*
 * Registry identity (D1): the persisted tokens (AutoSave, `.fx`, MIDI
 * Learn match these three bytes), the 5-cell abbreviation of the type
 * browse, and the 8-cell full name.
 */
const char space_delayToken3[]  = "dly";
const char space_delayAbbrev5[] = "Delay";
const char space_delayFull8[]   = "Delay";
const char space_reverbToken3[]  = "rvb";
const char space_reverbAbbrev5[] = "Revrb";
const char space_reverbFull8[]   = "Reverb";
const char space_echoToken3[]  = "ech";
const char space_echoAbbrev5[] = "Echo ";
const char space_echoFull8[]   = "Echo";

/*
 * Delay rows (plan §4.1; D3, D4, D5, D10, D23; options O1, O3, O4, O6, O8,
 * O9). Defaults: a musical starting point (mix 40, time row 64 = a
 * StepScale stop, feedback 64 ≈ 0.6, flat tone, clean tape).
 */
const effect_param_descriptor_t space_delayDescriptors[] = {
    EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                       EFFECT_FLAGS_IMAGE, EFFECT_FLAGS_IMAGE),
    EFFECT_ROW("dly_mix", "Delay", "Mix", "mix", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 40u, 127u),
    /* D3: CrumpBit's `dly` curve, clamped to the live tape (effective_max). */
    EFFECT_ROW_FLAGS("dly_time", "Delay", "Time", "tim", DTYPE_0B127,
                     EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u,
                     EFFECT_PARAM_FLAG_BUFFER_DEPENDENT),
    /* D23: continuous R/L time ratio 1/2..2 (63 = equal). */
    EFFECT_ROW("dly_spread", "Delay", "Spread", "spr", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("dly_feedback", "Delay", "Feedback", "fbk", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    /* D23: continuous L/R feedback cross-feed (0 stereo .. 127 ping-pong). */
    EFFECT_ROW("dly_xfeed", "Delay", "XFeed", "xfb", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("dly_tone", "Delay", "Tone", "ton", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("dly_degrade", "Delay", "Degrade", "deg", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("dly_wow", "Delay", "WowFlutr", "wow", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("dly_hold", "Delay", "Hold", "hld", DTYPE_ON_OFF,
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE, EFFECT_MOD_NONE, 0u, 1u),
    EFFECT_ROW("dly_reverse", "Delay", "Reverse", "rev", DTYPE_ON_OFF,
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE, EFFECT_MOD_NONE, 0u, 1u),
    /* §S18: seven stops in 0..127 zones, so the knob steps in order. */
    EFFECT_ROW("dly_heads", "Delay", "Heads", "hed", DTYPE_0B127,
               INSTRUMENT_PARAM_FLAG_MORPHABLE |
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
               EFFECT_MOD_NONE, SPACE_HEADS_DEFAULT, 127u),
    EFFECT_ROW("dly_duck", "Delay", "Ducking", "dck", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
};

/*
 * Reverb rows (plan §4.2; D7, D8, D12, O10-O12). `pre` is the only
 * BUFFER_DEPENDENT row; `frz` is the three-state freeze (O12).
 */
const effect_param_descriptor_t space_reverbDescriptors[] = {
    EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                       EFFECT_FLAGS_IMAGE, EFFECT_FLAGS_IMAGE),
    EFFECT_ROW("rvb_mix", "Reverb", "Mix", "mix", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 40u, 127u),
    EFFECT_ROW_FLAGS("rvb_predelay", "Reverb", "PreDelay", "pre",
                     DTYPE_0B127, EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u,
                     127u, EFFECT_PARAM_FLAG_BUFFER_DEPENDENT),
    EFFECT_ROW("rvb_size", "Reverb", "Size", "siz", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 96u, 127u),
    EFFECT_ROW("rvb_decay", "Reverb", "Decay", "dcy", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("rvb_diffusion", "Reverb", "Diffusn", "dif", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 96u, 127u),
    EFFECT_ROW("rvb_low_shelf", "Reverb", "LowShelf", "lsh", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("rvb_low_freq", "Reverb", "LowFreq", "lsf", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 40u, 127u),
    EFFECT_ROW("rvb_high_shelf", "Reverb", "HighDamp", "hsh", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 40u, 127u),
    EFFECT_ROW("rvb_high_freq", "Reverb", "HighFreq", "hsf", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 80u, 127u),
    EFFECT_ROW("rvb_mod", "Reverb", "Modulatn", "mod", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 32u, 127u),
    EFFECT_ROW("rvb_width", "Reverb", "Width", "wid", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("rvb_freeze", "Reverb", "Freeze", "frz", DTYPE_0B127,
               INSTRUMENT_PARAM_FLAG_MORPHABLE |
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
               EFFECT_MOD_NONE, 0u, 127u),
    EFFECT_ROW("rvb_shimmer", "Shimmer", "Amount", "shm", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("rvb_interval", "Shimmer", "Interval", "int", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE,
               EFFECT_MOD_RANGE(SPACE_INTERVAL_ROW_MIN,
                                SPACE_INTERVAL_ROW_MAX),
               75u, SPACE_INTERVAL_ROW_MAX),
    EFFECT_ROW("rvb_detune", "Shimmer", "Detune", "dtn", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("rvb_env", "Reverb", "GateBlom", "env", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("rvb_duck", "Reverb", "Ducking", "dck", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    /* §S18: a one-pole low-pass on each output side; 127 leaves it flat. */
    EFFECT_ROW("rvb_tone", "Reverb", "Tone", "ton", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 127u, 127u),
};

/*
 * Echo rows (plan §4.3; D11, O14-O16). A mono tape into a small plate; the
 * plate has no predelay and no shimmer. `tim` is the Delay's law.
 */
const effect_param_descriptor_t space_echoDescriptors[] = {
    EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                       EFFECT_FLAGS_IMAGE, EFFECT_FLAGS_IMAGE),
    EFFECT_ROW("ech_mix", "Echo", "Mix", "mix", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 40u, 127u),
    EFFECT_ROW_FLAGS("ech_time", "Echo", "Time", "tim", DTYPE_0B127,
                     EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u,
                     EFFECT_PARAM_FLAG_BUFFER_DEPENDENT),
    EFFECT_ROW("ech_feedback", "Echo", "Feedback", "fbk", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("ech_tone", "Echo", "Tone", "ton", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("ech_degrade", "Echo", "Degrade", "deg", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("ech_wow", "Echo", "WowFlutr", "wow", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("ech_to_reverb", "Echo", "EchoRvb", "e>r", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("ech_blend", "Echo", "Blend", "bln", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 48u, 127u),
    EFFECT_ROW("ech_decay", "Plate", "Decay", "dcy", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 48u, 127u),
    EFFECT_ROW("ech_low_shelf", "Plate", "LowShelf", "lsh", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("ech_high_shelf", "Plate", "HighDamp", "hsh", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 40u, 127u),
    EFFECT_ROW("ech_heads", "Echo", "Heads", "hed", DTYPE_0B127,
               INSTRUMENT_PARAM_FLAG_MORPHABLE |
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
               EFFECT_MOD_NONE, SPACE_HEADS_DEFAULT, 127u),
    EFFECT_ROW("ech_duck", "Echo", "Ducking", "dck", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("ech_routing", "Echo", "Routing", "rtg", DTYPE_0B127,
               INSTRUMENT_PARAM_FLAG_MORPHABLE |
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
               EFFECT_MOD_NONE, 0u, 127u),
};

_Static_assert(sizeof(space_delayDescriptors) /
                   sizeof(space_delayDescriptors[0]) == SPACE_DELAY_PARAM_COUNT,
               "SPACE_DELAY_PARAM_COUNT must match the descriptor table");
_Static_assert(SPACE_DELAY_PARAM_ENUM_COUNT == SPACE_DELAY_PARAM_COUNT,
               "space_delay_param_t must match the descriptor table");
_Static_assert(sizeof(space_reverbDescriptors) /
                   sizeof(space_reverbDescriptors[0]) == SPACE_REVERB_PARAM_COUNT,
               "SPACE_REVERB_PARAM_COUNT must match the descriptor table");
_Static_assert(SPACE_REVERB_PARAM_ENUM_COUNT == SPACE_REVERB_PARAM_COUNT,
               "space_reverb_param_t must match the descriptor table");
_Static_assert(sizeof(space_echoDescriptors) /
                   sizeof(space_echoDescriptors[0]) == SPACE_ECHO_PARAM_COUNT,
               "SPACE_ECHO_PARAM_COUNT must match the descriptor table");
_Static_assert(SPACE_ECHO_PARAM_ENUM_COUNT == SPACE_ECHO_PARAM_COUNT,
               "space_echo_param_t must match the descriptor table");

/*
 * Row kinds of the page text (one per row 3..; SPACE_TXT_NONE: the generic
 * dtype text).
 */
enum {
    SPACE_TXT_NONE = 0,
    SPACE_TXT_TIME,      /* `tim`: the row the DSP plays, CrumpBit's labels (D3) */
    SPACE_TXT_RATIO,     /* `spr`: R/L ratio, "0.5" .. "2.0" (D23) */
    SPACE_TXT_FBK,       /* `fbk`: loop gain in percent, 0 .. 120 (D4) */
    SPACE_TXT_PCT,       /* 0 .. 100 percent */
    SPACE_TXT_HEADS,     /* `hed`: the heads in use (O6) */
    SPACE_TXT_PRE,       /* `pre`: the predelay played, ms to 999, then s */
    SPACE_TXT_DECAY,     /* `dcy`: RT60 s, "0.3" .. "9.9", then 10 .. 20 */
    SPACE_TXT_FREEZE,    /* `frz`: off / on / shm (O12) */
    SPACE_TXT_SIGNED,    /* `int`: semitones, value - 63 */
    SPACE_TXT_CENTS,     /* `dtn`: 0 .. 50 cents */
    SPACE_TXT_ENV,       /* `env`: off at 63, else value - 63 (O10) */
    SPACE_TXT_ROUTE      /* `rtg`: e>r / r>e / lp (O16) */
};

static const uint8_t space_delayText[SPACE_DELAY_PARAM_COUNT - 3u] = {
    SPACE_TXT_NONE, SPACE_TXT_TIME, SPACE_TXT_RATIO, SPACE_TXT_FBK,
    SPACE_TXT_PCT, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_HEADS, SPACE_TXT_PCT
};

static const uint8_t space_reverbText[SPACE_REVERB_PARAM_COUNT - 3u] = {
    SPACE_TXT_NONE, SPACE_TXT_PRE, SPACE_TXT_NONE, SPACE_TXT_DECAY,
    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_FREEZE,
    SPACE_TXT_NONE, SPACE_TXT_SIGNED, SPACE_TXT_CENTS, SPACE_TXT_ENV,
    SPACE_TXT_PCT, SPACE_TXT_NONE
};

static const uint8_t space_echoText[SPACE_ECHO_PARAM_COUNT - 3u] = {
    SPACE_TXT_NONE, SPACE_TXT_TIME, SPACE_TXT_FBK, SPACE_TXT_NONE,
    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
    SPACE_TXT_DECAY, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_HEADS,
    SPACE_TXT_PCT, SPACE_TXT_ROUTE
};

/* The stepped rows' names (O12, O16). */
static const char space_frzNames[3][4] = { "off", " on", "shm" };
static const char space_rtgNames[3][4] = { "e>r", "r>e", "lp " };

/*
 * The seven `hed` stops in knob order, D/3 2D/3 D as 1 2 3 (O6; §S18: the
 * labels rise with the knob).
 */
static const char space_hedNames[SPACE_HEADS_STOPS][4] = {
    "  1", "  2", "  3", " 12", " 13", " 23", "123"
};

/* "x.y" from tenths (0..99). */
static uint8_t space_tenths(uint32_t t, char *out)
{
    out[0] = (char)('0' + (t / 10u) % 10u);
    out[1] = '.';
    out[2] = (char)('0' + t % 10u);
    return 1u;
}

/*
 * The value text of a Space row (S095 P1 plan §6 item 8).
 * What: out[0..2] for the row's kind (above); stepped kinds only when
 * `target` (the STEP automation page and Pattern entry). The `tim` text is
 * the row the DSP plays: the value clamped first to the live tape's limit
 * (space_timeLimit(), the clamp effects_service() applies).
 * Inputs: the type's kind table and tag, row index, value, out, target.
 * Output: nonzero when out was written. Foreground.
 * Callers: the three types' format hooks below.
 */
static __attribute__((optimize("Os"))) uint8_t space_formatValue(const uint8_t *kinds, uint8_t count,
                                 uint8_t tag, uint8_t index, uint8_t value,
                                 char *out, uint8_t target)
{
    uint8_t kind;

    if (index < EFFECT_COMMON_PARAM_COUNT ||
        index - EFFECT_COMMON_PARAM_COUNT >= count)
        return 0u;
    kind = kinds[index - EFFECT_COMMON_PARAM_COUNT];
    if (target && kind != SPACE_TXT_HEADS && kind != SPACE_TXT_FREEZE &&
        kind != SPACE_TXT_ROUTE)
        return 0u;
    switch (kind) {
    case SPACE_TXT_TIME: {
        fx_share_t share;
        uint8_t limit;

        fxbuf_effectShare(&share);
        limit = space_timeLimit(tag, &share);
        crumpBit_formatDelayRow((value > limit) ? limit : value, out);
        return 1u;
    }
    case SPACE_TXT_RATIO:
        return space_tenths((uint32_t)(space_sprRatio(value) * 10.0f + 0.5f),
                            out);
    case SPACE_TXT_FBK:
        numtostrpu(out, (uint8_t)(space_fbkGain(value) * 100.0f + 0.5f), ' ');
        return 1u;
    case SPACE_TXT_PCT:
        numtostrpu(out, (uint8_t)(((uint32_t)value * 100u + 63u) / 127u), ' ');
        return 1u;
    case SPACE_TXT_HEADS:
        memcpy(out, space_hedNames[space_stop(value, SPACE_HEADS_STOPS)], 3u);
        return 1u;
    case SPACE_TXT_PRE: {
        fx_share_t share;
        const uint8_t limit = (fxbuf_effectShare(&share),
                               space_preLimit(&share));
        const float r = (float)((value > limit) ? limit : value) *
                        (1.0f / 127.0f);
        const uint32_t ms = (uint32_t)(r * r * 1400.0f + 0.5f);

        if (ms >= 1000u)
            return space_tenths((ms + 50u) / 100u, out);
        numtostrpu(out, (uint8_t)((ms > 255u) ? 0u : ms), ' ');
        if (ms > 255u) {
            out[0] = (char)('0' + ms / 100u);
            out[1] = (char)('0' + (ms / 10u) % 10u);
            out[2] = (char)('0' + ms % 10u);
        }
        return 1u;
    }
    case SPACE_TXT_DECAY: {
        const float r = space_rt60(value);

        if (r < 9.95f)
            return space_tenths((uint32_t)(r * 10.0f + 0.5f), out);
        numtostrpu(out, (uint8_t)(r + 0.5f), ' ');
        return 1u;
    }
    case SPACE_TXT_FREEZE:
        memcpy(out, space_frzNames[space_stop(value, 3u)], 3u);
        return 1u;
    case SPACE_TXT_ROUTE:
        memcpy(out, space_rtgNames[space_stop(value, 3u)], 3u);
        return 1u;
    case SPACE_TXT_ENV:
        if (value == 63u) {
            memcpy(out, "off", 3u);
            return 1u;
        }
        __attribute__((fallthrough));
    case SPACE_TXT_SIGNED:
        numtostrps(out, (int8_t)((int32_t)value - 63));
        return 1u;
    case SPACE_TXT_CENTS:
        numtostrpu(out, (uint8_t)(((uint32_t)value * 50u + 63u) / 127u), ' ');
        return 1u;
    default:
        return 0u;
    }
}

static uint8_t space_delayFormatValue3(uint8_t index, uint8_t value, char *out)
{
    return space_formatValue(space_delayText, sizeof(space_delayText),
                             SPACE_TAG_DELAY, index, value, out, 0u);
}

static uint8_t space_delayFormatTarget3(uint8_t index, uint8_t value, char *out)
{
    return space_formatValue(space_delayText, sizeof(space_delayText),
                             SPACE_TAG_DELAY, index, value, out, 1u);
}

static uint8_t space_reverbFormatValue3(uint8_t index, uint8_t value, char *out)
{
    return space_formatValue(space_reverbText, sizeof(space_reverbText),
                             SPACE_TAG_REVERB, index, value, out, 0u);
}

static uint8_t space_reverbFormatTarget3(uint8_t index, uint8_t value, char *out)
{
    return space_formatValue(space_reverbText, sizeof(space_reverbText),
                             SPACE_TAG_REVERB, index, value, out, 1u);
}

static uint8_t space_echoFormatValue3(uint8_t index, uint8_t value, char *out)
{
    return space_formatValue(space_echoText, sizeof(space_echoText),
                             SPACE_TAG_ECHO, index, value, out, 0u);
}

static uint8_t space_echoFormatTarget3(uint8_t index, uint8_t value, char *out)
{
    return space_formatValue(space_echoText, sizeof(space_echoText),
                             SPACE_TAG_ECHO, index, value, out, 1u);
}

/*
 * The hooks: values only. No SELECT/TRACK/BAR ownership, no LED row, no
 * custom row 0 and no layout (the default page layout places rows 3.. on
 * SELECT 2.., menuEffects.c header), no label variants.
 */
const effect_ui_hooks_t space_delayUi = {
    .select = NULL, .track = NULL, .bar = NULL, .render_leds = NULL,
    .paint_row0 = NULL, .format_value3 = space_delayFormatValue3,
    .flags = 0u, .labels = NULL, .layout = NULL,
    .format_target3 = space_delayFormatTarget3,
};

const effect_ui_hooks_t space_reverbUi = {
    .select = NULL, .track = NULL, .bar = NULL, .render_leds = NULL,
    .paint_row0 = NULL, .format_value3 = space_reverbFormatValue3,
    .flags = 0u, .labels = NULL, .layout = NULL,
    .format_target3 = space_reverbFormatTarget3,
};

const effect_ui_hooks_t space_echoUi = {
    .select = NULL, .track = NULL, .bar = NULL, .render_leds = NULL,
    .paint_row0 = NULL, .format_value3 = space_echoFormatValue3,
    .flags = 0u, .labels = NULL, .layout = NULL,
    .format_target3 = space_echoFormatTarget3,
};
