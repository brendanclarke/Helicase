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

/*
 * Transfer work remains cooperative: one sdcard_poll() clocks one bounded
 * token/busy byte or SDCARD_BURST_SIZE data bytes. Response-wait abandonment,
 * however, is measured against TIM6 milliseconds rather than poll calls.
 *
 * Why: callers may legally change foreground poll density, including S058's
 * four consecutive AsyncFATFS polls while audio is suspended. Inputs are the
 * foreground poll stream and the interrupt-owned time_sysTick observer.
 * Outputs are unchanged SD callbacks and transfer states; extra polls advance
 * useful SPI work but cannot shorten a protocol deadline. Affiliates:
 * filesystem_tick(), afatfs_poll(), timebase.h, and SDCARD_BURST_SIZE.
 */

#include "sdcard_lxr02.h"
#include "spi_sd.h"
#include "sd_routines.h"   /* SDHC_flag, CMD defines */
#include "config.h"
#include "timebase.h"
#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
** Configuration
** ----------------------------------------------------------------------- */
/*
 * Bound asynchronous SD response phases in real milliseconds.
 *
 * What: allows up to 1 s for a CMD17 data token and 5 s for CMD24 program-busy
 * release while retaining the existing 16-byte data burst. Why: token/busy
 * latency belongs to the card, whereas poll density belongs to the caller;
 * coupling them made stopped-playback fast drain reject healthy writes.
 * Inputs: TIM6's 1 kHz time_sysTick and the state-entry timestamp. Outputs:
 * the existing success or null-buffer completion path after a stable elapsed
 * deadline. Both intervals remain below the project's 32,768 ms safe
 * uint16_t comparison range. Affiliates: sdcard_waitTimedOut(),
 * READING_WAIT_TOKEN, WRITING_WAIT_BUSY, and filesystem fast drain.
 */
#define SDCARD_BURST_SIZE       16u
#define SDCARD_TOKEN_TIMEOUT_MS 1000u
#define SDCARD_BUSY_TIMEOUT_MS  5000u

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
/*
 * Start time for the one currently active asynchronous SD response wait.
 *
 * Writer/lifetime: read admission owns it through READING_WAIT_TOKEN; an
 * accepted write-data response owns it through WRITING_WAIT_BUSY. Every exit
 * from either wait and boot-log abort clears it before invoking a callback.
 * Input is time_sysTick at wait entry; consumers produce timeout decisions or
 * diagnostic elapsed milliseconds. It has no meaning in data/CRC/idle states.
 * This repurposes retry_count's existing two bytes and adds no SRAM. Affiliates:
 * sdcard_waitTimedOut(), sdcard_getTransportSnapshot(), and the two wait states.
 */
static uint16_t       wait_started_tick;
static sdcard_operationCompleteCallback_c xfer_callback;
static uint32_t       xfer_callbackData;
static sdcardBlockOperation_e xfer_operation;

/*
 * Test one active SD response wait against foreground-independent time.
 *
 * What: subtracts the saved wait-entry tick from TIM6's wrapping millisecond
 * counter. Why: poll frequency changes when audio is suspended and can change
 * with any future drain budget, while a protocol deadline must retain one
 * real-time duration. Input: timeout_ms, nonzero and below 32,768 ms; the
 * current wait_started_tick and time_sysTick are private affiliates. Output:
 * one when elapsed time has reached the limit, otherwise zero. Side effects:
 * none - this function sends no clocks and changes no state, callback, buffer,
 * deadline, or chip select. Affiliates: READING_WAIT_TOKEN,
 * WRITING_WAIT_BUSY, timebase.h, and sdcard_getTransportSnapshot().
 */
static uint8_t sdcard_waitTimedOut(uint16_t timeout_ms)
{
    return (uint8_t)((uint16_t)(time_sysTick - wait_started_tick) >=
                     timeout_ms);
}

void sdcard_getTransportSnapshot(sdcardTransportSnapshot_t *snapshot)
{
    /*
     * Copy the live SD transfer for boot-time failure forensics.
     *
     * What: reports scalar transfer coordinates and elapsed milliseconds only
     * for an active read-token or write-busy wait. Why: boot recovery destroys
     * the transport state, and a real-time timeout must be diagnosed in its own
     * unit rather than as the retired number of caller polls. Input: caller-
     * owned, non-null snapshot plus private driver state. Output: a copy valid
     * until the next poll/abort; wait_ms is zero outside READING_WAIT_TOKEN and
     * WRITING_WAIT_BUSY. Side effects: none - no SPI clock, callback, allocation,
     * chip-select change, deadline reset, or ownership mutation. Affiliates:
     * sdcardTransportSnapshot_t, wait_started_tick, time_sysTick,
     * filesystem_hcprmsCapsuleFreeze(), and HCPRMS schema 2.
     */
    if (!snapshot)
        return;
    snapshot->state = (uint8_t)state;
    snapshot->operation = (uint8_t)xfer_operation;
    snapshot->callback_pending = (xfer_callback != NULL) ? 1u : 0u;
    snapshot->block = xfer_block;
    snapshot->offset = xfer_offset;
    snapshot->wait_ms =
        (state == SDCARD_STATE_READING_WAIT_TOKEN ||
         state == SDCARD_STATE_WRITING_WAIT_BUSY)
        ? (uint16_t)(time_sysTick - wait_started_tick) : 0u;
}

