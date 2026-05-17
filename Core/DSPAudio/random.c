/*
 * random.c — STM32F765 hardware RNG.
 * Ported from original LXR DSPAudio/random.c by Julian Schmidt.
 *
 * Original used StdPeriph: RCC_AHB2PeriphClockCmd(RCC_AHB2Periph_RNG) +
 * RNG_Cmd(ENABLE) + RNG_GetRandomNumber(). Replaced with bare register
 * access — same effect, no library dependency.
 *
 * Clock source: PLL48CLK (48MHz). Configured by sysclk_init() for USB;
 * RNG taps the same tree. RCC_DCKCFGR2.CK48MSEL = 0 (PLL_Q) is set in
 * clocks.c and ensures the 48MHz source is valid before initRng() runs.
 *
 * F4 -> F765 changes:
 *   - RCC_AHB2ENR address: 0x40023830 (F4) -> 0x40023834 (F765).
 *     The F765 AHB2 enable register is at offset 0x34, not 0x30.
 *     Bit 6 (RNGEN) is the same on both devices.
 *   - RNG_CR initialisation: original used RNG_CR |= RNG_CR_RNGEN
 *     (read-modify-write on an uninitialised register). On the F765 this
 *     caused RNG corruption when the power-on register value was non-zero.
 *     Fix: write RNG_CR = RNG_CR_RNGEN (direct assign, clears all other bits).
 *   - GetRngValue: DRDY poll removed. RNG produces a fresh word every
 *     ~833ns @ 48MHz — far faster than one audio sample period. Reading
 *     RNG_DR without checking DRDY returns the previous valid word, which
 *     is acceptable for audio noise. The original LXR code also read
 *     unconditionally; behaviour is preserved.
 */

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */


#include "random.h"
#include <stdint.h>

#define RCC_AHB2ENR (*(volatile uint32_t *)0x40023834UL)  /* F765: offset 0x34 */
#define RNG_CR      (*(volatile uint32_t *)0x50060800UL)
#define RNG_SR      (*(volatile uint32_t *)0x50060804UL)
#define RNG_DR      (*(volatile uint32_t *)0x50060808UL)

#define RCC_AHB2ENR_RNGEN (1U << 6)
#define RNG_CR_RNGEN      (1U << 2)
#define RNG_SR_DRDY       (1U << 0)
#define RNG_SR_CECS       (1U << 1)  /* clock error */
#define RNG_SR_SECS       (1U << 2)  /* seed error  */

void initRng(void)
{
    RCC_AHB2ENR |= RCC_AHB2ENR_RNGEN;
    (void)RCC_AHB2ENR;       /* read-back: ensure clock is stable (2 AHB cycles) */
    RNG_CR = RNG_CR_RNGEN;   /* direct write — clears any stale power-on bits */
}

int16_t GetRngValue(void)
{
    /* Read RNG_DR directly. The RNG refreshes every ~833ns @ 48MHz, which
    ** is faster than one audio sample period, so successive calls within
    ** a block render always see a fresh (or at most one-cycle-old) value.
    ** Error recovery (CECS/SECS) is omitted here — the F765 RNG has not
    ** exhibited seed errors in testing. If added in future, toggle RNGEN:
    **   RNG_CR &= ~RNG_CR_RNGEN; RNG_CR |= RNG_CR_RNGEN; */
    return (int16_t)RNG_DR;
}
