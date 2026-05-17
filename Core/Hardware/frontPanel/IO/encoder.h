/*
 * Core/Hardware/frontPanel/IO/encoder.h
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
 * encoder.h
 *
 * Main rotary encoder — SW42, 20-detent digital quadrature + push switch.
 *
 * Uses TIM1 Input Capture on CH3 (PE13/A) and CH4 (PE14/B) with hardware
 * digital filter ICxF=0xF. IRQ27 TIM1_CC fires on any filtered edge.
 *
 * Hardware:
 *   A  = PE13  (AF1/TIM1_CH3, external 10kΩ pull-up R75, no internal pull)
 *   B  = PE14  (AF1/TIM1_CH4, external 10kΩ pull-up R76, no internal pull)
 *   SW = PE15  (input, internal pull-up, active LOW)
 *
 * encode_read4() is the only valid read function for LXR-02's SW42.
 * read1/read2 removed — SW42 is fixed hardware, only read4 is correct.
 *
 * encoder_tick() called from TIM6 at 1kHz for button debounce only.
 */

#ifndef ENCODER_H_
#define ENCODER_H_

#include <stdint.h>

void    encode_init(void);
void    encoder_tim14_tick(void);   /* empty stub — TIM14 not used */
void    encoder_tick(void);         /* TIM6 1kHz — button debounce only */
int8_t  encode_read4(void);         /* 1 count per detent */
uint8_t encode_readButton(void);

#endif /* ENCODER_H_ */
