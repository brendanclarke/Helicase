/*
 * Core/Hardware/memtest.c
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
 * memtest.c — flash sector layout & sample-region probe.
 * See memtest.h for design notes.
 */

#include "memtest.h"

#if MEMTEST_ENABLED

#include "lcd.h"
#include "timebase.h"    /* time_sysTick */
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* --------------------------------------------------------------------
** Hardware register addresses (RM0410)
** -------------------------------------------------------------------- */

/* Factory flash size in KB, 16-bit, F7 series */
#define FLASH_SIZE_REG_ADDR    0x1FF0F442UL
#define FLASH_SIZE_REG         (*(volatile uint16_t *)FLASH_SIZE_REG_ADDR)

/* FLASH controller @ 0x40023C00 */
#define FLASH_BASE             0x40023C00UL
#define FLASH_ACR     (*(volatile uint32_t *)(FLASH_BASE + 0x00))
#define FLASH_KEYR    (*(volatile uint32_t *)(FLASH_BASE + 0x04))
#define FLASH_OPTKEYR (*(volatile uint32_t *)(FLASH_BASE + 0x08))
#define FLASH_SR      (*(volatile uint32_t *)(FLASH_BASE + 0x0C))
#define FLASH_CR      (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_OPTCR   (*(volatile uint32_t *)(FLASH_BASE + 0x14))

#define FLASH_KEY1   0x45670123UL
#define FLASH_KEY2   0xCDEF89ABUL

#define FLASH_CR_PG       (1U << 0)
#define FLASH_CR_SER      (1U << 1)
#define FLASH_CR_SNB_POS  3
#define FLASH_CR_SNB_MSK  (0xFU << FLASH_CR_SNB_POS)
#define FLASH_CR_PSIZE_POS 8
#define FLASH_CR_PSIZE_WORD (0x2U << FLASH_CR_PSIZE_POS)  /* 32-bit prog @ 2.7-3.6V */
#define FLASH_CR_STRT     (1U << 16)
#define FLASH_CR_LOCK     (1U << 31)

#define FLASH_SR_EOP      (1U << 0)
#define FLASH_SR_OPERR    (1U << 1)
#define FLASH_SR_WRPERR   (1U << 4)
#define FLASH_SR_PGAERR   (1U << 5)
#define FLASH_SR_PGPERR   (1U << 6)
#define FLASH_SR_ERSERR   (1U << 7)
#define FLASH_SR_BSY      (1U << 16)
#define FLASH_SR_ERR_MSK  (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                           FLASH_SR_PGPERR | FLASH_SR_ERSERR)

/* OPTCR.nDBANK is bit 29 — 1=single bank, 0=dual bank (F765 2MB) */
#define FLASH_OPTCR_NDBANK_POS  29
#define FLASH_OPTCR_NDBANK      (1U << FLASH_OPTCR_NDBANK_POS)

/* SCB cache control — used to invalidate D-cache after flash writes.
** Without this, F765 may return stale cached data after a flash program. */
#define SCB_BASE    0xE000ED00UL
#define SCB_CCR     (*(volatile uint32_t *)(SCB_BASE + 0x14))
#define SCB_CCR_DC  (1U << 16)

/* Linker-provided symbols telling us the application's code+ro+data extent.
** _etext is the highest flash address used by .text/.rodata; _edata-_sdata
** is .data length (loaded from flash at _sidata = LOADADDR(.data)). */
extern uint32_t _etext;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sidata;

/* --------------------------------------------------------------------
** F765 single-bank sector table (per AN4826)
** -------------------------------------------------------------------- */
typedef struct {
    uint32_t base;
    uint32_t size;
} sector_info_t;

static const sector_info_t f765_sectors_singlebank[12] = {
    { 0x08000000UL,  32 * 1024 },  /* 0  bootloader               */
    { 0x08008000UL,  32 * 1024 },  /* 1  app start                */
    { 0x08010000UL,  32 * 1024 },  /* 2  app                      */
    { 0x08018000UL,  32 * 1024 },  /* 3  app                      */
    { 0x08020000UL, 128 * 1024 },  /* 4  app                      */
    { 0x08040000UL, 256 * 1024 },  /* 5  app reserve              */
    { 0x08080000UL, 256 * 1024 },  /* 6  proposed sample storage  */
    { 0x080C0000UL, 256 * 1024 },  /* 7  proposed sample storage  */
    { 0x08100000UL, 256 * 1024 },  /* 8  proposed sample storage  */
    { 0x08140000UL, 256 * 1024 },  /* 9  proposed sample storage  */
    { 0x08180000UL, 256 * 1024 },  /* 10 proposed sample storage  */
    { 0x081C0000UL, 256 * 1024 },  /* 11 proposed sample storage  */
};

