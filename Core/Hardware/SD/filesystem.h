/*
 * Core/Hardware/SD/filesystem.h
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
 * filesystem.h - public SD filesystem facade for the LXR-02 port.
 *
 * Non-SD client code should include this header only. The asyncfatfs,
 * raw SD, and bit-banged SPI headers are private implementation details.
 */
#ifndef FILESYSTEM_H_
#define FILESYSTEM_H_

#include <stdbool.h>
#include <stdint.h>
#include "InstrumentManager.h"

typedef enum {
    FS_FILE_KIT = 0,
    FS_FILE_PATTERN,
    FS_FILE_MORPH,
    FS_FILE_PERFORMANCE,
    FS_FILE_ALL,
    FS_FILE_GLOBALS,
    FS_FILE_SAMPLES,
} fs_file_type_t;

typedef enum {
    FS_STATUS_IDLE,
    FS_STATUS_BUSY,
    FS_STATUS_DONE,
    FS_STATUS_ERROR,
} fs_status_t;

typedef enum {
    FS_OP_NONE,
    FS_OP_LOAD,
    FS_OP_SAVE,
    FS_OP_SCAN_KITS,
    FS_OP_SCAN_INSTRUMENTS,
    FS_OP_LOAD_NAME,
} fs_op_kind_t;

typedef enum {
    FS_MOUNT_RESULT_UNKNOWN = 0,
    FS_MOUNT_RESULT_READY,
    FS_MOUNT_RESULT_NO_CARD,
    FS_MOUNT_RESULT_CARD_INIT_FAILED,
    FS_MOUNT_RESULT_UNSUPPORTED_CARD,
    FS_MOUNT_RESULT_MOUNT_FAILED,
} fs_mount_result_t;

typedef enum {
    FS_STALE_WARNING_NONE = 0,
    FS_STALE_WARNING_GLO,
    FS_STALE_WARNING_ALL,
} fs_stale_warning_source_t;

typedef void (*fs_completion_cb_t)(void);

uint8_t     filesystem_initCardAndMountBlocking(void);
void        filesystem_initAfterCardReady(void);
void        filesystem_tick(void);
fs_status_t filesystem_status(void);
void        filesystem_ack(void);

bool filesystem_requestLoad(fs_file_type_t type, uint8_t slot, fs_completion_cb_t cb);
bool filesystem_requestSave(fs_file_type_t type, uint8_t slot, fs_completion_cb_t cb);
bool filesystem_requestLoadName(fs_file_type_t type, uint8_t slot, fs_completion_cb_t cb);
bool filesystem_requestScanKits(fs_completion_cb_t cb);
bool filesystem_requestScanInstruments(fs_completion_cb_t cb);
bool filesystem_requestLoadInstrument(uint8_t destination_slot,
                                      instrument_type_t type,
                                      uint8_t browser_index,
                                      fs_completion_cb_t cb);
uint8_t filesystem_installSamplesBlocking(void);
uint8_t filesystem_installLoopsBlocking(void);

/* Return the most recent eight-character name produced by a load-name request.
 *
 * Input: none. Output: pointer to filesystem-owned storage that remains valid
 * until the next filesystem_requestLoadName() or filesystem_start() operation
 * reuses it. Clients: presetManager.c and kitBrowser.c display name browsing.
 */
const char *filesystem_loadedName(void);

/* Query the Phase 2 Kit/ scan cache for a numbered kit folder.
 *
 * Input: zero_based_slot is the internal slot index used by preset/menu code;
 * SD folder names are one-based 001 Name through 128 Name, with underscore
 * accepted as a compatibility separator. Output: nonzero when
 * filesystem_requestScanKits() has found a matching Kit/NNN Name directory.
 * Clients: menu.c and any future load/save UI that must show explicit Empty
 * slots without trying to open a missing directory.
 */
uint8_t     filesystem_kitSlotExists(uint8_t zero_based_slot);

/* Return the eight-character display name from the Phase 2 Kit/ scan cache.
 *
 * Input: zero_based_slot as above. Output: filesystem-owned eight printable
 * characters plus NUL for existing kits, or the literal "Empty   " for missing
 * slots. Client: menu_repaintLoadSavePage() displays kit names directly from
 * the directory cache instead of reading legacy .SND headers.
 */
const char *filesystem_kitSlotName(uint8_t zero_based_slot);
uint8_t     filesystem_instrumentCount(instrument_type_t type);
const char *filesystem_instrumentName(instrument_type_t type,
                                      uint8_t browser_index);
uint16_t    filesystem_instrumentDisplayIndex(instrument_type_t type,
                                               uint8_t browser_index);
uint8_t     filesystem_diagOp(void);
uint8_t     filesystem_diagPhase(void);
uint32_t    filesystem_diagBytesDone(void);
fs_mount_result_t filesystem_lastMountResult(void);
uint8_t           filesystem_bootDetectedUnsupportedCard(void);

/* Session 025: stale globals are not fatal filesystem errors. The load path
** applies a safe subset/default fallback, then latches this one-shot source so
** menu.c can show "old settings" after the load UI is done. Reading clears it. */
fs_stale_warning_source_t filesystem_takeStaleGlobalsWarning(void);

#if FILESYSTEM_DIAGNOSTICS
uint8_t filesystem_diagRawCmd0(void);
#endif

#endif /* FILESYSTEM_H_ */
