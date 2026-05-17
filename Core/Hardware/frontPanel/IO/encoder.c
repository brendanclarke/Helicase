/*
 * Core/Hardware/frontPanel/IO/encoder.c
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
 * encoder.c
 *
 * Main rotary encoder — TIM1 Input Capture on CH3/CH4 with hardware
 * digital filter (ICxF=0xF). IRQ27 TIM1_CC fires on filtered edges.
 *
 * ISR: Dannegger difference algorithm, stores timestamp of each accepted
 * transition into an 8-entry circular buffer. No velocity computation
 * in the ISR.
 *
 * encode_read4(): reads raw Dannegger accumulator, computes average
 * interval over last 8 transitions, applies acceleration multiplier.
 * Velocity decays to zero if no transition for >100ms — prevents stale
 * fast-spin velocity from accelerating subsequent slow movement.
 *
 * Acceleration: 1× at ≤10 inc/sec (interval ≥25ms per transition),
 * 4× at max speed (~80 inc/sec, interval ≤3ms). Linear between.
 * Applied symmetrically to CW and CCW.
 *
 * Hardware (confirmed):
 *   PE13 (A): 1kΩ series (R73), 10kΩ pull-up (R75). No internal pull.
 *   PE14 (B): 1kΩ series (R74), 10kΩ pull-up (R76). No internal pull.
 *   PE15 (SW): internal pull-up only.
 *
 * encode_read4() is the only valid read function for LXR-02 SW42.
 * read1/read2 removed permanently.
 */

#include "encoder.h"

/* -----------------------------------------------------------------------
** Registers — GPIO
** ----------------------------------------------------------------------- */
#define RCC_AHB1ENR    (*((volatile uint32_t *)0x40023830UL))
#define RCC_APB2ENR    (*((volatile uint32_t *)0x40023844UL))
#define GPIOE_BASE     0x40021000UL
#define GPIOE_MODER    (*((volatile uint32_t *)(GPIOE_BASE+0x00)))
#define GPIOE_PUPDR    (*((volatile uint32_t *)(GPIOE_BASE+0x0C)))
#define GPIOE_IDR      (*((volatile uint32_t *)(GPIOE_BASE+0x10)))
#define GPIOE_AFRH     (*((volatile uint32_t *)(GPIOE_BASE+0x24)))

/* -----------------------------------------------------------------------
** Registers — TIM1
** ----------------------------------------------------------------------- */
#define TIM1_BASE      0x40010000UL
#define TIM1_CR1       (*((volatile uint32_t *)(TIM1_BASE+0x00)))
#define TIM1_DIER      (*((volatile uint32_t *)(TIM1_BASE+0x0C)))
#define TIM1_SR        (*((volatile uint32_t *)(TIM1_BASE+0x10)))
#define TIM1_CCMR2     (*((volatile uint32_t *)(TIM1_BASE+0x1C)))
#define TIM1_CCER      (*((volatile uint32_t *)(TIM1_BASE+0x20)))
#define TIM1_PSC       (*((volatile uint32_t *)(TIM1_BASE+0x28)))
#define TIM1_ARR       (*((volatile uint32_t *)(TIM1_BASE+0x2C)))
#define TIM1_BDTR      (*((volatile uint32_t *)(TIM1_BASE+0x44)))

/* -----------------------------------------------------------------------
** Registers — NVIC
** ----------------------------------------------------------------------- */
#define NVIC_ISER0     (*((volatile uint32_t *)0xE000E100UL))
#define NVIC_IPR(n)    (*((volatile uint8_t  *)(0xE000E400UL+(n))))
#define IRQ_TIM1_CC    27

/* -----------------------------------------------------------------------
** Acceleration constants
** ----------------------------------------------------------------------- */
#define ACCEL_BUF_SIZE   8      /* number of timestamps averaged */
#define ACCEL_SLOW_MS    25     /* interval (ms) at/above which mult=1× */
#define ACCEL_FAST_MS    3      /* interval (ms) at/below which mult=4× */
#define ACCEL_DECAY_MS   100    /* ms since last transition → velocity reset */
#define ACCEL_MAX_MULT   4      /* maximum multiplier */

