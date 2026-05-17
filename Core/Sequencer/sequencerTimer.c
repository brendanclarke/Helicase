/*
 * Core/Sequencer/sequencerTimer.c
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
 * sequencerTimer.c - TIM3 sequencer timing owner.
 *
 * Session 019 Phase 6: MIDI realtime and trigger-jack edges are still captured
 * by their source ISRs with TIM2 timestamps, but the musical work is consumed
 * here from a single 4kHz hardware owner. That keeps seq_tick(), MIDI clock
 * output, CLK OUT updates, and external sync corrections from waiting behind
 * LCD, SD, encoder, or foreground MIDI parsing work.
 */

#include "sequencerTimer.h"
#include "MidiParser.h"
#include "sequencer.h"
#include "triggerJacks.h"
#include <stdint.h>

/* TIM3 is an APB1 general-purpose timer. PCLK1=54MHz with APB1 prescaler != 1,
** so the timer clock is 108MHz. PSC=107 gives 1us ticks; ARR=249 gives the
** original/reference 0.25ms sequencer quantum. */
#define RCC_APB1ENR  (*((volatile uint32_t *)0x40023840UL))

#define TIM3_BASE    0x40000400UL
#define TIM3_CR1     (*((volatile uint32_t *)(TIM3_BASE + 0x00UL)))
#define TIM3_DIER    (*((volatile uint32_t *)(TIM3_BASE + 0x0CUL)))
#define TIM3_SR      (*((volatile uint32_t *)(TIM3_BASE + 0x10UL)))
#define TIM3_EGR     (*((volatile uint32_t *)(TIM3_BASE + 0x14UL)))
#define TIM3_PSC     (*((volatile uint32_t *)(TIM3_BASE + 0x28UL)))
#define TIM3_ARR     (*((volatile uint32_t *)(TIM3_BASE + 0x2CUL)))

#define NVIC_ISER0   (*((volatile uint32_t *)0xE000E100UL))
#define NVIC_IPR(n)  (*((volatile uint8_t  *)(0xE000E400UL + (n))))
#define IRQ_TIM3     29u

void sequencerTimer_init(void)
{
	RCC_APB1ENR |= (1UL << 1);  /* TIM3EN */
	(void)RCC_APB1ENR;

	TIM3_CR1 = 0;
	TIM3_PSC = 107;             /* 108MHz / 108 = 1MHz */
	TIM3_ARR = 249;             /* 1MHz / 250 = 4kHz */
	TIM3_EGR = 1;               /* load PSC/ARR */
	TIM3_SR = 0;
	TIM3_DIER = 1;              /* UIE */

	/* Sequencer timing sits below SysTick/TIM6/encoder, alongside the LCD
	** service tier, and above audio DMA packing. This is intentionally easy to
	** change after bench jitter/underrun measurements. */
	NVIC_IPR(IRQ_TIM3) = 2u << 4;
	NVIC_ISER0 |= (1UL << IRQ_TIM3);

	TIM3_CR1 = 1;               /* CEN */
}

void TIM3_IRQHandler(void)
{
	if (TIM3_SR & 1u) {
		TIM3_SR = 0;

		/* Phase 6 timing owner order:
		** 1. Apply timestamped MIDI realtime bytes.
		** 2. Apply timestamped CLK/RST jack events.
		** 3. Advance/schedule the sequencer and emit clock outs. */
		midiParser_processRealtimeEvents();
		triggerJacks_tick();
		seq_tick();
	}
}
