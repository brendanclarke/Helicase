/*
 * Compatibility shims for the frozen85 ARM probe (S085 C17.4).
 *
 * What:       re-declares the removed v1 sample entry points the pre-S085
 *             Oscillator.c still calls, and the retired OSC_SAMPLE_START.
 * Why:        the probe compiles the frozen source against today's headers to
 *             price the old loops; it is never linked into firmware.
 * Inputs:     none.  Outputs: declarations only.
 * Accessors:  make -C tools/dsp_test armcheck-osc-base.
 * Affiliates: frozen85/Core/DSPAudio/Oscillator.c.
 */
#ifndef OSB_BASE_COMPAT_H
#define OSB_BASE_COMPAT_H
#include <stdint.h>

#ifndef OSC_SAMPLE_START
#define OSC_SAMPLE_START 0x06
#endif
uint8_t sampleMemory_isLooped(uint8_t index);

#endif
