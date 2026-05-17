/*
 * Core/Hardware/frontPanel/lcd.h
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
 * lcd.h
 *
 * WS0010 OLED 16×2 display driver, 4-bit parallel interface.
 * Non-blocking implementation: all writes go into a queue drained by
 * TIM7 at 5kHz. The public API is identical to the original blocking
 * driver — menu.c and any other caller requires no changes.
 *
 * Pin assignments (STM32F765):
 *   RS  = PE12   (Register Select: 1=data, 0=command)
 *   E   = PE11   (Enable strobe)
 *   DB4 = PE7
 *   DB5 = PE8
 *   DB6 = PE9
 *   DB7 = PE10
 *
 * Architecture:
 *   lcd_init()      — blocking, called before TIM7 starts. Safe.
 *   lcd_command()   — enqueues one command byte. Returns immediately.
 *   lcd_data()      — enqueues one data byte. Returns immediately.
 *   lcd_clear()     — enqueues CLEAR op (long post-wait). Returns immediately.
 *   lcd_home()      — enqueues HOME op (long post-wait). Returns immediately.
 *   lcd_setcursor() — enqueues SETCURSOR command. Returns immediately.
 *   lcd_string()    — enqueues each character. Returns immediately.
 *   lcd_turnOn()    — enqueues display-control command. Returns immediately.
 *
 *   TIM7_IRQHandler — drains the queue one state-machine step per tick
 *                     (200us). Registered in startup_stm32f765xx.s IRQ55.
 *                     Started by lcd_tim7_init(), called after lcd_init().
 *
 * Queue behaviour:
 *   Capacity: LCD_QUEUE_SIZE entries (power of two for fast wrap).
 *   If the queue is full, single enqueue calls drop the op rather than
 *   blocking the main loop and risking audio refill starvation. Menu display
 *   frames should check lcd_queueFree() first and retry the whole frame if it
 *   will not fit.
 *
 * Deadlock guarantee:
 *   No lcd_* function (except lcd_init) ever touches a hardware register
 *   or spin-waits on any hardware flag. All GPIO manipulation is done
 *   exclusively inside TIM7_IRQHandler. TIM6 and TIM7 touch completely
 *   disjoint peripherals (SPI1/GPIOB vs GPIOE) so there is no bus conflict.
 */

#ifndef LCD_H_
#define LCD_H_

#include <stdint.h>

/* -----------------------------------------------------------------------
** Queue capacity — must be a power of two
** ----------------------------------------------------------------------- */
#define LCD_QUEUE_SIZE   128u   /* 64 ops: more than a full 32-char repaint */

/* -----------------------------------------------------------------------
** DDRAM line addresses
** ----------------------------------------------------------------------- */
#define LCD_DDADR_LINE1        0x00
#define LCD_DDADR_LINE2        0x40

/* -----------------------------------------------------------------------
** HD44780 / WS0010 command bytes
** ----------------------------------------------------------------------- */
#define LCD_CLEAR_DISPLAY      0x01
#define LCD_CURSOR_HOME        0x02
#define LCD_SET_ENTRY          0x04
#define LCD_ENTRY_INCREASE     0x02
#define LCD_ENTRY_NOSHIFT      0x00
#define LCD_SET_DISPLAY        0x08
#define LCD_DISPLAY_OFF        0x00
#define LCD_DISPLAY_ON         0x04
#define LCD_CURSOR_OFF         0x00
#define LCD_CURSOR_ON          0x02
#define LCD_BLINKING_OFF       0x00
#define LCD_BLINKING_ON        0x01
#define LCD_SET_FUNCTION       0x20
#define LCD_FUNCTION_4BIT      0x00
#define LCD_FUNCTION_8BIT      0x10
#define LCD_FUNCTION_2LINE     0x08
#define LCD_FUNCTION_5X7       0x00
#define LCD_SOFT_RESET         0x30
#define LCD_SET_DDADR          0x80
/* WS0010 specific: enables internal OLED power supply */
#define LCD_OLED_POWER         0x17

