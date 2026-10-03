/*
 * Core/DSP/Effects/StereoFilter/StereoFilterParameters.c
 *
 *  Created on: 27.09.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#include "StereoFilterParameters.h"
#include "menu.h"
#include "MenuText.h"
#include "EffectParamRows.h"

/*
 * StereoFilter descriptor source of truth (plan §6, Session 072 step 4).
 *
 * The filter rows use the same keys, display dtypes, and 0..127 value domain
 * as the voice filter rows. Frequency, resonance, and drive are Morphable,
 * LFO-modulatable, and automatable. Type is Morphable/automatable but not an
 * LFO destination. Audio out is step-automatable only. The frequency default
 * is 64, so a freshly selected `flt` is audibly filtered at mid cutoff.
 */
const char stereoFilter_token3[] = "flt";
const char stereoFilter_abbrev5[] = "StFlt";
const char stereoFilter_full8[] = "StFilter";

const effect_param_descriptor_t stereoFilter_descriptors[] = {
    EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                       EFFECT_FLAGS_IMAGE,
                       EFFECT_FLAGS_IMAGE),
    EFFECT_ROW("filter_freq", "Filter", "Frequncy", "frq", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("filter_reso", "Filter", "Resnance", "res", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("filter_drive", "Filter", "Overdriv", "drv", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW_MENU("filter_type", "Filter", "Type", "typ", MENU_FILTER,
                    INSTRUMENT_PARAM_FLAG_MORPHABLE |
                    INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                    EFFECT_MOD_NONE, 0u, 7u),
};

_Static_assert(sizeof(stereoFilter_descriptors) /
                   sizeof(stereoFilter_descriptors[0]) ==
                   STEREO_FILTER_PARAM_COUNT,
               "STEREO_FILTER_PARAM_COUNT must match the descriptor table");
_Static_assert(STEREO_FILTER_PARAM_ENUM_COUNT == STEREO_FILTER_PARAM_COUNT,
               "stereo_filter_param_t must match the descriptor table");
