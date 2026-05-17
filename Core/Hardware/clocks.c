/*
 * Core/Hardware/clocks.c
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
 * clocks.c
 *
 * Brings SYSCLK to 216MHz using HSE (ZQ1 = 16MHz confirmed) as PLL source.
 *
 * Configuration:
 *   HSE = 16MHz → PLLM=16 → VCO_in = 1MHz
 *   PLLN=432, PLLP=2 → SYSCLK = 216MHz  (F765 maximum)
 *   PLLQ=9           → PLL48CLK = 48MHz (USB)
 *   PLLR=2           → PLLCLK = 216MHz  (SAI, kept at reset value)
 *
 * Bus clocks:
 *   HCLK  = SYSCLK/1  = 216MHz  (AHB)
 *   PCLK1 = HCLK/4    =  54MHz  (APB1 — TIM6, USART3, SPI2, SPI3, I2S2, I2S3)
 *   PCLK2 = HCLK/2    = 108MHz  (APB2 — SPI1, ADC)
 *
 * Peripheral impacts vs 16MHz HSI baseline:
 *   TIM6:   PSC 15→107  (timer clock 16MHz→108MHz, stays 1kHz)
 *   USART3: BRR 0x0200→0x06C0  (PCLK1 16MHz→54MHz, stays 31250 baud)
 *   SPI1:   BR 000→010  (PCLK2 16MHz→108MHz, 8MHz→13.5MHz — within 74HC spec)
 *   PLLI2S: unaffected — independent clock, PLLM/PLLSRC already set here
 *
 * Sequence (ST RM0410 section 5.3, overdrive required for >200MHz):
 *   1. Flash wait states (set BEFORE increasing frequency)
 *   2. Enable PWR clock, enable overdrive
 *   3. Enable HSE, wait for HSERDY
 *   4. Configure main PLL
 *   5. Set AHB/APB prescalers
 *   6. Enable PLL, wait for PLLRDY
 *   7. Switch SYSCLK to PLL, wait for SWS confirmation
 */

#include "clocks.h"
#include <stdint.h>

/* -----------------------------------------------------------------------
** Flash
** ----------------------------------------------------------------------- */
#define FLASH_ACR   (*((volatile uint32_t *)0x40023C00UL))

/* -----------------------------------------------------------------------
** PWR  (APB1)
** ----------------------------------------------------------------------- */
#define PWR_CR1     (*((volatile uint32_t *)0x40007000UL))
#define PWR_CSR1    (*((volatile uint32_t *)0x40007004UL))

/* -----------------------------------------------------------------------
** RCC
** ----------------------------------------------------------------------- */
#define RCC_CR          (*((volatile uint32_t *)0x40023800UL))
#define RCC_PLLCFGR     (*((volatile uint32_t *)0x40023804UL))
#define RCC_CFGR        (*((volatile uint32_t *)0x40023808UL))
#define RCC_APB1ENR     (*((volatile uint32_t *)0x40023840UL))
/* RCC_DCKCFGR2: F765-specific register, no F4 equivalent.
** CLK48SEL [27:26]: selects 48MHz clock source for USB OTG FS / RNG / SDMMC.
**   00 = PLL48CLK (PLLQ output) — our PLLQ=9 → 432/9 = 48MHz exactly.
**   Must be written explicitly; F4 USB library never touches this register. */
#define RCC_DCKCFGR2    (*((volatile uint32_t *)0x40023890UL))

/* -----------------------------------------------------------------------
** RCC_PLLCFGR value
**
**   PLLM  [5:0]   = 16  → 16MHz/16 = 1MHz VCO input
**   PLLN  [14:6]  = 432 → VCO = 432MHz  (valid: 100–432MHz)
**   PLLP  [17:16] = 0   → ÷2  → SYSCLK = 216MHz
**   PLLSRC[22]    = 1   → HSE
**   PLLQ  [27:24] = 9   → PLL48CLK = 432/9 = 48MHz (exact, required for USB)
**   PLLR  [30:28] = 2   → reset default, kept valid
**
**   = (16<<0)|(432<<6)|(0<<16)|(1<<22)|(9<<24)|(2<<28)
**   = 0x29406C10
** ----------------------------------------------------------------------- */
#define PLLCFGR_VALUE   0x29406C10UL

/* -----------------------------------------------------------------------
** RCC_CFGR prescaler bits
**
**   HPRE  [7:4]   = 0000 → AHB/1  = 216MHz
**   PPRE1 [12:10] = 101  → APB1/4 =  54MHz
**   PPRE2 [15:13] = 100  → APB2/2 = 108MHz
** ----------------------------------------------------------------------- */
#define CFGR_PRESCALERS  ((5UL << 10) | (4UL << 13))

