/*
 * Core/Hardware/flashImage.h
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
 * flashImage.h — boot-time check of the application flash image (S073).
 *
 * What: flashImage_verifyAtBoot() recomputes a CRC32 for each application
 * flash sector (1..6) over its share of the load image and compares it with
 * the words the build stamped into the 32-byte image check block in sector 1
 * (STM32F765VIHx_FLASH.ld .image_check, tools/stamp_image_check.py). The
 * block's own bytes are skipped, and it also records the image length.
 *
 * Why: Session 073 grew the application window from 480 KB (sectors 1-5) to
 * 736 KB (sectors 1-6). The closed LXRV2 bootloader has only ever been shown
 * to program images up to ~483 KB; the factory application is 270 KB and has
 * no flash-writing code. If the bootloader ever fails to erase or program a
 * sector the image reaches, the damage would otherwise be silent code
 * corruption. The check names the bad sector instead.
 *
 * Result: silent on success. On a mismatch, or an unstamped image, rows 1-2
 * show the bad sectors and "Reflash. BAR1=go"; boot waits for a BAR1
 * press-and-release, then continues (so a checker fault can never brick a
 * unit).
 *
 * Cost: no RAM (locals only), ~0.5 KB flash, ~20 ms of boot time.
 *
 * Invocation: main.c after din_init() (PB7/BAR1 configured) and
 * time_initTimer(), before any flash-resident sample or DSP use.
 * Affiliates: STM32F765VIHx_FLASH.ld (_simage_check), Makefile .bin rule,
 * tools/stamp_image_check.py (must compute the same CRC32 as this file:
 * standard reflected CRC-32, poly 0xEDB88320, init/final 0xFFFFFFFF, as
 * Python zlib.crc32).
 */

#ifndef FLASHIMAGE_H_
#define FLASHIMAGE_H_

void flashImage_verifyAtBoot(void);

#endif /* FLASHIMAGE_H_ */
