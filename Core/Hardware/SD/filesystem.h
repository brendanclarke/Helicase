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
#include "SceneData.h"

typedef enum {
    FS_FILE_KIT = 0,
    FS_FILE_SCENE,
    FS_FILE_BANK,
    FS_FILE_PATTERN,
    FS_FILE_MORPH,
    FS_FILE_PERFORMANCE,
    FS_FILE_ALL,
    FS_FILE_SETTINGS,
    FS_FILE_SAMPLES,
} fs_file_type_t;

#define FS_TEST_NAME_MAX 48u
#define FS_TEST_RESULT_BYTES 4u

typedef enum {
    FS_TEST_RESULT_BYTES_READY = 0,
    FS_TEST_RESULT_DIRECTORY
} fs_test_result_kind_t;

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
    FS_STALE_WARNING_ALL,
} fs_stale_warning_source_t;

/*
 * Root numbered-library domains served by the one generalized name cache.
 * Kit, root Scene, and root Bank indexes preserve slot order, including blank slots, so
 * callers can reconstruct the visible `NNN Name` folder key. Instrument
 * indexes remain typed and are requested through their existing API because
 * their rows are alphabetically sorted files rather than numbered folders.
 */
typedef enum {
    FS_LIBRARY_INDEX_KIT = 0,
    FS_LIBRARY_INDEX_SCENE,
    /* Root Bank uses the same slot-preserving cache/index contract. */
    FS_LIBRARY_INDEX_BANK,
} fs_library_index_kind_t;

typedef void (*fs_completion_cb_t)(void);

/*
 * Temporary boot diagnostic callback for the resident-name writer.
 *
 * DEV_MODE_DIAGNOSTIC displays runtime information on the screen for the user
 * to assess how operations are proceeding. It does not and should not ever add
 * additional file interaction steps, since the diagnostic may be used to
 * assess in-situ file procedures.
 *
 * phase identifies the live HCNAMES state: 0=root/open request, 1=open wait,
 * 2=row streaming, 3=close wait, 4=final media flush, 5=done, and 6=error.
 * row is the next fixed-order SRAM row to write (0..129). The callback is
 * observational only and must not start or acknowledge filesystem operations.
 * It exists to locate the current hardware boot freeze and should be removed
 * after the stalled phase has been confirmed.
 */
typedef void (*fs_hcnames_diag_cb_t)(uint8_t phase, uint16_t row);

/*
 * Temporary operation codes returned by filesystem_getBootDiagnostic().
 *
 * DEV_MODE_DIAGNOSTIC displays runtime information on the screen for the user
 * to assess how operations are proceeding. It does not and should not ever add
 * additional file interaction steps, since the diagnostic may be used to
 * assess in-situ file procedures.
 *
 * Stage 11 can contain a Bank-name repair followed by Bank, Scene, or Kit
 * payload loading and a final flush. Stable public codes keep the OLED output
 * interpretable without exposing the private fs_internal_op_t enum itself.
 * FS_BOOT_DIAG_OTHER means the active operation is outside that expected boot
 * chain. Remove this diagnostic surface after the hardware stall is located.
 */
typedef enum {
    FS_BOOT_DIAG_OTHER = 0u,
    FS_BOOT_DIAG_REPAIR_NAMES = 1u,
    FS_BOOT_DIAG_LOAD_BANK = 2u,
    FS_BOOT_DIAG_LOAD_SCENE = 3u,
    FS_BOOT_DIAG_LOAD_KIT = 4u,
    FS_BOOT_DIAG_FLUSH = 5u,
} fs_boot_diag_op_t;

/*
 * Temporary phase-43 substep callback used only during hardware diagnosis.
 *
 * DEV_MODE_DIAGNOSTIC displays runtime information on the screen for the user
 * to assess how operations are proceeding. It does not and should not ever add
 * additional file interaction steps, since the diagnostic may be used to
 * assess in-situ file procedures.
 *
 * The Bank embedded-Kit quarantine path contains synchronous component calls
 * inside one filesystem phase, so the ordinary operation/phase accessor cannot
 * identify which call fails to return. The callback receives a documented
 * substep immediately before that call begins. It must remain observational
 * and must not invoke filesystem APIs.
 */
typedef void (*fs_boot_substep_diag_cb_t)(uint8_t substep);

/*
 * Pre-audio filesystem timeout logger.
 *
 * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
 * must never print anything to the screen or otherwise delay operations
 * unnecessarily since logging may be used to assess timing failures in other
 * modules that might otherwise be obscured by screen write delays.
 *
 * What: Begin opens the diagnostic window, Arm copies exactly eight operation
 * bytes and restarts its deadline, TimedOut/Code expose the latched result,
 * the blocking writer abandons a timed-out owner and makes one bounded remount
 * attempt to replace `/bootlog.bin`, and End disables the policy before
 * runtime. Why: a splash-screen stall otherwise leaves no durable indication
 * of the last storage boundary entered. Inputs are fixed-width operation codes
 * and the existing filesystem/SD state; outputs are a timeout flag and, when
 * recovery succeeds, an exactly eight-byte root file. These functions are
 * boot/main-context APIs only, are not ISR-safe, and a failed recovery never
 * prevents the caller from continuing startup. Affiliates:
 * filesystem_tick(), the private blocking FAT helpers, main.c's pre-audio
 * ladder, and sdcard_abortTransferForBootLog().
 */