/* -----------------------------------------------------------------------
** State
** ----------------------------------------------------------------------- */
volatile int8_t  enc_delta   = 0;
static   int8_t  last        = 0;
static   uint8_t last_button = 0;
static   uint8_t button_val  = 0;
static volatile int8_t enc_last_step = 0;

/* Circular buffer of timestamps (ms) for last ACCEL_BUF_SIZE transitions.
** Written by ISR, read by encode_read4(). Parallel ts_dirs[] stores the
** direction (+1/-1) of each transition — used to detect mechanical
** rebound (a sustained spin in one direction followed by a brief flick
** in the opposite direction at the moment the user stops). Without
** rebound suppression, those flicks get amplified by the velocity
** multiplier into spurious 4× counts in the wrong direction. */
static volatile uint16_t ts_buf[ACCEL_BUF_SIZE];
static volatile int8_t   ts_dirs[ACCEL_BUF_SIZE];
static volatile uint8_t  ts_head = 0;   /* next write index */
static volatile uint8_t  ts_count = 0;  /* number of valid entries, 0..8 */

extern volatile uint16_t time_sysTick;

/* -----------------------------------------------------------------------
** encode_init
** ----------------------------------------------------------------------- */
void encode_init(void)
{
    RCC_AHB1ENR |= (1UL << 4);
    (void)RCC_AHB1ENR;
    RCC_APB2ENR |= (1UL << 0);
    (void)RCC_APB2ENR;

    /* PE13/PE14: AF1 (TIM1_CH3/CH4), no internal pull — external 10kΩ.
    ** PE15: input, internal pull-up. */
    GPIOE_MODER &= ~((3UL<<26)|(3UL<<28)|(3UL<<30));
    GPIOE_MODER |=  ((2UL<<26)|(2UL<<28));
    GPIOE_PUPDR &= ~((3UL<<26)|(3UL<<28)|(3UL<<30));
    GPIOE_PUPDR |=  (1UL<<30);

    GPIOE_AFRH &= ~((0xFUL<<20)|(0xFUL<<24));
    GPIOE_AFRH |=  ((1UL<<20)|(1UL<<24));

    TIM1_PSC   = 0;
    TIM1_ARR   = 0xFFFF;
    TIM1_CCMR2 = (0x1UL<<0)|(0xFUL<<4)|(0x1UL<<8)|(0xFUL<<12);
    TIM1_CCER  = (1UL<<8)|(1UL<<9)|(1UL<<11)|
                 (1UL<<12)|(1UL<<13)|(1UL<<15);
    TIM1_BDTR  = (1UL<<15);
    TIM1_DIER  = (1UL<<3)|(1UL<<4);

    /* Seed with current Gray code state*/
    uint32_t idr = GPIOE_IDR;
    int8_t new = 0;
    if (idr & (1UL<<13)) new = 3;
    if (idr & (1UL<<14)) new ^= 1;
    last        = (int8_t)(new & 3);
    enc_delta   = 0;
    enc_last_step = 0;
    last_button = 0;
    button_val  = 0;

    /* Seed timestamp buffer with current time so first read doesn't
    ** see garbage intervals. Direction buffer cleared to zero (no opinion
    ** until real transitions arrive). */
    for (uint8_t i = 0; i < ACCEL_BUF_SIZE; i++) {
        ts_buf[i]  = time_sysTick;
        ts_dirs[i] = 0;
    }
    ts_head  = 0;
    ts_count = 0;

    TIM1_CR1 = (1UL<<0);

    NVIC_IPR(IRQ_TIM1_CC) = 1 << 4;
    NVIC_ISER0 |= (1UL << IRQ_TIM1_CC);
}

/* -----------------------------------------------------------------------
** encoder_tim14_tick — empty stub
** ----------------------------------------------------------------------- */
void encoder_tim14_tick(void) {}

