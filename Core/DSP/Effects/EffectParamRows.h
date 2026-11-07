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

/* S073 Step 2: Effect rows never use instrument special writers. */
#define EFFECT_BIND_NONE  { INSTRUMENT_BIND_NONE, 0u, 0u, IM_SPECIAL_NONE }
#define EFFECT_MOD_NONE   { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }
#define EFFECT_MOD_0_127  { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
/*
 * A continuous row confined to [lo_, hi_] (S091 P5 follow-up, CrumpBit's
 * +-36 st pitch rows). The domain's min_value is the row's inclusive
 * minimum everywhere a value is set or resolved
 * (effects_descriptorMin()), and hi_ must equal the row's max_value, so the
 * page, Pattern automation, FX lanes, LFO and the DSP all stop at the same
 * two ends. Every other row keeps min 0.
 */
#define EFFECT_MOD_RANGE(lo_, hi_) \
    { (lo_), (hi_), INSTRUMENT_MOD_DOMAIN_CONTINUOUS }

#define EFFECT_FLAGS_IMAGE \
    (INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_MODULATABLE | \
     INSTRUMENT_PARAM_FLAG_AUTOMATABLE)

/* Generic row: key, category, long label (<=8), short label (<=3), dtype,
 * ParamDescriptor flags/domain, type-change default, and inclusive maximum. */
#define EFFECT_ROW(key_, cat_, long_, short_, dtype_, flags_, mod_, def_, max_) \
    { { key_, short_, long_, cat_, (uint8_t)(dtype_), (uint8_t)(flags_), mod_, \
        EFFECT_BIND_NONE }, (uint8_t)(def_), (uint8_t)(max_), 0u, 0 }

/*
 * Generic row with Effect-only capability flags (S091 P2).
 *
 * What: EFFECT_ROW plus effect_flags_ (EFFECT_PARAM_FLAG_*), stored in
 * effect_param_descriptor_t.effect_flags. Why: EFFECT_ROW always writes 0
 * there, and CrumpBit's Rate is the first row that needs
 * EFFECT_PARAM_FLAG_BUFFER_DEPENDENT (its resolved value is clamped to the
 * live FxBuffer share through the type's effective_max op). expand7 stays 0:
 * a WIDE8 row would also need an expand function and is not built here.
 * Affiliates: EffectsManager.c effects_service() BUFFER_DEPENDENT clamp,
 * effects_paramEffectiveMax(), crumpBit_descriptors.
 */
#define EFFECT_ROW_FLAGS(key_, cat_, long_, short_, dtype_, flags_, mod_, def_, \
                         max_, effect_flags_) \
    { { key_, short_, long_, cat_, (uint8_t)(dtype_), (uint8_t)(flags_), mod_, \
        EFFECT_BIND_NONE }, (uint8_t)(def_), (uint8_t)(max_), \
        (uint8_t)(effect_flags_), 0 }

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
