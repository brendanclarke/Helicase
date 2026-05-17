/*
 * Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c
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
 * sdcard_lxr02.c — SD card driver shim for asyncfatfs.
 *
 * Implements the sdcard.h interface (sdcard_readBlock, sdcard_writeBlock,
 * sdcard_poll) on top of the existing spi_sd.c bit-bang SPI primitives.
 *
 * Each sdcard_poll() call clocks a burst of SDCARD_BURST_SIZE bytes.
 * A 512-byte sector completes in 512/SDCARD_BURST_SIZE polls.
 *
 * This file sends CMD17/CMD24 directly via SPI_transmit() rather than
 * using SD_sendCommand() from sd_routines.c, because SD_sendCommand()
 * deasserts CS after the response — we need CS to remain asserted
 * throughout the data transfer.
 *
 * SDHC cards use block addressing (blockIndex = sector number).
 * Standard SD cards use byte addressing (blockIndex << 9).
 * We check SDHC_flag from sd_routines.c to handle this.
 *
 * SD bit-bang SPI is NOT re-entrant. sdcard_poll() must only be called
 * from one context at a time (main loop OR ISR, never both).
 */

#include "sdcard_lxr02.h"
#include "spi_sd.h"
#include "sd_routines.h"   /* SDHC_flag, CMD defines */
#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
** Configuration
** ----------------------------------------------------------------------- */
#define SDCARD_BURST_SIZE       16      /* bytes per sdcard_poll() call    */
#define SDCARD_TOKEN_TIMEOUT    5000    /* polls waiting for 0xFE token    */
#define SDCARD_BUSY_TIMEOUT     50000   /* polls waiting for card ready    */

/* -----------------------------------------------------------------------
** State machine
** ----------------------------------------------------------------------- */
typedef enum {
    SDCARD_STATE_IDLE,
    SDCARD_STATE_SENDING_CMD,
    SDCARD_STATE_READING_WAIT_TOKEN,
    SDCARD_STATE_READING_DATA,
    SDCARD_STATE_READING_CRC,
    SDCARD_STATE_WRITING_TOKEN,
    SDCARD_STATE_WRITING_DATA,
    SDCARD_STATE_WRITING_CRC,
    SDCARD_STATE_WRITING_WAIT_BUSY,
} sdcard_state_t;

static sdcard_state_t state = SDCARD_STATE_IDLE;
static uint8_t       *xfer_buffer;
static uint32_t       xfer_block;
static uint16_t       xfer_offset;
static uint16_t       retry_count;
static sdcard_operationCompleteCallback_c xfer_callback;
static uint32_t       xfer_callbackData;
static sdcardBlockOperation_e xfer_operation;

/* -----------------------------------------------------------------------
** send_cmd_keep_cs — send SD command, keep CS asserted, return R1
**
** Unlike SD_sendCommand() in sd_routines.c, this does NOT deassert CS
** after the response. The caller is responsible for CS management.
**
** Handles SDHC vs standard addressing: SDHC uses block address directly,
** standard SD shifts left by 9 for byte addressing.
** ----------------------------------------------------------------------- */
static uint8_t send_cmd_keep_cs(uint8_t cmd, uint32_t arg)
{
    uint8_t response;
    uint8_t retry = 0;

    /* SDHC uses block addressing; standard SD uses byte addressing */
    if (SDHC_flag == 0) {
        if (cmd == READ_SINGLE_BLOCK || cmd == WRITE_SINGLE_BLOCK)
            arg = arg << 9;
    }

    SD_CS_ASSERT;

    SPI_transmit(cmd | 0x40);
    SPI_transmit((uint8_t)(arg >> 24));
    SPI_transmit((uint8_t)(arg >> 16));
    SPI_transmit((uint8_t)(arg >> 8));
    SPI_transmit((uint8_t)(arg));
    SPI_transmit(0x95);  /* dummy CRC (CMD0 CRC, ignored for others) */

    while ((response = SPI_receive()) == 0xFF)
        if (retry++ > 0xFE) break;

    /* Do NOT deassert CS — caller manages CS lifecycle */
    return response;
}

/* -----------------------------------------------------------------------
** sdcard_readBlock
** ----------------------------------------------------------------------- */
bool sdcard_readBlock(uint32_t blockIndex, uint8_t *buffer,
                      sdcard_operationCompleteCallback_c callback,
                      uint32_t callbackData)
{
    if (state != SDCARD_STATE_IDLE) return false;

    uint8_t r1 = send_cmd_keep_cs(READ_SINGLE_BLOCK, blockIndex);
    if (r1 != 0x00) {
        SD_CS_DEASSERT;
        SPI_transmit(0xFF);
        return false;
    }

    /* CS is asserted, command accepted — now wait for data token */
    xfer_buffer       = buffer;
    xfer_block        = blockIndex;
    xfer_callback     = callback;
    xfer_callbackData = callbackData;
    xfer_operation    = SDCARD_BLOCK_OPERATION_READ;
    xfer_offset       = 0;
    retry_count       = 0;
    state             = SDCARD_STATE_READING_WAIT_TOKEN;

    return true;
}

