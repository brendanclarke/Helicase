/*
 * Core/DSP/Effects/StereoFilter/StereoFilterParameters.h
 *
 *  Created on: 27.09.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef STEREO_FILTER_PARAMETERS_H_
#define STEREO_FILTER_PARAMETERS_H_

#include "EffectsManager.h"

/*
 * StereoFilter registry exports (Session 072 step 4; plan §6).
 *
 * EffectsManager owns the registry row; this header exports only the immutable
 * descriptor table and its display tokens so the type implementation remains
 * independent of manager state. The first three rows are the shared common
 * Effect rows.
 */
#define STEREO_FILTER_PARAM_COUNT 7u

extern const effect_param_descriptor_t stereoFilter_descriptors[];
extern const char stereoFilter_token3[];
extern const char stereoFilter_abbrev5[];
extern const char stereoFilter_full8[];

/* Local descriptor indices are stable file/automation identities. */
typedef enum {
    STEREO_FILTER_PARAM_AUDIO_OUT = EFFECT_COMMON_PARAM_AUDIO_OUT,
    STEREO_FILTER_PARAM_LEVEL     = EFFECT_COMMON_PARAM_LEVEL,
    STEREO_FILTER_PARAM_PAN       = EFFECT_COMMON_PARAM_PAN,
    STEREO_FILTER_PARAM_FREQ      = 3,
    STEREO_FILTER_PARAM_RESO,
    STEREO_FILTER_PARAM_DRIVE,
    STEREO_FILTER_PARAM_TYPE,
    STEREO_FILTER_PARAM_ENUM_COUNT
} stereo_filter_param_t;

#endif /* STEREO_FILTER_PARAMETERS_H_ */
