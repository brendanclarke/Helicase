/*
 * Core/Hardware/frontPanel/IO/endlessPots.h
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
 * endlessPots.h
 *
 * Analog endless pots RV1-RV4.
 *
 * Each pot has two sine wave outputs 90° apart (one electrical cycle
 * per full revolution). Direction and magnitude are derived via atan2
 * on the centre-subtracted ADC values, giving 200 integer increments
 * per full clockwise revolution.
 *
 * Output is a signed delta — the caller accumulates into whatever
 * parameter range it needs. No clamping is done here.
 *
 * The driver snapshots raw A/B baselines after page changes, after a long
 * timeout while moving, and shortly after the last emitted delta. Movement
 * inside ENDLESS_POT_DEADZONE is ignored.
 *
 * Delta width note: endlessPots_getDelta() returns int32_t, while current
 * menu callers still consume small per-pass deltas. The hardware cannot
 * realistically generate huge single-pass movement, but if the main loop is
 * ever allowed to stall for long periods, revisit that caller-side narrowing.
 */

#ifndef ENDLESSPOTS_H_
#define ENDLESSPOTS_H_

#include <stdint.h>
#include "config.h"

/* ADC DMA buffer — shared with adcPots.c */
extern volatile uint16_t adc_dma_buf[ADC_DMA_BUF_LEN];

/* Initialise GPIO analog pins and start ADC1 continuous scan + DMA. */
void endlessPots_init(void);

/* Called from TIM6 ISR at 1kHz — updates all endless-pot deltas. */
void endlessPots_tick(void);

/* Returns signed delta since last call. Self-clearing.
** Positive = clockwise, negative = counter-clockwise. */
int32_t endlessPots_getDelta(uint8_t i);

/* Returns 1 if endless pot i has moved since last getDelta() call. */
uint8_t endlessPots_hasChanged(uint8_t i);

/* Rebaseline one/all endless pots from current raw ADC values and clear
** pending movement. Use when the menu page changes parameter mapping. */
void endlessPots_snapshot(uint8_t i);
void endlessPots_snapshotAll(void);

/* Set per-pot angular scale. Double mode emits one delta after half the
** normal angular travel, used by 0-255 menu parameters. */
void endlessPots_setDouble(uint8_t i, uint8_t enabled);

#endif /* ENDLESSPOTS_H_ */
