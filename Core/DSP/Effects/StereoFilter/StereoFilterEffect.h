/*
 * Core/DSP/Effects/StereoFilter/StereoFilterEffect.h
 *
 *  Created on: 27.09.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef STEREO_FILTER_EFFECT_H_
#define STEREO_FILTER_EFFECT_H_

#include "EffectsManager.h"
#include "ResonantFilter.h"

/*
 * StereoFilter runtime (Session 072 step 4; plan §6).
 *
 * Two ResonantFilter states share linked coefficients while retaining
 * separate state variables. The runtime lives in EffectsManager's DTCM union;
 * no other module may retain or mutate a pointer to it. This first type never
 * uses FxBuffer, so its handoff export and share callbacks are NULL.
 */
typedef struct {
    ResonantFilter left;
    ResonantFilter right;
    uint8_t svf_type;
} StereoFilterRuntime;

extern const effect_type_ops_t stereoFilter_ops;

#endif /* STEREO_FILTER_EFFECT_H_ */