void        filesystem_bootLoggingBegin(void);
void        filesystem_bootLoggingArm(const char code[8]);
uint8_t     filesystem_bootLoggingTimedOut(void);
const uint8_t *filesystem_bootLoggingCode(void);
uint8_t     filesystem_writeBootTimeoutLogBlocking(void);
void        filesystem_bootLoggingEnd(void);

/*
 * Initialize and mount the SD card during pre-audio boot.
 *
 * Inputs: TIM6 millisecond timing is active and no runtime filesystem request
 * exists. Output: the bus is configured at initialization speed, held idle for
 * the warm-reset card-settle interval, initialized through the paced SD
 * command sequence, switched to transfer speed, and synchronously pumped until
 * asyncfatfs reports ready or fatal. Returns nonzero only for a ready mount.
 * This blocking API must not be called after audioCodec_init().
 */
uint8_t     filesystem_initCardAndMountBlocking(void);
void        filesystem_initAfterCardReady(void);
/*
 * Ensure the two root working-Bank autosave register files exist during
 * pre-audio boot.
 *
 * Inputs: a mounted card after the normal Bank-or-fallback ladder. Output:
 * when no resident Bank exists, return success without card I/O; otherwise
 * read HCNAMES and create only absent `/.hcprms1` or `/.hcprms2` records.
 *
 * New records are exact 34,768-byte baselines: a 64-byte validation header,
 * 3,856-byte mutation mask, 128-byte Bank section, and sixteen 1,920-byte
 * Scenes. Creation writes the current two-byte Bank restore slot plus names,
 * with mask/parameters/Effects/padding zero; `/.hcprms1` begins as the current
 * valid generation. An existing matching object is never opened for write.
 *
 * A successful return authorizes filesystem_tick()'s private parameter drain;
 * that runtime operation uses a separate cache and is not exposed here. An
 * error leaves it disabled so a missing pair cannot trigger recovery during
 * the boot ladder. If a complete FAT free-cluster search reports genuine
 * exhaustion, partial output is closed and this function returns zero rather
 * than trapping boot in a zero-byte fwrite retry.
 */
uint8_t     filesystem_ensureAutosaveFilesBlocking(void);
/*
 * Create/refresh one `.hcindex` file in every registry-defined Instrument
 * directory. This boot-only wrapper is valid before audio starts; normal
 * runtime SD work remains asynchronous through filesystem_tick(). It returns
 * nonzero only after every index file and the final FAT/data flush complete.
 */
uint8_t     filesystem_createBootIndexBlocking(void);
/*
 * Write the first-pass resident name register to `/.hcnames`.
 *
 * Inputs: resident BankData/Kit data after the boot load/fallback chain.
 * Output: a root SD-card text file containing fixed-order newline-delimited
 * names: one Bank row, sixteen Scene rows, sixteen embedded Kit rows, then
 * sixteen groups of six Instrument rows. Scene rows are deliberately blank in
 * this bootstrap writer because Scene identity is card-owned HCNAMES metadata,
 * not a scene_t field; successful root Scene and Bank operations subsequently
 * preserve/update them through the shared register cache. Rows with no loaded
 * object are blank. This is only a bootstrap writer, not a second name store.
 */
uint8_t filesystem_writeResidentNamesBlocking(
    fs_hcnames_diag_cb_t diagnostic_cb);
/*
 * Observe the active filesystem operation for the temporary boot-screen hook.
 *
 * DEV_MODE_DIAGNOSTIC displays runtime information on the screen for the user
 * to assess how operations are proceeding. It does not and should not ever add
 * additional file interaction steps, since the diagnostic may be used to
 * assess in-situ file procedures.
 *
 * Outputs: op receives one fs_boot_diag_op_t code and phase receives the
 * operation's current private state-machine phase. NULL outputs are allowed.
 * This function performs no polling, acknowledgement, or state mutation. For
 * Bank-name repair, phase 42 returns to root and phase 43 hands a successful
 * Bank Load into its payload reader without a second callback. The handoff
 * retains the original request callback; selected child Scene payloads then
 * progress through the ordinary foreground reader.
 */
void filesystem_getBootDiagnostic(uint8_t *op, uint8_t *phase);
/*
 * DEV_MODE_DIAGNOSTIC displays runtime information on the screen for the user
 * to assess how operations are proceeding. It does not and should not ever add
 * additional file interaction steps, since the diagnostic may be used to
 * assess in-situ file procedures.
 *
 * Register or clear the temporary phase-43 substep observer. Passing NULL
 * disables it. Registration changes no filesystem state or operation order.
 */
