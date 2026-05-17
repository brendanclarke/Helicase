/*
 * Core/compat/cmsis_intrinsics.h
 *
 *  Created on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR02 Open-Source, hardware driver code, or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions that are modified from the original source must include the complete
 *         source code, including the source code for all components used by a binary built
 *         from the modified sources. However, as a special exception, the source code distributed
 *         need not include anything that is normally distributed (in either source or binary form)
 *         with the major components (compiler, kernel, and so on) of the operating system on which
 *         the executable runs, unless that component itself accompanies the executable.
 *
 *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
 *         following disclaimer in the documentation and/or other materials provided with the distribution.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------------------------------------------------------
 */

/*
 * cmsis_intrinsics.h — minimal CMSIS-Core intrinsic shims for the LXR-02 port.
 *
 * The port keeps a hand-rolled register-access discipline (no full CMSIS
 * dependency for NVIC/SCB/etc.), but a handful of original LXR DSP files
 * call CMSIS-Core compiler intrinsics by name. Rather than pull in the
 * full CMSIS headers (~10K lines) for a few wrappers around inline
 * assembly, this header defines exactly what we need.
 *
 * Definitions are functionally identical to ARM's CMSIS_5/cmsis_gcc.h —
 * same instruction generated, same operand types, same return type.
 * Verified against:
 *   https://github.com/ARM-software/CMSIS_5/blob/develop/CMSIS/Core/Include/cmsis_gcc.h
 *
 * Cortex-M7 supports all of these instructions natively (it's an
 * ARMv7E-M part). No fallback paths needed.
 *
 * Pulled in via Core/compat/stm32f4xx.h so any DSP file ported from the
 * original mainboard (which includes "stm32f4xx.h") picks these up
 * transitively, matching the original's CMSIS-Core include chain.
 *
 * Intrinsics provided (full list of mainboard usage as of LXR 0.37):
 *   __QADD16  — saturating add of two pairs of int16 packed in uint32.
 *               Used by mixer.c, BufferTools.c, Snare.c, HiHat.c, CymbalVoice.c.
 *               Note on original LXR usage: code passes single int16s as
 *               uint32s and masks the result with & 0xFFFF, so only the
 *               lower 16-bit lane's saturation matters. Functionally
 *               correct, slightly wasteful. Cleanup deferred to enhancement.
 *   __QSUB16  — saturating sub, paired form. Used by BufferTools.c.
 *               Same single-lane usage pattern as __QADD16.
 *   __SSAT    — signed saturate to N bits. Used by ResonantFilter.c.
 *               Macro because the bit-position arg must be an immediate.
 *   __CLZ     — count leading zeros (32-bit). Used by Oscillator.c, EuklidGenerator.c.
 *               Maps to CLZ on ARMv7E-M — single-cycle, no software fallback.
 *   APSR_Type / __get_APSR
 *             — read the Application Program Status Register. Used by
 *               Oscillator.c calcNoise() to inspect the V (overflow) flag
 *               after a uint32 phase add, detecting phase-wrap for the
 *               noise oscillator.
 *
 * Extras included for forward-compatibility (zero cost — inline functions
 * are not emitted unless called):
 *   __QADD    — saturating add of two int32s.
 *   __QSUB    — saturating sub of two int32s.
 *   __USAT    — unsigned saturate to N bits.
 */
#ifndef CMSIS_INTRINSICS_H_
#define CMSIS_INTRINSICS_H_

#include <stdint.h>

#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE __attribute__((always_inline)) static inline
#endif

/* ---------------------------------------------------------------------
** __QADD16 — saturating add of two pairs of signed int16, packed in uint32.
** Each 16-bit half is added independently with signed saturation to
** [-32768, 32767]. Result returned packed in a uint32.
** -------------------------------------------------------------------- */
__STATIC_FORCEINLINE uint32_t __QADD16(uint32_t op1, uint32_t op2)
{
    uint32_t result;
    __asm ("qadd16 %0, %1, %2" : "=r" (result) : "r" (op1), "r" (op2));
    return result;
}

/* ---------------------------------------------------------------------
** __QSUB16 — saturating sub of two pairs of signed int16, packed in uint32.
** Result = op1 - op2, each 16-bit lane saturated to [-32768, 32767].
** -------------------------------------------------------------------- */
__STATIC_FORCEINLINE uint32_t __QSUB16(uint32_t op1, uint32_t op2)
{
    uint32_t result;
    __asm ("qsub16 %0, %1, %2" : "=r" (result) : "r" (op1), "r" (op2));
    return result;
}