/* The destructive write probe targets sector 11 ONLY. Any other sector
** number passed to memtest_erase_sector() is rejected at the gate. */
#define WRITE_PROBE_SECTOR  11
#define APP_SECTOR_FLOOR    1   /* sectors 0 (bootloader) and below: never touched */
#define ERASE_SECTOR_FLOOR  6   /* erase blocked unless sector >= this */

/* --------------------------------------------------------------------
** LCD helpers — short, blocking-on-queue
** -------------------------------------------------------------------- */
static void lcd_putline(uint8_t row, const char *s)
{
    char buf[17];
    uint8_t i = 0;
    while (i < 16 && s[i]) { buf[i] = s[i]; i++; }
    while (i < 16) { buf[i++] = ' '; }
    buf[16] = 0;
    lcd_setcursor(0, (uint8_t)(row + 1));
    lcd_string(buf);
}

static void lcd_show(const char *row1, const char *row2)
{
    lcd_putline(0, row1);
    lcd_putline(1, row2);
}

static void delay_ms(uint32_t ms)
{
    uint16_t t0 = time_sysTick;
    while ((uint16_t)(time_sysTick - t0) < ms) { /* spin */ }
}

/* hex8 helper — minimal sprintf replacement to avoid pulling in newlib bloat */
static char hexc(uint8_t nyb) { return (char)((nyb < 10) ? ('0' + nyb) : ('A' + nyb - 10)); }

static void hex8(char *out, uint32_t v)
{
    for (int i = 7; i >= 0; i--) {
        out[i] = hexc((uint8_t)(v & 0xF));
        v >>= 4;
    }
    out[8] = 0;
}

static void dec3(char *out, uint16_t v)
{
    if (v >= 1000) v = 999;
    out[0] = (char)('0' + (v / 100));
    out[1] = (char)('0' + ((v / 10) % 10));
    out[2] = (char)('0' + (v % 10));
    out[3] = 0;
}

/* --------------------------------------------------------------------
** Read helpers
** -------------------------------------------------------------------- */

/* Treat sector as blank if first / last / middle word are all 0xFFFFFFFF.
** Cheap heuristic — if all three are 0xFF a sector is overwhelmingly
** likely to be erased. False negatives possible (sector contains
** scattered 0xFF chunks); false positives nearly impossible. */
static bool sector_is_blank(const sector_info_t *s)
{
    volatile uint32_t *first  = (volatile uint32_t *)s->base;
    volatile uint32_t *last   = (volatile uint32_t *)(s->base + s->size - 4);
    volatile uint32_t *middle = (volatile uint32_t *)(s->base + (s->size / 2));
    return (*first == 0xFFFFFFFFUL) && (*last == 0xFFFFFFFFUL) && (*middle == 0xFFFFFFFFUL);
}

static uint32_t sector_first_word(const sector_info_t *s)
{
    return *(volatile uint32_t *)s->base;
}

/* --------------------------------------------------------------------
** Application boundary — derive highest flash address used
** -------------------------------------------------------------------- */
static uint32_t app_highest_flash_addr(void)
{
    /* .text/.rodata end at _etext. .data initialiser block starts at
    ** _sidata=LOADADDR(.data) and is (_edata-_sdata) bytes long, lives in
    ** flash too. Highest flash address = max of those two ranges. */
    uint32_t etext  = (uint32_t)&_etext;
    uint32_t sidata = (uint32_t)&_sidata;
    uint32_t edata  = (uint32_t)&_edata;
    uint32_t sdata  = (uint32_t)&_sdata;
    uint32_t data_end_in_flash = sidata + (edata - sdata);
    return (etext > data_end_in_flash) ? etext : data_end_in_flash;
}

static uint8_t sector_for_addr(uint32_t addr)
{
    for (int i = 11; i >= 0; i--) {
        if (addr >= f765_sectors_singlebank[i].base) return (uint8_t)i;
    }
    return 0xFF;
}

/* --------------------------------------------------------------------
** Flash unlock / lock
** -------------------------------------------------------------------- */
static void flash_unlock(void)
{
    if (FLASH_CR & FLASH_CR_LOCK) {
        FLASH_KEYR = FLASH_KEY1;
        FLASH_KEYR = FLASH_KEY2;
    }
}

static void flash_lock(void)
{
    FLASH_CR |= FLASH_CR_LOCK;
}

static void flash_clear_errors(void)
{
    /* Clear all error flags by writing 1 (rc_w1). Don't clear BSY. */
    FLASH_SR = FLASH_SR_ERR_MSK | FLASH_SR_EOP;
}

