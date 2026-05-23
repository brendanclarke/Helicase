/*
 * Core/Hardware/timebase.c
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
 * timebase.c
 *
 * LXR-02 system timers.
 *
 * TIM6 — 1kHz heartbeat: increments UI/service time and schedules a
 *         foreground front-panel scan at 500Hz.
 *
 * TIM7 — 5kHz LCD state machine: drains the lcd_* op queue one step
 *         per tick. Initialised by lcd_tim7_init() in lcd.c; the ISR
 *         lives here so all IRQ handlers are in one translation unit.
 *
 * TIM1  — Input capture on CH3/CH4 (PE13/PE14) with hardware digital
 *          filter (ICxF=0xF). IRQ27 TIM1_CC. ISR in encoder.c.
 *
 * TIM2  — Free-running 1MHz / 1us timestamp counter. This is shared by
 *          MIDI realtime RX and CLK/RST trigger-jack capture; code that
 *          samples edges must read it and use unsigned subtraction, never
 *          reset it on a pulse.
 *
 * SysTick — 4kHz canonical LXR mainboard tick for systick_ticks.
 *            lcd_ms_ticks is divided down from this during lcd_init()
 *            blocking delays, before TIM7 is running.
 */

#include "timebase.h"
#include "lcd.h"
#include "config.h"
#include "dout.h"
#include "din.h"
#include "encoder.h"
#include "endlessPots.h"
#include "mixer.h"

/* -----------------------------------------------------------------------
** TIM6 registers (APB1, base 0x40001000)
** ----------------------------------------------------------------------- */
#define RCC_APB1ENR  (*((volatile uint32_t *)0x40023840UL))
#define TIM6_BASE    0x40001000UL
#define TIM6_CR1     (*((volatile uint32_t *)(TIM6_BASE+0x00)))
#define TIM6_DIER    (*((volatile uint32_t *)(TIM6_BASE+0x0C)))
#define TIM6_SR      (*((volatile uint32_t *)(TIM6_BASE+0x10)))
#define TIM6_EGR     (*((volatile uint32_t *)(TIM6_BASE+0x14)))
#define TIM6_PSC     (*((volatile uint32_t *)(TIM6_BASE+0x28)))
#define TIM6_ARR     (*((volatile uint32_t *)(TIM6_BASE+0x2C)))
#define GPIOB_IDR    (*((volatile uint32_t *)0x40020410UL))
#define GPIOD_IDR    (*((volatile uint32_t *)0x40020C10UL))

/* -----------------------------------------------------------------------
** TIM2 free-running timestamp counter
** PCLK1=54MHz and APB1 prescaler != 1, so TIM2 sees 108MHz. PSC=107 gives
** 1MHz ticks. As a 32-bit timer it wraps after roughly 71 minutes, which is
** fine when callers compute deltas as (uint32_t)(now - then).
** ----------------------------------------------------------------------- */
#define TIM2_BASE    0x40000000UL
#define TIM2_CR1     (*((volatile uint32_t *)(TIM2_BASE+0x00)))
#define TIM2_EGR     (*((volatile uint32_t *)(TIM2_BASE+0x14)))
#define TIM2_CNT     (*((volatile uint32_t *)(TIM2_BASE+0x24)))
#define TIM2_PSC     (*((volatile uint32_t *)(TIM2_BASE+0x28)))
#define TIM2_ARR     (*((volatile uint32_t *)(TIM2_BASE+0x2C)))

/* -----------------------------------------------------------------------
** NVIC (shared by TIM6 init and TIM7 init in lcd.c)
** ----------------------------------------------------------------------- */
#define NVIC_ISER1   (*((volatile uint32_t *)0xE000E104UL))
#define NVIC_IPR(n)  (*((volatile uint8_t  *)(0xE000E400UL+(n))))
#define IRQ_TIM6_DAC 54

/* -----------------------------------------------------------------------
** SysTick — 4kHz canonical LXR mainboard tick
** ----------------------------------------------------------------------- */
#define SYSTICK_CTRL (*((volatile uint32_t *)0xE000E010UL))
#define SYSTICK_LOAD (*((volatile uint32_t *)0xE000E014UL))
#define SYSTICK_VAL  (*((volatile uint32_t *)0xE000E018UL))

/* -----------------------------------------------------------------------
** State
** ----------------------------------------------------------------------- */
volatile uint16_t time_sysTick = 0;
static volatile uint8_t frontpanel_service_due = 0;

/* systick_ticks — 32-bit canonical LXR mainboard counter. It advances at
** 4kHz from SysTick, matching the reference firmware's 0.25ms timebase.
** Keep front-panel millisecond code on time_sysTick below. */
volatile uint32_t systick_ticks = 0;

/* -----------------------------------------------------------------------
** TIM6 ISR — 1kHz front-panel/service heartbeat
**
** Keep the ISR small: it owns the millisecond counters and schedules the
** heavier front-panel scan for foreground service at 500Hz. This leaves more
** uninterrupted foreground time for DSP render while preserving 1ms UI timers.
** ----------------------------------------------------------------------- */
void TIM6_DAC_IRQHandler(void)
{
    TIM6_SR = 0;  /* clear update flag */

    time_sysTick++;
    screensaver_timer++;

    if ((time_sysTick & 1u) == 0u && frontpanel_service_due < 2u)
        frontpanel_service_due++;
}

