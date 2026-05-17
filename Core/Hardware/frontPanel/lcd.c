/*
 * Core/Hardware/frontPanel/lcd.c
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
 * lcd.c
 *
 * WS0010 OLED 16×2 display, 4-bit parallel, non-blocking.
 *
 * lcd_init()      — blocking hardware init (called before TIM7 starts).
 * lcd_tim7_init() — starts TIM7 at 5kHz to drain the queue.
 * lcd_*()         — enqueue ops, return immediately, never block.
 * lcd_tim7_tick() — called from TIM7_IRQHandler every 200us.
 *
 * See lcd.h for full architecture description.
 */

#include "lcd.h"
#include "config.h"
#include <stdint.h>

/* -----------------------------------------------------------------------
** GPIO registers — GPIOE only (PE7-PE12)
** ----------------------------------------------------------------------- */
#define RCC_AHB1ENR  (*((volatile uint32_t *)0x40023830UL))
#define GPIOE_BASE   0x40021000UL
#define GPIOE_MODER  (*((volatile uint32_t *)(GPIOE_BASE+0x00)))
#define GPIOE_OTYPER (*((volatile uint32_t *)(GPIOE_BASE+0x04)))
#define GPIOE_OSPEED (*((volatile uint32_t *)(GPIOE_BASE+0x08)))
#define GPIOE_PUPDR  (*((volatile uint32_t *)(GPIOE_BASE+0x0C)))
#define GPIOE_BSRR   (*((volatile uint32_t *)(GPIOE_BASE+0x18)))

#define PE_HI(p)   (GPIOE_BSRR = (1UL << (p)))
#define PE_LO(p)   (GPIOE_BSRR = (1UL << ((p) + 16)))
#define PE_WR(p,v) do { if (v) PE_HI(p); else PE_LO(p); } while (0)

#define PIN_RS   12
#define PIN_E    11
#define PIN_DB4   7
#define PIN_DB5   8
#define PIN_DB6   9
#define PIN_DB7  10

/* Clear all display pins in one write */
#define PE_DISP_ALL_LO() \
    (GPIOE_BSRR = ((1UL<<(16+PIN_DB4))|(1UL<<(16+PIN_DB5))| \
                   (1UL<<(16+PIN_DB6))|(1UL<<(16+PIN_DB7))| \
                   (1UL<<(16+PIN_E))  |(1UL<<(16+PIN_RS))))

/* -----------------------------------------------------------------------
** TIM7 registers
** ----------------------------------------------------------------------- */
#define RCC_APB1ENR  (*((volatile uint32_t *)0x40023840UL))
#define TIM7_BASE    0x40001400UL
#define TIM7_CR1     (*((volatile uint32_t *)(TIM7_BASE+0x00)))
#define TIM7_DIER    (*((volatile uint32_t *)(TIM7_BASE+0x0C)))
#define TIM7_SR      (*((volatile uint32_t *)(TIM7_BASE+0x10)))
#define TIM7_EGR     (*((volatile uint32_t *)(TIM7_BASE+0x14)))
#define TIM7_PSC     (*((volatile uint32_t *)(TIM7_BASE+0x28)))
#define TIM7_ARR     (*((volatile uint32_t *)(TIM7_BASE+0x2C)))

/* NVIC — IRQ55 = TIM7, sits in ISER1 (covers IRQ32-63) */
#define NVIC_ISER1   (*((volatile uint32_t *)0xE000E104UL))
#define NVIC_IPR(n)  (*((volatile uint8_t  *)(0xE000E400UL+(n))))
#define IRQ_TIM7     55

/* -----------------------------------------------------------------------
** SysTick — used only inside lcd_init() for blocking delays
** ----------------------------------------------------------------------- */
extern volatile uint32_t lcd_ms_ticks;  /* defined in timebase.c */

static void delay_ms(uint32_t ms)
{
    uint32_t start = lcd_ms_ticks;
    while ((lcd_ms_ticks - start) < ms);
}

static void delay_us(uint32_t us)
{
    /* Spin loop — only used during lcd_init() before TIM7 is running.
    ** At 216MHz, approximately 4 cycles per iteration ≈ 18.5ns each.
    ** Multiply by ~5 to get roughly 1us per count. */
    volatile uint32_t n = us * 5;
    while (n--);
}

