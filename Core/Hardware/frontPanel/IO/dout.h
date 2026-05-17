/*
 * Core/Hardware/frontPanel/IO/dout.h
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
 * dout.h
 *
 * Digital output via 74HC595 shift register chain (40 LEDs).
 * API mirrors original LXR dout.h by Julian Schmidt.
 *
 * Hardware:
 *   SPI1 MOSI = PA7 (AF5) — data into first 74HC595
 *   SPI1 SCK  = PB3 (AF5) — shared clock
 *   LATCH     = PB2 (GPIO) — rising edge transfers shift→storage registers
 *
 * Chain: 5 × 74HC595, 40 outputs total.
 * Bit ordering: led_chain_bit = 39 - button_index
 *   (chains run opposite direction to button scan chain)
 *
 * dout_outputData[] is written by ledHandler.c and pushed to hardware
 * by the TIM6 ISR (timebase.c) every 1ms.
 */

#ifndef DOUT_H_
#define DOUT_H_

#include <stdint.h>
#include "config.h"

/* Shadow buffer — one bit per LED output.
** Write here; TIM6 ISR flushes to hardware automatically. */
extern volatile uint8_t dout_outputData[NUM_OUTS / 8];

/* Initialise SPI1 and LATCH GPIO. Call before time_initTimer(). */
void dout_init(void);

/* Pulse the LATCH line — called from TIM6 ISR only. */
void dout_latch(void);

/* Set/clear the SW43 LED (PB8 GPIO, separate from shift register chain). */
void dout_setSw43Led(uint8_t on);

#endif /* DOUT_H_ */
