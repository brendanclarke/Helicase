/*
 * Core/DSP/Effects/EffectParamRows.h
 *
 *  Created on: 27.09.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef EFFECT_PARAM_ROWS_H_
#define EFFECT_PARAM_ROWS_H_

#include "EffectsManager.h"

/*
 * Descriptor-row builders shared by Effect parameter tables.
 *
 * What: keeps the ParamDescriptor field order and the three common rows in
 * one source of truth. Why: EffectsManager.h must not depend on Menu headers,
 * while each descriptor implementation needs the existing Menu dtype/table
 * constants. Include this header only after menu.h and MenuText.h in a table
 * implementation. Affiliates: EffectTypes.h and InstrumentManager.h.
 */

#define EFFECT_BIND_NONE  { INSTRUMENT_BIND_NONE, 0u, 0u }
#define EFFECT_MOD_NONE   { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }
#define EFFECT_MOD_0_127  { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }

#define EFFECT_FLAGS_IMAGE \
    (INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_MODULATABLE | \
     INSTRUMENT_PARAM_FLAG_AUTOMATABLE)

/* Generic row: key, category, long label (<=8), short label (<=3), dtype,
 * ParamDescriptor flags/domain, type-change default, and inclusive maximum. */
#define EFFECT_ROW(key_, cat_, long_, short_, dtype_, flags_, mod_, def_, max_) \
    { { key_, short_, long_, cat_, (uint8_t)(dtype_), (uint8_t)(flags_), mod_, \
        EFFECT_BIND_NONE }, (uint8_t)(def_), (uint8_t)(max_), 0u, 0 }

/* Menu dtype row; DTYPE_MENU stores the table id in its high nibble. */
#define EFFECT_ROW_MENU(key_, cat_, long_, short_, menu_, flags_, mod_, def_, max_) \
    { { key_, short_, long_, cat_, \
        (uint8_t)(DTYPE_MENU | ((menu_) << 4)), (uint8_t)(flags_), mod_, \
        EFFECT_BIND_NONE }, (uint8_t)(def_), (uint8_t)(max_), 0u, 0 }

/*
 * Shared common rows at indices 0..2.
 *
 * out is automatable only; vol and pan use the same Morph/modulation/automation
 * image contract as voice parameters. Defaults are the EffectTypes.h values,
 * so SceneData defaults and registry defaults cannot diverge silently.
 */
#define EFFECT_COMMON_ROWS(out_flags_, vol_flags_, pan_flags_) \
    EFFECT_ROW_MENU("effect_audio_out", "Effect", "AudioOut", "out", \
                    MENU_AUDIO_OUT, out_flags_, EFFECT_MOD_NONE, \
                    EFFECT_COMMON_DEFAULT_AUDIO_OUT, 5u), \
    EFFECT_ROW("effect_level", "Effect", "Level", "vol", DTYPE_0B127, \
               vol_flags_, EFFECT_MOD_0_127, EFFECT_COMMON_DEFAULT_LEVEL, 127u), \
    EFFECT_ROW("effect_pan", "Effect", "Panning", "pan", DTYPE_PM63, \
               pan_flags_, EFFECT_MOD_0_127, EFFECT_COMMON_DEFAULT_PAN, 127u)

#endif /* EFFECT_PARAM_ROWS_H_ */