void filesystem_setBootSubstepDiagnostic(fs_boot_substep_diag_cb_t cb);
/*
 * Repair host-created long or duplicate product names before index generation.
 *
 * Inputs: a mounted card and one product namespace. Output: all accepted
 * objects in that namespace have canonical display components that can be
 * reconstructed from slot/type plus eight display cells. The implementation
 * performs one-object rename/flush steps and does not allocate a parallel
 * browser cache; FAT itself is not journaled, so this is ordered repair rather
 * than filesystem-level power-loss atomicity.
 */
uint8_t     filesystem_repairLibraryNamesBlocking(fs_library_index_kind_t kind);
uint8_t     filesystem_repairInstrumentNamesBlocking(void);
/*
 * Repair one root Bank folder and its Bank-local Scene children before load.
 *
 * Inputs: root Bank slot from the active Bank cache. Output: the selected Bank
 * directory and its immediate `00..15` children use canonical names before the
 * Bank payload reader captures Scene/Kit provenance. This prevents resident
 * state from inheriting long or duplicate host-created keys.
 */
bool filesystem_requestRepairBankNames(uint16_t slot, fs_completion_cb_t cb);
/*
 * Write one slot-ordered Kit, root Scene, or root Bank cache as `.hcindex` at
 * boot. If the requested domain is not active in the one shared cache, the
 * implementation first performs the matching physical directory scan so a
 * missed caller scan cannot silently omit the index.
 */
uint8_t     filesystem_createLibraryIndexBlocking(fs_library_index_kind_t kind);
void        filesystem_tick(void);
fs_status_t filesystem_status(void);
const char *filesystem_errorCode(void);
void        filesystem_ack(void);

bool filesystem_requestLoad(fs_file_type_t type, uint16_t slot, fs_completion_cb_t cb);
/*
 * Load one numbered Kit directory into every Scene selected by scene_mask.
 *
 * Inputs: direct Kit library slot 000..999, a bit per resident Scene, and completion
 * callback. Output: one asynchronous directory read whose staged payload is
 * copied into all selected Scene kits only after every instrument validates.
 * The selected index row is copied into existing operation scratch before the
 * separate typed stage begins, keeping the visible directory key stable for
 * the request. The 9,000-byte index cache remains dedicated to names; typed
 * staging is separate SRAM and does not alias the browser cache.
 * Clients: preset_loadKitForScenes() and boot through the same Preset API.
 * This cannot be folded into filesystem_requestLoad(): that generic request
 * has no scene-mask coordinate and must keep its historical active-Scene
 * compatibility behavior for non-UI callers.
 */
bool filesystem_requestLoadKitForScenes(uint16_t slot, uint16_t scene_mask,
                                        fs_completion_cb_t cb);
/*
 * Save one root Kit directory from a resident Scene.
 *
 * Inputs: direct root Kit slot, source Scene, eight-cell display name, Morph
 * projection flag, and completion callback. Output: asynchronous recursive
 * slot replacement in /Kit/. morph_projection=0 writes normal Kit endpoints;
 * morph_projection!=0 writes KitMrp's flattened current interpolation into
 * both normal and morph endpoint storage and does not rename the resident kit.
 * After the directory is durable, the filesystem rescans all of /Kit/ and
 * rewrites the complete slot-ordered `.hcindex` before invoking cb. This is
 * necessary for new, renamed, or removed numbered folders; patching only the
 * previously active cache row would leave stale index rows behind.
 */
bool filesystem_requestSaveKitDirectory(uint16_t slot,
                                        uint8_t source_scene,
                                        const char display_name[8],
                                        uint8_t morph_projection,
                                        fs_completion_cb_t cb);
/*
 * Save one root Scene directory from a resident Scene.
 *
 * Inputs: direct root Scene slot, source resident Scene, eight-cell display
 * name, and completion callback. Output: asynchronous replacement scoped to
 * same-number children under /Scene/: Scene/<NNN Name>/ with sceneset.scg,
 * embedded Kit directory, six instrument files, a draft text pattern.pat, and
 * effects.fx placeholder. pattern.pat stores only the 128x7 step-active grid
 * plus per-track length/scale until the final pattern schema exists. Other
 * numbered Scene directories must not be removed, regardless of how many nested
 * children they contain. The resident Scene display name updates only after the
 * directory save succeeds. After the directory is durable, the filesystem
 * rescans all of /Scene/ and rewrites the complete slot-ordered `.hcindex`
 * before invoking cb, so the index reflects the actual folder set rather than
 * only the selected save slot.
 */
bool filesystem_requestSaveSceneDirectory(uint16_t slot,
                                          uint8_t source_scene,
                                          const char display_name[8],
                                          fs_completion_cb_t cb);
