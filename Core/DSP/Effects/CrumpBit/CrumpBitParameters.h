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

/* Descriptor rows: common rows 0..2 plus type rows 3..10. */
#define CRUMPBIT_PARAM_COUNT 11u

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
    CRUMPBIT_PARAM_ENUM_COUNT
} crumpbit_param_t;

/* Sub-effect behind Mix/Feedback/Rate; only the delay exists in v1. */
#define CRUMPBIT_SUBTYPE_DELAY 0u

extern const effect_param_descriptor_t crumpBit_descriptors[];
extern const char crumpBit_token3[];
extern const char crumpBit_abbrev5[];
extern const char crumpBit_full8[];

/*
 * Effect-page layout and hooks.
 *
 * crumpBit_layout places the overlay (home), page 2 and page 3 on SELECT 2.
 * crumpBit_ui owns the SELECT buttons (bit toggles), the SELECT LEDs (bit
 * states), the overlay's top row and the `sub`/Sync value text.
 */
extern const effect_select_layout_t crumpBit_layout;
extern const effect_ui_hooks_t crumpBit_ui;

#endif /* CRUMP_BIT_PARAMETERS_H_ */