void timebase_serviceFrontPanel(void)
{
    uint32_t gpiob_idr;
    uint32_t gpiod_idr;

    if (frontpanel_service_due == 0u)
        return;

    __asm volatile("cpsid i" ::: "memory");
    frontpanel_service_due = 0u;
    __asm volatile("cpsie i" ::: "memory");

    /* LED/button SPI exchange on SPI1 (PA7/PA6/PB3/PB2).
    ** SD card is on separate bit-bang pins (PC12/PD2/PC8/PD0). */
    dout_latch();
    din_dout_exchange();

    /* Output-jack detect is retained state, not edge timing. Refresh both
    ** banks here at the same 500Hz rate as the front-panel service. */
    gpiob_idr = GPIOB_IDR;
    gpiod_idr = GPIOD_IDR;
    mixer_setOutJackDetectPD((uint8_t)((gpiod_idr >> 6) & 1u),
                             (uint8_t)((gpiod_idr >> 7) & 1u));
    mixer_setOutJackDetectPB((uint8_t)((gpiob_idr >> 4) & 1u),
                             (uint8_t)((gpiob_idr >> 6) & 1u));

    encoder_tick();
    endlessPots_tick();
}

/* -----------------------------------------------------------------------
** TIM7 ISR — 5kHz LCD state machine
**
** Delegates entirely to lcd_tim7_tick() in lcd.c.
** Only touches GPIOE (PE7-PE12). No shared resources with TIM6.
** TIM7 registers are defined in lcd.c; we only need SR here.
** ----------------------------------------------------------------------- */
#define TIM7_SR (*((volatile uint32_t *)0x40001410UL))  /* TIM7_BASE+0x10 */

void TIM7_IRQHandler(void)
{
    TIM7_SR = 0;  /* clear update interrupt flag */
    lcd_tim7_tick();
}

/* TIM8_TRG_COM_TIM14_IRQHandler — unused, vector points to Default_Handler */

/* -----------------------------------------------------------------------
** SysTick handler — 4kHz
** systick_ticks is the canonical mainboard quarter-ms counter. lcd_ms_ticks
** is derived for lcd_init() blocking delays before TIM7 is running.
** ----------------------------------------------------------------------- */
volatile uint32_t lcd_ms_ticks = 0;
void SysTick_Handler(void)
{
    static uint8_t lcd_ms_divider = 0;

    systick_ticks++;

    lcd_ms_divider++;
    if (lcd_ms_divider >= SYSTICK_TICKS_PER_MS) {
        lcd_ms_divider = 0;
        lcd_ms_ticks++;
    }
}

/* -----------------------------------------------------------------------
** time_initTimer
** ----------------------------------------------------------------------- */
void time_initSysTick(void)
{
    /* SysTick at canonical 4kHz — must be called before lcd_init() so
    ** lcd_ms_ticks can be derived for blocking display init delays. */
    SYSTICK_LOAD = (SYSCLK_HZ / SYSTICK_HZ) - 1;
    SYSTICK_VAL  = 0;
    SYSTICK_CTRL = 0x07;  /* enable, tickint, processor clock */
}

void time_initTimer(void)
{
    /* TIM2 is deliberately initialised as a shared, free-running timestamp
    ** base before any later MIDI/trigger code can try to sample an edge.
    ** Do not add a TIM2 IRQ or reset CNT on external clock pulses; Phase 4+
    ** consumers need one monotonic counter for MIDI and trigger-jack events. */
    RCC_APB1ENR |= (1UL << 0);
    (void)RCC_APB1ENR;
    TIM2_CR1 = 0;
    TIM2_PSC = 107;          /* 108MHz / 108 = 1MHz */
    TIM2_ARR = 0xFFFFFFFFUL;
    TIM2_CNT = 0;
    TIM2_EGR = 1;            /* load PSC/ARR */
    TIM2_CR1 = 1;            /* CEN */

    /* TIM6 at 1kHz front-panel/service rate */
    RCC_APB1ENR |= (1UL << 4);
    (void)RCC_APB1ENR;

    TIM6_PSC  = 107;    /* PCLK1=54MHz, timer clock=108MHz, /108 = 1MHz */
    TIM6_ARR  = 999;    /* 1MHz / 1000 = 1kHz */
    TIM6_EGR  = 1;      /* UG: force update to load PSC/ARR */
    TIM6_SR   = 0;
    TIM6_DIER = 1;      /* UIE */
    TIM6_CR1  = 1;      /* CEN */

    /* Low priority: the ISR is now only counters + a foreground due flag. */
    NVIC_IPR(IRQ_TIM6_DAC) = 6u << 4;
    NVIC_ISER1 |= (1UL << (IRQ_TIM6_DAC - 32));
}

uint32_t timebase_tim2Now(void)
{
    return TIM2_CNT;
}

uint32_t timebase_tim2Delta(uint32_t newer, uint32_t older)
{
    return (uint32_t)(newer - older);
}