/*
 * Stage one numbered Kit directory for a Preset-owned morph commit.
 *
 * Inputs: direct Kit library slot 000..999, resident Scene mask, and completion
 * callback. Output: one asynchronous directory read into filesystem-owned
 * staging with no live Scene replacement. The selected index row is retained
 * only in existing operation scratch before the separate typed stage begins;
 * this keeps the morph source immutable and does not change resident HCNAMES
 * identity. Preset reads the staged kit after
 * completion and copies only same-type morphable normal endpoints into the
 * currently loaded destination kit morph endpoints.
 */
bool filesystem_requestLoadKitMorphForScenes(uint16_t slot,
                                             uint16_t scene_mask,
                                             fs_completion_cb_t cb);
/*
 * Load one numbered root Scene directory into every selected resident Scene.
 *
 * Inputs: direct root Scene library slot 000..999, destination Scene mask, and
 * completion callback. Output: asynchronous staged Scene load; resident Scene
 * memory changes only after sceneset.scg, one embedded Kit directory, one
 * pattern file, and one effect file validate. Scene Load is explicit-OK from
 * the UI, unlike Kit Load's instant-on-scroll behavior. Its selected index row
 * is copied into existing operation scratch before separate Scene staging,
 * providing the two later directory opens and targeted HCNAMES source. The
 * name cache remains independently available throughout validation.
 */
bool filesystem_requestLoadSceneForScenes(uint16_t slot,
                                          uint16_t scene_mask,
                                          fs_completion_cb_t cb);
/*
 * Load one root Bank directory and its selected Bank-local Scene.
 *
 * Inputs: root Bank slot 000..999, resident destination Scene mask, and
 * completion callback. Output: asynchronous selected-child validation and,
 * when the Bank contains a usable child, a staged Scene load from
 * Bank/<NNN>/<SS Name>/.
 * The Bank cache remains intact through the asynchronous immediate-child name
 * repair; the child Scene stage is initialized only after that repair consumes
 * its selected row. The reader then validates only selected payloads, rather
 * than performing a recursive embedded-Kit preflight.
 * Every selected Bank-local Scene is an independent foreground-pumped
 * directory payload: its
 * embedded `Kit <name>` directory and its pattern/effect files are discovered
 * afresh before that child is read. This matters when a full Bank contains
 * different Kit names, because child `01` must never inherit child `00`'s
 * filenames. Empty Banks are successful Bank loads; callers inspect
 * filesystem_lastBankLoadLoadedScene() and run the fallback chain when no
 * child Scene was supplied. Runtime Bank Load never recursively quarantines
 * unselected embedded Kits; the shared Scene payload reader validates each
 * selected child before committing it, preserving the single callback and the
 * flexible declared Instrument/LFO payload mapping.
 */
bool filesystem_requestLoadBank(uint16_t slot,
                                uint16_t scene_mask,
                                fs_completion_cb_t cb);
/*
 * Scan one root Bank's Bank-local Scene children.
 *
 * Inputs: root Bank slot 000..999 and completion callback. Output:
 * filesystem_bankChildSceneMask() reports which two-digit 00..15 child Scene
 * folders are present after the async request completes. This is a preview
 * operation for Load:[Bank]; it does not read bankset.bcg or mutate resident
 * Scene/BankData state. Clients must verify they are still browsing the same
 * slot before applying the returned mask to LEDs.
 */
bool filesystem_requestScanBankScenes(uint16_t slot, fs_completion_cb_t cb);
uint16_t filesystem_bankChildSceneMask(void);
/*
 * Save one root Bank directory from resident Scene memory.
 *
 * Inputs: root Bank slot, source Scene, eight-cell Bank display name, future
 * Bank-local Scene save mask, and completion callback. Output: bankset.bcg
 * plus selected two-digit child Scene folders. This first implementation
 * passes mask bit 0 only, but the writer loops over the mask boundary so
 * future 16-Scene toggles do not need a new public contract.
 */
bool filesystem_requestSaveBank(uint16_t slot,
                                uint8_t source_scene,
                                const char display_name[8],
                                uint16_t bank_scene_save_mask,
                                fs_completion_cb_t cb);
bool filesystem_requestSave(fs_file_type_t type, uint16_t slot, fs_completion_cb_t cb);
bool filesystem_requestLoadName(fs_file_type_t type, uint16_t slot, fs_completion_cb_t cb);
/*
 * Scan the physical Kit/, root Scene/, or root Bank/ directory representation.
 *
 * What: clears and repopulates the single slot-ordered generalized name
 * cache from numbered directory entries. Why: boot/index maintenance still
 * needs to discover names from the card, while Load/Save browsing normally
 * enters through the corresponding `.hcindex`; a non-blank cache row is the
 * sole Kit/Scene/Bank occupancy record. No 1,000-entry presence bitmap or FAT
 * alias table is retained. Inputs: completion callback. Output: cache rows
 * only. Kit, Scene, and Bank no longer maintain parallel browser maps.
 */
