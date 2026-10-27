/*
 * Host stand-in for the CMSIS intrinsics header (S085 C17.1).
 *
 * What: portable __SSAT/__QADD16/__CLZ for the S085 host tests. __SSAT is
 * normally provided by prelude_osc.h; this header fills the other two and
 * avoids the ARM assembly in Core/compat.
 * Why: globals.h includes this header directly, and the host cannot assemble
 * the ARM instructions.
 * Inputs: integers. Outputs: the intrinsic results.
 * Accessors: the S085 host tests. Affiliates: hostcompat/stm32f4xx.h.
 */
#ifndef HOST_CMSIS_INTRINSICS_H
#define HOST_CMSIS_INTRINSICS_H
#include <stdint.h>

static inline int32_t host_cmsis_qadd16(int32_t a, int32_t b)
{
    return a + b;
}

static inline uint32_t host_cmsis_clz(uint32_t v)
{
    uint32_t n = 0u;

    if (v == 0u)
        return 32u;
    while ((v & 0x80000000u) == 0u) {
        v <<= 1;
        n++;
    }
    return n;
}

#define __QADD16(a, b) host_cmsis_qadd16((a), (b))
#define __CLZ(v) host_cmsis_clz((uint32_t)(v))

#endif
