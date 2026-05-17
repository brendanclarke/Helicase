/*
 * Core/compat/stm32f4xx.h
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
 * Core/compat/stm32f4xx.h — compatibility shim.
 *
 * Many DSP/Sequencer files in the original LXR source carry a vestigial
 *   #include "stm32f4xx.h"
 * but only use it for stdint types. To let those files land verbatim
 * during the F765 port, this shim resolves the include and pulls in
 * <stdint.h>.
 *
 * What this shim does NOT do: provide F4 peripheral register definitions
 * (GPIO_, RCC_, DMA_, TIM_, SPI_, NVIC_, …). Any file relying on those
 * tokens for register access needs explicit porting and will fail to
 * compile against this shim — that is intentional, the failure flags
 * the file as needing review rather than silently miscompiling.
 *
 * Files known to need rework (do NOT just compile against this shim):
 *   - DSPAudio/mixer.c — reads GPIOC/GPIOA IDR for jack-detect
 *     (PA0/PA5/PC4/PC5 are slider/encoder ADC inputs on LXR-02)
 *   - DSPAudio/random.c — already ported (Core/DSPAudio/random.c)
 *
 * Note: NOT placed at top of any include path that would cover the rest
 * of the firmware. Only files that legitimately use this shim should
 * have Core/compat reachable. See Makefile.
 */
 #ifndef STM32F4XX_H_SHIM_
 #define STM32F4XX_H_SHIM_
 
 #include <stdint.h>
 #include "cmsis_intrinsics.h"   /* __QADD16, __QSUB16, __SSAT, __CLZ — for ported DSP files */
 
 #endif
