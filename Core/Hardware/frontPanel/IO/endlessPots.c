/*
 * Core/Hardware/frontPanel/IO/endlessPots.c
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
 * endlessPots.c
 *
 * Analog endless pots RV1-RV4 — atan2-based sine/cosine tracking.
 *
 * Each endless pot outputs two sine waves 90° apart, centred at ~1.67V
 * (ADC midpoint 2048), amplitude ~2048 counts, one electrical cycle
 * per full 360° mechanical revolution.
 *
 * Algorithm:
 *   angle = atan2f(A - 2048, B - 2048)
 *   delta_angle = angle - prev_angle  (with wrap correction)
 *   accumulator += delta_angle * SCALE_FACTOR
 *   emit integer part of accumulator as delta
 *
 * SCALE_FACTOR = 200 / (2*pi) ≈ 31.83
 * => 200 integer increments per full revolution
 * => CW rotation = positive delta (verified against measured signal data)
 *
 * Runs in TIM6 ISR (1kHz) via endlessPots_tick().
 * Uses Cortex-M7 FPV5-D16 hardware FPU — atan2f costs ~0.14us per call,
 * 4 endless pots = ~0.56us, negligible against 1000us TIM6 budget.
 *
 * Output: endlessPots_getDelta(i) returns signed integer delta since last
 * call, self-clearing. Caller accumulates into whatever parameter range
 * it needs — no clamping here. For 0-255 menu parameters the menu enables
 * per-pot double angular scale, so one delta is emitted after half the
 * normal angular travel without blindly turning +1 into +2.
 *
 * Delta width note: the public delta is int32_t, while the current menu path
 * consumes small per-main-loop deltas. The RV1-RV4 hardware is not fast enough
 * to produce a problematic single-pass delta in normal use, but if main-loop
 * latency grows enough to accumulate very large values, revisit the caller.
 */

#include "endlessPots.h"
#include "config.h"
#include <math.h>

/* -----------------------------------------------------------------------
** Registers — unchanged from original
** ----------------------------------------------------------------------- */
#define RCC_AHB1ENR  (*((volatile uint32_t *)0x40023830UL))
#define RCC_APB2ENR  (*((volatile uint32_t *)0x40023844UL))

#define GPIOA_BASE   0x40020000UL
#define GPIOB_BASE   0x40020400UL
#define GPIOC_BASE   0x40020800UL
#define MODER(b)     (*((volatile uint32_t *)((b)+0x00)))
#define PUPDR(b)     (*((volatile uint32_t *)((b)+0x0C)))

#define ADC1_BASE    0x40012000UL
#define ADC1_CR1     (*((volatile uint32_t *)(ADC1_BASE+0x04)))
#define ADC1_CR2     (*((volatile uint32_t *)(ADC1_BASE+0x08)))
#define ADC1_SMPR1   (*((volatile uint32_t *)(ADC1_BASE+0x0C)))
#define ADC1_SMPR2   (*((volatile uint32_t *)(ADC1_BASE+0x10)))
#define ADC1_SQR1    (*((volatile uint32_t *)(ADC1_BASE+0x2C)))
#define ADC1_SQR2    (*((volatile uint32_t *)(ADC1_BASE+0x30)))
#define ADC1_SQR3    (*((volatile uint32_t *)(ADC1_BASE+0x34)))
#define ADC1_DR      (*((volatile uint32_t *)(ADC1_BASE+0x4C)))
#define ADC_CCR      (*((volatile uint32_t *)0x40012304UL))

#define DMA2_BASE    0x40026400UL
#define DMA2_LIFCR   (*((volatile uint32_t *)(DMA2_BASE+0x08)))
#define DMA2_S0CR    (*((volatile uint32_t *)(DMA2_BASE+0x10)))
#define DMA2_S0NDTR  (*((volatile uint32_t *)(DMA2_BASE+0x14)))
#define DMA2_S0PAR   (*((volatile uint32_t *)(DMA2_BASE+0x18)))
#define DMA2_S0M0AR  (*((volatile uint32_t *)(DMA2_BASE+0x1C)))

/* -----------------------------------------------------------------------
** Shared ADC DMA buffer (also used by adcPots.c)
** ----------------------------------------------------------------------- */
volatile uint16_t adc_dma_buf[ADC_DMA_BUF_LEN] __attribute__((section(".dma_nocache")));

/* -----------------------------------------------------------------------
** Scale factor: increments per radian
** 200 increments per revolution / (2 * pi) radians per revolution
** ----------------------------------------------------------------------- */
#define SCALE_FACTOR  31.830988f   /* 200 / (2*pi) */
#define TWO_PI        6.2831853f