/* -----------------------------------------------------------------------
** TIM1_CC_IRQHandler — fires on filtered edge of PE13/PE14
**
** Runs Dannegger. On accepted transition: accumulates enc_delta and
** stores current timestamp in circular buffer. No velocity computation.
** ----------------------------------------------------------------------- */
void TIM1_CC_IRQHandler(void)
{
    TIM1_SR = ~((1UL<<3)|(1UL<<4));

    uint32_t idr = GPIOE_IDR;
    int8_t new = 0;
    if (idr & (1UL<<13)) new = 3;
    if (idr & (1UL<<14)) new ^= 1;

    int8_t diff = (int8_t)(last - new);
    if (diff & 1) {
        last      = new;
        int8_t step = (int8_t)((diff & 2) - 1);   /* +1 or -1 */

        /* If the UI read side leaves a sub-detent residue (+/-1..3) and
        ** the user reverses direction, the next detent can otherwise be
        ** spent cancelling stale residue. That feels like the encoder needs
        ** two clicks after a direction change. Preserve complete pending
        ** counts, but discard only partial residue when travel reverses. */
        if (enc_last_step != 0 && step != enc_last_step) {
            if (enc_delta > -4 && enc_delta < 4)
                enc_delta = 0;
            ts_count = 0;
        }
        enc_last_step = step;
        enc_delta = (int8_t)(enc_delta + step);

        /* Store timestamp + direction of this accepted transition */
        ts_buf[ts_head]  = time_sysTick;
        ts_dirs[ts_head] = step;
        ts_head = (uint8_t)((ts_head + 1) & (ACCEL_BUF_SIZE - 1));
        if (ts_count < ACCEL_BUF_SIZE) ts_count++;
    }
}

/* -----------------------------------------------------------------------
** encoder_tick — TIM6 1kHz, button debounce only
** ----------------------------------------------------------------------- */
void encoder_tick(void)
{
    uint32_t idr = GPIOE_IDR;
    uint8_t btn = (uint8_t)((~idr >> 15) & 1);
    if (btn == last_button)
        button_val = btn;
    last_button = btn;
}

