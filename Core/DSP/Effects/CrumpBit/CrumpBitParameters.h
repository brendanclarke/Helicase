/*
 * Core/DSP/Effects/CrumpBit/CrumpBitParameters.h
 *
 *  Created on: 29.09.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */

#ifndef CRUMP_BIT_PARAMETERS_H_
#define CRUMP_BIT_PARAMETERS_H_

#include "EffectsManager.h"

/*
 * CrumpBit (`cbt`) registry exports (Session 074; S074_CRUMPBIT_EFFECT.md).
 *
 * What: the immutable descriptor table, display tokens, Effect-page layout
 * and page hooks of the CrumpBit Effect type: a bipolar 8-bit ADC whose
 * eight data lines can be forced off or inverted, an 8-bit DAC with AC
 * coupling, and a mono 8-bit tape-style delay in the FxBuffer Effect share.
 * Why a separate file: EffectsManager owns the registry row but not a type's
 * control tables. This side is ordinary -O2 control and page code;
 * CrumpBitEffect.c is the -Ofast DSP.
 * Inputs/outputs: compile-time tables, plus page hooks that run in foreground
 * on the Effect page and write retained data only through
 * menuEffects_editParam(), and so through the EffectsManager edit API.
 * Accessors: EffectsManager.c (registry row), menuEffects.c (layout, hooks).
 * Affiliates: CrumpBitEffect.c/.h (DSP, shared Rate/Sync helpers),
 * EffectParamRows.h, StepScale.h, ledHandler.h.
 */

/*
 * Descriptor rows: common rows 0..2 plus type rows 3..22.
 * S091 P2 appended rows 11..14 (`ton`, `typ`, `frq`, `res`). S091 P3
 * appended row 15 (`aux`, the per-sub-type pitch/drive row) and the granular
 * rows 16..21. S091 P4 (S091_P4_GRN_TWEAKS.md): row 20 is the grain Length
 * (`crump_grain_length`; the retired level jitter `amp` held this index),
 * and row 22 is the shared output-filter balance `bal`. The deferred
 * output-filter drive (`drv`) would append as 23; the tone drive (`tdr`) is
 * the `aux` row's meaning in `dly`/`cmb` when the drives land.
 */
#define CRUMPBIT_PARAM_COUNT 23u

/*
 * Local descriptor indices.
 *
 * These are permanent identities: they are the Effect-local numbers used by
 * FX lanes, Pattern automation targets (block 7, 448 + index) and LFO
 * tokens, and each row's file key is stored in `.fx` and AutoSave. Append
 * only; never renumber.
 */
typedef enum {
    CRUMPBIT_PARAM_AUDIO_OUT  = EFFECT_COMMON_PARAM_AUDIO_OUT,
    CRUMPBIT_PARAM_LEVEL      = EFFECT_COMMON_PARAM_LEVEL,
    CRUMPBIT_PARAM_PAN        = EFFECT_COMMON_PARAM_PAN,
    CRUMPBIT_PARAM_BIT_OFF    = 3,
    CRUMPBIT_PARAM_BIT_INVERT,
    CRUMPBIT_PARAM_MIX,
    CRUMPBIT_PARAM_FEEDBACK,
    CRUMPBIT_PARAM_RATE,
    CRUMPBIT_PARAM_SUBTYPE,
    CRUMPBIT_PARAM_SYNC,
    CRUMPBIT_PARAM_DLY_PAN,
    /* S091 P2, delay sub-type: feedback-return tone tilt (63 = flat). */
    CRUMPBIT_PARAM_TONE,
    /* S091 P2, shared output stage: the last element of the Effect. */
    CRUMPBIT_PARAM_FILTER_TYPE,
    CRUMPBIT_PARAM_FILTER_FREQ,
    CRUMPBIT_PARAM_FILTER_RESO,
    /* S091 P3: per-sub-type `aux` (pit interval, grn root `st1`). */
    CRUMPBIT_PARAM_AUX,
    /* S091 P3, `grn` only (S091 P4 meanings): pitches P2 and P3 (absolute),
     * spread, density (bipolar: trigger rate), Length (bipolar: size and
     * direction; index 20 was the retired level jitter) and width. */
    CRUMPBIT_PARAM_GRAIN_ST2,
    CRUMPBIT_PARAM_GRAIN_ST3,
    CRUMPBIT_PARAM_GRAIN_SPREAD,
    CRUMPBIT_PARAM_GRAIN_DENSITY,
    CRUMPBIT_PARAM_GRAIN_LENGTH,
    CRUMPBIT_PARAM_GRAIN_WIDTH,
    /* S091 P4, shared output stage: the filter's stereo balance. */
    CRUMPBIT_PARAM_FILTER_BAL,
    CRUMPBIT_PARAM_ENUM_COUNT
} crumpbit_param_t;

