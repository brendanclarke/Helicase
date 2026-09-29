/*
 * ARM codegen check and instruction count for the one-pass mixer (S073 Step 5).
 *
 * What:       compiles the frozen dry and send functions and the new combined
 *             function with the firmware's DSP flags, so fpseq.py can confirm
 *             the VFP/saturation sequence and report loop instruction counts.
 * Why:        host bit identity must be accompanied by matching target code
 *             generation for the S0 mixer refactor.
 * Inputs:     generated ARM fragments from the Makefile target.
 * Outputs:    build/ac_mixer.o, consumed by fpseq.py.
 * Accessors:  make -C tools/dsp_golden armcheck-mixer.
 * Affiliates: mixer.c, BufferTools.h and fpseq.py.
 */
#include "config.h"
#include "BufferTools.h"

/* Routing values mirrored from mixer.h; the harness does not need its full
** include tree. */
enum { MIXER_ROUTING_DAC1_STEREO = 0, MIXER_ROUTING_DAC2_STEREO,
       MIXER_ROUTING_DAC1_L, MIXER_ROUTING_DAC1_R,
       MIXER_ROUTING_DAC2_L, MIXER_ROUTING_DAC2_R };

typedef union {
    sample_mx_t mx[2][OUTPUT_DMA_SIZE];
    float f[2][OUTPUT_DMA_SIZE];
} mixer_fx_bus_t;

static mixer_fx_bus_t old_bus;
static mixer_fx_bus_t new_bus;

#include "mx_old_arm.c"
#include "mx_new_arm.c"