/* Wait for BSY to clear with a timeout. Returns 0 on success, 1 on
** timeout, 2 on error flag set. Polled — must be called with IRQs
** disabled because the polling loop can't tolerate interrupt code
** fetches from a flash region currently being erased.
**
** Timeout chosen well above the worst-case 4s/256KB sector erase time
** quoted by the F765 datasheet. */
static int flash_wait(uint32_t timeout_ms)
{
    /* systick_ticks doesn't advance with IRQs disabled — use a CPU-cycle
    ** counter approximation: at 216MHz, 1ms ~= 216,000 cycles. The polling
    ** loop is roughly 4-6 cycles per iteration so iterate * 50000 per ms. */
    uint32_t budget = timeout_ms * 50000UL;
    while (FLASH_SR & FLASH_SR_BSY) {
        if (--budget == 0) return 1;
    }
    if (FLASH_SR & FLASH_SR_ERR_MSK) return 2;
    return 0;
}

/* Invalidate D-cache by address range. F765 has a unified D-cache that
** caches flash reads; after a flash write, cached lines must be
** invalidated or reads return stale pre-write data.
** Cortex-M7 cache lines are 32 bytes. */
static void dcache_invalidate(uint32_t addr, uint32_t length)
{
    if (!(SCB_CCR & SCB_CCR_DC)) return;  /* D-cache off — nothing to do */

    #define SCB_DCIMVAC (*(volatile uint32_t *)0xE000EF5CUL)

    uint32_t start = addr & ~0x1FUL;
    uint32_t end   = (addr + length + 31) & ~0x1FUL;
    __asm volatile("dsb" ::: "memory");
    for (uint32_t a = start; a < end; a += 32) {
        SCB_DCIMVAC = a;
    }
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");
}

/* --------------------------------------------------------------------
** memtest_erase_sector — erase ONLY allowed sectors
**
** GUARD: rejects any sector < ERASE_SECTOR_FLOOR (6). This is the
** load-bearing safety check — even if a caller bug passes sector 0,
** this returns -1 without ever touching FLASH_CR.SER. We never erase
** application code or the bootloader.
** -------------------------------------------------------------------- */
static int memtest_erase_sector(uint8_t sector_nr)
{
    if (sector_nr < ERASE_SECTOR_FLOOR || sector_nr > 11) return -1;

    /* IRQs off for the duration. Without this, TIM6 ISR could fire
    ** mid-erase, attempt a code fetch from the sector being erased,
    ** and hard-fault. (Our application is in sectors 1-5, sample
    ** sectors are 6-11 — different sectors, so cross-sector fetches
    ** during erase do work BSY-stalled, but disabling IRQs gives us
    ** a clean atomic operation either way.) */
    __asm volatile("cpsid i" ::: "memory");

    flash_unlock();
    flash_clear_errors();

    if (FLASH_SR & FLASH_SR_BSY) {
        flash_lock();
        __asm volatile("cpsie i" ::: "memory");
        return -2;
    }

    /* Set: SER, SNB=sector_nr, PSIZE=word (x32 @ 2.7-3.6V) */
    uint32_t cr = FLASH_CR;
    cr &= ~(FLASH_CR_SNB_MSK | (0x3U << FLASH_CR_PSIZE_POS));
    cr |= FLASH_CR_SER;
    cr |= ((uint32_t)sector_nr << FLASH_CR_SNB_POS);
    cr |= FLASH_CR_PSIZE_WORD;
    FLASH_CR = cr;

    /* Start the erase */
    FLASH_CR |= FLASH_CR_STRT;

    int rc = flash_wait(5000);  /* 5s budget — plenty for 256KB sector */

    /* Clear SER */
    FLASH_CR &= ~FLASH_CR_SER;

    flash_lock();

    /* Invalidate D-cache for the now-erased region so subsequent reads
    ** see 0xFF instead of pre-erase cached data. */
    dcache_invalidate(f765_sectors_singlebank[sector_nr].base,
                      f765_sectors_singlebank[sector_nr].size);

    __asm volatile("cpsie i" ::: "memory");
    return rc;
}