/* -----------------------------------------------------------------------
** ADC index lookup tables — replaces compound-literal ADC_IDX_ENDLESS_A/B
** macros that would allocate a fresh 4-byte array on each call.
** Values identical: pot 0=RV1, 1=RV2, 2=RV3, 3=RV4.
** ----------------------------------------------------------------------- */
static const uint8_t endless_idx_a[ENDLESS_POT_COUNT] = {
    ADC_IDX_RV1A, ADC_IDX_RV2A, ADC_IDX_RV3A, ADC_IDX_RV4A
};
static const uint8_t endless_idx_b[ENDLESS_POT_COUNT] = {
    ADC_IDX_RV1B, ADC_IDX_RV2B, ADC_IDX_RV3B, ADC_IDX_RV4B
};

/* -----------------------------------------------------------------------
** Endless-pot state.
**
** ISR-only fields (prev_angle, accumulator, initialised) are NOT volatile.
** They are written and read only inside the TIM6 ISR. Volatile here would
** force the compiler to spill them to memory between every statement,
** preventing register caching of the accumulator math across the loop.
**
** Cross-thread fields (delta, changed) are read by the main thread via
** getDelta()/hasChanged(). Those remain volatile, accessed under
** interrupts-disabled in getDelta() to make the int32 read+clear atomic.
**
** Behavioural equivalence to the previous all-volatile version: identical.
** Same arithmetic, same threshold, same emission. The only difference is
** the compiler is now allowed to keep intermediates in registers.
** ----------------------------------------------------------------------- */
typedef struct {
    /* ISR-only — non-volatile, register-cacheable */
    float   prev_angle;
    float   accumulator;
    uint16_t baseline_a;
    uint16_t baseline_b;
    uint16_t baseline_ticks;
    uint16_t delta_idle_ticks;
    uint8_t delta_seen;
    uint8_t active;
    uint8_t initialised;
    /* Cross-thread — volatile */
    volatile int32_t delta;
    volatile uint8_t changed;
    volatile uint8_t double_speed;
} EndlessPots;

static EndlessPots endless[ENDLESS_POT_COUNT];