/* -----------------------------------------------------------------------
** char definitions — custom CGRAM defined characters {lcd_string(0x00-0x08)}
** ----------------------------------------------------------------------- */
static const uint8_t lcd_char_bell[8] = {
    0b00100, //    #
    0b01110, //   ###
    0b01110, //   ###
    0b01110, //   ###
    0b11111, //  #####
    0b00000, //  
    0b00100, //    #
    0b00000  //  
};

static const uint8_t lcd_char_pop[8] = {
    0b00000, //    
    0b00000, //   
    0b01010, //   # #
    0b00000, //   
    0b10001, //  #   #
    0b00000, //  
    0b01010, //   # #
    0b00000  //  
};

static const uint8_t lcd_char_check[8] = {
    0b00000,
    0b00000,
    0b00001,
    0b00011,
    0b10110,
    0b11100,
    0b01000,
    0b00000
};

static const uint8_t lcd_char_heart[8] = {
    0b00000, 
    0b01010, 
    0b11111, 
    0b11111, 
    0b01110, 
    0b00100, 
    0b00000, 
    0b00000
};

static const uint8_t lcd_char_ellipsis[8] = {
    0b00000, 
    0b00000, 
    0b00000, 
    0b00000, 
    0b00000, 
    0b00000, 
    0b10101, 
    0b00000
};


/* -----------------------------------------------------------------------
** Op queue
**
** Each entry is a (rs, data) pair where:
**   rs   = 0: command byte, rs = 1: data byte
**   data = byte to send
**   long_wait = 1: use 20-tick post-byte wait (CLEAR and HOME only)
**
** Producer (lcd_* functions) writes to head.
** Consumer (lcd_tim7_tick) reads from tail.
** Both indices are masked with (LCD_QUEUE_SIZE - 1).
**
** Head and tail are uint8_t; at LCD_QUEUE_SIZE=64 they never overflow
** their range before wrapping. The full/empty check uses the count.
** ----------------------------------------------------------------------- */
typedef struct {
    uint8_t rs;
    uint8_t data;
    uint8_t long_wait;  /* 1 = use LCD_TICKS_CLEAR_WAIT after this byte */
} LcdOp;

/* -----------------------------------------------------------------------
** SPSC ring buffer — single-producer (main thread), single-consumer (TIM7 ISR).
**
** Producer (lcd_enqueue) only writes lcd_q_head; only reads lcd_q_tail.
** Consumer (lcd_tim7_tick, SM_IDLE) only writes lcd_q_tail; only reads lcd_q_head.
** No shared read-modify-write target. uint8_t single-byte loads/stores on
** Cortex-M7 are atomic with respect to each other.
**
** Indices are free-running uint8_t. They wrap mod 256, but
** (uint8_t)(head - tail) gives the count modulo 256 because two's-complement
** subtraction is well-defined on uint8_t. This works as long as
** LCD_QUEUE_SIZE <= 128 (so count never reaches 256, never wraps).
**
** Empty: head == tail
** Full:  (uint8_t)(head - tail) == LCD_QUEUE_SIZE
** Index: head & (LCD_QUEUE_SIZE - 1)   (and likewise for tail)
**
** Previous code used a separate lcd_q_count variable, which both producer
** and consumer modified via count++/count--. This is a classic non-atomic
** RMW race on Cortex-M7: if TIM7 ISR fires between the producer's load and
** store of count, the consumer's decrement is silently lost when the
** producer writes back its incremented value. The race only triggers when
** producer and consumer are concurrently active — i.e. fast input that
** keeps the queue non-empty. Symptoms: phantom dequeues read stale ring
** slots and send garbage commands to the LCD, scrambling cursor position
** and causing characters to appear at wrong cells.
** ----------------------------------------------------------------------- */
static volatile LcdOp  lcd_queue[LCD_QUEUE_SIZE];
static volatile uint8_t lcd_q_head = 0;  /* producer-owned: next write position */
static volatile uint8_t lcd_q_tail = 0;  /* consumer-owned: next read  position */
static volatile uint8_t lcd_driver_busy = 0;

