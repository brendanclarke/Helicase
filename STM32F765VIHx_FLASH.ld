/*
 * STM32F765VIHx_FLASH.ld
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
 * STM32F765VIH6 — application linker script for LXR-02
 *
 * Memory layout:
 *   FLASH  0x08008000-0x0807FFFF (480 KB)   app starts after 32KB bootloader
 *   SAMPLE 0x08080000-0x081FFFFF (1536 KB)  reserved for user samples
 *   ITCM   0x00000000-0x00003FFF (16 KB)    tightly-coupled instruction RAM
 *   DTCM   0x20000000-0x2001FFFF (128 KB)   tightly-coupled SRAM, no DMA access
 *   SRAM1  0x20020000–0x2007BFFF (368 KB)   regular SRAM, DMA accessible
 *   SRAM2  0x2007C000–0x2007FFFF (16 KB)    shared SRAM (not currently mapped)
 *
 * Stack lives at top of SRAM1 (_estack = 0x20080000) — 16 KB SRAM2 is left
 * unmapped for now. Stack does NOT live in DTCM because the original LXR
 * AVR/STM32 firmware places hot DSP state there via INCCM/INCCMZ
 * (mapped here as INDTCM/INDTCMZ in config.h). We need contiguous DTCM
 * for that.
 *
 * Sections:
 *   .isr_vector  → FLASH @ origin
 *   .text/.rodata → FLASH
 *   .itcm        → ITCM (loaded from FLASH)   — INITCM hot code
 *   .data        → SRAM1 (loaded from FLASH)
 *   .bss         → SRAM1
 *   .dtcm        → DTCM (loaded from FLASH)   — INDTCM data
 *   .dtcmz       → DTCM (NOLOAD, runtime-zero) — INDTCMZ uninitialised
 *
 * .dtcm is intentionally placed BEFORE .dtcmz in the DTCM region so any
 * accidental size growth in .dtcm shows up as a link error rather than
 * silently overlapping zero-init data. ASSERT below catches DTCM overflow.
 */
ENTRY(Reset_Handler)
_estack = 0x20080000;
MEMORY {
    ITCM  (rx)  : ORIGIN = 0x00000000, LENGTH = 0x4000
    FLASH (rx)  : ORIGIN = 0x08008000, LENGTH = 0x78000
    DTCM  (xrw) : ORIGIN = 0x20000000, LENGTH = 0x20000
    SRAM1 (xrw) : ORIGIN = 0x20020000, LENGTH = 0x5C000
}
SECTIONS {
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } >FLASH
    .text : {
        . = ALIGN(4);
        *(.text) *(.text*)
        *(.rodata) *(.rodata*)
        *(.ARM.extab* .gnu.linkonce.armextab.*)
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        KEEP(*(.init)) KEEP(*(.fini))
        . = ALIGN(4); _etext = .;
    } >FLASH
    /* INITCM — hot code copied from FLASH to ITCM at startup. */
    _siitcm = LOADADDR(.itcm);
    .itcm : {
        . = ALIGN(4); _sitcm = .;
        *(.itcm) *(.itcm.*)
        . = ALIGN(4); _eitcm = .;
    } >ITCM AT> FLASH
    ASSERT(_eitcm <= ORIGIN(ITCM) + LENGTH(ITCM),
           "ITCM overflow: .itcm exceeds 16KB")

    /* DMA buffers — non-cacheable region at start of SRAM1.
    ** MPU region 1 marks this 4KB as Device memory so D-cache
    ** never caches DMA-visible data. Must stay ≤ 4KB total. */
    .dma_nocache (NOLOAD) : {
        . = ALIGN(4);
        _sdma_nocache = .;
        *(.dma_nocache) *(.dma_nocache.*)
        . = ALIGN(4);
        _edma_nocache = .;
    } >SRAM1
    ASSERT(_edma_nocache - _sdma_nocache <= 4096,
           "DMA non-cacheable section exceeds 4KB MPU region")

    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data) *(.data*) . = ALIGN(4); _edata = .; } >SRAM1 AT> FLASH
    .bss  : { . = ALIGN(4); _sbss = .; *(.bss) *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } >SRAM1

    /* INDTCM — initialised data placed in DTCM, loaded from FLASH at startup.
    ** Mirrors original LXR's .ccm section. Startup code copies _sidtcm..__edtcm
    ** from LOADADDR(.dtcm) to _sdtcm. */
    _sidtcm = LOADADDR(.dtcm);
    .dtcm : {
        . = ALIGN(4); _sdtcm = .;
        *(.dtcm) *(.dtcm.*)
        . = ALIGN(4); _edtcm = .;
    } >DTCM AT> FLASH
    _eflash_load = LOADADDR(.dtcm) + SIZEOF(.dtcm);

    /* INDTCMZ — zero-initialised data in DTCM, NOLOAD (no flash image).
    ** Startup code zeroes _sdtcmz..__edtcmz. */
    .dtcmz (NOLOAD) : {
        . = ALIGN(4); _sdtcmz = .;
        *(.dtcmz) *(.dtcmz.*)
        . = ALIGN(4); _edtcmz = .;
    } >DTCM

    /* No ._stack reservation here — main stack lives at _estack (top of
    ** SRAM1). DTCM is reserved in full for INDTCM/INDTCMZ DSP data. */

    ASSERT(_edtcmz <= ORIGIN(DTCM) + LENGTH(DTCM),
           "DTCM overflow: .dtcm + .dtcmz exceed 128KB")
    ASSERT(_etext <= 0x08080000,
           "Application overlaps sample flash region")
    ASSERT(_eflash_load <= ORIGIN(FLASH) + LENGTH(FLASH),
           "Application load image overlaps sample flash region")

    /DISCARD/ : { libc.a(*) libm.a(*) libgcc.a(*) }
    .ARM.attributes 0 : { *(.ARM.attributes) }
}
