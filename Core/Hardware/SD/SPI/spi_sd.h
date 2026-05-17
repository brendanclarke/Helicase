/*
 * Core/Hardware/SD/SPI/spi_sd.h
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
 * spi_sd.h — bit-banged SPI for SD card on STM32F765 LXR-02.
 *
 * Confirmed pin assignments (physically traced, XP12):
 *   PD0 = CS   (active low)     XP12 pin 16
 *   PD2 = MOSI (DI)             XP12 pin 15
 *   PD6 = SCLK (hypothesis)     XP12 pin 5
 *   PC8 = MISO (DO)             XP12 pin 17
 *
 * CS macros used by sd_routines.c.
 * SPI_transmit() / SPI_receive() implemented in spi_sd.c.
 */

#ifndef SPI_SD_H_
#define SPI_SD_H_

#include <stdint.h>

/* GPIOD BSRR — used by CS macros and spi_sd.c */
#define GPIOD_BSRR  (*((volatile uint32_t *)0x40020C18UL))

/* CS = PD0, active low.
** BSRR bit 0  = set PD0 high (deassert CS)
** BSRR bit 16 = set PD0 low  (assert CS) */
#define SD_CS_ASSERT    do { GPIOD_BSRR = (1UL << 16); } while(0)
#define SD_CS_DEASSERT  do { GPIOD_BSRR = (1UL << 0);  } while(0)

/* Configure GPIO and set speed for SD initialisation (≤400kHz) */
void spi_sd_set_slow(void);

/* Switch to full-speed bit-bang after card init */
void spi_sd_set_fast(void);

/* Transmit one byte, return received byte */
unsigned char SPI_transmit(unsigned char data);

/* Receive one byte (sends 0xFF) */
unsigned char SPI_receive(void);

#endif /* SPI_SD_H_ */