/* memtest_program_word — write one 32-bit word to flash. Address must
** point inside an erase-allowed sector AND have been erased to 0xFF
** beforehand. */
static int memtest_program_word(uint32_t addr, uint32_t data)
{
    uint8_t sec = sector_for_addr(addr);
    if (sec < ERASE_SECTOR_FLOOR || sec > 11) return -1;

    __asm volatile("cpsid i" ::: "memory");

    flash_unlock();
    flash_clear_errors();

    if (FLASH_SR & FLASH_SR_BSY) {
        flash_lock();
        __asm volatile("cpsie i" ::: "memory");
        return -2;
    }

    uint32_t cr = FLASH_CR;
    cr &= ~(FLASH_CR_SNB_MSK | (0x3U << FLASH_CR_PSIZE_POS) | FLASH_CR_SER);
    cr |= FLASH_CR_PG;
    cr |= FLASH_CR_PSIZE_WORD;
    FLASH_CR = cr;

    *(volatile uint32_t *)addr = data;
    __asm volatile("dsb" ::: "memory");

    int rc = flash_wait(100);

    FLASH_CR &= ~FLASH_CR_PG;
    flash_lock();

    dcache_invalidate(addr, 4);

    __asm volatile("cpsie i" ::: "memory");
    return rc;
}

/* --------------------------------------------------------------------
** Test runners
** -------------------------------------------------------------------- */

static void show_test(const char *title, const char *result, uint32_t hold_ms)
{
    lcd_show(title, result);
    delay_ms(hold_ms);
}

static void test_flash_size(void)
{
    uint16_t kb = FLASH_SIZE_REG;
    char buf[17] = "Size:           ";
    /* "Size: NNNN KB"  e.g. "Size: 2048 KB   " */
    if (kb >= 1000) {
        buf[6] = (char)('0' + (kb / 1000));
        buf[7] = (char)('0' + ((kb / 100) % 10));
        buf[8] = (char)('0' + ((kb / 10) % 10));
        buf[9] = (char)('0' + (kb % 10));
    } else {
        char tmp[4]; dec3(tmp, kb);
        buf[6] = ' '; buf[7] = tmp[0]; buf[8] = tmp[1]; buf[9] = tmp[2];
    }
    buf[10] = ' '; buf[11] = 'K'; buf[12] = 'B';
    show_test("1 Flash size", buf, 800);
}

static void test_dual_bank(void)
{
    bool single = (FLASH_OPTCR & FLASH_OPTCR_NDBANK) != 0;
    show_test("2 Bank mode",
              single ? "SINGLE (OK)     " : "DUAL (UNSUPP!)  ",
              1200);
}

static void test_app_boundary(void)
{
    uint32_t hi  = app_highest_flash_addr();
    uint8_t  sec = sector_for_addr(hi);
    char buf[17] = "etext=          ";  /* "etext=08xxxxxx" */
    char hx[9]; hex8(hx, hi);
    buf[6] = hx[0]; buf[7] = hx[1]; buf[8] = hx[2]; buf[9] = hx[3];
    buf[10] = hx[4]; buf[11] = hx[5]; buf[12] = hx[6]; buf[13] = hx[7];
    show_test("3 App boundary", buf, 1200);

    char buf2[17] = "in sector NN    ";
    if (sec < 10) { buf2[10] = (char)('0' + sec); buf2[11] = ' '; }
    else { buf2[10] = (char)('0' + (sec / 10)); buf2[11] = (char)('0' + (sec % 10)); }
    show_test("3 App boundary", buf2, 1200);
}

static void test_sector_contents(uint8_t sec)
{
    const sector_info_t *s = &f765_sectors_singlebank[sec];
    bool blank = sector_is_blank(s);
    uint32_t fw = sector_first_word(s);

    /* Title row: "S NN @ 08xxxxxx " */
    char title[17] = "S    @ 08       ";
    title[2] = (sec < 10) ? ' ' : (char)('0' + (sec / 10));
    title[3] = (char)('0' + (sec % 10));
    char hx[9]; hex8(hx, s->base);
    /* Already starts 08; copy chars 2-7 of hex */
    title[10] = hx[2]; title[11] = hx[3];
    title[12] = hx[4]; title[13] = hx[5];
    title[14] = hx[6]; title[15] = hx[7];

    char val[17];
    if (blank) {
        memcpy(val, "BLANK (0xFF)    ", 16); val[16] = 0;
    } else {
        /* Show first word: "data=xxxxxxxx   " */
        memcpy(val, "data=           ", 16); val[16] = 0;
        char hf[9]; hex8(hf, fw);
        for (int i = 0; i < 8; i++) val[5 + i] = hf[i];
    }
    show_test(title, val, 700);
}

static void test_read_only_pass(void)
{
    test_flash_size();
    test_dual_bank();
    test_app_boundary();

    show_test("4 Sector probe", "5,6,7,8,9,10,11 ", 1200);
    test_sector_contents(5);
    test_sector_contents(6);
    test_sector_contents(7);
    test_sector_contents(8);
    test_sector_contents(9);
    test_sector_contents(10);
    test_sector_contents(11);
}