/* Enqueue one op. Drop if queue is full.
**
** Audio rendering runs in the main loop on this single-chip port. Blocking
** here while the LCD drains can starve the audio block refill path during
** fast knob edits, producing a high-pitched repeated-block artifact. A
** skipped LCD op is repaired by the next repaint; a missed audio block is
** audible immediately. */
static void lcd_enqueue(uint8_t rs, uint8_t data, uint8_t long_wait)
{
    if ((uint8_t)(lcd_q_head - lcd_q_tail) >= LCD_QUEUE_SIZE)
        return;

    lcd_driver_busy = 1;

    uint8_t idx = lcd_q_head & (uint8_t)(LCD_QUEUE_SIZE - 1);
    lcd_queue[idx].rs        = rs;
    lcd_queue[idx].data      = data;
    lcd_queue[idx].long_wait = long_wait;

    /* Barrier: queue contents must be visible before head advances.
    ** Consumer only dequeues entries below the head it reads. */
    __asm volatile("dmb" ::: "memory");

    lcd_q_head++;

    /* Restart TIM7 if it was stopped by SM_IDLE on a prior empty-queue exit.
    ** Writing CEN=1 to an already-running timer is a no-op, so no read-check
    ** is needed — this avoids a wakeup race where the consumer could observe
    ** CEN=1 (still draining) and skip its own stop, while we'd skip our
    ** restart. Unconditional restart is safe and ~5 cycles. */
    TIM7_CR1 = 1;
}

uint8_t lcd_queueFree(void)
{
    uint8_t used = (uint8_t)(lcd_q_head - lcd_q_tail);
    if (used >= LCD_QUEUE_SIZE)
        return 0;
    return (uint8_t)(LCD_QUEUE_SIZE - used);
}

void lcd_waitForIdle(void)
{
    while (lcd_driver_busy || (lcd_q_head != lcd_q_tail)) {
        /* TIM7 drains the queue. Only use this from foreground modal paths. */
    }
}

/* -----------------------------------------------------------------------
** GPIO helpers — used only by lcd_init() (blocking path)
** ----------------------------------------------------------------------- */
static void lcd_gpio_init(void)
{
    RCC_AHB1ENR |= (1UL << 4);  /* GPIOE clock */
    (void)RCC_AHB1ENR;           /* ensure write completes before proceeding */

    /* PE7-PE12: output, push-pull, low speed, no pull */
    GPIOE_MODER &= ~((3UL<<14)|(3UL<<16)|(3UL<<18)|(3UL<<20)|(3UL<<22)|(3UL<<24));
    GPIOE_MODER |=  ((1UL<<14)|(1UL<<16)|(1UL<<18)|(1UL<<20)|(1UL<<22)|(1UL<<24));
    GPIOE_OTYPER &= ~((1UL<<7)|(1UL<<8)|(1UL<<9)|(1UL<<10)|(1UL<<11)|(1UL<<12));
    GPIOE_OSPEED &= ~((3UL<<14)|(3UL<<16)|(3UL<<18)|(3UL<<20)|(3UL<<22)|(3UL<<24));
    GPIOE_PUPDR  &= ~((3UL<<14)|(3UL<<16)|(3UL<<18)|(3UL<<20)|(3UL<<22)|(3UL<<24));
    PE_DISP_ALL_LO();
}

static void lcd_enable_pulse(void)
{
    PE_HI(PIN_E);
    delay_us(LCD_ENABLE_US);
    PE_LO(PIN_E);
    delay_us(LCD_ENABLE_US);
}

static void lcd_write_nibble_blocking(uint8_t nibble)
{
    PE_LO(PIN_DB4); PE_LO(PIN_DB5); PE_LO(PIN_DB6); PE_LO(PIN_DB7);
    PE_WR(PIN_DB4, (nibble >> 0) & 1);
    PE_WR(PIN_DB5, (nibble >> 1) & 1);
    PE_WR(PIN_DB6, (nibble >> 2) & 1);
    PE_WR(PIN_DB7, (nibble >> 3) & 1);
    lcd_enable_pulse();
}