/*
 * Sub-engines (the `sub` row, index 8; S091 P3 §2.3).
 *
 * What: the stored values of `crump_subtype`. 0 is the S074 delay, so saved
 * Scenes keep playing `dly`. The row is Pattern-automatable (not Morph, not
 * LFO, never on an FX lane); a Pattern value above the last id is clamped to
 * it by the manager (user OQ3: the ids are not spread over 0..127).
 * Accessors: crumpBit_descriptors (max), crumpBit_process() dispatch,
 * crumpBit_enter(), the page hooks (names, layouts, values).
 */
#define CRUMPBIT_SUBTYPE_DELAY    0u   /* `dly` tape delay (engine A) */
#define CRUMPBIT_SUBTYPE_COMB     1u   /* `cmb` comb / resonator (engine A) */
#define CRUMPBIT_SUBTYPE_PITCH    2u   /* `pit` pitch shifter (engine B) */
#define CRUMPBIT_SUBTYPE_GRAIN    3u   /* `grn` granular + delay (engine B') */
#define CRUMPBIT_SUBTYPE_REVERB   4u   /* `rvb` 4-line FDN reverb (engine C) */
#define CRUMPBIT_SUBTYPE_COUNT    5u

/*
 * Row ownership by stage (S091 P2 §4; the single record of which rows belong
 * to which CrumpBit stage).
 *
 * What: one 32-bit mask per owner, bit n = descriptor index n.
 * - CRUMPBIT_ROWS_SHARED: the stages every sub-type runs: the ADC data-line
 *   masks (`bof`, `biv`), the sub-type selector itself (`sub`), the MIX
 *   STAGE (`mix`, S091 P3 §2.8: always dry <-> the sub-engine's wet, never a
 *   sub-type row) and the output filter (`typ`, `frq`, `res`, and since
 *   S091 P4 its balance `bal`), which is the last element of the Effect
 *   whatever the sub-type is.
 * - CRUMPBIT_ROWS_SUBTYPE: rows whose meaning is set by the active sub-type
 *   (`fbk`, `rte`, `snc`, `dpn`, `ton`, `aux`), aligned by role across
 *   sub-types (plan §6, A1): `fbk` amount back, `rte` length (higher =
 *   longer), `snc` the switch, `dpn` where the wet sits, `ton` colour/shape,
 *   `aux` pitch. A sub-type change keeps their values (plan §2.4).
 * - CRUMPBIT_ROWS_GRN: rows only `grn` reads (`st2 st3 spr den len wid`;
 *   S091 P4: `len` replaced the retired `amp`).
 * Why: a new sub-type adds its own mask here; the _Static_asserts in
 * CrumpBitParameters.c prove the masks are disjoint and cover every type
 * row 3..COUNT-1, so a row appended without an owner fails the build. The
 * common rows 0..2 belong to the manager and are in no mask.
 * Accessors: CrumpBitParameters.c (guards), tools/dsp_test crumpbit (CB9).
 * Affiliates: crumpbit_param_t, CRUMPBIT_SUBTYPE_*, crumpBit_process().
 */