bool filesystem_requestScanKits(fs_completion_cb_t cb);
bool filesystem_requestScanScenes(fs_completion_cb_t cb);
bool filesystem_requestScanBanks(fs_completion_cb_t cb);
bool filesystem_requestScanInstruments(fs_completion_cb_t cb);
/*
 * Operation-scoped authoritative identity rows.
 *
 * What: exposes logical Bank/Scene/Kit/six-Instrument identity rows shared by
 * Menu and filesystem completion code. The physical 81-byte total is BankData's
 * sole 9-byte Bank name plus this module's 72-byte Scene/Kit/Instrument block.
 * Why: resident Scene payloads deliberately contain no display names or
 * filename stems, while `.hcindex` owns its dedicated 9,000-byte name cache.
 *
 * Inputs: row 0..8 and a fixed-width eight-cell name. Outputs: Bank row zero
 * aliases BankData; other copied rows remain valid until session invalidation.
 * Affiliates: HCNAMES read/update, BankData, Kit/Instrument/Scene menus, and
 * derived filename formatting.
 */
enum {
    FS_IDENTITY_BANK_ROW = 0u,
    FS_IDENTITY_SCENE_ROW,
    FS_IDENTITY_KIT_ROW,
    FS_IDENTITY_INSTRUMENT_ROW_0,
    FS_IDENTITY_ROW_COUNT = FS_IDENTITY_INSTRUMENT_ROW_0 + 6u,
};
void filesystem_setIdentityName(uint8_t row, const char name[8]);
const char *filesystem_identityName(uint8_t row);
/*
 * Borrow one mutable identity row for the Menu character editor.
 *
 * Inputs: a valid 0..8 identity row. Output: the sole stored row, marked valid
 * before return, or NULL for an invalid selector. Why: character editing must
 * modify the authoritative operation copy directly instead of allocating a
 * second nine-byte edit buffer. Affiliates: menu_instrumentSaveName and the
 * deferred HCNAMES update at menu exit.
 */
char *filesystem_identityNameMutable(uint8_t row);
void filesystem_clearIdentityNames(void);
/*
 * Resident Instrument name-register access.
 *
 * What: the load request reads root `/.hcnames` into the existing generalized
 * name cache so Menu can copy one Scene/voice name on nested Instrument Load or
 * Save entry. The update request performs the same read, replaces only the
 * Instrument row(s) selected by scene_mask/instrument_slot from committed
 * resident state, and streams the variable-length file back before callback.
 *
 * Why the complete file is borrowed: `.hcnames` lines are trimmed, so changing
 * one name can change its byte length and cannot safely be overwritten at a
 * fixed byte offset. Unrelated rows are preserved from the file. No additional
 * SRAM array is allocated: both operations temporarily reuse the single
 * `fs_list_cache_name[1000][9]` allocation normally occupied by `.hcindex`.
 * A multi-Scene normal Instrument Load may set several scene_mask bits; a Save
 * passes one bit. All calls are asynchronous and return false when busy or when
 * coordinates are invalid.
 */
bool filesystem_requestLoadResidentInstrumentName(uint8_t scene_index,
                                                  uint8_t instrument_slot,
                                                  fs_completion_cb_t cb);
bool filesystem_requestUpdateResidentInstrumentNames(
    uint16_t scene_mask,
    uint8_t instrument_slot,
    fs_completion_cb_t cb);
/*
 * Borrow the selected eight-cell Instrument name after either request above.
 * The pointer is valid only while HCNAMES owns the shared cache; copy it before
 * requesting a typed `.hcindex`. Blank/invalid rows return `Empty   `.
 */
const char *filesystem_residentInstrumentName(uint8_t scene_index,
                                              uint8_t instrument_slot);
/*
 * Resident Kit name-register access.
 *
 * The load request mirrors Instrument menu entry: it borrows the generalized
 * cache for all 129 root HCNAMES rows so Menu can copy one resident Scene's Kit
 * name plus all six Instrument names before `/Kit/.hcindex` replaces that same
 * allocation. Menu retains those seven rows for the complete combined
 * Kit/Instrument session. Loads and saves only update the Menu scratch and an
 * accumulated dirty-Scene mask; they do not reopen HCNAMES. At session exit,
 * one update request replaces exactly the Kit row plus all six Instrument rows
 * for every bit in scene_mask from committed resident state, while preserving
 * every other logical row read from the variable-length file. A successful
 * request makes those seven rows
 * authoritative even when the source Scene's Bank-present bit is clear; this
 * prevents a valid Kit Save from serializing its new Kit name as a blank row.
 * Both requests are asynchronous, return false for busy/invalid input, and
 * allocate no additional persistent SRAM.
 */
bool filesystem_requestLoadResidentKitName(uint8_t scene_index,
                                           fs_completion_cb_t cb);
bool filesystem_requestUpdateResidentKitNames(uint16_t scene_mask,
                                              fs_completion_cb_t cb);
/*
 * Borrow the selected eight-cell Kit name after either Kit request above.
 * The pointer is valid only while HCNAMES owns the shared cache; callers must
 * copy it before requesting `/Kit/.hcindex`. Blank/invalid rows return
 * `Empty   `.
 */