static void lcd_command_blocking(uint8_t data)
{
    PE_LO(PIN_RS);
    lcd_write_nibble_blocking(data >> 4);
    lcd_write_nibble_blocking(data & 0x0F);
    delay_us(50);
}

/* -----------------------------------------------------------------------
** lcd_init — blocking hardware initialisation
**
** Must be called before lcd_tim7_init().
** SysTick (time_initSysTick) must be running for delay_ms() to work.
** TIM7 is NOT running yet — all GPIO access is safe to do directly here.
** ----------------------------------------------------------------------- */
void lcd_init(void)
{
    lcd_gpio_init();
    PE_DISP_ALL_LO();
    delay_ms(LCD_BOOTUP_MS);

    /* WS0010 / HD44780 4-bit mode wake-up: 3× soft reset then switch */
    PE_LO(PIN_RS);
    lcd_write_nibble_blocking(0x03); delay_ms(LCD_SOFT_RESET_MS1);
    lcd_write_nibble_blocking(0x03); delay_ms(LCD_SOFT_RESET_MS2);
    lcd_write_nibble_blocking(0x03); delay_ms(LCD_SOFT_RESET_MS3);
    lcd_write_nibble_blocking(0x02); delay_ms(LCD_SET_4BITMODE_MS);

    lcd_command_blocking(LCD_SET_FUNCTION | LCD_FUNCTION_4BIT |
                         LCD_FUNCTION_2LINE | LCD_FUNCTION_5X7);

    /* WS0010 OLED internal charge pump */
    lcd_command_blocking(LCD_OLED_POWER);
    delay_ms(LCD_OLED_POWER_MS);

    lcd_command_blocking(LCD_SET_DISPLAY | LCD_DISPLAY_OFF |
                         LCD_CURSOR_OFF  | LCD_BLINKING_OFF);
    lcd_command_blocking(LCD_SET_DISPLAY | LCD_DISPLAY_ON |
                         LCD_CURSOR_OFF  | LCD_BLINKING_OFF);
    lcd_command_blocking(LCD_SET_ENTRY | LCD_ENTRY_INCREASE | LCD_ENTRY_NOSHIFT);

    /* Clear with blocking delay — TIM7 not yet running */
    lcd_command_blocking(LCD_CLEAR_DISPLAY);
    lcd_define_char(0, lcd_char_ellipsis);
    lcd_define_char(1, lcd_char_pop);
    lcd_define_char(2, lcd_char_check);
    lcd_define_char(3, lcd_char_heart);
    lcd_define_char(4, lcd_char_bell);
    delay_ms(2);
}

/* -----------------------------------------------------------------------
** lcd_tim7_init — start TIM7 at 5kHz
**
** Call after lcd_init() and after all other drivers are initialised.
** From this point all lcd_* calls are non-blocking (queue-based).
** ----------------------------------------------------------------------- */
void lcd_tim7_init(void)
{
    RCC_APB1ENR |= (1UL << 5);  /* TIM7EN — bit 5, cf. TIM6EN = bit 4 */
    (void)RCC_APB1ENR;

    /* Same PSC as TIM6: 108MHz timer clock / (107+1) = 1MHz tick.
    ** ARR=199: 1MHz / (199+1) = 5kHz */
    TIM7_PSC  = 107;
    TIM7_ARR  = 199;
    TIM7_EGR  = 1;   /* UG: force load of PSC and ARR */
    TIM7_SR   = 0;   /* clear any update flag raised by UG */
    TIM7_DIER = 1;   /* UIE: update interrupt enable */
    TIM7_CR1  = 1;   /* CEN: counter enable */

    /* Low priority: display drain can tolerate latency under DSP load. */
    NVIC_IPR(IRQ_TIM7) = 7u << 4;
    NVIC_ISER1 |= (1UL << (IRQ_TIM7 - 32));  /* bit 23 of ISER1 */
}