/* ---------------------------------------------------------------------
** __QADD / __QSUB — saturating add/sub of two signed int32s.
** Saturate to [-2^31, 2^31 - 1].
** -------------------------------------------------------------------- */
__STATIC_FORCEINLINE int32_t __QADD(int32_t op1, int32_t op2)
{
    int32_t result;
    __asm ("qadd %0, %1, %2" : "=r" (result) : "r" (op1), "r" (op2));
    return result;
}

__STATIC_FORCEINLINE int32_t __QSUB(int32_t op1, int32_t op2)
{
    int32_t result;
    __asm ("qsub %0, %1, %2" : "=r" (result) : "r" (op1), "r" (op2));
    return result;
}

/* ---------------------------------------------------------------------
** __SSAT — signed saturate. Saturates `val` to a signed N-bit range
** [-2^(N-1), 2^(N-1) - 1]. The bit-count `sat` MUST be a compile-time
** constant in [1..32] because the SSAT instruction encodes it as an
** immediate. Implemented as a macro to enforce the constant via the
** "I" inline-asm constraint.
** -------------------------------------------------------------------- */
#define __SSAT(ARG1, ARG2)                                              \
    __extension__                                                       \
    ({                                                                  \
        int32_t __RES, __ARG1 = (ARG1);                                 \
        __asm volatile ("ssat %0, %1, %2"                               \
                        : "=r" (__RES)                                  \
                        : "I" (ARG2), "r" (__ARG1)                      \
                        : "cc");                                        \
        __RES;                                                          \
    })

/* ---------------------------------------------------------------------
** __USAT — unsigned saturate. Saturates `val` to a U-bit range
** [0, 2^N - 1]. Same immediate-constant requirement as __SSAT.
** -------------------------------------------------------------------- */
#define __USAT(ARG1, ARG2)                                              \
    __extension__                                                       \
    ({                                                                  \
        uint32_t __RES, __ARG1 = (ARG1);                                \
        __asm volatile ("usat %0, %1, %2"                               \
                        : "=r" (__RES)                                  \
                        : "I" (ARG2), "r" (__ARG1)                      \
                        : "cc");                                        \
        __RES;                                                          \
    })

/* ---------------------------------------------------------------------
** __CLZ — count leading zeros. ARM's CLZ instruction returns 32 for
** input 0, and the original Euklid generator depends on that exact
** behaviour for empty groups. Do not use __builtin_clz here: C leaves
** __builtin_clz(0) undefined, which lets LTO collapse Euklid patterns
** into front-stacked pulses.
** -------------------------------------------------------------------- */
#define __CLZ(value)                                                   \
    __extension__                                                      \
    ({                                                                 \
        uint32_t __RES, __ARG = (uint32_t)(value);                     \
        __asm ("clz %0, %1" : "=r" (__RES) : "r" (__ARG));            \
        (uint8_t)__RES;                                                \
    })

/* ---------------------------------------------------------------------
** APSR_Type / __get_APSR — read the Application Program Status Register.
** Used by Oscillator.c calcNoise() to check the V (overflow) flag after
** a uint32 phase += phaseInc, detecting phase-wrap for the noise
** generator. Verbatim port from CMSIS_5/cmsis_gcc.h + core_cm7.h.
**
** Bit-field layout assumes little-endian byte order — true for all
** Cortex-M parts including F765. Verified against ARM-software/CMSIS_5
** core_cm7.h.
**
** Caveat (carried from original LXR): this depends on the compiler not
** reordering instructions between the increment that sets V and the
** MRS that reads APSR. Optimization levels above -O1 sometimes do this.
** A parallel code path in Oscillator.c (calcNoiseBlock) uses a pure-C
** overflow check (`lastPhase > newPhase`) instead — that's the safer
** pattern. If APSR-based wrap detection misfires on F765/-O1, the fix
** is to swap calcNoise to the lastPhase comparison. Not done here
** because we're doing a clean port; deviation deferred to enhancement.
** -------------------------------------------------------------------- */
typedef union
{
    struct
    {
        uint32_t _reserved0:16;     /* bit  0..15  Reserved */
        uint32_t GE:4;              /* bit 16..19  Greater-than-or-Equal flags */
        uint32_t _reserved1:7;      /* bit 20..26  Reserved */
        uint32_t Q:1;               /* bit     27  Saturation condition flag */
        uint32_t V:1;               /* bit     28  Overflow condition code flag */
        uint32_t C:1;               /* bit     29  Carry condition code flag */
        uint32_t Z:1;               /* bit     30  Zero condition code flag */
        uint32_t N:1;               /* bit     31  Negative condition code flag */
    } b;                            /* Structure used for bit access */
    uint32_t w;                     /* Type used for word access */
} APSR_Type;

__STATIC_FORCEINLINE uint32_t __get_APSR(void)
{
    uint32_t result;
    __asm volatile ("mrs %0, apsr" : "=r" (result));
    return result;
}

#endif /* CMSIS_INTRINSICS_H_ */