const char *filesystem_residentKitName(uint8_t scene_index);
/*
 * Resident Scene name-register access.
 *
 * What: reads one requested Scene identity from root `/.hcnames`, or replaces
 * only Scene rows selected by scene_mask with one fixed eight-cell name. Why:
 * SceneData deliberately has no per-Scene display-name mirror; Menu needs one
 * name for one Scene Load/Save editor, while a multi-target Scene Load must
 * publish the one loaded directory name for every destination. Inputs: Scene
 * coordinate/mask, eight-cell display name for update, optional callback.
 * Outputs: async operations borrowing the existing 1,000-row general cache;
 * no additional cache or retained Scene-name SRAM is allocated. Affiliates:
 * Menu Scene entry and filesystem root Scene/Bank load-save state machines.
 */
bool filesystem_requestLoadResidentSceneName(uint8_t scene_index,
                                             fs_completion_cb_t cb);
bool filesystem_requestUpdateResidentSceneNames(
    uint16_t scene_mask,
    const char name[8],
    fs_completion_cb_t cb);
/* Borrow the requested Scene row while HCNAMES owns the shared cache. */
const char *filesystem_residentSceneName(uint8_t scene_index);
/*
 * Load one registered Instrument type's `.hcindex` asynchronously.
 *
 * Inputs: a registered type and optional completion callback. Output: the
 * single shared Instrument name cache is replaced from that type's own
 * directory index. Clients call this when either nested Instrument Load or
 * nested Instrument Save is entered, and whenever its type changes. The
 * request never performs blocking SD work and returns false when the
 * filesystem is already busy or the type is not present in the registry.
 */
bool filesystem_requestLoadInstrumentIndex(instrument_type_t type,
                                           fs_completion_cb_t cb);
/*
 * Reload one existing root-library `.hcindex` into the shared browser cache.
 *
 * Inputs: a numbered root-library kind and optional completion callback.
 * Output: the selected Kit, Scene, or Bank slot-ordered index replaces the
 * previous cache domain while preserving blank rows. This is a read-only cache
 * restoration operation: it does not scan a directory or rewrite `.hcindex`.
 * Pure Loads use it only after their runtime apply has completed; Saves use
 * the separate filesystem-owned scan/rebuild chain after mutating a namespace.
 */
bool filesystem_requestReloadLibraryIndex(fs_library_index_kind_t kind,
                                          fs_completion_cb_t cb);
/*
 * Compatibility wrappers for callers that already name one root domain.
 * They delegate to filesystem_requestReloadLibraryIndex() and retain the same
 * read-only, slot-preserving contract.
 */
bool filesystem_requestLoadKitIndex(fs_completion_cb_t cb);
bool filesystem_requestLoadSceneIndex(fs_completion_cb_t cb);
bool filesystem_requestLoadBankIndex(fs_completion_cb_t cb);
/* True when the requested root library currently owns the shared cache. */
bool filesystem_libraryNameCacheLoaded(fs_library_index_kind_t kind);
/* Dispose the single shared Instrument/Kit/Scene/Bank browser cache or its
 * temporary 129-row HCNAMES view; no second name allocation exists. */
void filesystem_clearNameCache(void);
/* Compatibility spelling retained for existing Instrument menu callers. */
void filesystem_clearInstrumentCache(void);
/*
 * Retired File/Dir diagnostic compatibility API.
 *
 * Inputs are accepted only to keep stale developer-only callers buildable.
 * Outputs are empty/false and no filesystem transaction or SRAM cache is
 * created. Affiliates: matching presetManager compatibility stubs; musical
 * Load/Save code must use its Kit/Scene/Bank/Instrument requests instead.
 */
bool filesystem_requestScanTestFiles(fs_completion_cb_t cb);
bool filesystem_requestScanTestDirs(fs_completion_cb_t cb);
uint8_t filesystem_testFileCount(void);
uint8_t filesystem_testDirCount(void);
const char *filesystem_testFileName(uint8_t index);
const char *filesystem_testDirName(uint8_t index);
bool filesystem_requestLoadTestFile(const char *display_name, fs_completion_cb_t cb);
bool filesystem_requestLoadTestDir(const char *display_name, fs_completion_cb_t cb);
bool filesystem_requestSaveTestFile(const char *display_name, fs_completion_cb_t cb);
bool filesystem_requestSaveTestDir(const char *display_name, fs_completion_cb_t cb);
bool filesystem_requestSaveTestSimpleDir(const char *display_name, fs_completion_cb_t cb);
fs_test_result_kind_t filesystem_testResultKind(void);
const uint8_t *filesystem_testResultBytes(void);
const char *filesystem_testResultName(void);
/*
 * Load one root Instrument/ file into an explicit Scene slot.
 *
 * Inputs: resident Scene index, zero-based kit slot, registry type, shared-cache
 * index (0..999), and completion callback. Output: one asynchronous parse into
 * filesystem-owned staging; live SceneData and DSP state are unchanged until
 * Preset commits the validated payload. Client: preset_loadInstrument(). The
 * explicit Scene/slot coordinates remain immutable completion context even
 * though parsing itself is off-scene. Before staging aliases the typed cache,
 * the selected filename is copied into existing operation scratch; that one
 * request-stable key supplies the later open and HCNAMES update without an
 * additional cache or SRAM allocation.
 */