/* -----------------------------------------------------------------------
** lcd_define_char — set a custom character in LCD memory
**
** Call after lcd_init() and after all other drivers are initialised.
** Non-blocking, queue-based, 10 command queue ~4ms per character.
** ----------------------------------------------------------------------- */
void lcd_define_char(uint8_t location, const uint8_t *charmap)
{
    // 1. Set CGRAM Address. The base command is 0x40.
    // Each character occupies 8 bytes of CGRAM.
    lcd_command(0x40 + ((location & 0x07) * 8));

    // 2. Send the 8 bytes of pixel data
    for (uint8_t i = 0; i < 8; i++) {
        lcd_data(charmap[i]);
    }

    // 3. Return to DDRAM (standard display memory).
    // This ensures subsequent strings/chars don't keep writing to CGRAM.
    lcd_command(LCD_SET_DDADR); 
}

/* -----------------------------------------------------------------------
** TIM7 state machine — called from TIM7_IRQHandler every 200us
**
** Each byte transmission takes 4 ticks (800us total):
**   Tick 0: set RS + high nibble on DB4-7, raise E
**   Tick 1: lower E  (E was HIGH for 200us — min spec 450ns)
**   Tick 2: set low nibble on DB4-7, raise E
**   Tick 3: lower E, start post-byte wait countdown
**   Ticks 4+: wait countdown (1 tick normal, 20 ticks for CLEAR/HOME)
**   -> dequeue next op on tick after wait expires
**
** All GPIO writes are to GPIOE (PE7-PE12) only.
** No spin-waits. No shared resources with TIM6 or SysTick.
** ----------------------------------------------------------------------- */
void lcd_tim7_tick(void)
{
    /* Byte-level state machine */
    typedef enum {
        SM_IDLE,       /* waiting for a queue entry      */
        SM_E_HI_HIGH,  /* E raised for high nibble        */
        SM_E_LO_HIGH,  /* E lowered after high nibble     */
        SM_E_HI_LOW,   /* E raised for low nibble         */
        SM_E_LO_LOW,   /* E lowered, begin post-byte wait */
        SM_WAIT,       /* counting down post-byte ticks   */
    } SmState;

    static SmState   state     = SM_IDLE;
    static uint8_t   cur_rs    = 0;
    static uint8_t   cur_data  = 0;
    static uint8_t   wait_long = 0;
    static uint8_t   wait_cnt  = 0;

    switch (state) {

    case SM_IDLE:
        if (lcd_q_head == lcd_q_tail) {
            /* Queue empty — stop TIM7 to eliminate the steady-state
            ** ISR overhead. lcd_enqueue restarts CEN unconditionally on the
            ** next enqueue. Pending-but-not-yet-fired NVIC interrupts on
            ** TIM7 are harmless: the ISR will re-enter SM_IDLE, re-check
            ** head==tail, and re-disable. Idempotent. */
            lcd_driver_busy = 0;
            TIM7_CR1 = 0;
            return;
        }

        /* Dequeue next op. Producer-owned head is read once; consumer-owned
        ** tail is the only thing we modify here. */
        {
            uint8_t idx = lcd_q_tail & (uint8_t)(LCD_QUEUE_SIZE - 1);
            cur_rs    = lcd_queue[idx].rs;
            cur_data  = lcd_queue[idx].data;
            wait_long = lcd_queue[idx].long_wait;
            __asm volatile("dmb" ::: "memory");
            lcd_q_tail++;
        }

        /* Set RS, put high nibble on data pins, raise E */
        PE_WR(PIN_RS, cur_rs);
        PE_LO(PIN_DB4); PE_LO(PIN_DB5); PE_LO(PIN_DB6); PE_LO(PIN_DB7);
        PE_WR(PIN_DB4, (cur_data >> 4) & 1);
        PE_WR(PIN_DB5, (cur_data >> 5) & 1);
        PE_WR(PIN_DB6, (cur_data >> 6) & 1);
        PE_WR(PIN_DB7, (cur_data >> 7) & 1);
        PE_HI(PIN_E);
        state = SM_E_HI_HIGH;
        break;

    case SM_E_HI_HIGH:
        /* E has been HIGH for one full tick (200us). Lower it. */
        PE_LO(PIN_E);
        state = SM_E_LO_HIGH;
        break;

    case SM_E_LO_HIGH:
        /* E low for 200us. Now set low nibble and raise E again. */
        PE_LO(PIN_DB4); PE_LO(PIN_DB5); PE_LO(PIN_DB6); PE_LO(PIN_DB7);
        PE_WR(PIN_DB4, (cur_data >> 0) & 1);
        PE_WR(PIN_DB5, (cur_data >> 1) & 1);
        PE_WR(PIN_DB6, (cur_data >> 2) & 1);
        PE_WR(PIN_DB7, (cur_data >> 3) & 1);
        PE_HI(PIN_E);
        state = SM_E_HI_LOW;
        break;

    case SM_E_HI_LOW:
        /* E has been HIGH for one full tick (200us). Lower it. Byte done. */
        PE_LO(PIN_E);
        wait_cnt = wait_long ? LCD_TICKS_CLEAR_WAIT : LCD_TICKS_NORMAL_WAIT;
        state = SM_WAIT;
        break;

    case SM_WAIT:
        if (--wait_cnt == 0)
            state = SM_IDLE;  /* ready for next op next tick */
        break;

    /* SM_E_LO_LOW removed — merged into SM_E_HI_LOW for clarity */
    default:
        state = SM_IDLE;
        break;
    }
}