#define CRUMPBIT_ROW_BIT(index_) (1UL << (uint32_t)(index_))
#define CRUMPBIT_ROWS_SHARED                                  \
    (CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_BIT_OFF) |               \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_BIT_INVERT) |            \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_MIX) |                   \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_SUBTYPE) |               \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_TYPE) |           \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_FREQ) |           \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_RESO) |           \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_BAL))
#define CRUMPBIT_ROWS_SUBTYPE                                 \
    (CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FEEDBACK) |              \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_RATE) |                  \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_SYNC) |                  \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_DLY_PAN) |               \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_TONE) |                  \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_AUX))
#define CRUMPBIT_ROWS_GRN                                     \
    (CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_GRAIN_ST2) |             \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_GRAIN_ST3) |             \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_GRAIN_SPREAD) |          \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_GRAIN_DENSITY) |         \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_GRAIN_LENGTH) |          \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_GRAIN_WIDTH))

/*
 * Tone tilt centre (S091 P2 §5.3): stored 63 displays 0 through DTYPE_PM63
 * and selects the flat table entry (crumpBit_toneK[63] == 1.0).
 * Accessors: crumpBit_descriptors (default), crumpBit_process() (side select).
 */
#define CRUMPBIT_TONE_FLAT 63u

/*
 * Output filter types (S091 P2 §5.4), stored by the `typ` row 0..7.
 *
 * What: the MENU_FILTER display order (MenuText.h filterTypes: LP HP BP UBP
 * Nch Pek LP2 off), so the row shows and stores exactly what a voice filter
 * type does (user U1). Each value is ResonantFilter's FILTER_* id minus 1,
 * the same offset StereoFilter applies (`svf_type = value + 1`).
 * CRUMPBIT_FILTER_LP2 is rendered as LP: the voice LP2 is a different naive
 * 2-pole structure that would break constant CPU here (plan Q9).
 * CRUMPBIT_FILTER_OFF passes the input exactly once its one-block coefficient
 * ramp has settled; the filter still runs (constant CPU, warm state).
 * S091 P5 F1: in `grn` and `rvb` the filter is a one-pole and the eight
 * values map onto three: LP, LP2, BP, UBP low-pass; HP, notch high-pass;
 * peak, off flat (the page shows LP/HP/off there).
 * Accessors: crumpBit_descriptors (default/max), crumpBit_filterMix(),
 * crumpBit_onePoleMixTable, crumpBit_uiFormatValue3().
 * Affiliates: MENU_FILTER, StereoFilterEffect.c stereoFilter_writeParam().
 */
#define CRUMPBIT_FILTER_LP    0u
#define CRUMPBIT_FILTER_HP    1u
#define CRUMPBIT_FILTER_BP    2u
#define CRUMPBIT_FILTER_UBP   3u
#define CRUMPBIT_FILTER_NOTCH 4u
#define CRUMPBIT_FILTER_PEAK  5u
#define CRUMPBIT_FILTER_LP2   6u
#define CRUMPBIT_FILTER_OFF   7u

extern const effect_param_descriptor_t crumpBit_descriptors[];
extern const char crumpBit_token3[];
extern const char crumpBit_abbrev5[];
extern const char crumpBit_full8[];

/*
 * Effect-page layout and hooks.
 *
 * crumpBit_layout places the overlay (home) and pages 2..4 on SELECT 2
 * (S091 P2: four screens, the layout maximum); S091 P3 adds the `pit` and
 * `grn` variants, chosen by crumpBit_ui.layout. S091 P4 (user): the overlay
 * is `mrp frq res bal`, then `typ frq res bal`, then `mix sub` and the
 * sub-effect's controls (CrumpBitParameters.c). crumpBit_ui owns the SELECT
 * buttons (bit toggles), the SELECT LEDs (bit states), the overlay's top row
 * and the `sub`/Rate value text (the Rate shows the share-clamped value and,
 * with Sync on, the division the DSP plays).
 */
extern const effect_select_layout_t crumpBit_layout;

extern const effect_ui_hooks_t crumpBit_ui;

#endif /* CRUMP_BIT_PARAMETERS_H_ */
