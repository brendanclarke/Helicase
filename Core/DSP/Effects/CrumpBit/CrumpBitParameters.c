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
#include "FxBuffer.h"   /* fxbuf_effectShare(): the Rate label clamp (S091 P2) */
#include <math.h>      /* powf(): the `pit` window label (S091 P3) */
#include "CrumpBitTables.h" /* crumpBit_grainStopCc: `grn` labels (S091 P4) */

/* Persisted token (permanent), UI abbreviation (5) and full name (<= 8). */
const char crumpBit_token3[] = "cbt";
const char crumpBit_abbrev5[] = "CrmBt";
const char crumpBit_full8[] = "CrumpBit";

/*
 * CrumpBit descriptor table (S074_CRUMPBIT_EFFECT.md §4).
 *
 * What: rows 0..2 are the shared common rows; rows 3..22 are CrumpBit's.
 * - bit off / bit invert (3, 4): 8-bit masks, one bit per ADC data line
 *   (bit 0 = LSB). Not Morphable (interpolating a mask gives unrelated
 *   bits), not modulatable, not Pattern-automatable (a Pattern value is
 *   7-bit and cannot reach bit 7). Sequenceable through FX lanes 1 and 2,
 *   which store full 8-bit values. Edited by the SELECT buttons (Stage 2);
 *   in Stage 1 they show as plain 0..255 numbers.
 * - mix, feedback, rate, delay pan (5, 6, 7, 10): Morphable, LFO and
 *   Pattern automation, like voice rows. Delay pan (10) is Position in
 *   `grn`, whose rhythmic lags must fit the line: S091 P4 clamped it in the
 *   manager (BUFFER_DEPENDENT); S091 P5 A moved the clamp into the engine
 *   (crumpBit_grainPick()), so the row is a plain image row again and the
 *   manager's per-block Position search is gone. The page still shows the
 *   stop that plays (crumpBit_grainPosLimit()).
 * - rate (7) is BUFFER_DEPENDENT (S091 P2): a higher value is a longer
 *   delay, up to the whole arena, and EffectsManager clamps the resolved
 *   value to the live share through crumpBit_ops.effective_max; the page
 *   clamps edits the same way (effects_paramEffectiveMax()).
 * - sub-type (8, S091 P3): the sub-engine, 0 dly, 1 cmb, 2 pit, 3 grn,
 *   4 rvb (max 4). Pattern-automatable (user S7: switches per step, with the
 *   buffer reinterpreted; glitches wanted), not Morph, not LFO, never on an
 *   FX lane (user OQ2); a Pattern value above 4 clamps to `rvb` (OQ3). Its
 *   labels come from the format hooks (every DTYPE_MENU id is in use).
 * - sync (9): on/off; Pattern-automatable and sequenceable.
 * - tone (11, S091 P2, delay sub-type): bipolar tilt on the feedback return,
 *   DTYPE_PM63 so stored 63 is flat and shows 0; below 63 cuts highs, above
 *   63 cuts lows. Morphable, LFO and Pattern, like `dpn`.
 * - filter type / freq / reso (12..14, S091 P2, shared output stage): the
 *   voice-filter rows by name, label and value law (user U1): `typ` is the
 *   MENU_FILTER list (LP HP BP UBP Nch Pek LP2 off), Morphable and
 *   Pattern-automatable but not LFO-modulatable, exactly like StereoFilter's
 *   `typ` (user U2); `frq` and `res` are full image rows. Defaults match
 *   StereoFilter (frq 64, res 0) except `typ`, which starts `off` so a
 *   Scene's output is unfiltered until a type is chosen.
 * - aux (15, S091 P3): the per-sub-type pitch row, bipolar (63 = 0): the
 *   `pit` interval, the `grn` root `st1`; the tone drive in `dly`/`cmb`
 *   when the deferred drives land. Morph, LFO and Pattern.
 * - grain rows (16..21, `grn` only; S091 P4 meanings, S091_P4_GRN_TWEAKS.md
 *   Part A): `p2`, `p3` (rows 16, 17, keys `crump_grain_st2/st3`: absolute
 *   pitches, bipolar semitones, Q4), `spr` spread/detune, `den` Density
 *   (bipolar: 63 off, 64 +1 = one trigger per line, rhythmic above, random
 *   below; default 64), `lng` Length (row 20, bipolar: size, reversed left
 *   of 63; default 127 = the whole line forward; the retired level jitter
 *   `amp` held this index), `wid` Width (mono at 0). Defaults give the copy
 *   setting (A1.9). Morph, LFO and Pattern; lanes from the `grn` lane map
 *   (den 4, lng 10, spr 15).
 * - filter balance (22, S091 P4, shared output stage): `bal`, bipolar
 *   (63 = 0): the right channel's cutoff index is frq + (bal - 63), the
 *   left's frq - (bal - 63). Morph, LFO and Pattern, like `frq`; no lane.
 * - Defaults (S075 F2-E, user): mix 0, feedback 64, rate 64, delay pan 63
 *   (the absolute centre, shows 0). The sync row's 3-letter label is `snc`;
 *   its file key remains `crump_sync`.
 * Why: the registry makes storage, AutoSave, the Effect page, lanes,
 * automation and LFO work without type-specific code elsewhere. Inputs:
 * none (const). Output: 23 rows. Accessors: EffectsManager (resolution,
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
    /*
     * S091 P2: EFFECT_ROW_FLAGS adds EFFECT_PARAM_FLAG_BUFFER_DEPENDENT, so
     * the resolved Rate is clamped to the longest delay the live share holds.
     */
    EFFECT_ROW_FLAGS("crump_rate", "Delay", "Rate", "rte", DTYPE_0B127,
                     EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u,
                     EFFECT_PARAM_FLAG_BUFFER_DEPENDENT),
    /* S091 P3: five sub-engines; Pattern-automatable only (S7, OQ2). */
    EFFECT_ROW("crump_subtype", "CrumpBit", "SubType", "sub", DTYPE_0B127,
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE, EFFECT_MOD_NONE,
               CRUMPBIT_SUBTYPE_DELAY, CRUMPBIT_SUBTYPE_COUNT - 1u),
    EFFECT_ROW("crump_sync", "Delay", "Sync", "snc", DTYPE_ON_OFF,
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE, EFFECT_MOD_NONE, 0u, 1u),
    /* S091 P5 A: the `grn` Position clamp is the engine's; no flag. */
    EFFECT_ROW("crump_dly_pan", "Delay", "DlyPan", "dpn", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    /* S091 P2, delay sub-type: tone tilt on the feedback return. */
    EFFECT_ROW("crump_tone", "Delay", "Tone", "ton", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, CRUMPBIT_TONE_FLAT, 127u),
    /* S091 P2, output stage: voice-aligned filter rows (U1, U2). */
    EFFECT_ROW_MENU("crump_filter_type", "Filter", "Type", "typ", MENU_FILTER,
                    INSTRUMENT_PARAM_FLAG_MORPHABLE |
                    INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                    EFFECT_MOD_NONE, CRUMPBIT_FILTER_OFF, CRUMPBIT_FILTER_OFF),
    EFFECT_ROW("crump_filter_freq", "Filter", "Frequncy", "frq", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("crump_filter_reso", "Filter", "Resnance", "res", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    /* S091 P3: the per-sub-type pitch row (pit interval, grn root). */
    EFFECT_ROW("crump_aux", "CrumpBit", "Aux", "aux", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    /*
     * grn only (S091 P4): absolute pitches P2/P3, spread, bipolar Density,
     * bipolar Length (row 20, was the level jitter), width.
     */
    EFFECT_ROW("crump_grain_st2", "Grain", "Pitch2", "p2", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("crump_grain_st3", "Grain", "Pitch3", "p3", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("crump_grain_spread", "Grain", "Spread", "spr", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("crump_grain_density", "Grain", "Density", "den", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("crump_grain_length", "Grain", "Length", "lng", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 127u, 127u),
    EFFECT_ROW("crump_grain_width", "Grain", "Width", "wid", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    /* S091 P4, output stage: stereo balance of the filter cutoff. */
    EFFECT_ROW("crump_filter_bal", "Filter", "Balance", "bal", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
};

_Static_assert(sizeof(crumpBit_descriptors) /
                   sizeof(crumpBit_descriptors[0]) == CRUMPBIT_PARAM_COUNT,
               "CRUMPBIT_PARAM_COUNT must match the descriptor table");
_Static_assert(CRUMPBIT_PARAM_ENUM_COUNT == CRUMPBIT_PARAM_COUNT,
               "crumpbit_param_t must match the descriptor table");

/*
 * Row-ownership guards (S091 P2 §4; three owners since S091 P3). The masks
 * in CrumpBitParameters.h must not overlap, and together they must name
 * every type row 3..COUNT-1 and nothing else, so appending a row without
 * deciding its owner (shared, sub-type, or grn) stops the build.
 */
_Static_assert((CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROWS_SUBTYPE) == 0u &&
               (CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROWS_GRN) == 0u &&
               (CRUMPBIT_ROWS_SUBTYPE & CRUMPBIT_ROWS_GRN) == 0u,
               "a CrumpBit row cannot belong to two stages");
_Static_assert((CRUMPBIT_ROWS_SHARED | CRUMPBIT_ROWS_SUBTYPE |
                CRUMPBIT_ROWS_GRN) ==
               (CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_COUNT) -
                CRUMPBIT_ROW_BIT(EFFECT_COMMON_PARAM_COUNT)),
               "every CrumpBit type row needs exactly one owner");

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
 * Sub-engine tokens (S091 P3): the `sub` row's value text, indexed by the
 * stored id (CRUMPBIT_SUBTYPE_*). Accessors: crumpBit_uiFormatValue3(),
 * crumpBit_uiFormatTarget3().
 */
static const char crumpBit_subtypeNames[CRUMPBIT_SUBTYPE_COUNT][4] = {
    "dly", "cmb", "pit", "grn", "rvb"
};

/*
 * Per-sub-type names of the sub-type rows on the Effect page (plan §6,
 * S2): index 0 fbk, 1 rte, 2 snc, 3 dpn, 4 ton, 5 aux, per sub-type.
 * "" keeps the descriptor's own short name. Accessor: crumpBit_uiName3().
 */
static const char crumpBit_subtypeRowNames[CRUMPBIT_SUBTYPE_COUNT][6][4] = {
    [CRUMPBIT_SUBTYPE_DELAY]  = { "",    "",    "",    "",    "",    ""    },
    [CRUMPBIT_SUBTYPE_COMB]   = { "",    "not", "pol", "",    "",    ""    },
    [CRUMPBIT_SUBTYPE_PITCH]  = { "",    "win", "snp", "",    "",    "int" },
    /* S091 P4: delay time, freeze, Position, P1 (`ton` is not on its page). */
    [CRUMPBIT_SUBTYPE_GRAIN]  = { "",    "tim", "frz", "pos", "",    "p1"  },
    [CRUMPBIT_SUBTYPE_REVERB] = { "dcy", "siz", "frz", "wid", "dmp", ""    },
};

/*
 * `grn` rhythmic stop names (S091 P4, plan A1.6/A1.8): StepScale's 14 stops
 * (stepScale_shortName()) plus d/2 and one bar, indexed by stop k as
 * crumpBit_grainStopCc. Accessor: crumpBit_formatGrainStop().
 */
static const char crumpBit_grainStopNames[CRUMPBIT_GRAIN_STOPS][4] = {
    "/64", "32t", "/32", "16t", "d32", "/16", "8Tr", "d16",
    "/8 ", "4Tr", "d/8", "/4 ", "d/4", "/2 ", "d/2", "1br"
};

/*
 * The `grn` delay-time label of an `rte` position: the stop's name when the
 * position is a stop (the divisions are marked, Q8), else the position
 * number, as StepScale's controls show them.
 */
static void crumpBit_formatGrainTime(uint8_t cc, char *out)
{
    uint32_t k;

    for (k = 0u; k < CRUMPBIT_GRAIN_STOPS; k++) {
        if (crumpBit_grainStopCc[k] == cc) {
            memcpy(out, crumpBit_grainStopNames[k], 3u);
            return;
        }
    }
    numtostrpu(out, cc, ' ');
}

/* The shown sub-type (the page's view of the record), clamped. */
static uint8_t crumpBit_shownSubtype(void)
{
    const uint8_t sub = menuEffects_shownParam(CRUMPBIT_PARAM_SUBTYPE);

    return (sub < CRUMPBIT_SUBTYPE_COUNT) ? sub
                                          : (uint8_t)(CRUMPBIT_SUBTYPE_COUNT - 1u);
}

/* Signed semitones of a bipolar pitch row (63 = 0, clamped to +-24). */
static void crumpBit_formatSemitones(uint8_t value, char *out)
{
    int32_t s = (int32_t)value - 63;

    s = (s > 24) ? 24 : ((s < -24) ? -24 : s);
    numtostrps(out, (int8_t)s);
}

/*
 * Note name of a MIDI note, the menu's DTYPE_NOTE_NAME convention (octave =
 * note / 12; menu.c setNoteName() is file-static, so this is its twin).
 */
static void crumpBit_formatNote(uint8_t note, char *out)
{
    const uint8_t n = (uint8_t)(note % 12u);

    out[0] = (char)((n > 8u ? 60 : 67) + ((n + (n > 4u)) / 2u));
    out[1] = ((n < 5u && (n & 1u)) || (n > 5u && !(n & 1u))) ? '#' : ' ';
    out[2] = (char)('0' + note / 12u);
}

/*
 * Format hook: CrumpBit value text on the Effect page (Q7; S091 P2; per
 * sub-type since S091 P3).
 *
 * What:
 * - `sub` shows the sub-engine token (dly cmb pit grn rvb);
 * - `rte`, clamped first to the longest value the live share holds under the
 *   shown sub-type's law (crumpBit_subtypeRateLimit(), the clamp
 *   EffectsManager applies before the DSP): `dly` the step division with
 *   the shown Sync on (as S091 P2), else the clamped number; `cmb` the note
 *   it plays (a higher value is a lower note); `pit` the window in ms;
 *   `grn` the step division it always snaps to (one half line);
 * - `aux` in `pit`/`grn`, and `p2`/`p3` in `grn`: signed semitones;
 * - S091 P4 `grn`: `rte` is the delay-time curve (a stop's name, else its
 *   position); Position's rhythmic half the stop it plays (clamped);
 *   Density `off`, `buf` (+1) or its stop;
 * - `aux` where it has no meaning yet (`dly`, `cmb`, `rvb`): `---`;
 * - S091 P5, what the two cheaper paths play:
 *   - `typ` in `grn`/`rvb` (F1, Q5): the one-pole's mapped type, `LP`
 *     (LP, LP2, BP, UBP), `HP` (HP, notch) or `off` (peak, off); the stored
 *     value is untouched, so `dly`/`cmb`/`pit` still play it as stored;
 *   - `res` in `grn`/`rvb` (F1): `---` (the one-pole has no resonance);
 *   - `ton` (`dmp`) in `rvb` above 63 (R1, Q10): `  0`, as 63 shows (the
 *     damping is low-pass only; values above 63 play flat);
 *   - `fbk` in `grn` (G4, Q7): the echo gain in percent, 0..98
 *     (crumpBit_grainFeedbackGain() x 100, rounded).
 * Everything else returns 0 for generic dtype formatting.
 * Why: the stored raw byte stays underneath (pots, storage, automation,
 * locks, LFO); only what is played and shown follows the sub-type.
 * Inputs: row index, value shown, 3-character output. Output: nonzero when
 * out[0..2] was written. Foreground; the share read is a struct copy.
 * Affiliates: stepScale_formatShort(), seq_getBpm(), menuEffects_shownParam(),
 * fxbuf_effectShare(), crumpBit_subtypeRateLimit(), crumpBit_combNote().
 */
static uint8_t crumpBit_uiFormatValue3(uint8_t index, uint8_t value,
                                       char *out)
{
    const uint8_t sub = crumpBit_shownSubtype();

    if (index == CRUMPBIT_PARAM_SUBTYPE) {
        memcpy(out, crumpBit_subtypeNames[(value < CRUMPBIT_SUBTYPE_COUNT)
                                          ? value
                                          : CRUMPBIT_SUBTYPE_COUNT - 1u], 3u);
        return 1u;
    }
    if (index == CRUMPBIT_PARAM_RATE) {
        fx_share_t share;
        /* limit: the largest `rte` that fits this share for this engine. */
        uint8_t limit;
        /* shown: the Rate the DSP will play, never above the clamp. */
        uint8_t shown;

        fxbuf_effectShare(&share);
        limit = crumpBit_subtypeRateLimit(sub, &share);
        shown = (value > limit) ? limit : value;
        switch (sub) {
        case CRUMPBIT_SUBTYPE_COMB:
            crumpBit_formatNote(crumpBit_combNote(shown), out);
            return 1u;
        case CRUMPBIT_SUBTYPE_PITCH:
            /* Window in ms: 10 * 20^(rte/127), the table law. */
            numtostrpu(out, (uint8_t)(10.0f * powf(20.0f, (float)shown /
                                                         127.0f) + 0.5f), ' ');
            return 1u;
        case CRUMPBIT_SUBTYPE_GRAIN:
            /* S091 P4: the delay-time curve, its divisions marked. */
            crumpBit_formatGrainTime(shown, out);
            return 1u;
        case CRUMPBIT_SUBTYPE_REVERB:
            break;
        default:
            if (menuEffects_shownParam(CRUMPBIT_PARAM_SYNC) != 0u) {
                /* S078: nudge stops show their symbol, others the raw CC. */
                stepScale_formatShort(
                    crumpBit_syncDivision(shown, seq_getBpm(),
                                          crumpBit_shareMaxSamples(&share)),
                    out);
                return 1u;
            }
            break;
        }
        if (shown != value) {
            numtostrpu(out, shown, ' ');
            return 1u;
        }
        return 0u;
    }
    if (index == CRUMPBIT_PARAM_AUX) {
        if (sub == CRUMPBIT_SUBTYPE_PITCH || sub == CRUMPBIT_SUBTYPE_GRAIN) {
            crumpBit_formatSemitones(value, out);
            return 1u;
        }
        memcpy(out, "---", 3u);
        return 1u;
    }
    if (index == CRUMPBIT_PARAM_GRAIN_ST2 || index == CRUMPBIT_PARAM_GRAIN_ST3) {
        crumpBit_formatSemitones(value, out);
        return 1u;
    }
    /*
     * S091 P4, `grn` Position and Density: the rhythmic half names its stop
     * (Position after the live clamp, as the DSP plays it); Density 63 is
     * `off` and 64 (+1) `buf` (one trigger per line); the rest generic.
     */
    if (sub == CRUMPBIT_SUBTYPE_GRAIN && index == CRUMPBIT_PARAM_DLY_PAN &&
        value > 63u) {
        fx_share_t share;
        uint8_t limit;

        fxbuf_effectShare(&share);
        limit = crumpBit_grainPosLimit(&share);
        if (limit > 63u) {
            memcpy(out, crumpBit_grainStopNames[crumpBit_grainPosStop(
                            (value > limit) ? limit : value)], 3u);
            return 1u;
        }
        memcpy(out, "  0", 3u);
        return 1u;
    }
    if (index == CRUMPBIT_PARAM_GRAIN_DENSITY && value >= 63u) {
        if (value == 63u)
            memcpy(out, "off", 3u);
        else if (value == 64u)
            memcpy(out, "buf", 3u);
        else
            memcpy(out, crumpBit_grainStopNames[crumpBit_grainDenStop(value)],
                   3u);
        return 1u;
    }
    /*
     * S091 P5 F1: the one-pole sub-types show the type they play and no
     * resonance. The mapping is crumpBit_onePoleMixTable's (CrumpBitEffect.c):
     * the low-pass rows, then HP and notch as high-pass, the rest flat.
     */
    if (sub == CRUMPBIT_SUBTYPE_GRAIN || sub == CRUMPBIT_SUBTYPE_REVERB) {
        if (index == CRUMPBIT_PARAM_FILTER_TYPE) {
            const char *mapped = "off";

            if (value == CRUMPBIT_FILTER_LP || value == CRUMPBIT_FILTER_LP2 ||
                value == CRUMPBIT_FILTER_BP || value == CRUMPBIT_FILTER_UBP)
                mapped = "LP ";
            else if (value == CRUMPBIT_FILTER_HP ||
                     value == CRUMPBIT_FILTER_NOTCH)
                mapped = "HP ";
            memcpy(out, mapped, 3u);
            return 1u;
        }
        if (index == CRUMPBIT_PARAM_FILTER_RESO) {
            memcpy(out, "---", 3u);
            return 1u;
        }
    }
    /* S091 P5 R1: `rvb` damping above 63 plays flat, so it shows 63's 0. */
    if (sub == CRUMPBIT_SUBTYPE_REVERB && index == CRUMPBIT_PARAM_TONE &&
        value > CRUMPBIT_TONE_FLAT) {
        numtostrps(out, 0);
        return 1u;
    }
    /* S091 P5 G4: `grn` echo gain 0..0.98 as a percentage, 0..98. */
    if (sub == CRUMPBIT_SUBTYPE_GRAIN && index == CRUMPBIT_PARAM_FEEDBACK) {
        numtostrpu(out, (uint8_t)((value * 98u + 63u) / 127u), ' ');
        return 1u;
    }
    return 0u;
}

/*
 * Name hook: per-sub-type 3-letter names of the sub-type rows on the Effect
 * page only (S091 P3 S2; plan §6). Other screens keep the descriptor names.
 * Inputs: row index, 3-character output. Output: nonzero when written.
 * Accessor: menuEffects_shortName(). Affiliates: crumpBit_subtypeRowNames.
 */
static uint8_t crumpBit_uiName3(uint8_t index, char *out)
{
    static const uint8_t rows[6] = {
        CRUMPBIT_PARAM_FEEDBACK, CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SYNC,
        CRUMPBIT_PARAM_DLY_PAN, CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_AUX
    };
    const char (*names)[4] = crumpBit_subtypeRowNames[crumpBit_shownSubtype()];
    uint8_t i;

    for (i = 0u; i < 6u; i++) {
        if (rows[i] == index && names[i][0] != '\0') {
            memcpy(out, names[i], 3u);
            return 1u;
        }
    }
    return 0u;
}

/*
 * Target-value hook: the `sub` row's token on automation pages (S091 P3,
 * OQ5: the STEP automation page and Pattern value entry show dly cmb pit grn
 * rvb, not 0..4). Other rows keep generic text (their meaning needs the
 * Effect page's context). Accessor: effects_formatTargetValue3().
 */
static uint8_t crumpBit_uiFormatTarget3(uint8_t index, uint8_t value,
                                        char *out)
{
    if (index != CRUMPBIT_PARAM_SUBTYPE)
        return 0u;
    memcpy(out, crumpBit_subtypeNames[(value < CRUMPBIT_SUBTYPE_COUNT)
                                      ? value
                                      : CRUMPBIT_SUBTYPE_COUNT - 1u], 3u);
    return 1u;
}

/*
 * CrumpBit Effect-page layouts (S074 §5.1; S091 P2 §3; S091 P4 §A6 of
 * S091_P4_GRN_TWEAKS.md, user 2026-11-04).
 *
 * What: SELECT 1 stays manager-owned (`typ out vol pan`, `mrp run len scl`).
 * CrumpBit's pages follow, in encoder order:
 *   SELECT 2 s0 home overlay (bit row on top, values only): mrp frq res bal
 *   SELECT 2 s1 the output filter with its type:             typ frq res bal
 *   SELECT 2 s2 the mix stage, the sub-type, then the sub-effect's first
 *              two controls:                                 mix sub  x   y
 *   SELECT 2 s3 (and SELECT 3 for `grn`): the rest of the sub-effect's
 *              controls.
 * Rule (user): only sub-effect controls follow `sub`. `frq` and `res` sit on
 * s0 and s1; both cells edit the same rows. Every SELECT button stays a bit
 * toggle (crumpBit_uiSelect()); the encoder walks cells across screens and
 * SELECT groups, so SELECT 3's screens follow SELECT 2's last one.
 * - crumpBit_layout (`dly`, `cmb`, `rvb`, same rows; names per sub-type
 *   through crumpBit_uiName3()): s2 mix sub rte fbk; s3 snc dpn ton -.
 * - crumpBit_layoutPitch: s2 mix sub aux(int) rte(win); s3 fbk snc(snp)
 *   dpn ton.
 * - crumpBit_layoutGrain (user order: freeze, position, length, density,
 *   pitches, width, delay time, feedback): s2 mix sub snc(frz) dpn(pos);
 *   s3 lng den aux(p1) p2; SELECT 3 s0 p3 spr wid rte(tim); s1 fbk.
 * Inputs: none (const). Output: layouts consumed by menuEffects through
 * crumpBit_uiLayout(). Affiliates: EFFECT_LAYOUT_CELL_MORPH,
 * crumpBit_uiPaintRow0(), menuEffects_layout().
 */
#define CRUMPBIT_LAYOUT_FILTER_SCREENS                              \
    { EFFECT_LAYOUT_CELL_MORPH, CRUMPBIT_PARAM_FILTER_FREQ,         \
      CRUMPBIT_PARAM_FILTER_RESO, CRUMPBIT_PARAM_FILTER_BAL },      \
    { CRUMPBIT_PARAM_FILTER_TYPE, CRUMPBIT_PARAM_FILTER_FREQ,       \
      CRUMPBIT_PARAM_FILTER_RESO, CRUMPBIT_PARAM_FILTER_BAL }

const effect_select_layout_t crumpBit_layout = {
    .screen_count = { 0u, 4u, 0u, 0u, 0u, 0u, 0u, 0u },
    .cells = {
        [1] = {
            CRUMPBIT_LAYOUT_FILTER_SCREENS,
            { CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_SUBTYPE,
              CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_FEEDBACK },
            { CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_DLY_PAN,
              CRUMPBIT_PARAM_TONE, EFFECT_LANE_NONE },
        },
    },
    .custom_row0 = { 0u, 0x01u, 0u, 0u, 0u, 0u, 0u, 0u },
    .home_sub_page = 1u,
    .home_screen = 0u,
};

static const effect_select_layout_t crumpBit_layoutPitch = {
    .screen_count = { 0u, 4u, 0u, 0u, 0u, 0u, 0u, 0u },
    .cells = {
        [1] = {
            CRUMPBIT_LAYOUT_FILTER_SCREENS,
            { CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_SUBTYPE,
              CRUMPBIT_PARAM_AUX, CRUMPBIT_PARAM_RATE },
            { CRUMPBIT_PARAM_FEEDBACK, CRUMPBIT_PARAM_SYNC,
              CRUMPBIT_PARAM_DLY_PAN, CRUMPBIT_PARAM_TONE },
        },
    },
    .custom_row0 = { 0u, 0x01u, 0u, 0u, 0u, 0u, 0u, 0u },
    .home_sub_page = 1u,
    .home_screen = 0u,
};

static const effect_select_layout_t crumpBit_layoutGrain = {
    .screen_count = { 0u, 4u, 2u, 0u, 0u, 0u, 0u, 0u },
    .cells = {
        [1] = {
            CRUMPBIT_LAYOUT_FILTER_SCREENS,
            { CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_SUBTYPE,
              CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_DLY_PAN },
            { CRUMPBIT_PARAM_GRAIN_LENGTH, CRUMPBIT_PARAM_GRAIN_DENSITY,
              CRUMPBIT_PARAM_AUX, CRUMPBIT_PARAM_GRAIN_ST2 },
        },
        [2] = {
            { CRUMPBIT_PARAM_GRAIN_ST3, CRUMPBIT_PARAM_GRAIN_SPREAD,
              CRUMPBIT_PARAM_GRAIN_WIDTH, CRUMPBIT_PARAM_RATE },
            { CRUMPBIT_PARAM_FEEDBACK, EFFECT_LANE_NONE,
              EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        },
    },
    .custom_row0 = { 0u, 0x01u, 0u, 0u, 0u, 0u, 0u, 0u },
    .home_sub_page = 1u,
    .home_screen = 0u,
};

/*
 * Layout hook (S091 P3): the layout of the shown sub-type. NULL never: the
 * base layout serves `dly`, `cmb` and `rvb`. Accessor: menuEffects_layout().
 */
static const struct effect_select_layout *crumpBit_uiLayout(void)
{
    switch (crumpBit_shownSubtype()) {
    case CRUMPBIT_SUBTYPE_PITCH: return &crumpBit_layoutPitch;
    case CRUMPBIT_SUBTYPE_GRAIN: return &crumpBit_layoutGrain;
    default:                     return &crumpBit_layout;
    }
}

/*
 * CrumpBit page hooks.
 *
 * What: SELECT toggles bits and returns EFFECT_UI_SHOW_HOME, render_leds owns
 * the SELECT row, paint_row0 draws the overlay row, and format_value3 labels
 * `sub` and the per-sub-type values. S091 P3: name3 renames the sub-type
 * rows per sub-type, layout picks the sub-type's screens, format_target3
 * names `sub` on automation pages. TRACK and BAR keep the default page
 * behaviour (NULL).
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
    .name3 = crumpBit_uiName3,
    .layout = crumpBit_uiLayout,
    .format_target3 = crumpBit_uiFormatTarget3,
};
