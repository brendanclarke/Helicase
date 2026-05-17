/*
 * Core/Hardware/memtest.h
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
 * memtest.h — flash sector layout & sample-region probe.
 *
 * Boot-time diagnostic that reads (and optionally writes) MCU flash to
 * verify the layout we assume for sample storage on the LXR-02 (F765VI).
 *
 * What it tests:
 *   1. FLASH_SIZE register (factory value) — confirms 2MB part
 *   2. FLASH_OPTCR.nDBANK option byte — single vs dual bank mode
 *   3. Application reach: highest address in .text/.data, and the
 *      sector that contains it
 *   4. Read-only contents probe of sectors 5 (proposed app reserve)
 *      and 6-11 (proposed sample region) — first/last/middle word
 *      sampled, blank-erased status reported
 *   5. (Optional, BAR1 held at boot) Erase + program + verify probe
 *      of sector 11 only — proves erase/program path works AND that
 *      erase scope is correctly limited (sector 5 unchanged after
 *      sector 11 erase)
 *
 * Why this exists: deciding the sample-storage region requires knowing
 * whether the LXRV2 bootloader writes anywhere in sectors 6-11, what
 * dual-bank mode the part is in, and whether our F765-specific erase/
 * program code actually works against the real part. We do not commit
 * to a sample-storage layout based on assumptions.
 *
 * BAR1 vs SHIFT: BAR1 is a new button on LXR-02 (not present on the
 * original LXR). It is wired to SW43 / PB7 GPIO, separate from the
 * shift-register button chain. The SHIFT button is a different button
 * (index 39 in the shift-register chain), used in the menu as the
 * SHIFT modifier (e.g. SHIFT+MODE4 → globals). The memtest gates on
 * BAR1 specifically because (a) it's a hardware-direct GPIO that
 * works without TIM6 polling having stabilised, and (b) BAR1 has no
 * other firmware function yet, so using it as a boot-time gate
 * doesn't conflict with anything.
 *
 * Hard safety guarantees (do not weaken):
 *   - Read-only path: no writes anywhere, ever.
 *   - Write path: erases ONLY sector 11 (highest sector, furthest
 *     from anything important). Hard guard at top of erase function;
 *     any other sector number → return error without touching FLASH_CR.
 *   - All erase/program code runs with interrupts disabled. TIM6 stalls
 *     ~2s during sector erase. Acceptable for one-shot boot test;
 *     production sample-install code will need a different strategy.
 *   - D-cache invalidated after writes (F765 has D-cache; F4 didn't).
 *     Without this, reads after writes can return stale cached data.
 *
 * Invocation: memtest_run() called from main.c after time_initTimer()
 * (TIM6 must be live for time_sysTick) and after lcd_init/tim7_init,
 * dout_init, led_init, din_init (need to display results, light LEDs,
 * read BAR1). Returns void; tests are reported on the LCD with a 5s
 * final hold so the user has time to read.
 */
#ifndef MEMTEST_H_
#define MEMTEST_H_

#include "config.h"

#if MEMTEST_ENABLED
void memtest_run(void);
#else
static inline void memtest_run(void) { }
#endif

#endif