/* --------------------------------------------------------------------
** Destructive write probe — only when SHIFT held at boot
** -------------------------------------------------------------------- */
static void test_write_pass(void)
{
    uint8_t sec = WRITE_PROBE_SECTOR;
    const sector_info_t *s = &f765_sectors_singlebank[sec];
    uint32_t addr_lo = s->base;
    uint32_t addr_mid = s->base + (s->size / 2);
    uint32_t addr_hi = s->base + s->size - 4;

    show_test("5 WRITE PROBE", "Erasing s.11... ", 600);

    /* Read sector 5 first word — we'll re-check after erase to confirm
    ** the erase scope is correctly limited to sector 11. */
    uint32_t s5_before = sector_first_word(&f765_sectors_singlebank[5]);

    int rc = memtest_erase_sector(sec);
    if (rc != 0) {
        char buf[17] = "Erase FAIL rc=  ";
        buf[14] = (char)('0' + (rc < 0 ? -rc : rc));
        show_test("5 WRITE PROBE", buf, 3000);
        return;
    }

    /* Confirm sector 11 is now blank */
    if (!sector_is_blank(s)) {
        show_test("5 WRITE PROBE", "Erase NOT blank!", 3000);
        return;
    }
    show_test("5 WRITE PROBE", "Erase OK -> 0xFF", 800);

    /* Confirm sector 5 unchanged — proves erase scope was limited */
    uint32_t s5_after = sector_first_word(&f765_sectors_singlebank[5]);
    if (s5_after != s5_before) {
        show_test("5 SCOPE FAIL!!! ", "S5 changed!     ", 4000);
        return;
    }
    show_test("5 Scope check", "S5 unchanged OK ", 800);

    /* Program three test words */
    rc = memtest_program_word(addr_lo,  0xCAFEBABEUL);
    if (rc != 0) goto write_fail;
    rc = memtest_program_word(addr_mid, 0xDEADBEEFUL);
    if (rc != 0) goto write_fail;
    rc = memtest_program_word(addr_hi,  0x12345678UL);
    if (rc != 0) goto write_fail;

    /* Verify */
    if (*(volatile uint32_t *)addr_lo  != 0xCAFEBABEUL ||
        *(volatile uint32_t *)addr_mid != 0xDEADBEEFUL ||
        *(volatile uint32_t *)addr_hi  != 0x12345678UL) {
        show_test("5 VERIFY FAIL  ", "data mismatch   ", 4000);
        return;
    }
    show_test("5 Program/verify", "3 words OK      ", 1200);

    /* Re-erase to leave sector 11 blank — never leave test data in flash */
    rc = memtest_erase_sector(sec);
    if (rc != 0) {
        show_test("5 Re-erase FAIL", "                ", 3000);
        return;
    }
    show_test("5 Re-erase", "OK back to 0xFF ", 800);
    return;

write_fail:
    show_test("5 PROGRAM FAIL ", "                ", 3000);
}

/* --------------------------------------------------------------------
** Public entry
** -------------------------------------------------------------------- */
void memtest_run(void)
{
    /* Detect BAR1-held-at-boot: read PB7 GPIO directly. PB7 is wired
    ** to SW43 (the BAR1 button — new on LXR-02, distinct from the
    ** SHIFT button which lives in the shift-register chain). Direct
    ** GPIO read is correct here because the user is holding the
    ** button steady from before power-on; we don't need debounce.
    ** Reading the GPIO directly also removes any timing dependency on
    ** TIM6 having polled the debounce filter enough times to settle. */
    #define GPIOB_IDR (*(volatile uint32_t *)0x40020410UL)
    bool bar1_held = ((GPIOB_IDR >> 7) & 1U) != 0;

    lcd_show("LXR-02 MEMTEST", bar1_held ? "+ WRITE PROBE   " : "read-only       ");
    delay_ms(1500);

    test_read_only_pass();

    if (bar1_held) {
        /* Safety gate: destructive test relies on the single-bank sector
        ** table. If the part is in dual-bank mode, our SNB encoding is
        ** wrong and we'd erase the wrong sector. Refuse to proceed. */
        bool single_bank = (FLASH_OPTCR & FLASH_OPTCR_NDBANK) != 0;
        if (!single_bank) {
            show_test("5 Write probe",
                      "DUAL BANK abort ", 4000);
        } else {
            test_write_pass();
        }
    } else {
        show_test("5 Write probe", "skipped         ", 700);
    }

    /* Final summary — 5 second hold */
    lcd_show("MEMTEST done    ", "                ");
    delay_ms(5000);
}

#endif /* MEMTEST_ENABLED */
