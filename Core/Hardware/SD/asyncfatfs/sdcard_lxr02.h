/*
 * Core/Hardware/SD/asyncfatfs/sdcard_lxr02.h
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
 * sdcard_lxr02.h — SD card driver shim for asyncfatfs on LXR-02.
 *
 * This header re-exports the sdcard.h interface that asyncfatfs expects,
 * plus any LXR-02-specific declarations.
 *
 * asyncfatfs.c includes this instead of sdcard.h directly.
 */

#ifndef SDCARD_LXR02_H_
#define SDCARD_LXR02_H_

#include "sdcard.h"

/* Diagnostic: returns internal state machine state (0=IDLE, 1=SENDING_CMD, etc.) */
uint8_t sdcard_getState(void);

/*
 * Read-only SD transport copy used by boot-time failure forensics.
 *
 * What: reports the current private transfer coordinates without exposing the
 * private state-machine object. Why: AsyncFATFS may be correctly waiting for
 * an SD command that has stopped advancing, and aborting that command clears
 * the only evidence. Input: none; output: scalar copy valid until the next
 * poll/abort. This getter never clocks SPI, invokes callbacks, or changes
 * retries, chip-select, or transfer ownership; filesystem.c copies it before
 * sdcard_abortTransferForBootLog().
 */
typedef struct {
    uint8_t state;
    uint8_t operation;
    uint8_t callback_pending;
    uint32_t block;
    uint16_t offset;
    uint16_t retry_count;
} sdcardTransportSnapshot_t;

void sdcard_getTransportSnapshot(sdcardTransportSnapshot_t *snapshot);

/*
 * Abandon one LXR-02 SD block transfer for boot-log recovery.
 *
 * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
 * must never print anything to the screen or otherwise delay operations
 * unnecessarily since logging may be used to assess timing failures in other
 * modules that might otherwise be obscured by screen write delays.
 *
 * What: deasserts chip select and clears the private transfer callback/state
 * without reporting completion. Why: a DEV_MODE_LOGGING timeout discards the
 * owning asyncfatfs image before it remounts to write `/bootlog.bin`; a delayed
 * callback into that discarded image would corrupt the recovery mount.
 * Inputs: the current read/write shim state. Outputs/effects: transport becomes
 * idle and the interrupted operation is lost. This is not a general runtime
 * cancellation API. Affiliates: filesystem_writeBootFailureLogBlocking(),
 * afatfs_destroy(true), and SD_init().
 */
void sdcard_abortTransferForBootLog(void);

#endif /* SDCARD_LXR02_H_ */