/* -----------------------------------------------------------------------
** sdcard_writeBlock
** ----------------------------------------------------------------------- */
sdcardOperationStatus_e sdcard_writeBlock(uint32_t blockIndex, uint8_t *buffer,
                                          sdcard_operationCompleteCallback_c callback,
                                          uint32_t callbackData)
{
    if (state != SDCARD_STATE_IDLE) return SDCARD_OPERATION_BUSY;

    uint8_t r1 = send_cmd_keep_cs(WRITE_SINGLE_BLOCK, blockIndex);
    if (r1 != 0x00) {
        SD_CS_DEASSERT;
        SPI_transmit(0xFF);
        return SDCARD_OPERATION_FAILURE;
    }

    /* CS is asserted, command accepted — send data token then data */
    xfer_buffer       = buffer;
    xfer_block        = blockIndex;
    xfer_callback     = callback;
    xfer_callbackData = callbackData;
    xfer_operation    = SDCARD_BLOCK_OPERATION_WRITE;
    xfer_offset       = 0;
    state             = SDCARD_STATE_WRITING_TOKEN;

    return SDCARD_OPERATION_IN_PROGRESS;
}

/* -----------------------------------------------------------------------
** sdcard_poll — advance current transfer by one burst
**
** Returns true if the card is idle (ready for new commands).
** Called by afatfs_poll().
** ----------------------------------------------------------------------- */
bool sdcard_poll(void)
{
    switch (state) {

    case SDCARD_STATE_IDLE:
        return true;

    /* ---- READ path ---- */
    case SDCARD_STATE_READING_WAIT_TOKEN:
    {
        uint8_t r = SPI_receive();
        if (r == 0xFE) {
            state = SDCARD_STATE_READING_DATA;
            xfer_offset = 0;
        } else if (++retry_count > SDCARD_TOKEN_TIMEOUT) {
            SD_CS_DEASSERT;
            SPI_transmit(0xFF);
            state = SDCARD_STATE_IDLE;
            if (xfer_callback)
                xfer_callback(SDCARD_BLOCK_OPERATION_READ,
                              xfer_block, NULL, xfer_callbackData);
            return true;
        }
        return false;
    }

    case SDCARD_STATE_READING_DATA:
    {
        uint16_t end = xfer_offset + SDCARD_BURST_SIZE;
        if (end > 512) end = 512;
        while (xfer_offset < end)
            xfer_buffer[xfer_offset++] = SPI_receive();
        if (xfer_offset >= 512)
            state = SDCARD_STATE_READING_CRC;
        return false;
    }

    case SDCARD_STATE_READING_CRC:
        SPI_receive();       /* CRC byte 1 */
        SPI_receive();       /* CRC byte 2 */
        SD_CS_DEASSERT;
        SPI_transmit(0xFF);  /* extra 8 clocks */
        state = SDCARD_STATE_IDLE;
        if (xfer_callback)
            xfer_callback(SDCARD_BLOCK_OPERATION_READ,
                          xfer_block, xfer_buffer, xfer_callbackData);
        return true;

    /* ---- WRITE path ---- */
    case SDCARD_STATE_WRITING_TOKEN:
        SPI_transmit(0xFE);  /* start block token */
        state = SDCARD_STATE_WRITING_DATA;
        xfer_offset = 0;
        return false;

    case SDCARD_STATE_WRITING_DATA:
    {
        uint16_t end = xfer_offset + SDCARD_BURST_SIZE;
        if (end > 512) end = 512;
        while (xfer_offset < end)
            SPI_transmit(xfer_buffer[xfer_offset++]);
        if (xfer_offset >= 512)
            state = SDCARD_STATE_WRITING_CRC;
        return false;
    }

    case SDCARD_STATE_WRITING_CRC:
        SPI_transmit(0xFF);  /* dummy CRC byte 1 */
        SPI_transmit(0xFF);  /* dummy CRC byte 2 */
        /* Read data response token */
        {
            uint8_t resp = SPI_receive();
            if ((resp & 0x1F) != 0x05) {
                /* Write rejected by card */
                SD_CS_DEASSERT;
                SPI_transmit(0xFF);
                state = SDCARD_STATE_IDLE;
                if (xfer_callback)
                    xfer_callback(SDCARD_BLOCK_OPERATION_WRITE,
                                  xfer_block, NULL, xfer_callbackData);
                return true;
            }
        }
        state = SDCARD_STATE_WRITING_WAIT_BUSY;
        retry_count = 0;
        return false;

    case SDCARD_STATE_WRITING_WAIT_BUSY:
    {
        uint8_t r = SPI_receive();
        if (r != 0x00) {
            /* Card is ready */
            SD_CS_DEASSERT;
            SPI_transmit(0xFF);
            state = SDCARD_STATE_IDLE;
            if (xfer_callback)
                xfer_callback(SDCARD_BLOCK_OPERATION_WRITE,
                              xfer_block, xfer_buffer, xfer_callbackData);
            return true;
        }
        if (++retry_count > SDCARD_BUSY_TIMEOUT) {
            SD_CS_DEASSERT;
            SPI_transmit(0xFF);
            state = SDCARD_STATE_IDLE;
            if (xfer_callback)
                xfer_callback(SDCARD_BLOCK_OPERATION_WRITE,
                              xfer_block, NULL, xfer_callbackData);
            return true;
        }
        return false;
    }

    default:
        return true;
    }
}

/* -----------------------------------------------------------------------
** Multi-block write stubs — not used (AFATFS_MIN_MULTIPLE_BLOCK_WRITE_COUNT
** is undefined in our asyncfatfs.c). Provided for interface completeness.
** ----------------------------------------------------------------------- */
sdcardOperationStatus_e sdcard_beginWriteBlocks(uint32_t blockIndex, uint32_t blockCount)
{
    (void)blockIndex;
    (void)blockCount;
    return SDCARD_OPERATION_SUCCESS;
}

sdcardOperationStatus_e sdcard_endWriteBlocks(void)
{
    return SDCARD_OPERATION_SUCCESS;
}

void sdcard_setProfilerCallback(sdcard_profilerCallback_c callback)
{
    (void)callback;
}

uint8_t sdcard_getState(void)
{
    return (uint8_t)state;
}
