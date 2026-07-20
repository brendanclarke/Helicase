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

uint8_t     filesystem_initCardAndMountBlocking(void);
void        filesystem_initAfterCardReady(void);
/*
 * Create/refresh one `.hcindex` file in every registry-defined Instrument
 * directory. This boot-only wrapper is valid before audio starts; normal
 * runtime SD work remains asynchronous through filesystem_tick(). It returns
 * nonzero only after every index file and the final FAT/data flush complete.
 */
uint8_t     filesystem_createBootIndexBlocking(void);
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
 * staging with no live Scene replacement. Preset reads the staged kit after
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
 * the UI, unlike Kit Load's instant-on-scroll behavior.
 */
bool filesystem_requestLoadSceneForScenes(uint16_t slot,
                                          uint16_t scene_mask,
                                          fs_completion_cb_t cb);
/*
 * Load one root Bank directory and its selected Bank-local Scene.
 *
 * Inputs: root Bank slot 000..999, resident destination Scene mask, and
 * completion callback. Output: asynchronous Bank validation and, when the Bank
 * contains a usable child, a staged Scene load from Bank/<NNN>/<SS Name>/.
 * Every selected Bank-local Scene is an independent directory payload: its
 * embedded `Kit <name>` directory and its pattern/effect files are discovered
 * afresh before that child is read. This matters when a full Bank contains
 * different Kit names, because child `01` must never inherit child `00`'s
 * filenames. Empty Banks are successful Bank loads; callers inspect
 * filesystem_lastBankLoadLoadedScene() and run the fallback chain when no
 * child Scene was supplied.
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
 * and, for Kit, the legacy kb_map compatibility view.
 */
bool filesystem_requestScanKits(fs_completion_cb_t cb);
bool filesystem_requestScanScenes(fs_completion_cb_t cb);
bool filesystem_requestScanBanks(fs_completion_cb_t cb);
bool filesystem_requestScanInstruments(fs_completion_cb_t cb);
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
 * Load root Kit, root Scene, or root Bank slot-ordered names into the shared cache.
 * Each asynchronous request disposes the previous domain first, preserves
 * blank rows, and completes only when the selected `.hcindex` is available.
 */
bool filesystem_requestLoadKitIndex(fs_completion_cb_t cb);
bool filesystem_requestLoadSceneIndex(fs_completion_cb_t cb);
/* Load `/Bank/.hcindex` into the one shared slot-ordered name cache; this
 * replaces any Kit or Scene rows because there is only one SRAM cache. */
bool filesystem_requestLoadBankIndex(fs_completion_cb_t cb);
/* True when the requested root library currently owns the shared cache. */
bool filesystem_libraryNameCacheLoaded(fs_library_index_kind_t kind);
/* Dispose the single shared Instrument/Kit/Scene/Bank browser name cache. */
void filesystem_clearNameCache(void);
/* Compatibility spelling retained for existing Instrument menu callers. */
void filesystem_clearInstrumentCache(void);
/*
 * Generic asyncfatfs File/Dir test browser and payload API.
 *
 * These calls are the only load/save surface expected to work during the
 * asyncfatfs expansion. Inputs are exact single-component display names with
 * preserved case, never slash-separated paths. File operations and Dir scan/load
 * still use the root; Save:[Dir] creates its directory under /Kit/. Outputs are
 * filesystem-owned scan caches and a four-byte result record used by Menu's
 * two-second test display. Affiliates: asyncfatfs LFN object iterator/open/create
 * APIs and presetManager's PRESET_OP_TEST_* completion wrappers.
 */
bool filesystem_requestScanTestFiles(fs_completion_cb_t cb);
bool filesystem_requestScanTestDirs(fs_completion_cb_t cb);
/*
 * Generic asyncfatfs File/Dir diagnostic browser accessors.
 *
 * These temporary test menus list concrete root objects exactly as asyncfatfs
 * reports them after structural FAT filtering: VFAT fragments, deleted
 * entries, volume labels, and structural dot entries are hidden, but ordinary
 * names beginning with '.' remain selectable because they are valid files or
 * directories. Inputs: root scan requests and selected display names. Outputs:
 * case-preserved display names, four read/write test bytes, or a child
 * directory display name.
 */
uint8_t filesystem_testFileCount(void);
uint8_t filesystem_testDirCount(void);
const char *filesystem_testFileName(uint8_t index);
const char *filesystem_testDirName(uint8_t index);
bool filesystem_requestLoadTestFile(const char *display_name,
                                    fs_completion_cb_t cb);
bool filesystem_requestLoadTestDir(const char *display_name,
                                   fs_completion_cb_t cb);
bool filesystem_requestSaveTestFile(const char *display_name,
                                    fs_completion_cb_t cb);
bool filesystem_requestSaveTestDir(const char *display_name,
                                   fs_completion_cb_t cb);
bool filesystem_requestSaveTestSimpleDir(const char *display_name,
                                         fs_completion_cb_t cb);
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
 * though parsing itself is off-scene.
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
 * Outputs: a filesystem-owned immutable slot image and eight-character display
 * name that remain valid until the next filesystem operation starts. Clients:
 * Preset's Instrument transaction copies them only after clearing outgoing DSP
 * owners. These accessors cannot commit SceneData themselves because storage
 * must not choose audio lifecycle order or reset runtime instances.
 */
const struct kit_instrument_slot *filesystem_loadedInstrumentSlot(void);
const char *filesystem_loadedInstrumentDisplayName(void);
/*
 * Read the staged Instrument source stem captured during root Instrument load.
 *
 * Output is the first retained filename-stem characters from the selected
 * Instrument/ entry. Preset copies it into SceneData only after the staged
 * Instrument commit succeeds, keeping save metadata paired with the payload.
 */
const char *filesystem_loadedInstrumentStem(void);
uint8_t filesystem_installSamplesBlocking(void);
uint8_t filesystem_installLoopsBlocking(void);

/* Return the most recent eight-character name produced by a load-name request.
 *
 * Input: none. Output: pointer to filesystem-owned storage that remains valid
 * until the next filesystem_requestLoadName() or filesystem_start() operation
 * reuses it. Clients: presetManager.c and kitBrowser.c display name browsing.
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
/* Return the eight-character name at a zero-based shared-cache row 0..999. */
const char *filesystem_instrumentName(instrument_type_t type,
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
