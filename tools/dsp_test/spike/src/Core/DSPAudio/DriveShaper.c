/*
 * DriveShaper.c — drive-type level trims and state reset (S084 VX2 VX2-7.2).
 *
 * What:       driveShaper_trim[] in dtype order, the linear gains printed by
 *             tools/plots/plot_waveshapers.py level_trims() (exported with
 *             its --emit-c option); driveShaper_reset() sets the script's
 *             initial state (ramp 1, period 200, both arm flags, the rest 0).
 * Why:        plan §4.4 level trims; one table shared by the filter and the
 *             host test.
 * Inputs:     none.  Outputs: the table; a reset state.
 * Accessors:  driveShaper_apply(); the engines' init; the dty writer.
 * Affiliates: tools/plots/plot_waveshapers.py.
 *
 *  Modified on: 25.10.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */
#include "DriveShaper.h"

const float driveShaper_trim[DRIVE_TYPE_COUNT] = {
    1.000000f, /* sft  +0.00 dB */  1.166908f, /* rct  +1.34 dB */
    1.259799f, /* art  +2.01 dB */  1.673173f, /* thf  +4.47 dB */
    1.369885f, /* fdc  +2.73 dB */  1.852455f, /* bsw  +5.35 dB */
    1.002718f, /* tsw  +0.02 dB */  1.002718f, /* lsw  +0.02 dB */
    0.752294f, /* lsq  -2.47 dB */  1.732458f, /* sfd  +4.77 dB */
    1.842502f, /* afd  +5.31 dB */  1.481852f, /* asf  +3.42 dB */
    1.469742f, /* chb  +3.34 dB */  1.000000f, /* rsb  +0.00 dB */
};

void driveShaper_reset(DriveShaperState *st)
{
    if (!st)
        return;
    st->ramp = 1.0f;
    st->env = 0.0f;
    st->curMax = 0.0f;
    st->dcX = 0.0f;
    st->dcY = 0.0f;
    st->since = 0u;
    st->period = 200u;
    st->flags = (uint8_t)(DRIVE_FLAG_ARMED | DRIVE_FLAG_NEG_ARMED);
    st->count = 0u;
    st->reserved = 0u;
}