/* -----------------------------------------------------------------------
** encode_read4 — Dannegger read with velocity-averaged acceleration.
**
** Reads raw accumulator (verbatim from original LXR), then computes
** average interval over last 8 transitions. If the most recent transition
** was >100ms ago, velocity is treated as zero (no acceleration).
** Multiplier applied to reported count, symmetrically for ±.
** ----------------------------------------------------------------------- */
int8_t encode_read4(void)
{
    int8_t val;
    uint16_t buf_copy[ACCEL_BUF_SIZE];
    int8_t   dir_copy[ACCEL_BUF_SIZE];
    uint8_t  head_copy, count_copy;

    /* Atomically read accumulator, divide-by-4 (round toward zero, NOT
    ** floor), and snapshot timestamp + direction buffers.
    **
    ** Why explicit round-toward-zero:
    **
    ** ARM's `>> 2` on int8_t is arithmetic shift = FLOOR division for
    ** negative values:  -1>>2 = -1,  -3>>2 = -1,  -5>>2 = -2.
    ** This made tiny CCW jiggles fire a -1 count (val=-1 → returns -1,
    ** leaves +3 residue in enc_delta) while equivalent CW jiggles (val=+1)
    ** correctly didn't fire (returned 0). User-visible asymmetry:
    **   - tiny CCW wiggle decremented and bounced back via the +3 residue
    **   - param-at-zero CCW wiggle clamped at 0 then bounced to 1
    **   - sustained CCW produced ~25% more counts than equivalent CW
    **
    ** Round-toward-zero is symmetric: both directions need exactly 4
    ** transitions to fire one count, and the residue carries the same
    ** sign as val so the accumulator semantics stay consistent.
    **
    ** Bug history (do not regress):
    **   Session 4 tried to compensate by seeding `last = (new+3)&3`.
    **   That actually loses the first physical edge AND produces a
    **   wrong-direction click on motion onset. Session 5 reverted to
    **   `last = new & 3` (the correct seed). The asymmetry survived
    **   because it lives in encode_read4's divide, not in the seed.
    **   Keep the current seed; do not "phase offset" it. */
    __asm volatile("cpsid i" ::: "memory");
    val = enc_delta;
    {
        /* int16_t intermediates to dodge any int8_t -val overflow risk
        ** (val can never reach -128 in practice, but paranoia is cheap). */
        int16_t v16 = (int16_t)val;
        int16_t mag, res;
        if (v16 >= 0) {
            mag = (int16_t)(v16 >> 2);
            res = (int16_t)(v16 & 3);
        } else {
            int16_t pos = (int16_t)(-v16);
            mag = (int16_t)(-(pos >> 2));
            res = (int16_t)(-(pos & 3));
        }
        enc_delta = (int8_t)res;
        val       = (int8_t)mag;
    }
    head_copy  = ts_head;
    count_copy = ts_count;
    for (uint8_t i = 0; i < ACCEL_BUF_SIZE; i++) {
        buf_copy[i] = ts_buf[i];
        dir_copy[i] = ts_dirs[i];
    }
    __asm volatile("cpsie i" ::: "memory");

    if (!val) return 0;

    /* Decay check: if no transition for >100ms, no acceleration */
    uint8_t last_idx = (uint8_t)((head_copy - 1) & (ACCEL_BUF_SIZE - 1));
    uint16_t age = (uint16_t)(time_sysTick - buf_copy[last_idx]);
    if (age > ACCEL_DECAY_MS || count_copy < 2) return val;

    /* Rebound detection: the buffer holds the last N transitions.
    ** When the user spins fast and stops abruptly, the encoder mechanism
    ** can produce a brief flick of opposite-direction transitions before
    ** settling. Because the buffer is still populated with fast intervals
    ** from the spin, the velocity multiplier kicks in and amplifies the
    ** flick — producing a spurious jump in the wrong direction.
    **
    ** Detect this by counting majority direction in the buffer. If the
    ** majority does NOT agree with the sign of val, the current read is
    ** a rebound: return val unaccelerated (saturating to ±1 in practice). */
    int8_t pos_count = 0, neg_count = 0;
    {
        uint8_t n = count_copy;
        uint8_t idx = (uint8_t)((head_copy - n) & (ACCEL_BUF_SIZE - 1));
        for (uint8_t i = 0; i < n; i++) {
            if (dir_copy[idx] > 0) pos_count++;
            else if (dir_copy[idx] < 0) neg_count++;
            idx = (uint8_t)((idx + 1) & (ACCEL_BUF_SIZE - 1));
        }
    }
    int8_t majority_sign = (pos_count > neg_count) ? 1 : (neg_count > pos_count) ? -1 : 0;
    int8_t val_sign      = (val > 0) ? 1 : -1;
    if (majority_sign != 0 && majority_sign != val_sign) {
        /* This read is moving against recent travel — rebound. No accel. */
        return val;
    }

    /* Compute average interval over min(count_copy, ACCEL_BUF_SIZE) transitions.
    ** Oldest entry in buffer is at head (circular), newest is at head-1.
    ** Span = newest_timestamp - oldest_timestamp, divided by (count-1) gaps. */
    uint8_t n = count_copy;   /* number of valid entries */
    uint8_t oldest_idx = (uint8_t)((head_copy - n) & (ACCEL_BUF_SIZE - 1));
    uint16_t newest_ts = buf_copy[last_idx];
    uint16_t oldest_ts = buf_copy[oldest_idx];
    uint16_t span      = (uint16_t)(newest_ts - oldest_ts);
    uint16_t avg_interval = span / (n - 1);   /* ms per transition */

    /* Clamp and compute integer multiplier */
    uint8_t mult;
    if      (avg_interval >= ACCEL_SLOW_MS) mult = 1;
    else if (avg_interval <= ACCEL_FAST_MS) mult = ACCEL_MAX_MULT;
    else    mult = (uint8_t)(1 + ((ACCEL_MAX_MULT - 1) *
                  (ACCEL_SLOW_MS - avg_interval)) /
                  (ACCEL_SLOW_MS - ACCEL_FAST_MS));

    /* Apply multiplier — val is already signed, mult is always positive */
    int8_t result = (int8_t)(val * (int8_t)mult);
    return result;
}

uint8_t encode_readButton(void)
{
    return button_val;
}