/* -----------------------------------------------------------------------
** Init-time timing constants — used only in lcd_init() (blocking, pre-TIM7)
** ----------------------------------------------------------------------- */
#define LCD_BOOTUP_MS          50
#define LCD_ENABLE_US           1
#define LCD_SOFT_RESET_MS1      5
#define LCD_SOFT_RESET_MS2      2
#define LCD_SOFT_RESET_MS3      2
#define LCD_SET_4BITMODE_MS     2
#define LCD_OLED_POWER_MS      10

/* -----------------------------------------------------------------------
** TIM7 state machine post-byte wait (in 200us ticks)
** ----------------------------------------------------------------------- */
#define LCD_TICKS_NORMAL_WAIT   1u   /* 200us after cmd/data  (min 37us)  */
#define LCD_TICKS_CLEAR_WAIT   10u   /* 2ms   after CLEAR/HOME (min 1.52ms) */

/* -----------------------------------------------------------------------
** Public API — identical to original blocking driver
** ----------------------------------------------------------------------- */

/* Initialise display hardware. Blocking — uses SysTick ms delays.
** time_initSysTick() must have been called first.
** Call BEFORE lcd_tim7_init(). */
void lcd_init(void);

/* Start TIM7 at 5kHz. Call after lcd_init() and after all drivers
** are ready. All post-boot lcd_* calls require TIM7 to be running. */
void lcd_tim7_init(void);

/* Add a custom character in display CGRAM, access with */
void lcd_define_char(uint8_t location, const uint8_t *charmap);

/* Enqueue a clear-display command (includes long post-wait). */
void lcd_clear(void);

/* Enqueue a cursor-home command (includes long post-wait). */
void lcd_home(void);

/* Enqueue a cursor-position command. x: 0-15, y: 1 or 2. */
void lcd_setcursor(uint8_t x, uint8_t y);

/* Enqueue one data byte (character) to write at cursor position. */
void lcd_data(uint8_t data);

/* Enqueue all characters of a null-terminated string. */
void lcd_string(const char *data);

/* Enqueue a raw command byte. */
void lcd_command(uint8_t data);

/* Enqueue display on/off + cursor visibility command. */
void lcd_turnOn(uint8_t isOn, uint8_t cursorOn);
void lcd_turnOff(void);

/* Number of free operation slots in the async LCD queue. */
uint8_t lcd_queueFree(void);

/* Blocking helper for rare modal operations that suspend audio anyway.
** Waits until all queued bytes have physically drained through TIM7. */
void lcd_waitForIdle(void);

/* -----------------------------------------------------------------------
** Called from TIM7_IRQHandler — do not call directly.
** ----------------------------------------------------------------------- */
void lcd_tim7_tick(void);


/* -----------------------------------------------------------------------
** Diagnostic display helpers
**
** lcd_diagDisplayInt: show two labelled signed integers on rows 1 and 2.
**   label: up to 4 chars (truncated). val sign always shown. Right-justified.
**   Format: "LABL: +NNNNNNNNN" (16 chars per row).
**
** lcd_diagDisplayFloat: show two labelled floats in scientific notation.
**   Format: "LABL: +N.NNNNNEn" (positive exponent) or "LABL: +N.NNNNNen"
**   (negative exponent, lowercase 'e'). '?' if |exp| > 9.
**   Suitable for DSP values: filter coefficients, LFO rates, etc.
**   Callers must include <stdint.h> (for int32_t) and ensure float support
**   is available (FPU enabled by sysclk_init, -mfpu=fpv5-d16 in Makefile).
** ----------------------------------------------------------------------- */
void lcd_diagDisplayInt(const char *label1, int32_t val1,
                        const char *label2, int32_t val2);
void lcd_diagDisplayFloat(const char *label1, float val1,
                          const char *label2, float val2);

#endif /* LCD_H_ */
