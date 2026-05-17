/*
 * Core/Hardware/frontPanel/IO/din.c
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
 * din.c
 *
 * Digital input via 74HC165 × 5, read over SPI1 simultaneously with
 * LED output (74HC595) in full-duplex mode.
 */

#include "din.h"
#include "dout.h"
#include "buttonHandler.h"
#include <string.h>

/* -----------------------------------------------------------------------
** Registers
** ----------------------------------------------------------------------- */
#define RCC_AHB1ENR  (*((volatile uint32_t *)0x40023830UL))
#define GPIOA_BASE   0x40020000UL
#define GPIOB_BASE   0x40020400UL
#define MODER(b)     (*((volatile uint32_t *)((b)+0x00)))
#define PUPDR(b)     (*((volatile uint32_t *)((b)+0x0C)))
#define IDR(b)       (*((volatile uint32_t *)((b)+0x10)))
#define AFRL(b)      (*((volatile uint32_t *)((b)+0x20)))
#define SPI1_SR      (*((volatile uint32_t *)0x40013008UL))

/* -----------------------------------------------------------------------
** State
** ----------------------------------------------------------------------- */
volatile uint8_t din_inputData[NUM_INPUTS / 8 + 1];

/* Debounce history — 3-sample shift register per button */
static uint8_t  btn_hist[NUM_INPUTS + 1];
static uint8_t  btn_held[NUM_INPUTS + 1];

/* SW43 debounce */
static uint8_t sw43_hist = 0;
static uint8_t sw43_held = 0;

/* -----------------------------------------------------------------------
** dout_spi_xfer declared in dout.c
** ----------------------------------------------------------------------- */
extern uint8_t dout_spi_xfer(uint8_t tx);

/* -----------------------------------------------------------------------
** Debounce helper
** ----------------------------------------------------------------------- */
static void debounce_bit(uint8_t idx, uint8_t raw)
{
    btn_hist[idx] = (uint8_t)((btn_hist[idx] << 1) | (raw & 1)) & 0x07;
    uint8_t new_held;
    if      (btn_hist[idx] == 0x07) new_held = 1;
    else if (btn_hist[idx] == 0x00) new_held = 0;
    else return;

    if (new_held != btn_held[idx]) {
        btn_held[idx] = new_held;
        if (new_held) buttonHandler_buttonPressed(idx);
        else          buttonHandler_buttonReleased(idx);
    }
}

/* -----------------------------------------------------------------------
** Public API
** ----------------------------------------------------------------------- */
void din_init(void)
{
    RCC_AHB1ENR |= (1UL<<0) | (1UL<<1);  /* GPIOA, GPIOB */
    (void)RCC_AHB1ENR;

    /* PA6 = MISO, AF5 */
    MODER(GPIOA_BASE) &= ~(3UL << 12);
    MODER(GPIOA_BASE) |=  (2UL << 12);
    AFRL(GPIOA_BASE)  &= ~(0xFUL << 24);
    AFRL(GPIOA_BASE)  |=  (5UL  << 24);

    /* PB7 = SW43 switch, input, no pull */
    MODER(GPIOB_BASE) &= ~(3UL << 14);
    PUPDR(GPIOB_BASE) &= ~(3UL << 14);

    memset((void *)din_inputData, 0, sizeof(din_inputData));
    memset(btn_hist, 0, sizeof(btn_hist));
    memset(btn_held, 0, sizeof(btn_held));
}

void din_dout_exchange(void)
{
    uint8_t rx[SR_CHAIN_BYTES];

    /* Full-duplex: TX = LED shadow, RX = button data */
    for (int i = 0; i < SR_CHAIN_BYTES; i++)
        rx[i] = dout_spi_xfer(dout_outputData[i]);

    /* Wait for SPI to finish — timeout guards against stall from rail glitch */
    uint32_t t = 10000; while ((SPI1_SR & (1UL << 7)) && --t);

    /* Debounce shift-register buttons (active HIGH, MSB first per byte) */
    for (int n = 0; n < NUM_INPUTS; n++) {
        uint8_t raw = (rx[n / 8] >> (7 - (n % 8))) & 1;
        debounce_bit(n, raw);
        /* Update raw state array */
        uint8_t ap = (uint8_t)(n / 8);
        uint8_t bp = (uint8_t)(n % 8);
        if (raw) din_inputData[ap] |=  (uint8_t)(1 << bp);
        else     din_inputData[ap] &= (uint8_t)~(1 << bp);
    }

    /* SW43 (PB7) — debounce separately */
    uint8_t sw43_raw = (uint8_t)((IDR(GPIOB_BASE) >> 7) & 1);
    sw43_hist = (uint8_t)((sw43_hist << 1) | sw43_raw) & 0x07;
    uint8_t new_held;
    if      (sw43_hist == 0x07) new_held = 1;
    else if (sw43_hist == 0x00) new_held = 0;
    else { return; }
    if (new_held != sw43_held) {
        sw43_held = new_held;
        if (new_held) buttonHandler_buttonPressed(SW43_BUTTON_IDX);
        else          buttonHandler_buttonReleased(SW43_BUTTON_IDX);
    }
    /* Update raw state */
    if (sw43_raw) din_inputData[NUM_INPUTS / 8] |=  0x01;
    else          din_inputData[NUM_INPUTS / 8] &= ~0x01;
}

uint8_t din_readSw43(void)
{
    return sw43_held;
}