/* -----------------------------------------------------------------------
** Public API — enqueue operations, return immediately
** ----------------------------------------------------------------------- */

void lcd_command(uint8_t data)
{
    lcd_enqueue(0, data, 0);
}

void lcd_data(uint8_t data)
{
    lcd_enqueue(1, data, 0);
}

void lcd_clear(void)
{
    lcd_enqueue(0, LCD_CLEAR_DISPLAY, 1);  /* long_wait=1 */
}

void lcd_home(void)
{
    lcd_enqueue(0, LCD_CURSOR_HOME, 1);    /* long_wait=1 */
}

void lcd_setcursor(uint8_t x, uint8_t y)
{
    uint8_t addr = (uint8_t)(LCD_SET_DDADR +
                   (y == 2 ? LCD_DDADR_LINE2 : LCD_DDADR_LINE1) + x);
    lcd_enqueue(0, addr, 0);
}

void lcd_string(const char *data)
{
    while (*data != '\0')
        lcd_enqueue(1, (uint8_t)*data++, 0);
}

void lcd_turnOn(uint8_t isOn, uint8_t cursorOn)
{
    uint8_t cmd = (uint8_t)(LCD_SET_DISPLAY |
                  (isOn     ? LCD_DISPLAY_ON  : LCD_DISPLAY_OFF) |
                  (cursorOn ? LCD_CURSOR_ON   : LCD_CURSOR_OFF)  |
                  LCD_BLINKING_OFF);
    lcd_enqueue(0, cmd, 0);
}

void lcd_turnOff(void)
{
    lcd_turnOn(0, 0);
}

/* -----------------------------------------------------------------------
** lcd_diagDisplayInt — display a labelled signed integer on two LCD rows.
**
** label: up to 4 characters + ':' (5 chars total, truncated if longer).
** val1 / val2: signed 32-bit values, displayed right-justified with sign.
**
** Format per row:  "LABL: ±NNNNNNNNNN"  (16 chars total)
**   Col  0-4:  label[0..3] + ':'
**   Col  5:    ' '
**   Col  6-15: sign + up to 9 decimal digits, right-justified.
**
** No printf, no libc dependency. Integer arithmetic only. */
void lcd_diagDisplayInt(const char *label1, int32_t val1,
                        const char *label2, int32_t val2)
{
    uint8_t i;
    char buf[17];

    /* Helper lambda-style: format one row into buf */
    for (uint8_t row = 0; row < 2; row++) {
        const char *lbl = (row == 0) ? label1 : label2;
        int32_t     val = (row == 0) ? val1   : val2;

        /* Label: up to 4 chars, padded with spaces, then ':' */
        for (i = 0; i < 4; i++)
            buf[i] = (lbl[i] && i < 4) ? lbl[i] : ' ';
        buf[4] = ':';
        buf[5] = ' ';

        /* Value: right-justified in cols 6-15, sign always shown */
        uint32_t abs_val = (val < 0) ? (uint32_t)(-(int32_t)val) : (uint32_t)val;
        char sign = (val < 0) ? '-' : '+';
        i = 15;
        if (abs_val == 0) {
            buf[i--] = '0';
        } else {
            uint32_t n = abs_val;
            while (n > 0) { buf[i--] = (char)('0' + (n % 10)); n /= 10; }
        }
        buf[i--] = sign;
        /* Pad remaining value area with spaces */
        while (i >= 6) buf[i--] = ' ';
        buf[16] = '\0';

        lcd_setcursor(0, (uint8_t)(row + 1));
        lcd_string(buf);
    }
}

