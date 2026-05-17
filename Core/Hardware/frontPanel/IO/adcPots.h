/*
 * Core/Hardware/frontPanel/IO/adcPots.h
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
 * adcPots.h
 *
 * Slider potentiometers RV5-RV10 (single-ended, 0-3.3V).
 * API mirrors original LXR adcPots.h by Julian Schmidt.
 *
 * The ADC DMA buffer (adc_dma_buf[]) is shared with endlessPots.c and
 * filled continuously — no blocking ADC reads here.
 *
 * adc_checkPots() reads from the DMA buffer, applies deadzone mapping,
 * and updates always-on slider gain multipliers.
 * Call once per main loop iteration.
 */

#ifndef ADCPOTS_H_
#define ADCPOTS_H_

#include <stdint.h>
#include "config.h"

/* ADC and DMA hardware are initialised by endlessPots_init() since both
** share ADC1 and the DMA buffer. adcPots_init() only initialises
** the software state (baseline values). */
void adc_init(void);

/* Check all 6 sliders and unconditionally refresh slider_vol[].
** The mixer applies slider_vol[] as a separate post-voice gain stage
** (voice output multiplied by slider gain).
** Call once per main loop iteration. */
void adc_checkPots(void);

/* Slider volume as float [0.0, 1.0], index 0=RV5..5=RV10.
** Updated by adc_checkPots(). */
extern float slider_vol[ADC_POT_COUNT];

/* Raw 12-bit ADC reading for slider i (0=RV5 .. 5=RV10). */
uint16_t adc_getPotRaw(uint8_t i);

/* Scaled 0-100 value for slider i. */
uint8_t adc_getPotValue(uint8_t i);

#endif /* ADCPOTS_H_ */
