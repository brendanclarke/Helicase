/*
 * Core/DSP/Effects/CrumpBit/CrumpBitParameters.c
 *
 *  Created on: 29.09.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */

#include "CrumpBitParameters.h"
#include "CrumpBitEffect.h"
#include "menu.h"
#include "MenuText.h"
#include "EffectParamRows.h"
#include "menuEffects.h"
#include "ledHandler.h"
#include "StepScale.h"
#include "sequencer.h"
#include <string.h>

/* Persisted token (permanent), UI abbreviation (5) and full name (<= 8). */
const char crumpBit_token3[] = "cbt";
const char crumpBit_abbrev5[] = "CrmBt";
const char crumpBit_full8[] = "CrumpBit";

/*
 * CrumpBit descriptor table (S074_CRUMPBIT_EFFECT.md §4).
 *
 * What: rows 0..2 are the shared common rows; rows 3..10 are CrumpBit's.
 * - bit off / bit invert (3, 4): 8-bit masks, one bit per ADC data line
 *   (bit 0 = LSB). Not Morphable (interpolating a mask gives unrelated
 *   bits), not modulatable, not Pattern-automatable (a Pattern value is
 *   7-bit and cannot reach bit 7). Sequenceable through FX lanes 1 and 2,
 *   which store full 8-bit values. Edited by the SELECT buttons (Stage 2);
 *   in Stage 1 they show as plain 0..255 numbers.
 * - mix, feedback, rate, delay pan (5, 6, 7, 10): Morphable, LFO and
 *   Pattern automation, like voice rows.
 * - sub-type (8): only `dly` (max 0); its label comes from the format hook,
 *   because every DTYPE_MENU table id (4 bits) is already in use.
 * - sync (9): on/off; Pattern-automatable and sequenceable.
 * - Defaults (S075 F2-E, user): mix 0, feedback 64, rate 64, delay pan 63
 *   (the absolute centre, shows 0). The sync row's 3-letter label is `snc`;
 *   its file key remains `crump_sync`.
 * Why: the registry makes storage, AutoSave, the Effect page, lanes,
 * automation and LFO work without type-specific code elsewhere. Inputs:
 * none (const). Output: 11 rows. Accessors: EffectsManager (resolution,
 * clamps, capabilities), menuEffects/menu.c (labels, dtypes), `.fx` and
 * AutoSave (file keys). Affiliates: EffectParamRows.h, CrumpBitEffect.c
 * write_param().
 */
