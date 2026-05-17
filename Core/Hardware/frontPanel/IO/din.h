/*
 * Core/Hardware/frontPanel/IO/din.h
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
 * din.h
 *
 * Digital input via 74HC165 shift register chain (40 buttons).
 * API mirrors original LXR din.h by Julian Schmidt.
 *
 * Hardware:
 *   SPI1 MISO = PA6 (AF5)  — serial data from last 74HC165 in chain
 *   SPI1 SCK  = PB3 (AF5)  — shared clock (also used by dout)
 *   LATCH     = PB2 (GPIO) — falling edge parallel-loads all button states
 *
 * The 74HC165 and 74HC595 chains share SPI1 (full-duplex). dout_latch()
 * pulses PB2: falling edge loads 74HC165, rising edge latches 74HC595.
 * din_dout_exchange() then clocks SR_CHAIN_BYTES through SPI simultaneously.
 *
 * Button states: active HIGH (button pressed = 1).
 * SW43 (PB7) is a separate GPIO, read independently.
 *
 * din_inputData[] is updated by the TIM6 ISR every 1ms.
 * buttonHandler_buttonPressed() / _buttonReleased() are called on edges.
 */

#ifndef DIN_H_
#define DIN_H_

#include <stdint.h>
#include "config.h"

/* Raw button state — 1 bit per button, updated by ISR.
** Byte 0 bit 0 = button 0, etc. */
extern volatile uint8_t din_inputData[NUM_INPUTS / 8 + 1];

/* Initialise MISO GPIO and clear button state. Call before time_initTimer(). */
void din_init(void);

/* Full-duplex SPI exchange: clock out LED data, clock in button data.
** Called from TIM6 ISR after dout_latch(). Updates din_inputData[],
** detects edges, calls buttonHandler_buttonPressed/Released. */
void din_dout_exchange(void);

/* Read the SW43 button (PB7 GPIO). Returns 1 if pressed. */
uint8_t din_readSw43(void);

#endif /* DIN_H_ */