/* -----------------------------------------------------------------------
** lcd_diagDisplayFloat — display a labelled float on two LCD rows.
**
** label: up to 4 characters + ':' (5 chars, truncated if longer).
** val1 / val2: float values displayed in scientific notation ±N.NNNNNEn.
**
** Format per row:  "LABL: ±N.NNNNNEn"  (16 chars total)
**   Col  0-4:  label[0..3] + ':'
**   Col  5:    ' '
**   Col  6:    sign ('+' or '-')
**   Col  7:    leading digit (0-9)
**   Col  8:    '.'
**   Col  9-13: 5 fractional digits
**   Col 14:    'E'
**   Col 15:    exponent digit (single digit 0-9 for typical DSP values)
**
** If the exponent exceeds one digit (|exp| > 9), 'E?' is shown.
** No printf, no float-to-string library. Manual IEEE754-style extraction
** using integer arithmetic after scaling to a 0.1-1.0 normalised form.
**
** Suitable for DSP values in the range [0.0, 1.0] (filter coefficients,
** LFO rates, etc.) as well as larger diagnostic values. */
void lcd_diagDisplayFloat(const char *label1, float val1,
                          const char *label2, float val2)
{
    uint8_t row;
    char buf[17];

    for (row = 0; row < 2; row++) {
        const char *lbl = (row == 0) ? label1 : label2;
        float       fv  = (row == 0) ? val1   : val2;
        uint8_t     i;

        /* Label */
        for (i = 0; i < 4; i++)
            buf[i] = (lbl[i] && i < 4) ? lbl[i] : ' ';
        buf[4] = ':';
        buf[5] = ' ';

        /* Sign */
        char sign = (fv < 0.0f) ? '-' : '+';
        if (fv < 0.0f) fv = -fv;
        buf[6] = sign;

        /* Find exponent: normalise fv to [1.0, 10.0) range */
        int8_t exp = 0;
        if (fv != 0.0f) {
            while (fv >= 10.0f) { fv /= 10.0f; exp++; }
            while (fv < 1.0f && fv > 0.0f) { fv *= 10.0f; exp--; }
        }

        /* Extract integer part (0-9) and 5 fractional digits */
        uint32_t scaled = (uint32_t)(fv * 100000.0f + 0.5f); /* 6 sig digits */
        /* Guard: rounding may push scaled to 1000000 */
        if (scaled >= 1000000u) { scaled = 999999u; }

        buf[7]  = (char)('0' + (scaled / 100000u));
        buf[8]  = '.';
        buf[9]  = (char)('0' + ((scaled / 10000u) % 10u));
        buf[10] = (char)('0' + ((scaled / 1000u)  % 10u));
        buf[11] = (char)('0' + ((scaled / 100u)   % 10u));
        buf[12] = (char)('0' + ((scaled / 10u)    % 10u));
        buf[13] = (char)('0' + (scaled             % 10u));
        buf[14] = 'E';

        /* Exponent: single digit only; '?' if out of range */
        if (exp < 0) {
            /* Negative exponent: show as E-N if it fits, else E? */
            if (exp >= -9) {
                buf[14] = 'e'; /* lowercase 'e' signals negative exp */
                buf[15] = (char)('0' + (uint8_t)(-exp));
            } else {
                buf[15] = '?';
            }
        } else if (exp <= 9) {
            buf[15] = (char)('0' + (uint8_t)exp);
        } else {
            buf[15] = '?';
        }
        buf[16] = '\0';

        lcd_setcursor(0, (uint8_t)(row + 1));
        lcd_string(buf);
    }
}