bool filesystem_requestLoadInstrument(uint8_t destination_scene,
                                      uint8_t destination_slot,
                                      instrument_type_t type,
                                      uint16_t browser_index,
                                      fs_completion_cb_t cb);
/*
 * Save one resident kit voice as a root Instrument/ file.
 *
 * Inputs: resident source Scene index, zero-based kit voice slot, display stem
 * from the Save UI, and completion callback. Output: asynchronous
 * Instrument/<stem.ext> write using the source slot's current type and
 * storageTypes instrument serializer. The source slot is a six-voice kit
 * coordinate; it is unrelated to the 000..999 library-slot numbering used by
 * Kit and Scene folders. Client: preset_saveInstrument().
 */
bool filesystem_requestSaveInstrument(uint8_t source_scene,
                                      uint8_t source_slot,
                                      const char *display_name,
                                      fs_completion_cb_t cb);
/*
 * Save one resident kit voice as a root Instrument Morph file.
 *
 * Inputs: resident source Scene/voice, edited display stem, and completion
 * callback. Output: asynchronous Instrument/<stem.ext> write using the source
 * slot's current type and Morph Save endpoint projection. The target filename
 * and overwrite rules match normal Instrument Save; retained Instrument source
 * name is not updated on completion.
 *
 * Affiliates/clients: preset_saveInstrumentMorph(), nested Save:[TypeMrp],
 * filesystem_saveInstrument_tick().
 */
bool filesystem_requestSaveInstrumentMorph(uint8_t source_scene,
                                           uint8_t source_slot,
                                           const char *display_name,
                                           fs_completion_cb_t cb);
struct kit_instrument_slot;
/*
 * Read the most recently validated staged Kit directory payload.
 *
 * Inputs: none; valid use begins after an FS_STATUS_DONE KitMrp callback.
 * Output: a filesystem-owned immutable kit image that remains valid until the
 * next filesystem operation starts. Client: Preset's morph-load commit copies
 * selected endpoint values from this staging image without letting filesystem
 * choose Scene or DSP lifecycle policy.
 */
const kit_t *filesystem_loadedKit(void);
/*
 * Read the most recently validated staged Instrument payload.
 *
 * Inputs: none; valid use begins after an FS_STATUS_DONE Instrument callback.
 * Outputs: a filesystem-owned immutable slot image that remains valid until
 * the next filesystem operation starts. The HCNAMES identity is published
 * separately through the one operation identity block; this accessor cannot
 * commit SceneData because storage must not choose DSP lifecycle order.
 */
const struct kit_instrument_slot *filesystem_loadedInstrumentSlot(void);
/*
 * Save/load the reversible normal Instrument Load `kit` source.
 *
 * Inputs: one Scene/voice and its typed family. Output: the normal Instrument
 * serializer/parser writes or reads the hidden `.hctmp.<ext>` inside that
 * family directory. The temporary operation never updates HCNAMES or the
 * typed `.hcindex`; Menu owns the one nine-byte original label and invalidates
 * it on voice/type/mode/exit boundaries. Affiliates: presetManager wrappers,
 * Menu's direct `kit` row, and filesystem_recordInstrumentFile() exclusion.
 */
bool filesystem_requestSaveInstrumentTemp(uint8_t source_scene,
                                          uint8_t source_slot,
                                          fs_completion_cb_t cb);
bool filesystem_requestLoadInstrumentTemp(uint8_t destination_scene,
                                          uint8_t destination_slot,
                                          instrument_type_t type,
                                          fs_completion_cb_t cb);
uint8_t filesystem_installSamplesBlocking(void);
uint8_t filesystem_installLoopsBlocking(void);

/* Return the most recent eight-character name produced by a load-name request.
 *
 * Input: none. Output: pointer to filesystem-owned storage that remains valid
 * until the next filesystem_requestLoadName() or filesystem_start() operation
 * reuses it. Client: presetManager.c legacy display-name browsing.
 */
const char *filesystem_loadedName(void);

/* Query the shared-cache-backed Kit occupancy map for a numbered kit folder.
 *
 * Input: slot is the direct Kit library index used by preset/menu code; SD
 * folder names are 000 Name through 999 Name, with underscore accepted as a
 * compatibility separator. Slot 000 is real. Output: nonzero when the active
 * shared Kit cache row is non-blank, whether it came from a scan or the
 * slot-ordered `.hcindex`. The row itself is the only occupancy record, so it
 * cannot disagree with filesystem_kitSlotName(). Clients: menu.c and any
 * future load/save UI that must show explicit Empty slots without trying to
 * open a missing directory.
 */
