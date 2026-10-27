/*
 * Host stand-in for the CMSIS device header (S085 C17.1).
 *
 * What: the few ARM intrinsics the oscillator sources use, as portable host
 * code: APSR read and the C-flag branch in calcNoise().
 * Why: Core/compat/cmsis_intrinsics.h contains ARM assembly, so the host
 * harness cannot include it; this header shadows stm32f4xx.h from the test
 * include path only.
 * Inputs: none. Outputs: APSR_Type and __get_APSR().
 * Accessors: the S085 host tests. Affiliates: prelude_osc.h, __SSAT there.
 */
#ifndef HOST_STM32F4XX_H
#define HOST_STM32F4XX_H
#include <stdint.h>

typedef union {
    struct {
        uint32_t _reserved0:28;
        uint32_t V:1;
        uint32_t C:1;
        uint32_t Z:1;
        uint32_t N:1;
    } b;
    uint32_t w;
} APSR_Type;

static inline uint32_t __get_APSR(void)
{
    return 0u;
}

#endif