static uint16_t endless_absDiff(uint16_t a, uint16_t b)
{
    return (a > b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static float endless_angle(uint16_t raw_a, uint16_t raw_b)
{
    float a = (float)raw_a - 2048.0f;
    float b = (float)raw_b - 2048.0f;

    /* Swap A and B to atan2f for correct physical rotation: CW=+ */
    return atan2f(b, a);
}

static uint8_t endless_outsideDeadzone(const EndlessPots *pot,
                                       uint16_t raw_a,
                                       uint16_t raw_b)
{
    return (endless_absDiff(raw_a, pot->baseline_a) > ENDLESS_POT_DEADZONE) ||
           (endless_absDiff(raw_b, pot->baseline_b) > ENDLESS_POT_DEADZONE);
}

static void endless_snapshotUnlocked(uint8_t i,
                                     uint16_t raw_a,
                                     uint16_t raw_b,
                                     float angle,
                                     uint8_t clear_pending)
{
    EndlessPots *pot = &endless[i];
    pot->prev_angle  = angle;
    pot->accumulator = 0.0f;
    pot->baseline_a  = raw_a;
    pot->baseline_b  = raw_b;
    pot->baseline_ticks = 0;
    pot->delta_idle_ticks = 0;
    pot->delta_seen = 0;
    pot->active      = 0;
    if (clear_pending) {
        pot->delta   = 0;
        pot->changed = 0;
    }
    if (!pot->initialised) {
        pot->initialised = 1;
    }
}

/* -----------------------------------------------------------------------
** endlessPots_tick — called from TIM6 ISR at 1kHz
**
** Hot loop: copies struct fields to local floats, runs the math against
** locals (compiler keeps them in FPU registers), writes back the few
** updated fields. Reduces volatile memory traffic ~4×.
**
** Movement is ignored until raw A or B leaves ENDLESS_POT_DEADZONE from the
** latest baseline. ENDLESS_POT_TIMEOUT_MS is a long naive rebaseline while
** moving. ENDLESS_POT_DELTA_TIMEOUT_MS is a short recenter after the last
** emitted delta only, so very slow pre-delta movement can still accumulate.
** A pre-delta false start that falls back inside the deadzone is cancelled
** before its angle noise can accumulate into a parameter edit.
** ----------------------------------------------------------------------- */
void endlessPots_tick(void)
{
    for (int i = 0; i < ENDLESS_POT_COUNT; i++) {
        EndlessPots *pot = &endless[i];
        uint16_t raw_a = adc_dma_buf[endless_idx_a[i]];
        uint16_t raw_b = adc_dma_buf[endless_idx_b[i]];
        float angle = endless_angle(raw_a, raw_b);

        if (!pot->initialised) {
            endless_snapshotUnlocked((uint8_t)i, raw_a, raw_b, angle, 1);
            continue;
        }

        if (!pot->active) {
            pot->prev_angle  = angle;
            pot->accumulator = 0.0f;
            if (endless_outsideDeadzone(pot, raw_a, raw_b)) {
                pot->active = 1;
                pot->baseline_ticks = 0;
                pot->delta_idle_ticks = 0;
                pot->delta_seen = 0;
            }
            continue;
        }

        if (!pot->delta_seen &&
            !endless_outsideDeadzone(pot, raw_a, raw_b)) {
            endless_snapshotUnlocked((uint8_t)i, raw_a, raw_b, angle, 0);
            continue;
        }

        float d = angle - pot->prev_angle;
        if      (d >  3.14159265f) d -= TWO_PI;
        else if (d < -3.14159265f) d += TWO_PI;

        float scale = pot->double_speed ? (SCALE_FACTOR * 2.0f) : SCALE_FACTOR;
        float acc = pot->accumulator + d * scale;
        int32_t inc = (int32_t)acc;

        pot->prev_angle  = angle;
        pot->accumulator = acc - (float)inc;

        if (inc != 0) {
            pot->delta  += inc;
            pot->changed = 1;
            pot->delta_idle_ticks = 0;
            pot->delta_seen = 1;
        } else if (pot->delta_seen &&
                   pot->delta_idle_ticks >= ENDLESS_POT_DELTA_TIMEOUT_MS) {
            endless_snapshotUnlocked((uint8_t)i, raw_a, raw_b, angle, 0);
        } else if (pot->delta_seen) {
            pot->delta_idle_ticks++;
        }

        if (pot->active) {
            if (pot->baseline_ticks >= ENDLESS_POT_TIMEOUT_MS)
                endless_snapshotUnlocked((uint8_t)i, raw_a, raw_b, angle, 0);
            else
                pot->baseline_ticks++;
        }
    }
}

/* -----------------------------------------------------------------------
** Public API
** ----------------------------------------------------------------------- */

/* Returns signed integer delta since last call. Self-clearing.
** Positive = CW, negative = CCW. No clamping — caller decides range. */
int32_t endlessPots_getDelta(uint8_t i)
{
    if (i >= ENDLESS_POT_COUNT) return 0;
    __asm volatile("cpsid i" ::: "memory");
    int32_t d = endless[i].delta;
    endless[i].delta   = 0;
    endless[i].changed = 0;
    __asm volatile("cpsie i" ::: "memory");
    return d;
}

/* Returns 1 if endless pot has moved since last getDelta(), 0 otherwise.
** Does NOT clear the delta — call getDelta() to consume it. */
uint8_t endlessPots_hasChanged(uint8_t i)
{
    if (i >= ENDLESS_POT_COUNT) return 0;
    return endless[i].changed;
}

void endlessPots_snapshot(uint8_t i)
{
    if (i >= ENDLESS_POT_COUNT) return;

    uint16_t raw_a = adc_dma_buf[endless_idx_a[i]];
    uint16_t raw_b = adc_dma_buf[endless_idx_b[i]];
    float angle = endless_angle(raw_a, raw_b);

    __asm volatile("cpsid i" ::: "memory");
    endless_snapshotUnlocked(i, raw_a, raw_b, angle, 1);
    __asm volatile("cpsie i" ::: "memory");
}

void endlessPots_snapshotAll(void)
{
    uint16_t raw_a[ENDLESS_POT_COUNT];
    uint16_t raw_b[ENDLESS_POT_COUNT];
    float angle[ENDLESS_POT_COUNT];

    for (uint8_t i = 0; i < ENDLESS_POT_COUNT; i++) {
        raw_a[i] = adc_dma_buf[endless_idx_a[i]];
        raw_b[i] = adc_dma_buf[endless_idx_b[i]];
        angle[i] = endless_angle(raw_a[i], raw_b[i]);
    }

    __asm volatile("cpsid i" ::: "memory");
    for (uint8_t i = 0; i < ENDLESS_POT_COUNT; i++)
        endless_snapshotUnlocked(i, raw_a[i], raw_b[i], angle[i], 1);
    __asm volatile("cpsie i" ::: "memory");
}

void endlessPots_setDouble(uint8_t i, uint8_t enabled)
{
    if (i >= ENDLESS_POT_COUNT) return;
    __asm volatile("cpsid i" ::: "memory");
    endless[i].double_speed = (uint8_t)(enabled != 0u);
    __asm volatile("cpsie i" ::: "memory");
}

/* -----------------------------------------------------------------------
** Hardware init — unchanged from original (ADC + DMA setup)
** ----------------------------------------------------------------------- */
void endlessPots_init(void)
{
    RCC_AHB1ENR |= (1UL<<0)|(1UL<<1)|(1UL<<2)|(1UL<<22); /* GPIOA,B,C,DMA2 */
    (void)RCC_AHB1ENR;
    RCC_APB2ENR |= (1UL<<8);  /* ADC1 */
    (void)RCC_APB2ENR;

    /* PA0-PA5: analog */
    MODER(GPIOA_BASE) |= (3UL<<0)|(3UL<<2)|(3UL<<4)|(3UL<<6)|(3UL<<8)|(3UL<<10);
    PUPDR(GPIOA_BASE) &=~((3UL<<0)|(3UL<<2)|(3UL<<4)|(3UL<<6)|(3UL<<8)|(3UL<<10));

    /* PB0, PB1: analog */
    MODER(GPIOB_BASE) |= (3UL<<0)|(3UL<<2);
    PUPDR(GPIOB_BASE) &=~((3UL<<0)|(3UL<<2));

    /* PC0-PC5: analog */
    MODER(GPIOC_BASE) |= (3UL<<0)|(3UL<<2)|(3UL<<4)|(3UL<<6)|(3UL<<8)|(3UL<<10);
    PUPDR(GPIOC_BASE) &=~((3UL<<0)|(3UL<<2)|(3UL<<4)|(3UL<<6)|(3UL<<8)|(3UL<<10));

    /* ADC common: /4 prescaler -> 4MHz */
    ADC_CCR = (1UL<<16);

    ADC1_CR2 = 0;
    ADC1_CR1 = (1UL<<8);                         /* SCAN */
    ADC1_CR2 = (1UL<<1)|(1UL<<8)|(1UL<<9);       /* CONT, DMA, DDS */

    /* 480-cycle sample time for all channels */
    ADC1_SMPR2 = (7UL<<0)|(7UL<<3)|(7UL<<6)|(7UL<<9)|(7UL<<12)|(7UL<<15)|
                 (7UL<<24)|(7UL<<27);
    ADC1_SMPR1 = (7UL<<0)|(7UL<<3)|(7UL<<6)|(7UL<<9)|(7UL<<12)|(7UL<<15);

    /* 14-channel scan sequence (unchanged) */
    ADC1_SQR3 = (0UL<<0)|(1UL<<5)|(2UL<<10)|(3UL<<15)|(4UL<<20)|(5UL<<25);
    ADC1_SQR2 = (8UL<<0)|(9UL<<5)|(10UL<<10)|(11UL<<15)|(12UL<<20)|(13UL<<25);
    ADC1_SQR1 = (13UL<<20) | (14UL<<0) | (15UL<<5);

    /* DMA2 Stream0, channel 0, circular */
    DMA2_S0CR = 0; while (DMA2_S0CR & 1);
    DMA2_LIFCR = 0x3F;
    DMA2_S0PAR  = (uint32_t)&ADC1_DR;
    DMA2_S0M0AR = (uint32_t)adc_dma_buf;
    DMA2_S0NDTR = ADC_DMA_BUF_LEN;
    DMA2_S0CR = (0UL<<25) |  /* channel 0 */
                (1UL<<13) |  /* MSIZE 16-bit */
                (1UL<<11) |  /* PSIZE 16-bit */
                (1UL<<10) |  /* MINC */
                (1UL<<8)  |  /* CIRC */
                (1UL<<0);    /* EN */

    /* Enable ADC, stabilise, start */
    ADC1_CR2 |= (1UL<<0);
    volatile uint32_t t = 10000; while(t--);
    ADC1_CR2 |= (1UL<<30);  /* SWSTART */

    /* Endless-pot state cleared — initialised on first tick */
    for (int i = 0; i < ENDLESS_POT_COUNT; i++) {
        endless[i].prev_angle  = 0.0f;
        endless[i].accumulator = 0.0f;
        endless[i].baseline_a  = 0;
        endless[i].baseline_b  = 0;
        endless[i].baseline_ticks = 0;
        endless[i].delta_idle_ticks = 0;
        endless[i].delta_seen   = 0;
        endless[i].active      = 0;
        endless[i].delta       = 0;
        endless[i].initialised = 0;
        endless[i].changed     = 0;
        endless[i].double_speed = 0;
    }
}
