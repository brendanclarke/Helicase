/*
 * sd_routines.h
 *
 * SD card SPI protocol routines for STM32F765.
 * Ported from original LXR mainboard sd_routines.h / SD_routines.c
 * by CC Dharmani / Julian Schmidt.
 *
 * StdPeriph GPIO macros replaced with bare-metal BSRR writes.
 * CS pin is PA8 (active LOW), matching confirmed hardware.
 */

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */


#ifndef SD_ROUTINES_H_
#define SD_ROUTINES_H_

#include <stdint.h>
#include "spi_sd.h"

/* SD command definitions */
#define GO_IDLE_STATE           0
#define SEND_OP_COND            1
#define SEND_IF_COND            8
#define SEND_CSD                9
#define STOP_TRANSMISSION       12
#define SEND_STATUS             13
#define SET_BLOCK_LEN           16
#define READ_SINGLE_BLOCK       17
#define READ_MULTIPLE_BLOCKS    18
#define WRITE_SINGLE_BLOCK      24
#define WRITE_MULTIPLE_BLOCKS   25
#define ERASE_BLOCK_START_ADDR  32
#define ERASE_BLOCK_END_ADDR    33
#define ERASE_SELECTED_BLOCKS   38
#define SD_SEND_OP_COND         41   /* ACMD41 */
#define APP_CMD                 55
#define READ_OCR                58
#define CRC_ON_OFF              59

extern volatile unsigned char SDHC_flag;
extern volatile unsigned char cardType;

/*
 * Enter the card's SPI initialization state during pre-audio boot.
 *
 * Inputs: spi_sd_set_slow() has configured the bus and the shared TIM6
 * millisecond counter is running. Output: zero after CMD0/CMD8/ACMD41/OCR
 * establish a ready card, one when CMD0 cannot find a card, or two when card
 * readiness times out. ACMD41 attempts are paced in real milliseconds rather
 * than a CPU-speed counter loop so fast and slow cards receive the same
 * bounded one-second initialization window.
 */
unsigned char SD_init(void);
unsigned char SD_sendCommand(unsigned char cmd, unsigned long arg);
unsigned char SD_readSingleBlockCustomBuffer(unsigned long startBlock, uint8_t *target);
unsigned char SD_writeSingleBlockCustomBuffer(unsigned long startBlock, uint8_t *source);

#endif /* SD_ROUTINES_H_ */
