/*
 * Core/Hardware/frontPanel/IO/dout.c
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
 * dout.c
 *
 * Digital output via 74HC595 × 5, driven over SPI1.
 */

#include "dout.h"
#include <string.h>

/* -----------------------------------------------------------------------
** Registers
** ----------------------------------------------------------------------- */
#define RCC_AHB1ENR  (*((volatile uint32_t *)0x40023830UL))
#define RCC_APB2ENR  (*((volatile uint32_t *)0x40023844UL))

#define GPIOA_BASE   0x40020000UL
#define GPIOB_BASE   0x40020400UL
#define MODER(b)     (*((volatile uint32_t *)((b)+0x00)))
#define OTYPER(b)    (*((volatile uint32_t *)((b)+0x04)))
#define PUPDR(b)     (*((volatile uint32_t *)((b)+0x0C)))
#define BSRR(b)      (*((volatile uint32_t *)((b)+0x18)))
#define AFRL(b)      (*((volatile uint32_t *)((b)+0x20)))
#define AFRH(b)      (*((volatile uint32_t *)((b)+0x24)))

#define SPI1_CR1     (*((volatile uint32_t *)0x40013000UL))
#define SPI1_CR2     (*((volatile uint32_t *)0x40013004UL))
#define SPI1_SR      (*((volatile uint32_t *)0x40013008UL))
#define SPI1_DR      (*((volatile uint32_t *)0x4001300CUL))

#define PB_HI(p)  (BSRR(GPIOB_BASE) = (1UL<<(p)))
#define PB_LO(p)  (BSRR(GPIOB_BASE) = (1UL<<((p)+16)))

/* -----------------------------------------------------------------------
** State
** ----------------------------------------------------------------------- */
volatile uint8_t dout_outputData[NUM_OUTS / 8];

/* -----------------------------------------------------------------------
** SPI byte — polled, blocking ~1μs at 13.5MHz SPI clock.
** Timeouts guard against SPI1 stall from power rail glitches
** (e.g. VT1/VT2 transistor switching on RST IN / CLK IN jacks).
** At 216MHz, 1000 iterations ≈ 5μs — well beyond any normal SPI timeout.
** ----------------------------------------------------------------------- */
static inline uint8_t spi_xfer(uint8_t tx)
{
    uint32_t t;
    t = 1000; while (!(SPI1_SR & (1UL << 1)) && --t);  /* wait TXE  */
    *((volatile uint8_t *)&SPI1_DR) = tx;
    t = 1000; while (!(SPI1_SR & (1UL << 0)) && --t);  /* wait RXNE */
    return *((volatile uint8_t *)&SPI1_DR);
}

/* -----------------------------------------------------------------------
** Public API
** ----------------------------------------------------------------------- */
void dout_init(void)
{
    RCC_AHB1ENR |= (1UL<<0) | (1UL<<1);  /* GPIOA, GPIOB */
    (void)RCC_AHB1ENR;
    RCC_APB2ENR |= (1UL<<12);            /* SPI1 */
    (void)RCC_APB2ENR;

    /* PA7 = MOSI, AF5 */
    MODER(GPIOA_BASE) &= ~(3UL << 14);
    MODER(GPIOA_BASE) |=  (2UL << 14);
    AFRL(GPIOA_BASE)  &= ~(0xFUL << 28);
    AFRL(GPIOA_BASE)  |=  (5UL  << 28);

    /* PB3 = SCK, AF5 */
    MODER(GPIOB_BASE) &= ~(3UL << 6);
    MODER(GPIOB_BASE) |=  (2UL << 6);
    AFRL(GPIOB_BASE)  &= ~(0xFUL << 12);
    AFRL(GPIOB_BASE)  |=  (5UL  << 12);

    /* PB2 = LATCH, output, high initially */
    MODER(GPIOB_BASE) &= ~(3UL << 4);
    MODER(GPIOB_BASE) |=  (1UL << 4);
    OTYPER(GPIOB_BASE) &= ~(1UL << 2);
    PUPDR(GPIOB_BASE)  &= ~(3UL << 4);
    PB_HI(2);

    /* PB8 = SW43 LED, output, low initially */
    MODER(GPIOB_BASE) &= ~(3UL << 16);
    MODER(GPIOB_BASE) |=  (1UL << 16);
    OTYPER(GPIOB_BASE) &= ~(1UL << 8);
    PUPDR(GPIOB_BASE)  &= ~(3UL << 16);
    PB_LO(8);

    /* SPI1: master, software NSS, 8-bit, CPOL=0, CPHA=0
    ** BR=010 → PCLK2/8 = 108MHz/8 = 13.5MHz (safe for 74HC595 at 3.3V) */
    SPI1_CR1 = (1UL<<9) | (1UL<<8) | (2UL<<3) | (1UL<<2);  /* SSM,SSI,BR=010,MSTR */
    SPI1_CR2 = (1UL<<12) | (0x7UL<<8);            /* FRXTH, DS=8bit */
    SPI1_CR1 |= (1UL<<6);                          /* SPE */

    memset((void *)dout_outputData, 0, sizeof(dout_outputData));
}

void dout_latch(void)
{
    /* Falling edge: parallel-loads 74HC165 button data.
    ** Rising edge: latches shifted data into 74HC595 output registers. */
    PB_LO(2);
    __asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop");
    PB_HI(2);
    __asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop");
}

/* Called from din_dout_exchange() in din.c — not directly from application */
uint8_t dout_spi_xfer(uint8_t tx) { return spi_xfer(tx); }

void dout_setSw43Led(uint8_t on)
{
    if (on) PB_HI(8); else PB_LO(8);
}