const effect_param_descriptor_t crumpBit_descriptors[] = {
    EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                       EFFECT_FLAGS_IMAGE,
                       EFFECT_FLAGS_IMAGE),
    EFFECT_ROW("crump_bit_off", "Bits", "BitOff", "bof", DTYPE_0B255,
               0u, EFFECT_MOD_NONE, 0u, 255u),
    EFFECT_ROW("crump_bit_invert", "Bits", "BitInv", "biv", DTYPE_0B255,
               0u, EFFECT_MOD_NONE, 0u, 255u),
    EFFECT_ROW("crump_mix", "Delay", "Mix", "mix", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("crump_feedback", "Delay", "Feedback", "fbk", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("crump_rate", "Delay", "Rate", "rte", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("crump_subtype", "CrumpBit", "SubType", "sub", DTYPE_0B127,
               0u, EFFECT_MOD_NONE, CRUMPBIT_SUBTYPE_DELAY, 0u),
    EFFECT_ROW("crump_sync", "Delay", "Sync", "snc", DTYPE_ON_OFF,
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE, EFFECT_MOD_NONE, 0u, 1u),
    EFFECT_ROW("crump_dly_pan", "Delay", "DlyPan", "dpn", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
};

_Static_assert(sizeof(crumpBit_descriptors) /
                   sizeof(crumpBit_descriptors[0]) == CRUMPBIT_PARAM_COUNT,
               "CRUMPBIT_PARAM_COUNT must match the descriptor table");
_Static_assert(CRUMPBIT_PARAM_ENUM_COUNT == CRUMPBIT_PARAM_COUNT,
               "crumpbit_param_t must match the descriptor table");

/* Three states per data line, shown as `-`, `0` and `!`. */
#define CRUMPBIT_BIT_NORMAL 0u
#define CRUMPBIT_BIT_OFF    1u
#define CRUMPBIT_BIT_INVERT 2u
#define CRUMPBIT_BIT_COUNT  8u

/*
 * Resolve one data line's state from a mask pair.
 *
 * Input: the shown bit-off and bit-invert masks and one bit. Output:
 * CRUMPBIT_BIT_INVERT if the invert bit is set (invert wins, as in the DSP),
 * else CRUMPBIT_BIT_OFF if the off bit is set, else CRUMPBIT_BIT_NORMAL.
 * Why: the SELECT cycle, the top row and the LEDs must read a stale off+invert
 * pair exactly as the DSP plays it. Callers: the three hooks below.
 */
static uint8_t crumpBit_bitState(uint8_t off, uint8_t invert, uint8_t bit)
{
    if ((invert & bit) != 0u)
        return CRUMPBIT_BIT_INVERT;
    if ((off & bit) != 0u)
        return CRUMPBIT_BIT_OFF;
    return CRUMPBIT_BIT_NORMAL;
}

/*
 * SELECT hook: cycle one data line (S074 Q13, Q14, F3).
 *
 * What: SELECT n (button n-1 = bit n-1, bit 0 = LSB) moves that bit
 * normal -> off -> invert -> normal. SHIFT+SELECT resets it to normal. The
 * new whole masks are written with menuEffects_editParam(): with no SEQ hold
 * they become the retained masks (fanned out to same-type masked Scenes,
 * AutoSave marked); during a hold both mask lanes are locked on every held
 * step with the same values, so all held steps end up equal.
 * Inputs: button 0..7, shift, pressed (the page only calls on press).
 * Output: EFFECT_UI_SHOW_HOME, so the page jumps to the overlay and
 * re-renders the LEDs; 0 for anything it does not handle. Accessor:
 * menuEffects_hookSelect() from buttonHandler's FX SELECT paths.
 * Affiliates: menuEffects_shownParam(), menuEffects_editParam(),
 * effects_setParameter(), effects_setSeqLaneLock().
 */
static uint8_t crumpBit_uiSelect(uint8_t button, uint8_t shift,
                                 uint8_t pressed)
{
    uint8_t off;
    uint8_t invert;
    uint8_t bit;
    uint8_t next;

    if (!pressed || button >= CRUMPBIT_BIT_COUNT)
        return 0u;
    off = menuEffects_shownParam(CRUMPBIT_PARAM_BIT_OFF);
    invert = menuEffects_shownParam(CRUMPBIT_PARAM_BIT_INVERT);
    bit = (uint8_t)(1u << button);
    if (shift) {
        next = CRUMPBIT_BIT_NORMAL;
    } else {
        switch (crumpBit_bitState(off, invert, bit)) {
        case CRUMPBIT_BIT_NORMAL: next = CRUMPBIT_BIT_OFF;    break;
        case CRUMPBIT_BIT_OFF:    next = CRUMPBIT_BIT_INVERT; break;
        default:                  next = CRUMPBIT_BIT_NORMAL; break;
        }
    }
    off = (uint8_t)((off & (uint8_t)~bit) |
                    ((next == CRUMPBIT_BIT_OFF) ? bit : 0u));
    invert = (uint8_t)((invert & (uint8_t)~bit) |
                       ((next == CRUMPBIT_BIT_INVERT) ? bit : 0u));
    (void)menuEffects_editParam(CRUMPBIT_PARAM_BIT_OFF, off);
    (void)menuEffects_editParam(CRUMPBIT_PARAM_BIT_INVERT, invert);
    return EFFECT_UI_SHOW_HOME;
}

/*
 * LED hook: the SELECT row shows the eight data lines.
 *
 * What: SELECT LED n is on when bit n-1 is off or inverted, and off when it
 * is normal. The source is menuEffects_shownParam(), so during a SEQ hold the
 * LEDs recover the last step held's locked masks, and otherwise the retained
 * masks. Why: user specification; the page's own active SELECT LED is
 * suppressed because this type sets EFFECT_UI_FLAG_OWNS_SELECT_LEDS.
 * Inputs: none. Output: eight led_setValue() calls. Accessors:
 * menuEffects_renderLeds(), menuEffects_renderSelectLeds(), and
 * menuEffects_service(). Affiliates: ledHandler.
 */
static void crumpBit_uiRenderLeds(void)
{
    const uint8_t used =
        (uint8_t)(menuEffects_shownParam(CRUMPBIT_PARAM_BIT_OFF) |
                  menuEffects_shownParam(CRUMPBIT_PARAM_BIT_INVERT));
    uint8_t i;

    for (i = 0u; i < CRUMPBIT_BIT_COUNT; i++)
        led_setValue((uint8_t)((used >> i) & 1u),
                     (uint8_t)(LED_PART_SELECT1 + i));
}

/*
 * Row-0 hook: paint the overlay's data-line row.
 *
 * What: writes `- - - - - - - -` into columns 0..14. Column 2n shows bit n
 * (LSB left): `-` normal, `0` off, `!` inverted; odd columns are spaces.
 * Column 15 is left alone, so the page's scroll marker (`>`) stays. The
 * source is menuEffects_shownParam() (the last step held's masks during a
 * hold). Inputs: sub-page, screen (unused: only one screen is flagged in
 * crumpBit_layout.custom_row0) and the 16-byte row. Output: row0[0..14].
 * Accessor: menuEffects_paintRow0() from the compact branch of
 * menu_repaintGeneric(). Affiliates: crumpBit_layout, menu.c.
 */
static void crumpBit_uiPaintRow0(uint8_t sub_page, uint8_t screen,
                                 char *row0)
{
    static const char glyph[3] = { '-', '0', '!' };
    const uint8_t off = menuEffects_shownParam(CRUMPBIT_PARAM_BIT_OFF);
    const uint8_t invert = menuEffects_shownParam(CRUMPBIT_PARAM_BIT_INVERT);
    uint8_t i;

    (void)sub_page;
    (void)screen;
    for (i = 0u; i < CRUMPBIT_BIT_COUNT; i++) {
        row0[2u * i] = glyph[crumpBit_bitState(off, invert,
                                               (uint8_t)(1u << i))];
        if (i + 1u < CRUMPBIT_BIT_COUNT)
            row0[2u * i + 1u] = ' ';
    }
}

/*
 * Format hook: CrumpBit value text on the Effect page (Q7).
 *
 * What: `sub` shows `dly`; `rte` shows the snapped step division when the
 * shown Sync is on. crumpBit_syncDivision() applies the same rule the DSP
 * uses, at the current tempo. Everything else returns 0 for generic dtype
 * formatting. Why: the raw 0..127 Rate stays underneath (pots, storage,
 * automation, locks, LFO), and only the Effect page shows the division.
 * Inputs: row index, value being shown, and a 3-character output. Output:
 * nonzero when out[0..2] was written. Affiliates: stepScale_shortName(),
 * seq_getBpm(), menuEffects_shownParam().
 */
static uint8_t crumpBit_uiFormatValue3(uint8_t index, uint8_t value,
                                       char *out)
{
    if (index == CRUMPBIT_PARAM_SUBTYPE) {
        memcpy(out, (value == CRUMPBIT_SUBTYPE_DELAY) ? "dly" : "---", 3u);
        return 1u;
    }
    if (index == CRUMPBIT_PARAM_RATE &&
        menuEffects_shownParam(CRUMPBIT_PARAM_SYNC) != 0u) {
        memcpy(out, stepScale_shortName(
                        crumpBit_syncDivision(value, seq_getBpm())), 3u);
        return 1u;
    }
    return 0u;
}

/*
 * CrumpBit Effect-page layout (S074_CRUMPBIT_EFFECT.md §5.1).
 *
 * What: SELECT 1 stays manager-owned. SELECT 2 holds three screens: home
 * overlay (mix/fbk/rte/mrp), page 2 (mix/fbk/rte/sub), and page 3 (snc/dpn).
 * Every other SELECT has zero screens because its buttons are bit toggles.
 * The encoder walks SELECT 1 s0 -> s1 -> overlay -> page 2 -> page 3.
 * The page-3 short labels are `snc` and `dpn` (S075 F2-E).
 * Inputs: none (const). Output: registry layout consumed by menuEffects.
 * Affiliates: EFFECT_LAYOUT_CELL_MORPH, crumpBit_uiPaintRow0().
 */
const effect_select_layout_t crumpBit_layout = {
    .screen_count = { 0u, 3u, 0u, 0u, 0u, 0u, 0u, 0u },
    .cells = {
        [1] = {
            { CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK,
              CRUMPBIT_PARAM_RATE, EFFECT_LAYOUT_CELL_MORPH },
            { CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK,
              CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SUBTYPE },
            { CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_DLY_PAN,
              EFFECT_LANE_NONE, EFFECT_LANE_NONE },
            { EFFECT_LANE_NONE, EFFECT_LANE_NONE,
              EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        },
    },
    .custom_row0 = { 0u, 0x01u, 0u, 0u, 0u, 0u, 0u, 0u },
    .home_sub_page = 1u,
    .home_screen = 0u,
};

/*
 * CrumpBit page hooks.
 *
 * What: SELECT toggles bits and returns EFFECT_UI_SHOW_HOME, render_leds owns
 * the SELECT row, paint_row0 draws the overlay row, and format_value3 labels
 * `sub` and Sync. TRACK and BAR keep the default page behaviour (NULL).
 * Why: the registry-driven hook contract keeps type UI out of menu.c.
 * Accessors: the registry row and menuEffects hook dispatchers. Affiliates:
 * buttonHandler FX SELECT paths.
 */
const effect_ui_hooks_t crumpBit_ui = {
    .select = crumpBit_uiSelect,
    .track = NULL,
    .bar = NULL,
    .render_leds = crumpBit_uiRenderLeds,
    .paint_row0 = crumpBit_uiPaintRow0,
    .format_value3 = crumpBit_uiFormatValue3,
    .flags = EFFECT_UI_FLAG_OWNS_SELECT_LEDS,
};
