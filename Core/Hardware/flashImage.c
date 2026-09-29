/*
 * Core/Hardware/flashImage.c
 *
 *  Created on: 28.09.2026
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
 * flashImage.c — boot-time check of the application flash image (S073).
 * See flashImage.h for the contract.
 */

#include "flashImage.h"
#include "config.h"
#include "lcd.h"
#include "timebase.h"    /* time_sysTick */
#include <stdint.h>

#define FLASH_IMAGE_ORIGIN         0x08008000UL
#define FLASH_IMAGE_MAGIC_STAMPED  0x4B434D49UL   /* "IMCK", little-endian */
#define FLASH_IMAGE_SECTORS        6u             /* application sectors 1..6 */

/* SW43/BAR1: PB7, active high, configured as an input by din_init(). */
#define FLASH_IMAGE_GPIOB_IDR      (*(volatile uint32_t *)0x40020410UL)
#define FLASH_IMAGE_BAR1           (1UL << 7)

/* Linker (sector 1, after the vector table): word 0 = magic, words 1..6 =
** CRC32 of sectors 1..6, word 7 = image length in bytes. */
extern const uint32_t _simage_check[];
extern const uint32_t _eimage_check[];
extern const uint8_t _eflash_load[];

/* End address of application sectors 1..6 (F765 single bank). */
static const uint32_t flashImage_sectorEnd[FLASH_IMAGE_SECTORS] = {
    0x08010000UL, 0x08018000UL, 0x08020000UL,
    0x08040000UL, 0x08080000UL, 0x080C0000UL,
};

/* Nibble table for the reflected CRC-32 polynomial 0xEDB88320. */
static const uint32_t flashImage_crcNibble[16] = {
    0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
    0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
    0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
    0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL,
};

#if FLASH_GROWTH_DRILL_KB
/* Growth drill (config.h): pads the image past 0x08080000 into sector 6.
** Its address is shown at boot, which also keeps it linked. */
static const uint32_t flashImage_drillTable[FLASH_GROWTH_DRILL_KB * 256u] = {
    [0 ... (FLASH_GROWTH_DRILL_KB * 256u) - 1u] = 0x5AC3A53CUL
};
#endif

/* Continue a CRC-32 (running form, start 0xFFFFFFFF, final ~) over the
** bytes in [start, end), both word aligned. Reflected CRC-32 is linear, so
** XORing a whole little-endian word and shifting it out as eight nibbles
** equals processing its four bytes in memory order. */
static uint32_t flashImage_crc32Update(uint32_t crc, uint32_t start, uint32_t end)
{
    const uint32_t *p = (const uint32_t *)start;
    const uint32_t *e = (const uint32_t *)end;

    while (p < e) {
        crc ^= *p++;
        for (uint8_t i = 0; i < 8u; i++)
            crc = (crc >> 4) ^ flashImage_crcNibble[crc & 0xFu];
    }
    return crc;
}

#if FLASH_GROWTH_DRILL_KB
static void flashImage_hex8(char *out, uint32_t v)
{
    for (int8_t i = 7; i >= 0; i--) {
        uint8_t n = (uint8_t)(v & 0xFu);
        out[i] = (n < 10u) ? (char)('0' + n) : (char)('A' + (n - 10u));
        v >>= 4;
    }
}
#endif

static void flashImage_show(const char row1[17], const char row2[17])
{
    lcd_clear();
    lcd_setcursor(0, 1);
    lcd_string(row1);
    lcd_setcursor(0, 2);
    lcd_string(row2);
}

static void flashImage_delayMs(uint16_t ms)
{
    uint16_t t0 = time_sysTick;
    while ((uint16_t)(time_sysTick - t0) < ms) { /* boot-only hold */ }
}

void flashImage_verifyAtBoot(void)
{
    /*
     * Recompute every application sector's CRC and compare with the stamp.
     *
     * Inputs: the flash image and its stamped check block. Output: nothing
     * on success; otherwise an LCD report and a BAR1 hold. Why a hold and
     * not a halt: a corrupt image usually fails on its own, while a fault in
     * this checker or the stamp must never stop a good image from booting.
     */
    const uint32_t image_end = (uint32_t)_eflash_load;
    const uint32_t block_lo = (uint32_t)_simage_check;
    const uint32_t block_hi = (uint32_t)_eimage_check;
    const uint8_t stamped =
        _simage_check[0] == FLASH_IMAGE_MAGIC_STAMPED &&
        _simage_check[7] == image_end - FLASH_IMAGE_ORIGIN;
    uint32_t lo = FLASH_IMAGE_ORIGIN;
    uint8_t bad = 0u;
    char row1[17] = "Img BAD s:......";
    char row2[17] = "Reflash. BAR1=go";

    for (uint8_t i = 0; i < FLASH_IMAGE_SECTORS; i++) {
        /* This sector's share of the image, split around the check block
        ** (same arithmetic as tools/stamp_image_check.py). */
        uint32_t sector_end = flashImage_sectorEnd[i];
        uint32_t hi = (sector_end < image_end) ? sector_end : image_end;
        uint32_t a_hi, b_lo, crc;

        if (lo > hi)
            lo = hi;
        a_hi = (block_lo > lo) ? block_lo : lo;
        if (a_hi > hi)
            a_hi = hi;
        b_lo = (block_hi < hi) ? block_hi : hi;
        if (b_lo < lo)
            b_lo = lo;
        crc = flashImage_crc32Update(0xFFFFFFFFUL, lo, a_hi);
        crc = ~flashImage_crc32Update(crc, b_lo, hi);

        if (crc != _simage_check[1u + i]) {
            bad |= (uint8_t)(1u << i);
            row1[10u + i] = (char)('1' + i);
        }
        lo = sector_end;
    }

    if (stamped && bad == 0u) {
#if FLASH_GROWTH_DRILL_KB
        /* "Img OK  <image end>" / "drill   <table address>" */
        char ok1[17] = "Img OK  ........";
        char ok2[17] = "drill   ........";
        flashImage_hex8(&ok1[8], image_end);
        flashImage_hex8(&ok2[8], (uint32_t)flashImage_drillTable);
        flashImage_show(ok1, ok2);
        flashImage_delayMs(3000u);
#endif
        return;
    }

    if (!stamped) {
        const char unstamped[17] = "Img unstamped   ";
        flashImage_show(unstamped, row2);
    } else {
        flashImage_show(row1, row2);
    }

    while (!(FLASH_IMAGE_GPIOB_IDR & FLASH_IMAGE_BAR1)) { /* wait press */ }
    flashImage_delayMs(30u);
    while (FLASH_IMAGE_GPIOB_IDR & FLASH_IMAGE_BAR1) { /* wait release */ }
    flashImage_delayMs(30u);
}