void sdcard_abortTransferForBootLog(void)
{
    /*
     * Tear down only the transport half of a timed-out boot transaction.
     *
     * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
     * must never print anything to the screen or otherwise delay operations
     * unnecessarily since logging may be used to assess timing failures in
     * other modules that might otherwise be obscured by screen write delays.
     *
     * Inputs: DEV_MODE_LOGGING timeout may arrive while CMD17/CMD24 owns CS, a
     * caller buffer, and an asyncfatfs completion callback. Outputs/effects:
     * CS is released, idle clocks are supplied, and every retained transfer
     * coordinate is cleared without invoking the stale callback. Why: the
     * following dirty afatfs_destroy() invalidates that callback's cache
     * descriptor. Affiliates: sdcard_poll(), filesystem boot-log recovery, and
     * the subsequent full SD_init() protocol reset.
     *
     * The abort also retires any active response-wait timestamp before
     * discarding the callback. Input may be any transfer state; output is an
     * idle transport with no live deadline. Why: the diagnostic snapshot is
     * frozen before this call, and the recovery mount must not inherit timing
     * ownership from the destroyed operation. Affiliates:
     * sdcard_getTransportSnapshot(), wait_started_tick, afatfs_destroy(true),
     * and boot-log remount recovery.
     */
#if DEV_MODE_LOGGING
    SD_CS_DEASSERT;
    SPI_transmit(0xFF);
    state = SDCARD_STATE_IDLE;
    xfer_buffer = NULL;
    xfer_block = 0u;
    xfer_offset = 0u;
    wait_started_tick = 0u;
    xfer_callback = NULL;
    xfer_callbackData = 0u;
    xfer_operation = SDCARD_BLOCK_OPERATION_READ;
#endif
}

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
    /*
     * Arm the read-token deadline only after CMD17 has been accepted.
     *
     * Inputs: accepted R1 response and current time_sysTick. Output: the
     * transfer owns wait_started_tick while entering READING_WAIT_TOKEN;
     * command transport and callback data remain unchanged. Why: command
     * transmission must not consume the card's token-response allowance, and
     * subsequent caller poll density must not alter it. Affiliates:
     * sdcard_waitTimedOut(), sdcard_poll(), and afatfs_sdcardReadComplete().
     */
    wait_started_tick = time_sysTick;
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
        /*
         * Poll one read-token response byte and bound only the wait in
         * milliseconds.
         *
         * Inputs: current SPI byte, wait_started_tick, and time_sysTick.
         * Outputs: 0xFE advances to READING_DATA; elapsed expiry releases CS
         * and reports the existing null-buffer read completion; any other byte
         * remains in this state. Why: S058 may call this state four times per
         * filesystem pass, so caller polls are not a valid duration. On either
         * terminal transition, clear the timestamp before any callback can
         * admit another transfer. Affiliates: sdcard_waitTimedOut(),
         * SDCARD_TOKEN_TIMEOUT_MS, SPI_receive(), and
         * afatfs_sdcardReadComplete().
         */
        if (r == 0xFE) {
            state = SDCARD_STATE_READING_DATA;
            xfer_offset = 0;
            wait_started_tick = 0u;
        } else if (sdcard_waitTimedOut(SDCARD_TOKEN_TIMEOUT_MS)) {
            SD_CS_DEASSERT;
            SPI_transmit(0xFF);
            state = SDCARD_STATE_IDLE;
            wait_started_tick = 0u;
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
        /*
         * Arm program-busy timing only after the card accepts the complete
         * block.
         *
         * Inputs: accepted data-response token and current time_sysTick.
         * Output: WRITING_WAIT_BUSY owns a fresh wait_started_tick. Why:
         * transmitting the start token, 512-byte payload, and CRC is
         * cooperative transfer work and must not consume the card's separate
         * internal-programming allowance. Rejected responses keep their
         * existing immediate failure callback and never enter or arm the busy
         * wait. Affiliates: WRITING_CRC, WRITING_WAIT_BUSY,
         * SDCARD_BUSY_TIMEOUT_MS, and afatfs_sdcardWriteComplete().
         */
        wait_started_tick = time_sysTick;
        state = SDCARD_STATE_WRITING_WAIT_BUSY;
        return false;

    case SDCARD_STATE_WRITING_WAIT_BUSY:
    {
        uint8_t r = SPI_receive();
        /*
         * Poll one program-busy byte and bound only the busy wait in
         * milliseconds.
         *
         * Inputs: current SPI byte, wait_started_tick, and time_sysTick.
         * Outputs: the first nonzero byte releases CS and reports the existing
         * successful buffer; elapsed expiry releases CS and reports the existing
         * null buffer; zero before expiry remains busy. Why: fast foreground
         * polling previously consumed a count ceiling before a healthy card
         * completed internal programming, after which AsyncFATFS re-dirtied and
         * retried the same sector indefinitely. Clear the timestamp before
         * either callback so a callback-admitted successor owns its own deadline.
         * Affiliates: sdcard_waitTimedOut(), SDCARD_BUSY_TIMEOUT_MS,
         * afatfs_sdcardWriteComplete(), cache retry policy, and
         * filesystem_tick() fast drain.
         */
        if (r != 0x00) {
            /* Card is ready */
            SD_CS_DEASSERT;
            SPI_transmit(0xFF);
            state = SDCARD_STATE_IDLE;
            wait_started_tick = 0u;
            if (xfer_callback)
                xfer_callback(SDCARD_BLOCK_OPERATION_WRITE,
                              xfer_block, xfer_buffer, xfer_callbackData);
            return true;
        }
        if (sdcard_waitTimedOut(SDCARD_BUSY_TIMEOUT_MS)) {
            SD_CS_DEASSERT;
            SPI_transmit(0xFF);
            state = SDCARD_STATE_IDLE;
            wait_started_tick = 0u;
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