uint8_t     filesystem_kitSlotExists(uint16_t zero_based_slot);

/* Return the eight-character name from the shared slot-ordered Kit cache.
 *
 * Input: zero_based_slot as above. Output: filesystem-owned eight printable
 * characters plus NUL for existing kits, or the literal "Empty   " for missing
 * slots. The name is loaded from `/Kit/.hcindex`; the cache row excludes the
 * `NNN ` folder prefix because the slot number is already the array index.
 * Client: menu_repaintLoadSavePage().
 */
const char *filesystem_kitSlotName(uint16_t zero_based_slot);
/*
 * Query and read root Scene names from the same shared slot-ordered cache.
 * Scene is deliberately the standalone `/Scene/` library; Bank-local child
 * Scenes never enter this API or `/Scene/.hcindex`. A non-blank row is both
 * the display name and occupancy bit; no per-slot alias or presence storage
 * exists. A load may use one transient alias internally while reopening a
 * selected directory, but that alias is not exposed or retained per slot.
 */
uint8_t     filesystem_sceneSlotExists(uint16_t zero_based_slot);
const char *filesystem_sceneSlotName(uint16_t zero_based_slot);
/*
 * Root Bank/ shared-cache queries and fallback helpers.
 *
 * Bank slots use root three-digit library numbering 000..999. The display name
 * and occupancy bit come from the active `FS_LIBRARY_INDEX_BANK` rows; no
 * per-slot presence or FAT-alias array is retained. Bank-local Scene slots are
 * not exposed here: they are two-digit child folders inside one selected Bank
 * and remain operation-local state in the Bank load/save state machines.
 */
uint8_t     filesystem_bankSlotExists(uint16_t zero_based_slot);
const char *filesystem_bankSlotName(uint16_t zero_based_slot);
uint16_t    filesystem_firstKitSlot(void);
uint16_t    filesystem_firstSceneSlot(void);
uint16_t    filesystem_firstBankSlot(void);
/*
 * Report whether the completed Bank Load committed at least one child Scene.
 *
 * The result becomes true only after the shared Scene reader validates and
 * commits a selected child, and remains available for the immediate Preset/
 * Menu completion decision. Pure Bank Load does not start an index rebuild
 * before that decision; Menu consumes this result before its later read-only
 * post-DSP Bank-index reload.
 */
uint8_t     filesystem_lastBankLoadLoadedScene(void);
/*
 * Query whether a root Instrument save target already exists.
 *
 * What: Builds the same visible `stem.ext` component that root Instrument Save
 * will write, then checks the current Instrument/ scan cache for a
 * case-insensitive match of the same instrument type.
 *
 * Why: Menu must render persistent `OW` before the user confirms Save.
 * Numbered slots can answer from occupancy caches, but root Instrument Save is
 * filename-based and needs the extension/type rule owned by filesystem.
 *
 * Inputs: resident instrument type and the eight-character Save editor stem.
 * Outputs: nonzero when confirming would overwrite at least one on-card
 * same-casefold Instrument file.
 *
 * Affiliates/clients: menu_currentSaveWouldOverwrite(), root Instrument Save,
 * Instrument browser duplicate suppression.
 */
uint8_t     filesystem_instrumentTargetExists(instrument_type_t type,
                                              const char *display_stem);
/*
 * Query the active typed Instrument view of the one generalized 1,000-entry
 * name cache. The return value is 0..1000; it is not a per-type SRAM capacity.
 * Instrument type changes dispose/reload this same cache, so callers must not
 * retain names or create a second type-specific array.
 */
uint16_t    filesystem_instrumentCount(instrument_type_t type);
/*
 * Return the raw Instrument filename at a zero-based shared-cache row 0..999.
 *
 * Input: type/index in the active typed `.hcindex` cache. Output: an exact
 * filesystem filename used only to derive an open key; callers must not put it
 * directly in an eight-cell LCD/name field because a short stem would expose
 * `.ext`.  Use filesystem_copyInstrumentDisplayName() for UI/HCNAMES text.
 * Affiliates: filesystem_requestLoadInstrument() and Menu pool rendering.
 */
const char *filesystem_instrumentName(instrument_type_t type,
                                      uint16_t browser_index);
/*
 * Copy the UI/HCNAMES-safe eight-cell stem for one cached Instrument row.
 *
 * Inputs: active type/index and caller-owned nine-byte destination. Output:
 * the filename stem padded and NUL-terminated, never its extension. This
 * derives presentation text from the one `.hcindex` cache row and stores no
 * additional filename/key. Affiliates: nested Instrument Load display and
 * preview finalization before the existing HCNAMES exit-time write.
 */
void filesystem_copyInstrumentDisplayName(char destination[9],
                                          instrument_type_t type,
                                          uint16_t browser_index);
/* Return the one-based LCD position for a shared-cache row; LCD output caps at 999. */
uint16_t    filesystem_instrumentDisplayIndex(instrument_type_t type,
                                               uint16_t browser_index);
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
