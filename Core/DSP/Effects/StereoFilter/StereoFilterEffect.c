/*
 * Core/DSP/Effects/StereoFilter/StereoFilterEffect.c
 *
 *  Created on: 27.09.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#include "StereoFilterEffect.h"
#include "StereoFilterParameters.h"
#include "globals.h"
#include "valueShaper.h"

/*
 * Link the left channel's filter coefficients into the right channel.
 *
 * The stereo type has one linked cutoff/resonance/drive setting but two
 * independent filter state vectors. Only coefficient fields are copied; s1,
 * s2, a, b, and zi remain channel-local so the channels cannot contaminate
 * one another's history.
 */
static void stereoFilter_linkCoefficients(StereoFilterRuntime *rt)
{
    rt->right.f = rt->left.f;
    rt->right.g = rt->left.g;
    rt->right.q = rt->left.q;
    rt->right.drive = rt->left.drive;
}

/*
 * Initialize a newly live StereoFilter runtime.
 *
 * Inputs: manager-owned runtime storage and the FxBuffer handoff. Output:
 * reset left/right SVF states and a temporary LP type until the manager's
 * forced resolution writes the retained descriptor image. This type never
 * claims the shared arena, so the handoff is intentionally unused.
 */
static void stereoFilter_init(void *rt_void, const fxbuf_handoff_t *handoff)
{
    StereoFilterRuntime *rt = (StereoFilterRuntime *)rt_void;

    (void)handoff;
    SVF_init(&rt->left);
    SVF_init(&rt->right);
    SVF_reset(&rt->left);
    SVF_reset(&rt->right);
    rt->svf_type = FILTER_LP;
    stereoFilter_linkCoefficients(rt);
}

/*
 * Apply one resolved type-specific descriptor value.
 *
 * Inputs: descriptor index 3..6 and its clamped 0..127 value. Output: linked
 * SVF coefficients or the selected filter type. The value shaping and drive
 * conversion deliberately mirror InstrumentManager's voice-filter writer so
 * the Effect filter has the same control response as a voice filter.
 */
static void stereoFilter_writeParam(void *rt_void, uint8_t index,
                                    uint8_t value)
{
    StereoFilterRuntime *rt = (StereoFilterRuntime *)rt_void;

    switch (index) {
    case STEREO_FILTER_PARAM_FREQ:
        SVF_directSetFilterValue(&rt->left,
            valueShaperF2F(value / 127.0f, FILTER_SHAPER));
        break;
    case STEREO_FILTER_PARAM_RESO:
        SVF_setReso(&rt->left, value / 127.0f);
        break;
    case STEREO_FILTER_PARAM_DRIVE:
        SVF_setDrive(&rt->left, value);
        break;
    case STEREO_FILTER_PARAM_TYPE:
        rt->svf_type = (uint8_t)(value + 1u);
        return;
    default:
        return;
    }
    stereoFilter_linkCoefficients(rt);
}

/*
 * Process one normalized stereo block in place.
 *
 * Inputs: manager-owned runtime and an Effect bus block. Output: float ZDF
 * filtering without int16 input saturation; the later mixer return performs
 * the one final bus saturation. A missing right channel is accepted so the
 * operation remains safe when the Step 5 bus negotiates mono input.
 */
static void stereoFilter_process(void *rt_void, effect_io_t *io)
{
    StereoFilterRuntime *rt = (StereoFilterRuntime *)rt_void;

    if (!io || !io->l)
        return;
    SVF_calcBlockZDFFloat(&rt->left, rt->svf_type, io->l, io->frames);
    if (io->channels > 1u && io->r)
        SVF_calcBlockZDFFloat(&rt->right, rt->svf_type, io->r, io->frames);
}

/* This first type uses no FxBuffer memory and needs no share callbacks. */
const effect_type_ops_t stereoFilter_ops = {
    stereoFilter_init,
    NULL,
    stereoFilter_writeParam,
    stereoFilter_process,
    NULL,
    NULL,
};