void sysclk_init(void)
{

    /* Enable FPU coprocessors CP10 and CP11 */
    *((volatile uint32_t *)0xE000ED88UL) |= (0xFUL << 20);
    __asm volatile("dsb");
    __asm volatile("isb");

    /* Enable Cortex-M7 L1 I-Cache (16KB).
    ** Invalidate first (required before first enable per ARM TRM),
    ** then set IC bit in SCB->CCR. */
    *((volatile uint32_t *)0xE000EF50UL) = 0;          /* ICIALLU — invalidate all */
    __asm volatile("dsb");
    __asm volatile("isb");
    *((volatile uint32_t *)0xE000ED14UL) |= (1UL << 17); /* SCB->CCR |= IC */
    __asm volatile("dsb");
    __asm volatile("isb");

    /* ------------------------------------------------------------------
    ** MPU + D-Cache
    **
    ** Configure MPU so D-cache can be safely enabled alongside DMA.
    ** SRAM1 starts at 0x20020000 (368KB). DMA buffers occupy the first
    ** 4KB (.dma_nocache linker section). The rest is general data + stack.
    ** DTCM (0x20000000) bypasses cache by hardware — no MPU region needed.
    **
    ** Region 0: All internal SRAM — Normal, Write-Through No-Write-Allocate.
    **   CPU writes go to both cache and memory, so DMA always sees them.
    **   SIZE=19 → 2^(19+1) = 1MB, base 0x20000000, covers 0x20000000–0x20100000.
    **   This spans DTCM + SRAM1 + SRAM2. DTCM is accessed via the TCM
    **   interface (not AHB), so the cache controller never sees DTCM
    **   accesses regardless of MPU attributes — marking it WT is harmless.
    **   The 1MB region overshoots physical RAM; unmapped addresses would
    **   bus-fault on access regardless of caching.
    **
    ** Region 1: DMA buffers (first 4KB of SRAM1) — Strongly-Ordered.
    **   SIZE=11 → 2^(11+1) = 4KB, covers 0x20020000–0x20021000.
    **   Higher region number wins on overlap, so this overrides Region 0
    **   for the DMA buffer area. Non-cacheable, non-bufferable.
    **
    ** MPU register addresses (ARMv7-M):
    **   MPU_TYPE    0xE000ED90  (read-only, DREGION field)
    **   MPU_CTRL    0xE000ED94
    **   MPU_RNR     0xE000ED98  (region number)
    **   MPU_RBAR    0xE000ED9C  (region base address)
    **   MPU_RASR    0xE000EDA0  (region attribute and size)
    ** ------------------------------------------------------------------ */
    {
        #define MPU_CTRL  (*((volatile uint32_t *)0xE000ED94UL))
        #define MPU_RNR   (*((volatile uint32_t *)0xE000ED98UL))
        #define MPU_RBAR  (*((volatile uint32_t *)0xE000ED9CUL))
        #define MPU_RASR  (*((volatile uint32_t *)0xE000EDA0UL))

        /* Disable MPU during configuration */
        __asm volatile("dmb");
        MPU_CTRL = 0;

        /* Region 0: All internal SRAM — Normal, Write-Through, No Write-Allocate
        ** TEX=0 C=1 B=0 S=1 → Normal WT, shareable
        ** AP=011 → full access.  XN=0.
        ** RASR: XN=0, AP=011(bits[26:24]), TEX=000(bits[21:19]),
        **       S=1(bit18), C=1(bit17), B=0(bit16),
        **       SRD=0x00(bits[15:8]), SIZE=19(bits[5:1]), ENABLE=1(bit0) */
        MPU_RNR  = 0;
        MPU_RBAR = 0x20000000UL;
        MPU_RASR = (3UL << 24) | (1UL << 18) | (1UL << 17) | (19UL << 1) | 1UL;

        /* Region 1: DMA buffers (4KB) — Strongly-Ordered (non-cacheable)
        ** TEX=0 C=0 B=0 S=1 → Strongly-Ordered
        ** AP=011, XN=1 (no execute)
        ** = (1<<28) | (3<<24) | (0<<19) | (1<<18) | (0<<17) | (0<<16) | (11<<1) | 1 */
        MPU_RNR  = 1;
        MPU_RBAR = 0x20020000UL;
        MPU_RASR = (1UL << 28) | (3UL << 24) | (1UL << 18) | (11UL << 1) | 1UL;

        /* Enable MPU with default background map (PRIVDEFENA=1).
        ** Regions not covered by MPU use the default ARM memory map. */
        MPU_CTRL = (1UL << 2) | (1UL << 0);    /* PRIVDEFENA | ENABLE */
        __asm volatile("dsb");
        __asm volatile("isb");
    }

    /* Enable Cortex-M7 L1 D-Cache (16KB).
    ** Invalidate all lines first, then set DC bit in SCB->CCR.
    ** M7 D-cache: 4-way, 16KB, 32-byte lines → 128 sets.
    ** DCISW format: Way[31:30], Set[13:5] */
    for (uint32_t way = 0; way < 4; way++)
        for (uint32_t set = 0; set < 128; set++)
            *((volatile uint32_t *)0xE000EF60UL) = (way << 30) | (set << 5);
    __asm volatile("dsb");
    __asm volatile("isb");
    *((volatile uint32_t *)0xE000ED14UL) |= (1UL << 16); /* SCB->CCR |= DC */
    __asm volatile("dsb");
    __asm volatile("isb");
    /* ------------------------------------------------------------------
    ** Step 1 — Flash wait states
    **
    ** At 216MHz, VOS Scale 1, 3.3V: 7 wait states required (RM0410 Table 5).
    ** Set BEFORE increasing frequency or the CPU may fetch wrong instructions.
    **   LATENCY[3:0] = 7
    **   PRFTEN  (bit 8) = 1  prefetch enable
    **   ARTEN   (bit 9) = 1  ART accelerator (instruction cache) enable
    ** ------------------------------------------------------------------ */
    FLASH_ACR = (7UL | (1UL << 8) | (1UL << 9));
    (void)FLASH_ACR;  /* read back to flush */

    /* ------------------------------------------------------------------
    ** Step 2 — Overdrive mode (required for SYSCLK > 180MHz on F765)
    **
    ** PWR clock: APB1 bit 28
    ** ODEN   = PWR_CR1 bit 16
    ** ODRDY  = PWR_CSR1 bit 16
    ** ODSWEN = PWR_CR1 bit 17
    ** ODSWRDY= PWR_CSR1 bit 17
    ** ------------------------------------------------------------------ */
    RCC_APB1ENR |= (1UL << 28);
    (void)RCC_APB1ENR;

    PWR_CR1 |= (1UL << 16);                        /* ODEN */
    while (!(PWR_CSR1 & (1UL << 16)));             /* wait ODRDY */
    PWR_CR1 |= (1UL << 17);                        /* ODSWEN */
    while (!(PWR_CSR1 & (1UL << 17)));             /* wait ODSWRDY */

    /* ------------------------------------------------------------------
    ** Step 3 — Enable HSE
    **
    ** HSEON  = RCC_CR bit 16
    ** HSERDY = RCC_CR bit 17
    ** No timeout here — if the crystal is absent the system cannot run
    ** at 216MHz; a production build would fall back gracefully.
    ** ------------------------------------------------------------------ */
    RCC_CR |= (1UL << 16);
    while (!(RCC_CR & (1UL << 17)));

    /* ------------------------------------------------------------------
    ** Step 4 — Configure main PLL
    **
    ** PLL must be off before writing PLLCFGR. It is off at reset.
    ** This also sets PLLM and PLLSRC which are shared with PLLI2S —
    ** audioTest_plli2sInit() checks HSERDY and skips its own PLLCFGR
    ** write if we've already set it here.
    ** ------------------------------------------------------------------ */
    RCC_PLLCFGR = PLLCFGR_VALUE;

    /* ------------------------------------------------------------------
    ** Step 5 — AHB/APB prescalers
    **
    ** Set BEFORE switching SYSCLK so the bus clocks are within spec
    ** the instant the faster clock source becomes active.
    ** ------------------------------------------------------------------ */
    RCC_CFGR = CFGR_PRESCALERS;  /* SW=00 (HSI still selected) */

    /* ------------------------------------------------------------------
    ** Step 6 — Enable main PLL
    **
    ** PLLON  = RCC_CR bit 24
    ** PLLRDY = RCC_CR bit 25
    ** ------------------------------------------------------------------ */
    RCC_CR |= (1UL << 24);
    while (!(RCC_CR & (1UL << 25)));

    /* ------------------------------------------------------------------
    ** Step 7 — Switch SYSCLK to PLL
    **
    ** SW[1:0]  = 10 → select PLL  (RCC_CFGR bits[1:0])
    ** SWS[1:0] = 10 → PLL active  (RCC_CFGR bits[3:2])
    ** ------------------------------------------------------------------ */
    RCC_CFGR |= 2UL;
    while ((RCC_CFGR & (3UL << 2)) != (2UL << 2));

    /* ------------------------------------------------------------------
    ** Step 8 — Select PLL48CLK as 48MHz source for USB OTG FS / RNG
    **
    ** RCC_DCKCFGR2 CLK48SEL [27:26] = 00 → PLLQ output (48MHz).
    ** This register does not exist on STM32F4 — the F4 USB library never
    ** writes it, and the reset value is 0x00 which is correct. We write it
    ** explicitly here because ST's F7 application notes recommend not
    ** relying on reset state for this field when USB is used.
    ** ------------------------------------------------------------------ */
    RCC_DCKCFGR2 &= ~(3UL << 26);  /* CLK48SEL = 00 → PLL48CLK (PLLQ) */
}
