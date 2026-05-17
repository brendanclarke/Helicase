/*
 * Core/Hardware/timebase.h
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
 * timebase.h
 *
 * LXR-02 system timers.
 *
 * TIM6 — 1kHz heartbeat ISR (TIM6_DAC_IRQHandler, IRQ54):
 *   - Increments front-panel/service time.
 *   - Schedules a 500Hz foreground front-panel service pass.
 *
 * TIM7 — 5kHz LCD state machine (TIM7_IRQHandler, IRQ55):
 *   - Drains the lcd_* op queue one state-machine step per tick (200us).
 *   - Initialised by lcd_tim7_init() in lcd.c.
 *   - ISR body lives in timebase.c; calls lcd_tim7_tick().
 *
 * TIM2 — 1MHz free-running timestamp counter:
 *   - Shared by future MIDI realtime RX and CLK/RST trigger-jack capture.
 *   - Never reset on a pulse; callers use unsigned subtraction for deltas.
 *
 * SysTick — 4kHz canonical LXR mainboard tick (priority 0):
 *   - Increments systick_ticks at 0.25ms resolution.
 *   - Divides down lcd_ms_ticks for lcd_init() blocking delays only.
 *   - After TIM7 starts, lcd_ms_ticks still increments but is unused.
 *
 * time_sysTick increments in TIM6 ISR — 1ms front-panel/service tick for
 * UI timeouts, rate limiting, blink timers, etc.
 */

#ifndef TIMEBASE_H_
#define TIMEBASE_H_

#include <stdint.h>
#include "../Menu/screensaver.h"
/* Front-panel/service tick counter — increments every 1ms in TIM6 ISR.
** Wraps at 65535 (~65 seconds). Use (uint16_t)(now - last) for safe
** subtraction across the wrap boundary. */
extern volatile uint16_t time_sysTick;

/* Initialise SysTick at 4kHz. Must be called BEFORE lcd_init() since
** the display init uses blocking ms delays derived from it. */
void time_initSysTick(void);

/* Initialise TIM6 at 1kHz and enable its interrupt.
** Must be called after dout_init(), din_init(), encoder_init(),
** endlessPots_init(). */
void time_initTimer(void);

/* Foreground front-panel service. Drains the TIM6-scheduled service flag and
** performs the heavier shift-register, encoder-button, jack-detect, and
** endless-pot work outside interrupt context. */
void timebase_serviceFrontPanel(void);

/* 1us shared timestamp. Safe from foreground code and ISRs. TIM2 wraps after
** about 71 minutes; always compute intervals with timebase_tim2Delta() or
** equivalent unsigned subtraction. */
uint32_t timebase_tim2Now(void);
uint32_t timebase_tim2Delta(uint32_t newer, uint32_t older);

#endif /* TIMEBASE_H_ */
