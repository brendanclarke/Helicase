/*
 * Core/Hardware/SD/SPI/spi_sd.c
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
 * spi_sd.c — bit-banged SPI for SD card, all pins confirmed.
 *
 *   PD0  = CS    active low     ✓ GPIOD bit test
 *   PD1  = DETECT active low    ✓ GPIOD bit test
 *   PD2  = MOSI  (DI)          ✓ GPIOD bit test
 *   PC12 = SCLK                ✓ I2S3_SD was wrongly assigned here;
 *                                 correct I2S3_SD = PB5, PC12 is free
 *   PC8  = MISO  (DO)          ✓ GPIOC bit test
 *
 * SPI Mode 0: CPOL=0, CPHA=0.
 */

#include "spi_sd.h"
#include <stdint.h>

#define GPIOC_BASE    0x40020800UL
#define GPIOC_MODER   (*((volatile uint32_t *)(GPIOC_BASE + 0x00UL)))
#define GPIOC_OTYPER  (*((volatile uint32_t *)(GPIOC_BASE + 0x04UL)))
#define GPIOC_PUPDR   (*((volatile uint32_t *)(GPIOC_BASE + 0x0CUL)))
#define GPIOC_IDR     (*((volatile uint32_t *)(GPIOC_BASE + 0x10UL)))
#define GPIOC_BSRR    (*((volatile uint32_t *)(GPIOC_BASE + 0x18UL)))
#define GPIOC_AFRH    (*((volatile uint32_t *)(GPIOC_BASE + 0x24UL)))

#define GPIOD_BASE    0x40020C00UL
#define GPIOD_MODER   (*((volatile uint32_t *)(GPIOD_BASE + 0x00UL)))
#define GPIOD_OTYPER  (*((volatile uint32_t *)(GPIOD_BASE + 0x04UL)))
#define GPIOD_PUPDR   (*((volatile uint32_t *)(GPIOD_BASE + 0x0CUL)))
/* GPIOD_BSRR in spi_sd.h for CS macros */

/* PC12: BSRR bit 12 high, bit 28 low */
#define SCLK_HIGH()  (GPIOC_BSRR = (1UL << 12))
#define SCLK_LOW()   (GPIOC_BSRR = (1UL << 28))
#define MISO_READ()  ((GPIOC_IDR >> 8) & 1U)
#define MOSI_HIGH()  (GPIOD_BSRR = (1UL << 2))
#define MOSI_LOW()   (GPIOD_BSRR = (1UL << 18))

static uint8_t bb_slow = 1;

static void bb_half_delay(void)
{
    for (volatile int i = 0; i < 100; i++)
        __asm volatile("nop");
}

void spi_sd_set_slow(void)
{
    /* PC12 (SCLK): output, push-pull, no pull, AF cleared
    ** MODER[25:24], OTYPER bit12, AFRH[19:16] */
    GPIOC_AFRH   &= ~(0xFUL << 16);          /* clear AF12 */
    GPIOC_MODER  &= ~((3UL << 24) | (3UL << 16));  /* clear PC12+PC8 */
    GPIOC_MODER  |=  (1UL << 24);            /* PC12 output */
                                              /* PC8 input (00) */
    GPIOC_OTYPER &= ~((1UL << 12) | (1UL << 8));
    GPIOC_PUPDR  &= ~((3UL << 24) | (3UL << 16));
    GPIOC_PUPDR  |=  (1UL << 16);            /* PC8 pull-up */

    /* PD0 (CS), PD2 (MOSI): output, push-pull, no pull */
    GPIOD_MODER  &= ~((3UL << 0) | (3UL << 4));
    GPIOD_MODER  |=  ((1UL << 0) | (1UL << 4));
    GPIOD_OTYPER &= ~((1UL << 0) | (1UL << 2));
    GPIOD_PUPDR  &= ~((3UL << 0) | (3UL << 4));

    SD_CS_DEASSERT;
    SCLK_LOW();
    MOSI_HIGH();

    bb_slow = 1;
}

void spi_sd_set_fast(void) { bb_slow = 0; }

unsigned char SPI_transmit(unsigned char tx)
{
    unsigned char rx = 0;
    int i;
    for (i = 7; i >= 0; i--) {
        if (tx & (1u << i)) MOSI_HIGH(); else MOSI_LOW();
        if (bb_slow) bb_half_delay();
        SCLK_HIGH();
        if (bb_slow) bb_half_delay();
        rx = (unsigned char)((rx << 1) | MISO_READ());
        SCLK_LOW();
        if (bb_slow) bb_half_delay();
    }
    return rx;
}

unsigned char SPI_receive(void) { return SPI_transmit(0xFF); }
