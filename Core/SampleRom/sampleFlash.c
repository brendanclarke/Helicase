/*
 * Core/SampleRom/sampleFlash.c
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

#include "sampleFlash.h"
#include "SampleMemory.h"
#include <stdint.h>

#define FLASH_BASE             0x40023C00UL
#define FLASH_KEYR    (*(volatile uint32_t *)(FLASH_BASE + 0x04))
#define FLASH_SR      (*(volatile uint32_t *)(FLASH_BASE + 0x0C))
#define FLASH_CR      (*(volatile uint32_t *)(FLASH_BASE + 0x10))

#define FLASH_KEY1   0x45670123UL
#define FLASH_KEY2   0xCDEF89ABUL

#define FLASH_CR_PG       (1U << 0)
#define FLASH_CR_SER      (1U << 1)
#define FLASH_CR_SNB_POS  3
#define FLASH_CR_SNB_MSK  (0xFU << FLASH_CR_SNB_POS)
#define FLASH_CR_PSIZE_POS 8
#define FLASH_CR_PSIZE_WORD (0x2U << FLASH_CR_PSIZE_POS)
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

#define SCB_CCR     (*(volatile uint32_t *)(0xE000ED00UL + 0x14))
#define SCB_CCR_DC  (1U << 16)
#define SCB_DCIMVAC (*(volatile uint32_t *)0xE000EF5CUL)

typedef struct {
    uint32_t base;
    uint32_t size;
} sample_flash_sector_t;

static const sample_flash_sector_t sample_flash_sectors[12] = {
    { 0x08000000UL,  32 * 1024 },
    { 0x08008000UL,  32 * 1024 },
    { 0x08010000UL,  32 * 1024 },
    { 0x08018000UL,  32 * 1024 },
    { 0x08020000UL, 128 * 1024 },
    { 0x08040000UL, 256 * 1024 },
    { 0x08080000UL, 256 * 1024 },
    { 0x080C0000UL, 256 * 1024 },
    { 0x08100000UL, 256 * 1024 },
    { 0x08140000UL, 256 * 1024 },
    { 0x08180000UL, 256 * 1024 },
    { 0x081C0000UL, 256 * 1024 },
};

static uint32_t irq_save(void)
{
    uint32_t primask;
    __asm volatile("mrs %0, primask\ncpsid i" : "=r"(primask) :: "memory");
    return primask;
}

static void irq_restore(uint32_t primask)
{
    __asm volatile("msr primask, %0" :: "r"(primask) : "memory");
}

static void flash_unlock(void)
{
    if (FLASH_CR & FLASH_CR_LOCK) {
        FLASH_KEYR = FLASH_KEY1;
        FLASH_KEYR = FLASH_KEY2;
    }
}

void sampleFlash_lock(void)
{
    FLASH_CR |= FLASH_CR_LOCK;
}

static void flash_clear_errors(void)
{
    FLASH_SR = FLASH_SR_ERR_MSK | FLASH_SR_EOP;
}

static int flash_wait(uint32_t timeout_units)
{
    while (FLASH_SR & FLASH_SR_BSY) {
        if (--timeout_units == 0)
            return 1;
    }
    return (FLASH_SR & FLASH_SR_ERR_MSK) ? 2 : 0;
}

static uint8_t sector_for_addr(uint32_t addr)
{
    for (int i = 11; i >= 0; i--) {
        if (addr >= sample_flash_sectors[i].base)
            return (uint8_t)i;
    }
    return 0xffu;
}

static int addr_in_sample_region(uint32_t address)
{
    return address >= SAMPLE_ROM_START_ADDRESS && address < 0x08200000UL;
}

void sampleFlash_init(void)
{
    uint32_t primask = irq_save();
    flash_unlock();
    flash_clear_errors();
    irq_restore(primask);
}

void sampleFlash_invalidateRange(uint32_t address, uint32_t bytes)
{
    if (!(SCB_CCR & SCB_CCR_DC) || bytes == 0)
        return;

    uint32_t start = address & ~0x1FUL;
    uint32_t end = (address + bytes + 31u) & ~0x1FUL;

    __asm volatile("dsb" ::: "memory");
    for (uint32_t a = start; a < end; a += 32u)
        SCB_DCIMVAC = a;
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");
}

static int sampleFlash_eraseSector(uint8_t sector)
{
    if (sector < 6u || sector > 11u)
        return -1;

    uint32_t primask = irq_save();
    flash_unlock();
    flash_clear_errors();

    if (FLASH_SR & FLASH_SR_BSY) {
        sampleFlash_lock();
        irq_restore(primask);
        return -2;
    }

    uint32_t cr = FLASH_CR;
    cr &= ~(FLASH_CR_SNB_MSK | (0x3U << FLASH_CR_PSIZE_POS));
    cr |= FLASH_CR_SER | FLASH_CR_PSIZE_WORD;
    cr |= ((uint32_t)sector << FLASH_CR_SNB_POS);
    FLASH_CR = cr;
    FLASH_CR |= FLASH_CR_STRT;

    int rc = flash_wait(250000000UL);

    FLASH_CR &= ~FLASH_CR_SER;
    sampleFlash_lock();
    sampleFlash_invalidateRange(sample_flash_sectors[sector].base,
                                sample_flash_sectors[sector].size);
    irq_restore(primask);
    return rc;
}

int sampleFlash_eraseAllSamples(void)
{
    for (uint8_t sector = 6; sector <= 11; sector++) {
        int rc = sampleFlash_eraseSector(sector);
        if (rc != 0)
            return rc;
    }
    return 0;
}

int sampleFlash_writeWord(uint32_t address, uint32_t data)
{
    uint8_t sector;

    if ((address & 3u) != 0 || !addr_in_sample_region(address))
        return -1;
    sector = sector_for_addr(address);
    if (sector < 6u || sector > 11u)
        return -1;

    uint32_t primask = irq_save();
    flash_unlock();
    flash_clear_errors();

    if (FLASH_SR & FLASH_SR_BSY) {
        sampleFlash_lock();
        irq_restore(primask);
        return -2;
    }

    uint32_t cr = FLASH_CR;
    cr &= ~(FLASH_CR_SNB_MSK | (0x3U << FLASH_CR_PSIZE_POS) | FLASH_CR_SER);
    cr |= FLASH_CR_PG | FLASH_CR_PSIZE_WORD;
    FLASH_CR = cr;

    *(volatile uint32_t *)address = data;
    __asm volatile("dsb" ::: "memory");

    int rc = flash_wait(1000000UL);

    FLASH_CR &= ~FLASH_CR_PG;
    sampleFlash_lock();
    sampleFlash_invalidateRange(address, 4);
    irq_restore(primask);

    if (rc != 0)
        return rc;
    return (*(volatile uint32_t *)address == data) ? 0 : 3;
}

int sampleFlash_writeBytes(uint32_t *address, const uint8_t *data, uint32_t bytes)
{
    uint32_t addr;
    uint32_t i;

    if (address == 0 || data == 0)
        return -1;
    addr = *address;
    if ((addr & 3u) != 0)
        return -1;

    for (i = 0; i < bytes; i += 4u) {
        uint32_t word = 0xffffffffUL;
        uint32_t remaining = bytes - i;

        if (remaining > 0u)
            word = (word & 0xffffff00UL) | data[i];
        if (remaining > 1u)
            word = (word & 0xffff00ffUL) | ((uint32_t)data[i + 1u] << 8);
        if (remaining > 2u)
            word = (word & 0xff00ffffUL) | ((uint32_t)data[i + 2u] << 16);
        if (remaining > 3u)
            word = (word & 0x00ffffffUL) | ((uint32_t)data[i + 3u] << 24);

        int rc = sampleFlash_writeWord(addr, word);
        if (rc != 0)
            return rc;
        addr += 4u;
    }

    *address = addr;
    return 0;
}
