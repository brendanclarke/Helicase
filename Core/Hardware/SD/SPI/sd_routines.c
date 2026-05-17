/*
 * sd_routines.c
 *
 * SD card SPI protocol for STM32F765.
 * Ported from CC Dharmani / Julian Schmidt original.
 *
 * Changes from original:
 *   - StdPeriph GPIO calls → SD_CS_ASSERT/SD_CS_DEASSERT macros (bare BSRR)
 *   - SPI_SD / SPI_HIGH_SPEED macros → spi_sd_set_slow() / spi_sd_set_fast()
 *   - encode_setInterrupt() calls removed (not applicable)
 *   - FAT_TESTING_ONLY defined (no multi-block needed for kit browser)
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


#include "sd_routines.h"
#include "spi_sd.h"
#include <stdint.h>

volatile unsigned char SDHC_flag = 0;
volatile unsigned char cardType  = 0;

/* -----------------------------------------------------------------------
** SD_init — initialise SD card in SPI mode
** Returns 0 on success, 1 = card not detected, 2 = init failed.
** Call spi_sd_set_slow() before this, spi_sd_set_fast() after success.
** ----------------------------------------------------------------------- */
unsigned char SD_init(void)
{
    unsigned char i, response, SD_version;
    unsigned int retry = 0;

    /* 80 clock pulses with CS deasserted */
    SD_CS_DEASSERT;
    for (i = 0; i < 10; i++)
        SPI_transmit(0xFF);

    SD_CS_ASSERT;
    do {
        response = SD_sendCommand(GO_IDLE_STATE, 0);
        retry++;
        if (retry > 0x20)
            return 1;  /* timeout, no card */
    } while (response != 0x01);

    SD_CS_DEASSERT;
    SPI_transmit(0xFF);
    SPI_transmit(0xFF);

    retry = 0;
    SD_version = 2;

    do {
        response = SD_sendCommand(SEND_IF_COND, 0x000001AA);
        retry++;
        if (retry > 0xFE) {
            SD_version = 1;
            cardType   = 1;
            break;
        }
    } while (response != 0x01);

    retry = 0;

    do {
        SD_sendCommand(APP_CMD, 0);
        response = SD_sendCommand(SD_SEND_OP_COND, 0x40000000);
        retry++;
        if (retry > 0xFE)
            return 2;
    } while (response != 0x00);

    retry    = 0;
    SDHC_flag = 0;

    if (SD_version == 2) {
        do {
            response = SD_sendCommand(READ_OCR, 0);
            retry++;
            if (retry > 0xFE) {
                cardType = 0;
                break;
            }
        } while (response != 0x00);

        if (SDHC_flag == 1) cardType = 2;
        else                 cardType = 3;
    }

    return 0;
}

/* -----------------------------------------------------------------------
** SD_sendCommand
** ----------------------------------------------------------------------- */
unsigned char SD_sendCommand(unsigned char cmd, unsigned long arg)
{
    unsigned char response, retry = 0, status;

    /* SD cards need byte address; SDHC uses block address */
    if (SDHC_flag == 0)
        if (cmd == READ_SINGLE_BLOCK  ||
            cmd == READ_MULTIPLE_BLOCKS ||
            cmd == WRITE_SINGLE_BLOCK ||
            cmd == WRITE_MULTIPLE_BLOCKS ||
            cmd == ERASE_BLOCK_START_ADDR ||
            cmd == ERASE_BLOCK_END_ADDR)
            arg = arg << 9;

    SD_CS_ASSERT;

    SPI_transmit(cmd | 0x40);
    SPI_transmit((unsigned char)(arg >> 24));
    SPI_transmit((unsigned char)(arg >> 16));
    SPI_transmit((unsigned char)(arg >> 8));
    SPI_transmit((unsigned char)(arg));

    if (cmd == SEND_IF_COND)
        SPI_transmit(0x87);  /* CRC for CMD8 */
    else
        SPI_transmit(0x95);  /* CRC for CMD0 (ignored for others) */

    while ((response = SPI_receive()) == 0xFF)
        if (retry++ > 0xFE) break;

    if (response == 0x00 && cmd == READ_OCR) {
        status = SPI_receive() & 0x40;
        if (status == 0x40) SDHC_flag = 1;
        else                SDHC_flag = 0;
        SPI_receive();
        SPI_receive();
        SPI_receive();
    }

    SPI_receive();  /* extra 8 clocks */
    SD_CS_DEASSERT;

    return response;
}

/* -----------------------------------------------------------------------
** SD_readSingleBlockCustomBuffer — read 512 bytes into caller buffer
** ----------------------------------------------------------------------- */
unsigned char SD_readSingleBlockCustomBuffer(unsigned long startBlock, uint8_t *target)
{
    unsigned char response;
    unsigned int  i, retry = 0;

    response = SD_sendCommand(READ_SINGLE_BLOCK, startBlock);
    if (response != 0x00) return response;

    SD_CS_ASSERT;

    while (SPI_receive() != 0xFE)
        if (retry++ > 0xFFFE) { SD_CS_DEASSERT; return 1; }

    for (i = 0; i < 512; i++)
        target[i] = SPI_receive();

    SPI_receive();  /* CRC high */
    SPI_receive();  /* CRC low  */
    SPI_receive();  /* extra 8 clocks */

    SD_CS_DEASSERT;
    return 0;
}

/* -----------------------------------------------------------------------
** SD_writeSingleBlockCustomBuffer — write 512 bytes from caller buffer
** ----------------------------------------------------------------------- */
unsigned char SD_writeSingleBlockCustomBuffer(unsigned long startBlock, uint8_t *source)
{
    unsigned char response;
    unsigned int  i, retry = 0;

    response = SD_sendCommand(WRITE_SINGLE_BLOCK, startBlock);
    if (response != 0x00) return response;

    SD_CS_ASSERT;

    SPI_transmit(0xFE);  /* start block token */

    for (i = 0; i < 512; i++)
        SPI_transmit(source[i]);

    SPI_transmit(0xFF);  /* dummy CRC */
    SPI_transmit(0xFF);

    response = SPI_receive();
    if ((response & 0x1F) != 0x05) {
        SD_CS_DEASSERT;
        return response;
    }

    while (!SPI_receive())
        if (retry++ > 0xFFFE) { SD_CS_DEASSERT; return 1; }

    SD_CS_DEASSERT;
    SPI_transmit(0xFF);
    SD_CS_ASSERT;

    while (!SPI_receive())
        if (retry++ > 0xFFFE) { SD_CS_DEASSERT; return 1; }

    SD_CS_DEASSERT;
    return 0;
}
